#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AiToast.h — the small strip the sidebar drops in when something needs saying
// but not answering: no connection, quota spent, an attachment refused.
//
// It clears itself after a few seconds and carries its own close button, so it
// never becomes one more thing to dismiss. Sitting inside the sidebar rather
// than floating over the document keeps it where the user is already looking.
// ─────────────────────────────────────────────────────────────────────────────

#include <QWidget>

class QLabel;
class QTimer;
class QPropertyAnimation;
class QGraphicsOpacityEffect;

namespace NativeOffice {

class AiToast : public QWidget {
    Q_OBJECT
public:
    enum class Tone { Info, Warning };

    explicit AiToast(QWidget* parent = nullptr);

    // Shows text and starts the countdown. Calling it again while one is up
    // replaces the message and restarts the clock.
    void post(const QString& text, Tone tone = Tone::Warning, int msVisible = 5000);

    void dismiss();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QLabel* m_text  { nullptr };
    QTimer* m_timer { nullptr };
    QGraphicsOpacityEffect* m_fade { nullptr };
    QPropertyAnimation*     m_anim { nullptr };
    Tone    m_tone { Tone::Warning };
};

} // namespace NativeOffice
