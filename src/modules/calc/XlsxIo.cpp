// ─────────────────────────────────────────────────────────────────────────────
// XlsxIo.cpp — see XlsxIo.h
// Self-contained .xlsx reader/writer. The DEFLATE inflater + ZIP reader mirror
// the proven implementation used by the Impress .pptx importer; export uses a
// STORED (uncompressed) ZIP so no compressor is needed (Excel reads it fine).
// ─────────────────────────────────────────────────────────────────────────────
#include "XlsxIo.h"
#include "XlsxDrawing.h"
#include "XlsxDrawingWriter.h"

#include "core/watermark/Watermark.h"
#include "core/watermark/WatermarkOoxml.h"

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QList>
#include <QMap>
#include <QSet>
#include <QVector>
#include <QStringList>
#include <QXmlStreamReader>
#include <QtGlobal>
#include <algorithm>

namespace NativeOffice {
namespace {

// Upper bound for a <col> span. Files routinely write max=16384 for the
// trailing default run, and expanding that into per-column entries would
// mean 16k pointless overrides on every sheet.
constexpr int SpreadsheetModelColLimit = 1024;

// ─────────────────────────────────────────────────────────────────────────────
// Raw DEFLATE inflater (RFC 1951) — puff-style, self-contained.
// ─────────────────────────────────────────────────────────────────────────────
class Inflater {
public:
    Inflater(const uchar* data, int len) : m_d(data), m_n(len) {}
    QByteArray run() {
        int last;
        do {
            last = getBits(1);
            const int type = getBits(2);
            if (m_err) return {};
            if (type == 0)      storedBlock();
            else if (type == 1) fixedBlock();
            else if (type == 2) dynamicBlock();
            else { m_err = true; return {}; }
            if (m_err) return {};
        } while (!last);
        return m_out;
    }
private:
    static constexpr int kMaxBits = 15;
    struct Huffman { short count[kMaxBits + 1] = {0}; QVector<short> symbol; };
    const uchar* m_d; int m_n; int m_pos = 0; int m_bitbuf = 0; int m_bitcnt = 0;
    QByteArray m_out; bool m_err = false;

    int getBits(int need) {
        long val = m_bitbuf;
        while (m_bitcnt < need) {
            if (m_pos >= m_n) { m_err = true; return 0; }
            val |= static_cast<long>(m_d[m_pos++]) << m_bitcnt;
            m_bitcnt += 8;
        }
        m_bitbuf = static_cast<int>(val >> need);
        m_bitcnt -= need;
        return static_cast<int>(val & ((1L << need) - 1));
    }
    void storedBlock() {
        m_bitbuf = 0; m_bitcnt = 0;
        if (m_pos + 4 > m_n) { m_err = true; return; }
        const int len = m_d[m_pos] | (m_d[m_pos + 1] << 8);
        m_pos += 4;
        if (m_pos + len > m_n) { m_err = true; return; }
        m_out.append(reinterpret_cast<const char*>(m_d + m_pos), len);
        m_pos += len;
    }
    static void construct(Huffman& h, const short* length, int n) {
        for (int i = 0; i <= kMaxBits; ++i) h.count[i] = 0;
        for (int s = 0; s < n; ++s) h.count[length[s]]++;
        short offs[kMaxBits + 1];
        offs[1] = 0;
        for (int len = 1; len < kMaxBits; ++len)
            offs[len + 1] = offs[len] + h.count[len];
        h.symbol.resize(n);
        for (int s = 0; s < n; ++s)
            if (length[s] != 0) h.symbol[offs[length[s]]++] = static_cast<short>(s);
    }
    int decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            code |= getBits(1);
            if (m_err) return -1;
            const int count = h.count[len];
            if (code - count < first) return h.symbol[index + (code - first)];
            index += count; first += count; first <<= 1; code <<= 1;
        }
        m_err = true; return -1;
    }
    bool codes(const Huffman& litlen, const Huffman& dist) {
        static const short lens[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
                                       35,43,51,59,67,83,99,115,131,163,195,227,258};
        static const short lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,
                                       4,4,4,4,5,5,5,5,0};
        static const short dists[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
                                        257,385,513,769,1025,1537,2049,3073,4097,6145,
                                        8193,12289,16385,24577};
        static const short dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,
                                       10,10,11,11,12,12,13,13};
        for (;;) {
            int sym = decode(litlen);
            if (m_err || sym < 0) return false;
            if (sym == 256) return true;
            if (sym < 256) { m_out.append(static_cast<char>(sym)); }
            else {
                sym -= 257;
                if (sym >= 29) { m_err = true; return false; }
                const int len = lens[sym] + getBits(lext[sym]);
                sym = decode(dist);
                if (m_err || sym < 0 || sym >= 30) { m_err = true; return false; }
                const int dst = dists[sym] + getBits(dext[sym]);
                if (dst > m_out.size()) { m_err = true; return false; }
                int from = m_out.size() - dst;
                for (int i = 0; i < len; ++i) m_out.append(m_out.at(from++));
            }
            if (m_err) return false;
        }
    }
    void fixedBlock() {
        short ll[288];
        for (int i = 0;   i < 144; ++i) ll[i] = 8;
        for (int i = 144; i < 256; ++i) ll[i] = 9;
        for (int i = 256; i < 280; ++i) ll[i] = 7;
        for (int i = 280; i < 288; ++i) ll[i] = 8;
        short dd[30]; for (int i = 0; i < 30; ++i) dd[i] = 5;
        Huffman litlen, dist;
        construct(litlen, ll, 288); construct(dist, dd, 30);
        codes(litlen, dist);
    }
    void dynamicBlock() {
        static const short order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
        const int hlit  = getBits(5) + 257;
        const int hdist = getBits(5) + 1;
        const int hclen = getBits(4) + 4;
        if (m_err || hlit > 286 || hdist > 30) { m_err = true; return; }
        short clLen[19] = {0};
        for (int i = 0; i < hclen; ++i) clLen[order[i]] = static_cast<short>(getBits(3));
        Huffman clCode; construct(clCode, clLen, 19);
        short lengths[286 + 30] = {0};
        int idx = 0;
        while (idx < hlit + hdist) {
            int sym = decode(clCode);
            if (m_err) return;
            if (sym < 16) lengths[idx++] = static_cast<short>(sym);
            else if (sym == 16) {
                if (idx == 0) { m_err = true; return; }
                const short prev = lengths[idx - 1];
                int rep = 3 + getBits(2);
                while (rep-- && idx < hlit + hdist) lengths[idx++] = prev;
            } else if (sym == 17) {
                int rep = 3 + getBits(3);
                while (rep-- && idx < hlit + hdist) lengths[idx++] = 0;
            } else {
                int rep = 11 + getBits(7);
                while (rep-- && idx < hlit + hdist) lengths[idx++] = 0;
            }
            if (m_err) return;
        }
        Huffman litlen, dist;
        construct(litlen, lengths, hlit);
        construct(dist, lengths + hlit, hdist);
        codes(litlen, dist);
    }
};

QByteArray inflateRaw(const QByteArray& src) {
    if (src.isEmpty()) return {};
    Inflater inf(reinterpret_cast<const uchar*>(src.constData()), src.size());
    return inf.run();
}

// ─────────────────────────────────────────────────────────────────────────────
// ZIP reader (store + deflate)
// ─────────────────────────────────────────────────────────────────────────────
quint16 rd16(const uchar* p) { return p[0] | (p[1] << 8); }
quint32 rd32(const uchar* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (quint32(p[3]) << 24); }

class ZipReader {
public:
    bool open(const QByteArray& bytes) {
        m_b = bytes;
        const uchar* d = reinterpret_cast<const uchar*>(m_b.constData());
        const int n = m_b.size();
        if (n < 22) return false;
        int eocd = -1;
        for (int i = n - 22; i >= 0 && i >= n - 22 - 65536; --i)
            if (rd32(d + i) == 0x06054b50u) { eocd = i; break; }
        if (eocd < 0) return false;
        const quint16 count   = rd16(d + eocd + 10);
        const quint32 cdSize  = rd32(d + eocd + 12);
        const quint32 cdStart = rd32(d + eocd + 16);
        if (cdStart + cdSize > static_cast<quint32>(n)) return false;
        quint32 p = cdStart;
        for (int i = 0; i < count; ++i) {
            if (p + 46 > static_cast<quint32>(n) || rd32(d + p) != 0x02014b50u) break;
            const quint16 method = rd16(d + p + 10);
            const quint32 compSz = rd32(d + p + 20);
            const quint16 fnLen  = rd16(d + p + 28);
            const quint16 exLen  = rd16(d + p + 30);
            const quint16 cmLen  = rd16(d + p + 32);
            const quint32 lhOff  = rd32(d + p + 42);
            QString name = QString::fromUtf8(
                reinterpret_cast<const char*>(d + p + 46), fnLen);
            name.replace('\\', '/');
            if (lhOff + 30 <= static_cast<quint32>(n) && rd32(d + lhOff) == 0x04034b50u) {
                const quint16 lfn = rd16(d + lhOff + 26);
                const quint16 lex = rd16(d + lhOff + 28);
                const quint32 dataAt = lhOff + 30 + lfn + lex;
                if (dataAt + compSz <= static_cast<quint32>(n)) {
                    const QByteArray raw(reinterpret_cast<const char*>(d + dataAt),
                                         static_cast<int>(compSz));
                    m_files.insert(name, (method == 0) ? raw : inflateRaw(raw));
                    m_lower.insert(name.toLower(), name);
                    m_order << name;
                }
            }
            p += 46 + fnLen + exLen + cmLen;
        }
        return !m_files.isEmpty();
    }
    // Part names are matched case-insensitively. ECMA-376 compares them that
    // way, and files in the wild mix cases between a part and its _rels entry;
    // an exact match quietly loses every object the sheet points at.
    // Part names in the order they appear in the archive, so a rewritten
    // package keeps the original's layout.
    QStringList names() const { return m_order; }

    bool has(const QString& name) const {
        return m_files.contains(name) || m_lower.contains(name.toLower());
    }
    QByteArray file(const QString& name) const {
        const auto it = m_files.constFind(name);
        if (it != m_files.constEnd()) return *it;
        const auto lit = m_lower.constFind(name.toLower());
        return lit == m_lower.constEnd() ? QByteArray() : m_files.value(*lit);
    }
private:
    QByteArray m_b;
    QHash<QString, QByteArray> m_files;
    // lowercased name -> the name as actually stored in the archive
    QHash<QString, QString> m_lower;
    QStringList m_order;
};

// ─────────────────────────────────────────────────────────────────────────────
// STORED ZIP writer (CRC32, no compression)
// ─────────────────────────────────────────────────────────────────────────────
quint32 crc32(const QByteArray& data) {
    static quint32 table[256];
    static bool init = false;
    if (!init) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    quint32 c = 0xFFFFFFFFu;
    const uchar* d = reinterpret_cast<const uchar*>(data.constData());
    for (int i = 0; i < data.size(); ++i) c = table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// Raw DEFLATE (ZIP method 8) from Qt's zlib wrapper.
//
// qCompress returns: 4 bytes of Qt's own uncompressed-length prefix, then a
// zlib stream (2-byte header, deflate payload, 4-byte adler32). A ZIP entry
// wants the deflate payload on its own, so both ends are trimmed. Returns an
// empty array when the input will not compress usefully.
inline QByteArray deflateRaw(const QByteArray& src) {
    if (src.isEmpty()) return {};
    const QByteArray z = qCompress(src, 9);
    if (z.size() <= 4 + 2 + 4) return {};
    const QByteArray raw = z.mid(4 + 2, z.size() - (4 + 2) - 4);
    return raw.size() < src.size() ? raw : QByteArray();
}

class ZipWriter {
public:
    void add(const QString& name, const QByteArray& data) {
        const quint32 crc = crc32(data);
        const quint32 off = static_cast<quint32>(m_out.size());
        const QByteArray nm = name.toUtf8();

        // Store the part only when compressing it would not help; the media in
        // a workbook is already-compressed PNG/JPEG and gains nothing.
        const QByteArray packed = deflateRaw(data);
        const bool deflated = !packed.isEmpty();
        const QByteArray& body = deflated ? packed : data;
        const quint16 method = deflated ? 8 : 0;

        putU32(0x04034b50); putU16(20); putU16(0); putU16(method);
        putU16(0); putU16(0);                          // mod time/date
        putU32(crc); putU32(body.size()); putU32(data.size());
        putU16(nm.size()); putU16(0);
        m_out.append(nm); m_out.append(body);
        m_dir.push_back({name, crc, static_cast<quint32>(body.size()),
                         static_cast<quint32>(data.size()), off, method});
    }
    QByteArray finish() {
        const quint32 cdStart = static_cast<quint32>(m_out.size());
        for (const Entry& e : m_dir) {
            const QByteArray nm = e.name.toUtf8();
            putU32(0x02014b50); putU16(20); putU16(20); putU16(0); putU16(e.method);
            putU16(0); putU16(0);
            putU32(e.crc); putU32(e.packed); putU32(e.size);
            putU16(nm.size()); putU16(0); putU16(0); putU16(0); putU16(0);
            putU32(0); putU32(e.offset);
            m_out.append(nm);
        }
        const quint32 cdSize = static_cast<quint32>(m_out.size()) - cdStart;
        putU32(0x06054b50); putU16(0); putU16(0);
        putU16(m_dir.size()); putU16(m_dir.size());
        putU32(cdSize); putU32(cdStart); putU16(0);
        return m_out;
    }
private:
    struct Entry { QString name; quint32 crc; quint32 packed; quint32 size;
                   quint32 offset; quint16 method; };
    QByteArray m_out;
    QVector<Entry> m_dir;
    void putU16(quint32 v) { m_out.append(char(v & 0xFF)); m_out.append(char((v >> 8) & 0xFF)); }
    void putU32(quint32 v) { putU16(v & 0xFFFF); putU16((v >> 16) & 0xFFFF); }
};

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
bool parseRef(const QString& ref, int& col, int& row) {
    int i = 0, c = 0;
    while (i < ref.size() && ref[i].isLetter()) {
        c = c * 26 + (ref[i].toUpper().toLatin1() - 'A' + 1); ++i;
    }
    if (i == 0) return false;
    bool ok = false;
    const int r = ref.mid(i).toInt(&ok);
    if (!ok || r < 1) return false;
    col = c - 1; row = r - 1;
    return true;
}

QString colLabel(int col) {
    QString s; col += 1;
    while (col > 0) { int rem = (col - 1) % 26; s.prepend(QChar('A' + rem)); col = (col - 1) / 26; }
    return s;
}

// ── OPC part paths ───────────────────────────────────────────────────────────
// Relationship targets are written relative to the part that owns them, so
// "xl/worksheets/sheet1.xml" pointing at "../drawings/drawing1.xml" has to be
// resolved back to "xl/drawings/drawing1.xml" before the ZIP can be asked for it.
QString partDir(const QString& part) {
    const int slash = part.lastIndexOf('/');
    return slash < 0 ? QString() : part.left(slash);
}

QString relsPathFor(const QString& part) {
    const int slash = part.lastIndexOf('/');
    const QString dir  = slash < 0 ? QString()  : part.left(slash);
    const QString file = slash < 0 ? part       : part.mid(slash + 1);
    return (dir.isEmpty() ? QString() : dir + '/') + "_rels/" + file + ".rels";
}

QString resolveTarget(QString target, const QString& baseDir) {
    if (target.isEmpty()) return target;
    if (target.startsWith('/')) return target.mid(1);        // package-absolute

    QStringList base = baseDir.isEmpty() ? QStringList()
                                         : baseDir.split('/', Qt::SkipEmptyParts);
    for (const QString& seg : target.split('/', Qt::SkipEmptyParts)) {
        if (seg == ".") continue;
        if (seg == "..") { if (!base.isEmpty()) base.removeLast(); continue; }
        base << seg;
    }
    return base.join('/');
}

// rId -> resolved part path, for every relationship in a .rels part.
QHash<QString, QString> parseRels(const QByteArray& xml, const QString& baseDir) {
    QHash<QString, QString> out;
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        if (r.readNext() == QXmlStreamReader::StartElement && r.name() == u"Relationship") {
            const QString id = r.attributes().value("Id").toString();
            const QString tg = r.attributes().value("Target").toString();
            // External targets (a linked, not embedded, image) cannot be read
            // out of this package, so they are dropped rather than resolved.
            if (r.attributes().value("TargetMode") == u"External") continue;
            if (!id.isEmpty() && !tg.isEmpty()) out.insert(id, resolveTarget(tg, baseDir));
        }
    }
    return out;
}

// The first relationship whose Type ends with `suffix`, resolved to a part path.
QString firstRelOfType(const QByteArray& xml, const char* suffix, const QString& baseDir) {
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        if (r.readNext() == QXmlStreamReader::StartElement && r.name() == u"Relationship") {
            if (r.attributes().value("Type").toString().endsWith(QLatin1String(suffix)))
                return resolveTarget(r.attributes().value("Target").toString(), baseDir);
        }
    }
    return QString();
}

// xlsx column width is in default-font character units; row height is in points.
// Approximate conversions for Calibri 11 (max digit width ≈ 7px) at 96 DPI.
int    colWidthToPx(double w)  { return int(qRound(w * 7.0) + 5); }
double pxToColWidth(int px)    { return (px - 5) / 7.0; }
int    rowHtToPx(double pt)    { return int(qRound(pt * 4.0 / 3.0)); }
double pxToRowHt(int px)       { return px * 3.0 / 4.0; }

QString xmlEscape(QString s) {
    s.replace('&', "&amp;"); s.replace('<', "&lt;");
    s.replace('>', "&gt;");  s.replace('"', "&quot;");
    return s;
}

QStringList parseSharedStrings(const QByteArray& xml) {
    QStringList out;
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        if (r.readNext() == QXmlStreamReader::StartElement && r.name() == u"si")
            out << r.readElementText(QXmlStreamReader::IncludeChildElements);
    }
    return out;
}

// ── styles.xml import ─────────────────────────────────────────────────────────
QColor rgbColor(const QString& rgb) {
    if (rgb.isEmpty()) return {};
    return QColor('#' + rgb);     // "AARRGGBB" or "RRGGBB" both accepted
}

QString attrS(QXmlStreamReader& r, const char* n) {
    return r.attributes().value(n).toString();
}

// A differential format: the styling a conditional-format rule applies when it
// matches. Only the parts this app can render are kept.
struct ImpDxf { QColor bg, fg; bool bold = false; };

struct ImpFont   { bool bold=false, italic=false, underline=false; int size=0; QString name; QColor color; };
struct ImpFill   { QColor fg; };
struct ImpBorder { int edges=0; QColor color; };
struct ImpXf     { int numFmtId=0, fontId=0, fillId=0, borderId=0, hAlign=0, vAlign=0; bool wrap=false; };

struct StyleSheetData {
    QHash<int, QString> numFmts;
    QVector<ImpFont>   fonts;
    QVector<ImpFill>   fills;
    QVector<ImpBorder> borders;
    QVector<ImpXf>     xfs;

    static QString builtinNumFmt(int id) {
        switch (id) {
            case 1:  return "0";        case 2:  return "0.00";
            case 3:  return "#,##0";    case 4:  return "#,##0.00";
            case 9:  return "0%";       case 10: return "0.00%";
            default: return {};
        }
    }

    CellFormat formatFor(int s) const {
        CellFormat f;
        if (s < 0 || s >= xfs.size()) return f;
        const ImpXf& x = xfs[s];
        if (x.fontId > 0 && x.fontId < fonts.size()) {
            const ImpFont& fo = fonts[x.fontId];
            f.bold = fo.bold; f.italic = fo.italic; f.underline = fo.underline;
            if (fo.size > 0 && fo.size != 11) f.fontSize = fo.size;
            if (!fo.name.isEmpty() && fo.name != "Calibri") f.fontFamily = fo.name;
            if (fo.color.isValid()) f.textColor = fo.color;
        }
        if (x.fillId > 0 && x.fillId < fills.size() && fills[x.fillId].fg.isValid())
            f.bgColor = fills[x.fillId].fg;
        if (x.borderId > 0 && x.borderId < borders.size()) {
            f.borderEdges = borders[x.borderId].edges;
            if (borders[x.borderId].color.isValid()) f.borderColor = borders[x.borderId].color;
        }
        if (x.numFmtId != 0) {
            const QString code = numFmts.contains(x.numFmtId)
                                     ? numFmts.value(x.numFmtId) : builtinNumFmt(x.numFmtId);
            if (!code.isEmpty() && code != "General") f.numberFormat = code;
        }
        f.hAlign = x.hAlign; f.vAlign = x.vAlign; f.wrap = x.wrap;
        return f;
    }
};

ImpFont parseFontEl(QXmlStreamReader& r) {
    ImpFont f;
    auto boolAttr = [&]{ const auto v = r.attributes().value("val");
                         return v.isEmpty() || (v != u"0" && v != u"false"); };
    while (!(r.tokenType() == QXmlStreamReader::EndElement && r.name() == u"font")) {
        if (r.atEnd()) break;
        r.readNext();
        if (r.tokenType() != QXmlStreamReader::StartElement) continue;
        const auto n = r.name();
        if      (n == u"b")    f.bold      = boolAttr();
        else if (n == u"i")    f.italic    = boolAttr();
        else if (n == u"u")    f.underline = boolAttr();
        else if (n == u"sz")   f.size      = attrS(r, "val").toInt();
        else if (n == u"color")f.color     = rgbColor(attrS(r, "rgb"));
        else if (n == u"name") f.name      = attrS(r, "val");
    }
    return f;
}

ImpFill parseFillEl(QXmlStreamReader& r) {
    ImpFill fl; QString ptype; QColor fg;
    while (!(r.tokenType() == QXmlStreamReader::EndElement && r.name() == u"fill")) {
        if (r.atEnd()) break;
        r.readNext();
        if (r.tokenType() != QXmlStreamReader::StartElement) continue;
        if (r.name() == u"patternFill") ptype = attrS(r, "patternType");
        else if (r.name() == u"fgColor") fg = rgbColor(attrS(r, "rgb"));
    }
    if (ptype == "solid") fl.fg = fg;
    return fl;
}

ImpBorder parseBorderEl(QXmlStreamReader& r) {
    ImpBorder b; QColor col;
    while (!(r.tokenType() == QXmlStreamReader::EndElement && r.name() == u"border")) {
        if (r.atEnd()) break;
        r.readNext();
        if (r.tokenType() != QXmlStreamReader::StartElement) continue;
        const auto n = r.name();
        if (n == u"left" || n == u"right" || n == u"top" || n == u"bottom") {
            const QString style = attrS(r, "style");
            if (!style.isEmpty() && style != "none") {
                if      (n == u"left")   b.edges |= CellFormat::BLeft;
                else if (n == u"right")  b.edges |= CellFormat::BRight;
                else if (n == u"top")    b.edges |= CellFormat::BTop;
                else                     b.edges |= CellFormat::BBottom;
            }
        } else if (n == u"color") {
            const QColor c = rgbColor(attrS(r, "rgb"));
            if (c.isValid()) col = c;
        }
    }
    b.color = col;
    return b;
}

ImpXf parseXfEl(QXmlStreamReader& r) {
    ImpXf x;
    x.numFmtId = attrS(r, "numFmtId").toInt();
    x.fontId   = attrS(r, "fontId").toInt();
    x.fillId   = attrS(r, "fillId").toInt();
    x.borderId = attrS(r, "borderId").toInt();
    while (!(r.tokenType() == QXmlStreamReader::EndElement && r.name() == u"xf")) {
        if (r.atEnd()) break;
        r.readNext();
        if (r.tokenType() == QXmlStreamReader::StartElement && r.name() == u"alignment") {
            const QString h = attrS(r, "horizontal"), v = attrS(r, "vertical");
            x.hAlign = h == "left" ? Qt::AlignLeft : h == "center" ? Qt::AlignHCenter
                     : h == "right" ? Qt::AlignRight : 0;
            x.vAlign = v == "top" ? Qt::AlignTop : v == "center" ? Qt::AlignVCenter
                     : v == "bottom" ? Qt::AlignBottom : 0;
            x.wrap = attrS(r, "wrapText") == "1";
        }
    }
    return x;
}

StyleSheetData parseStyles(const QByteArray& xml) {
    StyleSheetData st;
    QXmlStreamReader r(xml);
    bool inCellXfs = false;
    while (!r.atEnd()) {
        const auto tok = r.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            const auto n = r.name();
            if      (n == u"cellXfs") inCellXfs = true;
            else if (n == u"numFmt")  st.numFmts.insert(attrS(r, "numFmtId").toInt(),
                                                        attrS(r, "formatCode"));
            else if (n == u"font")    st.fonts.push_back(parseFontEl(r));
            else if (n == u"fill")    st.fills.push_back(parseFillEl(r));
            else if (n == u"border")  st.borders.push_back(parseBorderEl(r));
            else if (n == u"xf" && inCellXfs) st.xfs.push_back(parseXfEl(r));
        } else if (tok == QXmlStreamReader::EndElement && r.name() == u"cellXfs") {
            inCellXfs = false;
        }
    }
    return st;
}

// Parse the <dxfs> block of styles.xml, indexed by dxfId.
//
// The trap here: inside a dxf the fill colour is written in <bgColor>, not
// <fgColor> as it is for an ordinary cell style. Reading fgColor gives an
// invalid colour and every rule silently paints nothing.
std::vector<ImpDxf> parseDxfs(const QByteArray& xml) {
    std::vector<ImpDxf> out;
    QXmlStreamReader r(xml);
    bool inDxfs = false, inFont = false, inFill = false;
    ImpDxf cur;
    while (!r.atEnd()) {
        const auto tok = r.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            const auto n = r.name();
            if      (n == u"dxfs") inDxfs = true;
            else if (!inDxfs)      continue;
            else if (n == u"dxf")  { cur = ImpDxf{}; inFont = inFill = false; }
            else if (n == u"font") inFont = true;
            else if (n == u"fill") inFill = true;
            else if (n == u"b")    { if (inFont) cur.bold = true; }
            else if (n == u"color" && inFont) cur.fg = rgbColor(attrS(r, "rgb"));
            else if (inFill && (n == u"bgColor" || n == u"fgColor")) {
                const QColor c = rgbColor(attrS(r, "rgb"));
                // bgColor is the fill in a dxf; fgColor only counts if bgColor
                // never turns up.
                if (c.isValid() && (n == u"bgColor" || !cur.bg.isValid())) cur.bg = c;
            }
        } else if (tok == QXmlStreamReader::EndElement) {
            const auto n = r.name();
            if      (n == u"dxfs") break;
            else if (n == u"dxf")  out.push_back(cur);
            else if (n == u"font") inFont = false;
            else if (n == u"fill") inFill = false;
        }
    }
    return out;
}

// Replace every whole-word use of a defined name with what it refers to.
// Quoted text is stepped over so a name that also appears inside a string
// literal is left alone.
QString applyDefinedNames(const QString& formula,
                          const QHash<QString, QString>& names) {
    if (names.isEmpty() || formula.isEmpty()) return formula;

    QString out;
    out.reserve(formula.size());
    int i = 0;
    const int n = formula.size();
    while (i < n) {
        const QChar ch = formula.at(i);
        if (ch == QLatin1Char('"')) {                 // copy the literal verbatim
            out += ch;
            for (++i; i < n; ++i) {
                out += formula.at(i);
                if (formula.at(i) == QLatin1Char('"')) { ++i; break; }
            }
            continue;
        }
        if (ch.isLetter() || ch == QLatin1Char('_')) {
            const int start = i;
            while (i < n && (formula.at(i).isLetterOrNumber()
                             || formula.at(i) == QLatin1Char('_')
                             || formula.at(i) == QLatin1Char('.'))) ++i;
            const QString word = formula.mid(start, i - start);
            // A name followed by '(' is a function call, and one followed by
            // '!' is a sheet qualifier: neither is a defined-name use.
            const QChar next = i < n ? formula.at(i) : QChar();
            const bool isCall = (next == QLatin1Char('(') || next == QLatin1Char('!'));
            const QString hit = isCall ? QString() : names.value(word.toUpper());
            out += hit.isEmpty() ? word : hit;
            continue;
        }
        out += ch;
        ++i;
    }
    return out;
}

// "A1:B2 D5" -> the rectangles it names.
std::vector<QRect> parseSqref(const QString& sqref) {
    std::vector<QRect> out;
    for (const QString& part : sqref.split(' ', Qt::SkipEmptyParts)) {
        const QStringList ends = part.split(':');
        int c1, r1, c2, r2;
        QString a = ends.value(0); a.remove('$');
        QString b = ends.value(ends.size() > 1 ? 1 : 0); b.remove('$');
        if (!parseRef(a, c1, r1) || !parseRef(b, c2, r2)) continue;
        out.push_back(QRect(QPoint(qMin(c1, c2), qMin(r1, r2)),
                            QPoint(qMax(c1, c2), qMax(r1, r2))));
    }
    return out;
}

// Build a formula this app's engine can evaluate from a cfRule. Excel supplies
// a ready-made <formula> for some rule types, but the ones it writes lean on
// ISERROR/ISNUMBER which are not implemented here, so the text rules are
// rebuilt from the rule's own `text` attribute instead.
QString cfRuleFormula(const QString& type, const QString& op,
                      const QString& text, const QStringList& formulas,
                      const QString& topLeft) {
    auto quoted = [&] {
        QString t = text; t.replace('"', QStringLiteral("\"\""));
        return QStringLiteral("\"") + t + QStringLiteral("\"");
    };
    if (type == "containsText")
        return QStringLiteral("=IFERROR(SEARCH(%1,%2)>0,FALSE)").arg(quoted(), topLeft);
    if (type == "notContainsText")
        return QStringLiteral("=IFERROR(SEARCH(%1,%2)>0,FALSE)=FALSE").arg(quoted(), topLeft);
    if (type == "beginsWith")
        return QStringLiteral("=LEFT(%1,%2)=%3").arg(topLeft).arg(text.size()).arg(quoted());
    if (type == "endsWith")
        return QStringLiteral("=RIGHT(%1,%2)=%3").arg(topLeft).arg(text.size()).arg(quoted());
    if (type == "cellIs") {
        const QString a = formulas.value(0);
        if (a.isEmpty()) return {};
        static const QHash<QString, QString> ops {
            {"equal", "="}, {"notEqual", "<>"}, {"greaterThan", ">"},
            {"lessThan", "<"}, {"greaterThanOrEqual", ">="}, {"lessThanOrEqual", "<="}
        };
        if (op == "between" && formulas.size() > 1)
            return QStringLiteral("=AND(%1>=%2,%1<=%3)").arg(topLeft, a, formulas.value(1));
        const QString sym = ops.value(op);
        if (sym.isEmpty()) return {};
        return QStringLiteral("=%1%2%3").arg(topLeft, sym, a);
    }
    if (type == "expression") {
        const QString f = formulas.value(0);
        // Written without a leading '=' in the file.
        return f.isEmpty() ? QString() : (f.startsWith('=') ? f : "=" + f);
    }
    // dataBar / colorScale / iconSet draw a graphic rather than a fill, which
    // this app has no representation for yet, so they are skipped.
    return {};
}


// Shift a formula's relative cell references by (dCol, dRow).
//
// Quoted text is copied through untouched, a name followed by '(' is a function
// call rather than a reference, and a '$' pins the column or the row it precedes.
QString shiftFormulaRefs(const QString& formula, int dCol, int dRow) {
    if (dCol == 0 && dRow == 0) return formula;
    QString out;
    out.reserve(formula.size());
    int i = 0;
    const int n = formula.size();
    while (i < n) {
        const QChar ch = formula.at(i);
        if (ch == QLatin1Char('"')) {
            out += ch;
            for (++i; i < n; ++i) { out += formula.at(i);
                                    if (formula.at(i) == QLatin1Char('"')) { ++i; break; } }
            continue;
        }
        if (ch == QLatin1Char('$') || ch.isLetter()) {
            const int start = i;
            bool colAbs = false, rowAbs = false;
            int j = i;
            if (formula.at(j) == QLatin1Char('$')) { colAbs = true; ++j; }
            const int ls = j;
            while (j < n && formula.at(j).isLetter()) ++j;
            const QString letters = formula.mid(ls, j - ls);

            // A function name, or a sheet qualifier: copy it and move on.
            if (!colAbs && !letters.isEmpty()) {
                int k = j;
                while (k < n && formula.at(k).isSpace()) ++k;
                if (k < n && (formula.at(k) == QLatin1Char('(')
                              || formula.at(k) == QLatin1Char('!'))) {
                    out += formula.mid(start, j - start);
                    i = j;
                    continue;
                }
            }

            int j2 = j;
            if (j2 < n && formula.at(j2) == QLatin1Char('$')) { rowAbs = true; ++j2; }
            const int ds = j2;
            while (j2 < n && formula.at(j2).isDigit()) ++j2;
            const QString digits = formula.mid(ds, j2 - ds);

            int col = 0, row = 0;
            if (!letters.isEmpty() && !digits.isEmpty()
                && parseRef(letters + digits, col, row)) {
                if (!colAbs) col += dCol;
                if (!rowAbs) row += dRow;
                if (col < 0) col = 0;
                if (row < 0) row = 0;
                out += (colAbs ? QStringLiteral("$") : QString()) + colLabel(col)
                     + (rowAbs ? QStringLiteral("$") : QString()) + QString::number(row + 1);
                i = j2;
                continue;
            }
            out += formula.mid(start, j - start);
            i = j;
            continue;
        }
        out += ch;
        ++i;
    }
    return out;
}

struct SharedMaster { QString text; int col; int row; };

void parseWorksheet(const QByteArray& xml, const QStringList& shared,
                    const StyleSheetData& styles, XlsxSheet& sheet,
                    const std::vector<ImpDxf>& dxfs,
                    const QHash<QString, QString>& definedNames) {
    QHash<QString, SharedMaster> sharedMasters;
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        if (r.readNext() != QXmlStreamReader::StartElement) continue;
        if (r.name() == u"mergeCell") {
            const QString ref = r.attributes().value("ref").toString();
            if (!ref.isEmpty()) sheet.merges.push_back(ref);
            continue;
        }
        if (r.name() == u"conditionalFormatting") {
            const std::vector<QRect> ranges = parseSqref(attrS(r, "sqref"));
            // Every rule in the block shares the block's range, and the rule
            // formulas are written relative to that range's top-left cell.
            while (!r.atEnd()) {
                const auto t = r.readNext();
                if (t == QXmlStreamReader::EndElement && r.name() == u"conditionalFormatting") break;
                if (t != QXmlStreamReader::StartElement || r.name() != u"cfRule") continue;

                const QString type = attrS(r, "type");
                const QString op   = attrS(r, "operator");
                const QString text = attrS(r, "text");
                const int dxfId    = attrS(r, "dxfId").isEmpty() ? -1 : attrS(r, "dxfId").toInt();

                QStringList formulas;
                QColor barColor;
                while (!r.atEnd()) {
                    const auto t2 = r.readNext();
                    if (t2 == QXmlStreamReader::EndElement && r.name() == u"cfRule") break;
                    if (t2 != QXmlStreamReader::StartElement) continue;
                    if (r.name() == u"formula")
                        formulas << r.readElementText(QXmlStreamReader::IncludeChildElements);
                    else if (r.name() == u"color")
                        barColor = rgbColor(attrS(r, "rgb"));
                }

                // dataBar carries its colour in a <color> child rather than
                // in a dxf, and has no dxfId at all.
                if (type == "dataBar") {
                    if (!barColor.isValid()) continue;
                    for (const QRect& rc : ranges) {
                        CondFormatRule rule;
                        rule.range    = rc;
                        rule.barColor = barColor;
                        sheet.condRules.push_back(rule);
                    }
                    continue;
                }

                if (dxfId < 0 || dxfId >= (int)dxfs.size()) continue;
                const ImpDxf& dx = dxfs[dxfId];
                if (!dx.bg.isValid() && !dx.fg.isValid() && !dx.bold) continue;

                for (const QRect& rc : ranges) {
                    const QString topLeft = colLabel(rc.left()) + QString::number(rc.top() + 1);
                    // Rule formulas use defined names exactly as cell formulas
                    // do, and a rule that fails to parse simply never matches,
                    // which is why the whole Gantt band stayed blank.
                    QStringList resolved;
                    for (const QString& fx : formulas)
                        resolved << applyDefinedNames(fx, definedNames);
                    const QString f = cfRuleFormula(type, op, text, resolved, topLeft);
                    if (f.isEmpty()) continue;
                    CondFormatRule rule;
                    rule.range     = rc;
                    rule.formula   = f;
                    rule.bgColor   = dx.bg;
                    rule.textColor = dx.fg;
                    rule.bold      = dx.bold;
                    sheet.condRules.push_back(rule);
                }
            }
            continue;
        }
        if (r.name() == u"sheetView") {
            const QString z = attrS(r, "zoomScale");
            if (!z.isEmpty()) {
                const int zi = z.toInt();
                if (zi >= 10 && zi <= 400) sheet.zoomScale = zi;
            }
            // Absent means shown; only an explicit "0" hides the grid.
            const QString g = attrS(r, "showGridLines");
            if (g == "0" || g == "false") sheet.showGridLines = false;
            continue;
        }
        if (r.name() == u"col") {
            const int mn = attrS(r, "min").toInt();
            const int mx = std::min(attrS(r, "max").toInt(), SpreadsheetModelColLimit);
            if (attrS(r, "customWidth") == "1") {
                const double w = attrS(r, "width").toDouble();
                if (mn >= 1 && w > 0)
                    for (int c = mn; c <= mx; ++c)
                        sheet.colWidths.push_back({c - 1, colWidthToPx(w)});
            }
            // Hidden is its own attribute: a hidden column still carries a
            // width, and leaving it visible shifts everything to its right.
            if (mn >= 1 && attrS(r, "hidden") == "1")
                for (int c = mn; c <= mx; ++c)
                    sheet.hiddenCols.push_back(c - 1);
            continue;
        }
        if (r.name() == u"row") {
            const int rowNo = attrS(r, "r").toInt();
            if (attrS(r, "customHeight") == "1") {
                const double ht = attrS(r, "ht").toDouble();
                if (rowNo >= 1 && ht > 0) sheet.rowHeights.push_back({rowNo - 1, rowHtToPx(ht)});
            }
            if (rowNo >= 1 && attrS(r, "hidden") == "1")
                sheet.hiddenRows.push_back(rowNo - 1);
            continue;   // cells are emitted as separate <c> StartElements
        }
        if (r.name() != u"c") continue;

        const QString ref  = r.attributes().value("r").toString();
        const QString type = r.attributes().value("t").toString();
        const QString sAttr = r.attributes().value("s").toString();
        QString formula, value, inlineStr;
        QString sharedType, sharedIdx;

        while (!(r.tokenType() == QXmlStreamReader::EndElement && r.name() == u"c")) {
            if (r.atEnd()) break;
            r.readNext();
            if (r.tokenType() == QXmlStreamReader::StartElement) {
                if (r.name() == u"f") {
                    sharedType = attrS(r, "t");
                    sharedIdx  = attrS(r, "si");
                    formula    = r.readElementText(QXmlStreamReader::IncludeChildElements);
                }
                else if (r.name() == u"v")  value     = r.readElementText(QXmlStreamReader::IncludeChildElements);
                else if (r.name() == u"is") inlineStr = r.readElementText(QXmlStreamReader::IncludeChildElements);
            }
        }

        // ── Shared formulas ─────────────────────────────────────────
        // The first cell of a run carries the text; the rest carry only the
        // shared id and the value Excel cached. Re-emit the master's text,
        // shifted to this cell, so the formula stays a formula.
        if (sharedType == QLatin1String("shared") && !sharedIdx.isEmpty()) {
            int c0 = 0, r0 = 0;
            if (!formula.isEmpty()) {
                if (parseRef(ref, c0, r0))
                sharedMasters.insert(sharedIdx, {formula, c0, r0});
            } else {
                const auto it = sharedMasters.constFind(sharedIdx);
                if (it != sharedMasters.constEnd() && parseRef(ref, c0, r0))
                formula = shiftFormulaRefs(it->text, c0 - it->col, r0 - it->row);
            }
        }

        QString content;
        if (!formula.isEmpty()) {
            // Excel marks functions newer than the file's baseline version with
            // an "_xlfn." (and worksheet-only ones with "_xlws.") prefix. It is
            // a forward-compatibility marker in the serialized form only: the
            // function is plain XLOOKUP. Leaving the prefix on meant every one
            // of them evaluated to #ERR even once the function was implemented.
            formula.remove(QStringLiteral("_xlfn."));
            formula.remove(QStringLiteral("_xlws."));
            formula = applyDefinedNames(formula, definedNames);
            content = "=" + formula;
        } else if (type == "s") {
            const int idx = value.toInt();
            if (idx >= 0 && idx < shared.size()) content = shared[idx];
        } else if (type == "inlineStr") {
            content = inlineStr;
        } else if (type == "str") {
            content = value;
        } else if (type == "b") {
            content = (value == "1") ? "TRUE" : "FALSE";
        } else {
            content = value;        // number (or empty)
        }

        CellFormat fmt;
        if (!sAttr.isEmpty()) fmt = styles.formatFor(sAttr.toInt());

        int col = 0, row = 0;
        if ((!content.isEmpty() || !fmt.isDefault()) && parseRef(ref, col, row))
            sheet.cells.push_back({col, row, content, fmt,
                                   sAttr.isEmpty() ? -1 : sAttr.toInt()});
    }
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Import
// ─────────────────────────────────────────────────────────────────────────────
bool importXlsx(const QString& path, std::vector<XlsxSheet>& outSheets) {
    outSheets.clear();

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray bytes = f.readAll();
    f.close();

    ZipReader zip;
    if (!zip.open(bytes)) return false;

    // Shared strings + styles (optional).
    QStringList shared;
    if (zip.has("xl/sharedStrings.xml"))
        shared = parseSharedStrings(zip.file("xl/sharedStrings.xml"));
    StyleSheetData styles;
    std::vector<ImpDxf> dxfs;
    if (zip.has("xl/styles.xml")) {
        styles = parseStyles(zip.file("xl/styles.xml"));
        dxfs   = parseDxfs(zip.file("xl/styles.xml"));
    }

    // workbook rels: rId → target part.
    QHash<QString, QString> ridToTarget;
    if (zip.has("xl/_rels/workbook.xml.rels")) {
        QXmlStreamReader r(zip.file("xl/_rels/workbook.xml.rels"));
        while (!r.atEnd()) {
            if (r.readNext() == QXmlStreamReader::StartElement && r.name() == u"Relationship") {
                const QString id = r.attributes().value("Id").toString();
                QString tgt = r.attributes().value("Target").toString();
                if (tgt.startsWith('/')) tgt = tgt.mid(1);
                else tgt = "xl/" + tgt;
                ridToTarget.insert(id, tgt);
            }
        }
    }

    // The workbook's colour scheme. Drawings name their colours through it far
    // more often than they spell out RGB, so a missing theme is why shapes and
    // chart series used to come out uncoloured.
    ThemeColors theme;
    for (const QString& tgt : ridToTarget) {
        if (tgt.contains(QLatin1String("theme"), Qt::CaseInsensitive)
            && tgt.endsWith(QLatin1String(".xml"), Qt::CaseInsensitive) && zip.has(tgt)) {
            theme = parseThemeColors(zip.file(tgt));
            if (!theme.isEmpty()) break;
        }
    }

    // Defined names: NAME -> what it refers to. The built-in _xlnm.* entries
    // (print areas and the like) are layout metadata, not formula values.
    QHash<QString, QString> definedNames;
    if (zip.has("xl/workbook.xml")) {
        QXmlStreamReader r(zip.file("xl/workbook.xml"));
        while (!r.atEnd()) {
            if (r.readNext() == QXmlStreamReader::StartElement && r.name() == u"definedName") {
                const QString nm = r.attributes().value("name").toString();
                const QString to = r.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
                if (!nm.isEmpty() && !to.isEmpty() && !nm.startsWith(QLatin1String("_xlnm")))
                    definedNames.insert(nm.toUpper(), to);
            }
        }
    }

    // workbook: ordered sheets (name + rId).
    struct SheetRef { QString name; QString rid; bool hidden = false; };
    QVector<SheetRef> sheetRefs;
    if (zip.has("xl/workbook.xml")) {
        QXmlStreamReader r(zip.file("xl/workbook.xml"));
        while (!r.atEnd()) {
            if (r.readNext() == QXmlStreamReader::StartElement && r.name() == u"sheet") {
                SheetRef sr;
                for (const auto& a : r.attributes()) {
                    if (a.name() == u"name") sr.name = a.value().toString();
                    else if (a.name() == u"id") sr.rid = a.value().toString();  // r:id
                    else if (a.name() == u"state")
                        sr.hidden = (a.value() == u"hidden" || a.value() == u"veryHidden");
                }
                sheetRefs.push_back(sr);
            }
        }
    }

    for (const SheetRef& sr : sheetRefs) {
        const QString part = ridToTarget.value(sr.rid);
        if (part.isEmpty() || !zip.has(part)) continue;
        XlsxSheet sheet;
        sheet.name = sr.name.isEmpty() ? QString("Sheet%1").arg(outSheets.size() + 1) : sr.name;
        sheet.hidden = sr.hidden;
        parseWorksheet(zip.file(part), shared, styles, sheet, dxfs, definedNames);

        // Charts and pictures live in a separate drawing part that the sheet
        // points at through its relationship file. A sheet has at most one.
        const QString relsPart = relsPathFor(part);
        if (zip.has(relsPart)) {
            const QString drawPart = firstRelOfType(zip.file(relsPart), "/drawing", partDir(part));
            if (!drawPart.isEmpty() && zip.has(drawPart)) {
                QHash<QString, QString> drawRels;
                const QString drawRelsPart = relsPathFor(drawPart);
                if (zip.has(drawRelsPart))
                    drawRels = parseRels(zip.file(drawRelsPart), partDir(drawPart));
                parseSheetDrawing(zip.file(drawPart), drawRels,
                                  [&zip](const QString& p) { return zip.file(p); },
                                  sheet.charts, sheet.images, sheet.shapes, theme);
            }
        }

        outSheets.push_back(std::move(sheet));
    }

    return !outSheets.empty();
}

// ─────────────────────────────────────────────────────────────────────────────
// Export — with a deduplicated style table (styles.xml + per-cell s= indices)
// ─────────────────────────────────────────────────────────────────────────────
namespace {

QString argbHex(const QColor& c) { return c.name(QColor::HexArgb).mid(1).toUpper(); }

QString halignName(int a) {
    if (a == Qt::AlignLeft)    return "left";
    if (a == Qt::AlignHCenter) return "center";
    if (a == Qt::AlignRight)   return "right";
    return {};
}
QString valignName(int a) {
    if (a == Qt::AlignTop)     return "top";
    if (a == Qt::AlignVCenter) return "center";
    if (a == Qt::AlignBottom)  return "bottom";
    return {};
}

// Builds and dedupes the OOXML style records, and maps a CellFormat → cellXf index.
struct StyleTable {
    QStringList fonts, fills, borders, xfs;
    QVector<QPair<int, QString>> numFmts;        // (id, code) for custom formats
    QHash<QString, int> fontKey, fillKey, borderKey, numFmtKey, xfKey;

    StyleTable() {
        fonts   << "<font><sz val=\"11\"/><name val=\"Calibri\"/></font>";
        fills   << "<fill><patternFill patternType=\"none\"/></fill>"
                << "<fill><patternFill patternType=\"gray125\"/></fill>";
        borders << "<border><left/><right/><top/><bottom/><diagonal/></border>";
        xfs     << "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>";
        xfKey.insert("0|0|0|0|", 0);
    }

    static int intern(QStringList& list, QHash<QString, int>& keys, const QString& frag) {
        auto it = keys.constFind(frag);
        if (it != keys.constEnd()) return it.value();
        const int idx = list.size();
        list << frag;
        keys.insert(frag, idx);
        return idx;
    }

    int fontFor(const CellFormat& f) {
        if (f.fontFamily.isEmpty() && f.fontSize == 0 && !f.bold && !f.italic
            && !f.underline && !f.textColor.isValid())
            return 0;
        QString frag = "<font>";
        if (f.bold)      frag += "<b/>";
        if (f.italic)    frag += "<i/>";
        if (f.underline) frag += "<u/>";
        frag += QString("<sz val=\"%1\"/>").arg(f.fontSize > 0 ? f.fontSize : 11);
        if (f.textColor.isValid())
            frag += QString("<color rgb=\"%1\"/>").arg(argbHex(f.textColor));
        frag += QString("<name val=\"%1\"/>")
                    .arg(f.fontFamily.isEmpty() ? "Calibri" : f.fontFamily);
        frag += "</font>";
        return intern(fonts, fontKey, frag);
    }
    int fillFor(const CellFormat& f) {
        if (!f.bgColor.isValid()) return 0;
        const QString frag = QString(
            "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"%1\"/>"
            "<bgColor indexed=\"64\"/></patternFill></fill>").arg(argbHex(f.bgColor));
        return intern(fills, fillKey, frag);
    }
    int borderFor(const CellFormat& f) {
        if (!f.borderEdges) return 0;
        const QString col = f.borderColor.isValid() ? argbHex(f.borderColor) : "FF000000";
        auto edge = [&](const char* tag, bool on) {
            return on ? QString("<%1 style=\"thin\"><color rgb=\"%2\"/></%1>").arg(tag, col)
                      : QString("<%1/>").arg(tag);
        };
        const QString frag = "<border>"
            + edge("left",   f.borderEdges & CellFormat::BLeft)
            + edge("right",  f.borderEdges & CellFormat::BRight)
            + edge("top",    f.borderEdges & CellFormat::BTop)
            + edge("bottom", f.borderEdges & CellFormat::BBottom)
            + "<diagonal/></border>";
        return intern(borders, borderKey, frag);
    }
    int numFmtFor(const CellFormat& f) {
        if (f.numberFormat.isEmpty() || f.numberFormat == "General") return 0;
        auto it = numFmtKey.constFind(f.numberFormat);
        if (it != numFmtKey.constEnd()) return it.value();
        const int id = 164 + numFmts.size();
        numFmts.push_back({id, f.numberFormat});
        numFmtKey.insert(f.numberFormat, id);
        return id;
    }
    int xfFor(const CellFormat& f) {
        if (f.isDefault()) return 0;
        const int fo = fontFor(f), fi = fillFor(f), bo = borderFor(f), nf = numFmtFor(f);
        const QString h = halignName(f.hAlign), v = valignName(f.vAlign);
        QString align;
        if (!h.isEmpty() || !v.isEmpty() || f.wrap) {
            align = "<alignment";
            if (!h.isEmpty()) align += " horizontal=\"" + h + "\"";
            if (!v.isEmpty()) align += " vertical=\"" + v + "\"";
            if (f.wrap)       align += " wrapText=\"1\"";
            align += "/>";
        }
        const QString key = QString("%1|%2|%3|%4|%5").arg(nf).arg(fo).arg(fi).arg(bo).arg(align);
        auto it = xfKey.constFind(key);
        if (it != xfKey.constEnd()) return it.value();

        QString frag = QString("<xf numFmtId=\"%1\" fontId=\"%2\" fillId=\"%3\" borderId=\"%4\" xfId=\"0\"")
                           .arg(nf).arg(fo).arg(fi).arg(bo);
        if (nf) frag += " applyNumberFormat=\"1\"";
        if (fo) frag += " applyFont=\"1\"";
        if (fi) frag += " applyFill=\"1\"";
        if (bo) frag += " applyBorder=\"1\"";
        if (!align.isEmpty()) frag += " applyAlignment=\"1\">" + align + "</xf>";
        else                  frag += "/>";
        const int idx = xfs.size();
        xfs << frag;
        xfKey.insert(key, idx);
        return idx;
    }

    QByteArray toXml() const {
        QString s = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                    "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">";
        if (!numFmts.isEmpty()) {
            s += QString("<numFmts count=\"%1\">").arg(numFmts.size());
            for (const auto& nf : numFmts)
                s += QString("<numFmt numFmtId=\"%1\" formatCode=\"%2\"/>")
                         .arg(nf.first).arg(xmlEscape(nf.second));
            s += "</numFmts>";
        }
        s += QString("<fonts count=\"%1\">%2</fonts>").arg(fonts.size()).arg(fonts.join(""));
        s += QString("<fills count=\"%1\">%2</fills>").arg(fills.size()).arg(fills.join(""));
        s += QString("<borders count=\"%1\">%2</borders>").arg(borders.size()).arg(borders.join(""));
        s += "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>";
        s += QString("<cellXfs count=\"%1\">%2</cellXfs>").arg(xfs.size()).arg(xfs.join(""));
        s += "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>";
        s += "</styleSheet>";
        return s.toUtf8();
    }
};

// `hasDrawing` says whether a drawing part was generated for this sheet.
// A worksheet may reference exactly one, and it now covers the charts as
// well as the export mark, so the caller decides and passes it in.
QByteArray buildWorksheet(const XlsxSheet& sheet, StyleTable& styles,
                          bool hasDrawing) {
    QMap<int, QVector<const XlsxCell*>> byRow;
    for (const XlsxCell& c : sheet.cells) byRow[c.row].push_back(&c);

    QMap<int, int> rowH;                       // row → pixels
    for (const auto& rh : sheet.rowHeights) rowH.insert(rh.first, rh.second);

    QString xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        // The r namespace is needed by the watermark's <drawing r:id>; declaring
        // it unconditionally keeps the element list below uniform.
        "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">";

    if (!sheet.colWidths.empty()) {
        xml += "<cols>";
        for (const auto& cw : sheet.colWidths)
            xml += QString("<col min=\"%1\" max=\"%1\" width=\"%2\" customWidth=\"1\"/>")
                       .arg(cw.first + 1).arg(pxToColWidth(cw.second), 0, 'g', 6);
        xml += "</cols>";
    }
    xml += "<sheetData>";

    // Iterate the union of rows that have cells or a custom height.
    QSet<int> rowSet;
    for (auto it = byRow.begin(); it != byRow.end(); ++it) rowSet.insert(it.key());
    for (auto it = rowH.begin();  it != rowH.end();  ++it) rowSet.insert(it.key());
    QList<int> rows = rowSet.values();
    std::sort(rows.begin(), rows.end());

    for (int row : rows) {
        QString rowAttr = QString(" r=\"%1\"").arg(row + 1);
        if (rowH.contains(row))
            rowAttr += QString(" ht=\"%1\" customHeight=\"1\"")
                           .arg(pxToRowHt(rowH.value(row)), 0, 'g', 6);
        xml += "<row" + rowAttr + ">";
        QVector<const XlsxCell*> cells = byRow.value(row);
        std::sort(cells.begin(), cells.end(),
                  [](const XlsxCell* a, const XlsxCell* b){ return a->col < b->col; });
        for (const XlsxCell* c : cells) {
            const QString ref = colLabel(c->col) + QString::number(c->row + 1);
            const int sIdx = styles.xfFor(c->format);
            const QString sAttr = sIdx ? QString(" s=\"%1\"").arg(sIdx) : QString();
            const QString& content = c->content;
            if (content.startsWith('=')) {
                xml += QString("<c r=\"%1\"%2><f>%3</f></c>")
                           .arg(ref, sAttr, xmlEscape(content.mid(1)));
            } else {
                bool isNum = false;
                content.toDouble(&isNum);
                const QString up = content.toUpper();
                if (up == "TRUE" || up == "FALSE") {
                    xml += QString("<c r=\"%1\"%2 t=\"b\"><v>%3</v></c>")
                               .arg(ref, sAttr, up == "TRUE" ? "1" : "0");
                } else if (isNum) {
                    xml += QString("<c r=\"%1\"%2><v>%3</v></c>")
                               .arg(ref, sAttr, content.trimmed());
                } else {
                    xml += QString("<c r=\"%1\"%2 t=\"inlineStr\"><is><t xml:space=\"preserve\">%3</t></is></c>")
                               .arg(ref, sAttr, xmlEscape(content));
                }
            }
        }
        xml += "</row>";
    }
    xml += "</sheetData>";
    if (!sheet.merges.empty()) {
        xml += QString("<mergeCells count=\"%1\">").arg((int)sheet.merges.size());
        for (const QString& m : sheet.merges)
            xml += QString("<mergeCell ref=\"%1\"/>").arg(m);
        xml += "</mergeCells>";
    }
    // Schema order, and it is not advisory: CT_Worksheet is an xsd:sequence
    // that runs ... pageSetup, headerFooter, rowBreaks, ... , drawing,
    // legacyDrawing, legacyDrawingHF. So headerFooter comes BEFORE drawing.
    //
    // This used to emit drawing first, with a comment asserting that was what
    // Excel wanted. It was the wrong way round, and Excel refused to open the
    // file at all: not a repair prompt, a flat refusal. Every workbook created
    // from scratch here and saved as .xlsx was unopenable in Excel from 1.5.0,
    // when the export mark first put a <drawing> on the sheet, until 1.7.6.
    // Nothing caught it because the preserving save (which splices into the
    // original worksheet and never comes through here) is the path a workbook
    // opened FROM Excel takes, and that one was fine.
    if (NativeOffice::Watermark::enabledForExport())
        xml += NativeOffice::Watermark::Ooxml::xlsxHeaderFooterXml();
    if (hasDrawing) xml += "<drawing r:id=\"rIdDraw\"/>";
    if (NativeOffice::Watermark::enabledForExport())
        xml += "<legacyDrawingHF r:id=\"rIdWmVml\"/>";

    xml += "</worksheet>";
    return xml.toUtf8();
}

} // namespace


// ─────────────────────────────────────────────────────────────────────────────
// Preserving export, see XlsxIo.h
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// Replace the <sheetData> element of a worksheet part, leaving every other
// element (cols, drawing references, merges, pageSetup, the lot) exactly as it
// was. Returns false when the element cannot be located.
bool spliceSheetData(QByteArray& xml, const QByteArray& newSheetData) {
    const int selfClosing = xml.indexOf("<sheetData/>");
    if (selfClosing >= 0) {
        xml.replace(selfClosing, int(strlen("<sheetData/>")), newSheetData);
        return true;
    }
    const int open = xml.indexOf("<sheetData");
    if (open < 0) return false;
    const int openEnd = xml.indexOf('>', open);
    if (openEnd < 0) return false;
    const int close = xml.indexOf("</sheetData>", openEnd);
    if (close < 0) return false;
    const int end = close + int(strlen("</sheetData>"));
    xml.replace(open, end - open, newSheetData);
    return true;
}

// The cells of one sheet as a <sheetData> block, re-using each cell's original
// style index. Returns false if any cell needs a style the file does not have.
bool buildPreservedSheetData(const XlsxSheet& sh, QByteArray& out) {
    // Group by row; a row element must list its cells in column order and the
    // rows must be in row order.
    QMap<int, QMap<int, const XlsxCell*>> rows;
    for (const XlsxCell& c : sh.cells) {
        if (c.content.isEmpty() && c.format.isDefault() && c.xfIndex < 0) continue;
        rows[c.row][c.col] = &c;
    }

    QString s = "<sheetData>";
    for (auto rit = rows.begin(); rit != rows.end(); ++rit) {
        s += QString("<row r=\"%1\">").arg(rit.key() + 1);
        for (auto cit = rit.value().begin(); cit != rit.value().end(); ++cit) {
            const XlsxCell& c = *cit.value();
            const QString ref = colLabel(c.col) + QString::number(c.row + 1);

            // A cell restyled in the app has no index the original file knows.
            if (c.xfIndex < 0 && !c.format.isDefault()) return false;

            QString attrs = QString(" r=\"%1\"").arg(ref);
            if (c.xfIndex >= 0) attrs += QString(" s=\"%1\"").arg(c.xfIndex);

            if (c.content.startsWith('=')) {
                s += "<c" + attrs + "><f>"
                   + xmlEscape(c.content.mid(1)) + "</f></c>";
            } else if (c.content.isEmpty()) {
                s += "<c" + attrs + "/>";
            } else {
                bool isNum = false;
                const double d = c.content.toDouble(&isNum);
                if (isNum) {
                    s += "<c" + attrs + "><v>"
                       + QString::number(d, 'g', 15) + "</v></c>";
                } else {
                    // Inline string: avoids having to rewrite sharedStrings.xml,
                    // and Excel/WPS read it identically.
                    s += "<c" + attrs + " t=\"inlineStr\"><is><t xml:space=\"preserve\">"
                       + xmlEscape(c.content) + "</t></is></c>";
                }
            }
        }
        s += "</row>";
    }
    s += "</sheetData>";
    out = s.toUtf8();
    return true;
}


// ── Adding app-made objects to a package that is being put back ──────────
//
// A preserving save copies the original package, so on its own it can only ever
// hold the charts and pictures that package already held. Anything drawn in the
// app has no part to copy, which is why every .xlsx save dropped it. What
// follows puts the missing pieces in: a chart or media part, an anchor in the
// sheet's drawing (creating that drawing when the sheet has none), and the
// relationships and content types tying them together.
//
// Anything that cannot be done is skipped. One object failing to be written
// must never cost the workbook content it already had.

const char* kRelsHead =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
const char* kRelsTail = "</Relationships>";

// Byte offset of the first depth-1 child of the root element whose local name
// is in `stop`, or of the root's closing tag when none of them is there.
//
// This is a depth-aware scan rather than an indexOf, because every name that
// matters here also occurs nested inside other elements: a <extLst> under a
// data validation is not the worksheet's own <extLst>, and splicing ahead of
// it would drop the new element into the middle of something else. Returns -1
// when the document cannot be scanned.
int insertPointBefore(const QByteArray& xml, const QSet<QByteArray>& stop) {
    const int n = xml.size();
    int i = 0, depth = 0;
    while (i < n) {
        if (xml[i] != '<') { ++i; continue; }
        if (xml.mid(i, 4) == "<!--") {
            i = xml.indexOf("-->", i);  if (i < 0) return -1;  i += 3;  continue;
        }
        if (xml.mid(i, 9) == "<![CDATA[") {
            i = xml.indexOf("]]>", i);  if (i < 0) return -1;  i += 3;  continue;
        }
        if (xml.mid(i, 2) == "<?") {
            i = xml.indexOf("?>", i);   if (i < 0) return -1;  i += 2;  continue;
        }
        if (xml.mid(i, 2) == "<!") {
            i = xml.indexOf('>', i);    if (i < 0) return -1;  i += 1;  continue;
        }
        const bool closing = (i + 1 < n && xml[i + 1] == '/');

        // End of the tag, honouring quoted attribute values so a '>' inside
        // one does not end it early.
        int j = i + 1;
        bool inQuote = false;
        char quoteCh = 0;
        while (j < n) {
            const char ch = xml[j];
            if (inQuote)                             { if (ch == quoteCh) inQuote = false; }
            else if (ch == '"' || ch == '\'')        { inQuote = true; quoteCh = ch; }
            else if (ch == '>')                      break;
            ++j;
        }
        if (j >= n) return -1;
        const bool selfClose = (xml[j - 1] == '/');

        int a = i + (closing ? 2 : 1), b = a;
        while (b < j && xml[b] != ' ' && xml[b] != '/' && xml[b] != '>'
               && xml[b] != '\t' && xml[b] != '\n' && xml[b] != '\r') ++b;
        QByteArray name = xml.mid(a, b - a);
        const int colon = name.lastIndexOf(':');
        if (colon >= 0) name = name.mid(colon + 1);   // drop the prefix

        if (closing) {
            if (--depth == 0) return i;               // the root's closing tag
        } else {
            ++depth;
            if (depth == 2 && stop.contains(name)) return i;
            if (selfClose) --depth;
        }
        i = j + 1;
    }
    return -1;
}

bool spliceBeforeRootClose(QByteArray& xml, const QByteArray& fragment) {
    const int at = insertPointBefore(xml, {});
    if (at < 0) return false;
    xml.insert(at, fragment);
    return true;
}

// A relationship id this rels part does not already use. The prefix keeps it
// clear of the ids the original file chose, whatever scheme those follow.
QString freeRelId(const QByteArray& rels) {
    for (int n = 1; ; ++n) {
        const QString id = QString("rIdNO%1").arg(n);
        if (!rels.contains(("\"" + id + "\"").toUtf8())) return id;
    }
}

// "xl/charts/chart3.xml" seen from "xl/drawings" is "../charts/chart3.xml".
QString relativeTarget(const QString& part, const QString& baseDir) {
    QStringList a = part.split('/', Qt::SkipEmptyParts);
    QStringList b = baseDir.split('/', Qt::SkipEmptyParts);
    while (!a.isEmpty() && !b.isEmpty() && a.first() == b.first()) {
        a.removeFirst(); b.removeFirst();
    }
    QString up;
    for (int k = 0; k < b.size(); ++k) up += QStringLiteral("../");
    return up + a.join('/');
}

void injectAppObjects(const ZipReader& zip,
                     const std::vector<XlsxSheet>& sheets,
                     const QStringList& sheetParts,
                     QHash<QString, QByteArray>& replaced,
                     QStringList& added) {
    // Part names already spoken for, so a new part never lands on an existing
    // one. Matched case-insensitively for the same reason the reader is.
    QSet<QString> used;
    for (const QString& n : zip.names()) used.insert(n.toLower());
    auto claim = [&](const char* pattern, int& counter) {
        QString name;
        do { name = QString(pattern).arg(++counter); } while (used.contains(name.toLower()));
        used.insert(name.toLower());
        return name;
    };
    auto partBytes = [&](const QString& path) {
        return replaced.contains(path) ? replaced.value(path) : zip.file(path);
    };

    int chartCounter = 0, drawingCounter = 0;
    QStringList   overrides;
    QSet<QString> mediaExts;

    for (int i = 0; i < sheetParts.size() && i < static_cast<int>(sheets.size()); ++i) {
        const XlsxSheet& sh = sheets[i];
        if (!sh.cellText) continue;         // no reader, so no way to resolve a range

        // Only what the app added. The file's own objects are already in the
        // package this save is putting back, in better shape than a rewrite.
        QVector<const ChartSpec*>  myCharts;
        QVector<const SheetImage*> myImages;
        for (const ChartSpec& c : sh.charts) if (!c.fromFile) myCharts.push_back(&c);
        for (const SheetImage& i : sh.images)
            if (!i.fromFile && !i.data.isEmpty()) myImages.push_back(&i);
        if (myCharts.isEmpty() && myImages.isEmpty()) continue;

        const QString sheetDir      = partDir(sheetParts[i]);
        const QString sheetRelsPath = relsPathFor(sheetParts[i]);
        QByteArray    sheetRels     = partBytes(sheetRelsPath);

        QString drawPart = sheetRels.isEmpty()
            ? QString() : firstRelOfType(sheetRels, "/drawing", sheetDir);
        const bool newDrawing = drawPart.isEmpty() || !zip.has(drawPart);
        if (newDrawing) drawPart = claim("xl/drawings/drawing%1.xml", drawingCounter);

        const QString drawDir       = partDir(drawPart);
        const QString drawRelsPath  = relsPathFor(drawPart);
        QByteArray    drawXml       = newDrawing ? QByteArray() : partBytes(drawPart);
        QByteArray    drawRels      = newDrawing ? QByteArray() : partBytes(drawRelsPath);

        if (drawXml.isEmpty())
            drawXml = QByteArray(
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                "<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/"
                "spreadsheetDrawing\" xmlns:a=\"http://schemas.openxmlformats.org/"
                "drawingml/2006/main\"></xdr:wsDr>");
        if (drawRels.isEmpty()) drawRels = QByteArray(kRelsHead) + kRelsTail;

        QByteArray anchors;
        int shapeId = 5000;                 // clear of the ids a file usually uses
        for (const ChartSpec* c : myCharts) {
            const QByteArray part = buildChartPartXml(*c, sh.name, sh.cellText);
            if (part.isEmpty()) continue;   // nothing plottable, leave it out

            const QString chartPart = claim("xl/charts/chart%1.xml", chartCounter);
            const QString relId     = freeRelId(drawRels);
            const int at = drawRels.lastIndexOf(kRelsTail);
            if (at < 0) continue;
            drawRels.insert(at, QString("<Relationship Id=\"%1\" Type=\"http://schemas."
                                        "openxmlformats.org/officeDocument/2006/relationships"
                                        "/chart\" Target=\"%2\"/>")
                                    .arg(relId, relativeTarget(chartPart, drawDir)).toUtf8());
            anchors += buildChartAnchorXml(*c, relId, ++shapeId).toUtf8();
            replaced.insert(chartPart, part);
            added << chartPart;
            overrides << QString("<Override PartName=\"/%1\" ContentType=\"application/vnd."
                                 "openxmlformats-officedocument.drawingml.chart+xml\"/>")
                             .arg(chartPart);
        }
        for (const SheetImage* im : myImages) {
            const QString ext = imageExtension(im->data);
            QString mediaPart;
            {
                // Media is named by extension, so the counter is per extension.
                const QString pattern = QStringLiteral("xl/media/image%1.") + ext;
                int n = 0;
                do { mediaPart = pattern.arg(++n); }
                while (used.contains(mediaPart.toLower()));
                used.insert(mediaPart.toLower());
            }
            const QString relId = freeRelId(drawRels);
            const int at = drawRels.lastIndexOf(kRelsTail);
            if (at < 0) continue;
            drawRels.insert(at, QString("<Relationship Id=\"%1\" Type=\"http://schemas."
                                        "openxmlformats.org/officeDocument/2006/relationships"
                                        "/image\" Target=\"%2\"/>")
                                    .arg(relId, relativeTarget(mediaPart, drawDir)).toUtf8());
            anchors += buildPictureAnchorXml(*im, relId, ++shapeId).toUtf8();
            replaced.insert(mediaPart, im->data);
            added << mediaPart;
            mediaExts.insert(ext);
        }
        if (anchors.isEmpty()) continue;
        if (!spliceBeforeRootClose(drawXml, anchors)) continue;

        replaced.insert(drawPart, drawXml);
        replaced.insert(drawRelsPath, drawRels);
        if (!zip.has(drawRelsPath)) added << drawRelsPath;

        if (!newDrawing) continue;          // the worksheet already points at it
        added << drawPart;
        overrides << QString("<Override PartName=\"/%1\" ContentType=\"application/vnd."
                             "openxmlformats-officedocument.drawing+xml\"/>").arg(drawPart);

        // Point the worksheet at the new drawing. <drawing> has a fixed place
        // in the worksheet schema, after everything that may precede it and
        // before the legacy-drawing and table elements that follow.
        if (sheetRels.isEmpty()) sheetRels = QByteArray(kRelsHead) + kRelsTail;
        const QString relId = freeRelId(sheetRels);
        const int at = sheetRels.lastIndexOf(kRelsTail);
        if (at < 0) continue;
        sheetRels.insert(at, QString("<Relationship Id=\"%1\" Type=\"http://schemas."
                                     "openxmlformats.org/officeDocument/2006/relationships"
                                     "/drawing\" Target=\"%2\"/>")
                                 .arg(relId, relativeTarget(drawPart, sheetDir)).toUtf8());

        static const QSet<QByteArray> kAfterDrawing = {
            "legacyDrawing", "legacyDrawingHF", "drawingHF", "picture", "oleObjects",
            "controls", "webPublishItems", "tableParts", "extLst"
        };
        QByteArray wsXml = partBytes(sheetParts[i]);
        const int ins = insertPointBefore(wsXml, kAfterDrawing);
        if (ins < 0) continue;
        wsXml.insert(ins, QString("<drawing xmlns:r=\"http://schemas.openxmlformats.org/"
                                  "officeDocument/2006/relationships\" r:id=\"%1\"/>")
                              .arg(relId).toUtf8());
        replaced.insert(sheetParts[i], wsXml);
        replaced.insert(sheetRelsPath, sheetRels);
        if (!zip.has(sheetRelsPath)) added << sheetRelsPath;
    }

    if (overrides.isEmpty() && mediaExts.isEmpty()) return;

    // Content types last, once the full list of new parts is known.
    QByteArray ct = partBytes(QStringLiteral("[Content_Types].xml"));
    if (ct.isEmpty()) return;
    QByteArray add;
    for (const QString& o : overrides) add += o.toUtf8();
    // A media part is typed by a <Default> on its extension. Most workbooks
    // that already hold pictures declare it; adding a second one for the same
    // extension is what makes Excel call the package invalid, so this only
    // adds what is missing.
    for (const QString& ext : mediaExts) {
        if (ct.contains(QString("Extension=\"%1\"").arg(ext).toUtf8())) continue;
        if (ct.contains(QString("Extension=\"%1\"").arg(ext.toUpper()).toUtf8())) continue;
        add += QString("<Default Extension=\"%1\" ContentType=\"image/%1\"/>")
                   .arg(ext).toUtf8();
    }
    if (add.isEmpty()) return;
    if (spliceBeforeRootClose(ct, add))
        replaced.insert(QStringLiteral("[Content_Types].xml"), ct);
}

} // namespace

// Everything the preserving write does except the writing. `outBytes` may be
// null, which makes this a dry run: the same checks and the same failure
// points, no package assembled and nothing put on disk. That is what lets the
// save path ask whether a workbook's charts would survive without keeping a
// second copy of the rules that would drift out of step with these.
static bool rebuildPreservedPackage(const std::vector<XlsxSheet>& sheets,
                                    const QByteArray& original,
                                    QByteArray* outBytes) {
    if (original.isEmpty() || sheets.empty()) return false;

    ZipReader zip;
    if (!zip.open(original)) return false;
    if (!zip.has("xl/workbook.xml")) return false;

    // Sheet parts in workbook order, exactly as the importer resolves them.
    QHash<QString, QString> ridToTarget;
    if (zip.has("xl/_rels/workbook.xml.rels")) {
        QXmlStreamReader r(zip.file("xl/_rels/workbook.xml.rels"));
        while (!r.atEnd()) {
            if (r.readNext() == QXmlStreamReader::StartElement && r.name() == u"Relationship") {
                const QString id = r.attributes().value("Id").toString();
                QString tgt = r.attributes().value("Target").toString();
                if (tgt.startsWith('/')) tgt = tgt.mid(1); else tgt = "xl/" + tgt;
                ridToTarget.insert(id, tgt);
            }
        }
    }
    QStringList sheetParts;
    {
        QXmlStreamReader r(zip.file("xl/workbook.xml"));
        while (!r.atEnd()) {
            if (r.readNext() == QXmlStreamReader::StartElement && r.name() == u"sheet") {
                QString rid;
                for (const auto& a : r.attributes())
                    if (a.name() == u"id") rid = a.value().toString();
                const QString part = ridToTarget.value(rid);
                if (part.isEmpty() || !zip.has(part)) return false;
                sheetParts << part;
            }
        }
    }
    // The sheet set must line up; anything else (added or removed sheets) needs
    // the workbook rebuilt, which this path deliberately does not do.
    if (sheetParts.size() != int(sheets.size())) return false;

    // Regenerate each worksheet.
    QHash<QString, QByteArray> replaced;
    for (int i = 0; i < sheetParts.size(); ++i) {
        QByteArray sheetData;
        if (!buildPreservedSheetData(sheets[i], sheetData)) return false;
        QByteArray xml = zip.file(sheetParts[i]);
        if (xml.isEmpty()) return false;
        if (!spliceSheetData(xml, sheetData)) return false;
        replaced.insert(sheetParts[i], xml);
    }

    if (!outBytes) return true;      // a dry run, and every check passed

    // Charts the app added have no part in this package to keep, so their parts
    // are created here. Everything it adds lands in `replaced` (for parts that
    // already exist) or in `extra` (for brand new ones).
    QStringList extra;
    injectAppObjects(zip, sheets, sheetParts, replaced, extra);

    // Write the package back out: every original part, with the worksheets
    // swapped for the regenerated ones, then whatever the charts needed added.
    ZipWriter out;
    for (const QString& name : zip.names())
        out.add(name, replaced.contains(name) ? replaced.value(name) : zip.file(name));
    for (const QString& name : extra)
        out.add(name, replaced.value(name));
    *outBytes = out.finish();
    return true;
}

bool canPreserveXlsx(const std::vector<XlsxSheet>& sheets,
                     const QByteArray& original) {
    return rebuildPreservedPackage(sheets, original, nullptr);
}

bool exportXlsxPreserving(const QString& path,
                          const std::vector<XlsxSheet>& sheets,
                          const QByteArray& original) {
    QByteArray bytes;
    if (!rebuildPreservedPackage(sheets, original, &bytes)) return false;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    const bool ok = f.write(bytes) == bytes.size();
    f.close();
    return ok;
}

bool exportXlsx(const QString& path, const std::vector<XlsxSheet>& sheets) {
    const int nSheets = std::max<int>(1, static_cast<int>(sheets.size()));
    ZipWriter zip;

    // ── Charts ──────────────────────────────────────────────────────────────
    // Worked out before the worksheets, because a worksheet has to say whether
    // it references a drawing and a sheet has a drawing when it carries either
    // a chart or the export mark.
    struct SheetDrawing {
        QStringList         partNames;   // "xl/charts/chart1.xml", "xl/media/image1.png"
        QStringList         targets;     // "../charts/chart1.xml", as the rel wants it
        QStringList         relTypes;    // "chart" or "image"
        QVector<QByteArray> partXml;
        QStringList         relIds;
        QString             anchors;     // the graphicFrames and pictures, concatenated
        // A chart part that references a picture fill needs a .rels of its own,
        // which nothing else in this package does: every other relationship
        // here hangs off a worksheet or off the drawing.
        QStringList         chartRelsFor;  // "xl/charts/_rels/chart1.xml.rels"
        QStringList         chartRelsXml;
    };
    QVector<SheetDrawing> draw(nSheets);
    QSet<QString> mediaExts;             // extensions needing a <Default> content type
    const QString kRelsHead =
        QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                       "<Relationships xmlns=\"http://schemas.openxmlformats.org/"
                       "package/2006/relationships\">");
    const QString kImageRelType =
        QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/"
                       "relationships/image");
    int chartNo = 0, imageNo = 0;
    for (int i = 0; i < nSheets && i < (int)sheets.size(); ++i) {
        const XlsxSheet& sh = sheets[i];
        // Shape ids only have to be unique inside one drawing part.
        int shapeId = 1;
        for (const ChartSpec& cs : sh.charts) {
            // One per chart: the ids inside are numbered within a single part.
            ChartMedia cm;
            const QByteArray part = buildChartPartXml(cs, sh.name, sh.cellText, &cm);
            if (part.isEmpty()) continue;   // nothing plottable, skip it
            ++chartNo;
            const QString relId = QString("rIdCh%1").arg(chartNo);
            draw[i].partNames << QString("xl/charts/chart%1.xml").arg(chartNo);
            draw[i].targets   << QString("../charts/chart%1.xml").arg(chartNo);
            draw[i].relTypes  << QStringLiteral("chart");
            draw[i].partXml   << part;
            draw[i].relIds    << relId;
            draw[i].anchors   += buildChartAnchorXml(cs, relId, ++shapeId);

            // The pictures that chart's series are filled with. They are the
            // chart's own relationships, not the drawing's, so they go under
            // xl/charts/_rels rather than beside the sheet's images.
            if (!cm.data.isEmpty()) {
                QString rels = kRelsHead;
                for (int k = 0; k < cm.data.size(); ++k) {
                    ++imageNo;
                    const QString ext = imageExtension(cm.data.at(k));
                    mediaExts.insert(ext);
                    draw[i].partNames << QString("xl/media/image%1.%2").arg(imageNo).arg(ext);
                    draw[i].targets   << QString();   // referenced by the chart, not the drawing
                    draw[i].relTypes  << QStringLiteral("media");
                    draw[i].partXml   << cm.data.at(k);
                    draw[i].relIds    << QString();
                    rels += QString("<Relationship Id=\"%1\" Type=\"%2\" Target=\"../media/image%3.%4\"/>")
                                .arg(cm.relIds.at(k), kImageRelType)
                                .arg(imageNo).arg(ext);
                }
                draw[i].chartRelsFor << QString("xl/charts/_rels/chart%1.xml.rels").arg(chartNo);
                draw[i].chartRelsXml << rels + QStringLiteral("</Relationships>");
            }
        }
        for (const SheetImage& im : sh.images) {
            if (im.data.isEmpty()) continue;
            ++imageNo;
            const QString ext   = imageExtension(im.data);
            const QString relId = QString("rIdIm%1").arg(imageNo);
            mediaExts.insert(ext);
            draw[i].partNames << QString("xl/media/image%1.%2").arg(imageNo).arg(ext);
            draw[i].targets   << QString("../media/image%1.%2").arg(imageNo).arg(ext);
            draw[i].relTypes  << QStringLiteral("image");
            draw[i].partXml   << im.data;
            draw[i].relIds    << relId;
            draw[i].anchors   += buildPictureAnchorXml(im, relId, ++shapeId);
        }

        // Shapes last, so they land over the charts and pictures the way the
        // sheet shows them. A shape filled with a picture claims a media part
        // in this drawing's own rels, which is where the shape references it.
        if (!sh.shapes.empty()) {
            const QVector<SheetShape> shapes(sh.shapes.begin(), sh.shapes.end());
            auto fillRel = [&](const QByteArray& bytes) -> QString {
                ++imageNo;
                const QString ext   = imageExtension(bytes);
                const QString relId = QString("rIdShp%1").arg(imageNo);
                mediaExts.insert(ext);
                draw[i].partNames << QString("xl/media/image%1.%2").arg(imageNo).arg(ext);
                draw[i].targets   << QString("../media/image%1.%2").arg(imageNo).arg(ext);
                draw[i].relTypes  << QStringLiteral("image");
                draw[i].partXml   << bytes;
                draw[i].relIds    << relId;
                return relId;
            };
            draw[i].anchors += buildShapeAnchorsXml(shapes, fillRel, shapeId);
        }
    }
    const bool wm = NativeOffice::Watermark::enabledForExport();
    auto hasDrawing = [&](int i) { return wm || !draw[i].anchors.isEmpty(); };

    // Generate worksheets so the style table is fully populated.
    StyleTable styles;
    QVector<QByteArray> sheetXml;
    for (int i = 0; i < nSheets; ++i) {
        const XlsxSheet empty;
        const XlsxSheet& sh = (i < (int)sheets.size()) ? sheets[i] : empty;
        sheetXml.push_back(buildWorksheet(sh, styles, hasDrawing(i)));
    }

    // [Content_Types].xml
    {
        QString s =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
            "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
            "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
            "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
            "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>";
        for (int i = 0; i < nSheets; ++i)
            s += QString("<Override PartName=\"/xl/worksheets/sheet%1.xml\" "
                         "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>")
                     .arg(i + 1);
        if (wm) {
            s += "<Default Extension=\"png\" ContentType=\"image/png\"/>";
            s += "<Default Extension=\"vml\" ContentType=\"application/vnd.openxmlformats-officedocument.vmlDrawing\"/>";
        }
        for (int i = 0; i < nSheets; ++i)
            if (hasDrawing(i))
                s += QString("<Override PartName=\"/xl/drawings/drawing%1.xml\" "
                             "ContentType=\"application/vnd.openxmlformats-officedocument.drawing+xml\"/>")
                         .arg(i + 1);
        for (int i = 0; i < nSheets; ++i)
            for (int k = 0; k < draw[i].partNames.size(); ++k)
                if (draw[i].relTypes.at(k) == QLatin1String("chart"))
                    s += QString("<Override PartName=\"/%1\" ContentType=\"application/vnd."
                                 "openxmlformats-officedocument.drawingml.chart+xml\"/>")
                             .arg(draw[i].partNames.at(k));
        // Media is typed by extension. The watermark already declares png when
        // it is on, so only what is not there yet gets added.
        for (const QString& ext : mediaExts) {
            if (wm && ext == QLatin1String("png")) continue;
            s += QString("<Default Extension=\"%1\" ContentType=\"image/%2\"/>")
                     .arg(ext, ext == QLatin1String("jpeg") ? QStringLiteral("jpeg") : ext);
        }
        s += "</Types>";
        zip.add("[Content_Types].xml", s.toUtf8());
    }

    // ── Drawings: the charts, and the export mark ────────────────────
    // One drawing part per sheet that needs one. A worksheet may reference only
    // a single drawing, so the mark and the sheet's charts go in as anchors
    // inside the same document rather than as parts of their own.
    {
        namespace WO = NativeOffice::Watermark::Ooxml;
        const QString relsHead =
            QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                           "<Relationships xmlns=\"http://schemas.openxmlformats.org/"
                           "package/2006/relationships\">");

        if (wm) zip.add("xl/media/nativeoffice-watermark.png", WO::pngBytes());

        for (int i = 0; i < nSheets; ++i) {
            for (int k = 0; k < draw[i].partNames.size(); ++k)
                zip.add(draw[i].partNames.at(k), draw[i].partXml.at(k));

            if (!hasDrawing(i)) continue;

            QString anchors, rels = relsHead;
            if (wm) {
                // Anchor just past the last used cell so the mark never lands
                // on top of the user's data.
                int col = 0, row = 0;
                if (i < static_cast<int>(sheets.size()))
                    for (const auto& c : sheets[i].cells) {
                        col = std::max(col, c.col);
                        row = std::max(row, c.row);
                    }
                anchors += WO::xlsxDrawingAnchorXml(QStringLiteral("rIdWmPic"),
                                                    QStringLiteral("rIdWmLink"),
                                                    col + 1, row + 2);
                rels += WO::imageRel(QStringLiteral("rIdWmPic"),
                                     QStringLiteral("../media/nativeoffice-watermark.png"))
                      + WO::hyperlinkRel(QStringLiteral("rIdWmLink"));
            }
            anchors += draw[i].anchors;
            for (int k = 0; k < draw[i].relIds.size(); ++k) {
                // A chart's own picture fills are parts of this package but not
                // relationships of this drawing: the chart references them, and
                // its own .rels (written below) is where they are declared.
                if (draw[i].relIds.at(k).isEmpty()) continue;
                rels += QString("<Relationship Id=\"%1\" Type=\"http://schemas."
                                "openxmlformats.org/officeDocument/2006/relationships/%2\" "
                                "Target=\"%3\"/>")
                            .arg(draw[i].relIds.at(k), draw[i].relTypes.at(k),
                                 draw[i].targets.at(k));
            }
            rels += QStringLiteral("</Relationships>");

            for (int k = 0; k < draw[i].chartRelsFor.size(); ++k)
                zip.add(draw[i].chartRelsFor.at(k), draw[i].chartRelsXml.at(k).toUtf8());

            zip.add(QString("xl/drawings/drawing%1.xml").arg(i + 1),
                (QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                                "<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/"
                                "drawingml/2006/spreadsheetDrawing\" "
                                "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
                                "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/"
                                "2006/relationships\">")
                 + anchors + QStringLiteral("</xdr:wsDr>")).toUtf8());
            zip.add(QString("xl/drawings/_rels/drawing%1.xml.rels").arg(i + 1), rels.toUtf8());

            QString sheetRels = relsHead
                + QString("<Relationship Id=\"rIdDraw\" "
                          "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
                          "relationships/drawing\" Target=\"../drawings/drawing%1.xml\"/>")
                      .arg(i + 1);
            if (wm) {
                // The repeated footer graphic: a VML part plus its image rel.
                zip.add(QString("xl/drawings/vmlDrawing%1.vml").arg(i + 1),
                        WO::xlsxFooterVml(QStringLiteral("rIdWmVmlImg")));
                zip.add(QString("xl/drawings/_rels/vmlDrawing%1.vml.rels").arg(i + 1),
                    (relsHead
                     + WO::imageRel(QStringLiteral("rIdWmVmlImg"),
                                    QStringLiteral("../media/nativeoffice-watermark.png"))
                     + QStringLiteral("</Relationships>")).toUtf8());
                sheetRels += QString("<Relationship Id=\"rIdWmVml\" "
                                     "Type=\"http://schemas.openxmlformats.org/officeDocument/"
                                     "2006/relationships/vmlDrawing\" "
                                     "Target=\"../drawings/vmlDrawing%1.vml\"/>").arg(i + 1);
            }
            sheetRels += QStringLiteral("</Relationships>");
            zip.add(QString("xl/worksheets/_rels/sheet%1.xml.rels").arg(i + 1),
                    sheetRels.toUtf8());
        }
    }

    // _rels/.rels
    zip.add("_rels/.rels",
        QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
        "</Relationships>"));

    // xl/workbook.xml
    {
        QString s =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
            "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets>";
        for (int i = 0; i < nSheets; ++i) {
            QString name = (i < (int)sheets.size()) ? sheets[i].name : QString("Sheet%1").arg(i + 1);
            if (name.isEmpty()) name = QString("Sheet%1").arg(i + 1);
            s += QString("<sheet name=\"%1\" sheetId=\"%2\" r:id=\"rId%2\"/>")
                     .arg(xmlEscape(name)).arg(i + 1);
        }
        s += "</sheets></workbook>";
        zip.add("xl/workbook.xml", s.toUtf8());
    }

    // xl/_rels/workbook.xml.rels
    {
        QString s =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
        for (int i = 0; i < nSheets; ++i)
            s += QString("<Relationship Id=\"rId%1\" "
                         "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" "
                         "Target=\"worksheets/sheet%1.xml\"/>").arg(i + 1);
        s += QString("<Relationship Id=\"rId%1\" "
                     "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
                     "Target=\"styles.xml\"/>").arg(nSheets + 1);
        s += "</Relationships>";
        zip.add("xl/_rels/workbook.xml.rels", s.toUtf8());
    }

    // xl/styles.xml (built from the populated style table)
    zip.add("xl/styles.xml", styles.toXml());

    // worksheets (pre-generated)
    for (int i = 0; i < nSheets; ++i)
        zip.add(QString("xl/worksheets/sheet%1.xml").arg(i + 1), sheetXml[i]);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(zip.finish());
    f.close();
    return true;
}

} // namespace NativeOffice
