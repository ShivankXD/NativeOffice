#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SettingsDialog.h — the app-wide settings window.
//
// A resizable, sectioned window (sidebar + pages) replacing the old cramped
// single-form dialog:
//   • Account — profile summary (photo, name, email, plan, join date) with
//     browser links for everything editable (profile edit, premium, keys) and
//     Sign Out. Profile editing itself lives on nativeoffice.online only.
//   • General — startup behaviour.
//   • Writer  — editing defaults (spell check, autocorrect, autosave, zoom,
//     rulers), persisted in QSettings and read by the modules.
// ─────────────────────────────────────────────────────────────────────────────

#include <QDialog>

class QCheckBox;
class QListWidget;
class QSpinBox;
class QStackedWidget;

namespace NativeOffice {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private:
    QWidget* buildAccountPage();
    QWidget* buildGeneralPage();
    QWidget* buildWriterPage();
    void     save();

    QListWidget*    m_nav   { nullptr };
    QStackedWidget* m_pages { nullptr };

    QCheckBox* m_splashChk   { nullptr };
    QCheckBox* m_spellChk    { nullptr };
    QCheckBox* m_autoChk     { nullptr };
    QSpinBox*  m_autosaveSpin{ nullptr };
    QSpinBox*  m_zoomSpin    { nullptr };
    QCheckBox* m_rulersChk   { nullptr };
};

} // namespace NativeOffice
