#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ImpressStatusBar.h  (Sprint 12)
// Thin bottom bar: slide n/total, theme/layout name, view-mode buttons, zoom.
// ─────────────────────────────────────────────────────────────────────────────

#include <QWidget>

class QLabel;
class QSlider;
class QToolButton;

namespace NativeOffice {

enum class ImpressViewMode;

class ImpressStatusBar : public QWidget {
    Q_OBJECT

public:
    explicit ImpressStatusBar(QWidget* parent = nullptr);

    void setSlideInfo(int currentIndex, int total);
    void setThemeName(const QString& name);
    void setZoomPercent(int percent);   // updates slider/label without re-emitting
    void setViewMode(ImpressViewMode mode);

signals:
    void zoomChanged(int percent);
    void viewModeChanged(ImpressViewMode mode);
    void commentToggleRequested();

private:
    void applyTheme();

    QLabel*      m_slideLabel { nullptr };
    QLabel*      m_themeLabel { nullptr };
    QSlider*     m_zoomSlider { nullptr };
    QLabel*      m_zoomLabel  { nullptr };
    QToolButton* m_btnComment { nullptr };
    QToolButton* m_btnNormal  { nullptr };
    QToolButton* m_btnOutline { nullptr };
    QToolButton* m_btnSorter  { nullptr };
};

} // namespace NativeOffice
