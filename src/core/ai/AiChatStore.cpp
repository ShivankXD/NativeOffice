#include "AiChatStore.h"

#include "auth/AuthManager.h"
#include "auth/SecureStore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>

namespace NativeOffice {

namespace {

// Every call here needs the same three things, and getting one of them wrong
// is a 401 that looks like an empty history.
bool authorize(QNetworkRequest& req, const QString& path) {
    const QByteArray token = SecureStore::load(QStringLiteral("appToken"));
    if (token.isEmpty()) return false;
    req.setUrl(QUrl(AuthManager::instance().baseUrl() + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + token);
    return true;
}

} // namespace

AiChatStore::AiChatStore(QObject* parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

QString AiChatStore::mintId() {
    // 16 hex characters. The endpoint accepts 8 to 40, and this is well inside
    // the range where a collision between two of a user's own chats is not a
    // thing that happens.
    QString hex;
    hex.reserve(16);
    for (int i = 0; i < 2; ++i)
        hex += QStringLiteral("%1").arg(QRandomGenerator::global()->generate(),
                                        8, 16, QLatin1Char('0'));
    return QStringLiteral("chat_") + hex;
}

QString AiChatStore::titleFrom(const QString& firstPrompt) {
    QString t = firstPrompt.simplified();      // collapses newlines too
    if (t.isEmpty()) return QStringLiteral("New chat");
    constexpr int kMax = 60;
    if (t.size() <= kMax) return t;
    // Cut on a word boundary when there is one nearby, so the label does not
    // end mid-word for the sake of four characters.
    const int space = t.lastIndexOf(QLatin1Char(' '), kMax);
    t.truncate(space > kMax - 15 ? space : kMax);
    return t + QStringLiteral("...");
}

void AiChatStore::save(const QString& chatId, const QString& title, const QString& mode) {
    QNetworkRequest req;
    if (chatId.isEmpty() || title.isEmpty()
        || !authorize(req, QStringLiteral("/api/ai/chats"))) return;

    const QJsonObject body{
        {QStringLiteral("id"),    chatId},
        {QStringLiteral("title"), title},
        {QStringLiteral("mode"),  mode},
    };
    QNetworkReply* r = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    // Nothing to do with the answer. The reply is still consumed so the socket
    // is released rather than left for the manager to reap later.
    connect(r, &QNetworkReply::finished, r, &QNetworkReply::deleteLater);
}

void AiChatStore::appendTurn(const QString& chatId, bool fromUser,
                             const QString& text, const QJsonObject& action) {
    QNetworkRequest req;
    if (chatId.isEmpty() || text.isEmpty()
        || !authorize(req, QStringLiteral("/api/ai/messages"))) return;

    QJsonObject body{
        {QStringLiteral("chat"),    chatId},
        {QStringLiteral("role"),    fromUser ? QStringLiteral("user")
                                             : QStringLiteral("assistant")},
        {QStringLiteral("content"), text},
    };
    if (!action.isEmpty()) body.insert(QStringLiteral("action"), action);

    QNetworkReply* r = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, r, &QNetworkReply::deleteLater);
}

void AiChatStore::loadChats() {
    QNetworkRequest req;
    if (!authorize(req, QStringLiteral("/api/ai/chats"))) {
        emit loadFailed(QStringLiteral("Sign in to see your history."));
        return;
    }
    QNetworkReply* r = m_net->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            emit loadFailed(QStringLiteral("Could not reach your history."));
            return;
        }
        const QJsonArray arr =
            QJsonDocument::fromJson(r->readAll()).object()
                .value(QStringLiteral("chats")).toArray();
        QVector<AiChatSummary> out;
        out.reserve(arr.size());
        for (const QJsonValue& v : arr) {
            const QJsonObject o = v.toObject();
            AiChatSummary c;
            c.id    = o.value(QStringLiteral("id")).toString();
            c.title = o.value(QStringLiteral("title")).toString();
            c.mode  = o.value(QStringLiteral("mode")).toString();
            c.updatedAt = QDateTime::fromSecsSinceEpoch(
                qint64(o.value(QStringLiteral("updated_at")).toDouble()));
            if (!c.id.isEmpty()) out.push_back(c);
        }
        emit chatsLoaded(out);
    });
}

void AiChatStore::loadTurns(const QString& chatId) {
    QNetworkRequest req;
    if (chatId.isEmpty()
        || !authorize(req, QStringLiteral("/api/ai/messages?chat=") + chatId)) {
        emit loadFailed(QStringLiteral("Sign in to see your history."));
        return;
    }
    QNetworkReply* r = m_net->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r, chatId] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            emit loadFailed(QStringLiteral("Could not open that conversation."));
            return;
        }
        const QJsonArray arr =
            QJsonDocument::fromJson(r->readAll()).object()
                .value(QStringLiteral("messages")).toArray();
        QVector<AiStoredTurn> out;
        out.reserve(arr.size());
        for (const QJsonValue& v : arr) {
            const QJsonObject o = v.toObject();
            AiStoredTurn t;
            t.fromUser = o.value(QStringLiteral("role")).toString()
                         == QLatin1String("user");
            t.text = o.value(QStringLiteral("content")).toString();
            t.at   = QDateTime::fromSecsSinceEpoch(
                qint64(o.value(QStringLiteral("created_at")).toDouble()));
            // Stored as a JSON string, because D1 has no object column.
            const QString raw = o.value(QStringLiteral("action")).toString();
            if (!raw.isEmpty())
                t.action = QJsonDocument::fromJson(raw.toUtf8()).object();
            if (!t.text.isEmpty()) out.push_back(t);
        }
        emit turnsLoaded(chatId, out);
    });
}

void AiChatStore::remove(const QString& chatId) {
    QNetworkRequest req;
    if (chatId.isEmpty()
        || !authorize(req, QStringLiteral("/api/ai/chats?id=") + chatId)) return;
    QNetworkReply* r = m_net->deleteResource(req);
    connect(r, &QNetworkReply::finished, this, [this, r, chatId] {
        r->deleteLater();
        emit removed(chatId);
    });
}

} // namespace NativeOffice
