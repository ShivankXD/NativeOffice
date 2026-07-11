// ─────────────────────────────────────────────────────────────────────────────
// ImpressModule.cpp  (Sprint 8 → Sprint 12)
// Full NativeOffice Presentation Tool: ribbon-driven editor with undo/redo,
// Slides/Outline panel, notes panel, status bar, slide show mode, and
// JSON-based file persistence (.noff, schema v2).
// ─────────────────────────────────────────────────────────────────────────────
#include "ImpressModule.h"
#include "ImpressRibbon.h"
#include "ImpressStatusBar.h"
#include "SlidePanelWidget.h"
#include "OutlineWidget.h"
#include "SlideScene.h"
#include "PresentModeWindow.h"
#include "PptxImport.h"
#include "core/theme/ThemeManager.h"
#include "core/common/BrandBar.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QVBoxLayout>
#include <QHash>
#include <QGraphicsView>
#include <QGraphicsTextItem>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTextBlockFormat>
#include <QTextListFormat>
#include <QFrame>
#include <QSplitter>
#include <QTextEdit>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QInputDialog>
#include <QDialog>
#include <QScrollArea>
#include <QGridLayout>
#include <QToolButton>
#include <QShortcut>
#include <QIcon>
#include <QPixmap>
#include <QSizePolicy>
#include <QMessageBox>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QFileDialog>
#include <QDir>
#include <QPageSize>
#include <QUndoStack>
#include <QUndoCommand>
#include <QTimer>
#include <QVariantAnimation>
#include <QAbstractAnimation>
#include <QPolygon>
#include <algorithm>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QBuffer>
#include <QImage>
#include <QTabWidget>
#include <QDate>
#include <functional>

namespace NativeOffice {

// ─────────────────────────────────────────────────────────────────────────────
// DeckSnapshotCommand — whole-deck JSON snapshot used for undo/redo. Simpler
// and far less error-prone than per-operation commands, and cheap enough
// since a deck's JSON is small.
// ─────────────────────────────────────────────────────────────────────────────
class DeckSnapshotCommand : public QUndoCommand {
public:
    DeckSnapshotCommand(ImpressModule* module, QJsonObject before, QJsonObject after,
                         int beforeIdx, int afterIdx)
        : m_module(module), m_before(std::move(before)), m_after(std::move(after))
        , m_beforeIdx(beforeIdx), m_afterIdx(afterIdx) {}

    void undo() override {
        m_module->m_restoringUndo = true;
        m_module->deckFromJson(m_before);
        const int idx = qBound(0, m_beforeIdx, m_module->slideCount() - 1);
        m_module->switchToSlide(idx);
        m_module->m_undoBaseline = QJsonDocument(m_before).toJson(QJsonDocument::Compact);
        // Keep the baseline slide index in step with the restored state, or the
        // next edit records a stale "before" index and a later undo jumps to the
        // wrong slide.
        m_module->m_undoBaselineIdx = idx;
        m_module->m_restoringUndo = false;
    }
    void redo() override {
        // QUndoStack::push() calls redo() once immediately. At that point the
        // edit is ALREADY reflected in the live scene, so tearing the whole
        // deck down and rebuilding it (deckFromJson) is both wasteful and
        // unsafe — it frees the very SlideScene the caller is still using,
        // which corrupts the heap. Skip that first, redundant rebuild; only
        // rebuild on a genuine redo that follows an undo.
        if (m_skipInitialRedo) {
            m_skipInitialRedo = false;
            return;
        }
        m_module->m_restoringUndo = true;
        m_module->deckFromJson(m_after);
        const int idx = qBound(0, m_afterIdx, m_module->slideCount() - 1);
        m_module->switchToSlide(idx);
        m_module->m_undoBaseline = QJsonDocument(m_after).toJson(QJsonDocument::Compact);
        m_module->m_undoBaselineIdx = idx;
        m_module->m_restoringUndo = false;
    }

private:
    ImpressModule* m_module;
    QJsonObject m_before, m_after;
    int m_beforeIdx, m_afterIdx;
    bool m_skipInitialRedo { true };   // the change is already live on push
};

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────
ImpressModule::ImpressModule(QWidget* parent)
    : QWidget(parent)
{
    m_undoStack = new QUndoStack(this);

    m_undoDebounce = new QTimer(this);
    m_undoDebounce->setSingleShot(true);
    m_undoDebounce->setInterval(600);
    connect(m_undoDebounce, &QTimer::timeout, this, &ImpressModule::commitUndoStep);

    buildUi();
    applyStyles();
    setObjectName("impressModule");

    // Dev-only capture hook (same pattern as PdfModule): PrintWindow-based
    // external captures drop drawPixmap content on this machine, so
    // verification screenshots come from Qt's own render path instead.
    // Set NATIVEOFFICE_IMPRESS_GRAB to a .png path; a second grab with the
    // comments panel open lands next to it as <path>.comments.png.
    if (qEnvironmentVariableIsSet("NATIVEOFFICE_IMPRESS_GRAB")) {
        const QString grabPath = qEnvironmentVariable("NATIVEOFFICE_IMPRESS_GRAB");
        QTimer::singleShot(6000, this, [this, grabPath] {
            grab().save(grabPath, "PNG");
            m_commentsVisible = true;
            m_commentsPanel->setVisible(true);
            refreshComments();
            QTimer::singleShot(500, this, [this, grabPath] {
                grab().save(grabPath + ".comments.png", "PNG");
            });
        });
    }

    // Create the first slide automatically
    addNewSlide();
    captureUndoBaseline();
    // Creating the initial slide is not a user edit — start with a clean undo
    // history so the Undo button is disabled until the user actually changes
    // something (and so undo can never empty the whole deck).
    m_undoStack->clear();

    // …and clean the dirty flag: a freshly-created deck has no unsaved user
    // edits, so the tab must show no "*" and closing it must not prompt to save.
    QTimer::singleShot(0, this, [this]{
        if (m_currentPath.isEmpty()) { m_dirty = false; emit documentModified(); }
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// UI Construction
// ─────────────────────────────────────────────────────────────────────────────
void ImpressModule::buildUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ── Brand tray ─────────────────────────────────────────────────────────
    // A small, semi-transparent white "tray" pinned top-left that carries the
    // NativeOffice logo and tagline. Replaces the old flat dark hero strip.
    rootLayout->addWidget(buildBrandBar());

    // ── Ribbon ────────────────────────────────────────────────────────────
    m_ribbon = new ImpressRibbon(this);

    // ── Body row: left tabs + canvas/notes splitter ─────────────────────────
    auto* bodyWidget = new QWidget(this);
    bodyWidget->setObjectName("impressBody");
    auto* bodyLayout = new QHBoxLayout(bodyWidget);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    // Left: Slides / Outline tabs
    m_leftTabs = new QTabWidget(bodyWidget);
    m_leftTabs->setObjectName("impressLeftTabs");
    m_leftTabs->setFixedWidth(200);
    m_leftTabs->setTabPosition(QTabWidget::South);

    m_slidePanel = new SlidePanelWidget(m_leftTabs);
    m_outline    = new OutlineWidget(m_leftTabs);
    m_leftTabs->addTab(m_slidePanel, "Slides");
    m_leftTabs->addTab(m_outline,    "Outline");

    // Center: canvas + notes splitter
    m_canvasSplitter = new QSplitter(Qt::Vertical, bodyWidget);
    m_canvasSplitter->setObjectName("impressSplitter");
    m_canvasSplitter->setHandleWidth(4);

    m_view = new QGraphicsView(m_canvasSplitter);
    m_view->setObjectName("impressCanvas");
    m_view->setAlignment(Qt::AlignCenter);
    m_view->setRenderHints(QPainter::Antialiasing |
                           QPainter::SmoothPixmapTransform |
                           QPainter::TextAntialiasing);
    m_view->setDragMode(QGraphicsView::RubberBandDrag);
    m_view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_view->setFrameShape(QFrame::NoFrame);
    // Re-fit the slide whenever the viewport changes size for any reason
    // (window resize, splitter drag, notes toggle, tab switch).
    m_view->viewport()->installEventFilter(this);

    // Speaker notes live in a hidden-by-default pane (View → Speaker Notes);
    // the everyday review flow is the Comments panel on the status tray.
    m_notesEdit = new QTextEdit(m_canvasSplitter);
    m_notesEdit->setObjectName("impressNotes");
    m_notesEdit->setPlaceholderText("Speaker notes…");
    m_notesEdit->setVisible(false);

    m_canvasSplitter->addWidget(m_view);
    m_canvasSplitter->addWidget(m_notesEdit);
    m_canvasSplitter->setStretchFactor(0, 5);
    m_canvasSplitter->setStretchFactor(1, 1);
    m_canvasSplitter->setSizes({ 760, 150 });
    m_canvasSplitter->setCollapsible(0, false);

    // Thin separator between panel and canvas
    auto* sep = new QFrame(bodyWidget);
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedWidth(1);
    sep->setStyleSheet("background: #D7DAE0; border: none;");

    bodyLayout->addWidget(m_leftTabs);
    bodyLayout->addWidget(sep);
    bodyLayout->addWidget(m_canvasSplitter, 1);
    bodyLayout->addWidget(buildCommentsPanel());

    // ── Status bar ───────────────────────────────────────────────────────
    m_statusBar = new ImpressStatusBar(this);

    rootLayout->addWidget(m_ribbon);
    rootLayout->addWidget(bodyWidget, 1);
    rootLayout->addWidget(m_statusBar);

    // ── Wire ribbon signals ─────────────────────────────────────────────
    connect(m_ribbon, &ImpressRibbon::undoRequested, this, &ImpressModule::undo);
    connect(m_ribbon, &ImpressRibbon::redoRequested, this, &ImpressModule::redo);

    // Undo/redo keyboard shortcuts (the brand tray no longer carries buttons).
    // WidgetWithChildrenShortcut keeps them scoped to this tab so multiple open
    // documents in the shell don't fight over Ctrl+Z; the notes QTextEdit still
    // wins while focused because it consumes the key via ShortcutOverride.
    auto addSc = [this](const QKeySequence& seq, void (ImpressModule::*slot)()) {
        auto* sc = new QShortcut(seq, this);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, this, slot);
    };
    addSc(QKeySequence::Undo, &ImpressModule::undo);                    // Ctrl+Z
    addSc(QKeySequence::Redo, &ImpressModule::redo);                    // Ctrl+Y
    addSc(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), &ImpressModule::redo);

    connect(m_ribbon, &ImpressRibbon::insertModeChanged, this, [this](InsertMode mode) {
        if (m_currentIdx >= 0 && m_currentIdx < static_cast<int>(m_scenes.size()))
            m_scenes[m_currentIdx]->setInsertMode(mode);
    });

    connect(m_ribbon, &ImpressRibbon::shapeInsertRequested, this, [this](ShapeKind kind) {
        if (m_currentIdx >= 0 && m_currentIdx < static_cast<int>(m_scenes.size()))
            m_scenes[m_currentIdx]->beginInsertShape(kind);
    });

    connect(m_ribbon, &ImpressRibbon::shapeFillRequested, this, [this](const QColor& c) {
        if (m_currentIdx < 0) return;
        m_scenes[m_currentIdx]->setSelectedFill(c);
        commitUndoStep();
    });
    connect(m_ribbon, &ImpressRibbon::shapeOutlineRequested, this, [this](const QColor& c) {
        if (m_currentIdx < 0) return;
        m_scenes[m_currentIdx]->setSelectedOutline(c);
        commitUndoStep();
    });
    connect(m_ribbon, &ImpressRibbon::shadowToggleRequested, this, [this] {
        if (m_currentIdx < 0) return;
        if (!m_scenes[m_currentIdx]->hasSelection()) {
            QMessageBox::information(this, "Shadow",
                "Select an object on the slide first, then toggle its shadow.");
            return;
        }
        m_scenes[m_currentIdx]->toggleSelectedShadow();
        commitUndoStep();
    });

    connect(m_ribbon, &ImpressRibbon::newSlideRequested,       this, &ImpressModule::addNewSlide);
    connect(m_ribbon, &ImpressRibbon::duplicateSlideRequested, this, &ImpressModule::duplicateCurrentSlide);
    connect(m_ribbon, &ImpressRibbon::deleteSlideRequested,    this, &ImpressModule::deleteCurrentSlide);
    connect(m_ribbon, &ImpressRibbon::layoutSelected,          this, &ImpressModule::applyLayoutToCurrentSlide);
    connect(m_ribbon, &ImpressRibbon::transitionSelected,      this, &ImpressModule::applyTransitionToCurrentSlide);
    connect(m_ribbon, &ImpressRibbon::transitionApplyAllRequested, this, &ImpressModule::applyTransitionToAllSlides);
    connect(m_ribbon, &ImpressRibbon::animationSelected,       this, &ImpressModule::applyAnimationToSelection);
    connect(m_ribbon, &ImpressRibbon::slideAnimationSelected, this, [this](SlideAnimation a) {
        if (m_currentIdx < 0 || m_currentIdx >= static_cast<int>(m_slideData.size())) return;
        m_slideData[m_currentIdx].slideAnimation = a;
        if (!m_dirty) { m_dirty = true; emit documentModified(); }
        commitUndoStep();
        previewSlideAnimation(a);   // play it once so the user sees it applied
    });
    connect(m_ribbon, &ImpressRibbon::insertImageRequested,    this, &ImpressModule::insertImageFromFile);
    connect(m_ribbon, &ImpressRibbon::insertTableRequested, this, [this](int rows, int cols) {
        if (m_currentIdx < 0) return;
        m_scenes[m_currentIdx]->insertTable(rows, cols);
        commitUndoStep();
    });
    connect(m_ribbon, &ImpressRibbon::smartArtRequested, this, [this](SmartArtKind kind) {
        if (m_currentIdx < 0) return;
        m_scenes[m_currentIdx]->insertSmartArt(kind);
        commitUndoStep();
    });
    connect(m_ribbon, &ImpressRibbon::chartRequested, this, [this](ChartKind kind) {
        if (m_currentIdx < 0) return;
        m_scenes[m_currentIdx]->insertChart(kind);
        commitUndoStep();
    });
    connect(m_ribbon, &ImpressRibbon::wordArtRequested, this, [this] {
        insertPresetText("WordArt", 44.0, true, QColor("#E8372A"));
    });
    connect(m_ribbon, &ImpressRibbon::symbolRequested, this, [this](const QString& sym) {
        insertPresetText(sym, 40.0, false, QColor("#2C3140"));
    });
    connect(m_ribbon, &ImpressRibbon::slideNumberRequested, this, [this] {
        insertPresetText(QString::number(m_currentIdx + 1), 20.0, true, QColor("#2C3140"));
    });
    connect(m_ribbon, &ImpressRibbon::dateTimeRequested, this, [this] {
        insertPresetText(QDate::currentDate().toString("MMM d, yyyy"), 20.0, false, QColor("#2C3140"));
    });
    connect(m_ribbon, &ImpressRibbon::viewModeChanged,         this, &ImpressModule::setViewMode);
    connect(m_ribbon, &ImpressRibbon::slideShowFromBeginningRequested, this, [this] {
        switchToSlide(0);
        startSlideShow();
    });
    connect(m_ribbon, &ImpressRibbon::slideShowFromCurrentRequested, this, &ImpressModule::startSlideShow);
    connect(m_ribbon, &ImpressRibbon::presenterViewRequested, this, &ImpressModule::startPresenterView);

    connect(m_ribbon, &ImpressRibbon::notesToggleRequested, this, [this] {
        m_notesVisible = !m_notesVisible;
        m_notesEdit->setVisible(m_notesVisible);
    });

    auto toggleComments = [this] {
        m_commentsVisible = !m_commentsVisible;
        m_commentsPanel->setVisible(m_commentsVisible);
        if (m_commentsVisible) refreshComments();
    };
    connect(m_ribbon, &ImpressRibbon::commentsToggleRequested, this, toggleComments);
    connect(m_statusBar, &ImpressStatusBar::commentToggleRequested, this, toggleComments);

    connect(m_ribbon, &ImpressRibbon::designColorSelected, this,
            &ImpressModule::applyDesignColor);

    connect(m_ribbon, &ImpressRibbon::designThemeSelected, this,
            [this](const QColor& top, const QColor& bottom) {
        if (m_currentIdx < 0) return;
        auto* scene = m_scenes[m_currentIdx];
        SlideData snap; scene->saveToData(snap);
        snap.background  = top;
        snap.background2 = bottom;     // two-colour vertical gradient
        scene->loadFromData(snap);
        commitUndoStep();
    });

    // Apply a background colour / theme gradient to EVERY slide in the deck.
    auto applyBgToAll = [this](const QColor& top, const QColor& bottom) {
        for (size_t i = 0; i < m_scenes.size(); ++i) {
            SlideData snap; m_scenes[i]->saveToData(snap);
            snap.background  = top;
            snap.background2 = bottom;
            m_scenes[i]->loadFromData(snap);
            m_slideData[i] = snap;
            m_slidePanel->refreshSlide(static_cast<int>(i));
        }
        if (!m_dirty) { m_dirty = true; emit documentModified(); }
        commitUndoStep();
    };
    connect(m_ribbon, &ImpressRibbon::templatesRequested, this, &ImpressModule::showTemplatesGallery);
    connect(m_ribbon, &ImpressRibbon::designColorAllRequested, this,
            [applyBgToAll](const QColor& c) { applyBgToAll(c, QColor()); });
    connect(m_ribbon, &ImpressRibbon::designThemeAllRequested, this,
            [applyBgToAll](const QColor& top, const QColor& bottom) { applyBgToAll(top, bottom); });

    // ── Text formatting (applies to the last-edited text item) ───────────
    auto withCursor = [this](const std::function<void(QTextCursor&)>& fn) {
        if (m_currentIdx < 0) return;
        auto* ti = m_scenes[m_currentIdx]->activeTextItem();
        if (!ti) return;
        QTextCursor cur = ti->textCursor();
        fn(cur);
        ti->setTextCursor(cur);
    };

    connect(m_ribbon, &ImpressRibbon::boldToggled, this, [withCursor](bool on) {
        withCursor([on](QTextCursor& cur) {
            QTextCharFormat fmt; fmt.setFontWeight(on ? QFont::Bold : QFont::Normal);
            cur.mergeCharFormat(fmt);
        });
    });
    connect(m_ribbon, &ImpressRibbon::italicToggled, this, [withCursor](bool on) {
        withCursor([on](QTextCursor& cur) {
            QTextCharFormat fmt; fmt.setFontItalic(on);
            cur.mergeCharFormat(fmt);
        });
    });
    connect(m_ribbon, &ImpressRibbon::underlineToggled, this, [withCursor](bool on) {
        withCursor([on](QTextCursor& cur) {
            QTextCharFormat fmt; fmt.setFontUnderline(on);
            cur.mergeCharFormat(fmt);
        });
    });
    connect(m_ribbon, &ImpressRibbon::strikeToggled, this, [withCursor](bool on) {
        withCursor([on](QTextCursor& cur) {
            QTextCharFormat fmt; fmt.setFontStrikeOut(on);
            cur.mergeCharFormat(fmt);
        });
    });
    connect(m_ribbon, &ImpressRibbon::fontFamilyChanged, this, [withCursor](const QString& f) {
        withCursor([f](QTextCursor& cur) {
            QTextCharFormat fmt; fmt.setFontFamilies({f});
            cur.mergeCharFormat(fmt);
        });
    });
    connect(m_ribbon, &ImpressRibbon::fontSizeChanged, this, [withCursor](int pt) {
        withCursor([pt](QTextCursor& cur) {
            QTextCharFormat fmt; fmt.setFontPointSize(pt);
            cur.mergeCharFormat(fmt);
        });
    });
    connect(m_ribbon, &ImpressRibbon::textColorChanged, this, [withCursor](const QColor& c) {
        withCursor([c](QTextCursor& cur) {
            QTextCharFormat fmt; fmt.setForeground(c);
            cur.mergeCharFormat(fmt);
        });
    });
    connect(m_ribbon, &ImpressRibbon::alignChanged, this, [withCursor](Qt::Alignment a) {
        withCursor([a](QTextCursor& cur) {
            QTextBlockFormat bf = cur.blockFormat();
            bf.setAlignment(a);
            cur.mergeBlockFormat(bf);
        });
    });
    connect(m_ribbon, &ImpressRibbon::bulletsRequested, this, [withCursor] {
        withCursor([](QTextCursor& cur) { cur.createList(QTextListFormat::ListDisc); });
    });
    connect(m_ribbon, &ImpressRibbon::numberingRequested, this, [withCursor] {
        withCursor([](QTextCursor& cur) { cur.createList(QTextListFormat::ListDecimal); });
    });
    connect(m_ribbon, &ImpressRibbon::indentRequested, this, [withCursor](int dir) {
        withCursor([dir](QTextCursor& cur) {
            QTextBlockFormat bf = cur.blockFormat();
            bf.setIndent(std::max(0, bf.indent() + dir));
            cur.mergeBlockFormat(bf);
        });
    });
    connect(m_ribbon, &ImpressRibbon::lineSpacingChanged, this, [withCursor](double mult) {
        withCursor([mult](QTextCursor& cur) {
            QTextBlockFormat bf = cur.blockFormat();
            bf.setLineHeight(mult * 100.0, QTextBlockFormat::ProportionalHeight);
            cur.mergeBlockFormat(bf);
        });
    });

    // ── Wire slide panel ──────────────────────────────────────────────────
    connect(m_slidePanel, &SlidePanelWidget::slideClicked,      this, &ImpressModule::switchToSlide);
    connect(m_slidePanel, &SlidePanelWidget::addSlideRequested, this, &ImpressModule::addNewSlide);
    connect(m_slidePanel, &SlidePanelWidget::duplicateRequested, this, [this](int idx) {
        switchToSlide(idx);
        duplicateCurrentSlide();
    });
    connect(m_slidePanel, &SlidePanelWidget::deleteRequested, this, [this](int idx) {
        switchToSlide(idx);
        deleteCurrentSlide();
    });
    connect(m_slidePanel, &SlidePanelWidget::reorderRequested, this, &ImpressModule::moveSlide);

    // ── Wire outline ──────────────────────────────────────────────────────
    connect(m_outline, &OutlineWidget::slideSelected, this, &ImpressModule::switchToSlide);
    connect(m_outline, &OutlineWidget::textEdited, this,
            [this](int slideIdx, int itemIdx, const QString& newText) {
        if (slideIdx < 0 || slideIdx >= static_cast<int>(m_slideData.size())) return;
        if (slideIdx == m_currentIdx) m_scenes[slideIdx]->saveToData(m_slideData[slideIdx]);

        if (itemIdx == -1) {
            m_slideData[slideIdx].title = newText;
        } else if (itemIdx >= 0 && itemIdx < static_cast<int>(m_slideData[slideIdx].items.size())) {
            m_slideData[slideIdx].items[itemIdx].text = newText;
            m_slideData[slideIdx].items[itemIdx].html.clear();   // fall back to plain text rendering
        }

        if (slideIdx == m_currentIdx) m_scenes[slideIdx]->loadFromData(m_slideData[slideIdx]);
        m_slidePanel->refreshSlide(slideIdx);
        if (!m_dirty) { m_dirty = true; emit documentModified(); }
        commitUndoStep();
    });

    // ── Status bar wiring ───────────────────────────────────────────────
    connect(m_statusBar, &ImpressStatusBar::zoomChanged,    this, &ImpressModule::setZoomPercent);
    connect(m_statusBar, &ImpressStatusBar::viewModeChanged, this, &ImpressModule::setViewMode);

    // Clicking the left Slides/Outline tab directly keeps the view-mode buttons
    // in sync and (re)populates the outline so it always reflects the deck.
    connect(m_leftTabs, &QTabWidget::currentChanged, this, [this](int idx) {
        const bool outline = (m_leftTabs->widget(idx) == m_outline);
        if (outline) {
            m_outline->rebuild(m_slideData);
            if (m_currentIdx >= 0) m_outline->setActiveSlide(m_currentIdx);
        }
        m_statusBar->setViewMode(outline ? ImpressViewMode::Outline
                                         : ImpressViewMode::Normal);
    });

    // ── Notes panel ────────────────────────────────────────────────────
    connect(m_notesEdit, &QTextEdit::textChanged, this, [this] {
        if (m_currentIdx < 0 || m_ignoreChange) return;
        m_slideData[m_currentIdx].notes = m_notesEdit->toPlainText();
        if (!m_dirty) { m_dirty = true; emit documentModified(); }
        m_undoDebounce->start();
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Brand tray — the shared NativeOffice brand bar (logo mark, wordmark, plan
// pill). Undo/redo live on keyboard shortcuts, not tray buttons.
// ─────────────────────────────────────────────────────────────────────────────
QWidget* ImpressModule::buildBrandBar() {
    return new BrandBar(this);
}

// ─────────────────────────────────────────────────────────────────────────────
// Comments panel — per-slide review comments (right dock, toggled from Review)
// ─────────────────────────────────────────────────────────────────────────────
QWidget* ImpressModule::buildCommentsPanel() {
    m_commentsPanel = new QWidget(this);
    m_commentsPanel->setObjectName("impressComments");
    m_commentsPanel->setAttribute(Qt::WA_StyledBackground, true);
    m_commentsPanel->setFixedWidth(280);
    m_commentsPanel->setVisible(false);
    m_commentsPanel->setStyleSheet(
        "QWidget#impressComments { background:#FFFFFF; border-left:1px solid #E4E7ED; }");

    auto* v = new QVBoxLayout(m_commentsPanel);
    v->setContentsMargins(14, 12, 14, 12);
    v->setSpacing(8);

    // Header: "Comments" + close button
    auto* header = new QHBoxLayout;
    auto* title = new QLabel("Comments", m_commentsPanel);
    title->setStyleSheet("font-weight:600; font-size:14px; color:#1C1E26;");
    auto* closeBtn = new QToolButton(m_commentsPanel);
    closeBtn->setText(QString::fromUtf8("\xE2\x9C\x95"));   // ✕
    closeBtn->setToolTip("Close comments");
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        "QToolButton { border:none; background:transparent; color:#5A6071; "
        "font-size:13px; padding:2px 6px; border-radius:4px; }"
        "QToolButton:hover { background:#EFF1F5; color:#1C1E26; }");
    connect(closeBtn, &QToolButton::clicked, this, [this] {
        m_commentsVisible = false;
        m_commentsPanel->setVisible(false);
    });
    header->addWidget(title);
    header->addStretch();
    header->addWidget(closeBtn);
    v->addLayout(header);

    // Empty state — shown while the current slide has no comments
    m_commentEmpty = new QLabel(
        QString::fromUtf8("\xF0\x9F\x92\xAC\n\nThere are no comments\non the current slide"),
        m_commentsPanel);
    m_commentEmpty->setAlignment(Qt::AlignCenter);
    m_commentEmpty->setStyleSheet("color:#8A90A0; font-size:12px;");

    m_commentList = new QListWidget(m_commentsPanel);
    m_commentList->setWordWrap(true);
    m_commentList->setVisible(false);
    m_commentList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_commentList->setStyleSheet(
        "QListWidget { background:#FFFFFF; border:none; font-size:12px; }"
        "QListWidget::item { background:#F7F8FA; border:1px solid #E4E7ED; "
        "border-radius:6px; padding:8px; margin-bottom:6px; color:#1C1E26; }"
        "QListWidget::item:selected { border-color:#6D5BE8; background:#F3F1FD; }");
    connect(m_commentList, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
        const int r = m_commentList->indexAt(pos).row();
        if (r < 0 || m_currentIdx < 0) return;
        QMenu menu(m_commentList);
        QAction* del = menu.addAction("Delete comment");
        if (menu.exec(m_commentList->viewport()->mapToGlobal(pos)) != del) return;
        auto& cs = m_slideData[m_currentIdx].comments;
        if (r >= static_cast<int>(cs.size())) return;
        cs.erase(cs.begin() + r);
        refreshComments();
        if (!m_dirty) { m_dirty = true; emit documentModified(); }
        commitUndoStep();
    });

    v->addWidget(m_commentEmpty, 1);
    v->addWidget(m_commentList, 1);

    // Footer: note input + Cancel / Insert
    m_commentInput = new QLineEdit(m_commentsPanel);
    m_commentInput->setPlaceholderText("Enter note");
    m_commentInput->setStyleSheet(
        "QLineEdit { border:1px solid #D7DAE0; border-radius:6px; padding:7px 10px; "
        "font-size:12px; background:#FFFFFF; color:#1C1E26; }"
        "QLineEdit:focus { border-color:#6D5BE8; }");

    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    auto* cancelBtn = new QPushButton("Cancel", m_commentsPanel);
    auto* insertBtn = new QPushButton("Insert", m_commentsPanel);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    insertBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setStyleSheet(
        "QPushButton { background:#F3F4F6; color:#5A6071; border:1px solid #D7DAE0; "
        "border-radius:6px; padding:6px 16px; font-size:12px; }"
        "QPushButton:hover { background:#E7E9EE; }");
    insertBtn->setStyleSheet(
        "QPushButton { background:#6D5BE8; color:#FFFFFF; border:none; "
        "border-radius:6px; padding:6px 16px; font-size:12px; }"
        "QPushButton:hover { background:#8674F0; }");
    row->addStretch();
    row->addWidget(cancelBtn);
    row->addWidget(insertBtn);

    v->addWidget(m_commentInput);
    v->addLayout(row);

    auto insertComment = [this] {
        if (m_currentIdx < 0) return;
        const QString t = m_commentInput->text().trimmed();
        if (t.isEmpty()) return;
        m_slideData[m_currentIdx].comments.push_back({ "You", t });
        m_commentInput->clear();
        refreshComments();
        if (!m_dirty) { m_dirty = true; emit documentModified(); }
        commitUndoStep();
    };
    connect(insertBtn, &QPushButton::clicked, this, insertComment);
    connect(m_commentInput, &QLineEdit::returnPressed, this, insertComment);
    connect(cancelBtn, &QPushButton::clicked, this, [this] { m_commentInput->clear(); });

    return m_commentsPanel;
}

void ImpressModule::refreshComments() {
    if (!m_commentList) return;
    m_commentList->clear();
    bool hasComments = false;
    if (m_currentIdx >= 0 && m_currentIdx < static_cast<int>(m_slideData.size())) {
        for (const auto& cm : m_slideData[m_currentIdx].comments) {
            auto* item = new QListWidgetItem(QString("%1\n%2").arg(cm.author, cm.text));
            item->setToolTip(cm.text);
            m_commentList->addItem(item);
        }
        hasComments = !m_slideData[m_currentIdx].comments.empty();
    }
    m_commentList->setVisible(hasComments);
    if (m_commentEmpty) m_commentEmpty->setVisible(!hasComments);
}

// ─────────────────────────────────────────────────────────────────────────────
// Templates — ~60 ready-made slide designs across 5 layout styles & 12 palettes
// (centred title, left heading, two-tone split, top banner, minimalist accent)
// ─────────────────────────────────────────────────────────────────────────────
namespace {
std::vector<SlideData> builtinTemplates() {
    auto txt = [](double x, double y, double w, double h, const QString& t,
                  int sz, const QString& col, bool bold, const QString& align) {
        SlideItem s;
        s.type     = SlideItemType::TextBox;
        s.rect     = QRectF(x, y, w, h);
        s.text     = t;
        s.fontSize = sz;
        s.penColor = QColor(col);
        s.html = QString("<p align=\"%1\" style=\"margin:0; font-size:%2pt; color:%3;\">%4%5%6</p>")
                     .arg(align).arg(sz).arg(col)
                     .arg(bold ? "<b>" : "", t, bold ? "</b>" : "");
        return s;
    };
    auto shp = [](double x, double y, double w, double h, ShapeKind k, const QString& fill) {
        SlideItem s;
        s.type      = SlideItemType::Shape;
        s.shapeKind = k;
        s.rect      = QRectF(x, y, w, h);
        s.fillColor = QColor(fill);
        s.penColor  = QColor(fill);
        s.penWidth  = 0.0;
        return s;
    };

    struct Pal { const char* top; const char* bot; const char* title; const char* body; const char* accent; };
    const Pal pals[] = {
        { "#0F172A", "#1E293B", "#FFFFFF", "#CBD5E1", "#38BDF8" },
        { "#FFFFFF", "#DBEAFE", "#1E3A8A", "#334155", "#2563EB" },
        { "#FEF3C7", "#FDE68A", "#7C2D12", "#92400E", "#EA580C" },
        { "#ECFDF5", "#A7F3D0", "#064E3B", "#065F46", "#10B981" },
        { "#FDF2F8", "#FBCFE8", "#831843", "#9D174D", "#DB2777" },
        { "#1A1F2E", "#2C3140", "#FFFFFF", "#C6CAD3", "#E8372A" },
        { "#F5F3FF", "#DDD6FE", "#4C1D95", "#5B21B6", "#7C3AED" },
        { "#FFFFFF", "#F1F5F9", "#0F172A", "#475569", "#0EA5E9" },
        { "#082F49", "#0C4A6E", "#E0F2FE", "#BAE6FD", "#38BDF8" },
        { "#1C1917", "#292524", "#FAFAF9", "#D6D3D1", "#F59E0B" },
        { "#022C22", "#064E3B", "#ECFDF5", "#A7F3D0", "#34D399" },
        { "#4A044E", "#701A75", "#FAE8FF", "#F5D0FE", "#E879F9" },
    };

    std::vector<SlideData> out;
    for (const auto& p : pals) {
        // Style A — centred title on a gradient, accent bar along the bottom.
        {
            SlideData d;
            d.background  = QColor(p.top);
            d.background2 = QColor(p.bot);
            d.items.push_back(shp(0, 472, 960, 68, ShapeKind::Rectangle, p.accent));
            d.items.push_back(txt(120, 168, 720, 90, "Presentation Title", 44, p.title, true, "center"));
            d.items.push_back(txt(160, 286, 640, 50, "Your subtitle goes here", 22, p.body, false, "center"));
            out.push_back(d);
        }
        // Style B — left-aligned heading, solid fill, vertical accent bar.
        {
            SlideData d;
            d.background = QColor(p.top);
            d.items.push_back(shp(70, 150, 12, 240, ShapeKind::Rectangle, p.accent));
            d.items.push_back(txt(108, 158, 720, 90, "Section Heading", 40, p.title, true, "left"));
            d.items.push_back(txt(108, 268, 700, 150,
                                  "Add your key points or a short description here.", 20, p.body, false, "left"));
            out.push_back(d);
        }
        // Style C — two-tone vertical split: a coloured left panel (the larger
        // division part) beside a content column on the slide background.
        {
            SlideData d;
            d.background = QColor(p.top);
            d.items.push_back(shp(0, 0, 372, 540, ShapeKind::Rectangle, p.bot));
            d.items.push_back(txt(44, 196, 300, 150, "Title\nGoes Here", 36, p.title, true, "left"));
            d.items.push_back(txt(420, 150, 480, 90, "Key Message", 30, p.title, true, "left"));
            d.items.push_back(txt(420, 244, 480, 220,
                                  "Use the wider column for supporting detail, bullet points or a short paragraph.",
                                  20, p.body, false, "left"));
            out.push_back(d);
        }
        // Style D — bold top banner bar with the title reversed out in white.
        {
            SlideData d;
            d.background = QColor(p.top);
            d.items.push_back(shp(0, 0, 960, 132, ShapeKind::Rectangle, p.accent));
            d.items.push_back(txt(60, 38, 840, 70, "Presentation Title", 36, "#FFFFFF", true, "left"));
            d.items.push_back(txt(60, 200, 840, 60, "A clear, concise subtitle", 24, p.title, true, "left"));
            d.items.push_back(txt(60, 300, 840, 180,
                                  "Add an overview of what this section covers here.", 20, p.body, false, "left"));
            out.push_back(d);
        }
        // Style E — minimalist: big centred title with a corner accent triangle.
        {
            SlideData d;
            d.background = QColor(p.top);
            d.items.push_back(shp(700, 360, 260, 180, ShapeKind::Triangle, p.accent));
            d.items.push_back(shp(0, 0, 200, 140, ShapeKind::Triangle, p.bot));
            d.items.push_back(txt(120, 196, 720, 100, "The Big Idea", 48, p.title, true, "center"));
            d.items.push_back(txt(180, 312, 600, 50, "one line that says it all", 22, p.body, false, "center"));
            out.push_back(d);
        }
    }
    return out;
}
} // namespace

void ImpressModule::showTemplatesGallery() {
    const std::vector<SlideData> templates = builtinTemplates();

    QDialog dlg(this);
    dlg.setWindowTitle("Choose a Template");
    dlg.resize(840, 580);

    auto* scroll = new QScrollArea(&dlg);
    scroll->setWidgetResizable(true);
    auto* container = new QWidget;
    auto* grid = new QGridLayout(container);
    grid->setSpacing(12);
    grid->setContentsMargins(12, 12, 12, 12);

    int col = 0, row = 0;
    for (const SlideData& tpl : templates) {
        SlideScene scene;
        scene.loadFromData(tpl);
        QPixmap pm(256, 144);
        pm.fill(Qt::white);
        {
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.setRenderHint(QPainter::TextAntialiasing);
            scene.render(&p, QRectF(0, 0, 256, 144),
                         QRectF(0, 0, SlideScene::SLIDE_W, SlideScene::SLIDE_H));
        }

        auto* btn = new QToolButton(container);
        btn->setIcon(QIcon(pm));
        btn->setIconSize(QSize(256, 144));
        btn->setFixedSize(268, 160);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet("QToolButton{border:1px solid #D7DAE0; border-radius:6px; background:#fff;}"
                           "QToolButton:hover{border:2px solid #6D5BE8;}");
        const SlideData chosen = tpl;
        connect(btn, &QToolButton::clicked, &dlg, [this, &dlg, chosen] {
            applyTemplate(chosen);
            dlg.accept();
        });
        grid->addWidget(btn, row, col);
        if (++col == 3) { col = 0; ++row; }
    }

    scroll->setWidget(container);
    auto* v = new QVBoxLayout(&dlg);
    v->setContentsMargins(0, 0, 0, 0);
    v->addWidget(scroll);
    dlg.exec();
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-slide deck templates for the Home screen gallery
// ─────────────────────────────────────────────────────────────────────────────
void ImpressModule::applyDeckTemplate(const QString& name) {
    auto txt = [](double x, double y, double w, double h, const QString& t,
                  int sz, const QString& col, bool bold, const QString& align) {
        SlideItem s;
        s.type     = SlideItemType::TextBox;
        s.rect     = QRectF(x, y, w, h);
        s.text     = t;
        s.fontSize = sz;
        s.penColor = QColor(col);
        s.html = QString("<p align=\"%1\" style=\"margin:0; font-size:%2pt; color:%3;\">%4%5%6</p>")
                     .arg(align).arg(sz).arg(col)
                     .arg(bold ? "<b>" : "", t.toHtmlEscaped(), bold ? "</b>" : "");
        return s;
    };
    auto shp = [](double x, double y, double w, double h, ShapeKind k, const QString& fill) {
        SlideItem s;
        s.type      = SlideItemType::Shape;
        s.shapeKind = k;
        s.rect      = QRectF(x, y, w, h);
        s.fillColor = QColor(fill);
        s.penColor  = QColor(fill);
        s.penWidth  = 0.0;
        return s;
    };

    struct Pal { QString bg, bg2, title, body, accent; };
    struct Sect { QString heading; QStringList bullets; };
    struct Deck { QString title, subtitle; Pal pal; QList<Sect> sections; };

    const Pal navy   { "#0F172A", "#1E293B", "#FFFFFF", "#CBD5E1", "#38BDF8" };
    const Pal blue   { "#FFFFFF", "#DBEAFE", "#1E3A8A", "#334155", "#2563EB" };
    const Pal green  { "#ECFDF5", "#A7F3D0", "#064E3B", "#065F46", "#10B981" };
    const Pal amber  { "#FEF3C7", "#FDE68A", "#7C2D12", "#92400E", "#EA580C" };
    const Pal violet { "#F8FAFC", "#EDE9FE", "#4C1D95", "#334155", "#7C3AED" };
    const Pal dark   { "#18181B", "#27272A", "#FAFAFA", "#D4D4D8", "#F59E0B" };

    QHash<QString, Deck> decks;
    decks["Pitch Deck"] = { "Your Startup", "one line that investors remember", navy, {
        { "The Problem",  { "What's broken today", "Who feels the pain and how often", "Cost of the status quo" } },
        { "The Solution", { "Your product in one sentence", "Why now — what changed", "Live demo" } },
        { "Market & Model", { "TAM / SAM / SOM", "How you make money", "Pricing" } },
        { "Traction",     { "Key metrics and growth", "Customers & pipeline", "Milestones hit" } },
        { "The Ask",      { "Raising: amount & instrument", "Use of funds", "12-month plan" } } } };
    decks["Business Review"] = { "Quarterly Business Review", "Q_ 20__ · Team", blue, {
        { "Highlights",     { "Wins this quarter", "Revenue vs plan", "Team changes" } },
        { "Key Metrics",    { "Revenue & margin", "Customer growth / churn", "NPS" } },
        { "Challenges",     { "What slipped and why", "Risks ahead", "Mitigations" } },
        { "Next Quarter",   { "Top 3 priorities", "Targets", "Asks from leadership" } } } };
    decks["Project Plan"] = { "Project Kickoff", "goal · scope · timeline", green, {
        { "Objectives",   { "What success looks like", "In scope / out of scope", "Stakeholders" } },
        { "Timeline",     { "Phase 1 — Discovery", "Phase 2 — Build", "Phase 3 — Launch" } },
        { "Team & Roles", { "Sponsor", "Project lead", "Contributors" } },
        { "Risks",        { "Top risks & owners", "Dependencies", "Contingency" } } } };
    decks["Portfolio"] = { "Your Name", "designer · developer · creator", dark, {
        { "About Me",       { "Short bio", "Skills & tools", "Contact" } },
        { "Selected Work",  { "Project one — result", "Project two — result", "Project three — result" } },
        { "Process",        { "Discover", "Design", "Deliver" } },
        { "Let's Talk",     { "email@example.com", "portfolio.example.com" } } } };
    decks["Marketing Plan"] = { "Marketing Plan", "reach · engage · convert", amber, {
        { "Goals",     { "Awareness targets", "Lead / signup targets", "Revenue contribution" } },
        { "Audience",  { "Primary persona", "Where they spend time", "What they care about" } },
        { "Channels",  { "Content & SEO", "Paid & social", "Email & community" } },
        { "Budget & KPIs", { "Spend by channel", "CAC / ROAS", "Review cadence" } } } };
    decks["Company Profile"] = { "Company Name", "what we do, in one line", violet, {
        { "Who We Are",   { "Founded · HQ · team size", "Mission", "Values" } },
        { "What We Do",   { "Products & services", "Who we serve", "What makes us different" } },
        { "Our Numbers",  { "Customers", "Revenue growth", "Coverage" } },
        { "Contact",      { "hello@company.com", "company.com" } } } };
    decks["Product Roadmap"] = { "Product Roadmap", "now · next · later", navy, {
        { "Now",   { "Shipping this quarter", "Owner & status", "Launch dates" } },
        { "Next",  { "Committed for next quarter", "Dependencies", "Open questions" } },
        { "Later", { "Exploring / research", "Big bets", "Ideas parking lot" } },
        { "Themes", { "Quality & performance", "Growth", "Platform" } } } };
    decks["Training Deck"] = { "Training Session", "topic · audience · duration", blue, {
        { "Agenda",       { "What we'll cover", "Timing", "Ground rules" } },
        { "Core Concepts", { "Concept one", "Concept two", "Concept three" } },
        { "Hands-On",     { "Exercise walkthrough", "Common mistakes", "Tips" } },
        { "Wrap-Up",      { "Key takeaways", "Resources", "Q&A" } } } };

    if (!decks.contains(name)) return;
    const Deck deck = decks.value(name);
    const Pal& p = deck.pal;

    std::vector<SlideData> slides;
    {   // Title slide
        SlideData d;
        d.title = deck.title;
        d.background = QColor(p.bg);
        d.background2 = QColor(p.bg2);
        d.items.push_back(shp(0, 476, 960, 64, ShapeKind::Rectangle, p.accent));
        d.items.push_back(txt(80, 170, 800, 110, deck.title, 46, p.title, true, "center"));
        d.items.push_back(txt(120, 300, 720, 50, deck.subtitle, 20, p.body, false, "center"));
        slides.push_back(d);
    }
    for (const Sect& s : deck.sections) {
        SlideData d;
        d.title = s.heading;
        d.background = QColor(p.bg);
        d.items.push_back(shp(60, 108, 66, 6, ShapeKind::Rectangle, p.accent));
        d.items.push_back(txt(60, 48, 840, 60, s.heading, 32, p.title, true, "left"));
        QString html, plain;
        for (const QString& b : s.bullets) {
            html += QString("<p style=\"margin:0 0 14px 0; font-size:20pt; color:%1;\">•  %2</p>")
                        .arg(p.body, b.toHtmlEscaped());
            plain += "• " + b + "\n";
        }
        SlideItem body;
        body.type = SlideItemType::TextBox;
        body.rect = QRectF(80, 150, 800, 330);
        body.text = plain.trimmed();
        body.fontSize = 20;
        body.penColor = QColor(p.body);
        body.html = html;
        d.items.push_back(body);
        slides.push_back(d);
    }

    m_ignoreChange = true;
    clearDeck();
    for (const SlideData& d : slides) createSlide(d);
    m_ignoreChange = false;
    if (!m_scenes.empty()) switchToSlide(0);
    captureUndoBaseline();
    m_undoStack->clear();
}

void ImpressModule::applyTemplate(const SlideData& tpl) {
    if (m_currentIdx < 0 || m_currentIdx >= static_cast<int>(m_slideData.size())) return;

    SlideData d = tpl;
    // Keep slide-level metadata the user already set; replace only the design.
    d.notes          = m_slideData[m_currentIdx].notes;
    d.transition     = m_slideData[m_currentIdx].transition;
    d.slideAnimation = m_slideData[m_currentIdx].slideAnimation;
    d.comments       = m_slideData[m_currentIdx].comments;

    m_slideData[m_currentIdx] = d;
    m_scenes[m_currentIdx]->loadFromData(d);
    m_slidePanel->refreshSlide(m_currentIdx);
    if (!m_dirty) { m_dirty = true; emit documentModified(); }
    commitUndoStep();
}

// ─────────────────────────────────────────────────────────────────────────────
// Insert a ready-made text box (WordArt / Symbol / Slide Number / Date & Time)
// ─────────────────────────────────────────────────────────────────────────────
void ImpressModule::insertPresetText(const QString& text, double fontSize,
                                     bool bold, const QColor& color) {
    if (m_currentIdx < 0 || m_currentIdx >= static_cast<int>(m_scenes.size())) return;
    m_scenes[m_currentIdx]->insertPresetText(text, fontSize, bold, color);
    commitUndoStep();
}

// ─────────────────────────────────────────────────────────────────────────────
// Low-level: allocate a SlideScene from a SlideData and register it
// ─────────────────────────────────────────────────────────────────────────────
void ImpressModule::createSlide(const SlideData& data) {
    const int idx = static_cast<int>(m_scenes.size());

    auto* scene = new SlideScene(this);
    scene->loadFromData(data);

    m_scenes.push_back(scene);
    m_slideData.push_back(data);

    // Add thumbnail to panel
    m_slidePanel->addSlide(idx, scene);

    // Any visual change (move/resize/rotate/text edit/add/remove) refreshes
    // the thumbnail, marks the document dirty, and schedules an undo
    // checkpoint a short idle period later. We resolve the slide index live
    // (not captured) so reordering/deletion can't refresh the wrong thumbnail.
    connect(scene, &QGraphicsScene::changed, this, [this, scene](const QList<QRectF>&) {
        const int cur = indexOfScene(scene);
        if (cur >= 0) m_slidePanel->refreshSlide(cur);
        if (!m_ignoreChange && !m_dirty) {
            m_dirty = true;
            emit documentModified();
        }
        if (!m_ignoreChange && !m_restoringUndo)
            m_undoDebounce->start();
    });

    // Non-visual changes (e.g. setting an object animation via right-click)
    // don't fire QGraphicsScene::changed, so track them via sceneModified too.
    connect(scene, &SlideScene::sceneModified, this, [this] {
        if (m_ignoreChange) return;
        if (!m_dirty) { m_dirty = true; emit documentModified(); }
        if (!m_restoringUndo) m_undoDebounce->start();
    });

    // Reflect the current selection's formatting in the ribbon
    connect(scene, &SlideScene::selectionInfoChanged,
            this, &ImpressModule::syncRibbonToSelection);

    // When an object animation is applied (ribbon or right-click → Animate),
    // play a one-shot preview on the canvas so the user sees it take effect.
    connect(scene, &SlideScene::animationApplied, this, [this, scene](ItemAnimation) {
        if (indexOfScene(scene) == m_currentIdx) previewObjectAnimations();
    });

    // When insert mode completes → reset ribbon button
    connect(scene, &SlideScene::insertModeLeft,
            m_ribbon, &ImpressRibbon::resetInsertMode);
}

int ImpressModule::indexOfScene(SlideScene* scene) const {
    const auto it = std::find(m_scenes.begin(), m_scenes.end(), scene);
    return it == m_scenes.end() ? -1 : static_cast<int>(it - m_scenes.begin());
}

void ImpressModule::syncRibbonToSelection() {
    if (m_currentIdx < 0 || m_currentIdx >= static_cast<int>(m_scenes.size())) return;
    auto* ti = m_scenes[m_currentIdx]->activeTextItem();
    if (!ti) return;

    const QTextCursor cur = ti->textCursor();
    const QTextCharFormat fmt = cur.charFormat();
    const bool bold = fmt.fontWeight() >= QFont::Bold;
    const QString family = fmt.fontFamilies().toStringList().value(0);
    const int pt = static_cast<int>(fmt.fontPointSize());
    m_ribbon->syncCharFormat(bold, fmt.fontItalic(), fmt.fontUnderline(),
                             fmt.fontStrikeOut(), family, pt,
                             fmt.foreground().color());
    m_ribbon->syncAlignment(cur.blockFormat().alignment());
}

// ─────────────────────────────────────────────────────────────────────────────
// Public slots
// ─────────────────────────────────────────────────────────────────────────────
void ImpressModule::addNewSlide() {
    SlideData data;
    data.title      = QString("Slide %1").arg(m_scenes.size() + 1);
    data.background = Qt::white;
    data.layout     = SlideLayout::Title;

    createSlide(data);
    const int newIdx = static_cast<int>(m_scenes.size()) - 1;

    // Add default placeholders on the scene (title + subtitle text)
    m_scenes[newIdx]->addDefaultPlaceholders();

    switchToSlide(newIdx);
    commitUndoStep();
}

void ImpressModule::switchToSlide(int index) {
    if (index < 0 || index >= static_cast<int>(m_scenes.size())) return;

    // Save current scene state back to SlideData before switching
    if (m_currentIdx >= 0 && m_currentIdx < static_cast<int>(m_scenes.size()))
        m_scenes[m_currentIdx]->saveToData(m_slideData[m_currentIdx]);

    // Reset insert mode on old scene
    if (m_currentIdx >= 0 && m_currentIdx < static_cast<int>(m_scenes.size()))
        m_scenes[m_currentIdx]->setInsertMode(InsertMode::None);
    m_ribbon->resetInsertMode();

    m_currentIdx = index;
    m_view->setScene(m_scenes[index]);

    // Fit the 960×540 slide to the view, maintaining aspect ratio
    refitView();

    m_slidePanel->setActiveSlide(index);
    m_outline->setActiveSlide(index);

    // Sync notes panel (without re-triggering dirty tracking)
    {
        const QSignalBlocker blocker(m_notesEdit);
        m_notesEdit->setPlainText(m_slideData[index].notes);
    }

    m_statusBar->setSlideInfo(index, static_cast<int>(m_scenes.size()));

    m_slidePanel->refreshSlide(index);   // force an immediate thumbnail refresh
    refreshComments();
}

void ImpressModule::duplicateCurrentSlide() {
    if (m_currentIdx < 0 || m_currentIdx >= static_cast<int>(m_scenes.size())) return;

    // Save current first
    m_scenes[m_currentIdx]->saveToData(m_slideData[m_currentIdx]);

    SlideData copy = m_slideData[m_currentIdx];
    copy.title     = copy.title + " (copy)";
    createSlide(copy);
    switchToSlide(static_cast<int>(m_scenes.size()) - 1);
    commitUndoStep();
}

void ImpressModule::deleteCurrentSlide() {
    if (m_scenes.size() <= 1) {
        QMessageBox::information(this, "Cannot Delete",
            "A presentation must have at least one slide.");
        return;
    }

    const int idx = m_currentIdx;

    // Detach the scene from the view before deletion
    m_view->setScene(nullptr);

    delete m_scenes[idx];
    m_scenes.erase(m_scenes.begin() + idx);
    m_slideData.erase(m_slideData.begin() + idx);

    // Re-index thumbnails (simplest correct approach: rebuild the panel)
    m_slidePanel->clear();
    for (int i = 0; i < static_cast<int>(m_scenes.size()); ++i)
        m_slidePanel->addSlide(i, m_scenes[i]);

    m_currentIdx = -1;
    const int nextIdx = std::min(idx, static_cast<int>(m_scenes.size()) - 1);
    switchToSlide(nextIdx);
    // Removing a slide changes no scene content, so no QGraphicsScene::changed
    // fires — mark dirty explicitly or the deletion is lost with no save prompt.
    if (!m_dirty) { m_dirty = true; emit documentModified(); }
    commitUndoStep();
}

void ImpressModule::moveSlide(int fromIndex, int toIndex) {
    if (fromIndex == toIndex) return;
    if (fromIndex < 0 || fromIndex >= static_cast<int>(m_scenes.size())) return;
    toIndex = std::clamp(toIndex, 0, static_cast<int>(m_scenes.size()) - 1);

    if (m_currentIdx >= 0)
        m_scenes[m_currentIdx]->saveToData(m_slideData[m_currentIdx]);

    auto* scene = m_scenes[fromIndex];
    SlideData data = m_slideData[fromIndex];
    m_scenes.erase(m_scenes.begin() + fromIndex);
    m_slideData.erase(m_slideData.begin() + fromIndex);
    m_scenes.insert(m_scenes.begin() + toIndex, scene);
    m_slideData.insert(m_slideData.begin() + toIndex, data);

    m_view->setScene(nullptr);
    m_slidePanel->clear();
    for (int i = 0; i < static_cast<int>(m_scenes.size()); ++i)
        m_slidePanel->addSlide(i, m_scenes[i]);

    m_currentIdx = -1;
    switchToSlide(toIndex);
    // Reordering touches no scene content, so mark dirty explicitly.
    if (!m_dirty) { m_dirty = true; emit documentModified(); }
    commitUndoStep();
}

void ImpressModule::applyLayoutToCurrentSlide(SlideLayout layout) {
    if (m_currentIdx < 0) return;
    m_slideData[m_currentIdx].layout = layout;
    m_scenes[m_currentIdx]->applyLayout(layout);
    commitUndoStep();
}

void ImpressModule::applyTransitionToCurrentSlide(SlideTransition transition) {
    if (m_currentIdx < 0) return;
    m_slideData[m_currentIdx].transition = transition;
    // A transition is slide metadata, not scene content — mark dirty explicitly
    // (the apply-to-all variant already does this).
    if (!m_dirty) { m_dirty = true; emit documentModified(); }
    commitUndoStep();
    previewTransition(transition);   // play it once so the user sees it applied
}

void ImpressModule::applyTransitionToAllSlides(SlideTransition transition) {
    if (m_slideData.empty()) return;
    for (auto& slide : m_slideData)
        slide.transition = transition;
    if (!m_dirty) { m_dirty = true; emit documentModified(); }
    commitUndoStep();
    previewTransition(transition);
}

void ImpressModule::applyAnimationToSelection(ItemAnimation anim) {
    if (m_currentIdx < 0) return;
    auto* scene = m_scenes[m_currentIdx];
    if (!scene->hasSelection()) {
        QMessageBox::information(this, "Animations",
            "Select an object on the slide first, then choose an entrance animation.");
        return;
    }
    scene->setSelectedAnimation(anim);
    commitUndoStep();
}

void ImpressModule::insertImageFromFile() {
    if (m_currentIdx < 0) return;
    const QString path = QFileDialog::getOpenFileName(
        this, "Insert Image", QDir::homePath(),
        "Images (*.png *.jpg *.jpeg *.bmp *.gif)");
    if (path.isEmpty()) return;

    QImage img(path);
    if (img.isNull()) {
        QMessageBox::warning(this, "Insert Image", "Could not load image:\n" + path);
        return;
    }
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");

    m_scenes[m_currentIdx]->insertImage(bytes);
    commitUndoStep();
}

void ImpressModule::setZoomPercent(int percent) {
    m_zoomPercent = percent;
    refitView();
}

void ImpressModule::setViewMode(ImpressViewMode mode) {
    switch (mode) {
    case ImpressViewMode::Normal:
        m_leftTabs->setCurrentWidget(m_slidePanel);
        break;
    case ImpressViewMode::Outline:
        m_outline->rebuild(m_slideData);
        if (m_currentIdx >= 0) m_outline->setActiveSlide(m_currentIdx);
        m_leftTabs->setCurrentWidget(m_outline);
        break;
    case ImpressViewMode::SlideSorter:
        m_leftTabs->setCurrentWidget(m_slidePanel);
        break;
    }
    m_statusBar->setViewMode(mode);
}

void ImpressModule::startSlideShow() {
    launchShow(false);
}

void ImpressModule::startPresenterView() {
    launchShow(true);
}

void ImpressModule::launchShow(bool presenter) {
    if (m_scenes.empty()) return;
    if (m_currentIdx >= 0)
        m_scenes[m_currentIdx]->saveToData(m_slideData[m_currentIdx]);

    // Snapshot every scene's current state into the data deck, then present
    // from a fresh copy so the show never mutates the editor's live scenes.
    for (int i = 0; i < static_cast<int>(m_scenes.size()); ++i)
        m_scenes[i]->saveToData(m_slideData[i]);

    auto* win = new PresentModeWindow(m_slideData, std::max(0, m_currentIdx), presenter, this);
    win->show();
}

// ─────────────────────────────────────────────────────────────────────────────
// Undo / redo
// ─────────────────────────────────────────────────────────────────────────────
void ImpressModule::syncCurrentSceneToData() {
    if (m_currentIdx >= 0 && m_currentIdx < static_cast<int>(m_scenes.size()))
        m_scenes[m_currentIdx]->saveToData(m_slideData[m_currentIdx]);
}

void ImpressModule::captureUndoBaseline() {
    syncCurrentSceneToData();
    m_undoBaseline    = QJsonDocument(deckToJson()).toJson(QJsonDocument::Compact);
    m_undoBaselineIdx = m_currentIdx;
}

void ImpressModule::commitUndoStep() {
    if (m_restoringUndo) return;
    syncCurrentSceneToData();

    const QJsonObject afterJson = deckToJson();
    const QString afterStr = QJsonDocument(afterJson).toJson(QJsonDocument::Compact);
    if (afterStr == m_undoBaseline) return;

    const QJsonDocument beforeDoc = QJsonDocument::fromJson(m_undoBaseline.toUtf8());
    auto* cmd = new DeckSnapshotCommand(this, beforeDoc.object(), afterJson,
                                         m_undoBaselineIdx, m_currentIdx);
    m_undoStack->push(cmd);

    m_undoBaseline    = afterStr;
    m_undoBaselineIdx = m_currentIdx;
}

void ImpressModule::undo() {
    if (m_undoStack->canUndo()) m_undoStack->undo();
}

void ImpressModule::redo() {
    if (m_undoStack->canRedo()) m_undoStack->redo();
}

// ─────────────────────────────────────────────────────────────────────────────
// Accessors
// ─────────────────────────────────────────────────────────────────────────────
int ImpressModule::slideCount() const noexcept {
    return static_cast<int>(m_scenes.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// Styling
// ─────────────────────────────────────────────────────────────────────────────
void ImpressModule::applyStyles() {
    setStyleSheet(R"(
QWidget#impressModule {
    background-color: #E8E9ED;
}
QWidget#impressBody {
    background-color: #E8E9ED;
}
QGraphicsView#impressCanvas {
    background-color: #E8E9ED;
    border: none;
}
QTabWidget#impressLeftTabs::pane {
    border: none;
    background: #F3F4F6;
}
QTabWidget#impressLeftTabs QTabBar::tab {
    background: #E7E9EE;
    color: #5A6071;
    padding: 6px 12px;
    font-size: 11px;
    font-weight: 500;
    font-family: "Segoe UI", "Inter", sans-serif;
}
QTabWidget#impressLeftTabs QTabBar::tab:selected {
    background: #F3F4F6;
    color: #1C1E26;
    border-top: 2px solid #6D5BE8;
}
QTextEdit#impressNotes {
    background-color: #FFFFFF;
    border: none;
    border-top: 1px solid #D7DAE0;
    padding: 8px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-size: 13px;
    color: #1C1E26;
}
QSplitter#impressSplitter::handle {
    background-color: #D7DAE0;
}
)");
}

// ─────────────────────────────────────────────────────────────────────────────
// Keep slide surface fitted when the widget is resized
// ─────────────────────────────────────────────────────────────────────────────
void ImpressModule::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    refitView();
}

bool ImpressModule::eventFilter(QObject* obj, QEvent* event) {
    if (m_view && obj == m_view->viewport()) {
        if (event->type() == QEvent::Resize) {
            refitView();
        } else if (event->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            // Arrow keys page through slides — but only when nothing on the
            // canvas is selected or being text-edited (those consume arrows to
            // nudge shapes / move the caret).
            const bool busy = m_currentIdx >= 0 &&
                m_currentIdx < static_cast<int>(m_scenes.size()) &&
                (m_scenes[m_currentIdx]->hasSelection() ||
                 m_scenes[m_currentIdx]->focusItem() != nullptr);
            if (!busy) {
                switch (ke->key()) {
                case Qt::Key_Left:
                case Qt::Key_Up:
                    if (m_currentIdx > 0) switchToSlide(m_currentIdx - 1);
                    return true;
                case Qt::Key_Right:
                case Qt::Key_Down:
                    if (m_currentIdx < static_cast<int>(m_scenes.size()) - 1)
                        switchToSlide(m_currentIdx + 1);
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

// Fit the fixed 960×540 slide into the viewport and apply the zoom factor.
// 100% means "fit to window"; higher values scale up (with scrollbars).
void ImpressModule::refitView() {
    if (!m_view || !m_view->scene()) return;
    const QSize vp = m_view->viewport()->size();
    if (vp.width() < 4 || vp.height() < 4) return;

    const double fit = std::min(vp.width()  / SlideScene::SLIDE_W,
                                vp.height() / SlideScene::SLIDE_H);
    const double scale = fit * (m_zoomPercent / 100.0);
    if (scale <= 0.0) return;

    m_view->setSceneRect(0, 0, SlideScene::SLIDE_W, SlideScene::SLIDE_H);
    QTransform t;
    t.scale(scale, scale);
    m_view->setTransform(t);
    m_view->centerOn(SlideScene::SLIDE_W / 2.0, SlideScene::SLIDE_H / 2.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// In-editor effect previews
//
// When the user picks a transition, a whole-slide animation, or an object
// animation, we play it once right on the canvas so they get immediate visual
// confirmation that the effect was applied. The preview is rendered into a
// transient QLabel overlaid on the canvas and uses the exact same compositing
// maths as the real slide show (PresentModeWindow), so it is faithful.
// ─────────────────────────────────────────────────────────────────────────────
QPixmap ImpressModule::renderSceneToPixmap(SlideScene* scene, const QSize& sz) const {
    QPixmap pm(sz);
    pm.fill(Qt::transparent);
    if (!scene || sz.isEmpty()) return pm;
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::TextAntialiasing);
    scene->render(&p, QRectF(QPointF(0, 0), QSizeF(sz)),
                  QRectF(0, 0, SlideScene::SLIDE_W, SlideScene::SLIDE_H));
    return pm;
}

void ImpressModule::runOverlayPreview(int durationMs,
                                      const std::function<QPixmap(double)>& frame,
                                      QObject* owned) {
    if (!m_view || m_currentIdx < 0) { if (owned) owned->deleteLater(); return; }

    // The slide's on-screen rectangle inside the viewport.
    const QRect r = m_view->mapFromScene(
        QRectF(0, 0, SlideScene::SLIDE_W, SlideScene::SLIDE_H)).boundingRect();
    if (r.width() < 8 || r.height() < 8) { if (owned) owned->deleteLater(); return; }

    // Tear down any preview still in flight.
    if (m_previewAnim) { m_previewAnim->stop(); m_previewAnim = nullptr; }
    if (m_previewOverlay) { m_previewOverlay->deleteLater(); m_previewOverlay = nullptr; }

    auto* ov = new QLabel(m_view->viewport());
    ov->setAttribute(Qt::WA_TransparentForMouseEvents);
    ov->setGeometry(r);
    ov->setPixmap(frame(0.0));
    ov->show();
    ov->raise();
    m_previewOverlay = ov;
    if (owned) owned->setParent(ov);   // tie the temp scene's lifetime to the overlay

    auto* anim = new QVariantAnimation(this);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setDuration(durationMs);
    connect(anim, &QVariantAnimation::valueChanged, ov, [ov, frame](const QVariant& v) {
        ov->setPixmap(frame(v.toDouble()));
    });
    connect(anim, &QVariantAnimation::finished, this, [this, ov] {
        if (m_previewOverlay == ov) m_previewOverlay = nullptr;
        m_previewAnim = nullptr;
        ov->deleteLater();
    });
    m_previewAnim = anim;
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void ImpressModule::previewTransition(SlideTransition transition) {
    if (transition == SlideTransition::None) return;
    if (m_currentIdx < 0) return;
    const QRect r = m_view->mapFromScene(
        QRectF(0, 0, SlideScene::SLIDE_W, SlideScene::SLIDE_H)).boundingRect();
    const QSize sz = r.size();
    if (sz.isEmpty()) return;

    const QPixmap newPm = renderSceneToPixmap(m_scenes[m_currentIdx], sz);
    QPixmap oldPm;
    if (m_currentIdx > 0) {
        oldPm = renderSceneToPixmap(m_scenes[m_currentIdx - 1], sz);
    } else {
        oldPm = QPixmap(sz);
        oldPm.fill(QColor("#C6CAD3"));   // neutral backdrop when there's no prior slide
    }
    runOverlayPreview(450, [sz, oldPm, newPm, transition](double t) {
        return PresentModeWindow::compositeFrame(sz, oldPm, newPm, transition, t);
    });
}

void ImpressModule::previewSlideAnimation(SlideAnimation anim) {
    if (anim == SlideAnimation::None) return;
    if (m_currentIdx < 0) return;
    const QRect r = m_view->mapFromScene(
        QRectF(0, 0, SlideScene::SLIDE_W, SlideScene::SLIDE_H)).boundingRect();
    const QSize sz = r.size();
    if (sz.isEmpty()) return;

    const QPixmap newPm = renderSceneToPixmap(m_scenes[m_currentIdx], sz);
    QPixmap backdrop(sz);
    backdrop.fill(QColor("#C6CAD3"));    // neutral backdrop behind the entering slide
    runOverlayPreview(650, [sz, backdrop, newPm, anim](double t) {
        return PresentModeWindow::compositeSlideAnim(sz, backdrop, newPm, anim, t);
    });
}

void ImpressModule::previewObjectAnimations() {
    if (m_currentIdx < 0) return;
    syncCurrentSceneToData();

    const QRect r = m_view->mapFromScene(
        QRectF(0, 0, SlideScene::SLIDE_W, SlideScene::SLIDE_H)).boundingRect();
    const QSize sz = r.size();
    if (sz.isEmpty()) return;

    // Build a throwaway scene from the current slide data so the live editor
    // scene (and its selection handles / undo) is never disturbed.
    auto* tmp = new SlideScene(this);
    tmp->loadFromData(m_slideData[m_currentIdx]);

    struct AnimRec { QGraphicsItem* it; ItemAnimation a; PresentModeWindow::ItemNatural nat; };
    auto* recs = new std::vector<AnimRec>();   // owned via the overlay (cleaned on finish)
    for (auto* it : tmp->items()) {
        if (it->zValue() < 0) continue;        // background
        if (it->parentItem()) continue;        // child (e.g. table cell)
        const auto a = static_cast<ItemAnimation>(it->data(SlideScene::AnimationKey).toInt());
        if (a == ItemAnimation::None) continue;
        it->setTransformOriginPoint(it->boundingRect().center());
        recs->push_back({ it, a, { it->pos(), it->opacity(), it->scale(), it->rotation() } });
    }
    if (recs->empty()) { delete recs; delete tmp; return; }

    // Free the helper vector when the temp scene (its parent-to-be) dies.
    connect(tmp, &QObject::destroyed, this, [recs] { delete recs; });

    runOverlayPreview(700, [this, tmp, recs, sz](double t) {
        for (const auto& rec : *recs)
            PresentModeWindow::applyItemAnimFrame(rec.it, rec.a, rec.nat, t);
        return renderSceneToPixmap(tmp, sz);
    }, tmp);
}

void ImpressModule::applyDesignColor(const QColor& color) {
    if (m_currentIdx < 0 || !color.isValid()) return;
    auto* scene = m_scenes[m_currentIdx];

    // If the user has selected a colour-division part (a shape) on the slide,
    // recolour just that part. Otherwise recolour the whole-slide background,
    // which is the largest portion of the slide.
    if (scene->hasShapeSelection()) {
        scene->setSelectedFill(color);
    } else {
        SlideData snap; scene->saveToData(snap);
        snap.background  = color;
        snap.background2 = QColor();   // solid fill — clear any gradient
        scene->loadFromData(snap);
    }
    commitUndoStep();
}

// ─────────────────────────────────────────────────────────────────────────────
// Export all slides to PDF
// ─────────────────────────────────────────────────────────────────────────────
void ImpressModule::exportToPdf() {
    if (m_scenes.empty()) {
        QMessageBox::information(this, "Export to PDF",
            "There are no slides to export.");
        return;
    }

    syncCurrentSceneToData();

    const QString path = QFileDialog::getSaveFileName(
        this,
        "Export Presentation to PDF",
        QDir::homePath() + "/Presentation.pdf",
        "PDF Document (*.pdf)");

    if (path.isEmpty()) return;

    QPdfWriter writer(path);
    writer.setCreator("NativeOffice Impress");
    writer.setTitle("Presentation");
    writer.setPageSize(QPageSize(QSizeF(338.667, 190.5), QPageSize::Millimeter,
                                  "Widescreen 16:9"));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    writer.setResolution(150);

    QPainter painter(&writer);
    if (!painter.isActive()) {
        QMessageBox::critical(this, "Export Failed",
            "Could not open the PDF file for writing:\n" + path);
        return;
    }
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    for (int i = 0; i < static_cast<int>(m_scenes.size()); ++i) {
        if (i > 0)
            writer.newPage();

        const QRect pageRect = painter.viewport();
        const qreal aspect = SlideScene::SLIDE_W / SlideScene::SLIDE_H;
        QRectF target = pageRect;
        if (target.width() / target.height() > aspect) {
            const qreal w = target.height() * aspect;
            target.setLeft(target.left() + (target.width() - w) / 2.0);
            target.setWidth(w);
        } else {
            const qreal h = target.width() / aspect;
            target.setTop(target.top() + (target.height() - h) / 2.0);
            target.setHeight(h);
        }

        painter.fillRect(pageRect, Qt::white);
        m_scenes[i]->render(&painter, target,
                            QRectF(0, 0, SlideScene::SLIDE_W, SlideScene::SLIDE_H));
    }

    painter.end();

    QMessageBox::information(this, "Export Complete",
        QString("Presentation exported to PDF successfully!\n\n%1 slide(s) → %2")
            .arg(m_scenes.size())
            .arg(path));
}

// ─────────────────────────────────────────────────────────────────────────────
// Export to PowerPoint (.pptx) — OOXML package written with a minimal,
// self-contained store-only ZIP writer (no external deps). Produces a valid
// package that PowerPoint, WPS and LibreOffice open cleanly: one slide master,
// one blank layout, a standard theme, and one slide per deck slide carrying
// the background (solid or two-colour gradient), text boxes, rectangles,
// ellipses and images.
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// ── Tiny store-only (uncompressed) ZIP writer ───────────────────────────────
quint32 zipCrc32(const QByteArray& data) {
    static quint32 table[256];
    static bool init = false;
    if (!init) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    quint32 c = 0xFFFFFFFFu;
    for (int i = 0; i < data.size(); ++i)
        c = table[(c ^ static_cast<unsigned char>(data[i])) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void putU16(QByteArray& b, quint16 v) {
    b.append(char(v & 0xFF)); b.append(char((v >> 8) & 0xFF));
}
void putU32(QByteArray& b, quint32 v) {
    for (int i = 0; i < 4; ++i) b.append(char((v >> (8 * i)) & 0xFF));
}

class Zip {
public:
    void add(const QString& name, const QByteArray& data) {
        Entry e;
        e.name   = name.toUtf8();
        e.crc    = zipCrc32(data);
        e.size   = static_cast<quint32>(data.size());
        e.offset = static_cast<quint32>(m_buf.size());

        QByteArray h;
        putU32(h, 0x04034b50);          // local file header signature
        putU16(h, 20); putU16(h, 0);    // version needed, flags
        putU16(h, 0);                   // method: store
        putU16(h, 0); putU16(h, 0);     // mod time, date
        putU32(h, e.crc);
        putU32(h, e.size); putU32(h, e.size);
        putU16(h, static_cast<quint16>(e.name.size()));
        putU16(h, 0);                   // extra len
        h.append(e.name);
        m_buf.append(h);
        m_buf.append(data);
        m_entries.append(e);
    }
    QByteArray finish() {
        const quint32 cdStart = static_cast<quint32>(m_buf.size());
        QByteArray cd;
        for (const Entry& e : m_entries) {
            QByteArray h;
            putU32(h, 0x02014b50);      // central directory header signature
            putU16(h, 20); putU16(h, 20);
            putU16(h, 0); putU16(h, 0);
            putU16(h, 0); putU16(h, 0);
            putU32(h, e.crc);
            putU32(h, e.size); putU32(h, e.size);
            putU16(h, static_cast<quint16>(e.name.size()));
            putU16(h, 0); putU16(h, 0); // extra, comment
            putU16(h, 0); putU16(h, 0); // disk start, internal attrs
            putU32(h, 0);               // external attrs
            putU32(h, e.offset);
            h.append(e.name);
            cd.append(h);
        }
        m_buf.append(cd);
        QByteArray eocd;
        putU32(eocd, 0x06054b50);
        putU16(eocd, 0); putU16(eocd, 0);
        putU16(eocd, static_cast<quint16>(m_entries.size()));
        putU16(eocd, static_cast<quint16>(m_entries.size()));
        putU32(eocd, static_cast<quint32>(cd.size()));
        putU32(eocd, cdStart);
        putU16(eocd, 0);
        m_buf.append(eocd);
        return m_buf;
    }
private:
    struct Entry { QByteArray name; quint32 crc; quint32 size; quint32 offset; };
    QByteArray      m_buf;
    QList<Entry>    m_entries;
};

QString xmlEscape(const QString& s) {
    QString o = s;
    o.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;")
     .replace('"', "&quot;").replace('\'', "&apos;");
    return o;
}

// scene pixels → EMU (914400 EMU / inch, 96 px / inch ⇒ 9525 EMU / px)
qint64 emu(double px) { return static_cast<qint64>(px * 9525.0); }

QString hex6(const QColor& c) { return c.name(QColor::HexRgb).mid(1).toUpper(); }

// Map a gallery ShapeKind to its PowerPoint preset geometry name.
QString prstForShape(ShapeKind k) {
    switch (k) {
    case ShapeKind::Rectangle:     return "rect";
    case ShapeKind::RoundedRect:   return "roundRect";
    case ShapeKind::Ellipse:       return "ellipse";
    case ShapeKind::Triangle:      return "triangle";
    case ShapeKind::RightTriangle: return "rtTriangle";
    case ShapeKind::Diamond:       return "diamond";
    case ShapeKind::Pentagon:      return "pentagon";
    case ShapeKind::Hexagon:       return "hexagon";
    case ShapeKind::Star5:         return "star5";
    case ShapeKind::Arrow:         return "rightArrow";
    case ShapeKind::Chevron:       return "chevron";
    case ShapeKind::Cloud:         return "cloud";
    case ShapeKind::Heart:         return "heart";
    case ShapeKind::Line:          return "line";
    }
    return "rect";
}

QString xfrmXml(const QRectF& r, qreal rotDeg) {
    QString s = "<a:xfrm";
    if (!qFuzzyIsNull(rotDeg))
        s += QString(" rot=\"%1\"").arg(static_cast<qint64>(rotDeg * 60000.0));
    s += QString(">"
                 "<a:off x=\"%1\" y=\"%2\"/>"
                 "<a:ext cx=\"%3\" cy=\"%4\"/>"
                 "</a:xfrm>")
            .arg(emu(r.x())).arg(emu(r.y()))
            .arg(emu(std::max(1.0, r.width()))).arg(emu(std::max(1.0, r.height())));
    return s;
}

const char* kRelNs   = "http://schemas.openxmlformats.org/package/2006/relationships";
const char* kRelBase = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";

} // namespace

void ImpressModule::exportToPptx() {
    if (m_scenes.empty()) {
        QMessageBox::information(this, "Save as PowerPoint",
            "There are no slides to export.");
        return;
    }

    const QString suggested =
        (m_currentPath.isEmpty()
             ? QDir::homePath() + "/Presentation.pptx"
             : QFileInfo(m_currentPath).absolutePath() + "/"
                   + QFileInfo(m_currentPath).completeBaseName() + ".pptx");

    const QString path = QFileDialog::getSaveFileName(
        this, "Save as PowerPoint", suggested, "PowerPoint Presentation (*.pptx)");
    if (path.isEmpty()) return;

    if (!exportPptxTo(path)) {
        QMessageBox::critical(this, "Save Failed",
            "Could not write the PowerPoint file:\n" + path);
        return;
    }
    QMessageBox::information(this, "Saved",
        QString("Presentation saved as PowerPoint successfully!\n\n%1 slide(s) → %2")
            .arg(m_slideData.size()).arg(path));
}

bool ImpressModule::exportPptxTo(const QString& path) {
    if (m_scenes.empty()) return false;

    // Snapshot every live scene back into the data deck first.
    syncCurrentSceneToData();
    for (int i = 0; i < static_cast<int>(m_scenes.size()); ++i)
        m_scenes[i]->saveToData(m_slideData[i]);

    const int n = static_cast<int>(m_slideData.size());

    Zip zip;

    // ── [Content_Types].xml ─────────────────────────────────────────────
    {
        QString ct = QString(
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
            "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
            "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
            "<Default Extension=\"png\" ContentType=\"image/png\"/>"
            "<Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/>"
            "<Default Extension=\"jpg\" ContentType=\"image/jpeg\"/>"
            "<Override PartName=\"/ppt/presentation.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml\"/>"
            "<Override PartName=\"/ppt/slideMasters/slideMaster1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slideMaster+xml\"/>"
            "<Override PartName=\"/ppt/slideLayouts/slideLayout1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slideLayout+xml\"/>"
            "<Override PartName=\"/ppt/theme/theme1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.theme+xml\"/>");
        for (int s = 1; s <= n; ++s)
            ct += QString("<Override PartName=\"/ppt/slides/slide%1.xml\" "
                          "ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slide+xml\"/>").arg(s);
        ct += "</Types>";
        zip.add("[Content_Types].xml", ct.toUtf8());
    }

    // ── _rels/.rels ─────────────────────────────────────────────────────
    zip.add("_rels/.rels", QString(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"%1\">"
        "<Relationship Id=\"rId1\" Type=\"%2/officeDocument\" Target=\"ppt/presentation.xml\"/>"
        "</Relationships>").arg(kRelNs, kRelBase).toUtf8());

    // ── ppt/presentation.xml (+ rels) ───────────────────────────────────
    {
        QString sldIds;
        QString relItems = QString(
            "<Relationship Id=\"rId1\" Type=\"%1/slideMaster\" Target=\"slideMasters/slideMaster1.xml\"/>")
            .arg(kRelBase);
        for (int s = 1; s <= n; ++s) {
            const int rid = s + 1;                 // rId2..
            sldIds += QString("<p:sldId id=\"%1\" r:id=\"rId%2\"/>").arg(255 + s).arg(rid);
            relItems += QString("<Relationship Id=\"rId%1\" Type=\"%2/slide\" Target=\"slides/slide%3.xml\"/>")
                            .arg(rid).arg(kRelBase).arg(s);
        }
        relItems += QString("<Relationship Id=\"rId%1\" Type=\"%2/theme\" Target=\"theme/theme1.xml\"/>")
                        .arg(n + 2).arg(kRelBase);

        zip.add("ppt/presentation.xml", QString(
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<p:presentation xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
            "xmlns:r=\"%1\" "
            "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\">"
            "<p:sldMasterIdLst><p:sldMasterId id=\"2147483648\" r:id=\"rId1\"/></p:sldMasterIdLst>"
            "<p:sldIdLst>%2</p:sldIdLst>"
            "<p:sldSz cx=\"9144000\" cy=\"5143500\" type=\"screen16x9\"/>"
            "<p:notesSz cx=\"6858000\" cy=\"9144000\"/>"
            "</p:presentation>").arg(kRelBase, sldIds).toUtf8());

        zip.add("ppt/_rels/presentation.xml.rels", QString(
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<Relationships xmlns=\"%1\">%2</Relationships>").arg(kRelNs, relItems).toUtf8());
    }

    // ── ppt/theme/theme1.xml ────────────────────────────────────────────
    zip.add("ppt/theme/theme1.xml", QString(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<a:theme xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" name=\"Office Theme\">"
        "<a:themeElements>"
        "<a:clrScheme name=\"Office\">"
        "<a:dk1><a:sysClr val=\"windowText\" lastClr=\"000000\"/></a:dk1>"
        "<a:lt1><a:sysClr val=\"window\" lastClr=\"FFFFFF\"/></a:lt1>"
        "<a:dk2><a:srgbClr val=\"44546A\"/></a:dk2><a:lt2><a:srgbClr val=\"E7E6E6\"/></a:lt2>"
        "<a:accent1><a:srgbClr val=\"4472C4\"/></a:accent1><a:accent2><a:srgbClr val=\"ED7D31\"/></a:accent2>"
        "<a:accent3><a:srgbClr val=\"A5A5A5\"/></a:accent3><a:accent4><a:srgbClr val=\"FFC000\"/></a:accent4>"
        "<a:accent5><a:srgbClr val=\"5B9BD5\"/></a:accent5><a:accent6><a:srgbClr val=\"70AD47\"/></a:accent6>"
        "<a:hlink><a:srgbClr val=\"0563C1\"/></a:hlink><a:folHlink><a:srgbClr val=\"954F72\"/></a:folHlink>"
        "</a:clrScheme>"
        "<a:fontScheme name=\"Office\">"
        "<a:majorFont><a:latin typeface=\"Calibri Light\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:majorFont>"
        "<a:minorFont><a:latin typeface=\"Calibri\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:minorFont>"
        "</a:fontScheme>"
        "<a:fmtScheme name=\"Office\">"
        "<a:fillStyleLst>"
        "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
        "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
        "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
        "</a:fillStyleLst>"
        "<a:lnStyleLst>"
        "<a:ln w=\"6350\" cap=\"flat\" cmpd=\"sng\" algn=\"ctr\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:prstDash val=\"solid\"/></a:ln>"
        "<a:ln w=\"12700\" cap=\"flat\" cmpd=\"sng\" algn=\"ctr\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:prstDash val=\"solid\"/></a:ln>"
        "<a:ln w=\"19050\" cap=\"flat\" cmpd=\"sng\" algn=\"ctr\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:prstDash val=\"solid\"/></a:ln>"
        "</a:lnStyleLst>"
        "<a:effectStyleLst>"
        "<a:effectStyle><a:effectLst/></a:effectStyle>"
        "<a:effectStyle><a:effectLst/></a:effectStyle>"
        "<a:effectStyle><a:effectLst/></a:effectStyle>"
        "</a:effectStyleLst>"
        "<a:bgFillStyleLst>"
        "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
        "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
        "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
        "</a:bgFillStyleLst>"
        "</a:fmtScheme>"
        "</a:themeElements>"
        "</a:theme>").toUtf8());

    // ── ppt/slideMasters/slideMaster1.xml (+ rels) ──────────────────────
    zip.add("ppt/slideMasters/slideMaster1.xml", QString(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<p:sldMaster xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
        "xmlns:r=\"%1\" "
        "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\">"
        "<p:cSld>"
        "<p:bg><p:bgRef idx=\"1001\"><a:schemeClr val=\"bg1\"/></p:bgRef></p:bg>"
        "<p:spTree>"
        "<p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>"
        "<p:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/>"
        "<a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr>"
        "</p:spTree>"
        "</p:cSld>"
        "<p:clrMap bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" tx2=\"dk2\" accent1=\"accent1\" "
        "accent2=\"accent2\" accent3=\"accent3\" accent4=\"accent4\" accent5=\"accent5\" "
        "accent6=\"accent6\" hlink=\"hlink\" folHlink=\"folHlink\"/>"
        "<p:sldLayoutIdLst><p:sldLayoutId id=\"2147483649\" r:id=\"rId1\"/></p:sldLayoutIdLst>"
        "<p:txStyles><p:titleStyle/><p:bodyStyle/><p:otherStyle/></p:txStyles>"
        "</p:sldMaster>").arg(kRelBase).toUtf8());

    zip.add("ppt/slideMasters/_rels/slideMaster1.xml.rels", QString(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"%1\">"
        "<Relationship Id=\"rId1\" Type=\"%2/slideLayout\" Target=\"../slideLayouts/slideLayout1.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"%2/theme\" Target=\"../theme/theme1.xml\"/>"
        "</Relationships>").arg(kRelNs, kRelBase).toUtf8());

    // ── ppt/slideLayouts/slideLayout1.xml (+ rels) ──────────────────────
    zip.add("ppt/slideLayouts/slideLayout1.xml", QString(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<p:sldLayout xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
        "xmlns:r=\"%1\" "
        "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\" "
        "type=\"blank\" preserve=\"1\">"
        "<p:cSld name=\"Blank\">"
        "<p:spTree>"
        "<p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>"
        "<p:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/>"
        "<a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr>"
        "</p:spTree>"
        "</p:cSld>"
        "<p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr>"
        "</p:sldLayout>").arg(kRelBase).toUtf8());

    zip.add("ppt/slideLayouts/_rels/slideLayout1.xml.rels", QString(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"%1\">"
        "<Relationship Id=\"rId1\" Type=\"%2/slideMaster\" Target=\"../slideMasters/slideMaster1.xml\"/>"
        "</Relationships>").arg(kRelNs, kRelBase).toUtf8());

    // ── Slides ──────────────────────────────────────────────────────────
    int mediaCounter = 0;
    for (int s = 0; s < n; ++s) {
        const SlideData& slide = m_slideData[s];

        // Background fill (solid or two-colour vertical gradient).
        QString bg;
        {
            QString fill;
            if (slide.background2.isValid()) {
                fill = QString(
                    "<a:gradFill rotWithShape=\"1\"><a:gsLst>"
                    "<a:gs pos=\"0\"><a:srgbClr val=\"%1\"/></a:gs>"
                    "<a:gs pos=\"100000\"><a:srgbClr val=\"%2\"/></a:gs>"
                    "</a:gsLst><a:lin ang=\"5400000\" scaled=\"0\"/></a:gradFill>")
                    .arg(hex6(slide.background), hex6(slide.background2));
            } else {
                fill = QString("<a:solidFill><a:srgbClr val=\"%1\"/></a:solidFill>")
                           .arg(hex6(slide.background));
            }
            bg = "<p:bg><p:bgPr>" + fill + "<a:effectLst/></p:bgPr></p:bg>";
        }

        QString shapes;
        QString slideRels = QString(
            "<Relationship Id=\"rId1\" Type=\"%1/slideLayout\" Target=\"../slideLayouts/slideLayout1.xml\"/>")
            .arg(kRelBase);
        int shapeId = 2;
        int imgRel  = 1;          // rId2.. reserved for images on this slide

        for (const SlideItem& item : slide.items) {
            switch (item.type) {
            case SlideItemType::TextBox: {
                const int sz = std::max(1, static_cast<int>(item.fontSize * 100));
                const bool bold = item.html.contains("font-weight:6") ||
                                  item.html.contains("font-weight:7") ||
                                  item.html.contains("font-weight:8") ||
                                  item.html.contains("font-weight:9") ||
                                  item.html.contains("font-weight:bold");
                const bool ital = item.html.contains("font-style:italic");
                const QString col = hex6(item.penColor);

                QString body;
                const QStringList lines = item.text.split('\n');
                for (const QString& line : lines) {
                    body += "<a:p>";
                    if (!line.isEmpty()) {
                        body += QString("<a:r><a:rPr lang=\"en-US\" sz=\"%1\" b=\"%2\" i=\"%3\" dirty=\"0\">"
                                        "<a:solidFill><a:srgbClr val=\"%4\"/></a:solidFill></a:rPr>"
                                        "<a:t>%5</a:t></a:r>")
                                    .arg(sz).arg(bold ? 1 : 0).arg(ital ? 1 : 0)
                                    .arg(col, xmlEscape(line));
                    }
                    body += "</a:p>";
                }
                if (lines.isEmpty()) body = "<a:p/>";

                shapes += QString(
                    "<p:sp><p:nvSpPr><p:cNvPr id=\"%1\" name=\"TextBox %1\"/>"
                    "<p:cNvSpPr txBox=\"1\"/><p:nvPr/></p:nvSpPr>"
                    "<p:spPr>%2<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom><a:noFill/></p:spPr>"
                    "<p:txBody><a:bodyPr wrap=\"square\" rtlCol=\"0\"/><a:lstStyle/>%3</p:txBody>"
                    "</p:sp>")
                    .arg(shapeId).arg(xfrmXml(item.rect, item.rotation), body);
                ++shapeId;
                break;
            }
            case SlideItemType::Rectangle:
            case SlideItemType::Ellipse: {
                const QString prst = (item.type == SlideItemType::Rectangle) ? "rect" : "ellipse";
                const int fillAlpha = static_cast<int>(item.fillColor.alphaF() * 100000);
                const qint64 lnW = emu(std::max(0.5, item.penWidth));
                shapes += QString(
                    "<p:sp><p:nvSpPr><p:cNvPr id=\"%1\" name=\"Shape %1\"/>"
                    "<p:cNvSpPr/><p:nvPr/></p:nvSpPr>"
                    "<p:spPr>%2<a:prstGeom prst=\"%3\"><a:avLst/></a:prstGeom>"
                    "<a:solidFill><a:srgbClr val=\"%4\"><a:alpha val=\"%5\"/></a:srgbClr></a:solidFill>"
                    "<a:ln w=\"%6\"><a:solidFill><a:srgbClr val=\"%7\"/></a:solidFill></a:ln></p:spPr>"
                    "<p:txBody><a:bodyPr rtlCol=\"0\"/><a:lstStyle/><a:p/></p:txBody></p:sp>")
                    .arg(shapeId).arg(xfrmXml(item.rect, item.rotation), prst,
                                       hex6(item.fillColor)).arg(fillAlpha).arg(lnW)
                    .arg(hex6(item.penColor));
                ++shapeId;
                break;
            }
            case SlideItemType::Shape: {
                const int fillAlpha = static_cast<int>(item.fillColor.alphaF() * 100000);
                const qint64 lnW = emu(std::max(0.5, item.penWidth));
                if (item.shapeKind == ShapeKind::Line) {
                    shapes += QString(
                        "<p:cxnSp><p:nvCxnSpPr><p:cNvPr id=\"%1\" name=\"Line %1\"/>"
                        "<p:cNvCxnSpPr/><p:nvPr/></p:nvCxnSpPr>"
                        "<p:spPr>%2<a:prstGeom prst=\"line\"><a:avLst/></a:prstGeom>"
                        "<a:ln w=\"%3\"><a:solidFill><a:srgbClr val=\"%4\"/></a:solidFill></a:ln></p:spPr>"
                        "</p:cxnSp>")
                        .arg(shapeId).arg(xfrmXml(item.rect, item.rotation)).arg(lnW)
                        .arg(hex6(item.penColor));
                } else {
                    shapes += QString(
                        "<p:sp><p:nvSpPr><p:cNvPr id=\"%1\" name=\"Shape %1\"/>"
                        "<p:cNvSpPr/><p:nvPr/></p:nvSpPr>"
                        "<p:spPr>%2<a:prstGeom prst=\"%3\"><a:avLst/></a:prstGeom>"
                        "<a:solidFill><a:srgbClr val=\"%4\"><a:alpha val=\"%5\"/></a:srgbClr></a:solidFill>"
                        "<a:ln w=\"%6\"><a:solidFill><a:srgbClr val=\"%7\"/></a:solidFill></a:ln></p:spPr>"
                        "<p:txBody><a:bodyPr rtlCol=\"0\"/><a:lstStyle/><a:p/></p:txBody></p:sp>")
                        .arg(shapeId).arg(xfrmXml(item.rect, item.rotation),
                                           prstForShape(item.shapeKind), hex6(item.fillColor))
                        .arg(fillAlpha).arg(lnW).arg(hex6(item.penColor));
                }
                ++shapeId;
                break;
            }
            case SlideItemType::Table: {
                const int rws = std::max(1, item.rows);
                const int cls = std::max(1, item.cols);
                const qint64 colW = emu(item.rect.width()  / cls);
                const qint64 rowH = emu(item.rect.height() / rws);

                QString grid;
                for (int c = 0; c < cls; ++c)
                    grid += QString("<a:gridCol w=\"%1\"/>").arg(colW);

                QString trs;
                for (int r = 0; r < rws; ++r) {
                    QString tcs;
                    for (int c = 0; c < cls; ++c) {
                        const int idx = r * cls + c;
                        const QString txt = (idx < static_cast<int>(item.cells.size()))
                                                ? item.cells[idx] : QString();
                        const QString para = txt.isEmpty()
                            ? "<a:p/>"
                            : QString("<a:p><a:r><a:rPr lang=\"en-US\" dirty=\"0\"/>"
                                      "<a:t>%1</a:t></a:r></a:p>").arg(xmlEscape(txt));
                        tcs += QString(
                            "<a:tc><a:txBody><a:bodyPr/><a:lstStyle/>%1</a:txBody>"
                            "<a:tcPr/></a:tc>").arg(para);
                    }
                    trs += QString("<a:tr h=\"%1\">%2</a:tr>").arg(rowH).arg(tcs);
                }

                shapes += QString(
                    "<p:graphicFrame><p:nvGraphicFramePr>"
                    "<p:cNvPr id=\"%1\" name=\"Table %1\"/><p:cNvGraphicFramePr/><p:nvPr/>"
                    "</p:nvGraphicFramePr>"
                    "<p:xfrm><a:off x=\"%2\" y=\"%3\"/><a:ext cx=\"%4\" cy=\"%5\"/></p:xfrm>"
                    "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/table\">"
                    "<a:tbl><a:tblPr firstRow=\"1\" bandRow=\"1\"/>"
                    "<a:tblGrid>%6</a:tblGrid>%7</a:tbl>"
                    "</a:graphicData></a:graphic></p:graphicFrame>")
                    .arg(shapeId)
                    .arg(emu(item.rect.x())).arg(emu(item.rect.y()))
                    .arg(emu(item.rect.width())).arg(emu(item.rect.height()))
                    .arg(grid, trs);
                ++shapeId;
                break;
            }
            case SlideItemType::Chart: {
                // Rasterise the chart to a PNG and embed it as a picture — robust
                // and renders identically in every viewer.
                const int pxW = std::max(64, static_cast<int>(item.rect.width()  * 2));
                const int pxH = std::max(64, static_cast<int>(item.rect.height() * 2));
                QImage img(pxW, pxH, QImage::Format_ARGB32);
                img.fill(Qt::transparent);
                {
                    QPainter cp(&img);
                    SlideScene::paintChart(cp, QRectF(0, 0, pxW, pxH), item.chartKind,
                                           item.chartLabels, item.chartValues, item.chartTitle);
                }
                QByteArray chartPng;
                QBuffer cbuf(&chartPng);
                cbuf.open(QIODevice::WriteOnly);
                img.save(&cbuf, "PNG");

                ++mediaCounter;
                const QString mediaName = QString("image%1.png").arg(mediaCounter);
                zip.add("ppt/media/" + mediaName, chartPng);
                ++imgRel;
                const QString rid = QString("rId%1").arg(imgRel);
                slideRels += QString("<Relationship Id=\"%1\" Type=\"%2/image\" Target=\"../media/%3\"/>")
                                 .arg(rid, kRelBase, mediaName);
                shapes += QString(
                    "<p:pic><p:nvPicPr><p:cNvPr id=\"%1\" name=\"Chart %1\"/>"
                    "<p:cNvPicPr/><p:nvPr/></p:nvPicPr>"
                    "<p:blipFill><a:blip r:embed=\"%2\"/><a:stretch><a:fillRect/></a:stretch></p:blipFill>"
                    "<p:spPr>%3<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></p:spPr></p:pic>")
                    .arg(shapeId).arg(rid, xfrmXml(item.rect, item.rotation));
                ++shapeId;
                break;
            }
            case SlideItemType::Image: {
                if (item.imageData.isEmpty()) break;
                ++mediaCounter;
                const QString mediaName = QString("image%1.png").arg(mediaCounter);
                zip.add("ppt/media/" + mediaName, item.imageData);
                ++imgRel;
                const QString rid = QString("rId%1").arg(imgRel);
                slideRels += QString("<Relationship Id=\"%1\" Type=\"%2/image\" Target=\"../media/%3\"/>")
                                 .arg(rid, kRelBase, mediaName);
                shapes += QString(
                    "<p:pic><p:nvPicPr><p:cNvPr id=\"%1\" name=\"Picture %1\"/>"
                    "<p:cNvPicPr/><p:nvPr/></p:nvPicPr>"
                    "<p:blipFill><a:blip r:embed=\"%2\"/><a:stretch><a:fillRect/></a:stretch></p:blipFill>"
                    "<p:spPr>%3<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></p:spPr></p:pic>")
                    .arg(shapeId).arg(rid, xfrmXml(item.rect, item.rotation));
                ++shapeId;
                break;
            }
            }
        }

        const QString slideXml = QString(
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<p:sld xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
            "xmlns:r=\"%1\" "
            "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\">"
            "<p:cSld>%2"
            "<p:spTree>"
            "<p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>"
            "<p:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/>"
            "<a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr>"
            "%3"
            "</p:spTree>"
            "</p:cSld>"
            "<p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr>"
            "</p:sld>").arg(kRelBase, bg, shapes);

        zip.add(QString("ppt/slides/slide%1.xml").arg(s + 1), slideXml.toUtf8());
        zip.add(QString("ppt/slides/_rels/slide%1.xml.rels").arg(s + 1), QString(
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<Relationships xmlns=\"%1\">%2</Relationships>").arg(kRelNs, slideRels).toUtf8());
    }

    const QByteArray pkg = zip.finish();
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(pkg);
    f.close();

    // Saving to .pptx adopts that file as the working document (it is now the
    // native format on disk), so a subsequent plain Save round-trips to it.
    m_currentPath = path;
    m_dirty       = false;
    emit filePathChanged(path);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// File I/O — JSON schema v2
// ─────────────────────────────────────────────────────────────────────────────
QString ImpressModule::titleString() const {
    const QString base = m_currentPath.isEmpty()
                             ? "Untitled Presentation"
                             : QFileInfo(m_currentPath).fileName();
    return (m_dirty ? "* " : "") + base + " — NativeOffice Impress";
}

void ImpressModule::markClean() {
    m_dirty = false;
}

void ImpressModule::clearDeck() {
    m_view->setScene(nullptr);
    for (auto* s : m_scenes)
        delete s;
    m_scenes.clear();
    m_slideData.clear();
    m_currentIdx = -1;
    m_slidePanel->clear();
}

namespace {

QString itemTypeToString(SlideItemType t) {
    switch (t) {
    case SlideItemType::Rectangle: return "rectangle";
    case SlideItemType::Ellipse:   return "ellipse";
    case SlideItemType::Image:     return "image";
    case SlideItemType::Shape:     return "shape";
    case SlideItemType::Table:     return "table";
    case SlideItemType::Chart:     return "chart";
    case SlideItemType::TextBox:
    default:                       return "textbox";
    }
}

SlideItemType itemTypeFromString(const QString& s) {
    if (s == "rectangle") return SlideItemType::Rectangle;
    if (s == "ellipse")   return SlideItemType::Ellipse;
    if (s == "image")     return SlideItemType::Image;
    if (s == "shape")     return SlideItemType::Shape;
    if (s == "table")     return SlideItemType::Table;
    if (s == "chart")     return SlideItemType::Chart;
    return SlideItemType::TextBox;
}

QString chartKindToString(ChartKind k) {
    switch (k) {
    case ChartKind::Line: return "line";
    case ChartKind::Pie:  return "pie";
    case ChartKind::Area: return "area";
    case ChartKind::Bar:
    default:              return "bar";
    }
}

ChartKind chartKindFromString(const QString& s) {
    if (s == "line") return ChartKind::Line;
    if (s == "pie")  return ChartKind::Pie;
    if (s == "area") return ChartKind::Area;
    return ChartKind::Bar;
}

QString shapeKindToString(ShapeKind k) {
    switch (k) {
    case ShapeKind::Rectangle:     return "rectangle";
    case ShapeKind::RoundedRect:   return "roundedRect";
    case ShapeKind::Ellipse:       return "ellipse";
    case ShapeKind::Triangle:      return "triangle";
    case ShapeKind::RightTriangle: return "rightTriangle";
    case ShapeKind::Diamond:       return "diamond";
    case ShapeKind::Pentagon:      return "pentagon";
    case ShapeKind::Hexagon:       return "hexagon";
    case ShapeKind::Star5:         return "star5";
    case ShapeKind::Arrow:         return "arrow";
    case ShapeKind::Chevron:       return "chevron";
    case ShapeKind::Line:          return "line";
    case ShapeKind::Cloud:         return "cloud";
    case ShapeKind::Heart:         return "heart";
    }
    return "rectangle";
}

ShapeKind shapeKindFromString(const QString& s) {
    if (s == "roundedRect")   return ShapeKind::RoundedRect;
    if (s == "ellipse")       return ShapeKind::Ellipse;
    if (s == "triangle")      return ShapeKind::Triangle;
    if (s == "rightTriangle") return ShapeKind::RightTriangle;
    if (s == "diamond")       return ShapeKind::Diamond;
    if (s == "pentagon")      return ShapeKind::Pentagon;
    if (s == "hexagon")       return ShapeKind::Hexagon;
    if (s == "star5")         return ShapeKind::Star5;
    if (s == "arrow")         return ShapeKind::Arrow;
    if (s == "chevron")       return ShapeKind::Chevron;
    if (s == "line")          return ShapeKind::Line;
    if (s == "cloud")         return ShapeKind::Cloud;
    if (s == "heart")         return ShapeKind::Heart;
    return ShapeKind::Rectangle;
}

QString layoutToString(SlideLayout l) {
    switch (l) {
    case SlideLayout::Title:        return "title";
    case SlideLayout::TitleContent: return "titleContent";
    case SlideLayout::Blank:
    default:                        return "blank";
    }
}

SlideLayout layoutFromString(const QString& s) {
    if (s == "title")        return SlideLayout::Title;
    if (s == "titleContent") return SlideLayout::TitleContent;
    return SlideLayout::Blank;
}

QString transitionToString(SlideTransition t) {
    switch (t) {
    case SlideTransition::Fade:     return "fade";
    case SlideTransition::Push:     return "push";
    case SlideTransition::Wipe:     return "wipe";
    case SlideTransition::Zoom:     return "zoom";
    case SlideTransition::Cut:      return "cut";
    case SlideTransition::Cover:    return "cover";
    case SlideTransition::Uncover:  return "uncover";
    case SlideTransition::Dissolve: return "dissolve";
    case SlideTransition::Blinds:   return "blinds";
    case SlideTransition::None:
    default:                        return "none";
    }
}

SlideTransition transitionFromString(const QString& s) {
    if (s == "fade")     return SlideTransition::Fade;
    if (s == "push")     return SlideTransition::Push;
    if (s == "wipe")     return SlideTransition::Wipe;
    if (s == "zoom")     return SlideTransition::Zoom;
    if (s == "cut")      return SlideTransition::Cut;
    if (s == "cover")    return SlideTransition::Cover;
    if (s == "uncover")  return SlideTransition::Uncover;
    if (s == "dissolve") return SlideTransition::Dissolve;
    if (s == "blinds")   return SlideTransition::Blinds;
    return SlideTransition::None;
}

QString slideAnimationToString(SlideAnimation a) {
    switch (a) {
    case SlideAnimation::FadeIn:      return "fadeIn";
    case SlideAnimation::ZoomIn:      return "zoomIn";
    case SlideAnimation::WhirlIn:     return "whirlIn";
    case SlideAnimation::FlyInLeft:   return "flyInLeft";
    case SlideAnimation::FlyInRight:  return "flyInRight";
    case SlideAnimation::FlyInTop:    return "flyInTop";
    case SlideAnimation::FlyInBottom: return "flyInBottom";
    case SlideAnimation::Bounce:      return "bounce";
    case SlideAnimation::RiseUp:      return "riseUp";
    case SlideAnimation::SpiralIn:    return "spiralIn";
    case SlideAnimation::Drop:        return "drop";
    case SlideAnimation::None:
    default:                          return "none";
    }
}

SlideAnimation slideAnimationFromString(const QString& s) {
    if (s == "fadeIn")      return SlideAnimation::FadeIn;
    if (s == "zoomIn")      return SlideAnimation::ZoomIn;
    if (s == "whirlIn")     return SlideAnimation::WhirlIn;
    if (s == "flyInLeft")   return SlideAnimation::FlyInLeft;
    if (s == "flyInRight")  return SlideAnimation::FlyInRight;
    if (s == "flyInTop")    return SlideAnimation::FlyInTop;
    if (s == "flyInBottom") return SlideAnimation::FlyInBottom;
    if (s == "bounce")      return SlideAnimation::Bounce;
    if (s == "riseUp")      return SlideAnimation::RiseUp;
    if (s == "spiralIn")    return SlideAnimation::SpiralIn;
    if (s == "drop")        return SlideAnimation::Drop;
    return SlideAnimation::None;
}

QString animationToString(ItemAnimation a) {
    switch (a) {
    case ItemAnimation::FadeIn:      return "fadeIn";
    case ItemAnimation::FlyInLeft:   return "flyInLeft";
    case ItemAnimation::ZoomIn:      return "zoomIn";
    case ItemAnimation::FlyInRight:  return "flyInRight";
    case ItemAnimation::FlyInTop:    return "flyInTop";
    case ItemAnimation::FlyInBottom: return "flyInBottom";
    case ItemAnimation::SpinIn:        return "spinIn";
    case ItemAnimation::EmphasisPulse: return "emphasisPulse";
    case ItemAnimation::EmphasisSpin:  return "emphasisSpin";
    case ItemAnimation::EmphasisBlink: return "emphasisBlink";
    case ItemAnimation::ExitFadeOut:   return "exitFadeOut";
    case ItemAnimation::ExitFlyLeft:   return "exitFlyLeft";
    case ItemAnimation::ExitFlyRight:  return "exitFlyRight";
    case ItemAnimation::ExitZoomOut:   return "exitZoomOut";
    case ItemAnimation::None:
    default:                         return "none";
    }
}

ItemAnimation animationFromString(const QString& s) {
    if (s == "fadeIn")      return ItemAnimation::FadeIn;
    if (s == "flyInLeft")   return ItemAnimation::FlyInLeft;
    if (s == "zoomIn")      return ItemAnimation::ZoomIn;
    if (s == "flyInRight")  return ItemAnimation::FlyInRight;
    if (s == "flyInTop")    return ItemAnimation::FlyInTop;
    if (s == "flyInBottom") return ItemAnimation::FlyInBottom;
    if (s == "spinIn")        return ItemAnimation::SpinIn;
    if (s == "emphasisPulse") return ItemAnimation::EmphasisPulse;
    if (s == "emphasisSpin")  return ItemAnimation::EmphasisSpin;
    if (s == "emphasisBlink") return ItemAnimation::EmphasisBlink;
    if (s == "exitFadeOut")   return ItemAnimation::ExitFadeOut;
    if (s == "exitFlyLeft")   return ItemAnimation::ExitFlyLeft;
    if (s == "exitFlyRight")  return ItemAnimation::ExitFlyRight;
    if (s == "exitZoomOut")   return ItemAnimation::ExitZoomOut;
    return ItemAnimation::None;
}

} // namespace

QJsonObject ImpressModule::deckToJson() const {
    QJsonArray slidesArray;
    for (const auto& slide : m_slideData) {
        QJsonObject slideObj;
        slideObj["title"]      = slide.title;
        slideObj["background"] = slide.background.name(QColor::HexArgb);
        if (slide.background2.isValid())
            slideObj["background2"] = slide.background2.name(QColor::HexArgb);
        slideObj["layout"]     = layoutToString(slide.layout);
        slideObj["transition"] = transitionToString(slide.transition);
        if (slide.slideAnimation != SlideAnimation::None)
            slideObj["slideAnimation"] = slideAnimationToString(slide.slideAnimation);
        slideObj["notes"]      = slide.notes;

        if (!slide.comments.empty()) {
            QJsonArray commentsArr;
            for (const auto& cm : slide.comments) {
                QJsonObject co;
                co["author"] = cm.author;
                co["text"]   = cm.text;
                commentsArr.append(co);
            }
            slideObj["comments"] = commentsArr;
        }

        QJsonArray itemsArray;
        for (const auto& item : slide.items) {
            QJsonObject itemObj;
            itemObj["type"]     = itemTypeToString(item.type);
            itemObj["x"]        = item.rect.x();
            itemObj["y"]        = item.rect.y();
            itemObj["w"]        = item.rect.width();
            itemObj["h"]        = item.rect.height();
            itemObj["rotation"] = item.rotation;

            if (item.type == SlideItemType::TextBox) {
                itemObj["text"]        = item.text;
                itemObj["html"]        = item.html;
                itemObj["fontSize"]    = item.fontSize;
                itemObj["placeholder"] = item.isPlaceholder;
            }
            if (item.type == SlideItemType::Image) {
                itemObj["imageData"] = QString::fromLatin1(item.imageData.toBase64());
                if (item.brightness != 0) itemObj["brightness"] = item.brightness;
                if (item.contrast   != 0) itemObj["contrast"]   = item.contrast;
            }
            if (item.type == SlideItemType::Shape) {
                itemObj["shapeKind"] = shapeKindToString(item.shapeKind);
            }
            if (item.type == SlideItemType::Table) {
                itemObj["rows"] = item.rows;
                itemObj["cols"] = item.cols;
                QJsonArray cellsArr;
                for (const QString& cell : item.cells) cellsArr.append(cell);
                itemObj["cells"] = cellsArr;
            }
            if (item.type == SlideItemType::Chart) {
                itemObj["chartKind"]  = chartKindToString(item.chartKind);
                itemObj["chartTitle"] = item.chartTitle;
                QJsonArray labelsArr, valuesArr;
                for (const QString& l : item.chartLabels) labelsArr.append(l);
                for (double v : item.chartValues) valuesArr.append(v);
                itemObj["chartLabels"] = labelsArr;
                itemObj["chartValues"] = valuesArr;
            }

            itemObj["fillColor"] = item.fillColor.name(QColor::HexArgb);
            itemObj["penColor"]  = item.penColor.name(QColor::HexArgb);
            itemObj["penWidth"]  = item.penWidth;
            itemObj["animation"] = animationToString(item.animation);
            if (item.shadow) itemObj["shadow"] = true;
            if (item.opacity < 1.0) itemObj["opacity"] = item.opacity;
            if (!item.hyperlink.isEmpty()) itemObj["hyperlink"] = item.hyperlink;
            if (item.locked) itemObj["locked"] = true;

            itemsArray.append(itemObj);
        }

        slideObj["items"] = itemsArray;
        slidesArray.append(slideObj);
    }

    QJsonObject root;
    root["type"]    = "impress";
    root["version"] = 2;
    root["slides"]  = slidesArray;
    return root;
}

void ImpressModule::deckFromJson(const QJsonObject& root) {
    const QJsonArray slides = root["slides"].toArray();

    m_ignoreChange = true;
    clearDeck();

    for (const auto& slideVal : slides) {
        const QJsonObject slideObj = slideVal.toObject();

        SlideData data;
        data.title      = slideObj["title"].toString("Untitled Slide");
        data.background = QColor(slideObj["background"].toString("#ffffff"));
        if (slideObj.contains("background2"))
            data.background2 = QColor(slideObj["background2"].toString());
        data.layout      = layoutFromString(slideObj["layout"].toString("blank"));
        data.transition  = transitionFromString(slideObj["transition"].toString("none"));
        data.slideAnimation = slideAnimationFromString(slideObj["slideAnimation"].toString("none"));
        data.notes       = slideObj["notes"].toString();

        for (const auto& cv : slideObj["comments"].toArray()) {
            const QJsonObject co = cv.toObject();
            data.comments.push_back({ co["author"].toString(), co["text"].toString() });
        }

        const QJsonArray items = slideObj["items"].toArray();
        for (const auto& itemVal : items) {
            const QJsonObject itemObj = itemVal.toObject();

            SlideItem si;
            si.type = itemTypeFromString(itemObj["type"].toString());
            si.rect = QRectF(itemObj["x"].toDouble(),
                             itemObj["y"].toDouble(),
                             itemObj["w"].toDouble(),
                             itemObj["h"].toDouble());
            si.rotation = itemObj["rotation"].toDouble(0.0);

            if (si.type == SlideItemType::TextBox) {
                si.text          = itemObj["text"].toString();
                si.html          = itemObj["html"].toString();
                si.fontSize      = itemObj["fontSize"].toDouble(14.0);
                si.isPlaceholder = itemObj["placeholder"].toBool(false);
            }
            if (si.type == SlideItemType::Image) {
                si.imageData = QByteArray::fromBase64(itemObj["imageData"].toString().toLatin1());
                si.brightness = itemObj["brightness"].toInt(0);
                si.contrast   = itemObj["contrast"].toInt(0);
            }
            if (si.type == SlideItemType::Shape) {
                si.shapeKind = shapeKindFromString(itemObj["shapeKind"].toString("rectangle"));
            }
            if (si.type == SlideItemType::Table) {
                si.rows = itemObj["rows"].toInt(1);
                si.cols = itemObj["cols"].toInt(1);
                const QJsonArray cellsArr = itemObj["cells"].toArray();
                for (const auto& cv : cellsArr) si.cells.push_back(cv.toString());
            }
            if (si.type == SlideItemType::Chart) {
                si.chartKind  = chartKindFromString(itemObj["chartKind"].toString("bar"));
                si.chartTitle = itemObj["chartTitle"].toString();
                for (const auto& lv : itemObj["chartLabels"].toArray())
                    si.chartLabels.push_back(lv.toString());
                for (const auto& vv : itemObj["chartValues"].toArray())
                    si.chartValues.push_back(vv.toDouble());
            }

            si.fillColor = QColor(itemObj["fillColor"].toString("#ffffff"));
            si.penColor  = QColor(itemObj["penColor"].toString("#1C1E26"));
            si.penWidth  = itemObj["penWidth"].toDouble(1.5);
            si.animation = animationFromString(itemObj["animation"].toString("none"));
            si.shadow    = itemObj["shadow"].toBool(false);
            si.opacity   = itemObj["opacity"].toDouble(1.0);
            si.hyperlink = itemObj["hyperlink"].toString();
            si.locked    = itemObj["locked"].toBool(false);

            data.items.push_back(si);
        }

        createSlide(data);
    }

    m_ignoreChange = false;
}

bool ImpressModule::saveToPath(const QString& path) {
    syncCurrentSceneToData();

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;

    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << "<!-- NativeOffice Impress Presentation (.noff) -->\n";
    out << QJsonDocument(deckToJson()).toJson(QJsonDocument::Indented);
    f.close();

    m_currentPath = path;
    m_dirty       = false;
    emit filePathChanged(path);
    return true;
}

bool ImpressModule::loadFromPath(const QString& path) {
    // PowerPoint packages are ZIP archives beginning with the "PK" signature —
    // import those via the .pptx reader instead of the JSON (.noff) parser.
    {
        QFile probe(path);
        if (probe.open(QIODevice::ReadOnly)) {
            const QByteArray magic = probe.read(2);
            probe.close();
            if (magic == QByteArray("PK")) {
                std::vector<SlideData> slides;
                if (!importPptx(path, slides) || slides.empty())
                    return false;
                m_ignoreChange = true;
                clearDeck();
                for (const auto& d : slides) createSlide(d);
                m_ignoreChange = false;
                if (!m_scenes.empty()) switchToSlide(0);
                m_currentPath = path;
                m_dirty       = false;
                emit filePathChanged(path);
                captureUndoBaseline();
                m_undoStack->clear();
                return true;
            }
        }
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    QString content = in.readAll();
    f.close();

    content.remove("<!-- NativeOffice Impress Presentation (.noff) -->\n");

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &err);
    if (doc.isNull() || !doc.isObject())
        return false;

    const QJsonObject root = doc.object();
    if (root["slides"].toArray().isEmpty())
        return false;

    deckFromJson(root);

    if (!m_scenes.empty())
        switchToSlide(0);

    m_currentPath = path;
    m_dirty       = false;
    emit filePathChanged(path);

    captureUndoBaseline();
    m_undoStack->clear();
    return true;
}

void ImpressModule::setReadOnly(bool on) {
    if (m_ribbon)    m_ribbon->setEnabled(!on);
    if (m_notesEdit) m_notesEdit->setReadOnly(on);
    if (m_view)      m_view->setInteractive(!on);
}

} // namespace NativeOffice
