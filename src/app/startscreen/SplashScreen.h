// ─────────────────────────────────────────────────────────────────────────────
// SplashScreen.h — the startup card
//
// A frameless, translucent, always-on-top window showing the brand card while
// the app starts. It is the artwork and nothing else: no status line, no
// progress bar. Those were reporting on a sequence the user cannot act on, and
// a card that says "Initializing" for as long as it takes reads as a wait,
// while a card that is simply there for a fixed moment reads as a brand.
//
// The window holds for a fixed span rather than for however long startup takes.
// remainingMs() is how the caller honours that: it asks how much of the span is
// left once its own work has finished, so a fast machine still sees the card
// for the full time and a slow one is never made to wait beyond it.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <QElapsedTimer>
#include <QPixmap>
#include <QWidget>

namespace NativeOffice {

class SplashScreen : public QWidget {
    Q_OBJECT
public:
    explicit SplashScreen(QWidget* parent = nullptr);

    // Centre on the primary screen and show the card. Starts the clock that
    // remainingMs() reads.
    void begin();

    // Milliseconds still to run of a `totalMs` span, 0 once it has passed.
    [[nodiscard]] int remainingMs(int totalMs) const;

    // Emit finished() and close (WA_DeleteOnClose cleans up).
    void finish();

signals:
    void finished();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QPixmap       m_art;
    QElapsedTimer m_shown;
};

} // namespace NativeOffice
