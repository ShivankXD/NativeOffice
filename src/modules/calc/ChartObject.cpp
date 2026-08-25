// ─────────────────────────────────────────────────────────────────────────────
// ChartObject.cpp  (Sprint 26 — Charts)
// ─────────────────────────────────────────────────────────────────────────────
#include "ChartObject.h"
#include "SpreadsheetModel.h"
#include "CalcIcons.h"
#include "ChartResolve.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QMenu>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QContextMenuEvent>
#include <QPainter>
#include <QLinearGradient>
#include <QBrush>
#include <QVector>
#include <QStringList>
#include <QMargins>
#include <algorithm>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QAbstractBarSeries>
#include <QtCharts/QBarSeries>
#include <QtCharts/QHorizontalBarSeries>
#include <QtCharts/QStackedBarSeries>
#include <QtCharts/QPercentBarSeries>
#include <QtCharts/QHorizontalStackedBarSeries>
#include <QtCharts/QHorizontalPercentBarSeries>
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
    case ChartType::Doughnut:return "Doughnut";
    }
    return "Chart";
}

ChartType chartTypeFromInt(int v) {
    if (v < 0 || v > int(ChartType::Doughnut)) return ChartType::Column;
    return static_cast<ChartType>(v);
}

// An axis label font that fits the axis it is on.
//
// Qt hides a label rather than overlapping it with its neighbour, so an axis
// with more categories than room shows only some of them: the corpus's
// four-category bar chart, 148 pixels tall, showed exactly one. What decides it
// is the space per category ALONG the axis the categories run down, so that is
// what the size is taken from.
//
// `spanPx` is the chart's extent along that axis and `categories` how many
// share it. The plot area is roughly two fifths of a small chart once the
// title, the legend and the other axis have taken theirs, and a label needs
// about three times its point size in pixels before Qt will place it beside
// another. Both are estimates, checked against the corpus, where the
// alternative was one label out of four.
static QFont axisLabelFont(QFont base, int spanPx, int categories) {
    // A value axis places as many ticks as it has room for and is not at risk
    // of dropping any, so it keeps the font it was given. Sizing it too was a
    // mistake the first time round: it came out LARGER than the default, took
    // height from the plot, and cost the category axis the room this was
    // supposed to win it.
    if (categories <= 0) return base;
    // The floor is 4pt, which is small, but the alternative is worse than
    // small: Qt shows ONE of four categories rather than four cramped ones, and
    // a chart labelled only "Class 3" reads as though it has a single bar.
    base.setPointSizeF(qBound(4.0, (spanPx * 0.4) / categories / 3.2, 10.0));
    return base;
}

// The brush a series filled with a picture should be painted with.
//
// Excel stretches the picture into each bar, so the bar carries the image's
// shading over its own height. A chart library that paints a brush cannot give
// the bar the picture's silhouette, but ObjectBoundingMode makes a gradient
// stretch into each bar exactly the same way, which is the shading part of it.
static QBrush pictureBrush(const QVector<QColor>& stops, const QVector<qreal>& pos) {
    QLinearGradient g(0, 0, 0, 1);
    g.setCoordinateMode(QGradient::ObjectBoundingMode);
    for (int i = 0; i < stops.size() && i < pos.size(); ++i)
        if (stops.at(i).isValid()) g.setColorAt(qBound(0.0, pos.at(i), 1.0), stops.at(i));
    return QBrush(g);
}

// ─────────────────────────────────────────────────────────────────────────────
ChartObject::ChartObject(SpreadsheetModel* model, const ChartSpec& spec, QWidget* parent)
    : QFrame(parent)
    , m_model(model)
    , m_spec(spec)
{
    setObjectName("chartObject");
    setFrameShape(QFrame::NoFrame);
    setAutoFillBackground(true);
    // Solid object that swallows clicks (so they don't fall through to the grid).
    setAttribute(Qt::WA_NoMousePropagation);
    setCursor(Qt::SizeAllCursor);   // drag the body to move it (Excel-style)
    // No card and no border: the chart's own plot area is the whole object, so
    // it sits on the cells instead of looking like a window opened over them.
    setStyleSheet("QFrame#chartObject{background:transparent;border:none;}");

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    // ── Chart view (fills the object; transparent to mouse so the body drags) ──
    m_view = new QChartView(this);
    m_view->setRenderHint(QPainter::Antialiasing);
    // The view is a QGraphicsView: both the widget and its scene background
    // have to be cleared or it paints its own opaque backdrop first.
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setStyleSheet("background:transparent;border:none;");
    m_view->setBackgroundBrush(Qt::NoBrush);
    m_view->viewport()->setAutoFillBackground(false);
    m_view->setAttribute(Qt::WA_TransparentForMouseEvents);
    v->addWidget(m_view, 1);

    // There is deliberately no close button. A little cross floating over the
    // sheet made every chart look like a dialog; selecting the object and
    // pressing Delete, or using its context menu, is how a spreadsheet does it.

    // ── Resize grip (bottom-right) ────────────────────────────────────────────
    m_grip = new QWidget(this);
    m_grip->setFixedSize(16, 16);
    m_grip->setCursor(Qt::SizeFDiagCursor);
    m_grip->setStyleSheet("background:transparent;");

    setGeometry(spec.geom.isValid() ? spec.geom : QRect(40, 40, 420, 280));
    m_grip->installEventFilter(this);
    setSelected(false);        // bare chart until clicked (Excel/WPS style)
    rebuild();
}

void ChartObject::setSelected(bool on) {
    // Selected: a thin accent outline and the resize grip. Unselected: nothing
    // at all, so the object is indistinguishable from part of the sheet.
    setStyleSheet(on
        ? QStringLiteral("QFrame#chartObject{background:transparent;border:1px solid #1A73E8;}")
        : QStringLiteral("QFrame#chartObject{background:transparent;border:none;}"));
    if (m_grip) m_grip->setVisible(on);
}

ChartSpec ChartObject::spec() const {
    ChartSpec s = m_spec;      // series, title, categories and anchor survive
    s.geom = geometry();
    return s;
}

void ChartObject::resizeEvent(QResizeEvent* e) {
    QFrame::resizeEvent(e);
    if (m_grip) {
        m_grip->move(width() - m_grip->width(), height() - m_grip->height());
        m_grip->raise();
    }
}

// Drag the body to move (clicks never fall through to the grid).
void ChartObject::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        setSelected(true);
        emit selected(this);      // module deselects the others
        m_dragging  = true;
        m_pressPos  = e->globalPosition().toPoint();
        m_startGeom = geometry();
        raise();
    }
    e->accept();
}

void ChartObject::mouseMoveEvent(QMouseEvent* e) {
    if (m_dragging) {
        const QPoint d = e->globalPosition().toPoint() - m_pressPos;
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
    }
    e->accept();
}

void ChartObject::mouseReleaseEvent(QMouseEvent* e) {
    if (m_dragging) { m_dragging = false; emit geometryEdited(); }
    e->accept();
}

void ChartObject::mouseDoubleClickEvent(QMouseEvent* e) {
    e->accept();          // don't let a double-click edit the cell underneath
}

// Right-click → change chart type / delete (replaces the old title-bar buttons).
void ChartObject::contextMenuEvent(QContextMenuEvent* e) {
    QMenu menu(this);
    const ChartType types[] = { ChartType::Column, ChartType::Bar, ChartType::Line,
                                ChartType::Area, ChartType::Pie, ChartType::Scatter };
    for (ChartType t : types)
        connect(menu.addAction(chartTypeName(t)), &QAction::triggered, this, [this, t]{
            setChartType(t); emit geometryEdited();
        });
    menu.addSeparator();
    connect(menu.addAction("Delete Chart"), &QAction::triggered, this, [this]{ emit closed(this); });
    menu.exec(e->globalPos());
    e->accept();
}

// ── Resize via the bottom-right grip ───────────────────────────────────────────
bool ChartObject::eventFilter(QObject* w, QEvent* e) {
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
// Translate the file's value-axis number format into the label format Qt uses.
// Only the shape matters here (currency, thousands, decimals); the full
// OOXML format grammar is not reimplemented for an axis label.
void ChartObject::applyValueAxisFormat(QValueAxis* axis) const {
    if (!axis) return;
    const QString code = m_spec.valueAxisFormat;
    // No format in the file: leave Qt's own label formatting alone. Forcing
    // "%.0f" here rounded fractional values (a 4.2 GPA axis read as 4).
    if (code.isEmpty()) return;

    const bool currency  = code.contains('$');
    const bool percent   = code.contains('%');
    int decimals = 0;
    const int dot = code.indexOf('.');
    if (dot >= 0)
        for (int i = dot + 1; i < code.size() && (code[i] == '0' || code[i] == '#'); ++i)
            ++decimals;

    // Qt's axis label format is printf-style and cannot group thousands, while
    // its own locale-aware formatting (used when no format is set) can but has
    // no way to carry a currency symbol. So: keep the symbol when the file asks
    // for one, and otherwise leave the labels to the locale so large numbers
    // read as 1,500,000 rather than 1500000.
    if (!currency && !percent) return;

    QString fmt = QStringLiteral("%.") + QString::number(decimals) + QLatin1Char('f');
    if (currency) fmt.prepend(QLatin1Char('$'));
    if (percent)  fmt.append(QLatin1Char('%'));
    axis->setLabelFormat(fmt);
}

void ChartObject::rebuild() {
    if (!m_model) return;

    struct Series {
        QString name; QVector<double> vals; QColor color; QVector<QColor> pointColors;
        QVector<QColor> gradient; QVector<qreal> gradientPos;   // picture fill
    };
    QStringList     cats;
    QVector<Series> series;
    QString         explicitTitle = m_spec.title;

    // Reading a range out of whichever sheet a reference names. A chart that
    // came from a file often plots a different sheet than the one it sits on.
    auto sheetFor = [&](const QString& name) -> SpreadsheetModel* {
        if (name.isEmpty() || !m_resolveSheet) return m_model;
        SpreadsheetModel* m = m_resolveSheet(name);
        return m ? m : m_model;
    };
    // Cells of a range in reading order, so a row range and a column range both
    // come back as a flat list of points.
    auto readRange = [&](SpreadsheetModel* m, const QRect& r) {
        QStringList out;
        if (!m || r.isNull()) return out;
        for (int row = r.top(); row <= r.bottom(); ++row)
            for (int col = r.left(); col <= r.right(); ++col)
                out << m->displayValue(col, row);
        return out;
    };

    if (m_spec.isExplicit()) {
        // ── Imported chart: every series names its own range ──────────────
        SpreadsheetModel* catModel = sheetFor(m_spec.catSheet);
        cats = readRange(catModel, m_spec.catRange);
        // Some producers write no cached labels and some write no live range;
        // whichever exists wins, and the cached copy is the fallback.
        bool catsEmpty = true;
        for (const QString& c : cats) if (!c.isEmpty()) { catsEmpty = false; break; }
        if (catsEmpty && !m_spec.categories.isEmpty()) cats = m_spec.categories;

        for (const ChartSeries& cs : m_spec.series) {
            Series s;
            s.color        = cs.color;
            s.pointColors  = cs.pointColors;
            s.gradient     = cs.fillGradient;
            s.gradientPos  = cs.fillGradientPos;
            s.name         = cs.name;

            // A name given as a reference to a header cell.
            if (s.name.isEmpty() && !cs.nameRange.isNull()) {
                const QStringList n = readRange(sheetFor(cs.nameSheet), cs.nameRange);
                if (!n.isEmpty()) s.name = n.first();
            }

            const QStringList raw = readRange(sheetFor(cs.sheet), cs.valRange);
            bool anyLive = false;
            for (const QString& v : raw) {
                bool ok = false;
                const double d = v.toDouble(&ok);
                s.vals << (ok ? d : 0.0);
                if (ok) anyLive = true;
            }
            // Nothing readable at the reference: fall back to the values the
            // producing application cached in the file.
            if (!anyLive && !cs.cache.isEmpty()) s.vals = cs.cache;

            if (s.name.isEmpty()) s.name = QString("Series %1").arg(series.size() + 1);
            series << s;
        }

        // Labels still missing: number the points so the axis is not blank.
        if (cats.isEmpty() && !series.isEmpty())
            for (int i = 0; i < series.first().vals.size(); ++i)
                cats << QString::number(i + 1);

    } else {
        // ── In-app chart: one contiguous block, scanned for a header row ───
        // The scan itself lives in ChartResolve so the .xlsx writer reaches the
        // same answer; see the header comment there for why it had to move.
        const RangeChart rc = resolveRangeChart(
            m_spec.range,
            [this](int c, int r) { return m_model->displayValue(c, r); });

        cats = rc.categories;
        for (const RangeSeries& rs : rc.series) {
            Series s;
            s.name = rs.name;
            s.vals = rs.values;
            series << s;
        }
        if (explicitTitle.isEmpty()) explicitTitle = rc.cornerTitle;
    }

    auto* chart = new QChart();
    chart->setAnimationOptions(QChart::SeriesAnimations);
    chart->setBackgroundRoundness(0);
    // The chart draws straight onto the sheet rather than onto a white card of
    // its own, so the cells it spans stay visible behind it and it reads as an
    // object sitting on the grid instead of a window opened over it.
    chart->setLocalizeNumbers(true);      // groups thousands on unformatted axes
    chart->setBackgroundVisible(false);
    chart->setPlotAreaBackgroundVisible(false);
    chart->setBackgroundBrush(Qt::NoBrush);
    chart->setMargins(QMargins(4, 2, 4, 2));
    // An imported chart says whether it has a legend and where; one built in
    // the app keeps the old default of a bottom legend when it needs one.
    if (m_spec.isExplicit()) {
        chart->legend()->setVisible(m_spec.showLegend);
        switch (m_spec.legendPos) {
        case 'r': chart->legend()->setAlignment(Qt::AlignRight);  break;
        case 'l': chart->legend()->setAlignment(Qt::AlignLeft);   break;
        case 't': chart->legend()->setAlignment(Qt::AlignTop);    break;
        default:  chart->legend()->setAlignment(Qt::AlignBottom); break;
        }
    } else {
        chart->legend()->setVisible(series.size() > 1
                                    || m_spec.type == ChartType::Pie
                                    || m_spec.type == ChartType::Doughnut);
        chart->legend()->setAlignment(Qt::AlignBottom);
    }

    // No title unless there is one to show.
    if (!explicitTitle.isEmpty())
        chart->setTitle(explicitTitle);
    else if (!m_spec.isExplicit())
        chart->setTitle(QString("%1 Chart").arg(chartTypeName(m_spec.type)));

    switch (m_spec.type) {
    case ChartType::Column:
    case ChartType::Bar: {
        // Stacking is a different SERIES CLASS in Qt Charts, not a property, so
        // the grouping has to be known before anything is built. Drawing a
        // stacked chart side by side turns four solid bars into sixteen
        // hairlines, which is how this workbook looked next to Excel.
        const bool horiz = (m_spec.type == ChartType::Bar);
        QAbstractBarSeries* bs = nullptr;
        switch (m_spec.grouping) {
        case ChartGrouping::Stacked:
            bs = horiz ? static_cast<QAbstractBarSeries*>(new QHorizontalStackedBarSeries())
                       : static_cast<QAbstractBarSeries*>(new QStackedBarSeries());
            break;
        case ChartGrouping::PercentStacked:
            bs = horiz ? static_cast<QAbstractBarSeries*>(new QHorizontalPercentBarSeries())
                       : static_cast<QAbstractBarSeries*>(new QPercentBarSeries());
            break;
        case ChartGrouping::Clustered:
            bs = horiz ? static_cast<QAbstractBarSeries*>(new QHorizontalBarSeries())
                       : static_cast<QAbstractBarSeries*>(new QBarSeries());
            break;
        }
        for (const auto& s : series) {
            auto* set = new QBarSet(s.name);
            for (double v : s.vals) *set << v;
            if (s.gradient.size() >= 2)      set->setBrush(pictureBrush(s.gradient, s.gradientPos));
            else if (s.color.isValid())      set->setColor(s.color);
            bs->append(set);
        }

        chart->addSeries(bs);
        auto* axCat = new QBarCategoryAxis(); axCat->append(cats);
        auto* axVal = new QValueAxis();
        applyValueAxisFormat(axVal);
        if (m_spec.type == ChartType::Column) {
            chart->addAxis(axCat, Qt::AlignBottom);
            chart->addAxis(axVal, Qt::AlignLeft);
        } else {
            chart->addAxis(axCat, Qt::AlignLeft);
            chart->addAxis(axVal, Qt::AlignBottom);
        }
        bs->attachAxis(axCat);
        bs->attachAxis(axVal);
        // A horizontal bar chart runs its categories DOWN the chart, so their
        // room is its height; a column chart runs them across, so it is width.
        const int catSpan = horiz ? height() : width();
        // Applied after attaching, for the same reason applyNiceNumbers() is:
        // QChart::addAxis() runs the chart theme over the axis and puts its own
        // label font back. Set before, the size is silently discarded, and a
        // chart short enough to sit beside another one shows one category label
        // out of four because Qt drops the ones that would overlap.
        // setTruncateLabels stops the other half of it, the "..." that appears
        // when a label is cut to the room available.
        axCat->setTruncateLabels(false);
        axVal->setTruncateLabels(false);
        axCat->setLabelsFont(axisLabelFont(chart->font(), catSpan, cats.size()));
        axVal->setLabelsFont(axisLabelFont(chart->font(), horiz ? width() : height(), 0));
        // Round tick values, applied after attaching: applyNiceNumbers() works
        // on the axis's current range, which only exists once it is bound to
        // the series. Called earlier it silently does nothing.
        axVal->applyNiceNumbers();
        break;
    }
    case ChartType::Line:
    case ChartType::Area: {
        for (const auto& s : series) {
            auto* ls = new QLineSeries();
            ls->setName(s.name);
            for (int i = 0; i < s.vals.size(); ++i) ls->append(i, s.vals[i]);
            if (m_spec.type == ChartType::Area) {
                auto* as = new QAreaSeries(ls);
                as->setName(s.name);
                if (s.color.isValid()) as->setColor(s.color);
                chart->addSeries(as);
            } else {
                if (s.color.isValid()) ls->setColor(s.color);
                chart->addSeries(ls);
            }
        }
        auto* axCat = new QBarCategoryAxis(); axCat->append(cats);
        auto* axVal = new QValueAxis();
        applyValueAxisFormat(axVal);
        chart->addAxis(axCat, Qt::AlignBottom);
        chart->addAxis(axVal, Qt::AlignLeft);
        for (auto* s : chart->series()) { s->attachAxis(axCat); s->attachAxis(axVal); }
        // Applied after attaching, for the same reason applyNiceNumbers() is:
        // QChart::addAxis() runs the chart theme over the axis and puts its own
        // label font back. Set before, the size is silently discarded, and a
        // chart short enough to sit beside another one shows one category label
        // out of four because Qt drops the ones that would overlap.
        // setTruncateLabels stops the other half of it, the "..." that appears
        // when a label is cut to the room available.
        axCat->setTruncateLabels(false);
        axVal->setTruncateLabels(false);
        axCat->setLabelsFont(axisLabelFont(chart->font(), width(), cats.size()));
        axVal->setLabelsFont(axisLabelFont(chart->font(), height(), 0));
        axVal->applyNiceNumbers();
        break;
    }
    case ChartType::Pie:
    case ChartType::Doughnut: {
        auto* ps = new QPieSeries();
        const Series first = series.isEmpty() ? Series{} : series.first();
        for (int i = 0; i < cats.size() && i < first.vals.size(); ++i) {
            QPieSlice* slice = ps->append(cats[i], first.vals[i]);
            // Pie and doughnut charts colour each slice separately.
            if (i < first.pointColors.size() && first.pointColors[i].isValid())
                slice->setBrush(first.pointColors[i]);
        }
        // The size has to be stated. Left to itself with labels outside, Qt
        // shrinks the pie to make room for them, and with several categories
        // that collapsed it to nothing: the labels piled up in the middle of an
        // empty chart and no wedge was drawn at all.
        // With labels outside, Qt shrinks the pie to make room for them and
        // several categories collapsed it to nothing, so the size is stated.
        // A chart that asks for no data labels gets a bigger pie and lets the
        // legend name the slices, which is what Excel and WPS draw.
        const bool wantLabels = !m_spec.isExplicit() || m_spec.showDataLabels;
        ps->setPieSize(wantLabels ? 0.62 : 0.78);
        if (m_spec.type == ChartType::Doughnut) ps->setHoleSize(0.35);
        ps->setLabelsPosition(QPieSlice::LabelOutside);
        ps->setLabelsVisible(wantLabels);
        chart->addSeries(ps);
        break;
    }
    case ChartType::Scatter: {
        for (const auto& s : series) {
            auto* ss = new QScatterSeries();
            ss->setName(s.name);
            ss->setMarkerSize(11);
            for (int i = 0; i < s.vals.size(); ++i) ss->append(i, s.vals[i]);
            if (s.color.isValid()) ss->setColor(s.color);
            chart->addSeries(ss);
        }
        auto* axX = new QValueAxis();
        auto* axY = new QValueAxis();
        chart->addAxis(axX, Qt::AlignBottom);
        chart->addAxis(axY, Qt::AlignLeft);
        // Applied after attaching, for the same reason applyNiceNumbers() is:
        // QChart::addAxis() runs the chart theme over the axis and puts its own
        // label font back. Set before, the size is silently discarded, and a
        // chart short enough to sit beside another one shows one category label
        // out of four because Qt drops the ones that would overlap.
        // setTruncateLabels stops the other half of it, the "..." that appears
        // when a label is cut to the room available.
        axX->setTruncateLabels(false);
        axY->setTruncateLabels(false);
        axX->setLabelsFont(axisLabelFont(chart->font(), width(), 0));
        axY->setLabelsFont(axisLabelFont(chart->font(), height(), 0));
        for (auto* s : chart->series()) { s->attachAxis(axX); s->attachAxis(axY); }
        break;
    }
    }

    m_view->setChart(chart);   // takes ownership; deletes the previous chart
}

} // namespace NativeOffice
