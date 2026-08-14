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

void AiImageFetcher::request(quint64 token, const QString& query) {
    const QString key = query.trimmed().toLower();
    if (key.isEmpty()) { emit ready(token, {}); return; }

    const auto hit = m_cache.constFind(key);
    if (hit != m_cache.constEnd()) { emit ready(token, hit.value()); return; }

    m_queue.enqueue({ token, query.trimmed() });
    pump();
}

void AiImageFetcher::abandonAll() {
    m_queue.clear();
    // Replies already in flight are not aborted: they will complete, find the
    // generation has moved on and drop their bytes. Aborting mid-body is what
    // leaves a socket in a state the next request pays for.
    ++m_generation;
}

void AiImageFetcher::pump() {
    while (m_inFlight < kMaxInFlight && !m_queue.isEmpty()) {
        const Pending p = m_queue.dequeue();
        start(p.token, p.query);
    }
}

void AiImageFetcher::start(quint64 token, const QString& query) {
    QUrl url(AuthManager::instance().baseUrl() + QStringLiteral("/api/ai/image"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query);
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ")
                                      + SecureStore::load(QStringLiteral("appToken")));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(15000);

    const quint64 generation = m_generation;
    const QString key = query.toLower();

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
        if (!bytes.isEmpty()) m_cache.insert(key, bytes);

        // A reply that outlived its deck is read and thrown away rather than
        // delivered: the slide index it was for now belongs to something else.
        if (generation == m_generation) emit ready(token, bytes);
        pump();
    });
}

} // namespace NativeOffice
