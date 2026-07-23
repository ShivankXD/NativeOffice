// ─────────────────────────────────────────────────────────────────────────────
// UpdateChecker.cpp — see header.
// ─────────────────────────────────────────────────────────────────────────────
#include "UpdateChecker.h"
#include "core/auth/AuthManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#include <appmodel.h>
#endif

namespace NativeOffice {

// True when the process runs from an MSIX package (Microsoft Store install).
// Store builds must not self-update: the package directory is read-only, and
// running the downloaded Inno installer would plant a second, unmanaged copy
// of the app beside the Store one. The Store delivers updates itself, so the
// same binary serves both channels — packaged installs just report UpToDate.
static bool runningPackaged() {
#ifdef Q_OS_WIN
    UINT32 len = 0;
    return GetCurrentPackageFullName(&len, nullptr) != APPMODEL_ERROR_NO_PACKAGE;
#else
    return false;
#endif
}

// The manifest key for this platform's installer.
#if defined(Q_OS_WIN)
static constexpr const char* kPlatformKey = "windows";
#elif defined(Q_OS_MACOS)
static constexpr const char* kPlatformKey = "mac";
#else
static constexpr const char* kPlatformKey = "linux";
#endif

// Compare dotted numeric versions. >0 if a>b, <0 if a<b, 0 if equal.
static int cmpVersion(const QString& a, const QString& b) {
    const QStringList pa = a.split('.'), pb = b.split('.');
    const int n = qMax(pa.size(), pb.size());
    for (int i = 0; i < n; ++i) {
        const int x = i < pa.size() ? pa.at(i).toInt() : 0;
        const int y = i < pb.size() ? pb.at(i).toInt() : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

UpdateChecker& UpdateChecker::instance() {
    static UpdateChecker s;
    return s;
}

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this)) {}

void UpdateChecker::setState(State s) {
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

QString UpdateChecker::bannerMessage() const {
    switch (m_state) {
    case State::Scanning:        return tr("Scanning for updates…");
    case State::UpToDate:        return tr("You're on the latest version.");
    case State::Offline:         return tr("Update check unavailable — no internet connection.");
    case State::UpdateAvailable: return tr("A new version is available — preparing update…");
    case State::Downloading:     return tr("Downloading the latest version…");
    case State::ReadyToRestart:  return tr("Update ready. Restart NativeOffice to finish.");
    case State::Failed:          return tr("Couldn't check for updates right now.");
    default:                     return QString();
    }
}

void UpdateChecker::checkForUpdates() {
    if (m_checked) return;             // once per process only
    m_checked = true;
    if (runningPackaged()) {           // Store install: updates are the Store's job
        setState(State::UpToDate);
        return;
    }
    setState(State::Scanning);

    QNetworkRequest req(QUrl(AuthManager::instance().baseUrl()
                             + QStringLiteral("/version.json")));
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::AlwaysNetwork);
    req.setTransferTimeout(8000);      // keep the home-lock window short
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int http =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && http == 200) {
            onManifest(reply->readAll());
        } else if (http == 0) {
            // No response at all → treat as offline (non-disruptive).
            setState(State::Offline);
        } else {
            setState(State::Failed);
        }
    });
}

void UpdateChecker::onManifest(const QByteArray& body) {
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    m_latest      = o.value("version").toString();
    m_downloadUrl = o.value(QLatin1String(kPlatformKey)).toString();

    const QString current = QCoreApplication::applicationVersion();
    if (m_latest.isEmpty() || cmpVersion(m_latest, current) <= 0) {
        setState(State::UpToDate);     // equal or manifest older → nothing to do
        return;
    }
    setState(State::UpdateAvailable);
    if (m_downloadUrl.isEmpty()) {     // newer, but no installer for this OS
        setState(State::Failed);
        return;
    }
    startDownload();
}

void UpdateChecker::startDownload() {
    setState(State::Downloading);
    QNetworkRequest req{ QUrl(m_downloadUrl) };
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_nam->get(req);

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 got, qint64 total) {
        if (total > 0) emit downloadProgress(int(got * 100 / total));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { setState(State::Failed); return; }
        const QByteArray data = reply->readAll();
        if (data.isEmpty()) { setState(State::Failed); return; }

        // Save alongside temp with the installer's real filename so the OS
        // treats it correctly when launched.
        QString name = QUrl(m_downloadUrl).fileName();
        if (name.isEmpty()) name = QStringLiteral("NativeOffice-Setup.exe");
        const QString dir =
            QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        m_installerPath = QDir(dir).filePath(name);

        QFile f(m_installerPath);
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
            setState(State::Failed);
            return;
        }
        f.close();
        setState(State::ReadyToRestart);
    });
}

void UpdateChecker::relaunchForUpdate() {
    if (m_installerPath.isEmpty() || !QFile::exists(m_installerPath)) return;
    // Run the installer SILENTLY (/VERYSILENT): no wizard, no splash — it just
    // downloads + extracts the new build in place (into the remembered install
    // dir) and relaunches the app on finish. Then quit so this instance releases
    // its files for replacement. (Previously it launched the installer with no
    // args, which popped the full setup wizard on every "Restart to update".)
    QProcess::startDetached(m_installerPath, { "/VERYSILENT", "/NORESTART" });
    QCoreApplication::quit();
}

} // namespace NativeOffice
