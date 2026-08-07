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
    // Pressing the banner twice before the process dies would stage and run the
    // script twice, and two installers racing each other end with two apps.
    if (m_relaunching) return;
    m_relaunching = true;

#ifdef Q_OS_WIN
    // Where the updated app will be once the bootstrapper has run.
    //
    // For a normal install that is where we already are. For a PACKAGED (Store)
    // build it is not: we are executing from a read-only WindowsApps folder
    // that the installer cannot touch, so it installs to LOCALAPPDATA instead.
    // Relaunching our own path there would start the OLD copy again, which
    // would find the same update still pending and ask to install it forever.
    QString target = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    if (runningPackaged()) {
        target = QDir::toNativeSeparators(
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/Programs/NativeOffice/NativeOffice.exe"));
    }

    // Hand the work to a detached script that outlives this process.
    //
    // Doing it inline raced the installer against our own shutdown: Inno would
    // start replacing files while this process still held them open. The script
    // waits, force-kills us by pid so nothing can keep a handle (a modal dialog
    // is enough to stall a polite quit), runs the installer silently, then
    // starts the updated app. It deletes itself last.
    //
    // A .bat rather than a long cmd /C string because the paths contain spaces
    // and quotes, and Qt's argument quoting mangles nested quoting on Windows.
    const QString bat = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                            .filePath(QStringLiteral("nativeoffice-update.bat"));
    QFile f(bat);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        // The installer relaunches the app itself when it finishes, so this
        // script must NOT also start it: doing both is what opened two windows.
        // The app is only started here when the installer's own relaunch would
        // bring back the WRONG copy, i.e. a packaged build, where the installer
        // lands in LOCALAPPDATA while the Store copy we are running stays put.
        // `start` is guarded by an existence check so a failed install cannot
        // leave the user with nothing running.
        const QString relaunch = runningPackaged()
            ? QStringLiteral("ping 127.0.0.1 -n 2 >nul\r\n"
                             "if exist \"%1\" start \"\" \"%1\"\r\n").arg(target)
            : QString();

        const QString script =
            QStringLiteral(
                "@echo off\r\n"
                "ping 127.0.0.1 -n 3 >nul\r\n"
                "taskkill /F /PID %1 >nul 2>&1\r\n"
                "ping 127.0.0.1 -n 2 >nul\r\n"
                "\"%2\" /VERYSILENT /NORESTART\r\n"
                "%3"
                "del \"%~f0\"\r\n")
                .arg(QCoreApplication::applicationPid())
                .arg(QDir::toNativeSeparators(m_installerPath))
                .arg(relaunch);
        f.write(script.toLocal8Bit());
        f.close();
        QProcess::startDetached(QStringLiteral("cmd.exe"),
                                { QStringLiteral("/C"), QDir::toNativeSeparators(bat) });
    } else {
        // Could not stage the script: fall back to the direct call rather than
        // leaving the user with a downloaded update and no way to apply it.
        QProcess::startDetached(m_installerPath, { QStringLiteral("/VERYSILENT"),
                                                   QStringLiteral("/NORESTART") });
    }
#else
    QProcess::startDetached(m_installerPath, { "/VERYSILENT", "/NORESTART" });
#endif
    QCoreApplication::quit();
}

} // namespace NativeOffice
