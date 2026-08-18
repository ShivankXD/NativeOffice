#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// MarkdownEditor.h — Home-screen tool: a split-pane Markdown editor.
//
//  ┌──────────────────────────────────────────────────────────────────────┐
//  │  B  I  H1 H2 H3  🔗 🖼  `code`  {}  •  1.  ❝  ──        ☀/🌙 theme   │  toolbar
//  ├───────────────────────────────────────────┬──────────────────────────┤
//  │  raw markdown (QPlainTextEdit)             │  live preview            │
//  │  + markdown QSyntaxHighlighter             │  (QTextBrowser, GitHub   │
//  │                                            │   light/dark CSS)        │
//  │  ← 70% default, draggable divider →        │  ← 30% default →         │
//  └───────────────────────────────────────────┴──────────────────────────┘
//
// The preview is produced by md4c (third_party/md4c) → HTML fragment, wrapped
// in a GitHub-style stylesheet. The editor is self-themed (its own dark/light
// look, default dark) toggled by the sun/moon button in the toolbar tray — one
// switch drives the whole editor: toolbar, source pane, divider and preview.
// Fenced code blocks in the preview are colorized by a small rule-based
// tokenizer (colored <span>s); the raw-markdown pane gets a live
// QSyntaxHighlighter for its own source.
//
// The toolbar is a row of monochrome Lucide icon buttons (not text labels),
// grouped by thin separators. The pane divider is a 1px hairline painted by a
// custom splitter handle with a comfortable grab area.
//
// Re-render is debounced (~220 ms after typing pauses) to avoid flicker on
// long documents. The two panes scroll-sync by position ratio. The split ratio
// and theme persist across sessions via QSettings.
// ─────────────────────────────────────────────────────────────────────────────

#include <QWidget>

#include <utility>
#include <vector>

class QPlainTextEdit;
class QTextBrowser;
class QTextDocument;
class QSplitter;
class QTimer;
class QToolButton;

namespace NativeOffice {

class MarkdownHighlighter;   // markdown-source syntax highlighter (raw pane)

class MarkdownEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit MarkdownEditorWidget(QWidget* parent = nullptr);
    ~MarkdownEditorWidget() override;

    // The document base name used to seed the Export dialogs (set by the window
    // from the on-screen rename bar).
    void setDocName(const QString& name);

    // Load a .md file from disk into the source pane (Home → Open File routes
    // markdown here rather than into Writer). Returns false if it can't be read.
    bool loadFromFile(const QString& path);

private:
    // ── build / theming ────────────────────────────────────────────────────
    QWidget* buildToolbar();
    void     applyChrome();          // repaint toolbar/panes/divider for m_dark
    void     toggleTheme();          // flip the whole editor light ⇄ dark
    void     refreshToolbarIcons();  // recolor the toolbar glyphs for m_dark

    // ── preview pipeline ───────────────────────────────────────────────────
    void     scheduleRender();       // (re)start the debounce timer
    void     renderPreview();        // markdown → md4c → styled+colorized HTML

    // ── toolbar formatting actions ─────────────────────────────────────────
    void wrapSelection(const QString& left, const QString& right,
                       const QString& placeholder);       // **bold**, _italic_…
    void prefixLines(const QString& prefix);              // - , 1. , > …
    void cycleHeading();                                  // #, ##, ### … then off
    void insertLink();
    void insertImage();                                   // pick a file → ![]()
    void insertInlineCode();
    void insertCodeBlock();
    void insertTable();
    void insertEquation();
    void insertHorizontalRule();

    // ── export ───────────────────────────────────────────────────────────────
    void exportPdf();
    void exportDocx();
    [[nodiscard]] QTextDocument* renderedDocument() const;  // md → light-CSS doc

    // ── scroll sync (ratio-based, reentrancy-guarded) ──────────────────────
    void syncPreviewToEditor();
    void syncEditorToPreview();

    // ── persistence ────────────────────────────────────────────────────────
    void saveState();

    QPlainTextEdit*      m_editor      { nullptr };
    QTextBrowser*        m_preview     { nullptr };
    QSplitter*           m_split       { nullptr };
    QWidget*             m_toolbar     { nullptr };
    QTimer*              m_debounce    { nullptr };
    MarkdownHighlighter* m_highlighter { nullptr };
    QToolButton*         m_themeBtn    { nullptr };

    // Each toolbar button paired with its Lucide SVG, so a theme flip can
    // recolor every glyph in one pass.
    std::vector<std::pair<QToolButton*, const char*>> m_iconButtons;

    bool m_dark    { false };   // whole-editor theme (default light)
    bool m_syncing { false };   // guard so scroll-sync doesn't feed back
    QString m_docName { QStringLiteral("untitled") };  // export/save base name
};

} // namespace NativeOffice
