#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// StartScreen.h
// The NativeOffice home dashboard.
//
//  ┌────────────┬──────────────────────────────────────────┬──────────────┐
//  │ brand      │ search        date / time      AI  🔔 ⚙ 👤              │
//  ├────────────┼──────────────────────────────────────────┴──────────────┤
//  │ Home       │  ╔═══ Good evening, Shivank ═══ photo ═══╗ │ Your Activity│
//  │ Documents  │  ║ Create New ▾   Open File              ║ │ Tools        │
//  │ Sheets     │  ╚═══════════════════════════════════════╝ │ AI Assistant │
//  │ Slides     │  [Document][Sheet][Slides][PDF][Open File] │ Quick Tips   │
//  │ PDF        │  ┌ Recent Files ─┐ ┌ Templates for You ─┐  │              │
//  │ Templates  │  └───────────────┘ └────────────────────┘  │              │
//  │ Recycle    ├──────────────────────────────────────────────────────────┤
//  │ 👤 profile │ " quote "     Quick Access  W S P A 📁   saved locally    │
//  └────────────┴──────────────────────────────────────────────────────────┘
//
// The pieces with real behaviour behind them live in their own files:
// HeroBanner (time-of-day greeting band), FileSearch (a genuine index of the
// user's files), ActivityCard (history-backed graph), TemplateMarket +
// TemplateArt (the gallery and its painted thumbnails).
//
// Signals the app wires to AppController / the shell:
//   • newDocumentRequested(type), blank doc in Writer/Calc/Impress/PDF
//   • fileOpenRequested(path)   , open an existing file
//   • templateChosen(type, name), a named template from the gallery
//   • toolRequested(tool)       , one of the Home tools
//   • aiRequested()             , open the Stasis sidebar
//   • settingsRequested()       , open settings
// ─────────────────────────────────────────────────────────────────────────────

#include "core/application/AppController.h"

#include <QPointer>
#include <QWidget>

class QVBoxLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;

namespace NativeOffice {

class SettingsTray;
class SearchPopup;
class HeroBanner;

class StartScreen : public QWidget {
    Q_OBJECT

public:
    // The tools reachable from the right-hand Tools card.
    enum class Tool {
        ImageResizer, MarkdownEditor, QrCode, CompressPdf, Ocr, PdfToWord
    };
    Q_ENUM(Tool)

    explicit StartScreen(AppController* controller, QWidget* parent = nullptr);

signals:
    void newDocumentRequested(DocumentType type);
    void fileOpenRequested(const QString& path);
    void settingsRequested();
    void templateChosen(DocumentType type, const QString& name);
    void toolRequested(StartScreen::Tool tool);
    void aiRequested();

protected:
    void resizeEvent(QResizeEvent*) override;
    // Arrow keys and Escape belong to the search results while they are open.
    bool eventFilter(QObject*, QEvent*) override;

private:
    // ── chrome ──────────────────────────────────────────────────────────────
    void     buildUi();
    QWidget* buildSidebar();
    QWidget* buildTopBar();
    QWidget* buildBottomBar();
    QWidget* buildUpdateBanner();       // compact "checking for updates" pill
    void     refreshUpdateBanner();
    bool     launchLocked() const;      // true while the update scan is running

    // ── body ────────────────────────────────────────────────────────────────
    QWidget* buildCenterColumn();
    QWidget* buildCreateCards();
    QWidget* buildRecentPanel();
    QWidget* buildTemplatesPanel();
    QWidget* buildRightColumn();
    QWidget* buildToolsCard();
    QWidget* buildAiCard();
    QWidget* buildQuickTips();

    // ── actions ─────────────────────────────────────────────────────────────
    void openFileDialog();
    void showTemplateMarket(int category = 0);
    void showSettingsDialog();
    void showProfileTray();
    void showPlanPopup(QWidget* anchor);                // sidebar "Free plan" row
    void showNotificationsPopup(QWidget* anchor);
    void showShortcutsDialog();
    void showWhatsNewDialog();
    void showRecentFileMenu(const QString& path, const QPoint& at);
    void refreshRecentPanel();
    void openRecycleBin();

    AppController* m_controller   { nullptr };
    SettingsTray*  m_settingsTray { nullptr };

    // Update banner widgets (owned by the layout).
    QWidget* m_updateBanner { nullptr };
    QLabel*  m_updateSpin   { nullptr };
    QLabel*  m_updateText   { nullptr };
    QTimer*  m_spinTimer    { nullptr };
    int      m_spinPhase    { 0 };

    // Notifications popup (bell): tracked so the bell toggles it cleanly.
    QPointer<QWidget> m_notifPopup;
    qint64            m_notifClosedMs { 0 };

    // Global search.
    QLineEdit*             m_search      { nullptr };
    QPointer<SearchPopup>  m_searchPopup;

    HeroBanner*  m_hero        { nullptr };
    // Tracked so a narrow page can drop them rather than be clipped.
    QWidget*     m_rightColumn { nullptr };
    QWidget*     m_trustLocal   { nullptr };
    QWidget*     m_trustPrivate { nullptr };

    // Recent panel, kept so a star / delete can refresh it in place.
    QHBoxLayout* m_recentRowLayout { nullptr };
    QWidget*     m_recentPanel     { nullptr };
};

} // namespace NativeOffice
