// ─────────────────────────────────────────────────────────────────────────────
// WriterCollab.cpp  (Tier 5 — real-time collaboration over the LAN)
// ─────────────────────────────────────────────────────────────────────────────
#include "WriterCollab.h"

#include <QTextEdit>
#include <QTextDocument>
#include <QTextCursor>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QDataStream>
#include <QtEndian>

namespace NativeOffice {

WriterCollab::WriterCollab(QTextEdit* editor, QObject* parent)
    : QObject(parent), m_editor(editor)
{
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(350);
    connect(m_debounce, &QTimer::timeout, this, &WriterCollab::onLocalChange);

    if (m_editor) {
        connect(m_editor->document(), &QTextDocument::contentsChanged, this, [this]{
            if (active() && !m_applyingRemote) m_debounce->start();
        });
    }
}

int WriterCollab::peerCount() const {
    if (m_server) return int(m_peers.size());
    return m_client ? 1 : 0;
}

QByteArray WriterCollab::frame(const QByteArray& payload) const {
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);
    ds << quint32(payload.size());
    out.append(payload);
    return out;
}

bool WriterCollab::startHost(quint16 port, QString& error) {
    stop();
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::Any, port)) {
        error = m_server->errorString();
        m_server->deleteLater();
        m_server = nullptr;
        return false;
    }
    connect(m_server, &QTcpServer::newConnection, this, [this]{
        while (m_server->hasPendingConnections()) {
            QTcpSocket* s = m_server->nextPendingConnection();
            m_peers.append(s);
            connect(s, &QTcpSocket::readyRead, this, [this, s]{ onReadyRead(s); });
            connect(s, &QTcpSocket::disconnected, this, [this, s]{
                m_peers.removeAll(s); m_rx.remove(s); s->deleteLater();
                emit statusChanged(QString("Hosting — %1 peer(s)").arg(peerCount()));
            });
            sendSnapshotTo(s);   // bring the newcomer in sync immediately
            emit statusChanged(QString("Hosting — %1 peer(s)").arg(peerCount()));
        }
    });
    emit statusChanged(QString("Hosting on port %1 — waiting for peers").arg(port));
    return true;
}

bool WriterCollab::joinHost(const QString& host, quint16 port, QString& error) {
    stop();
    m_client = new QTcpSocket(this);
    connect(m_client, &QTcpSocket::readyRead, this, [this]{ onReadyRead(m_client); });
    connect(m_client, &QTcpSocket::connected, this, [this, host, port]{
        emit statusChanged(QString("Connected to %1:%2").arg(host).arg(port));
    });
    connect(m_client, &QTcpSocket::disconnected, this, [this]{
        emit statusChanged("Disconnected");
    });
    m_client->connectToHost(host, port);
    if (!m_client->waitForConnected(4000)) {
        error = m_client->errorString();
        m_client->deleteLater();
        m_client = nullptr;
        return false;
    }
    return true;
}

void WriterCollab::stop() {
    for (QTcpSocket* s : m_peers) s->deleteLater();
    m_peers.clear();
    m_rx.clear();
    if (m_client) { m_client->deleteLater(); m_client = nullptr; }
    if (m_server) { m_server->deleteLater(); m_server = nullptr; }
}

void WriterCollab::onLocalChange() {
    if (!m_editor || !active()) return;
    const QByteArray payload = m_editor->document()->toHtml().toUtf8();
    broadcast(frame(payload));
}

void WriterCollab::broadcast(const QByteArray& payload, QTcpSocket* except) {
    if (m_client) { m_client->write(payload); return; }
    for (QTcpSocket* s : m_peers)
        if (s != except && s->state() == QAbstractSocket::ConnectedState)
            s->write(payload);
}

void WriterCollab::sendSnapshotTo(QTcpSocket* s) {
    if (!m_editor) return;
    s->write(frame(m_editor->document()->toHtml().toUtf8()));
}

void WriterCollab::onReadyRead(QTcpSocket* s) {
    m_rx[s].append(s->readAll());
    // Parse all complete length-framed messages currently buffered.
    for (;;) {
        QByteArray& buf = m_rx[s];
        if (buf.size() < 4) break;
        const quint32 len = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(buf.constData()));
        if (buf.size() < int(4 + len)) break;
        const QByteArray payload = buf.mid(4, int(len));
        buf.remove(0, int(4 + len));
        const QString html = QString::fromUtf8(payload);
        applyRemote(html);
        // Host relays the change to the other peers so everyone converges.
        if (m_server) broadcast(frame(payload), s);
    }
}

void WriterCollab::applyRemote(const QString& html) {
    if (!m_editor) return;
    m_applyingRemote = true;
    const int pos = m_editor->textCursor().position();
    m_editor->setHtml(html);
    QTextCursor c = m_editor->textCursor();
    c.setPosition(qMin(pos, m_editor->document()->characterCount() - 1));
    m_editor->setTextCursor(c);
    m_applyingRemote = false;
}

} // namespace NativeOffice
