// ─────────────────────────────────────────────────────────────────────────────
// MarkdownEditor.cpp — see MarkdownEditor.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "MarkdownEditor.h"

#include "theme/ThemeManager.h"

#include <QByteArray>
#include <QFrame>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QSyntaxHighlighter>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <set>

// md4c — vendored CommonMark parser (third_party/md4c). Header self-guards with
// extern "C", so it is safe to include directly from this C++ translation unit.
#include "md4c-html.h"

namespace NativeOffice {

namespace {

// Escape a plain string for safe HTML embedding.
QString htmlEscape(const QString& in) {
    QString out;
    out.reserve(in.size());
    for (const QChar c : in) {
        switch (c.unicode()) {
        case '&': out += QStringLiteral("&amp;");  break;
        case '<': out += QStringLiteral("&lt;");   break;
        case '>': out += QStringLiteral("&gt;");   break;
        case '"': out += QStringLiteral("&quot;"); break;
        default:  out += c;                        break;
        }
    }
    return out;
}

// Reverse md4c's HTML entity escaping so we can re-tokenize a code block's text.
QString htmlUnescape(QString s) {
    s.replace(QStringLiteral("&lt;"),   QStringLiteral("<"));
    s.replace(QStringLiteral("&gt;"),   QStringLiteral(">"));
    s.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    s.replace(QStringLiteral("&#39;"),  QStringLiteral("'"));
    s.replace(QStringLiteral("&amp;"),  QStringLiteral("&"));  // last: avoid double-decode
    return s;
}

// A broad union of keywords across the common languages we colorize. We don't
// try to be language-perfect — just readable color-coding, per the spec.
const std::set<QString>& keywordSet() {
    static const std::set<QString> kw = {
        "if","else","elif","for","while","do","return","break","continue","switch",
        "case","default","function","def","class","struct","enum","interface","public",
        "private","protected","static","const","let","var","void","int","float","double",
        "char","bool","boolean","string","new","delete","this","self","import","from",
        "export","require","include","namespace","using","template","typename","try",
        "catch","finally","throw","throws","async","await","yield","lambda","in","is",
        "and","or","not","true","false","null","none","None","True","False","nullptr",
        "nil","undefined","print","package","func","fn","type","val","match","when","then",
        "begin","module","extends","implements","super","override","virtual","final",
        "abstract","sizeof","typedef","union","unsigned","signed","long","short","auto",
        "volatile","extern","inline","goto","with","as","pass","raise","global","del",
        "assert","mut","impl","trait","where","use","pub","crate","dyn","of","end","echo",
    };
    return kw;
}

// Token colors for the preview code blocks (GitHub-ish, light + dark variants).
struct CodePalette { QString keyword, str, comment, number, base; };
CodePalette codePalette(bool dark) {
    return dark
        ? CodePalette{ "#ff7b72", "#a5d6ff", "#8b949e", "#79c0ff", "#e6edf3" }
        : CodePalette{ "#cf222e", "#0a3069", "#6e7781", "#0550ae", "#1f2328" };
}

// Rule-based tokenizer: turn a raw (unescaped) code string into HTML with
// colored <span>s. One combined regex, ordered so comments/strings win over
// keywords, then bare words are looked up in the keyword set.
QString colorizeCode(const QString& code, bool dark) {
    const CodePalette pal = codePalette(dark);
    static const QRegularExpression re(
        R"((//[^\n]*|#[^\n]*|/\*[^]*?\*/)|("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|`(?:\\.|[^`\\])*`)|(\b\d[\d._]*(?:\.\d+)?\b)|([A-Za-z_]\w*))");

    QString out;
    int last = 0;
    auto it = re.globalMatch(code);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += htmlEscape(code.mid(last, m.capturedStart() - last));
        const QString tok = m.captured();
        QString color;
        if (m.capturedStart(1) >= 0)                    color = pal.comment;
        else if (m.capturedStart(2) >= 0)               color = pal.str;
        else if (m.capturedStart(3) >= 0)               color = pal.number;
        else if (keywordSet().count(tok))               color = pal.keyword;
        if (color.isEmpty())
            out += htmlEscape(tok);
        else
            out += QStringLiteral("<span style=\"color:%1;\">%2</span>")
                       .arg(color, htmlEscape(tok));
        last = m.capturedEnd();
    }
    out += htmlEscape(code.mid(last));
    return out;
}

// Post-process md4c's HTML: find <pre><code …>…</code></pre> blocks and replace
// their (entity-escaped) contents with colorized spans.
QString colorizeCodeBlocks(const QString& html, bool dark) {
    static const QRegularExpression block(
        R"(<pre><code(?:\s+class="language-[^"]*")?>([\s\S]*?)</code></pre>)");
    QString out;
    int last = 0;
    auto it = block.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += html.mid(last, m.capturedStart() - last);
        const QString colored = colorizeCode(htmlUnescape(m.captured(1)), dark);
        out += QStringLiteral("<pre><code>") + colored + QStringLiteral("</code></pre>");
        last = m.capturedEnd();
    }
    out += html.mid(last);
    return out;
}

// GitHub-style preview stylesheet (Qt rich-text CSS subset).
QString previewCss(bool dark) {
    if (dark) {
        return R"(
            body { background:#0d1117; color:#e6edf3; font-family:'Segoe UI',Arial,sans-serif;
                   font-size:14px; }
            a { color:#4493f8; }
            h1,h2,h3,h4 { color:#e6edf3; font-weight:600; }
            h1 { font-size:26px; } h2 { font-size:21px; } h3 { font-size:17px; }
            code { background:#161b22; color:#e6edf3; font-family:Consolas,'Courier New',monospace; }
            pre { background:#161b22; color:#e6edf3; padding:10px;
                  font-family:Consolas,'Courier New',monospace; }
            blockquote { color:#8b949e; border-left:3px solid #30363d; padding-left:12px; }
            table { border:1px solid #30363d; }
            th,td { border:1px solid #30363d; padding:4px 9px; }
            th { background:#161b22; }
            hr { color:#30363d; }
        )";
    }
    return R"(
        body { background:#ffffff; color:#1f2328; font-family:'Segoe UI',Arial,sans-serif;
               font-size:14px; }
        a { color:#0969da; }
        h1,h2,h3,h4 { color:#1f2328; font-weight:600; }
        h1 { font-size:26px; } h2 { font-size:21px; } h3 { font-size:17px; }
        code { background:#f0f2f5; color:#1f2328; font-family:Consolas,'Courier New',monospace; }
        pre { background:#f6f8fa; color:#1f2328; padding:10px;
              font-family:Consolas,'Courier New',monospace; }
        blockquote { color:#59636e; border-left:3px solid #d0d7de; padding-left:12px; }
        table { border:1px solid #d0d7de; }
        th,td { border:1px solid #d0d7de; padding:4px 9px; }
        th { background:#f6f8fa; }
        hr { color:#d0d7de; }
    )";
}

// md_html sink: append each HTML chunk to the QByteArray passed as userdata.
void mdSink(const MD_CHAR* text, MD_SIZE size, void* userdata) {
    static_cast<QByteArray*>(userdata)->append(text, int(size));
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// MarkdownHighlighter — live syntax highlighting for the RAW markdown source
// pane. This is the idiomatic use of QSyntaxHighlighter (an editable document);
// the preview's code coloring is handled separately by colorizeCode() above.
// ─────────────────────────────────────────────────────────────────────────────
class MarkdownHighlighter : public QSyntaxHighlighter {
public:
    explicit MarkdownHighlighter(QTextDocument* doc, bool dark)
        : QSyntaxHighlighter(doc) { setDark(dark); }

    void setDark(bool dark) {
        // Colors tuned to read on either chrome background.
        m_heading.setForeground(QColor(dark ? "#79c0ff" : "#0550ae"));
        m_heading.setFontWeight(QFont::Bold);
        m_emphasis.setForeground(QColor(dark ? "#e6edf3" : "#1f2328"));
        m_emphasis.setFontItalic(true);
        m_strong.setForeground(QColor(dark ? "#e6edf3" : "#1f2328"));
        m_strong.setFontWeight(QFont::Bold);
        m_code.setForeground(QColor(dark ? "#ff7b72" : "#cf222e"));
        m_code.setFontFamily("Consolas");
        m_link.setForeground(QColor(dark ? "#4493f8" : "#0969da"));
        m_quote.setForeground(QColor(dark ? "#8b949e" : "#59636e"));
        m_list.setForeground(QColor(dark ? "#d2a8ff" : "#8250df"));
        m_list.setFontWeight(QFont::Bold);
        rehighlight();
    }

protected:
    void highlightBlock(const QString& text) override {
        // Fenced code region: track across blocks with the block state.
        static const QRegularExpression fence(R"(^\s*(```|~~~))");
        const bool wasInFence = previousBlockState() == 1;
        if (fence.match(text).hasMatch()) {
            setFormat(0, text.length(), m_code);
            setCurrentBlockState(wasInFence ? 0 : 1);
            return;
        }
        if (wasInFence) {
            setFormat(0, text.length(), m_code);
            setCurrentBlockState(1);
            return;
        }
        setCurrentBlockState(0);

        static const QRegularExpression heading(R"(^#{1,6}\s.*$)");
        if (auto m = heading.match(text); m.hasMatch()) {
            setFormat(0, text.length(), m_heading);
            return;   // whole heading line styled; inline rules below skipped
        }
        static const QRegularExpression quote(R"(^\s*>\s?.*$)");
        if (quote.match(text).hasMatch())
            setFormat(0, text.length(), m_quote);

        applyInline(text, QRegularExpression(R"((\*\*|__)(?=\S)(.+?)(?<=\S)\1)"), m_strong);
        applyInline(text, QRegularExpression(R"((?<![\*_])[\*_](?=\S)([^\*_]+?)(?<=\S)[\*_](?![\*_]))"), m_emphasis);
        applyInline(text, QRegularExpression(R"(`[^`]+`)"), m_code);
        applyInline(text, QRegularExpression(R"(\[[^\]]*\]\([^\)]*\))"), m_link);

        static const QRegularExpression listItem(R"(^\s*([-\*\+]|\d+\.)\s)");
        if (auto m = listItem.match(text); m.hasMatch())
            setFormat(m.capturedStart(1), m.capturedLength(1), m_list);
    }

private:
    void applyInline(const QString& text, const QRegularExpression& re,
                     const QTextCharFormat& fmt) {
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), fmt);
        }
    }

    QTextCharFormat m_heading, m_emphasis, m_strong, m_code, m_link, m_quote, m_list;
};

// ─────────────────────────────────────────────────────────────────────────────
// MarkdownEditorWidget
// ─────────────────────────────────────────────────────────────────────────────
MarkdownEditorWidget::MarkdownEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("markdownEditor");
    setAttribute(Qt::WA_StyledBackground, true);

    m_previewDark = QSettings().value("tools/markdownEditor/previewDark", false).toBool();

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_toolbar = buildToolbar();
    root->addWidget(m_toolbar);

    // ── panes ───────────────────────────────────────────────────────────────
    m_editor = new QPlainTextEdit(this);
    m_editor->setObjectName("mdSource");
    m_editor->setFrameShape(QFrame::NoFrame);
    m_editor->setTabStopDistance(32);
    m_editor->setPlaceholderText(tr("Write Markdown here…"));
    {
        QFont mono("Consolas");
        mono.setStyleHint(QFont::Monospace);
        mono.setPointSize(11);
        m_editor->setFont(mono);
    }

    m_preview = new QTextBrowser(this);
    m_preview->setObjectName("mdPreview");
    m_preview->setFrameShape(QFrame::NoFrame);
    m_preview->setOpenLinks(false);          // a preview shouldn't navigate away
    m_preview->setOpenExternalLinks(false);

    m_split = new QSplitter(Qt::Horizontal, this);
    m_split->addWidget(m_editor);
    m_split->addWidget(m_preview);
    m_split->setChildrenCollapsible(false);
    m_split->setHandleWidth(6);
    m_split->setStretchFactor(0, 7);
    m_split->setStretchFactor(1, 3);
    m_split->setSizes({ 700, 300 });         // 70 / 30 default
    root->addWidget(m_split, 1);

    m_highlighter = new MarkdownHighlighter(m_editor->document(),
                                            ThemeManager::instance().isDark());

    // ── debounced live preview ───────────────────────────────────────────────
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(220);            // re-render ~220 ms after typing stops
    connect(m_debounce, &QTimer::timeout, this, &MarkdownEditorWidget::renderPreview);
    connect(m_editor, &QPlainTextEdit::textChanged, this,
            &MarkdownEditorWidget::scheduleRender);

    // ── scroll sync (ratio-based) ─────────────────────────────────────────────
    connect(m_editor->verticalScrollBar(), &QScrollBar::valueChanged, this,
            &MarkdownEditorWidget::syncPreviewToEditor);
    connect(m_preview->verticalScrollBar(), &QScrollBar::valueChanged, this,
            &MarkdownEditorWidget::syncEditorToPreview);

    // ── follow chrome light/dark for the toolbar + panes ──────────────────────
    connect(&ThemeManager::instance(), &ThemeManager::modeChanged, this, [this] {
        applyChrome();
        m_highlighter->setDark(ThemeManager::instance().isDark());
    });

    // Restore the last split ratio, if any.
    const QByteArray splitState =
        QSettings().value("tools/markdownEditor/splitState").toByteArray();
    if (!splitState.isEmpty())
        m_split->restoreState(splitState);
    connect(m_split, &QSplitter::splitterMoved, this,
            [this](int, int) { saveState(); });

    applyChrome();

    // A friendly starter document so the split-pane behavior is obvious.
    m_editor->setPlainText(QStringLiteral(
        "# Markdown Editor\n\n"
        "Type **Markdown** on the left and see it _rendered_ live on the right.\n\n"
        "- Bullet lists\n- With `inline code`\n\n"
        "> Blockquotes look like this.\n\n"
        "```python\n"
        "def greet(name):\n"
        "    print(f\"Hello, {name}!\")  # syntax-highlighted\n"
        "```\n"));
    renderPreview();

    // Dev-only capture hook (same family as NATIVEOFFICE_RESIZER_GRAB): grab()
    // uses Qt's render path, which captures the rendered preview + toolbar that
    // PrintWindow-based external captures would drop.
    if (qEnvironmentVariableIsSet("NATIVEOFFICE_MARKDOWN_GRAB")) {
        const QString grabPath = qEnvironmentVariable("NATIVEOFFICE_MARKDOWN_GRAB");
        QTimer::singleShot(1800, this, [this, grabPath] {
            grab().save(grabPath, "PNG");
        });
    }
}

MarkdownEditorWidget::~MarkdownEditorWidget() { saveState(); }

// ── toolbar ──────────────────────────────────────────────────────────────────
QWidget* MarkdownEditorWidget::buildToolbar() {
    auto* bar = new QFrame(this);
    bar->setObjectName("mdToolbar");
    bar->setAttribute(Qt::WA_StyledBackground, true);
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(8, 6, 8, 6);
    h->setSpacing(3);

    auto addBtn = [&](const QString& label, const QString& tip,
                      std::function<void()> slot) {
        auto* b = new QToolButton(bar);
        b->setText(label);
        b->setToolTip(tip);
        b->setCursor(Qt::PointingHandCursor);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, std::move(slot));
        h->addWidget(b);
        return b;
    };
    auto addSep = [&] {
        auto* line = new QFrame(bar);
        line->setObjectName("mdSep");
        line->setFrameShape(QFrame::VLine);
        h->addSpacing(3);
        h->addWidget(line);
        h->addSpacing(3);
    };

    addBtn(tr("B"),  tr("Bold  (**text**)"),
           [this] { wrapSelection("**", "**", tr("bold text")); })
        ->setObjectName("mdBold");
    addBtn(tr("I"),  tr("Italic  (_text_)"),
           [this] { wrapSelection("_", "_", tr("italic text")); })
        ->setObjectName("mdItalic");
    addSep();
    addBtn(tr("H1"), tr("Heading 1"), [this] { insertHeading(1); });
    addBtn(tr("H2"), tr("Heading 2"), [this] { insertHeading(2); });
    addBtn(tr("H3"), tr("Heading 3"), [this] { insertHeading(3); });
    addSep();
    addBtn(tr("Link"),  tr("Insert link"),          [this] { insertLink(); });
    addBtn(tr("Image"), tr("Insert image"),         [this] { insertImage(); });
    addSep();
    addBtn(tr("Code"),  tr("Inline code  (`code`)"), [this] { insertInlineCode(); });
    addBtn(tr("Block"), tr("Code block"),            [this] { insertCodeBlock(); });
    addSep();
    addBtn(tr("• List"), tr("Bullet list"),   [this] { prefixLines("- "); });
    addBtn(tr("1. List"), tr("Numbered list"), [this] { prefixLines("1. "); });
    addBtn(tr("Quote"),  tr("Blockquote"),    [this] { prefixLines("> "); });
    addBtn(tr("HR"),     tr("Horizontal rule"), [this] { insertHorizontalRule(); });

    h->addStretch();

    m_themeBtn = addBtn(QString(), tr("Toggle the preview's light/dark theme"),
                        [this] { togglePreviewTheme(); });
    m_themeBtn->setObjectName("mdThemeToggle");
    m_themeBtn->setText(m_previewDark ? tr("Preview: Dark") : tr("Preview: Light"));

    return bar;
}

// ── chrome theming (ThemeManager) ─────────────────────────────────────────────
void MarkdownEditorWidget::applyChrome() {
    auto& tm = ThemeManager::instance();
    const QString bg     = tm.chromeBg();
    const QString panel  = tm.chromePanelBg();
    const QString hover  = tm.chromeHoverBg();
    const QString border = tm.chromeBorder();
    const QString text   = tm.chromeText();
    const QString muted  = tm.chromeTextMuted();
    // The raw editor follows the chrome surface; the preview paints its own
    // GitHub theme, so we leave its viewport to the rendered CSS.
    const QString editorBg = tm.isDark() ? "#0D1117" : "#FFFFFF";

    setStyleSheet(QString(R"(
        QWidget#markdownEditor { background:%1; }
        QFrame#mdToolbar { background:%2; border-bottom:1px solid %3; }
        QFrame#mdSep { color:%3; background:%3; max-width:1px; }
        #mdToolbar QToolButton {
            color:%4; background:transparent; border:1px solid transparent;
            border-radius:6px; padding:4px 9px; font-size:12px;
        }
        #mdToolbar QToolButton:hover { background:%5; border:1px solid %3; }
        QToolButton#mdBold   { font-weight:700; }
        QToolButton#mdItalic { font-style:italic; }
        QToolButton#mdThemeToggle { color:%6; }
        QPlainTextEdit#mdSource {
            background:%7; color:%4; border:none; padding:12px;
            selection-background-color:#2563EB; selection-color:#FFFFFF;
        }
        QTextBrowser#mdPreview { border:none; }
        QSplitter::handle { background:%3; }
    )").arg(bg, panel, border, text, hover, muted, editorBg));
}

// ── preview pipeline ──────────────────────────────────────────────────────────
void MarkdownEditorWidget::scheduleRender() { m_debounce->start(); }

void MarkdownEditorWidget::renderPreview() {
    const QByteArray src = m_editor->toPlainText().toUtf8();
    QByteArray body;
    md_html(src.constData(), MD_SIZE(src.size()), &mdSink, &body,
            MD_DIALECT_GITHUB, 0);

    QString html = colorizeCodeBlocks(QString::fromUtf8(body), m_previewDark);
    const QString doc = QStringLiteral(
        "<html><head><style>%1</style></head><body>%2</body></html>")
        .arg(previewCss(m_previewDark), html);

    // Preserve scroll position across the re-render so typing doesn't jump.
    const int pos = m_preview->verticalScrollBar()->value();
    m_preview->setHtml(doc);
    m_preview->verticalScrollBar()->setValue(pos);
}

void MarkdownEditorWidget::togglePreviewTheme() {
    m_previewDark = !m_previewDark;
    m_themeBtn->setText(m_previewDark ? tr("Preview: Dark") : tr("Preview: Light"));
    QSettings().setValue("tools/markdownEditor/previewDark", m_previewDark);
    renderPreview();
}

// ── toolbar formatting actions ────────────────────────────────────────────────
void MarkdownEditorWidget::wrapSelection(const QString& left, const QString& right,
                                         const QString& placeholder) {
    QTextCursor c = m_editor->textCursor();
    if (c.hasSelection()) {
        const QString sel = c.selectedText();
        c.insertText(left + sel + right);
    } else {
        c.insertText(left + placeholder + right);
        // Place the cursor over the placeholder so the user can overtype it.
        c.setPosition(c.position() - right.length() - placeholder.length());
        c.setPosition(c.position() + placeholder.length(), QTextCursor::KeepAnchor);
    }
    m_editor->setTextCursor(c);
    m_editor->setFocus();
}

void MarkdownEditorWidget::prefixLines(const QString& prefix) {
    QTextCursor c = m_editor->textCursor();
    const int start = c.selectionStart();
    const int end   = c.selectionEnd();
    c.setPosition(start);
    c.movePosition(QTextCursor::StartOfBlock);
    c.beginEditBlock();
    int ordinal = 1;
    while (true) {
        QTextCursor line = c;
        line.movePosition(QTextCursor::StartOfBlock);
        // Numbered lists increment; others use the literal prefix.
        QString p = prefix;
        if (prefix == QLatin1String("1. "))
            p = QString::number(ordinal++) + ". ";
        line.insertText(p);
        if (!c.movePosition(QTextCursor::NextBlock))
            break;
        if (c.position() > end + 1)     // moved past the selection
            break;
    }
    c.endEditBlock();
    m_editor->setFocus();
}

void MarkdownEditorWidget::insertHeading(int level) {
    QTextCursor c = m_editor->textCursor();
    c.movePosition(QTextCursor::StartOfBlock);
    // Drop any existing heading marker so toggling levels doesn't stack them.
    QTextCursor probe = c;
    probe.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    QString lineText = probe.selectedText();
    static const QRegularExpression lead(R"(^#{1,6}\s+)");
    const auto m = lead.match(lineText);
    c.beginEditBlock();
    if (m.hasMatch()) {
        QTextCursor del = c;
        del.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor,
                         m.capturedLength());
        del.removeSelectedText();
    }
    c.insertText(QString(level, '#') + ' ');
    c.endEditBlock();
    m_editor->setFocus();
}

void MarkdownEditorWidget::insertLink() {
    QTextCursor c = m_editor->textCursor();
    const QString sel = c.hasSelection() ? c.selectedText() : tr("link text");
    c.insertText(QStringLiteral("[%1](https://)").arg(sel));
    m_editor->setTextCursor(c);
    m_editor->setFocus();
}

void MarkdownEditorWidget::insertImage() {
    QTextCursor c = m_editor->textCursor();
    const QString sel = c.hasSelection() ? c.selectedText() : tr("alt text");
    c.insertText(QStringLiteral("![%1](https://)").arg(sel));
    m_editor->setTextCursor(c);
    m_editor->setFocus();
}

void MarkdownEditorWidget::insertInlineCode() {
    wrapSelection("`", "`", tr("code"));
}

void MarkdownEditorWidget::insertCodeBlock() {
    QTextCursor c = m_editor->textCursor();
    const QString sel = c.hasSelection() ? c.selectedText() : tr("code");
    c.insertText(QStringLiteral("```\n%1\n```\n").arg(sel));
    m_editor->setFocus();
}

void MarkdownEditorWidget::insertHorizontalRule() {
    QTextCursor c = m_editor->textCursor();
    c.movePosition(QTextCursor::EndOfBlock);
    c.insertText(QStringLiteral("\n\n---\n"));
    m_editor->setFocus();
}

// ── scroll sync ───────────────────────────────────────────────────────────────
void MarkdownEditorWidget::syncPreviewToEditor() {
    if (m_syncing) return;
    m_syncing = true;
    auto* es = m_editor->verticalScrollBar();
    auto* ps = m_preview->verticalScrollBar();
    const double ratio = es->maximum() > 0 ? double(es->value()) / es->maximum() : 0.0;
    ps->setValue(int(ratio * ps->maximum()));
    m_syncing = false;
}

void MarkdownEditorWidget::syncEditorToPreview() {
    if (m_syncing) return;
    m_syncing = true;
    auto* es = m_editor->verticalScrollBar();
    auto* ps = m_preview->verticalScrollBar();
    const double ratio = ps->maximum() > 0 ? double(ps->value()) / ps->maximum() : 0.0;
    es->setValue(int(ratio * es->maximum()));
    m_syncing = false;
}

// ── persistence ───────────────────────────────────────────────────────────────
void MarkdownEditorWidget::saveState() {
    QSettings().setValue("tools/markdownEditor/splitState", m_split->saveState());
}

} // namespace NativeOffice
