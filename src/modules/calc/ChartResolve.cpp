// ─────────────────────────────────────────────────────────────────────────────
// ChartResolve.cpp
// See ChartResolve.h. This is the scan that used to sit inside
// ChartObject::rebuild(); it is unchanged in behaviour, only moved somewhere
// the .xlsx writer can reach it too.
// ─────────────────────────────────────────────────────────────────────────────
#include "ChartResolve.h"

namespace NativeOffice {

RangeChart resolveRangeChart(const QRect& range,
                             const std::function<QString(int, int)>& disp) {
    RangeChart out;
    if (range.isNull() || !disp) return out;

    const int c1 = range.left(), c2 = range.right();
    const int r1 = range.top(),  r2 = range.bottom();

    auto isNum = [&](int c, int r) {
        const QString d = disp(c, r);
        if (d.isEmpty()) return false;
        bool ok = false; d.toDouble(&ok); return ok;
    };

    // Header row: top row carries text labels and there is data below it.
    if (r2 > r1)
        for (int c = c1; c <= c2; ++c)
            if (!disp(c, r1).isEmpty() && !isNum(c, r1)) { out.headerRow = true; break; }
    const int firstDataRow = out.headerRow ? r1 + 1 : r1;

    // Category column: left column carries text labels.
    if (c2 > c1)
        for (int r = firstDataRow; r <= r2; ++r)
            if (!disp(c1, r).isEmpty() && !isNum(c1, r)) { out.catCol = true; break; }
    const int firstDataCol = out.catCol ? c1 + 1 : c1;

    for (int r = firstDataRow; r <= r2; ++r)
        out.categories << (out.catCol ? disp(c1, r)
                                      : QString::number(r - firstDataRow + 1));
    // Invented labels have no cells behind them, so they stay a cached list
    // with no reference. A file writer has to notice that: pointing a chart at
    // the numbers it just made up would plot the wrong column.
    if (out.catCol) out.catRange = QRect(QPoint(c1, firstDataRow), QPoint(c1, r2));

    for (int c = firstDataCol; c <= c2; ++c) {
        RangeSeries s;
        s.name = out.headerRow ? disp(c, r1) : QString();
        if (out.headerRow && !s.name.isEmpty()) s.nameCell = QRect(c, r1, 1, 1);
        if (s.name.isEmpty()) s.name = QString("Series %1").arg(c - firstDataCol + 1);
        s.valRange = QRect(QPoint(c, firstDataRow), QPoint(c, r2));
        for (int r = firstDataRow; r <= r2; ++r) {
            bool ok = false; const double val = disp(c, r).toDouble(&ok);
            s.values << (ok ? val : 0.0);
        }
        out.series << s;
    }

    if (out.headerRow && out.catCol) out.cornerTitle = disp(c1, r1);
    return out;
}

} // namespace NativeOffice
