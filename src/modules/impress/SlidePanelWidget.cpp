// ─────────────────────────────────────────────────────────────────────────────
// SlidePanelWidget.cpp  (Sprint 5)
// ─────────────────────────────────────────────────────────────────────────────
#include "SlidePanelWidget.h"
#include "SlideThumbnailWidget.h"
#include "SlideScene.h"

#include <QPushButton>
#include <QFrame>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

namespace NativeOffice {

// ── Drop-enabled list container ──────────────────────────────────────────────
// Accepts thumbnail drags (started in SlideThumbnailWidget) and forwards the
// resolved drop index to the owning SlidePanelWidget.
class ThumbListContainer : public QWidget {
public:
    explicit ThumbListContainer(SlidePanelWidget* panel, QWidget* parent = nullptr)
        : QWidget(parent), m_panel(panel) {
        setAcceptDrops(true);
    }
protected:
    void dragEnterEvent(QDragEnterEvent* e) override {
        if (e->mimeData()->hasText()) e->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent* e) override {
        if (e->mimeData()->hasText()) e->acceptProposedAction();
    }
    void dropEvent(QDropEvent* e) override {
        if (!e->mimeData()->hasText()) return;
        bool ok = false;
        const int from = e->mimeData()->text().toInt(&ok);
        if (!ok) return;
        const int to = m_panel->dropIndexForY(e->position().toPoint().y());
        e->acceptProposedAction();
        emit m_panel->reorderRequested(from, to);
    }
private:
    SlidePanelWidget* m_panel;
};

SlidePanelWidget::SlidePanelWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("slidePanelWidget");
    setFixedWidth(200);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ── "+" Add Slide button ─────────────────────────────────────────────
    auto* addBtn = new QPushButton("＋  New Slide", this);
    addBtn->setObjectName("addSlideBtn");
    addBtn->setFixedHeight(40);
    addBtn->setCursor(Qt::PointingHandCursor);
    connect(addBtn, &QPushButton::clicked, this, &SlidePanelWidget::addSlideRequested);

    // ── Thin separator ───────────────────────────────────────────────────
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFixedHeight(1);
    sep->setStyleSheet("background: #E2E4E9; border: none;");

    // ── Scroll area of thumbnails ─────────────────────────────────────────
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName("thumbScrollArea");
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFrameShape(QFrame::NoFrame);

    m_listWidget = new ThumbListContainer(this, m_scroll);
    m_listWidget->setObjectName("thumbListWidget");
    m_listLayout = new QVBoxLayout(m_listWidget);
    m_listLayout->setContentsMargins(0, 8, 0, 8);
    m_listLayout->setSpacing(4);
    m_listLayout->addStretch();

    m_scroll->setWidget(m_listWidget);

    rootLayout->addWidget(addBtn);
    rootLayout->addWidget(sep);
    rootLayout->addWidget(m_scroll, 1);

    setStyleSheet(R"(
QWidget#slidePanelWidget {
    background-color: #F3F4F6;
}
QPushButton#addSlideBtn {
    background-color: #E8372A;
    color: #FFFFFF;
    font-size: 13px;
    font-weight: 700;
    font-family: "Segoe UI", "Inter", sans-serif;
    border: none;
    border-radius: 0;
    letter-spacing: 0.5px;
}
QPushButton#addSlideBtn:hover {
    background-color: #FF5247;
}
QPushButton#addSlideBtn:pressed {
    background-color: #C0271C;
}
QScrollArea#thumbScrollArea {
    background: transparent;
    border: none;
}
QWidget#thumbListWidget {
    background: transparent;
}
)");
}

void SlidePanelWidget::addSlide(int slideIndex, SlideScene* scene) {
    auto* thumb = new SlideThumbnailWidget(slideIndex, scene, m_listWidget);
    m_thumbnails.push_back(thumb);

    // Insert before the stretch (which is always the last item)
    const int insertPos = m_listLayout->count() - 1;
    m_listLayout->insertWidget(insertPos, thumb, 0, Qt::AlignHCenter);

    connect(thumb, &SlideThumbnailWidget::clicked,
            this,  &SlidePanelWidget::slideClicked);
    connect(thumb, &SlideThumbnailWidget::duplicateRequested,
            this,  &SlidePanelWidget::duplicateRequested);
    connect(thumb, &SlideThumbnailWidget::deleteRequested,
            this,  &SlidePanelWidget::deleteRequested);
    connect(thumb, &SlideThumbnailWidget::addRequested,
            this,  &SlidePanelWidget::addSlideRequested);
}

int SlidePanelWidget::dropIndexForY(int y) const {
    // Find the thumbnail whose vertical center is closest to the drop point
    for (int i = 0; i < static_cast<int>(m_thumbnails.size()); ++i) {
        const QRect geo = m_thumbnails[i]->geometry();
        if (y < geo.center().y())
            return i;
    }
    return static_cast<int>(m_thumbnails.size()) - 1;
}

void SlidePanelWidget::refreshSlide(int slideIndex) {
    if (slideIndex >= 0 && slideIndex < static_cast<int>(m_thumbnails.size()))
        m_thumbnails[slideIndex]->refresh();
}

void SlidePanelWidget::setActiveSlide(int slideIndex) {
    m_activeIdx = slideIndex;
    for (int i = 0; i < static_cast<int>(m_thumbnails.size()); ++i)
        m_thumbnails[i]->setActive(i == slideIndex);
}

void SlidePanelWidget::clear() {
    for (auto* thumb : m_thumbnails) {
        m_listLayout->removeWidget(thumb);
        thumb->deleteLater();
    }
    m_thumbnails.clear();
    m_activeIdx = 0;
}

int SlidePanelWidget::slideCount() const noexcept {
    return static_cast<int>(m_thumbnails.size());
}

} // namespace NativeOffice
