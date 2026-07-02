#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// InstanceGuard.h — nativeoffice:// protocol plumbing.
//
// Two jobs:
//   • registerProtocolScheme(): registers the nativeoffice:// URL scheme for
//     the current user (HKCU — no admin rights needed) pointing at this exe.
//   • Single-instance forwarding for protocol launches: when the browser
//     launches a second instance with a nativeoffice:// argument, the URL is
//     forwarded over a QLocalSocket to the running instance (which reacts via
//     urlReceived) and the second instance exits. Plain file-open launches are
//     unaffected — only protocol URLs are forwarded.
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

    // True if a primary instance accepted the URL (caller should exit).
    bool forwardToPrimary(const QString& url);

    // Begin listening as the primary instance.
    void startPrimary();

signals:
    void urlReceived(const QString& url);

private:
    QLocalServer* m_server { nullptr };
};

} // namespace NativeOffice
