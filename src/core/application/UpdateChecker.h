#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UpdateChecker.h — one-shot "am I the latest build?" check for the desktop app.
//
// Flow (see the header comment on checkForUpdates):
//   • On launch, once the user is signed in, checkForUpdates() does a single
//     fast GET of nativeoffice.online/version.json and compares the manifest's
//     `version` to QApplication::applicationVersion().
//   • Same version  → UpToDate.
//   • Newer version → UpdateAvailable, then the matching installer is downloaded
//     to a temp file (Downloading → ReadyToRestart). relaunchForUpdate() runs
//     the installer and quits, so the old instance releases its files.
//   • No network    → Offline (the check is abandoned for this session — it is
//     never retried on navigation, so it can't interrupt the user mid-work).
//
// The check runs at most once per process (m_checked), so returning to the home
// screen or opening tabs never re-triggers it. The home screen locks its launch
// actions only while state()==Scanning (a sub-second window at startup).
// ─────────────────────────────────────────────────────────────────────────────

#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace NativeOffice {

class UpdateChecker : public QObject {
    Q_OBJECT
public:
    enum class State {
        Idle,             // not started
        Scanning,         // version.json in flight — home is locked
        UpToDate,         // running the latest version
        Offline,          // no internet; check abandoned for this session
        UpdateAvailable,  // newer version found; download about to start
        Downloading,      // fetching the installer
        ReadyToRestart,   // installer downloaded; awaiting user's "restart"
        Failed            // manifest/download error (non-fatal; app still usable)
    };

    static UpdateChecker& instance();

    State   state() const        { return m_state; }
    bool    isScanning() const   { return m_state == State::Scanning; }
    QString latestVersion() const { return m_latest; }
    // One-line banner text for the current state.
    QString bannerMessage() const;

public slots:
    // Kicks off the single check for this process. No-op if already run.
    void checkForUpdates();
    // Force-quit this process, run the downloaded installer, and start the
    // updated app. Every channel including a Store install goes through here.
    void relaunchForUpdate();

signals:
    void stateChanged(State state);
    void downloadProgress(int percent);

private:
    explicit UpdateChecker(QObject* parent = nullptr);
    void setState(State s);
    void onManifest(const QByteArray& body);
    void startDownload();

    QNetworkAccessManager* m_nam { nullptr };
    State   m_state { State::Idle };
    bool    m_checked { false };      // guards against a second run this session
    QString m_latest;                 // manifest version
    QString m_downloadUrl;            // platform installer URL
    QString m_installerPath;          // temp path of the downloaded installer
};

} // namespace NativeOffice
