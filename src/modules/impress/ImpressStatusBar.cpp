// ─────────────────────────────────────────────────────────────────────────────
// ImpressStatusBar.cpp  (Sprint 12)
// ─────────────────────────────────────────────────────────────────────────────
#include "ImpressStatusBar.h"
#include "ImpressModule.h"   // ImpressViewMode definition
#include "core/theme/ThemeManager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QToolButton>
#include <QButtonGroup>

namespace NativeOffice {

ImpressStatusBar::ImpressStatusBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("impressStatusBar");
    setFixedHeight(28);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(10);

    m_slideLabel = new QLabel("Slide 1 / 1", this);
    m_slideLabel->setObjectName("statusLabel");

    m_themeLabel = new QLabel("Default Theme", this);
    m_themeLabel->setObjectName("statusLabel");

    layout->addWidget(m_slideLabel);
    layout->addWidget(m_themeLabel);
    layout->addStretch();

    // ── Comment (toggles the right-hand Comments panel) ─────────────────
    m_btnComment = new QToolButton(this);
    m_btnComment->setText(QString::fromUtf8("\xF0\x9F\x92\xAC Comment"));
    m_btnComment->setToolTip("Show or hide comments on this slide");
    m_btnComment->setObjectName("statusViewBtn");
    m_btnComment->setCursor(Qt::PointingHandCursor);
    connect(m_btnComment, &QToolButton::clicked,
            this, &ImpressStatusBar::commentToggleRequested);
    layout->addWidget(m_btnComment);

    // ── View mode buttons ───────────────────────────────────────────────
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);

    auto makeBtn = [&](const QString& text, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setCheckable(true);
        b->setObjectName("statusViewBtn");
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };

    m_btnNormal  = makeBtn("▭ Normal",  "Normal view");
    m_btnOutline = makeBtn("≡ Outline", "Outline view");
    m_btnSorter  = makeBtn("▦ Sorter",  "Slide Sorter view");
    m_btnNormal->setChecked(true);

    group->addButton(m_btnNormal,  static_cast<int>(ImpressViewMode::Normal));
    group->addButton(m_btnOutline, static_cast<int>(ImpressViewMode::Outline));
    group->addButton(m_btnSorter,  static_cast<int>(ImpressViewMode::SlideSorter));

    connect(group, &QButtonGroup::idClicked, this, [this](int id) {
        emit viewModeChanged(static_cast<ImpressViewMode>(id));
    });

    layout->addWidget(m_btnNormal);
    layout->addWidget(m_btnOutline);
    layout->addWidget(m_btnSorter);

    // ── Zoom slider ─────────────────────────────────────────────────────
    m_zoomSlider = new QSlider(Qt::Horizontal, this);
    m_zoomSlider->setRange(25, 200);
    m_zoomSlider->setValue(100);
    m_zoomSlider->setFixedWidth(110);
    m_zoomSlider->setObjectName("statusZoomSlider");

    m_zoomLabel = new QLabel("100%", this);
    m_zoomLabel->setObjectName("statusLabel");
    m_zoomLabel->setFixedWidth(40);

    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int v) {
        m_zoomLabel->setText(QString::number(v) + "%");
        emit zoomChanged(v);
    });

    layout->addWidget(m_zoomSlider);
    layout->addWidget(m_zoomLabel);

    applyTheme();
    connect(&ThemeManager::instance(), &ThemeManager::modeChanged,
            this, [this](ThemeMode) { applyTheme(); });
}

void ImpressStatusBar::applyTheme() {
    // The status tray is always white chrome, matching the rest of the shell,
    // regardless of the app-wide dark/light mode.
    setStyleSheet(R"(
QWidget#impressStatusBar {
    background-color: #FFFFFF;
    border-top: 1px solid #E4E7ED;
}
QLabel#statusLabel {
    color: #5A6071;
    font-size: 11px;
    font-family: "Segoe UI", "Inter", sans-serif;
}
QToolButton#statusViewBtn {
    color: #5A6071;
    background: transparent;
    border: none;
    border-radius: 4px;
    padding: 2px 8px;
    font-size: 11px;
    font-family: "Segoe UI", "Inter", sans-serif;
}
QToolButton#statusViewBtn:hover {
    background: #E7E9EE;
    color: #1C1E26;
}
QToolButton#statusViewBtn:checked {
    background: #EDE9FC;
    color: #4C3BD6;
}
QSlider#statusZoomSlider::groove:horizontal {
    height: 4px;
    background: #D7DAE0;
    border-radius: 2px;
}
QSlider#statusZoomSlider::handle:horizontal {
    width: 12px;
    height: 12px;
    margin: -4px 0;
    background: #6D5BE8;
    border-radius: 6px;
}
QSlider#statusZoomSlider::handle:horizontal:hover {
    background: #8674F0;
}
)");
}

void ImpressStatusBar::setSlideInfo(int currentIndex, int total) {
    m_slideLabel->setText(QString("Slide %1 / %2").arg(currentIndex + 1).arg(total));
}

void ImpressStatusBar::setThemeName(const QString& name) {
    m_themeLabel->setText(name);
}

void ImpressStatusBar::setZoomPercent(int percent) {
    const QSignalBlocker blocker(m_zoomSlider);
    m_zoomSlider->setValue(percent);
    m_zoomLabel->setText(QString::number(percent) + "%");
}

void ImpressStatusBar::setViewMode(ImpressViewMode mode) {
    switch (mode) {
    case ImpressViewMode::Normal:      m_btnNormal->setChecked(true);  break;
    case ImpressViewMode::Outline:     m_btnOutline->setChecked(true); break;
    case ImpressViewMode::SlideSorter: m_btnSorter->setChecked(true);  break;
    }
}

} // namespace NativeOffice
