#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfModule.h — the "PDF" tab: a tool hub (not a page-renderer/viewer, since
// the app has no PDF rendering surface today). Each tool card opens file
// picker(s), runs the operation via PdfOps, and shows a result panel.
//
// Mirrors the file-state interface WriterModule/CalcModule/ImpressModule
// expose (currentFilePath/isDirty/titleString) so PdfWindow in main.cpp can
// wrap it exactly like the other three EditorWindow subclasses.
// ─────────────────────────────────────────────────────────────────────────────

#include <QWidget>
#include <functional>

class QLabel;
class QVBoxLayout;
class QPushButton;

namespace NativeOffice {

class PdfModule : public QWidget {
    Q_OBJECT

public:
    explicit PdfModule(QWidget* parent = nullptr);

    [[nodiscard]] QString currentFilePath() const noexcept { return m_currentPath; }
    [[nodiscard]] bool    isDirty()         const noexcept { return false; }  // tool hub: nothing to save
    [[nodiscard]] QString titleString()     const;

    // Pre-loads a path as the working file (e.g. a .pdf opened from Home/CLI).
    void setInitialFile(const QString& path);

signals:
    void documentModified();               // never emitted; kept for interface symmetry
    void filePathChanged(const QString& newPath);

private:
    void buildUi();
    QWidget* buildToolCard(const QString& title, const QString& subtitle, const QString& color,
                           const char* icon, bool enabled, std::function<void()> onClick);
    void applyTheme();

    void runMerge();
    void runSplit();
    void runCompress();
    void showComingSoon(const QString& toolName);

    void reportResult(const QString& opLabel, bool ok, const QString& message, const QString& outputPath);
    void setCurrentPath(const QString& path);

    QString      m_currentPath;
    QWidget*     m_root        { nullptr };
    QLabel*      m_titleLabel  { nullptr };
    QWidget*     m_resultPanel { nullptr };
    QLabel*      m_resultLabel { nullptr };
    QPushButton* m_revealBtn   { nullptr };
    QString      m_lastOutputFolder;
};

} // namespace NativeOffice
