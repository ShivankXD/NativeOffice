// ─────────────────────────────────────────────────────────────────────────────
// SidebarWidget.cpp
// Brand sidebar: Deep charcoal background with scarlet active highlight.
// ─────────────────────────────────────────────────────────────────────────────
#include "SidebarWidget.h"
#include "core/theme/ThemeManager.h"

#include <QLabel>
#include <QPixmap>
#include <QSvgRenderer>
#include <QPainter>
#include <QSpacerItem>
#include <QFont>
#include <QFrame>

namespace NativeOffice {

SidebarWidget::SidebarWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    applyStyles();
    setFixedWidth(220);
}

void SidebarWidget::buildUi() {
    m_layout  = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    // ── Logo / branding area ───────────────────────────────────────────────
    m_logoArea = new QWidget(this);
    m_logoArea->setObjectName("logoArea");
    m_logoArea->setFixedHeight(80);

    auto* logoLayout = new QHBoxLayout(m_logoArea);
    logoLayout->setContentsMargins(20, 0, 20, 0);
    logoLayout->setSpacing(10);

    // Logo "NP" badge using brand colors
    auto* badge = new QLabel(m_logoArea);
    badge->setObjectName("logoBadge");
    badge->setText("NP");
    badge->setFixedSize(40, 40);
    badge->setAlignment(Qt::AlignCenter);

    auto* appName = new QLabel("NativeOffice", m_logoArea);
    appName->setObjectName("appNameLabel");

    logoLayout->addWidget(badge);
    logoLayout->addWidget(appName);
    logoLayout->addStretch();

    // ── Separator ──────────────────────────────────────────────────────────
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setObjectName("sidebarSeparator");
    sep->setFixedHeight(1);

    // ── Navigation area ────────────────────────────────────────────────────
    m_navArea = new QWidget(this);
    auto* navLayout = new QVBoxLayout(m_navArea);
    navLayout->setContentsMargins(12, 16, 12, 16);
    navLayout->setSpacing(4);

    m_btnGroup = new QButtonGroup(this);
    m_btnGroup->setExclusive(true);

    m_btnHome     = makeNavButton("🏠", "Home",     SidebarItem::Home);
    m_btnNew      = makeNavButton("✨", "New",      SidebarItem::New);
    m_btnOpen     = makeNavButton("📂", "Open",     SidebarItem::Open);

    navLayout->addWidget(m_btnHome);
    navLayout->addWidget(m_btnNew);
    navLayout->addWidget(m_btnOpen);
    navLayout->addStretch();

    m_btnSettings = makeNavButton("⚙️", "Settings", SidebarItem::Settings);
    navLayout->addWidget(m_btnSettings);

    m_layout->addWidget(m_logoArea);
    m_layout->addWidget(sep);
    m_layout->addWidget(m_navArea, 1);

    // Set home active by default
    m_btnHome->setChecked(true);
}

QPushButton* SidebarWidget::makeNavButton(const QString& iconText,
                                           const QString& label,
                                           SidebarItem    item) {
    auto* btn = new QPushButton(m_navArea);
    btn->setObjectName("sidebarNavBtn");
    btn->setText(QString("  %1   %2").arg(iconText, label));
    btn->setCheckable(true);
    btn->setFlat(true);
    btn->setFixedHeight(44);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setProperty("sidebarItem", QVariant::fromValue(static_cast<int>(item)));

    m_btnGroup->addButton(btn);

    connect(btn, &QPushButton::clicked, this, [this, item]() {
        m_activeItem = item;
        emit itemSelected(item);
    });

    return btn;
}

void SidebarWidget::setActiveItem(SidebarItem item) {
    m_activeItem = item;
    auto btns = m_btnGroup->buttons();
    for (auto* b : btns) {
        int prop = b->property("sidebarItem").toInt();
        b->setChecked(static_cast<SidebarItem>(prop) == item);
    }
}

void SidebarWidget::applyStyles() {
    const auto& t = ThemeManager::instance().theme();

    setStyleSheet(QString(R"(
SidebarWidget {
    background-color: %1;
}

#logoArea {
    background-color: %2;
}

#logoBadge {
    background-color: %3;
    color: #FFFFFF;
    border-radius: 8px;
    font-size: 16px;
    font-weight: 900;
    font-family: "Segoe UI", sans-serif;
}

#appNameLabel {
    color: #FFFFFF;
    font-size: 15px;
    font-weight: 700;
    font-family: "Segoe UI", "Inter", sans-serif;
    letter-spacing: 0.5px;
}

#sidebarSeparator {
    background-color: rgba(255,255,255,0.08);
    border: none;
}

QPushButton#sidebarNavBtn {
    background-color: transparent;
    color: rgba(255,255,255,0.65);
    border: none;
    border-radius: 8px;
    font-size: 13px;
    font-weight: 500;
    text-align: left;
    padding: 0 12px;
    font-family: "Segoe UI", "Inter", sans-serif;
}

QPushButton#sidebarNavBtn:hover {
    background-color: rgba(255,255,255,0.10);
    color: #FFFFFF;
}

QPushButton#sidebarNavBtn:checked {
    background-color: %3;
    color: #FFFFFF;
    font-weight: 700;
}

QPushButton#sidebarNavBtn:pressed {
    background-color: rgba(232,55,42,0.80);
}
)")
    .arg(ThemeManager::cssColor(t.sidebarBg))    // %1 sidebar bg
    .arg(ThemeManager::cssColor(t.accent))        // %2 logo area (darker)
    .arg(ThemeManager::cssColor(t.secondary))     // %3 active/badge scarlet
    );
}

} // namespace NativeOffice
