// ─────────────────────────────────────────────────────────────────────────────
// main.cpp — NativeOffice entry point  (Sprint 8)
//
// Sprint 8 additions:
//   • CalcWindow     — QMainWindow subclass with Save/SaveAs/Open/dirty/close
//   • ImpressWindow  — QMainWindow subclass with Save/SaveAs/Open/dirty/close
//   • createCalcWindow(filePath)    — full menu bar, file loading
//   • createImpressWindow(filePath) — upgraded with file persistence
//   • openDocumentByPath uses new helpers for all three module types
//
// Sprint 7: Smart content-based file routing via FileRouter
// Sprint 6: Writer/Impress PDF export
// Sprint 3: .noff file format, WriterWindow, RecentFilesManager
// ─────────────────────────────────────────────────────────────────────────────
#include "startscreen/StartScreen.h"
#include "core/theme/ThemeManager.h"
#include "core/application/AppController.h"
#include "core/application/RecentFilesManager.h"
#include "core/application/FileRouter.h"

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
// Open: accept Excel, NativeOffice and CSV spreadsheets.
static const QString CALC_FILTER =
    "Spreadsheets (*.xlsx *.noff *.csv);;Excel Workbook (*.xlsx);;"
    "NativeOffice Spreadsheet (*.noff);;CSV File (*.csv);;All Files (*)";
// Save As: Excel (.xlsx) is offered first so it is the default format.
static const QString CALC_SAVE_FILTER =
    "Excel Workbook (*.xlsx);;NativeOffice Spreadsheet (*.noff);;CSV File (*.csv)";
static const QString IMPRESS_FILTER =
    "NativeOffice Presentation (*.noff);;All Files (*)";
// Save As: PowerPoint (.pptx) is offered first so it is the default format.
static const QString IMPRESS_SAVE_FILTER =
    "PowerPoint Presentation (*.pptx);;NativeOffice Presentation (*.noff);;All Files (*)";
// Open: accept PowerPoint and NativeOffice presentations.
static const QString IMPRESS_OPEN_FILTER =
    "Presentations (*.pptx *.noff);;PowerPoint Presentation (*.pptx);;"
    "NativeOffice Presentation (*.noff);;All Files (*)";
static const QString PDF_FILTER =
    "PDF Document (*.pdf)";

// Forward declarations for free-function helpers
static class WriterWindow* createWriterWindow(const QString& filePath = {});
static class CalcWindow*   createCalcWindow(const QString& filePath = {});
static class ImpressWindow* createImpressWindow(const QString& filePath = {});

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
                QString("\"%1\" has unsaved changes.\nDo you want to save before closing?")
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
// CalcWindow — QMainWindow subclass with close-confirmation logic (Sprint 8)
// ─────────────────────────────────────────────────────────────────────────────
class CalcWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit CalcWindow(QWidget* parent = nullptr)
        : QMainWindow(parent)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setMinimumSize(960, 640);
        resize(1200, 800);
    }

    void setCalc(NativeOffice::CalcModule* c) {
        m_calc = c;
        setCentralWidget(c);
        updateTitle();

        connect(c, &NativeOffice::CalcModule::documentModified,
                this, &CalcWindow::updateTitle);
        connect(c, &NativeOffice::CalcModule::filePathChanged,
                this, [this](const QString&) { updateTitle(); });
    }

    NativeOffice::CalcModule* calc() const { return m_calc; }

    bool saveAs() {
        QString selectedFilter;
        QString path = QFileDialog::getSaveFileName(
            this, "Save As…",
            m_calc->currentFilePath().isEmpty()
                ? QDir::homePath() + "/Untitled.xlsx"
                : m_calc->currentFilePath(),
            CALC_SAVE_FILTER, &selectedFilter);

        if (path.isEmpty()) return false;
        // Append the extension matching the chosen filter if the user omitted one.
        if (QFileInfo(path).suffix().isEmpty()) {
            if      (selectedFilter.contains("noff")) path += ".noff";
            else if (selectedFilter.contains("csv"))  path += ".csv";
            else                                       path += ".xlsx";
        }
        return performSave(path);
    }

    bool save() {
        if (m_calc->currentFilePath().isEmpty()) return saveAs();
        return performSave(m_calc->currentFilePath());
    }

public slots:
    void updateTitle() {
        setWindowTitle(m_calc ? m_calc->titleString() : "NativeOffice Calc");
    }

protected:
    void closeEvent(QCloseEvent* e) override {
        if (m_calc && m_calc->isDirty()) {
            const auto btn = QMessageBox::question(
                this, "Unsaved Changes",
                QString("\"%1\" has unsaved changes.\nDo you want to save before closing?")
                    .arg(m_calc->currentFilePath().isEmpty()
                             ? "Untitled Spreadsheet"
                             : QFileInfo(m_calc->currentFilePath()).fileName()),
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
        if (!m_calc->saveToPath(path)) {
            QMessageBox::critical(this, "Save Failed",
                "Could not write to:\n" + path);
            return false;
        }
        NativeOffice::RecentFilesManager::instance().addFile(path, "Calc");
        updateTitle();
        return true;
    }

    NativeOffice::CalcModule* m_calc { nullptr };
};

// ─────────────────────────────────────────────────────────────────────────────
// ImpressWindow — QMainWindow subclass with close-confirmation logic (Sprint 8)
// ─────────────────────────────────────────────────────────────────────────────
class ImpressWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ImpressWindow(QWidget* parent = nullptr)
        : QMainWindow(parent)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setMinimumSize(1100, 680);
        resize(1280, 800);
    }

    void setImpress(NativeOffice::ImpressModule* im) {
        m_impress = im;
        setCentralWidget(im);
        updateTitle();

        connect(im, &NativeOffice::ImpressModule::documentModified,
                this, &ImpressWindow::updateTitle);
        connect(im, &NativeOffice::ImpressModule::filePathChanged,
                this, [this](const QString&) { updateTitle(); });
    }

    NativeOffice::ImpressModule* impress() const { return m_impress; }

    bool saveAs() {
        const QString suggested =
            m_impress->currentFilePath().isEmpty()
                ? QDir::homePath() + "/Untitled.pptx"
                : QFileInfo(m_impress->currentFilePath()).absolutePath() + "/"
                      + QFileInfo(m_impress->currentFilePath()).completeBaseName() + ".pptx";
        const QString path = QFileDialog::getSaveFileName(
            this, "Save As…", suggested, IMPRESS_SAVE_FILTER);

        if (path.isEmpty()) return false;
        return performSave(path);
    }

    bool save() {
        if (m_impress->currentFilePath().isEmpty()) return saveAs();
        return performSave(m_impress->currentFilePath());
    }

public slots:
    void updateTitle() {
        setWindowTitle(m_impress ? m_impress->titleString() : "NativeOffice Impress");
    }

protected:
    void closeEvent(QCloseEvent* e) override {
        if (m_impress && m_impress->isDirty()) {
            const auto btn = QMessageBox::question(
                this, "Unsaved Changes",
                QString("\"%1\" has unsaved changes.\nDo you want to save before closing?")
                    .arg(m_impress->currentFilePath().isEmpty()
                             ? "Untitled Presentation"
                             : QFileInfo(m_impress->currentFilePath()).fileName()),
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
        // Route by extension: a .pptx path is exported as a PowerPoint package;
        // anything else is saved as the native .noff document.
        const bool ok = path.endsWith(".pptx", Qt::CaseInsensitive)
                            ? m_impress->exportPptxTo(path)
                            : m_impress->saveToPath(path);
        if (!ok) {
            QMessageBox::critical(this, "Save Failed",
                "Could not write to:\n" + path);
            return false;
        }
        NativeOffice::RecentFilesManager::instance().addFile(path, "Impress");
        updateTitle();
        return true;
    }

    NativeOffice::ImpressModule* m_impress { nullptr };
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
static WriterWindow* createWriterWindow(const QString& filePath) {
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
// Helper: build the Calc spreadsheet window with full menu bar  (Sprint 8)
// ─────────────────────────────────────────────────────────────────────────────
static CalcWindow* createCalcWindow(const QString& filePath) {
    auto* win  = new CalcWindow;
    auto* calc = new NativeOffice::CalcModule;
    win->setCalc(calc);

    // Centre on screen
    const QScreen* screen = QApplication::primaryScreen();
    win->move(screen->availableGeometry().center() - win->rect().center());

    // ── Load file if provided ─────────────────────────────────────────────
    if (!filePath.isEmpty()) {
        if (!calc->loadFromPath(filePath)) {
            QMessageBox::critical(win, "Open Failed",
                "Could not read:\n" + filePath);
        } else {
            NativeOffice::RecentFilesManager::instance().addFile(filePath, "Calc");
        }
    }

    // ── Menu bar ──────────────────────────────────────────────────────────
    auto* mb       = win->menuBar();
    // Light, clean menu bar (no black hero strip) to sit above the banner.
    mb->setStyleSheet(R"(
QMenuBar {
    background-color: #FFFFFF;
    color: #2C3140;
    border-bottom: 1px solid #E6E8ED;
    padding: 2px 6px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-size: 13px;
}
QMenuBar::item { background: transparent; color: #2C3140; padding: 5px 12px; border-radius: 6px; }
QMenuBar::item:selected { background-color: #EAF3EE; color: #107C41; }
QMenuBar::item:pressed  { background-color: #E0EFE6; color: #0E6F3A; }
)");
    auto* fileMenu = mb->addMenu("&File");

    auto* actNew = fileMenu->addAction("&New Spreadsheet");
    actNew->setShortcut(QKeySequence::New);
    auto* actOpen = fileMenu->addAction("&Open…");
    actOpen->setShortcut(QKeySequence::Open);
    fileMenu->addSeparator();
    auto* actSave   = fileMenu->addAction("&Save");
    actSave->setShortcut(QKeySequence::Save);
    auto* actSaveAs = fileMenu->addAction("Save &As…");
    actSaveAs->setShortcut(QKeySequence::SaveAs);
    fileMenu->addSeparator();
    auto* actClose = fileMenu->addAction("&Close");
    actClose->setShortcut(QKeySequence::Close);

    auto* editMenu = mb->addMenu("&Edit");
    editMenu->addAction(calc->undoAction());
    editMenu->addAction(calc->redoAction());
    editMenu->addSeparator();
    editMenu->addAction(calc->cutAction());
    editMenu->addAction(calc->copyAction());
    editMenu->addAction(calc->pasteAction());
    editMenu->addAction(calc->deleteAction());

    mb->addMenu("&View");
    mb->addMenu("&Help");

    // ── Wire actions ──────────────────────────────────────────────────────
    QObject::connect(actNew, &QAction::triggered, win, []() {
        createCalcWindow()->show();
    });
    QObject::connect(actOpen, &QAction::triggered, win, [win]() {
        const QString path = QFileDialog::getOpenFileName(
            win, "Open Spreadsheet",
            QDir::homePath(),
            CALC_FILTER);
        if (!path.isEmpty()) {
            createCalcWindow(path)->show();
        }
    });
    QObject::connect(actSave,   &QAction::triggered, win, [win]() { win->save();   });
    QObject::connect(actSaveAs, &QAction::triggered, win, [win]() { win->saveAs(); });
    QObject::connect(actClose,  &QAction::triggered, win, &QMainWindow::close);

    return win;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: build the Impress presentation window with full menu bar  (Sprint 8)
// ─────────────────────────────────────────────────────────────────────────────
static ImpressWindow* createImpressWindow(const QString& filePath) {
    auto* win     = new ImpressWindow;
    auto* impress = new NativeOffice::ImpressModule(win);
    win->setImpress(impress);

    // Centre on screen
    const QScreen* screen = QApplication::primaryScreen();
    win->move(screen->availableGeometry().center() - win->rect().center());

    // ── Load file if provided ─────────────────────────────────────────────
    if (!filePath.isEmpty()) {
        if (!impress->loadFromPath(filePath)) {
            QMessageBox::critical(win, "Open Failed",
                "Could not read:\n" + filePath);
        } else {
            NativeOffice::RecentFilesManager::instance().addFile(filePath, "Impress");
        }
    }

    // ── Menu bar ──────────────────────────────────────────────────────────
    auto* mb       = win->menuBar();
    // Light menu bar so the top of the window reads as a clean branded strip
    // rather than the old flat-black hero band.
    mb->setStyleSheet(R"(
QMenuBar {
    background-color: #F3F4F6;
    color: #2C3140;
    border-bottom: 1px solid #E2E4E9;
    padding: 2px 6px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-size: 13px;
}
QMenuBar::item {
    background: transparent;
    color: #2C3140;
    padding: 5px 12px;
    border-radius: 6px;
}
QMenuBar::item:selected {
    background-color: #E7E9EE;
    color: #1C1E26;
}
QMenuBar::item:pressed {
    background-color: #FCE4E2;
    color: #C0271C;
}
)");
    auto* fileMenu = mb->addMenu("&File");

    auto* actNew = fileMenu->addAction("&New Presentation");
    actNew->setShortcut(QKeySequence::New);
    auto* actOpen = fileMenu->addAction("&Open…");
    actOpen->setShortcut(QKeySequence::Open);
    fileMenu->addSeparator();
    auto* actSave   = fileMenu->addAction("&Save");
    actSave->setShortcut(QKeySequence::Save);
    auto* actSaveAs = fileMenu->addAction("Save &As…");
    actSaveAs->setShortcut(QKeySequence::SaveAs);
    fileMenu->addSeparator();
    // Sprint 6: Export to PDF
    auto* actExportPptx = fileMenu->addAction("Save as &PowerPoint (*.pptx)…");
    actExportPptx->setToolTip("Export this presentation as a PowerPoint .pptx file");
    auto* actExportPdf = fileMenu->addAction("★ Export to PDF…");
    actExportPdf->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    actExportPdf->setToolTip("Export all slides to a PDF file");
    fileMenu->addSeparator();
    auto* actClose = fileMenu->addAction("&Close");
    actClose->setShortcut(QKeySequence::Close);

    // Slide Show is driven from the ribbon's "Slide Show" tab, so there's no
    // duplicate top-level menu. We still register F5 / Shift+F5 as window-level
    // shortcuts so the keyboard flow keeps working.
    auto* actPlayFromStart = new QAction(win);
    actPlayFromStart->setShortcut(Qt::Key_F5);
    win->addAction(actPlayFromStart);
    auto* actPlayFromCurrent = new QAction(win);
    actPlayFromCurrent->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F5));
    win->addAction(actPlayFromCurrent);

    auto* viewMenu      = mb->addMenu("&View");
    auto* actViewNormal = viewMenu->addAction("&Normal");
    auto* actViewOutline = viewMenu->addAction("&Outline");
    auto* actViewSorter = viewMenu->addAction("Slide &Sorter");
    viewMenu->addSeparator();
    auto* actNewSlide   = viewMenu->addAction("New Sli&de");
    actNewSlide->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    auto* actDupSlide   = viewMenu->addAction("Du&plicate Slide");

    auto* helpMenu      = mb->addMenu("&Help");
    auto* actShortcuts  = helpMenu->addAction("&Keyboard Shortcuts");
    helpMenu->addSeparator();
    auto* actAbout      = helpMenu->addAction("&About NativeOffice");

    // ── Wire actions ──────────────────────────────────────────────────────
    QObject::connect(actNew, &QAction::triggered, win, []() {
        createImpressWindow()->show();
    });
    QObject::connect(actOpen, &QAction::triggered, win, [win]() {
        const QString path = QFileDialog::getOpenFileName(
            win, "Open Presentation",
            QDir::homePath(),
            IMPRESS_OPEN_FILTER);
        if (path.isEmpty()) return;
        // If this window is still the untouched, untitled, single-slide blank
        // that opens with the app, load the file into it instead of spawning a
        // second window (which would leave the empty one lingering).
        auto* im = win->impress();
        const bool fresh = im->currentFilePath().isEmpty() && im->slideCount() <= 1;
        if (fresh) {
            if (im->loadFromPath(path))
                NativeOffice::RecentFilesManager::instance().addFile(path, "Impress");
            else
                QMessageBox::critical(win, "Open Failed", "Could not read:\n" + path);
        } else {
            createImpressWindow(path)->show();
        }
    });
    QObject::connect(actSave,      &QAction::triggered, win, [win]() { win->save();   });
    QObject::connect(actSaveAs,    &QAction::triggered, win, [win]() { win->saveAs(); });
    QObject::connect(actExportPdf, &QAction::triggered, win, [impress]() {
        impress->exportToPdf();
    });
    QObject::connect(actExportPptx, &QAction::triggered, win, [impress]() {
        impress->exportToPptx();
    });
    QObject::connect(actClose, &QAction::triggered, win, &QMainWindow::close);
    QObject::connect(actPlayFromStart, &QAction::triggered, win, [impress]() {
        impress->switchToSlide(0);
        impress->startSlideShow();
    });
    QObject::connect(actPlayFromCurrent, &QAction::triggered, win, [impress]() {
        impress->startSlideShow();
    });

    using NativeOffice::ImpressViewMode;
    QObject::connect(actViewNormal,  &QAction::triggered, win, [impress]() {
        impress->setViewMode(ImpressViewMode::Normal);
    });
    QObject::connect(actViewOutline, &QAction::triggered, win, [impress]() {
        impress->setViewMode(ImpressViewMode::Outline);
    });
    QObject::connect(actViewSorter,  &QAction::triggered, win, [impress]() {
        impress->setViewMode(ImpressViewMode::SlideSorter);
    });
    QObject::connect(actNewSlide, &QAction::triggered, win, [impress]() {
        impress->addNewSlide();
    });
    QObject::connect(actDupSlide, &QAction::triggered, win, [impress]() {
        impress->duplicateCurrentSlide();
    });

    QObject::connect(actShortcuts, &QAction::triggered, win, [win]() {
        QMessageBox::information(win, "Keyboard Shortcuts",
            "Slide Show\n"
            "  F5\t\tStart from beginning\n"
            "  Shift+F5\tStart from current slide\n"
            "  Esc\t\tExit slide show\n"
            "  →/Space\tNext slide\n"
            "  ←/Backspace\tPrevious slide\n"
            "  B\t\tBlack screen\n\n"
            "Editing\n"
            "  Ctrl+S\t\tSave\n"
            "  Ctrl+Shift+S\tSave As\n"
            "  Ctrl+M\t\tNew slide\n"
            "  Ctrl+Z / Ctrl+Y\tUndo / Redo\n"
            "  Ctrl+Shift+E\tExport to PDF\n"
            "  Delete\t\tDelete selected object");
    });
    QObject::connect(actAbout, &QAction::triggered, win, [win]() {
        QMessageBox::about(win, "About NativeOffice",
            "<h3>NativeOffice Impress</h3>"
            "<p>A high-performance, cross-platform presentation tool.</p>"
            "<p>Version 0.4.0</p>"
            "<p>NativeOffice is your go to OfficeSuite!</p>");
    });

    return win;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: open any document via smart content-based routing  (Sprint 7)
//
// Uses FileRouter::detectFileType() to read the file content first and
// dispatch to the correct module (Writer, Calc, Impress) regardless of
// file extension.
// ─────────────────────────────────────────────────────────────────────────────
static void openDocumentByPath(const QString& path) {
    using NativeOffice::DetectedFileType;

    const auto fileType = NativeOffice::FileRouter::detectFileType(path);

    switch (fileType) {
    case DetectedFileType::SpreadsheetData:
        createCalcWindow(path)->show();
        break;
    case DetectedFileType::PresentationData:
        createImpressWindow(path)->show();
        break;
    case DetectedFileType::WriterDocument:
    default:
        // createWriterWindow already calls addFile(path, "Writer") on success
        createWriterWindow(path)->show();
        break;
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
    app.setApplicationVersion("0.4.0");
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
        case DocumentType::Calc:
            createCalcWindow()->show();
            break;
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

    // ── Open any document passed on the command line (file association /
    //    double-click-to-open). Each existing path is routed by content type.
    const QStringList cliArgs = app.arguments();
    for (int i = 1; i < cliArgs.size(); ++i) {
        const QString path = cliArgs.at(i);
        if (QFileInfo::exists(path))
            openDocumentByPath(path);
    }

    return app.exec();
}

#include "main.moc"
