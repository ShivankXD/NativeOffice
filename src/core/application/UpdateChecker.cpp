// ─────────────────────────────────────────────────────────────────────────────
// UpdateChecker.cpp — see header.
// ─────────────────────────────────────────────────────────────────────────────
#include "UpdateChecker.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

#include <vector>

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

// Where the update manifest is fetched from.
//
// Deliberately NOT AuthManager::baseUrl(). That key exists so the API can be
// pointed at a local backend during development, and updates used to ride on
// it: with "auth/baseUrl" set to a dev server, the manifest poll went to
// localhost, got no reply, and the app sat on State::Offline forever. The
// symptom was a machine that silently never updated while every other install
// updated fine, which is a miserable thing to diagnose.
//
// Updates now always check production. "update/baseUrl" is a separate, explicit
// override for anyone who genuinely wants to test the update path elsewhere, so
// changing the API target can no longer disable updates as a side effect.
static QString updateOrigin() {
    return QSettings()
        .value(QStringLiteral("update/baseUrl"),
               QStringLiteral("https://nativeoffice.online"))
        .toString();
}

// Compare dotted numeric versions component by component: major, then minor,
// then patch, returning at the first difference. 1.5.0 is correctly newer than
// 1.4.16 because the minor component decides it and the patch is never reached.
// Missing components count as 0, so "1.5" and "1.5.0" compare equal.
// >0 if a>b, <0 if a<b, 0 if equal.
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
    case State::StoreUpdateAvailable:
        return tr("Version %1 is available in the Microsoft Store.").arg(m_latest);
    default:                     return QString();
    }
}

void UpdateChecker::checkForUpdates() {
    if (m_checked) return;             // once per process only
    m_checked = true;
    // A packaged build still CHECKS; it just cannot install. It used to return
    // UpToDate here without asking anyone, which meant a Store user running an
    // old build was told they were current, with nothing anywhere in the UI
    // hinting otherwise. The check now runs and resolves to
    // StoreUpdateAvailable when the Store copy is behind.
    setState(State::Scanning);

    QNetworkRequest req(QUrl(updateOrigin() + QStringLiteral("/version.json")));
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

    // Behind, but a Store install must not install anything itself: the
    // package directory is read-only, and the Inno bootstrapper would plant a
    // second unmanaged copy beside the Store one. Report it and point at the
    // Store instead.
    if (runningPackaged()) {
        setState(State::StoreUpdateAvailable);
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

// Open this app's Store listing so the user can pull the update.
//
// The PFN is read from the running package rather than a hard-coded product
// id, so this cannot drift if the listing is ever recreated, and there is
// nothing to keep in sync between the manifest and this file. If the family
// name is unavailable for any reason, the Store's own Downloads and updates
// page is the honest fallback: it is where "get updates" lives.
void UpdateChecker::openStorePage() {
#ifdef Q_OS_WIN
    QString url = QStringLiteral("ms-windows-store://downloadsandupdates");
    UINT32 len = 0;
    if (GetCurrentPackageFamilyName(&len, nullptr) == ERROR_INSUFFICIENT_BUFFER && len > 0) {
        std::vector<wchar_t> buf(len);
        if (GetCurrentPackageFamilyName(&len, buf.data()) == ERROR_SUCCESS) {
            const QString pfn = QString::fromWCharArray(buf.data());
            if (!pfn.isEmpty())
                url = QStringLiteral("ms-windows-store://pdp/?PFN=") + pfn;
        }
    }
    QDesktopServices::openUrl(QUrl(url));
#endif
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
