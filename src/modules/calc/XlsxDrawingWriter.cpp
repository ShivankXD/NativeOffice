// ─────────────────────────────────────────────────────────────────────────────
// XlsxDrawingWriter.cpp
// See XlsxDrawingWriter.h.
//
// Two things about the chart schema drove how this is written.
//
// First, element order is not advisory. CT_BarSer and friends are xsd:sequence,
// so <c:cat> after <c:val>, or <c:tx> after <c:idx> in the wrong place, gives a
// file Excel opens with "unreadable content" and repairs by throwing the chart
// away. Every emitter below writes its elements in schema order and says so.
//
// Second, a chart part carries each reference TWICE: the formula that points at
// the cells, and a cache of the values those cells held when it was written.
// The cache is what other applications draw before they recalculate, and a
// chart written with references but no cache shows up blank in most viewers.
// So every reference here is emitted with its cache alongside.
// ─────────────────────────────────────────────────────────────────────────────
#include "XlsxDrawingWriter.h"
#include "ChartResolve.h"

#include <QStringList>
#include <QVector>

namespace NativeOffice {
namespace {

QString xmlEsc(QString s) {
    s.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;")
     .replace('"', "&quot;").replace('\'', "&apos;");
    return s;
}

QString colLabel(int col) {
    QString s;
    int c = col + 1;
    while (c > 0) { int r = (c - 1) % 26; s.prepend(QChar('A' + r)); c = (c - 1) / 26; }
    return s;
}

// "Sheet1!" or "'Q3 actuals'!". A name that is not a plain word has to be
// quoted, and an apostrophe inside it is written doubled.
QString sheetPrefix(const QString& name) {
    if (name.isEmpty()) return {};
    bool plain = true;
    for (const QChar& ch : name)
        if (!ch.isLetterOrNumber() && ch != '_') { plain = false; break; }
    if (plain) return name + QLatin1String("!");
    QString q = name;
    q.replace(QLatin1String("'"), QLatin1String("''"));
    return QLatin1String("'") + q + QLatin1String("'!");
}

// Chart references are always absolute and sheet-qualified.
QString absRef(const QString& sheet, const QRect& r) {
    if (r.isNull()) return {};
    const QString a = QLatin1String("$") + colLabel(r.left())
                    + QLatin1String("$") + QString::number(r.top() + 1);
    if (r.width() == 1 && r.height() == 1) return sheetPrefix(sheet) + a;
    const QString b = QLatin1String("$") + colLabel(r.right())
                    + QLatin1String("$") + QString::number(r.bottom() + 1);
    return sheetPrefix(sheet) + a + QLatin1String(":") + b;
}

QString num(double v) { return QString::number(v, 'g', 15); }

int pxToEmu(int px) { return px * 9525; }   // 914400 EMU per inch at 96 DPI

// Declared on each anchor rather than relied on from the document around it:
// these fragments are spliced into a drawing part that some other application
// wrote, and that application picks its own namespace prefixes.
const char* kAnchorNs =
    "xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\" "
    "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"";

// The anchor around whatever object it places. Charts and pictures differ only
// in the element in the middle, so the placement is written once.
QString anchorAround(const CellAnchor& a, const QRect& geom, const QString& body) {
    const int cx = pxToEmu(geom.width()  > 0 ? geom.width()  : 420);
    const int cy = pxToEmu(geom.height() > 0 ? geom.height() : 280);

    // A two-corner anchor is what an object dragged around the sheet has, and
    // it is what makes it keep spanning those cells when they are resized.
    if (a.hasFrom() && a.hasTo())
        return QStringLiteral("<xdr:twoCellAnchor ") + kAnchorNs
             + QString(" editAs=\"oneCell\">"
                       "<xdr:from><xdr:col>%1</xdr:col><xdr:colOff>%2</xdr:colOff>"
                       "<xdr:row>%3</xdr:row><xdr:rowOff>%4</xdr:rowOff></xdr:from>"
                       "<xdr:to><xdr:col>%5</xdr:col><xdr:colOff>%6</xdr:colOff>"
                       "<xdr:row>%7</xdr:row><xdr:rowOff>%8</xdr:rowOff></xdr:to>"
                       "%9<xdr:clientData/></xdr:twoCellAnchor>")
                   .arg(QString::number(a.fromCol), QString::number(pxToEmu(a.fromDx)),
                        QString::number(a.fromRow), QString::number(pxToEmu(a.fromDy)),
                        QString::number(a.toCol),   QString::number(pxToEmu(a.toDx)),
                        QString::number(a.toRow),   QString::number(pxToEmu(a.toDy)),
                        body);

    // Only a top-left cell (or nothing at all): keep the object's own size and
    // pin it to that cell.
    return QStringLiteral("<xdr:oneCellAnchor ") + kAnchorNs
         + QString(">"
                   "<xdr:from><xdr:col>%1</xdr:col><xdr:colOff>%2</xdr:colOff>"
                   "<xdr:row>%3</xdr:row><xdr:rowOff>%4</xdr:rowOff></xdr:from>"
                   "<xdr:ext cx=\"%5\" cy=\"%6\"/>"
                   "%7<xdr:clientData/></xdr:oneCellAnchor>")
               .arg(QString::number(a.hasFrom() ? a.fromCol : 0),
                    QString::number(a.hasFrom() ? pxToEmu(a.fromDx) : 0),
                    QString::number(a.hasFrom() ? a.fromRow : 0),
                    QString::number(a.hasFrom() ? pxToEmu(a.fromDy) : 0),
                    QString::number(cx), QString::number(cy), body);
}


// ── One chart, flattened to what the file format wants ──────────────────────
// Both kinds of chart (a contiguous range scanned for headers, or an explicit
// series list read out of a file) collapse to this before anything is written,
// so there is one emitter rather than two.
struct WSeries {
    QString         name;
    QString         nameRef;    // empty => write the name literally
    QString         valRef;     // empty => write only the cache
    QVector<double> vals;
    QColor          color;      // invalid => let the reader pick
    QVector<QColor> pointColors;
};

struct Plot {
    ChartType     type { ChartType::Column };
    ChartGrouping grouping { ChartGrouping::Clustered };
    QString     title;              // empty => autoTitleDeleted
    bool        legend { true };
    char        legendPos { 'b' };
    bool        dataLabels { false };
    QStringList cats;
    QString     catRef;             // empty => write only the cache
    QVector<WSeries> series;
};

// Collapse a ChartSpec into a Plot. Returns false when nothing is plottable.
bool flatten(const ChartSpec& spec, const QString& sheetName,
             const std::function<QString(int, int)>& disp, Plot& out) {
    out.type       = spec.type;
    out.grouping   = spec.grouping;
    out.dataLabels = spec.showDataLabels;

    if (spec.isExplicit()) {
        // A chart that came from a file already names its own ranges. Its
        // presentation flags came from the file too, so they are used as-is
        // rather than re-guessed.
        out.legend    = spec.showLegend;
        out.legendPos = spec.legendPos;
        out.title     = spec.hasTitle ? spec.title : QString();
        out.cats      = spec.categories;
        out.catRef    = absRef(spec.catSheet.isEmpty() ? sheetName : spec.catSheet,
                               spec.catRange);

        for (const ChartSeries& cs : spec.series) {
            WSeries w;
            w.name    = cs.name;
            w.nameRef = absRef(cs.nameSheet.isEmpty() ? sheetName : cs.nameSheet,
                               cs.nameRange);
            w.valRef  = absRef(cs.sheet.isEmpty() ? sheetName : cs.sheet, cs.valRange);
            w.vals    = cs.cache;
            w.color       = cs.color;
            w.pointColors = cs.pointColors;
            // No cached copy in the spec: read the values back off the sheet so
            // the part still carries a cache.
            if (w.vals.isEmpty() && !cs.valRange.isNull() && disp)
                for (int r = cs.valRange.top(); r <= cs.valRange.bottom(); ++r)
                    for (int c = cs.valRange.left(); c <= cs.valRange.right(); ++c) {
                        bool ok = false;
                        const double d = disp(c, r).toDouble(&ok);
                        w.vals << (ok ? d : 0.0);
                    }
            out.series << w;
        }
        return !out.series.isEmpty();
    }

    // ── In-app chart: the same scan the renderer runs ────────────────────────
    const RangeChart rc = resolveRangeChart(spec.range, disp);
    if (rc.series.isEmpty()) return false;

    out.cats   = rc.categories;
    out.catRef = absRef(sheetName, rc.catRange);
    for (const RangeSeries& rs : rc.series) {
        WSeries w;
        w.name    = rs.name;
        w.nameRef = absRef(sheetName, rs.nameCell);
        w.valRef  = absRef(sheetName, rs.valRange);
        w.vals    = rs.values;
        out.series << w;
    }

    // Presentation has to match what ChartObject actually draws, or the chart
    // changes appearance the first time it is saved and reopened.
    out.title = spec.title.isEmpty() ? rc.cornerTitle : spec.title;
    if (out.title.isEmpty())
        out.title = QString("%1 Chart").arg(chartTypeName(spec.type));
    out.legend = out.series.size() > 1
                 || spec.type == ChartType::Pie
                 || spec.type == ChartType::Doughnut;
    out.legendPos = 'b';
    return true;
}

// ── Reference plus cache, the shape every c:cat / c:val / c:tx takes ─────────

// <c:strRef><c:f>..</c:f><c:strCache>..</c:strCache></c:strRef>, or a bare
// <c:strLit> when there is no reference (invented category labels have no
// cells behind them, and pointing at cells that do not hold them would plot
// the wrong column).
QString strSource(const QString& ref, const QStringList& vals) {
    QString cache = QString("<c:ptCount val=\"%1\"/>").arg(vals.size());
    for (int i = 0; i < vals.size(); ++i)
        cache += QString("<c:pt idx=\"%1\"><c:v>%2</c:v></c:pt>")
                     .arg(i).arg(xmlEsc(vals.at(i)));
    if (ref.isEmpty())
        return "<c:strLit>" + cache + "</c:strLit>";
    return "<c:strRef><c:f>" + xmlEsc(ref) + "</c:f><c:strCache>"
         + cache + "</c:strCache></c:strRef>";
}

QString numSource(const QString& ref, const QVector<double>& vals) {
    QString cache = QString("<c:formatCode>General</c:formatCode>"
                            "<c:ptCount val=\"%1\"/>").arg(vals.size());
    for (int i = 0; i < vals.size(); ++i)
        cache += QString("<c:pt idx=\"%1\"><c:v>%2</c:v></c:pt>")
                     .arg(i).arg(num(vals.at(i)));
    if (ref.isEmpty())
        return "<c:numLit>" + cache + "</c:numLit>";
    return "<c:numRef><c:f>" + xmlEsc(ref) + "</c:f><c:numCache>"
         + cache + "</c:numCache></c:numRef>";
}

// <c:spPr> goes immediately after <c:tx> in every CT_*Ser. Empty when the
// series has no colour, so a chart with none keeps whatever the reader picks.
QString seriesFill(const WSeries& s) {
    if (!s.color.isValid()) return {};
    return QString("<c:spPr><a:solidFill><a:srgbClr val=\"%1\"/></a:solidFill>"
                   "<a:ln><a:noFill/></a:ln></c:spPr>")
        .arg(s.color.name(QColor::HexRgb).mid(1).toUpper());
}

// Per-point colours, for the types that colour each point separately. Sits
// after spPr (and after explosion, which is not written) and before dLbls.
QString seriesPoints(const WSeries& s) {
    QString out;
    for (int i = 0; i < s.pointColors.size(); ++i) {
        if (!s.pointColors.at(i).isValid()) continue;
        out += QString("<c:dPt><c:idx val=\"%1\"/><c:bubble3D val=\"0\"/>"
                       "<c:spPr><a:solidFill><a:srgbClr val=\"%2\"/></a:solidFill>"
                       "<a:ln><a:noFill/></a:ln></c:spPr></c:dPt>")
                   .arg(i)
                   .arg(s.pointColors.at(i).name(QColor::HexRgb).mid(1).toUpper());
    }
    return out;
}

QString seriesName(const WSeries& s) {
    if (s.nameRef.isEmpty())
        return "<c:tx><c:v>" + xmlEsc(s.name) + "</c:v></c:tx>";
    return "<c:tx>" + strSource(s.nameRef, QStringList{ s.name }) + "</c:tx>";
}

QString dLbls(bool on) {
    // Excel writes the whole block with everything off when it wants no
    // labels, and the importer keys off any single "show" being 1.
    return QString("<c:dLbls><c:showLegendKey val=\"0\"/><c:showVal val=\"%1\"/>"
                   "<c:showCatName val=\"0\"/><c:showSerName val=\"0\"/>"
                   "<c:showPercent val=\"0\"/><c:showBubbleSize val=\"0\"/></c:dLbls>")
        .arg(on ? 1 : 0);
}

// ── The plot element, one per chart type ────────────────────────────────────
// Axis ids only have to be unique inside this part, so they are constants.
const char* kCatAxId = "111111111";
const char* kValAxId = "222222222";

QString buildPlot(const Plot& p) {
    const QString cat = strSource(p.catRef, p.cats);
    QString body;

    switch (p.type) {
    case ChartType::Column:
    case ChartType::Bar: {
        // CT_BarChart: barDir, grouping, varyColors, ser*, dLbls, gapWidth,
        // overlap, axId, axId.
        const char* grp = p.grouping == ChartGrouping::Stacked        ? "stacked"
                        : p.grouping == ChartGrouping::PercentStacked ? "percentStacked"
                                                                      : "clustered";
        body = QString("<c:barChart><c:barDir val=\"%1\"/>"
                       "<c:grouping val=\"%2\"/><c:varyColors val=\"0\"/>")
                   .arg(p.type == ChartType::Bar ? "bar" : "col", grp);
        for (int i = 0; i < p.series.size(); ++i) {
            // CT_BarSer: idx, order, tx, spPr, invertIfNegative, dPt, dLbls,
            // cat, val.
            body += QString("<c:ser><c:idx val=\"%1\"/><c:order val=\"%1\"/>").arg(i)
                  + seriesName(p.series.at(i))
                  + seriesFill(p.series.at(i))
                  + "<c:invertIfNegative val=\"0\"/>"
                  + seriesPoints(p.series.at(i))
                  + dLbls(p.dataLabels)
                  + "<c:cat>" + cat + "</c:cat>"
                  + "<c:val>" + numSource(p.series.at(i).valRef, p.series.at(i).vals) + "</c:val>"
                  + "</c:ser>";
        }
        body += QString("<c:gapWidth val=\"150\"/><c:overlap val=\"%1\"/>"
                        "<c:axId val=\"%2\"/><c:axId val=\"%3\"/></c:barChart>")
                    .arg(p.grouping == ChartGrouping::Clustered ? "-27" : "100",
                         kCatAxId, kValAxId);
        break;
    }
    case ChartType::Line: {
        // CT_LineChart: grouping, varyColors, ser*, dLbls, marker, axId, axId.
        body = "<c:lineChart><c:grouping val=\"standard\"/><c:varyColors val=\"0\"/>";
        for (int i = 0; i < p.series.size(); ++i) {
            // CT_LineSer: idx, order, tx, spPr, marker, dPt, dLbls, cat, val,
            // smooth.
            body += QString("<c:ser><c:idx val=\"%1\"/><c:order val=\"%1\"/>").arg(i)
                  + seriesName(p.series.at(i))
                  + seriesFill(p.series.at(i))
                  + "<c:marker><c:symbol val=\"none\"/></c:marker>"
                  + seriesPoints(p.series.at(i))
                  + dLbls(p.dataLabels)
                  + "<c:cat>" + cat + "</c:cat>"
                  + "<c:val>" + numSource(p.series.at(i).valRef, p.series.at(i).vals) + "</c:val>"
                  + "<c:smooth val=\"0\"/>"
                  + "</c:ser>";
        }
        body += QString("<c:marker val=\"1\"/><c:axId val=\"%1\"/><c:axId val=\"%2\"/>"
                        "</c:lineChart>").arg(kCatAxId, kValAxId);
        break;
    }
    case ChartType::Area: {
        // CT_AreaChart: grouping, varyColors, ser*, dLbls, axId, axId.
        body = "<c:areaChart><c:grouping val=\"standard\"/><c:varyColors val=\"0\"/>";
        for (int i = 0; i < p.series.size(); ++i) {
            // CT_AreaSer: idx, order, tx, spPr, dPt, dLbls, cat, val.
            body += QString("<c:ser><c:idx val=\"%1\"/><c:order val=\"%1\"/>").arg(i)
                  + seriesName(p.series.at(i))
                  + seriesFill(p.series.at(i))
                  + seriesPoints(p.series.at(i))
                  + dLbls(p.dataLabels)
                  + "<c:cat>" + cat + "</c:cat>"
                  + "<c:val>" + numSource(p.series.at(i).valRef, p.series.at(i).vals) + "</c:val>"
                  + "</c:ser>";
        }
        body += QString("<c:axId val=\"%1\"/><c:axId val=\"%2\"/></c:areaChart>")
                    .arg(kCatAxId, kValAxId);
        break;
    }
    case ChartType::Pie:
    case ChartType::Doughnut: {
        // CT_PieChart / CT_DoughnutChart: varyColors, ser*, dLbls, [holeSize].
        // No axes at all, and emitting axId here is one of the ways to get a
        // chart Excel repairs away.
        const bool ring = (p.type == ChartType::Doughnut);
        body = QString("<c:%1Chart><c:varyColors val=\"1\"/>")
                   .arg(ring ? "doughnut" : "pie");
        for (int i = 0; i < p.series.size(); ++i) {
            // CT_PieSer: idx, order, tx, spPr, explosion, dPt, dLbls, cat, val.
            body += QString("<c:ser><c:idx val=\"%1\"/><c:order val=\"%1\"/>").arg(i)
                  + seriesName(p.series.at(i))
                  + seriesFill(p.series.at(i))
                  + seriesPoints(p.series.at(i))
                  + dLbls(p.dataLabels)
                  + "<c:cat>" + cat + "</c:cat>"
                  + "<c:val>" + numSource(p.series.at(i).valRef, p.series.at(i).vals) + "</c:val>"
                  + "</c:ser>";
        }
        body += "<c:firstSliceAng val=\"0\"/>";
        if (ring) body += "<c:holeSize val=\"50\"/>";
        body += QString("</c:%1Chart>").arg(ring ? "doughnut" : "pie");
        break;
    }
    case ChartType::Scatter: {
        // CT_ScatterChart: scatterStyle, varyColors, ser*, dLbls, axId, axId.
        // Both axes are value axes here, not a category axis and a value axis.
        body = "<c:scatterChart><c:scatterStyle val=\"marker\"/><c:varyColors val=\"0\"/>";
        for (int i = 0; i < p.series.size(); ++i) {
            // CT_ScatterSer: idx, order, tx, spPr, marker, dPt, dLbls, xVal,
            // yVal, smooth. The empty line is what keeps it points-only, which
            // is how ChartObject draws it.
            body += QString("<c:ser><c:idx val=\"%1\"/><c:order val=\"%1\"/>").arg(i)
                  + seriesName(p.series.at(i))
                  + (p.series.at(i).color.isValid()
                        ? QString("<c:spPr><a:solidFill><a:srgbClr val=\"%1\"/></a:solidFill>"
                                  "<a:ln w=\"0\"><a:noFill/></a:ln></c:spPr>")
                              .arg(p.series.at(i).color.name(QColor::HexRgb).mid(1).toUpper())
                        : QString("<c:spPr><a:ln w=\"0\"><a:noFill/></a:ln></c:spPr>"))
                  + "<c:marker><c:symbol val=\"circle\"/><c:size val=\"5\"/></c:marker>"
                  + dLbls(p.dataLabels)
                  + "<c:xVal>" + cat + "</c:xVal>"
                  + "<c:yVal>" + numSource(p.series.at(i).valRef, p.series.at(i).vals) + "</c:yVal>"
                  + "<c:smooth val=\"0\"/>"
                  + "</c:ser>";
        }
        body += QString("<c:axId val=\"%1\"/><c:axId val=\"%2\"/></c:scatterChart>")
                    .arg(kCatAxId, kValAxId);
        break;
    }
    }

    // Axes. Pie and doughnut have none; scatter wants two value axes.
    QString axes;
    if (p.type == ChartType::Pie || p.type == ChartType::Doughnut) {
        // nothing
    } else {
        const bool bar = (p.type == ChartType::Bar);
        const char* catAxEl = (p.type == ChartType::Scatter) ? "valAx" : "catAx";
        // A bar chart lays its category axis down the left and its values
        // along the bottom, the opposite of every other type here.
        axes = QString("<c:%1><c:axId val=\"%2\"/>"
                       "<c:scaling><c:orientation val=\"minMax\"/></c:scaling>"
                       "<c:delete val=\"0\"/><c:axPos val=\"%3\"/>"
                       "<c:crossAx val=\"%4\"/></c:%1>")
                   .arg(catAxEl, kCatAxId, bar ? "l" : "b", kValAxId);
        axes += QString("<c:valAx><c:axId val=\"%1\"/>"
                        "<c:scaling><c:orientation val=\"minMax\"/></c:scaling>"
                        "<c:delete val=\"0\"/><c:axPos val=\"%2\"/>"
                        "<c:crossAx val=\"%3\"/></c:valAx>")
                    .arg(kValAxId, bar ? "b" : "l", kCatAxId);
    }

    return "<c:plotArea><c:layout/>" + body + axes + "</c:plotArea>";
}

} // namespace

bool chartIsWritable(const ChartSpec& spec,
                     const std::function<QString(int, int)>& disp) {
    Plot p;
    return flatten(spec, QStringLiteral("Sheet1"), disp, p);
}

QByteArray buildChartPartXml(const ChartSpec& spec,
                             const QString& sheetName,
                             const std::function<QString(int, int)>& disp) {
    Plot p;
    if (!flatten(spec, sheetName, disp, p)) return {};

    // CT_Chart: title, autoTitleDeleted, plotArea, legend, plotVisOnly,
    // dispBlanksAs.
    QString title;
    if (p.title.isEmpty()) {
        title = "<c:autoTitleDeleted val=\"1\"/>";
    } else {
        title = "<c:title><c:tx><c:rich><a:bodyPr/><a:lstStyle/><a:p><a:r><a:t>"
              + xmlEsc(p.title)
              + "</a:t></a:r></a:p></c:rich></c:tx><c:overlay val=\"0\"/></c:title>"
                "<c:autoTitleDeleted val=\"0\"/>";
    }

    QString legend;
    if (p.legend)
        legend = QString("<c:legend><c:legendPos val=\"%1\"/><c:overlay val=\"0\"/>"
                         "</c:legend>").arg(QChar(p.legendPos));

    const QString xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<c:chartSpace "
        "xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" "
        "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<c:chart>"
        + title
        + buildPlot(p)
        + legend
        + "<c:plotVisOnly val=\"1\"/><c:dispBlanksAs val=\"gap\"/>"
        "</c:chart>"
        "</c:chartSpace>";
    return xml.toUtf8();
}

QString buildChartAnchorXml(const ChartSpec& spec,
                            const QString& relId,
                            int shapeId) {
    const QString frame =
        QString("<xdr:graphicFrame macro=\"\">"
                "<xdr:nvGraphicFramePr>"
                "<xdr:cNvPr id=\"%1\" name=\"Chart %1\"/>"
                "<xdr:cNvGraphicFramePr/>"
                "</xdr:nvGraphicFramePr>"
                "<xdr:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%2\" cy=\"%3\"/></xdr:xfrm>"
                // <a:graphic>, not <xdr:graphic>. The frame is a spreadsheet
                // drawing element but the graphic inside it belongs to the
                // DrawingML main namespace, and Excel refuses to open a
                // workbook that gets this wrong. Our own reader matches on
                // local name, so a round trip through it cannot catch it.
                "<a:graphic><a:graphicData "
                "uri=\"http://schemas.openxmlformats.org/drawingml/2006/chart\">"
                "<c:chart "
                "xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" "
                "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
                "r:id=\"%4\"/>"
                "</a:graphicData></a:graphic>"
                "</xdr:graphicFrame>")
            .arg(QString::number(shapeId),
                 QString::number(pxToEmu(spec.geom.width()  > 0 ? spec.geom.width()  : 420)),
                 QString::number(pxToEmu(spec.geom.height() > 0 ? spec.geom.height() : 280)),
                 relId);

    return anchorAround(spec.anchor, spec.geom, frame);
}

QString buildPictureAnchorXml(const SheetImage& img,
                              const QString& relId,
                              int shapeId) {
    const int cx = pxToEmu(img.geom.width()  > 0 ? img.geom.width()  : 300);
    const int cy = pxToEmu(img.geom.height() > 0 ? img.geom.height() : 220);
    const QString pic =
        QString("<xdr:pic>"
                "<xdr:nvPicPr>"
                "<xdr:cNvPr id=\"%1\" name=\"Picture %1\"/>"
                "<xdr:cNvPicPr><a:picLocks noChangeAspect=\"1\"/></xdr:cNvPicPr>"
                "</xdr:nvPicPr>"
                "<xdr:blipFill><a:blip r:embed=\"%2\"/>"
                "<a:stretch><a:fillRect/></a:stretch></xdr:blipFill>"
                "<xdr:spPr>"
                "<a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%3\" cy=\"%4\"/></a:xfrm>"
                "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>"
                "</xdr:spPr>"
                "</xdr:pic>")
            .arg(QString::number(shapeId), relId,
                 QString::number(cx), QString::number(cy));

    return anchorAround(img.anchor, img.geom, pic);
}

QString imageExtension(const QByteArray& data) {
    if (data.startsWith(QByteArray("\x89PNG", 4)))  return QStringLiteral("png");
    if (data.startsWith(QByteArray("\xFF\xD8", 2))) return QStringLiteral("jpeg");
    if (data.startsWith("GIF8"))                     return QStringLiteral("gif");
    if (data.startsWith("BM"))                       return QStringLiteral("bmp");
    return QStringLiteral("png");
}

} // namespace NativeOffice
