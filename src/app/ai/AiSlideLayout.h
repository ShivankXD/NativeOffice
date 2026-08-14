#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AiSlideLayout.h — turns one slide operation into placed items on the canvas.
//
// Split out of the agent because laying out a slide is the part that decides
// whether a deck looks designed or generated, and it is worth reading on its
// own.
//
// Two rules run through all of it.
//
// Text is measured, never assumed. The first version gave every bullet the same
// fixed height, so a bullet that wrapped to two lines ran straight through the
// one below it. Every block here is measured at its real width with real font
// metrics, and the type is stepped down until the content fits the space it has.
//
// Inline markdown is rendered, not printed. The model writes "**Turbocharging:**
// Forced induction", and a slide that shows the asterisks looks broken.
// ─────────────────────────────────────────────────────────────────────────────

#include <QColor>
#include <QJsonObject>
#include <QString>

#include "SlideData.h"

namespace NativeOffice {

// The canvas everything is authored against.
inline constexpr qreal kSlideW = 960.0;
inline constexpr qreal kSlideH = 540.0;

// Builds a fully populated slide from one {"op":"slide",...} object.
// charactersWritten receives the visible text length, for the usage tally.
SlideData buildSlideFromOp(const QJsonObject& op, int* charactersWritten);

// Escapes and renders **bold**, *italic* and `code` as HTML. Exposed for tests.
QString inlineHtml(const QString& markdown);

// The same text with the markers removed and nothing added, for the plain-text
// fallback the outline view reads.
QString inlinePlain(const QString& markdown);

} // namespace NativeOffice
