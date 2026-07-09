// ─────────────────────────────────────────────────────────────────────────────
// PdfModule.cpp — see PdfModule.h. Wires PdfRibbon actions to EditSession
// operations and owns the small interaction dialogs (page ranges, page size,
// find bar, print). Feature areas that live in their own files (watermarks,
// annotations, crypto, converters…) hook in through dispatch() as they land.
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfModule.h"
#include "PdfAnnots.h"
#include "PdfConvert.h"
#include "PdfCryptoUi.h"
#include "PdfOcr.h"
#include "PdfDecorUi.h"
#include "PdfEditSession.h"
#include "PdfForms.h"
#include "PdfOps.h"
#include "PdfSign.h"
#include "PdfSignUi.h"
#include "PdfSignature.h"
#include "PdfOrganizer.h"
#include "PdfPanels.h"
#include "PdfViewer.h"
#include "core/theme/ThemeManager.h"
#include "core/common/BrandBar.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <memory>

namespace NativeOffice {

using Pdf::AnnotSpec;
using Pdf::EditSession;
using Pdf::OpResult;

namespace {

// "1-3,5,9-12" → sorted unique 0-based indices; empty vector on parse error.
std::vector<int> parsePageRanges(const QString& text, int pageCount, bool* ok) {
    std::vector<int> pages;
    *ok = false;
    const QStringList parts = text.split(',', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return pages;
    for (const QString& rawPart : parts) {
        const QString part = rawPart.trimmed();
        const qsizetype dash = part.indexOf('-');
        bool aOk = false, bOk = false;
        int a = 0, b = 0;
        if (dash < 0) {
            a = b = part.toInt(&aOk);
            bOk = aOk;
        } else {
            a = part.left(dash).trimmed().toInt(&aOk);
            b = part.mid(dash + 1).trimmed().toInt(&bOk);
        }
        if (!aOk || !bOk || a < 1 || b > pageCount || a > b) return {};
        for (int p = a; p <= b; ++p) pages.push_back(p - 1);
    }
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    *ok = true;
    return pages;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// PdfFindBar — small floating find widget over the viewer (Ctrl+F).
// ─────────────────────────────────────────────────────────────────────────────
class PdfFindBar : public QWidget {
public:
    std::function<void(const QString&, int direction)> onFind;   // ±1
    std::function<void()> onClosed;

    explicit PdfFindBar(QWidget* parent)
        : QWidget(parent)
    {
        setObjectName("pdfFindBar");
        auto* h = new QHBoxLayout(this);
        h->setContentsMargins(10, 6, 6, 6);
        h->setSpacing(4);

        m_edit = new QLineEdit(this);
        m_edit->setPlaceholderText(tr("Find in document…"));
        m_edit->setFixedWidth(200);
        m_count = new QLabel(this);
        m_count->setObjectName("pdfFindCount");

        auto mk = [this](const QString& t, const QString& tip) {
            auto* b = new QToolButton(this);
            b->setText(t);
            b->setToolTip(tip);
            b->setAutoRaise(true);
            return b;
        };
        auto* prev  = mk(QStringLiteral("‹"), tr("Previous match"));
        auto* next  = mk(QStringLiteral("›"), tr("Next match (Enter)"));
        auto* close = mk(QStringLiteral("✕"), tr("Close (Esc)"));

        connect(m_edit, &QLineEdit::returnPressed, this, [this] { if (onFind) onFind(m_edit->text(), +1); });
        connect(prev,  &QToolButton::clicked, this, [this] { if (onFind) onFind(m_edit->text(), -1); });
        connect(next,  &QToolButton::clicked, this, [this] { if (onFind) onFind(m_edit->text(), +1); });
        connect(close, &QToolButton::clicked, this, [this] { hide(); if (onClosed) onClosed(); });

        h->addWidget(m_edit);
        h->addWidget(prev);
        h->addWidget(next);
        h->addWidget(m_count);
        h->addWidget(close);

        applyTheme();
        connect(&ThemeManager::instance(), &ThemeManager::modeChanged,
                this, [this](ThemeMode) { applyTheme(); });
        hide();
    }

    void open() {
        show();
        raise();
        m_edit->setFocus();
        m_edit->selectAll();
    }

    void setCountText(const QString& t) { m_count->setText(t); }

protected:
    void keyPressEvent(QKeyEvent* ev) override {
        if (ev->key() == Qt::Key_Escape) { hide(); if (onClosed) onClosed(); return; }
        QWidget::keyPressEvent(ev);
    }

private:
    void applyTheme() {
        const auto& tm = ThemeManager::instance();
        setStyleSheet(QString(R"(
QWidget#pdfFindBar { background: %1; border: 1px solid %2; border-radius: 8px; }
QWidget#pdfFindBar QLineEdit { background: %3; border: 1px solid %2; border-radius: 4px;
    padding: 3px 6px; color: %4; font: 9pt "Segoe UI"; }
QWidget#pdfFindBar QToolButton { color: %4; border: none; border-radius: 4px; padding: 3px 6px; }
QWidget#pdfFindBar QToolButton:hover { background: %5; }
QLabel#pdfFindCount { color: %6; font: 8.5pt "Segoe UI"; }
)")
            .arg(tm.chromePanelBg(), tm.chromeBorder(), tm.chromeBg(),
                 tm.chromeText(), tm.chromeHoverBg(), tm.chromeTextMuted()));
    }

    QLineEdit* m_edit  { nullptr };
    QLabel*    m_count { nullptr };
};

// ─────────────────────────────────────────────────────────────────────────────
// construction
// ─────────────────────────────────────────────────────────────────────────────

PdfModule::PdfModule(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("pdfModule");
    m_session = new EditSession(this);

    m_ribbon    = new PdfRibbon(this);
    m_viewer    = new Pdf::Viewer(m_session, this);
    m_sidebar   = new Pdf::Sidebar(m_session, this);
    m_status    = new Pdf::StatusBar(this);
    m_organizer = new PageOrganizer(m_session, this);

    m_center = new QStackedWidget(this);
    m_center->addWidget(m_viewer);      // 0 — all tabs except Page
    m_center->addWidget(m_organizer);   // 1 — Page tab

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(new BrandBar(this));   // unified full-width brand tray
    root->addWidget(m_ribbon);

    auto* body = new QWidget(this);
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addWidget(m_sidebar);
    bodyLayout->addWidget(m_center, 1);
    root->addWidget(body, 1);
    root->addWidget(m_status);

    m_findBar = new PdfFindBar(m_viewer);

    // ── ribbon → module ─────────────────────────────────────────────────
    connect(m_ribbon, &PdfRibbon::action, this, &PdfModule::dispatch);
    connect(m_ribbon, &PdfRibbon::zoomPercentRequested, this,
            [this](int pct) { m_viewer->setZoom(pct / 100.0); });
    connect(m_ribbon, &PdfRibbon::tabChanged, this, [this](int idx) {
        m_center->setCurrentIndex(idx == 2 ? 1 : 0);   // 2 == Page tab
    });
    connect(m_ribbon, &PdfRibbon::stampSelected, this,
            [this](const QString& s) { comingSoon(tr("Stamp \"%1\"").arg(s)); });

    // ── viewer/status/sidebar sync ──────────────────────────────────────
    connect(m_viewer, &Pdf::Viewer::currentPageChanged, this, [this](int p) {
        m_status->setPageInfo(p, m_session->pageCount());
        m_sidebar->setCurrentPage(p);
    });
    connect(m_viewer, &Pdf::Viewer::zoomChanged, this, [this](qreal z) {
        const int pct = int(z * 100 + 0.5);
        m_status->setZoomPercent(pct);
        m_ribbon->setZoomPercent(pct);
    });
    connect(m_status, &Pdf::StatusBar::pageJumpRequested, m_viewer, &Pdf::Viewer::goToPage);
    connect(m_status, &Pdf::StatusBar::prevPageRequested, this,
            [this] { m_viewer->goToPage(m_viewer->currentPage() - 1); });
    connect(m_status, &Pdf::StatusBar::nextPageRequested, this,
            [this] { m_viewer->goToPage(m_viewer->currentPage() + 1); });
    connect(m_status, &Pdf::StatusBar::zoomChanged, this,
            [this](int pct) { m_viewer->setZoom(pct / 100.0); });
    connect(m_status, &Pdf::StatusBar::zoomInRequested,  m_viewer, &Pdf::Viewer::zoomIn);
    connect(m_status, &Pdf::StatusBar::zoomOutRequested, m_viewer, &Pdf::Viewer::zoomOut);
    connect(m_status, &Pdf::StatusBar::fitWidthRequested, m_viewer, &Pdf::Viewer::fitWidth);
    connect(m_status, &Pdf::StatusBar::fitPageRequested,  m_viewer, &Pdf::Viewer::fitPage);
    connect(m_sidebar, &Pdf::Sidebar::pageActivated, m_viewer, &Pdf::Viewer::goToPage);
    connect(m_viewer, &Pdf::Viewer::openRequested, this, &PdfModule::openInteractive);

    // ── annotation placement (comment tools) ────────────────────────────
    connect(m_viewer, &Pdf::Viewer::rectPlaced,  this, &PdfModule::onRectPlaced);
    connect(m_viewer, &Pdf::Viewer::clickPlaced, this, &PdfModule::onClickPlaced);
    connect(m_viewer, &Pdf::Viewer::inkDrawn,    this, &PdfModule::onInkDrawn);
    connect(m_session, &EditSession::documentChanged, this, &PdfModule::refreshComments);
    // Clicking a comment in the pane jumps to its page.
    connect(m_sidebar->commentsList(), &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        const int page = it->data(Qt::UserRole).toInt();
        m_center->setCurrentIndex(0);
        m_viewer->goToPage(page);
    });

    // ── organizer → structural ops ──────────────────────────────────────
    connect(m_organizer, &PageOrganizer::reorderRequested, this, [this](const std::vector<int>& order) {
        const OpResult r = m_session->apply(tr("Reorder pages"),
            [&order](const QString& in, const QString& out) {
                return Pdf::reorderPages(in, order, out);
            });
        if (!r.ok) toast(r.message);
    });
    connect(m_organizer, &PageOrganizer::rotateRequested, this, [this](const std::vector<int>& pages, int deg) {
        const OpResult r = m_session->apply(tr("Rotate pages"),
            [&pages, deg](const QString& in, const QString& out) {
                return Pdf::rotatePages(in, pages, deg, out);
            });
        if (!r.ok) toast(r.message);
    });
    connect(m_organizer, &PageOrganizer::deleteRequested, this, [this](const std::vector<int>& pages) {
        const OpResult r = m_session->apply(tr("Delete pages"),
            [&pages](const QString& in, const QString& out) {
                return Pdf::deletePages(in, pages, out);
            });
        if (!r.ok) toast(r.message);
    });
    connect(m_organizer, &PageOrganizer::extractRequested, this, [this](const std::vector<int>& pages) {
        const QString out = QFileDialog::getSaveFileName(this, tr("Extract Pages"),
            QFileInfo(currentFilePath()).path() + "/extracted.pdf", tr("PDF files (*.pdf)"));
        if (out.isEmpty()) return;
        const OpResult r = Pdf::extractPages(m_session->currentRevisionPath(), pages, out);
        toast(r.ok ? tr("Extracted %1 page(s) to \"%2\".")
                         .arg(pages.size()).arg(QFileInfo(out).fileName())
                   : r.message);
    });
    connect(m_organizer, &PageOrganizer::insertBlankAfterRequested, this, [this](int after) {
        const QSizeF sz = m_session->renderer()->pageSizePt(std::max(0, after));
        const OpResult r = m_session->apply(tr("Insert blank page"),
            [after, sz](const QString& in, const QString& out) {
                return Pdf::insertBlankPage(in, after + 1, sz.width(), sz.height(), out);
            });
        if (!r.ok) toast(r.message);
    });
    connect(m_organizer, &PageOrganizer::pageActivated, this, [this](int page) {
        m_center->setCurrentIndex(0);
        m_viewer->goToPage(page);
    });

    // ── session → interface signals ─────────────────────────────────────
    connect(m_session, &EditSession::dirtyChanged, this, [this](bool) { emit documentModified(); });
    connect(m_session, &EditSession::filePathChanged, this, &PdfModule::filePathChanged);
    connect(m_session, &EditSession::documentChanged, this, [this] {
        m_status->setPageInfo(m_viewer->currentPage(), m_session->pageCount());
    });

    // ── shortcuts ───────────────────────────────────────────────────────
    auto addSc = [this](const QKeySequence& seq, std::function<void()> fn) {
        auto* sc = new QShortcut(seq, this);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, this, std::move(fn));
    };
    addSc(QKeySequence::Open,   [this] { dispatch(PdfAction::OpenFile); });
    addSc(QKeySequence::Save,   [this] { dispatch(PdfAction::SaveFile); });
    addSc(QKeySequence::SaveAs, [this] { dispatch(PdfAction::SaveFileAs); });
    addSc(QKeySequence::Undo,   [this] { dispatch(PdfAction::UndoEdit); });
    addSc(QKeySequence::Redo,   [this] { dispatch(PdfAction::RedoEdit); });
    addSc(QKeySequence::Find,   [this] { doFind(); });
    addSc(QKeySequence::Print,  [this] { doPrint(); });
    addSc(QKeySequence(Qt::Key_Escape), [this] {
        if (m_readMode) dispatch(PdfAction::ReadMode);
    });

    const auto& tm = ThemeManager::instance();
    setStyleSheet(QString("QWidget#pdfModule { background: %1; }").arg(tm.chromeBg()));

    // Dev-only capture hook: PrintWindow-based external captures drop
    // drawPixmap content on this machine, so verification screenshots come
    // from Qt's own render path instead. Set NATIVEOFFICE_PDF_GRAB to a .png
    // path to have the module grab itself a few seconds after startup.
    if (qEnvironmentVariableIsSet("NATIVEOFFICE_PDF_GRAB")) {
        const QString grabPath = qEnvironmentVariable("NATIVEOFFICE_PDF_GRAB");
        QTimer::singleShot(8000, this, [this, grabPath] {
            grab().save(grabPath, "PNG");
        });
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// interface for PdfWindow
// ─────────────────────────────────────────────────────────────────────────────

QString PdfModule::currentFilePath() const noexcept {
    return m_session->filePath();
}

bool PdfModule::isDirty() const noexcept {
    return m_session->isDirty();
}

QString PdfModule::titleString() const {
    if (!m_session->hasDocument()) return tr("NativeOffice PDF");
    const QString name = QFileInfo(m_session->filePath()).fileName();
    return (m_session->isDirty() ? QStringLiteral("● ") : QString())
           + name + tr(" — NativeOffice PDF");
}

void PdfModule::setInitialFile(const QString& path) {
    if (!path.isEmpty()) openPath(path);
}

bool PdfModule::saveToPath(const QString& path) {
    QString err;
    if (!m_session->saveAs(path, &err)) {
        if (!err.isEmpty()) toast(err);
        return false;
    }
    emit documentModified();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// open
// ─────────────────────────────────────────────────────────────────────────────

bool PdfModule::openPath(const QString& path) {
    QString password;
    for (;;) {
        QString err;
        bool needsPassword = false;
        if (m_session->openFile(path, password, &err, &needsPassword)) {
            m_viewer->fitWidthWhenReady();
            m_status->setPageInfo(0, m_session->pageCount());
            return true;
        }
        if (!needsPassword) {
            QMessageBox::warning(this, tr("Open PDF"), err);
            return false;
        }
        bool ok = false;
        password = QInputDialog::getText(this, tr("Password Required"),
            tr("\"%1\" is password-protected.\nEnter the password to open it:")
                .arg(QFileInfo(path).fileName()),
            QLineEdit::Password, {}, &ok);
        if (!ok) return false;
    }
}

void PdfModule::openInteractive() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open PDF"), QDir::homePath(), tr("PDF files (*.pdf)"));
    if (!path.isEmpty()) openPath(path);
}

// ─────────────────────────────────────────────────────────────────────────────
// dispatch
// ─────────────────────────────────────────────────────────────────────────────

std::vector<int> PdfModule::selectedOrCurrentPages() const {
    if (m_center->currentIndex() == 1) {
        auto sel = m_organizer->selectedPages();
        if (!sel.empty()) return sel;
    }
    if (m_session->hasDocument())
        return { m_viewer->currentPage() };
    return {};
}

void PdfModule::dispatch(PdfAction a) {
    using A = PdfAction;

    // Actions that work without a document.
    switch (a) {
    case A::OpenFile:     openInteractive(); return;
    case A::MergePdf:     doMerge(); return;
    case A::PictureToPdf: doPictureToPdf(); return;
    case A::Translate:    comingSoon(tr("Parallel Translate")); return;
    default: break;
    }

    if (!m_session->hasDocument()) {
        toast(tr("Open a PDF first."));
        return;
    }

    switch (a) {
    // ── tools / view ────────────────────────────────────────────────────
    case A::PanTool:
        m_viewer->setMode(Pdf::Viewer::Mode::Pan);
        m_ribbon->syncToolMode(true);
        break;
    case A::SelectTool:
        m_viewer->setMode(Pdf::Viewer::Mode::Select);
        m_ribbon->syncToolMode(false);
        break;
    case A::ZoomIn:  m_viewer->zoomIn();  break;
    case A::ZoomOut: m_viewer->zoomOut(); break;
    case A::ReadMode:
        m_readMode = !m_readMode;
        m_ribbon->setVisible(!m_readMode);
        m_sidebar->setVisible(!m_readMode);
        if (m_readMode) toast(tr("Read mode — press Esc to exit"));
        break;
    case A::Find: doFind(); break;

    // ── file ────────────────────────────────────────────────────────────
    case A::SaveFile:   doSave(false); break;
    case A::SaveFileAs: doSave(true);  break;
    case A::UndoEdit:
        if (!m_session->undo()) toast(tr("Nothing to undo."));
        break;
    case A::RedoEdit:
        if (!m_session->redo()) toast(tr("Nothing to redo."));
        break;
    case A::Print: doPrint(); break;

    // ── page structure ──────────────────────────────────────────────────
    case A::SplitPdf:        doSplit(); break;
    case A::Compress:        doCompress(); break;
    case A::RotateLeft:      doRotate(270, false); break;
    case A::RotateRight:     doRotate(90, false);  break;
    case A::RotateAllPages:  doRotate(90, true);   break;
    case A::DeletePages:     doDeletePages(); break;
    case A::ExtractPageBtn:  doExtractPages(); break;
    case A::InsertBlankPage: doInsertBlank(); break;
    case A::InsertFromFile:  doInsertFromFile(); break;
    case A::ReplacePages:    doReplacePages(); break;
    case A::PageSizeDlg:     doPageSize(); break;
    case A::CropPages:       doCropPages(); break;

    // ── text ────────────────────────────────────────────────────────────
    case A::ExtractText:     doExtractText(); break;

    // ── content edit (annotation-based) ─────────────────────────────────
    case A::AddText:    startCommentTool(int(AnnotSpec::Kind::PlainText), QColor("#1C1E26")); break;
    case A::AddPicture: {
        const QString img = QFileDialog::getOpenFileName(this, tr("Add Picture"),
            QDir::homePath(), tr("Images (*.png *.jpg *.jpeg *.bmp)"));
        if (img.isEmpty()) break;
        m_pendingAnnotImage = img;
        startCommentTool(int(AnnotSpec::Kind::Picture), Qt::transparent);
        toast(tr("Drag a box on the page to place the picture."));
        break;
    }
    case A::WipeOff:    startCommentTool(int(AnnotSpec::Kind::WipeOff), Qt::white); break;

    // ── comment tools ───────────────────────────────────────────────────
    case A::HighlightText:  startCommentTool(int(AnnotSpec::Kind::Highlight),  QColor("#FFD400")); break;
    case A::HighlightArea:  startCommentTool(int(AnnotSpec::Kind::Highlight),  QColor("#FFD400")); break;
    case A::Underline:      startCommentTool(int(AnnotSpec::Kind::Underline),  QColor("#E8372A")); break;
    case A::Strikethrough:  startCommentTool(int(AnnotSpec::Kind::StrikeOut),  QColor("#E8372A")); break;
    case A::TextComment:    startCommentTool(int(AnnotSpec::Kind::FreeText),   QColor("#1C1E26")); break;
    case A::TextBox:        startCommentTool(int(AnnotSpec::Kind::FreeText),   QColor("#1C1E26")); break;
    case A::Callout:        startCommentTool(int(AnnotSpec::Kind::Callout),    QColor("#1C1E26")); break;
    case A::NoteAnnot:      startCommentTool(int(AnnotSpec::Kind::Note),       QColor("#FFCC33")); break;
    case A::InkDraw:        startCommentTool(int(AnnotSpec::Kind::Ink),        QColor("#E8372A")); break;
    case A::ShapeRect:      startCommentTool(int(AnnotSpec::Kind::Square),     QColor("#E8372A")); break;
    case A::ShapeEllipse:   startCommentTool(int(AnnotSpec::Kind::Circle),     QColor("#E8372A")); break;
    case A::ShapeLine:      startCommentTool(int(AnnotSpec::Kind::Line),       QColor("#E8372A")); break;
    case A::ShapeArrow:     startCommentTool(int(AnnotSpec::Kind::Arrow),      QColor("#E8372A")); break;
    case A::ReplaceTextAnnot: startCommentTool(int(AnnotSpec::Kind::StrikeOut), QColor("#2563EB")); break;
    case A::InsertTextAnnot:  startCommentTool(int(AnnotSpec::Kind::Caret),     QColor("#2563EB")); break;
    case A::AttachFile: {
        const QString file = QFileDialog::getOpenFileName(this, tr("Attach File"), QDir::homePath());
        if (file.isEmpty()) break;
        m_pendingAnnotImage = file;   // reuse the "pending file" slot
        startCommentTool(int(AnnotSpec::Kind::FileAttachment), QColor("#2563EB"));
        toast(tr("Click on the page to place the attachment."));
        break;
    }
    case A::HideComments:
        m_commentsHidden = !m_commentsHidden;
        toast(m_commentsHidden ? tr("Comments hidden in the viewer.")
                               : tr("Comments shown."));
        break;
    case A::ManageComments:
        toast(tr("Open the Comments panel from the left sidebar to manage comments."));
        break;
    case A::ExportComments: doExportComments(); break;
    case A::ImportComments: doImportComments(); break;
    case A::AddLink:
        startCommentTool(int(AnnotSpec::Kind::Link), QColor("#2563EB"));
        toast(tr("Drag a box, then enter the URL it should open."));
        break;
    case A::AddBookmark:    doAddBookmark(); break;
    case A::Watermark:
        Pdf::runWatermarkUi(this, m_session, [this](const QString& m) { toast(m); });
        break;
    case A::Background:
        Pdf::runBackgroundUi(this, m_session, [this](const QString& m) { toast(m); });
        break;
    case A::PageNumber:
        Pdf::runPageNumberUi(this, m_session, [this](const QString& m) { toast(m); });
        break;
    case A::HeaderFooter:
        Pdf::runHeaderFooterUi(this, m_session, [this](const QString& m) { toast(m); });
        break;
    case A::FillForm:        doFillForm(); break;
    case A::HighlightFields: doHighlightFields(); break;
    case A::AddSignature:    doAddSignature(false); break;
    case A::AddInitials:     doAddSignature(true);  break;
    case A::Encrypt:      doEncrypt(); break;
    case A::CertSign:     doSign(); break;
    case A::Timestamp:    doTimestamp(); break;
    case A::ManageCerts:  Pdf::runCertificateManager(this); break;
    case A::ValidateSigs: doValidateSignatures(); break;
    case A::ToWord:          doConvert(A::ToWord); break;
    case A::ToExcel:         doConvert(A::ToExcel); break;
    case A::ToPpt:           doConvert(A::ToPpt); break;
    case A::ToPicture:       doConvert(A::ToPicture); break;
    case A::ToText:          doConvert(A::ToText); break;
    case A::ToImageOnlyPdf:  doConvert(A::ToImageOnlyPdf); break;
    case A::ExtractPicture:  doConvert(A::ExtractPicture); break;
    case A::OcrPdf:          doOcr(); break;
    case A::EditContent:     break;   // disabled button; unreachable
    default: break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// file actions
// ─────────────────────────────────────────────────────────────────────────────

void PdfModule::doSave(bool saveAs) {
    QString target = m_session->filePath();
    if (saveAs || target.isEmpty()) {
        target = QFileDialog::getSaveFileName(this, tr("Save PDF"),
            target.isEmpty() ? QDir::homePath() + "/document.pdf" : target,
            tr("PDF files (*.pdf)"));
        if (target.isEmpty()) return;
        if (QFileInfo(target).suffix().isEmpty()) target += ".pdf";
    }
    if (saveToPath(target))
        toast(tr("Saved \"%1\".").arg(QFileInfo(target).fileName()));
}

void PdfModule::doPrint() {
    if (!m_session->hasDocument()) { toast(tr("Open a PDF first.")); return; }

    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dlg(&printer, this);
    dlg.setWindowTitle(tr("Print PDF"));
    if (dlg.exec() != QDialog::Accepted) return;

    const int n = m_session->pageCount();
    int from = printer.fromPage() > 0 ? printer.fromPage() - 1 : 0;
    int to   = printer.toPage()   > 0 ? printer.toPage()   - 1 : n - 1;
    from = std::clamp(from, 0, n - 1);
    to   = std::clamp(to,   from, n - 1);

    QPainter painter(&printer);
    if (!painter.isActive()) { toast(tr("Could not start the print job.")); return; }

    for (int i = from; i <= to; ++i) {
        if (i > from) printer.newPage();
        const QSizeF pagePt = m_session->renderer()->pageSizePt(i);
        const QRectF target = printer.pageRect(QPrinter::DevicePixel);
        const qreal scale = std::min(target.width()  / pagePt.width(),
                                     target.height() / pagePt.height());
        const QImage img = m_session->renderer()->renderPage(i, scale);
        if (img.isNull()) continue;
        painter.drawImage(QPointF(0, 0), img);
    }
    painter.end();
    toast(tr("Sent %1 page(s) to the printer.").arg(to - from + 1));
}

// ─────────────────────────────────────────────────────────────────────────────
// structural ops (dialog + session apply)
// ─────────────────────────────────────────────────────────────────────────────

void PdfModule::doMerge() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Pick PDFs to merge (in order)"), QDir::homePath(), tr("PDF files (*.pdf)"));
    if (files.size() < 2) {
        if (!files.isEmpty()) toast(tr("Pick at least two PDF files to merge."));
        return;
    }
    const QString out = QFileDialog::getSaveFileName(this, tr("Save merged PDF"),
        QFileInfo(files.first()).path() + "/merged.pdf", tr("PDF files (*.pdf)"));
    if (out.isEmpty()) return;

    const OpResult r = Pdf::mergePdfs(files, out);
    if (!r.ok) { toast(r.message); return; }
    if (QMessageBox::question(this, tr("Merge PDFs"),
            tr("Merged %1 files.\nOpen the result?").arg(files.size()))
        == QMessageBox::Yes)
        openPath(out);
}

void PdfModule::doSplit() {
    const int n = m_session->pageCount();
    bool ok = false;
    const QString range = QInputDialog::getText(this, tr("Split PDF"),
        tr("Extract pages (e.g. 1-3) of %1 into a new file:").arg(n),
        QLineEdit::Normal, QStringLiteral("1-%1").arg(n), &ok);
    if (!ok) return;
    bool parsed = false;
    const std::vector<int> pages = parsePageRanges(range, n, &parsed);
    if (!parsed || pages.empty()) { toast(tr("That page range isn't valid.")); return; }

    const QString out = QFileDialog::getSaveFileName(this, tr("Save split PDF"),
        QFileInfo(currentFilePath()).path() + "/split.pdf", tr("PDF files (*.pdf)"));
    if (out.isEmpty()) return;
    const OpResult r = Pdf::extractPages(m_session->currentRevisionPath(), pages, out);
    toast(r.ok ? tr("Wrote %1 page(s) to \"%2\".").arg(pages.size()).arg(QFileInfo(out).fileName())
               : r.message);
}

void PdfModule::doCompress() {
    const QString out = QFileDialog::getSaveFileName(this, tr("Save compressed PDF"),
        QFileInfo(currentFilePath()).path() + "/"
            + QFileInfo(currentFilePath()).completeBaseName() + "-compressed.pdf",
        tr("PDF files (*.pdf)"));
    if (out.isEmpty()) return;
    const qint64 before = QFileInfo(m_session->currentRevisionPath()).size();
    const OpResult r = Pdf::compressPdf(m_session->currentRevisionPath(), out);
    if (!r.ok) { toast(r.message); return; }
    const qint64 after = QFileInfo(out).size();
    toast(tr("Compressed: %1 KB → %2 KB.").arg(before / 1024).arg(after / 1024));
}

void PdfModule::doRotate(int degreesCW, bool allPages) {
    std::vector<int> pages;
    if (!allPages) pages = selectedOrCurrentPages();
    const OpResult r = m_session->apply(tr("Rotate"),
        [&pages, degreesCW](const QString& in, const QString& out) {
            return Pdf::rotatePages(in, pages, degreesCW, out);
        });
    if (!r.ok) toast(r.message);
}

void PdfModule::doDeletePages() {
    const int n = m_session->pageCount();
    std::vector<int> pages = selectedOrCurrentPages();
    const QString preset = pages.size() == 1 ? QString::number(pages[0] + 1) : QString();
    bool ok = false;
    const QString range = QInputDialog::getText(this, tr("Delete Pages"),
        tr("Pages to delete (e.g. 2 or 1-3,5) of %1:").arg(n),
        QLineEdit::Normal, preset, &ok);
    if (!ok) return;
    bool parsed = false;
    pages = parsePageRanges(range, n, &parsed);
    if (!parsed || pages.empty()) { toast(tr("That page range isn't valid.")); return; }

    const OpResult r = m_session->apply(tr("Delete pages"),
        [&pages](const QString& in, const QString& out) {
            return Pdf::deletePages(in, pages, out);
        });
    if (!r.ok) toast(r.message);
}

void PdfModule::doExtractPages() {
    const int n = m_session->pageCount();
    bool ok = false;
    const QString range = QInputDialog::getText(this, tr("Extract Pages"),
        tr("Pages to extract (e.g. 2 or 1-3,5) of %1:").arg(n),
        QLineEdit::Normal, QString::number(m_viewer->currentPage() + 1), &ok);
    if (!ok) return;
    bool parsed = false;
    const std::vector<int> pages = parsePageRanges(range, n, &parsed);
    if (!parsed || pages.empty()) { toast(tr("That page range isn't valid.")); return; }

    const QString out = QFileDialog::getSaveFileName(this, tr("Extract Pages"),
        QFileInfo(currentFilePath()).path() + "/extracted.pdf", tr("PDF files (*.pdf)"));
    if (out.isEmpty()) return;
    const OpResult r = Pdf::extractPages(m_session->currentRevisionPath(), pages, out);
    toast(r.ok ? tr("Extracted %1 page(s).").arg(pages.size()) : r.message);
}

void PdfModule::doInsertBlank() {
    const int cur = m_viewer->currentPage();
    bool ok = false;
    const int after = QInputDialog::getInt(this, tr("Insert Blank Page"),
        tr("Insert after page (0 = at the beginning):"),
        cur + 1, 0, m_session->pageCount(), 1, &ok);
    if (!ok) return;
    const QSizeF sz = m_session->renderer()->pageSizePt(
        std::clamp(cur, 0, m_session->pageCount() - 1));
    const OpResult r = m_session->apply(tr("Insert blank page"),
        [after, sz](const QString& in, const QString& out) {
            return Pdf::insertBlankPage(in, after, sz.width(), sz.height(), out);
        });
    if (!r.ok) toast(r.message);
}

void PdfModule::doInsertFromFile() {
    const QString src = QFileDialog::getOpenFileName(this, tr("Insert Pages From PDF"),
        QDir::homePath(), tr("PDF files (*.pdf)"));
    if (src.isEmpty()) return;
    bool ok = false;
    const int after = QInputDialog::getInt(this, tr("Insert Pages"),
        tr("Insert after page (0 = at the beginning):"),
        m_viewer->currentPage() + 1, 0, m_session->pageCount(), 1, &ok);
    if (!ok) return;
    const OpResult r = m_session->apply(tr("Insert pages"),
        [&src, after](const QString& in, const QString& out) {
            return Pdf::insertPdfAt(in, src, after, out);
        });
    if (!r.ok) toast(r.message);
}

void PdfModule::doReplacePages() {
    const int n = m_session->pageCount();
    bool ok = false;
    const QString range = QInputDialog::getText(this, tr("Replace Pages"),
        tr("Pages to replace (a contiguous range, e.g. 2-4) of %1:").arg(n),
        QLineEdit::Normal, QString::number(m_viewer->currentPage() + 1), &ok);
    if (!ok) return;
    bool parsed = false;
    const std::vector<int> pages = parsePageRanges(range, n, &parsed);
    if (!parsed || pages.empty()) { toast(tr("That page range isn't valid.")); return; }
    for (size_t i = 1; i < pages.size(); ++i)
        if (pages[i] != pages[i - 1] + 1) { toast(tr("Pick a contiguous range.")); return; }

    const QString src = QFileDialog::getOpenFileName(this, tr("Replacement PDF"),
        QDir::homePath(), tr("PDF files (*.pdf)"));
    if (src.isEmpty()) return;

    const int from = pages.front(), to = pages.back();
    const OpResult r = m_session->apply(tr("Replace pages"),
        [from, to, &src](const QString& in, const QString& out) {
            return Pdf::replacePages(in, from, to, src, out);
        });
    if (!r.ok) toast(r.message);
}

void PdfModule::doPageSize() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Page Size"));
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    auto* form = new QFormLayout(&dlg);

    auto* preset = new QComboBox(&dlg);
    struct Size { const char* name; double w, h; };
    static const Size kSizes[] = {
        { "A4 (210 × 297 mm)",    595.28, 841.89  },
        { "Letter (8.5 × 11 in)", 612.0,  792.0   },
        { "Legal (8.5 × 14 in)",  612.0,  1008.0  },
        { "A3 (297 × 420 mm)",    841.89, 1190.55 },
        { "A5 (148 × 210 mm)",    419.53, 595.28  },
    };
    for (const auto& s : kSizes) preset->addItem(s.name);
    preset->addItem(tr("Custom…"));

    auto* wSpin = new QDoubleSpinBox(&dlg);
    wSpin->setRange(18, 14400); wSpin->setValue(595.28); wSpin->setSuffix(" pt");
    auto* hSpin = new QDoubleSpinBox(&dlg);
    hSpin->setRange(18, 14400); hSpin->setValue(841.89); hSpin->setSuffix(" pt");
    wSpin->setEnabled(false); hSpin->setEnabled(false);

    connect(preset, &QComboBox::currentIndexChanged, &dlg, [&](int idx) {
        const bool custom = idx >= int(std::size(kSizes));
        wSpin->setEnabled(custom);
        hSpin->setEnabled(custom);
        if (!custom) { wSpin->setValue(kSizes[idx].w); hSpin->setValue(kSizes[idx].h); }
    });

    auto* scope = new QComboBox(&dlg);
    scope->addItem(tr("All pages"));
    scope->addItem(tr("Current page"));

    form->addRow(tr("Size:"), preset);
    form->addRow(tr("Width:"), wSpin);
    form->addRow(tr("Height:"), hSpin);
    form->addRow(tr("Apply to:"), scope);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted) return;

    std::vector<int> pages;
    if (scope->currentIndex() == 1) pages = { m_viewer->currentPage() };
    const double w = wSpin->value(), h = hSpin->value();
    const OpResult r = m_session->apply(tr("Page size"),
        [&pages, w, h](const QString& in, const QString& out) {
            return Pdf::resizePages(in, pages, w, h, out);
        });
    if (!r.ok) toast(r.message);
}

void PdfModule::doCropPages() {
    m_center->setCurrentIndex(0);
    m_viewer->setMode(Pdf::Viewer::Mode::PlaceRect, QStringLiteral("crop"));
    toast(tr("Drag an area on a page to crop to it."));

    // One-shot connection; reset to Select afterwards.
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(m_viewer, &Pdf::Viewer::rectPlaced, this,
        [this, conn](const QString& tag, int page, const QRectF& rectPt) {
            if (tag != QStringLiteral("crop")) return;
            QObject::disconnect(*conn);
            m_viewer->setMode(Pdf::Viewer::Mode::Select);
            m_ribbon->syncToolMode(false);

            QMessageBox box(this);
            box.setWindowTitle(tr("Crop Pages"));
            box.setText(tr("Crop to the selected area?"));
            auto* thisPageBtn = box.addButton(tr("This Page"), QMessageBox::YesRole);
            auto* allPagesBtn = box.addButton(tr("All Pages"), QMessageBox::AcceptRole);
            box.addButton(QMessageBox::Cancel);
            box.exec();
            if (box.clickedButton() != thisPageBtn && box.clickedButton() != allPagesBtn)
                return;

            std::vector<int> pages;
            if (box.clickedButton() == thisPageBtn) pages = { page };

            // top-left origin → PDF bottom-left coordinates
            const qreal pageH = m_session->renderer()->pageSizePt(page).height();
            const double x0 = rectPt.left();
            const double x1 = rectPt.right();
            const double y1 = pageH - rectPt.top();
            const double y0 = pageH - rectPt.bottom();

            const OpResult r = m_session->apply(tr("Crop pages"),
                [&pages, x0, y0, x1, y1](const QString& in, const QString& out) {
                    return Pdf::setCropBox(in, pages, x0, y0, x1, y1, out);
                });
            if (!r.ok) toast(r.message);
        });
}

// ─────────────────────────────────────────────────────────────────────────────
// find / text
// ─────────────────────────────────────────────────────────────────────────────

void PdfModule::doFind() {
    if (!m_session->hasDocument()) { toast(tr("Open a PDF first.")); return; }

    m_findBar->adjustSize();
    m_findBar->move(m_viewer->width() - m_findBar->width() - 24, 12);
    m_findBar->open();

    m_findBar->onClosed = [this] { m_viewer->clearHighlights(); };
    m_findBar->onFind = [this](const QString& needle, int dir) {
        if (needle.isEmpty()) return;
        m_viewer->clearHighlights();

        // Search from the current page outward in `dir`, wrapping once.
        const int n = m_session->pageCount();
        int start = m_viewer->currentPage();
        if (needle == m_lastFindNeedle && m_lastFindPage >= 0)
            start = m_lastFindPage + dir;

        for (int k = 0; k < n; ++k) {
            int page = (start + dir * k) % n;
            if (page < 0) page += n;
            const auto rects = m_session->renderer()->searchPage(page, needle);
            if (rects.empty()) continue;
            m_viewer->setHighlightRects(page, rects);
            m_viewer->goToPage(page);
            m_findBar->setCountText(tr("%1 hit(s) on page %2").arg(rects.size()).arg(page + 1));
            m_lastFindNeedle = needle;
            m_lastFindPage = page;
            return;
        }
        m_findBar->setCountText(tr("Not found"));
        m_lastFindNeedle.clear();
        m_lastFindPage = -1;
    };
}

void PdfModule::doExtractText() {
    const QString out = QFileDialog::getSaveFileName(this, tr("Extract Text"),
        QFileInfo(currentFilePath()).path() + "/"
            + QFileInfo(currentFilePath()).completeBaseName() + ".txt",
        tr("Text files (*.txt)"));
    if (out.isEmpty()) return;

    QFile f(out);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        toast(tr("Could not write \"%1\".").arg(out));
        return;
    }
    const int n = m_session->pageCount();
    for (int i = 0; i < n; ++i) {
        f.write(m_session->renderer()->pageText(i).toUtf8());
        if (i + 1 < n) f.write("\n\n");
    }
    f.close();
    toast(tr("Extracted text from %1 page(s).").arg(n));
}

// ─────────────────────────────────────────────────────────────────────────────
// comment / annotation tools
// ─────────────────────────────────────────────────────────────────────────────

void PdfModule::startCommentTool(int kind, const QColor& color) {
    m_center->setCurrentIndex(0);   // annotations live on the viewer, not the organizer
    m_pendingAnnot = kind;
    m_pendingAnnotColor = color;

    using K = AnnotSpec::Kind;
    const K k = K(kind);
    if (k == K::Note)
        m_viewer->setMode(Pdf::Viewer::Mode::PlaceClick, QStringLiteral("annot"));
    else if (k == K::FileAttachment)
        m_viewer->setMode(Pdf::Viewer::Mode::PlaceClick, QStringLiteral("annot"));
    else if (k == K::Ink)
        m_viewer->setMode(Pdf::Viewer::Mode::Ink, QStringLiteral("annot"));
    else
        m_viewer->setMode(Pdf::Viewer::Mode::PlaceRect, QStringLiteral("annot"));

    m_ribbon->syncToolMode(false);
}

void PdfModule::onRectPlaced(const QString& tag, int page, const QRectF& rectPt) {
    if (tag != QStringLiteral("annot") || m_pendingAnnot < 0) return;
    using K = AnnotSpec::Kind;
    const K kind = K(m_pendingAnnot);
    const int pending = m_pendingAnnot;
    m_pendingAnnot = -1;
    m_viewer->setMode(Pdf::Viewer::Mode::Select);

    if (kind == K::Link) { doAddLink(page, rectPt); return; }

    AnnotSpec spec;
    spec.kind = kind;
    spec.pageIndex = page;
    spec.rect = rectPt;
    spec.color = m_pendingAnnotColor;

    // Text-markup snaps to the actual glyphs under the drag; "Highlight Area"
    // (which also uses Highlight kind) falls back to the raw rect if the drag
    // covered no text.
    if (kind == K::Highlight || kind == K::Underline || kind == K::StrikeOut ||
        kind == K::Caret) {
        auto quads = m_session->renderer()->snapTextRects(page, rectPt);
        if (!quads.empty()) spec.quads = quads;
        else if (kind == K::Highlight) spec.quads = { rectPt };  // area highlight
        else { toast(tr("Drag across some text to mark it.")); return; }
    }

    // Text-bearing kinds prompt for their content.
    if (kind == K::FreeText || kind == K::Callout || kind == K::PlainText) {
        bool ok = false;
        const QString text = QInputDialog::getMultiLineText(this, tr("Comment Text"),
            tr("Enter the text:"), {}, &ok);
        if (!ok || text.isEmpty()) return;
        spec.contents = text;
    }
    if (kind == K::StrikeOut && pending == int(K::StrikeOut)) {
        // Replace-text mark carries a suggested replacement note (optional).
    }
    if (kind == K::Picture) {
        spec.kind = K::Picture;
        spec.filePath = m_pendingAnnotImage;
        m_pendingAnnotImage.clear();
    }

    const OpResult r = m_session->apply(tr("Add annotation"),
        [&spec](const QString& in, const QString& out) {
            return Pdf::addAnnotation(in, out, spec);
        });
    if (!r.ok) toast(r.message);
}

void PdfModule::onClickPlaced(const QString& tag, int page, const QPointF& posPt) {
    if (tag != QStringLiteral("annot") || m_pendingAnnot < 0) return;
    using K = AnnotSpec::Kind;
    const K kind = K(m_pendingAnnot);
    m_pendingAnnot = -1;
    m_viewer->setMode(Pdf::Viewer::Mode::Select);

    AnnotSpec spec;
    spec.kind = kind;
    spec.pageIndex = page;
    spec.rect = QRectF(posPt, QSizeF(20, 20));
    spec.color = m_pendingAnnotColor;

    if (kind == K::Note) {
        bool ok = false;
        const QString text = QInputDialog::getMultiLineText(this, tr("Note"),
            tr("Note text:"), {}, &ok);
        if (!ok) return;
        spec.contents = text;
    } else if (kind == K::FileAttachment) {
        spec.filePath = m_pendingAnnotImage;
        m_pendingAnnotImage.clear();
    }

    const OpResult r = m_session->apply(tr("Add annotation"),
        [&spec](const QString& in, const QString& out) {
            return Pdf::addAnnotation(in, out, spec);
        });
    if (!r.ok) toast(r.message);
}

void PdfModule::onInkDrawn(const QString& tag, int page, const QPolygonF& strokePt) {
    if (tag != QStringLiteral("annot") || m_pendingAnnot < 0) return;
    m_pendingAnnot = -1;
    m_viewer->setMode(Pdf::Viewer::Mode::Select);

    AnnotSpec spec;
    spec.kind = AnnotSpec::Kind::Ink;
    spec.pageIndex = page;
    spec.ink = strokePt;
    spec.rect = strokePt.boundingRect();
    spec.color = m_pendingAnnotColor;
    spec.borderWidth = 2.0;

    const OpResult r = m_session->apply(tr("Draw"),
        [&spec](const QString& in, const QString& out) {
            return Pdf::addAnnotation(in, out, spec);
        });
    if (!r.ok) toast(r.message);
}

void PdfModule::doAddLink(int page, const QRectF& rectPt) {
    bool ok = false;
    const QString url = QInputDialog::getText(this, tr("Add Link"),
        tr("URL to open when clicked:"), QLineEdit::Normal,
        QStringLiteral("https://"), &ok);
    if (!ok || url.isEmpty()) return;

    AnnotSpec spec;
    spec.kind = AnnotSpec::Kind::Link;
    spec.pageIndex = page;
    spec.rect = rectPt;
    spec.url = url;

    const OpResult r = m_session->apply(tr("Add link"),
        [&spec](const QString& in, const QString& out) {
            return Pdf::addAnnotation(in, out, spec);
        });
    if (!r.ok) toast(r.message);
}

void PdfModule::doAddBookmark() {
    bool ok = false;
    const QString title = QInputDialog::getText(this, tr("Add Bookmark"),
        tr("Bookmark title:"), QLineEdit::Normal,
        tr("Page %1").arg(m_viewer->currentPage() + 1), &ok);
    if (!ok || title.isEmpty()) return;
    const int page = m_viewer->currentPage();
    const OpResult r = m_session->apply(tr("Add bookmark"),
        [&title, page](const QString& in, const QString& out) {
            return Pdf::addOutlineBookmark(in, out, title, page);
        });
    if (!r.ok) toast(r.message);
    else toast(tr("Bookmark added — see the Bookmarks panel."));
}

void PdfModule::refreshComments() {
    QListWidget* list = m_sidebar->commentsList();
    list->clear();
    if (!m_session->hasDocument()) return;

    const auto annots = Pdf::listAnnotations(m_session->currentRevisionPath());
    for (const auto& a : annots) {
        QString label = QStringLiteral("p.%1  %2").arg(a.pageIndex + 1).arg(a.subtype);
        if (!a.contents.isEmpty())
            label += QStringLiteral("\n%1").arg(a.contents.left(80));
        auto* item = new QListWidgetItem(label, list);
        item->setData(Qt::UserRole, a.pageIndex);
        if (!a.author.isEmpty())
            item->setToolTip(tr("%1 — %2").arg(a.author, a.contents));
    }
    if (annots.empty())
        new QListWidgetItem(tr("No comments yet."), list);
}

void PdfModule::doExportComments() {
    const auto annots = Pdf::listAnnotations(m_session->currentRevisionPath());
    if (annots.empty()) { toast(tr("This document has no comments to export.")); return; }
    const QString out = QFileDialog::getSaveFileName(this, tr("Export Comments"),
        QFileInfo(currentFilePath()).path() + "/comments.xfdf", tr("XFDF files (*.xfdf)"));
    if (out.isEmpty()) return;
    const OpResult r = Pdf::exportXfdf(m_session->currentRevisionPath(), out);
    toast(r.ok ? tr("Exported %1 comment(s).").arg(annots.size()) : r.message);
}

void PdfModule::doImportComments() {
    const QString src = QFileDialog::getOpenFileName(this, tr("Import Comments"),
        QDir::homePath(), tr("XFDF files (*.xfdf)"));
    if (src.isEmpty()) return;
    const OpResult r = m_session->apply(tr("Import comments"),
        [&src](const QString& in, const QString& out) {
            return Pdf::importXfdf(in, src, out);
        });
    toast(r.ok ? tr("Comments imported.") : r.message);
}

// ─────────────────────────────────────────────────────────────────────────────
// fill & sign
// ─────────────────────────────────────────────────────────────────────────────

void PdfModule::doFillForm() {
    const auto fields = Pdf::detectFormFields(m_session->currentRevisionPath());
    if (fields.empty()) {
        toast(tr("This PDF has no fillable form fields."));
        return;
    }

    // Count text fields — only those are editable here.
    std::vector<const Pdf::FormField*> textFields;
    for (const auto& f : fields)
        if (f.type == Pdf::FormField::Type::Text) textFields.push_back(&f);
    if (textFields.empty()) {
        toast(tr("This form has %1 field(s), but none are text fields we can fill yet.")
                  .arg(fields.size()));
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Fill out Form"));
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    dlg.resize(460, std::min(560, 80 + int(textFields.size()) * 46));
    auto* outer = new QVBoxLayout(&dlg);

    auto* scroll = new QScrollArea(&dlg);
    scroll->setWidgetResizable(true);
    auto* inner = new QWidget;
    auto* form = new QFormLayout(inner);

    std::vector<QLineEdit*> edits;
    edits.reserve(textFields.size());
    for (const Pdf::FormField* f : textFields) {
        auto* edit = new QLineEdit(f->value, inner);
        edit->setPlaceholderText(tr("(page %1)").arg(f->pageIndex + 1));
        form->addRow(f->fullName, edit);
        edits.push_back(edit);
    }
    scroll->setWidget(inner);
    outer->addWidget(scroll, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    outer->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) return;

    std::map<QString, QString> values;
    for (size_t i = 0; i < textFields.size(); ++i)
        values[textFields[i]->fullName] = edits[i]->text();

    const OpResult r = m_session->apply(tr("Fill form"),
        [&values](const QString& in, const QString& out) {
            return Pdf::fillTextFields(in, out, values);
        });
    toast(r.ok ? tr("Form filled.") : r.message);
}

void PdfModule::doHighlightFields() {
    m_center->setCurrentIndex(0);
    if (m_fieldsHighlighted) {
        m_viewer->clearHighlights();
        m_fieldsHighlighted = false;
        return;
    }
    const auto fields = Pdf::detectFormFields(m_session->currentRevisionPath());
    if (fields.empty()) { toast(tr("This PDF has no form fields.")); return; }

    std::map<int, std::vector<QRectF>> byPage;
    for (const auto& f : fields)
        if (f.pageIndex >= 0) byPage[f.pageIndex].push_back(f.rect);
    for (const auto& [page, rects] : byPage)
        m_viewer->setHighlightRects(page, rects);
    m_fieldsHighlighted = true;
    toast(tr("Highlighted %1 field(s). Click again to hide.").arg(fields.size()));
}

void PdfModule::doAddSignature(bool initials) {
    const QImage sig = Pdf::runSignatureDialog(this, initials);
    if (sig.isNull()) return;

    // Stash the signature to a temp PNG for the Picture-annotation placement.
    const QString tmp = QDir::tempPath() + "/nosig_"
        + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".png";
    if (!sig.save(tmp, "PNG")) { toast(tr("Could not prepare the signature image.")); return; }

    m_pendingAnnotImage = tmp;
    startCommentTool(int(AnnotSpec::Kind::Picture), Qt::transparent);
    toast(initials ? tr("Drag a small box to place your initials.")
                   : tr("Drag a box on the page to place your signature."));
}

// ─────────────────────────────────────────────────────────────────────────────
// protect
// ─────────────────────────────────────────────────────────────────────────────

void PdfModule::doEncrypt() {
    const auto res = Pdf::runEncryptDialog(this);
    if (!res.confirmed) return;

    const QString base = QFileInfo(currentFilePath()).completeBaseName();
    const QString suggested = QFileInfo(currentFilePath()).path()
        + "/" + (base.isEmpty() ? "document" : base) + "-protected.pdf";
    const QString out = QFileDialog::getSaveFileName(this, tr("Save Encrypted PDF"),
        suggested, tr("PDF files (*.pdf)"));
    if (out.isEmpty()) return;

    const OpResult r = Pdf::encryptDocument(m_session->currentRevisionPath(), out, res.options);
    if (!r.ok) { toast(r.message); return; }

    if (QMessageBox::question(this, tr("Encrypt"),
            tr("Saved an encrypted copy to \"%1\".\nOpen it now?")
                .arg(QFileInfo(out).fileName()))
        == QMessageBox::Yes)
        openPath(out);
    else
        toast(tr("Encrypted copy saved."));
}

void PdfModule::doSign() {
    const auto res = Pdf::runSignDialog(this);
    if (!res.confirmed) return;

    const QString base = QFileInfo(currentFilePath()).completeBaseName();
    const QString out = QFileDialog::getSaveFileName(this, tr("Save Signed PDF"),
        QFileInfo(currentFilePath()).path() + "/" + (base.isEmpty() ? "document" : base) + "-signed.pdf",
        tr("PDF files (*.pdf)"));
    if (out.isEmpty()) return;

    const OpResult r = Pdf::signPdf(m_session->currentRevisionPath(), out, res.options);
    if (!r.ok) { toast(r.message); return; }

    if (QMessageBox::question(this, tr("Certificate Signature"),
            tr("Signed copy saved to \"%1\".\nOpen it now?").arg(QFileInfo(out).fileName()))
        == QMessageBox::Yes)
        openPath(out);
    else
        toast(tr("Document signed."));
}

void PdfModule::doTimestamp() {
    const QString tsa = Pdf::runTimestampDialog(this);
    if (tsa.isEmpty()) return;

    const QString base = QFileInfo(currentFilePath()).completeBaseName();
    const QString out = QFileDialog::getSaveFileName(this, tr("Save Timestamped PDF"),
        QFileInfo(currentFilePath()).path() + "/" + (base.isEmpty() ? "document" : base) + "-timestamped.pdf",
        tr("PDF files (*.pdf)"));
    if (out.isEmpty()) return;

    toast(tr("Contacting the timestamp authority…"));
    QApplication::processEvents();
    const OpResult r = Pdf::timestampPdf(m_session->currentRevisionPath(), out, tsa);
    if (!r.ok) { toast(r.message); return; }
    toast(tr("Timestamp added — saved to \"%1\".").arg(QFileInfo(out).fileName()));
}

void PdfModule::doValidateSignatures() {
    const auto results = Pdf::validateSignatures(m_session->currentRevisionPath());
    Pdf::showValidationReport(this, results);
}

// ─────────────────────────────────────────────────────────────────────────────
// convert
// ─────────────────────────────────────────────────────────────────────────────

void PdfModule::doConvert(PdfAction which) {
    using A = PdfAction;
    const QString src = m_session->currentRevisionPath();
    const QString baseDir = QFileInfo(currentFilePath()).path();
    const QString baseName = QFileInfo(currentFilePath()).completeBaseName().isEmpty()
        ? QStringLiteral("document") : QFileInfo(currentFilePath()).completeBaseName();

    auto saveAs = [&](const QString& filter, const QString& ext) {
        QString out = QFileDialog::getSaveFileName(this, tr("Convert"),
            baseDir + "/" + baseName + "." + ext, filter);
        if (!out.isEmpty() && QFileInfo(out).suffix().isEmpty()) out += "." + ext;
        return out;
    };

    Pdf::OpResult r{ false, tr("Unsupported conversion.") };
    switch (which) {
    case A::ToText: {
        const QString out = saveAs(tr("Text files (*.txt)"), "txt");
        if (out.isEmpty()) return;
        r = Pdf::toTxt(src, out);
        break;
    }
    case A::ToWord: {
        const QString out = saveAs(tr("Word documents (*.docx)"), "docx");
        if (out.isEmpty()) return;
        r = Pdf::toDocx(src, out);
        break;
    }
    case A::ToExcel: {
        const QString out = saveAs(tr("Excel workbooks (*.xlsx)"), "xlsx");
        if (out.isEmpty()) return;
        r = Pdf::toXlsx(src, out);
        break;
    }
    case A::ToPpt: {
        const QString out = saveAs(tr("PowerPoint presentations (*.pptx)"), "pptx");
        if (out.isEmpty()) return;
        r = Pdf::toPptx(src, out, 150);
        break;
    }
    case A::ToImageOnlyPdf: {
        const QString out = saveAs(tr("PDF files (*.pdf)"), "pdf");
        if (out.isEmpty()) return;
        r = Pdf::toImageOnlyPdf(src, out, 150);
        break;
    }
    case A::ToPicture:
    case A::ExtractPicture: {
        const QString dir = QFileDialog::getExistingDirectory(this,
            which == A::ToPicture ? tr("Save Page Images To…") : tr("Extract Pictures To…"),
            baseDir);
        if (dir.isEmpty()) return;
        QStringList written;
        r = which == A::ToPicture
            ? Pdf::toImages(src, dir, baseName, Pdf::RasterFormat::Png, 150, &written)
            : Pdf::extractImages(src, dir, baseName, &written);
        if (r.ok) { toast(tr("Wrote %1 image(s) to \"%2\".").arg(written.size()).arg(QDir(dir).dirName())); return; }
        break;
    }
    default: break;
    }

    toast(r.ok ? tr("Converted successfully.") : r.message);
}

void PdfModule::doOcr() {
    if (!Pdf::ocrAvailable()) {
        toast(tr("OCR needs a Windows OCR language pack "
                 "(Settings › Time & Language › Language › Optional features)."));
        return;
    }

    const QStringList langs = Pdf::ocrLanguages();
    QDialog dlg(this);
    dlg.setWindowTitle(tr("OCR PDF"));
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    auto* form = new QFormLayout(&dlg);
    auto* langCombo = new QComboBox(&dlg);
    langCombo->addItems(langs);
    auto* dpiCombo = new QComboBox(&dlg);
    dpiCombo->addItems({ "200 dpi (faster)", "300 dpi (recommended)", "400 dpi (best)" });
    dpiCombo->setCurrentIndex(1);
    form->addRow(tr("Language:"), langCombo);
    form->addRow(tr("Resolution:"), dpiCombo);
    form->addRow(new QLabel(tr("Adds an invisible, searchable text layer over the page images."), &dlg));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Run OCR"));
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString lang = langCombo->currentText();
    const int dpi = dpiCombo->currentIndex() == 0 ? 200 : dpiCombo->currentIndex() == 2 ? 400 : 300;

    toast(tr("Recognizing text… this may take a moment."));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QApplication::processEvents();

    const OpResult r = m_session->apply(tr("OCR"),
        [&lang, dpi](const QString& in, const QString& out) {
            return Pdf::ocrPdf(in, out, lang, dpi);
        });
    QApplication::restoreOverrideCursor();
    toast(r.ok ? tr("OCR complete — the text is now searchable (Ctrl+F).") : r.message);
}

void PdfModule::doPictureToPdf() {
    const QStringList images = QFileDialog::getOpenFileNames(this,
        tr("Pick images to combine into a PDF"), QDir::homePath(),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)"));
    if (images.isEmpty()) return;
    const QString out = QFileDialog::getSaveFileName(this, tr("Save PDF"),
        QFileInfo(images.first()).path() + "/images.pdf", tr("PDF files (*.pdf)"));
    if (out.isEmpty()) return;
    const OpResult r = Pdf::imagesToPdf(images, out);
    if (!r.ok) { toast(r.message); return; }
    if (QMessageBox::question(this, tr("Picture to PDF"),
            tr("Created \"%1\".\nOpen it now?").arg(QFileInfo(out).fileName()))
        == QMessageBox::Yes)
        openPath(out);
}

// ─────────────────────────────────────────────────────────────────────────────
// notices
// ─────────────────────────────────────────────────────────────────────────────

void PdfModule::toast(const QString& message) {
    auto* label = new QLabel(message, m_viewer);
    label->setObjectName("pdfToast");
    const auto& tm = ThemeManager::instance();
    label->setStyleSheet(QString(
        "QLabel#pdfToast { background: %1; color: %2; border: 1px solid %3;"
        " border-radius: 8px; padding: 8px 16px; font: 9.5pt 'Segoe UI'; }")
        .arg(tm.chromePanelBg(), tm.chromeText(), tm.chromeBorder()));
    label->adjustSize();
    label->move((m_viewer->width() - label->width()) / 2,
                m_viewer->height() - label->height() - 32);
    label->show();
    label->raise();
    QTimer::singleShot(3200, label, &QLabel::deleteLater);
}

void PdfModule::comingSoon(const QString& feature) {
    toast(tr("%1 — coming soon.").arg(feature));
}

void PdfModule::setReadOnly(bool on) {
    if (m_ribbon) m_ribbon->setEnabled(!on);
}

} // namespace NativeOffice
