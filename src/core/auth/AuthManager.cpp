// ─────────────────────────────────────────────────────────────────────────────
// AuthManager.cpp — device-pairing sign-in + entitlement refresh (see header).
// ─────────────────────────────────────────────────────────────────────────────
#include "AuthManager.h"
#include "SecureStore.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace NativeOffice {

static const QString kTokenKey       = QStringLiteral("appToken");
// Profile/entitlement refresh cadence. Short enough that edits made on the
// website (name, photo, plan) show up in a running app within a few minutes.
static const int     kEntitlementMin = 5;

AuthManager& AuthManager::instance() {
    static AuthManager s;
    return s;
}

AuthManager::AuthManager(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_pollTimer(new QTimer(this))
    , m_entitlementTimer(new QTimer(this))
{
    m_pollTimer->setSingleShot(true);
    connect(m_pollTimer, &QTimer::timeout, this, &AuthManager::pollOnce);

    m_entitlementTimer->setInterval(kEntitlementMin * 60 * 1000);
    connect(m_entitlementTimer, &QTimer::timeout,
            this, &AuthManager::refreshEntitlement);
}

QString AuthManager::baseUrl() const {
    return QSettings().value("auth/baseUrl",
                             QStringLiteral("https://nativeoffice.online"))
        .toString();
}

bool AuthManager::hasToken() const {
    return !SecureStore::load(kTokenKey).isEmpty();
}

QString AuthManager::userEmail() const {
    return QSettings().value("account/email").toString();
}

QString AuthManager::userName() const {
    return QSettings().value("account/name").toString();
}

QString AuthManager::firstName() const {
    return QSettings().value("account/firstName").toString();
}

QString AuthManager::occupation() const {
    return QSettings().value("account/occupation").toString();
}

QDateTime AuthManager::joinedAt() const {
    const qint64 t = QSettings().value("account/createdAt", 0).toLongLong();
    return t > 0 ? QDateTime::fromSecsSinceEpoch(t) : QDateTime();
}

QString AuthManager::displayName() const {
    const QString first = firstName();
    if (!first.isEmpty()) return first;
    const QString name = userName().trimmed();
    if (!name.isEmpty()) return name.section(QLatin1Char(' '), 0, 0);
    return userEmail().section(QLatin1Char('@'), 0, 0);
}

static QString avatarFilePath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
           + QStringLiteral("/avatar.img");
}

QString AuthManager::avatarPath() const {
    const QString p = avatarFilePath();
    return QFile::exists(p) ? p : QString();
}

bool AuthManager::premiumActive() const {
    // Locally enforce the expiry the server told us, so a lapsed subscription
    // doesn't stay unlocked between refreshes.
    QSettings st;
    if (!st.value("license/premiumActive", false).toBool()) return false;
    const qint64 until = st.value("license/premiumUntil", 0).toLongLong();
    return until == 0 /* lifetime */
        || until > QDateTime::currentSecsSinceEpoch();
}

QString AuthManager::premiumPlan() const {
    return QSettings().value("license/premiumPlan").toString();
}

QDateTime AuthManager::premiumUntil() const {
    const qint64 t = QSettings().value("license/premiumUntil", 0).toLongLong();
    return t > 0 ? QDateTime::fromSecsSinceEpoch(t) : QDateTime();
}

QString AuthManager::premiumPlanLabel() const {
    if (!premiumActive()) return QStringLiteral("Free");
    const QString plan = premiumPlan().toLower();
    QString pretty;
    if      (plan == QLatin1String("lifetime")) pretty = QStringLiteral("Lifetime");
    else if (plan == QLatin1String("monthly"))  pretty = QStringLiteral("Monthly");
    else if (plan == QLatin1String("yearly"))   pretty = QStringLiteral("1-year");
    else if (!plan.isEmpty()) {
        // "6-month", "1-year", "2-year", "<n>-day" → Title Case with spaces.
        pretty = plan;
        pretty.replace(QLatin1Char('-'), QLatin1Char(' '));
        if (!pretty.isEmpty()) pretty[0] = pretty[0].toUpper();
    } else {
        pretty = QStringLiteral("Premium");
    }
    return QStringLiteral("Premium · ") + pretty;
}

// ── Requests ──────────────────────────────────────────────────────────────────

QNetworkReply* AuthManager::getWithToken(const QString& path) {
    const QByteArray token = SecureStore::load(kTokenKey);
    QNetworkRequest req(QUrl(baseUrl() + path));
    req.setRawHeader("Authorization", "Bearer " + token);
    req.setTransferTimeout(10000);
    return m_nam->get(req);
}

void AuthManager::applyMe(const QByteArray& body) {
    const QJsonObject root    = QJsonDocument::fromJson(body).object();
    const QJsonObject user    = root.value("user").toObject();
    const QJsonObject premium = root.value("premium").toObject();

    QSettings st;
    const QString prevIdentity = st.value("account/name").toString()
        + st.value("account/firstName").toString()
        + st.value("account/occupation").toString();

    // Only overwrite a field the response actually carries. A reply that
    // parses but omits the user block used to blank the cached identity, and
    // an empty email moves the AI consent record onto its anonymous slot, so
    // an account that had already accepted the notice was asked again. It also
    // emptied the name the greeting and the profile page read.
    auto keep = [&st, &user](const char* jsonKey, const char* settingsKey) {
        const QJsonValue v = user.value(QLatin1String(jsonKey));
        if (v.isUndefined() || v.isNull()) return;
        const QString text = v.toString();
        if (text.isEmpty() && !st.value(settingsKey).toString().isEmpty()) return;
        st.setValue(settingsKey, text);
    };
    keep("email",      "account/email");
    keep("name",       "account/name");
    keep("first_name", "account/firstName");
    keep("last_name",  "account/lastName");
    // Occupation is legitimately clearable, so it is written as it arrives.
    if (user.contains(QLatin1String("occupation")))
        st.setValue("account/occupation", user.value("occupation").toString());
    st.setValue("account/createdAt",
                static_cast<qint64>(user.value("created_at").toDouble(0)));

    const bool wasActive = premiumActive();
    st.setValue("license/premiumActive", premium.value("active").toBool());
    st.setValue("license/premiumPlan",   premium.value("plan").toString());
    st.setValue("license/premiumUntil",
                static_cast<qint64>(premium.value("expires_at").toDouble(0)));

    if (wasActive != premiumActive()) {
        // Crossing into premium clears the export watermark preference, so an
        // upgrade takes effect immediately instead of leaving the new premium
        // user to find the toggle and turn it off themselves. Only the
        // false->true edge does this, so switching it back on afterwards is
        // respected; refreshEntitlement() runs every 30 minutes and does not
        // re-fire unless the value actually changed.
        if (premiumActive()) st.remove("premium/watermarkOnExport");
        emit entitlementChanged(premiumActive());
    }

    const QString newIdentity = st.value("account/name").toString()
        + st.value("account/firstName").toString()
        + st.value("account/occupation").toString();
    if (newIdentity != prevIdentity)
        emit profileChanged();

    // Keep the profile photo cached locally (photo URL changed, or no cache yet).
    const QString picture = user.value("picture").toString();
    if (!picture.isEmpty()
        && (picture != st.value("account/pictureUrl").toString()
            || avatarPath().isEmpty())) {
        fetchAvatar(picture);
    } else if (picture.isEmpty() && !avatarPath().isEmpty()) {
        QFile::remove(avatarFilePath());
        st.remove("account/pictureUrl");
        emit profileChanged();
    }
}

void AuthManager::fetchAvatar(const QString& pictureUrl) {
    QNetworkRequest req{ QUrl(pictureUrl) };
    req.setTransferTimeout(10000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, pictureUrl]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;   // retry next refresh
        const QByteArray img = reply->readAll();
        if (img.isEmpty()) return;
        QDir().mkpath(QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation));
        QFile f(avatarFilePath());
        if (f.open(QIODevice::WriteOnly)) {
            f.write(img);
            f.close();
            QSettings().setValue("account/pictureUrl", pictureUrl);
            emit profileChanged();
        }
    });
}

// ── Startup validation ────────────────────────────────────────────────────────

void AuthManager::validateStoredToken() {
    if (!hasToken()) {
        emit sessionValidated(false);
        return;
    }
    QNetworkReply* reply = getWithToken(QStringLiteral("/api/me"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int http =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && http == 200) {
            applyMe(reply->readAll());
            m_entitlementTimer->start();
            emit sessionValidated(true);
        } else if (http == 401 || http == 403) {
            // Token revoked/expired server-side: force a fresh sign-in.
            SecureStore::remove(kTokenKey);
            emit sessionValidated(false);
        } else {
            // Network trouble — local-first app, let the user in on the
            // cached session rather than locking their documents away.
            emit sessionValidated(true);
        }
    });
}

// ── Device pairing flow ───────────────────────────────────────────────────────

void AuthManager::startDeviceFlow() {
    cancelDeviceFlow();

    QNetworkRequest req(QUrl(baseUrl() + QStringLiteral("/api/device/start")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(10000);
    QNetworkReply* reply = m_nam->post(req, QByteArrayLiteral("{}"));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit deviceFlowFailed(
                tr("Could not reach nativeoffice.online.\nCheck your internet "
                   "connection and try again."));
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        m_code           = o.value("code").toString();
        m_deviceToken    = o.value("device_token").toString();
        m_verifyUrl      = o.value("verification_url").toString();
        m_pollIntervalMs = qMax(2, o.value("interval").toInt(3)) * 1000;
        m_flowDeadline   = QDateTime::currentDateTime()
                               .addSecs(o.value("expires_in").toInt(600));
        if (m_code.isEmpty() || m_deviceToken.isEmpty()) {
            emit deviceFlowFailed(tr("The sign-in service returned an "
                                     "unexpected response. Try again shortly."));
            return;
        }

        QDesktopServices::openUrl(QUrl(m_verifyUrl));
        emit deviceFlowStarted(m_code, m_verifyUrl);
        m_pollTimer->start(m_pollIntervalMs);
    });
}

void AuthManager::cancelDeviceFlow() {
    m_pollTimer->stop();
    m_pollInFlight = false;
    m_code.clear();
    m_deviceToken.clear();
    m_verifyUrl.clear();
}

void AuthManager::pollNow() {
    if (!m_deviceToken.isEmpty() && !m_pollInFlight) pollOnce();
}

void AuthManager::pollOnce() {
    if (m_deviceToken.isEmpty() || m_pollInFlight) return;
    if (QDateTime::currentDateTime() > m_flowDeadline) {
        cancelDeviceFlow();
        emit deviceFlowFailed(tr("The sign-in code expired. Click "
                                 "\"Sign in\" to get a fresh one."));
        return;
    }

    m_pollInFlight = true;
    QNetworkRequest req(QUrl(baseUrl() + QStringLiteral("/api/device/poll")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(10000);
    const QByteArray body = QJsonDocument(
        QJsonObject{{ "device_token", m_deviceToken }}).toJson(QJsonDocument::Compact);
    QNetworkReply* reply = m_nam->post(req, body);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply]() { handlePollReply(reply); });
}

void AuthManager::handlePollReply(QNetworkReply* reply) {
    reply->deleteLater();
    m_pollInFlight = false;
    if (m_deviceToken.isEmpty()) return;   // flow was cancelled mid-request

    const int http =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (http == 429) {                     // rate limited — back off politely
        m_pollIntervalMs = qMin(m_pollIntervalMs * 2, 15000);
        m_pollTimer->start(m_pollIntervalMs);
        return;
    }
    if (reply->error() != QNetworkReply::NoError && http == 0) {
        // Transient network hiccup: keep trying until the code expires.
        m_pollTimer->start(m_pollIntervalMs);
        return;
    }

    const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
    const QString status = o.value("status").toString();

    if (status == QLatin1String("pending")) {
        m_pollTimer->start(m_pollIntervalMs);
        return;
    }
    if (status == QLatin1String("approved")) {
        const QString token = o.value("token").toString();
        cancelDeviceFlow();
        if (token.isEmpty() || !SecureStore::save(kTokenKey, token.toUtf8())) {
            emit deviceFlowFailed(tr("Sign-in succeeded but the session could "
                                     "not be stored securely on this device."));
            return;
        }
        // The poll response carries user + premium in the same shape as
        // /api/me; refresh immediately anyway in case this server's poll
        // payload is older/slimmer than the /api/me contract.
        applyMe(QJsonDocument(o).toJson());
        m_entitlementTimer->start();
        refreshEntitlement();
        emit authenticated();
        return;
    }

    // denied / expired / anything else
    cancelDeviceFlow();
    emit deviceFlowFailed(status == QLatin1String("denied")
        ? tr("The request was denied in the browser.")
        : tr("The sign-in code expired. Click \"Sign in\" to get a fresh one."));
}

// ── Entitlement refresh ───────────────────────────────────────────────────────

void AuthManager::refreshEntitlement() {
    if (!hasToken()) return;
    QNetworkReply* reply = getWithToken(QStringLiteral("/api/me"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int http =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && http == 200) {
            applyMe(reply->readAll());
        } else if (http == 401 || http == 403) {
            signOut();
        }
        // Network errors: keep the cached entitlement (offline grace).
    });
}

void AuthManager::signOut() {
    cancelDeviceFlow();
    m_entitlementTimer->stop();
    SecureStore::remove(kTokenKey);
    QSettings st;
    st.remove("account");          // email/name/firstName/occupation/pictureUrl/…
    st.remove("license/premiumActive");
    st.remove("license/premiumPlan");
    st.remove("license/premiumUntil");
    QFile::remove(avatarFilePath());
    emit signedOut();
}

// ── Browser entry points ──────────────────────────────────────────────────────

void AuthManager::openPremiumPage() {
    QDesktopServices::openUrl(QUrl(baseUrl() + QStringLiteral("/premium.html")));
}

void AuthManager::openActivateKeyPage() {
    QDesktopServices::openUrl(QUrl(baseUrl() + QStringLiteral("/activate.html")));
}

void AuthManager::openAccountPage() {
    QDesktopServices::openUrl(QUrl(baseUrl() + QStringLiteral("/account.html")));
}

} // namespace NativeOffice
