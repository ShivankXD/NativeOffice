// ─────────────────────────────────────────────────────────────────────────────
// InstanceGuard.cpp — implementation (see header).
// ─────────────────────────────────────────────────────────────────────────────
#include "InstanceGuard.h"

#include <QCoreApplication>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSettings>

namespace NativeOffice {

// Per-user server name so multiple Windows sessions don't collide.
static QString serverName() {
    return QStringLiteral("NativeOffice-Instance-") +
           qEnvironmentVariable("USERNAME", QStringLiteral("default"));
}

InstanceGuard::InstanceGuard(QObject* parent)
    : QObject(parent)
{}

void InstanceGuard::registerProtocolScheme() {
#ifdef Q_OS_WIN
    const QString exe =
        QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    QSettings cls(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\nativeoffice"),
                  QSettings::NativeFormat);
    cls.setValue(QStringLiteral("."), QStringLiteral("URL:NativeOffice Protocol"));
    cls.setValue(QStringLiteral("URL Protocol"), QString());
    QSettings cmd(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\nativeoffice")
                  + QStringLiteral("\\shell\\open\\command"),
                  QSettings::NativeFormat);
    cmd.setValue(QStringLiteral("."),
                 QStringLiteral("\"%1\" \"%2\"").arg(exe, QStringLiteral("%1")));
#endif
}

bool InstanceGuard::forwardToPrimary(const QString& url) {
    QLocalSocket sock;
    sock.connectToServer(serverName());
    if (!sock.waitForConnected(500)) return false;   // no primary running
    sock.write(url.toUtf8());
    sock.flush();
    sock.waitForBytesWritten(500);
    sock.disconnectFromServer();
    return true;
}

void InstanceGuard::startPrimary() {
    // A stale server can linger after a crash; reclaim the name.
    QLocalServer::removeServer(serverName());
    m_server = new QLocalServer(this);
    if (!m_server->listen(serverName())) return;     // non-fatal: fast path off

    connect(m_server, &QLocalServer::newConnection, this, [this]() {
        while (QLocalSocket* sock = m_server->nextPendingConnection()) {
            connect(sock, &QLocalSocket::readyRead, this, [this, sock]() {
                const QString url = QString::fromUtf8(sock->readAll()).trimmed();
                if (url.startsWith(QLatin1String("nativeoffice://")))
                    emit urlReceived(url);
                sock->deleteLater();
            });
            connect(sock, &QLocalSocket::disconnected,
                    sock, &QLocalSocket::deleteLater);
        }
    });
}

} // namespace NativeOffice
