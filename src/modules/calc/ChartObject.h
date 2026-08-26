#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ChartObject.h  (Sprint 26 — Charts)
// A floating, draggable, resizable chart rendered over the Calc grid via
// Qt Charts. Reads its data from a SpreadsheetModel range and rebuilds on
// demand (live refresh when the underlying cells change).
// ─────────────────────────────────────────────────────────────────────────────

#include <QFrame>
#include <QRect>
#include <QPoint>
#include <QString>
#include <functional>

#include "ChartSpec.h"

class QLabel;
class QToolButton;
QT_BEGIN_NAMESPACE
class QChartView;
class QValueAxis;
QT_END_NAMESPACE

namespace NativeOffice {

class SpreadsheetModel;

// Paints picture-filled bars over the chart. See ChartObject.cpp: Qt Charts
// fills a bar by painting a brush into a rectangle, so a brush alone can never
// give a bar the outline of the picture it is filled with.
class PictureBarOverlay;

class ChartObject : public QFrame {
    Q_OBJECT
public:
    // An imported chart can plot cells from a sheet other than the one it sits
    // on, so the object needs a way to reach sibling sheets by name.
    using SheetResolver = std::function<SpreadsheetModel*(const QString&)>;

    ChartObject(SpreadsheetModel* model, const ChartSpec& spec, QWidget* parent);

    // Must be set before the first rebuild for cross-sheet references to
    // resolve; without it they fall back to the sheet the chart sits on.
    void setSheetResolver(SheetResolver r) { m_resolveSheet = std::move(r); }

    // Rebuild the chart from the model + range (call after data changes).
    void rebuild();

    // The stored spec with the object's current placement folded back in.
    [[nodiscard]] ChartSpec spec() const;
    [[nodiscard]] ChartType type() const { return m_spec.type; }
    [[nodiscard]] QRect     range() const { return m_spec.range; }
    [[nodiscard]] const CellAnchor& anchor() const { return m_spec.anchor; }
    void setAnchor(const CellAnchor& a) { m_spec.anchor = a; }

    void setChartType(ChartType t) { m_spec.type = t; rebuild(); }

private:
    void applyValueAxisFormat(QValueAxis* axis) const;
public:

    // Excel-style: chrome (border + handles + delete) only shows when selected.
    void setSelected(bool on);

signals:
    void closed(ChartObject* self);
    void geometryEdited();          // emitted after a drag/resize finishes
    void selected(ChartObject* self);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    SpreadsheetModel* m_model { nullptr };
    // The whole imported description is kept, not just type+range: dragging a
    // chart round-trips through spec(), and rebuilding it from three fields
    // would silently throw away an imported chart's series and title.
    ChartSpec         m_spec;
    SheetResolver     m_resolveSheet;

    QWidget*     m_grip     { nullptr };
    QChartView*  m_view     { nullptr };
    // Sits over the view and draws the bars whose fill is a picture. Always
    // present; it has nothing to paint unless a series carries an image.
    PictureBarOverlay* m_picBars { nullptr };

    // Drag / resize state.
    bool   m_dragging { false };
    bool   m_resizing { false };
    QPoint m_pressPos;          // global pos at press
    QRect  m_startGeom;         // geometry at press
};

} // namespace NativeOffice
