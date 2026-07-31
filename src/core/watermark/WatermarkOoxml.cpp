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

QByteArray xlsxDrawingXml(const QString& imageRelId, const QString& linkRelId,
                          int anchorCol, int anchorRow) {
    const QString xml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\" "
        "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
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
        "</xdr:oneCellAnchor>"
        "</xdr:wsDr>")
        .arg(QString::number(anchorCol), QString::number(anchorRow),
             QString::number(widthEmu()), QString::number(heightEmu()),
             linkRelId, imageRelId);
    return xml.toUtf8();
}

QString xlsxFooterRef() { return QStringLiteral("&amp;R&amp;G"); }

} // namespace Ooxml
} // namespace Watermark
} // namespace NativeOffice
