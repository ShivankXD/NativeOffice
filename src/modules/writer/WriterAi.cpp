// ─────────────────────────────────────────────────────────────────────────────
// WriterAi.cpp  (Tier 5 — AI assistant)
// ─────────────────────────────────────────────────────────────────────────────
#include "WriterAi.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QUrl>

namespace NativeOffice {

namespace {
QSettings settings() { return QSettings("NativeOffice", "NativeOffice"); }
constexpr const char* kDefaultModel = "claude-opus-4-8";
}

WriterAi::WriterAi(QObject* parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

QString WriterAi::apiKey() { return settings().value("ai/apiKey").toString(); }
void    WriterAi::setApiKey(const QString& key) { settings().setValue("ai/apiKey", key); }

QString WriterAi::model() {
    const QString m = settings().value("ai/model").toString();
    return m.isEmpty() ? QString::fromLatin1(kDefaultModel) : m;
}
void WriterAi::setModel(const QString& m) { settings().setValue("ai/model", m); }

bool WriterAi::hasKey() { return !apiKey().trimmed().isEmpty(); }

void WriterAi::ask(const QString& systemPrompt, const QString& userContent,
                   std::function<void(bool, QString)> cb) {
    const QString key = apiKey().trimmed();
    if (key.isEmpty()) {
        cb(false, "No API key set.\n\nOpen Tools ▸ AI Assistant ▸ Settings and paste "
                  "your Anthropic API key to enable the AI features.");
        return;
    }

    QJsonObject body;
    body["model"]      = model();
    body["max_tokens"] = 4096;
    if (!systemPrompt.isEmpty()) body["system"] = systemPrompt;
    QJsonArray messages;
    QJsonObject msg;
    msg["role"]    = "user";
    msg["content"] = userContent;
    messages.append(msg);
    body["messages"] = messages;

    QNetworkRequest req(QUrl("https://api.anthropic.com/v1/messages"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("x-api-key", key.toUtf8());
    req.setRawHeader("anthropic-version", "2023-06-01");

    QNetworkReply* reply = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(data);

        if (reply->error() != QNetworkReply::NoError) {
            QString msg = reply->errorString();
            if (doc.isObject()) {
                const QJsonObject err = doc.object().value("error").toObject();
                if (!err.isEmpty()) msg = err.value("message").toString();
            }
            cb(false, "AI request failed:\n" + msg);
            return;
        }

        // Refusal / non-text stop reasons still return 200 — surface gracefully.
        QString text;
        for (const QJsonValue& v : doc.object().value("content").toArray()) {
            const QJsonObject o = v.toObject();
            if (o.value("type").toString() == "text") text += o.value("text").toString();
        }
        if (text.isEmpty())
            cb(false, "The AI returned no text (it may have declined the request).");
        else
            cb(true, text);
    });
}

} // namespace NativeOffice
