#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// LoginGate.h — the sign-in gate shown before the home screen.
//
// A centered window in NativeOffice's home-screen identity (dark navy, electric
// blue primary action, gold pairing code). Three states:
//   Checking  — a stored session is being validated ("Restoring your session…")
//   SignIn    — logo + headline + "Sign in to continue" (opens the browser)
//   Waiting   — the pairing code + animated status while the user finishes in
//               the browser, with copy-link / reopen / cancel fallbacks
//
// Emits proceed() once the user is authenticated (or a stored session checked
// out); closing the window before that quits the app — the suite is gated.
// ─────────────────────────────────────────────────────────────────────────────

#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;

namespace NativeOffice {

class LoginGate : public QWidget {
    Q_OBJECT
public:
    explicit LoginGate(QWidget* parent = nullptr);

    // Show the gate: validates any stored session first, otherwise sign-in.
    // When silentChecking is true, the gate window stays hidden while a stored
    // session is validated (the startup splash shows "Restoring your session"
    // instead); it only becomes visible if sign-in is actually needed.
    void begin(bool silentChecking = false);

signals:
    void proceed();
    // Emitted when the gate must show its sign-in UI, so a covering splash can
    // step aside.
    void signInRequired();

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    QWidget* buildCheckingPage();
    QWidget* buildSignInPage();
    QWidget* buildWaitingPage();
    void     showSignIn(const QString& error = QString());
    void     finishGate();

    QStackedWidget* m_stack        { nullptr };
    QLabel*         m_errorLabel   { nullptr };
    QLabel*         m_codeLabel    { nullptr };
    QLabel*         m_waitStatus   { nullptr };
    QPushButton*    m_signInBtn    { nullptr };
    QTimer*         m_dotTimer     { nullptr };
    int             m_dotCount     { 0 };
    bool            m_done         { false };
};

} // namespace NativeOffice
