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
QByteArray buildChartPartXml(const ChartSpec& spec,
                             const QString& sheetName,
                             const std::function<QString(int, int)>& disp);

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
