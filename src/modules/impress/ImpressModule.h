#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ImpressModule.h  (Sprint 6)
// NativeOffice Presentation Tool — three-pane layout + PDF export.
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

public slots:
    void addNewSlide();
    void switchToSlide(int index);
    void duplicateCurrentSlide();
    void deleteCurrentSlide();

    // Sprint 6: export every slide as one page in a PDF file
    void exportToPdf();

private:
    void buildUi();
    void applyStyles();
    void createSlide(const SlideData& data);   // low-level: allocates scene + thumb

    void resizeEvent(QResizeEvent* event) override;

    ImpressToolbar*   m_toolbar   { nullptr };
    SlidePanelWidget* m_slidePanel{ nullptr };
    QGraphicsView*    m_view      { nullptr };

    // Deck: parallel arrays (scenes own their data; we hold pointers)
    std::vector<SlideScene*> m_scenes;
    std::vector<SlideData>   m_slideData;
    int                      m_currentIdx { -1 };
};

} // namespace NativeOffice
