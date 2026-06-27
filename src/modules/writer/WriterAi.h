#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// WriterAi.h  (Tier 5 — AI assistant)
// A thin, real client for the Anthropic Messages API (raw HTTP via
// QNetworkAccessManager — this is a C++/Qt app with no official Anthropic SDK).
// Powers the Writer's rewrite / summarize / generate actions.
//
//   • The API key and model are stored in QSettings (set in-app); nothing is
//     hard-coded. Without a key the actions explain how to add one.
//   • ask() POSTs a single-turn request and delivers the reply text to a
//     callback on the GUI thread. Requires network access at runtime.
//
// Wire format follows the Anthropic API: POST /v1/messages with the
// x-api-key + anthropic-version headers; the answer is content[0].text.
// ─────────────────────────────────────────────────────────────────────────────

#include <QObject>
#include <QString>
#include <functional>

class QNetworkAccessManager;

namespace NativeOffice {

class WriterAi : public QObject {
    Q_OBJECT
public:
    explicit WriterAi(QObject* parent = nullptr);

    static QString apiKey();
    static void    setApiKey(const QString& key);
    static QString model();                       // default: claude-opus-4-8
    static void    setModel(const QString& model);
    [[nodiscard]] static bool hasKey();

    // Single-turn request. cb(ok, text) runs on the GUI thread when it returns.
    void ask(const QString& systemPrompt, const QString& userContent,
             std::function<void(bool ok, QString text)> cb);

private:
    QNetworkAccessManager* m_net { nullptr };
};

} // namespace NativeOffice
