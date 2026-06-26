#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// WriterModule.h  (Sprint 3 → Sprint 10)
// Full NativeOffice Word Processor module.
//
// Sprint 3 additions:
//   • Carries its own file path (m_currentPath) and dirty flag
//   • saveToPath(path)  – writes .noff (UTF-8 HTML) to disk, clears dirty
//   • loadFromPath(path)– reads .noff from disk into the editor, clears dirty
//   • currentFilePath() – the currently bound file (empty = untitled)
//   • isDirty()         – true when unsaved changes exist
//   • markClean()       – externally mark the document as saved
//   • titleString()     – full window-title-ready string
//
// Sprint 10 additions:
//   • insertImage()     – opens file dialog, scales, embeds as Base64 <img>
// ─────────────────────────────────────────────────────────────────────────────

#include <QWidget>
#include <QTextEdit>
#include <QString>
#include <QFont>

class QTimer;

class QScrollArea;

namespace NativeOffice {

class WriterRibbon;
class WriterStatusBar;
class PagedTextEdit;

class WriterModule : public QWidget {
    Q_OBJECT

public:
    explicit WriterModule(QWidget* parent = nullptr);

    // ── Editor access ─────────────────────────────────────────────────────
    [[nodiscard]] QTextEdit*     editor()   const noexcept { return m_editor; }
    [[nodiscard]] QTextDocument* document() const noexcept;

    // ── File state ────────────────────────────────────────────────────────
    [[nodiscard]] QString currentFilePath() const noexcept { return m_currentPath; }
    [[nodiscard]] bool    isDirty()         const noexcept { return m_dirty; }
    [[nodiscard]] QString titleString()     const;

    // ── File I/O ──────────────────────────────────────────────────────────
    // Saves content to 'path' (must end in .noff).
    // Returns true on success.  Updates m_currentPath and clears dirty flag.
    bool saveToPath(const QString& path);

    // Loads a .noff file into the editor.
    // Returns true on success.  Updates m_currentPath and clears dirty flag.
    bool loadFromPath(const QString& path);

    // Legacy helpers for compat with Sprint 2 code
    void setContent(const QString& html);
    void setPlainContent(const QString& text);
    void markClean();

signals:
    void documentModified();
    // Emitted after a successful save/load with the new path
    void filePathChanged(const QString& newPath);

protected:
    // Ctrl+scroll over the canvas zooms the page (Sprint 14).
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    void onContentsChanged();

    // Sprint 10: Image insertion
    void insertImage();

private:
    void buildUi();
    void applyCanvasStyles();

    // Sprint 14: status bar wiring
    void updateStatus();              // recomputes word + page count (debounced)
    void scheduleStatusUpdate();      // coalesces rapid recompute requests
    void applyZoom(int percent);      // status-bar zoom slider
    void setWebLayout(bool web);      // print/web page-view toggle
    void zoomBy(int deltaPercent);    // Ctrl+scroll zoom step

    // Sprint 15: Page Layout (driven by ribbon signals)
    void setPageMargin(double px);
    void setOrientation(bool landscape);
    void setPageSize(double portraitW, double portraitH);
    void setPageColor(const QColor& color);

    WriterRibbon*    m_ribbon    { nullptr };
    WriterStatusBar* m_statusBar { nullptr };
    QWidget*         m_canvas    { nullptr };
    QScrollArea*     m_scroll    { nullptr };
    QTextEdit*       m_editor    { nullptr };   // == m_paper (QTextEdit API surface)
    PagedTextEdit*   m_paper     { nullptr };   // same object, paged-specific calls
    QTimer*          m_statusTimer { nullptr };  // debounce for updateStatus

    QFont          m_baseFont;                // body font at 100% zoom
    int            m_zoom         { 100 };
    bool           m_webLayout    { false };
    bool           m_applyingZoom { false };  // re-entrancy guard for applyZoom

    // Page geometry at 100% zoom (portrait base dims; landscape swaps them).
    double         m_basePageW   { 794.0 };   // A4 width  @96dpi
    double         m_basePageH   { 1123.0 };  // A4 height @96dpi
    double         m_pageMargin  { 60.0 };
    bool           m_landscape   { false };
    QColor         m_pageColor   { "#FFFFFF" };

    QString        m_currentPath;             // empty = untitled
    bool           m_dirty       { false };
    bool           m_ignoreChange{ false };   // guard during load
};

} // namespace NativeOffice
