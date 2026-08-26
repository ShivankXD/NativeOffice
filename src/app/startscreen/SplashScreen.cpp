// ─────────────────────────────────────────────────────────────────────────────
// SplashScreen.cpp — implementation
// ─────────────────────────────────────────────────────────────────────────────
#include "SplashScreen.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QScreen>
#include <QTimer>

namespace NativeOffice {

namespace {
// The card is sized against the SCREEN, not in fixed pixels, so it holds the
// same share of the display on a 1366 laptop panel and on a 4K monitor. Word's
// startup card is a shade over a quarter of the screen's width; this is that,
// rounded up a little, and clamped so it stays sensible at both extremes.
constexpr double kScreenShare = 0.29;
constexpr int    kMinWidth    = 420;
constexpr int    kMaxWidth    = 760;
constexpr int    kShadowPad   = 26;   // room outside the card for the shadow
constexpr int    kRadius      = 18;
} // namespace

SplashScreen::SplashScreen(QWidget* parent)
    : QWidget(parent)
{
    // Frameless, translucent, always-on-top splash that deletes itself on close.
    setWindowFlags(Qt::SplashScreen | Qt::FramelessWindowHint
                   | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);

    m_art = QPixmap(QStringLiteral(":/assets/splash-card.jpg"));

    int cardW = kMinWidth;
    if (const QScreen* screen = QApplication::primaryScreen())
        cardW = int(screen->geometry().width() * kScreenShare);
    cardW = qBound(kMinWidth, cardW, kMaxWidth);

    // The card takes the artwork's own proportions. Forcing a shape on it would
    // either letterbox the picture or crop the wordmark out of it.
    const double aspect = (!m_art.isNull() && m_art.width() > 0)
                              ? double(m_art.height()) / m_art.width()
                              : 0.75;
    const int cardH = int(cardW * aspect);
    setFixedSize(cardW + kShadowPad * 2, cardH + kShadowPad * 2);
}

void SplashScreen::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const QRectF card = QRectF(rect()).adjusted(kShadowPad, kShadowPad,
                                                -kShadowPad, -kShadowPad);

    // A soft shadow, painted as a few nested rounded rectangles rather than
    // through QGraphicsDropShadowEffect: the effect renders the whole widget to
    // an offscreen buffer first, and on a translucent frameless window that
    // costs a visible flicker on the very first frame, which is the one frame
    // this window exists to look right in.
    for (int i = kShadowPad; i > 0; --i) {
        const double t = double(i) / kShadowPad;
        QColor c(10, 12, 22);
        c.setAlphaF(0.055 * (1.0 - t));
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(card.adjusted(-i, -i + 3, i, i + 3),
                          kRadius + i, kRadius + i);
    }

    QPainterPath clip;
    clip.addRoundedRect(card, kRadius, kRadius);
    p.setClipPath(clip);

    if (!m_art.isNull()) {
        p.drawPixmap(card.toRect(),
                     m_art.scaled(card.size().toSize(), Qt::KeepAspectRatioByExpanding,
                                  Qt::SmoothTransformation));
    } else {
        // The artwork is the whole card, so a missing resource has to leave
        // something behind rather than a transparent hole.
        p.fillRect(card, QColor(8, 10, 18));
        p.setPen(QColor(0xF2, 0xF5, 0xFA));
        QFont f(QStringLiteral("Segoe UI"));
        f.setPixelSize(qMax(16, int(card.width() * 0.06)));
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.drawText(card, Qt::AlignCenter, QStringLiteral("NativeOffice"));
    }
}

void SplashScreen::begin() {
    if (const QScreen* screen = QApplication::primaryScreen())
        move(screen->geometry().center() - rect().center());
    m_shown.start();
    show();
    raise();

    // Dev-only: capture the card itself. The startup window is on screen for
    // two seconds and the dev build is masked from screen capture, so this is
    // the only way to look at it.
    if (qEnvironmentVariableIsSet("NATIVEOFFICE_SPLASH_GRAB")) {
        const QString out = qEnvironmentVariable("NATIVEOFFICE_SPLASH_GRAB");
        QTimer::singleShot(600, this, [this, out] {
            if (grab().save(out))
                qWarning("[splash] grabbed %s  card %dx%d screen %dx%d",
                         qUtf8Printable(out), width(), height(),
                         QApplication::primaryScreen()->geometry().width(),
                         QApplication::primaryScreen()->geometry().height());
        });
    }
}

int SplashScreen::remainingMs(int totalMs) const {
    if (!m_shown.isValid()) return 0;
    return qBound(0, totalMs - int(m_shown.elapsed()), totalMs);
}

void SplashScreen::finish() {
    emit finished();
    close();                   // WA_DeleteOnClose handles cleanup
}

} // namespace NativeOffice
