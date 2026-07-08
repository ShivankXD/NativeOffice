// ─────────────────────────────────────────────────────────────────────────────
// PdfPanels.cpp — see PdfPanels.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfPanels.h"
#include "PdfEditSession.h"
#include "core/theme/ThemeManager.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QSlider>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>

namespace NativeOffice::Pdf {

namespace {

QIcon strokeIcon(const std::function<void(QPainter&)>& draw) {
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const bool dark = ThemeManager::instance().isDark();
    QPen pen(dark ? QColor("#9AA4B8") : QColor("#3A3F4B"));
    pen.setWidthF(2.4);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    draw(p);
    p.end();
    return QIcon(pm);
}

QIcon bookmarkIcon() {
    return strokeIcon([](QPainter& p) {
        p.drawPolyline(QPolygonF({ QPointF(12, 8), QPointF(28, 8), QPointF(28, 33),
                                   QPointF(20, 26), QPointF(12, 33), QPointF(12, 8) }));
    });
}

QIcon thumbsIcon() {
    return strokeIcon([](QPainter& p) {
        p.drawRoundedRect(QRectF(8, 8, 10, 13), 1.5, 1.5);
        p.drawRoundedRect(QRectF(22, 8, 10, 13), 1.5, 1.5);
        p.drawRoundedRect(QRectF(8, 25, 10, 8), 1.5, 1.5);
        p.drawRoundedRect(QRectF(22, 25, 10, 8), 1.5, 1.5);
    });
}

QIcon commentIcon() {
    return strokeIcon([](QPainter& p) {
        p.drawRoundedRect(QRectF(8, 9, 24, 17), 3, 3);
        p.drawPolyline(QPolygonF({ QPointF(14, 26), QPointF(14, 33), QPointF(21, 26) }));
    });
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Sidebar
// ─────────────────────────────────────────────────────────────────────────────

Sidebar::Sidebar(EditSession* session, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
{
    setObjectName("pdfSidebar");

    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);

    // icon strip
    m_iconStrip = new QWidget(this);
    m_iconStrip->setObjectName("pdfSidebarStrip");
    m_iconStrip->setFixedWidth(40);
    auto* sv = new QVBoxLayout(m_iconStrip);
    sv->setContentsMargins(4, 12, 4, 12);
    sv->setSpacing(8);

    const QIcon icons[3] = { bookmarkIcon(), thumbsIcon(), commentIcon() };
    const QString tips[3] = { tr("Bookmarks"), tr("Page thumbnails"), tr("Comments") };
    for (int i = 0; i < 3; ++i) {
        auto* b = new QToolButton(m_iconStrip);
        b->setObjectName("pdfSidebarBtn");
        b->setIcon(icons[i]);
        b->setIconSize(QSize(20, 20));
        b->setCheckable(true);
        b->setToolTip(tips[i]);
        b->setFixedSize(32, 32);
        connect(b, &QToolButton::clicked, this, [this, i] { togglePanel(i); });
        sv->addWidget(b);
        m_btns[i] = b;
    }
    sv->addStretch();

    // panel stack
    m_panelStack = new QStackedWidget(this);
    m_panelStack->setObjectName("pdfSidebarPanel");
    m_panelStack->setFixedWidth(230);
    m_panelStack->hide();

    m_bookmarks = new QTreeWidget(m_panelStack);
    m_bookmarks->setObjectName("pdfBookmarksTree");
    m_bookmarks->setHeaderHidden(true);
    connect(m_bookmarks, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it, int) {
        const int pg = it->data(0, Qt::UserRole).toInt();
        if (pg >= 0) emit pageActivated(pg);
    });

    m_thumbs = new QListWidget(m_panelStack);
    m_thumbs->setObjectName("pdfThumbList");
    m_thumbs->setViewMode(QListView::IconMode);
    m_thumbs->setIconSize(QSize(150, 200));
    m_thumbs->setResizeMode(QListView::Adjust);
    m_thumbs->setMovement(QListView::Static);
    m_thumbs->setSpacing(10);
    m_thumbs->setUniformItemSizes(false);
    connect(m_thumbs, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        emit pageActivated(it->data(Qt::UserRole).toInt());
    });

    m_comments = new QListWidget(m_panelStack);
    m_comments->setObjectName("pdfCommentsList");
    m_comments->setWordWrap(true);

    m_panelStack->addWidget(m_bookmarks);
    m_panelStack->addWidget(m_thumbs);
    m_panelStack->addWidget(m_comments);

    h->addWidget(m_iconStrip);
    h->addWidget(m_panelStack);

    connect(session, &EditSession::documentChanged, this, &Sidebar::refresh);
    connect(&ThemeManager::instance(), &ThemeManager::modeChanged,
            this, [this](ThemeMode) { applyTheme(); });
    applyTheme();
}

void Sidebar::togglePanel(int panelId) {
    if (m_panelOpen && m_panelStack->currentIndex() == panelId) {
        m_panelOpen = false;
        m_panelStack->hide();
        m_btns[panelId]->setChecked(false);
        return;
    }
    m_panelOpen = true;
    for (int i = 0; i < 3; ++i) m_btns[i]->setChecked(i == panelId);
    m_panelStack->setCurrentIndex(panelId);
    m_panelStack->show();
    if (panelId == int(Panel::Thumbnails) && m_nextThumb < m_session->pageCount())
        QTimer::singleShot(0, this, &Sidebar::renderNextThumb);
}

void Sidebar::refresh() {
    // bookmarks
    m_bookmarks->clear();
    if (m_session->hasDocument())
        addOutlineNodes(nullptr, m_session->renderer()->outline());

    // thumbnails: create placeholder items; rasters fill in chunked.
    m_thumbs->clear();
    m_nextThumb = 0;
    if (m_session->hasDocument()) {
        const int n = m_session->pageCount();
        for (int i = 0; i < n; ++i) {
            auto* it = new QListWidgetItem(QString::number(i + 1), m_thumbs);
            it->setData(Qt::UserRole, i);
            it->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
        }
        if (m_panelOpen && m_panelStack->currentIndex() == int(Panel::Thumbnails))
            QTimer::singleShot(0, this, &Sidebar::renderNextThumb);
    }
}

void Sidebar::addOutlineNodes(QTreeWidgetItem* parent, const std::vector<OutlineNode>& nodes) {
    for (const OutlineNode& n : nodes) {
        auto* it = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_bookmarks);
        it->setText(0, n.title.isEmpty() ? tr("(untitled)") : n.title);
        it->setData(0, Qt::UserRole, n.pageIndex);
        addOutlineNodes(it, n.children);
    }
}

void Sidebar::renderNextThumb() {
    if (!m_session->hasDocument()) return;
    if (m_nextThumb >= m_thumbs->count()) return;
    // Render two pages per event-loop tick: keeps the UI responsive while a
    // long document's thumbnails stream in.
    for (int k = 0; k < 2 && m_nextThumb < m_thumbs->count(); ++k, ++m_nextThumb) {
        const int page = m_nextThumb;
        const QSizeF sz = m_session->renderer()->pageSizePt(page);
        const qreal scale = sz.width() > 0 ? 150.0 / sz.width() : 0.2;
        const QImage img = m_session->renderer()->renderPage(page, scale);
        if (!img.isNull())
            m_thumbs->item(page)->setIcon(QIcon(QPixmap::fromImage(img)));
    }
    if (m_nextThumb < m_thumbs->count())
        QTimer::singleShot(0, this, &Sidebar::renderNextThumb);
}

void Sidebar::setCurrentPage(int pageIndex) {
    if (pageIndex >= 0 && pageIndex < m_thumbs->count())
        m_thumbs->setCurrentRow(pageIndex);
}

void Sidebar::applyTheme() {
    const auto& tm = ThemeManager::instance();
    setStyleSheet(QString(R"(
QWidget#pdfSidebarStrip { background: %1; border-right: 1px solid %2; }
QToolButton#pdfSidebarBtn { background: transparent; border: none; border-radius: 6px; }
QToolButton#pdfSidebarBtn:hover { background: %3; }
QToolButton#pdfSidebarBtn:checked { background: %4; }
QStackedWidget#pdfSidebarPanel { background: %1; border-right: 1px solid %2; }
QTreeWidget#pdfBookmarksTree, QListWidget#pdfThumbList, QListWidget#pdfCommentsList {
    background: %1; border: none; color: %5; font: 10pt "Segoe UI";
}
QListWidget#pdfThumbList::item { color: %6; }
QListWidget#pdfThumbList::item:selected { background: %3; border: 1px solid %7; }
QTreeWidget#pdfBookmarksTree::item { padding: 3px; }
QTreeWidget#pdfBookmarksTree::item:hover, QListWidget#pdfCommentsList::item:hover { background: %3; }
)")
        .arg(tm.chromePanelBg(), tm.chromeBorder(), tm.chromeHoverBg(),
             tm.chromeActiveBg(), tm.chromeText(), tm.chromeTextMuted(), "#6D5BE8"));
}

// ─────────────────────────────────────────────────────────────────────────────
// StatusBar
// ─────────────────────────────────────────────────────────────────────────────

StatusBar::StatusBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("pdfStatusBar");
    // Custom QWidget subclasses don't paint stylesheet backgrounds unless
    // told to — without this the tray is transparent and the (dark) shell
    // chrome behind it shows through.
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(30);

    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(10, 2, 10, 2);
    h->setSpacing(6);

    auto mkBtn = [this](const QString& text, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setObjectName("pdfStatusBtn");
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        return b;
    };

    m_prev = mkBtn("‹", tr("Previous page"));
    m_next = mkBtn("›", tr("Next page"));
    m_pageEdit = new QLineEdit(this);
    m_pageEdit->setObjectName("pdfPageEdit");
    m_pageEdit->setFixedWidth(44);
    m_pageEdit->setAlignment(Qt::AlignCenter);
    m_pageTotal = new QLabel("/ 0", this);
    m_pageTotal->setObjectName("pdfStatusLabel");

    connect(m_prev, &QToolButton::clicked, this, &StatusBar::prevPageRequested);
    connect(m_next, &QToolButton::clicked, this, &StatusBar::nextPageRequested);
    connect(m_pageEdit, &QLineEdit::returnPressed, this, [this] {
        bool ok = false;
        const int p = m_pageEdit->text().toInt(&ok);
        if (ok && p >= 1 && p <= m_total) emit pageJumpRequested(p - 1);
    });

    m_fitW = mkBtn("⇔", tr("Fit width"));
    m_fitP = mkBtn("▭", tr("Fit page"));
    connect(m_fitW, &QToolButton::clicked, this, &StatusBar::fitWidthRequested);
    connect(m_fitP, &QToolButton::clicked, this, &StatusBar::fitPageRequested);

    m_zoomOut = mkBtn("−", tr("Zoom out"));
    m_zoomIn  = mkBtn("+", tr("Zoom in"));
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setObjectName("pdfZoomSlider");
    m_slider->setRange(10, 640);
    m_slider->setValue(100);
    m_slider->setFixedWidth(120);
    m_zoomLabel = new QLabel("100%", this);
    m_zoomLabel->setObjectName("pdfStatusLabel");
    m_zoomLabel->setFixedWidth(44);

    connect(m_zoomOut, &QToolButton::clicked, this, &StatusBar::zoomOutRequested);
    connect(m_zoomIn,  &QToolButton::clicked, this, &StatusBar::zoomInRequested);
    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        if (!m_syncing) emit zoomChanged(v);
    });

    h->addWidget(m_prev);
    h->addWidget(m_pageEdit);
    h->addWidget(m_pageTotal);
    h->addWidget(m_next);
    h->addStretch();
    h->addWidget(m_fitW);
    h->addWidget(m_fitP);
    h->addSpacing(8);
    h->addWidget(m_zoomOut);
    h->addWidget(m_slider);
    h->addWidget(m_zoomIn);
    h->addWidget(m_zoomLabel);

    connect(&ThemeManager::instance(), &ThemeManager::modeChanged,
            this, [this](ThemeMode) { applyTheme(); });
    applyTheme();
}

void StatusBar::setPageInfo(int current, int total) {
    m_total = total;
    m_pageEdit->setText(QString::number(total > 0 ? current + 1 : 0));
    m_pageTotal->setText(QStringLiteral("/ %1").arg(total));
    m_prev->setEnabled(current > 0);
    m_next->setEnabled(current + 1 < total);
}

void StatusBar::setZoomPercent(int pct) {
    m_syncing = true;
    m_slider->setValue(pct);
    m_zoomLabel->setText(QString::number(pct) + "%");
    m_syncing = false;
}

void StatusBar::applyTheme() {
    // The status tray is always white chrome, matching the rest of the shell,
    // regardless of the app-wide dark/light mode.
    setStyleSheet(R"(
QWidget#pdfStatusBar { background: #FFFFFF; border-top: 1px solid #E4E7ED; }
QToolButton#pdfStatusBtn { background: transparent; border: none; border-radius: 4px;
    color: #5A6071; font: 12pt "Segoe UI"; min-width: 22px; }
QToolButton#pdfStatusBtn:hover { background: #EFF1F5; }
QToolButton#pdfStatusBtn:disabled { color: #C1C6D2; }
QLabel#pdfStatusLabel { color: #5A6071; font: 9pt "Segoe UI"; }
QLineEdit#pdfPageEdit { background: #FFFFFF; border: 1px solid #D7DAE0; border-radius: 4px;
    color: #1C1E26; font: 9pt "Segoe UI"; padding: 1px; }
QSlider#pdfZoomSlider::groove:horizontal { height: 3px; background: #D7DAE0; border-radius: 1px; }
QSlider#pdfZoomSlider::handle:horizontal { width: 12px; height: 12px; margin: -5px 0;
    border-radius: 6px; background: #6D5BE8; }
)");
}

} // namespace NativeOffice::Pdf
