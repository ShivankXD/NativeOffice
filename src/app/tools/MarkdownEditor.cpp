// ─────────────────────────────────────────────────────────────────────────────
// MarkdownEditor.cpp — see MarkdownEditor.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "MarkdownEditor.h"

#include "startscreen/LucideIcons.h"
#include "DocxIo.h"   // WriterModule's OOXML writer, reused for markdown → .docx
#include "core/common/BrandBar.h"

#include <QByteArray>
#include <QDir>
#include <QEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QSplitterHandle>
#include <QSyntaxHighlighter>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>
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
            body { background:#1e1e1e; color:#e6e6e6; font-family:'Segoe UI',Arial,sans-serif;
                   font-size:14px; }
            a { color:#4493f8; }
            h1,h2,h3,h4 { color:#ffffff; font-weight:600; }
            h1 { font-size:26px; } h2 { font-size:21px; } h3 { font-size:17px; }
            code { background:#2d2d2d; color:#e6e6e6; font-family:Consolas,'Courier New',monospace; }
            pre { background:#252526; color:#e6e6e6; padding:10px;
                  font-family:Consolas,'Courier New',monospace; }
            blockquote { color:#9aa0a6; border-left:3px solid #3a3a3a; padding-left:12px; }
            table { border:1px solid #3a3a3a; }
            th,td { border:1px solid #3a3a3a; padding:4px 9px; }
            th { background:#252526; }
            hr { color:#3a3a3a; }
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

// ── editor chrome palette (self-themed, decoupled from app ThemeManager) ──────
struct Chrome {
    QString paneBg, toolbarBg, border, sep, icon, iconHover, iconHoverBg, divider;
};
Chrome chrome(bool dark) {
    return dark
        ? Chrome{ "#1E1E1E", "#1E1E1E", "#2E2E2E", "#3A3A3A",
                  "#C7C7C7", "#FFFFFF", "#2E2E2E", "#3A3A3A" }
        : Chrome{ "#FFFFFF", "#F6F7F9", "#E2E4E9", "#DADCE2",
                  "#4B5262", "#111318", "#E9EBEF", "#E2E4E9" };
}

// A pane divider that LOOKS like a 1px hairline but keeps a comfortable grab
// area (the handle is several px wide; only a centered 1px line is painted).
class HairlineHandle : public QSplitterHandle {
public:
    HairlineHandle(Qt::Orientation o, QSplitter* parent)
        : QSplitterHandle(o, parent) {
        setAttribute(Qt::WA_OpaquePaintEvent, true);   // we cover the whole rect
    }
    void setColors(const QColor& bg, const QColor& line) {
        m_bg = bg; m_line = line; update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), m_bg);                       // blend with the panes…
        const QColor c = m_hover ? m_line.lighter(150) : m_line;
        p.fillRect(width() / 2, 0, 1, height(), c);     // …then a 1px hairline
    }
    void enterEvent(QEnterEvent*) override { m_hover = true;  update(); }
    void leaveEvent(QEvent*)      override { m_hover = false; update(); }

private:
    QColor m_bg   { "#1E1E1E" };
    QColor m_line { "#3A3A3A" };
    bool   m_hover { false };
};

class HairlineSplitter : public QSplitter {
public:
    using QSplitter::QSplitter;
    void setDividerColors(const QColor& bg, const QColor& line) {
        // Every handle we create is a HairlineHandle, so the cast is safe.
        for (int i = 1; i < count(); ++i)
            static_cast<HairlineHandle*>(handle(i))->setColors(bg, line);
    }

protected:
    QSplitterHandle* createHandle() override {
        return new HairlineHandle(orientation(), this);
    }
};

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

    m_dark = QSettings().value("tools/markdownEditor/dark", false).toBool();

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // The shared NativeOffice brand strip (mark + wordmark + Free/Premium pill
    // + on-screen rename bar) sits above the editor's own toolbar, matching the
    // other editors. It follows the app chrome theme, independent of the
    // editor's light/dark preview toggle below it.
    root->addWidget(new BrandBar(this));

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

    m_editor->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_preview = new QTextBrowser(this);
    m_preview->setObjectName("mdPreview");
    m_preview->setFrameShape(QFrame::NoFrame);
    m_preview->setOpenLinks(false);          // a preview shouldn't navigate away
    m_preview->setOpenExternalLinks(false);
    m_preview->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_preview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* split = new HairlineSplitter(Qt::Horizontal, this);
    m_split = split;
    m_split->addWidget(m_editor);
    m_split->addWidget(m_preview);
    m_split->setChildrenCollapsible(false);
    m_split->setHandleWidth(9);              // wide grab area; paints a 1px line
    m_split->setStretchFactor(0, 7);
    m_split->setStretchFactor(1, 3);
    m_split->setSizes({ 700, 300 });         // 70 / 30 default
    root->addWidget(m_split, 1);

    m_highlighter = new MarkdownHighlighter(m_editor->document(), m_dark);

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

void MarkdownEditorWidget::setDocName(const QString& name) {
    if (!name.trimmed().isEmpty()) m_docName = name.trimmed();
}

// ── toolbar (monochrome Lucide icon buttons) ──────────────────────────────────
QWidget* MarkdownEditorWidget::buildToolbar() {
    auto* bar = new QFrame(this);
    bar->setObjectName("mdToolbar");
    bar->setAttribute(Qt::WA_StyledBackground, true);
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(8, 5, 8, 5);
    h->setSpacing(2);

    auto addBtn = [&](const char* svg, const QString& tip,
                      std::function<void()> slot) {
        auto* b = new QToolButton(bar);
        b->setToolTip(tip);
        b->setIconSize(QSize(18, 18));
        b->setFixedSize(30, 30);
        b->setAutoRaise(true);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QToolButton::clicked, this, std::move(slot));
        h->addWidget(b);
        m_iconButtons.emplace_back(b, svg);       // for theme recolor
        return b;
    };
    auto addSep = [&] {
        auto* line = new QFrame(bar);
        line->setObjectName("mdSep");
        line->setFrameShape(QFrame::VLine);
        line->setFixedWidth(1);
        h->addSpacing(4);
        h->addWidget(line);
        h->addSpacing(4);
    };

    // Order mirrors the reference layout exactly.
    addBtn(Lucide::kHeading, tr("Heading  (click again for a smaller level)"),
           [this] { cycleHeading(); });
    addBtn(Lucide::kBold, tr("Bold  (**text**)"),
           [this] { wrapSelection("**", "**", tr("bold text")); });
    addBtn(Lucide::kItalic, tr("Italic  (_text_)"),
           [this] { wrapSelection("_", "_", tr("italic text")); });
    addSep();
    addBtn(Lucide::kListOrdered, tr("Numbered list"), [this] { prefixLines("1. "); });
    addBtn(Lucide::kListBullet,  tr("Bullet list"),   [this] { prefixLines("- "); });
    addBtn(Lucide::kMinus,       tr("Horizontal rule"), [this] { insertHorizontalRule(); });
    addSep();
    addBtn(Lucide::kTerminalSquare, tr("Code block"),           [this] { insertCodeBlock(); });
    addBtn(Lucide::kCode,           tr("Inline code  (`code`)"), [this] { insertInlineCode(); });
    addBtn(Lucide::kTable,          tr("Table"),                 [this] { insertTable(); });
    addBtn(Lucide::kSigma,          tr("Equation  ($…$)"),       [this] { insertEquation(); });
    addSep();
    addBtn(Lucide::kImage, tr("Image"),      [this] { insertImage(); });
    addBtn(Lucide::kLink,  tr("Link"),       [this] { insertLink(); });
    addBtn(Lucide::kQuote, tr("Blockquote"), [this] { prefixLines("> "); });

    h->addStretch();

    // Export ▾ (PDF / Word) — a menu button on the right of the tray.
    auto* exportBtn = new QToolButton(bar);
    exportBtn->setToolTip(tr("Export…"));
    exportBtn->setIconSize(QSize(18, 18));
    exportBtn->setFixedSize(34, 30);
    exportBtn->setAutoRaise(true);
    exportBtn->setCursor(Qt::PointingHandCursor);
    exportBtn->setPopupMode(QToolButton::InstantPopup);
    exportBtn->setStyleSheet("QToolButton::menu-indicator { image:none; }");
    auto* menu = new QMenu(exportBtn);
    menu->addAction(tr("Export as PDF…"),  this, &MarkdownEditorWidget::exportPdf);
    menu->addAction(tr("Export as Word (.docx)…"), this, &MarkdownEditorWidget::exportDocx);
    exportBtn->setMenu(menu);
    h->addWidget(exportBtn);
    m_iconButtons.emplace_back(exportBtn, NativeOffice::Lucide::kDownload);

    auto* sep = new QFrame(bar);
    sep->setObjectName("mdSep");
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedWidth(1);
    h->addSpacing(4); h->addWidget(sep); h->addSpacing(4);

    // Light/dark toggle lives in the toolbar tray, far right (sun ⇄ moon).
    m_themeBtn = addBtn(m_dark ? Lucide::kSun : Lucide::kMoon,
                        tr("Toggle light / dark theme"),
                        [this] { toggleTheme(); });
    m_themeBtn->setObjectName("mdThemeToggle");

    return bar;
}

// Recolor every toolbar glyph for the current theme (icons are rendered
// pixmaps, so a theme flip re-renders them at the new tint).
void MarkdownEditorWidget::refreshToolbarIcons() {
    const Chrome c = chrome(m_dark);
    const qreal dpr = devicePixelRatioF();
    for (auto& [btn, svg] : m_iconButtons)
        btn->setIcon(NativeOffice::Lucide::icon(svg, c.icon, 18, dpr));
    if (m_themeBtn)
        m_themeBtn->setIcon(NativeOffice::Lucide::icon(
            m_dark ? NativeOffice::Lucide::kSun : NativeOffice::Lucide::kMoon,
            c.icon, 18, dpr));
}

// ── self-themed chrome (toolbar / source pane / divider) ──────────────────────
void MarkdownEditorWidget::applyChrome() {
    const Chrome c = chrome(m_dark);
    const QString sbIdle  = m_dark ? "#242424" : "#F1F2F4";  // blends with the pane
    const QString sbHover = m_dark ? "#4A4A4A" : "#C2C6CE";

    // Toolbar + root only — pane/scrollbar styling is applied on the panes
    // themselves below (a descendant selector doesn't reliably reach a
    // scrollbar owned by QPlainTextEdit/QTextBrowser).
    setStyleSheet(QString(R"(
        QWidget#markdownEditor { background:%1; }
        QFrame#mdToolbar { background:%2; border-bottom:1px solid %3; }
        QFrame#mdSep { background:%4; border:none; }
        #mdToolbar QToolButton {
            background:transparent; border:1px solid transparent; border-radius:6px;
        }
        #mdToolbar QToolButton:hover { background:%5; }
    )").arg(c.paneBg, c.toolbarBg, c.border, c.sep, c.iconHoverBg));

    // A thin, subtle scrollbar shared by both panes.
    const QString scrollbars = QString(R"(
        QScrollBar:vertical { background:transparent; width:10px; margin:0; }
        QScrollBar::handle:vertical { background:%1; min-height:30px; border-radius:5px; }
        QScrollBar::handle:vertical:hover { background:%2; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }
        QScrollBar:horizontal { height:0; }
    )").arg(sbIdle, sbHover);

    m_editor->setStyleSheet(QString(R"(
        QPlainTextEdit#mdSource {
            background:%1; color:%2; border:none; padding:12px 14px;
            selection-background-color:#2563EB; selection-color:#FFFFFF;
        }
    )").arg(c.paneBg, c.icon) + scrollbars);
    m_preview->setStyleSheet(
        QString("QTextBrowser#mdPreview { background:%1; border:none; }").arg(c.paneBg)
        + scrollbars);

    // m_split is always a HairlineSplitter (created in the constructor).
    static_cast<HairlineSplitter*>(m_split)->setDividerColors(
        QColor(c.paneBg), QColor(c.divider));
    refreshToolbarIcons();
}

// ── preview pipeline ──────────────────────────────────────────────────────────
void MarkdownEditorWidget::scheduleRender() { m_debounce->start(); }

void MarkdownEditorWidget::renderPreview() {
    const QByteArray src = m_editor->toPlainText().toUtf8();
    QByteArray body;
    md_html(src.constData(), MD_SIZE(src.size()), &mdSink, &body,
            MD_DIALECT_GITHUB, 0);

    QString html = colorizeCodeBlocks(QString::fromUtf8(body), m_dark);
    const QString doc = QStringLiteral(
        "<html><head><style>%1</style></head><body>%2</body></html>")
        .arg(previewCss(m_dark), html);

    // Preserve scroll position across the re-render so typing doesn't jump.
    const int pos = m_preview->verticalScrollBar()->value();
    m_preview->setHtml(doc);
    m_preview->verticalScrollBar()->setValue(pos);
}

void MarkdownEditorWidget::toggleTheme() {
    m_dark = !m_dark;
    QSettings().setValue("tools/markdownEditor/dark", m_dark);
    m_highlighter->setDark(m_dark);   // recolor the source-pane syntax
    applyChrome();                    // toolbar / panes / divider + toolbar icons
    renderPreview();                  // preview CSS + code colors
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

void MarkdownEditorWidget::cycleHeading() {
    // Each click bumps the current line's heading one level smaller
    // (# → ## → … → ######), then clears it and starts over.
    QTextCursor c = m_editor->textCursor();
    c.movePosition(QTextCursor::StartOfBlock);
    QTextCursor probe = c;
    probe.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    const QString lineText = probe.selectedText();

    static const QRegularExpression lead(R"(^(#{1,6})\s+)");
    const auto m = lead.match(lineText);
    const int current = m.hasMatch() ? int(m.captured(1).length()) : 0;
    const int next    = (current >= 6) ? 0 : current + 1;   // 6 → back to plain

    c.beginEditBlock();
    if (m.hasMatch()) {          // strip the existing "### " marker first
        QTextCursor del = c;
        del.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor,
                         m.capturedLength());
        del.removeSelectedText();
    }
    if (next > 0)
        c.insertText(QString(next, '#') + ' ');
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
    // Pick a real image file and insert a markdown image that the preview can
    // actually render (a file:// URL loads locally in the QTextBrowser).
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Insert Image"), QDir::homePath(),
        tr("Images (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.svg)"));
    if (path.isEmpty()) return;

    const QString alt = QFileInfo(path).completeBaseName();
    const QString url = QUrl::fromLocalFile(path).toString();
    QTextCursor c = m_editor->textCursor();
    c.insertText(QStringLiteral("![%1](%2)").arg(alt, url));
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

void MarkdownEditorWidget::insertTable() {
    QTextCursor c = m_editor->textCursor();
    c.movePosition(QTextCursor::EndOfBlock);
    c.insertText(QStringLiteral(
        "\n\n| Column A | Column B |\n| --- | --- |\n| Cell 1 | Cell 2 |\n"));
    m_editor->setFocus();
}

void MarkdownEditorWidget::insertEquation() {
    // Inline LaTeX-style math span. (Shown as source text in the preview — we
    // don't bundle a math renderer — but round-trips as standard markdown.)
    wrapSelection("$", "$", tr("E = mc^2"));
}

void MarkdownEditorWidget::insertHorizontalRule() {
    QTextCursor c = m_editor->textCursor();
    c.movePosition(QTextCursor::EndOfBlock);
    c.insertText(QStringLiteral("\n\n---\n"));
    m_editor->setFocus();
}

// ── export ────────────────────────────────────────────────────────────────────
// Build a print-friendly (always light) QTextDocument from the markdown. The
// caller owns the returned document.
QTextDocument* MarkdownEditorWidget::renderedDocument() const {
    const QByteArray src = m_editor->toPlainText().toUtf8();
    QByteArray body;
    md_html(src.constData(), MD_SIZE(src.size()), &mdSink, &body,
            MD_DIALECT_GITHUB, 0);
    const QString html = colorizeCodeBlocks(QString::fromUtf8(body), /*dark*/false);
    const QString full = QStringLiteral(
        "<html><head><style>%1</style></head><body>%2</body></html>")
        .arg(previewCss(false), html);

    auto* doc = new QTextDocument;
    doc->setHtml(full);
    return doc;
}

void MarkdownEditorWidget::exportPdf() {
    const QString suggested = QDir::homePath() + "/" + m_docName + ".pdf";
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export as PDF"), suggested, tr("PDF Document (*.pdf)"));
    if (path.isEmpty()) return;

    QPdfWriter pdf(path);
    pdf.setCreator(QStringLiteral("NativeOffice Markdown Editor"));
    pdf.setTitle(QFileInfo(path).completeBaseName());
    pdf.setPageSize(QPageSize::A4);
    pdf.setPageMargins(QMarginsF(18, 18, 18, 18), QPageLayout::Millimeter);
    pdf.setResolution(300);

    QTextDocument* doc = renderedDocument();
    doc->print(&pdf);
    delete doc;

    QMessageBox::information(this, tr("Export Complete"),
                            tr("Exported to PDF:\n%1").arg(path));
}

void MarkdownEditorWidget::exportDocx() {
    const QString suggested = QDir::homePath() + "/" + m_docName + ".docx";
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export as Word"), suggested, tr("Word Document (*.docx)"));
    if (path.isEmpty()) return;

    QTextDocument* doc = renderedDocument();
    const bool ok = DocxIo::exportDocx(doc, path);
    delete doc;

    if (ok)
        QMessageBox::information(this, tr("Export Complete"),
                                tr("Exported to Word:\n%1").arg(path));
    else
        QMessageBox::warning(this, tr("Export Failed"),
                            tr("Could not write the .docx file:\n%1").arg(path));
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
