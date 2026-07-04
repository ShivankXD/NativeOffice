#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ThemeManager.h
// Central authority for the NativeOffice design system.
// Colors are derived from the official NativeOffice logo:
//   Primary   (#2C3140) – Deep Charcoal (the "N" letterform)
//   Secondary (#E8372A) – Vivid Scarlet  (the "P" letterform)
//   Accent    (#1A1F2E) – Near-Black shadow tone
//   Surface   (#F5F6FA) – Off-white surface
//   Text      (#1C1E26) – Primary text
// ─────────────────────────────────────────────────────────────────────────────

#include <QColor>
#include <QFont>
#include <QObject>
#include <QString>

namespace NativeOffice {

struct Theme {
    // ── Brand palette ──────────────────────────────────────────────────────
    QColor primary       { "#2C3140" };   // Charcoal N
    QColor secondary     { "#E8372A" };   // Scarlet P
    QColor accent        { "#1A1F2E" };   // Near-black
    QColor accentLight   { "#FF5247" };   // Lighter scarlet hover

    // ── Surface / background ───────────────────────────────────────────────
    QColor background    { "#F5F6FA" };   // App background
    QColor surface       { "#FFFFFF" };   // Card / panel white
    QColor sidebarBg     { "#2C3140" };   // Sidebar matches primary
    QColor sidebarHover  { "#3A4054" };   // Sidebar item hover
    QColor sidebarActive { "#E8372A" };   // Active sidebar item

    // ── Text ───────────────────────────────────────────────────────────────
    QColor textPrimary   { "#1C1E26" };
    QColor textSecondary { "#6B7280" };
    QColor textMuted     { "#9CA3AF" };
    QColor textOnDark    { "#FFFFFF" };
    QColor textOnPrimary { "#FFFFFF" };

    // ── State colors ───────────────────────────────────────────────────────
    QColor writerBlue    { "#2563EB" };   // Writer/Docs
    QColor calcGreen     { "#16A34A" };   // Calc/Sheets
    QColor impressOrange { "#EA580C" };   // Impress/Slides

    // ── Border & shadow ────────────────────────────────────────────────────
    QColor border        { "#E5E7EB" };
    QColor borderFocus   { "#E8372A" };
    QColor shadow        { "#0000001A" };

    // ── Radii (in px, encoded as int for convenience) ──────────────────────
    int radiusSmall  = 6;
    int radiusMedium = 10;
    int radiusLarge  = 16;
};

// Chrome-only light/dark mode: affects ribbons, menu bars, status bars,
// sidebars, and the Home page. Document/sheet/slide canvases are
// intentionally excluded — they stay white/paper-like, matching how
// Word/Excel/PowerPoint's own dark mode behaves.
enum class ThemeMode { Light, Dark };

class ThemeManager : public QObject {
    Q_OBJECT

public:
    static ThemeManager& instance();

    [[nodiscard]] const Theme& theme() const noexcept { return m_theme; }

    // Generates a full Qt stylesheet for the application window
    [[nodiscard]] QString applicationStyleSheet() const;

    // Self-contained stylesheet for prompt/input dialogs (QInputDialog and
    // friends). Set it directly on the dialog instance so it overrides any
    // inherited app/module stylesheet — guarantees a light surface with
    // high-contrast, visible text in the input field.
    [[nodiscard]] static QString inputDialogStyleSheet();

    // Helper: build a CSS color string from a QColor
    [[nodiscard]] static QString cssColor(const QColor& c);

    // ── Chrome-only dark mode ──────────────────────────────────────────────
    [[nodiscard]] ThemeMode mode() const noexcept { return m_mode; }
    [[nodiscard]] bool isDark() const noexcept { return m_mode == ThemeMode::Dark; }
    void setMode(ThemeMode mode);
    void toggleMode() { setMode(isDark() ? ThemeMode::Light : ThemeMode::Dark); }

    // Chrome color helpers — return the right hex for the current mode, so
    // call sites (ribbons, status bars, menu bars, Home) don't duplicate
    // light/dark branching themselves.
    [[nodiscard]] QString chromeBg()        const { return isDark() ? "#0D1117" : "#FFFFFF"; }
    [[nodiscard]] QString chromePanelBg()   const { return isDark() ? "#12161F" : "#F3F4F6"; }
    [[nodiscard]] QString chromeActiveBg()  const { return isDark() ? "#17233B" : "#FFFFFF"; }
    [[nodiscard]] QString chromeHoverBg()   const { return isDark() ? "#1E2737" : "#ECEEF2"; }
    [[nodiscard]] QString chromeBorder()    const { return isDark() ? "#1B212C" : "#E2E4E9"; }
    [[nodiscard]] QString chromeText()      const { return isDark() ? "#E6E9F0" : "#2C3140"; }
    [[nodiscard]] QString chromeTextMuted() const { return isDark() ? "#9AA4B8" : "#5A6071"; }

signals:
    void modeChanged(ThemeMode mode);

private:
    explicit ThemeManager(QObject* parent = nullptr);
    ~ThemeManager() override = default;

    ThemeManager(const ThemeManager&)            = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    Theme m_theme;
    ThemeMode m_mode { ThemeMode::Dark };
};

} // namespace NativeOffice
