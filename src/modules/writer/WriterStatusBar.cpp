// ─────────────────────────────────────────────────────────────────────────────
// WriterStatusBar.cpp  (Sprint 14)
// ─────────────────────────────────────────────────────────────────────────────
#include "WriterStatusBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QToolButton>
#include <QButtonGroup>

namespace NativeOffice {

WriterStatusBar::WriterStatusBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("writerStatusBar");
    setFixedHeight(28);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 0, 14, 0);
    layout->setSpacing(14);

    // ── Left cluster: page + word count ─────────────────────────────────────
    m_pageLabel = new QLabel("Page 1 / 1", this);
    m_pageLabel->setObjectName("statusLabel");

    m_wordLabel = new QLabel("0 words", this);
    m_wordLabel->setObjectName("statusLabel");

    // ── AI Spell Check pill toggle (UI only) ────────────────────────────────
    m_spellPill = new QToolButton(this);
    m_spellPill->setObjectName("statusSpellPill");
    m_spellPill->setCheckable(true);
    m_spellPill->setChecked(true);
    m_spellPill->setCursor(Qt::PointingHandCursor);
    m_spellPill->setToolTip("Toggle AI Spell Check (display only)");
    auto syncPill = [this] {
        m_spellPill->setText(m_spellPill->isChecked()
                                 ? "  ●  AI Spell Check"
                                 : "  ○  AI Spell Check");
    };
    syncPill();
    connect(m_spellPill, &QToolButton::toggled, this, [syncPill](bool){ syncPill(); });

    layout->addWidget(m_pageLabel);
    layout->addWidget(m_wordLabel);
    layout->addWidget(m_spellPill);
    layout->addStretch();

    // ── Right cluster: page-view toggles + zoom ─────────────────────────────
    auto* viewGroup = new QButtonGroup(this);
    viewGroup->setExclusive(true);

    auto makeViewBtn = [&](const QString& glyph, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setText(glyph);
        b->setToolTip(tip);
        b->setCheckable(true);
        b->setObjectName("statusViewBtn");
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };

    m_btnPrint = makeViewBtn("▭", "Print Layout");
    m_btnWeb   = makeViewBtn("☷", "Web Layout");
    m_btnPrint->setChecked(true);
    viewGroup->addButton(m_btnPrint, 0);
    viewGroup->addButton(m_btnWeb,   1);
    connect(viewGroup, &QButtonGroup::idClicked, this, [this](int id) {
        emit webLayoutToggled(id == 1);
    });

    layout->addWidget(m_btnPrint);
    layout->addWidget(m_btnWeb);

    // ── Zoom slider (far right) ─────────────────────────────────────────────
    m_zoomSlider = new QSlider(Qt::Horizontal, this);
    m_zoomSlider->setRange(75, 200);
    m_zoomSlider->setValue(100);
    m_zoomSlider->setFixedWidth(120);
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

    setStyleSheet(R"(
QWidget#writerStatusBar {
    background-color: #F3F4F6;
    border-top: 1px solid #D7DAE0;
}
QLabel#statusLabel {
    color: #5A6071;
    font-size: 11px;
    font-family: "Segoe UI", "Inter", sans-serif;
}
QToolButton#statusSpellPill {
    color: #5A6071;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 10px;
    padding: 2px 10px;
    font-size: 11px;
    font-family: "Segoe UI", "Inter", sans-serif;
}
QToolButton#statusSpellPill:hover {
    background: #E7E9EE;
}
QToolButton#statusSpellPill:checked {
    color: #16A34A;
    background: #E7F6EC;
    border-color: #BfE6CC;
}
QToolButton#statusViewBtn {
    color: #5A6071;
    background: transparent;
    border: none;
    border-radius: 4px;
    padding: 2px 9px;
    font-size: 13px;
    font-family: "Segoe UI", "Inter", sans-serif;
}
QToolButton#statusViewBtn:hover {
    background: #E7E9EE;
    color: #1C1E26;
}
QToolButton#statusViewBtn:checked {
    background: #FCE4E2;
    color: #C0271C;
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
    background: #E8372A;
    border-radius: 6px;
}
QSlider#statusZoomSlider::handle:horizontal:hover {
    background: #FF5247;
}
)");
}

void WriterStatusBar::setPageInfo(int current, int total) {
    m_pageLabel->setText(QString("Page %1 / %2").arg(current).arg(total));
}

void WriterStatusBar::setWordCount(int words) {
    m_wordLabel->setText(QString("%1 %2").arg(words).arg(words == 1 ? "word" : "words"));
}

void WriterStatusBar::setZoomPercent(int percent) {
    const QSignalBlocker blocker(m_zoomSlider);
    m_zoomSlider->setValue(percent);
    m_zoomLabel->setText(QString::number(percent) + "%");
}

void WriterStatusBar::setWebLayoutActive(bool web) {
    const QSignalBlocker b1(m_btnPrint);
    const QSignalBlocker b2(m_btnWeb);
    m_btnPrint->setChecked(!web);
    m_btnWeb->setChecked(web);
}

} // namespace NativeOffice
