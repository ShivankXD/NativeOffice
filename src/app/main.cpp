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
#include "startscreen/LucideIcons.h"
#include "tools/ImageResizer.h"
#include "tools/MarkdownEditor.h"
#include "tools/QrCodeGenerator.h"
#include "tools/PdfToolPage.h"
#include "auth/LoginGate.h"
#include "auth/InstanceGuard.h"
#include "core/common/BrandBar.h"
#include "core/theme/ThemeManager.h"
#include "core/application/AppController.h"
#include "core/application/RecentFilesManager.h"
#include "core/application/FileRouter.h"
#include "core/application/UpdateChecker.h"
#include "core/auth/AuthManager.h"
#include "core/watermark/WatermarkPdf.h"
#include "core/settings/ExportPrefs.h"
#include "core/settings/UsageStats.h"
#include <QScopedValueRollback>

#include "WriterModule.h"
#include "CalcModule.h"
#include "SpreadsheetModel.h"
#include "ImpressModule.h"
#include "PdfModule.h"

#include <QApplication>
#include <QDateTime>
#include <QMainWindow>
#include <QScreen>
#include <QIcon>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QCloseEvent>
#include <QMouseEvent>
#include <QTime>
#include <QDate>
#include <QSettings>
#include <QPointer>
#include <QTimer>
#include <QTabWidget>
#include <QTabBar>
#include <QToolButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFontDatabase>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QShortcut>
#include <QKeySequence>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QLinearGradient>
#include <QSplitter>
#include "ai/AiConsent.h"
#include "ai/AiTypes.h"
#include "ai/ui/AiConsentDialog.h"
#include "ai/ui/AiSidebar.h"
#include "ai/AiSheetAgent.h"
#include "ai/AiSlideAgent.h"
#include <functional>
#include <thread>
// Sprint 6: PDF export
#include <QPdfWriter>
#include <QPainter>
#include <QPageSize>
#include <QPageLayout>

// Deliberately last: windows.h defines min/max macros and a pile of generic
// names that collide with Qt if it is pulled in ahead of the Qt headers.
#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <windowsx.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Window sizing
// ─────────────────────────────────────────────────────────────────────────────
// Fit a freshly built top-level window to the display it will actually appear
// on, then centre it there.
//
// Every window in this file is constructed at a fixed pixel size (1480x900 for
// the shell, 1200x800 for the editors) chosen on a large monitor. Those are
// LOGICAL pixels, so on a 1920x1080 screen at 150% scaling the usable desktop
// is only about 1280x680 and the window opened wider and taller than the
// screen, hanging off the right and bottom edges with no way to see what was
// out there.
//
// The minimum size matters just as much as the requested size: a minimum
// larger than the screen (Impress asked for 1100x680) makes the overflow
// permanent, because the user cannot resize the window back down again. So
// clamp both.
//
// Called after construction rather than inside it, so it sees the window's
// real screen and the size the constructor asked for.
static void fitWindowToScreen(QWidget* w) {
    if (!w) return;
    QScreen* scr = w->screen();
    if (!scr) scr = QApplication::primaryScreen();
    if (!scr) return;

    const QRect avail = scr->availableGeometry();
    // Small margin so the frame and its shadow are not flush against the edges
    // of the work area (and the taskbar stays reachable).
    const int maxW = qMax(480, avail.width()  - 48);
    const int maxH = qMax(360, avail.height() - 48);

    const QSize mn = w->minimumSize();
    if (mn.width() > maxW || mn.height() > maxH)
        w->setMinimumSize(qMin(mn.width(), maxW), qMin(mn.height(), maxH));

    w->resize(qMin(w->width(), maxW), qMin(w->height(), maxH));
    w->move(avail.center() - w->rect().center());
}

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

    // ── On-screen name model (drives the tab label + BrandBar rename field) ──
    // kindWord(): noun used in the "untitled <word>" tab label. Empty ⇒ a tool
    //   (no rename bar; the tab just shows docKindName()).
    // nameExt(): the format suffix shown outside the rename field (".docx", …).
    virtual QString kindWord() const { return {}; }
    virtual QString nameExt()  const { return {}; }

    QString baseName() const { return m_baseName; }
    bool    isNamed()  const { return m_named; }

    void setBaseName(const QString& n) {
        const QString t = n.trimmed();
        if (t.isEmpty() || t == m_baseName) return;
        m_baseName = t;
        m_named = true;
        emit displayNameChanged();
    }
    // Adopt the base name from the current file path (after open / save-as).
    void syncNameFromPath() {
        const QString p = currentDocPath();
        if (p.isEmpty()) return;
        const QString b = QFileInfo(p).completeBaseName();
        if (b.isEmpty() || (m_named && b == m_baseName)) return;
        m_baseName = b;
        m_named = true;
        emit displayNameChanged();
    }
    // Bind the module's embedded BrandBar rename field to this window's name.
    // Call at the end of each subclass constructor (once the central widget —
    // which owns the BrandBar — is in place).
    void wireDocNameBar() {
        if (kindWord().isEmpty()) return;             // tools have no rename bar
        auto* bb = findChild<NativeOffice::BrandBar*>();
        if (!bb) return;
        bb->setDocName(m_baseName, nameExt());
        connect(bb, &NativeOffice::BrandBar::docNameEdited,
                this, [this](const QString& n) { setBaseName(n); });
        connect(this, &EditorWindow::displayNameChanged, bb,
                [this, bb] { bb->setDocName(m_baseName, nameExt()); });
    }

    // ── Autosave ─────────────────────────────────────────────────────────────
    // Deliberately built out of what every window already exposes: docDirty()
    // to know there is work to store and currentDocPath() to know where. There
    // is no change signal to subscribe to and no per-window timer; the shell
    // ticks every open tab from one timer (MainShell::startAutoSave), so the
    // cost is one bool check per tab per few seconds whether or not anything
    // is being edited.
    //
    // saveToCurrentPath() must never open a dialog. Writing to a path the
    // document already has cannot need one, and an autosave that could pop a
    // file chooser while someone types would be worse than no autosave.
    // Saves to an exact path with no dialog of any kind. Each window forwards
    // this to the same performSave() its File menu uses.
    virtual bool saveQuietlyTo(const QString& path) { Q_UNUSED(path); return false; }
    virtual bool autoSaveEnabled() const { return true; }

    // Whether an automatic write is allowed to land on the document's own file.
    //
    // It is not, for any format this app reads more of than it writes. A real
    // .xlsx carries charts, pictures, pivot tables and defined names that the
    // exporter does not model, so an autosave tick over the original replaced a
    // 78-part workbook with a 7-part one: the charts were gone and other office
    // suites could no longer open it. Opening someone's file must never be able
    // to damage it, so for those formats the tick writes a recovery copy
    // alongside and leaves the original exactly as it was found.
    virtual bool canAutoSaveInPlace() const { return true; }

    // Where the recovery copy for a document we must not overwrite goes.
    QString recoveryPathFor(const QString& original) const {
        QDir dir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                 + QStringLiteral("/NativeOffice/Recovered"));
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) return {};
        const QFileInfo fi(original);
        // Kept in our own format: it is the one thing guaranteed to hold
        // everything the editor currently has in memory.
        return dir.filePath(fi.completeBaseName() + QStringLiteral(".noff"));
    }

    // True only while an autosave tick is writing. Subclasses check it in
    // performSave() so an automatic write is not counted as the user editing
    // and saving a file.
    bool m_autoSaving { false };

    // Writes at most once every kAutoSaveGapMs, so holding a key down cannot
    // turn into a save per keystroke on a large file.
    static constexpr qint64 kAutoSaveGapMs = 2500;

    // A brand new document has no file behind it, which is exactly why the
    // first version of this did nothing at all for the case that matters most.
    // Autosave gives it one: a real file in Documents/NativeOffice named after
    // whatever the rename field says, so from the first tick onwards it is an
    // ordinary saved document that Save As can later move wherever you like.
    QString reserveAutoSavePath() {
        const QString ext = nameExt();
        if (ext.isEmpty()) return {};                  // a tool, not a document
        QDir dir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                 + QStringLiteral("/NativeOffice"));
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) return {};

        QString base = baseName().trimmed();
        if (base.isEmpty()) base = QStringLiteral("untitled");
        // Anything Windows forbids in a file name would make the save fail.
        // Written as a plain character sweep rather than a regex: moc stops
        // parsing this file at a raw string literal containing a quote, which
        // silently strips the meta-objects from every class defined below it.
        const QString forbidden = QStringLiteral("\\/:*?<>|") + QChar(0x22);
        for (int i = 0; i < base.size(); ++i)
            if (forbidden.contains(base.at(i))) base[i] = QLatin1Char('-');

        QString path = dir.filePath(base + ext);
        for (int n = 2; QFileInfo::exists(path) && n < 500; ++n)
            path = dir.filePath(base + QStringLiteral("-") + QString::number(n) + ext);
        return path;
    }

    // Stores the document right now, whether or not it has ever been saved.
    // Returns true when there is nothing left unsaved.
    bool autoSaveNow() {
        if (!autoSaveEnabled() || !docDirty()) return true;
        QString path = currentDocPath();
        if (path.isEmpty())            path = reserveAutoSavePath();
        else if (!canAutoSaveInPlace()) path = recoveryPathFor(path);
        if (path.isEmpty()) return false;
        // Marks this write as automatic so the "Files edited" counter does not
        // tick every few seconds while someone is simply typing.
        QScopedValueRollback<bool> autoGuard(m_autoSaving, true);
        if (!saveQuietlyTo(path)) return false;
        m_lastAutoSaveMs = QDateTime::currentMSecsSinceEpoch();
        syncNameFromPath();          // tab + rename field adopt the new name
        return true;
    }

    // The periodic tick only stores documents that already have a file. A blank
    // new document reports dirty from the moment it is created, so naming those
    // on a timer produced an empty untitled.docx every time a tab was opened and
    // abandoned. Naming happens on close instead, where there is real content to
    // keep, which is why closing still never asks anything.
    bool autoSaveTick() {
        if (!autoSaveEnabled() || !docDirty() || currentDocPath().isEmpty()) return false;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_lastAutoSaveMs && now - m_lastAutoSaveMs < kAutoSaveGapMs) return false;
        return autoSaveNow();
    }

    // Counts in seconds and switches to minutes once past 60, which is the
    // wording that was asked for. "Autosave on" covers the moment before
    // anything has been written, because claiming "0 sec ago" there would be
    // reporting a save that has not happened.
    QString autoSaveStatusText() const {
        if (!autoSaveEnabled()) return {};
        if (!m_lastAutoSaveMs)  return QStringLiteral("Autosave on");
        const qint64 s = (QDateTime::currentMSecsSinceEpoch() - m_lastAutoSaveMs) / 1000;
        if (s < 60) return QStringLiteral("Autosave %1 sec ago").arg(s);
        const qint64 m = s / 60;
        if (m < 60) return QStringLiteral("Autosave %1 min ago").arg(m);
        return QStringLiteral("Autosave %1 hr ago").arg(m / 60);
    }

    // Closing. Every dirty document is stored first, including one that has
    // never been named, so there is nothing left to ask about. The old prompt
    // now only appears if the write itself failed, where staying silent would
    // mean losing the work.
    bool closedCleanlyByAutoSave() {
        // A recovery copy is not the same as having saved the user's file, so
        // closing a modified document in one of those formats still asks.
        if (!canAutoSaveInPlace() && docDirty()) { autoSaveNow(); return false; }
        return autoSaveNow();
    }

signals:
    void displayNameChanged();

protected:
    QString m_baseName { QStringLiteral("untitled") };
    bool    m_named    { false };
    qint64  m_lastAutoSaveMs { 0 };
};

// Forward declarations for free-function helpers
static class WriterWindow* createWriterWindow(const QString& filePath = {});
static class CalcWindow*   createCalcWindow(const QString& filePath = {});
static class ImpressWindow* createImpressWindow(const QString& filePath = {});
static class PdfWindow*    createPdfWindow(const QString& filePath = {});
static class ImageResizerWindow* createImageResizerWindow();
static class MarkdownEditorWindow* createMarkdownEditorWindow(const QString& filePath = {});
static class QrCodeWindow*   createQrCodeWindow();
static class PdfToolWindow*  createPdfToolWindow(NativeOffice::PdfToolPage::Job job);
// One entry point for every tool tile on the Home screen.
static EditorWindow* createToolWindow(NativeOffice::StartScreen::Tool tool);

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
        syncNameFromPath();
        wireDocNameBar();

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
                ? QDir::homePath() + "/" + baseName() + "."
                      + NativeOffice::ExportPrefs::defaultDocFormat()
                : m_writer->currentFilePath(),
            WRITER_SAVE_FILTER, &selectedFilter);

        if (path.isEmpty()) return false;
        // Append an extension matching the chosen filter if the user omitted one.
        // With no filter hint, fall back to the premium default format.
        if (QFileInfo(path).suffix().isEmpty()) {
            if      (selectedFilter.contains("noff")) path += ".noff";
            else if (selectedFilter.contains("html")) path += ".html";
            else if (selectedFilter.contains("docx")) path += ".docx";
            else path += "." + NativeOffice::ExportPrefs::defaultDocFormat();
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
    QString kindWord()       const override { return QStringLiteral("document"); }
    QString nameExt()        const override { return QStringLiteral(".docx"); }
    QString currentDocPath() const override { return m_writer ? m_writer->currentFilePath() : QString(); }
    bool    docDirty()       const override { return m_writer && m_writer->isDirty(); }
    bool saveQuietlyTo(const QString& path) override { return performSave(path); }

    // An automatic write must never land on a document in a format this app
    // rebuilds rather than round-trips: .docx carries far more than the
    // exporter models, and a timer-driven rewrite would quietly strip it.
    // Autosave still protects the work, in a recovery copy alongside.
    bool canAutoSaveInPlace() const override {
        const QString path = currentDocPath();
        return path.isEmpty()
               || path.endsWith(QStringLiteral(".noff"), Qt::CaseInsensitive);
    }

    bool requestClose() override {
        if (closedCleanlyByAutoSave()) return true;
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
        if (!m_autoSaving) NativeOffice::UsageStats::instance().noteFileEdited();
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
        syncNameFromPath();
        wireDocNameBar();

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
                ? QDir::homePath() + "/" + baseName() + "."
                      + NativeOffice::ExportPrefs::defaultSheetFormat()
                : m_calc->currentFilePath(),
            CALC_SAVE_FILTER, &selectedFilter);

        if (path.isEmpty()) return false;
        // Append the extension matching the chosen filter if the user omitted one.
        // With no filter hint, fall back to the premium default format.
        if (QFileInfo(path).suffix().isEmpty()) {
            if      (selectedFilter.contains("noff")) path += ".noff";
            else if (selectedFilter.contains("csv"))  path += ".csv";
            else if (selectedFilter.contains("xlsx")) path += ".xlsx";
            else path += "." + NativeOffice::ExportPrefs::defaultSheetFormat();
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
    QString kindWord()       const override { return QStringLiteral("sheet"); }
    QString nameExt()        const override { return QStringLiteral(".xlsx"); }
    QString currentDocPath() const override { return m_calc ? m_calc->currentFilePath() : QString(); }
    bool    docDirty()       const override { return m_calc && m_calc->isDirty(); }
    bool saveQuietlyTo(const QString& path) override { return performSave(path); }

    // ── Autosave is OFF for spreadsheets ────────────────────────────────────
    // Turned off at the user's explicit request while .xlsx fidelity is being
    // worked on, and it stays off until they say otherwise.
    //
    // The reason it cannot simply be left on: the exporter rebuilds the package
    // out of the cells alone, so an automatic write turned a real .xlsx into a
    // cells-only file - charts and pictures gone, and other office suites could
    // not open the result. Nothing is written unless the user asks for it.
    bool autoSaveEnabled() const override { return false; }

    // Kept for when autosave is switched back on: even then, only our own
    // format is safe for an automatic in-place write.
    bool canAutoSaveInPlace() const override {
        const QString path = currentDocPath();
        return path.isEmpty()
               || path.endsWith(QStringLiteral(".noff"), Qt::CaseInsensitive);
    }

    bool requestClose() override {
        if (closedCleanlyByAutoSave()) return true;
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
        if (!confirmLossySave(path)) return false;
        if (!m_calc->saveToPath(path)) {
            QMessageBox::critical(this, "Save Failed",
                "Could not write to:\n" + path);
            return false;
        }
        NativeOffice::RecentFilesManager::instance().addFile(path, "Calc");
        if (!m_autoSaving) NativeOffice::UsageStats::instance().noteFileEdited();
        updateTitle();
        return true;
    }

    // Writing .xlsx rebuilds the package out of the cells, so the charts and
    // pictures this workbook carries would not survive the trip. Asked once per
    // document: silently dropping someone's charts is how a file comes back
    // "broken" in another office suite.
    bool confirmLossySave(const QString& path) {
        if (m_autoSaving || m_lossySaveAccepted) return true;
        if (!path.endsWith(QStringLiteral(".xlsx"), Qt::CaseInsensitive)) return true;
        const int objects = m_calc ? m_calc->sheetObjectCount() : 0;
        if (objects <= 0) return true;

        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(QStringLiteral("Charts and pictures will not be saved"));
        box.setText(QString("This workbook contains %1 chart%2 or picture%2 that "
                            "NativeOffice cannot yet write back into an .xlsx file.")
                        .arg(objects).arg(objects == 1 ? "" : "s"));
        box.setInformativeText(
            "Saving as .xlsx keeps the cells and loses those objects.\n"
            "Saving as .noff keeps everything.");
        QPushButton* keep = box.addButton(QStringLiteral("Save as .noff instead"),
                                          QMessageBox::AcceptRole);
        QPushButton* anyway = box.addButton(QStringLiteral("Save as .xlsx anyway"),
                                            QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(keep);
        box.exec();

        if (box.clickedButton() == anyway) { m_lossySaveAccepted = true; return true; }
        if (box.clickedButton() == keep) {
            QString alt = path;
            alt.chop(5);                       // ".xlsx"
            alt += QStringLiteral(".noff");
            return performSave(alt);           // writes the lossless copy instead
        }
        return false;                          // cancelled
    }

    bool m_lossySaveAccepted { false };
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
        syncNameFromPath();
        wireDocNameBar();

        connect(im, &NativeOffice::ImpressModule::documentModified,
                this, &ImpressWindow::updateTitle);
        connect(im, &NativeOffice::ImpressModule::filePathChanged,
                this, [this](const QString&) { updateTitle(); });
    }

    NativeOffice::ImpressModule* impress() const { return m_impress; }

    bool saveAs() {
        const QString ext = "." + NativeOffice::ExportPrefs::defaultDeckFormat();
        const QString suggested =
            m_impress->currentFilePath().isEmpty()
                ? QDir::homePath() + "/" + baseName() + ext
                : QFileInfo(m_impress->currentFilePath()).absolutePath() + "/"
                      + QFileInfo(m_impress->currentFilePath()).completeBaseName() + ext;
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
    QString kindWord()       const override { return QStringLiteral("presentation"); }
    QString nameExt()        const override { return QStringLiteral(".pptx"); }
    QString currentDocPath() const override { return m_impress ? m_impress->currentFilePath() : QString(); }
    bool    docDirty()       const override { return m_impress && m_impress->isDirty(); }

    bool saveQuietlyTo(const QString& path) override { return performSave(path); }

    // An automatic write must never land on a presentation in a format this app
    // rebuilds rather than round-trips: .pptx carries far more than the
    // exporter models, and a timer-driven rewrite would quietly strip it.
    // Autosave still protects the work, in a recovery copy alongside.
    bool canAutoSaveInPlace() const override {
        const QString path = currentDocPath();
        return path.isEmpty()
               || path.endsWith(QStringLiteral(".noff"), Qt::CaseInsensitive);
    }

    bool requestClose() override {
        if (closedCleanlyByAutoSave()) return true;
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
        if (!m_autoSaving) NativeOffice::UsageStats::instance().noteFileEdited();
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
        syncNameFromPath();
        wireDocNameBar();
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
    QString kindWord()       const override { return QStringLiteral("pdf"); }
    QString nameExt()        const override { return QStringLiteral(".pdf"); }
    QString currentDocPath() const override { return m_pdf ? m_pdf->currentFilePath() : QString(); }
    bool    docDirty()       const override { return m_pdf && m_pdf->isDirty(); }
    // PdfWindow has no performSave(); it writes through the module directly.
    bool saveQuietlyTo(const QString& path) override {
        return m_pdf && m_pdf->saveToPath(path);
    }

    bool requestClose() override {
        if (closedCleanlyByAutoSave()) return true;
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
    // Explicitly excluded: it edits an image you then export, not a document it
    // owns, so there is nothing for an autosave to write back to.
    bool    autoSaveEnabled() const override { return false; }
};

static ImageResizerWindow* createImageResizerWindow() {
    auto* win = new ImageResizerWindow;
    fitWindowToScreen(win);
    return win;
}

// ─────────────────────────────────────────────────────────────────────────────
// MarkdownEditorWindow — the Markdown Editor tool tab (Home → Tools). Like the
// Image Resizer, it holds no persistent document, so closing never prompts.
// ─────────────────────────────────────────────────────────────────────────────
class MarkdownEditorWindow : public EditorWindow {
    Q_OBJECT
public:
    explicit MarkdownEditorWindow(QWidget* parent = nullptr)
        : EditorWindow(parent)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setMinimumSize(900, 600);
        resize(1200, 800);
        setWindowTitle("Markdown Editor");
        m_md = new NativeOffice::MarkdownEditorWidget(this);
        setCentralWidget(m_md);
        wireDocNameBar();
        m_md->setDocName(baseName());
        connect(this, &EditorWindow::displayNameChanged, this,
                [this] { m_md->setDocName(baseName()); });
    }

    bool    requestClose()          override { return true; }
    QString docKindName()     const override { return QStringLiteral("Markdown Editor"); }
    QString kindWord()        const override { return QStringLiteral("markdown"); }
    QString nameExt()         const override { return QStringLiteral(".md"); }
    QString currentDocPath()  const override { return {}; }
    bool    docDirty()        const override { return false; }

    // Home → Open File routes .md here instead of into Writer.
    void loadFile(const QString& path) {
        if (!m_md->loadFromFile(path)) return;
        setBaseName(QFileInfo(path).completeBaseName());
        NativeOffice::RecentFilesManager::instance().addFile(path, "Markdown");
    }

private:
    NativeOffice::MarkdownEditorWidget* m_md { nullptr };
};

static MarkdownEditorWindow* createMarkdownEditorWindow(const QString& filePath) {
    auto* win = new MarkdownEditorWindow;
    if (!filePath.isEmpty()) win->loadFile(filePath);
    fitWindowToScreen(win);
    return win;
}

// ─────────────────────────────────────────────────────────────────────────────
// QrCodeWindow — the QR Code Generator tool tab (Home → Tools). Like the other
// tools it owns no document, so closing never prompts.
// ─────────────────────────────────────────────────────────────────────────────
class QrCodeWindow : public EditorWindow {
    Q_OBJECT
public:
    explicit QrCodeWindow(QWidget* parent = nullptr) : EditorWindow(parent) {
        setAttribute(Qt::WA_DeleteOnClose);
        setMinimumSize(900, 620);
        resize(1120, 760);
        setWindowTitle("QR Code Generator");
        setCentralWidget(new NativeOffice::QrCodeGeneratorWidget(this));
    }

    bool    requestClose()          override { return true; }
    QString docKindName()     const override { return QStringLiteral("QR Code Generator"); }
    QString currentDocPath()  const override { return {}; }
    bool    docDirty()        const override { return false; }
    bool    autoSaveEnabled() const override { return false; }
};

static QrCodeWindow* createQrCodeWindow() {
    auto* win = new QrCodeWindow;
    fitWindowToScreen(win);
    return win;
}

// ─────────────────────────────────────────────────────────────────────────────
// PdfToolWindow — Compress PDF / OCR / PDF to Word. One wrapper, three jobs;
// each opens a file, runs it, and writes the result where the user chooses.
// ─────────────────────────────────────────────────────────────────────────────
class PdfToolWindow : public EditorWindow {
    Q_OBJECT
public:
    explicit PdfToolWindow(NativeOffice::PdfToolPage::Job job, QWidget* parent = nullptr)
        : EditorWindow(parent), m_title(NativeOffice::PdfToolPage::jobTitle(job)) {
        setAttribute(Qt::WA_DeleteOnClose);
        setMinimumSize(820, 560);
        resize(1040, 700);
        setWindowTitle(m_title);
        setCentralWidget(new NativeOffice::PdfToolPage(job, this));
    }

    bool    requestClose()          override { return true; }
    QString docKindName()     const override { return m_title; }
    QString currentDocPath()  const override { return {}; }
    bool    docDirty()        const override { return false; }
    bool    autoSaveEnabled() const override { return false; }

private:
    QString m_title;
};

static PdfToolWindow* createPdfToolWindow(NativeOffice::PdfToolPage::Job job) {
    auto* win = new PdfToolWindow(job);
    fitWindowToScreen(win);
    return win;
}

// Maps a Home tool tile onto its window.
static EditorWindow* createToolWindow(NativeOffice::StartScreen::Tool tool) {
    using Tool = NativeOffice::StartScreen::Tool;
    using Job  = NativeOffice::PdfToolPage::Job;
    switch (tool) {
    case Tool::ImageResizer:   return createImageResizerWindow();
    case Tool::MarkdownEditor: return createMarkdownEditorWindow();
    case Tool::QrCode:         return createQrCodeWindow();
    case Tool::CompressPdf:    return createPdfToolWindow(Job::Compress);
    case Tool::Ocr:            return createPdfToolWindow(Job::Ocr);
    case Tool::PdfToWord:      break;
    }
    return createPdfToolWindow(Job::ToWord);
}

// ─────────────────────────────────────────────────────────────────────────────
// ShellTabBar — custom-painted tab bar: a white strip, the first ("Home") tab
// violet, every other tab white. (Native QTabBar ignores per-tab background, so
// we paint it ourselves for an exact, style-independent look.)
// ─────────────────────────────────────────────────────────────────────────────
class ShellTabBar : public QTabBar {
    Q_OBJECT
public:
    // The strip is the topmost thing in the window now, so its height is the
    // app's title bar height. 30 keeps a comfortable click target while looking
    // like a tab strip rather than a toolbar.
    static constexpr int kTabHeight = 26;

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
        // The bar lives inside a QScrollArea now, so its own overflow arrows
        // would be a second, competing scroll mechanism.
        setUsesScrollButtons(false);
        setFixedHeight(kTabHeight);
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
        s.setHeight(kTabHeight);
        s.setWidth(qMax(s.width() + 20, 104));   // padding + room for close button
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
                // Flush with the very bottom row. Sitting it two pixels up left
                // a dark band underneath the violet, which read as a stray line
                // below the selected tab.
                if (sel)
                    p.fillRect(QRect(r.left(), r.bottom() - 1, r.width(), 2),
                               QColor("#7C3AED"));
            }

            QFont f = font();
            f.setBold(home || sel);
            p.setFont(f);
            p.setPen(fg);
            // Leave room on the right for the close button on non-Home tabs.
            const QRect tr = r.adjusted(12, 0, home ? -12 : -26, 0);
            p.drawText(tr, Qt::AlignVCenter | Qt::AlignLeft,
                       fontMetrics().elidedText(tabText(i), Qt::ElideRight, tr.width()));
        }

        // No bottom hairline. It used to separate the strip from the page below,
        // but the strip is the title bar now and the line drew straight across
        // the selected tab's violet underline, leaving a dark seam under it.
    }

private:
    int m_hover { -1 };
};

// ─────────────────────────────────────────────────────────────────────────────
// TabFade — the soft darkening at the edges of the tab strip when it scrolls,
// the same cue chat sidebars use to say "there is more this way". Purely
// decorative: it sits over the scroll viewport and passes every click through.
// ─────────────────────────────────────────────────────────────────────────────
class TabFade : public QWidget {
    Q_OBJECT
public:
    explicit TabFade(QWidget* parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
    }
    // How much room is left to scroll in each direction, so a fade is only
    // drawn on a side that actually has hidden tabs.
    void setOverflow(bool left, bool right) {
        if (left == m_left && right == m_right) return;
        m_left = left; m_right = right;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        static constexpr int kW = 26;
        QPainter p(this);
        const QColor solid("#0D1117");
        QColor clear = solid; clear.setAlpha(0);

        if (m_left) {
            QLinearGradient g(0, 0, kW, 0);
            g.setColorAt(0.0, solid);
            g.setColorAt(1.0, clear);
            p.fillRect(QRect(0, 0, kW, height()), g);
        }
        if (m_right) {
            QLinearGradient g(width() - kW, 0, width(), 0);
            g.setColorAt(0.0, clear);
            g.setColorAt(1.0, solid);
            p.fillRect(QRect(width() - kW, 0, kW, height()), g);
        }
    }

private:
    bool m_left { false };
    bool m_right { false };
};

// ─────────────────────────────────────────────────────────────────────────────
// WindowControls — minimise, maximise/restore and close.
//
// The native title bar is gone (see MainShell::nativeEvent), so these are ours
// to draw. They stay invisible until the pointer reaches the top strip, which
// keeps the chrome quiet while working; the space is reserved permanently so
// nothing shifts when they fade in, and it is the one region of the strip that
// never holds tabs.
// ─────────────────────────────────────────────────────────────────────────────
class WindowControls : public QWidget {
    Q_OBJECT
public:
    // Windows' own title bar glyphs, from the shell icon font. Typed characters
    // like "─", "□" and "✕" were being drawn by whatever text font happened to
    // contain them, at a size they were never hinted for, which is why they
    // looked ragged next to every other window on the desktop. These are the
    // exact codepoints Explorer uses. Segoe Fluent Icons ships with Windows 11
    // and Segoe MDL2 Assets with Windows 10, so both are named.
    static constexpr char16_t kGlyphMin[]     = u"";   // ChromeMinimize
    static constexpr char16_t kGlyphMax[]     = u"";   // ChromeMaximize
    static constexpr char16_t kGlyphRestore[] = u"";   // ChromeRestore
    static constexpr char16_t kGlyphClose[]   = u"";   // ChromeClose

    explicit WindowControls(QWidget* target, QWidget* parent = nullptr)
        : QWidget(parent), m_target(target)
    {
        auto* h = new QHBoxLayout(this);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(0);

        m_min   = makeButton(kGlyphMin,   QStringLiteral("Minimise"), false);
        m_max   = makeButton(kGlyphMax,   QStringLiteral("Maximise"), false);
        m_close = makeButton(kGlyphClose, QStringLiteral("Close"),    true);
        h->addWidget(m_min);
        h->addWidget(m_max);
        h->addWidget(m_close);

        connect(m_min, &QToolButton::clicked, this, [this] { m_target->showMinimized(); });
        connect(m_max, &QToolButton::clicked, this, [this] {
            if (m_target->isMaximized()) m_target->showNormal();
            else                         m_target->showMaximized();
            refreshMaxIcon();
        });
        connect(m_close, &QToolButton::clicked, this, [this] { m_target->close(); });

        // Faded rather than hidden so the strip's layout never moves.
        m_fade = new QGraphicsOpacityEffect(this);
        m_fade->setOpacity(0.0);
        setGraphicsEffect(m_fade);
        m_anim = new QPropertyAnimation(m_fade, "opacity", this);
        m_anim->setDuration(130);
    }

    void setRevealed(bool on) {
        if (on == m_revealed) return;
        m_revealed = on;
        m_anim->stop();
        m_anim->setStartValue(m_fade->opacity());
        m_anim->setEndValue(on ? 1.0 : 0.0);
        m_anim->start();
    }

    void refreshMaxIcon() {
        const bool zoomed = m_target->isMaximized();
        m_max->setText(QString::fromUtf16(zoomed ? kGlyphRestore : kGlyphMax));
        m_max->setToolTip(zoomed ? QStringLiteral("Restore") : QStringLiteral("Maximise"));
    }

private:
    QToolButton* makeButton(const char16_t* glyph, const QString& tip, bool danger) {
        auto* b = new QToolButton(this);
        b->setText(QString::fromUtf16(glyph));
        b->setToolTip(tip);
        b->setCursor(Qt::ArrowCursor);
        b->setFixedSize(44, ShellTabBar::kTabHeight);
        b->setFocusPolicy(Qt::NoFocus);
        // 10px is the size Windows draws these at; the font is hinted for it, so
        // the strokes land on whole pixels instead of being resampled.
        b->setStyleSheet(QString(
            "QToolButton { border:none; background:transparent; color:#FFFFFF;"
            "  font-family:'Segoe Fluent Icons','Segoe MDL2 Assets';"
            "  font-size:10px; }"
            "QToolButton:hover { background:%1; color:#FFFFFF; }"
            "QToolButton:pressed { background:%2; }")
            .arg(danger ? "#E81123" : "rgba(255,255,255,0.14)",
                 danger ? "#C50F1F" : "rgba(255,255,255,0.22)"));
        return b;
    }

    QWidget*                m_target { nullptr };
    QToolButton*            m_min    { nullptr };
    QToolButton*            m_max    { nullptr };
    QToolButton*            m_close  { nullptr };
    QGraphicsOpacityEffect* m_fade   { nullptr };
    QPropertyAnimation*     m_anim   { nullptr };
    bool                    m_revealed { false };
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

        // ── Top strip ────────────────────────────────────────────────────────
        // This is now the very first row of the window: the OS title bar is
        // suppressed in nativeEvent, so the tab strip IS the title bar. It holds
        // the scrollable tab region, the "+" button, a draggable gap, and the
        // window buttons pinned right.
        m_topBar = new QWidget(central);
        m_topBar->setObjectName("shellTopBar");
        m_topBar->setStyleSheet("#shellTopBar { background:#0D1117; }");
        m_topBar->setAttribute(Qt::WA_Hover, true);
        m_topBar->installEventFilter(this);
        // Two rows: the tabs, and a scrollbar underneath that exists only when
        // the tabs actually overflow. The first version reserved that second row
        // permanently, which left a dark 4px band sitting directly under the
        // selected tab's violet underline. That band was the "black line".
        auto* topV = new QVBoxLayout(m_topBar);
        topV->setContentsMargins(0, 0, 0, 0);
        topV->setSpacing(0);
        auto* row = new QWidget(m_topBar);
        row->setStyleSheet("background:#0D1117;");
        auto* hb = new QHBoxLayout(row);
        hb->setContentsMargins(0, 0, 0, 0);
        hb->setSpacing(0);

        // Tabs live inside a scroll area rather than using QTabBar's own arrow
        // buttons, so overflow reads as a scrollable list with a slim bar under
        // it instead of two little chevrons.
        m_scroll = new QScrollArea(m_topBar);
        m_scroll->setFrameShape(QFrame::NoFrame);
        m_scroll->setWidgetResizable(false);
        m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_scroll->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        m_scroll->viewport()->setStyleSheet("background:#0D1117;");
        m_scroll->setStyleSheet(
            "QScrollArea { background:#0D1117; border:none; }"
            "QScrollBar:horizontal { height:4px; background:#0D1117; margin:0; border:none; }"
            "QScrollBar::handle:horizontal { background:#39414F; border-radius:2px; min-width:36px; }"
            "QScrollBar::handle:horizontal:hover { background:#525C6E; }"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; height:0; }"
            "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background:transparent; }");

        // Exactly the tab height. The scrollbar is a separate widget on the row
        // below, so nothing is reserved and the strip is only ever as tall as
        // the tabs unless there is genuinely something to scroll.
        m_scroll->setFixedHeight(ShellTabBar::kTabHeight);
        m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        m_bar = new ShellTabBar(m_scroll);
        m_scroll->setWidget(m_bar);

        m_fade = new TabFade(m_scroll->viewport());
        m_scroll->viewport()->installEventFilter(this);
        connect(m_scroll->horizontalScrollBar(), &QScrollBar::valueChanged,
                this, [this](int) { refreshFade(); });
        connect(m_scroll->horizontalScrollBar(), &QScrollBar::rangeChanged,
                this, [this](int, int) { refreshFade(); });

        auto* plus = new QToolButton(m_topBar);
        plus->setText("+");
        plus->setAutoRaise(true);
        plus->setToolTip("New tab (Ctrl+T)");
        plus->setCursor(Qt::PointingHandCursor);
        plus->setFixedHeight(ShellTabBar::kTabHeight);
        plus->setFocusPolicy(Qt::NoFocus);
        plus->setStyleSheet(
            "QToolButton { font-size:17px; font-weight:600; color:#A78BFA;"
            "  background:#0D1117; padding:0 13px; border:none; }"
            "QToolButton:hover { color:#C4B5FD; background:#17233B; }");
        connect(plus, &QToolButton::clicked, this, [this]() { openHomeTab(); });
        m_plus = plus;

        // Use AI. Sits at the right of the strip, before the window buttons:
        // far enough from the tabs that it is never hit by accident, and on the
        // one row that is visible in every mode.
        m_useAi = new QToolButton(m_topBar);
        m_useAi->setToolTip(QStringLiteral("Open Stasis  (Ctrl+A+I)"));
        m_useAi->setCursor(Qt::PointingHandCursor);
        m_useAi->setFocusPolicy(Qt::NoFocus);
        m_useAi->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        // Sized and centred rather than stretched to the full strip height: a
        // chip as tall as a tab read as a slab with its corners cut off, and
        // the old stasis mark came out as a grey squiggle beside white text.
        m_useAi->setFixedSize(150, 28);
        m_useAi->setIconSize(QSize(15, 15));
        m_useAi->setIcon(QIcon(NativeOffice::Lucide::pixmap(
            NativeOffice::Lucide::kSparkles, QStringLiteral("#FFFFFF"), 15,
            devicePixelRatio())));
        // The same violet chip Home shows in its own top bar, so the button
        // does not change shape depending on which tab you are looking at.
        m_useAi->setText(QStringLiteral(" AI Assistant "));
        m_useAi->setStyleSheet(
            "QToolButton { color:#FFFFFF; font:600 12px 'Segoe UI';"
            "  background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "    stop:0 #6D5BF0, stop:1 #9B6BF6);"
            "  border:none; border-radius:9px; padding:0 10px; }"
            "QToolButton:hover { background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "    stop:0 #7C6CF6, stop:1 #AC7DFF); }"
            "QToolButton:pressed { background:#6455D8; }");
        connect(m_useAi, &QToolButton::clicked, this, &MainShell::toggleAiSidebar);

        m_controls = new WindowControls(this, m_topBar);

        // Top row of the strip; the scrollbar the QScrollArea adds appears
        // directly beneath the tabs, inside m_scroll itself.
        hb->addWidget(m_scroll, 0, Qt::AlignTop);
        hb->addWidget(plus, 0, Qt::AlignTop);
        hb->addStretch(1);             // draggable gap
        hb->addWidget(m_useAi, 0, Qt::AlignVCenter);
        hb->addWidget(m_controls, 0, Qt::AlignTop);

        // The visible scrollbar, mirroring the scroll area's own hidden one.
        // Hidden entirely when everything fits, so it costs no height at all.
        m_hbar = new QScrollBar(Qt::Horizontal, m_topBar);
        m_hbar->setFixedHeight(4);
        m_hbar->hide();
        m_hbar->setStyleSheet(
            "QScrollBar:horizontal { height:4px; background:#0D1117; margin:0; border:none; }"
            "QScrollBar::handle:horizontal { background:#39414F; border-radius:2px; min-width:40px; }"
            "QScrollBar::handle:horizontal:hover { background:#5A657A; }"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; height:0; }"
            "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background:transparent; }");
        connect(m_hbar, &QScrollBar::valueChanged, this, [this](int v) {
            m_scroll->horizontalScrollBar()->setValue(v);
        });
        connect(m_scroll->horizontalScrollBar(), &QScrollBar::valueChanged,
                this, [this](int v) { if (m_hbar->value() != v) m_hbar->setValue(v); });

        topV->addWidget(row, 0);
        topV->addWidget(m_hbar, 0);

        m_stack = new QStackedWidget(central);

        // The assistant sits beside the suite rather than over it. A splitter
        // is what makes that true in both directions: opening the panel narrows
        // the document instead of covering it, and dragging the divider resizes
        // both at once, which is the whole point of putting it here.
        m_split = new QSplitter(Qt::Horizontal, central);
        m_split->setObjectName("shellSplit");
        m_split->setChildrenCollapsible(false);
        m_split->setHandleWidth(1);
        m_split->setStyleSheet(
            "QSplitter#shellSplit::handle { background:rgba(255,255,255,0.10); }"
            "QSplitter#shellSplit::handle:hover { background:#7C5CFF; }");
        m_split->addWidget(m_stack);

        m_slideAgent = new NativeOffice::AiSlideAgent(this);
        m_sheetAgent = new NativeOffice::AiSheetAgent(this);
        m_ai = new NativeOffice::AiSidebar(m_split);
        m_ai->hide();
        m_split->addWidget(m_ai);
        m_split->setStretchFactor(0, 1);   // the suite takes the slack
        m_split->setStretchFactor(1, 0);   // the panel keeps the width it is given
        connect(m_ai, &NativeOffice::AiSidebar::closeRequested,
                this, &MainShell::closeAiSidebar);

        v->addWidget(m_topBar, 0);
        v->addWidget(m_split, 1);
        setCentralWidget(central);

        connect(m_bar, &QTabBar::currentChanged, this, [this](int i) {
            m_stack->setCurrentIndex(i);
            ensureTabVisible(i);
            syncAiMode();
            syncAiButtonVisibility();
        });
        connect(m_bar, &QTabBar::tabCloseRequested,
                this, &MainShell::handleTabClose);

        installShortcuts();
        startHoverWatch();
        startAutoSave();
    }

    // One timer for the whole application rather than one per document.
    // Each tick is a docDirty() check per open tab, which costs nothing when
    // nothing is being edited, and the writes themselves are rate limited
    // inside EditorWindow::autoSaveTick. The same tick refreshes the "Autosaved
    // 7s ago" text so the wording keeps counting up between saves.
    void startAutoSave() {
        auto* t = new QTimer(this);
        t->setInterval(1000);
        connect(t, &QTimer::timeout, this, [this] {
            for (int i = 0; i < m_stack->count(); ++i) {
                auto* win = qobject_cast<EditorWindow*>(m_stack->widget(i));
                if (!win) continue;
                win->autoSaveTick();
                // Only the visible tab's bar can be seen, so only it is updated.
                if (i == m_stack->currentIndex()) {
                    if (auto* bb = win->findChild<NativeOffice::BrandBar*>())
                        bb->setAutoSaveStatus(win->autoSaveStatusText());
                }
            }
        });
        t->start();
    }

    // ── Tab keyboard commands ───────────────────────────────────────────────
    // ApplicationShortcut because focus is almost always deep inside an editor
    // widget when these are pressed. None of them collide with an editor
    // binding; plain Tab and Shift+Tab stay with the document (Writer uses them
    // for list indenting) and are untouched here.
    void installShortcuts() {
        auto add = [this](QKeySequence seq, void (MainShell::*slot)()) {
            auto* sc = new QShortcut(seq, this);
            sc->setContext(Qt::ApplicationShortcut);
            connect(sc, &QShortcut::activated, this, slot);
        };
        add(QKeySequence(Qt::CTRL | Qt::Key_W),                  &MainShell::closeCurrentTab);
        add(QKeySequence(Qt::CTRL | Qt::Key_F4),                 &MainShell::closeCurrentTab);
        add(QKeySequence(Qt::CTRL | Qt::Key_T),                  &MainShell::openHomeTab);
        add(QKeySequence(Qt::CTRL | Qt::Key_Tab),                &MainShell::nextTab);
        add(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab),    &MainShell::prevTab);
        add(QKeySequence(Qt::CTRL | Qt::Key_PageDown),           &MainShell::nextTab);
        add(QKeySequence(Qt::CTRL | Qt::Key_PageUp),             &MainShell::prevTab);
        add(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T),      &MainShell::reopenClosedTab);

        // Ctrl+A+I is handled in eventFilter rather than here, for the reason
        // written there. The filter has to be on the application, not on this
        // window, or the key never arrives while a document has focus.
        qApp->installEventFilter(this);

        // Ctrl+1..8 jump to that tab, Ctrl+9 to the last one, matching browsers.
        for (int n = 1; n <= 9; ++n) {
            auto* sc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key(Qt::Key_0 + n)), this);
            sc->setContext(Qt::ApplicationShortcut);
            connect(sc, &QShortcut::activated, this, [this, n] {
                const int idx = (n == 9) ? m_bar->count() - 1 : n - 1;
                if (idx >= 0 && idx < m_bar->count()) m_bar->setCurrentIndex(idx);
            });
        }
    }

    // A Home tab carries its own "AI Assistant" chip in its top bar, so the
    // strip's copy would be a second button for the same thing two rows apart.
    // It shows only where Home's does not: inside an editor.
    void syncAiButtonVisibility() {
        if (!m_useAi) return;
        const int i = m_bar->currentIndex();
        const bool onHome = i >= 0 && i < m_stack->count()
                            && qobject_cast<NativeOffice::StartScreen*>(m_stack->widget(i));
        m_useAi->setVisible(!onHome);
    }

public slots:
    // Opening the assistant from a Home card / chip. Same gate as the strip's
    // own button, so the consent notice is never bypassed.
    void requestAiSidebar() { toggleAiSidebar(); }

    // ── Stasis assistant ─────────────────────────────────────────────────────
    // Use AI is a gate before it is a toggle. Until the notice has been
    // accepted the panel does not open at all, and every press re-asks rather
    // than silently doing nothing, so the way back in is always the button the
    // user already pressed.
    void toggleAiSidebar() {
        if (m_ai->isVisible()) { closeAiSidebar(); return; }
        if (!NativeOffice::AiConsent::accepted()) {
            if (!NativeOffice::AiConsentDialog::runFor(this)) return;   // declined
        }
        openAiSidebar();
    }

    void openAiSidebar() {
        if (m_ai->isVisible()) { m_ai->focusComposer(); return; }
        // The hero belongs to a run of the app, not to a click: the first time
        // the panel opens in this process it starts clean, and reopening it
        // later in the same run keeps the conversation.
        if (!m_aiSessionStarted) {
            m_ai->startNewSession();
            m_aiSessionStarted = true;
        }
        syncAiMode();
        m_ai->show();

        const int total = m_split->width();
        const int panel = qBound(320, total / 4, 460);
        m_split->setSizes({ qMax(320, total - panel), panel });
        m_ai->focusComposer();
    }

    void closeAiSidebar() { m_ai->hide(); }

    // Tells the panel which surface it is looking at, which is what the
    // "In <mode>" chip reports and what decides whether it may edit or only
    // answer. Driven off the tab stack rather than tracked by each window, so a
    // new document type cannot forget to announce itself.
    void syncAiMode() {
        if (!m_ai) return;
        QWidget* w = m_stack->currentWidget();
        using NativeOffice::AiMode;
        AiMode m = AiMode::Home;
        QTextEdit* target = nullptr;
        NativeOffice::AiStreamTarget* deck = nullptr;
        if (auto* ww = qobject_cast<WriterWindow*>(w)) {
            m = AiMode::Writer;
            // The one surface the agent can write into today. Handed over
            // explicitly rather than found with findChild, which would just as
            // happily return the comments pane.
            if (ww->writer()) target = ww->writer()->editor();
        }
        else if (auto* cw = qobject_cast<CalcWindow*>(w)) {
            m = AiMode::Calc;
            m_sheetAgent->setTarget(cw->calc());
            deck = m_sheetAgent;
        }
        else if (auto* iw = qobject_cast<ImpressWindow*>(w)) {
            m = AiMode::Impress;
            // One agent, re-pointed at whichever deck is in front. Building a
            // new one per tab would lose the rollback record the moment the
            // user glanced at another tab and came back.
            m_slideAgent->setTarget(iw->impress());
            deck = m_slideAgent;
        }
        else if (qobject_cast<PdfWindow*>(w))            m = AiMode::Pdf;
        else if (qobject_cast<ImageResizerWindow*>(w))   m = AiMode::ImageResizer;
        else if (qobject_cast<MarkdownEditorWindow*>(w)) m = AiMode::MarkdownEditor;
        m_ai->setMode(m);
        m_ai->setDocumentTarget(target);
        m_ai->setDeckTarget(deck);
    }

    void closeCurrentTab() { handleTabClose(m_bar->currentIndex()); }

    void nextTab() {
        if (m_bar->count() < 2) return;
        m_bar->setCurrentIndex((m_bar->currentIndex() + 1) % m_bar->count());
    }
    void prevTab() {
        if (m_bar->count() < 2) return;
        m_bar->setCurrentIndex((m_bar->currentIndex() - 1 + m_bar->count()) % m_bar->count());
    }

    // Reopens the most recently closed tab. Only documents that had been saved
    // to disk can come back, because that path is the whole of what we kept; an
    // untitled buffer has nothing to reopen from and is skipped rather than
    // resurrected empty, which would be worse than doing nothing.
    void reopenClosedTab() {
        while (!m_closed.isEmpty()) {
            const QString path = m_closed.takeLast();
            if (path.isEmpty() || !QFileInfo::exists(path)) continue;
            openPathInNewTab(path);
            return;
        }
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
        // currentChanged does not fire when the index is already 0.
        syncAiButtonVisibility();
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

        win->syncNameFromPath();       // adopt the file's name if already open
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
        win->syncNameFromPath();

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

        syncTabWidth();
        m_bar->setCurrentIndex(idx);
        m_stack->setCurrentIndex(idx);
        ensureTabVisible(idx);
        return idx;
    }

    // Keep the tab label in sync with the document's name + save/dirty state.
    void watchTitle(EditorWindow* win) {
        auto refresh = [this, win] {
            const int i = m_stack->indexOf(win);
            // A renamed tab is a differently sized tab, so the scroll range has
            // to be recomputed or the last tab drifts out of reach.
            if (i >= 0) { m_bar->setTabText(i, labelFor(win)); syncTabWidth(); }
        };
        connect(win, &QWidget::windowTitleChanged, this,
                [win, refresh](const QString&) { win->syncNameFromPath(); refresh(); });
        connect(win, &EditorWindow::displayNameChanged, this, refresh);
    }

    // Tab label: tools show their kind name ("Image Resizer"); documents show
    // "untitled <kind>" until named, then the base name, with a "* " prefix
    // while a saved file has unsaved edits.
    QString labelFor(EditorWindow* win) const {
        if (win->kindWord().isEmpty())
            return elide(win->docKindName());
        if (!win->isNamed())                       // fresh doc: no dirty marker
            return elide(QStringLiteral("untitled ") + win->kindWord());
        const QString star = win->docDirty() ? QStringLiteral("* ") : QString();
        return star + elide(win->baseName());
    }

    static QString elide(QString s) {
        if (s.size() > 26) s = s.left(25) + QChar(0x2026);
        return s;
    }

    void handleTabClose(int idx) {
        if (idx <= 0) return;                      // Home is pinned
        QWidget* w = m_stack->widget(idx);
        if (!w) return;
        auto* win = qobject_cast<EditorWindow*>(w);
        // requestClose() is what raises the "save your changes?" prompt, so
        // Ctrl+W and the close button both go through it and neither can drop
        // unsaved work.
        if (win && !win->requestClose()) return;   // user cancelled

        // Remember where it came from so Ctrl+Shift+T can bring it back.
        if (win) {
            const QString path = win->currentDocPath();
            if (!path.isEmpty()) {
                m_closed.removeAll(path);
                m_closed.append(path);
                if (m_closed.size() > 20) m_closed.removeFirst();
            }
        }

        m_bar->removeTab(idx);
        m_stack->removeWidget(w);
        w->deleteLater();
        syncTabWidth();
    }

    // The tab bar sits inside a scroll area that does not resize its widget, so
    // the bar has to be told to match its own content width whenever the set of
    // tabs changes. Without this the scroll range never grows and later tabs
    // simply cannot be reached.
    void syncTabWidth() {
        if (!m_bar || !m_scroll || !m_plus) return;
        const int contentW = m_bar->sizeHint().width();
        m_bar->resize(contentW, ShellTabBar::kTabHeight);

        // The viewport has to be told how wide to be. A QScrollArea whose widget
        // is not resizable falls back to a generic sizeHint of a couple of
        // hundred pixels, which made two tabs overflow instantly and scrolled
        // the pinned Home tab out of sight. Track the content instead, up to the
        // 80% cap, so the bar is only ever scrollable when it genuinely is.
        const int cap = qMax(200, int(width() * 0.80) - m_plus->sizeHint().width());
        m_scroll->setFixedWidth(qMin(contentW, cap));
        refreshFade();
    }

    void refreshFade() {
        if (!m_fade || !m_scroll) return;
        auto* sb = m_scroll->horizontalScrollBar();
        m_fade->setGeometry(m_scroll->viewport()->rect());
        m_fade->setOverflow(sb->value() > sb->minimum(), sb->value() < sb->maximum());
        m_fade->raise();

        // Mirror the real range onto the visible bar, and take it out of the
        // layout completely when there is nothing to scroll.
        if (m_hbar) {
            const bool need = sb->maximum() > sb->minimum();
            m_hbar->setRange(sb->minimum(), sb->maximum());
            m_hbar->setPageStep(sb->pageStep());
            m_hbar->setValue(sb->value());
            m_hbar->setVisible(need);
        }
    }

    // Keep the selected tab on screen when it is reached by keyboard.
    //
    // Deferred by a turn of the event loop on purpose: called straight out of
    // addPage the viewport has not been laid out yet and reports a width of
    // zero, so every tab looks off-screen and the bar scrolls itself to the end.
    void ensureTabVisible(int idx) {
        QTimer::singleShot(0, this, [this, idx] {
            if (!m_scroll || idx < 0 || idx >= m_bar->count()) return;
            const int vw = m_scroll->viewport()->width();
            if (vw <= 0) return;
            const QRect r = m_bar->tabRect(idx);
            auto* sb = m_scroll->horizontalScrollBar();
            if (r.left() < sb->value())            sb->setValue(r.left());
            else if (r.right() > sb->value() + vw) sb->setValue(r.right() - vw);
        });
    }

    // Tabs are capped at 80% of the window. The rest is deliberately left free:
    // it is the drag handle for the window and the home of the window buttons,
    // and tabs running the full width would leave nowhere to grab.
    void updateTabRegionWidth() { syncTabWidth(); }

    // Drives the show/hide of the window buttons purely from where the cursor
    // is.
    //
    // Enter/Leave events are not usable for this. The draggable part of the
    // strip hit-tests as HTCAPTION, so Windows delivers movement over it as
    // non-client messages that Qt never turns into an Enter, and the buttons
    // stayed hidden over exactly the area meant to reveal them. Sampling the
    // cursor four times a second sidesteps the client/non-client split entirely
    // and costs a point comparison.
    void startHoverWatch() {
        m_hoverPoll = new QTimer(this);
        m_hoverPoll->setInterval(250);
        connect(m_hoverPoll, &QTimer::timeout, this, [this] {
            if (!m_controls || !m_topBar || !isVisible()) return;
            const bool inside = m_topBar->rect().contains(
                m_topBar->mapFromGlobal(QCursor::pos()));
            m_controls->setRevealed(inside);
        });
        m_hoverPoll->start();
    }

    void revealControls() { if (m_controls) m_controls->setRevealed(true); }

protected:
    void resizeEvent(QResizeEvent* e) override {
        QMainWindow::resizeEvent(e);
        updateTabRegionWidth();
    }

    void changeEvent(QEvent* e) override {
        QMainWindow::changeEvent(e);
        if (e->type() == QEvent::WindowStateChange && m_controls)
            m_controls->refreshMaxIcon();
    }

    // Is the A key down at this instant? Ctrl+I is Italic in Writer, so the
    // chord must not fire on a stale flag: a tracked bool can be left set when
    // the key release lands somewhere that consumes it, and stealing Italic
    // would be a far worse bug than a shortcut that occasionally needs a second
    // press. Windows can answer definitively, so it is asked.
    bool aKeyPhysicallyDown() const {
#ifdef Q_OS_WIN
        return (GetAsyncKeyState('A') & 0x8000) != 0;
#else
        return m_aHeld;   // no equivalent query; the tracked flag is all there is
#endif
    }

    bool eventFilter(QObject* o, QEvent* e) override {
        // Ctrl+A+I, as a genuinely held combination. It cannot be a QShortcut:
        // "Ctrl+A, I" would be a two-step sequence and would have to swallow
        // every Ctrl+A in the app while waiting for the second key, which would
        // break Select All everywhere. Watching the A key's own press and
        // release costs nothing and leaves Ctrl+A alone.
        if (e->type() == QEvent::KeyPress) {
            auto* k = static_cast<QKeyEvent*>(e);
            if (k->key() == Qt::Key_A) m_aHeld = true;
            else if (k->key() == Qt::Key_I && (k->modifiers() & Qt::ControlModifier)
                     && aKeyPhysicallyDown()) {
                toggleAiSidebar();
                return true;
            }
        } else if (e->type() == QEvent::KeyRelease) {
            auto* k = static_cast<QKeyEvent*>(e);
            if (k->key() == Qt::Key_A) m_aHeld = false;
        } else if (e->type() == QEvent::WindowDeactivate) {
            m_aHeld = false;      // a key released while we were away is missed
        }

        if (o == m_topBar) {
            // Reveal the window buttons for the whole strip rather than only
            // their own 132px. They are invisible until hovered, so requiring
            // the pointer to find them exactly would make them undiscoverable.
            if (e->type() == QEvent::Enter || e->type() == QEvent::HoverEnter ||
                e->type() == QEvent::HoverMove)
                revealControls();
        } else if (m_scroll && o == m_scroll->viewport() && e->type() == QEvent::Resize) {
            refreshFade();
        }
        return QMainWindow::eventFilter(o, e);
    }

#ifdef Q_OS_WIN
    // Removes the OS title bar while keeping everything Windows gives a normal
    // window: resize borders, Aero snap, Snap Layouts and double-click to
    // maximise. That is the reason this is done by answering WM_NCCALCSIZE
    // rather than with Qt::FramelessWindowHint, which strips WS_THICKFRAME and
    // takes all of those away with it.
    bool nativeEvent(const QByteArray& type, void* message, qintptr* result) override {
        MSG* msg = static_cast<MSG*>(message);
        if (!msg || !msg->hwnd) return QMainWindow::nativeEvent(type, message, result);
        // The handle comes from the message, never from winId(): WM_NCCALCSIZE
        // arrives while the native window is still being created, and winId()
        // forces creation, so calling it here re-enters that construction and
        // tears the window down before it is ever shown.
        HWND hwnd = msg->hwnd;

        switch (msg->message) {
        case WM_NCCALCSIZE: {
            if (msg->wParam == TRUE) {
                auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
                // A maximised window is intentionally sized larger than the work
                // area by the frame thickness. Without putting that back, the top
                // of the tab strip would sit off the top of the screen.
                if (::IsZoomed(hwnd)) {
                    const int fx = ::GetSystemMetrics(SM_CXSIZEFRAME)
                                 + ::GetSystemMetrics(SM_CXPADDEDBORDER);
                    const int fy = ::GetSystemMetrics(SM_CYSIZEFRAME)
                                 + ::GetSystemMetrics(SM_CXPADDEDBORDER);
                    p->rgrc[0].left   += fx;
                    p->rgrc[0].right  -= fx;
                    p->rgrc[0].top    += fy;
                    p->rgrc[0].bottom -= fy;
                }
                *result = 0;
                return true;      // client area now covers the whole window
            }
            break;
        }
        // The draggable part of the strip hit-tests as HTCAPTION, which makes
        // Windows deliver movement there as NON-client mouse messages. Qt never
        // turns those into an Enter event, so the hover reveal below would never
        // fire over the very area it exists for. Catching it here covers the
        // caption; the Qt hover in eventFilter covers the client parts.
        case WM_NCMOUSEMOVE: {
            revealControls();
            break;                 // let Qt and DefWindowProc still see it
        }
        case WM_NCHITTEST: {
            // Qt keeps QCursor::pos() in logical pixels, which mapFromGlobal
            // expects; taking the coordinates out of lParam instead would be
            // wrong on any display that is not at 100% scaling.
            const QPoint local = mapFromGlobal(QCursor::pos());
            const int bw = 6;     // resize border thickness
            const bool zoomed = isMaximized();

            if (!zoomed) {
                const bool L = local.x() >= 0 && local.x() < bw;
                const bool R = local.x() < width()  && local.x() >= width()  - bw;
                const bool T = local.y() >= 0 && local.y() < bw;
                const bool B = local.y() < height() && local.y() >= height() - bw;
                if (T && L) { *result = HTTOPLEFT;     return true; }
                if (T && R) { *result = HTTOPRIGHT;    return true; }
                if (B && L) { *result = HTBOTTOMLEFT;  return true; }
                if (B && R) { *result = HTBOTTOMRIGHT; return true; }
                if (L)      { *result = HTLEFT;        return true; }
                if (R)      { *result = HTRIGHT;       return true; }
                if (T)      { *result = HTTOP;         return true; }
                if (B)      { *result = HTBOTTOM;      return true; }
            }

            // Anything in the strip that is not itself clickable drags the
            // window. childAt() returns the deepest interactive child, so tabs,
            // the "+" and the window buttons keep their own behaviour.
            if (m_topBar && local.y() < m_topBar->height()) {
                QWidget* child = m_topBar->childAt(m_topBar->mapFrom(this, local));
                if (!child || child == m_topBar) { *result = HTCAPTION; return true; }
            }
            break;
        }
        default: break;
        }
        return QMainWindow::nativeEvent(type, message, result);
    }
#endif

private:
    ShellTabBar*               m_bar   { nullptr };
    QStackedWidget*            m_stack { nullptr };
    QWidget*                   m_topBar { nullptr };
    QScrollArea*               m_scroll { nullptr };
    TabFade*                   m_fade   { nullptr };
    QScrollBar*                m_hbar   { nullptr };
    QToolButton*               m_plus   { nullptr };
    QToolButton*               m_useAi  { nullptr };
    QSplitter*                 m_split  { nullptr };
    NativeOffice::AiSidebar*   m_ai     { nullptr };
    NativeOffice::AiSlideAgent* m_slideAgent { nullptr };
    NativeOffice::AiSheetAgent* m_sheetAgent { nullptr };
    bool                       m_aiSessionStarted { false };
    // Ctrl+A+I is a held combination rather than a key sequence, so the A has
    // to be tracked by hand; see nativeEvent-adjacent filter in installShortcuts.
    bool                       m_aHeld  { false };
    WindowControls*            m_controls { nullptr };
    QTimer*                    m_hoverPoll { nullptr };
    QStringList                m_closed;          // recently closed file paths
    std::function<QWidget*()>  m_homeFactory;

public:
    // Set by main(): reopening a closed tab has to go back through the same
    // file router the rest of the app uses, and that lives further down.
    void setOpenPathHook(std::function<void(const QString&)> f) {
        m_openPath = std::move(f);
    }

private:
    void openPathInNewTab(const QString& path) { if (m_openPath) m_openPath(path); }
    std::function<void(const QString&)> m_openPath;
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
    // Premium "PDF export quality"; 300 dpi for everyone else, as before.
    pdfWriter.setResolution(NativeOffice::ExportPrefs::pdfExportDpi());

    // ── Print via QTextDocument::print() ─────────────────────────────────────
    // QTextDocument::print() automatically handles pagination, rich text,
    // inline images, fonts, and colors — all at the target device resolution.
    writer->document()->print(&pdfWriter);

    // The PDF is complete on disk at this point; the mark and its link go on
    // afterwards because QPdfWriter has no way to express an annotation.
    NativeOffice::Watermark::stampIfRequired(path);

    QMessageBox::information(parent, "Export Complete",
        "Document exported to PDF successfully!\n\n" + path);
}

// Free accounts used to open every module read-only. That gate is gone: the
// free tier now edits without restriction and the plan difference shows up on
// export instead, as the "Made with NativeOffice" mark (core/watermark).
//
// Each module keeps its own setReadOnly() — it still drives Writer's Restrict
// Editing and Read Mode, which are user-chosen states, not licensing.

// ─────────────────────────────────────────────────────────────────────────────
// Helper: build the Writer editor window with full menu bar
// ─────────────────────────────────────────────────────────────────────────────
static WriterWindow* createWriterWindow(const QString& filePath) {
    auto* win    = new WriterWindow;
    auto* writer = new NativeOffice::WriterModule;
    win->setWriter(writer);

    // Centre on screen
    fitWindowToScreen(win);

    // ── Load file if provided ─────────────────────────────────────────────
    if (!filePath.isEmpty()) {
        if (!writer->loadFromPath(filePath)) {
            QMessageBox::critical(win, "Open Failed",
                "Could not read:\n" + filePath);
        } else {
            // Bump to top of recent list
            NativeOffice::RecentFilesManager::instance().addFile(filePath, "Writer");
            NativeOffice::UsageStats::instance().noteDocumentOpened();
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
    fitWindowToScreen(win);

    // ── Load file if provided ─────────────────────────────────────────────
    if (!filePath.isEmpty()) {
        if (!calc->loadFromPath(filePath)) {
            QMessageBox::critical(win, "Open Failed",
                "Could not read:\n" + filePath);
        } else {
            NativeOffice::RecentFilesManager::instance().addFile(filePath, "Calc");
            NativeOffice::UsageStats::instance().noteDocumentOpened();
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
    fitWindowToScreen(win);

    // ── Load file if provided ─────────────────────────────────────────────
    if (!filePath.isEmpty()) {
        if (!impress->loadFromPath(filePath)) {
            QMessageBox::critical(win, "Open Failed",
                "Could not read:\n" + filePath);
        } else {
            NativeOffice::RecentFilesManager::instance().addFile(filePath, "Impress");
            NativeOffice::UsageStats::instance().noteDocumentOpened();
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
            "<p>Version 1.7.3</p>"
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

    fitWindowToScreen(win);

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
        // Laid out as the card shows it: a header block, then two columns,
        // with the skill strengths drawn as bars rather than described. Tables
        // are the only way to get columns in a QTextDocument, so the structure
        // is a table even though it does not read as one.
        auto bar = [&accent](const QString& skill, int percent) {
            return QString(
                "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\" "
                "style=\"margin-bottom:6px;\"><tr>"
                "<td style=\"font-size:9pt; color:#3C4250;\">%1</td></tr><tr>"
                "<td><table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\"><tr>"
                "<td width=\"%2%\" bgcolor=\"%3\" style=\"font-size:3pt;\">&nbsp;</td>"
                "<td width=\"%4%\" bgcolor=\"#E4E8F0\" style=\"font-size:3pt;\">&nbsp;</td>"
                "</tr></table></td></tr></table>")
                .arg(skill).arg(percent).arg(accent).arg(100 - percent);
        };

        return QString(R"(<table width="100%" cellspacing="0" cellpadding="0"><tr>
<td><h1 style="%1 color:%2; margin:0;">Your Name</h1>
<p style="%1 color:%2; margin:2px 0 0 0; font-size:12pt;"><b>Professional Title</b></p></td>
<td align="right" style="%1 color:#5A6071; font-size:9pt;">youremail@example.com<br/>
+1 234 567 890<br/>City, Country</td></tr></table>
<hr/>
<table width="100%" cellspacing="0" cellpadding="8"><tr valign="top">
<td width="58%">
<h2 style="%1 color:%2; margin-top:0;">Summary</h2>
<p style="%1">Results-driven professional with X years of experience in ____.
Known for ____ and ____.</p>
<h2 style="%1 color:%2;">Experience</h2>
<p style="%1"><b>Job Title, Company</b><br/><i style="color:#6B7280;">20XX to Present</i></p>
<ul><li style="%1">Achievement with a measurable result (grew X by Y%)</li>
<li style="%1">Responsibility that shows scope and ownership</li>
<li style="%1">Tool, process or initiative you led</li></ul>
<p style="%1"><b>Previous Title, Company</b><br/><i style="color:#6B7280;">20XX to 20XX</i></p>
<ul><li style="%1">Key contribution</li><li style="%1">Key contribution</li></ul>
</td>
<td width="42%" bgcolor="#F5F7FB">
<h2 style="%1 color:%2; margin-top:0;">Education</h2>
<p style="%1 font-size:10pt;"><b>Degree, Institution</b><br/>
<span style="color:#6B7280;">20XX</span></p>
<p style="%1 font-size:10pt;"><b>Certification</b><br/>
<span style="color:#6B7280;">20XX</span></p>
<h2 style="%1 color:%2;">Skills</h2>
%3%4%5%6
<h2 style="%1 color:%2;">Languages</h2>
<p style="%1 font-size:10pt;">Language one, native<br/>Language two, fluent<br/>
Language three, basic</p>
</td></tr></table>)")
            .arg(base, accent, bar(QStringLiteral("Skill one"), 90),
                 bar(QStringLiteral("Skill two"), 75),
                 bar(QStringLiteral("Skill three"), 60),
                 bar(QStringLiteral("Skill four"), 45));
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
        // A title band, a real timeline table and a status column, as the card
        // shows, rather than a run of headings.
        return QString(R"(<table width="100%" cellspacing="0" cellpadding="10">
<tr><td bgcolor="#1F3864">
<h1 style="%1 color:#FFFFFF; margin:0;">Project Name</h1>
<p style="%1 color:#C3CEE4; margin:4px 0 0 0; font-size:10pt;">Prepared by Your Name &nbsp;·&nbsp; %2</p>
</td></tr></table>
<h2 style="%1 color:#1F3864;">Project Overview</h2>
<p style="%1">Two or three sentences: what the project is, its current status,
and the headline result.</p>
<h2 style="%1 color:#1F3864;">Objectives</h2>
<ul><li style="%1">Objective one, with the measure that says it is met</li>
<li style="%1">Objective two</li><li style="%1">Objective three</li></ul>
<h2 style="%1 color:#1F3864;">Timeline</h2>
<table width="100%" cellspacing="0" cellpadding="6" border="1"
       style="border-collapse:collapse; border-color:#D5DAE3;">
<tr bgcolor="#1F3864">
<td style="%1 color:#FFFFFF;"><b>Phase</b></td>
<td style="%1 color:#FFFFFF;"><b>Dates</b></td>
<td style="%1 color:#FFFFFF;"><b>Owner</b></td>
<td style="%1 color:#FFFFFF;"><b>Status</b></td></tr>
<tr><td style="%1">1. Discovery</td><td style="%1">20XX-XX to 20XX-XX</td>
<td style="%1">Name</td><td style="%1 color:#147B45;"><b>Complete</b></td></tr>
<tr bgcolor="#F5F7FB"><td style="%1">2. Build</td><td style="%1">20XX-XX to 20XX-XX</td>
<td style="%1">Name</td><td style="%1 color:#B7791F;"><b>In progress</b></td></tr>
<tr><td style="%1">3. Launch</td><td style="%1">20XX-XX to 20XX-XX</td>
<td style="%1">Name</td><td style="%1 color:#6B7280;"><b>Not started</b></td></tr>
</table>
<h2 style="%1 color:#1F3864;">Risks and Issues</h2>
<table width="100%" cellspacing="0" cellpadding="6" border="1"
       style="border-collapse:collapse; border-color:#D5DAE3;">
<tr bgcolor="#F5F7FB"><td style="%1"><b>Risk</b></td><td style="%1"><b>Owner</b></td>
<td style="%1"><b>Mitigation</b></td></tr>
<tr><td style="%1">What could go wrong</td><td style="%1">Name</td>
<td style="%1">What is being done about it</td></tr>
</table>
<h2 style="%1 color:#1F3864;">Summary</h2>
<p style="%1">Overall assessment and recommendation.</p>)")
            .arg(base, QDate::currentDate().toString("MMMM d, yyyy"));
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
    // ── Monthly Budget ───────────────────────────────────────────────────────
    // Built cell by cell rather than from the plain rows table below, because
    // the card on the Home screen shows a designed sheet: a green header band,
    // money formatting, a pie of where it goes and a bar chart beside it. What
    // the card promises is what has to open.
    if (name == "Monthly Budget") {
        using NativeOffice::Cell;
        const QColor headerBg(0x14, 0x7B, 0x45);
        const QColor bandBg(0xE8, 0xF6, 0xEE);
        const QColor titleFg(0x0B, 0x4A, 0x2A);
        const QColor good(0x14, 0x7B, 0x45);
        const QColor over(0xC0, 0x39, 0x2B);

        std::vector<std::pair<QPoint, Cell>> edits;
        auto put = [&edits](int c, int r, const QString& text,
                            const std::function<void(NativeOffice::CellFormat&)>& style = {}) {
            Cell cell;
            cell.content = text;
            if (style) style(cell.format);
            edits.emplace_back(QPoint(c, r), cell);
        };

        put(0, 0, "Monthly Budget", [&](NativeOffice::CellFormat& f) {
            f.bold = true; f.fontSize = 18; f.textColor = titleFg;
        });
        put(0, 1, "Where the money goes this month", [&](NativeOffice::CellFormat& f) {
            f.italic = true; f.textColor = QColor(0x6B, 0x72, 0x80);
        });

        const char* heads[] = { "Category", "Budget", "Actual", "Difference" };
        for (int c = 0; c < 4; ++c)
            put(c, 2, QString::fromLatin1(heads[c]), [&](NativeOffice::CellFormat& f) {
                f.bold = true; f.textColor = QColor(Qt::white); f.bgColor = headerBg;
                f.hAlign = c == 0 ? Qt::AlignLeft : Qt::AlignRight;
            });

        struct Row { const char* label; int budget; int actual; bool moreIsBetter; };
        const Row rows[] = {
            { "Income",         5000, 5200, true  },
            { "Housing",        1500, 1450, false },
            { "Food",            800,  750, false },
            { "Transportation",  400,  380, false },
            { "Utilities",       300,  320, false },
            { "Entertainment",   200,  180, false },
            { "Savings",         800,  900, true  },
        };
        int r = 3;
        for (const Row& row : rows) {
            const int line = r + 1;                 // 1-based for formulas
            const bool banded = (r % 2) == 1;
            put(0, r, QString::fromLatin1(row.label), [&](NativeOffice::CellFormat& f) {
                if (banded) f.bgColor = bandBg;
            });
            put(1, r, QString::number(row.budget), [&](NativeOffice::CellFormat& f) {
                f.numberFormat = QStringLiteral("$#,##0");
                if (banded) f.bgColor = bandBg;
            });
            put(2, r, QString::number(row.actual), [&](NativeOffice::CellFormat& f) {
                f.numberFormat = QStringLiteral("$#,##0");
                if (banded) f.bgColor = bandBg;
            });
            // Under budget is good for spending, over target is good for income
            // and savings, so the sign is taken the way each row is read, and
            // the one row that went the wrong way is the one that shows red.
            const int delta = row.moreIsBetter ? row.actual - row.budget
                                               : row.budget - row.actual;
            put(3, r, row.moreIsBetter
                          ? QStringLiteral("=C%1-B%1").arg(line)
                          : QStringLiteral("=B%1-C%1").arg(line),
                [&](NativeOffice::CellFormat& f) {
                    f.numberFormat = QStringLiteral("$#,##0");
                    f.bold = true;
                    f.textColor = delta < 0 ? over : good;
                    if (banded) f.bgColor = bandBg;
                });
            ++r;
        }

        ++r;                                        // blank spacer row
        put(0, r, "Total out", [&](NativeOffice::CellFormat& f) {
            f.bold = true; f.textColor = QColor(Qt::white); f.bgColor = headerBg;
        });
        for (int c = 1; c <= 3; ++c) {
            // Built by concatenation: "%1" followed by a digit would be read as
            // argument 14, not argument 1 then a 4.
            const QString col = QString(QChar('A' + c));
            put(c, r, QStringLiteral("=SUM(") + col + QStringLiteral("5:")
                      + col + QStringLiteral("10)"),
                [&](NativeOffice::CellFormat& f) {
                    f.bold = true; f.textColor = QColor(Qt::white); f.bgColor = headerBg;
                    f.numberFormat = QStringLiteral("$#,##0");
                    f.hAlign = Qt::AlignRight;
                });
        }

        calc->setTemplateColumnWidths({ {0, 150}, {1, 90}, {2, 90}, {3, 100} });
        calc->model()->applyCellEdits(edits, QStringLiteral("Apply Template"));
        // The two charts the card shows: spending by category, and budget
        // against actual side by side. Both read the cells above, so editing a
        // number redraws them.
        // Both ranges start on the header row, so the chart names its series
        // from it rather than treating the first category as a heading.
        calc->addChartAt(NativeOffice::ChartType::Pie,
                         QRect(0, 2, 2, 8), QRect(520, 18, 430, 300));
        calc->addChartAt(NativeOffice::ChartType::Column,
                         QRect(0, 2, 3, 8), QRect(520, 336, 430, 300));

        calc->markClean();
        return;
    }

    if (name == "Invoice")
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

    // Markdown belongs to the Markdown Editor, not Writer: FileRouter classifies
    // it as plain text, which used to open it as a document and throw away the
    // live preview and the source pane the file is written for.
    if (path.endsWith(".md", Qt::CaseInsensitive)
        || path.endsWith(".markdown", Qt::CaseInsensitive))
        return createMarkdownEditorWindow(path);

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
    app.setApplicationVersion("1.7.3");
    app.setOrganizationName("NativeOffice");
    app.setOrganizationDomain("nativeoffice.app");

    // Window icon, set explicitly rather than left to Windows.
    //
    // Nothing called setWindowIcon before, so the taskbar button fell back to
    // the .exe resource icon. Setting the multi-resolution .ico here lets Qt
    // hand Windows the frame that matches each surface (16px window corner,
    // 32/40/48px taskbar depending on scaling, 256px alt-tab and thumbnails)
    // instead of one frame being squashed to fit them all. Every window and
    // dialog in the app inherits it.
    app.setWindowIcon(QIcon(":/assets/app.ico"));

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

    // Every other launch hands over to the running instance too, and exits.
    // Without this a second process started a whole second app, which is what
    // produced two windows after an update: the installer relaunches the app
    // on finish, so any other launch on top of that was one too many. It also
    // means double-clicking a file while the app is open adds a tab instead of
    // opening a rival copy of the same document.
    if (protocolUrl.isEmpty()) {
        const QString payload =
            cliFiles.isEmpty() ? NativeOffice::InstanceGuard::activatePayload()
                               : NativeOffice::InstanceGuard::openFilesPayload(cliFiles);
        if (guard.forwardToPrimary(payload)) return 0;
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
    // Ad-hoc dialogs across all four modules carried no stylesheet, so they
    // inherited the platform palette and rendered dark text on dark inputs for
    // anyone running Windows in dark mode. This gives every one of them a
    // readable surface without touching each call site.
    NativeOffice::ThemeManager::installDialogStyleGuard(&app);

    // ── Usage stats ────────────────────────────────────────────────────────
    // Backs Home's "Your Activity" panel, whose time-spent row was a literal
    // dash. Folded into settings every minute and again on quit, so a crash
    // loses at most a minute rather than the whole session.
    NativeOffice::UsageStats::instance().startSession();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [] {
        NativeOffice::UsageStats::instance().flush();
    });

    // ── App controller ─────────────────────────────────────────────────────
    NativeOffice::AppController controller;

    // ── Main shell window (WPS-style tabbed workflow) ──────────────────────
    MainShell shell;
    g_shell = &shell;

    shell.resize(1480, 900);
    fitWindowToScreen(&shell);

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
        QObject::connect(s, &NativeOffice::StartScreen::toolRequested,
                         s, [](NativeOffice::StartScreen::Tool tool) {
            presentEditor(createToolWindow(tool));
        });
        QObject::connect(s, &NativeOffice::StartScreen::aiRequested, s, [] {
            if (g_shell) g_shell->requestAiSidebar();
        });
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
        QObject::connect(s, &NativeOffice::StartScreen::toolRequested,
                         s, [s, &shell](NativeOffice::StartScreen::Tool tool) {
            shell.presentInHomeTab(s, createToolWindow(tool));
        });
        QObject::connect(s, &NativeOffice::StartScreen::aiRequested,
                         s, [&shell] { shell.requestAiSidebar(); });
        QObject::connect(s, &NativeOffice::StartScreen::settingsRequested,
                         &controller, &NativeOffice::AppController::openSettings);
        return s;
    };

    shell.setHomeFactory(makeEphemeralHome);
    // Ctrl+Shift+T reopens through the same router as every other file open,
    // so a restored tab is identical to one opened from Home or the shell.
    shell.setOpenPathHook([](const QString& path) { openDocumentByPath(path); });
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
    if (qEnvironmentVariableIsSet("NATIVEOFFICE_OPEN_MARKDOWN"))
        presentEditor(createMarkdownEditorWindow());
    if (qEnvironmentVariableIsSet("NATIVEOFFICE_OPEN_WRITER"))
        presentEditor(createWriterWindow());
    if (qEnvironmentVariableIsSet("NATIVEOFFICE_OPEN_CALC"))
        presentEditor(createCalcWindow());
    if (qEnvironmentVariableIsSet("NATIVEOFFICE_OPEN_IMPRESS"))
        presentEditor(createImpressWindow());

    auto& auth = NativeOffice::AuthManager::instance();
    // Bring the one real window to the front. Windows will not raise a window
    // for a process that does not own the foreground, so the minimised case is
    // handled explicitly rather than looking like the click did nothing.
    auto surfaceShell = [&shell] {
        if (shell.isMinimized()) shell.showNormal();
        shell.show();
        shell.raise();
        shell.activateWindow();
    };

    QObject::connect(&guard, &NativeOffice::InstanceGuard::urlReceived,
                     &auth, [&auth, surfaceShell](const QString&) {
        auth.pollNow();                   // browser says approval landed
        auth.refreshEntitlement();        // ...and may carry a fresh purchase/key
        surfaceShell();
    });

    // A second launch handed its work over instead of starting its own app.
    QObject::connect(&guard, &NativeOffice::InstanceGuard::activateRequested,
                     &shell, surfaceShell);
    QObject::connect(&guard, &NativeOffice::InstanceGuard::filesReceived,
                     &shell, [surfaceShell](const QStringList& paths) {
        for (const QString& p : paths) openDocumentByPath(p);
        surfaceShell();
    });

    // Premium sync without continuous polling: whenever the app regains focus
    // (e.g. the user just bought premium or redeemed a key on the website and
    // switched back), re-read entitlement from /api/me. Throttled to at most
    // once every 20s so rapid focus toggling can't hammer the backend. This is
    // the "website changes it, the app catches it on return" path — cheap on
    // bandwidth versus a background timer that runs while the user is away.
    QObject::connect(&app, &QApplication::applicationStateChanged, &auth,
                     [&auth](Qt::ApplicationState st) {
        if (st != Qt::ApplicationActive) return;
        static qint64 lastMs = 0;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (nowMs - lastMs < 20000) return;
        lastMs = nowMs;
        auth.refreshEntitlement();        // no-op when signed out
    });

    // ── Reveal flow (after the sign-in gate) ───────────────────────────────
    // When launched to open a specific document, go straight to its tab (no
    // splash). Otherwise show the WPS-style startup splash, then reveal the
    // shell on the Home tab once it finishes.
    // Kick the one-shot update scan the moment the home screen becomes visible,
    // so the "scanning for updates" box is actually seen and the home stays
    // locked until the (fast) check resolves. Guarded to run once per process.
    auto beginUpdateScan = [] {
        NativeOffice::UpdateChecker::instance().checkForUpdates();
    };
    auto revealShell = [&shell, beginUpdateScan]() {
        shell.show();
        shell.raise();
        shell.activateWindow();
        beginUpdateScan();
    };

    // ── Unified startup: one splash card + the sign-in gate ────────────────
    // A single card carries the whole launch — it shows "Restoring your
    // session" while the stored session validates, then "Initializing" until
    // the shell is ready (only the text changes). If sign-in is actually
    // needed, the splash steps aside and the gate's window takes over.
    {
        const bool useSplash =
            !openedFromCli && QSettings().value("app/showSplash", true).toBool();
        QPointer<NativeOffice::SplashScreen> splash =
            useSplash ? new NativeOffice::SplashScreen : nullptr;
        if (splash) splash->beginWith(QStringLiteral("Restoring your session"));

        auto* gate = new NativeOffice::LoginGate;
        QObject::connect(gate, &NativeOffice::LoginGate::proceed,
                         &shell, [splash, revealShell]() {
            if (splash) {
                splash->setStatus(QStringLiteral("Initializing"));
                QTimer::singleShot(1400, splash, [splash, revealShell]() {
                    revealShell();
                    if (splash) splash->finish();
                });
            } else {
                revealShell();
            }
        });
        QObject::connect(gate, &NativeOffice::LoginGate::signInRequired,
                         gate, [splash]() { if (splash) splash->finish(); });
        gate->begin(/*silentChecking=*/splash != nullptr);
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
