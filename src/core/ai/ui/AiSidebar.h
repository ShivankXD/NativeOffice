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

#include <QVector>
#include <QWidget>

#include "ai/AiTypes.h"

class QLabel;
class QPushButton;
class QScrollArea;
class QTextEdit;
class QTimer;
class QVBoxLayout;
class QGraphicsOpacityEffect;
class QPropertyAnimation;

namespace NativeOffice {

class AiChatStore;
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

    // ── conversation ────────────────────────────────────────────────────────
    void submit();
    void appendMessage(const AiMessage& m);
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
    QScrollArea* m_scroll   { nullptr };
    QWidget*     m_chatBody { nullptr };
    QVBoxLayout* m_chatLay  { nullptr };
    QWidget*     m_hero     { nullptr };
    AiToast*     m_toast    { nullptr };
    QWidget*     m_composerBox { nullptr };
    QTextEdit*   m_input    { nullptr };
    QPushButton* m_send     { nullptr };
    QPushButton* m_attach   { nullptr };
    QLabel*      m_quota    { nullptr };

    // Header close control, revealed only while the pointer is near the top.
    QWidget*                m_header      { nullptr };
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
    QString               m_streamAccum;                // raw reply, marker included
    bool                  m_streamDecided { false };    // chat or document, settled
    bool                  m_streamToDoc   { false };    // feeding the page live
    bool                  m_streamToDeck  { false };    // ... and it is a deck
};

} // namespace NativeOffice
