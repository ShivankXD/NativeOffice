#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AiChatStore.h — remembers what a conversation was called, and nothing else.
//
// Titles go to the account's row in D1 so the list survives a reinstall and
// follows the user between machines. The transcript deliberately does not: the
// notice the user accepts says their prompts are sent so the model can answer,
// which is a far narrower promise than keeping a copy of every conversation
// they have ever had. Storing the name is enough to show a history.
//
// The id is minted here, before the first request, so a conversation has a
// stable identity from its opening message rather than only once a server has
// answered. Failures are silent by design: a history label that did not save is
// not worth interrupting someone mid-sentence for, and the next title write for
// the same id simply overwrites.
// ─────────────────────────────────────────────────────────────────────────────

#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace NativeOffice {

class AiChatStore : public QObject {
    Q_OBJECT
public:
    explicit AiChatStore(QObject* parent = nullptr);

    // A fresh conversation id, "chat_" plus random hex.
    static QString mintId();

    // Turns an opening prompt into a short label. Trimmed to one line and
    // capped, because this is a list entry and not a summary.
    static QString titleFrom(const QString& firstPrompt);

    // Creates or renames the conversation. Safe to call repeatedly.
    void save(const QString& chatId, const QString& title, const QString& mode);

private:
    QNetworkAccessManager* m_net { nullptr };
};

} // namespace NativeOffice
