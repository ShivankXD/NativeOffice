// ─────────────────────────────────────────────────────────────────────────────
// PptxImport.cpp — see PptxImport.h
// ─────────────────────────────────────────────────────────────────────────────
#include "PptxImport.h"

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QString>
#include <QStringView>
#include <QXmlStreamReader>
#include <QRectF>
#include <QColor>
#include <QImage>
#include <QtGlobal>
#include <algorithm>

namespace NativeOffice {
namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Raw DEFLATE inflater (RFC 1951) — self-contained, puff-style.
// ─────────────────────────────────────────────────────────────────────────────
class Inflater {
public:
    Inflater(const uchar* data, int len) : m_d(data), m_n(len) {}

    // Returns the decompressed bytes, or an empty array on malformed input.
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

    struct Huffman {
        short count[kMaxBits + 1] = {0};
        QVector<short> symbol;
    };

    const uchar* m_d;
    int          m_n;
    int          m_pos    = 0;
    int          m_bitbuf = 0;
    int          m_bitcnt = 0;
    QByteArray   m_out;
    bool         m_err    = false;

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
        m_bitbuf = 0; m_bitcnt = 0;               // discard to byte boundary
        if (m_pos + 4 > m_n) { m_err = true; return; }
        const int len = m_d[m_pos] | (m_d[m_pos + 1] << 8);
        m_pos += 4;                                // skip LEN + NLEN
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
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        m_err = true;
        return -1;
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
            if (m_err) return false;
            if (sym < 0) return false;
            if (sym == 256) return true;            // end of block
            if (sym < 256) {
                m_out.append(static_cast<char>(sym));
            } else {
                sym -= 257;
                if (sym >= 29) { m_err = true; return false; }
                const int len = lens[sym] + getBits(lext[sym]);
                sym = decode(dist);
                if (m_err || sym < 0 || sym >= 30) { m_err = true; return false; }
                const int dst = dists[sym] + getBits(dext[sym]);
                if (dst > m_out.size()) { m_err = true; return false; }
                int from = m_out.size() - dst;
                for (int i = 0; i < len; ++i)
                    m_out.append(m_out.at(from++));
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
        short dd[30];
        for (int i = 0; i < 30; ++i) dd[i] = 5;
        Huffman litlen, dist;
        construct(litlen, ll, 288);
        construct(dist, dd, 30);
        codes(litlen, dist);
    }

    void dynamicBlock() {
        static const short order[19] =
            {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
        const int hlit  = getBits(5) + 257;
        const int hdist = getBits(5) + 1;
        const int hclen = getBits(4) + 4;
        if (m_err || hlit > 286 || hdist > 30) { m_err = true; return; }

        short clLen[19] = {0};
        for (int i = 0; i < hclen; ++i) clLen[order[i]] = static_cast<short>(getBits(3));
        Huffman clCode;
        construct(clCode, clLen, 19);

        short lengths[286 + 30] = {0};
        int idx = 0;
        while (idx < hlit + hdist) {
            int sym = decode(clCode);
            if (m_err) return;
            if (sym < 16) {
                lengths[idx++] = static_cast<short>(sym);
            } else if (sym == 16) {
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
// Minimal ZIP reader (store + deflate)
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

        // Locate the End Of Central Directory record (scan back from the end).
        int eocd = -1;
        for (int i = n - 22; i >= 0 && i >= n - 22 - 65536; --i) {
            if (rd32(d + i) == 0x06054b50u) { eocd = i; break; }
        }
        if (eocd < 0) return false;

        const quint16 count   = rd16(d + eocd + 10);
        const quint32 cdSize  = rd32(d + eocd + 12);
        const quint32 cdStart = rd32(d + eocd + 16);
        if (cdStart + cdSize > static_cast<quint32>(n)) return false;

        quint32 p = cdStart;
        for (int i = 0; i < count; ++i) {
            if (p + 46 > static_cast<quint32>(n) || rd32(d + p) != 0x02014b50u) break;
            const quint16 method  = rd16(d + p + 10);
            const quint32 compSz  = rd32(d + p + 20);
            const quint16 fnLen   = rd16(d + p + 28);
            const quint16 exLen   = rd16(d + p + 30);
            const quint16 cmLen   = rd16(d + p + 32);
            const quint32 lhOff   = rd32(d + p + 42);
            QString name = QString::fromUtf8(
                reinterpret_cast<const char*>(d + p + 46), fnLen);
            name.replace('\\', '/');               // normalise separators

            // Jump to the local header to find where the data actually begins.
            if (lhOff + 30 <= static_cast<quint32>(n) && rd32(d + lhOff) == 0x04034b50u) {
                const quint16 lfn = rd16(d + lhOff + 26);
                const quint16 lex = rd16(d + lhOff + 28);
                const quint32 dataAt = lhOff + 30 + lfn + lex;
                if (dataAt + compSz <= static_cast<quint32>(n)) {
                    const QByteArray raw(reinterpret_cast<const char*>(d + dataAt),
                                         static_cast<int>(compSz));
                    QByteArray content = (method == 0) ? raw : inflateRaw(raw);
                    m_files.insert(name, content);
                }
            }
            p += 46 + fnLen + exLen + cmLen;
        }
        return !m_files.isEmpty();
    }

    bool has(const QString& name) const { return m_files.contains(name); }
    QByteArray file(const QString& name) const { return m_files.value(name); }
    QList<QString> names() const { return m_files.keys(); }

private:
    QByteArray              m_b;
    QHash<QString, QByteArray> m_files;
};

// ─────────────────────────────────────────────────────────────────────────────
// DrawingML → SlideData
// ─────────────────────────────────────────────────────────────────────────────
QString av(const QXmlStreamAttributes& a, const char* ln) {
    for (const auto& at : a)
        if (at.name() == QLatin1String(ln)) return at.value().toString();
    return QString();
}

ShapeKind shapeFromPrst(const QString& prst) {
    if (prst == "roundRect")   return ShapeKind::RoundedRect;
    if (prst == "ellipse")     return ShapeKind::Ellipse;
    if (prst == "triangle")    return ShapeKind::Triangle;
    if (prst == "rtTriangle")  return ShapeKind::RightTriangle;
    if (prst == "diamond")     return ShapeKind::Diamond;
    if (prst == "pentagon")    return ShapeKind::Pentagon;
    if (prst == "hexagon")     return ShapeKind::Hexagon;
    if (prst == "star5")       return ShapeKind::Star5;
    if (prst == "rightArrow")  return ShapeKind::Arrow;
    if (prst == "chevron")     return ShapeKind::Chevron;
    if (prst == "cloud")       return ShapeKind::Cloud;
    if (prst == "heart")       return ShapeKind::Heart;
    if (prst == "line" || prst == "straightConnector1") return ShapeKind::Line;
    return ShapeKind::Rectangle;
}

// Scale factors mapping EMU → our 960×540 scene.
struct Scale { double sx, sy; };

// One text run's character formatting.
struct RunFmt {
    double  sizePt { 18.0 };
    bool    bold   { false };
    bool    italic { false };
    bool    underline { false };
    QColor  color;
    QString face;
};
struct TextRun { QString text; RunFmt fmt; };
struct TextPara {
    QString          align { "left" };
    double           lineSpacingPct { 0 };      // 0 = leave to the renderer
    QVector<TextRun> runs;
};

// Parse one <p:sp> subtree. The reader is positioned on the <p:sp> start element.
// Appends a SlideItem to `out` if the shape carries text or a known geometry.
//
// Text is read run-by-run and paragraph-by-paragraph. Collapsing a shape onto
// the *first* run's formatting (as this once did) is wrong for any shape that
// mixes sizes or weights — a name over a lighter subtitle, say — which is most
// real decks.
void parseSp(QXmlStreamReader& xml, const Scale& sc, std::vector<SlideItem>& out) {
    SlideItem item;
    bool haveXfrm = false;
    double ox = 0, oy = 0, ex = 0, ey = 0, rot = 0;
    QString prst;
    QColor shapeFill;
    QString anchor = "t";
    double cornerAdj = 0.16667;          // roundRect default when no <a:gd adj>
    // <a:bodyPr><a:normAutofit fontScale="92500" lnSpcReduction="10000"/> —
    // PowerPoint's shrink-text-on-overflow, with the computed scale baked in at
    // save time. Bare <a:normAutofit/> means the text fit at 100%; only the
    // baked attributes matter. Ignoring them overflowed every dense slide of a
    // real 43-slide deck (42 of its bodies carry normAutofit, 18 with scales
    // down to 85%).
    double autofitScale = 1.0;           // multiplies every run's font size
    double autofitLnRed = 0.0;           // fraction shaved off line spacing


    QVector<TextPara> paras;
    RunFmt  curFmt;                 // formatting of the run being read
    QString curText;
    bool    inRun = false;

    // Transparency and outlines. A colour carries its alpha as a child element
    // rather than in the hex, and an outline is a separate colour again, so a
    // reader that takes only the fill hex draws every soft panel at full
    // strength and every outline-only shape as a solid disc. pendingClr points
    // at whichever colour an <a:alpha> that comes next belongs to.
    QColor  lineColor;
    double  lineWidth = 0.0;
    bool    shapeNoFill = false;
    QColor* pendingClr = nullptr;

    int depth = 1;                                  // we are inside <p:sp>
    QStringList path;                               // local-name stack below <p:sp>

    while (depth > 0 && !xml.atEnd()) {
        const auto tok = xml.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            const QString ln = xml.name().toString();
            const auto attrs = xml.attributes();
            path.push_back(ln);
            ++depth;

            // Only inside <a:xfrm>: <a:ext> is ALSO the name of the extension
            // container inside <a:extLst> (e.g. a hyperlink's colour extension),
            // which carries a uri and no cx/cy. Reading that as a size zeroed the
            // shape to 1x1 — its text then wrapped one character per line and
            // trailed off the bottom of the slide.
            if (ln == "off" && path.contains("xfrm")) {
                ox = av(attrs, "x").toDouble(); oy = av(attrs, "y").toDouble();
            }
            else if (ln == "ext" && path.contains("xfrm")) {
                ex = av(attrs, "cx").toDouble(); ey = av(attrs, "cy").toDouble(); haveXfrm = true;
            }
            else if (ln == "xfrm") { const QString r = av(attrs, "rot"); if (!r.isEmpty()) rot = r.toDouble() / 60000.0; }
            else if (ln == "prstGeom") { prst = av(attrs, "prst"); }
            // <a:avLst><a:gd name="adj" fmla="val 2128"/></a:avLst> — the shape's
            // own corner radius, as adj/100000 of its smaller side.
            else if (ln == "gd" && path.contains("avLst")) {
                if (av(attrs, "name") == "adj") {
                    const QString f = av(attrs, "fmla");        // "val 2128"
                    const QString v = f.section(' ', -1);
                    bool ok = false;
                    const double n = v.toDouble(&ok);
                    if (ok && n >= 0) cornerAdj = n / 100000.0;
                }
            }
            else if (ln == "bodyPr") { const QString a = av(attrs, "anchor"); if (!a.isEmpty()) anchor = a; }
            else if (ln == "normAutofit" && path.contains("bodyPr")) {
                bool ok = false;
                const double fs = av(attrs, "fontScale").toDouble(&ok);
                if (ok && fs > 0 && fs <= 100000) autofitScale = fs / 100000.0;
                const double lr = av(attrs, "lnSpcReduction").toDouble(&ok);
                if (ok && lr > 0 && lr < 100000) autofitLnRed = lr / 100000.0;
            }
            else if (ln == "p") { paras.push_back(TextPara{}); }
            else if (ln == "pPr" && !paras.isEmpty()) {
                const QString a = av(attrs, "algn");
                if (a == "ctr") paras.back().align = "center";
                else if (a == "r") paras.back().align = "right";
                else if (a == "just") paras.back().align = "justify";
            }
            else if (ln == "spcPct" && path.contains("lnSpc") && !paras.isEmpty()) {
                const double v = av(attrs, "val").toDouble();
                if (v > 0) paras.back().lineSpacingPct = v / 1000.0;   // 90000 → 90%
            }
            else if (ln == "r") { inRun = true; curFmt = RunFmt{}; curText.clear(); }
            else if (ln == "rPr" && inRun) {
                const QString sz = av(attrs, "sz"); if (!sz.isEmpty()) curFmt.sizePt = sz.toDouble() / 100.0;
                curFmt.bold      = (av(attrs, "b") == "1");
                curFmt.italic    = (av(attrs, "i") == "1");
                curFmt.underline = (av(attrs, "u") == "sng");
            }
            else if (ln == "latin" && inRun && path.contains("rPr")) {
                const QString tf = av(attrs, "typeface");
                if (!tf.isEmpty() && !tf.startsWith('+')) curFmt.face = tf;
            }
            // <a:br/> is a paragraph-level sibling of <a:r>, not a child of it.
            // Mark it with U+2028 so it survives to the HTML as a real break —
            // raw newlines inside <a:t> are just whitespace and must not.
            else if (ln == "br" && !paras.isEmpty()) {
                paras.back().runs.push_back({ QString(QChar(0x2028)), curFmt });
            }
            else if (ln == "ln" && path.contains("spPr")) {
                const QString w = av(attrs, "w");
                if (!w.isEmpty()) lineWidth = w.toDouble() / 9525.0;   // EMU to px
            }
            else if (ln == "noFill" && path.contains("spPr")) {
                if (path.contains("ln")) lineColor = QColor(Qt::transparent);
                else                     shapeNoFill = true;
            }
            else if (ln == "alpha") {
                // Belongs to the colour element it sits inside.
                const int v = av(attrs, "val").toInt();
                if (pendingClr && pendingClr->isValid())
                    pendingClr->setAlphaF(qBound(0.0, v / 100000.0, 1.0));
            }
            else if (ln == "srgbClr") {
                const QString val = av(attrs, "val");
                if (!val.isEmpty()) {
                    const QColor c("#" + val);
                    // Scope carefully: srgbClr shows up under rPr (text colour),
                    // spPr (shape fill) and ln (outline). endParaRPr carries one
                    // too and must not leak into a run.
                    if (inRun && path.contains("rPr")) {
                        if (!curFmt.color.isValid()) { curFmt.color = c; pendingClr = &curFmt.color; }
                    } else if (path.contains("spPr") && path.contains("ln")) {
                        if (!lineColor.isValid()) { lineColor = c; pendingClr = &lineColor; }
                    } else if (path.contains("spPr")) {
                        if (!shapeFill.isValid()) { shapeFill = c; pendingClr = &shapeFill; }
                    }
                }
            }
            else if (ln == "t") {
                QString s = xml.readElementText(QXmlStreamReader::IncludeChildElements);
                // A literal newline inside <a:t> IS a line break: PowerPoint and
                // WPS both render one, and decks use it for the blank line
                // between bullets. (Briefly treated as whitespace here, which
                // collapsed that spacing away — the reference renderers settle it.)
                s.replace(QChar(0x2028), '\n');    // normalise, then mark as a break
                curText += s;
                path.pop_back(); --depth;            // readElementText consumed </t>
            }
        } else if (tok == QXmlStreamReader::EndElement) {
            const QString ln = xml.name().toString();
            if (ln == "r") {
                if (!curText.isEmpty() && !paras.isEmpty())
                    paras.back().runs.push_back({ curText, curFmt });
                inRun = false;
                curText.clear();
            }
            if (!path.isEmpty()) path.pop_back();
            --depth;
        }
    }

    const QRectF rect(ox * sc.sx, oy * sc.sy,
                      std::max(1.0, ex * sc.sx), std::max(1.0, ey * sc.sy));
    item.rect     = rect;
    item.rotation = rot;
    item.vAlign   = (anchor == "ctr") ? 1 : (anchor == "b" ? 2 : 0);

    // Plain-text fallback (outline view) + emptiness test.
    QString plain;
    for (int i = 0; i < paras.size(); ++i) {
        for (const TextRun& r : paras[i].runs) plain += r.text;
        if (i + 1 < paras.size()) plain += '\n';
    }
    while (plain.endsWith('\n')) plain.chop(1);

    if (!plain.trimmed().isEmpty()) {
        // A text-bearing shape becomes a text box.
        item.type = SlideItemType::TextBox;
        item.text = plain;

        // sz is hundredths of a point. The scene is point-based (960 units across
        // a 13.33in slide = 72 units/inch), so a point maps to a scene unit via
        // sc.sy * 12700 (12700 EMU per point) — ~1.0 for a standard 16:9 deck,
        // and proportional for any other slide size.
        const double ptToUnit = sc.sy * 12700.0;

        QString html;
        bool first = true;
        for (const TextPara& p : paras) {
            QString pstyle = "margin:0;";
            // lnSpcReduction applies on top of the paragraph's own spacing (or
            // the 100% default), same percent-of-font-size model as spcPct.
            double lnPct = p.lineSpacingPct;
            if (autofitLnRed > 0) lnPct = (lnPct > 0 ? lnPct : 100.0) * (1.0 - autofitLnRed);
            if (lnPct > 0)
                pstyle += QString("line-height:%1%;").arg(lnPct, 0, 'f', 0);
            html += QString("<p align=\"%1\" style=\"%2\">").arg(p.align, pstyle);
            if (p.runs.isEmpty()) html += "<br/>";
            for (const TextRun& r : p.runs) {
                const double units = std::max(1.0, r.fmt.sizePt * autofitScale * ptToUnit);
                if (first) { item.fontSize = units; item.penColor =
                    r.fmt.color.isValid() ? r.fmt.color : QColor("#1C1E26"); first = false; }
                // px, not pt: Qt converts CSS pt to pixels at ~96dpi, which would
                // inflate every run by 96/72 and overflow its box.
                QString s = QString("font-size:%1px;").arg(units, 0, 'f', 1);
                if (!r.fmt.face.isEmpty()) s += QString("font-family:'%1';").arg(r.fmt.face);
                if (r.fmt.color.isValid())  s += QString("color:%1;").arg(r.fmt.color.name(QColor::HexRgb));
                if (r.fmt.bold)      s += "font-weight:bold;";
                if (r.fmt.italic)    s += "font-style:italic;";
                if (r.fmt.underline) s += "text-decoration:underline;";
                QString t = r.text.toHtmlEscaped();
                t.replace(QChar(0x2028), "\n");        // <a:br/> marker
                // A trailing break would otherwise be swallowed by the closing
                // </p>; keep it so the authored blank line survives.
                const bool trailingBreak = t.endsWith('\n');
                t.replace("\n", "<br/>");
                if (trailingBreak) t += "&nbsp;";
                html += QString("<span style=\"%1\">%2</span>").arg(s, t);
            }
            html += "</p>";
        }
        item.html = html;
        out.push_back(item);
    } else if (!prst.isEmpty() && haveXfrm) {
        // A geometry-only shape.
        item.type      = SlideItemType::Shape;
        item.shapeKind = shapeFromPrst(prst);
        item.cornerAdj = cornerAdj;
        // No fill means no fill. Falling back to a default here painted every
        // outline-only shape as a solid pale blue disc, including in decks this
        // app had exported itself moments earlier.
        item.fillColor = shapeNoFill ? QColor(Qt::transparent)
                       : shapeFill.isValid() ? shapeFill : QColor("#B7CFF4");
        if (lineColor.isValid() && lineColor.alpha() > 0 && lineWidth > 0.01) {
            item.penColor = lineColor;
            item.penWidth = lineWidth;
        } else {
            item.penColor = item.fillColor;
            item.penWidth = 0.0;
        }
        // A shape that blankets the whole slide is a backdrop, not a draggable
        // object — lock it so the slide stays fixed.
        if (rect.left() <= 24 && rect.top() <= 24 &&
            rect.width() >= 912 && rect.height() >= 513)
            item.locked = true;
        out.push_back(item);
    }
}

// Parse one <p:pic> subtree → an Image item, resolving the blip rel to bytes.
void parsePic(QXmlStreamReader& xml, const Scale& sc,
              const QHash<QString, QByteArray>& mediaByRid,
              std::vector<SlideItem>& out) {
    double ox = 0, oy = 0, ex = 0, ey = 0, rot = 0;
    bool haveXfrm = false;
    bool sawStretch = false, sawFillRect = false;
    int  xfrmDepth = -1;                 // >=0 while inside <a:xfrm> (see parseSp)
    QByteArray imgData;

    int depth = 1;
    while (depth > 0 && !xml.atEnd()) {
        const auto tok = xml.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            const QString ln = xml.name().toString();
            const auto attrs = xml.attributes();
            ++depth;
            if (ln == "xfrm") {
                xfrmDepth = depth;
                const QString r = av(attrs, "rot"); if (!r.isEmpty()) rot = r.toDouble() / 60000.0;
            }
            else if (ln == "off" && xfrmDepth > 0) { ox = av(attrs, "x").toDouble(); oy = av(attrs, "y").toDouble(); }
            else if (ln == "ext" && xfrmDepth > 0) { ex = av(attrs, "cx").toDouble(); ey = av(attrs, "cy").toDouble(); haveXfrm = true; }
            else if (ln == "stretch")  { sawStretch = true; }
            else if (ln == "fillRect") { sawFillRect = true; }
            else if (ln == "blip") { const QString rid = av(attrs, "embed");
                if (mediaByRid.contains(rid)) imgData = mediaByRid.value(rid); }
        } else if (tok == QXmlStreamReader::EndElement) {
            if (xfrmDepth == depth) xfrmDepth = -1;
            --depth;
        }
    }
    if (imgData.isEmpty() || !haveXfrm) return;
    SlideItem item;
    item.type      = SlideItemType::Image;
    item.imageData = imgData;
    QRectF r(ox * sc.sx, oy * sc.sy,
             std::max(1.0, ex * sc.sx), std::max(1.0, ey * sc.sy));

    // A picture always fills its frame, whether <a:stretch/> carries a fillRect
    // or not: PowerPoint's default fillRect is the shape's bounds. (An earlier
    // build fitted bare-stretch pictures to preserve their aspect, which only
    // shrank them inside their frames. WPS renders this deck's annotated_xray at
    // aspect 2.167 against a frame of 2.170 — it stretches, and so do we.)
    Q_UNUSED(sawStretch); Q_UNUSED(sawFillRect);
    item.rect      = r;
    item.rotation  = rot;
    // A picture that fills the whole slide is the slide's backdrop — lock it so
    // it behaves as a fixed background rather than a draggable object.
    if (item.rect.left() <= 24 && item.rect.top() <= 24 &&
        item.rect.width() >= 912 && item.rect.height() >= 513)
        item.locked = true;
    out.push_back(item);
}

// Parse a chart part (ppt/charts/chartN.xml) into a Chart SlideItem's data:
// every series with its authored name, fill colour and values, plus the shared
// category labels, whether data labels are shown, and whether there's a legend.
bool parseChartPart(const QByteArray& xmlBytes, SlideItem& item) {
    QXmlStreamReader xml(xmlBytes);
    bool haveKind = false;
    bool inSer = false, inCat = false, inVal = false, inTx = false;
    bool inTitle = false, inDLbls = false;
    int  serDepth = -1, depth = 0;

    std::vector<QString> labels;             // categories (from the first series)
    QString title;
    QString serName;
    QColor  serColor;
    std::vector<double> serValues;

    auto flushSeries = [&] {
        if (serValues.empty()) return;
        item.chartSeriesNames.push_back(serName);
        item.chartSeries.push_back(serValues);
        item.chartSeriesColors.push_back(serColor.isValid() ? serColor : QColor());
        serName.clear(); serColor = QColor(); serValues.clear();
    };

    while (!xml.atEnd()) {
        const auto tok = xml.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            ++depth;
            const QString ln = xml.name().toString();
            if (!haveKind) {
                if      (ln == "barChart")   { item.chartKind = ChartKind::Bar;  haveKind = true; }
                else if (ln == "lineChart")  { item.chartKind = ChartKind::Line; haveKind = true; }
                else if (ln == "pieChart" ||
                         ln == "doughnutChart") { item.chartKind = ChartKind::Pie; haveKind = true; }
                else if (ln == "areaChart")  { item.chartKind = ChartKind::Area; haveKind = true; }
            }
            if (ln == "ser")            { inSer = true; serDepth = depth; }
            else if (ln == "legend")    { item.chartShowLegend = true; }
            else if (ln == "title")     { inTitle = true; }
            else if (ln == "dLbls")     { inDLbls = true; }
            else if (ln == "showVal") {
                // Only the series-level dLbls decides data labels for us.
                if (inSer && inDLbls && av(xml.attributes(), "val") == "1")
                    item.chartShowValues = true;
            }
            else if (inSer && ln == "tx")  { inTx = true; }
            else if (inSer && ln == "cat") { inCat = true; }
            else if (inSer && ln == "val") { inVal = true; }
            else if (inSer && ln == "srgbClr" && !inDLbls && !serColor.isValid()) {
                // The series fill lives in <c:spPr>; dLbls text colour must not win.
                const QString v = av(xml.attributes(), "val");
                if (!v.isEmpty()) serColor = QColor("#" + v);
            }
            else if (ln == "v") {
                const QString v = xml.readElementText();
                --depth;                                  // readElementText ate </c:v>
                if (inTx)            serName = v;
                else if (inCat)      { if (labels.size() < 512) labels.push_back(v); }
                else if (inVal)      serValues.push_back(v.toDouble());
                else if (inTitle && title.isEmpty()) title = v;
            }
        } else if (tok == QXmlStreamReader::EndElement) {
            const QString ln = xml.name().toString();
            if (ln == "ser" && depth == serDepth) { flushSeries(); inSer = false; serDepth = -1; }
            else if (ln == "cat")   inCat = false;
            else if (ln == "val")   inVal = false;
            else if (ln == "tx")    inTx = false;
            else if (ln == "title") inTitle = false;
            else if (ln == "dLbls") inDLbls = false;
            --depth;
        }
    }
    flushSeries();
    if (item.chartSeries.empty()) return false;

    // Categories come from the first series' cache; later series repeat them, and
    // a multi-level cache repeats them per level. Keep one label per data point.
    const size_t n = item.chartSeries.front().size();
    if (labels.size() > n) labels.resize(n);
    while (labels.size() < n) labels.push_back(QString());

    item.type        = SlideItemType::Chart;
    item.chartTitle  = title;
    item.chartLabels = labels;
    item.chartValues = item.chartSeries.front();   // series 0 mirror
    return true;
}

// Parse a <p:graphicFrame> — the wrapper PowerPoint puts charts (and tables) in.
// Skipping it, as this once did, silently drops the slide's main content.
void parseGraphicFrame(QXmlStreamReader& xml, const Scale& sc,
                       const QHash<QString, QByteArray>& partByRid,
                       std::vector<SlideItem>& out) {
    double ox = 0, oy = 0, ex = 0, ey = 0;
    bool haveXfrm = false;
    int  xfrmDepth = -1;                 // <a:ext> is only a size inside <p:xfrm>
    QByteArray chartXml;

    int depth = 1;
    while (depth > 0 && !xml.atEnd()) {
        const auto tok = xml.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            const QString ln = xml.name().toString();
            const auto attrs = xml.attributes();
            ++depth;
            if (ln == "xfrm") { xfrmDepth = depth; }
            else if (ln == "off" && xfrmDepth > 0) { ox = av(attrs, "x").toDouble(); oy = av(attrs, "y").toDouble(); }
            else if (ln == "ext" && xfrmDepth > 0) { ex = av(attrs, "cx").toDouble(); ey = av(attrs, "cy").toDouble(); haveXfrm = true; }
            else if (ln == "chart") {
                const QString rid = av(attrs, "id");
                if (partByRid.contains(rid)) chartXml = partByRid.value(rid);
            }
        } else if (tok == QXmlStreamReader::EndElement) {
            if (xfrmDepth == depth) xfrmDepth = -1;
            --depth;
        }
    }
    if (chartXml.isEmpty() || !haveXfrm) return;
    SlideItem item;
    if (!parseChartPart(chartXml, item)) return;
    item.rect = QRectF(ox * sc.sx, oy * sc.sy,
                       std::max(1.0, ex * sc.sx), std::max(1.0, ey * sc.sy));
    out.push_back(item);
}

// Parse a single slide part (and its already-resolved media map) into SlideData.
SlideData parseSlide(const QByteArray& xmlBytes, const Scale& sc,
                     const QHash<QString, QByteArray>& mediaByRid) {
    SlideData slide;
    slide.background = Qt::white;

    QXmlStreamReader xml(xmlBytes);
    bool inBg = false;
    while (!xml.atEnd()) {
        const auto tok = xml.readNext();
        if (tok != QXmlStreamReader::StartElement) {
            if (tok == QXmlStreamReader::EndElement && xml.name() == QLatin1String("bg"))
                inBg = false;
            continue;
        }
        const QString ln = xml.name().toString();
        if (ln == "bg") { inBg = true; }
        else if (inBg && ln == "srgbClr") {
            const QString val = av(xml.attributes(), "val");
            if (!val.isEmpty()) slide.background = QColor("#" + val);
        }
        else if (ln == "sp")  { parseSp(xml, sc, slide.items); }
        else if (ln == "pic") { parsePic(xml, sc, mediaByRid, slide.items); }
        else if (ln == "graphicFrame") { parseGraphicFrame(xml, sc, mediaByRid, slide.items); }
    }
    return slide;
}

// Read a slide's .rels part → map { relationship-id → embedded image bytes }.
QHash<QString, QByteArray> mediaForSlide(const ZipReader& zip, const QString& slidePath) {
    QHash<QString, QByteArray> result;
    const int slash = slidePath.lastIndexOf('/');
    const QString dir  = slidePath.left(slash);              // ppt/slides
    const QString file = slidePath.mid(slash + 1);           // slideN.xml
    const QString relsPath = dir + "/_rels/" + file + ".rels";
    if (!zip.has(relsPath)) return result;

    QXmlStreamReader xml(zip.file(relsPath));
    while (!xml.atEnd()) {
        if (xml.readNext() == QXmlStreamReader::StartElement &&
            xml.name() == QLatin1String("Relationship")) {
            const auto a = xml.attributes();
            const QString id = av(a, "Id");
            QString target = av(a, "Target");
            if (id.isEmpty() || target.isEmpty()) continue;
            // A target may be absolute ("/ppt/charts/chart1.xml" — which charts
            // use) or relative to the slide directory ("../media/image1.png").
            QString full;
            if (target.startsWith('/')) {
                full = target.mid(1);
            } else {
                QString resolved = target;
                QString base = dir;                           // ppt/slides
                while (resolved.startsWith("../")) {
                    resolved = resolved.mid(3);
                    base = base.left(base.lastIndexOf('/'));
                }
                full = base.isEmpty() ? resolved : base + "/" + resolved;
            }
            if (zip.has(full)) result.insert(id, zip.file(full));
        }
    }
    return result;
}

// Determine slide parts in presentation order; fall back to numeric sort.
QList<QString> orderedSlideParts(const ZipReader& zip) {
    QList<QString> result;

    // Map rId → slide part via ppt/_rels/presentation.xml.rels
    QHash<QString, QString> ridToPart;
    if (zip.has("ppt/_rels/presentation.xml.rels")) {
        QXmlStreamReader xml(zip.file("ppt/_rels/presentation.xml.rels"));
        while (!xml.atEnd()) {
            if (xml.readNext() == QXmlStreamReader::StartElement &&
                xml.name() == QLatin1String("Relationship")) {
                const auto a = xml.attributes();
                QString target = av(a, "Target");
                if (target.contains("slides/slide")) {
                    if (!target.startsWith("ppt/")) target = "ppt/" + target;
                    ridToPart.insert(av(a, "Id"), target);
                }
            }
        }
    }
    // Read slide id list order from presentation.xml
    if (zip.has("ppt/presentation.xml") && !ridToPart.isEmpty()) {
        // <p:sldId> carries BOTH a plain "id" (a numeric slide id) and the
        // relationship "r:id" we actually want. av() matches by local name, and
        // r:id's local name is also "id" — so av(attrs,"id") returns the plain
        // id and the rId lookup never hits (order then falls back to filename
        // sort). Resolve r:id by its relationships namespace instead.
        static const QString kRelNs = QStringLiteral(
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships");
        QXmlStreamReader xml(zip.file("ppt/presentation.xml"));
        while (!xml.atEnd()) {
            if (xml.readNext() == QXmlStreamReader::StartElement &&
                xml.name() == QLatin1String("sldId")) {
                const QString rid =
                    xml.attributes().value(kRelNs, QStringLiteral("id")).toString();
                if (ridToPart.contains(rid)) result.append(ridToPart.value(rid));
            }
        }
    }
    if (!result.isEmpty()) return result;

    // Fallback: every ppt/slides/slideN.xml, sorted by N.
    for (const QString& name : zip.names())
        if (name.startsWith("ppt/slides/slide") && name.endsWith(".xml") &&
            !name.contains("_rels"))
            result.append(name);
    std::sort(result.begin(), result.end(), [](const QString& a, const QString& b) {
        auto num = [](const QString& s) {
            int i = s.lastIndexOf("slide") + 5, j = i;   // last "slide", not "slides"
            while (j < s.size() && s[j].isDigit()) ++j;
            return s.mid(i, j - i).toInt();
        };
        return num(a) < num(b);
    });
    return result;
}

Scale slideScale(const ZipReader& zip) {
    double cx = 12192000.0, cy = 6858000.0;        // default 16:9 widescreen
    if (zip.has("ppt/presentation.xml")) {
        QXmlStreamReader xml(zip.file("ppt/presentation.xml"));
        while (!xml.atEnd()) {
            if (xml.readNext() == QXmlStreamReader::StartElement &&
                xml.name() == QLatin1String("sldSz")) {
                const auto a = xml.attributes();
                const double w = av(a, "cx").toDouble();
                const double h = av(a, "cy").toDouble();
                if (w > 0) cx = w;
                if (h > 0) cy = h;
                break;
            }
        }
    }
    return { 960.0 / cx, 540.0 / cy };
}

} // namespace

bool importPptx(const QString& path, std::vector<SlideData>& outSlides) {
    outSlides.clear();

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray bytes = f.readAll();
    f.close();

    ZipReader zip;
    if (!zip.open(bytes)) return false;

    const Scale sc = slideScale(zip);
    const QList<QString> parts = orderedSlideParts(zip);
    for (const QString& part : parts) {
        if (!zip.has(part)) continue;
        const auto media = mediaForSlide(zip, part);
        outSlides.push_back(parseSlide(zip.file(part), sc, media));
    }
    return !outSlides.empty();
}

} // namespace NativeOffice
