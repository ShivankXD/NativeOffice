#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AiConsentDialog.h — the notice shown the first time someone reaches for the
// Stasis sidebar, and again on every attempt until they accept.
//
// It exists because the sidebar breaks the promise the rest of the app keeps:
// NativeOffice works fully offline once signed in, and Stasis does not. Saying
// so once, in plain words, before anything is sent, is the honest version of
// that trade. The notice therefore states what leaves the machine (the prompt,
// and only when the assistant is used), what does not (everything else), and
// what declining costs (the assistant, and nothing besides).
// ─────────────────────────────────────────────────────────────────────────────

#include <QDialog>

class QPushButton;

namespace NativeOffice {

class AiConsentDialog : public QDialog {
    Q_OBJECT
public:
    explicit AiConsentDialog(QWidget* parent = nullptr);

    // Runs the notice modally and records the answer. Returns true when the
    // user accepted, which is the only outcome that opens the sidebar.
    static bool runFor(QWidget* parent);

protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    QWidget* buildBody();
    QPushButton* m_accept  { nullptr };
    QPushButton* m_decline { nullptr };
};

} // namespace NativeOffice
