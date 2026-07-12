// ─────────────────────────────────────────────────────────────────────────────
// DocxIo.cpp  (Sprint 19 — Microsoft Word .docx import / export)
// ─────────────────────────────────────────────────────────────────────────────
#include "DocxIo.h"

#include <QtCore/private/qzipreader_p.h>
#include <QtCore/private/qzipwriter_p.h>

#include <QTextDocument>
#include <QTextCursor>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextTable>
#include <QTextTableCell>
#include <QTextList>
#include <QTextFrame>
#include <QTextCharFormat>
#include <QTextBlockFormat>
#include <QTextImageFormat>
#include <QTextTableCellFormat>
#include <QXmlStreamReader>
#include <QImage>
#include <QBuffer>
#include <QUrl>
#include <QColor>
#include <QFont>
#include <functional>

namespace NativeOffice {

namespace {

const char* kWNs   = "http://schemas.openxmlformats.org/wordprocessingml/2006/main";
const char* kRNs   = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";

QString esc(const QString& s) {
    QString r = s;
    r.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;")
     .replace('"', "&quot;").replace('\'', "&apos;");
    // Word uses U+2028 / U+2029 internally for line/para breaks inside fragments.
    r.replace(QChar(0x2028), QString());
    return r;
}

QString hex6(const QColor& c) { return c.name().mid(1).toUpper(); }   // "RRGGBB"

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// EXPORT
// ═════════════════════════════════════════════════════════════════════════════
bool DocxIo::exportDocx(QTextDocument* doc, const QString& path) {
    if (!doc) return false;

    QStringList   relEntries;                 // <Relationship .../> for document.xml.rels
    QList<QPair<QString, QByteArray>> media;  // filename -> PNG bytes
    int imgCounter = 0;
    bool usesLists = false;

    auto imageFor = [doc](const QTextImageFormat& f) -> QImage {
        QImage img = qvariant_cast<QImage>(
            doc->resource(QTextDocument::ImageResource, QUrl(f.name())));
        if (img.isNull()) {
            const QString name = f.name();
            const int comma = name.indexOf("base64,");
            if (comma >= 0) {
                const QByteArray b = QByteArray::fromBase64(name.mid(comma + 7).toLatin1());
                img.loadFromData(b);
            }
        }
        return img;
    };

    auto runXml = [](const QString& text, const QTextCharFormat& cf) -> QString {
        if (text.isEmpty()) return QString();
        QString rPr;
        if (cf.fontWeight() >= QFont::Bold) rPr += "<w:b/>";
        if (cf.fontItalic())   rPr += "<w:i/>";
        if (cf.fontUnderline())rPr += "<w:u w:val=\"single\"/>";
        if (cf.fontStrikeOut())rPr += "<w:strike/>";
        if (cf.foreground().style() != Qt::NoBrush) {
            const QColor col = cf.foreground().color();
            if (col.isValid() && col != QColor("#1C1E26") && col != QColor(Qt::black))
                rPr += "<w:color w:val=\"" + hex6(col) + "\"/>";
        }
        if (cf.background().style() != Qt::NoBrush && cf.background().color().alpha() > 0)
            rPr += "<w:highlight w:val=\"yellow\"/>";
        double pt = cf.fontPointSize(); if (pt <= 0) pt = 12;
        rPr += "<w:sz w:val=\"" + QString::number(int(pt * 2)) + "\"/>";
        QString fam = cf.fontFamilies().toStringList().value(0);
        if (fam.isEmpty()) fam = "Segoe UI";
        rPr += "<w:rFonts w:ascii=\"" + esc(fam) + "\" w:hAnsi=\"" + esc(fam) + "\"/>";
        if (cf.verticalAlignment() == QTextCharFormat::AlignSuperScript)
            rPr += "<w:vertAlign w:val=\"superscript\"/>";
        else if (cf.verticalAlignment() == QTextCharFormat::AlignSubScript)
            rPr += "<w:vertAlign w:val=\"subscript\"/>";

        QString r = "<w:r>";
        if (!rPr.isEmpty()) r += "<w:rPr>" + rPr + "</w:rPr>";
        r += "<w:t xml:space=\"preserve\">" + esc(text) + "</w:t></w:r>";
        return r;
    };

    auto imageRun = [&](const QTextImageFormat& fmt) -> QString {
        const QImage img = imageFor(fmt);
        if (img.isNull()) return QString();
        QByteArray png;
        { QBuffer buf(&png); buf.open(QIODevice::WriteOnly); img.save(&buf, "PNG"); }
        const int id = ++imgCounter;
        const QString fn  = QString("image%1.png").arg(id);
        const QString rId = QString("rIdImg%1").arg(id);
        media.append({ fn, png });
        relEntries << "<Relationship Id=\"" + rId +
            "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"media/" + fn + "\"/>";
        const int wpx = fmt.width()  > 0 ? int(fmt.width())  : img.width();
        const int hpx = fmt.height() > 0 ? int(fmt.height()) : img.height();
        const qint64 cx = qint64(wpx) * 9525, cy = qint64(hpx) * 9525;
        return
          "<w:r><w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">"
          "<wp:extent cx=\"" + QString::number(cx) + "\" cy=\"" + QString::number(cy) + "\"/>"
          "<wp:docPr id=\"" + QString::number(id) + "\" name=\"Image" + QString::number(id) + "\"/>"
          "<a:graphic xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">"
          "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
          "<pic:pic xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
          "<pic:nvPicPr><pic:cNvPr id=\"" + QString::number(id) + "\" name=\"Image" + QString::number(id) + "\"/>"
          "<pic:cNvPicPr/></pic:nvPicPr>"
          "<pic:blipFill><a:blip r:embed=\"" + rId + "\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
          "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"" + QString::number(cx) +
          "\" cy=\"" + QString::number(cy) + "\"/></a:xfrm>"
          "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr></pic:pic>"
          "</a:graphicData></a:graphic></wp:inline></w:drawing></w:r>";
    };

    std::function<QString(const QTextBlock&)> paraXml = [&](const QTextBlock& block) -> QString {
        const QTextBlockFormat bf = block.blockFormat();
        QString pPr;
        const Qt::Alignment a = bf.alignment();
        QString jc;
        if (a & Qt::AlignHCenter)      jc = "center";
        else if (a & Qt::AlignRight)   jc = "right";
        else if (a & Qt::AlignJustify) jc = "both";
        if (block.textList()) {
            usesLists = true;
            const QTextListFormat lf = block.textList()->format();
            int lvl = qMax(0, lf.indent() - 1);
            const int numId = (lf.style() == QTextListFormat::ListDecimal ||
                               lf.style() == QTextListFormat::ListLowerAlpha ||
                               lf.style() == QTextListFormat::ListUpperAlpha ||
                               lf.style() == QTextListFormat::ListLowerRoman ||
                               lf.style() == QTextListFormat::ListUpperRoman) ? 2 : 1;
            pPr += "<w:numPr><w:ilvl w:val=\"" + QString::number(lvl) +
                   "\"/><w:numId w:val=\"" + QString::number(numId) + "\"/></w:numPr>";
        }
        if (bf.leftMargin() > 0 || bf.textIndent() != 0) {
            const int left = int(bf.leftMargin() / 96.0 * 1440);   // px → twips
            pPr += "<w:ind w:left=\"" + QString::number(left) + "\"/>";
        }
        if (bf.topMargin() > 0 || bf.bottomMargin() > 0) {
            pPr += "<w:spacing w:before=\"" + QString::number(int(bf.topMargin() / 96.0 * 1440)) +
                   "\" w:after=\"" + QString::number(int(bf.bottomMargin() / 96.0 * 1440)) + "\"/>";
        }
        if (!jc.isEmpty()) pPr += "<w:jc w:val=\"" + jc + "\"/>";

        // Heading paragraphs carry a Word style so headings/TOC/outline survive.
        const int hl = bf.headingLevel();
        if (hl >= 1 && hl <= 6)
            pPr.prepend("<w:pStyle w:val=\"Heading" + QString::number(hl) + "\"/>");

        QString runs;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.isValid()) continue;
            const QTextCharFormat cf = f.charFormat();
            if (cf.isImageFormat()) runs += imageRun(cf.toImageFormat());
            else                    runs += runXml(f.text(), cf);
        }
        QString p = "<w:p>";
        if (!pPr.isEmpty()) p += "<w:pPr>" + pPr + "</w:pPr>";
        p += runs + "</w:p>";
        return p;
    };

    std::function<QString(QTextTable*)> tableXml = [&](QTextTable* t) -> QString {
        QString s = "<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/><w:tblBorders>";
        for (const char* side : { "top", "left", "bottom", "right", "insideH", "insideV" })
            s += QString("<w:%1 w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"9CA3AF\"/>").arg(side);
        s += "</w:tblBorders></w:tblPr>";
        const int rows = t->rows(), cols = t->columns();
        // tblGrid (column definitions) — required by Word.
        s += "<w:tblGrid>";
        for (int c = 0; c < cols; ++c) s += "<w:gridCol w:w=\"" + QString::number(9000 / qMax(1, cols)) + "\"/>";
        s += "</w:tblGrid>";
        for (int r = 0; r < rows; ++r) {
            s += "<w:tr>";
            for (int c = 0; c < cols; ++c) {
                const QTextTableCell cell = t->cellAt(r, c);
                if (cell.column() != c) continue;      // covered by a gridSpan to the left
                QString tcPr;
                if (cell.columnSpan() > 1)
                    tcPr += "<w:gridSpan w:val=\"" + QString::number(cell.columnSpan()) + "\"/>";
                const bool vOrigin = (cell.row() == r);
                if (cell.rowSpan() > 1)
                    tcPr += vOrigin ? "<w:vMerge w:val=\"restart\"/>" : "<w:vMerge/>";
                const QTextTableCellFormat cfmt = cell.format().toTableCellFormat();
                if (cfmt.background().style() != Qt::NoBrush && cfmt.background().color().isValid())
                    tcPr += "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"" + hex6(cfmt.background().color()) + "\"/>";
                s += "<w:tc>";
                s += "<w:tcPr>" + tcPr + "</w:tcPr>";
                if (vOrigin) {
                    for (QTextBlock b = cell.firstCursorPosition().block(); ; b = b.next()) {
                        s += paraXml(b);
                        if (b == cell.lastCursorPosition().block() || !b.isValid()) break;
                    }
                } else {
                    s += "<w:p/>";   // continuation of a vertical merge
                }
                s += "</w:tc>";
            }
            s += "</w:tr>";
        }
        s += "</w:tbl><w:p/>";   // Word requires a paragraph after a table
        return s;
    };

    // ── Walk the document body ──────────────────────────────────────────────
    QString body;
    for (auto it = doc->rootFrame()->begin(); !it.atEnd(); ++it) {
        if (QTextFrame* cf = it.currentFrame()) {
            if (auto* t = qobject_cast<QTextTable*>(cf)) body += tableXml(t);
        } else if (it.currentBlock().isValid()) {
            body += paraXml(it.currentBlock());
        }
    }

    // ── Assemble the package parts ──────────────────────────────────────────
    const QString documentXml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:document xmlns:w=\"" + QString(kWNs) + "\" xmlns:r=\"" + QString(kRNs) + "\""
        " xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\">"
        "<w:body>" + body +
        "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/>"
        "<w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\" w:left=\"1440\"/></w:sectPr>"
        "</w:body></w:document>";

    QString contentTypes =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Default Extension=\"png\" ContentType=\"image/png\"/>"
        "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>";
    if (usesLists)
        contentTypes += "<Override PartName=\"/word/numbering.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml\"/>";
    contentTypes += "</Types>";

    const QString rootRels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
        "</Relationships>";

    if (usesLists)
        relEntries << "<Relationship Id=\"rIdNum\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering\" Target=\"numbering.xml\"/>";

    const QString docRels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        + relEntries.join("") + "</Relationships>";

    QString numberingXml;
    if (usesLists) {
        auto lvls = [](const char* fmt, const char* text) {
            QString s;
            for (int i = 0; i < 6; ++i)
                s += "<w:lvl w:ilvl=\"" + QString::number(i) + "\"><w:start w:val=\"1\"/><w:numFmt w:val=\"" + fmt +
                     "\"/><w:lvlText w:val=\"" + text + "\"/><w:lvlJc w:val=\"left\"/>"
                     "<w:pPr><w:ind w:left=\"" + QString::number(720 * (i + 1)) + "\" w:hanging=\"360\"/></w:pPr></w:lvl>";
            return s;
        };
        numberingXml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<w:numbering xmlns:w=\"" + QString(kWNs) + "\">"
            "<w:abstractNum w:abstractNumId=\"0\">" + lvls("bullet", "\xEF\x82\xB7") + "</w:abstractNum>"
            "<w:abstractNum w:abstractNumId=\"1\">" + lvls("decimal", "%1.") + "</w:abstractNum>"
            "<w:num w:numId=\"1\"><w:abstractNumId w:val=\"0\"/></w:num>"
            "<w:num w:numId=\"2\"><w:abstractNumId w:val=\"1\"/></w:num>"
            "</w:numbering>";
    }

    // ── Write the ZIP ───────────────────────────────────────────────────────
    QZipWriter zip(path);
    if (zip.status() != QZipWriter::NoError) return false;
    zip.addFile("[Content_Types].xml", contentTypes.toUtf8());
    zip.addFile("_rels/.rels", rootRels.toUtf8());
    zip.addFile("word/document.xml", documentXml.toUtf8());
    if (usesLists) zip.addFile("word/numbering.xml", numberingXml.toUtf8());
    if (!relEntries.isEmpty())
        zip.addFile("word/_rels/document.xml.rels", docRels.toUtf8());
    for (const auto& m : media)
        zip.addFile("word/media/" + m.first, m.second);
    zip.close();
    return zip.status() == QZipWriter::NoError;
}

// ═════════════════════════════════════════════════════════════════════════════
// IMPORT
// ═════════════════════════════════════════════════════════════════════════════
bool DocxIo::importDocx(const QString& path, QTextDocument* doc) {
    if (!doc) return false;
    QZipReader zip(path);
    if (zip.status() != QZipReader::NoError) return false;

    const QByteArray docXml = zip.fileData("word/document.xml");
    if (docXml.isEmpty()) return false;
    const QByteArray relXml = zip.fileData("word/_rels/document.xml.rels");

    // Map relationship id -> media bytes (for images).
    QHash<QString, QByteArray> imgByRel;
    if (!relXml.isEmpty()) {
        QXmlStreamReader rr(relXml);
        while (!rr.atEnd()) {
            if (rr.readNext() == QXmlStreamReader::StartElement && rr.name() == QLatin1String("Relationship")) {
                const QString id = rr.attributes().value("Id").toString();
                const QString target = rr.attributes().value("Target").toString();
                if (target.contains("media/"))
                    imgByRel.insert(id, zip.fileData("word/" + target));
            }
        }
    }

    doc->clear();
    QTextCursor cur(doc);
    cur.beginEditBlock();
    bool firstBlock = true;

    QXmlStreamReader xml(docXml);
    // Recursive paragraph/table parsing.
    std::function<void(int)> parseBody;

    auto readParagraph = [&]() {
        // We're at <w:p>. Read pPr then runs.
        if (!firstBlock) cur.insertBlock();
        firstBlock = false;

        QTextBlockFormat bf;
        bf.setHeadingLevel(0);   // explicit, so a new block never inherits a heading
        QTextCharFormat baseFmt;
        QString listFmt;
        int listLevel = 0;

        QTextCharFormat runFmt;
        while (!xml.atEnd()) {
            const auto tok = xml.readNext();
            if (tok == QXmlStreamReader::EndElement && xml.name() == QLatin1String("p")) break;
            if (tok != QXmlStreamReader::StartElement) continue;
            const QStringView n = xml.name();

            if (n == QLatin1String("pStyle")) {
                const QString v = xml.attributes().value("w:val").toString();
                if (v.startsWith("Heading")) {
                    const int lvl = v.mid(7).trimmed().toInt();
                    if (lvl >= 1 && lvl <= 6) bf.setHeadingLevel(lvl);
                }
            } else if (n == QLatin1String("jc")) {
                const QString v = xml.attributes().value("w:val").toString();
                if (v == "center") bf.setAlignment(Qt::AlignHCenter);
                else if (v == "right") bf.setAlignment(Qt::AlignRight);
                else if (v == "both") bf.setAlignment(Qt::AlignJustify);
                else bf.setAlignment(Qt::AlignLeft);
            } else if (n == QLatin1String("numId")) {
                listFmt = xml.attributes().value("w:val").toString();
            } else if (n == QLatin1String("ilvl")) {
                listLevel = xml.attributes().value("w:val").toInt();
            } else if (n == QLatin1String("ind")) {
                const int left = xml.attributes().value("w:left").toInt();
                if (left > 0) bf.setLeftMargin(left / 1440.0 * 96.0);
            } else if (n == QLatin1String("spacing")) {
                bf.setTopMargin(xml.attributes().value("w:before").toInt() / 1440.0 * 96.0);
                bf.setBottomMargin(xml.attributes().value("w:after").toInt() / 1440.0 * 96.0);
            } else if (n == QLatin1String("r")) {
                // a run: read rPr + content
                runFmt = QTextCharFormat();
                runFmt.setFontFamilies({ "Segoe UI" });
                runFmt.setForeground(QColor("#1C1E26"));
                qint64 drawW = 0, drawH = 0;   // drawing extent (EMU), if present
                while (!xml.atEnd()) {
                    const auto rt = xml.readNext();
                    if (rt == QXmlStreamReader::EndElement && xml.name() == QLatin1String("r")) break;
                    if (rt != QXmlStreamReader::StartElement) continue;
                    const QStringView rn = xml.name();
                    if      (rn == QLatin1String("b"))      runFmt.setFontWeight(QFont::Bold);
                    else if (rn == QLatin1String("i"))      runFmt.setFontItalic(true);
                    else if (rn == QLatin1String("u"))      runFmt.setFontUnderline(true);
                    else if (rn == QLatin1String("strike")) runFmt.setFontStrikeOut(true);
                    else if (rn == QLatin1String("color")) {
                        const QString v = xml.attributes().value("w:val").toString();
                        if (v.size() == 6 && v != "auto") runFmt.setForeground(QColor("#" + v));
                    } else if (rn == QLatin1String("highlight")) {
                        runFmt.setBackground(QColor("#FFF27A"));
                    } else if (rn == QLatin1String("sz")) {
                        const int half = xml.attributes().value("w:val").toInt();
                        if (half > 0) runFmt.setFontPointSize(half / 2.0);
                    } else if (rn == QLatin1String("rFonts")) {
                        const QString fam = xml.attributes().value("w:ascii").toString();
                        if (!fam.isEmpty()) runFmt.setFontFamilies({ fam });
                    } else if (rn == QLatin1String("vertAlign")) {
                        const QString v = xml.attributes().value("w:val").toString();
                        if (v == "superscript") runFmt.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
                        else if (v == "subscript") runFmt.setVerticalAlignment(QTextCharFormat::AlignSubScript);
                    } else if (rn == QLatin1String("t")) {
                        const QString text = xml.readElementText();
                        cur.insertText(text, runFmt);
                    } else if (rn == QLatin1String("tab")) {
                        cur.insertText(QStringLiteral("	"), runFmt);
                    } else if (rn == QLatin1String("br")) {
                        // A page break splits onto a fresh page; a plain break
                        // stays a line break within the paragraph.
                        if (xml.attributes().value("w:type").toString() == QLatin1String("page")) {
                            QTextBlockFormat pbf;
                            pbf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
                            cur.insertBlock(pbf);
                        } else {
                            cur.insertText(QString(QChar(QChar::LineSeparator)), runFmt);
                        }
                    } else if (rn == QLatin1String("extent")) {
                        drawW = xml.attributes().value("cx").toLongLong();
                        drawH = xml.attributes().value("cy").toLongLong();
                    } else if (rn == QLatin1String("blip")) {
                        const QString rId = xml.attributes().value("r:embed").toString();
                        QImage img; img.loadFromData(imgByRel.value(rId));
                        if (!img.isNull()) {
                            QByteArray png; { QBuffer bf2(&png); bf2.open(QIODevice::WriteOnly); img.save(&bf2, "PNG"); }
                            const QString url = "data:image/png;base64," + png.toBase64();
                            doc->addResource(QTextDocument::ImageResource, QUrl(url), img);
                            QTextImageFormat imf; imf.setName(url);
                            // Honour the drawing extent (EMU->px) when present,
                            // else native size; then clamp to the page content
                            // width so oversized cover images / figures do not
                            // overflow and clip off the right edge.
                            double dw = drawW > 0 ? drawW / 9525.0 : img.width();
                            double dh = drawH > 0 ? drawH / 9525.0 : img.height();
                            const double maxW = 648.0;   // ~A4 width minus margins @96dpi
                            if (dw > maxW) { dh *= maxW / dw; dw = maxW; }
                            if (dw > 0 && dh > 0) { imf.setWidth(dw); imf.setHeight(dh); }
                            cur.insertImage(imf);
                        }
                    }
                }
            }
        }
        if (!listFmt.isEmpty()) {
            QTextListFormat lf;
            lf.setStyle(listFmt == "2" ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc);
            lf.setIndent(listLevel + 1);
            cur.createList(lf);
        }
        cur.mergeBlockFormat(bf);
    };

    std::function<void()> readTable = [&]() {
        // Collect rows/cells as text, then build a QTextTable.
        struct Cell { QString text; int gridSpan = 1; QColor shade; };
        QList<QList<Cell>> rows;
        while (!xml.atEnd()) {
            const auto tok = xml.readNext();
            if (tok == QXmlStreamReader::EndElement && xml.name() == QLatin1String("tbl")) break;
            if (tok == QXmlStreamReader::StartElement && xml.name() == QLatin1String("tr")) {
                QList<Cell> row;
                while (!xml.atEnd()) {
                    const auto t2 = xml.readNext();
                    if (t2 == QXmlStreamReader::EndElement && xml.name() == QLatin1String("tr")) break;
                    if (t2 == QXmlStreamReader::StartElement && xml.name() == QLatin1String("tc")) {
                        Cell cell;
                        while (!xml.atEnd()) {
                            const auto t3 = xml.readNext();
                            if (t3 == QXmlStreamReader::EndElement && xml.name() == QLatin1String("tc")) break;
                            if (t3 == QXmlStreamReader::StartElement) {
                                if (xml.name() == QLatin1String("gridSpan"))
                                    cell.gridSpan = qMax(1, xml.attributes().value("w:val").toInt());
                                else if (xml.name() == QLatin1String("shd")) {
                                    const QString f = xml.attributes().value("w:fill").toString();
                                    if (f.size() == 6 && f != "auto") cell.shade = QColor("#" + f);
                                } else if (xml.name() == QLatin1String("t"))
                                    cell.text += xml.readElementText();
                            }
                        }
                        row << cell;
                    }
                }
                if (!row.isEmpty()) rows << row;
            }
        }
        if (rows.isEmpty()) return;
        int cols = 0;
        for (const auto& r : rows) { int n = 0; for (const auto& c : r) n += c.gridSpan; cols = qMax(cols, n); }
        if (cols < 1) cols = 1;

        QTextTableFormat tf;
        tf.setCellPadding(4); tf.setBorder(1);
        tf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
        tf.setBorderBrush(QColor("#9CA3AF"));
        tf.setWidth(QTextLength(QTextLength::PercentageLength, 100));
        if (!firstBlock) cur.insertBlock();
        firstBlock = false;
        QTextTable* tbl = cur.insertTable(rows.size(), cols, tf);
        for (int r = 0; r < rows.size() && tbl; ++r) {
            int cc = 0;
            for (const Cell& cell : rows[r]) {
                if (cc >= cols) break;
                if (cell.gridSpan > 1 && cc + cell.gridSpan <= cols)
                    tbl->mergeCells(r, cc, 1, cell.gridSpan);
                QTextTableCell tc = tbl->cellAt(r, cc);
                if (tc.isValid()) {
                    if (cell.shade.isValid()) {
                        QTextTableCellFormat f = tc.format().toTableCellFormat();
                        f.setBackground(cell.shade); tc.setFormat(f);
                    }
                    tc.firstCursorPosition().insertText(cell.text);
                }
                cc += cell.gridSpan;
            }
        }
        cur.movePosition(QTextCursor::End);
    };

    while (!xml.atEnd()) {
        const auto tok = xml.readNext();
        if (tok != QXmlStreamReader::StartElement) continue;
        if (xml.name() == QLatin1String("p"))   readParagraph();
        else if (xml.name() == QLatin1String("tbl")) readTable();
    }

    cur.endEditBlock();
    return !xml.hasError();
}

} // namespace NativeOffice
