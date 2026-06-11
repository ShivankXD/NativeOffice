// ─────────────────────────────────────────────────────────────────────────────
// ImpressToolbar.cpp  (Sprint 5)
// ─────────────────────────────────────────────────────────────────────────────
#include "ImpressToolbar.h"

#include <QHBoxLayout>
#include <QToolButton>
#include <QButtonGroup>
#include <QFrame>
#include <QFont>

namespace NativeOffice {

ImpressToolbar::ImpressToolbar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("impressToolbar");
    setFixedHeight(44);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(4);

    // ── Shape insert group ─────────────────────────────────────────────────
    m_shapeGroup = new QButtonGroup(this);
    m_shapeGroup->setExclusive(true);

    m_btnText    = makeButton("T",  "Add Text Box",  true);
    m_btnRect    = makeButton("☐",  "Add Rectangle", true);
    m_btnEllipse = makeButton("◯",  "Add Ellipse",   true);

    m_shapeGroup->addButton(m_btnText,    static_cast<int>(InsertMode::TextBox));
    m_shapeGroup->addButton(m_btnRect,    static_cast<int>(InsertMode::Rectangle));
    m_shapeGroup->addButton(m_btnEllipse, static_cast<int>(InsertMode::Ellipse));

    // Emit the mode when a shape button is toggled on
    connect(m_shapeGroup, &QButtonGroup::idToggled,
            this, [this](int id, bool checked) {
        if (checked)
            emit insertModeChanged(static_cast<InsertMode>(id));
    });

    // ── Separator ──────────────────────────────────────────────────────────
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedHeight(22);
    sep->setStyleSheet("background: rgba(255,255,255,0.15); border: none;");

    // ── Slide management ───────────────────────────────────────────────────
    auto* btnDup = makeButton("⎘ Duplicate", "Duplicate this slide");
    auto* btnDel = makeButton("✕ Delete",    "Delete this slide");

    connect(btnDup, &QToolButton::clicked, this, &ImpressToolbar::duplicateSlideRequested);
    connect(btnDel, &QToolButton::clicked, this, &ImpressToolbar::deleteSlideRequested);

    layout->addWidget(m_btnText);
    layout->addWidget(m_btnRect);
    layout->addWidget(m_btnEllipse);
    layout->addWidget(sep);
    layout->addWidget(btnDup);
    layout->addWidget(btnDel);
    layout->addStretch();

    setStyleSheet(R"(
QWidget#impressToolbar {
    background-color: #2C3140;
    border-bottom: 1px solid #1A1F2E;
}
QToolButton {
    color: rgba(255,255,255,0.80);
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 5px 10px;
    font-size: 13px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-weight: 500;
}
QToolButton:hover {
    background: rgba(255,255,255,0.10);
    color: #FFFFFF;
    border-color: rgba(255,255,255,0.15);
}
QToolButton:checked {
    background-color: #E8372A;
    color: #FFFFFF;
    border-color: #C0271C;
    font-weight: 700;
}
QToolButton:pressed {
    background-color: #C0271C;
}
)");
}

void ImpressToolbar::resetInsertMode() {
    // Uncheck all shape buttons
    if (auto* checked = m_shapeGroup->checkedButton())
        checked->setChecked(false);
}

QToolButton* ImpressToolbar::makeButton(const QString& label,
                                         const QString& tooltip,
                                         bool           checkable) {
    auto* btn = new QToolButton(this);
    btn->setText(label);
    btn->setToolTip(tooltip);
    btn->setCheckable(checkable);
    btn->setCursor(Qt::PointingHandCursor);
    return btn;
}

} // namespace NativeOffice
