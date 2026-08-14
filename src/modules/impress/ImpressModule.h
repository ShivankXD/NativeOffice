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
#include <QPixmap>
#include <functional>
#include <vector>

class QUndoStack;
class QTextEdit;
class QSplitter;
class QJsonObject;
class QTabWidget;
class QTimer;
class QListWidget;
class QLineEdit;
class QLabel;
class QToolButton;
class QVariantAnimation;

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
    // Re-clears the dirty flag across the open-time scene build, so opening a
    // deck never leaves it looking modified (and autosave never rewrites a file
    // the user has not edited).
    void markCleanAfterLoad();

    [[nodiscard]] QUndoStack* undoStack() const noexcept { return m_undoStack; }

public slots:
    // Replace the deck with a ready-made multi-slide template ("Pitch Deck",
    // "Business Review", …) — used by the Home screen template gallery.
    void applyDeckTemplate(const QString& name);
    void addNewSlide();

    // ── Generated decks (the Stasis assistant) ───────────────────────────────
    // appendSlide adds a slide that was built elsewhere, already populated,
    // rather than the empty placeholder addNewSlide creates. Returns its index.
    // removeTrailingSlides takes the last n back off again, which is how a
    // generated deck is rolled back; unlike deleteCurrentSlide it will happily
    // leave the deck empty, because the state before generating may well have
    // been empty and restoring it exactly is the whole point.
    int  appendSlide(const SlideData& data);
    void removeTrailingSlides(int n);
    void removeSlideAt(int index);
    // True when the deck is still the single empty slide a new presentation
    // opens with: nothing typed, nothing added. Generating into one of those
    // should replace it rather than leave it sitting in front of the result.
    [[nodiscard]] bool deckIsPristine() const;
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
    // Free-plan view-only mode: ribbon disabled, canvas non-interactive (no
    // item edits), notes read-only. Navigation and slide show stay available.
    void setReadOnly(bool on);
    void startSlideShow();
    void startPresenterView();

    void undo();
    void redo();

signals:
    void documentModified();
    void filePathChanged(const QString& newPath);

private:
    void buildUi();
    void launchShow(bool presenter);
    QWidget* buildBrandBar();
    QWidget* buildCommentsPanel();
    void refreshComments();
    void showTemplatesGallery();
    void applyTemplate(const SlideData& tpl);
    void insertPresetText(const QString& text, double fontSize, bool bold, const QColor& color);
    void applyStyles();
    void createSlide(const SlideData& data);   // low-level: allocates scene + thumb
    void clearDeck();                           // remove all slides
    void refitView();                           // fit slide to viewport at current zoom

    // ── In-editor effect previews (so the user immediately sees the chosen
    //    transition / slide animation / object animation play once) ─────────
    [[nodiscard]] QPixmap renderSceneToPixmap(SlideScene* scene, const QSize& sz) const;
    void runOverlayPreview(int durationMs,
                           const std::function<QPixmap(double)>& frame,
                           QObject* owned = nullptr);
    void previewTransition(SlideTransition transition);
    void previewSlideAnimation(SlideAnimation anim);
    void previewObjectAnimations();
    // Apply a design background colour, honouring an on-slide selection: a
    // selected shape (a colour-division part) is recoloured; otherwise the
    // whole-slide background (the largest portion) is recoloured.
    void applyDesignColor(const QColor& color);

    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

    // True when an arrow key should page between slides: the module is the
    // active editor, focus sits somewhere inside it (or nowhere), and the user
    // is not typing into a text field or editing/selecting a canvas object.
    [[nodiscard]] bool shouldPageSlides() const;

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
    QWidget*          m_commentsPanel { nullptr };
    QListWidget*      m_commentList   { nullptr };
    QLabel*           m_commentEmpty  { nullptr };
    QLineEdit*        m_commentInput  { nullptr };
    bool              m_commentsVisible { false };

    // Deck: parallel arrays (scenes own their data; we hold pointers)
    std::vector<SlideScene*> m_scenes;
    std::vector<SlideData>   m_slideData;
    int                      m_currentIdx { -1 };

    // ── Slide-number field (deck-wide) ──────────────────────────────────────
    bool    m_showSlideNumbers { false };
    QPointF m_slideNumberPos   { -1, -1 };   // invalid = default bottom-right
    void applySlideNumbers();                // push state to every scene + data
    void connectSceneSlideNumber(SlideScene* scene);
    void refreshSlideNumberState();          // adopt toggle/pos from loaded deck

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

    bool m_notesVisible { false };   // speaker notes pane is opt-in (View tab)
    int  m_zoomPercent  { 100 };   // 100% == fit-to-window

    // ── Effect-preview overlay (sits over the canvas during a preview) ────
    QLabel*            m_previewOverlay { nullptr };
    QVariantAnimation* m_previewAnim    { nullptr };
};

} // namespace NativeOffice
