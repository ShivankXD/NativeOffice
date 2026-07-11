// ─────────────────────────────────────────────────────────────────────────────
// PdfViewer.cpp — see PdfViewer.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfViewer.h"
#include "PdfEditSession.h"
#include "core/theme/ThemeManager.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

#include <QDebug>

#include <algorithm>
#include <cmath>

namespace NativeOffice::Pdf {

Viewer::Viewer(EditSession* session, QWidget* parent)
    : QAbstractScrollArea(parent)
    , m_session(session)
{
    setObjectName("pdfViewer");
    setFrameShape(QFrame::NoFrame);     // no dark frame between canvas and chrome
    // Light scrollbars so the chrome around the white canvas stays white.
    setStyleSheet(R"(
QAbstractScrollArea#pdfViewer { border: none; background: transparent; }
QScrollBar:vertical { background: #F6F7F9; width: 12px; border: none; }
QScrollBar::handle:vertical { background: #CDD2DC; border-radius: 4px; min-height: 40px; margin: 2px; }
QScrollBar::handle:vertical:hover { background: #B7BDC9; }
QScrollBar:horizontal { background: #F6F7F9; height: 12px; border: none; }
QScrollBar::handle:horizontal { background: #CDD2DC; border-radius: 4px; min-width: 40px; margin: 2px; }
QScrollBar::handle:horizontal:hover { background: #B7BDC9; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
)");
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    connect(session, &EditSession::documentChanged, this, &Viewer::refresh);
    connect(&ThemeManager::instance(), &ThemeManager::modeChanged,
            this, [this](ThemeMode) { viewport()->update(); });

    relayout();
}

// ── layout ──────────────────────────────────────────────────────────────────

void Viewer::relayout() {
    m_pages.clear();
    m_contentW = m_contentH = 0;
    if (m_session->hasDocument()) {
        const int n = m_session->pageCount();
        qreal y = kMarginPt;
        qreal maxW = 0;
        m_pages.reserve(size_t(n));
        for (int i = 0; i < n; ++i) {
            const QSizeF sz = m_session->renderer()->pageSizePt(i);
            m_pages.push_back({ QRectF(0, y, sz.width(), sz.height()) });
            y += sz.height() + kPageGapPt;
            maxW = std::max(maxW, sz.width());
        }
        // Center pages horizontally in content space.
        for (auto& pg : m_pages)
            pg.rect.moveLeft(kMarginPt + (maxW - pg.rect.width()) / 2.0);
        m_contentW = maxW + 2 * kMarginPt;
        m_contentH = y - kPageGapPt + kMarginPt;
    }
    updateScrollBars();
    viewport()->update();
}

void Viewer::updateScrollBars() {
    const QSize vp = viewport()->size();
    horizontalScrollBar()->setRange(0, std::max(0, int(m_contentW * m_zoom) - vp.width()));
    horizontalScrollBar()->setPageStep(vp.width());
    horizontalScrollBar()->setSingleStep(48);
    verticalScrollBar()->setRange(0, std::max(0, int(m_contentH * m_zoom) - vp.height()));
    verticalScrollBar()->setPageStep(vp.height());
    verticalScrollBar()->setSingleStep(48);   // brisker keyboard / scrollbar steps
}

QPointF Viewer::contentOffset() const {
    // When content is narrower than the viewport, center it.
    const QSize vp = viewport()->size();
    const qreal w = m_contentW * m_zoom;
    const qreal x = (w < vp.width()) ? (vp.width() - w) / 2.0 : -horizontalScrollBar()->value();
    return { x, qreal(-verticalScrollBar()->value()) };
}

QRectF Viewer::pageViewRect(int i) const {
    if (i < 0 || i >= int(m_pages.size())) return {};
    const QPointF off = contentOffset();
    const QRectF r = m_pages[size_t(i)].rect;
    return QRectF(off.x() + r.x() * m_zoom, off.y() + r.y() * m_zoom,
                  r.width() * m_zoom, r.height() * m_zoom);
}

int Viewer::pageAt(const QPoint& viewPos) const {
    for (int i = 0; i < int(m_pages.size()); ++i)
        if (pageViewRect(i).contains(viewPos)) return i;
    return -1;
}

QPointF Viewer::toPagePt(int page, const QPoint& viewPos) const {
    const QRectF r = pageViewRect(page);
    return { (viewPos.x() - r.x()) / m_zoom, (viewPos.y() - r.y()) / m_zoom };
}

QRect Viewer::emptyPlusCircle() const {
    QRect circle(0, 0, 64, 64);
    circle.moveCenter(viewport()->rect().center() - QPoint(0, 34));
    return circle;
}

QRect Viewer::emptyStateHitRect() const {
    // The "+" circle plus its captions below.
    return emptyPlusCircle().adjusted(-70, -10, 70, 74);
}

// ── painting ────────────────────────────────────────────────────────────────

void Viewer::invalidateCache() {
    m_cache.clear();
    m_cacheZoom = 0;
}

void Viewer::renderVisible() {
    if (!m_session->hasDocument()) return;
    const qreal dpr = devicePixelRatioF();
    if (m_cacheZoom != m_zoom) invalidateCache();
    m_cacheZoom = m_zoom;

    const QRect vp = viewport()->rect();
    for (int i = 0; i < int(m_pages.size()); ++i) {
        const QRectF pr = pageViewRect(i);
        if (!pr.intersects(vp)) continue;
        if (m_cache.count(i)) continue;
        QImage img = m_session->renderer()->renderPage(i, m_zoom * dpr);
        if (img.isNull()) continue;
        QPixmap pm = QPixmap::fromImage(std::move(img));
        pm.setDevicePixelRatio(dpr);
        m_cache[i] = std::move(pm);
    }

    // Evict pages far outside the viewport so long documents don't pile up
    // rasters (keep ~2 viewport-heights of slack either side).
    const qreal keepTop = vp.top() - 2.0 * vp.height();
    const qreal keepBot = vp.bottom() + 2.0 * vp.height();
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        const QRectF pr = pageViewRect(it->first);
        if (pr.bottom() < keepTop || pr.top() > keepBot)
            it = m_cache.erase(it);
        else
            ++it;
    }
}

void Viewer::paintEvent(QPaintEvent*) {
    // Deferred fit-width: by the first paint the viewport has its real size.
    if (m_pendingFitWidth && viewport()->width() > 50) {
        m_pendingFitWidth = false;
        fitPage();          // open showing the whole first page, not zoomed-in
        goToPage(0);        // a freshly opened document starts at the top
    }

    QPainter p(viewport());
    const QRect vp = viewport()->rect();

    // Figma-style canvas: soft white with a subtle dotted grid that pans
    // with the content. The grid is a cached tile so scrolling stays cheap.
    const QColor canvasBg("#FDFDFE");
    const qreal dprBg = devicePixelRatioF();
    if (m_dotTile.isNull() || !qFuzzyCompare(m_dotTile.devicePixelRatio(), dprBg)) {
        constexpr int step = 24;
        m_dotTile = QPixmap(qRound(step * dprBg), qRound(step * dprBg));
        m_dotTile.setDevicePixelRatio(dprBg);
        m_dotTile.fill(canvasBg);
        QPainter tp(&m_dotTile);
        tp.setRenderHint(QPainter::Antialiasing);
        tp.setPen(Qt::NoPen);
        tp.setBrush(QColor("#D8DCE4"));
        tp.drawEllipse(QPointF(step / 2.0, step / 2.0), 1.2, 1.2);
    }
    p.fillRect(vp, canvasBg);
    const QPointF bgOff = contentOffset();
    p.drawTiledPixmap(vp, m_dotTile, QPointF(-bgOff.x(), -bgOff.y()));

    if (!m_session->hasDocument()) {
        p.setRenderHint(QPainter::Antialiasing);

        // Centered "+" button with a soft halo, WPS/Figma-style empty state.
        const QRect circle = emptyPlusCircle();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(109, 91, 232, 26));
        p.drawEllipse(circle.adjusted(-8, -8, 8, 8));
        p.setBrush(QColor(m_emptyHover ? "#8674F0" : "#6D5BE8"));
        p.drawEllipse(circle);

        p.setPen(QPen(Qt::white, 3, Qt::SolidLine, Qt::RoundCap));
        const QPoint c = circle.center();
        p.drawLine(c - QPoint(12, 0), c + QPoint(12, 0));
        p.drawLine(c - QPoint(0, 12), c + QPoint(0, 12));

        p.setPen(QColor("#3B4152"));
        p.setFont(QFont("Segoe UI", 11, QFont::DemiBold));
        p.drawText(QRect(vp.left(), circle.bottom() + 16, vp.width(), 24),
                   Qt::AlignHCenter | Qt::AlignTop, tr("Click to add a PDF"));
        p.setPen(QColor("#9AA0AE"));
        p.setFont(QFont("Segoe UI", 9));
        p.drawText(QRect(vp.left(), circle.bottom() + 44, vp.width(), 20),
                   Qt::AlignHCenter | Qt::AlignTop,
                   tr("or drop a file anywhere on the canvas"));
        return;
    }

    renderVisible();

    for (int i = 0; i < int(m_pages.size()); ++i) {
        const QRectF pr = pageViewRect(i);
        if (!pr.intersects(viewport()->rect())) continue;

        // soft page shadow + white base (visible before raster arrives)
        p.fillRect(pr.translated(0, 2), QColor(20, 24, 40, 26));
        p.fillRect(pr, Qt::white);
        auto it = m_cache.find(i);
        if (it != m_cache.end())
            p.drawPixmap(pr.topLeft(), it->second);

        // hairline page edge so white pages read against the white canvas
        p.setPen(QColor("#E4E7ED"));
        p.setBrush(Qt::NoBrush);
        p.drawRect(pr.adjusted(0, 0, -1, -1));

        // overlays: search/highlight rects
        auto hi = m_highlights.find(i);
        if (hi != m_highlights.end()) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 200, 0, 90));
            for (const QRectF& r : hi->second)
                p.drawRect(QRectF(pr.x() + r.x() * m_zoom, pr.y() + r.y() * m_zoom,
                                  r.width() * m_zoom, r.height() * m_zoom));
        }

        // overlays: text selection
        auto sel = m_selectionRects.find(i);
        if (sel != m_selectionRects.end()) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(37, 99, 235, 70));
            for (const QRectF& r : sel->second)
                p.drawRect(QRectF(pr.x() + r.x() * m_zoom, pr.y() + r.y() * m_zoom,
                                  r.width() * m_zoom, r.height() * m_zoom));
        }
    }

    // rubber band / ink preview
    if (m_dragging && m_dragPage >= 0) {
        const QRectF pr = pageViewRect(m_dragPage);
        if (m_mode == Mode::PlaceRect && !m_rubberBandPt.isNull()) {
            const QRectF r(pr.x() + m_rubberBandPt.x() * m_zoom,
                           pr.y() + m_rubberBandPt.y() * m_zoom,
                           m_rubberBandPt.width() * m_zoom,
                           m_rubberBandPt.height() * m_zoom);
            p.setPen(QPen(QColor("#E8372A"), 1, Qt::DashLine));
            p.setBrush(QColor(232, 55, 42, 25));
            p.drawRect(r);
        } else if (m_mode == Mode::Ink && m_inkStrokePt.size() > 1) {
            QPolygonF poly;
            poly.reserve(m_inkStrokePt.size());
            for (const QPointF& pt : m_inkStrokePt)
                poly << QPointF(pr.x() + pt.x() * m_zoom, pr.y() + pt.y() * m_zoom);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(QColor("#E8372A"), 2.0 * m_zoom, Qt::SolidLine,
                          Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPolyline(poly);
        }
    }
}

// ── events ──────────────────────────────────────────────────────────────────

void Viewer::resizeEvent(QResizeEvent* ev) {
    QAbstractScrollArea::resizeEvent(ev);
    updateScrollBars();
}

void Viewer::scrollContentsBy(int, int) {
    updateCurrentPage();
    viewport()->update();
}

void Viewer::wheelEvent(QWheelEvent* ev) {
    if (ev->modifiers() & Qt::ControlModifier) {
        const qreal step = ev->angleDelta().y() > 0 ? 1.1 : 1.0 / 1.1;
        setZoom(m_zoom * step);
        ev->accept();
        return;
    }
    // Scroll a full wheel notch (~120px) per detent instead of the tiny default
    // single-step, so paging through a document feels responsive.
    const int dy = ev->angleDelta().y();
    const int dx = ev->angleDelta().x();
    if (dy != 0) verticalScrollBar()->setValue(verticalScrollBar()->value() - dy);
    if (dx != 0) horizontalScrollBar()->setValue(horizontalScrollBar()->value() - dx);
    if (dy != 0 || dx != 0) { ev->accept(); return; }
    QAbstractScrollArea::wheelEvent(ev);
}

void Viewer::mousePressEvent(QMouseEvent* ev) {
    // Middle button = hand-scroll (drag to pan the document), regardless of the
    // current tool — like the browser/Reader "scroll button" grab.
    if (ev->button() == Qt::MiddleButton && m_session->hasDocument()) {
        m_midPanning = true;
        m_midLast = ev->pos();
        viewport()->setCursor(Qt::SizeAllCursor);
        ev->accept();
        return;
    }
    if (ev->button() != Qt::LeftButton) return;
    if (!m_session->hasDocument()) {
        if (emptyStateHitRect().contains(ev->pos()))
            emit openRequested();
        return;
    }
    m_dragging = true;
    m_dragStartView = m_dragLastView = ev->pos();
    m_dragPage = pageAt(ev->pos());
    m_rubberBandPt = QRectF();
    m_inkStrokePt.clear();

    if (m_mode == Mode::Ink && m_dragPage >= 0)
        m_inkStrokePt << toPagePt(m_dragPage, ev->pos());
    if (m_mode == Mode::Select) {
        m_selectionRects.clear();
        m_selectedText.clear();
        emit selectionChanged({});
        viewport()->update();
    }
}

void Viewer::mouseMoveEvent(QMouseEvent* ev) {
    if (m_midPanning) {
        const QPoint d = ev->pos() - m_midLast;
        m_midLast = ev->pos();
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        ev->accept();
        return;
    }
    if (!m_session->hasDocument()) {
        const bool hover = emptyStateHitRect().contains(ev->pos());
        if (hover != m_emptyHover) {
            m_emptyHover = hover;
            viewport()->update();
        }
        viewport()->setCursor(hover ? Qt::PointingHandCursor : Qt::ArrowCursor);
        return;
    }
    if (!m_dragging) {
        if (m_mode == Mode::Pan)
            viewport()->setCursor(Qt::OpenHandCursor);
        else if (m_mode == Mode::Select && pageAt(ev->pos()) >= 0)
            viewport()->setCursor(Qt::IBeamCursor);
        else
            viewport()->setCursor(Qt::ArrowCursor);
        return;
    }

    switch (m_mode) {
    case Mode::Pan: {
        const QPoint d = ev->pos() - m_dragLastView;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        viewport()->setCursor(Qt::ClosedHandCursor);
        break;
    }
    case Mode::Select:
    case Mode::PlaceRect: {
        if (m_dragPage < 0) break;
        const QPointF a = toPagePt(m_dragPage, m_dragStartView);
        const QPointF b = toPagePt(m_dragPage, ev->pos());
        m_rubberBandPt = QRectF(a, b).normalized();
        if (m_mode == Mode::Select)
            updateSelection(m_dragPage, m_rubberBandPt);
        viewport()->update();
        break;
    }
    case Mode::Ink:
        if (m_dragPage >= 0) {
            m_inkStrokePt << toPagePt(m_dragPage, ev->pos());
            viewport()->update();
        }
        break;
    case Mode::PlaceClick:
        break;
    }
    m_dragLastView = ev->pos();
}

void Viewer::mouseReleaseEvent(QMouseEvent* ev) {
    if (ev->button() == Qt::MiddleButton && m_midPanning) {
        m_midPanning = false;
        viewport()->setCursor(m_mode == Mode::Pan ? Qt::OpenHandCursor : Qt::ArrowCursor);
        ev->accept();
        return;
    }
    if (ev->button() != Qt::LeftButton || !m_dragging) return;
    m_dragging = false;

    switch (m_mode) {
    case Mode::PlaceRect:
        if (m_dragPage >= 0 && m_rubberBandPt.width() > 2 && m_rubberBandPt.height() > 2)
            emit rectPlaced(m_placementTag, m_dragPage, m_rubberBandPt);
        m_rubberBandPt = QRectF();
        viewport()->update();
        break;
    case Mode::PlaceClick: {
        const int pg = pageAt(ev->pos());
        if (pg >= 0)
            emit clickPlaced(m_placementTag, pg, toPagePt(pg, ev->pos()));
        break;
    }
    case Mode::Ink:
        if (m_dragPage >= 0 && m_inkStrokePt.size() > 2)
            emit inkDrawn(m_placementTag, m_dragPage, m_inkStrokePt);
        m_inkStrokePt.clear();
        viewport()->update();
        break;
    case Mode::Pan:
        viewport()->setCursor(Qt::OpenHandCursor);
        break;
    case Mode::Select:
        break;
    }
}

void Viewer::contextMenuEvent(QContextMenuEvent* ev) {
    const int pg = pageAt(ev->pos());
    if (pg >= 0)
        emit contextMenuOnPage(pg, toPagePt(pg, ev->pos()), ev->globalPos());
}

// ── selection ───────────────────────────────────────────────────────────────

void Viewer::updateSelection(int page, const QRectF& dragPt) {
    m_selectionRects.clear();
    m_selectionRects[page] = m_session->renderer()->snapTextRects(page, dragPt);

    // Selected text: chars whose center falls inside the drag rect, in
    // extraction order.
    QString text;
    for (const auto& cb : m_session->renderer()->pageChars(page))
        if (dragPt.contains(cb.box.center())) text += cb.ch;
    if (m_selectedText != text) {
        m_selectedText = text;
        emit selectionChanged(text);
    }
}

// ── zoom / navigation ───────────────────────────────────────────────────────

void Viewer::setZoom(qreal factor) {
    factor = std::clamp(factor, 0.1, 6.4);
    if (qFuzzyCompare(factor, m_zoom)) return;

    // Keep the viewport-center point stable across the zoom change.
    const QPointF off = contentOffset();
    const QPointF centerContent((viewport()->width() / 2.0 - off.x()) / m_zoom,
                                (viewport()->height() / 2.0 - off.y()) / m_zoom);
    m_zoom = factor;
    invalidateCache();
    updateScrollBars();
    horizontalScrollBar()->setValue(int(centerContent.x() * m_zoom - viewport()->width() / 2.0));
    verticalScrollBar()->setValue(int(centerContent.y() * m_zoom - viewport()->height() / 2.0));
    updateCurrentPage();
    viewport()->update();
    emit zoomChanged(m_zoom);
}

void Viewer::zoomIn()  { setZoom(m_zoom * 1.2); }
void Viewer::zoomOut() { setZoom(m_zoom / 1.2); }

void Viewer::fitWidth() {
    if (m_contentW <= 0) return;
    setZoom(viewport()->width() / m_contentW);
}

void Viewer::fitWidthWhenReady() {
    // An unshown widget reports a default ~100px size, so "has a width" is
    // not enough — only trust the viewport once the widget is actually
    // visible; otherwise wait for the first real paint.
    if (isVisible() && viewport()->width() > 200) {
        fitPage();          // open at whole-page view; users zoom in as needed
        goToPage(0);
    } else {
        m_pendingFitWidth = true;
    }
}

void Viewer::fitPage() {
    if (m_pages.empty()) return;
    const QRectF r = m_pages[size_t(std::clamp(m_currentPage, 0, int(m_pages.size()) - 1))].rect;
    const qreal zw = viewport()->width()  / (r.width()  + 2 * kMarginPt);
    const qreal zh = viewport()->height() / (r.height() + 2 * kMarginPt);
    setZoom(std::min(zw, zh));
}

void Viewer::goToPage(int pageIndex) {
    if (pageIndex < 0 || pageIndex >= int(m_pages.size())) return;
    verticalScrollBar()->setValue(int((m_pages[size_t(pageIndex)].rect.top() - kMarginPt / 2) * m_zoom));
    updateCurrentPage();
}

void Viewer::updateCurrentPage() {
    const QPoint center(viewport()->width() / 2, viewport()->height() / 2);
    int best = m_currentPage;
    qreal bestDist = 1e18;
    for (int i = 0; i < int(m_pages.size()); ++i) {
        const QRectF pr = pageViewRect(i);
        if (pr.contains(center)) { best = i; bestDist = 0; break; }
        const qreal d = std::abs(pr.center().y() - center.y());
        if (d < bestDist) { bestDist = d; best = i; }
    }
    if (best != m_currentPage) {
        m_currentPage = best;
        emit currentPageChanged(best);
    }
}

// ── overlays / modes ────────────────────────────────────────────────────────

void Viewer::setMode(Mode m, const QString& placementTag) {
    m_mode = m;
    m_placementTag = placementTag;
    viewport()->setCursor(m == Mode::Pan ? Qt::OpenHandCursor
                        : m == Mode::Ink ? Qt::CrossCursor
                        : m == Mode::PlaceRect ? Qt::CrossCursor
                        : m == Mode::PlaceClick ? Qt::PointingHandCursor
                        : Qt::ArrowCursor);
}

void Viewer::setHighlightRects(int pageIndex, const std::vector<QRectF>& rects) {
    m_highlights[pageIndex] = rects;
    viewport()->update();
}

void Viewer::clearHighlights() {
    m_highlights.clear();
    viewport()->update();
}

void Viewer::refresh() {
    invalidateCache();
    m_selectionRects.clear();
    m_selectedText.clear();
    m_highlights.clear();
    relayout();
    updateCurrentPage();
}

} // namespace NativeOffice::Pdf
