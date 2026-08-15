#include "AiImageFetcher.h"

#include "auth/AuthManager.h"
#include "auth/SecureStore.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace NativeOffice {

namespace {
// Three at a time. A ten slide deck can ask for eight pictures at once, and
// opening eight sockets to the same host makes every one of them slower than
// running them three at a time would have been.
constexpr int kMaxInFlight = 3;
constexpr int kMaxBytes    = 8 * 1024 * 1024;
}

AiImageFetcher::AiImageFetcher(QObject* parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

void AiImageFetcher::request(quint64 token, const QString& query,
                             int targetWidth) {
    const QString key = query.trimmed().toLower();
    if (key.isEmpty()) { emit ready(token, {}); return; }

    // A deck that asks for the same subject twice should not show the same
    // photograph twice: two consecutive slides carrying one picture reads as a
    // fault rather than as a motif. The repeat is asked for by index instead,
    // and only an exact repeat of a query already served is cached.
    const int seen = m_seen.value(key, 0);
    m_seen.insert(key, seen + 1);

    if (seen == 0) {
        const auto hit = m_cache.constFind(key);
        if (hit != m_cache.constEnd()) { emit ready(token, hit.value()); return; }
    }

    m_queue.enqueue({ token, query.trimmed(), seen, targetWidth });
    pump();
}

void AiImageFetcher::abandonAll() {
    m_queue.clear();
    m_seen.clear();
    // Replies already in flight are not aborted: they will complete, find the
    // generation has moved on and drop their bytes. Aborting mid-body is what
    // leaves a socket in a state the next request pays for.
    ++m_generation;
}

void AiImageFetcher::pump() {
    while (m_inFlight < kMaxInFlight && !m_queue.isEmpty()) {
        const Pending p = m_queue.dequeue();
        start(p.token, p.query, p.variant, p.width);
    }
}

void AiImageFetcher::start(quint64 token, const QString& query, int variant,
                           int width) {
    QUrl url(AuthManager::instance().baseUrl() + QStringLiteral("/api/ai/image"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query);
    if (variant > 0) q.addQueryItem(QStringLiteral("i"), QString::number(variant));
    // The exact width this picture will be drawn at, so the backend can ask
    // its sources for a thumbnail of that size rather than a scan that has to
    // be downloaded in full and then thrown away.
    q.addQueryItem(QStringLiteral("w"), QString::number(qBound(320, width, 1920)));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ")
                                      + SecureStore::load(QStringLiteral("appToken")));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(15000);

    const quint64 generation = m_generation;
    // Only the first result for a query is worth remembering: the later ones
    // exist precisely because that first one was already used.
    const QString key = variant == 0 ? query.toLower() : QString();

    ++m_inFlight;
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, token, key, generation] {
        reply->deleteLater();
        --m_inFlight;

        QByteArray bytes;
        if (reply->error() == QNetworkReply::NoError) {
            bytes = reply->readAll();
            if (bytes.size() > kMaxBytes) bytes.clear();
        }
        if (!bytes.isEmpty() && !key.isEmpty()) m_cache.insert(key, bytes);

        // A reply that outlived its deck is read and thrown away rather than
        // delivered: the slide index it was for now belongs to something else.
        if (generation == m_generation) emit ready(token, bytes);
        pump();
    });
}

} // namespace NativeOffice
