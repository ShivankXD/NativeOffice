#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AuthManager.h — account sign-in + licensing for the desktop app.
//
// Implements the device-pairing flow against nativeoffice.online:
//   startDeviceFlow()  → POST /api/device/start, open the browser at the
//                        verification URL, then poll /api/device/poll until
//                        the user approves in the browser. On approval the
//                        one-time app token is stored via SecureStore (DPAPI).
//   validateStoredToken() → GET /api/me with the stored Bearer token; decides
//                        whether the login gate can be skipped. If the network
//                        is unreachable but a token exists, the user is let
//                        through (offline grace) — documents are local-first.
//   refreshEntitlement() → re-reads premium status; runs on launch and every
//                        30 minutes so purchases/keys unlock automatically.
//
// Secrets: only the app token is secret and it lives in SecureStore. Cached
// identity/entitlement (email, premium flag/expiry) is non-secret QSettings
// state so modules can check `license/premiumActive` synchronously.
// ─────────────────────────────────────────────────────────────────────────────

#include <QDateTime>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace NativeOffice {

class AuthManager : public QObject {
    Q_OBJECT
public:
    static AuthManager& instance();

    // Backend origin; override for local testing via QSettings "auth/baseUrl".
    QString baseUrl() const;

    bool    hasToken() const;
    QString userEmail() const;
    QString userName() const;
    QString firstName() const;        // from onboarding; may be empty
    QString occupation() const;
    QDateTime joinedAt() const;       // account creation date (invalid if unknown)
    // Preferred short display name: onboarding first name, else the Google
    // name's first word, else the email's local part.
    QString displayName() const;
    // Local path of the cached profile photo; empty until downloaded.
    QString avatarPath() const;
    bool    premiumActive() const;
    QString premiumPlan() const;
    // Expiry for time-limited plans; invalid for Free or Lifetime.
    QDateTime premiumUntil() const;
    // "Premium · 1-year", "Premium · Lifetime", "Free" — for profile display.
    QString premiumPlanLabel() const;

    // Current pairing state (valid between deviceFlowStarted and completion).
    QString pairingCode() const     { return m_code; }
    QString verificationUrl() const { return m_verifyUrl; }

public slots:
    // Async; answers with sessionValidated(ok). ok=true either means the token
    // checked out online, or we're offline with a stored token (grace).
    void validateStoredToken();

    void startDeviceFlow();
    void cancelDeviceFlow();
    void pollNow();              // nudge from the nativeoffice:// fast path

    void refreshEntitlement();
    void signOut();

    // Browser entry points (premium purchase / key activation are website-only).
    void openPremiumPage();
    void openActivateKeyPage();
    void openAccountPage();

signals:
    void sessionValidated(bool ok);
    void deviceFlowStarted(const QString& code, const QString& verificationUrl);
    void authenticated();
    void deviceFlowFailed(const QString& message);
    void entitlementChanged(bool premiumActive);
    // Identity data (name/photo/occupation) was refreshed from the backend.
    void profileChanged();
    void signedOut();

private:
    explicit AuthManager(QObject* parent = nullptr);

    void pollOnce();
    void handlePollReply(QNetworkReply* reply);
    void applyMe(const QByteArray& body);    // cache user + premium from /api/me JSON
    void fetchAvatar(const QString& pictureUrl);
    QNetworkReply* getWithToken(const QString& path);

    QNetworkAccessManager* m_nam { nullptr };
    QTimer*                m_pollTimer { nullptr };
    QTimer*                m_entitlementTimer { nullptr };

    QString   m_code;            // display code "XXXX-XXXX"
    QString   m_verifyUrl;
    QString   m_deviceToken;     // secret; memory only, never persisted
    QDateTime m_flowDeadline;
    int       m_pollIntervalMs { 3000 };
    bool      m_pollInFlight { false };
};

} // namespace NativeOffice
