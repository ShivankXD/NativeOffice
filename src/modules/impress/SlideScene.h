#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SlideScene.h  (Sprint 5 → Sprint 12)
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
//   Image     – inserted directly via insertImage(), no drag needed
//
// Selected items show resize handles (corners) and a rotate handle. Handles
// are plain QGraphicsScene items (not children of the target) so coordinate
// math stays in scene space.
//
// Public signals:
//   sceneModified()  – emitted after any item add/move/edit/resize/rotate
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
class QGraphicsPixmapItem;

namespace NativeOffice {

enum class InsertMode {
    None,
    TextBox,
    Rectangle,
    Ellipse,
    Image,
};

class SlideHandleItem;

enum class HandleRole {
    TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left, Rotate
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

    // Insert an image (PNG bytes) centered on the slide
    void insertImage(const QByteArray& pngData);

    // ── Convenience: add default placeholders for a given layout ──────────
    void addDefaultPlaceholders();
    void applyLayout(SlideLayout layout);

    // ── Text formatting (operates on the currently focused text item) ─────
    [[nodiscard]] QGraphicsTextItem* activeTextItem() const;
    void deleteSelectedItem();

    // ── Entrance animation on the selected item (Sprint 13) ───────────────
    // Animation is stored on each QGraphicsItem via data(AnimationKey) so the
    // slide-show window can read it back without index correlation.
    static constexpr int AnimationKey = 0;
    void setSelectedAnimation(ItemAnimation anim);
    [[nodiscard]] ItemAnimation selectedAnimation() const;
    [[nodiscard]] bool hasSelection() const;

signals:
    void sceneModified();
    void insertModeLeft();
    void selectionInfoChanged();   // selection changed -> ribbon should re-sync

protected:
    void mousePressEvent  (QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent   (QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void keyPressEvent    (QKeyEvent* event) override;

private slots:
    void onSelectionChanged();

private:
    QGraphicsTextItem*    addTextBox  (const QPointF& pos, const QString& placeholder = {}, qreal fontSize = 14.0);
    QGraphicsRectItem*    addRectangle(const QRectF&  rect);
    QGraphicsEllipseItem* addEllipse  (const QRectF&  rect);
    QGraphicsItem*        addImageItem(const QRectF& rect, const QByteArray& pngData);

    // Returns the slide background rect item (always the first item added)
    QGraphicsRectItem* backgroundItem() const;

    void rebuildHandles();
    void clearHandles();
    void resizeTargetTo(QGraphicsItem* target, HandleRole role, const QPointF& scenePos);

    InsertMode     m_insertMode  { InsertMode::None };
    QPointF        m_dragStart;
    QGraphicsItem* m_dragItem    { nullptr };   // temporary during drag

    std::vector<SlideHandleItem*> m_handles;
    QGraphicsItem*                m_handleTarget { nullptr };
    QGraphicsTextItem*            m_lastTextItem { nullptr };

    friend class SlideHandleItem;
};

} // namespace NativeOffice
