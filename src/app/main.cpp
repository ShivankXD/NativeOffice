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
        setTabsClosable(true);
        setElideMode(Qt::ElideRight);
        setMovable(false);
    }

protected:
    QSize tabSizeHint(int index) const override {
        QSize s = QTabBar::tabSizeHint(index);
        s.setHeight(qMax(s.height(), 36));
        s.setWidth(qMax(s.width() + 24, 110));   // padding + room for close button
        return s;
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#FFFFFF"));      // white strip

        for (int i = 0; i < count(); ++i) {
            const QRect r   = tabRect(i);
            const bool home = (i == 0);
            const bool sel  = (i == currentIndex());

            const QColor bg = home ? QColor(sel ? "#6D28D9" : "#7C3AED")
                                   : QColor("#FFFFFF");
            const QColor fg = home ? QColor("#FFFFFF")
                                   : QColor(sel ? "#6D28D9" : "#2C3140");
            p.fillRect(r, bg);

            if (!home) {
                p.setPen(QColor("#E6E8ED"));
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
    }
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

        // White top strip: tab bar on the left, "+" right after the last tab.
        auto* topBar = new QWidget(central);
        topBar->setObjectName("shellTopBar");
        topBar->setStyleSheet("#shellTopBar { background:#FFFFFF; }");
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
            "QToolButton { font-size:20px; font-weight:600; color:#6D28D9;"
            "  background:#FFFFFF; padding:0 16px; border:none; }"
            "QToolButton:hover { color:#7C3AED; background:#F3F0FB; }");
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

        // Assign a per-type sequential name ("Document 1", "Spreadsheet 1", …).
        const QString kind = win->docKindName();
        int n = 0;
        if      (kind == QLatin1String("Spreadsheet"))  n = ++m_nSheet;
        else if (kind == QLatin1String("Presentation")) n = ++m_nPres;
        else                                             n = ++m_nDoc;
        win->setProperty("autoName", QStringLiteral("%1 %2").arg(kind).arg(n));

        addPage(win, labelFor(win));

        // Keep the tab label in sync with the document's save/dirty state.
        connect(win, &QWidget::windowTitleChanged, this, [this, win](const QString&) {
            const int i = m_stack->indexOf(win);
            if (i >= 0) m_bar->setTabText(i, labelFor(win));
        });
    }

protected:
    void closeEvent(QCloseEvent* e) override {
        // Prompt for every dirty document before the whole app exits.
        for (int i = m_stack->count() - 1; i >= 0; --i) {
            auto* win = qobject_cast<EditorWindow*>(m_stack->widget(i));
            if (win && !win->requestClose()) { e->ignore(); return; }
        }
        e->accept();
    }

private:
    // Append a page: add the tab and the matching stacked widget (1:1 indices),
    // then focus it.
    int addPage(QWidget* page, const QString& label) {
        const int idx = m_bar->addTab(label);
        m_stack->insertWidget(idx, page);
        m_bar->setCurrentIndex(idx);
        m_stack->setCurrentIndex(idx);
        return idx;
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
        presentEditor(createCalcWindow(path));
        break;
    case DetectedFileType::PresentationData:
        presentEditor(createImpressWindow(path));
        break;
    case DetectedFileType::WriterDocument:
    default:
        // createWriterWindow already calls addFile(path, "Writer") on success
        presentEditor(createWriterWindow(path));
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

    // Factory for Home pages (StartScreen wired to the controller). Used for the
    // pinned first tab and for every extra Home page opened via the "+" button.
    auto makeHome = [&controller]() -> QWidget* {
        auto* s = new NativeOffice::StartScreen(&controller);
        QObject::connect(s, &NativeOffice::StartScreen::newDocumentRequested,
                         &controller, &NativeOffice::AppController::newDocument);
        QObject::connect(s, &NativeOffice::StartScreen::fileOpenRequested,
                         &controller, &NativeOffice::AppController::openFile);
        QObject::connect(s, &NativeOffice::StartScreen::settingsRequested,
                         &controller, &NativeOffice::AppController::openSettings);
        return s;
    };
    shell.setHomeFactory(makeHome);
    shell.setHomePage(makeHome());   // pinned first tab

    // ── AppController → open new documents / files as tabs ────────────────
    QObject::connect(&controller, &NativeOffice::AppController::newDocumentRequested,
                     &app, [](NativeOffice::DocumentType type) {
        using NativeOffice::DocumentType;
        switch (type) {
        case DocumentType::Writer:  presentEditor(createWriterWindow());  break;
        case DocumentType::Calc:    presentEditor(createCalcWindow());    break;
        case DocumentType::Impress: presentEditor(createImpressWindow()); break;
        }
    });

    QObject::connect(&controller, &NativeOffice::AppController::fileOpenRequested,
                     &app, [](const QString& path) {
        openDocumentByPath(path);
    });

    // ── Open any document passed on the command line (file association /
    //    double-click-to-open). Each existing path is routed by content type
    //    and opens as a tab inside the shell.
    const QStringList cliArgs = app.arguments();
    bool openedFromCli = false;
    for (int i = 1; i < cliArgs.size(); ++i) {
        const QString path = cliArgs.at(i);
        if (QFileInfo::exists(path)) {
            openDocumentByPath(path);
            openedFromCli = true;
        }
    }

    // When launched to open a specific document, go straight to its tab (no
    // splash). Otherwise show the WPS-style startup splash, then reveal the
    // shell on the Home tab once it finishes.
    if (openedFromCli) {
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

    const int rc = app.exec();
    if (fontWarmup.joinable()) fontWarmup.join();
    return rc;
}

#include "main.moc"
