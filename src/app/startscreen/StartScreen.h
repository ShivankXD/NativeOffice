#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// StartScreen.h
// The NativeOffice home dashboard — a modern dark-themed launcher (logo + search
// bar, left navigation, create cards, quick actions, recent files, a
// "Templates for You" gallery, and activity/sync panels).
//
// Emits three signals the app wires to AppController:
//   • newDocumentRequested(type) – create a blank doc in Writer/Calc/Impress
//   • fileOpenRequested(path)     – open an existing file
//   • settingsRequested()         – open settings
// ─────────────────────────────────────────────────────────────────────────────

#include "core/application/AppController.h"

#include <QPointer>
#include <QWidget>

class QVBoxLayout;
class QLabel;
class QPushButton;
class QTimer;

namespace NativeOffice {

class SettingsTray;

class StartScreen : public QWidget {
    Q_OBJECT

public:
    explicit StartScreen(AppController* controller, QWidget* parent = nullptr);

signals:
    void newDocumentRequested(DocumentType type);
    void fileOpenRequested(const QString& path);
    void settingsRequested();
    // A named (non-blank) template was chosen in the gallery.
    void templateChosen(DocumentType type, const QString& name);
    // The Image Resizer tool card (right column) was clicked.
    void imageResizerRequested();
    // The Markdown Editor tool card (right column) was clicked.
    void markdownEditorRequested();

private:
    void     buildUi();
    QWidget* buildSidebar();
    QWidget* buildTopBar();
    QWidget* buildUpdateBanner();       // blue "scanning for updates" box
    void     refreshUpdateBanner();     // sync the banner to UpdateChecker state
    bool     launchLocked() const;      // true while the update scan is running
    QWidget* buildCenterColumn();
    QWidget* buildCreateCards();
    QWidget* buildRecentPanel();
    QWidget* buildTemplatesPanel();
    QWidget* buildRightColumn();

    void openFileDialog();
    void showTemplatesDialog(int initialCategory = 0);  // 0 Word, 1 Sheet, 2 Slides
    void showSettingsDialog();                          // real, QSettings-backed
    void showNotificationsPopup(QWidget* anchor);       // bell dropdown
    void showShortcutsDialog();                         // real supported shortcuts
    void showWhatsNewDialog();                          // release highlights

    AppController* m_controller { nullptr };
    SettingsTray*  m_settingsTray { nullptr };   // right-side slide-in settings

    // Update banner widgets (owned by the layout).
    QWidget*     m_updateBanner { nullptr };   // compact top-right status pill
    QLabel*      m_updateSpin   { nullptr };
    QLabel*      m_updateText   { nullptr };
    QTimer*      m_spinTimer    { nullptr };
    int          m_spinPhase    { 0 };

    // Notifications popup (bell): tracked so the bell toggles it cleanly and the
    // dismiss-then-reopen flicker is suppressed.
    QPointer<QWidget> m_notifPopup;
    qint64            m_notifClosedMs { 0 };
};

} // namespace NativeOffice
