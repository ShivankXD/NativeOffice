#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// WriterStatusBar.h  (Sprint 14)
// Bottom status bar for the NativeOffice Writer, WPS/Word-style:
//   • Page X / Y  (live)
//   • Word count  (live)
//   • AI Spell Check pill toggle (UI only — no backend)
//   • Page view toggle buttons (Print Layout / Web Layout)
//   • Zoom slider (75%–200%) on the far right
// ─────────────────────────────────────────────────────────────────────────────

#include <QWidget>

class QLabel;
class QSlider;
class QToolButton;

namespace NativeOffice {

class WriterStatusBar : public QWidget {
    Q_OBJECT

public:
    explicit WriterStatusBar(QWidget* parent = nullptr);

    void setPageInfo(int current, int total);
    void setWordCount(int words);
    void setZoomPercent(int percent);   // updates slider/label without re-emitting

signals:
    void zoomChanged(int percent);
    // true  → web layout (full-width, no page),
    // false → print layout (paged A4 view).
    void webLayoutToggled(bool web);

private:
    QLabel*      m_pageLabel  { nullptr };
    QLabel*      m_wordLabel  { nullptr };
    QToolButton* m_spellPill  { nullptr };
    QToolButton* m_btnPrint   { nullptr };
    QToolButton* m_btnWeb     { nullptr };
    QSlider*     m_zoomSlider { nullptr };
    QLabel*      m_zoomLabel  { nullptr };
};

} // namespace NativeOffice
