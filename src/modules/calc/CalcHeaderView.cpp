// ─────────────────────────────────────────────────────────────────────────────
// CalcHeaderView.cpp  (Sprint 4)
// ─────────────────────────────────────────────────────────────────────────────
#include "CalcHeaderView.h"

#include <QPainter>
#include <QFont>
#include <QFontMetrics>

namespace NativeOffice {

CalcHeaderView::CalcHeaderView(Qt::Orientation orientation, QWidget* parent)
    : QHeaderView(orientation, parent)
{
    setSectionsClickable(true);
    setHighlightSections(true);
    setSectionResizeMode(QHeaderView::Interactive);
    setDefaultAlignment(Qt::AlignCenter);
}

void CalcHeaderView::setHighlightedSections(const QSet<int>& sections) {
    m_highlighted = sections;
    viewport()->update();
}

void CalcHeaderView::paintSection(QPainter*    painter,
                                   const QRect& rect,
                                   int          logicalIndex) const {
    painter->save();
    painter->setClipRect(rect);

    const bool isHorizontal = (orientation() == Qt::Horizontal);
    const bool highlighted  = m_highlighted.contains(logicalIndex);

    // Excel palette: light-gray headers, green highlight for the selected
    // row/column, a green accent line on the inner edge, thin gray borders.
    const QColor kHeaderBg   ("#F5F5F5");
    const QColor kSelectedBg ("#CFE7DA");   // light green
    const QColor kBorder     ("#D4D4D4");
    const QColor kText       ("#5A5A5A");
    const QColor kSelText    ("#0E703C");   // Excel green
    const QColor kAccent     ("#107C41");   // Excel green accent

    // ── Background ─────────────────────────────────────────────────────────
    painter->fillRect(rect, highlighted ? kSelectedBg : kHeaderBg);

    // ── Thin gray borders (right + bottom) ───────────────────────────────────
    painter->setPen(QPen(kBorder, 1));
    painter->drawLine(rect.topRight(),   rect.bottomRight());
    painter->drawLine(rect.bottomLeft(), rect.bottomRight());

    // ── Green accent line along the grid-facing edge when selected ────────────
    if (highlighted) {
        painter->setPen(QPen(kAccent, 2));
        if (isHorizontal) {
            const int y = rect.bottom() - 1;
            painter->drawLine(rect.left(), y, rect.right(), y);
        } else {
            const int x = rect.right() - 1;
            painter->drawLine(x, rect.top(), x, rect.bottom());
        }
    }

    // ── Label ───────────────────────────────────────────────────────────────
    QFont f = painter->font();
    f.setFamily("Segoe UI");
    f.setPointSize(9);
    f.setWeight(highlighted ? QFont::DemiBold : QFont::Normal);
    painter->setFont(f);

    painter->setPen(highlighted ? kSelText : kText);

    const QRect textRect = rect.adjusted(2, 1, -2, -1);
    const QString label  = model()->headerData(logicalIndex, orientation()).toString();
    painter->drawText(textRect, Qt::AlignCenter, label);

    painter->restore();
}

QSize CalcHeaderView::sectionSizeFromContents(int logicalIndex) const {
    const QSize base = QHeaderView::sectionSizeFromContents(logicalIndex);
    if (orientation() == Qt::Horizontal)
        return QSize(base.width(), 28);    // taller column header
    return QSize(48, base.height());       // wider row header
}

} // namespace NativeOffice
