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
// in a GitHub-style stylesheet (light or dark, toggled in the toolbar tray,
// independent of the app-wide ThemeManager chrome mode). Fenced code blocks in
// the preview are colorized by a small rule-based tokenizer (colored <span>s);
// the raw-markdown pane gets a live QSyntaxHighlighter for its own source.
//
// Re-render is debounced (~220 ms after typing pauses) to avoid flicker on
// long documents. The two panes scroll-sync by position ratio. The split ratio
// and preview theme persist across sessions via QSettings.
// ─────────────────────────────────────────────────────────────────────────────

#include <QWidget>

class QPlainTextEdit;
class QTextBrowser;
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

private:
    // ── build / theming ────────────────────────────────────────────────────
    QWidget* buildToolbar();
    void     applyChrome();          // toolbar/splitter chrome from ThemeManager
    void     togglePreviewTheme();   // editor-local light/dark preview CSS

    // ── preview pipeline ───────────────────────────────────────────────────
    void     scheduleRender();       // (re)start the debounce timer
    void     renderPreview();        // markdown → md4c → styled+colorized HTML

    // ── toolbar formatting actions ─────────────────────────────────────────
    void wrapSelection(const QString& left, const QString& right,
                       const QString& placeholder);       // **bold**, _italic_…
    void prefixLines(const QString& prefix);              // - , 1. , > …
    void insertHeading(int level);                        // #, ##, ###
    void insertLink();
    void insertImage();
    void insertInlineCode();
    void insertCodeBlock();
    void insertHorizontalRule();

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

    bool m_previewDark { false };   // preview CSS theme (default: light)
    bool m_syncing     { false };   // guard so scroll-sync doesn't feed back
};

} // namespace NativeOffice
