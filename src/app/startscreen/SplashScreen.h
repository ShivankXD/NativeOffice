// ─────────────────────────────────────────────────────────────────────────────
// SplashScreen.h — WPS-style startup splash (single unified card)
//
// A frameless, translucent splash window shown the moment the app launches.
// It displays the NativeOffice brand logo over a rounded white card with an
// animated status line (dots) and a thin indeterminate progress bar. One card
// carries the whole startup: it shows "Restoring your session" while the gate
// validates the stored session, then setStatus("Initializing") until the shell
// is ready — only the text changes; the card and progress bar stay put.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QTimer;

namespace NativeOffice {

class SplashScreen : public QWidget {
    Q_OBJECT
public:
    explicit SplashScreen(QWidget* parent = nullptr);

    // Centre on the primary screen, set the initial status and show the card.
    void beginWith(const QString& baseText);
    // Swap the status line's base text (the animated dots continue).
    void setStatus(const QString& baseText);
    // Stop the animation, emit finished() and close (WA_DeleteOnClose cleans up).
    void finish();

signals:
    void finished();

private:
    void refreshStatus();

    QLabel*  m_status   { nullptr };
    QTimer*  m_dotTimer { nullptr };
    QString  m_base     { QStringLiteral("Initializing") };
    int      m_dotCount { 3 };
};

} // namespace NativeOffice
