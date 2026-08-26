#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AiChatStore.h — the conversations behind the History panel.
//
// This used to store titles and nothing else, on the reasoning that the notice
// the user accepts says prompts are sent so the model can answer, which is a
// narrower promise than keeping a copy of every conversation. That has been
// reversed deliberately: a History list that can only show headings is not a
// history, and the notice now says the conversation is kept and that deleting a
// chat deletes it. If what is stored here grows, the notice is what changes
// first. See migrations/0016_ai_messages.sql.
//
// What is still NOT stored is the contents of the user's file. A prompt they
// typed is theirs and they know they sent it; the document Stasis was reading
// over their shoulder is a different thing, and it stays on the machine.
//
// The id is minted here, before the first request, so a conversation has a
// stable identity from its opening message rather than only once a server has
// answered. Write failures are silent by design: a history entry that did not
// save is not worth interrupting someone mid-sentence for. Read failures are
// not silent, because a History panel that shows nothing looks the same as an
// account with no history, and those are very different things.
// ─────────────────────────────────────────────────────────────────────────────

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

class QNetworkAccessManager;

namespace NativeOffice {

// One entry in the History list.
struct AiChatSummary {
    QString   id;
    QString   title;
    QString   mode;
    QDateTime updatedAt;
};

// One turn of a reopened conversation. `action` is what Stasis actually did,
// and is empty for a plain answer. It matters because the bubble text often
// cannot carry it: a turn that built a deck says "Built into your
// presentation." and everything of substance went into the file.
struct AiStoredTurn {
    bool        fromUser { false };
    QString     text;
    QJsonObject action;
    QDateTime   at;
};

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

    // Appends one turn. `action` may be empty for an ordinary answer.
    void appendTurn(const QString& chatId, bool fromUser, const QString& text,
                    const QJsonObject& action = {});

    // Asks for the History list; answers on chatsLoaded or loadFailed.
    void loadChats();

    // Asks for one conversation's turns; answers on turnsLoaded or loadFailed.
    void loadTurns(const QString& chatId);

    // Removes a conversation and everything said in it.
    void remove(const QString& chatId);

signals:
    void chatsLoaded(const QVector<AiChatSummary>& chats);
    void turnsLoaded(const QString& chatId, const QVector<AiStoredTurn>& turns);
    void loadFailed(const QString& reason);
    void removed(const QString& chatId);

private:
    QNetworkAccessManager* m_net { nullptr };
};

} // namespace NativeOffice
