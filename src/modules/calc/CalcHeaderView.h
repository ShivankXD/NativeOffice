#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// CalcHeaderView.h  (Sprint 4)
// Custom QHeaderView for the NativeOffice Calc grid.
//
// Horizontal header (column letters A–Z):
//   Background  : Charcoal  #2C3140
//   Text        : White
//   Selected col: Scarlet   #E8372A
//
// Vertical header (row numbers 1–100):
//   Background  : #3A4054  (slightly lighter charcoal)
//   Text        : rgba(255,255,255,0.75)
//   Selected row: Scarlet   #E8372A
// ─────────────────────────────────────────────────────────────────────────────

#include <QHeaderView>
#include <QSet>

namespace NativeOffice {

class CalcHeaderView : public QHeaderView {
    Q_OBJECT

public:
    explicit CalcHeaderView(Qt::Orientation orientation,
                            QWidget*        parent = nullptr);

    // Set which sections are highlighted (e.g. selected column/row)
    void setHighlightedSections(const QSet<int>& sections);

protected:
    void paintSection(QPainter* painter,
                      const QRect& rect,
                      int logicalIndex) const override;
    QSize sectionSizeFromContents(int logicalIndex) const override;

private:
    QSet<int> m_highlighted;
};

} // namespace NativeOffice
