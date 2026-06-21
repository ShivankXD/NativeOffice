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
#include "core/theme/ThemeManager.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
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
#include <QPixmap>
#include <QSizePolicy>
#include <QMessageBox>
#include <QResizeEvent>
#include <QPainter>
#include <QPdfWriter>
#include <QFileDialog>
#include <QDir>
#include <QPageSize>
#include <QUndoStack>
#include <QUndoCommand>
#include <QTimer>
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
        m_module->switchToSlide(qBound(0, m_beforeIdx, m_module->slideCount() - 1));
        m_module->m_undoBaseline = QJsonDocument(m_before).toJson(QJsonDocument::Compact);
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
        m_module->switchToSlide(qBound(0, m_afterIdx, m_module->slideCount() - 1));
        m_module->m_undoBaseline = QJsonDocument(m_after).toJson(QJsonDocument::Compact);
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

    // Create the first slide automatically
    addNewSlide();
    captureUndoBaseline();
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

    m_notesEdit = new QTextEdit(m_canvasSplitter);
    m_notesEdit->setObjectName("impressNotes");
    m_notesEdit->setPlaceholderText("Click to add speaker notes…");

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

    // ── Status bar ───────────────────────────────────────────────────────
    m_statusBar = new ImpressStatusBar(this);

    rootLayout->addWidget(m_ribbon);
    rootLayout->addWidget(bodyWidget, 1);
    rootLayout->addWidget(m_statusBar);

    // ── Wire ribbon signals ─────────────────────────────────────────────
    connect(m_ribbon, &ImpressRibbon::undoRequested, this, &ImpressModule::undo);
    connect(m_ribbon, &ImpressRibbon::redoRequested, this, &ImpressModule::redo);

    // Keep the ribbon's undo/redo buttons enabled in step with the stack
    auto refreshUndoButtons = [this] {
        m_ribbon->setUndoRedoEnabled(m_undoStack->canUndo(), m_undoStack->canRedo());
    };
    connect(m_undoStack, &QUndoStack::canUndoChanged, this, [refreshUndoButtons](bool){ refreshUndoButtons(); });
    connect(m_undoStack, &QUndoStack::canRedoChanged, this, [refreshUndoButtons](bool){ refreshUndoButtons(); });

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

    connect(m_ribbon, &ImpressRibbon::notesToggleRequested, this, [this] {
        m_notesVisible = !m_notesVisible;
        m_notesEdit->setVisible(m_notesVisible);
    });

    connect(m_ribbon, &ImpressRibbon::designColorSelected, this, [this](const QColor& c) {
        if (m_currentIdx < 0) return;
        auto* scene = m_scenes[m_currentIdx];
        SlideData snap; scene->saveToData(snap);
        snap.background  = c;
        snap.background2 = QColor();   // solid fill — clear any gradient
        scene->loadFromData(snap);
        commitUndoStep();
    });

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

    // ── Notes panel ────────────────────────────────────────────────────
    connect(m_notesEdit, &QTextEdit::textChanged, this, [this] {
        if (m_currentIdx < 0 || m_ignoreChange) return;
        m_slideData[m_currentIdx].notes = m_notesEdit->toPlainText();
        if (!m_dirty) { m_dirty = true; emit documentModified(); }
        m_undoDebounce->start();
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Brand tray — logo + tagline pinned top-left on a translucent white card
// ─────────────────────────────────────────────────────────────────────────────
QWidget* ImpressModule::buildBrandBar() {
    auto* bar = new QWidget(this);
    bar->setObjectName("impressBrandBar");
    bar->setFixedHeight(48);

    auto* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(12, 6, 12, 6);
    barLayout->setSpacing(0);

    // The translucent white "tray" that the logo + text sit on.
    auto* tray = new QWidget(bar);
    tray->setObjectName("impressBrandTray");

    auto* trayLayout = new QHBoxLayout(tray);
    trayLayout->setContentsMargins(10, 4, 16, 4);
    trayLayout->setSpacing(10);

    // Logo (logo.jpg) on top-left of the tray.
    auto* logo = new QLabel(tray);
    logo->setObjectName("impressBrandLogo");
    QPixmap pm(":/assets/logo.jpg");
    if (!pm.isNull()) {
        logo->setPixmap(pm.scaledToHeight(28, Qt::SmoothTransformation));
    } else {
        logo->setText("NP");   // graceful fallback if the resource is missing
    }
    logo->setAlignment(Qt::AlignCenter);

    auto* tagline = new QLabel("NativeOffice is your go to OfficeSuite!", tray);
    tagline->setObjectName("impressBrandText");

    trayLayout->addWidget(logo);
    trayLayout->addWidget(tagline);

    barLayout->addWidget(tray, 0, Qt::AlignLeft);
    barLayout->addStretch();
    return bar;
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

    // Reflect the current selection's formatting in the ribbon
    connect(scene, &SlideScene::selectionInfoChanged,
            this, &ImpressModule::syncRibbonToSelection);

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
    commitUndoStep();
}

void ImpressModule::applyTransitionToAllSlides(SlideTransition transition) {
    if (m_slideData.empty()) return;
    for (auto& slide : m_slideData)
        slide.transition = transition;
    if (!m_dirty) { m_dirty = true; emit documentModified(); }
    commitUndoStep();
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
    if (m_scenes.empty()) return;
    if (m_currentIdx >= 0)
        m_scenes[m_currentIdx]->saveToData(m_slideData[m_currentIdx]);

    // Snapshot every scene's current state into the data deck, then present
    // from a fresh copy so the show never mutates the editor's live scenes.
    for (int i = 0; i < static_cast<int>(m_scenes.size()); ++i)
        m_scenes[i]->saveToData(m_slideData[i]);

    auto* win = new PresentModeWindow(m_slideData, std::max(0, m_currentIdx), this);
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
QWidget#impressBrandBar {
    background-color: #F3F4F6;
    border-bottom: 1px solid #E2E4E9;
}
QWidget#impressBrandTray {
    background-color: rgba(255, 255, 255, 0.65);
    border: 1px solid rgba(255, 255, 255, 0.85);
    border-radius: 9px;
}
QLabel#impressBrandLogo {
    background: transparent;
    border-radius: 6px;
    color: #E8372A;
    font-size: 14px;
    font-weight: 900;
    font-family: "Segoe UI", sans-serif;
    min-width: 30px;
}
QLabel#impressBrandText {
    background: transparent;
    color: #2C3140;
    font-size: 13px;
    font-weight: 600;
    font-family: "Segoe UI", "Inter", sans-serif;
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
    border-top: 2px solid #E8372A;
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
    if (m_view && obj == m_view->viewport() && event->type() == QEvent::Resize)
        refitView();
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
    return SlideItemType::TextBox;
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
        slideObj["notes"]      = slide.notes;

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

            itemObj["fillColor"] = item.fillColor.name(QColor::HexArgb);
            itemObj["penColor"]  = item.penColor.name(QColor::HexArgb);
            itemObj["penWidth"]  = item.penWidth;
            itemObj["animation"] = animationToString(item.animation);
            if (item.shadow) itemObj["shadow"] = true;
            if (!item.hyperlink.isEmpty()) itemObj["hyperlink"] = item.hyperlink;

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
        data.notes       = slideObj["notes"].toString();

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

            si.fillColor = QColor(itemObj["fillColor"].toString("#ffffff"));
            si.penColor  = QColor(itemObj["penColor"].toString("#1C1E26"));
            si.penWidth  = itemObj["penWidth"].toDouble(1.5);
            si.animation = animationFromString(itemObj["animation"].toString("none"));
            si.shadow    = itemObj["shadow"].toBool(false);
            si.hyperlink = itemObj["hyperlink"].toString();

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

} // namespace NativeOffice
