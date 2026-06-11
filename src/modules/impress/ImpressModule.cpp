// ─────────────────────────────────────────────────────────────────────────────
// ImpressModule.cpp  (Sprint 6)
// Full NativeOffice Presentation Tool: three-pane layout with slide management,
// QGraphicsView canvas, toolbar-driven shape insertion, and PDF export.
// ─────────────────────────────────────────────────────────────────────────────
#include "ImpressModule.h"
#include "ImpressToolbar.h"
#include "SlidePanelWidget.h"
#include "SlideScene.h"
#include "core/theme/ThemeManager.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGraphicsView>
#include <QFrame>
#include <QSizePolicy>
#include <QMessageBox>
#include <QResizeEvent>
#include <QPainter>
#include <QPdfWriter>
#include <QFileDialog>
#include <QDir>
#include <QPageSize>
#include <algorithm>

namespace NativeOffice {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────
ImpressModule::ImpressModule(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    applyStyles();
    setObjectName("impressModule");

    // Create the first slide automatically
    addNewSlide();
}

// ─────────────────────────────────────────────────────────────────────────────
// UI Construction
// ─────────────────────────────────────────────────────────────────────────────
void ImpressModule::buildUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ── Toolbar ───────────────────────────────────────────────────────────
    m_toolbar = new ImpressToolbar(this);

    // ── Body row: slide panel + canvas ────────────────────────────────────
    auto* bodyWidget = new QWidget(this);
    bodyWidget->setObjectName("impressBody");
    auto* bodyLayout = new QHBoxLayout(bodyWidget);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    // Left: slide thumbnail panel
    m_slidePanel = new SlidePanelWidget(bodyWidget);

    // Center: QGraphicsView for the active slide scene
    m_view = new QGraphicsView(bodyWidget);
    m_view->setObjectName("impressCanvas");
    m_view->setAlignment(Qt::AlignCenter);
    m_view->setRenderHints(QPainter::Antialiasing |
                           QPainter::SmoothPixmapTransform |
                           QPainter::TextAntialiasing);
    m_view->setDragMode(QGraphicsView::RubberBandDrag);
    m_view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_view->setFrameShape(QFrame::NoFrame);

    // Thin separator between panel and canvas
    auto* sep = new QFrame(bodyWidget);
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedWidth(1);
    sep->setStyleSheet("background: #1A1F2E; border: none;");

    bodyLayout->addWidget(m_slidePanel);
    bodyLayout->addWidget(sep);
    bodyLayout->addWidget(m_view, 1);

    rootLayout->addWidget(m_toolbar);
    rootLayout->addWidget(bodyWidget, 1);

    // ── Wire toolbar signals ──────────────────────────────────────────────
    connect(m_toolbar, &ImpressToolbar::insertModeChanged,
            this, [this](InsertMode mode) {
        if (m_currentIdx >= 0 && m_currentIdx < static_cast<int>(m_scenes.size()))
            m_scenes[m_currentIdx]->setInsertMode(mode);
    });

    connect(m_toolbar, &ImpressToolbar::duplicateSlideRequested,
            this, &ImpressModule::duplicateCurrentSlide);
    connect(m_toolbar, &ImpressToolbar::deleteSlideRequested,
            this, &ImpressModule::deleteCurrentSlide);

    // ── Wire slide panel ──────────────────────────────────────────────────
    connect(m_slidePanel, &SlidePanelWidget::slideClicked,
            this, &ImpressModule::switchToSlide);
    connect(m_slidePanel, &SlidePanelWidget::addSlideRequested,
            this, &ImpressModule::addNewSlide);
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

    // When scene changes → refresh its thumbnail
    connect(scene, &SlideScene::sceneModified, this, [this, idx]() {
        m_slidePanel->refreshSlide(idx);
    });

    // When insert mode completes → reset toolbar button
    connect(scene, &SlideScene::insertModeLeft,
            m_toolbar, &ImpressToolbar::resetInsertMode);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public slots
// ─────────────────────────────────────────────────────────────────────────────
void ImpressModule::addNewSlide() {
    SlideData data;
    data.title      = QString("Slide %1").arg(m_scenes.size() + 1);
    data.background = Qt::white;

    createSlide(data);
    const int newIdx = static_cast<int>(m_scenes.size()) - 1;

    // Add default placeholders on the scene (title + subtitle text)
    m_scenes[newIdx]->addDefaultPlaceholders();

    switchToSlide(newIdx);
}

void ImpressModule::switchToSlide(int index) {
    if (index < 0 || index >= static_cast<int>(m_scenes.size())) return;

    // Save current scene state back to SlideData before switching
    if (m_currentIdx >= 0 && m_currentIdx < static_cast<int>(m_scenes.size()))
        m_scenes[m_currentIdx]->saveToData(m_slideData[m_currentIdx]);

    // Reset insert mode on old scene
    if (m_currentIdx >= 0 && m_currentIdx < static_cast<int>(m_scenes.size()))
        m_scenes[m_currentIdx]->setInsertMode(InsertMode::None);
    m_toolbar->resetInsertMode();

    m_currentIdx = index;
    m_view->setScene(m_scenes[index]);

    // Fit the 960×540 slide to the view, maintaining aspect ratio
    m_view->fitInView(QRectF(0, 0, SlideScene::SLIDE_W, SlideScene::SLIDE_H),
                      Qt::KeepAspectRatio);

    m_slidePanel->setActiveSlide(index);
    emit m_scenes[index]->sceneModified();   // force thumbnail refresh
}

void ImpressModule::duplicateCurrentSlide() {
    if (m_currentIdx < 0 || m_currentIdx >= static_cast<int>(m_scenes.size())) return;

    // Save current first
    m_scenes[m_currentIdx]->saveToData(m_slideData[m_currentIdx]);

    SlideData copy = m_slideData[m_currentIdx];
    copy.title     = copy.title + " (copy)";
    createSlide(copy);
    switchToSlide(static_cast<int>(m_scenes.size()) - 1);
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

    // NOTE: Thumbnail rebuilding on delete would require re-indexing all
    // thumbnails; for Sprint 5 we simply switch to the adjacent slide.
    // A full rebuild is a Sprint 6 enhancement.
    m_currentIdx = -1;

    const int nextIdx = std::min(idx, static_cast<int>(m_scenes.size()) - 1);
    switchToSlide(nextIdx);
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
)");
}

// ─────────────────────────────────────────────────────────────────────────────
// Keep slide surface fitted when the widget is resized
// ─────────────────────────────────────────────────────────────────────────────
// Override resizeEvent so the slide always fits the canvas without scrollbars
void ImpressModule::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_view && m_view->scene()) {
        m_view->fitInView(QRectF(0, 0, SlideScene::SLIDE_W, SlideScene::SLIDE_H),
                          Qt::KeepAspectRatio);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Sprint 6: Export all slides to PDF
// ─────────────────────────────────────────────────────────────────────────────
void ImpressModule::exportToPdf() {
    if (m_scenes.empty()) {
        QMessageBox::information(this, "Export to PDF",
            "There are no slides to export.");
        return;
    }

    // ── Save current scene state before iterating ────────────────────────────────────
    if (m_currentIdx >= 0 && m_currentIdx < static_cast<int>(m_scenes.size()))
        m_scenes[m_currentIdx]->saveToData(m_slideData[m_currentIdx]);

    // ── Ask user for destination ────────────────────────────────────────────────────
    const QString path = QFileDialog::getSaveFileName(
        this,
        "Export Presentation to PDF",
        QDir::homePath() + "/Presentation.pdf",
        "PDF Document (*.pdf)");

    if (path.isEmpty()) return;

    // ── Set up QPdfWriter in landscape 16:9 ──────────────────────────────────────
    QPdfWriter writer(path);
    writer.setCreator("NativeOffice Impress");
    writer.setTitle("Presentation");

    // Use a custom 16:9 page: 338.67 mm × 190.5 mm  (≈ 1920:1080 at 72 dpi)
    // We set the page size to a landscape widescreen ratio.
    writer.setPageSize(QPageSize(QSizeF(338.667, 190.5), QPageSize::Millimeter,
                                  "Widescreen 16:9"));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));   // full-bleed, no margins
    writer.setResolution(150);                        // 150 dpi: quality vs file size

    // ── Paint each scene onto its own page ───────────────────────────────────────
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

        // Page rectangle in device pixels
        const QRect pageRect = painter.viewport();

        // Target rect preserving 16:9 inside the page (should be identical
        // since we set the page to 16:9, but guard against rounding)
        const qreal aspect = SlideScene::SLIDE_W / SlideScene::SLIDE_H;
        QRectF target = pageRect;
        if (target.width() / target.height() > aspect) {
            // Page is wider than 16:9 — letterbox horizontally
            const qreal w = target.height() * aspect;
            target.setLeft(target.left() + (target.width() - w) / 2.0);
            target.setWidth(w);
        } else {
            // Page is taller than 16:9 — letterbox vertically
            const qreal h = target.width() / aspect;
            target.setTop(target.top() + (target.height() - h) / 2.0);
            target.setHeight(h);
        }

        // Fill any letterbox areas with white
        painter.fillRect(pageRect, Qt::white);

        // Render the scene into the target rect
        m_scenes[i]->render(&painter, target,
                            QRectF(0, 0, SlideScene::SLIDE_W, SlideScene::SLIDE_H));
    }

    painter.end();

    QMessageBox::information(this, "Export Complete",
        QString("Presentation exported to PDF successfully!\n\n%1 slide(s) → %2")
            .arg(m_scenes.size())
            .arg(path));
}

} // namespace NativeOffice
