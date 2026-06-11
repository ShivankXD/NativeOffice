#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SlideScene.h  (Sprint 5)
// QGraphicsScene subclass that renders one presentation slide.
//
// Slide dimensions: 960 × 540 px  (16:9 at 1x scale).
// A white background rectangle fills the entire scene rect.
//
// Insert modes:
//   None      – normal selection/move interaction
//   TextBox   – next click inserts an editable QGraphicsTextItem
//   Rectangle – next drag inserts a QGraphicsRectItem
//   Ellipse   – next drag inserts a QGraphicsEllipseItem
//
// Public signals:
//   sceneModified()  – emitted after any item add/move/edit
//   insertModeLeft() – emitted after a shape is placed (so toolbar can reset)
// ─────────────────────────────────────────────────────────────────────────────

#include "SlideData.h"

#include <QGraphicsScene>
#include <QPointF>
#include <QRectF>
#include <QColor>

class QGraphicsItem;
class QGraphicsRectItem;
class QGraphicsEllipseItem;
class QGraphicsTextItem;

namespace NativeOffice {

enum class InsertMode {
    None,
    TextBox,
    Rectangle,
    Ellipse,
};

class SlideScene : public QGraphicsScene {
    Q_OBJECT

public:
    // Standard 16:9 slide dimensions in scene units (pixels at 1x)
    static constexpr qreal SLIDE_W = 960.0;
    static constexpr qreal SLIDE_H = 540.0;

    explicit SlideScene(QObject* parent = nullptr);

    // Populate the scene from a SlideData object (used when switching slides)
    void loadFromData(const SlideData& data);

    // Serialize the scene items back into a SlideData (called before switching)
    void saveToData(SlideData& data) const;

    // ── Insert mode ───────────────────────────────────────────────────────
    void setInsertMode(InsertMode mode);
    [[nodiscard]] InsertMode insertMode() const noexcept { return m_insertMode; }

    // ── Convenience: add default title + subtitle placeholders ────────────
    void addDefaultPlaceholders();

signals:
    void sceneModified();
    void insertModeLeft();

protected:
    void mousePressEvent  (QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent   (QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    void addTextBox  (const QPointF& pos, const QString& placeholder = {}, qreal fontSize = 14.0);
    void addRectangle(const QRectF&  rect);
    void addEllipse  (const QRectF&  rect);

    // Returns the slide background rect item (always the first item added)
    QGraphicsRectItem* backgroundItem() const;

    InsertMode    m_insertMode  { InsertMode::None };
    QPointF       m_dragStart;
    QGraphicsItem* m_dragItem   { nullptr };   // temporary during drag
};

} // namespace NativeOffice
