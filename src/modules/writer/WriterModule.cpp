// ─────────────────────────────────────────────────────────────────────────────
// WriterModule.cpp  (Sprint 3 → Sprint 10)
// Word processor with integrated .noff file I/O and image insertion.
// ─────────────────────────────────────────────────────────────────────────────
#include "WriterModule.h"
#include "WriterRibbon.h"
#include "WriterStatusBar.h"
#include "core/theme/ThemeManager.h"

#include <QVBoxLayout>
#include <QScrollArea>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QFont>
#include <QSizePolicy>
#include <QShortcut>
#include <QKeySequence>
#include <QGraphicsDropShadowEffect>
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

    // ── Paper (QTextEdit) ─────────────────────────────────────────────────
    m_editor = new QTextEdit(m_canvas);
    m_editor->setObjectName("writerPaper");
    m_editor->setAcceptRichText(true);
    m_editor->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_editor->setPlaceholderText(tr("Start typing your document..."));

    // A4 proportions at 96 dpi: 794 × 1123 px
    m_editor->setFixedWidth(794);
    m_editor->setMinimumHeight(1123);
    m_editor->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    // Document margins (~2.5 cm)
    m_editor->document()->setDocumentMargin(60.0);

    // Body font lives on the document's default font so it scales with zoom;
    // the default char format only fixes family + colour (no explicit size).
    m_baseFont = QFont("Segoe UI", 12);
    m_editor->document()->setDefaultFont(m_baseFont);

    QTextCharFormat defaultFmt;
    defaultFmt.setFontFamilies({"Segoe UI", "Inter", "Roboto", "sans-serif"});
    defaultFmt.setForeground(QColor("#1C1E26"));
    m_editor->setCurrentCharFormat(defaultFmt);

    // Drop-shadow (paper-on-desk effect)
    auto* shadow = new QGraphicsDropShadowEffect(m_editor);
    shadow->setBlurRadius(28);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 55));
    m_editor->setGraphicsEffect(shadow);

    canvasLayout->addWidget(m_editor);

    // ── Scroll area ───────────────────────────────────────────────────────
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("writerScroll");
    scroll->setWidgetResizable(true);
    scroll->setWidget(m_canvas);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // Ctrl+scroll over the page (or the surrounding canvas) zooms in/out.
    m_editor->viewport()->installEventFilter(this);
    scroll->viewport()->installEventFilter(this);

    // ── Status bar ────────────────────────────────────────────────────────
    m_statusBar = new WriterStatusBar(this);

    // Word/page count is recomputed off a debounce timer so that fast typing
    // and zooming don't force a full document relayout on every event.
    m_statusTimer = new QTimer(this);
    m_statusTimer->setSingleShot(true);
    m_statusTimer->setInterval(220);
    connect(m_statusTimer, &QTimer::timeout, this, &WriterModule::updateStatus);

    rootLayout->addWidget(m_ribbon);
    rootLayout->addWidget(scroll, 1);
    rootLayout->addWidget(m_statusBar);

    // ── Wire ribbon → editor ──────────────────────────────────────────────
    m_ribbon->attachEditor(m_editor);
    connect(m_ribbon, &WriterRibbon::insertImageRequested,
            this,     &WriterModule::insertImage);

    // ── Wire status bar ───────────────────────────────────────────────────
    connect(m_statusBar, &WriterStatusBar::zoomChanged,
            this, &WriterModule::applyZoom);
    connect(m_statusBar, &WriterStatusBar::webLayoutToggled,
            this, &WriterModule::setWebLayout);

    // ── Dirty-state + live status tracking ────────────────────────────────
    connect(m_editor->document(), &QTextDocument::contentsChanged,
            this, &WriterModule::onContentsChanged);
    connect(m_editor->document(), &QTextDocument::contentsChanged,
            this, &WriterModule::scheduleStatusUpdate);

    applyCanvasStyles();
    updateStatus();
    m_editor->setFocus();
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
bool WriterModule::saveToPath(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;

    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);

    // Write the .noff header comment so the file is self-describing
    out << "<!-- NativeOffice Writer Document (.noff) -->\n";
    out << m_editor->toHtml();

    f.close();

    m_currentPath = path;
    m_dirty       = false;
    emit filePathChanged(path);
    return true;
}

bool WriterModule::loadFromPath(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    QString content = in.readAll();
    f.close();

    // Strip the .noff header comment if present
    content.remove("<!-- NativeOffice Writer Document (.noff) -->\n");

    m_ignoreChange = true;
    m_editor->setHtml(content);
    m_ignoreChange = false;

    m_currentPath = path;
    m_dirty       = false;
    emit filePathChanged(path);
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
    background-color: #FFFFFF;
    color: #1C1E26;
    border: none;
    border-radius: 2px;
    padding: 0;
    selection-background-color: %2;
    selection-color: #FFFFFF;
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
    .arg(ThemeManager::cssColor(t.secondary))
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

    // Page count — content height divided by a (zoom-scaled) A4 page height.
    const double z = m_zoom / 100.0;
    const double pageH = 1123.0 * z;
    const double docH  = m_editor->document()->size().height();
    const int pages = qMax(1, static_cast<int>(std::ceil(docH / qMax(1.0, pageH))));
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
    m_editor->document()->setDocumentMargin(60.0 * z);

    if (!m_webLayout) {
        m_editor->setFixedWidth(static_cast<int>(794 * z));
        m_editor->setMinimumHeight(static_cast<int>(1123 * z));
    }

    m_ignoreChange = guard;
    m_applyingZoom = false;
    scheduleStatusUpdate();   // debounced — avoids forcing layout per wheel tick
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
        // Full-width, fluid page — fill the canvas.
        m_editor->setMinimumWidth(0);
        m_editor->setMaximumWidth(QWIDGETSIZE_MAX);
        m_editor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    } else {
        // Paged A4 view.
        m_editor->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        m_editor->setFixedWidth(static_cast<int>(794 * z));
        m_editor->setMinimumHeight(static_cast<int>(1123 * z));
    }
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
