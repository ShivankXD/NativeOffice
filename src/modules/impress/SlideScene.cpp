// ─────────────────────────────────────────────────────────────────────────────
// SlideScene.cpp  (Sprint 5 → Sprint 12)
// ─────────────────────────────────────────────────────────────────────────────
#include "SlideScene.h"

#include <QGraphicsRectItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsTextItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsSceneMouseEvent>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QColor>
#include <QTextCursor>
#include <QCursor>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QBuffer>
#include <QtMath>
#include <cmath>

namespace NativeOffice {

// ─────────────────────────────────────────────────────────────────────────────
// SlideImageItem — QGraphicsPixmapItem that remembers its full-res source so
// repeated resizes don't compound quality loss.
// ─────────────────────────────────────────────────────────────────────────────
class SlideImageItem : public QGraphicsPixmapItem {
public:
    explicit SlideImageItem(const QPixmap& original) : m_original(original) {}
    QPixmap original() const { return m_original; }
private:
    QPixmap m_original;
};

// ─────────────────────────────────────────────────────────────────────────────
// SlideHandleItem — small square (or circular, for rotate) handle drawn on
// top of the currently-selected item. Lives directly in the scene (not as a
// child of the target) so its own geometry stays simple to reason about.
// ─────────────────────────────────────────────────────────────────────────────
class SlideHandleItem : public QGraphicsRectItem {
public:
    SlideHandleItem(HandleRole role, QGraphicsItem* target, SlideScene* scene)
        : QGraphicsRectItem(-5, -5, 10, 10)
        , m_role(role)
        , m_target(target)
        , m_scene(scene)
    {
        setZValue(1000.0);
        setFlag(QGraphicsItem::ItemIsSelectable, false);
        if (role == HandleRole::Rotate) {
            setBrush(QColor("#2C3140"));
            setPen(QPen(QColor("#E8372A"), 1.5));
            setCursor(Qt::OpenHandCursor);
        } else {
            setBrush(QColor("#FFFFFF"));
            setPen(QPen(QColor("#E8372A"), 1.5));
            setCursor(cursorFor(role));
        }
    }

    HandleRole role() const { return m_role; }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override {
        if (!m_target) return;
        m_pressAngle    = angleFromCenter(e->scenePos());
        m_startRotation = m_target->rotation();
        e->accept();
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override {
        if (!m_target || !m_scene) return;
        if (m_role == HandleRole::Rotate) {
            const qreal ang   = angleFromCenter(e->scenePos());
            const qreal delta = ang - m_pressAngle;
            m_target->setTransformOriginPoint(m_target->boundingRect().center());
            m_target->setRotation(m_startRotation + delta);
        } else {
            m_scene->resizeTargetTo(m_target, m_role, e->scenePos());
        }
        m_scene->rebuildHandles();
        e->accept();
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override {
        if (m_scene) emit m_scene->sceneModified();
        e->accept();
    }

private:
    qreal angleFromCenter(const QPointF& scenePos) const {
        const QPointF center = m_target->mapToScene(m_target->boundingRect().center());
        const QPointF v = scenePos - center;
        return qRadiansToDegrees(std::atan2(v.y(), v.x()));
    }

    static Qt::CursorShape cursorFor(HandleRole role) {
        switch (role) {
        case HandleRole::TopLeft:
        case HandleRole::BottomRight: return Qt::SizeFDiagCursor;
        case HandleRole::TopRight:
        case HandleRole::BottomLeft:  return Qt::SizeBDiagCursor;
        case HandleRole::Top:
        case HandleRole::Bottom:      return Qt::SizeVerCursor;
        case HandleRole::Left:
        case HandleRole::Right:       return Qt::SizeHorCursor;
        default:                       return Qt::ArrowCursor;
        }
    }

    HandleRole     m_role;
    QGraphicsItem* m_target;
    SlideScene*    m_scene;
    qreal          m_pressAngle    { 0.0 };
    qreal          m_startRotation { 0.0 };
};

// ── Construction ─────────────────────────────────────────────────────────────
SlideScene::SlideScene(QObject* parent)
    : QGraphicsScene(0, 0, SLIDE_W, SLIDE_H, parent)
{
    setBackgroundBrush(QColor("#E8E9ED"));  // same canvas grey as Writer

    // White slide surface — always the first item, z = -1
    auto* bg = new QGraphicsRectItem(0, 0, SLIDE_W, SLIDE_H);
    bg->setBrush(Qt::white);
    bg->setPen(Qt::NoPen);
    bg->setZValue(-1.0);
    bg->setFlag(QGraphicsItem::ItemIsSelectable, false);
    bg->setFlag(QGraphicsItem::ItemIsMovable,    false);
    addItem(bg);

    connect(this, &QGraphicsScene::selectionChanged, this, &SlideScene::onSelectionChanged);
    connect(this, &QGraphicsScene::focusItemChanged, this,
            [this](QGraphicsItem* newItem, QGraphicsItem*, Qt::FocusReason) {
        if (auto* ti = qgraphicsitem_cast<QGraphicsTextItem*>(newItem))
            m_lastTextItem = ti;
    });
}

// ── Background accessor ───────────────────────────────────────────────────────
QGraphicsRectItem* SlideScene::backgroundItem() const {
    for (auto* item : items()) {
        if (auto* r = qgraphicsitem_cast<QGraphicsRectItem*>(item)) {
            if (r->zValue() < 0) return r;
        }
    }
    return nullptr;
}

// ── Default placeholders ──────────────────────────────────────────────────────
void SlideScene::addDefaultPlaceholders() {
    // Title
    addTextBox(QPointF(80, 160), "Click to add Title",    40.0);
    // Subtitle
    addTextBox(QPointF(80, 310), "Click to add Subtitle", 22.0);
}

void SlideScene::applyLayout(SlideLayout layout) {
    m_lastTextItem = nullptr;
    // Clear all non-background items first
    const QList<QGraphicsItem*> all = items();
    for (auto* it : all) {
        if (it != backgroundItem()) {
            removeItem(it);
            delete it;
        }
    }
    clearHandles();

    switch (layout) {
    case SlideLayout::Title:
        addTextBox(QPointF(80, 160), "Click to add Title",    40.0);
        addTextBox(QPointF(80, 310), "Click to add Subtitle", 22.0);
        break;
    case SlideLayout::TitleContent:
        addTextBox(QPointF(60, 40),  "Click to add Title",   32.0);
        addTextBox(QPointF(60, 130), "Click to add Text",    18.0);
        break;
    case SlideLayout::Blank:
    default:
        break;
    }

    emit sceneModified();
}

// ── Insert mode ───────────────────────────────────────────────────────────────
void SlideScene::setInsertMode(InsertMode mode) {
    m_insertMode = mode;
    // Change cursor for all views
    const Qt::CursorShape shape = (mode == InsertMode::None)
                                  ? Qt::ArrowCursor
                                  : Qt::CrossCursor;
    for (auto* v : views()) v->setCursor(shape);
}

void SlideScene::insertImage(const QByteArray& pngData) {
    QPixmap pm;
    if (!pm.loadFromData(pngData, "PNG")) return;

    // Fit within a reasonable default box, preserving aspect ratio
    QSizeF size = pm.size();
    const qreal maxW = 480.0, maxH = 360.0;
    const qreal scale = std::min({1.0, maxW / size.width(), maxH / size.height()});
    size *= scale;

    const QRectF rect(QPointF((SLIDE_W - size.width()) / 2.0,
                               (SLIDE_H - size.height()) / 2.0),
                       size);
    addImageItem(rect, pngData);
    emit sceneModified();
}

// ── Mouse events ──────────────────────────────────────────────────────────────
void SlideScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() != Qt::LeftButton || m_insertMode == InsertMode::None) {
        QGraphicsScene::mousePressEvent(event);
        return;
    }

    m_dragStart = event->scenePos();

    if (m_insertMode == InsertMode::TextBox) {
        // Immediately place; no drag needed for text boxes
        addTextBox(m_dragStart);
        setInsertMode(InsertMode::None);
        emit insertModeLeft();
        emit sceneModified();
        return;
    }

    // For rect / ellipse: show a zero-size preview item during drag
    const QRectF r(m_dragStart, QSizeF(1, 1));
    if (m_insertMode == InsertMode::Rectangle) {
        auto* rect = new QGraphicsRectItem(r);
        rect->setPen(QPen(QColor("#E8372A"), 2, Qt::DashLine));
        rect->setBrush(Qt::NoBrush);
        m_dragItem = rect;
    } else {
        auto* ell = new QGraphicsEllipseItem(r);
        ell->setPen(QPen(QColor("#E8372A"), 2, Qt::DashLine));
        ell->setBrush(Qt::NoBrush);
        m_dragItem = ell;
    }
    addItem(m_dragItem);
    event->accept();
}

void SlideScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (m_insertMode == InsertMode::None || !m_dragItem) {
        QGraphicsScene::mouseMoveEvent(event);
        return;
    }

    const QPointF cur = event->scenePos();
    const QRectF rect = QRectF(m_dragStart, cur).normalized();

    if (auto* ri = qgraphicsitem_cast<QGraphicsRectItem*>(m_dragItem))
        ri->setRect(rect);
    else if (auto* ei = qgraphicsitem_cast<QGraphicsEllipseItem*>(m_dragItem))
        ei->setRect(rect);

    event->accept();
}

void SlideScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (m_insertMode == InsertMode::None || !m_dragItem) {
        QGraphicsScene::mouseReleaseEvent(event);
        return;
    }

    const QPointF cur = event->scenePos();
    QRectF rect = QRectF(m_dragStart, cur).normalized();

    // Minimum size so a click (no drag) still produces a visible shape
    if (rect.width()  < 20) rect.setWidth(160);
    if (rect.height() < 20) rect.setHeight(90);

    // Remove the preview dash-line item
    removeItem(m_dragItem);
    delete m_dragItem;
    m_dragItem = nullptr;

    if (m_insertMode == InsertMode::Rectangle) {
        addRectangle(rect);
    } else if (m_insertMode == InsertMode::Ellipse) {
        addEllipse(rect);
    }

    setInsertMode(InsertMode::None);
    emit insertModeLeft();
    emit sceneModified();
    event->accept();
}

void SlideScene::keyPressEvent(QKeyEvent* event) {
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) &&
        !selectedItems().isEmpty()) {
        // Don't eat the key while editing text (let it edit the text instead)
        bool editingText = false;
        for (auto* it : selectedItems()) {
            if (auto* ti = qgraphicsitem_cast<QGraphicsTextItem*>(it)) {
                if (ti->textInteractionFlags() & Qt::TextEditable) { editingText = true; break; }
            }
        }
        if (!editingText) {
            deleteSelectedItem();
            event->accept();
            return;
        }
    }
    QGraphicsScene::keyPressEvent(event);
}

QGraphicsTextItem* SlideScene::activeTextItem() const {
    if (auto* fi = qgraphicsitem_cast<QGraphicsTextItem*>(focusItem()))
        return fi;
    return m_lastTextItem;
}

void SlideScene::deleteSelectedItem() {
    const QList<QGraphicsItem*> sel = selectedItems();
    auto* bg = backgroundItem();
    for (auto* it : sel) {
        if (it == bg) continue;
        if (it == m_lastTextItem) m_lastTextItem = nullptr;
        removeItem(it);
        delete it;
    }
    clearHandles();
    emit sceneModified();
}

void SlideScene::setSelectedAnimation(ItemAnimation anim) {
    bool changed = false;
    for (auto* it : selectedItems()) {
        if (it == backgroundItem()) continue;
        it->setData(AnimationKey, static_cast<int>(anim));
        changed = true;
    }
    if (changed) emit sceneModified();
}

ItemAnimation SlideScene::selectedAnimation() const {
    for (auto* it : selectedItems()) {
        if (it == backgroundItem()) continue;
        return static_cast<ItemAnimation>(it->data(AnimationKey).toInt());
    }
    return ItemAnimation::None;
}

bool SlideScene::hasSelection() const {
    for (auto* it : selectedItems())
        if (it != backgroundItem()) return true;
    return false;
}

// ── Selection → handle overlay ─────────────────────────────────────────────────
void SlideScene::onSelectionChanged() {
    rebuildHandles();
    emit selectionInfoChanged();
}

void SlideScene::clearHandles() {
    for (auto* h : m_handles) {
        removeItem(h);
        delete h;
    }
    m_handles.clear();
    m_handleTarget = nullptr;
}

void SlideScene::rebuildHandles() {
    clearHandles();

    const QList<QGraphicsItem*> sel = selectedItems();
    if (sel.size() != 1) return;

    QGraphicsItem* target = sel.first();
    if (target == backgroundItem()) return;

    m_handleTarget = target;
    const QRectF r = target->boundingRect();

    const bool isText = qgraphicsitem_cast<QGraphicsTextItem*>(target) != nullptr;

    auto place = [&](HandleRole role, const QPointF& localPt) {
        auto* h = new SlideHandleItem(role, target, this);
        h->setPos(target->mapToScene(localPt));
        addItem(h);
        m_handles.push_back(h);
    };

    if (isText) {
        // Text boxes only support width-resize from the right edge
        place(HandleRole::Right,       QPointF(r.right(), r.center().y()));
        place(HandleRole::TopRight,    r.topRight());
        place(HandleRole::BottomRight, r.bottomRight());
    } else {
        place(HandleRole::TopLeft,     r.topLeft());
        place(HandleRole::Top,         QPointF(r.center().x(), r.top()));
        place(HandleRole::TopRight,    r.topRight());
        place(HandleRole::Right,       QPointF(r.right(), r.center().y()));
        place(HandleRole::BottomRight, r.bottomRight());
        place(HandleRole::Bottom,      QPointF(r.center().x(), r.bottom()));
        place(HandleRole::BottomLeft,  r.bottomLeft());
        place(HandleRole::Left,        QPointF(r.left(), r.center().y()));
    }

    place(HandleRole::Rotate, QPointF(r.center().x(), r.top() - 26));
}

void SlideScene::resizeTargetTo(QGraphicsItem* target, HandleRole role, const QPointF& scenePos) {
    if (!target) return;
    const QPointF local = target->mapFromScene(scenePos);

    if (auto* ti = qgraphicsitem_cast<QGraphicsTextItem*>(target)) {
        const qreal newWidth = std::max(40.0, local.x());
        ti->setTextWidth(newWidth);
        return;
    }

    QRectF r;
    if (auto* ri = qgraphicsitem_cast<QGraphicsRectItem*>(target)) {
        r = ri->rect();
    } else if (auto* ei = qgraphicsitem_cast<QGraphicsEllipseItem*>(target)) {
        r = ei->rect();
    } else if (qgraphicsitem_cast<QGraphicsPixmapItem*>(target)) {
        r = target->boundingRect();
    } else {
        return;
    }

    switch (role) {
    case HandleRole::TopLeft:     r.setTopLeft(local);     break;
    case HandleRole::Top:         r.setTop(local.y());     break;
    case HandleRole::TopRight:    r.setTopRight(local);    break;
    case HandleRole::Right:       r.setRight(local.x());   break;
    case HandleRole::BottomRight: r.setBottomRight(local); break;
    case HandleRole::Bottom:      r.setBottom(local.y());  break;
    case HandleRole::BottomLeft:  r.setBottomLeft(local);  break;
    case HandleRole::Left:        r.setLeft(local.x());    break;
    default: break;
    }
    r = r.normalized();
    if (r.width()  < 20) r.setWidth(20);
    if (r.height() < 20) r.setHeight(20);

    if (auto* ri = qgraphicsitem_cast<QGraphicsRectItem*>(target)) {
        ri->setRect(r);
    } else if (auto* ei = qgraphicsitem_cast<QGraphicsEllipseItem*>(target)) {
        ei->setRect(r);
    } else if (auto* pi = qgraphicsitem_cast<QGraphicsPixmapItem*>(target)) {
        pi->setOffset(r.topLeft());
        QPixmap src;
        if (auto* simg = dynamic_cast<SlideImageItem*>(pi))
            src = simg->original();
        else
            src = pi->pixmap();
        pi->setPixmap(src.scaled(r.size().toSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }
}

// ── Item factory helpers ──────────────────────────────────────────────────────
QGraphicsTextItem* SlideScene::addTextBox(const QPointF& pos,
                                           const QString& placeholder,
                                           qreal          fontSize) {
    auto* item = new QGraphicsTextItem;
    item->setPlainText(placeholder.isEmpty() ? "Text" : placeholder);
    item->setPos(pos);
    item->setTextWidth(800);

    QFont f("Segoe UI", static_cast<int>(fontSize));
    item->setFont(f);
    item->setDefaultTextColor(placeholder.isEmpty() ? QColor("#1C1E26")
                                                     : QColor("#9CA3AF"));

    item->setFlags(QGraphicsItem::ItemIsSelectable |
                   QGraphicsItem::ItemIsMovable    |
                   QGraphicsItem::ItemIsFocusable);
    item->setTextInteractionFlags(Qt::TextEditorInteraction);

    // Connect content change to scene modification
    connect(item->document(), &QTextDocument::contentsChanged,
            this, &SlideScene::sceneModified);

    addItem(item);
    return item;
}

QGraphicsRectItem* SlideScene::addRectangle(const QRectF& rect) {
    auto* item = new QGraphicsRectItem(rect);
    item->setPen(QPen(QColor("#2C3140"), 2));
    item->setBrush(QBrush(QColor(44, 49, 64, 40)));   // translucent charcoal fill
    item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    addItem(item);
    return item;
}

QGraphicsEllipseItem* SlideScene::addEllipse(const QRectF& rect) {
    auto* item = new QGraphicsEllipseItem(rect);
    item->setPen(QPen(QColor("#E8372A"), 2));
    item->setBrush(QBrush(QColor(232, 55, 42, 40)));  // translucent scarlet fill
    item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    addItem(item);
    return item;
}

QGraphicsItem* SlideScene::addImageItem(const QRectF& rect, const QByteArray& pngData) {
    QPixmap original;
    if (!original.loadFromData(pngData, "PNG")) return nullptr;

    auto* item = new SlideImageItem(original);
    item->setPixmap(original.scaled(rect.size().toSize(), Qt::IgnoreAspectRatio,
                                     Qt::SmoothTransformation));
    item->setOffset(rect.topLeft());
    item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    addItem(item);
    return item;
}

// ── Persistence ───────────────────────────────────────────────────────────────
void SlideScene::loadFromData(const SlideData& data) {
    clearHandles();
    m_lastTextItem = nullptr;

    // Clear everything except the background
    const QList<QGraphicsItem*> all = items();
    for (auto* it : all) {
        if (it != backgroundItem()) {
            removeItem(it);
            delete it;
        }
    }

    // Restore background color
    if (auto* bg = backgroundItem())
        bg->setBrush(data.background);

    // Recreate items
    for (const auto& si : data.items) {
        QGraphicsItem* created = nullptr;
        switch (si.type) {
        case SlideItemType::TextBox: {
            auto* ti = addTextBox(si.rect.topLeft(), si.text, si.fontSize);
            if (ti) {
                if (!si.html.isEmpty()) ti->setHtml(si.html);
                ti->setTextWidth(si.rect.width() > 0 ? si.rect.width() : 800);
                ti->setTransformOriginPoint(ti->boundingRect().center());
                ti->setRotation(si.rotation);
                ti->setDefaultTextColor(si.isPlaceholder ? QColor("#9CA3AF") : QColor(si.penColor));
            }
            created = ti;
            break;
        }
        case SlideItemType::Rectangle:
            if (auto* ri = addRectangle(si.rect)) {
                ri->setPen(QPen(si.penColor, si.penWidth));
                ri->setBrush(si.fillColor);
                ri->setTransformOriginPoint(ri->boundingRect().center());
                ri->setRotation(si.rotation);
                created = ri;
            }
            break;
        case SlideItemType::Ellipse:
            if (auto* ei = addEllipse(si.rect)) {
                ei->setPen(QPen(si.penColor, si.penWidth));
                ei->setBrush(si.fillColor);
                ei->setTransformOriginPoint(ei->boundingRect().center());
                ei->setRotation(si.rotation);
                created = ei;
            }
            break;
        case SlideItemType::Image:
            if (auto* pi = addImageItem(si.rect, si.imageData)) {
                pi->setTransformOriginPoint(pi->boundingRect().center());
                pi->setRotation(si.rotation);
                created = pi;
            }
            break;
        }
        if (created)
            created->setData(AnimationKey, static_cast<int>(si.animation));
    }
}

void SlideScene::saveToData(SlideData& data) const {
    data.items.clear();
    if (auto* bg = backgroundItem())
        data.background = bg->brush().color();

    for (auto* it : items()) {
        if (it == backgroundItem()) continue;
        if (dynamic_cast<SlideHandleItem*>(it)) continue;

        SlideItem si;
        si.rotation  = it->rotation();
        si.animation = static_cast<ItemAnimation>(it->data(AnimationKey).toInt());

        if (auto* ti = qgraphicsitem_cast<QGraphicsTextItem*>(it)) {
            si.type     = SlideItemType::TextBox;
            si.rect     = QRectF(ti->pos(), QSizeF(ti->textWidth(), ti->boundingRect().height()));
            si.text     = ti->toPlainText();
            si.html     = ti->toHtml();
            si.fontSize = ti->font().pointSizeF();
            si.penColor = ti->defaultTextColor();
            si.isPlaceholder = (ti->defaultTextColor() == QColor("#9CA3AF"));
        } else if (auto* ri = qgraphicsitem_cast<QGraphicsRectItem*>(it)) {
            si.type      = SlideItemType::Rectangle;
            si.rect      = ri->rect().translated(ri->pos());
            si.fillColor = ri->brush().color();
            si.penColor  = ri->pen().color();
            si.penWidth  = ri->pen().widthF();
        } else if (auto* ei = qgraphicsitem_cast<QGraphicsEllipseItem*>(it)) {
            si.type      = SlideItemType::Ellipse;
            si.rect      = ei->rect().translated(ei->pos());
            si.fillColor = ei->brush().color();
            si.penColor  = ei->pen().color();
            si.penWidth  = ei->pen().widthF();
        } else if (auto* pi = qgraphicsitem_cast<QGraphicsPixmapItem*>(it)) {
            si.type = SlideItemType::Image;
            si.rect = QRectF(pi->pos() + pi->offset(), pi->pixmap().size());

            QPixmap src = pi->pixmap();
            if (auto* simg = dynamic_cast<SlideImageItem*>(pi)) src = simg->original();
            QByteArray bytes;
            QBuffer buf(&bytes);
            buf.open(QIODevice::WriteOnly);
            src.save(&buf, "PNG");
            si.imageData = bytes;
        } else {
            continue;
        }
        data.items.push_back(si);
    }
}

} // namespace NativeOffice
