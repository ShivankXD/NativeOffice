#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// XlsxDrawingWriter.h
// The other half of XlsxDrawing: turning the objects that sit on a sheet back
// into the OOXML parts they need, so an object built inside NativeOffice
// survives a save.
//
// Until this existed the exporter could read charts and pictures and write
// neither. A workbook opened from Excel kept them only because the save put the
// original package back verbatim; anything the user added here had no part to
// put back and was silently dropped on every .xlsx write.
//
// Two pieces, because OOXML splits a chart in two:
//
//   * xl/charts/chartN.xml   what is plotted (series, categories, legend)
//   * an anchor in the sheet's xl/drawings/drawingN.xml   where it sits
//
// This file knows nothing about ZIP or about packages. It hands back XML and
// lets XlsxIo.cpp decide which part names and relationship ids are free, which
// is the only thing that differs between rebuilding a package and adding to
// one that already exists.
// ─────────────────────────────────────────────────────────────────────────────

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

#include "ChartSpec.h"

namespace NativeOffice {

// Serialize one chart as a complete xl/charts/chartN.xml.
//
//   spec       the chart to write
//   sheetName  the sheet its ranges refer to, for the "Sheet1!$A$1" in each
//              reference. Cell references in a chart part are absolute and
//              sheet-qualified, so this cannot be left out.
//   disp       reads a cell's displayed text, used to work out what the
//              chart's range means and to cache the plotted values next to
//              each reference. Same callback ChartObject uses, and the same
//              scan behind it, so the file agrees with the screen.
//
// Returns an empty array when the chart has nothing plottable.
//
// `media` collects the pictures the chart part references, for a series whose
// fill is an image rather than a colour. Pass one (freshly default-constructed,
// one per chart, because the relationship ids are numbered within a single
// chart part) when the caller can write media parts and a
// xl/charts/chartN.xml.rels alongside; the caller writes `data[k]` under a part
// name of its choosing and relates it as `relIds[k]`.
//
// Pass nothing and a picture fill degrades to a gradient built from the
// shading sampled out of it, and then to a flat colour: valid, self-contained,
// and lossy. Both are better than what happened before, which was to write the
// flat average and lose the shading with it.
struct ChartMedia {
    QVector<QByteArray> data;
    QStringList         relIds;   // parallel to data
};

QByteArray buildChartPartXml(const ChartSpec& spec,
                             const QString& sheetName,
                             const std::function<QString(int, int)>& disp,
                             ChartMedia* media = nullptr);

// The <xdr:twoCellAnchor> (or <xdr:oneCellAnchor>, for a chart that only ever
// had pixel geometry) that places this chart and points at `relId`.
//
// `shapeId` must be unique within the drawing part; the caller allocates it.
// The result is a fragment, to be concatenated into a <xdr:wsDr> document.
QString buildChartAnchorXml(const ChartSpec& spec,
                            const QString& relId,
                            int shapeId);

// The <xdr:twoCellAnchor> (or <xdr:oneCellAnchor>) that places a picture and
// points at `relId` for its bytes.
//
// The bytes themselves go in a media part, which the caller writes; this only
// references it. Same shapes and the same reasoning as the chart anchor above.
QString buildPictureAnchorXml(const SheetImage& img,
                              const QString& relId,
                              int shapeId);

// The anchors that place every drawn shape on a sheet.
//
// Shapes are the one kind of object the reader handled and the writer did not,
// so a rebuild dropped every banner, button, rule and callout on the sheet.
//
// This takes the whole list rather than one shape at a time because shapes that
// share an anchor are the members of a GROUP, and a group is what gives them
// their separate places: each one covers a fraction of the anchored box
// (SheetShape::frac), and written as separate anchors they would all claim the
// whole box and pile up on each other. Consecutive shapes with the same anchor
// are re-grouped here, with `frac` written back as the group's child
// coordinates, which is where it was read from.
//
// `fillRel` is called for a shape filled with a picture and must write that
// image as a media part and hand back the relationship id for it, in the SAME
// drawing part these anchors go into. Pass nothing and picture fills are
// skipped (the shape keeps its outline and its text).
//
// `nextShapeId` is advanced past every id used; ids only have to be unique
// within one drawing part, and the caller shares the counter with the charts
// and pictures in that part.
QString buildShapeAnchorsXml(const QVector<SheetShape>& shapes,
                             const std::function<QString(const QByteArray&)>& fillRel,
                             int& nextShapeId);

// The file extension for `data`, sniffed from its first bytes: "png", "jpeg",
// "gif" or "bmp", and "png" when nothing matches.
//
// The extension is not cosmetic. A media part is typed by a <Default> on its
// extension, so calling a JPEG ".png" tells the reader to decode it as one.
// A picture inserted here is always PNG, but a picture that came out of some
// other file is whatever that file held.
QString imageExtension(const QByteArray& data);

// Does this chart carry enough to be written at all? A chart whose range is
// empty and whose series list is empty plots nothing, and emitting a chart
// part for it produces a file Excel refuses to open.
bool chartIsWritable(const ChartSpec& spec,
                     const std::function<QString(int, int)>& disp);

} // namespace NativeOffice
