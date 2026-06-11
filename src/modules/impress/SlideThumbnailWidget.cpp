// ─────────────────────────────────────────────────────────────────────────────
// SlideThumbnailWidget.cpp  (Sprint 5)
// ─────────────────────────────────────────────────────────────────────────────
#include "SlideThumbnailWidget.h"
#include "SlideScene.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QColor>

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
        emit clicked(m_slideIndex);
    }
    QWidget::mousePressEvent(e);
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
