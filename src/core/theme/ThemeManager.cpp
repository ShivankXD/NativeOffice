// ─────────────────────────────────────────────────────────────────────────────
// ThemeManager.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "ThemeManager.h"

#include <QStringBuilder>
#include <QSettings>
#include <QCoreApplication>
#include <QDialog>
#include <QEvent>

namespace NativeOffice {

ThemeManager& ThemeManager::instance() {
    static ThemeManager inst;
    return inst;
}

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent)
    , m_theme{}
{
    // Chrome is always light. Drop any persisted mode from the retired
    // dark-chrome experiment so a stale "dark" can never resurface.
    QSettings().remove("app/themeMode");
}

void ThemeManager::setMode(ThemeMode mode) {
    if (mode == m_mode) return;
    m_mode = mode;
    emit modeChanged(m_mode);
}

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
/* 8px wide with a 24px minimum handle was reported as too thin to grab
   accurately, especially over a spreadsheet. 13px is still slim next to the
   17px Windows default but is a real hit target, and the handle minimum is
   large enough to stay grabbable in a long sheet. */
QScrollBar:vertical {
    background: %3;
    width: 13px;
    border-radius: 6px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: %4;
    border: 3px solid %3;          /* inset, so the bar reads slim at rest */
    border-radius: 6px;
    min-height: 40px;
}
QScrollBar::handle:vertical:hover {
    background: %5;
    border-width: 2px;             /* thickens under the cursor */
}
QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical {
    height: 0;
}
QScrollBar::add-page:vertical,
QScrollBar::sub-page:vertical {
    background: transparent;
}

QScrollBar:horizontal {
    background: %3;
    height: 13px;
    border-radius: 6px;
    margin: 0;
}
QScrollBar::handle:horizontal {
    background: %4;
    border: 3px solid %3;
    border-radius: 6px;
    min-width: 40px;
}
QScrollBar::handle:horizontal:hover {
    background: %5;
    border-width: 2px;
}
QScrollBar::add-line:horizontal,
QScrollBar::sub-line:horizontal {
    width: 0;
}
QScrollBar::add-page:horizontal,
QScrollBar::sub-page:horizontal {
    background: transparent;
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

QString ThemeManager::inputDialogStyleSheet() {
    // Every colour here is stated explicitly, including the ones that look
    // redundant. Beta testers on Windows dark mode reported input fields whose
    // text matched their background across all four modules: any property left
    // unstated falls through to the platform palette, which is dark on those
    // machines while these surfaces are white.
    return QStringLiteral(R"(
QInputDialog, QDialog, QMessageBox {
    background-color: #FFFFFF;
}
QDialog > QWidget, QMessageBox > QWidget {
    background-color: transparent;
}
QLabel {
    background: transparent;
    color: #1C1E26;
    font-size: 13px;
}
QGroupBox {
    background: transparent;
    color: #1C1E26;
    border: 1px solid #E2E4E9;
    border-radius: 8px;
    margin-top: 10px;
    padding-top: 10px;
    font-size: 13px;
    font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 4px;
    color: #1C1E26;
}
QLineEdit, QPlainTextEdit, QTextEdit, QTextBrowser,
QSpinBox, QDoubleSpinBox, QComboBox, QDateEdit, QTimeEdit, QDateTimeEdit {
    background-color: #FFFFFF;
    color: #1C1E26;
    border: 1px solid #D5D8DF;
    border-radius: 6px;
    padding: 6px 8px;
    font-size: 13px;
    selection-background-color: #E8372A;
    selection-color: #FFFFFF;
}
QLineEdit:disabled, QPlainTextEdit:disabled, QTextEdit:disabled,
QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled {
    background-color: #F2F3F6;
    color: #9CA3AF;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus,
QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
    border: 1px solid #E8372A;
}
/* Spin buttons inherit the platform palette unless painted here, which is
   how the Custom Page Size arrows ended up dark-on-dark. */
QSpinBox::up-button, QDoubleSpinBox::up-button,
QDateEdit::up-button, QTimeEdit::up-button, QDateTimeEdit::up-button {
    subcontrol-origin: border;
    subcontrol-position: top right;
    width: 18px;
    background-color: #F5F6FA;
    border-left: 1px solid #D5D8DF;
    border-top-right-radius: 6px;
}
QSpinBox::down-button, QDoubleSpinBox::down-button,
QDateEdit::down-button, QTimeEdit::down-button, QDateTimeEdit::down-button {
    subcontrol-origin: border;
    subcontrol-position: bottom right;
    width: 18px;
    background-color: #F5F6FA;
    border-left: 1px solid #D5D8DF;
    border-bottom-right-radius: 6px;
}
QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover {
    background-color: #ECEEF2;
}
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow,
QDateEdit::up-arrow, QTimeEdit::up-arrow, QDateTimeEdit::up-arrow {
    image: none;
    width: 0; height: 0;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-bottom: 5px solid #4A5060;
}
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow,
QDateEdit::down-arrow, QTimeEdit::down-arrow, QDateTimeEdit::down-arrow {
    image: none;
    width: 0; height: 0;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid #4A5060;
}
QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: center right;
    width: 20px;
    border: none;
    background: transparent;
}
QComboBox::down-arrow {
    image: none;
    width: 0; height: 0;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid #4A5060;
    margin-right: 7px;
}
/* The popup is a separate top-level window and does NOT inherit the rules
   above, so it needs its own full set. */
QComboBox QAbstractItemView {
    background-color: #FFFFFF;
    color: #1C1E26;
    border: 1px solid #D5D8DF;
    border-radius: 6px;
    padding: 4px;
    outline: none;
    selection-background-color: #FCE4E2;
    selection-color: #C0271C;
}
QComboBox QAbstractItemView::item {
    background: transparent;
    color: #1C1E26;
    min-height: 22px;
    padding: 3px 8px;
    border-radius: 4px;
}
QComboBox QAbstractItemView::item:selected,
QComboBox QAbstractItemView::item:hover {
    background-color: #FCE4E2;
    color: #C0271C;
}
QListView, QListWidget, QTreeView, QTreeWidget, QTableView, QTableWidget {
    background-color: #FFFFFF;
    alternate-background-color: #FAFBFD;
    color: #1C1E26;
    border: 1px solid #D5D8DF;
    border-radius: 6px;
    outline: none;
    font-size: 13px;
}
QListView::item, QListWidget::item, QTreeView::item, QTreeWidget::item {
    color: #1C1E26;
    padding: 4px 6px;
}
QListView::item:selected, QListWidget::item:selected,
QTreeView::item:selected, QTreeWidget::item:selected,
QTableView::item:selected {
    background-color: #FCE4E2;
    color: #C0271C;
}
QHeaderView::section {
    background-color: #F5F6FA;
    color: #1C1E26;
    border: none;
    border-right: 1px solid #E2E4E9;
    border-bottom: 1px solid #E2E4E9;
    padding: 4px 8px;
    font-size: 12px;
    font-weight: 600;
}
QCheckBox, QRadioButton {
    background: transparent;
    color: #1C1E26;
    font-size: 13px;
    spacing: 8px;
}
QCheckBox::indicator, QRadioButton::indicator {
    width: 16px;
    height: 16px;
    background-color: #FFFFFF;
    border: 1px solid #B9BEC9;
}
QCheckBox::indicator { border-radius: 4px; }
QRadioButton::indicator { border-radius: 9px; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover {
    border-color: #E8372A;
}
QCheckBox::indicator:checked {
    background-color: #E8372A;
    border-color: #E8372A;
    image: url(:/assets/check-white.png);
}
QRadioButton::indicator:checked {
    background-color: #FFFFFF;
    border: 5px solid #E8372A;
}
QTabWidget::pane {
    border: 1px solid #E2E4E9;
    border-radius: 8px;
    background: #FFFFFF;
}
QTabBar::tab {
    background: transparent;
    color: #5A6071;
    padding: 7px 14px;
    font-size: 13px;
}
QTabBar::tab:selected {
    color: #1C1E26;
    border-bottom: 2px solid #E8372A;
}
QSlider::groove:horizontal {
    height: 4px;
    background: #E2E4E9;
    border-radius: 2px;
}
QSlider::handle:horizontal {
    width: 14px;
    margin: -6px 0;
    background: #E8372A;
    border-radius: 7px;
}
QPushButton {
    background-color: #FFFFFF;
    color: #1C1E26;
    border: 1px solid #D5D8DF;
    border-radius: 6px;
    padding: 6px 18px;
    min-width: 84px;
    font-size: 13px;
    font-weight: 500;
}
QPushButton:hover {
    background-color: #F5F6FA;
    border-color: #E8372A;
}
QPushButton:disabled {
    background-color: #F2F3F6;
    color: #A8ADB8;
    border-color: #E2E4E9;
}
QPushButton:default {
    background-color: #E8372A;
    color: #FFFFFF;
    border-color: #E8372A;
    font-weight: 600;
}
QPushButton:default:hover {
    background-color: #FF5247;
    border-color: #FF5247;
}
)");
}

// ─────────────────────────────────────────────────────────────────────────────
// Dialog style guard
// ─────────────────────────────────────────────────────────────────────────────
// Ad-hoc dialogs (QInputDialog::getDouble, QMessageBox, one-off QDialogs) are
// scattered across all four modules and none of them carried a stylesheet, so
// they fell through to the platform palette. On a machine running Windows in
// dark mode that produced dark text on dark input fields, which testers hit in
// Writer, Calc, Impress and the PDF editor alike.
//
// Rather than patch 150-odd call sites, stamp the dialog sheet on at show time.
// Dialogs that style themselves are left alone, so bespoke panels keep their look.
namespace {

class DialogStyleGuard final : public QObject {
public:
    explicit DialogStyleGuard(QObject* parent) : QObject(parent) {}

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (ev->type() == QEvent::Show) {
            if (auto* dlg = qobject_cast<QDialog*>(obj)) {
                if (dlg->styleSheet().isEmpty())
                    dlg->setStyleSheet(ThemeManager::inputDialogStyleSheet());
            }
        }
        return QObject::eventFilter(obj, ev);
    }
};

} // namespace

void ThemeManager::installDialogStyleGuard(QCoreApplication* app) {
    if (!app) return;
    app->installEventFilter(new DialogStyleGuard(app));
}

} // namespace NativeOffice
