// ─────────────────────────────────────────────────────────────────────────────
// TemplateBarWidget.cpp
// Top quick-create bar: Writer (blue), Calc (green), Impress (orange).
// ─────────────────────────────────────────────────────────────────────────────
#include "TemplateBarWidget.h"
#include "core/theme/ThemeManager.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QFont>
#include <QSizePolicy>

namespace NativeOffice {

TemplateBarWidget::TemplateBarWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    setObjectName("templateBar");
}

void TemplateBarWidget::buildUi() {
    // Outer wrapper with a label
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(32, 28, 32, 20);
    outerLayout->setSpacing(14);

    auto* heading = new QLabel("Create New", this);
    heading->setObjectName("templateHeading");

    m_layout = new QHBoxLayout();
    m_layout->setSpacing(16);

    auto* writerBtn  = makeTemplateButton("📝", "Document",
                                          "Word processor",
                                          "#2563EB", "#1D4ED8",
                                          DocumentType::Writer);
    auto* calcBtn    = makeTemplateButton("📊", "Spreadsheet",
                                          "Organize data",
                                          "#16A34A", "#15803D",
                                          DocumentType::Calc);
    auto* impressBtn = makeTemplateButton("🎨", "Presentation",
                                          "Slides & visuals",
                                          "#EA580C", "#C2410C",
                                          DocumentType::Impress);

    m_layout->addWidget(writerBtn);
    m_layout->addWidget(calcBtn);
    m_layout->addWidget(impressBtn);
    m_layout->addStretch();

    outerLayout->addWidget(heading);
    outerLayout->addLayout(m_layout);

    setStyleSheet(R"(
QWidget#templateBar {
    background-color: #FFFFFF;
    border-bottom: 1px solid #E5E7EB;
}

QLabel#templateHeading {
    font-size: 20px;
    font-weight: 700;
    color: #1C1E26;
    font-family: "Segoe UI", "Inter", sans-serif;
}
)");
}

QPushButton* TemplateBarWidget::makeTemplateButton(const QString& emoji,
                                                    const QString& title,
                                                    const QString& subtitle,
                                                    const QString& bgColor,
                                                    const QString& hoverColor,
                                                    DocumentType   type) {
    auto* btn = new QPushButton(this);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedSize(190, 80);
    btn->setObjectName("templateBtn");

    // Build rich label inside the button using HTML
    btn->setText(QString());

    auto* layout = new QHBoxLayout(btn);
    layout->setContentsMargins(14, 0, 14, 0);
    layout->setSpacing(12);

    auto* iconLabel = new QLabel(emoji, btn);
    iconLabel->setObjectName("templateIcon");
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setFixedSize(36, 36);

    auto* textWidget = new QWidget(btn);
    textWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* textLayout = new QVBoxLayout(textWidget);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    auto* titleLabel    = new QLabel(title, textWidget);
    titleLabel->setObjectName("templateTitle");
    auto* subtitleLabel = new QLabel(subtitle, textWidget);
    subtitleLabel->setObjectName("templateSubtitle");

    textLayout->addWidget(titleLabel);
    textLayout->addWidget(subtitleLabel);

    layout->addWidget(iconLabel);
    layout->addWidget(textWidget);

    btn->setStyleSheet(QString(R"(
QPushButton#templateBtn {
    background-color: %1;
    border: none;
    border-radius: 12px;
    text-align: left;
}
QPushButton#templateBtn:hover {
    background-color: %2;
}
QPushButton#templateBtn:pressed {
    background-color: %2;
    padding-top: 2px;
}
QLabel#templateIcon {
    font-size: 22px;
    background: rgba(255,255,255,0.20);
    border-radius: 8px;
}
QLabel#templateTitle {
    font-size: 14px;
    font-weight: 700;
    color: #FFFFFF;
    font-family: "Segoe UI", "Inter", sans-serif;
    background: transparent;
}
QLabel#templateSubtitle {
    font-size: 11px;
    font-weight: 400;
    color: rgba(255,255,255,0.75);
    font-family: "Segoe UI", "Inter", sans-serif;
    background: transparent;
}
)").arg(bgColor, hoverColor));

    connect(btn, &QPushButton::clicked, this, [this, type]() {
        emit createDocument(type);
    });

    return btn;
}

} // namespace NativeOffice
