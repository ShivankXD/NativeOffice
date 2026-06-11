#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SlideData.h  (Sprint 5)
// Plain data type that represents one presentation slide.
//
// Each SlideData is owned by the deck (ImpressModule).  The corresponding
// SlideScene is created on demand and cached.  This separation keeps the
// serialisable data layer independent of the Qt graphics layer.
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>
#include <QRectF>
#include <QColor>
#include <vector>

namespace NativeOffice {

// ── Item types that can live on a slide ──────────────────────────────────────
enum class SlideItemType {
    TextBox,
    Rectangle,
    Ellipse,
};

struct SlideItem {
    SlideItemType type     { SlideItemType::TextBox };
    QRectF        rect     { 0, 0, 200, 50 };
    QString       text;
    QColor        fillColor{ Qt::white };
    QColor        penColor { "#1C1E26" };
    qreal         penWidth { 1.5 };
    qreal         fontSize { 14.0 };   // pt, for TextBox items
    bool          isPlaceholder { false };
};

struct SlideData {
    QString               title { "Untitled Slide" };
    QColor                background { Qt::white };
    std::vector<SlideItem> items;
};

} // namespace NativeOffice
