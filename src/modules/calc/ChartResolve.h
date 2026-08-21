#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ChartResolve.h
// The header/category scan that turns a chart's contiguous `range` into the
// series a renderer or a file writer needs.
//
// A chart built inside NativeOffice is described by one rectangle of cells
// (ChartSpec::range). Deciding what that rectangle means - which row holds the
// series names, which column holds the category labels, where the numbers
// start - is a guess, and it has to be the SAME guess everywhere. It was
// previously made inside ChartObject::rebuild(), so the only thing that could
// act on it was the screen. Writing that chart into an .xlsx needs the same
// answer, expressed as cell references rather than as values, and a second
// copy of the rule would drift from the first the moment either changed.
//
// So the scan lives here, widget-free and file-free, and both callers use it.
// ─────────────────────────────────────────────────────────────────────────────

#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace NativeOffice {

// One series pulled out of the block: what to call it, what to plot, and the
// cells both came from. The ranges are what a chart part references; the
// values are what a renderer draws and what the file caches alongside the
// reference so another application can show the chart without recalculating.
struct RangeSeries {
    QString         name;
    QRect           nameCell;   // header cell the name came from, null if invented
    QRect           valRange;   // cells the numbers came from
    QVector<double> values;
};

struct RangeChart {
    bool headerRow { false };   // top row of the block holds series names
    bool catCol    { false };   // left column of the block holds category labels

    QStringList categories;
    QRect       catRange;       // null when the labels are invented (1, 2, 3, ...)

    QVector<RangeSeries> series;

    // The block's top-left cell, which is a caption when the block has both a
    // header row and a category column (that corner belongs to neither).
    QString cornerTitle;
};

// Scan `range`, reading cells through `disp(col, row)`.
//
// `disp` returns a cell's displayed text, which is what the scan classifies:
// a cell that shows text is a label, a cell that parses as a number is data.
// Empty ranges come back with no series.
RangeChart resolveRangeChart(const QRect& range,
                             const std::function<QString(int, int)>& disp);

} // namespace NativeOffice
