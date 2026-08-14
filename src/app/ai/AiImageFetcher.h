#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AiImageFetcher.h — fetches the photographs a generated deck asked for.
//
// The app never reaches out to an image host directly. It asks our own backend
// for "a photograph of a turbocharger" and gets bytes back; the backend does
// the searching, checks the licence, follows the redirect and caps the size.
// That is the same reasoning as the chat endpoint: one origin the app talks to,
// and no path by which a model's output turns into a request to an arbitrary
// address chosen by a model.
//
// Fetching is deliberately detached from slide building. A slide is placed the
// instant its line arrives, complete with a sized placeholder in the theme's
// colours, and the picture drops into it whenever it lands. A deck generated
// with the network down is a deck of colour blocks, not a deck of holes.
// ─────────────────────────────────────────────────────────────────────────────

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace NativeOffice {

class AiImageFetcher : public QObject {
    Q_OBJECT
public:
    explicit AiImageFetcher(QObject* parent = nullptr);

    // Asks for one picture. `token` is handed straight back with the result, so
    // the caller can find the slide and the item it belongs to without this
    // class knowing anything about decks.
    void request(quint64 token, const QString& query);

    // Drops every queued and in-flight request. Called when a generation is
    // rolled back or a new one starts: pictures for a deck that no longer
    // exists must not land in the one that replaced it.
    void abandonAll();

signals:
    // Raw bytes as they arrived, in whatever format the host served. Empty
    // means the picture could not be had, and the placeholder stays.
    void ready(quint64 token, const QByteArray& bytes);

private:
    void pump();
    void start(quint64 token, const QString& query, int variant);

    struct Pending { quint64 token; QString query; int variant; };

    QNetworkAccessManager*    m_net { nullptr };
    QQueue<Pending>           m_queue;
    int                       m_inFlight { 0 };
    // A deck routinely wants the same subject twice. Serving the second one
    // from memory saves a round trip and, more usefully, guarantees the two
    // slides do not show two different photographs of the same thing.
    QHash<QString, QByteArray> m_cache;
    // How many times each query has been asked for in this generation, so a
    // repeat can ask the backend for the next result rather than the same one.
    QHash<QString, int>       m_seen;
    quint64                   m_generation { 0 };
};

} // namespace NativeOffice
