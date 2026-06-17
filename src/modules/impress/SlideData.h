#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SlideData.h  (Sprint 5 → Sprint 12)
// Plain data type that represents one presentation slide.
//
// Each SlideData is owned by the deck (ImpressModule).  The corresponding
// SlideScene is created on demand and cached.  This separation keeps the
// serialisable data layer independent of the Qt graphics layer.
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>
#include <QRectF>
#include <QColor>
#include <QByteArray>
#include <vector>

namespace NativeOffice {

// ── Item types that can live on a slide ──────────────────────────────────────
enum class SlideItemType {
    TextBox,
    Rectangle,
    Ellipse,
    Image,
};

// ── Slide-level layout kind (controls default placeholders) ─────────────────
enum class SlideLayout {
    Title,          // centered title + subtitle
    TitleContent,   // title bar + body content placeholder
    Blank,          // no placeholders
};

// ── Slide transition (Sprint 12 → 13) ───────────────────────────────────────
enum class SlideTransition {
    None,
    Fade,
    Push,    // new slide pushes the old one out to the left
    Wipe,    // new slide wipes in left-to-right over the old
    Zoom,    // new slide zooms up from the centre
};

// ── Per-object entrance animation (Sprint 13) ────────────────────────────────
enum class ItemAnimation {
    None,
    FadeIn,
    FlyInLeft,
    ZoomIn,
};

struct SlideItem {
    SlideItemType type     { SlideItemType::TextBox };
    QRectF        rect     { 0, 0, 200, 50 };
    qreal         rotation { 0.0 };             // degrees, clockwise
    QString       text;                          // plain-text fallback (outline view)
    QString       html;                          // rich text content for TextBox items
    QColor        fillColor{ Qt::white };
    QColor        penColor { "#1C1E26" };
    qreal         penWidth { 1.5 };
    qreal         fontSize { 14.0 };   // pt, for TextBox items
    bool          isPlaceholder { false };
    QByteArray    imageData;            // PNG bytes, for Image items
    ItemAnimation animation { ItemAnimation::None };  // entrance effect in slide show
};

struct SlideData {
    QString               title { "Untitled Slide" };
    QColor                background { Qt::white };
    SlideLayout            layout { SlideLayout::Blank };
    SlideTransition        transition { SlideTransition::None };
    QString                notes;
    std::vector<SlideItem> items;
};

} // namespace NativeOffice
