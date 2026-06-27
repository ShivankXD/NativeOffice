// ─────────────────────────────────────────────────────────────────────────────
// WriterModule.cpp  (Sprint 3 → Sprint 10)
// Word processor with integrated .noff file I/O and image insertion.
// ─────────────────────────────────────────────────────────────────────────────
#include "WriterModule.h"
#include "WriterRibbon.h"
#include "WriterStatusBar.h"
#include "PagedTextEdit.h"
#include "WriterRuler.h"
#include "core/theme/ThemeManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QResizeEvent>
#include <QStandardPaths>
#include <QMessageBox>
#include <QDir>
#include <QApplication>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QFont>
#include <QSizePolicy>
#include <QShortcut>
#include <QKeySequence>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QIODevice>
#include <QFileDialog>
#include <QImage>
#include <QBuffer>
#include <QByteArray>
#include <QRegularExpression>
#include <QtMath>
#include <QWheelEvent>
#include <QEvent>
#include <QTimer>
#include <QScrollBar>

namespace NativeOffice {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────
WriterModule::WriterModule(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    setObjectName("writerModule");
}

void WriterModule::buildUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ── Ribbon (WPS/Word-style tabbed toolbar) ────────────────────────────
    m_ribbon = new WriterRibbon(this);

    // ── Canvas (scrollable area behind the paper) ─────────────────────────
    m_canvas = new QWidget(this);
    m_canvas->setObjectName("writerCanvas");
    m_canvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* canvasLayout = new QVBoxLayout(m_canvas);
    canvasLayout->setContentsMargins(40, 40, 40, 60);
    canvasLayout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    // ── Paper (paginated QTextEdit) ───────────────────────────────────────
    m_paper = new PagedTextEdit(m_canvas);
    m_editor = m_paper;                       // QTextEdit* API surface for the ribbon
    m_editor->setObjectName("writerPaper");
    m_editor->setAcceptRichText(true);
    m_editor->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_editor->setPlaceholderText(tr("Start typing your document..."));

    // A4 @96 dpi: 794 × 1123 px, 2.5 cm margins. PagedTextEdit owns the geometry.
    m_paper->setPageMetrics(m_basePageW, m_basePageH, m_pageMargin);

    // Body font lives on the document's default font so it scales with zoom;
    // the default char format only fixes family + colour (no explicit size).
    m_baseFont = QFont("Segoe UI", 12);
    m_editor->document()->setDefaultFont(m_baseFont);

    QTextCharFormat defaultFmt;
    defaultFmt.setFontFamilies({"Segoe UI", "Inter", "Roboto", "sans-serif"});
    defaultFmt.setForeground(QColor("#1C1E26"));
    m_editor->setCurrentCharFormat(defaultFmt);

    canvasLayout->addWidget(m_editor);

    // ── Scroll area ───────────────────────────────────────────────────────
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName("writerScroll");
    m_scroll->setWidgetResizable(true);
    m_scroll->setWidget(m_canvas);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // Ctrl+scroll over the page (or the surrounding canvas) zooms in/out.
    m_editor->viewport()->installEventFilter(this);
    m_scroll->viewport()->installEventFilter(this);

    // The paper sizes itself to the full multi-page height, so the editor never
    // scrolls internally — keep the cursor visible by scrolling the outer area.
    connect(m_editor, &QTextEdit::cursorPositionChanged, this, [this] {
        if (!m_scroll || !m_editor || m_ignoreChange) return;   // not during load
        const QRect cr = m_editor->cursorRect();
        const QPoint inCanvas = m_editor->mapTo(m_canvas, cr.bottomLeft());
        m_scroll->ensureVisible(inCanvas.x(), inCanvas.y(), 40, 90);
    });

    // ── Rulers (Tier 4) ───────────────────────────────────────────────────
    m_hRuler = new WriterRuler(Qt::Horizontal, this);
    m_vRuler = new WriterRuler(Qt::Vertical, this);
    m_hRuler->setEditor(m_editor);
    m_vRuler->setEditor(m_editor);
    connect(m_hRuler, &WriterRuler::marginChangeRequested, this, &WriterModule::setPageMargin);
    connect(m_vRuler, &WriterRuler::marginChangeRequested, this, &WriterModule::setPageMargin);
    // Re-align the rulers with the page whenever the view scrolls.
    connect(m_scroll->horizontalScrollBar(), &QScrollBar::valueChanged,
            this, [this]{ updateRulerGeometry(); });
    connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this]{ updateRulerGeometry(); });

    // ── Status bar ────────────────────────────────────────────────────────
    m_statusBar = new WriterStatusBar(this);

    // Word/page count is recomputed off a debounce timer so that fast typing
    // and zooming don't force a full document relayout on every event.
    m_statusTimer = new QTimer(this);
    m_statusTimer->setSingleShot(true);
    m_statusTimer->setInterval(220);
    connect(m_statusTimer, &QTimer::timeout, this, &WriterModule::updateStatus);

    // Autosave: snapshot unsaved changes to a recovery file every 20s. A clean
    // app exit deletes it; a crash leaves it for recovery on next launch.
    m_autosaveTimer = new QTimer(this);
    m_autosaveTimer->setInterval(20000);
    connect(m_autosaveTimer, &QTimer::timeout, this, &WriterModule::writeRecovery);
    m_autosaveTimer->start();
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]{ QFile::remove(recoveryFilePath()); });

    rootLayout->addWidget(m_ribbon);
    rootLayout->addWidget(m_hRuler);

    // The vertical ruler sits to the left of the scrolling page area.
    auto* midRow = new QWidget(this);
    auto* midLayout = new QHBoxLayout(midRow);
    midLayout->setContentsMargins(0, 0, 0, 0);
    midLayout->setSpacing(0);
    midLayout->addWidget(m_vRuler);
    midLayout->addWidget(m_scroll, 1);

    rootLayout->addWidget(midRow, 1);
    rootLayout->addWidget(m_statusBar);

    // ── Wire ribbon → editor ──────────────────────────────────────────────
    m_ribbon->attachEditor(m_editor);
    connect(m_ribbon, &WriterRibbon::insertImageRequested,
            this,     &WriterModule::insertImage);

    // Page-level operations (Page Layout + View tabs)
    connect(m_ribbon, &WriterRibbon::zoomInRequested,  this, [this]{ zoomBy(+10); });
    connect(m_ribbon, &WriterRibbon::zoomOutRequested, this, [this]{ zoomBy(-10); });
    connect(m_ribbon, &WriterRibbon::zoomResetRequested, this, [this]{
        applyZoom(100); if (m_statusBar) m_statusBar->setZoomPercent(100);
    });
    connect(m_ribbon, &WriterRibbon::pageMarginsRequested, this, &WriterModule::setPageMargin);
    connect(m_ribbon, &WriterRibbon::orientationRequested, this, &WriterModule::setOrientation);
    connect(m_ribbon, &WriterRibbon::pageSizeRequested,    this, &WriterModule::setPageSize);
    connect(m_ribbon, &WriterRibbon::pageColorRequested,   this, &WriterModule::setPageColor);
    connect(m_ribbon, &WriterRibbon::webLayoutRequested,   this, [this](bool web){
        setWebLayout(web); if (m_statusBar) m_statusBar->setWebLayoutActive(web);
    });
    connect(m_ribbon, &WriterRibbon::rulerToggled, this, &WriterModule::setRulersVisible);

    // ── Wire status bar ───────────────────────────────────────────────────
    connect(m_statusBar, &WriterStatusBar::zoomChanged,
            this, &WriterModule::applyZoom);
    connect(m_statusBar, &WriterStatusBar::webLayoutToggled,
            this, &WriterModule::setWebLayout);
    connect(m_statusBar, &WriterStatusBar::spellCheckToggled, this, [this](bool on){
        if (m_paper) m_paper->setSpellCheckEnabled(on);
    });
    // Spell check on by default; sync the editor to the pill's initial state.
    if (m_paper) m_paper->setSpellCheckEnabled(true);
    m_statusBar->setSpellCheckActive(true);

    // Keep the {file} header/footer field in sync with the document name.
    connect(this, &WriterModule::filePathChanged, this, [this](const QString& p) {
        if (m_paper) m_paper->setDocName(QFileInfo(p).fileName());
    });

    // ── Dirty-state + live status tracking ────────────────────────────────
    connect(m_editor->document(), &QTextDocument::contentsChanged,
            this, &WriterModule::onContentsChanged);
    connect(m_editor->document(), &QTextDocument::contentsChanged,
            this, &WriterModule::scheduleStatusUpdate);

    applyCanvasStyles();
    updateStatus();
    m_editor->setFocus();

    // Align the rulers once the initial layout has settled.
    QTimer::singleShot(0, this, [this]{ updateRulerGeometry(); });

    // For an untitled session, offer crash recovery once shown (loadFromPath
    // handles the case where a file is opened).
    QTimer::singleShot(500, this, [this]{ if (m_currentPath.isEmpty()) checkCrashRecovery(); });
}

// ─────────────────────────────────────────────────────────────────────────────
// Dirty state
// ─────────────────────────────────────────────────────────────────────────────
void WriterModule::onContentsChanged() {
    if (m_ignoreChange) return;
    if (!m_dirty) {
        m_dirty = true;
        emit documentModified();
    }
}

void WriterModule::markClean() {
    m_dirty = false;
}

QString WriterModule::titleString() const {
    const QString base = m_currentPath.isEmpty()
                             ? "Untitled Document"
                             : QFileInfo(m_currentPath).fileName();
    return (m_dirty ? "* " : "") + base + " — NativeOffice Writer";
}

// ─────────────────────────────────────────────────────────────────────────────
// File I/O (.noff format = UTF-8 HTML)
// ─────────────────────────────────────────────────────────────────────────────
// Serialises the document to the full .noff string (header + style sidecar + HTML).
// Shared by saveToPath() and the autosave/recovery snapshot.
QString WriterModule::buildNoffString() const {
    QString out = "<!-- NativeOffice Writer Document (.noff) -->\n";

    // Persist named styles + per-paragraph style tags as base64 in HTML comments
    // (ignored by setHtml on load), which is what fixes the style round-trip.
    if (m_ribbon) {
        const QString defs   = m_ribbon->styleDefsJson();
        const QString blocks = m_ribbon->blockAssignmentsJson();
        if (!defs.isEmpty())
            out += "<!--NOFF-STYLEDEFS " + QString::fromLatin1(defs.toUtf8().toBase64()) + " -->\n";
        if (!blocks.isEmpty())
            out += "<!--NOFF-BLOCKSTYLES " + QString::fromLatin1(blocks.toUtf8().toBase64()) + " -->\n";
    }
    out += m_editor->toHtml();
    return out;
}

bool WriterModule::saveToPath(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;

    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << buildNoffString();
    f.close();

    m_currentPath = path;
    m_dirty       = false;
    emit filePathChanged(path);

    // A clean save supersedes any crash-recovery snapshot for this document.
    QFile::remove(recoveryFilePath());
    return true;
}

// ── Autosave + crash recovery ────────────────────────────────────────────────
QString WriterModule::recoveryFilePath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) dir = QDir::tempPath();
    dir += "/recovery";
    QDir().mkpath(dir);
    const QString key = m_currentPath.isEmpty()
                            ? QStringLiteral("untitled")
                            : QString::number(qHash(m_currentPath), 16);
    return dir + "/" + key + ".noff";
}

void WriterModule::writeRecovery() {
    if (!m_editor || !m_dirty) return;          // nothing unsaved → no snapshot
    if (m_editor->document()->isEmpty()) return;
    QFile f(recoveryFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return;
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << buildNoffString();
    f.close();
}

void WriterModule::checkCrashRecovery() {
    const QString rp = recoveryFilePath();
    if (!QFileInfo::exists(rp)) return;          // clean previous session

    const QString name = m_currentPath.isEmpty()
                             ? QStringLiteral("an untitled document")
                             : QFileInfo(m_currentPath).fileName();
    const auto choice = QMessageBox::question(
        this, tr("Recover Unsaved Changes"),
        tr("NativeOffice found unsaved changes for %1 from a previous session "
           "that didn't close normally.\n\nRecover them?").arg(name),
        QMessageBox::Yes | QMessageBox::No);

    if (choice == QMessageBox::Yes) {
        QFile f(rp);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            in.setEncoding(QStringConverter::Utf8);
            const QString content = in.readAll();
            f.close();
            const QString keep = m_currentPath;      // recovery keeps the bound path
            loadNoffString(content);
            m_currentPath = keep;
            m_dirty = true;                          // recovered = still unsaved
            emit documentModified();
        }
    } else {
        QFile::remove(rp);
    }
}

// Parse a .noff string (strip header, pull style sidecar, set HTML, re-tag).
void WriterModule::loadNoffString(QString content) {
    content.remove("<!-- NativeOffice Writer Document (.noff) -->\n");

    QString styleDefs, blockStyles;
    auto extract = [&content](const QString& tag) -> QString {
        const QRegularExpression re("<!--" + tag + " ([A-Za-z0-9+/=]+) -->\\n?");
        const QRegularExpressionMatch m = re.match(content);
        if (!m.hasMatch()) return QString();
        const QString b64 = m.captured(1);
        content.remove(m.capturedStart(), m.capturedLength());
        return QString::fromUtf8(QByteArray::fromBase64(b64.toLatin1()));
    };
    styleDefs   = extract("NOFF-STYLEDEFS");
    blockStyles = extract("NOFF-BLOCKSTYLES");

    m_ignoreChange = true;
    m_editor->setHtml(content);
    m_editor->moveCursor(QTextCursor::Start);
    if (m_ribbon) m_ribbon->applyPersistedStyles(styleDefs, blockStyles);
    m_ignoreChange = false;

    // Show the top of the document once layout settles.
    QTimer::singleShot(0, this, [this] {
        if (m_scroll && m_scroll->verticalScrollBar())
            m_scroll->verticalScrollBar()->setValue(0);
        updateRulerGeometry();
    });
}

bool WriterModule::loadFromPath(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    const QString content = in.readAll();
    f.close();

    loadNoffString(content);

    m_currentPath = path;
    m_dirty       = false;
    emit filePathChanged(path);

    // After binding the file, offer to restore a leftover crash-recovery snapshot.
    QTimer::singleShot(400, this, [this]{ checkCrashRecovery(); });
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Legacy helpers
// ─────────────────────────────────────────────────────────────────────────────
void WriterModule::setContent(const QString& html) {
    m_ignoreChange = true;
    if (m_editor) m_editor->setHtml(html);
    m_ignoreChange = false;
    m_dirty = false;
}

void WriterModule::setPlainContent(const QString& text) {
    m_ignoreChange = true;
    if (m_editor) m_editor->setPlainText(text);
    m_ignoreChange = false;
    m_dirty = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public accessors
// ─────────────────────────────────────────────────────────────────────────────
QTextDocument* WriterModule::document() const noexcept {
    return m_editor ? m_editor->document() : nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Styling
// ─────────────────────────────────────────────────────────────────────────────
void WriterModule::applyCanvasStyles() {
    const auto& t = ThemeManager::instance().theme();

    setStyleSheet(QString(R"(
QWidget#writerModule {
    background-color: %1;
}
QWidget#writerCanvas {
    background-color: #E8E9ED;
}
QTextEdit#writerPaper {
    background-color: %3;
    color: #1C1E26;
    border: none;
    border-radius: 2px;
    padding: 0;
    selection-background-color: %2;
    selection-color: #1C1E26;
    font-family: "Segoe UI", "Inter", "Roboto", sans-serif;
    font-size: 12pt;
    line-height: 1.6;
}
QScrollArea#writerScroll {
    background-color: #E8E9ED;
    border: none;
}
QScrollArea#writerScroll > QWidget > QWidget {
    background-color: #E8E9ED;
}
)")
    .arg(ThemeManager::cssColor(t.primary))
    .arg(QStringLiteral("#ADD2F5"))      // traditional light-blue text selection
    .arg(m_pageColor.name())
    );
}

// ─────────────────────────────────────────────────────────────────────────────
// Sprint 14: Status bar — live word/page count, zoom, page-view layout
// ─────────────────────────────────────────────────────────────────────────────
void WriterModule::scheduleStatusUpdate() {
    if (m_statusTimer) m_statusTimer->start();   // (re)start the debounce window
}

void WriterModule::updateStatus() {
    if (!m_editor || !m_statusBar) return;

    // Word count
    const QString text = m_editor->document()->toPlainText().trimmed();
    const int words = text.isEmpty()
                          ? 0
                          : static_cast<int>(text.split(QRegularExpression("\\s+"),
                                                        Qt::SkipEmptyParts).size());
    m_statusBar->setWordCount(words);

    // Page count — exact, straight from the paginated layout.
    const int pages = m_paper ? m_paper->pageCountValue() : 1;
    m_statusBar->setPageInfo(1, pages);
}

void WriterModule::applyZoom(int percent) {
    if (!m_editor || m_applyingZoom) return;   // ignore re-entrant calls
    m_applyingZoom = true;
    m_zoom = percent;
    const double z = percent / 100.0;

    const bool guard = m_ignoreChange;
    m_ignoreChange = true;

    QFont f = m_baseFont;
    f.setPointSizeF(m_baseFont.pointSizeF() * z);
    m_editor->document()->setDefaultFont(f);

    if (!m_webLayout && m_paper) {
        const double effW = (m_landscape ? m_basePageH : m_basePageW) * z;
        const double effH = (m_landscape ? m_basePageW : m_basePageH) * z;
        m_paper->setPageMetrics(effW, effH, m_pageMargin * z);
    } else {
        m_editor->document()->setDocumentMargin(m_pageMargin * z);
    }

    m_ignoreChange = guard;
    m_applyingZoom = false;
    updateRulerGeometry();
    scheduleStatusUpdate();   // debounced — avoids forcing layout per wheel tick
}

// ─────────────────────────────────────────────────────────────────────────────
// Sprint 15: Page Layout
// ─────────────────────────────────────────────────────────────────────────────
void WriterModule::setPageMargin(double px) {
    m_pageMargin = px;
    applyZoom(m_zoom);
}

void WriterModule::setOrientation(bool landscape) {
    m_landscape = landscape;
    applyZoom(m_zoom);
}

void WriterModule::setPageSize(double portraitW, double portraitH) {
    m_basePageW = portraitW;
    m_basePageH = portraitH;
    applyZoom(m_zoom);
}

void WriterModule::setPageColor(const QColor& color) {
    m_pageColor = color.isValid() ? color : QColor("#FFFFFF");
    if (m_paper) m_paper->setPaperColor(m_pageColor);
    applyCanvasStyles();
}

void WriterModule::resizeEvent(QResizeEvent* ev) {
    QWidget::resizeEvent(ev);
    updateRulerGeometry();
}

void WriterModule::updateRulerGeometry() {
    if (!m_hRuler || !m_vRuler || !m_editor || !m_scroll) return;
    if (!m_rulersVisible) return;
    const double z = m_zoom / 100.0;
    const int marginPx = qRound(m_pageMargin * z);

    const int pageW = m_editor->width();
    const int hOriginX = m_editor->mapTo(m_hRuler, QPoint(0, 0)).x();
    m_hRuler->setPageGeometry(hOriginX, pageW, marginPx, z);

    // The editor is many pages tall; the vertical ruler shows the first page's
    // scale/margins from the document top (which scrolls with the content). The
    // ruler shares the scroll viewport's top edge, so the page's y within the
    // viewport is exactly its y on the ruler.
    const int vOriginY = m_editor->mapTo(m_scroll->viewport(), QPoint(0, 0)).y();
    const int pageH = qRound((m_landscape ? m_basePageW : m_basePageH) * z);
    m_vRuler->setPageGeometry(vOriginY, pageH, marginPx, z);
}

void WriterModule::setRulersVisible(bool on) {
    m_rulersVisible = on;
    if (m_hRuler) m_hRuler->setVisible(on);
    if (m_vRuler) m_vRuler->setVisible(on);
    if (on) updateRulerGeometry();
}

bool WriterModule::eventFilter(QObject* obj, QEvent* ev) {
    if (ev->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(ev);
        if (we->modifiers() & Qt::ControlModifier) {
            zoomBy(we->angleDelta().y() > 0 ? +10 : -10);
            return true;   // consume so the editor doesn't also scroll/zoom
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void WriterModule::zoomBy(int deltaPercent) {
    const int nz = qBound(75, m_zoom + deltaPercent, 200);
    if (nz == m_zoom) return;
    applyZoom(nz);
    if (m_statusBar) m_statusBar->setZoomPercent(nz);  // keep the slider in sync
}

void WriterModule::setWebLayout(bool web) {
    if (!m_editor) return;
    m_webLayout = web;
    const double z = m_zoom / 100.0;

    if (web) {
        // Full-width, fluid page — no pagination, fill the canvas.
        if (m_paper) m_paper->setPaged(false);
        m_editor->setMinimumWidth(0);
        m_editor->setMaximumWidth(QWIDGETSIZE_MAX);
        m_editor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    } else {
        // Paged view (respects current size + orientation).
        m_editor->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        if (m_paper) {
            m_paper->setPaged(true);
            const double effW = (m_landscape ? m_basePageH : m_basePageW) * z;
            const double effH = (m_landscape ? m_basePageW : m_basePageH) * z;
            m_paper->setPageMetrics(effW, effH, m_pageMargin * z);
        }
    }
    updateRulerGeometry();
    scheduleStatusUpdate();
}

// ─────────────────────────────────────────────────────────────────────────────
// Sprint 10: Image Insertion
//
// Pipeline: QFileDialog → QImage → scale to fit margins → Base64 → HTML <img>
//
// Max image width = A4 paper (794 px) minus left+right document margins (60+60).
// Images are encoded as PNG Base64 data URIs so they embed directly inside
// the HTML string that QTextEdit produces, which means they automatically
// persist in our .noff file format without any file-system changes.
// ─────────────────────────────────────────────────────────────────────────────
void WriterModule::insertImage() {
    if (!m_editor) return;

    // 1. Open a file dialog for common image formats
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Insert Image"),
        QString(),
        tr("Images (*.png *.jpg *.jpeg *.bmp);;All Files (*)"));

    if (path.isEmpty()) return;

    // 2. Load the image
    QImage image(path);
    if (image.isNull()) return;

    // 3. Scale down to fit within the A4 paper content area
    //    Paper width = 794 px, document margin = 60 px each side
    //    Max usable width = 794 - 60 - 60 = 674 px
    constexpr int maxWidth = 674;
    if (image.width() > maxWidth) {
        image = image.scaledToWidth(maxWidth, Qt::SmoothTransformation);
    }

    // 4. Encode the scaled image as PNG Base64
    QByteArray imageData;
    {
        QBuffer buffer(&imageData);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
    }
    const QString base64 = QString::fromLatin1(imageData.toBase64());

    // 5. Insert as an inline HTML <img> at the current cursor position
    const QString html = QStringLiteral(
        "<img src=\"data:image/png;base64,%1\"/>").arg(base64);

    QTextCursor cursor = m_editor->textCursor();
    cursor.insertHtml(html);

    // The QTextDocument::contentsChanged signal fires automatically,
    // which triggers onContentsChanged() and sets the dirty flag.
    m_editor->setFocus();
}

} // namespace NativeOffice
