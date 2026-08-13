#include "AiChatStore.h"

#include "auth/AuthManager.h"
#include "auth/SecureStore.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>

namespace NativeOffice {

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
    const QByteArray token = SecureStore::load(QStringLiteral("appToken"));
    if (token.isEmpty() || chatId.isEmpty() || title.isEmpty()) return;

    QNetworkRequest req{QUrl(AuthManager::instance().baseUrl()
                             + QStringLiteral("/api/ai/chats"))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + token);

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

} // namespace NativeOffice
