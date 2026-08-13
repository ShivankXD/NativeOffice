#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// StasisClient.h — the network side of the assistant.
//
// Speaks the OpenAI-compatible chat-completions wire format, which is what
// Azure AI Foundry serves, so the endpoint, key and deployment name are the
// only things that change when the model behind Stasis changes. All three are
// configuration rather than code: today they point at a Foundry deployment,
// and when Stasis itself is ready only the settings move.
//
// Deliberately no user-facing settings screen. Keys are operator configuration,
// not a preference, and a field where a user can paste one is a field where a
// user can paste the wrong one and be told the assistant is broken.
//
// Streaming is the default because it is what makes the sidebar feel alive:
// delta() fires per chunk as the answer arrives, and finished() closes the
// turn. A non-streaming reply still works, it simply arrives as one delta.
// ─────────────────────────────────────────────────────────────────────────────

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include "AiTypes.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace NativeOffice {

class StasisClient : public QObject {
    Q_OBJECT
public:
    explicit StasisClient(QObject* parent = nullptr);

    // ── Operator configuration ──────────────────────────────────────────────
    // Read from QSettings so a build can be pointed at a different deployment
    // without recompiling. setEndpoint/setApiKey exist for provisioning, not
    // for a preferences page.
    static QString endpoint();          // full chat-completions URL
    static QString apiKey();
    static QString deployment();        // model / deployment name
    static void    setEndpoint(const QString& url);
    static void    setApiKey(const QString& key);
    static void    setDeployment(const QString& name);

    // True when the client has everything it needs to make a request.
    static bool configured();

    // True when the machine currently has a route to the internet. Cheap and
    // local: it reads the OS interface list rather than making a request, so it
    // answers instantly and cannot itself hang when offline.
    static bool online();

    // Whether this turn produces a file. Generate spends one of the month's
    // generations; Answer spends none. The backend decides on this, and it
    // re-reads the stored tally rather than trusting the number in the request.
    enum class Intent { Answer, Generate };

    // Sends the conversation and streams the reply. Only one request runs at a
    // time; starting another cancels the one in flight.
    void send(const QString& systemPrompt, const QVector<AiMessage>& history,
              Intent intent = Intent::Answer);

    void cancel();
    bool busy() const { return m_reply != nullptr; }

signals:
    void delta(const QString& textChunk);
    void finished(bool ok, const QString& fullText, const QString& error);

private:
    void readStream();
    void handleFinished();
    // Turns one server-sent-event data line into its text delta. Returns an
    // empty string for keep-alives and for the terminating sentinel.
    static QString deltaFromEvent(const QByteArray& jsonLine);

    Intent                 m_intent { Intent::Answer };
    QNetworkAccessManager* m_net   { nullptr };
    QNetworkReply*         m_reply { nullptr };
    QByteArray             m_buffer;      // partial SSE line carry-over
    QString                m_accumulated;
};

} // namespace NativeOffice
