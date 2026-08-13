#include "AiDocumentAgent.h"

#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextList>
#include <QTimer>

namespace NativeOffice {

namespace {

// The tint newly written text carries before it settles. Violet rather than a
// highlight block: it marks the words themselves, so the page is never covered.
const QColor kGhostInk(0x8B, 0x74, 0xFF);

// Lines are written a few per tick. Fast enough not to be a wait, slow enough
// that the document is visibly being built rather than simply appearing.
constexpr int kTickMs = 45;
constexpr int kLinesPerTick = 1;

// Strips inline markdown from one line and returns the runs that make it up.
// Only the three that actually turn up in prose are handled: bold, italic and
// inline code. Anything else is left as written, which is the right failure:
// showing a stray asterisk beats swallowing the text around it.
struct Run { QString text; bool bold {false}; bool italic {false}; bool code {false}; };

QVector<Run> inlineRuns(const QString& src) {
    QVector<Run> out;
    Run cur;
    bool bold = false, italic = false, code = false;
    for (int i = 0; i < src.size(); ) {
        const QChar c = src.at(i);
        const bool dbl = (i + 1 < src.size()) && src.at(i + 1) == c;
        // Inside a code span nothing is a marker. Without this, `calc_margin()`
        // loses its underscore to an italic toggle and the identifier silently
        // changes, which in someone's document is data loss rather than a
        // formatting slip.
        if (!code && c == QLatin1Char('*') && dbl) {
            if (!cur.text.isEmpty()) { out.append(cur); cur = Run{}; }
            bold = !bold; cur.bold = bold; cur.italic = italic; cur.code = code;
            i += 2; continue;
        }
        if (!code && (c == QLatin1Char('*') || c == QLatin1Char('_'))) {
            // An underscore only opens or closes emphasis at a word boundary,
            // so snake_case survives intact. Asterisks are unambiguous and are
            // always treated as markers.
            bool marker = true;
            if (c == QLatin1Char('_')) {
                const QChar prev = i > 0 ? src.at(i - 1) : QChar(' ');
                const QChar next = (i + 1 < src.size()) ? src.at(i + 1) : QChar(' ');
                marker = !(prev.isLetterOrNumber() && next.isLetterOrNumber());
            }
            if (marker) {
                if (!cur.text.isEmpty()) { out.append(cur); cur = Run{}; }
                italic = !italic; cur.bold = bold; cur.italic = italic; cur.code = code;
                i += 1; continue;
            }
        }
        if (c == QLatin1Char('`')) {
            if (!cur.text.isEmpty()) { out.append(cur); cur = Run{}; }
            code = !code; cur.bold = bold; cur.italic = italic; cur.code = code;
            i += 1; continue;
        }
        cur.text += c;
        cur.bold = bold; cur.italic = italic; cur.code = code;
        ++i;
    }
    if (!cur.text.isEmpty()) out.append(cur);
    return out;
}

} // namespace

AiDocumentAgent::AiDocumentAgent(QObject* parent)
    : QObject(parent)
{
    m_tick = new QTimer(this);
    m_tick->setInterval(kTickMs);
    connect(m_tick, &QTimer::timeout, this, &AiDocumentAgent::step);

    m_settle = new QTimer(this);
    m_settle->setSingleShot(true);
    m_settle->setInterval(500);
    connect(m_settle, &QTimer::timeout, this, &AiDocumentAgent::settleTint);
}

bool AiDocumentAgent::busy() const { return m_tick->isActive(); }

void AiDocumentAgent::stop() { m_tick->stop(); }

void AiDocumentAgent::beginLive(QTextEdit* target) {
    if (!target) return;
    m_tick->stop();
    m_settle->stop();
    m_target   = target;
    m_markdown.clear();
    m_pending.clear();
    m_lines.clear();
    m_line     = 0;
    m_written  = 0;
    m_blocks   = 0;
    m_live     = true;

    QTextCursor c = target->textCursor();
    c.movePosition(QTextCursor::End);
    // A blank line between what was already there and what is about to be
    // written, so the addition never runs into the user's own last sentence.
    if (!c.atStart()) c.insertBlock();
    m_start = c.position();
    target->setTextCursor(c);
    m_state = State::Applied;
}

void AiDocumentAgent::feed(const QString& chunk) {
    if (!m_live || !m_target) return;
    m_markdown += chunk;
    m_pending  += chunk;

    // Only whole lines are rendered. Holding the tail back is what stops a
    // half-arrived "## Hea" being drawn as a paragraph and then rewritten as a
    // heading once the rest of the line lands.
    int nl;
    while ((nl = m_pending.indexOf(QLatin1Char('\n'))) >= 0) {
        QString line = m_pending.left(nl);
        m_pending.remove(0, nl + 1);
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        m_written += renderLine(line);
        ++m_blocks;
    }
    m_target->ensureCursorVisible();
    emit progress(m_written);
}

void AiDocumentAgent::endLive() {
    if (!m_live) return;
    if (!m_pending.trimmed().isEmpty()) {
        m_written += renderLine(m_pending);
        ++m_blocks;
    }
    m_pending.clear();
    m_live = false;
    m_target->ensureCursorVisible();
    m_settle->start();
    emit finished(m_written);
}

void AiDocumentAgent::write(QTextEdit* target, const QString& markdown) {
    if (!target || markdown.isEmpty()) return;
    m_tick->stop();
    m_target   = target;
    m_markdown = markdown;
    m_lines    = markdown.split(QLatin1Char('\n'));
    m_line     = 0;
    m_written  = 0;
    m_blocks   = 0;
    m_live     = false;

    QTextCursor c = target->textCursor();
    c.movePosition(QTextCursor::End);
    if (!c.atStart()) c.insertBlock();
    m_start = c.position();
    target->setTextCursor(c);

    m_state = State::Applied;
    m_tick->start();
}

int AiDocumentAgent::renderLine(const QString& raw) {
    QTextCursor c = m_target->textCursor();
    QString line = raw;

    QTextBlockFormat bf;
    QTextCharFormat base;
    base.setForeground(kGhostInk);      // the tint, settled once the edit ends

    // ── block level ─────────────────────────────────────────────────────────
    int heading = 0;
    while (heading < 6 && heading < line.size() && line.at(heading) == QLatin1Char('#'))
        ++heading;
    const bool isHeading = heading > 0 && heading < line.size()
                           && line.at(heading) == QLatin1Char(' ');
    bool bullet = false, numbered = false;
    if (!isHeading) {
        const QString t = line.trimmed();
        if (t.startsWith(QStringLiteral("- ")) || t.startsWith(QStringLiteral("* "))) {
            bullet = true;
            line = t.mid(2);
        } else {
            // "1. " through "999. "
            int d = 0;
            while (d < t.size() && t.at(d).isDigit()) ++d;
            if (d > 0 && d + 1 < t.size() && t.at(d) == QLatin1Char('.')
                && t.at(d + 1) == QLatin1Char(' ')) {
                numbered = true;
                line = t.mid(d + 2);
            }
        }
    } else {
        line = line.mid(heading + 1);
    }

    if (isHeading) {
        // Sized off the document's own default so a heading stays proportional
        // to whatever body size the user is working at.
        const qreal bodyPt = m_target->document()->defaultFont().pointSizeF() > 0
                               ? m_target->document()->defaultFont().pointSizeF() : 11.0;
        static const qreal scale[6] = { 1.9, 1.55, 1.3, 1.15, 1.05, 1.0 };
        base.setFontPointSize(bodyPt * scale[qBound(0, heading - 1, 5)]);
        base.setFontWeight(QFont::DemiBold);
        bf.setHeadingLevel(heading);
        bf.setTopMargin(10);
        bf.setBottomMargin(4);
    }

    c.insertBlock(bf);
    if (bullet || numbered) {
        QTextListFormat lf;
        lf.setStyle(bullet ? QTextListFormat::ListDisc : QTextListFormat::ListDecimal);
        lf.setIndent(1);
        // Continue the list above when there is one, so consecutive items are
        // one list and numbering runs on instead of restarting at 1 each line.
        QTextList* existing = c.currentList();
        if (existing && existing->format().style() == lf.style()) existing->add(c.block());
        else c.createList(lf);
    }

    int added = 0;
    const QVector<Run> runs = inlineRuns(line);
    for (const Run& r : runs) {
        if (r.text.isEmpty()) continue;
        QTextCharFormat f = base;
        if (r.bold)   f.setFontWeight(QFont::Bold);
        if (r.italic) f.setFontItalic(true);
        if (r.code)   f.setFontFamilies({QStringLiteral("Consolas"),
                                         QStringLiteral("Courier New")});
        c.insertText(r.text, f);
        added += r.text.size();
    }
    m_target->setTextCursor(c);
    return added;
}

void AiDocumentAgent::step() {
    if (!m_target || m_line >= m_lines.size()) {
        m_tick->stop();
        m_settle->start();
        emit finished(m_written);
        return;
    }
    for (int n = 0; n < kLinesPerTick && m_line < m_lines.size(); ++n, ++m_line) {
        m_written += renderLine(m_lines.at(m_line));
        ++m_blocks;
    }

    // Keeps the newly written line on screen, which is what makes the writing
    // readable while it happens instead of scrolling away below the fold.
    m_target->ensureCursorVisible();
    emit progress(m_written);
}

void AiDocumentAgent::settleTint() {
    if (!m_target || m_state != State::Applied) return;
    // The tint has done its job once the edit is finished. Clearing the
    // foreground rather than setting black lets the text inherit whatever the
    // document's own colour is, including in a dark theme.
    QTextCursor c(m_target->document());
    c.setPosition(m_start);
    c.setPosition(qMin(m_start + m_written + m_blocks,
                       m_target->document()->characterCount() - 1),
                  QTextCursor::KeepAnchor);
    QTextCharFormat clear;
    clear.setForeground(m_target->palette().text());
    c.mergeCharFormat(clear);
}

void AiDocumentAgent::rollback() {
    if (!m_target || m_state != State::Applied) return;
    m_tick->stop();
    m_settle->stop();
    QTextCursor c(m_target->document());
    c.setPosition(qMax(0, m_start - 1));      // also takes the separating block
    c.setPosition(qMin(m_target->document()->characterCount() - 1,
                       m_start + m_written + m_blocks),
                  QTextCursor::KeepAnchor);
    c.removeSelectedText();
    m_state = State::RolledBack;
    emit rolledBack();
}

void AiDocumentAgent::rollforward() {
    if (!m_target || m_state != State::RolledBack) return;
    const QString md = m_markdown;
    write(m_target, md);          // replays the same text at the same place
    emit rolledForward();
}

} // namespace NativeOffice
