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
#include <QPointF>
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
    Chart,       // data chart — see ChartKind
};

// ── Chart kinds ──────────────────────────────────────────────────────────────
enum class ChartKind {
    Bar,
    Line,
    Pie,
    Area,
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

// ── Whole-slide entrance animation (applied via the Animations tab) ──────────
// Plays when the slide appears in the show: the entire slide animates in, then
// settles. Distinct from per-object ItemAnimation and from slide transitions.
enum class SlideAnimation {
    None,
    FadeIn,
    ZoomIn,
    WhirlIn,
    FlyInLeft,
    FlyInRight,
    FlyInTop,
    FlyInBottom,
    Bounce,
    RiseUp,
    SpiralIn,
    Drop,
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
    // Exit effects — the object animates away when advancing to the next slide.
    ExitFadeOut,
    ExitFlyLeft,
    ExitFlyRight,
    ExitZoomOut,
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
    // Vertical anchoring of text inside `rect` (PowerPoint's bodyPr anchor):
    // 0 = top, 1 = middle, 2 = bottom. Imported decks lean on this heavily —
    // without it, centred text sits at the top of its box.
    int           vAlign   { 0 };
    bool          isPlaceholder { false };
    QByteArray    imageData;            // PNG bytes, for Image items
    int           brightness { 0 };     // image adjustment (-100..100)
    int           contrast   { 0 };     // image adjustment (-100..100)
    ItemAnimation animation { ItemAnimation::None };  // entrance effect in slide show
    ShapeKind     shapeKind { ShapeKind::Rectangle }; // for Shape items
    // RoundedRect corner radius as a fraction of the shape's smaller side —
    // PowerPoint's roundRect "adj" guide (adj/100000). Its default is 0.16667,
    // but decks routinely ask for 0.02–0.07; ignoring it renders them far rounder
    // than authored, which reads as "square boxes came out round".
    qreal         cornerAdj { 0.16667 };
    bool          shadow    { false };  // soft drop shadow on this item
    qreal         opacity   { 1.0 };    // 0..1 object opacity
    QString       hyperlink;            // URL opened on click in slide show
    bool          locked    { false };  // fixed backdrop: not movable/selectable

    // ── Table items ───────────────────────────────────────────────────────
    int                  rows  { 0 };
    int                  cols  { 0 };
    std::vector<QString> cells;          // row-major, size rows*cols

    // ── Chart items ─────────────────────────────────────────────────────────
    ChartKind            chartKind { ChartKind::Bar };
    QString              chartTitle;
    std::vector<QString> chartLabels;
    std::vector<double>  chartValues;
};

// ── Review comment attached to a slide ───────────────────────────────────────
struct SlideComment {
    QString author;
    QString text;
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
    SlideAnimation         slideAnimation { SlideAnimation::None };
    QString                notes;
    std::vector<SlideItem> items;
    std::vector<SlideComment> comments;

    // ── Slide-number field (deck-wide toggle; each slide shows its own number) ─
    // Kept in sync across the deck by ImpressModule. slideNumberPos is the
    // top-left of the number in scene coords; an invalid point means "default
    // bottom-right".
    bool    showSlideNumber { false };
    QPointF slideNumberPos  { -1, -1 };
};

} // namespace NativeOffice
