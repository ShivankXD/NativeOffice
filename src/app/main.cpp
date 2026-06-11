// ─────────────────────────────────────────────────────────────────────────────
// main.cpp — NativeOffice entry point  (Sprint 6)
//
// Sprint 6 additions:
//   • Writer  → File → Export to PDF…  (QPdfWriter + QTextDocument::print)
//   • Impress → File → Export to PDF…  (QPdfWriter + QGraphicsScene::render)
//   • Qt6::PrintSupport linked across all affected targets
//
// Sprint 3 changes (legacy):
//   • .noff file format for Save / Save As / Open
//   • Proper "Save" behaviour: silent overwrite if path known, else Save As
//   • Dirty flag (*) cleared on successful save
//   • File open triggers RecentFilesManager::addFile() → Start Screen refreshes
//   • File open from the Start Screen’s RecentFilesWidget works end-to-end
//   • QSettings organization/app name set before first QSettings use
// ─────────────────────────────────────────────────────────────────────────────
#include "startscreen/StartScreen.h"
#include "core/theme/ThemeManager.h"
#include "core/application/AppController.h"
#include "core/application/RecentFilesManager.h"

#include "WriterModule.h"
#include "CalcModule.h"
#include "ImpressModule.h"

#include <QApplication>
#include <QMainWindow>
#include <QScreen>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QMessageBox>
#include <QString>
#include <QCloseEvent>
// Sprint 6: PDF export
#include <QPdfWriter>
#include <QPainter>
#include <QPageSize>
#include <QPageLayout>

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────
static const QString NOFF_FILTER =
    "NativeOffice Document (*.noff);;HTML Document (*.html);;All Files (*)";
static const QString PDF_FILTER =
    "PDF Document (*.pdf)";

// ─────────────────────────────────────────────────────────────────────────────
// WriterWindow — QMainWindow subclass with close-confirmation logic
// ─────────────────────────────────────────────────────────────────────────────
class WriterWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit WriterWindow(QWidget* parent = nullptr)
        : QMainWindow(parent)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setMinimumSize(960, 640);
        resize(1200, 800);
    }

    void setWriter(NativeOffice::WriterModule* w) {
        m_writer = w;
        setCentralWidget(w);
        updateTitle();

        // Keep title in sync with dirty state
        connect(w, &NativeOffice::WriterModule::documentModified,
                this, &WriterWindow::updateTitle);
        connect(w, &NativeOffice::WriterModule::filePathChanged,
                this, [this](const QString&) { updateTitle(); });
    }

    NativeOffice::WriterModule* writer() const { return m_writer; }

    // ── Save logic ────────────────────────────────────────────────────────
    // Returns false if the user cancelled.
    bool saveAs() {
        const QString path = QFileDialog::getSaveFileName(
            this, "Save As…",
            m_writer->currentFilePath().isEmpty()
                ? QDir::homePath() + "/Untitled.noff"
                : m_writer->currentFilePath(),
            NOFF_FILTER);

        if (path.isEmpty()) return false;
        return performSave(path);
    }

    bool save() {
        if (m_writer->currentFilePath().isEmpty()) return saveAs();
        return performSave(m_writer->currentFilePath());
    }

public slots:
    void updateTitle() {
        setWindowTitle(m_writer ? m_writer->titleString() : "NativeOffice Writer");
    }

protected:
    void closeEvent(QCloseEvent* e) override {
        if (m_writer && m_writer->isDirty()) {
            const auto btn = QMessageBox::question(
                this, "Unsaved Changes",
                QString(""%1" has unsaved changes.\nDo you want to save before closing?")
                    .arg(m_writer->currentFilePath().isEmpty()
                             ? "Untitled Document"
                             : QFileInfo(m_writer->currentFilePath()).fileName()),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                QMessageBox::Save);

            if (btn == QMessageBox::Cancel) {
                e->ignore();
                return;
            }
            if (btn == QMessageBox::Save && !save()) {
                e->ignore();
                return;
            }
        }
        e->accept();
    }

private:
    bool performSave(const QString& path) {
        if (!m_writer->saveToPath(path)) {
            QMessageBox::critical(this, "Save Failed",
                "Could not write to:\n" + path);
            return false;
        }
        // Register in recent files
        NativeOffice::RecentFilesManager::instance().addFile(path, "Writer");
        updateTitle();
        return true;
    }

    NativeOffice::WriterModule* m_writer { nullptr };
};

// ─────────────────────────────────────────────────────────────────────────────
// Sprint 6: Writer PDF export helper
// ─────────────────────────────────────────────────────────────────────────────
static void writerExportToPdf(NativeOffice::WriterModule* writer, QWidget* parent) {
    // ── Get destination path ────────────────────────────────────────────────────
    // Suggest a PDF filename based on the current .noff file (or "Untitled")
    QString suggested = writer->currentFilePath();
    if (suggested.isEmpty()) {
        suggested = QDir::homePath() + "/Untitled.pdf";
    } else {
        // Replace extension: ".noff" → ".pdf"
        suggested = QFileInfo(suggested).absolutePath() + "/"
                  + QFileInfo(suggested).completeBaseName() + ".pdf";
    }

    const QString path = QFileDialog::getSaveFileName(
        parent, "Export Document to PDF", suggested, PDF_FILTER);
    if (path.isEmpty()) return;

    // ── Create A4 PDF writer ───────────────────────────────────────────────────
    QPdfWriter pdfWriter(path);
    pdfWriter.setCreator("NativeOffice Writer");
    pdfWriter.setTitle(QFileInfo(path).completeBaseName());
    pdfWriter.setPageSize(QPageSize::A4);
    pdfWriter.setPageOrientation(QPageLayout::Portrait);
    // 20 mm margins on all sides (matches the Writer canvas visual margin)
    pdfWriter.setPageMargins(QMarginsF(20, 20, 20, 20), QPageLayout::Millimeter);
    pdfWriter.setResolution(300);   // 300 dpi for print-quality output

    // ── Print via QTextDocument::print() ─────────────────────────────────────
    // QTextDocument::print() automatically handles pagination, rich text,
    // inline images, fonts, and colors — all at the target device resolution.
    writer->document()->print(&pdfWriter);

    QMessageBox::information(parent, "Export Complete",
        "Document exported to PDF successfully!\n\n" + path);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: build the Writer editor window with full menu bar
// ─────────────────────────────────────────────────────────────────────────────
static WriterWindow* createWriterWindow(const QString& filePath = {}) {
    auto* win    = new WriterWindow;
    auto* writer = new NativeOffice::WriterModule;
    win->setWriter(writer);

    // Centre on screen
    const QScreen* screen = QApplication::primaryScreen();
    win->move(screen->availableGeometry().center() - win->rect().center());

    // ── Load file if provided ─────────────────────────────────────────────
    if (!filePath.isEmpty()) {
        if (!writer->loadFromPath(filePath)) {
            QMessageBox::critical(win, "Open Failed",
                "Could not read:\n" + filePath);
        } else {
            // Bump to top of recent list
            NativeOffice::RecentFilesManager::instance().addFile(filePath, "Writer");
        }
    }

    // ── Menu bar ──────────────────────────────────────────────────────────
    auto* mb = win->menuBar();

    // File
    auto* fileMenu  = mb->addMenu("&File");
    auto* actNew    = fileMenu->addAction("&New");
    actNew->setShortcut(QKeySequence::New);
    auto* actOpen   = fileMenu->addAction("&Open…");
    actOpen->setShortcut(QKeySequence::Open);
    fileMenu->addSeparator();
    auto* actSave   = fileMenu->addAction("&Save");
    actSave->setShortcut(QKeySequence::Save);
    auto* actSaveAs = fileMenu->addAction("Save &As…");
    actSaveAs->setShortcut(QKeySequence::SaveAs);
    fileMenu->addSeparator();
    // Sprint 6: Export to PDF
    auto* actExportPdf = fileMenu->addAction("★ Export to PDF…");
    actExportPdf->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    actExportPdf->setToolTip("Export this document as a PDF file");
    fileMenu->addSeparator();
    auto* actClose  = fileMenu->addAction("&Close");
    actClose->setShortcut(QKeySequence::Close);

    // Edit
    auto* editMenu  = mb->addMenu("&Edit");
    auto* actUndo   = editMenu->addAction("&Undo");   actUndo->setShortcut(QKeySequence::Undo);
    auto* actRedo   = editMenu->addAction("&Redo");   actRedo->setShortcut(QKeySequence::Redo);
    editMenu->addSeparator();
    auto* actCut    = editMenu->addAction("Cu&t");    actCut ->setShortcut(QKeySequence::Cut);
    auto* actCopy   = editMenu->addAction("&Copy");   actCopy->setShortcut(QKeySequence::Copy);
    auto* actPaste  = editMenu->addAction("&Paste");  actPaste->setShortcut(QKeySequence::Paste);
    editMenu->addSeparator();
    auto* actSelAll = editMenu->addAction("Select &All"); actSelAll->setShortcut(QKeySequence::SelectAll);

    // View (placeholder for future sprints)
    mb->addMenu("&View");
    mb->addMenu("&Help");

    // ── Wire actions ──────────────────────────────────────────────────────
    QObject::connect(actNew, &QAction::triggered, win, []() {
        createWriterWindow()->show();
    });

    QObject::connect(actOpen, &QAction::triggered, win, [win]() {
        const QString path = QFileDialog::getOpenFileName(
            win, "Open Document",
            QDir::homePath(),
            NOFF_FILTER);
        if (!path.isEmpty()) {
            auto* newWin = createWriterWindow(path);
            newWin->show();
        }
    });

    QObject::connect(actSave,      &QAction::triggered, win, [win]() { win->save();   });
    QObject::connect(actSaveAs,    &QAction::triggered, win, [win]() { win->saveAs(); });
    QObject::connect(actExportPdf, &QAction::triggered, win, [win, writer]() {
        writerExportToPdf(writer, win);
    });
    QObject::connect(actClose,  &QAction::triggered, win, &QMainWindow::close);

    QObject::connect(actUndo,   &QAction::triggered, writer->editor(), &QTextEdit::undo);
    QObject::connect(actRedo,   &QAction::triggered, writer->editor(), &QTextEdit::redo);
    QObject::connect(actCut,    &QAction::triggered, writer->editor(), &QTextEdit::cut);
    QObject::connect(actCopy,   &QAction::triggered, writer->editor(), &QTextEdit::copy);
    QObject::connect(actPaste,  &QAction::triggered, writer->editor(), &QTextEdit::paste);
    QObject::connect(actSelAll, &QAction::triggered, writer->editor(), &QTextEdit::selectAll);

    return win;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: build the Impress presentation window with menu bar  (Sprint 6)
// ─────────────────────────────────────────────────────────────────────────────
static QMainWindow* createImpressWindow(const QString& title = {}) {
    auto* win     = new QMainWindow;
    auto* impress = new NativeOffice::ImpressModule(win);
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->setCentralWidget(impress);
    win->setWindowTitle(title.isEmpty()
                        ? "New Presentation — NativeOffice Impress"
                        : title + " — NativeOffice Impress");
    win->setMinimumSize(1100, 680);
    win->resize(1280, 800);

    // Centre on screen
    const QScreen* screen = QApplication::primaryScreen();
    win->move(screen->availableGeometry().center() - win->rect().center());

    // ── Menu bar ──────────────────────────────────────────────────────────
    auto* mb       = win->menuBar();
    auto* fileMenu = mb->addMenu("&File");

    auto* actNew = fileMenu->addAction("&New Presentation");
    actNew->setShortcut(QKeySequence::New);
    fileMenu->addSeparator();

    // Sprint 6: Export to PDF
    auto* actExportPdf = fileMenu->addAction("★ Export to PDF…");
    actExportPdf->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    actExportPdf->setToolTip("Export all slides to a PDF file");
    fileMenu->addSeparator();

    auto* actClose = fileMenu->addAction("&Close");
    actClose->setShortcut(QKeySequence::Close);

    mb->addMenu("&View");
    mb->addMenu("&Help");

    // ── Wire actions ──────────────────────────────────────────────────────
    QObject::connect(actNew, &QAction::triggered, win, []() {
        createImpressWindow()->show();
    });
    QObject::connect(actExportPdf, &QAction::triggered, win, [impress]() {
        impress->exportToPdf();
    });
    QObject::connect(actClose, &QAction::triggered, win, &QMainWindow::close);

    return win;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: open any document type based on file extension
// ─────────────────────────────────────────────────────────────────────────────
static void openDocumentByPath(const QString& path) {
    using NativeOffice::DocumentType;

    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "xlsx" || ext == "csv" || ext == "ods") {
        auto* win = new QMainWindow;
        win->setAttribute(Qt::WA_DeleteOnClose);
        win->setWindowTitle(QFileInfo(path).fileName() + " — NativeOffice Calc");
        win->setCentralWidget(new NativeOffice::CalcModule(win));
        win->resize(1200, 800);
        win->show();
    } else if (ext == "pptx" || ext == "odp") {
        createImpressWindow(QFileInfo(path).fileName())->show();
    } else {
        // Default: Writer (.noff, .html, or any unknown extension)
        createWriterWindow(path)->show();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);

    // IMPORTANT: Set org/app before the first QSettings use (including
    // RecentFilesManager singleton construction triggered below)
    app.setApplicationName("NativeOffice");
    app.setApplicationVersion("0.3.0");
    app.setOrganizationName("NativeOffice");
    app.setOrganizationDomain("nativeoffice.app");

    // ── Global theme ───────────────────────────────────────────────────────
    auto& theme = NativeOffice::ThemeManager::instance();
    app.setStyleSheet(theme.applicationStyleSheet());

    // ── App controller ─────────────────────────────────────────────────────
    NativeOffice::AppController controller;

    // ── Start screen window ────────────────────────────────────────────────
    QMainWindow startWindow;
    startWindow.setWindowTitle("NativeOffice");
    startWindow.setMinimumSize(1024, 680);

    const QScreen* screen = QApplication::primaryScreen();
    startWindow.resize(1280, 800);
    startWindow.move(screen->availableGeometry().center()
                     - startWindow.rect().center());

    auto* startScreen = new NativeOffice::StartScreen(&controller);
    startWindow.setCentralWidget(startScreen);

    // ── Start Screen → AppController ──────────────────────────────────────
    QObject::connect(startScreen, &NativeOffice::StartScreen::newDocumentRequested,
                     &controller,  &NativeOffice::AppController::newDocument);
    QObject::connect(startScreen, &NativeOffice::StartScreen::fileOpenRequested,
                     &controller,  &NativeOffice::AppController::openFile);
    QObject::connect(startScreen, &NativeOffice::StartScreen::settingsRequested,
                     &controller,  &NativeOffice::AppController::openSettings);

    // ── AppController → editor windows ────────────────────────────────────
    QObject::connect(&controller, &NativeOffice::AppController::newDocumentRequested,
                     &app, [](NativeOffice::DocumentType type) {
        using NativeOffice::DocumentType;
        switch (type) {
        case DocumentType::Writer:
            createWriterWindow()->show();
            break;
        case DocumentType::Calc: {
            auto* win = new QMainWindow;
            win->setAttribute(Qt::WA_DeleteOnClose);
            win->setWindowTitle("New Spreadsheet — NativeOffice Calc");
            win->setCentralWidget(new NativeOffice::CalcModule(win));
            win->resize(1200, 800);
            win->show();
            break;
        }
        case DocumentType::Impress:
            createImpressWindow()->show();
            break;
        }
    });

    QObject::connect(&controller, &NativeOffice::AppController::fileOpenRequested,
                     &app, [](const QString& path) {
        openDocumentByPath(path);
    });

    startWindow.show();
    return app.exec();
}

#include "main.moc"
