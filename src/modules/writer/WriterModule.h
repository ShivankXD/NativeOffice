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

namespace NativeOffice {

class WriterToolbar;

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

private slots:
    void onContentsChanged();

    // Sprint 10: Image insertion
    void insertImage();

private:
    void buildUi();
    void applyCanvasStyles();

    WriterToolbar* m_toolbar     { nullptr };
    QWidget*       m_canvas      { nullptr };
    QTextEdit*     m_editor      { nullptr };

    QString        m_currentPath;             // empty = untitled
    bool           m_dirty       { false };
    bool           m_ignoreChange{ false };   // guard during load
};

} // namespace NativeOffice
