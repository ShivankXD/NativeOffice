// ─────────────────────────────────────────────────────────────────────────────
// SlideScene.cpp  (Sprint 5)
// ─────────────────────────────────────────────────────────────────────────────
#include "SlideScene.h"

#include <QGraphicsRectItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsTextItem>
#include <QGraphicsSceneMouseEvent>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QColor>
#include <QTextCursor>
#include <QCursor>

namespace NativeOffice {

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

// ── Insert mode ───────────────────────────────────────────────────────────────
void SlideScene::setInsertMode(InsertMode mode) {
    m_insertMode = mode;
    // Change cursor for all views
    const Qt::CursorShape shape = (mode == InsertMode::None)
                                  ? Qt::ArrowCursor
                                  : Qt::CrossCursor;
    for (auto* v : views()) v->setCursor(shape);
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

// ── Item factory helpers ──────────────────────────────────────────────────────
void SlideScene::addTextBox(const QPointF& pos,
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
}

void SlideScene::addRectangle(const QRectF& rect) {
    auto* item = new QGraphicsRectItem(rect);
    item->setPen(QPen(QColor("#2C3140"), 2));
    item->setBrush(QBrush(QColor(44, 49, 64, 40)));   // translucent charcoal fill
    item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    addItem(item);
}

void SlideScene::addEllipse(const QRectF& rect) {
    auto* item = new QGraphicsEllipseItem(rect);
    item->setPen(QPen(QColor("#E8372A"), 2));
    item->setBrush(QBrush(QColor(232, 55, 42, 40)));  // translucent scarlet fill
    item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    addItem(item);
}

// ── Persistence ───────────────────────────────────────────────────────────────
void SlideScene::loadFromData(const SlideData& data) {
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
        switch (si.type) {
        case SlideItemType::TextBox:
            addTextBox(si.rect.topLeft(), si.text, si.fontSize);
            break;
        case SlideItemType::Rectangle:
            addRectangle(si.rect);
            break;
        case SlideItemType::Ellipse:
            addEllipse(si.rect);
            break;
        }
    }
}

void SlideScene::saveToData(SlideData& data) const {
    data.items.clear();
    if (auto* bg = backgroundItem())
        data.background = bg->brush().color();

    for (auto* it : items()) {
        if (it == backgroundItem()) continue;

        SlideItem si;
        if (auto* ti = qgraphicsitem_cast<QGraphicsTextItem*>(it)) {
            si.type     = SlideItemType::TextBox;
            si.rect     = QRectF(ti->pos(), QSizeF(ti->textWidth(), 0));
            si.text     = ti->toPlainText();
            si.fontSize = ti->font().pointSizeF();
        } else if (auto* ri = qgraphicsitem_cast<QGraphicsRectItem*>(it)) {
            si.type = SlideItemType::Rectangle;
            si.rect = ri->rect().translated(ri->pos());
        } else if (auto* ei = qgraphicsitem_cast<QGraphicsEllipseItem*>(it)) {
            si.type = SlideItemType::Ellipse;
            si.rect = ei->rect().translated(ei->pos());
        } else {
            continue;
        }
        data.items.push_back(si);
    }
}

} // namespace NativeOffice
