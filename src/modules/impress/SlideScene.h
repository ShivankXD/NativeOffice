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
#include <QPointer>
#include <QGraphicsTextItem>   // complete type needed for QPointer<QGraphicsTextItem>

class QGraphicsItem;
class QGraphicsRectItem;
class QGraphicsEllipseItem;
class QGraphicsPixmapItem;

namespace NativeOffice {

enum class InsertMode {
    None,
    TextBox,
    Rectangle,
    Ellipse,
    Image,
    Shape,       // drag-insert a gallery shape (kind set via beginInsertShape)
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
    ~SlideScene() override;

    // Populate the scene from a SlideData object (used when switching slides)
    void loadFromData(const SlideData& data);

    // Serialize the scene items back into a SlideData (called before switching)
    void saveToData(SlideData& data) const;

    // ── Insert mode ───────────────────────────────────────────────────────
    void setInsertMode(InsertMode mode);
    [[nodiscard]] InsertMode insertMode() const noexcept { return m_insertMode; }

    // Enter Shape-insert mode for a specific gallery shape. The next drag (or
    // click) on the canvas places that shape.
    void beginInsertShape(ShapeKind kind);

    // Build the QPainterPath for a gallery shape within `rect` (scene units).
    static QPainterPath shapePath(ShapeKind kind, const QRectF& rect);

    // Insert an image (PNG bytes) centered on the slide
    void insertImage(const QByteArray& pngData);

    // Insert a rows×cols table, centred on the slide, ready to edit.
    void insertTable(int rows, int cols);

    // Drop a pre-arranged SmartArt diagram (shapes + labels) on the slide.
    void insertSmartArt(SmartArtKind kind);

    // Insert a chart (with sample data) centred on the slide.
    void insertChart(ChartKind kind);

    // Render a chart with QPainter — shared by the on-slide item and by PPTX
    // export (which rasterises it to an embedded image).
    static void paintChart(QPainter& p, const QRectF& rect, ChartKind kind,
                           const std::vector<QString>& labels,
                           const std::vector<double>& values, const QString& title);

    // Insert a ready-made text box (WordArt / Symbol / Slide Number / Date …),
    // centred horizontally near the top, then select it for editing.
    void insertPresetText(const QString& text, qreal fontSize = 28.0,
                          bool bold = false, const QColor& color = QColor("#1C1E26"));

    // ── Convenience: add default placeholders for a given layout ──────────
    void addDefaultPlaceholders();
    void applyLayout(SlideLayout layout);

    // ── Text formatting (operates on the currently focused text item) ─────
    [[nodiscard]] QGraphicsTextItem* activeTextItem() const;
    void deleteSelectedItem();

    // ── Selected-object styling (shapes / rectangles / ellipses) ──────────
    void setSelectedFill(const QColor& color);
    void setSelectedOutline(const QColor& color);
    void setSelectedOpacity(qreal opacity);
    void toggleSelectedShadow();
    [[nodiscard]] bool hasShapeSelection() const;

    // ── Entrance animation on the selected item (Sprint 13) ───────────────
    // Animation is stored on each QGraphicsItem via data(AnimationKey) so the
    // slide-show window can read it back without index correlation.
    static constexpr int AnimationKey = 0;
    static constexpr int HyperlinkKey = 1;   // QString URL stored on each item
    static constexpr int LockedKey    = 2;   // bool: non-movable backdrop element
    void setSelectedAnimation(ItemAnimation anim);
    [[nodiscard]] ItemAnimation selectedAnimation() const;
    [[nodiscard]] bool hasSelection() const;

    // ── Slide-number field ────────────────────────────────────────────────
    // Show/hide a movable, non-editable slide number. `number` is this slide's
    // 1-based position; `pos` is its top-left in scene coords (invalid ⇒ default
    // bottom-right). Managed deck-wide by ImpressModule — this item is NOT
    // serialised as a normal SlideItem.
    void setSlideNumber(bool show, int number, const QPointF& pos);

signals:
    void sceneModified();
    void insertModeLeft();
    void selectionInfoChanged();   // selection changed -> ribbon should re-sync
    void animationApplied(ItemAnimation anim);  // object animation set -> preview it
    void slideNumberMoved(const QPointF& scenePos);  // user dragged the number

protected:
    void mousePressEvent  (QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent   (QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void keyPressEvent    (QKeyEvent* event) override;
    void contextMenuEvent (QGraphicsSceneContextMenuEvent* event) override;

private slots:
    void onSelectionChanged();

private:
    QGraphicsTextItem*    addTextBox  (const QPointF& pos, const QString& placeholder = {}, qreal fontSize = 14.0);
    QGraphicsRectItem*    addRectangle(const QRectF&  rect);
    QGraphicsEllipseItem* addEllipse  (const QRectF&  rect);
    QGraphicsItem*        addImageItem(const QRectF& rect, const QByteArray& pngData);
    QGraphicsItem*        addShape    (const QRectF& rect, ShapeKind kind);
    QGraphicsItem*        addTable    (const QRectF& rect, int rows, int cols,
                                       const std::vector<QString>& texts = {});
    QGraphicsItem*        addChart    (const QRectF& rect, ChartKind kind, const QString& title,
                                       const std::vector<QString>& labels,
                                       const std::vector<double>& values);

    // Returns the slide background rect item (always the first item added)
    QGraphicsRectItem* backgroundItem() const;

    // Paint the background rect from m_bg1/m_bg2 (solid, or top→bottom gradient
    // when m_bg2 is valid).
    void applyBackgroundBrush();

    void rebuildHandles();
    void positionHandles();   // reposition existing handles without recreating them
    void clearHandles();
    void resizeTargetTo(QGraphicsItem* target, HandleRole role, const QPointF& scenePos);

    InsertMode     m_insertMode  { InsertMode::None };
    ShapeKind      m_pendingShape { ShapeKind::Rectangle };  // kind for InsertMode::Shape
    QColor         m_bg1 { Qt::white };   // background top / solid colour
    QColor         m_bg2;                  // background bottom (invalid = solid)
    QPointF        m_dragStart;
    QGraphicsItem* m_dragItem    { nullptr };   // temporary during drag

    void notifySlideNumberMoved(const QPointF& p);   // called by SlideNumberTextItem

    std::vector<SlideHandleItem*> m_handles;
    QGraphicsItem*                m_handleTarget { nullptr };
    // QPointer auto-nulls if the text item is destroyed by any path, so
    // activeTextItem() can never hand back a dangling item for a virtual call
    // (formatting/cursor ops) — a prior crash source.
    QPointer<QGraphicsTextItem>   m_lastTextItem { nullptr };
    QGraphicsItem*                m_slideNumberItem { nullptr };  // the page number
    bool                          m_suppressNumberSignal { false };

    friend class SlideHandleItem;
    friend class SlideNumberTextItem;
};

} // namespace NativeOffice
