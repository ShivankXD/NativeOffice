#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// XlsxDrawing.h
// Parser for the parts of an .xlsx that put things ON a sheet rather than in a
// cell: xl/drawings/drawingN.xml (where an object sits) and xl/charts/chartN.xml
// (what a chart plots).
//
// Until this existed, opening a workbook that contained charts or pictures
// showed the cells and silently dropped everything drawn over them.
//
// This file deliberately knows nothing about ZIP. The caller has already opened
// the package, so it passes in the drawing XML, that drawing's relationship
// table, and a `fetch` callback for pulling any part the drawing points at.
// That keeps the parser testable on loose XML and keeps the ZIP reader private
// to XlsxIo.cpp.
// ─────────────────────────────────────────────────────────────────────────────

#include <QByteArray>
#include <QColor>
#include <QVector>
#include <QHash>
#include <QString>
#include <functional>
#include <vector>

#include "ChartSpec.h"

namespace NativeOffice {

// A workbook's colour scheme, keyed by the names DrawingML uses for it:
// dk1, lt1, dk2, lt2, accent1..accent6, hlink, folHlink.
//
// Most colours in a real drawing are scheme references, not literal RGB: across
// the test corpus schemeClr outnumbers srgbClr better than two to one. Without
// the theme every one of them has to be guessed, and a shape whose fill and
// text are both guessed is as likely to come out white on white as right.
using ThemeColors = QHash<QString, QColor>;

// Parse xl/theme/themeN.xml. Returns an empty map when the part is missing,
// which every caller treats as "fall back to the built-in palette".
ThemeColors parseThemeColors(const QByteArray& themeXml);

// Parse one sheet's drawing part.
//
//   drawingXml   contents of xl/drawings/drawingN.xml
//   rels         rId -> part path, already resolved to package-absolute paths
//                (for example "rId1" -> "xl/charts/chart1.xml")
//   fetch        returns the bytes of a part, or an empty array if absent
//   theme        resolved colour scheme, may be empty
//
// Charts, pictures and shapes are appended to the three output vectors. Groups
// are walked through: a group is not an object in itself, it is a coordinate
// space, so its members come out as ordinary objects carrying the sub-rect the
// group gives them. Anything still unrecognised is skipped rather than treated
// as an error, so one odd object never costs the whole sheet.
void parseSheetDrawing(const QByteArray& drawingXml,
                       const QHash<QString, QString>& rels,
                       const std::function<QByteArray(const QString&)>& fetch,
                       std::vector<ChartSpec>& outCharts,
                       std::vector<SheetImage>& outImages,
                       std::vector<SheetShape>& outShapes,
                       const ThemeColors& theme = {});

// Parse a single xl/charts/chartN.xml into `spec` (type, title, categories and
// series). Placement is not touched: that comes from the drawing anchor.
// Returns false when the part holds no plottable series.
// What a picture fill on a chart series reads as.
//
// Excel stretches the picture into each bar, so the bar shows the image's
// shading over its own height. `average` is the one colour it comes to;
// `stops` and `positions` are that shading sampled top to bottom, in the same
// parallel-array form SheetShape uses for a gradient.
struct FillPicture {
    QColor          average;
    QVector<QColor> stops;
    QVector<qreal>  positions;   // 0..1, parallel to stops
};

// `blipPicture` resolves a picture fill on a series, given the relationship id
// from its <a:blip r:embed>. A chart series can be filled with an image rather
// than a colour, and without this such a series has no colour at all and falls
// through to a default the file never asked for. Optional: a caller that
// cannot fetch parts just passes nothing.
bool parseChartPart(const QByteArray& chartXml, ChartSpec& spec,
                    const ThemeColors& theme = {},
                    const std::function<FillPicture(const QString&)>& blipPicture = {});

// EMU (English Metric Units, 914400 per inch) -> pixels at 96 DPI.
int emuToPx(qint64 emu);

} // namespace NativeOffice
