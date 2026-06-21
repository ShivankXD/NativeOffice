#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ImpressModule.h  (Sprint 8 → Sprint 12)
// NativeOffice Presentation Tool — full ribbon-driven slide editor.
//
//  ┌────────────────────────────────────────────────────────────────┐
//  │  ImpressRibbon  (tabs: Home/Insert/Design/Transitions/...)      │
//  ├────────────┬───────────────────────────────────────────────────┤
//  │  Slides /  │          Center Canvas (QGraphicsView)             │
//  │  Outline   │   ┌──────────────────────────────────────────┐    │
//  │  tabs      │   │  SlideScene  960×540 white surface        │    │
//  │  (200 px)  │   └──────────────────────────────────────────┘    │
//  │            │          Notes panel (QTextEdit)                  │
//  ├────────────┴───────────────────────────────────────────────────┤
//  │  Status bar: slide n/total · theme · view buttons · zoom        │
//  └────────────────────────────────────────────────────────────────┘
//
// Sprint 12 additions:
//   • QUndoStack-backed undo/redo (whole-deck JSON snapshots)
//   • Ribbon replaces the old single-row toolbar
//   • Slides/Outline tabs, notes panel, status bar
//   • Slide layouts + transitions, image insertion
//   • Slide Show present mode
// ─────────────────────────────────────────────────────────────────────────────

#include "SlideData.h"
#include "SlideScene.h"

#include <QWidget>
#include <QGraphicsView>
#include <vector>

class QUndoStack;
class QTextEdit;
class QSplitter;
class QJsonObject;
class QTabWidget;
class QTimer;

namespace NativeOffice {

class ImpressRibbon;
class SlidePanelWidget;
class OutlineWidget;
class ImpressStatusBar;
class DeckSnapshotCommand;

enum class ImpressViewMode { Normal, Outline, SlideSorter };

class ImpressModule : public QWidget {
    Q_OBJECT

public:
    explicit ImpressModule(QWidget* parent = nullptr);

    [[nodiscard]] int  slideCount()    const noexcept;
    [[nodiscard]] int  currentSlide()  const noexcept { return m_currentIdx; }

    // ── File state ──────────────────────────────────────────────────────
    [[nodiscard]] QString currentFilePath() const noexcept { return m_currentPath; }
    [[nodiscard]] bool    isDirty()         const noexcept { return m_dirty; }
    [[nodiscard]] QString titleString()     const;

    // ── File I/O ────────────────────────────────────────────────────────
    bool saveToPath(const QString& path);
    bool loadFromPath(const QString& path);
    void markClean();

    [[nodiscard]] QUndoStack* undoStack() const noexcept { return m_undoStack; }

public slots:
    void addNewSlide();
    void switchToSlide(int index);
    void duplicateCurrentSlide();
    void deleteCurrentSlide();
    void moveSlide(int fromIndex, int toIndex);

    void exportToPdf();
    void exportToPptx();
    // Write the deck to a PowerPoint .pptx at `path` (no dialogs). Returns
    // false on a write failure. exportToPptx() is the interactive wrapper.
    bool exportPptxTo(const QString& path);
    void insertImageFromFile();
    void applyLayoutToCurrentSlide(SlideLayout layout);
    void applyTransitionToCurrentSlide(SlideTransition transition);
    void applyTransitionToAllSlides(SlideTransition transition);
    void applyAnimationToSelection(ItemAnimation anim);
    void setZoomPercent(int percent);
    void setViewMode(ImpressViewMode mode);
    void startSlideShow();

    void undo();
    void redo();

signals:
    void documentModified();
    void filePathChanged(const QString& newPath);

private:
    void buildUi();
    QWidget* buildBrandBar();
    void insertPresetText(const QString& text, double fontSize, bool bold, const QColor& color);
    void applyStyles();
    void createSlide(const SlideData& data);   // low-level: allocates scene + thumb
    void clearDeck();                           // remove all slides
    void refitView();                           // fit slide to viewport at current zoom

    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

    void syncRibbonToSelection();
    [[nodiscard]] int indexOfScene(SlideScene* scene) const;

    // ── Snapshot-based undo/redo ────────────────────────────────────────
    void syncCurrentSceneToData();
    QJsonObject deckToJson() const;
    void deckFromJson(const QJsonObject& root);
    void captureUndoBaseline();
    void commitUndoStep();

    friend class DeckSnapshotCommand;

    ImpressRibbon*    m_ribbon    { nullptr };
    QTabWidget*       m_leftTabs  { nullptr };
    SlidePanelWidget* m_slidePanel{ nullptr };
    OutlineWidget*    m_outline   { nullptr };
    QGraphicsView*    m_view      { nullptr };
    QTextEdit*        m_notesEdit { nullptr };
    ImpressStatusBar* m_statusBar { nullptr };
    QSplitter*        m_canvasSplitter { nullptr };

    // Deck: parallel arrays (scenes own their data; we hold pointers)
    std::vector<SlideScene*> m_scenes;
    std::vector<SlideData>   m_slideData;
    int                      m_currentIdx { -1 };

    // ── File state ──────────────────────────────────────────────────────
    QString m_currentPath;
    bool    m_dirty        { false };
    bool    m_ignoreChange { false };

    // ── Undo/redo ───────────────────────────────────────────────────────
    QUndoStack* m_undoStack    { nullptr };
    QString     m_undoBaseline;
    int         m_undoBaselineIdx { 0 };
    bool        m_restoringUndo { false };
    QTimer*     m_undoDebounce { nullptr };

    bool m_notesVisible { true };
    int  m_zoomPercent  { 100 };   // 100% == fit-to-window
};

} // namespace NativeOffice
