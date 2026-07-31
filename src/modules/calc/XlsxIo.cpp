// ─────────────────────────────────────────────────────────────────────────────
// XlsxIo.cpp — see XlsxIo.h
// Self-contained .xlsx reader/writer. The DEFLATE inflater + ZIP reader mirror
// the proven implementation used by the Impress .pptx importer; export uses a
// STORED (uncompressed) ZIP so no compressor is needed (Excel reads it fine).
// ─────────────────────────────────────────────────────────────────────────────
#include "XlsxIo.h"

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
                }
            }
            p += 46 + fnLen + exLen + cmLen;
        }
        return !m_files.isEmpty();
    }
    bool has(const QString& name) const { return m_files.contains(name); }
    QByteArray file(const QString& name) const { return m_files.value(name); }
private:
    QByteArray m_b;
    QHash<QString, QByteArray> m_files;
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

class ZipWriter {
public:
    void add(const QString& name, const QByteArray& data) {
        const quint32 crc = crc32(data);
        const quint32 off = static_cast<quint32>(m_out.size());
        const QByteArray nm = name.toUtf8();
        putU32(0x04034b50); putU16(20); putU16(0); putU16(0);
        putU16(0); putU16(0);                          // mod time/date
        putU32(crc); putU32(data.size()); putU32(data.size());
        putU16(nm.size()); putU16(0);
        m_out.append(nm); m_out.append(data);
        m_dir.push_back({name, crc, static_cast<quint32>(data.size()), off});
    }
    QByteArray finish() {
        const quint32 cdStart = static_cast<quint32>(m_out.size());
        for (const Entry& e : m_dir) {
            const QByteArray nm = e.name.toUtf8();
            putU32(0x02014b50); putU16(20); putU16(20); putU16(0); putU16(0);
            putU16(0); putU16(0);
            putU32(e.crc); putU32(e.size); putU32(e.size);
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
    struct Entry { QString name; quint32 crc; quint32 size; quint32 offset; };
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

void parseWorksheet(const QByteArray& xml, const QStringList& shared,
                    const StyleSheetData& styles, XlsxSheet& sheet) {
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        if (r.readNext() != QXmlStreamReader::StartElement) continue;
        if (r.name() == u"mergeCell") {
            const QString ref = r.attributes().value("ref").toString();
            if (!ref.isEmpty()) sheet.merges.push_back(ref);
            continue;
        }
        if (r.name() == u"col") {
            if (attrS(r, "customWidth") == "1") {
                const int mn = attrS(r, "min").toInt();
                const int mx = std::min(attrS(r, "max").toInt(), 256);
                const double w = attrS(r, "width").toDouble();
                if (mn >= 1 && w > 0)
                    for (int c = mn; c <= mx; ++c)
                        sheet.colWidths.push_back({c - 1, colWidthToPx(w)});
            }
            continue;
        }
        if (r.name() == u"row") {
            if (attrS(r, "customHeight") == "1") {
                const int rr = attrS(r, "r").toInt();
                const double ht = attrS(r, "ht").toDouble();
                if (rr >= 1 && ht > 0) sheet.rowHeights.push_back({rr - 1, rowHtToPx(ht)});
            }
            continue;   // cells are emitted as separate <c> StartElements
        }
        if (r.name() != u"c") continue;

        const QString ref  = r.attributes().value("r").toString();
        const QString type = r.attributes().value("t").toString();
        const QString sAttr = r.attributes().value("s").toString();
        QString formula, value, inlineStr;

        while (!(r.tokenType() == QXmlStreamReader::EndElement && r.name() == u"c")) {
            if (r.atEnd()) break;
            r.readNext();
            if (r.tokenType() == QXmlStreamReader::StartElement) {
                if (r.name() == u"f")       formula   = r.readElementText(QXmlStreamReader::IncludeChildElements);
                else if (r.name() == u"v")  value     = r.readElementText(QXmlStreamReader::IncludeChildElements);
                else if (r.name() == u"is") inlineStr = r.readElementText(QXmlStreamReader::IncludeChildElements);
            }
        }

        QString content;
        if (!formula.isEmpty()) {
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
            sheet.cells.push_back({col, row, content, fmt});
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
    if (zip.has("xl/styles.xml"))
        styles = parseStyles(zip.file("xl/styles.xml"));

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

    // workbook: ordered sheets (name + rId).
    struct SheetRef { QString name; QString rid; };
    QVector<SheetRef> sheetRefs;
    if (zip.has("xl/workbook.xml")) {
        QXmlStreamReader r(zip.file("xl/workbook.xml"));
        while (!r.atEnd()) {
            if (r.readNext() == QXmlStreamReader::StartElement && r.name() == u"sheet") {
                SheetRef sr;
                for (const auto& a : r.attributes()) {
                    if (a.name() == u"name") sr.name = a.value().toString();
                    else if (a.name() == u"id") sr.rid = a.value().toString();  // r:id
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
        parseWorksheet(zip.file(part), shared, styles, sheet);
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

QByteArray buildWorksheet(const XlsxSheet& sheet, StyleTable& styles) {
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
    // The watermark drawing is referenced last: the schema requires <drawing>
    // after <mergeCells>, and Excel rejects the sheet if the order is wrong.
    if (NativeOffice::Watermark::enabledForExport())
        xml += "<drawing r:id=\"rIdWmDraw\"/>";

    xml += "</worksheet>";
    return xml.toUtf8();
}

} // namespace

bool exportXlsx(const QString& path, const std::vector<XlsxSheet>& sheets) {
    const int nSheets = std::max<int>(1, static_cast<int>(sheets.size()));
    ZipWriter zip;

    // Generate worksheets first so the style table is fully populated.
    StyleTable styles;
    QVector<QByteArray> sheetXml;
    for (int i = 0; i < nSheets; ++i) {
        const XlsxSheet empty;
        const XlsxSheet& sh = (i < (int)sheets.size()) ? sheets[i] : empty;
        sheetXml.push_back(buildWorksheet(sh, styles));
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
        if (NativeOffice::Watermark::enabledForExport()) {
            s += "<Default Extension=\"png\" ContentType=\"image/png\"/>";
            for (int i = 0; i < nSheets; ++i)
                s += QString("<Override PartName=\"/xl/drawings/drawing%1.xml\" "
                             "ContentType=\"application/vnd.openxmlformats-officedocument.drawing+xml\"/>")
                         .arg(i + 1);
        }
        s += "</Types>";
        zip.add("[Content_Types].xml", s.toUtf8());
    }

    // ── Export watermark ────────────────────────────────────────────────────
    // One drawing part per sheet, each anchored past the used range so it sits
    // clear of the data, carrying the hyperlink on the picture.
    if (NativeOffice::Watermark::enabledForExport()) {
        namespace WO = NativeOffice::Watermark::Ooxml;
        zip.add("xl/media/nativeoffice-watermark.png", WO::pngBytes());

        for (int i = 0; i < nSheets; ++i) {
            // Anchor just past the last used cell so the mark never lands on
            // top of the user's data.
            int col = 0, row = 0;
            if (i < static_cast<int>(sheets.size())) {
                for (const auto& c : sheets[i].cells) {
                    col = std::max(col, c.col);
                    row = std::max(row, c.row);
                }
            }
            zip.add(QString("xl/drawings/drawing%1.xml").arg(i + 1),
                    WO::xlsxDrawingXml(QStringLiteral("rIdWmPic"),
                                       QStringLiteral("rIdWmLink"),
                                       col + 1, row + 2));
            zip.add(QString("xl/drawings/_rels/drawing%1.xml.rels").arg(i + 1),
                (QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                                "<Relationships xmlns=\"http://schemas.openxmlformats.org/"
                                "package/2006/relationships\">")
                 + WO::imageRel(QStringLiteral("rIdWmPic"),
                                QStringLiteral("../media/nativeoffice-watermark.png"))
                 + WO::hyperlinkRel(QStringLiteral("rIdWmLink"))
                 + QStringLiteral("</Relationships>")).toUtf8());

            zip.add(QString("xl/worksheets/_rels/sheet%1.xml.rels").arg(i + 1),
                QString("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                        "<Relationships xmlns=\"http://schemas.openxmlformats.org/"
                        "package/2006/relationships\">"
                        "<Relationship Id=\"rIdWmDraw\" "
                        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
                        "relationships/drawing\" Target=\"../drawings/drawing%1.xml\"/>"
                        "</Relationships>").arg(i + 1).toUtf8());
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
