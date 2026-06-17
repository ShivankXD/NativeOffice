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

    connect(m_ribbon, &ImpressRibbon::newSlideRequested,       this, &ImpressModule::addNewSlide);
    connect(m_ribbon, &ImpressRibbon::duplicateSlideRequested, this, &ImpressModule::duplicateCurrentSlide);
    connect(m_ribbon, &ImpressRibbon::deleteSlideRequested,    this, &ImpressModule::deleteCurrentSlide);
    connect(m_ribbon, &ImpressRibbon::layoutSelected,          this, &ImpressModule::applyLayoutToCurrentSlide);
    connect(m_ribbon, &ImpressRibbon::transitionSelected,      this, &ImpressModule::applyTransitionToCurrentSlide);
    connect(m_ribbon, &ImpressRibbon::transitionApplyAllRequested, this, &ImpressModule::applyTransitionToAllSlides);
    connect(m_ribbon, &ImpressRibbon::animationSelected,       this, &ImpressModule::applyAnimationToSelection);
    connect(m_ribbon, &ImpressRibbon::insertImageRequested,    this, &ImpressModule::insertImageFromFile);
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
        snap.background = c;
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
    case SlideItemType::TextBox:
    default:                       return "textbox";
    }
}

SlideItemType itemTypeFromString(const QString& s) {
    if (s == "rectangle") return SlideItemType::Rectangle;
    if (s == "ellipse")   return SlideItemType::Ellipse;
    if (s == "image")     return SlideItemType::Image;
    return SlideItemType::TextBox;
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
    case SlideTransition::Fade: return "fade";
    case SlideTransition::Push: return "push";
    case SlideTransition::Wipe: return "wipe";
    case SlideTransition::Zoom: return "zoom";
    case SlideTransition::None:
    default:                    return "none";
    }
}

SlideTransition transitionFromString(const QString& s) {
    if (s == "fade") return SlideTransition::Fade;
    if (s == "push") return SlideTransition::Push;
    if (s == "wipe") return SlideTransition::Wipe;
    if (s == "zoom") return SlideTransition::Zoom;
    return SlideTransition::None;
}

QString animationToString(ItemAnimation a) {
    switch (a) {
    case ItemAnimation::FadeIn:    return "fadeIn";
    case ItemAnimation::FlyInLeft: return "flyInLeft";
    case ItemAnimation::ZoomIn:    return "zoomIn";
    case ItemAnimation::None:
    default:                       return "none";
    }
}

ItemAnimation animationFromString(const QString& s) {
    if (s == "fadeIn")    return ItemAnimation::FadeIn;
    if (s == "flyInLeft") return ItemAnimation::FlyInLeft;
    if (s == "zoomIn")    return ItemAnimation::ZoomIn;
    return ItemAnimation::None;
}

} // namespace

QJsonObject ImpressModule::deckToJson() const {
    QJsonArray slidesArray;
    for (const auto& slide : m_slideData) {
        QJsonObject slideObj;
        slideObj["title"]      = slide.title;
        slideObj["background"] = slide.background.name(QColor::HexArgb);
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

            itemObj["fillColor"] = item.fillColor.name(QColor::HexArgb);
            itemObj["penColor"]  = item.penColor.name(QColor::HexArgb);
            itemObj["penWidth"]  = item.penWidth;
            itemObj["animation"] = animationToString(item.animation);

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

            si.fillColor = QColor(itemObj["fillColor"].toString("#ffffff"));
            si.penColor  = QColor(itemObj["penColor"].toString("#1C1E26"));
            si.penWidth  = itemObj["penWidth"].toDouble(1.5);
            si.animation = animationFromString(itemObj["animation"].toString("none"));

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
