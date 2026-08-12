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
class QListWidget;

namespace NativeOffice {

class WriterRibbon;
class WriterStatusBar;
class PagedTextEdit;
class WriterRuler;
class FocusOverlay;

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

    // Re-clears the dirty flag across the open-time relayout, so merely opening
    // a document never leaves it looking modified (and so autosave never
    // rewrites a file the user has not edited).
    void markCleanAfterLoad();

    // Free-plan view-only mode: editor becomes read-only and the ribbon's
    // editing controls are disabled. Viewing/scrolling stays available.
    void setReadOnly(bool on);

    // Focus Mode: everything except the page fades to black. Toggled from the
    // View tab or with kFocusShortcut (which also turns it back off).
    static constexpr const char* kFocusShortcut = "Ctrl+Shift+F";
    void setFocusMode(bool on);
    [[nodiscard]] bool focusMode() const noexcept { return m_focusMode; }

signals:
    void documentModified();
    // Emitted after a successful save/load with the new path
    void filePathChanged(const QString& newPath);

public slots:
    // Sprint 23: rulers
    void setRulersVisible(bool on);
    // Sprint 25: navigation pane (document outline)
    void setNavPaneVisible(bool on);
    // Sprint 26: comments pane
    void setCommentsPaneVisible(bool on);
    void refreshCommentsPane();

protected:
    // Ctrl+scroll over the canvas zooms the page (Sprint 14).
    bool eventFilter(QObject* obj, QEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;

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

    void updateRulerGeometry();       // keep rulers aligned with the page

    // Sprint 24: autosave + crash recovery
    QString buildNoffString() const;  // header + style sidecar + HTML
    void    loadNoffString(QString content);  // parse sidecar + set HTML
    QString recoveryFilePath() const; // per-document recovery file location
    void    writeRecovery();          // timed snapshot of unsaved changes
    void    checkCrashRecovery();     // offer to restore a leftover recovery file

    void refreshNavPane();            // rebuild the heading outline list

    // The page area to keep lit in Focus Mode: the visible strip of the paper
    // inside the scroll viewport, in WriterModule coordinates.
    [[nodiscard]] QRect focusHoleRect() const;
    void settlePageLayout();          // run any pending canvas layout now
    void syncFocusOverlay();          // re-cut the hole after the page geometry moves

    WriterRibbon*    m_ribbon    { nullptr };
    WriterRuler*     m_hRuler    { nullptr };
    WriterRuler*     m_vRuler    { nullptr };
    bool             m_rulersVisible { true };
    QWidget*         m_navPane   { nullptr };
    QListWidget*     m_navList   { nullptr };
    bool             m_navVisible { false };
    QWidget*         m_commentsPane { nullptr };
    QListWidget*     m_commentsList { nullptr };
    bool             m_commentsVisible { false };
    WriterStatusBar* m_statusBar { nullptr };
    QWidget*         m_canvas    { nullptr };
    QScrollArea*     m_scroll    { nullptr };
    QTextEdit*       m_editor    { nullptr };   // == m_paper (QTextEdit API surface)
    PagedTextEdit*   m_paper     { nullptr };   // same object, paged-specific calls
    QTimer*          m_statusTimer { nullptr };  // debounce for updateStatus
    QTimer*          m_autosaveTimer { nullptr }; // periodic crash-recovery snapshot

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

    FocusOverlay*  m_focusOverlay { nullptr };
    bool           m_focusMode    { false };

    QString        m_currentPath;             // empty = untitled
    bool           m_dirty       { false };
    bool           m_ignoreChange{ false };   // guard during load
};

} // namespace NativeOffice
