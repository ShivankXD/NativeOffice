// ─────────────────────────────────────────────────────────────────────────────
// SlidePanelWidget.cpp  (Sprint 5)
// ─────────────────────────────────────────────────────────────────────────────
#include "SlidePanelWidget.h"
#include "SlideThumbnailWidget.h"
#include "SlideScene.h"

#include <QPushButton>
#include <QFrame>

namespace NativeOffice {

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
    sep->setStyleSheet("background: rgba(255,255,255,0.10); border: none;");

    // ── Scroll area of thumbnails ─────────────────────────────────────────
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName("thumbScrollArea");
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFrameShape(QFrame::NoFrame);

    m_listWidget = new QWidget(m_scroll);
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
    background-color: #2C3140;
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

int SlidePanelWidget::slideCount() const noexcept {
    return static_cast<int>(m_thumbnails.size());
}

} // namespace NativeOffice
