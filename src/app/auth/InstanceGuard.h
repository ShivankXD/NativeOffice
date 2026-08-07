#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// InstanceGuard.h — nativeoffice:// protocol plumbing.
//
// Two jobs:
//   • registerProtocolScheme(): registers the nativeoffice:// URL scheme for
//     the current user (HKCU — no admin rights needed) pointing at this exe.
//   • Single-instance forwarding for EVERY launch. A second process hands its
//     request to the running one over a QLocalSocket and exits, so there is
//     only ever one NativeOffice window:
//        nativeoffice://…   protocol URL   -> urlReceived
//        open\n<path>\n…    files to open  -> filesReceived (opened as tabs)
//        activate           bare launch    -> activateRequested (raise)
//
//     Plain launches used to skip this entirely and start a second full app.
//     That showed up after an update: the installer relaunches the app itself,
//     so anything else that also launched it produced two windows.
//
// The protocol is a best-effort fast path; the pairing flow works purely via
// polling even if the browser blocks the custom scheme.
// ─────────────────────────────────────────────────────────────────────────────

#include <QObject>
#include <QString>

class QLocalServer;

namespace NativeOffice {

class InstanceGuard : public QObject {
    Q_OBJECT
public:
    explicit InstanceGuard(QObject* parent = nullptr);

    // Register nativeoffice:// for the current user (Windows; no-op elsewhere).
    static void registerProtocolScheme();

    // True if a primary instance accepted the payload (caller should exit).
    bool forwardToPrimary(const QString& payload);

    // Payload builders, so the wire format lives in one place.
    static QString openFilesPayload(const QStringList& paths);
    static QString activatePayload();

    // Begin listening as the primary instance.
    void startPrimary();

signals:
    void urlReceived(const QString& url);
    void filesReceived(const QStringList& paths);
    void activateRequested();

private:
    QLocalServer* m_server { nullptr };
};

} // namespace NativeOffice
