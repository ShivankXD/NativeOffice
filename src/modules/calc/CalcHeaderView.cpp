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

    // ── Background ─────────────────────────────────────────────────────────
    QColor bgColor;
    if (highlighted) {
        bgColor = QColor("#E8372A");               // Scarlet active
    } else if (isHorizontal) {
        bgColor = QColor("#2C3140");               // Charcoal col header
    } else {
        bgColor = QColor("#343848");               // Slightly lighter row header
    }
    painter->fillRect(rect, bgColor);

    // ── Bottom/right border ─────────────────────────────────────────────────
    painter->setPen(QPen(QColor("#1A1F2E"), 1));
    if (isHorizontal) {
        painter->drawLine(rect.bottomLeft(), rect.bottomRight());
        painter->drawLine(rect.topRight(), rect.bottomRight());
    } else {
        painter->drawLine(rect.topRight(), rect.bottomRight());
        painter->drawLine(rect.bottomLeft(), rect.bottomRight());
    }

    // ── Label ───────────────────────────────────────────────────────────────
    QFont f = painter->font();
    f.setFamily("Segoe UI");
    f.setPointSize(10);
    f.setWeight(highlighted ? QFont::Bold : QFont::Medium);
    painter->setFont(f);

    painter->setPen(highlighted ? Qt::white
                                : QColor(255, 255, 255, 200));

    // Leave a 2 px inset so text doesn't touch the border
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
