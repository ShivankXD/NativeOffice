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
// Content arrives as operations rather than prose: one JSON object per line,
// executed against QTextDocument's own API. That is what makes a real table, a
// tinted callout, a title block or a page break possible at all, none of which
// markdown can express. A line that is not an operation is still rendered as
// markdown, so a model that forgets the format produces a plain document rather
// than a page of visible JSON.
//
// Rollback is a range, not an undo depth: the agent remembers where it started
// and where it ended, so undoing is removing that span. Counting undo steps
// would be wrong the moment the user typed anything of their own in between,
// and counting characters would be wrong the moment a table was involved.
// ─────────────────────────────────────────────────────────────────────────────

#include <QJsonObject>
#include <QObject>
#include <QString>

class QTextCharFormat;
class QTextCursor;
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

    // One streamed line. Structured operations are the real path: a line that
    // parses as a JSON object with an "op" is executed against the document's
    // own API, which is how tables, callouts and title blocks become possible
    // at all. Markdown cannot express any of them.
    //
    // A line that is not an operation is still rendered as markdown, so a model
    // that forgets the format produces a plain document rather than a page of
    // visible JSON. That fallback is the difference between a bad answer and a
    // broken one.
    int  renderLine(const QString& line);
    int  executeOp(const QJsonObject& op);
    int  renderMarkdownLine(const QString& line);

    // ── operations ──────────────────────────────────────────────────────────
    int  opTitle(const QJsonObject& o);
    int  opHeading(const QJsonObject& o);
    int  opParagraph(const QJsonObject& o);
    int  opList(const QJsonObject& o, bool numbered);
    int  opQuote(const QJsonObject& o);
    int  opCallout(const QJsonObject& o);
    int  opTable(const QJsonObject& o);
    int  opDivider();
    int  opPageBreak();

    // Writes inline-markdown text at the cursor with a base format applied.
    int  insertRuns(QTextCursor& c, const QString& text, const QTextCharFormat& base);
    // The document's own body size, so generated headings stay proportional to
    // whatever the user is actually writing at.
    qreal bodyPt() const;

    enum class State { Idle, Applied, RolledBack };

    QTextEdit* m_target { nullptr };
    QStringList m_lines;
    int      m_line    { 0 };
    bool     m_live    { false };
    QString  m_pending;      // partial trailing line, waiting for its newline
    // An operation the model pretty-printed across several lines, held until
    // its braces balance so it is executed once rather than rendered in pieces.
    QString  m_jsonCarry;
    int      m_blocks  { 0 };  // blocks inserted, so rollback spans them all
    // Where the written region currently ends. Rollback works off positions
    // rather than a character tally, because a table or an image adds document
    // length that no count of visible characters can predict.
    int      m_endPos  { 0 };
    int      m_start   { 0 };     // document position the edit began at
    int      m_written { 0 };     // visible characters written
    QString  m_markdown;          // kept so rollforward can replay it
    QTimer*  m_tick    { nullptr };
    QTimer*  m_settle  { nullptr };
    State    m_state   { State::Idle };
};

} // namespace NativeOffice
