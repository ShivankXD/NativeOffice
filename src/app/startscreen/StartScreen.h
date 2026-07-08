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

#include <QWidget>

class QVBoxLayout;

namespace NativeOffice {

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

private:
    void     buildUi();
    QWidget* buildSidebar();
    QWidget* buildTopBar();
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
};

} // namespace NativeOffice
