// ─────────────────────────────────────────────────────────────────────────────
// StartScreen.cpp
// Root widget for the NativeOffice start experience.
// ─────────────────────────────────────────────────────────────────────────────
#include "StartScreen.h"
#include "SidebarWidget.h"
#include "TemplateBarWidget.h"
#include "RecentFilesWidget.h"
#include "core/theme/ThemeManager.h"

#include <QFrame>

namespace NativeOffice {

StartScreen::StartScreen(AppController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
{
    buildUi();
    connectSignals();
    setObjectName("startScreen");
}

void StartScreen::buildUi() {
    m_rootLayout = new QHBoxLayout(this);
    m_rootLayout->setContentsMargins(0, 0, 0, 0);
    m_rootLayout->setSpacing(0);

    // ── Left Sidebar ───────────────────────────────────────────────────────
    m_sidebar = new SidebarWidget(this);

    // ── Content Area ───────────────────────────────────────────────────────
    m_contentArea   = new QWidget(this);
    m_contentLayout = new QVBoxLayout(m_contentArea);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);
    m_contentArea->setObjectName("contentArea");

    // Template bar (top)
    m_templateBar = new TemplateBarWidget(m_contentArea);

    // Thin divider
    auto* divider = new QFrame(m_contentArea);
    divider->setFrameShape(QFrame::HLine);
    divider->setFixedHeight(1);
    divider->setStyleSheet("background: #E5E7EB; border: none;");

    // Recent files (fills remaining space)
    m_recentFiles = new RecentFilesWidget(m_contentArea);

    m_contentLayout->addWidget(m_templateBar);
    m_contentLayout->addWidget(divider);
    m_contentLayout->addWidget(m_recentFiles, 1);

    m_rootLayout->addWidget(m_sidebar);
    m_rootLayout->addWidget(m_contentArea, 1);

    setStyleSheet(R"(
QWidget#startScreen {
    background-color: #F5F6FA;
}
QWidget#contentArea {
    background-color: #FFFFFF;
}
)");
}

void StartScreen::connectSignals() {
    // Sidebar navigation
    connect(m_sidebar, &SidebarWidget::itemSelected,
            this, [this](SidebarItem item) {
        switch (item) {
        case SidebarItem::Settings:
            emit settingsRequested();
            break;
        default:
            break;
        }
    });

    // Template bar quick-create
    connect(m_templateBar, &TemplateBarWidget::createDocument,
            this, [this](DocumentType type) {
        emit newDocumentRequested(type);
    });

    // Recent files
    connect(m_recentFiles, &RecentFilesWidget::fileSelected,
            this, [this](const QString& path) {
        emit fileOpenRequested(path);
    });
}

} // namespace NativeOffice
