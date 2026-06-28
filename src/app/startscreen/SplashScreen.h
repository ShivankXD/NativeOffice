// ─────────────────────────────────────────────────────────────────────────────
// SplashScreen.h — WPS-style startup splash
//
// A frameless, translucent splash window shown the moment the app launches.
// It displays the NativeOffice brand logo over a rounded white card with an
// animated "Initializing....." status line and a thin indeterminate progress
// bar, then emits finished() so the home screen can take over.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <QWidget>

class QLabel;
class QTimer;

namespace NativeOffice {

class SplashScreen : public QWidget {
    Q_OBJECT
public:
    explicit SplashScreen(QWidget* parent = nullptr);

    // Centre on the primary screen, show the splash and start the animation.
    // finished() is emitted after durationMs and the splash closes itself.
    void start(int durationMs = 2200);

signals:
    void finished();

private:
    QLabel* m_status   { nullptr };
    QTimer* m_dotTimer { nullptr };
    int     m_dotCount { 5 };
};

} // namespace NativeOffice
