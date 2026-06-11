#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ImpressModule.h  (Sprint 8)
// NativeOffice Presentation Tool — three-pane layout + PDF export + file I/O.
//
//  ┌────────────────────────────────────────────────────────────────┐
//  │  ImpressToolbar  (Charcoal, 44 px: T / ☐ / ◯ / Dup / Del)    │
//  ├────────────┬───────────────────────────────────────────────────┤
//  │            │          Center Canvas (QGraphicsView)            │
//  │  Slide     │   ┌──────────────────────────────────────────┐    │
//  │  Panel     │   │  SlideScene  960×540 white surface       │    │
//  │  (200 px   │   │  (scales to fit available width × 16:9) │    │
//  │  Charcoal) │   └──────────────────────────────────────────┘    │
//  │            │                                                   │
//  └────────────┴───────────────────────────────────────────────────┘
//
// Sprint 8 additions:
//   • saveToPath(path)   — writes JSON slide/shape data to .noff file
//   • loadFromPath(path) — reads JSON and recreates the full slide deck
//   • isDirty / markClean / titleString — dirty-state tracking
//   • documentModified / filePathChanged signals
// ─────────────────────────────────────────────────────────────────────────────

#include "SlideData.h"
#include "SlideScene.h"

#include <QWidget>
#include <QGraphicsView>
#include <vector>

namespace NativeOffice {

class ImpressToolbar;
class SlidePanelWidget;

class ImpressModule : public QWidget {
    Q_OBJECT

public:
    explicit ImpressModule(QWidget* parent = nullptr);

    [[nodiscard]] int  slideCount()    const noexcept;
    [[nodiscard]] int  currentSlide()  const noexcept { return m_currentIdx; }

    // ── File state (Sprint 8) ─────────────────────────────────────────────
    [[nodiscard]] QString currentFilePath() const noexcept { return m_currentPath; }
    [[nodiscard]] bool    isDirty()         const noexcept { return m_dirty; }
    [[nodiscard]] QString titleString()     const;

    // ── File I/O (Sprint 8) ───────────────────────────────────────────────
    bool saveToPath(const QString& path);
    bool loadFromPath(const QString& path);
    void markClean();

public slots:
    void addNewSlide();
    void switchToSlide(int index);
    void duplicateCurrentSlide();
    void deleteCurrentSlide();

    // Sprint 6: export every slide as one page in a PDF file
    void exportToPdf();

signals:
    void documentModified();
    void filePathChanged(const QString& newPath);

private:
    void buildUi();
    void applyStyles();
    void createSlide(const SlideData& data);   // low-level: allocates scene + thumb
    void clearDeck();                           // Sprint 8: remove all slides

    void resizeEvent(QResizeEvent* event) override;

    ImpressToolbar*   m_toolbar   { nullptr };
    SlidePanelWidget* m_slidePanel{ nullptr };
    QGraphicsView*    m_view      { nullptr };

    // Deck: parallel arrays (scenes own their data; we hold pointers)
    std::vector<SlideScene*> m_scenes;
    std::vector<SlideData>   m_slideData;
    int                      m_currentIdx { -1 };

    // ── File state (Sprint 8) ─────────────────────────────────────────────
    QString m_currentPath;
    bool    m_dirty        { false };
    bool    m_ignoreChange { false };
};

} // namespace NativeOffice
