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

#include "ChartSpec.h"

class QLabel;
class QToolButton;
QT_BEGIN_NAMESPACE
class QChartView;
QT_END_NAMESPACE

namespace NativeOffice {

class SpreadsheetModel;

class ChartObject : public QFrame {
    Q_OBJECT
public:
    ChartObject(SpreadsheetModel* model, const ChartSpec& spec, QWidget* parent);

    // Rebuild the chart from the model + range (call after data changes).
    void rebuild();

    [[nodiscard]] ChartSpec spec() const;
    [[nodiscard]] ChartType type() const { return m_type; }
    [[nodiscard]] QRect     range() const { return m_range; }

    void setChartType(ChartType t) { m_type = t; rebuild(); }

signals:
    void closed(ChartObject* self);
    void geometryEdited();          // emitted after a drag/resize finishes
    void selected(ChartObject* self);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    SpreadsheetModel* m_model { nullptr };
    ChartType         m_type  { ChartType::Column };
    QRect             m_range;

    QWidget*     m_bar      { nullptr };
    QLabel*      m_titleBar { nullptr };
    QToolButton* m_closeBtn { nullptr };
    QToolButton* m_typeBtn  { nullptr };
    QWidget*     m_grip     { nullptr };
    QChartView*  m_view     { nullptr };

    // Drag / resize state.
    bool   m_dragging { false };
    bool   m_resizing { false };
    QPoint m_pressPos;          // global pos at press
    QRect  m_startGeom;         // geometry at press
};

} // namespace NativeOffice
