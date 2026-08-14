#include "AiDocumentAgent.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextLength>
#include <QTextList>
#include <QTextTable>
#include <QTextTableCell>
#include <QTextTableFormat>
#include <QTimer>

namespace NativeOffice {

namespace {


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
    m_jsonCarry.clear();
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
    m_endPos = m_start;
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
    m_endPos = m_start;
    target->setTextCursor(c);

    m_state = State::Applied;
    m_tick->start();
}

// ─────────────────────────────────────────────────────────────────────────────
// Operations
//
// A structured op is executed against QTextDocument's own API, which is the
// only way to produce a real table, a tinted callout or a title block; markdown
// has no syntax for any of them. Each op is one line of NDJSON, so the live
// line-by-line writing is unchanged: one complete, renderable thing per line.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// The accent used for rules, headings and table headers. Kept to one colour so
// a generated document reads as designed rather than decorated.
const QColor kAccent(0x5B, 0x4B, 0xD6);
const QColor kRule  (0xD8, 0xDC, 0xE6);

QColor toneFill(const QString& tone) {
    if (tone == QLatin1String("warn"))    return QColor(0xFF, 0xF6, 0xE5);
    if (tone == QLatin1String("success")) return QColor(0xEC, 0xFA, 0xF0);
    if (tone == QLatin1String("danger"))  return QColor(0xFD, 0xEE, 0xEC);
    return QColor(0xEF, 0xEF, 0xFB);                       // info
}
QColor toneEdge(const QString& tone) {
    if (tone == QLatin1String("warn"))    return QColor(0xE8, 0xA3, 0x3D);
    if (tone == QLatin1String("success")) return QColor(0x2E, 0xA8, 0x5C);
    if (tone == QLatin1String("danger"))  return QColor(0xD9, 0x4A, 0x3D);
    return kAccent;
}

Qt::Alignment alignFrom(const QString& s) {
    if (s == QLatin1String("center"))  return Qt::AlignHCenter;
    if (s == QLatin1String("right"))   return Qt::AlignRight;
    if (s == QLatin1String("justify")) return Qt::AlignJustify;
    return Qt::AlignLeft;
}

} // namespace

int AiDocumentAgent::insertRuns(QTextCursor& c, const QString& text,
                                const QTextCharFormat& base) {
    int added = 0;
    const QVector<Run> runs = inlineRuns(text);
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
    return added;
}

qreal AiDocumentAgent::bodyPt() const {
    const qreal pt = m_target->document()->defaultFont().pointSizeF();
    return pt > 0 ? pt : 11.0;
}

int AiDocumentAgent::opTitle(const QJsonObject& o) {
    QTextCursor c = m_target->textCursor();
    int added = 0;

    QTextBlockFormat bf;
    bf.setAlignment(Qt::AlignHCenter);
    bf.setTopMargin(28);
    bf.setBottomMargin(2);
    // Carries heading semantics, not just a large font. Without this the title
    // is invisible to the navigation pane and exports to .docx as body text,
    // which is the difference between a document and something that merely
    // looks like one.
    bf.setHeadingLevel(1);
    c.insertBlock(bf);

    QTextCharFormat tf;
    tf.setFontPointSize(bodyPt() * 2.4);
    tf.setFontWeight(QFont::Bold);
    tf.setForeground(QColor(0x14, 0x16, 0x1C));
    added += insertRuns(c, o.value(QStringLiteral("text")).toString(), tf);

    const QString sub = o.value(QStringLiteral("subtitle")).toString();
    if (!sub.isEmpty()) {
        QTextBlockFormat sbf;
        sbf.setAlignment(Qt::AlignHCenter);
        sbf.setTopMargin(4);
        sbf.setBottomMargin(10);
        c.insertBlock(sbf);
        QTextCharFormat sf;
        sf.setFontPointSize(bodyPt() * 1.15);
        sf.setForeground(QColor(0x6B, 0x72, 0x84));
        sf.setFontItalic(true);
        added += insertRuns(c, sub, sf);
    }
    m_target->setTextCursor(c);
    m_blocks += sub.isEmpty() ? 1 : 2;
    added += opDivider();
    return added;
}

int AiDocumentAgent::opHeading(const QJsonObject& o) {
    const int level = qBound(1, o.value(QStringLiteral("level")).toInt(1), 6);
    QTextCursor c = m_target->textCursor();

    QTextBlockFormat bf;
    bf.setHeadingLevel(level);
    bf.setTopMargin(level <= 2 ? 16 : 11);
    bf.setBottomMargin(4);
    c.insertBlock(bf);

    static const qreal scale[6] = { 1.75, 1.42, 1.22, 1.10, 1.03, 1.0 };
    QTextCharFormat f;
    f.setFontPointSize(bodyPt() * scale[level - 1]);
    f.setFontWeight(QFont::Bold);
    // Top two levels carry the accent; deeper ones stay ink so a page of
    // subheadings does not turn into a colour chart.
    f.setForeground(level <= 2 ? kAccent : QColor(0x1C, 0x1E, 0x26));
    const int added = insertRuns(c, o.value(QStringLiteral("text")).toString(), f);
    m_target->setTextCursor(c);
    ++m_blocks;
    return added;
}

int AiDocumentAgent::opParagraph(const QJsonObject& o) {
    QTextCursor c = m_target->textCursor();
    QTextBlockFormat bf;
    bf.setAlignment(alignFrom(o.value(QStringLiteral("align")).toString()));
    bf.setTopMargin(2);
    bf.setBottomMargin(6);
    bf.setLineHeight(132, QTextBlockFormat::ProportionalHeight);
    c.insertBlock(bf);

    QTextCharFormat f;
    f.setFontPointSize(bodyPt());
    const int added = insertRuns(c, o.value(QStringLiteral("text")).toString(), f);
    m_target->setTextCursor(c);
    ++m_blocks;
    return added;
}

int AiDocumentAgent::opList(const QJsonObject& o, bool numbered) {
    const QJsonArray items = o.value(QStringLiteral("items")).toArray();
    QTextCursor c = m_target->textCursor();
    int added = 0;
    QTextList* list = nullptr;

    // The list marker is drawn in the block's own character format, which a new
    // block inherits from the one before it. After a heading that made "1."
    // render at heading size next to body-sized text, so the format is set
    // explicitly rather than left to whatever preceded the list.
    QTextCharFormat markerFmt;
    markerFmt.setFontPointSize(bodyPt());
    markerFmt.setFontWeight(QFont::Normal);
    markerFmt.setForeground(QColor(0x1C, 0x1E, 0x26));

    for (const QJsonValue& v : items) {
        QTextBlockFormat bf;
        bf.setTopMargin(1);
        bf.setBottomMargin(1);
        c.insertBlock(bf, markerFmt);
        if (!list) {
            QTextListFormat lf;
            lf.setStyle(numbered ? QTextListFormat::ListDecimal
                                 : QTextListFormat::ListDisc);
            lf.setIndent(1);
            list = c.createList(lf);
        } else {
            list->add(c.block());
        }
        QTextCharFormat f;
        f.setFontPointSize(bodyPt());
            added += insertRuns(c, v.toString(), f);
        ++m_blocks;
    }
    m_target->setTextCursor(c);
    return added;
}

int AiDocumentAgent::opQuote(const QJsonObject& o) {
    QTextCursor c = m_target->textCursor();
    QTextBlockFormat bf;
    bf.setLeftMargin(26);
    bf.setRightMargin(18);
    bf.setTopMargin(10);
    bf.setBottomMargin(10);
    // The accent bar is the block's left border, so it tracks the text height
    // instead of being a drawn rectangle that stops matching after a reflow.
    bf.setProperty(QTextFormat::BlockLeftMargin, 26);
    c.insertBlock(bf);

    QTextCharFormat f;
    f.setFontPointSize(bodyPt() * 1.08);
    f.setFontItalic(true);
    f.setForeground(QColor(0x3A, 0x40, 0x52));
    int added = insertRuns(c, o.value(QStringLiteral("text")).toString(), f);

    const QString by = o.value(QStringLiteral("by")).toString();
    if (!by.isEmpty()) {
        QTextCharFormat af;
        af.setFontPointSize(bodyPt() * 0.92);
        af.setForeground(QColor(0x7A, 0x82, 0x94));
        c.insertText(QStringLiteral("  — ") + by, af);
        added += by.size() + 4;
    }
    m_target->setTextCursor(c);
    ++m_blocks;
    return added;
}

int AiDocumentAgent::opCallout(const QJsonObject& o) {
    const QString tone = o.value(QStringLiteral("tone")).toString();
    QTextCursor c = m_target->textCursor();

    // A one-cell table is the only way to get a filled, padded box in
    // QTextDocument that reflows with its text. A block background paints only
    // behind the glyphs and leaves no padding, which looks like a highlighter.
    QTextTableFormat tf;
    tf.setCellPadding(11);
    tf.setCellSpacing(0);
    tf.setBorder(0);
    tf.setBorderStyle(QTextFrameFormat::BorderStyle_None);
    tf.setTopMargin(10);
    tf.setBottomMargin(10);
    tf.setWidth(QTextLength(QTextLength::PercentageLength, 100));

    QTextTable* t = c.insertTable(1, 1, tf);
    // The fill goes on the cell, not the table. A background set on the table
    // format does not paint through the document layout, which is why the box
    // came out white while the data table's header cells were fine.
    QTextTableCell box = t->cellAt(0, 0);
    QTextCharFormat boxFmt = box.format();
    boxFmt.setBackground(toneFill(tone));
    box.setFormat(boxFmt);
    QTextCursor cc = box.firstCursorPosition();

    QTextCharFormat f;
    f.setFontPointSize(bodyPt() * 0.98);
    f.setForeground(QColor(0x2A, 0x2F, 0x3C));
    const QString title = o.value(QStringLiteral("title")).toString();
    int added = 0;
    if (!title.isEmpty()) {
        QTextCharFormat hf = f;
        hf.setFontWeight(QFont::Bold);
        hf.setForeground(toneEdge(tone));
        cc.insertText(title, hf);
        cc.insertBlock();
        added += title.size();
    }
    added += insertRuns(cc, o.value(QStringLiteral("text")).toString(), f);

    c.movePosition(QTextCursor::End);
    m_target->setTextCursor(c);
    ++m_blocks;
    return added;
}

int AiDocumentAgent::opTable(const QJsonObject& o) {
    const QJsonArray header = o.value(QStringLiteral("header")).toArray();
    const QJsonArray rows   = o.value(QStringLiteral("rows")).toArray();
    if (header.isEmpty() && rows.isEmpty()) return 0;

    const int cols = header.isEmpty()
                       ? rows.at(0).toArray().size()
                       : header.size();
    if (cols <= 0) return 0;
    const int bodyRows = rows.size();
    const int total = bodyRows + (header.isEmpty() ? 0 : 1);

    QTextCursor c = m_target->textCursor();
    QTextTableFormat tf;
    tf.setCellPadding(7);
    tf.setCellSpacing(0);
    tf.setBorder(1);
    tf.setBorderBrush(kRule);
    tf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    tf.setTopMargin(12);
    tf.setBottomMargin(12);
    tf.setWidth(QTextLength(QTextLength::PercentageLength, 100));
    QTextTable* t = c.insertTable(total, cols, tf);

    int added = 0;
    int r = 0;
    if (!header.isEmpty()) {
        for (int i = 0; i < cols; ++i) {
            QTextTableCell cell = t->cellAt(0, i);
            QTextCharFormat cf = cell.format();
            cf.setBackground(kAccent);
            cell.setFormat(cf);
            QTextCursor cc = cell.firstCursorPosition();
            QTextCharFormat f;
            f.setFontWeight(QFont::Bold);
            f.setFontPointSize(bodyPt() * 0.95);
            f.setForeground(QColor(Qt::white));   // on the accent fill
            const QString txt = header.at(i).toString();
            cc.insertText(txt, f);
            added += txt.size();
        }
        r = 1;
    }

    for (int i = 0; i < bodyRows; ++i, ++r) {
        const QJsonArray cells = rows.at(i).toArray();
        for (int j = 0; j < cols; ++j) {
            QTextTableCell cell = t->cellAt(r, j);
            // Banding, so a long table stays readable across a row.
            if (i % 2 == 1) {
                QTextCharFormat cf = cell.format();
                cf.setBackground(QColor(0xF7, 0xF8, 0xFB));
                cell.setFormat(cf);
            }
            QTextCursor cc = cell.firstCursorPosition();
            QTextCharFormat f;
            f.setFontPointSize(bodyPt() * 0.95);
                    added += insertRuns(cc, cells.at(j).toString(), f);
        }
    }

    c.movePosition(QTextCursor::End);
    m_target->setTextCursor(c);
    m_blocks += total;
    return added;
}

int AiDocumentAgent::opDivider() {
    QTextCursor c = m_target->textCursor();
    QTextBlockFormat bf;
    bf.setTopMargin(8);
    bf.setBottomMargin(14);
    bf.setAlignment(Qt::AlignHCenter);
    c.insertBlock(bf);
    // A short accent rule rather than a full-width line: it reads as a mark
    // between sections instead of a page break.
    QTextCharFormat f;
    f.setForeground(kAccent);
    f.setFontPointSize(bodyPt() * 0.9);
    c.insertText(QStringLiteral("———"), f);
    m_target->setTextCursor(c);
    ++m_blocks;
    return 3;
}

int AiDocumentAgent::opPageBreak() {
    QTextCursor c = m_target->textCursor();
    QTextBlockFormat bf;
    bf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    c.insertBlock(bf);
    m_target->setTextCursor(c);
    ++m_blocks;
    return 0;
}

int AiDocumentAgent::executeOp(const QJsonObject& o) {
    const QString op = o.value(QStringLiteral("op")).toString();
    if (op == QLatin1String("title"))     return opTitle(o);
    if (op == QLatin1String("heading"))   return opHeading(o);
    if (op == QLatin1String("para")
     || op == QLatin1String("paragraph")) return opParagraph(o);
    if (op == QLatin1String("bullets"))   return opList(o, false);
    if (op == QLatin1String("numbers"))   return opList(o, true);
    if (op == QLatin1String("quote"))     return opQuote(o);
    if (op == QLatin1String("callout"))   return opCallout(o);
    if (op == QLatin1String("table"))     return opTable(o);
    if (op == QLatin1String("divider"))   return opDivider();
    if (op == QLatin1String("pageBreak")) return opPageBreak();
    // An op we do not know is skipped rather than guessed at. Writing an
    // unrecognised instruction into the page as text would be worse than
    // quietly leaving it out.
    return 0;
}

namespace {
// Counts braces outside of strings, so a "{" inside a value does not make an
// object look unfinished.
int braceBalance(const QString& s) {
    int depth = 0;
    bool inStr = false, esc = false;
    for (const QChar ch : s) {
        if (esc)                       { esc = false; continue; }
        if (inStr) {
            if (ch == QLatin1Char('\\'))     esc = true;
            else if (ch == QLatin1Char('"')) inStr = false;
            continue;
        }
        if (ch == QLatin1Char('"'))      inStr = true;
        else if (ch == QLatin1Char('{')) ++depth;
        else if (ch == QLatin1Char('}')) --depth;
    }
    return depth;
}
} // namespace

int AiDocumentAgent::renderLine(const QString& raw) {
    QString t = raw.trimmed();

    // An object the model pretty-printed across several lines is still one
    // operation. Hold the opening line until its braces balance rather than
    // rendering each fragment as prose.
    if (!m_jsonCarry.isEmpty()) {
        m_jsonCarry += QLatin1Char('\n') + raw;
        if (braceBalance(m_jsonCarry) > 0) return 0;      // still incomplete
        t = m_jsonCarry.trimmed();
        m_jsonCarry.clear();
    } else if (t.startsWith(QLatin1Char('{')) && braceBalance(t) > 0) {
        m_jsonCarry = raw;
        return 0;
    }

    // Structured first. Only a line that is a complete JSON object carrying an
    // "op" is treated as one, so prose beginning with a brace is not mistaken
    // for an instruction.
    const bool looksJson = t.startsWith(QLatin1Char('{'));
    if (looksJson && t.endsWith(QLatin1Char('}'))) {
        QJsonParseError err{};
        const QJsonDocument d = QJsonDocument::fromJson(t.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && d.isObject()) {
            const QJsonObject o = d.object();
            if (o.contains(QStringLiteral("op"))) {
                const int n = executeOp(o);
                m_endPos = m_target->textCursor().position();
                return n;
            }
        }
    }
    // Malformed JSON is dropped, not printed. The markdown fallback exists for
    // a model that answered in prose; using it here would stamp a broken
    // operation into the page as literal text, which is worse than a gap.
    if (looksJson) return 0;

    const int n = renderMarkdownLine(raw);
    m_endPos = m_target->textCursor().position();
    return n;
}

int AiDocumentAgent::renderMarkdownLine(const QString& raw) {
    QTextCursor c = m_target->textCursor();
    QString line = raw;

    QTextBlockFormat bf;
    QTextCharFormat base;
    
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

    // Same reason as opList: the marker takes the block's character format, and
    // a block inherits it from whatever came before.
    QTextCharFormat blockFmt;
    blockFmt.setFontPointSize(isHeading ? base.fontPointSize() : bodyPt());
    if (isHeading) blockFmt.setFontWeight(QFont::Bold);
    c.insertBlock(bf, blockFmt);
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
    // Deliberately does nothing now. It used to clear a violet tint off newly
    // written text, which only worked while every generated character was the
    // same colour. Generated documents now carry real accents, header fills and
    // tone colours, and a blanket foreground merge would wipe exactly those.
    return;
    if (!m_target || m_state != State::Applied) return;
    // The tint has done its job once the edit is finished. Clearing the
    // foreground rather than setting black lets the text inherit whatever the
    // document's own colour is, including in a dark theme.
    QTextCursor c(m_target->document());
    c.setPosition(m_start);
    c.setPosition(qMin(m_endPos, m_target->document()->characterCount() - 1),
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
    c.setPosition(qMin(m_endPos, m_target->document()->characterCount() - 1),
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
