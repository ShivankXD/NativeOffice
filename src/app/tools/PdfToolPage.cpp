// ─────────────────────────────────────────────────────────────────────────────
// PdfToolPage.cpp: see PdfToolPage.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfToolPage.h"
#include "startscreen/HomeKit.h"
#include "startscreen/LucideIcons.h"

#include "PdfConvert.h"
#include "PdfOcr.h"
#include "PdfOps.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>

namespace NativeOffice {

namespace {

QString humanSize(qint64 bytes) {
    return QLocale().formattedDataSize(bytes, 2, QLocale::DataSizeTraditionalFormat);
}

const char* jobIcon(PdfToolPage::Job j) {
    switch (j) {
    case PdfToolPage::Job::Compress: return Lucide::kFileArchive;
    case PdfToolPage::Job::Ocr:      return Lucide::kScanText;
    case PdfToolPage::Job::ToWord:   break;
    }
    return Lucide::kFileDown;
}

QString jobAccent(PdfToolPage::Job j) {
    switch (j) {
    case PdfToolPage::Job::Compress: return Home::kPdf;
    case PdfToolPage::Job::Ocr:      return Home::kAccent;
    case PdfToolPage::Job::ToWord:   break;
    }
    return Home::kWriter;
}

} // namespace

QString PdfToolPage::jobTitle(Job job) {
    switch (job) {
    case Job::Compress: return QObject::tr("Compress PDF");
    case Job::Ocr:      return QObject::tr("OCR / Scan");
    case Job::ToWord:   break;
    }
    return QObject::tr("PDF to Word");
}

QString PdfToolPage::jobSubtitle(Job job) {
    switch (job) {
    case Job::Compress:
        return QObject::tr("Re-pack a PDF's streams at maximum compression. "
                           "Images are never re-encoded, so nothing loses quality.");
    case Job::Ocr:
        return QObject::tr("Recognise the text in a scanned PDF and lay it back over "
                           "the page, so the file becomes searchable and selectable.");
    case Job::ToWord:
        break;
    }
    return QObject::tr("Rebuild a PDF as an editable .docx, reconstructing paragraphs "
                       "from the page layout.");
}

PdfToolPage::PdfToolPage(Job job, QWidget* parent) : QWidget(parent), m_job(job) {
    setObjectName("pdfTool");
    setAcceptDrops(true);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addStretch();

    auto* card = new QFrame(this);
    card->setObjectName("toolCard");
    card->setFixedWidth(660);
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(34, 30, 34, 28);
    v->setSpacing(16);

    // ── Header ──────────────────────────────────────────────────────────────
    auto* head = new QHBoxLayout();
    head->setSpacing(14);
    auto* mark = new QLabel(card);
    mark->setFixedSize(46, 46);
    mark->setAlignment(Qt::AlignCenter);
    mark->setPixmap(Lucide::pixmap(jobIcon(job), "#FFFFFF", 22, devicePixelRatio()));
    mark->setStyleSheet(QString("background:%1;border-radius:13px;").arg(jobAccent(job)));
    head->addWidget(mark, 0, Qt::AlignTop);

    auto* titles = new QVBoxLayout();
    titles->setSpacing(4);
    titles->addWidget(heading(jobTitle(job), 19, Home::kText, true, card));
    auto* sub = heading(jobSubtitle(job), 12, Home::kMuted, false, card);
    sub->setWordWrap(true);
    titles->addWidget(sub);
    head->addLayout(titles, 1);
    v->addLayout(head);

    // ── Drop zone ───────────────────────────────────────────────────────────
    m_drop = new QFrame(card);
    m_drop->setObjectName("dropZone");
    m_drop->setFixedHeight(120);
    m_drop->setCursor(Qt::PointingHandCursor);
    auto* dl = new QVBoxLayout(m_drop);
    dl->setContentsMargins(16, 16, 16, 16);
    dl->setSpacing(8);
    dl->addStretch();
    auto* dropIcon = Lucide::label(Lucide::kUpload, Home::kMuted, 24, m_drop);
    dl->addWidget(dropIcon, 0, Qt::AlignHCenter);
    m_dropText = heading(tr("Drop a PDF here, or click to choose one"),
                         13, Home::kTextBody, false, m_drop);
    m_dropText->setAlignment(Qt::AlignCenter);
    dl->addWidget(m_dropText);
    dl->addStretch();
    // A plain QFrame has no click signal; the whole zone opens the picker.
    m_drop->installEventFilter(this);
    v->addWidget(m_drop);

    m_fileInfo = heading(QString(), 12, Home::kFaint, false, card);
    m_fileInfo->setVisible(false);
    v->addWidget(m_fileInfo);

    // ── The one option that matters per job ─────────────────────────────────
    auto* optRow = new QHBoxLayout();
    optRow->setSpacing(12);
    m_optionCaption = heading(QString(), 12, Home::kTextBody, false, card);
    optRow->addWidget(m_optionCaption);
    m_option = new QComboBox(card);
    m_option->setFixedWidth(220);
    optRow->addWidget(m_option);
    optRow->addStretch();

    switch (job) {
    case Job::Compress:
        m_optionCaption->setText(tr("Effort"));
        m_option->addItem(tr("Fast"), 4);
        m_option->addItem(tr("Balanced"), 7);
        m_option->addItem(tr("Smallest file"), 9);
        m_option->setCurrentIndex(1);
        break;
    case Job::Ocr: {
        m_optionCaption->setText(tr("Language"));
        const QStringList langs = Pdf::ocrLanguages();
        if (langs.isEmpty()) {
            m_option->addItem(tr("System default"), QString());
        } else {
            for (const QString& tag : langs)
                m_option->addItem(QLocale(tag).nativeLanguageName().isEmpty()
                                      ? tag
                                      : QLocale(tag).nativeLanguageName() +
                                            QStringLiteral(" (") + tag + QLatin1Char(')'),
                                  tag);
        }
        break;
    }
    case Job::ToWord:
        m_optionCaption->setVisible(false);
        m_option->setVisible(false);
        break;
    }
    v->addLayout(optRow);

    if (job == Job::Ocr && !Pdf::ocrAvailable()) {
        auto* warn = heading(tr("Windows has no OCR language pack installed. Add one in "
                                "Settings → Time & language → Language & region, then "
                                "reopen this tool."),
                             12, Home::kAmber, false, card);
        warn->setWordWrap(true);
        v->addWidget(warn);
    }

    // ── Run ─────────────────────────────────────────────────────────────────
    m_progress = new QProgressBar(card);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(6);
    m_progress->setVisible(false);
    v->addWidget(m_progress);

    auto* runRow = new QHBoxLayout();
    runRow->addStretch();
    m_runBtn = new QPushButton(job == Job::Compress ? tr("Compress")
                             : job == Job::Ocr      ? tr("Run OCR")
                                                    : tr("Convert"), card);
    m_runBtn->setObjectName("primary");
    m_runBtn->setCursor(Qt::PointingHandCursor);
    m_runBtn->setEnabled(false);
    connect(m_runBtn, &QPushButton::clicked, this, &PdfToolPage::run);
    runRow->addWidget(m_runBtn);
    v->addLayout(runRow);

    m_result = heading(QString(), 12, Home::kMuted, false, card);
    m_result->setWordWrap(true);
    m_result->setOpenExternalLinks(true);
    m_result->setVisible(false);
    v->addWidget(m_result);

    outer->addWidget(card, 0, Qt::AlignHCenter);
    outer->addStretch();

    // Dev-only, matching the NATIVEOFFICE_HOME_GRAB hook on the home screen:
    // the only way into this page is a file dialog, which cannot be driven
    // offscreen, so these let a build be exercised end to end without one.
    if (qEnvironmentVariableIsSet("NATIVEOFFICE_PDFTOOL_FILE")) {
        const QString in  = qEnvironmentVariable("NATIVEOFFICE_PDFTOOL_FILE");
        const QString out = qEnvironmentVariable("NATIVEOFFICE_PDFTOOL_OUT");
        QTimer::singleShot(300, this, [this, in, out] {
            setSource(in);
            if (!out.isEmpty()) runTo(out);
        });
    }

    setStyleSheet(QString(R"(
        QWidget#pdfTool { background:%1; }
        QFrame#toolCard { background:%2; border:1px solid %3; border-radius:18px; }
        QFrame#dropZone { background:%4; border:1px dashed %5; border-radius:12px; }
        QFrame#dropZone:hover { background:%6; border:1px dashed %7; }
        QComboBox { background:%4; border:1px solid %3; border-radius:8px;
            color:%8; padding:7px 10px; font:12px 'Segoe UI'; }
        QComboBox::drop-down { border:none; width:22px; }
        QComboBox::down-arrow { image:url(:/assets/chevron-down-light.png);
            width:10px; height:7px; }
        QComboBox QAbstractItemView { background:%2; border:1px solid %3; color:%8;
            selection-background-color:%6; outline:none; }
        QProgressBar { background:%4; border:none; border-radius:3px; }
        QProgressBar::chunk { background:%7; border-radius:3px; }
        QPushButton#primary { background:%7; border:none; border-radius:9px;
            color:#FFFFFF; font:600 13px 'Segoe UI'; padding:11px 30px; }
        QPushButton#primary:hover { background:%9; }
        QPushButton#primary:disabled { background:%4; color:%5; }
    )").arg(Home::kBg, Home::kPanel, Home::kBorder, Home::kPanelSoft, Home::kFaint,
            Home::kPanelHover, Home::kAccent, Home::kTextBody, Home::kAccentSoft));
}

bool PdfToolPage::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_drop && event->type() == QEvent::MouseButtonRelease) {
        chooseFile();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void PdfToolPage::dragEnterEvent(QDragEnterEvent* ev) {
    for (const QUrl& u : ev->mimeData()->urls())
        if (u.toLocalFile().endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)) {
            ev->acceptProposedAction();
            return;
        }
}

void PdfToolPage::dropEvent(QDropEvent* ev) {
    for (const QUrl& u : ev->mimeData()->urls()) {
        const QString p = u.toLocalFile();
        if (p.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)) {
            setSource(p);
            ev->acceptProposedAction();
            return;
        }
    }
}

void PdfToolPage::chooseFile() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choose a PDF"), QDir::homePath(), tr("PDF files (*.pdf)"));
    if (!path.isEmpty()) setSource(path);
}

void PdfToolPage::setSource(const QString& path) {
    const QFileInfo fi(path);
    if (!fi.exists()) return;
    m_source = path;
    m_sourceBytes = fi.size();

    QString error;
    const int pages = Pdf::pdfPageCountOrError(path, error);
    if (pages < 0) {
        m_source.clear();
        m_runBtn->setEnabled(false);
        report(false, error.isEmpty() ? tr("That PDF could not be opened.") : error);
        return;
    }

    m_dropText->setText(fi.fileName());
    m_fileInfo->setText(tr("%1  ·  %2").arg(humanSize(m_sourceBytes),
        pages == 1 ? tr("1 page") : tr("%1 pages").arg(pages)));
    m_fileInfo->setVisible(true);
    m_result->setVisible(false);
    m_runBtn->setEnabled(true);
}

QString PdfToolPage::defaultOutputPath() const {
    const QFileInfo fi(m_source);
    const QString base = fi.completeBaseName();
    switch (m_job) {
    case Job::Compress:
        return fi.absolutePath() + QLatin1Char('/') + base + QStringLiteral("-compressed.pdf");
    case Job::Ocr:
        return fi.absolutePath() + QLatin1Char('/') + base + QStringLiteral("-searchable.pdf");
    case Job::ToWord:
        break;
    }
    return fi.absolutePath() + QLatin1Char('/') + base + QStringLiteral(".docx");
}

void PdfToolPage::setBusy(bool busy) {
    m_runBtn->setEnabled(!busy && !m_source.isEmpty());
    m_drop->setEnabled(!busy);
    m_option->setEnabled(!busy);
    m_progress->setVisible(busy);
    m_progress->setRange(0, 0);          // indeterminate; OCR switches to real steps
}

void PdfToolPage::run() {
    if (m_source.isEmpty()) return;

    const QString suggested = defaultOutputPath();
    const QString filter = m_job == Job::ToWord ? tr("Word document (*.docx)")
                                                : tr("PDF file (*.pdf)");
    const QString out = QFileDialog::getSaveFileName(this, tr("Save result as"),
                                                     suggested, filter);
    if (out.isEmpty()) return;
    runTo(out);
}

void PdfToolPage::runTo(const QString& out) {
    if (m_source.isEmpty() || out.isEmpty()) return;

    setBusy(true);
    m_result->setVisible(false);

    const Job job = m_job;
    const QString in = m_source;
    const QVariant option = m_option->currentData();

    // A worker QThread rather than QtConcurrent, so the app does not have to
    // link (and ship) the Concurrent module for three buttons.
    auto result = std::make_shared<Pdf::OpResult>();
    QThread* worker = QThread::create([job, in, out, option, result] {
        switch (job) {
        case Job::Compress: *result = Pdf::compressPdf(in, out, option.toInt()); return;
        case Job::Ocr:      *result = Pdf::ocrPdf(in, out, option.toString(), 300); return;
        case Job::ToWord:   break;
        }
        *result = Pdf::toDocx(in, out);
    });
    connect(worker, &QThread::finished, this, [this, worker, out, result] {
        const Pdf::OpResult r = *result;
        worker->deleteLater();
        setBusy(false);
        if (!r.ok) {
            report(false, r.message.isEmpty() ? tr("The operation failed.") : r.message);
            return;
        }
        const qint64 after = QFileInfo(out).size();
        QString msg;
        if (m_job == Job::Compress) {
            const int pct = m_sourceBytes > 0
                ? int(100.0 * (m_sourceBytes - after) / double(m_sourceBytes)) : 0;
            msg = pct > 0
                ? tr("Saved %1, %2 → %3 (%4% smaller).")
                      .arg(QFileInfo(out).fileName(), humanSize(m_sourceBytes),
                           humanSize(after)).arg(pct)
                : tr("Saved %1, this PDF was already packed as tightly as it gets.")
                      .arg(QFileInfo(out).fileName());
        } else {
            msg = tr("Saved %1 (%2).").arg(QFileInfo(out).fileName(), humanSize(after));
        }
        report(true, msg + QStringLiteral(" <a href=\"%1\" style=\"color:%2;"
                                          "text-decoration:none;\">Open folder</a>")
                         .arg(QUrl::fromLocalFile(QFileInfo(out).absolutePath()).toString(),
                              Home::kAccentSoft));
    });
    worker->start();
}

void PdfToolPage::report(bool ok, const QString& message) {
    m_result->setTextFormat(Qt::RichText);
    m_result->setText((ok ? QStringLiteral("✓  ") : QStringLiteral("✕  ")) + message);
    m_result->setStyleSheet(QString("color:%1;font:12px 'Segoe UI';background:transparent;")
                                .arg(ok ? Home::kGreen : "#F87171"));
    m_result->setVisible(true);
}

} // namespace NativeOffice
