#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AiDocumentAgent.h — writes Stasis's answer into the open document, visibly.
//
// The reply is buffered while it streams and only then written out, which is
// the opposite of what "live" suggests but produces a better result for both
// reasons that matter. Markdown cannot be parsed incrementally without
// guessing (a line starting "#" is only a heading once you have seen the rest
// of the line, and "**" is only bold once it closes), and playing the finished
// text back at a readable speed is what makes the writing legible as it
// happens rather than a flicker of reflowing paragraphs.
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

    // Writes markdown into the editor, animated. Replaces any previous edit's
    // rollback record: one pending edit at a time is all the sidebar offers.
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
    int      m_start   { 0 };     // document position the edit began at
    int      m_written { 0 };     // visible characters written
    QString  m_markdown;          // kept so rollforward can replay it
    QTimer*  m_tick    { nullptr };
    QTimer*  m_settle  { nullptr };
    State    m_state   { State::Idle };
};

} // namespace NativeOffice
