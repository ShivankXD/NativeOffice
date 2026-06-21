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
    Rectangle,   // legacy primitive (kept for backward-compatible files)
    Ellipse,     // legacy primitive (kept for backward-compatible files)
    Image,
    Shape,       // generic vector shape — see ShapeKind
    Table,       // grid of editable text cells
};

// ── Shape gallery kinds (Shape items) ────────────────────────────────────────
// A single generic shape item renders any of these via a QPainterPath, and maps
// to a PowerPoint preset geometry (prstGeom) on export.
enum class ShapeKind {
    Rectangle,
    RoundedRect,
    Ellipse,
    Triangle,
    RightTriangle,
    Diamond,
    Pentagon,
    Hexagon,
    Star5,
    Arrow,        // right-pointing block arrow
    Chevron,
    Line,         // straight line (top-left → bottom-right of rect)
    Cloud,
    Heart,
};

// ── SmartArt diagram templates (decompose into shapes + text boxes) ──────────
// Not serialised as a unit: inserting one drops a pre-arranged set of ordinary
// Shape and TextBox items onto the slide, which then save/load like any other.
enum class SmartArtKind {
    ProcessFlow,
    Cycle,
    Hierarchy,
    Pyramid,
    BulletList,
    Venn,
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
    Push,      // new slide pushes the old one out to the left
    Wipe,      // new slide wipes in left-to-right over the old
    Zoom,      // new slide zooms up from the centre
    Cut,       // instant hard cut to the new slide
    Cover,     // new slide slides in from the right, over the old
    Uncover,   // old slide slides out to the left, revealing the new
    Dissolve,  // randomised block dissolve into the new slide
    Blinds,    // vertical blinds reveal the new slide
};

// ── Per-object animation (entrance + emphasis), played in slide show ─────────
enum class ItemAnimation {
    None,
    FadeIn,
    FlyInLeft,
    ZoomIn,
    FlyInRight,
    FlyInTop,
    FlyInBottom,
    SpinIn,
    // Emphasis effects — the object is already visible and draws attention to
    // itself when the slide opens.
    EmphasisPulse,
    EmphasisSpin,
    EmphasisBlink,
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
    ShapeKind     shapeKind { ShapeKind::Rectangle }; // for Shape items
    bool          shadow    { false };  // soft drop shadow on this item
    QString       hyperlink;            // URL opened on click in slide show

    // ── Table items ───────────────────────────────────────────────────────
    int                  rows  { 0 };
    int                  cols  { 0 };
    std::vector<QString> cells;          // row-major, size rows*cols
};

struct SlideData {
    QString               title { "Untitled Slide" };
    QColor                background { Qt::white };
    // Optional second colour: when valid, the slide background is a vertical
    // gradient from `background` (top) to `background2` (bottom). When invalid
    // (the default), the background is a solid `background` fill.
    QColor                background2 {};
    SlideLayout            layout { SlideLayout::Blank };
    SlideTransition        transition { SlideTransition::None };
    QString                notes;
    std::vector<SlideItem> items;
};

} // namespace NativeOffice
