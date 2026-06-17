// ─────────────────────────────────────────────────────────────────────────────
// SlideThumbnailWidget.cpp  (Sprint 5)
// ─────────────────────────────────────────────────────────────────────────────
#include "SlideThumbnailWidget.h"
#include "SlideScene.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QColor>
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QMenu>

namespace NativeOffice {

SlideThumbnailWidget::SlideThumbnailWidget(int slideIndex, SlideScene* scene,
                                           QWidget* parent)
    : QWidget(parent)
    , m_slideIndex(slideIndex)
    , m_scene(scene)
{
    setFixedSize(THUMB_W + 20, THUMB_H + 28);   // padding + slide number
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    refresh();
}

void SlideThumbnailWidget::refresh() {
    m_pixmap = QPixmap(THUMB_W, THUMB_H);
    m_pixmap.fill(Qt::white);

    if (m_scene) {
        QPainter p(&m_pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        m_scene->render(&p, QRectF(0, 0, THUMB_W, THUMB_H));
    }
    update();
}

void SlideThumbnailWidget::setActive(bool active) {
    if (m_active == active) return;
    m_active = active;
    update();
}

// ── Painting ──────────────────────────────────────────────────────────────────
void SlideThumbnailWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int leftPad  = 10;
    const int topPad   = 4;
    const int numH     = 16;

    // ── Slide number label ────────────────────────────────────────────────
    p.setFont(QFont("Segoe UI", 9));
    p.setPen(m_active ? QColor("#E8372A") : QColor("#9CA3AF"));
    p.drawText(QRect(leftPad, topPad, THUMB_W, numH),
               Qt::AlignLeft | Qt::AlignVCenter,
               QString::number(m_slideIndex + 1));

    const QRect thumbRect(leftPad, topPad + numH, THUMB_W, THUMB_H);

    // ── Drop shadow ───────────────────────────────────────────────────────
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 30));
    p.drawRect(thumbRect.adjusted(2, 2, 2, 2));

    // ── Thumbnail image ───────────────────────────────────────────────────
    p.drawPixmap(thumbRect, m_pixmap);

    // ── Selection / hover border ──────────────────────────────────────────
    QPen borderPen;
    if (m_active) {
        borderPen = QPen(QColor("#E8372A"), 2.5);
    } else if (m_hovered) {
        borderPen = QPen(QColor("#9CA3AF"), 1.5);
    } else {
        borderPen = QPen(QColor("#D1D5DB"), 1.0);
    }
    p.setPen(borderPen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(thumbRect);
}

// ── Events ────────────────────────────────────────────────────────────────────
void SlideThumbnailWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        m_pressPos = e->pos();
        m_dragging = false;
    }
    QWidget::mousePressEvent(e);
}

void SlideThumbnailWidget::mouseMoveEvent(QMouseEvent* e) {
    if ((e->buttons() & Qt::LeftButton) && !m_dragging &&
        (e->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
        m_dragging = true;
        auto* mime = new QMimeData;
        mime->setText(QString::number(m_slideIndex));
        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->setPixmap(m_pixmap.scaled(80, 45, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        drag->exec(Qt::MoveAction);
        m_dragging = false;
        return;
    }
    QWidget::mouseMoveEvent(e);
}

void SlideThumbnailWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && !m_dragging) {
        emit clicked(m_slideIndex);
    }
    QWidget::mouseReleaseEvent(e);
}

void SlideThumbnailWidget::contextMenuEvent(QContextMenuEvent* e) {
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #FFFFFF; color: #1C1E26; border: 1px solid #D5D8DF; padding: 4px; }"
        "QMenu::item { padding: 6px 18px; border-radius: 4px; }"
        "QMenu::item:selected { background-color: #E8372A; color: #FFFFFF; }"
        "QMenu::separator { height: 1px; background: #E2E4E9; margin: 4px 8px; }");
    QAction* actAdd = menu.addAction("➕  Add New Slide");
    QAction* actDup = menu.addAction("⎘  Duplicate Slide");
    menu.addSeparator();
    QAction* actDel = menu.addAction("✕  Delete Slide");

    QAction* chosen = menu.exec(e->globalPos());
    if (chosen == actAdd)      emit addRequested();
    else if (chosen == actDup) emit duplicateRequested(m_slideIndex);
    else if (chosen == actDel) emit deleteRequested(m_slideIndex);
}

void SlideThumbnailWidget::enterEvent(QEnterEvent* e) {
    m_hovered = true;
    update();
    QWidget::enterEvent(e);
}

void SlideThumbnailWidget::leaveEvent(QEvent* e) {
    m_hovered = false;
    update();
    QWidget::leaveEvent(e);
}

} // namespace NativeOffice
