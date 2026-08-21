// ─────────────────────────────────────────────────────────────────────────────
// WatermarkOoxml.cpp — the watermark rendered as OOXML parts.
// ─────────────────────────────────────────────────────────────────────────────
#include "WatermarkOoxml.h"
#include "Watermark.h"

#include <QBuffer>
#include <QImage>

namespace NativeOffice {
namespace Watermark {
namespace Ooxml {

namespace {

constexpr qint64 kEmuPerPoint = 12700;   // 914400 EMU per inch / 72 pt per inch

QByteArray renderPng(QSize* outSize) {
    // 4 px per point is roughly 288 dpi: sharp when printed, and the mark is
    // small enough that the file cost is a few kilobytes.
    const QImage img = renderImage(4.0);
    if (outSize) *outSize = img.size();

    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return png;
}

struct Cache {
    QByteArray png;
    QSize      size;
    Cache() { png = renderPng(&size); }
};

const Cache& cache() { static Cache c; return c; }

} // namespace

const QByteArray& pngBytes() { return cache().png; }
QSize             pngSizePx() { return cache().size; }

qint64 widthEmu()  { return qint64(sizePoints().width()  * kEmuPerPoint); }
qint64 heightEmu() { return qint64(sizePoints().height() * kEmuPerPoint); }

QString hyperlinkRel(const QString& relId) {
    return QStringLiteral(
        "<Relationship Id=\"%1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink\" "
        "Target=\"%2\" TargetMode=\"External\"/>")
        .arg(relId, targetUrl());
}

QString imageRel(const QString& relId, const QString& target) {
    return QStringLiteral(
        "<Relationship Id=\"%1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" "
        "Target=\"%2\"/>")
        .arg(relId, target);
}

QByteArray docxFooterXml(const QString& imageRelId, const QString& linkRelId) {
    // a:hlinkClick on the picture's non-visual properties makes the whole
    // picture, and only the picture, the hit area.
    const QString xml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:ftr xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
        "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" "
        "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
        "xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
        "<w:p><w:pPr><w:jc w:val=\"right\"/></w:pPr><w:r><w:drawing>"
        "<wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">"
        "<wp:extent cx=\"%1\" cy=\"%2\"/>"
        "<wp:docPr id=\"9001\" name=\"NativeOfficeWatermark\" descr=\"Made with NativeOffice\">"
        "<a:hlinkClick xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" r:id=\"%3\"/>"
        "</wp:docPr>"
        "<a:graphic><a:graphicData "
        "uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
        "<pic:pic>"
        "<pic:nvPicPr>"
        "<pic:cNvPr id=\"9001\" name=\"NativeOfficeWatermark\">"
        "<a:hlinkClick r:id=\"%3\"/>"
        "</pic:cNvPr>"
        "<pic:cNvPicPr/>"
        "</pic:nvPicPr>"
        "<pic:blipFill><a:blip r:embed=\"%4\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
        "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%1\" cy=\"%2\"/></a:xfrm>"
        "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr>"
        "</pic:pic></a:graphicData></a:graphic>"
        "</wp:inline></w:drawing></w:r></w:p>"
        "</w:ftr>")
        .arg(QString::number(widthEmu()), QString::number(heightEmu()),
             linkRelId, imageRelId);
    return xml.toUtf8();
}

QString pptxPicXml(int shapeId, const QString& imageRelId, const QString& linkRelId,
                   qint64 slideWEmu, qint64 slideHEmu) {
    const qint64 cx = widthEmu();
    const qint64 cy = heightEmu();
    const qint64 margin = qint64(marginPoints() * kEmuPerPoint);
    const qint64 x = slideWEmu - margin - cx;
    const qint64 y = slideHEmu - margin - cy;

    return QStringLiteral(
        "<p:pic>"
        "<p:nvPicPr>"
        "<p:cNvPr id=\"%1\" name=\"NativeOfficeWatermark\" descr=\"Made with NativeOffice\">"
        "<a:hlinkClick r:id=\"%2\"/>"
        "</p:cNvPr>"
        "<p:cNvPicPr><a:picLocks noChangeAspect=\"1\"/></p:cNvPicPr>"
        "<p:nvPr/>"
        "</p:nvPicPr>"
        "<p:blipFill><a:blip r:embed=\"%3\"/><a:stretch><a:fillRect/></a:stretch></p:blipFill>"
        "<p:spPr><a:xfrm><a:off x=\"%4\" y=\"%5\"/><a:ext cx=\"%6\" cy=\"%7\"/></a:xfrm>"
        "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></p:spPr>"
        "</p:pic>")
        .arg(QString::number(shapeId), linkRelId, imageRelId,
             QString::number(x), QString::number(y),
             QString::number(cx), QString::number(cy));
}

QString xlsxDrawingAnchorXml(const QString& imageRelId, const QString& linkRelId,
                             int anchorCol, int anchorRow) {
    return QStringLiteral(
        "<xdr:oneCellAnchor>"
        "<xdr:from><xdr:col>%1</xdr:col><xdr:colOff>0</xdr:colOff>"
        "<xdr:row>%2</xdr:row><xdr:rowOff>0</xdr:rowOff></xdr:from>"
        "<xdr:ext cx=\"%3\" cy=\"%4\"/>"
        "<xdr:pic>"
        "<xdr:nvPicPr>"
        "<xdr:cNvPr id=\"9001\" name=\"NativeOfficeWatermark\" descr=\"Made with NativeOffice\">"
        "<a:hlinkClick r:id=\"%5\"/>"
        "</xdr:cNvPr>"
        "<xdr:cNvPicPr><a:picLocks noChangeAspect=\"1\"/></xdr:cNvPicPr>"
        "</xdr:nvPicPr>"
        "<xdr:blipFill><a:blip r:embed=\"%6\"/><a:stretch><a:fillRect/></a:stretch></xdr:blipFill>"
        "<xdr:spPr><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></xdr:spPr>"
        "</xdr:pic>"
        "<xdr:clientData/>"
        "</xdr:oneCellAnchor>")
        .arg(QString::number(anchorCol), QString::number(anchorRow),
             QString::number(widthEmu()), QString::number(heightEmu()),
             linkRelId, imageRelId);
}

QByteArray xlsxDrawingXml(const QString& imageRelId, const QString& linkRelId,
                          int anchorCol, int anchorRow) {
    const QString xml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\" "
        "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">")
        + xlsxDrawingAnchorXml(imageRelId, linkRelId, anchorCol, anchorRow)
        + QStringLiteral("</xdr:wsDr>");
    return xml.toUtf8();
}

QString xlsxHeaderFooterXml() {
    // &R selects the right footer section, &G inserts the picture.
    return QStringLiteral("<headerFooter><oddFooter>&amp;R&amp;G</oddFooter></headerFooter>");
}

QByteArray xlsxFooterVml(const QString& imageRelId) {
    // Footer graphics are still VML, not DrawingML: this is the one place the
    // modern schema never replaced. The size is given in points, matching the
    // artwork, so Excel does not rescale it.
    const QSizeF sz = sizePoints();
    const QString xml = QStringLiteral(
        "<xml xmlns:v=\"urn:schemas-microsoft-com:vml\" "
        "xmlns:o=\"urn:schemas-microsoft-com:office:office\" "
        "xmlns:x=\"urn:schemas-microsoft-com:office:excel\">"
        "<o:shapelayout v:ext=\"edit\"><o:idmap v:ext=\"edit\" data=\"1\"/></o:shapelayout>"
        "<v:shapetype id=\"_x0000_t75\" coordsize=\"21600,21600\" o:spt=\"75\" "
        "o:preferrelative=\"t\" path=\"m@4@5l@4@11@9@11@9@5xe\" filled=\"f\" stroked=\"f\">"
        "<v:stroke joinstyle=\"miter\"/>"
        "<v:formulas><v:f eqn=\"if lineDrawn pixelLineWidth 0\"/><v:f eqn=\"sum @0 1 0\"/>"
        "<v:f eqn=\"sum 0 0 @1\"/><v:f eqn=\"prod @2 1 2\"/><v:f eqn=\"prod @3 21600 pixelWidth\"/>"
        "<v:f eqn=\"prod @3 21600 pixelHeight\"/><v:f eqn=\"sum @0 0 1\"/><v:f eqn=\"prod @6 1 2\"/>"
        "<v:f eqn=\"prod @7 21600 pixelWidth\"/><v:f eqn=\"sum @8 21600 0\"/>"
        "<v:f eqn=\"prod @7 21600 pixelHeight\"/><v:f eqn=\"sum @10 21600 0\"/></v:formulas>"
        "<v:path o:extrusionok=\"f\" gradientshapeok=\"t\" o:connecttype=\"rect\"/>"
        "<o:lock v:ext=\"edit\" aspectratio=\"t\"/></v:shapetype>"
        "<v:shape id=\"RF\" o:spid=\"_x0000_s1025\" type=\"#_x0000_t75\" "
        "style=\"position:absolute;margin-left:0;margin-top:0;"
        "width:%1pt;height:%2pt;z-index:1\">"
        "<v:imagedata o:relid=\"%3\" o:title=\"Made with NativeOffice\"/>"
        "<o:lock v:ext=\"edit\" rotation=\"t\"/></v:shape>"
        "</xml>")
        .arg(QString::number(sz.width(), 'f', 2),
             QString::number(sz.height(), 'f', 2),
             imageRelId);
    return xml.toUtf8();
}

} // namespace Ooxml
} // namespace Watermark
} // namespace NativeOffice
