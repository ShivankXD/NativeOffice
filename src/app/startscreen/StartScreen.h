#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// StartScreen.h
// The unified first screen of NativeOffice, modeled after modern office suite
// welcome experiences (WPS Office / Microsoft Office). Composes:
//   • SidebarWidget       – left navigation panel
//   • TemplateBarWidget   – top quick-create tiles
//   • RecentFilesWidget   – central recent-document list
// ─────────────────────────────────────────────────────────────────────────────

#include "core/application/AppController.h"

#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace NativeOffice {

class SidebarWidget;
class TemplateBarWidget;
class RecentFilesWidget;

class StartScreen : public QWidget {
    Q_OBJECT

public:
    explicit StartScreen(AppController* controller,
                         QWidget*       parent = nullptr);

signals:
    void newDocumentRequested(DocumentType type);
    void fileOpenRequested(const QString& path);
    void settingsRequested();

private:
    void buildUi();
    void connectSignals();

    AppController*    m_controller   { nullptr };

    QHBoxLayout*      m_rootLayout   { nullptr };

    SidebarWidget*    m_sidebar      { nullptr };
    QWidget*          m_contentArea  { nullptr };
    QVBoxLayout*      m_contentLayout{ nullptr };
    TemplateBarWidget* m_templateBar { nullptr };
    RecentFilesWidget* m_recentFiles { nullptr };
};

} // namespace NativeOffice
