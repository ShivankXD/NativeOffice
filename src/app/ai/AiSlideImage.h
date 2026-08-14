#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AiSlideImage.h — turns a downloaded photograph into a slide element.
//
// A picture dropped onto a slide at whatever size and aspect it happened to
// arrive in is what an amateur deck looks like. Three things have to happen
// first, and all three are done here rather than at paint time, because the
// scene stores an image at its pixel size and the .pptx export carries whatever
// bytes it is given:
//
//   1. Cover crop. The photo is scaled to fill its box completely and the
//      overflow is cut off, so nothing is ever stretched. The crop sits a
//      little above centre, because subjects almost always do.
//   2. Treatment. White text over an unmodified photograph is unreadable about
//      half the time and looks cheap the rest. A gradient scrim, an even
//      darkening or a duotone is baked into the pixels, so the type on top has
//      a surface it can be read against no matter what the photo turned out to
//      be.
//   3. Rounded corners, when the design asks for them, cut into the alpha
//      channel rather than faked with an overlay.
//
// The result is PNG, because that is the only format the scene's image loader
// is handed a hint for.
// ─────────────────────────────────────────────────────────────────────────────

#include <QByteArray>
#include <QSize>

#include "AiDeckTheme.h"

namespace NativeOffice {

enum class ImageTreatment {
    Plain,        // cover crop and nothing else: cards, grids, thumbnails
    ScrimBottom,  // darkness rising from the bottom, for a headline sitting there
    ScrimFull,    // an even darkening, for text centred over the whole frame
    ScrimSide,    // darkness from the left edge, for a half-bleed with text over it
    Duotone,      // luminance mapped between the theme's deep and its accent
    Tinted,       // a light accent wash, so a photo on a pale slide belongs to it
};

// Composes `source` (any format Qt can read) into a `target`-sized PNG.
// Returns an empty array if the bytes are not a readable image, which is the
// signal to leave the placeholder in place rather than draw something broken.
QByteArray composeSlideImage(const QByteArray& source, const QSize& target,
                             ImageTreatment treatment, const DeckTheme& theme,
                             qreal cornerRadius = 0);

} // namespace NativeOffice
