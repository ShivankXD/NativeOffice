// ─────────────────────────────────────────────────────────────────────────────
// SplashScreen.cpp — implementation
// ─────────────────────────────────────────────────────────────────────────────
#include "SplashScreen.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QPixmap>
#include <QTimer>
#include <QProgressBar>
#include <QGraphicsDropShadowEffect>
#include <QApplication>
#include <QScreen>

namespace NativeOffice {

SplashScreen::SplashScreen(QWidget* parent)
    : QWidget(parent)
{
    // Frameless, translucent, always-on-top splash that deletes itself on close.
    setWindowFlags(Qt::SplashScreen | Qt::FramelessWindowHint
                   | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setFixedSize(560, 440);

    // Outer margin gives the drop shadow room to render.
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(28, 24, 28, 28);

    // ── Rounded white card ────────────────────────────────────────────────
    auto* card = new QFrame(this);
    card->setObjectName("splashCard");
    card->setStyleSheet("#splashCard { background:#FFFFFF; border-radius:20px; }");
    outer->addWidget(card);

    auto* shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(54);
    shadow->setColor(QColor(20, 24, 40, 75));
    shadow->setOffset(0, 12);
    card->setGraphicsEffect(shadow);

    auto* cl = new QVBoxLayout(card);
    cl->setContentsMargins(44, 40, 44, 36);
    cl->setSpacing(0);
    cl->addStretch();

    // ── Brand logo ────────────────────────────────────────────────────────
    auto* logo = new QLabel(card);
    logo->setAlignment(Qt::AlignCenter);
    QPixmap pm(":/assets/nativeoffice-logo.png");
    if (!pm.isNull()) {
        // Fit inside a bounded box (keeping aspect ratio) so the tall wordmark
        // logo never overflows the card and pushes the status text off-screen.
        logo->setPixmap(pm.scaled(240, 168, Qt::KeepAspectRatio,
                                  Qt::SmoothTransformation));
    } else {
        logo->setText("NativeOffice");
        logo->setStyleSheet("color:#1A1F2E; font-size:30px; font-weight:700;");
    }
    cl->addWidget(logo);

    cl->addSpacing(28);

    // ── "Initializing....." status line ───────────────────────────────────
    m_status = new QLabel(QStringLiteral("Initializing....."), card);
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setStyleSheet(
        "color:#5B6473; font-family:'Segoe UI','Inter',sans-serif;"
        "font-size:15px; letter-spacing:1px;");
    cl->addWidget(m_status);

    cl->addSpacing(24);

    // ── Thin indeterminate progress bar ───────────────────────────────────
    auto* bar = new QProgressBar(card);
    bar->setRange(0, 0);            // busy / indeterminate
    bar->setTextVisible(false);
    bar->setFixedHeight(4);
    bar->setStyleSheet(
        "QProgressBar { background:#EEF1F6; border:none; border-radius:2px; }"
        "QProgressBar::chunk { border-radius:2px;"
        "  background:qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 #3B82F6, stop:1 #8B5CF6); }");
    cl->addWidget(bar);

    cl->addStretch();

    // Animate the trailing dots of "Initializing....." (1..5 dots).
    m_dotTimer = new QTimer(this);
    connect(m_dotTimer, &QTimer::timeout, this, [this]() {
        m_dotCount = (m_dotCount % 5) + 1;
        m_status->setText("Initializing" + QString(m_dotCount, '.'));
    });
}

void SplashScreen::start(int durationMs)
{
    if (const QScreen* screen = QApplication::primaryScreen())
        move(screen->geometry().center() - rect().center());

    m_status->setText(QStringLiteral("Initializing....."));
    m_dotTimer->start(280);

    show();
    raise();

    QTimer::singleShot(durationMs, this, [this]() {
        m_dotTimer->stop();
        emit finished();
        close();              // WA_DeleteOnClose handles cleanup
    });
}

} // namespace NativeOffice
