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
#include "startscreen/SplashScreen.h"
#include "tools/ImageResizer.h"
#include "auth/LoginGate.h"
#include "auth/InstanceGuard.h"
#include "core/theme/ThemeManager.h"
#include "core/application/AppController.h"
#include "core/application/RecentFilesManager.h"
#include "core/application/FileRouter.h"
#include "core/auth/AuthManager.h"

#include "WriterModule.h"
#include "CalcModule.h"
#include "SpreadsheetModel.h"
#include "ImpressModule.h"
#include "PdfModule.h"

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
#include <QMouseEvent>
#include <QTime>
#include <QDate>
#include <QSettings>
#include <QTabWidget>
#include <QTabBar>
#include <QToolButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFontDatabase>
#include <functional>
#include <thread>
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
// Writer open: accept Word, NativeOffice and HTML documents.
static const QString WRITER_OPEN_FILTER =
    "Documents (*.docx *.noff *.html);;Word Document (*.docx);;"
    "NativeOffice Document (*.noff);;HTML Document (*.html);;All Files (*)";
// Writer Save As: Word (.docx) is offered first so it is the default format.
static const QString WRITER_SAVE_FILTER =
    "Word Document (*.docx);;NativeOffice Document (*.noff);;HTML Document (*.html)";
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

// ─────────────────────────────────────────────────────────────────────────────
// EditorWindow — common base for the three document windows.
//
// Each document is a QMainWindow (with its own menu bar + central module
// widget). They live as pages inside the main shell's tab bar. requestClose()
// runs the unsaved-changes prompt and returns false if the user cancelled; it
// is shared by closeEvent() (window close) and the shell's tab-close handler.
// ─────────────────────────────────────────────────────────────────────────────
class EditorWindow : public QMainWindow {
    Q_OBJECT
public:
    using QMainWindow::QMainWindow;
    virtual bool requestClose() = 0;        // false ⇒ user cancelled, keep open
    // Used by the shell to build the tab label.
    virtual QString docKindName()   const = 0;  // "Document"/"Spreadsheet"/"Presentation"
    virtual QString currentDocPath() const = 0;  // empty while untitled
    virtual bool    docDirty()      const = 0;
};

// Forward declarations for free-function helpers
static class WriterWindow* createWriterWindow(const QString& filePath = {});
static class CalcWindow*   createCalcWindow(const QString& filePath = {});
static class ImpressWindow* createImpressWindow(const QString& filePath = {});
static class PdfWindow*    createPdfWindow(const QString& filePath = {});
static class ImageResizerWindow* createImageResizerWindow();

// Adds an editor window as a new tab in the main shell (defined after MainShell).
static void presentEditor(EditorWindow* win);

// ─────────────────────────────────────────────────────────────────────────────
// WriterWindow — document window with close-confirmation logic
// ─────────────────────────────────────────────────────────────────────────────
class WriterWindow : public EditorWindow {
    Q_OBJECT
public:
    explicit WriterWindow(QWidget* parent = nullptr)
        : EditorWindow(parent)
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
        QString selectedFilter;
        QString path = QFileDialog::getSaveFileName(
            this, "Save As…",
            m_writer->currentFilePath().isEmpty()
                ? QDir::homePath() + "/Untitled.docx"
                : m_writer->currentFilePath(),
            WRITER_SAVE_FILTER, &selectedFilter);

        if (path.isEmpty()) return false;
        // Append an extension matching the chosen filter if the user omitted one.
        if (QFileInfo(path).suffix().isEmpty()) {
            if      (selectedFilter.contains("noff")) path += ".noff";
            else if (selectedFilter.contains("html")) path += ".html";
            else                                      path += ".docx";
        }
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

public:
    QString docKindName()    const override { return QStringLiteral("Document"); }
    QString currentDocPath() const override { return m_writer ? m_writer->currentFilePath() : QString(); }
    bool    docDirty()       const override { return m_writer && m_writer->isDirty(); }

    bool requestClose() override {
        if (m_writer && m_writer->isDirty()) {
            const auto btn = QMessageBox::question(
                this, "Unsaved Changes",
                QString("\"%1\" has unsaved changes.\nDo you want to save before closing?")
                    .arg(m_writer->currentFilePath().isEmpty()
                             ? "Untitled Document"
                             : QFileInfo(m_writer->currentFilePath()).fileName()),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                QMessageBox::Save);

            if (btn == QMessageBox::Cancel)            return false;
            if (btn == QMessageBox::Save && !save())   return false;
        }
        return true;
    }

protected:
    void closeEvent(QCloseEvent* e) override {
        if (requestClose()) e->accept();
        else                e->ignore();
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
class CalcWindow : public EditorWindow {
    Q_OBJECT
public:
    explicit CalcWindow(QWidget* parent = nullptr)
        : EditorWindow(parent)
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

public:
    QString docKindName()    const override { return QStringLiteral("Spreadsheet"); }
    QString currentDocPath() const override { return m_calc ? m_calc->currentFilePath() : QString(); }
    bool    docDirty()       const override { return m_calc && m_calc->isDirty(); }

    bool requestClose() override {
        if (m_calc && m_calc->isDirty()) {
            const auto btn = QMessageBox::question(
                this, "Unsaved Changes",
                QString("\"%1\" has unsaved changes.\nDo you want to save before closing?")
                    .arg(m_calc->currentFilePath().isEmpty()
                             ? "Untitled Spreadsheet"
                             : QFileInfo(m_calc->currentFilePath()).fileName()),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                QMessageBox::Save);

            if (btn == QMessageBox::Cancel)            return false;
            if (btn == QMessageBox::Save && !save())   return false;
        }
        return true;
    }

protected:
    void closeEvent(QCloseEvent* e) override {
        if (requestClose()) e->accept();
        else                e->ignore();
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
class ImpressWindow : public EditorWindow {
    Q_OBJECT
public:
    explicit ImpressWindow(QWidget* parent = nullptr)
        : EditorWindow(parent)
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

public:
    QString docKindName()    const override { return QStringLiteral("Presentation"); }
    QString currentDocPath() const override { return m_impress ? m_impress->currentFilePath() : QString(); }
    bool    docDirty()       const override { return m_impress && m_impress->isDirty(); }

    bool requestClose() override {
        if (m_impress && m_impress->isDirty()) {
            const auto btn = QMessageBox::question(
                this, "Unsaved Changes",
                QString("\"%1\" has unsaved changes.\nDo you want to save before closing?")
                    .arg(m_impress->currentFilePath().isEmpty()
                             ? "Untitled Presentation"
                             : QFileInfo(m_impress->currentFilePath()).fileName()),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                QMessageBox::Save);

            if (btn == QMessageBox::Cancel)            return false;
            if (btn == QMessageBox::Save && !save())   return false;
        }
        return true;
    }

protected:
    void closeEvent(QCloseEvent* e) override {
        if (requestClose()) e->accept();
        else                e->ignore();
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
// PdfWindow — the PDF editor tab (ribbon-tabbed viewer/editor). A real
// document window since the PDF module became an editor: edits accumulate in
// its EditSession and requestClose() prompts to save, same as the others.
// ─────────────────────────────────────────────────────────────────────────────
class PdfWindow : public EditorWindow {
    Q_OBJECT
public:
    explicit PdfWindow(QWidget* parent = nullptr)
        : EditorWindow(parent)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setMinimumSize(960, 640);
        resize(1200, 800);
    }

    void setPdf(NativeOffice::PdfModule* pdf) {
        m_pdf = pdf;
        setCentralWidget(pdf);
        updateTitle();
        connect(pdf, &NativeOffice::PdfModule::documentModified,
                this, &PdfWindow::updateTitle);
        connect(pdf, &NativeOffice::PdfModule::filePathChanged, this,
                [this](const QString& path) {
                    updateTitle();
                    if (!path.isEmpty())
                        NativeOffice::RecentFilesManager::instance().addFile(path, "PDF");
                });
    }

    NativeOffice::PdfModule* pdf() const { return m_pdf; }

    bool save() {
        if (!m_pdf) return true;
        if (m_pdf->currentFilePath().isEmpty()) return false;   // nothing open
        return m_pdf->saveToPath(m_pdf->currentFilePath());
    }

public slots:
    void updateTitle() { setWindowTitle(m_pdf ? m_pdf->titleString() : "NativeOffice PDF"); }

public:
    QString docKindName()    const override { return QStringLiteral("PDF"); }
    QString currentDocPath() const override { return m_pdf ? m_pdf->currentFilePath() : QString(); }
    bool    docDirty()       const override { return m_pdf && m_pdf->isDirty(); }

    bool requestClose() override {
        if (m_pdf && m_pdf->isDirty()) {
            const auto btn = QMessageBox::question(
                this, "Unsaved Changes",
                QString("\"%1\" has unsaved changes.\nDo you want to save before closing?")
                    .arg(QFileInfo(m_pdf->currentFilePath()).fileName()),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                QMessageBox::Save);
            if (btn == QMessageBox::Cancel)          return false;
            if (btn == QMessageBox::Save && !save()) return false;
        }
        return true;
    }

protected:
    void closeEvent(QCloseEvent* e) override {
        if (requestClose()) e->accept();
        else                e->ignore();
    }

private:
    NativeOffice::PdfModule* m_pdf { nullptr };
};

// ─────────────────────────────────────────────────────────────────────────────
// ImageResizerWindow — the Image Resizer tool tab (Home → Tools). Stateless
// from the shell's point of view: nothing to save, closing never prompts.
// ─────────────────────────────────────────────────────────────────────────────
class ImageResizerWindow : public EditorWindow {
    Q_OBJECT
public:
    explicit ImageResizerWindow(QWidget* parent = nullptr)
        : EditorWindow(parent)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setMinimumSize(960, 640);
        resize(1200, 800);
        setWindowTitle("Image Resizer");
        setCentralWidget(new NativeOffice::ImageResizerWidget(this));
    }

    bool    requestClose()          override { return true; }
    QString docKindName()     const override { return QStringLiteral("Image Resizer"); }
    QString currentDocPath()  const override { return {}; }
    bool    docDirty()        const override { return false; }
};

static ImageResizerWindow* createImageResizerWindow() {
    auto* win = new ImageResizerWindow;
    const QScreen* screen = QApplication::primaryScreen();
    win->move(screen->availableGeometry().center() - win->rect().center());
    return win;
}

// ─────────────────────────────────────────────────────────────────────────────
// ShellTabBar — custom-painted tab bar: a white strip, the first ("Home") tab
// violet, every other tab white. (Native QTabBar ignores per-tab background, so
// we paint it ourselves for an exact, style-independent look.)
// ─────────────────────────────────────────────────────────────────────────────
class ShellTabBar : public QTabBar {
    Q_OBJECT
public:
    explicit ShellTabBar(QWidget* parent = nullptr) : QTabBar(parent) {
        setExpanding(false);
        setDrawBase(false);
        // Close buttons are custom QToolButtons (added per tab by MainShell) so
        // they match the custom-painted tabs; the native style-drawn "x" looked
        // broken on top of our painting.
        setTabsClosable(false);
        setElideMode(Qt::ElideRight);
        setMovable(false);
        setMouseTracking(true);
        // Overflow scroll arrows: keep them on the dark strip.
        setStyleSheet("QTabBar QToolButton { background:#0D1117; border:none; }");
    }

protected:
    void mouseMoveEvent(QMouseEvent* e) override {
        const int h = tabAt(e->pos());
        if (h != m_hover) { m_hover = h; update(); }
        QTabBar::mouseMoveEvent(e);
    }
    void leaveEvent(QEvent* e) override {
        if (m_hover != -1) { m_hover = -1; update(); }
        QTabBar::leaveEvent(e);
    }
    QSize tabSizeHint(int index) const override {
        QSize s = QTabBar::tabSizeHint(index);
        s.setHeight(qMax(s.height(), 36));
        s.setWidth(qMax(s.width() + 24, 110));   // padding + room for close button
        return s;
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#0D1117"));      // dark strip, matches Home's background

        for (int i = 0; i < count(); ++i) {
            const QRect r   = tabRect(i);
            const bool home = (i == 0);
            const bool sel  = (i == currentIndex());

            const bool hov = (i == m_hover);
            const QColor bg = home ? QColor(sel ? "#6D28D9" : hov ? "#8B5CF6" : "#7C3AED")
                                   : QColor(sel ? "#12161F" : hov ? "#17233B" : "#0D1117");
            const QColor fg = home ? QColor("#FFFFFF")
                                   : QColor(sel ? "#FFFFFF" : "#AEB6C6");
            p.fillRect(r, bg);

            if (!home) {
                p.setPen(QColor("#1B212C"));
                p.drawLine(r.topRight(), r.bottomRight());
                if (sel)
                    p.fillRect(QRect(r.left(), r.bottom() - 2, r.width(), 2),
                               QColor("#7C3AED"));
            }

            QFont f = font();
            f.setBold(home || sel);
            p.setFont(f);
            p.setPen(fg);
            // Leave room on the right for the close button on non-Home tabs.
            const QRect tr = r.adjusted(14, 0, home ? -14 : -30, 0);
            p.drawText(tr, Qt::AlignVCenter | Qt::AlignLeft,
                       fontMetrics().elidedText(tabText(i), Qt::ElideRight, tr.width()));
        }

        // Bottom hairline so the dark strip reads as a proper bar.
        p.setPen(QColor("#1B212C"));
        p.drawLine(0, height() - 1, width(), height() - 1);
    }

private:
    int m_hover { -1 };
};

// ─────────────────────────────────────────────────────────────────────────────
// MainShell — single top-level window hosting a WPS-style tab bar.
//
// Built from a QTabBar + QStackedWidget (rather than QTabWidget) on a white top
// strip, so the tab look is fully under our control:
//   • Tab 0 is the pinned, violet "Home" page (the StartScreen); other tabs are
//     white. The Home tab cannot be closed.
//   • Each document opens as its own tab named per type ("Document 1",
//     "Spreadsheet 1", "Presentation 1", …), switching to the file name on save.
//   • The "+" button opens a fresh Home page in a new tab; choosing a format
//     there opens the document as a new tab.
//   • Closing a document tab runs its unsaved-changes prompt first.
// ─────────────────────────────────────────────────────────────────────────────
class MainShell : public QMainWindow {
    Q_OBJECT
public:
    explicit MainShell(QWidget* parent = nullptr)
        : QMainWindow(parent)
    {
        setWindowTitle("NativeOffice");
        setMinimumSize(1024, 680);

        auto* central = new QWidget(this);
        central->setObjectName("shellCentral");
        // Match the home page's dark background — only the tab STRIP is white.
        // (Painting the whole central white bled through behind the home page.)
        central->setStyleSheet("#shellCentral { background:#0D1117; }");
        auto* v = new QVBoxLayout(central);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);

        // Dark top strip: tab bar on the left, "+" right after the last tab.
        auto* topBar = new QWidget(central);
        topBar->setObjectName("shellTopBar");
        topBar->setStyleSheet("#shellTopBar { background:#0D1117; }");
        auto* hb = new QHBoxLayout(topBar);
        hb->setContentsMargins(0, 0, 0, 0);
        hb->setSpacing(0);

        m_bar = new ShellTabBar(topBar);

        auto* plus = new QToolButton(topBar);
        plus->setText("+");
        plus->setAutoRaise(true);
        plus->setToolTip("New tab — open Home");
        plus->setCursor(Qt::PointingHandCursor);
        plus->setFixedHeight(36);
        plus->setStyleSheet(
            "QToolButton { font-size:20px; font-weight:600; color:#A78BFA;"
            "  background:#0D1117; padding:0 16px; border:none; }"
            "QToolButton:hover { color:#C4B5FD; background:#17233B; }");
        connect(plus, &QToolButton::clicked, this, [this]() { openHomeTab(); });

        hb->addWidget(m_bar, 0);
        hb->addWidget(plus, 0);
        hb->addStretch(1);             // white fill to the right of the "+"

        m_stack = new QStackedWidget(central);

        v->addWidget(topBar, 0);
        v->addWidget(m_stack, 1);
        setCentralWidget(central);

        connect(m_bar, &QTabBar::currentChanged,
                m_stack, &QStackedWidget::setCurrentIndex);
        connect(m_bar, &QTabBar::tabCloseRequested,
                this, &MainShell::handleTabClose);
    }

    // Factory used to spin up additional Home pages (wired to the controller).
    void setHomeFactory(std::function<QWidget*()> f) { m_homeFactory = std::move(f); }

    // Install the Home page (StartScreen) as the pinned first tab.
    void setHomePage(QWidget* home) {
        const int idx = addPage(home, "Home");
        // Home cannot be closed: drop its close button.
        m_bar->setTabButton(idx, QTabBar::RightSide, nullptr);
        m_bar->setTabButton(idx, QTabBar::LeftSide,  nullptr);
        m_bar->setCurrentIndex(idx);
    }

    // Open a fresh Home page in a new tab (the "+" action).
    void openHomeTab() {
        if (!m_homeFactory) { m_bar->setCurrentIndex(0); return; }
        addPage(m_homeFactory(), "Home");
    }

    // Add a document window as a new tab beside the others and focus it.
    void addEditorTab(EditorWindow* win) {
        // The shell owns lifetime now; cancel the per-window self-delete.
        win->setAttribute(Qt::WA_DeleteOnClose, false);
        win->setWindowFlags(Qt::Widget);   // behave as a plain child widget

        assignAutoName(win);
        addPage(win, labelFor(win));
        watchTitle(win);
    }

    // Convert an ephemeral "+"-tab's Home page into a document tab IN PLACE
    // (browser-style: a new-tab page navigating to a URL becomes that page,
    // rather than opening a second tab). The pinned first Home tab never
    // routes through here — main() only wires this into ephemeral Home pages
    // created via the "+" button, so it keeps opening new tabs as before.
    void presentInHomeTab(QWidget* homeWidget, EditorWindow* win) {
        const int idx = m_stack->indexOf(homeWidget);
        if (idx <= 0) { addEditorTab(win); return; }   // not an ephemeral tab: fall back

        win->setAttribute(Qt::WA_DeleteOnClose, false);
        win->setWindowFlags(Qt::Widget);
        assignAutoName(win);

        m_stack->removeWidget(homeWidget);
        homeWidget->deleteLater();
        m_stack->insertWidget(idx, win);
        m_bar->setTabText(idx, labelFor(win));
        m_bar->setCurrentIndex(idx);
        m_stack->setCurrentIndex(idx);
        watchTitle(win);
    }

protected:
    void closeEvent(QCloseEvent* e) override {
        // Prompt for every dirty document before the whole app exits.
        for (int i = m_stack->count() - 1; i >= 0; --i) {
            auto* win = qobject_cast<EditorWindow*>(m_stack->widget(i));
            if (win && !win->requestClose()) { e->ignore(); return; }
        }
        e->accept();
        // Quit-on-last-window-closed is off (see main()); closing the shell
        // is the explicit "exit the app" action.
        qApp->quit();
    }

private:
    // Append a page: add the tab and the matching stacked widget (1:1 indices),
    // then focus it.
    int addPage(QWidget* page, const QString& label) {
        const int idx = m_bar->addTab(label);
        m_stack->insertWidget(idx, page);

        // Custom close button matching the custom-painted tabs (the native
        // style-drawn one clashed visually). Resolved to an index at click
        // time, so tab reindexing after closes can't hit the wrong tab.
        auto* close = new QToolButton(m_bar);
        close->setText(QString::fromUtf8("✕"));
        close->setToolTip("Close tab");
        close->setCursor(Qt::PointingHandCursor);
        close->setFixedSize(18, 18);
        close->setStyleSheet(
            "QToolButton { border:none; border-radius:9px; background:transparent;"
            "  color:#9097A6; font-size:10px; }"
            "QToolButton:hover { background:#FCE4E2; color:#C0271C; }");
        connect(close, &QToolButton::clicked, this, [this, close] {
            for (int i = 0; i < m_bar->count(); ++i)
                if (m_bar->tabButton(i, QTabBar::RightSide) == close) {
                    handleTabClose(i);
                    return;
                }
        });
        m_bar->setTabButton(idx, QTabBar::RightSide, close);

        m_bar->setCurrentIndex(idx);
        m_stack->setCurrentIndex(idx);
        return idx;
    }

    // Assign a per-type sequential name ("Document 1", "Spreadsheet 1", "PDF 1", …).
    void assignAutoName(EditorWindow* win) {
        const QString kind = win->docKindName();
        int n = 0;
        if      (kind == QLatin1String("Spreadsheet"))  n = ++m_nSheet;
        else if (kind == QLatin1String("Presentation")) n = ++m_nPres;
        else if (kind == QLatin1String("PDF"))          n = ++m_nPdf;
        else                                             n = ++m_nDoc;
        win->setProperty("autoName", QStringLiteral("%1 %2").arg(kind).arg(n));
    }

    // Keep the tab label in sync with the document's save/dirty state.
    void watchTitle(EditorWindow* win) {
        connect(win, &QWidget::windowTitleChanged, this, [this, win](const QString&) {
            const int i = m_stack->indexOf(win);
            if (i >= 0) m_bar->setTabText(i, labelFor(win));
        });
    }

    // Tab label: the auto-assigned name while untitled ("Document 1"), or the
    // file name once saved, prefixed "* " when a saved file has unsaved edits.
    QString labelFor(EditorWindow* win) const {
        const QString path = win->currentDocPath();
        if (path.isEmpty()) {
            QString base = win->property("autoName").toString();
            if (base.isEmpty()) base = win->docKindName();
            return elide(base);
        }
        return (win->docDirty() ? QStringLiteral("* ") : QString())
               + elide(QFileInfo(path).fileName());
    }

    static QString elide(QString s) {
        if (s.size() > 26) s = s.left(25) + QChar(0x2026);
        return s;
    }

    void handleTabClose(int idx) {
        if (idx == 0) return;                      // Home is pinned
        QWidget* w = m_stack->widget(idx);
        if (!w) return;
        auto* win = qobject_cast<EditorWindow*>(w);
        if (win && !win->requestClose()) return;   // user cancelled
        m_bar->removeTab(idx);
        m_stack->removeWidget(w);
        w->deleteLater();
    }

    ShellTabBar*               m_bar   { nullptr };
    QStackedWidget*            m_stack { nullptr };
    std::function<QWidget*()>  m_homeFactory;
    int m_nDoc   { 0 };
    int m_nSheet { 0 };
    int m_nPres  { 0 };
    int m_nPdf   { 0 };
};

// The single shell instance; document windows route their tabs through it.
static MainShell* g_shell = nullptr;

static void presentEditor(EditorWindow* win) {
    if (g_shell) g_shell->addEditorTab(win);
    else         win->show();   // fallback (should not happen in normal flow)
}

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
    // Light, clean menu bar (no black hero strip) to sit above the banner,
    // matching Calc and Impress.
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
        presentEditor(createWriterWindow());
    });

    QObject::connect(actOpen, &QAction::triggered, win, [win]() {
        const QString path = QFileDialog::getOpenFileName(
            win, "Open Document",
            QDir::homePath(),
            WRITER_OPEN_FILTER);
        if (!path.isEmpty())
            presentEditor(createWriterWindow(path));
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
        presentEditor(createCalcWindow());
    });
    QObject::connect(actOpen, &QAction::triggered, win, [win]() {
        const QString path = QFileDialog::getOpenFileName(
            win, "Open Spreadsheet",
            QDir::homePath(),
            CALC_FILTER);
        if (!path.isEmpty())
            presentEditor(createCalcWindow(path));
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
        presentEditor(createImpressWindow());
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
            presentEditor(createImpressWindow(path));
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
// Helper: build the PDF tool-hub window  (no menu bar — it's a utility tab,
// not a document editor; File/Edit actions don't apply to it)
// ─────────────────────────────────────────────────────────────────────────────
static PdfWindow* createPdfWindow(const QString& filePath) {
    auto* win = new PdfWindow;
    auto* pdf = new NativeOffice::PdfModule;
    win->setPdf(pdf);

    const QScreen* screen = QApplication::primaryScreen();
    win->move(screen->availableGeometry().center() - win->rect().center());

    if (!filePath.isEmpty()) pdf->setInitialFile(filePath);

    return win;
}

// ─────────────────────────────────────────────────────────────────────────────
// Home-screen templates — real starter content per template name.
// ─────────────────────────────────────────────────────────────────────────────
static QString writerTemplateHtml(const QString& name) {
    const QString base = "font-family:'Segoe UI'; color:#1C1E26;";
    auto h1 = [&](const QString& t, const QString& col = "#1F3864") {
        return QString("<h1 style=\"%1 color:%2;\">%3</h1>").arg(base, col, t); };
    auto p = [&](const QString& t) {
        return QString("<p style=\"%1\">%2</p>").arg(base, t); };
    auto hr = []{ return QStringLiteral("<hr/>"); };

    if (name == "Professional Resume" || name == "Modern Resume") {
        const QString accent = name.startsWith("Modern") ? "#0E7C5A" : "#1F3864";
        return QString(R"(<h1 style="%1 color:%2; margin-bottom:2px;">Your Name</h1>
<p style="%1 color:#5A6071;">City, Country · +00 00000 00000 · you@email.com · linkedin.com/in/you</p><hr/>
<h2 style="%1 color:%2;">Summary</h2>
<p style="%1">Results-driven professional with X years of experience in ____. Known for ____ and ____.</p>
<h2 style="%1 color:%2;">Experience</h2>
<p style="%1"><b>Job Title — Company</b> &nbsp;·&nbsp; <i>20XX – Present</i></p>
<ul><li style="%1">Achievement with a measurable result (grew X by Y%)</li>
<li style="%1">Responsibility that shows scope and ownership</li>
<li style="%1">Tool, process or initiative you led</li></ul>
<p style="%1"><b>Previous Title — Company</b> &nbsp;·&nbsp; <i>20XX – 20XX</i></p>
<ul><li style="%1">Key contribution</li><li style="%1">Key contribution</li></ul>
<h2 style="%1 color:%2;">Education</h2>
<p style="%1"><b>Degree, Institution</b> — 20XX</p>
<h2 style="%1 color:%2;">Skills</h2>
<p style="%1">Skill one · Skill two · Skill three · Skill four · Skill five</p>)")
            .arg(base, accent);
    }
    if (name == "Cover Letter")
        return p("Your Name<br/>Your Address<br/>you@email.com") + p(QDate::currentDate().toString("MMMM d, yyyy"))
             + p("Hiring Manager<br/>Company Name<br/>Company Address") + p("<b>Re: Application for [Position]</b>")
             + p("Dear Hiring Manager,") + p("Opening paragraph — name the role, where you found it, and one line on why you're a strong fit.")
             + p("Body paragraph — connect two or three of your achievements directly to the role's requirements. Use numbers where you can.")
             + p("Closing paragraph — restate your enthusiasm, mention availability, and thank them for their time.")
             + p("Sincerely,<br/><b>Your Name</b>");
    if (name == "Business Letter")
        return p("<b>Your Company</b><br/>Street Address<br/>City, State ZIP") + p(QDate::currentDate().toString("MMMM d, yyyy"))
             + p("Recipient Name<br/>Title, Company<br/>Address") + p("Dear Mr./Ms. ____,")
             + p("First paragraph: state the purpose of this letter in one or two sentences.")
             + p("Second paragraph: give the supporting details — context, facts, amounts, dates.")
             + p("Final paragraph: state the action you request and by when, and how you can be reached.")
             + p("Yours faithfully,<br/><br/><b>Your Name</b><br/>Your Title");
    if (name == "Project Report")
        return h1("Project Report") + p("<i>Author · Date · Version 1.0</i>") + hr()
             + QString("<h2 style=\"%1 color:#1F3864;\">1. Executive Summary</h2>").arg(base)
             + p("Two or three sentences: what the project is, its current status, and the headline result.")
             + QString("<h2 style=\"%1 color:#1F3864;\">2. Objectives</h2>").arg(base)
             + QString("<ul><li style=\"%1\">Objective one</li><li style=\"%1\">Objective two</li></ul>").arg(base)
             + QString("<h2 style=\"%1 color:#1F3864;\">3. Progress</h2>").arg(base)
             + p("What was completed this period, what is in flight, and what's next.")
             + QString("<h2 style=\"%1 color:#1F3864;\">4. Risks & Issues</h2>").arg(base)
             + p("Top risks with owner and mitigation.")
             + QString("<h2 style=\"%1 color:#1F3864;\">5. Conclusion</h2>").arg(base)
             + p("Overall assessment and recommendation.");
    if (name == "Meeting Notes")
        return h1("Meeting Notes") + p(QString("<b>Date:</b> %1 &nbsp;&nbsp; <b>Time:</b> ____ &nbsp;&nbsp; <b>Location:</b> ____")
                    .arg(QDate::currentDate().toString("MMMM d, yyyy")))
             + p("<b>Attendees:</b> ____") + hr()
             + QString("<h2 style=\"%1 color:#1F3864;\">Agenda</h2><ol><li style=\"%1\">Topic one</li><li style=\"%1\">Topic two</li></ol>").arg(base)
             + QString("<h2 style=\"%1 color:#1F3864;\">Discussion</h2>").arg(base) + p("Notes…")
             + QString("<h2 style=\"%1 color:#1F3864;\">Decisions</h2><ul><li style=\"%1\">Decision</li></ul>").arg(base)
             + QString("<h2 style=\"%1 color:#1F3864;\">Action Items</h2><ul><li style=\"%1\">☐ Action — owner — due date</li><li style=\"%1\">☐ Action — owner — due date</li></ul>").arg(base);
    if (name == "Newsletter")
        return QString("<h1 style=\"%1 color:#7C3AED; text-align:center;\">THE MONTHLY BULLETIN</h1>").arg(base)
             + QString("<p style=\"%1 text-align:center; color:#5A6071;\">Issue #1 · %2</p>").arg(base, QDate::currentDate().toString("MMMM yyyy")) + hr()
             + QString("<h2 style=\"%1 color:#7C3AED;\">Top Story</h2>").arg(base)
             + p("Lead article text goes here. Hook the reader in the first sentence.")
             + QString("<h2 style=\"%1 color:#7C3AED;\">In Brief</h2><ul><li style=\"%1\">Short update one</li><li style=\"%1\">Short update two</li><li style=\"%1\">Short update three</li></ul>").arg(base)
             + QString("<h2 style=\"%1 color:#7C3AED;\">Upcoming</h2>").arg(base) + p("Dates and events to know about.");
    if (name == "Invoice Letter")
        return h1("INVOICE", "#B45309") + p("<b>Invoice #:</b> 0001 &nbsp;&nbsp; <b>Date:</b> " + QDate::currentDate().toString("MMM d, yyyy"))
             + p("<b>From:</b> Your Company · Address · Tax ID") + p("<b>Bill To:</b> Client Name · Address") + hr()
             + QString(R"(<table border="1" cellpadding="6" width="100%" style="%1">
<tr><th>Description</th><th>Qty</th><th>Rate</th><th>Amount</th></tr>
<tr><td>Service or product</td><td>1</td><td>0.00</td><td>0.00</td></tr>
<tr><td>Service or product</td><td>1</td><td>0.00</td><td>0.00</td></tr>
<tr><td colspan="3" align="right"><b>Total</b></td><td><b>0.00</b></td></tr></table>)").arg(base)
             + p("<i>Payment due within 30 days. Bank details: ____</i>");
    if (name == "To-Do List")
        return h1("To-Do List", "#0E7C5A") + p("<i>" + QDate::currentDate().toString("dddd, MMMM d") + "</i>") + hr()
             + QString("<h2 style=\"%1 color:#0E7C5A;\">Today</h2><ul><li style=\"%1\">☐ Most important task</li><li style=\"%1\">☐ Second task</li><li style=\"%1\">☐ Third task</li></ul>").arg(base)
             + QString("<h2 style=\"%1 color:#0E7C5A;\">This Week</h2><ul><li style=\"%1\">☐ Task</li><li style=\"%1\">☐ Task</li></ul>").arg(base)
             + QString("<h2 style=\"%1 color:#0E7C5A;\">Someday</h2><ul><li style=\"%1\">☐ Idea</li></ul>").arg(base);
    if (name == "Academic Essay")
        return QString("<p style=\"%1\">Student Name<br/>Instructor Name<br/>Course<br/>%2</p>").arg(base, QDate::currentDate().toString("d MMMM yyyy"))
             + QString("<h1 style=\"%1 text-align:center;\">Essay Title: Subtitle if Needed</h1>").arg(base)
             + p("&nbsp;&nbsp;&nbsp;&nbsp;Introduction — open with context, narrow to your thesis. End the paragraph with a clear thesis statement.")
             + p("&nbsp;&nbsp;&nbsp;&nbsp;Body paragraph one — topic sentence, evidence, analysis, transition.")
             + p("&nbsp;&nbsp;&nbsp;&nbsp;Body paragraph two — topic sentence, evidence, analysis, transition.")
             + p("&nbsp;&nbsp;&nbsp;&nbsp;Conclusion — restate the thesis in new words and state the wider significance.")
             + QString("<h2 style=\"%1 text-align:center;\">Works Cited</h2>").arg(base)
             + p("Author. <i>Title</i>. Publisher, Year.");
    if (name == "Press Release")
        return p("<b>FOR IMMEDIATE RELEASE</b>") + h1("Headline That States the News")
             + p("<i>Subheadline with one supporting detail</i>")
             + p(QString("<b>CITY, %1</b> — Opening paragraph: who, what, when, where, why — the entire story in two sentences.")
                   .arg(QDate::currentDate().toString("MMMM d, yyyy")))
             + p("Second paragraph: key details and context.")
             + p("“A quotation from a named spokesperson that adds a human voice,” said Name, Title at Company.")
             + p("Closing paragraph: what happens next and where to learn more.") + hr()
             + p("<b>Media Contact:</b> Name · email · phone");
    return h1("New Document") + p("Start typing…");
}

static void applyCalcTemplate(NativeOffice::CalcModule* calc, const QString& name) {
    if (!calc || !calc->model()) return;
    QList<QStringList> rows;
    if (name == "Monthly Budget")
        rows = { {"Monthly Budget"}, {},
                 {"Category","Budgeted","Actual","Difference"},
                 {"Housing / Rent","1200","","=B4-C4"},
                 {"Groceries","400","","=B5-C5"},
                 {"Transport","150","","=B6-C6"},
                 {"Utilities","180","","=B7-C7"},
                 {"Entertainment","120","","=B8-C8"},
                 {"Savings","300","","=B9-C9"},
                 {"Other","100","","=B10-C10"}, {},
                 {"Total","=SUM(B4:B10)","=SUM(C4:C10)","=B12-C12"} };
    else if (name == "Invoice")
        rows = { {"INVOICE"}, {"Invoice #","0001","","Date",""}, {},
                 {"From:","Your Company"}, {"Bill To:","Client Name"}, {},
                 {"Description","Qty","Unit Price","Amount"},
                 {"Item or service","1","0","=B8*C8"},
                 {"Item or service","1","0","=B9*C9"},
                 {"Item or service","1","0","=B10*C10"}, {},
                 {"","","Subtotal","=SUM(D8:D10)"},
                 {"","","Tax (10%)","=D12*0.1"},
                 {"","","TOTAL","=D12+D13"} };
    else if (name == "Expense Tracker")
        rows = { {"Expense Tracker"}, {},
                 {"Date","Description","Category","Amount"},
                 {"","","Food",""}, {"","","Travel",""}, {"","","Bills",""},
                 {"","","Shopping",""}, {"","","Other",""}, {},
                 {"","","Total","=SUM(D4:D8)"} };
    else if (name == "Sales Dashboard")
        rows = { {"Sales Dashboard"}, {},
                 {"Month","Revenue","Units","Avg Sale"},
                 {"January","10000","120","=B4/C4"},
                 {"February","12000","135","=B5/C5"},
                 {"March","14500","150","=B6/C6"},
                 {"April","13200","140","=B7/C7"},
                 {"May","16800","170","=B8/C8"},
                 {"June","18100","181","=B9/C9"}, {},
                 {"Total","=SUM(B4:B9)","=SUM(C4:C9)","=B11/C11"},
                 {"Best month","=MAX(B4:B9)"},
                 {"Average","=AVERAGE(B4:B9)"} };
    else if (name == "Inventory List")
        rows = { {"Inventory"}, {},
                 {"Item","SKU","Qty","Unit Cost","Value","Reorder at"},
                 {"Item A","SKU-001","25","4.50","=C4*D4","10"},
                 {"Item B","SKU-002","8","12.00","=C5*D5","5"},
                 {"Item C","SKU-003","60","1.75","=C6*D6","20"}, {},
                 {"","","","Total value","=SUM(E4:E6)"} };
    else if (name == "Loan Calculator")
        rows = { {"Loan Calculator (simple interest)"}, {},
                 {"Loan amount","10000"},
                 {"Annual rate (%)","8"},
                 {"Term (years)","3"}, {},
                 {"Total interest","=B3*B4/100*B5"},
                 {"Total repayable","=B3+B7"},
                 {"Monthly payment","=B8/(B5*12)"} };
    else if (name == "Timesheet")
        rows = { {"Weekly Timesheet"}, {"Name:","","Week of:",""}, {},
                 {"Day","Start","End","Break (h)","Hours"},
                 {"Monday","9:00","17:00","1",""},
                 {"Tuesday","","","",""}, {"Wednesday","","","",""},
                 {"Thursday","","","",""}, {"Friday","","","",""}, {},
                 {"","","","Total","=SUM(E5:E9)"} };
    else if (name == "Grade Book")
        rows = { {"Grade Book"}, {},
                 {"Student","Test 1","Test 2","Test 3","Average"},
                 {"Student A","85","92","78","=AVERAGE(B4:D4)"},
                 {"Student B","74","81","90","=AVERAGE(B5:D5)"},
                 {"Student C","95","88","93","=AVERAGE(B6:D6)"}, {},
                 {"Class average","=AVERAGE(B4:B6)","=AVERAGE(C4:C6)","=AVERAGE(D4:D6)","=AVERAGE(E4:E6)"} };
    else if (name == "Habit Tracker")
        rows = { {"Habit Tracker"}, {},
                 {"Habit","Mon","Tue","Wed","Thu","Fri","Sat","Sun"},
                 {"Exercise"}, {"Read 20 min"}, {"No junk food"}, {"Sleep by 11"}, {},
                 {"Mark each day with a 1 — weekly total:","=SUM(B4:H4)"} };
    else if (name == "Attendance Sheet")
        rows = { {"Attendance"}, {},
                 {"Name","Day 1","Day 2","Day 3","Day 4","Day 5","Present"},
                 {"Person A","1","1","","1","1","=SUM(B4:F4)"},
                 {"Person B","1","","1","1","","=SUM(B5:F5)"},
                 {"Person C","","1","1","1","1","=SUM(B6:F6)"}, {},
                 {"Daily total","=SUM(B4:B6)","=SUM(C4:C6)","=SUM(D4:D6)","=SUM(E4:E6)","=SUM(F4:F6)"} };
    else if (name == "Savings Goal")
        rows = { {"Savings Goal"}, {},
                 {"Goal amount","5000"},
                 {"Saved so far","1250"},
                 {"Remaining","=B3-B4"},
                 {"Progress (%)","=B4/B3*100"}, {},
                 {"Monthly deposit","250"},
                 {"Months to goal","=B5/B8"} };
    else return;

    for (int r = 0; r < rows.size(); ++r)
        for (int c = 0; c < rows[r].size(); ++c)
            if (!rows[r][c].isEmpty())
                calc->model()->setCellContent(c, r, rows[r][c], QStringLiteral("Apply Template"));
    calc->markClean();
}

// Build (but do not present) a document window pre-filled from a Home-screen
// template. Separated from presentation so callers can either open it as a
// new tab (presentEditor) or drop it into an ephemeral Home tab in place
// (MainShell::presentInHomeTab).
static EditorWindow* createWindowForTemplate(NativeOffice::DocumentType type, const QString& name) {
    using NativeOffice::DocumentType;
    switch (type) {
    case DocumentType::Writer: {
        auto* w = createWriterWindow();
        w->writer()->setContent(writerTemplateHtml(name));
        return w;
    }
    case DocumentType::Calc: {
        auto* w = createCalcWindow();
        applyCalcTemplate(w->calc(), name);
        return w;
    }
    case DocumentType::Impress: {
        auto* w = createImpressWindow();
        w->impress()->applyDeckTemplate(name);
        return w;
    }
    case DocumentType::Pdf:
        return createPdfWindow();
    }
    return createWriterWindow();
}

// Open a document pre-filled from a Home-screen template, as a new tab.
static void openFromTemplate(NativeOffice::DocumentType type, const QString& name) {
    presentEditor(createWindowForTemplate(type, name));
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: build (but do not present) the right editor window for a path via
// smart content-based routing (Sprint 7). A .pdf extension routes straight to
// the PDF tool hub (pre-loaded with that file); everything else is classified
// by FileRouter::detectFileType(), which reads the content itself.
// ─────────────────────────────────────────────────────────────────────────────
static EditorWindow* createWindowForPath(const QString& path) {
    using NativeOffice::DetectedFileType;

    if (path.endsWith(".pdf", Qt::CaseInsensitive))
        return createPdfWindow(path);

    const auto fileType = NativeOffice::FileRouter::detectFileType(path);

    switch (fileType) {
    case DetectedFileType::SpreadsheetData:
        return createCalcWindow(path);
    case DetectedFileType::PresentationData:
        return createImpressWindow(path);
    case DetectedFileType::WriterDocument:
    default:
        // createWriterWindow already calls addFile(path, "Writer") on success
        return createWriterWindow(path);
    }
}

// Open any document via smart content-based routing, as a new tab.
static void openDocumentByPath(const QString& path) {
    presentEditor(createWindowForPath(path));
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);

    // Quitting is explicit (gate cancelled, or the main shell closed). The
    // default quit-on-last-window-closed misfires during the gate → splash →
    // shell handoffs: Qt gives splash-type windows a transient parent, so
    // closing the gate while only the splash is up would exit the app.
    app.setQuitOnLastWindowClosed(false);

    // IMPORTANT: Set org/app before the first QSettings use (including
    // RecentFilesManager singleton construction triggered below)
    app.setApplicationName("NativeOffice");
    app.setApplicationVersion("0.4.0");
    app.setOrganizationName("NativeOffice");
    app.setOrganizationDomain("nativeoffice.app");

    // ── Parse the command line up front ─────────────────────────────────────
    // File paths open as tabs later (after the shell exists); a nativeoffice://
    // URL is the browser's auth fast path and may mean this whole process only
    // exists to deliver it to an already-running instance.
    const QStringList cliArgs = app.arguments();
    QStringList cliFiles;
    QString protocolUrl;
    for (int i = 1; i < cliArgs.size(); ++i) {
        const QString arg = cliArgs.at(i);
        if (arg.startsWith(QLatin1String("nativeoffice://")))
            protocolUrl = arg;
        else if (QFileInfo::exists(arg))
            cliFiles << arg;
    }

    // If the browser launched us purely to deliver an auth callback and a
    // primary instance is running, hand the URL over and exit immediately
    // (before any threads or windows are created).
    NativeOffice::InstanceGuard guard;
    if (!protocolUrl.isEmpty() && cliFiles.isEmpty()
        && guard.forwardToPrimary(protocolUrl)) {
        return 0;
    }
    guard.startPrimary();
    NativeOffice::InstanceGuard::registerProtocolScheme();

    // ── Warm Qt's font cache in the background ──────────────────────────────
    // The first document window builds a font picker; on font-heavy machines the
    // cold enumeration of installed fonts can stall for several seconds. Kicking
    // it off now (while the splash + home screen are on screen) means the cache
    // is already warm by the time the user opens a document, so it appears fast.
    // Joined after exec() so it never touches Qt during application teardown.
    std::thread fontWarmup([] {
        QFontDatabase::families();   // read-only; builds the shared font cache
    });

    // ── Global theme ───────────────────────────────────────────────────────
    auto& theme = NativeOffice::ThemeManager::instance();
    app.setStyleSheet(theme.applicationStyleSheet());

    // ── App controller ─────────────────────────────────────────────────────
    NativeOffice::AppController controller;

    // ── Main shell window (WPS-style tabbed workflow) ──────────────────────
    MainShell shell;
    g_shell = &shell;

    const QScreen* screen = QApplication::primaryScreen();
    shell.resize(1480, 900);
    shell.move(screen->availableGeometry().center() - shell.rect().center());

    // Home factories: the PINNED first tab always opens results as new tabs
    // (unchanged behavior), routing through AppController like before. Every
    // "+"-tab Home page is EPHEMERAL — picking a format converts that same
    // tab into the resulting document in place (browser-style new-tab-page
    // navigation), via MainShell::presentInHomeTab, instead of opening an
    // additional tab.
    auto makePinnedHome = [&controller]() -> QWidget* {
        auto* s = new NativeOffice::StartScreen(&controller);
        QObject::connect(s, &NativeOffice::StartScreen::newDocumentRequested,
                         &controller, &NativeOffice::AppController::newDocument);
        QObject::connect(s, &NativeOffice::StartScreen::fileOpenRequested,
                         &controller, &NativeOffice::AppController::openFile);
        QObject::connect(s, &NativeOffice::StartScreen::settingsRequested,
                         &controller, &NativeOffice::AppController::openSettings);
        QObject::connect(s, &NativeOffice::StartScreen::templateChosen,
                         s, [](NativeOffice::DocumentType type, const QString& name) {
            openFromTemplate(type, name);
        });
        QObject::connect(s, &NativeOffice::StartScreen::imageResizerRequested,
                         s, [] { presentEditor(createImageResizerWindow()); });
        return s;
    };

    auto makeEphemeralHome = [&controller, &shell]() -> QWidget* {
        auto* s = new NativeOffice::StartScreen(&controller);
        QObject::connect(s, &NativeOffice::StartScreen::newDocumentRequested,
                         s, [s, &shell](NativeOffice::DocumentType type) {
            using NativeOffice::DocumentType;
            EditorWindow* win = nullptr;
            switch (type) {
            case DocumentType::Writer:  win = createWriterWindow();  break;
            case DocumentType::Calc:    win = createCalcWindow();    break;
            case DocumentType::Impress: win = createImpressWindow(); break;
            case DocumentType::Pdf:     win = createPdfWindow();     break;
            }
            shell.presentInHomeTab(s, win);
        });
        QObject::connect(s, &NativeOffice::StartScreen::fileOpenRequested,
                         s, [s, &shell](const QString& path) {
            shell.presentInHomeTab(s, createWindowForPath(path));
        });
        QObject::connect(s, &NativeOffice::StartScreen::templateChosen,
                         s, [s, &shell](NativeOffice::DocumentType type, const QString& name) {
            shell.presentInHomeTab(s, createWindowForTemplate(type, name));
        });
        QObject::connect(s, &NativeOffice::StartScreen::imageResizerRequested,
                         s, [s, &shell] { shell.presentInHomeTab(s, createImageResizerWindow()); });
        QObject::connect(s, &NativeOffice::StartScreen::settingsRequested,
                         &controller, &NativeOffice::AppController::openSettings);
        return s;
    };

    shell.setHomeFactory(makeEphemeralHome);
    shell.setHomePage(makePinnedHome());   // pinned first tab

    // ── AppController → open new documents / files as tabs ────────────────
    QObject::connect(&controller, &NativeOffice::AppController::newDocumentRequested,
                     &app, [](NativeOffice::DocumentType type) {
        using NativeOffice::DocumentType;
        switch (type) {
        case DocumentType::Writer:  presentEditor(createWriterWindow());  break;
        case DocumentType::Calc:    presentEditor(createCalcWindow());    break;
        case DocumentType::Impress: presentEditor(createImpressWindow()); break;
        case DocumentType::Pdf:     presentEditor(createPdfWindow());     break;
        }
    });

    QObject::connect(&controller, &NativeOffice::AppController::fileOpenRequested,
                     &app, [](const QString& path) {
        openDocumentByPath(path);
    });

    // ── Open documents passed on the command line (file association /
    //    double-click-to-open). Each path is routed by content type and opens
    //    as a tab inside the shell.
    const bool openedFromCli = !cliFiles.isEmpty();
    for (const QString& path : cliFiles)
        openDocumentByPath(path);

    // Dev-only capture aid (same family as NATIVEOFFICE_*_GRAB): open the
    // Image Resizer tab on startup so it can be verified without UI clicks.
    if (qEnvironmentVariableIsSet("NATIVEOFFICE_OPEN_RESIZER"))
        presentEditor(createImageResizerWindow());

    auto& auth = NativeOffice::AuthManager::instance();
    QObject::connect(&guard, &NativeOffice::InstanceGuard::urlReceived,
                     &auth, [&auth, &shell](const QString&) {
        auth.pollNow();                   // browser says approval landed
        if (shell.isVisible()) { shell.raise(); shell.activateWindow(); }
    });

    // ── Reveal flow (after the sign-in gate) ───────────────────────────────
    // When launched to open a specific document, go straight to its tab (no
    // splash). Otherwise show the WPS-style startup splash, then reveal the
    // shell on the Home tab once it finishes.
    auto revealApp = [&shell, openedFromCli]() {
        const bool showSplash = QSettings().value("app/showSplash", true).toBool();
        if (openedFromCli || !showSplash) {
            shell.show();
            shell.raise();
            shell.activateWindow();
        } else {
            auto* splash = new NativeOffice::SplashScreen;
            QObject::connect(splash, &NativeOffice::SplashScreen::finished,
                             &shell, [&shell]() {
                shell.show();
                shell.raise();
                shell.activateWindow();
            });
            splash->start(2200);
        }
    };

    // ── Sign-in gate: the app is gated until a session exists ─────────────
    {
        auto* gate = new NativeOffice::LoginGate;
        QObject::connect(gate, &NativeOffice::LoginGate::proceed,
                         &shell, [revealApp]() { revealApp(); });
        gate->begin();
    }

    // Signing out (from Settings) hides the shell and re-gates.
    QObject::connect(&auth, &NativeOffice::AuthManager::signedOut,
                     &shell, [&shell]() {
        shell.hide();
        auto* gate = new NativeOffice::LoginGate;
        QObject::connect(gate, &NativeOffice::LoginGate::proceed,
                         &shell, [&shell]() {
            shell.show();
            shell.raise();
            shell.activateWindow();
        });
        gate->begin();
    });

    const int rc = app.exec();
    if (fontWarmup.joinable()) fontWarmup.join();
    return rc;
}

#include "main.moc"
