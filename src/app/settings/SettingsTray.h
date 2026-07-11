#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SettingsTray.h — the Settings/Account panel as a right-side slide-in tray.
//
// Replaces the old centered modal SettingsDialog with a website-style tray: it
// overlays the home screen, slides in from the right edge over the content, and
// dismisses (slides back out) when you click anywhere off it, hit its close
// button, or press Esc. It shows the account summary (plan, member since,
// occupation, browser links) plus the General/Writer editing defaults, which
// auto-save as you change them.
//
// It is a child overlay of the widget it should cover (the StartScreen), sized
// to fill it; the actual panel is a docked child on the right.
// ─────────────────────────────────────────────────────────────────────────────

#include <QWidget>

class QFrame;
class QPropertyAnimation;

namespace NativeOffice {

class SettingsTray : public QWidget {
    Q_OBJECT
public:
    explicit SettingsTray(QWidget* parent);

    void openTray();     // resize to the parent, slide the panel in
    void closeTray();    // slide the panel out, then hide

protected:
    void paintEvent(QPaintEvent*) override;         // dimmed scrim behind the panel
    void mousePressEvent(QMouseEvent*) override;    // click off the panel → dismiss
    void keyPressEvent(QKeyEvent*) override;        // Esc → dismiss
    void resizeEvent(QResizeEvent*) override;       // keep the panel docked right
    bool eventFilter(QObject*, QEvent*) override;   // follow the parent's resizes

private:
    QWidget* buildContent();
    int      panelX(bool opened) const;             // docked vs off-screen x

    QFrame*             m_panel   { nullptr };
    QPropertyAnimation* m_anim    { nullptr };
    int                 m_panelW  { 400 };
    bool                m_open     { false };
};

} // namespace NativeOffice
