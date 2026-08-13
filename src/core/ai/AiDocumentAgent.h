#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AiDocumentAgent.h — writes Stasis's answer into the open document, visibly.
//
// Writing happens as the reply arrives, straight into the page. The unit is a
// line, not a character: a markdown line is a complete block (a heading is only
// a heading once its line has arrived, and **bold** only closes within one), so
// rendering on each newline is both correct and immediate. Text lands in the
// document in real time, and nothing is staged in the chat first.
//
// A trailing partial line is held until it completes or the stream ends, which
// is what stops a half-written "## Hea" appearing as a paragraph and then
// being rewritten as a heading a moment later.
//
// Newly written text carries a violet tint that settles to the document's own
// colour a moment later, so you can see exactly what was added without a panel
// covering the page.
//
// Rollback is a range, not an undo depth. The agent remembers where it started
// and how much it wrote, so undoing is removing that span and redoing is
// putting it back. Counting undo steps would be wrong the moment the user
// typed anything of their own in between.
// ─────────────────────────────────────────────────────────────────────────────

#include <QObject>
#include <QString>

class QTextEdit;
class QTimer;

namespace NativeOffice {

class AiDocumentAgent : public QObject {
    Q_OBJECT
public:
    explicit AiDocumentAgent(QObject* parent = nullptr);

    // True when this mode has a document the agent can write into.
    static bool canWriteTo(QTextEdit* target) { return target != nullptr; }

    // ── live writing, as the reply streams ──────────────────────────────────
    // beginLive marks the insertion point and starts a new rollback record,
    // replacing any previous one: one pending edit at a time is all the sidebar
    // offers. feed renders every complete line it now has. endLive flushes the
    // last partial line and settles the tint.
    void beginLive(QTextEdit* target);
    void feed(const QString& chunk);
    void endLive();
    bool live() const { return m_live; }

    // Writes a finished string in one pass. Used by rollforward, where the text
    // is already known and there is nothing to wait for.
    void write(QTextEdit* target, const QString& markdown);

    bool busy() const;
    void stop();

    // ── rollback ────────────────────────────────────────────────────────────
    bool canRollback()    const { return m_state == State::Applied; }
    bool canRollforward() const { return m_state == State::RolledBack; }
    void rollback();
    void rollforward();

    int charactersWritten() const { return m_written; }

signals:
    void progress(int charactersSoFar);
    void finished(int charactersWritten);
    void rolledBack();
    void rolledForward();

private:
    void step();
    void settleTint();
    // Renders one markdown line at the cursor, applying heading, list and
    // inline emphasis. Returns the number of characters of visible text added.
    int  renderLine(const QString& line);

    enum class State { Idle, Applied, RolledBack };

    QTextEdit* m_target { nullptr };
    QStringList m_lines;
    int      m_line    { 0 };
    bool     m_live    { false };
    QString  m_pending;      // partial trailing line, waiting for its newline
    int      m_blocks  { 0 };  // blocks inserted, so rollback spans them all
    int      m_start   { 0 };     // document position the edit began at
    int      m_written { 0 };     // visible characters written
    QString  m_markdown;          // kept so rollforward can replay it
    QTimer*  m_tick    { nullptr };
    QTimer*  m_settle  { nullptr };
    State    m_state   { State::Idle };
};

} // namespace NativeOffice
