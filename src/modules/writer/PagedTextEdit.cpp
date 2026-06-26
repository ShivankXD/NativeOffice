// ─────────────────────────────────────────────────────────────────────────────
// PagedTextEdit.cpp  (Sprint 17 — real pagination)
// ─────────────────────────────────────────────────────────────────────────────
#include "PagedTextEdit.h"

#include <QPainter>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QLinearGradient>
#include <QResizeEvent>
#include <QtMath>
#include <cmath>

namespace NativeOffice {

PagedTextEdit::PagedTextEdit(QWidget* parent)
    : QTextEdit(parent)
{
    setFrameShape(QFrame::NoFrame);
    // The widget grows to its full multi-page height; the surrounding
    // QScrollArea does the scrolling, so we never scroll internally.
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    document()->setPageSize(QSizeF(m_pageW, m_pageH));
    document()->setDocumentMargin(m_margin);

    connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
            this, [this](const QSizeF&) { syncHeight(); });
}

void PagedTextEdit::setPageMetrics(double pageW, double pageH, double margin) {
    m_pageW = pageW; m_pageH = pageH; m_margin = margin;
    document()->setDocumentMargin(margin);
    if (m_paged) {
        document()->setPageSize(QSizeF(pageW, pageH));
        setFixedWidth(qRound(pageW));
    }
    syncHeight();
    viewport()->update();
}

void PagedTextEdit::setPaged(bool on) {
    if (m_paged == on) return;
    m_paged = on;
    if (on) {
        document()->setPageSize(QSizeF(m_pageW, m_pageH));
        setFixedWidth(qRound(m_pageW));
    } else {
        // Continuous flow — let the document grow to a single tall page.
        document()->setPageSize(QSizeF());
    }
    syncHeight();
    viewport()->update();
}

void PagedTextEdit::setPaperColor(const QColor& c) {
    m_paper = c.isValid() ? c : QColor("#FFFFFF");
    viewport()->update();
}

int PagedTextEdit::pageCountValue() const {
    return m_paged ? qMax(1, document()->pageCount()) : 1;
}

void PagedTextEdit::syncHeight() {
    double h = m_paged ? pageCountValue() * m_pageH
                       : document()->size().height();
    if (h < 1.0) h = m_pageH;
    setFixedHeight(static_cast<int>(std::ceil(h)) + 1);
}

void PagedTextEdit::resizeEvent(QResizeEvent* e) {
    QTextEdit::resizeEvent(e);
    // QTextEdit re-derives the document width on resize; re-assert pagination.
    if (m_paged) document()->setPageSize(QSizeF(m_pageW, m_pageH));
}

void PagedTextEdit::paintEvent(QPaintEvent* e) {
    // 1) Let QTextEdit paint the paper (its background) + text + selection/cursor.
    QTextEdit::paintEvent(e);
    if (!m_paged) return;

    // 2) Overlay page chrome: gray gaps carved from the inter-page margins,
    //    soft shadows, and footer page numbers — drawn in widget coordinates,
    //    which equal document coordinates because we never scroll internally.
    const int pages = pageCountValue();
    const int w = viewport()->width();

    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing);

    const double gap  = qBound(18.0, m_margin * 0.66, 46.0);   // gray gap height
    const double half = gap / 2.0;
    const double scale = m_pageH / 1123.0;
    QFont ff("Segoe UI", qMax(7, qRound(8.5 * scale)));
    p.setFont(ff);

    for (int i = 0; i < pages; ++i) {
        const double bottom = (i + 1) * m_pageH;        // page i bottom edge

        // Gap band between this page and the next (not after the last page).
        if (i < pages - 1) {
            p.fillRect(QRectF(0, bottom - half, w, gap), m_desk);
            // shadow under the upper page's edge
            QLinearGradient up(0, bottom - half - 7, 0, bottom - half);
            up.setColorAt(0.0, QColor(0, 0, 0, 0));
            up.setColorAt(1.0, QColor(0, 0, 0, 38));
            p.fillRect(QRectF(0, bottom - half - 7, w, 7), up);
            // faint top highlight on the lower page's edge
            p.fillRect(QRectF(0, bottom + half, w, 1), QColor(0, 0, 0, 18));
        }

        // Footer page number, centred in the white part of the bottom margin.
        const double footTop = bottom - m_margin;
        const double footBot = (i < pages - 1) ? bottom - half - 6 : bottom - m_margin * 0.30;
        if (footBot - footTop > 8.0) {
            p.setPen(QColor(150, 153, 160));
            p.drawText(QRectF(m_margin, footTop, w - 2 * m_margin, footBot - footTop),
                       Qt::AlignHCenter | Qt::AlignVCenter,
                       QString("Page %1 of %2").arg(i + 1).arg(pages));
        }
    }
    p.end();
}

} // namespace NativeOffice
