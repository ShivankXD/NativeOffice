// ─────────────────────────────────────────────────────────────────────────────
// PdfModule.cpp — see PdfModule.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfModule.h"
#include "PdfOps.h"
#include "core/theme/ThemeManager.h"
#include "app/startscreen/LucideIcons.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QDialog>
#include <QSpinBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QMouseEvent>
#include <QEnterEvent>

namespace NativeOffice {

namespace {

// A frame the whole of which responds to a left click — same pattern
// StartScreen's create-cards use, kept local since PdfModule lives in a
// different module target.
class ClickableFrame : public QFrame {
public:
    using QFrame::QFrame;
    std::function<void()> onClick;
protected:
    void enterEvent(QEnterEvent*) override { if (onClick) setCursor(Qt::PointingHandCursor); }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && onClick) { onClick(); e->accept(); return; }
        QFrame::mousePressEvent(e);
    }
};

} // namespace

PdfModule::PdfModule(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("pdfModule");
    buildUi();
    applyTheme();
    connect(&ThemeManager::instance(), &ThemeManager::modeChanged,
            this, [this](ThemeMode) { applyTheme(); });
}

QString PdfModule::titleString() const {
    return m_currentPath.isEmpty() ? QStringLiteral("PDF Tools")
                                    : QFileInfo(m_currentPath).fileName() + " — PDF Tools";
}

void PdfModule::setInitialFile(const QString& path) {
    setCurrentPath(path);
}

void PdfModule::setCurrentPath(const QString& path) {
    m_currentPath = path;
    emit filePathChanged(path);
    if (m_titleLabel)
        m_titleLabel->setText(path.isEmpty() ? "PDF Tools"
                                              : "PDF Tools — " + QFileInfo(path).fileName());
}

void PdfModule::buildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_root = new QWidget(this);
    m_root->setObjectName("pdfRoot");
    outer->addWidget(m_root);

    auto* v = new QVBoxLayout(m_root);
    v->setContentsMargins(36, 28, 36, 28);
    v->setSpacing(20);

    m_titleLabel = new QLabel("PDF Tools", m_root);
    m_titleLabel->setObjectName("pdfTitle");
    v->addWidget(m_titleLabel);

    auto* sub = new QLabel(
        "Merge, split, and compress PDFs — no file ever leaves this machine.", m_root);
    sub->setObjectName("pdfSubtitle");
    v->addWidget(sub);

    auto* grid = new QGridLayout();
    grid->setSpacing(18);
    grid->addWidget(buildToolCard("Merge PDFs", "Combine multiple PDFs into one",
                                   "#2563EB", Lucide::kMerge, true, [this] { runMerge(); }), 0, 0);
    grid->addWidget(buildToolCard("Split PDF", "Extract a page range into a new file",
                                   "#16A34A", Lucide::kSplit, true, [this] { runSplit(); }), 0, 1);
    grid->addWidget(buildToolCard("Compress PDF", "Shrink file size losslessly",
                                   "#EA580C", Lucide::kFileArchive, true, [this] { runCompress(); }), 0, 2);
    grid->addWidget(buildToolCard("Convert", "PDF ↔ Word / Excel / Image — coming soon",
                                   "#64748B", Lucide::kRepeat, false,
                                   [this] { showComingSoon("Convert"); }), 0, 3);
    v->addLayout(grid);

    m_resultPanel = new QFrame(m_root);
    m_resultPanel->setObjectName("pdfResultPanel");
    m_resultPanel->setVisible(false);
    auto* rl = new QHBoxLayout(m_resultPanel);
    rl->setContentsMargins(16, 12, 16, 12);
    m_resultLabel = new QLabel(m_resultPanel);
    m_resultLabel->setObjectName("pdfResultLabel");
    m_resultLabel->setWordWrap(true);
    rl->addWidget(m_resultLabel, 1);
    m_revealBtn = new QPushButton("Open Folder", m_resultPanel);
    m_revealBtn->setCursor(Qt::PointingHandCursor);
    m_revealBtn->setVisible(false);
    connect(m_revealBtn, &QPushButton::clicked, this, [this] {
        if (!m_lastOutputFolder.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_lastOutputFolder));
    });
    rl->addWidget(m_revealBtn, 0, Qt::AlignVCenter);
    v->addWidget(m_resultPanel);

    v->addStretch();
}

QWidget* PdfModule::buildToolCard(const QString& title, const QString& subtitle, const QString& color,
                                  const char* icon, bool enabled, std::function<void()> onClick) {
    auto* card = new ClickableFrame(m_root);
    card->setObjectName(enabled ? "pdfToolCard" : "pdfToolCardDisabled");
    card->setMinimumSize(160, 130);
    auto* cv = new QVBoxLayout(card);
    cv->setContentsMargins(16, 16, 16, 14);
    cv->setSpacing(10);

    auto* iconLabel = new QLabel(card);
    iconLabel->setPixmap(Lucide::pixmap(icon, enabled ? color : "#7B8494", 28, card->devicePixelRatio()));
    iconLabel->setFixedSize(28, 28);
    iconLabel->setStyleSheet("background:transparent;");
    cv->addWidget(iconLabel);

    auto* titleLbl = new QLabel(title, card);
    titleLbl->setObjectName("pdfCardTitle");
    cv->addWidget(titleLbl);

    auto* subLbl = new QLabel(subtitle, card);
    subLbl->setObjectName("pdfCardSubtitle");
    subLbl->setWordWrap(true);
    cv->addWidget(subLbl);
    cv->addStretch();

    card->onClick = std::move(onClick);   // disabled cards still click through to "coming soon"
    return card;
}

void PdfModule::applyTheme() {
    const bool dark = ThemeManager::instance().isDark();
    const QString bg       = dark ? "#0D1117" : "#F5F6FA";
    const QString panelBg  = dark ? "#12161F" : "#FFFFFF";
    const QString border   = dark ? "#202836" : "#E5E7EB";
    const QString text     = dark ? "#EAEDF3" : "#1C1E26";
    const QString muted    = dark ? "#7B8494" : "#6B7280";
    const QString hover    = dark ? "#161D29" : "#F3F4F6";

    setStyleSheet(QString(R"(
QWidget#pdfRoot { background: %1; }
QLabel#pdfTitle { color: %2; font: 700 22px 'Segoe UI'; background: transparent; }
QLabel#pdfSubtitle { color: %3; font: 13px 'Segoe UI'; background: transparent; }
QFrame#pdfToolCard, QFrame#pdfToolCardDisabled {
    background: %4; border: 1px solid %5; border-radius: 14px;
}
QFrame#pdfToolCard:hover { border: 1px solid #3B82F6; background: %6; }
QLabel#pdfCardTitle { color: %2; font: 700 14px 'Segoe UI'; background: transparent; }
QLabel#pdfCardSubtitle { color: %3; font: 12px 'Segoe UI'; background: transparent; }
QFrame#pdfResultPanel { background: %4; border: 1px solid %5; border-radius: 10px; }
QLabel#pdfResultLabel { color: %2; font: 13px 'Segoe UI'; background: transparent; }
)").arg(bg, text, muted, panelBg, border, hover));
}

// ── Tools ──────────────────────────────────────────────────────────────────
void PdfModule::runMerge() {
    const QStringList inputs = QFileDialog::getOpenFileNames(
        this, "Select PDFs to merge (in order)", QDir::homePath(), "PDF Files (*.pdf)");
    if (inputs.size() < 2) return;

    const QString suggested = QFileInfo(inputs.first()).absolutePath() + "/Merged.pdf";
    const QString output = QFileDialog::getSaveFileName(this, "Save Merged PDF As", suggested, "PDF Files (*.pdf)");
    if (output.isEmpty()) return;

    const auto result = Pdf::mergePdfs(inputs, output);
    reportResult("Merge", result.ok, result.message, output);
}

void PdfModule::runSplit() {
    const QString input = QFileDialog::getOpenFileName(
        this, "Select a PDF to split", QDir::homePath(), "PDF Files (*.pdf)");
    if (input.isEmpty()) return;

    QString countError;
    const int total = Pdf::pdfPageCountOrError(input, countError);
    if (total < 0) { reportResult("Split", false, countError, {}); return; }

    QDialog dlg(this);
    dlg.setWindowTitle("Extract Page Range");
    auto* form = new QFormLayout(&dlg);
    auto* fromBox = new QSpinBox(&dlg); fromBox->setRange(1, total); fromBox->setValue(1);
    auto* toBox   = new QSpinBox(&dlg); toBox->setRange(1, total);   toBox->setValue(total);
    form->addRow(QString("This PDF has %1 pages. From:").arg(total), fromBox);
    form->addRow("To:", toBox);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted) return;

    const int from = fromBox->value(), to = toBox->value();
    if (from > to) { reportResult("Split", false, "\"From\" must be less than or equal to \"To\".", {}); return; }

    const QString suggested = QFileInfo(input).absolutePath() + "/"
        + QFileInfo(input).completeBaseName() + QString("_p%1-%2.pdf").arg(from).arg(to);
    const QString output = QFileDialog::getSaveFileName(this, "Save Extracted Pages As", suggested, "PDF Files (*.pdf)");
    if (output.isEmpty()) return;

    const auto result = Pdf::splitPdf(input, from, to, output);
    reportResult("Split", result.ok, result.message, output);
}

void PdfModule::runCompress() {
    const QString input = QFileDialog::getOpenFileName(
        this, "Select a PDF to compress", QDir::homePath(), "PDF Files (*.pdf)");
    if (input.isEmpty()) return;

    const QString suggested = QFileInfo(input).absolutePath() + "/"
        + QFileInfo(input).completeBaseName() + "_compressed.pdf";
    const QString output = QFileDialog::getSaveFileName(this, "Save Compressed PDF As", suggested, "PDF Files (*.pdf)");
    if (output.isEmpty()) return;

    const qint64 beforeSize = QFileInfo(input).size();
    const auto result = Pdf::compressPdf(input, output);
    if (result.ok) {
        const qint64 afterSize = QFileInfo(output).size();
        const qint64 saved = beforeSize - afterSize;
        const QString note = saved > 0
            ? QString(" (%1% smaller)").arg(int(100.0 * saved / qMax<qint64>(1, beforeSize)))
            : QString(" (already well-compressed — size unchanged)");
        reportResult("Compress", true, note, output);
    } else {
        reportResult("Compress", false, result.message, output);
    }
}

void PdfModule::showComingSoon(const QString& toolName) {
    QMessageBox::information(this, toolName,
        toolName + " isn't available yet — it needs a full PDF parser (for text/layout extraction) "
                   "that would meaningfully increase NativeOffice's install size, so it's on hold "
                   "pending that decision.");
}

void PdfModule::reportResult(const QString& opLabel, bool ok, const QString& message, const QString& outputPath) {
    m_resultPanel->setVisible(true);
    if (ok) {
        setCurrentPath(outputPath);
        m_lastOutputFolder = QFileInfo(outputPath).absolutePath();
        m_resultLabel->setText(QString("✓ %1 complete%2 — saved to %3")
                                    .arg(opLabel, message, outputPath));
        m_revealBtn->setVisible(true);
    } else {
        m_resultLabel->setText(QString("✕ %1 failed: %2").arg(opLabel, message));
        m_revealBtn->setVisible(false);
    }
}

} // namespace NativeOffice
