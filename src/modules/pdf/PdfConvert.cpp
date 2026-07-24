// ─────────────────────────────────────────────────────────────────────────────
// PdfConvert.cpp — see PdfConvert.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfConvert.h"
#include "PdfRenderer.h"

#include <QtCore/private/qzipwriter_p.h>

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>

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
// ─────────────────────────────────────────────────────────────────────────────

OpResult toDocx(const QString& in, const QString& out) {
    OpResult err{ true, {} };
    auto r = openOrError(in, err);
    if (!r) return err;

    QString body;
    for (int p = 0; p < r->pageCount(); ++p) {
        for (const QString& line : pageTextLines(*r, p)) {
            const QString t = line.trimmed();
            if (t.isEmpty()) continue;
            body += "<w:p><w:r><w:t xml:space=\"preserve\">"
                  + xmlEscape(t) + "</w:t></w:r></w:p>";
        }
        if (p + 1 < r->pageCount())
            body += "<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>";
    }

    const QString documentXml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:body>" + body +
        "<w:sectPr><w:pgSz w:w=\"12240\" w:h=\"15840\"/></w:sectPr></w:body></w:document>";

    const QString contentTypes =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "</Types>";
    const QString rootRels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
        "</Relationships>";

    QZipWriter zip(out);
    if (zip.status() != QZipWriter::NoError)
        return { false, "Could not create the Word file." };
    zip.addFile("[Content_Types].xml", contentTypes.toUtf8());
    zip.addFile("_rels/.rels", rootRels.toUtf8());
    zip.addFile("word/document.xml", documentXml.toUtf8());
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
            "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
            "<sheetData>" + rows + "</sheetData></worksheet>");
        sheetNames << QStringLiteral("Page %1").arg(p + 1);
    }
    if (sheetXmls.isEmpty()) { sheetXmls << ""; sheetNames << "Page 1"; }

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
        + ctOverrides + "</Types>";
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
    zip.close();
    return zip.status() == QZipWriter::NoError
        ? OpResult{ true, {} } : OpResult{ false, "The Excel file could not be finalized." };
}

// ─────────────────────────────────────────────────────────────────────────────
// PDF → PowerPoint (.pptx) — one image-filled slide per page
// ─────────────────────────────────────────────────────────────────────────────

OpResult toPptx(const QString& in, const QString& out, int dpi) {
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
            "</p:spTree></p:cSld><p:clrMapOvr><a:overrideClrMapping "
            "bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" tx2=\"dk2\" accent1=\"accent1\" accent2=\"accent2\" "
            "accent3=\"accent3\" accent4=\"accent4\" accent5=\"accent5\" accent6=\"accent6\" "
            "hlink=\"hlink\" folHlink=\"folHlink\"/></p:clrMapOvr></p:sld>";
        zip.addFile(QStringLiteral("ppt/slides/slide%1.xml").arg(i + 1), slideXml.toUtf8());

        const QString slideRels =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
            "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"../media/image"
            + QString::number(i + 1) + ".png\"/></Relationships>";
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
