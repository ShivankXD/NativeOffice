#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfViewer.h — the central page canvas of the PDF editor: continuous
// vertical page layout, zoom (Ctrl+wheel / fit-width / fit-page), a raster
// cache over Pdf::Renderer, and interaction modes.
//
// Interaction is mode-based, mirroring the ribbon's tools:
//   • Pan          — drag scrolls
//   • Select       — text selection (char-box based) + annotation hit tests
//   • PlaceRect    — rubber-band a rectangle, then rectPlaced() fires with
//                    page + rect in page points (used by highlight-area, text
//                    box, shapes, link, crop, wipe-off, signature placement…)
//   • PlaceClick   — single click placement (notes, text comments)
//   • Ink          — freehand polyline capture, inkDrawn() on release
// What each placement *means* is the ribbon/module's business — the viewer
// only reports geometry. Overlay decorations (search hits, pending
// placements) are drawn on top of the rendered page.
// ─────────────────────────────────────────────────────────────────────────────

#include <QAbstractScrollArea>
#include <QPixmap>
#include <QPolygonF>
#include <map>
#include <vector>

namespace NativeOffice::Pdf {

class EditSession;

class Viewer : public QAbstractScrollArea {
    Q_OBJECT

public:
    enum class Mode { Pan, Select, PlaceRect, PlaceClick, Ink };

    explicit Viewer(EditSession* session, QWidget* parent = nullptr);

    void setMode(Mode m, const QString& placementTag = {});
    [[nodiscard]] Mode mode() const { return m_mode; }

    // ── zoom / navigation ───────────────────────────────────────────────
    void setZoom(qreal factor);                 // 0.1 … 6.4
    [[nodiscard]] qreal zoom() const { return m_zoom; }
    void zoomIn();
    void zoomOut();
    void fitWidth();
    // fitWidth once the viewport has a real size (safe to call before the
    // widget is shown — e.g. right after opening a file from the CLI).
    void fitWidthWhenReady();
    void fitPage();
    void goToPage(int pageIndex);               // scrolls page top to view top
    [[nodiscard]] int currentPage() const { return m_currentPage; }

    // ── overlays ────────────────────────────────────────────────────────
    // Highlight rectangles (page points, top-left origin), e.g. search hits.
    void setHighlightRects(int pageIndex, const std::vector<QRectF>& rects);
    void clearHighlights();

    // Current text selection as line rects + the selected text.
    [[nodiscard]] QString selectedText() const { return m_selectedText; }

signals:
    void currentPageChanged(int pageIndex);
    void zoomChanged(qreal factor);
    void selectionChanged(const QString& text);
    void openRequested();               // empty-state "+" button clicked

    // Geometry reports (page points, top-left origin), per the active mode.
    void rectPlaced(const QString& tag, int pageIndex, const QRectF& rectPt);
    void clickPlaced(const QString& tag, int pageIndex, const QPointF& posPt);
    void inkDrawn(const QString& tag, int pageIndex, const QPolygonF& strokePt);

    void contextMenuOnPage(int pageIndex, const QPointF& posPt, const QPoint& globalPos);

public slots:
    void refresh();                             // document changed: relayout + re-render

protected:
    void paintEvent(QPaintEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;
    void scrollContentsBy(int dx, int dy) override;
    void wheelEvent(QWheelEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void contextMenuEvent(QContextMenuEvent* ev) override;

private:
    struct PageGeom {
        QRectF rect;        // in unzoomed content coordinates (points + gaps)
    };

    void relayout();                            // recompute page positions
    void updateScrollBars();
    void renderVisible();                       // fill cache for visible pages
    void invalidateCache();
    [[nodiscard]] QPointF contentOffset() const;
    [[nodiscard]] QRectF pageViewRect(int i) const;       // page rect in viewport px
    [[nodiscard]] int pageAt(const QPoint& viewPos) const; // -1 if in a gap
    [[nodiscard]] QPointF toPagePt(int page, const QPoint& viewPos) const;
    [[nodiscard]] QRect emptyPlusCircle() const;     // empty-state "+" geometry
    [[nodiscard]] QRect emptyStateHitRect() const;   // clickable empty-state area
    void updateCurrentPage();
    void updateSelection(int page, const QRectF& dragPt);

    EditSession* m_session;
    std::vector<PageGeom> m_pages;
    qreal  m_contentW = 0, m_contentH = 0;      // unzoomed content size

    qreal m_zoom = 1.0;
    Mode  m_mode = Mode::Select;
    QString m_placementTag;

    // raster cache: pageIndex → pixmap at (m_zoom × devicePixelRatio)
    mutable std::map<int, QPixmap> m_cache;
    qreal m_cacheZoom = 0;

    QPixmap m_dotTile;                          // dotted-grid background tile
    bool m_emptyHover = false;                  // hovering the empty-state "+"

    int m_currentPage = 0;

    bool m_pendingFitWidth = false;

    // interaction state
    bool m_dragging = false;
    QPoint m_dragStartView;
    QPoint m_dragLastView;
    int m_dragPage = -1;
    bool   m_midPanning = false;   // middle-button hand-scroll active
    QPoint m_midLast;              // last cursor pos during middle-button pan
    QRectF m_rubberBandPt;                      // current drag rect in page pt
    QPolygonF m_inkStrokePt;

    // overlays
    std::map<int, std::vector<QRectF>> m_highlights;
    std::map<int, std::vector<QRectF>> m_selectionRects;
    QString m_selectedText;

    static constexpr qreal kPageGapPt = 14.0;   // gap between pages (points)
    static constexpr qreal kMarginPt  = 18.0;   // outer margin (points)
};

} // namespace NativeOffice::Pdf
