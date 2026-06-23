#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// CalcIcons.h  (Sprint 25)
// Professional vector icon set for the Calc ribbon.
//
// Icons are authored as compact SVG line-art (24×24 viewBox, Office/Fluent style)
// and rendered crisply via QSvgRenderer into cached QIcons — NO font glyphs.
// Call calcIcon("name") to get a ready-to-use QIcon for a QToolButton.
// ─────────────────────────────────────────────────────────────────────────────

#include <QIcon>
#include <QString>
#include <QColor>

namespace NativeOffice {

// Returns a crisp vector icon for the given logical name (e.g. "cut", "paste",
// "chart-line"). Unknown names fall back to a neutral placeholder. Results are
// cached per (name,colour). Default colour is the neutral ribbon ink.
QIcon calcIcon(const QString& name, const QColor& color = QColor(0x42, 0x49, 0x54));

} // namespace NativeOffice
