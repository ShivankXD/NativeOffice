#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AiSlideLayout.h — turns one slide operation into placed items on the canvas.
//
// Split out of the agent because laying out a slide is the part that decides
// whether a deck looks designed or generated, and it is worth reading on its
// own.
//
// Four rules run through all of it.
//
// Text is measured, never assumed. The first version gave every bullet the same
// fixed height, so a bullet that wrapped to two lines ran straight through the
// one below it. Every block here is measured at its real width with real font
// metrics, and the type is stepped down until the content fits the space it has.
//
// Inline markdown is rendered, not printed. The model writes "**Turbocharging:**
// Forced induction", and a slide that shows the asterisks looks broken.
//
// The deck's theme decides every colour and both typefaces. Nothing in here
// names a colour of its own; a slide that invents one is a slide that will not
// match the one before it.
//
// Pictures are asked for, not placed. A photograph has to be fetched over the
// network, which cannot happen while a slide is being built, so the layout
// leaves a sized placeholder and records what should go in it. The agent fills
// them in as the images arrive.
// ─────────────────────────────────────────────────────────────────────────────

#include <QColor>
#include <QJsonObject>
#include <QRectF>
#include <QString>
#include <QVector>

#include "AiDeckTheme.h"
#include "AiSlideImage.h"
#include "SlideData.h"

namespace NativeOffice {

// The canvas everything is authored against.
inline constexpr qreal kSlideW = 960.0;
inline constexpr qreal kSlideH = 540.0;

// A picture the finished slide wants but does not yet have.
struct SlideImageRequest {
    QString        query;                              // what to search for
    int            itemIndex { -1 };                   // placeholder to replace
    int            markIndex { -1 };                   // decoration to retire with it
    QSize          size;                               // exact pixels to compose to
    ImageTreatment treatment { ImageTreatment::Plain };
    qreal          radius    { 0 };                    // rounded corners, in pixels
};

// Everything a slide needs to know beyond its own operation.
struct SlideBuildContext {
    DeckTheme theme;
    int       ordinal { 0 };   // position in the deck, zero based
};

// Builds a fully populated slide from one {"op":"slide",...} object.
// charactersWritten receives the visible text length, for the usage tally.
// imageRequests receives any pictures the layout wants fetched.
SlideData buildSlideFromOp(const QJsonObject& op, const SlideBuildContext& ctx,
                           int* charactersWritten,
                           QVector<SlideImageRequest>* imageRequests);

// Escapes and renders **bold**, *italic* and `code` as HTML. Exposed for tests.
QString inlineHtml(const QString& markdown);

// The same text with the markers removed and nothing added, for the plain-text
// fallback the outline view reads.
QString inlinePlain(const QString& markdown);

} // namespace NativeOffice
