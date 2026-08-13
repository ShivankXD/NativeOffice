#include "AiToast.h"

#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QTimer>

namespace NativeOffice {

AiToast::AiToast(QWidget* parent)
    : QWidget(parent)
{
    // Without this a stylesheet background on a plain QWidget subclass silently
    // does not paint; here the background is painted by hand anyway, but the
    // attribute keeps child styling predictable.
    setAttribute(Qt::WA_StyledBackground, true);
    hide();

    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(12, 9, 8, 9);
    h->setSpacing(10);

    m_text = new QLabel(this);
    m_text->setWordWrap(true);
    m_text->setStyleSheet(QStringLiteral(
        "color:#F0D9A8; font:12px 'Segoe UI'; background:transparent;"));

    auto* close = new QPushButton(QStringLiteral("×"), this);
    close->setCursor(Qt::PointingHandCursor);
    close->setFixedSize(20, 20);
    close->setFocusPolicy(Qt::NoFocus);
    close->setToolTip(QStringLiteral("Dismiss"));
    close->setStyleSheet(QStringLiteral(
        "QPushButton { color:#9AA4B8; font:15px 'Segoe UI'; background:transparent;"
        "  border:none; border-radius:10px; }"
        "QPushButton:hover { color:#FFFFFF; background:rgba(255,255,255,0.10); }"));
    connect(close, &QPushButton::clicked, this, &AiToast::dismiss);

    h->addWidget(m_text, 1);
    h->addWidget(close, 0, Qt::AlignTop);

    m_fade = new QGraphicsOpacityEffect(this);
    m_fade->setOpacity(1.0);
    setGraphicsEffect(m_fade);

    m_anim = new QPropertyAnimation(m_fade, "opacity", this);
    m_anim->setDuration(280);
    connect(m_anim, &QPropertyAnimation::finished, this, [this] {
        if (m_fade->opacity() <= 0.01) hide();
    });

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &AiToast::dismiss);
}

void AiToast::post(const QString& text, Tone tone, int msVisible) {
    m_tone = tone;
    m_text->setText(text);
    m_anim->stop();
    m_fade->setOpacity(1.0);
    show();
    update();
    m_timer->start(msVisible);
}

void AiToast::dismiss() {
    m_timer->stop();
    if (!isVisible()) return;
    m_anim->stop();
    m_anim->setStartValue(m_fade->opacity());
    m_anim->setEndValue(0.0);
    m_anim->start();
}

void AiToast::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 9, 9);

    const bool warn = m_tone == Tone::Warning;
    p.fillPath(path, warn ? QColor(0xE8, 0xA3, 0x3D, 28) : QColor(0x7C, 0x5C, 0xFF, 28));
    p.setPen(QPen(warn ? QColor(0xE8, 0xA3, 0x3D, 90) : QColor(0x7C, 0x5C, 0xFF, 90), 1));
    p.drawPath(path);
}

} // namespace NativeOffice
