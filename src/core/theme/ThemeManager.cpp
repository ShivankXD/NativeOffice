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
)")
    .arg(cssColor(t.background))    // %1 app background
    .arg(cssColor(t.textPrimary))   // %2 primary text
    .arg(cssColor(t.border))        // %3 scrollbar track
    .arg(cssColor(t.textMuted))     // %4 scrollbar handle
    .arg(cssColor(t.textSecondary)) // %5 scrollbar handle hover
    .arg(cssColor(t.accent))        // %6 tooltip bg
    .arg(cssColor(t.textOnDark))    // %7 tooltip text
    .arg(cssColor(t.borderFocus));  // %8 tooltip border
}

} // namespace NativeOffice
