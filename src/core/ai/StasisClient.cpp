#include "StasisClient.h"

#include "auth/AuthManager.h"
#include "auth/SecureStore.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>

namespace NativeOffice {

namespace {
constexpr char kKeyEndpoint[]   = "ai/endpoint";
constexpr char kKeyApiKey[]     = "ai/apiKey";
constexpr char kKeyDeployment[] = "ai/deployment";

// The app talks to our own backend, not to the model provider. The provider key
// would otherwise have to ship inside the binary, where anyone could read it
// out; it lives as a Cloudflare secret instead, and the same hop is where the
// monthly quota is enforced somewhere the user cannot edit.
//
// ai/endpoint stays supported as a development override: point it straight at a
// provider and the client will use that instead, which is how the wire format
// gets tested without a deploy.
QString backendChatUrl() {
    return AuthManager::instance().baseUrl() + QStringLiteral("/api/ai/chat");
}

// Images are inlined as data URLs. That is the only shape the chat-completions
// format takes for an image the model has not been given a public link to, and
// a document on someone's disk has no public link by definition.
//
// Large photographs are scaled down first. A modern phone picture is several
// megabytes, base64 adds a third again, and none of that extra detail survives
// being turned into tokens: it costs upload time and the user's character
// budget to send a bigger picture of the same thing.
constexpr int kMaxImageEdge  = 1400;
constexpr qint64 kMaxRawBytes = 12 * 1024 * 1024;

QString imageDataUrl(const QString& path) {
    QFileInfo fi(path);
    if (!fi.exists() || fi.size() > kMaxRawBytes) return {};

    QImage img(path);
    if (img.isNull()) return {};
    if (img.width() > kMaxImageEdge || img.height() > kMaxImageEdge)
        img = img.scaled(kMaxImageEdge, kMaxImageEdge,
                         Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    // PNG for anything with transparency, JPEG otherwise: a screenshot stays
    // crisp and a photograph does not become a 6 MB PNG.
    const bool alpha = img.hasAlphaChannel();
    if (!img.save(&buf, alpha ? "PNG" : "JPEG", alpha ? -1 : 85)) return {};

    return QStringLiteral("data:image/%1;base64,%2")
        .arg(alpha ? QStringLiteral("png") : QStringLiteral("jpeg"),
             QString::fromLatin1(out.toBase64()));
}

// The content parts for one turn, or an empty array when the turn is plain text
// and the simple string form will do.
QJsonArray contentParts(const AiMessage& m) {
    QJsonArray images;
    for (const AiAttachment& a : m.attachments) {
        if (a.kind != AiAttachment::Kind::Image) continue;
        const QString url = imageDataUrl(a.path);
        if (url.isEmpty()) continue;
        images.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("image_url")},
            {QStringLiteral("image_url"), QJsonObject{{QStringLiteral("url"), url}}}});
    }
    if (images.isEmpty()) return {};

    QJsonArray parts;
    parts.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                             {QStringLiteral("text"), m.text}});
    for (const QJsonValue& v : images) parts.append(v);
    return parts;
}
} // namespace

StasisClient::StasisClient(QObject* parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

QString StasisClient::endpoint()   { return QSettings().value(kKeyEndpoint).toString().trimmed(); }
QString StasisClient::apiKey()     { return QSettings().value(kKeyApiKey).toString().trimmed(); }
QString StasisClient::deployment() { return QSettings().value(kKeyDeployment).toString().trimmed(); }

void StasisClient::setEndpoint(const QString& url) {
    QSettings s; s.setValue(kKeyEndpoint, url.trimmed()); s.sync();
}
void StasisClient::setApiKey(const QString& key) {
    QSettings s; s.setValue(kKeyApiKey, key.trimmed()); s.sync();
}
void StasisClient::setDeployment(const QString& name) {
    QSettings s; s.setValue(kKeyDeployment, name.trimmed()); s.sync();
}

bool StasisClient::configured() {
    // Normal operation needs nothing configured locally: the backend holds the
    // provider credentials and the app authenticates as itself. Only the
    // development override needs both halves before it can be used.
    if (endpoint().isEmpty()) return AuthManager::instance().hasToken();
    return !apiKey().isEmpty();
}

bool StasisClient::online() {
    // Any up, non-loopback interface holding a real address counts. This can
    // still be wrong in the direction that matters least (a live LAN with no
    // route out), and the request itself reports the truth in that case.
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& n : ifaces) {
        const auto flags = n.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp)) continue;
        if (!flags.testFlag(QNetworkInterface::IsRunning)) continue;
        if (flags.testFlag(QNetworkInterface::IsLoopBack)) continue;
        const auto entries = n.addressEntries();
        for (const QNetworkAddressEntry& e : entries) {
            const QHostAddress a = e.ip();
            if (a.isNull() || a.isLoopback()) continue;
            if (a.protocol() == QAbstractSocket::IPv4Protocol
                || a.protocol() == QAbstractSocket::IPv6Protocol)
                return true;
        }
    }
    return false;
}

void StasisClient::cancel() {
    if (!m_reply) return;
    QNetworkReply* r = m_reply;
    m_reply = nullptr;                 // so handleFinished treats it as stale
    r->abort();
    r->deleteLater();
}

void StasisClient::send(const QString& systemPrompt, const QVector<AiMessage>& history,
                        Intent intent, const QString& modeName,
                        const QString& documentExcerpt) {
    cancel();
    m_buffer.clear();
    m_accumulated.clear();
    m_intent = intent;

    if (!configured()) {
        emit finished(false, {}, QStringLiteral("Sign in to use Stasis."));
        return;
    }
    if (!online()) {
        emit finished(false, {}, QStringLiteral("No internet connection."));
        return;
    }

    QJsonArray messages;
    if (!systemPrompt.isEmpty()) {
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                    {QStringLiteral("content"), systemPrompt}});
    }
    for (const AiMessage& m : history) {
        QString role = QStringLiteral("user");
        if (m.role == AiMessage::Role::Assistant) role = QStringLiteral("assistant");
        else if (m.role == AiMessage::Role::System) role = QStringLiteral("system");

        // Content is a plain string until there is an image, at which point the
        // wire format requires an array of parts. Sending the array shape
        // always would work too, but the string is what every turn without an
        // attachment is, and it keeps the request small.
        const QJsonArray parts = contentParts(m);
        messages.append(QJsonObject{
            {QStringLiteral("role"), role},
            {QStringLiteral("content"), parts.isEmpty() ? QJsonValue(m.text)
                                                        : QJsonValue(parts)}});
    }

    QJsonObject body{
        {QStringLiteral("messages"), messages},
        {QStringLiteral("stream"),   true},
        {QStringLiteral("temperature"), 0.4},
    };
    // "generate" spends one of the month's file generations; answering a
    // question spends none. The backend is what acts on this, and it re-reads
    // the tally itself rather than trusting anything else in this request.
    body.insert(QStringLiteral("intent"),
                m_intent == Intent::Generate ? QStringLiteral("generate")
                                             : QStringLiteral("answer"));
    if (!modeName.isEmpty() || !documentExcerpt.isEmpty()) {
        body.insert(QStringLiteral("context"), QJsonObject{
            {QStringLiteral("mode"),    modeName},
            {QStringLiteral("excerpt"), documentExcerpt},
        });
    }

    const bool direct = !endpoint().isEmpty();
    QNetworkRequest req{QUrl(direct ? endpoint() : backendChatUrl())};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Accept", "text/event-stream");

    if (direct) {
        // Development override, straight at the provider. Foundry accepts
        // either header depending on how the resource is fronted, so both are
        // sent and one deployment style does not need a code change.
        if (!deployment().isEmpty())
            body.insert(QStringLiteral("model"), deployment());
        req.setRawHeader("api-key", apiKey().toUtf8());
        req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + apiKey().toUtf8());
    } else {
        // Normal path: authenticate as this installation. The provider key is
        // added at the edge and never exists on this machine.
        const QByteArray token = SecureStore::load(QStringLiteral("appToken"));
        req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + token);
    }

    m_reply = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, &StasisClient::readStream);
    connect(m_reply, &QNetworkReply::finished, this, &StasisClient::handleFinished);
}

QString StasisClient::deltaFromEvent(const QByteArray& jsonLine) {
    if (jsonLine.isEmpty() || jsonLine == "[DONE]") return {};
    const QJsonObject o = QJsonDocument::fromJson(jsonLine).object();
    const QJsonArray choices = o.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) return {};
    const QJsonObject first = choices.at(0).toObject();
    // Streaming puts the text under "delta"; a non-streaming reply that arrives
    // through the same path puts it under "message".
    const QJsonObject d = first.contains(QStringLiteral("delta"))
                            ? first.value(QStringLiteral("delta")).toObject()
                            : first.value(QStringLiteral("message")).toObject();
    return d.value(QStringLiteral("content")).toString();
}

void StasisClient::readStream() {
    if (!m_reply) return;
    m_buffer += m_reply->readAll();

    // Server-sent events are newline delimited; a chunk can split a line, so
    // only whole lines are consumed and the remainder is carried over.
    int nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        QByteArray line = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);
        if (line.endsWith('\r')) line.chop(1);
        if (line.isEmpty()) continue;
        if (!line.startsWith("data:")) continue;
        const QByteArray payload = line.mid(5).trimmed();
        // Our own backend adds a frame carrying the sources a search turned
        // up. It is not part of the chat-completions format, so it is picked
        // out before the frame is read as a content delta.
        const QJsonObject obj = QJsonDocument::fromJson(payload).object();
        if (obj.contains(QStringLiteral("stasis"))) {
            const QJsonArray arr = obj.value(QStringLiteral("stasis")).toObject()
                                      .value(QStringLiteral("sources")).toArray();
            QVector<AiSource> list;
            list.reserve(arr.size());
            for (const QJsonValue& v : arr) {
                const QJsonObject o = v.toObject();
                AiSource src;
                src.title  = o.value(QStringLiteral("title")).toString();
                src.url    = o.value(QStringLiteral("url")).toString();
                src.domain = o.value(QStringLiteral("domain")).toString();
                if (!src.url.isEmpty()) list.append(src);
            }
            if (!list.isEmpty()) emit sources(list);
            continue;
        }

        const QString chunk = deltaFromEvent(payload);
        if (chunk.isEmpty()) continue;
        m_accumulated += chunk;
        emit delta(chunk);
    }
}

void StasisClient::handleFinished() {
    QNetworkReply* r = qobject_cast<QNetworkReply*>(sender());
    if (!r) return;
    if (r != m_reply) { r->deleteLater(); return; }   // superseded by a newer send

    const QNetworkReply::NetworkError err = r->error();
    const QByteArray rest = r->readAll();
    m_reply = nullptr;
    r->deleteLater();

    if (err != QNetworkReply::NoError && m_accumulated.isEmpty()) {
        // A non-streaming error body carries the useful message.
        QString msg = QStringLiteral("Stasis could not be reached.");
        const QJsonObject o = QJsonDocument::fromJson(rest).object();
        // Two shapes reach here: our own backend answers {error, message} flat,
        // and a provider hit through the development override nests it under
        // "error". Quota refusals arrive as the flat form and their message is
        // the one worth showing, so it is preferred.
        const QString flat = o.value(QStringLiteral("message")).toString();
        const QString nested = o.value(QStringLiteral("error")).toObject()
                                .value(QStringLiteral("message")).toString();
        if (!flat.isEmpty())        msg = flat;
        else if (!nested.isEmpty()) msg = nested;
        emit finished(false, {}, msg);
        return;
    }
    emit finished(true, m_accumulated, {});
}

} // namespace NativeOffice
