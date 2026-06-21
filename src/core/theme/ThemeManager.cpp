// ─────────────────────────────────────────────────────────────────────────────
// ThemeManager.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "ThemeManager.h"

#include <QStringBuilder>

namespace NativeOffice {

ThemeManager& ThemeManager::instance() {
    static ThemeManager inst;
    return inst;
}

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent)
    , m_theme{}
{}

QString ThemeManager::cssColor(const QColor& c) {
    return c.name(QColor::HexRgb);
}

QString ThemeManager::applicationStyleSheet() const {
    const Theme& t = m_theme;

    return QString(R"(
/* ── Global ──────────────────────────────────────────────────── */
* {
    font-family: "Segoe UI", "Inter", "SF Pro Display", sans-serif;
    outline: none;
}

QMainWindow, QDialog {
    background-color: %1;
}

QWidget {
    background-color: transparent;
    color: %2;
}

/* ── Dialogs & message boxes ──────────────────────────────────── */
/* A globally-transparent QWidget background renders as black on a
   top-level window, which made native QMessageBox text unreadable.
   Give dialogs a solid surface and explicit, high-contrast text. */
QMessageBox, QFileDialog {
    background-color: %9;
}
QDialog QLabel {
    background-color: transparent;
    color: %2;
    font-size: 13px;
}
QDialog QPushButton {
    background-color: %9;
    color: %2;
    border: 1px solid %3;
    border-radius: 6px;
    padding: 6px 18px;
    min-width: 78px;
    font-size: 13px;
    font-weight: 500;
}
QDialog QPushButton:hover {
    background-color: %1;
    border-color: %4;
}
QDialog QPushButton:pressed {
    background-color: %3;
}
QDialog QPushButton:default {
    background-color: %10;
    color: %7;
    border-color: %10;
    font-weight: 600;
}
QDialog QPushButton:default:hover {
    background-color: %11;
    border-color: %11;
}

/* ── Scrollbars ──────────────────────────────────────────────── */
QScrollBar:vertical {
    background: %3;
    width: 8px;
    border-radius: 4px;
}
QScrollBar::handle:vertical {
    background: %4;
    border-radius: 4px;
    min-height: 24px;
}
QScrollBar::handle:vertical:hover {
    background: %5;
}
QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical {
    height: 0;
}

QScrollBar:horizontal {
    background: %3;
    height: 8px;
    border-radius: 4px;
}
QScrollBar::handle:horizontal {
    background: %4;
    border-radius: 4px;
    min-width: 24px;
}

/* ── Tooltips ────────────────────────────────────────────────── */
QToolTip {
    background-color: %6;
    color: %7;
    border: 1px solid %8;
    border-radius: 6px;
    padding: 6px 10px;
    font-size: 12px;
}

/* ── Menus (menu-bar dropdowns) ───────────────────────────────── */
/* Popup menus are separate top-level windows; without an explicit
   surface the globally-transparent QWidget rule renders them black. */
QMenu {
    background-color: #FFFFFF;
    color: #1C1E26;
    border: 1px solid #D5D8DF;
    border-radius: 8px;
    padding: 6px;
    font-size: 13px;
}
QMenu::item {
    background: transparent;
    padding: 6px 28px 6px 18px;
    border-radius: 6px;
    margin: 1px 4px;
}
QMenu::item:selected {
    background-color: #FCE4E2;
    color: #C0271C;
}
QMenu::item:disabled {
    color: #B0B4BD;
}
QMenu::separator {
    height: 1px;
    background: #E2E4E9;
    margin: 5px 10px;
}
QMenu::icon {
    padding-left: 8px;
}
)")
    .arg(cssColor(t.background))    // %1 app background
    .arg(cssColor(t.textPrimary))   // %2 primary text
    .arg(cssColor(t.border))        // %3 scrollbar track
    .arg(cssColor(t.textMuted))     // %4 scrollbar handle
    .arg(cssColor(t.textSecondary)) // %5 scrollbar handle hover
    .arg(cssColor(t.accent))        // %6 tooltip bg
    .arg(cssColor(t.textOnDark))    // %7 tooltip text
    .arg(cssColor(t.borderFocus))   // %8 tooltip border
    .arg(cssColor(t.surface))       // %9  dialog / button surface (white)
    .arg(cssColor(t.secondary))     // %10 default button (scarlet)
    .arg(cssColor(t.accentLight));  // %11 default button hover
}

} // namespace NativeOffice
