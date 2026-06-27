#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// WriterCollab.h  (Tier 5 — real-time collaboration over the LAN)
// Real peer-to-peer document sync between running NativeOffice instances on the
// same network, using QTcpServer / QTcpSocket. One instance hosts a session;
// others join by host:port. On every (debounced) local edit the document is
// broadcast to all peers as a length-framed HTML snapshot; incoming snapshots
// replace the local document (last-writer-wins).
//
// This is genuine networked sync — not a mock — but intentionally simple: there
// is no operational-transform/CRDT conflict resolution, so two people typing in
// the same spot at the same instant can overwrite each other. It is the
// honest, buildable core of collaboration for a standalone desktop app (true
// Google-Docs-grade merge needs a backend service).
// ─────────────────────────────────────────────────────────────────────────────

#include <QObject>
#include <QHash>
#include <QList>
#include <QByteArray>

class QTextEdit;
class QTcpServer;
class QTcpSocket;
class QTimer;

namespace NativeOffice {

class WriterCollab : public QObject {
    Q_OBJECT
public:
    explicit WriterCollab(QTextEdit* editor, QObject* parent = nullptr);

    bool startHost(quint16 port, QString& error);
    bool joinHost(const QString& host, quint16 port, QString& error);
    void stop();
    [[nodiscard]] bool active() const { return m_server || m_client; }
    [[nodiscard]] bool isHost() const { return m_server != nullptr; }
    [[nodiscard]] int  peerCount() const;

signals:
    void statusChanged(const QString& status);

private:
    void onLocalChange();
    void broadcast(const QByteArray& payload, QTcpSocket* except = nullptr);
    void sendSnapshotTo(QTcpSocket* s);
    void onReadyRead(QTcpSocket* s);
    void applyRemote(const QString& html);
    QByteArray frame(const QByteArray& payload) const;

    QTextEdit*          m_editor   { nullptr };
    QTcpServer*         m_server   { nullptr };
    QList<QTcpSocket*>  m_peers;          // host side
    QTcpSocket*         m_client   { nullptr };   // joined side
    QTimer*             m_debounce { nullptr };
    bool                m_applyingRemote { false };
    QHash<QTcpSocket*, QByteArray> m_rx;  // per-socket receive buffers
};

} // namespace NativeOffice
