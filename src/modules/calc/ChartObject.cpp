// ─────────────────────────────────────────────────────────────────────────────
// ChartObject.cpp  (Sprint 26 — Charts)
// ─────────────────────────────────────────────────────────────────────────────
#include "ChartObject.h"
#include "SpreadsheetModel.h"
#include "CalcIcons.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QMenu>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QPainter>
#include <QVector>
#include <QStringList>
#include <QMargins>
#include <algorithm>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QAbstractBarSeries>
#include <QtCharts/QBarSeries>
#include <QtCharts/QHorizontalBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QLineSeries>
#include <QtCharts/QAreaSeries>
#include <QtCharts/QPieSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QLegend>

namespace NativeOffice {

QString chartTypeName(ChartType t) {
    switch (t) {
    case ChartType::Column:  return "Column";
    case ChartType::Bar:     return "Bar";
    case ChartType::Line:    return "Line";
    case ChartType::Area:    return "Area";
    case ChartType::Pie:     return "Pie";
    case ChartType::Scatter: return "Scatter";
    }
    return "Chart";
}

ChartType chartTypeFromInt(int v) {
    if (v < 0 || v > int(ChartType::Scatter)) return ChartType::Column;
    return static_cast<ChartType>(v);
}

// ─────────────────────────────────────────────────────────────────────────────
ChartObject::ChartObject(SpreadsheetModel* model, const ChartSpec& spec, QWidget* parent)
    : QFrame(parent)
    , m_model(model)
    , m_type(spec.type)
    , m_range(spec.range)
{
    setObjectName("chartObject");
    setFrameShape(QFrame::NoFrame);
    setStyleSheet(
        "QFrame#chartObject{background:#FFFFFF;border:1px solid #C2C7CE;border-radius:5px;}");

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(1, 1, 1, 1);
    v->setSpacing(0);

    // ── Title bar (drag handle) ───────────────────────────────────────────────
    m_bar = new QWidget(this);
    m_bar->setObjectName("chartBar");
    m_bar->setFixedHeight(24);
    m_bar->setCursor(Qt::SizeAllCursor);
    m_bar->setStyleSheet(
        "QWidget#chartBar{background:#F3F4F6;border-top-left-radius:4px;"
        "border-top-right-radius:4px;border-bottom:1px solid #E3E6EA;}"
        "QToolButton{border:none;background:transparent;color:#5A5F66;padding:2px;}"
        "QToolButton:hover{background:#E2E8E4;border-radius:3px;}");
    auto* bl = new QHBoxLayout(m_bar);
    bl->setContentsMargins(8, 0, 4, 0);
    bl->setSpacing(2);
    m_titleBar = new QLabel("Chart", m_bar);
    m_titleBar->setStyleSheet("color:#3A3D42;font:600 11px 'Segoe UI';background:transparent;");
    m_typeBtn = new QToolButton(m_bar);
    m_typeBtn->setIcon(calcIcon("chart"));
    m_typeBtn->setIconSize(QSize(15, 15));
    m_typeBtn->setToolTip("Change chart type");
    m_typeBtn->setPopupMode(QToolButton::InstantPopup);
    m_typeBtn->setFocusPolicy(Qt::NoFocus);
    {
        auto* menu = new QMenu(m_typeBtn);
        const ChartType types[] = { ChartType::Column, ChartType::Bar, ChartType::Line,
                                    ChartType::Area, ChartType::Pie, ChartType::Scatter };
        for (ChartType t : types) {
            connect(menu->addAction(chartTypeName(t)), &QAction::triggered, this, [this, t]{
                setChartType(t);
                emit geometryEdited();
            });
        }
        m_typeBtn->setMenu(menu);
    }
    m_closeBtn = new QToolButton(m_bar);
    m_closeBtn->setText("✕");
    m_closeBtn->setToolTip("Delete chart");
    m_closeBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_closeBtn, &QToolButton::clicked, this, [this]{ emit closed(this); });
    bl->addWidget(m_titleBar, 1);
    bl->addWidget(m_typeBtn);
    bl->addWidget(m_closeBtn);

    // ── Chart view ────────────────────────────────────────────────────────────
    m_view = new QChartView(this);
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setStyleSheet("background:transparent;border:none;");

    v->addWidget(m_bar);
    v->addWidget(m_view, 1);

    // ── Resize grip (bottom-right) ────────────────────────────────────────────
    m_grip = new QWidget(this);
    m_grip->setFixedSize(16, 16);
    m_grip->setCursor(Qt::SizeFDiagCursor);
    m_grip->setStyleSheet("background:transparent;");

    setGeometry(spec.geom.isValid() ? spec.geom : QRect(40, 40, 420, 280));

    m_bar->installEventFilter(this);
    m_titleBar->installEventFilter(this);
    m_grip->installEventFilter(this);

    rebuild();
}

ChartSpec ChartObject::spec() const {
    ChartSpec s;
    s.type  = m_type;
    s.range = m_range;
    s.geom  = geometry();
    return s;
}

void ChartObject::resizeEvent(QResizeEvent* e) {
    QFrame::resizeEvent(e);
    if (m_grip) {
        m_grip->move(width() - m_grip->width(), height() - m_grip->height());
        m_grip->raise();
    }
}

void ChartObject::mousePressEvent(QMouseEvent* e) {
    emit selected(this);
    raise();
    QFrame::mousePressEvent(e);
}

// ── Drag (title bar) + resize (grip) via event filters ─────────────────────────
bool ChartObject::eventFilter(QObject* w, QEvent* e) {
    if (w == m_bar || w == m_titleBar) {
        if (e->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(e);
            if (me->button() == Qt::LeftButton) {
                m_dragging  = true;
                m_pressPos  = me->globalPosition().toPoint();
                m_startGeom = geometry();
                emit selected(this);
                raise();
                return true;
            }
        } else if (e->type() == QEvent::MouseMove && m_dragging) {
            auto* me = static_cast<QMouseEvent*>(e);
            const QPoint d = me->globalPosition().toPoint() - m_pressPos;
            QRect g = m_startGeom;
            g.moveTopLeft(m_startGeom.topLeft() + d);
            if (parentWidget()) {
                const QRect pr = parentWidget()->rect();
                if (g.left() < 0) g.moveLeft(0);
                if (g.top()  < 0) g.moveTop(0);
                if (g.right()  > pr.right())  g.moveRight(pr.right());
                if (g.bottom() > pr.bottom()) g.moveBottom(pr.bottom());
            }
            setGeometry(g);
            return true;
        } else if (e->type() == QEvent::MouseButtonRelease && m_dragging) {
            m_dragging = false;
            emit geometryEdited();
            return true;
        }
    }
    if (w == m_grip) {
        if (e->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(e);
            if (me->button() == Qt::LeftButton) {
                m_resizing  = true;
                m_pressPos  = me->globalPosition().toPoint();
                m_startGeom = geometry();
                return true;
            }
        } else if (e->type() == QEvent::MouseMove && m_resizing) {
            auto* me = static_cast<QMouseEvent*>(e);
            const QPoint d = me->globalPosition().toPoint() - m_pressPos;
            resize(std::max(200, m_startGeom.width()  + d.x()),
                   std::max(140, m_startGeom.height() + d.y()));
            return true;
        } else if (e->type() == QEvent::MouseButtonRelease && m_resizing) {
            m_resizing = false;
            emit geometryEdited();
            return true;
        }
    }
    return QFrame::eventFilter(w, e);
}

// ─────────────────────────────────────────────────────────────────────────────
// Build the chart from the model range.
// ─────────────────────────────────────────────────────────────────────────────
void ChartObject::rebuild() {
    if (!m_model) return;
    const int c1 = m_range.left(), c2 = m_range.right();
    const int r1 = m_range.top(),  r2 = m_range.bottom();

    auto disp  = [&](int c, int r) { return m_model->displayValue(c, r); };
    auto isNum = [&](int c, int r) {
        const QString d = disp(c, r);
        if (d.isEmpty()) return false;
        bool ok = false; d.toDouble(&ok); return ok;
    };

    // Header row: top row carries text labels and there is data below it.
    bool headerRow = false;
    if (r2 > r1)
        for (int c = c1; c <= c2; ++c)
            if (!disp(c, r1).isEmpty() && !isNum(c, r1)) { headerRow = true; break; }
    const int firstDataRow = headerRow ? r1 + 1 : r1;

    // Category column: left column carries text labels.
    bool catCol = false;
    if (c2 > c1)
        for (int r = firstDataRow; r <= r2; ++r)
            if (!disp(c1, r).isEmpty() && !isNum(c1, r)) { catCol = true; break; }
    const int firstDataCol = catCol ? c1 + 1 : c1;

    QStringList cats;
    for (int r = firstDataRow; r <= r2; ++r)
        cats << (catCol ? disp(c1, r) : QString::number(r - firstDataRow + 1));

    struct Series { QString name; QVector<double> vals; };
    QVector<Series> series;
    for (int c = firstDataCol; c <= c2; ++c) {
        Series s;
        s.name = headerRow ? disp(c, r1) : QString();
        if (s.name.isEmpty()) s.name = QString("Series %1").arg(c - firstDataCol + 1);
        for (int r = firstDataRow; r <= r2; ++r) {
            bool ok = false; const double val = disp(c, r).toDouble(&ok);
            s.vals << (ok ? val : 0.0);
        }
        series << s;
    }

    auto* chart = new QChart();
    chart->setAnimationOptions(QChart::SeriesAnimations);
    chart->setBackgroundRoundness(0);
    chart->setMargins(QMargins(4, 2, 4, 2));
    chart->legend()->setVisible(series.size() > 1 || m_type == ChartType::Pie);
    chart->legend()->setAlignment(Qt::AlignBottom);

    const QString cornerTitle = (headerRow && catCol) ? disp(c1, r1) : QString();
    chart->setTitle(cornerTitle.isEmpty()
                        ? QString("%1 Chart").arg(chartTypeName(m_type))
                        : cornerTitle);
    m_titleBar->setText(chart->title());

    switch (m_type) {
    case ChartType::Column:
    case ChartType::Bar: {
        QAbstractBarSeries* bs = (m_type == ChartType::Column)
            ? static_cast<QAbstractBarSeries*>(new QBarSeries())
            : static_cast<QAbstractBarSeries*>(new QHorizontalBarSeries());
        for (const auto& s : series) {
            auto* set = new QBarSet(s.name);
            for (double v : s.vals) *set << v;
            bs->append(set);
        }
        chart->addSeries(bs);
        auto* axCat = new QBarCategoryAxis(); axCat->append(cats);
        auto* axVal = new QValueAxis(); 
        if (m_type == ChartType::Column) {
            chart->addAxis(axCat, Qt::AlignBottom);
            chart->addAxis(axVal, Qt::AlignLeft);
        } else {
            chart->addAxis(axCat, Qt::AlignLeft);
            chart->addAxis(axVal, Qt::AlignBottom);
        }
        bs->attachAxis(axCat);
        bs->attachAxis(axVal);
        break;
    }
    case ChartType::Line:
    case ChartType::Area: {
        for (const auto& s : series) {
            auto* ls = new QLineSeries();
            ls->setName(s.name);
            for (int i = 0; i < s.vals.size(); ++i) ls->append(i, s.vals[i]);
            if (m_type == ChartType::Area) {
                auto* as = new QAreaSeries(ls);
                as->setName(s.name);
                chart->addSeries(as);
            } else {
                chart->addSeries(ls);
            }
        }
        auto* axCat = new QBarCategoryAxis(); axCat->append(cats);
        auto* axVal = new QValueAxis(); 
        chart->addAxis(axCat, Qt::AlignBottom);
        chart->addAxis(axVal, Qt::AlignLeft);
        for (auto* s : chart->series()) { s->attachAxis(axCat); s->attachAxis(axVal); }
        break;
    }
    case ChartType::Pie: {
        auto* ps = new QPieSeries();
        const Series first = series.isEmpty() ? Series{} : series.first();
        for (int i = 0; i < cats.size() && i < first.vals.size(); ++i)
            ps->append(cats[i], first.vals[i]);
        ps->setLabelsVisible(true);
        chart->addSeries(ps);
        break;
    }
    case ChartType::Scatter: {
        for (const auto& s : series) {
            auto* ss = new QScatterSeries();
            ss->setName(s.name);
            ss->setMarkerSize(11);
            for (int i = 0; i < s.vals.size(); ++i) ss->append(i, s.vals[i]);
            chart->addSeries(ss);
        }
        auto* axX = new QValueAxis();
        auto* axY = new QValueAxis();
        chart->addAxis(axX, Qt::AlignBottom);
        chart->addAxis(axY, Qt::AlignLeft);
        for (auto* s : chart->series()) { s->attachAxis(axX); s->attachAxis(axY); }
        break;
    }
    }

    m_view->setChart(chart);   // takes ownership; deletes the previous chart
}

} // namespace NativeOffice
