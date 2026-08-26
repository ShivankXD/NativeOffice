#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AiSidebar.h — the Stasis panel that lives beside the office suite.
//
// One instance for the whole window rather than one per document. The suite is
// a stack of tabs and the assistant is not: moving between Writer, Sheets and a
// PDF should carry the conversation with you, and the panel only has to notice
// where it now is. That is what setMode() is for, and what the "In <mode>" chip
// above the chat reports.
//
// The panel is a sibling of the tab stack inside a splitter, so opening it
// narrows the document rather than covering it, and the divider resizes both.
//
// What it does depends on where it is: in Writer, Sheets, Slides and the
// Markdown editor it can build and change the document; in PDF and the image
// resizer it answers questions only. See AiCapability in AiTypes.h.
// ─────────────────────────────────────────────────────────────────────────────

#include <QJsonObject>
#include <QVector>
#include <QWidget>

#include "ai/AiTypes.h"

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QToolButton;
class QStackedWidget;
class QScrollArea;
class QTextEdit;
class QTimer;
class QVBoxLayout;
class QGraphicsOpacityEffect;
class QPropertyAnimation;

namespace NativeOffice {

class AiChatStore;
struct AiStoredTurn;
class AiStreamTarget;
class AiSourcesStrip;
class AiDocumentAgent;
class AiToast;
class StasisClient;

class AiSidebar : public QWidget {
    Q_OBJECT
public:
    explicit AiSidebar(QWidget* parent = nullptr);

    // Where the suite currently is. Updates the chip and what the panel offers.
    void setMode(AiMode mode);
    AiMode mode() const { return m_mode; }

    // Clears the transcript back to the "Powered by Stasis" hero. Called when a
    // fresh app session opens the panel for the first time.
    void startNewSession();

    // Puts the caret in the composer.
    void focusComposer();

    // Dev-only: open the History list, which is otherwise a click on the rail.
    void devShowHistory();

    // The text surface the agent may write into for the current tab, or null
    // where there is nothing writable. Set by the shell alongside setMode.
    void setDocumentTarget(QTextEdit* target);

    // The deck to build into, for a presentation tab. Null everywhere else.
    // Kept behind an interface because core cannot depend on the Impress
    // module; see AiStreamTarget.h.
    void setDeckTarget(AiStreamTarget* target);

signals:
    void closeRequested();

protected:
    void resizeEvent(QResizeEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;

private:
    // ── construction ────────────────────────────────────────────────────────
    QWidget* buildHeader();
    QWidget* buildChatArea();
    QWidget* buildComposer();
    QWidget* buildHero();
    QWidget* buildRail();
    QWidget* buildHistoryPage();
    QWidget* buildFooter();
    QToolButton* railButton(QWidget* parent, const QString& text, bool on);

    // ── the two pages behind the header ─────────────────────────────────────
    void showAssist();
    void showHistory();
    void reloadHistory();
    void openHistoryItem(QListWidgetItem* item);
    void replayConversation(const QString& chatId, const QVector<AiStoredTurn>& turns);

    // ── "what Stasis did" ───────────────────────────────────────────────────
    // A turn that wrote into a file leaves a bubble saying almost nothing, so
    // what it actually did is recorded beside it and stored with it.
    static QString actionSummary(const QJsonObject& action);
    void attachActionBar(QWidget* bubble, const QJsonObject& action);
    void showActionDetail(const QJsonObject& action);
    // Assembled over the course of a turn and written out when it finishes.
    QJsonObject buildTurnAction(const QString& kind, int written, const QString& full);
    QJsonObject m_turnAction;
    QString     m_turnPrompt;

    // ── conversation ────────────────────────────────────────────────────────
    void submit();
    QWidget* appendMessage(const AiMessage& m);
    void refreshHeroVisibility();
    void scrollToBottom();
    void setWorking(bool on);
    void refreshQuotaLabel();
    // Draws the composer's focus ring. A text box that looks identical whether
    // or not it has the caret is the single cheapest-looking thing in a panel.
    void setComposerFocused(bool on);
    void pickAttachments();
    void rebuildAttachmentStrip();

    AiMode m_mode { AiMode::Home };

    QLabel*      m_modeChip { nullptr };
    QStackedWidget* m_pages       { nullptr };
    QWidget*        m_composerHost { nullptr };
    QToolButton*    m_railAssist  { nullptr };
    QToolButton*    m_railHistory { nullptr };
    QListWidget*    m_historyList { nullptr };
    QLabel*         m_historyNote { nullptr };
    QScrollArea* m_scroll   { nullptr };
    QWidget*     m_chatBody { nullptr };
    QVBoxLayout* m_chatLay  { nullptr };
    QWidget*     m_hero     { nullptr };
    AiToast*     m_toast    { nullptr };
    QWidget*     m_composerBox { nullptr };
    QLabel*      m_composerCaption { nullptr };
    QTextEdit*   m_input    { nullptr };
    QPushButton* m_send     { nullptr };
    QPushButton* m_attach   { nullptr };
    QLabel*      m_quota    { nullptr };

    // Header close control, revealed only while the pointer is near the top.
    QWidget*                m_header      { nullptr };
    QPushButton* m_newChat { nullptr };   // header "+": start a fresh conversation
    QPushButton*            m_close       { nullptr };
    QGraphicsOpacityEffect* m_closeFade   { nullptr };
    QPropertyAnimation*     m_closeAnim   { nullptr };

    // "Stasis is working" row, with the cycling dots.
    QWidget* m_workRow  { nullptr };
    QLabel*  m_workText { nullptr };
    QTimer*  m_workTick { nullptr };
    int      m_workDots { 0 };

    // Offers Rollback in the strip above the composer, and turns it into
    // Rollforward once used, so an accidental press is one press to undo.
    void showRollback(bool rolledBack);
    void hideRollback();

    StasisClient*         m_client { nullptr };
    AiChatStore*          m_chats  { nullptr };
    AiDocumentAgent*      m_agent  { nullptr };
    QTextEdit*            m_docTarget { nullptr };
    AiStreamTarget*         m_deckTarget { nullptr };
    AiSourcesStrip*       m_strip     { nullptr };
    bool                  m_rolledBack { false };
    QString               m_chatId;      // minted on the first prompt of a chat
    QVector<AiMessage>    m_history;
    QVector<AiAttachment> m_pending;
    QWidget*              m_attachStrip { nullptr };
    QLabel*               m_streamTarget { nullptr };   // bubble being streamed into
    QWidget*              m_streamBubble { nullptr };   // ... and the bubble itself
    QString               m_streamAccum;                // raw reply, marker included
    bool                  m_streamDecided { false };    // chat or document, settled
    bool                  m_streamToDoc   { false };    // feeding the page live
    bool                  m_streamToDeck  { false };    // ... and it is a deck
};

} // namespace NativeOffice
