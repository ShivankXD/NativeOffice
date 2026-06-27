#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// WriterEquation.h  (Tier 4 — equation editor)
// A small TeX-subset math layout renderer. Lays out an expression into a
// transparent QImage that the editor embeds inline (so equations look right and
// persist in the .noff HTML as base64 images).
//
// Supported syntax:
//   • plain text & Unicode symbols (variables render italic)
//   • ^ and _ for super/subscripts (single char or {group})
//   • \frac{num}{den}, \sqrt{expr}, {grouping}
//   • a set of \commands → Greek letters & operators (\alpha, \sum, \int, …)
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>
#include <QImage>
#include <QColor>

namespace NativeOffice {

class EquationRenderer {
public:
    // Render to a transparent image; pixelSize is the base glyph height.
    static QImage render(const QString& expr, int pixelSize, const QColor& color);
};

} // namespace NativeOffice
