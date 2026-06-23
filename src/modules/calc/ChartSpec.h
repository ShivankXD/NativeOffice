#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ChartSpec.h  (Sprint 26 — Charts)
// Lightweight, widget-free description of a chart so the model can store charts
// without depending on Qt Widgets / Qt Charts.
// ─────────────────────────────────────────────────────────────────────────────
#include <QRect>
#include <QString>

namespace NativeOffice {

enum class ChartType { Column = 0, Bar, Line, Area, Pie, Scatter };

// Serializable description of a chart (what + where).
struct ChartSpec {
    ChartType type { ChartType::Column };
    QRect     range;     // data range in cells (left=col1 … bottom=row2)
    QRect     geom;      // widget geometry in viewport pixels (x,y,w,h)
};

QString   chartTypeName(ChartType t);
ChartType chartTypeFromInt(int v);

} // namespace NativeOffice
