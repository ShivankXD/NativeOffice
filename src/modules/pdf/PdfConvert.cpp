// ─────────────────────────────────────────────────────────────────────────────
// PdfConvert.cpp — see PdfConvert.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfConvert.h"
#include "PdfRenderer.h"

#include <QtCore/private/qzipwriter_p.h>

#include <QBuffer>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>

#include "core/watermark/Watermark.h"
#include "core/watermark/WatermarkOoxml.h"
#include <QRegularExpression>

#include <cmath>

#include <algorithm>
#include <vector>

namespace NativeOffice::Pdf {

namespace {

std::unique_ptr<Renderer> openOrError(const QString& in, OpResult& err) {
    RenderOpenStatus st{};
    auto r = Renderer::open(in, {}, st);
    if (!r) {
        err.ok = false;
        err.message = st == RenderOpenStatus::PasswordRequired
            ? "This PDF is password-protected — open it first, then convert."
            : "The PDF could not be read for conversion.";
    }
    return r;
}

QString xmlEscape(const QString& s) {
    QString o;
    o.reserve(s.size());
    for (QChar c : s) {
        switch (c.unicode()) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            case '\'': o += "&apos;"; break;
            default:
                // Strip control chars that are illegal in XML 1.0.
                if (c.unicode() >= 0x20 || c == '\t' || c == '\n') o += c;
        }
    }
    return o;
}

// A page's text as visual lines. PDFium's own extraction preserves spacing
// and line breaks (and column gaps as multiple spaces), which is far more
// robust than re-clustering per-character boxes — glyph boxes vary vertically
// between cap and descender letters and drop space glyphs entirely.
QStringList pageTextLines(Renderer& r, int page) {
    QString text = r.pageText(page);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace('\r', '\n');
    return text.split('\n');
}

QByteArray encodeImage(const QImage& img, const char* fmt, int quality = -1) {
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, fmt, quality);
    return bytes;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// PDF → TXT
// ─────────────────────────────────────────────────────────────────────────────

OpResult toTxt(const QString& in, const QString& out) {
    OpResult err{ true, {} };
    auto r = openOrError(in, err);
    if (!r) return err;

    QFile f(out);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return { false, "Could not write the text file." };
    for (int i = 0; i < r->pageCount(); ++i) {
        f.write(r->pageText(i).toUtf8());
        if (i + 1 < r->pageCount()) f.write("\n\f\n");   // form-feed between pages
    }
    f.close();
    return { true, {} };
}

// ─────────────────────────────────────────────────────────────────────────────
// PDF → Picture
// ─────────────────────────────────────────────────────────────────────────────

OpResult toImages(const QString& in, const QString& outDir, const QString& baseName,
                  RasterFormat fmt, int dpi, QStringList* writtenPaths) {
    OpResult err{ true, {} };
    auto r = openOrError(in, err);
    if (!r) return err;
    QDir().mkpath(outDir);

    const qreal scale = std::clamp(dpi, 36, 600) / 72.0;
    const char* ext = fmt == RasterFormat::Png ? "png" : "jpg";
    int ok = 0;
    for (int i = 0; i < r->pageCount(); ++i) {
        const QImage img = r->renderPage(i, scale);
        if (img.isNull()) continue;
        const QString path = QStringLiteral("%1/%2-%3.%4")
            .arg(outDir, baseName).arg(i + 1).arg(ext);
        if (img.save(path, fmt == RasterFormat::Png ? "PNG" : "JPEG",
                     fmt == RasterFormat::Png ? -1 : 90)) {
            if (writtenPaths) *writtenPaths << path;
            ++ok;
        }
    }
    return ok > 0 ? OpResult{ true, {} }
                  : OpResult{ false, "No pages could be rendered." };
}

// ─────────────────────────────────────────────────────────────────────────────
// PDF → Word (.docx)
//
// Reconstructs an editable, formatted document rather than dumping plain text:
// glyphs are grouped into visual lines, each line split into runs that share a
// style (size / bold / italic / colour), word gaps re-inserted from glyph
// spacing, and paragraphs positioned with left indent + vertical spacing that
// mirror the page. Embedded images are placed inline at their own position and
// size, interleaved with the text in reading order. The Word page is sized to
// the PDF page. It is not pixel-perfect — it is a faithful, editable stand-in.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

int toEmu(double pt)   { return int(pt * 12700.0 + 0.5); }   // 914400 EMU / inch
int toTwips(double pt) { return int(pt * 20.0 + 0.5); }      // 1440 twips / inch

struct StyledRun { QString text; int szHalfPt; QColor color; bool bold; bool italic; };

struct DocLine {
    double top = 1e9, bottom = -1e9, left = 1e9, right = -1e9;
    QVector<StyledRun> runs;
};

// One emittable block (a text line or an image), carrying the geometry needed
// to place it: paragraphs get a left indent / alignment and vertical spacing.
struct Block {
    double top, bottom, left, right;
    QString inner;      // run/drawing XML, wrapped in <w:p> at emit time
};

// Cluster a page's glyphs into visual lines, then each line into styled runs.
QVector<DocLine> buildLines(std::vector<Renderer::CharBox> cs) {
    std::vector<Renderer::CharBox> g;
    g.reserve(cs.size());
    for (auto& c : cs) if (c.box.height() > 0.5) g.push_back(c);
    std::sort(g.begin(), g.end(), [](const Renderer::CharBox& a, const Renderer::CharBox& b) {
        const double ay = a.box.center().y(), by = b.box.center().y();
        if (std::abs(ay - by) > 0.5) return ay < by;
        return a.box.left() < b.box.left();
    });

    QVector<QVector<Renderer::CharBox>> lineGlyphs;
    double curTop = 0, curBot = 0;
    for (auto& c : g) {
        const double cy = c.box.center().y();
        if (!lineGlyphs.isEmpty() && cy >= curTop && cy <= curBot) {
            lineGlyphs.back().push_back(c);
            curTop = std::min(curTop, c.box.top());
            curBot = std::max(curBot, c.box.bottom());
        } else {
            lineGlyphs.append({ c });
            curTop = c.box.top();
            curBot = c.box.bottom();
        }
    }

    auto sizeHalfPt = [](const Renderer::CharBox& c) {
        const double s = c.fontSize > 0 ? c.fontSize : c.box.height();
        return std::clamp(int(s * 2.0 + 0.5), 2, 800);
    };

    QVector<DocLine> lines;
    for (auto& lc : lineGlyphs) {
        std::sort(lc.begin(), lc.end(), [](const Renderer::CharBox& a, const Renderer::CharBox& b) {
            return a.box.left() < b.box.left();
        });
        DocLine dl;
        StyledRun cur;
        bool have = false;
        double prevRight = 0;
        for (const auto& c : lc) {
            const int   sz  = sizeHalfPt(c);
            const QColor col = c.color.isValid() ? c.color : QColor();
            const bool sameStyle = have && cur.szHalfPt == sz && cur.bold == c.bold
                                 && cur.italic == c.italic && cur.color == col;
            // Re-insert a space where glyphs are visually separated (positioned
            // text often carries no space glyph between words). 0.35x the font
            // size keeps a spaced-out heading ("SHIVANK") from being split into
            // "SH IVAN K"; the trade-off is that a few very tightly-set words in
            // secondary text may join — acceptable, and far better than a broken
            // name. (Per-line adaptive thresholds do worse on mixed-size lines.)
            if (have && c.box.left() - prevRight > 0.35 * (c.fontSize > 0 ? c.fontSize : c.box.height())
                && !cur.text.endsWith(' '))
                cur.text += ' ';
            if (!sameStyle) {
                if (have && !cur.text.trimmed().isEmpty()) dl.runs.push_back(cur);
                else if (have && !cur.text.isEmpty() && !dl.runs.isEmpty())
                    dl.runs.last().text += cur.text;   // fold a pure-space run into the previous
                cur = StyledRun{ QString(), sz, col, c.bold, c.italic };
                have = true;
            }
            cur.text += c.ch;
            prevRight = c.box.right();
            dl.top = std::min(dl.top, c.box.top());
            dl.bottom = std::max(dl.bottom, c.box.bottom());
            dl.left = std::min(dl.left, c.box.left());
            dl.right = std::max(dl.right, c.box.right());
        }
        if (have && !cur.text.isEmpty()) dl.runs.push_back(cur);
        if (!dl.runs.isEmpty()) lines.push_back(dl);
    }
    return lines;
}

} // namespace

OpResult toDocx(const QString& in, const QString& out) {
    OpResult err{ true, {} };
    auto r = openOrError(in, err);
    if (!r) return err;

    // Page dimensions for the section come from the first page.
    const QSizeF page0 = r->pageCount() > 0 ? r->pageSizePt(0) : QSizeF(612, 792);

    auto runXml = [](const StyledRun& rn) -> QString {
        QString rpr = "<w:rPr>";
        if (rn.bold)   rpr += "<w:b/>";
        if (rn.italic) rpr += "<w:i/>";
        if (rn.color.isValid())
            rpr += "<w:color w:val=\"" + rn.color.name(QColor::HexRgb).mid(1).toUpper() + "\"/>";
        rpr += "<w:sz w:val=\"" + QString::number(rn.szHalfPt) + "\"/>"
               "<w:szCs w:val=\"" + QString::number(rn.szHalfPt) + "\"/></w:rPr>";
        return "<w:r>" + rpr + "<w:t xml:space=\"preserve\">" + xmlEscape(rn.text) + "</w:t></w:r>";
    };

    // Alignment from a block's horizontal position within the page.
    auto alignOf = [](double left, double right, double pageW) -> QString {
        if (pageW <= 0) return "left";
        const double lg = left, rg = pageW - right;
        if (rg > 0.02 * pageW && std::abs(lg - rg) < 0.12 * pageW && lg > 0.12 * pageW) return "center";
        if (rg < 0.10 * pageW && lg > 0.20 * pageW) return "right";
        return "left";
    };

    QString body;
    QString imageRels;
    QVector<QPair<QString, QByteArray>> media;   // (part name, bytes)
    int relId = 0;       // image relationship ids (rId1..)
    int drawId = 1;      // unique drawing/docPr ids

    for (int p = 0; p < r->pageCount(); ++p) {
        const QSizeF ps = r->pageSizePt(p);
        const double pageW = ps.width();

        std::vector<Block> blocks;
        for (const DocLine& dl : buildLines(r->pageChars(p))) {
            QString inner;
            for (const StyledRun& rn : dl.runs) inner += runXml(rn);
            blocks.push_back({ dl.top, dl.bottom, dl.left, dl.right, inner });
        }
        for (const Renderer::PlacedImage& pi : r->pagePlacedImages(p)) {
            if (pi.img.isNull() || pi.box.width() <= 0 || pi.box.height() <= 0) continue;
            ++relId;
            const QString partName = QStringLiteral("image%1.png").arg(relId);
            media.push_back({ partName, encodeImage(pi.img, "PNG") });
            imageRels += QStringLiteral(
                "<Relationship Id=\"rId%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"media/%2\"/>")
                .arg(relId).arg(partName);
            const int cx = toEmu(pi.box.width()), cy = toEmu(pi.box.height());
            const int id = drawId++;
            const QString draw = QStringLiteral(
                "<w:r><w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">"
                "<wp:extent cx=\"%1\" cy=\"%2\"/><wp:docPr id=\"%3\" name=\"Image%3\"/>"
                "<a:graphic xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">"
                "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
                "<pic:pic xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
                "<pic:nvPicPr><pic:cNvPr id=\"%3\" name=\"Image%3\"/><pic:cNvPicPr/></pic:nvPicPr>"
                "<pic:blipFill><a:blip r:embed=\"rId%4\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
                "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%1\" cy=\"%2\"/></a:xfrm>"
                "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr></pic:pic>"
                "</a:graphicData></a:graphic></wp:inline></w:drawing></w:r>")
                .arg(cx).arg(cy).arg(id).arg(relId);
            blocks.push_back({ pi.box.top(), pi.box.bottom(), pi.box.left(), pi.box.right(), draw });
        }

        std::sort(blocks.begin(), blocks.end(), [](const Block& a, const Block& b) {
            if (std::abs(a.top - b.top) > 1.0) return a.top < b.top;
            return a.left < b.left;
        });

        double prevBottom = 0;   // bottom of the previous block, for gap detection
        bool   firstBlock = true;
        for (const Block& blk : blocks) {
            const QString align = alignOf(blk.left, blk.right, pageW);
            // Add spacing only for a real vertical gap (a blank line / section
            // break), and keep it modest and capped. Reproducing the absolute
            // page position of every line — as an earlier version did — inflated
            // a one-page résumé to 21 Word pages; flowing text with occasional
            // gaps is both compact and what an editable conversion wants.
            const double lineH = std::max(1.0, blk.bottom - blk.top);
            const double gap   = blk.top - prevBottom;
            double before = 0.0;
            if (!firstBlock && gap > 1.2 * lineH) before = std::min(gap - lineH, 24.0);
            firstBlock = false;
            QString pr = "<w:pPr><w:spacing w:after=\"0\"";
            if (before > 0) pr += " w:before=\"" + QString::number(toTwips(before)) + "\"";
            pr += " w:line=\"240\" w:lineRule=\"auto\"/>";
            if (align == "center") pr += "<w:jc w:val=\"center\"/>";
            else if (align == "right") pr += "<w:jc w:val=\"right\"/>";
            // No absolute left indent: mapping a glyph's page-x to an indent
            // squeezed right-hand column text into a sliver that wrapped every
            // word (a one-page résumé blew up to 20 pages). Flowing text inside
            // normal margins is compact and editable; alignment is still kept.
            pr += "</w:pPr>";
            body += "<w:p>" + pr + blk.inner + "</w:p>";
            prevBottom = blk.bottom;
        }

        // No forced page break between PDF pages: an editable Word document
        // should reflow, and a hard break here linearises worse (and, in this
        // app's own importer, a lone break-only paragraph paginates wildly).
    }

    // Converting a PDF to Word produces a document like any other, so it is
    // marked like any other. Without this, "PDF to Word" would have been a way
    // for a free account to get an unmarked .docx out of the app.
    const bool wantMark = NativeOffice::Watermark::enabledForExport();
    const QString wmFooterRef = wantMark
        ? QStringLiteral("<w:footerReference w:type=\"default\" r:id=\"rIdWmFtr\"/>")
        : QString();

    const QString documentXml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
        "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" "
        "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
        "xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
        "<w:body>" + body +
        "<w:sectPr>" + wmFooterRef +
        "<w:pgSz w:w=\"" + QString::number(toTwips(page0.width())) +
        "\" w:h=\"" + QString::number(toTwips(page0.height())) + "\"/>"
        "<w:pgMar w:top=\"720\" w:right=\"720\" w:bottom=\"720\" w:left=\"720\" "
        "w:header=\"0\" w:footer=\"0\" w:gutter=\"0\"/></w:sectPr></w:body></w:document>";

    // Document defaults. Without a styles part, Word-family readers apply their
    // own built-in Normal style — here that meant a large paragraph space-after
    // and 1.15 line spacing, which inflated a ~5-page conversion to 20 pages.
    // Pin space-after to 0 and single line spacing, with an 11pt default size.
    const QString stylesXml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:docDefaults>"
        "<w:rPrDefault><w:rPr><w:sz w:val=\"22\"/><w:szCs w:val=\"22\"/></w:rPr></w:rPrDefault>"
        "<w:pPrDefault><w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/></w:pPr></w:pPrDefault>"
        "</w:docDefaults></w:styles>";

    const QString contentTypes =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Default Extension=\"png\" ContentType=\"image/png\"/>"
        "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
        + (wantMark ? QStringLiteral("<Override PartName=\"/word/footer9001.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml\"/>") : QString())
        + "</Types>";
    const QString rootRels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
        "</Relationships>";
    const QString documentRels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rIdSty\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
        + imageRels
        + (wantMark ? QStringLiteral("<Relationship Id=\"rIdWmFtr\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer\" Target=\"footer9001.xml\"/>") : QString())
        + "</Relationships>";

    QZipWriter zip(out);
    if (zip.status() != QZipWriter::NoError)
        return { false, "Could not create the Word file." };
    zip.addFile("[Content_Types].xml", contentTypes.toUtf8());
    zip.addFile("_rels/.rels", rootRels.toUtf8());
    zip.addFile("word/document.xml", documentXml.toUtf8());
    zip.addFile("word/styles.xml", stylesXml.toUtf8());
    zip.addFile("word/_rels/document.xml.rels", documentRels.toUtf8());
    for (const auto& m : media)
        zip.addFile("word/media/" + m.first, m.second);
    if (wantMark) {
        namespace WO = NativeOffice::Watermark::Ooxml;
        zip.addFile("word/media/nativeoffice-watermark.png", WO::pngBytes());
        zip.addFile("word/footer9001.xml",
                    WO::docxFooterXml(QStringLiteral("rIdWmImg"), QStringLiteral("rIdWmLink")));
        zip.addFile("word/_rels/footer9001.xml.rels",
            (QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">")
             + WO::imageRel(QStringLiteral("rIdWmImg"), QStringLiteral("media/nativeoffice-watermark.png"))
             + WO::hyperlinkRel(QStringLiteral("rIdWmLink"))
             + QStringLiteral("</Relationships>")).toUtf8());
    }
    zip.close();
    return zip.status() == QZipWriter::NoError
        ? OpResult{ true, {} } : OpResult{ false, "The Word file could not be finalized." };
}

// ─────────────────────────────────────────────────────────────────────────────
// PDF → Excel (.xlsx)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// Columns are inferred from runs of two-or-more spaces, which PDFium emits
// between text that is visually separated on the page (i.e. table columns).
// A single space stays within a cell.
QStringList lineToCells(const QString& line) {
    static const QRegularExpression gap(QStringLiteral("\\s{2,}"));
    QStringList cells;
    for (const QString& part : line.split(gap, Qt::SkipEmptyParts)) {
        const QString t = part.trimmed();
        if (!t.isEmpty()) cells << t;
    }
    return cells;
}

QString colRef(int col) {   // 0 -> A, 26 -> AA
    QString s;
    ++col;
    while (col > 0) { int r = (col - 1) % 26; s.prepend(QChar('A' + r)); col = (col - 1) / 26; }
    return s;
}

} // namespace

OpResult toXlsx(const QString& in, const QString& out) {
    // Same rule as every other spreadsheet this app writes.
    const bool wantMark = NativeOffice::Watermark::enabledForExport();
    const QString wmSheetDraw = wantMark
        ? QStringLiteral("<drawing r:id=\"rIdWmDraw\"/>") : QString();
    OpResult err{ true, {} };
    auto r = openOrError(in, err);
    if (!r) return err;

    // Build one worksheet per page.
    QStringList sheetXmls, sheetNames;
    for (int p = 0; p < r->pageCount(); ++p) {
        QString rows;
        int rowNum = 1;
        for (const QString& line : pageTextLines(*r, p)) {
            const QStringList cells = lineToCells(line);
            if (cells.isEmpty()) continue;
            QString rowXml = QStringLiteral("<row r=\"%1\">").arg(rowNum);
            for (int c = 0; c < cells.size(); ++c) {
                rowXml += QStringLiteral("<c r=\"%1%2\" t=\"inlineStr\"><is><t xml:space=\"preserve\">%3</t></is></c>")
                    .arg(colRef(c)).arg(rowNum).arg(xmlEscape(cells[c]));
            }
            rowXml += "</row>";
            rows += rowXml;
            ++rowNum;
        }
        sheetXmls << ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
            "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
            "<sheetData>" + rows + "</sheetData>" + wmSheetDraw + "</worksheet>");
        sheetNames << QStringLiteral("Page %1").arg(p + 1);
    }
    if (sheetXmls.isEmpty()) { sheetXmls << ""; sheetNames << "Page 1"; }

    QString wmDrawOverrides;
    if (wantMark)
        for (int i = 0; i < sheetXmls.size(); ++i)
            wmDrawOverrides += QStringLiteral("<Override PartName=\"/xl/drawings/drawing%1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.drawing+xml\"/>").arg(i + 1);

    QString sheetsRefs, wbRels, ctOverrides;
    for (int i = 0; i < sheetNames.size(); ++i) {
        sheetsRefs += QStringLiteral("<sheet name=\"%1\" sheetId=\"%2\" r:id=\"rId%2\"/>")
            .arg(xmlEscape(sheetNames[i])).arg(i + 1);
        wbRels += QStringLiteral("<Relationship Id=\"rId%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet%1.xml\"/>")
            .arg(i + 1);
        ctOverrides += QStringLiteral("<Override PartName=\"/xl/worksheets/sheet%1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>")
            .arg(i + 1);
    }

    const QString workbookXml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<sheets>" + sheetsRefs + "</sheets></workbook>";
    const QString contentTypes =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        + ctOverrides
        + (wantMark ? QStringLiteral("<Default Extension=\"png\" ContentType=\"image/png\"/>") : QString())
        + wmDrawOverrides + "</Types>";
    const QString rootRels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
        "</Relationships>";
    const QString workbookRels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        + wbRels + "</Relationships>";

    QZipWriter zip(out);
    if (zip.status() != QZipWriter::NoError)
        return { false, "Could not create the Excel file." };
    zip.addFile("[Content_Types].xml", contentTypes.toUtf8());
    zip.addFile("_rels/.rels", rootRels.toUtf8());
    zip.addFile("xl/workbook.xml", workbookXml.toUtf8());
    zip.addFile("xl/_rels/workbook.xml.rels", workbookRels.toUtf8());
    for (int i = 0; i < sheetXmls.size(); ++i)
        zip.addFile(QStringLiteral("xl/worksheets/sheet%1.xml").arg(i + 1), sheetXmls[i].toUtf8());
    if (wantMark) {
        namespace WO = NativeOffice::Watermark::Ooxml;
        zip.addFile("xl/media/nativeoffice-watermark.png", WO::pngBytes());
        const QString relsHead = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
        for (int i = 0; i < sheetXmls.size(); ++i) {
            zip.addFile(QStringLiteral("xl/drawings/drawing%1.xml").arg(i + 1),
                WO::xlsxDrawingXml(QStringLiteral("rIdWmPic"), QStringLiteral("rIdWmLink"), 1, 2));
            zip.addFile(QStringLiteral("xl/drawings/_rels/drawing%1.xml.rels").arg(i + 1),
                (relsHead + WO::imageRel(QStringLiteral("rIdWmPic"), QStringLiteral("../media/nativeoffice-watermark.png"))
                 + WO::hyperlinkRel(QStringLiteral("rIdWmLink")) + QStringLiteral("</Relationships>")).toUtf8());
            zip.addFile(QStringLiteral("xl/worksheets/_rels/sheet%1.xml.rels").arg(i + 1),
                QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                    "<Relationship Id=\"rIdWmDraw\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing\" Target=\"../drawings/drawing%1.xml\"/>"
                    "</Relationships>").arg(i + 1).toUtf8());
        }
    }
    zip.close();
    return zip.status() == QZipWriter::NoError
        ? OpResult{ true, {} } : OpResult{ false, "The Excel file could not be finalized." };
}

// ─────────────────────────────────────────────────────────────────────────────
// PDF → PowerPoint (.pptx) — one image-filled slide per page
// ─────────────────────────────────────────────────────────────────────────────

OpResult toPptx(const QString& in, const QString& out, int dpi) {
    // Same rule as every other deck this app writes.
    const bool wantMark = NativeOffice::Watermark::enabledForExport();
    OpResult err{ true, {} };
    auto r = openOrError(in, err);
    if (!r) return err;
    const int n = r->pageCount();
    if (n == 0) return { false, "The PDF has no pages." };

    const qreal scale = std::clamp(dpi, 72, 300) / 72.0;

    // Slide size in EMUs (914400 per inch). Use the first page's aspect on a
    // 10-inch-wide slide.
    const QSizeF p0 = r->pageSizePt(0);
    const double slideW = 9144000.0;    // 10 in
    const double slideH = p0.width() > 0 ? slideW * (p0.height() / p0.width()) : slideW * 0.75;

    QString slideRefs, presRels, ctOverrides;
    QZipWriter zip(out);
    if (zip.status() != QZipWriter::NoError)
        return { false, "Could not create the PowerPoint file." };

    const QString wmSlidePic = wantMark
        ? NativeOffice::Watermark::Ooxml::pptxPicXml(9001, QStringLiteral("rIdWmImg"),
              QStringLiteral("rIdWmLink"), qint64(slideW), qint64(slideH))
        : QString();
    if (wantMark)
        zip.addFile("ppt/media/nativeoffice-watermark.png",
                    NativeOffice::Watermark::Ooxml::pngBytes());

    // Per-slide parts.
    for (int i = 0; i < n; ++i) {
        const QImage img = r->renderPage(i, scale);
        const QByteArray png = encodeImage(img, "PNG");
        zip.addFile(QStringLiteral("ppt/media/image%1.png").arg(i + 1), png);

        const QString slideXml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<p:sld xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
            "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
            "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\">"
            "<p:cSld><p:spTree>"
            "<p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>"
            "<p:grpSpPr/>"
            "<p:pic><p:nvPicPr><p:cNvPr id=\"2\" name=\"Page\"/><p:cNvPicPr/><p:nvPr/></p:nvPicPr>"
            "<p:blipFill><a:blip r:embed=\"rId1\"/><a:stretch><a:fillRect/></a:stretch></p:blipFill>"
            "<p:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"" + QString::number(qint64(slideW))
            + "\" cy=\"" + QString::number(qint64(slideH)) + "\"/></a:xfrm>"
            "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></p:spPr></p:pic>"
            + wmSlidePic +
            "</p:spTree></p:cSld><p:clrMapOvr><a:overrideClrMapping "
            "bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" tx2=\"dk2\" accent1=\"accent1\" accent2=\"accent2\" "
            "accent3=\"accent3\" accent4=\"accent4\" accent5=\"accent5\" accent6=\"accent6\" "
            "hlink=\"hlink\" folHlink=\"folHlink\"/></p:clrMapOvr></p:sld>";
        zip.addFile(QStringLiteral("ppt/slides/slide%1.xml").arg(i + 1), slideXml.toUtf8());

        const QString slideRels =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
            "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"../media/image"
            + QString::number(i + 1) + ".png\"/>"
            + (wantMark ? NativeOffice::Watermark::Ooxml::imageRel(QStringLiteral("rIdWmImg"), QStringLiteral("../media/nativeoffice-watermark.png"))
                            + NativeOffice::Watermark::Ooxml::hyperlinkRel(QStringLiteral("rIdWmLink")) : QString())
            + "</Relationships>";
        zip.addFile(QStringLiteral("ppt/slides/_rels/slide%1.xml.rels").arg(i + 1), slideRels.toUtf8());

        slideRefs += QStringLiteral("<p:sldId id=\"%1\" r:id=\"rId%2\"/>").arg(256 + i).arg(i + 1);
        presRels += QStringLiteral("<Relationship Id=\"rId%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide\" Target=\"slides/slide%1.xml\"/>")
            .arg(i + 1);
        ctOverrides += QStringLiteral("<Override PartName=\"/ppt/slides/slide%1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slide+xml\"/>")
            .arg(i + 1);
    }

    const QString presentationXml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<p:presentation xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
        "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\">"
        "<p:sldIdLst>" + slideRefs + "</p:sldIdLst>"
        "<p:sldSz cx=\"" + QString::number(qint64(slideW)) + "\" cy=\"" + QString::number(qint64(slideH)) + "\"/>"
        "</p:presentation>";
    const QString presRelsXml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        + presRels + "</Relationships>";
    const QString contentTypes =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Default Extension=\"png\" ContentType=\"image/png\"/>"
        "<Override PartName=\"/ppt/presentation.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml\"/>"
        + ctOverrides + "</Types>";
    const QString rootRels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"ppt/presentation.xml\"/>"
        "</Relationships>";

    zip.addFile("[Content_Types].xml", contentTypes.toUtf8());
    zip.addFile("_rels/.rels", rootRels.toUtf8());
    zip.addFile("ppt/presentation.xml", presentationXml.toUtf8());
    zip.addFile("ppt/_rels/presentation.xml.rels", presRelsXml.toUtf8());
    zip.close();
    return zip.status() == QZipWriter::NoError
        ? OpResult{ true, {} } : OpResult{ false, "The PowerPoint file could not be finalized." };
}

// ─────────────────────────────────────────────────────────────────────────────
// Picture → PDF  /  Image-only PDF
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// One page per image, each page sized to its image's aspect ratio, with the
// FULL-resolution image embedded — not a copy pre-scaled to the page's pixel
// grid, which threw away source pixels and capped every output at ~150 dpi
// (a 12MP photo came out as a soft page). QPdfWriter embeds whatever image it
// is handed, so drawing the original preserves its resolution.
OpResult writeImagesPdf(const std::vector<QImage>& images, const QString& out) {
    if (images.empty()) return { false, "No images to write." };
    QPdfWriter writer(out);
    writer.setResolution(300);

    // Physical page: the image's aspect on a 10-inch long edge. Only the print
    // scale depends on this; the embedded pixel resolution comes from the image.
    auto pageSizeFor = [](const QImage& img) {
        const QSize px = img.size();
        const double longIn = 10.0;
        const QSizeF in = px.width() >= px.height()
            ? QSizeF(longIn, longIn * px.height() / double(std::max(1, px.width())))
            : QSizeF(longIn * px.width() / double(std::max(1, px.height())), longIn);
        return QPageSize(in, QPageSize::Inch);
    };

    QPainter painter;
    bool started = false;
    for (const QImage& img : images) {
        if (img.isNull()) continue;
        writer.setPageSize(pageSizeFor(img));
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        if (!started) {
            if (!painter.begin(&writer)) return { false, "Could not create the PDF." };
            started = true;
        } else {
            writer.newPage();
        }
        // Fill the (aspect-matched) page with the original image.
        painter.drawImage(painter.viewport(), img);
    }
    if (!started) return { false, "No images to write." };
    painter.end();
    return { true, {} };
}

} // namespace

OpResult imagesToPdf(const QStringList& imagePaths, const QString& out) {
    std::vector<QImage> images;
    for (const QString& p : imagePaths) {
        QImage img(p);
        if (!img.isNull()) images.push_back(img);
    }
    if (images.empty()) return { false, "None of the selected images could be loaded." };
    return writeImagesPdf(images, out);
}

OpResult toImageOnlyPdf(const QString& in, const QString& out, int dpi) {
    OpResult err{ true, {} };
    auto r = openOrError(in, err);
    if (!r) return err;
    const qreal scale = std::clamp(dpi, 72, 300) / 72.0;
    std::vector<QImage> images;
    for (int i = 0; i < r->pageCount(); ++i) {
        QImage img = r->renderPage(i, scale);
        if (!img.isNull()) images.push_back(std::move(img));
    }
    return writeImagesPdf(images, out);
}

// ─────────────────────────────────────────────────────────────────────────────
// Extract embedded images
// ─────────────────────────────────────────────────────────────────────────────

OpResult extractImages(const QString& in, const QString& outDir, const QString& baseName,
                       QStringList* writtenPaths) {
    OpResult err{ true, {} };
    auto r = openOrError(in, err);
    if (!r) return err;
    QDir().mkpath(outDir);

    int count = 0;
    for (int p = 0; p < r->pageCount(); ++p) {
        const auto images = r->pageImages(p);
        for (size_t k = 0; k < images.size(); ++k) {
            if (images[k].isNull()) continue;
            const QString path = QStringLiteral("%1/%2-p%3-%4.png")
                .arg(outDir, baseName).arg(p + 1).arg(k + 1);
            if (images[k].save(path, "PNG")) {
                if (writtenPaths) *writtenPaths << path;
                ++count;
            }
        }
    }
    return count > 0 ? OpResult{ true, {} }
                     : OpResult{ false, "No embedded images were found." };
}

} // namespace NativeOffice::Pdf
