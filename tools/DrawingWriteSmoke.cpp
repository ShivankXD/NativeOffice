// ─────────────────────────────────────────────────────────────────────────────
// DrawingWriteSmoke.cpp
// Dev-only check that a chart or picture built in the app survives an .xlsx save.
//
// This exists because the failure it guards against is silent and expensive.
// A chart part with its elements in the wrong order does not fail to write: it
// produces a file that Excel opens, calls damaged, and repairs by deleting the
// chart. Nothing in the app would notice, and the user finds out when they open
// the workbook somewhere else. So the check is a round trip through the app's
// own importer, which is the one piece of code here already known to agree with
// what real Excel files contain.
//
// Both save paths are covered, because they build the package in completely
// different ways:
//
//   * rebuild     exportXlsx(), for a workbook that has no original package
//   * preserving  exportXlsxPreserving(), which copies a real workbook and has
//                 to add chart parts to it without disturbing what is there
//
// Run with the path to a directory of .xlsx files (testdata/) to exercise the
// preserving path against real workbooks. Prints a line per check and exits
// non-zero on the first failure.
// ─────────────────────────────────────────────────────────────────────────────
#include <QBuffer>
#include <QGuiApplication>
#include <QImage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QHash>
#include <QTextStream>
#include <QXmlStreamReader>
#include <QtCore/private/qzipreader_p.h>

#include "XlsxIo.h"
#include "ChartSpec.h"
#include "XlsxDrawingWriter.h"

using namespace NativeOffice;

static QTextStream out(stdout);
static int failures = 0;

static void check(bool ok, const QString& what) {
    out << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    out.flush();
    if (!ok) ++failures;
}

// ── A small table with a header row and a category column ───────────────────
//        A         B        C
//   1   Region    Sales    Costs
//   2   North     120      80
//   3   South     90       55
//   4   East      140      70
static XlsxSheet makeSheet() {
    XlsxSheet sh;
    sh.name = QStringLiteral("Data");
    const char* grid[4][3] = {
        { "Region", "Sales", "Costs" },
        { "North",  "120",   "80"    },
        { "South",  "90",    "55"    },
        { "East",   "140",   "70"    },
    };
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 3; ++c)
            sh.cells.push_back({ c, r, QString::fromLatin1(grid[r][c]), CellFormat{}, -1 });
    return sh;      // the caller attaches tableReader(); see below
}

// The table as cells at an arbitrary origin, for planting into a real workbook.
static void plantTable(XlsxSheet& sh, int col0, int row0) {
    const char* grid[4][3] = {
        { "Region", "Sales", "Costs" },
        { "North",  "120",   "80"    },
        { "South",  "90",    "55"    },
        { "East",   "140",   "70"    },
    };
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 3; ++c)
            sh.cells.push_back({ col0 + c, row0 + r,
                                 QString::fromLatin1(grid[r][c]), CellFormat{}, -1 });
}

// The same table as a cell reader. It owns its data rather than capturing the
// local array above, which would dangle the moment makeSheet() returned.
static std::function<QString(int, int)> tableReader() {
    static const QStringList rows = {
        "Region,Sales,Costs", "North,120,80", "South,90,55", "East,140,70"
    };
    return [](int c, int r) -> QString {
        if (r < 0 || r >= rows.size()) return {};
        const QStringList cols = rows.at(r).split(',');
        return (c >= 0 && c < cols.size()) ? cols.at(c) : QString();
    };
}

static SheetImage makePicture() {
    QImage img(24, 16, QImage::Format_ARGB32);
    img.fill(0xFF3366CC);
    QByteArray png;
    { QBuffer b(&png); b.open(QIODevice::WriteOnly); img.save(&b, "PNG"); }

    SheetImage si;
    si.data = png;
    si.geom = QRect(10, 10, 24, 16);
    si.anchor.fromCol = 2;  si.anchor.fromRow = 20;
    si.anchor.toCol   = 5;  si.anchor.toRow   = 26;
    si.fromFile = false;                   // added here, not read from a file
    return si;
}

static ChartSpec makeChart(ChartType type, QRect range = QRect(0, 0, 3, 4)) {
    ChartSpec cs;
    cs.type  = type;
    cs.range = range;                      // A1:C4 unless the caller moved it
    cs.geom  = QRect(40, 40, 420, 280);
    cs.anchor.fromCol = 4;  cs.anchor.fromRow = 1;
    cs.anchor.toCol   = 10; cs.anchor.toRow   = 15;
    cs.fromFile = false;                   // built here, not read from a file
    return cs;
}

// ── What the importer does not check, and Excel does ──────────────────
//
// Reading a file back with our own importer proves the chart is described the
// way we meant. It does not prove Excel will accept the package: the importer
// is deliberately lenient, skips what it does not recognise, and never looks at
// content types at all. These are the structural rules that, broken, make Excel
// call a workbook damaged and repair it by deleting the chart.
static void validatePackage(const QString& path, const QString& label) {
    QZipReader zr(path);
    if (!zr.isReadable()) { check(false, label + ": package opens"); return; }

    QStringList parts;
    QHash<QString, QByteArray> bytes;
    for (const QZipReader::FileInfo& fi : zr.fileInfoList()) {
        QString n = fi.filePath;
        n.replace('\\', '/');
        // Directory entries are not parts. Real workbooks carry them, the
        // preserving save copies them back as it found them, and a content
        // type is neither expected nor allowed for one.
        if (!fi.isFile || n.endsWith('/') || !n.section('/', -1).contains('.')) continue;
        parts << n;
        bytes.insert(n, zr.fileData(fi.filePath));
    }

    // 1. Every XML part has to parse.
    bool wellFormed = true;
    QString firstBad;
    for (const QString& n : parts) {
        if (!n.endsWith(".xml") && !n.endsWith(".rels")) continue;
        QXmlStreamReader r(bytes.value(n));
        while (!r.atEnd()) r.readNext();
        if (r.hasError() && wellFormed) { wellFormed = false; firstBad = n + ": " + r.errorString(); }
    }
    check(wellFormed, label + ": every XML part is well formed"
                    + (wellFormed ? QString() : QString(" (%1)").arg(firstBad)));

    // 2. Every part needs a content type, by extension default or by override.
    QSet<QString> defaults, overrides;
    {
        QXmlStreamReader r(bytes.value("[Content_Types].xml"));
        while (!r.atEnd())
            if (r.readNext() == QXmlStreamReader::StartElement) {
                if (r.name() == u"Default")
                    defaults << r.attributes().value("Extension").toString().toLower();
                else if (r.name() == u"Override")
                    overrides << r.attributes().value("PartName").toString().toLower();
            }
    }
    QString untyped;
    for (const QString& n : parts) {
        if (n == "[Content_Types].xml") continue;
        const QString ext = n.section('.', -1).toLower();
        if (defaults.contains(ext) || overrides.contains("/" + n.toLower())) continue;
        if (untyped.isEmpty()) untyped = n;
    }
    check(untyped.isEmpty(), label + ": every part has a content type"
                           + (untyped.isEmpty() ? QString() : " (" + untyped + " does not)"));

    // 3. Every internal relationship has to point at a part that is there.
    QString dangling;
    for (const QString& n : parts) {
        if (!n.endsWith(".rels")) continue;
        // "xl/worksheets/_rels/sheet1.xml.rels" resolves targets against
        // "xl/worksheets". The package-root "_rels/.rels" has no "/_rels/" in
        // it at all and resolves against the root.
        const int cut = n.lastIndexOf("/_rels/");
        const QString base = cut >= 0 ? n.left(cut) : QString();
        QXmlStreamReader r(bytes.value(n));
        while (!r.atEnd()) {
            if (r.readNext() != QXmlStreamReader::StartElement) continue;
            if (r.name() != u"Relationship") continue;
            if (r.attributes().value("TargetMode") == u"External") continue;
            QString t = r.attributes().value("Target").toString();
            if (t.startsWith('/')) { t = t.mid(1); }
            else {
                QStringList segs = base.isEmpty() ? QStringList()
                                                  : base.split('/', Qt::SkipEmptyParts);
                for (const QString& seg : t.split('/', Qt::SkipEmptyParts)) {
                    if (seg == ".") continue;
                    if (seg == "..") { if (!segs.isEmpty()) segs.removeLast(); continue; }
                    segs << seg;
                }
                t = segs.join('/');
            }
            bool found = false;
            for (const QString& q : parts) if (q.compare(t, Qt::CaseInsensitive) == 0) found = true;
            if (!found && dangling.isEmpty()) dangling = n + " -> " + t;
        }
    }
    check(dangling.isEmpty(), label + ": every relationship resolves"
                            + (dangling.isEmpty() ? QString() : " (" + dangling + ")"));

    // 4. Inside every <c:ser>, the children have to appear in schema order.
    // CT_*Ser is an xsd:sequence, so a <c:cat> written after <c:val> is not a
    // loose end the reader tolerates: it is the single likeliest reason for a
    // chart to be silently repaired away.
    static const QStringList kOrder = {
        "idx", "order", "tx", "spPr", "invertIfNegative", "explosion", "marker",
        "dPt", "dLbls", "trendline", "errBars", "cat", "val", "xVal", "yVal",
        "smooth", "shape", "bubbleSize", "bubble3D", "extLst",
    };
    QString badOrder;
    for (const QString& n : parts) {
        if (!n.startsWith("xl/charts/chart")) continue;
        QXmlStreamReader r(bytes.value(n));
        int depth = 0, serDepth = -1, last = -1;
        while (!r.atEnd()) {
            const auto tok = r.readNext();
            if (tok == QXmlStreamReader::StartElement) {
                ++depth;
                const QString el = r.name().toString();
                if (el == "ser" && serDepth < 0) { serDepth = depth; last = -1; continue; }
                if (serDepth >= 0 && depth == serDepth + 1) {
                    const int pos = kOrder.indexOf(el);
                    if (pos < 0) { if (badOrder.isEmpty()) badOrder = n + ": unknown <c:" + el + ">"; }
                    else if (pos < last) {
                        if (badOrder.isEmpty())
                            badOrder = n + ": <c:" + el + "> out of order";
                    } else last = pos;
                }
            } else if (tok == QXmlStreamReader::EndElement) {
                if (serDepth == depth) serDepth = -1;
                --depth;
            }
        }
    }
    check(badOrder.isEmpty(), label + ": chart series are in schema order"
                            + (badOrder.isEmpty() ? QString() : " (" + badOrder + ")"));

    // 5. <graphic> inside a graphicFrame must be in the DrawingML MAIN
    // namespace, not the spreadsheet-drawing one.
    //
    // Written as <xdr:graphic> the file is still well-formed, and our own
    // reader still finds the chart because it matches on local name. Excel
    // refuses to open the workbook at all: not a repair prompt, a flat
    // refusal. This is checked by namespace URI rather than by prefix, since
    // the prefix is the file's to choose.
    static const QString kMainNs = "http://schemas.openxmlformats.org/drawingml/2006/main";
    QString badNs;
    for (const QString& n : parts) {
        if (!n.startsWith("xl/drawings/drawing") || !n.endsWith(".xml")) continue;
        QXmlStreamReader r(bytes.value(n));
        while (!r.atEnd()) {
            if (r.readNext() != QXmlStreamReader::StartElement) continue;
            if (r.name() != u"graphic") continue;
            if (r.namespaceUri() != kMainNs && badNs.isEmpty())
                badNs = n + ": <graphic> is in " + r.namespaceUri().toString();
        }
    }
    check(badNs.isEmpty(), label + ": graphicFrame graphics use the main namespace"
                         + (badNs.isEmpty() ? QString() : " (" + badNs + ")"));

    // 6. A worksheet's children must follow the CT_Worksheet sequence.
    //
    // <headerFooter> comes BEFORE <drawing>, which comes before
    // <legacyDrawingHF>. Emitting the drawing first is what made every
    // from-scratch export unopenable in Excel between 1.5.0 and 1.7.6.
    static const QStringList kWsOrder = {
        "sheetPr", "dimension", "sheetViews", "sheetFormatPr", "cols", "sheetData",
        "sheetCalcPr", "sheetProtection", "protectedRanges", "scenarios", "autoFilter",
        "sortState", "dataConsolidate", "customSheetViews", "mergeCells", "phoneticPr",
        "conditionalFormatting", "dataValidations", "hyperlinks", "printOptions",
        "pageMargins", "pageSetup", "headerFooter", "rowBreaks", "colBreaks",
        "customProperties", "cellWatches", "ignoredErrors", "smartTags", "drawing",
        "legacyDrawing", "legacyDrawingHF", "drawingHF", "picture", "oleObjects",
        "controls", "webPublishItems", "tableParts", "extLst",
    };
    QString wsBad;
    for (const QString& n : parts) {
        if (!n.startsWith("xl/worksheets/sheet") || !n.endsWith(".xml")) continue;
        QXmlStreamReader r(bytes.value(n));
        int depth = 0, last = -1;
        while (!r.atEnd()) {
            const auto tok = r.readNext();
            if (tok == QXmlStreamReader::StartElement) {
                if (++depth != 2) continue;
                const QString el = r.name().toString();
                const int pos = kWsOrder.indexOf(el);
                if (pos < 0) continue;              // not an element we police
                if (pos < last && wsBad.isEmpty())
                    wsBad = n + ": <" + el + "> comes too late";
                else if (pos >= last) last = pos;
            } else if (tok == QXmlStreamReader::EndElement) {
                --depth;
            }
        }
    }
    check(wsBad.isEmpty(), label + ": worksheet elements are in schema order"
                         + (wsBad.isEmpty() ? QString() : " (" + wsBad + ")"));
}

// ── The rebuild path ────────────────────────────────────────────────────────
static void testRebuild(const QString& dir) {
    out << "\n[rebuild] exportXlsx with an app-made chart\n";

    XlsxSheet sh = makeSheet();
    sh.cellText  = tableReader();
    sh.charts.push_back(makeChart(ChartType::Column));

    check(!buildChartPartXml(sh.charts.front(), sh.name, sh.cellText).isEmpty(),
          "a chart part was produced");

    const QString path = dir + "/rebuild.xlsx";
    std::vector<XlsxSheet> sheets { sh };
    check(exportXlsx(path, sheets), "exportXlsx wrote the file");

    validatePackage(path, "rebuild");

    std::vector<XlsxSheet> back;
    check(importXlsx(path, back), "the file reads back");
    if (back.empty()) return;

    check(back[0].charts.size() == 1,
          QString("one chart came back (got %1)").arg(back[0].charts.size()));
    if (back[0].charts.empty()) return;

    const ChartSpec& c = back[0].charts.front();
    check(c.type == ChartType::Column, "type survived as Column");
    check(c.series.size() == 2,
          QString("two series came back (got %1)").arg(c.series.size()));
    if (c.series.size() == 2) {
        check(c.series[0].name == "Sales", "first series is named Sales");
        check(c.series[1].name == "Costs", "second series is named Costs");
        check(c.series[0].valRange == QRect(1, 1, 1, 3),
              "first series points at B2:B4");
        check(c.series[0].cache.size() == 3
              && qFuzzyCompare(c.series[0].cache[0], 120.0),
              "cached values came through");
        check(c.series[0].sheet == "Data", "the reference names the sheet");
    }
    check(c.categories == QStringList({ "North", "South", "East" }),
          "categories came back");
    check(c.catRange == QRect(0, 1, 1, 3), "categories point at A2:A4");
    check(c.anchor.fromCol == 4 && c.anchor.fromRow == 1
          && c.anchor.toCol == 10 && c.anchor.toRow == 15,
          "the two-corner anchor survived");
}

// A picture added in the app has to survive a rebuild the same way a chart does:
// its bytes become a media part, and the anchor references it.
static void testPictureRebuild(const QString& dir) {
    out << "\n[rebuild] a picture added in the app survives\n";
    XlsxSheet sh = makeSheet();
    sh.cellText  = tableReader();
    sh.images.push_back(makePicture());

    const QString path = dir + "/picture.xlsx";
    std::vector<XlsxSheet> sheets { sh }, back;
    check(exportXlsx(path, sheets), "exportXlsx wrote the file");
    validatePackage(path, "picture");
    if (!importXlsx(path, back) || back.empty()) { check(false, "reads back"); return; }

    // The export mark is a picture too, so the sheet's own has to be picked out
    // by its bytes rather than by counting.
    const QByteArray want = makePicture().data;
    bool found = false;
    for (const SheetImage& i : back[0].images) if (i.data == want) found = true;
    check(found, "the picture came back with its bytes intact");
}

// Colours have to survive the trip out and back.
//
// A series can carry one colour of its own and a colour per point, and both are
// written now: a chart regenerated without them comes back in a default palette
// the file never mentioned. This also guards the schema positions, since
// <c:spPr> and <c:dPt> each sit at a fixed point in the series sequence and
// Excel refuses the file when they do not.
static void testColoursRoundTrip(const QString& dir) {
    out << "\n[rebuild] series and per-point colours survive\n";

    XlsxSheet sh = makeSheet();
    sh.cellText  = tableReader();

    // An explicit chart, the shape an imported one takes.
    ChartSpec cs;
    cs.type     = ChartType::Column;
    cs.geom     = QRect(40, 40, 420, 280);
    cs.anchor.fromCol = 4;  cs.anchor.fromRow = 1;
    cs.anchor.toCol   = 10; cs.anchor.toRow   = 15;
    cs.catRange = QRect(0, 1, 1, 3);
    cs.catSheet = QStringLiteral("Data");
    cs.hasTitle = false;

    ChartSeries a;
    a.name     = QStringLiteral("Sales");
    a.sheet    = QStringLiteral("Data");
    a.valRange = QRect(1, 1, 1, 3);
    a.cache    = { 120.0, 90.0, 140.0 };
    a.color    = QColor("#59BD94");
    cs.series.push_back(a);

    ChartSeries b;
    b.name     = QStringLiteral("Costs");
    b.sheet    = QStringLiteral("Data");
    b.valRange = QRect(2, 1, 1, 3);
    b.cache    = { 80.0, 55.0, 70.0 };
    b.pointColors = { QColor("#BBE192"), QColor("#59BD94"), QColor("#F4F4F7") };
    cs.series.push_back(b);

    sh.charts.push_back(cs);

    const QString path = dir + "/colours.xlsx";
    std::vector<XlsxSheet> sheets { sh }, back;
    check(exportXlsx(path, sheets), "exportXlsx wrote the file");
    validatePackage(path, "colours");
    if (!importXlsx(path, back) || back.empty() || back[0].charts.empty()) {
        check(false, "reads back with a chart");
        return;
    }
    const ChartSpec& got = back[0].charts.front();
    check(got.series.size() == 2,
          QString("two series came back (got %1)").arg(got.series.size()));
    if (got.series.size() != 2) return;

    check(got.series[0].color.name().compare("#59bd94", Qt::CaseInsensitive) == 0,
          QString("the series colour survived (got %1)").arg(got.series[0].color.name()));
    check(got.series[1].pointColors.size() == 3,
          QString("three point colours came back (got %1)")
              .arg(got.series[1].pointColors.size()));
    if (got.series[1].pointColors.size() == 3)
        check(got.series[1].pointColors[0].name().compare("#bbe192", Qt::CaseInsensitive) == 0
              && got.series[1].pointColors[2].name().compare("#f4f4f7", Qt::CaseInsensitive) == 0,
              "and they came back in the right order");
}

// Every chart type, to catch a plot element whose schema order is wrong.
static void testEveryType(const QString& dir) {
    out << "\n[rebuild] every chart type round-trips\n";
    const struct { ChartType t; const char* name; } kinds[] = {
        { ChartType::Column,   "Column"   }, { ChartType::Bar,      "Bar"      },
        { ChartType::Line,     "Line"     }, { ChartType::Area,     "Area"     },
        { ChartType::Pie,      "Pie"      }, { ChartType::Doughnut, "Doughnut" },
        { ChartType::Scatter,  "Scatter"  },
    };
    for (const auto& k : kinds) {
        XlsxSheet sh = makeSheet();
        sh.cellText  = tableReader();
        sh.charts.push_back(makeChart(k.t));
        const QString path = QString("%1/type-%2.xlsx").arg(dir, k.name);
        std::vector<XlsxSheet> sheets { sh }, back;
        if (!exportXlsx(path, sheets)) { check(false, QString("%1: write").arg(k.name)); continue; }
        if (!importXlsx(path, back))   { check(false, QString("%1: read").arg(k.name));  continue; }
        const bool got = !back.empty() && back[0].charts.size() == 1
                         && back[0].charts.front().type == k.t
                         && back[0].charts.front().series.size() == 2;
        check(got, QString("%1 round-tripped with both series").arg(k.name));
    }
}

// ── The preserving path, against real workbooks ─────────────────────────────
static void testPreserving(const QString& corpus, const QString& dir) {
    out << "\n[preserving] adding a chart to a real workbook\n";
    QDir d(corpus);
    const QStringList files = d.entryList({ "*.xlsx" }, QDir::Files, QDir::Name);
    if (files.isEmpty()) { out << "  (no .xlsx in " << corpus << ")\n"; return; }

    for (const QString& f : files) {
        const QString src = d.filePath(f);
        QFile in(src);
        if (!in.open(QIODevice::ReadOnly)) continue;
        const QByteArray original = in.readAll();
        in.close();

        std::vector<XlsxSheet> sheets;
        if (!importXlsx(src, sheets) || sheets.empty()) {
            out << "  skip " << f << " (does not import)\n";
            continue;
        }

        // Count what the workbook already had, then add one chart to EVERY
        // sheet. Both halves of the injection have to be covered and which one
        // a sheet takes depends on whether it already has a drawing part: a
        // sheet with one gets an anchor appended, a sheet without one needs the
        // drawing created and a <drawing> spliced into the worksheet. Charting
        // every sheet reaches both without depending on what this corpus holds.
        //
        // Each chart plots a table planted well clear of the workbook's own
        // data, so the assertions do not depend on this file's contents.
        int before = 0;
        for (const auto& s : sheets) before += int(s.charts.size());
        const int kCol = 40, kRow = 500;
        for (auto& s : sheets) plantTable(s, kCol, kRow);
        for (auto& s : sheets) {
            // The callback owns a copy of what it reads: capturing the sheet by
            // reference would dangle as soon as this loop moved on.
            QHash<qint64, QString> byCell;
            for (const auto& cell : s.cells)
                byCell.insert((qint64(cell.col) << 32) | quint32(cell.row), cell.content);
            s.cellText = [byCell](int c, int r) -> QString {
                return byCell.value((qint64(c) << 32) | quint32(r));
            };
        }
        int picsBefore = 0;
        for (const auto& s : sheets) picsBefore += int(s.images.size());
        for (auto& s : sheets) {
            s.charts.push_back(makeChart(ChartType::Bar, QRect(kCol, kRow, 3, 4)));
            s.images.push_back(makePicture());
        }
        const int added = int(sheets.size());

        if (!canPreserveXlsx(sheets, original)) {
            out << "  skip " << f << " (cannot be preserved, falls back to rebuild)\n";
            continue;
        }
        const QString path = dir + "/pres-" + f;
        if (!exportXlsxPreserving(path, sheets, original)) {
            check(false, QString("%1: preserving write").arg(f));
            continue;
        }

        validatePackage(path, f);

        std::vector<XlsxSheet> back;
        if (!importXlsx(path, back)) { check(false, QString("%1: reads back").arg(f)); continue; }
        int after = 0;
        for (const auto& s : back) after += int(s.charts.size());
        check(after == before + added,
              QString("%1: %2 chart(s) plus %3 added became %4")
                  .arg(f).arg(before).arg(added).arg(after));

        // The added chart must be the Bar one, and the originals untouched.
        int intact = 0;
        for (const auto& sheet : back)
            for (const ChartSpec& c : sheet.charts)
                if (c.type == ChartType::Bar && c.series.size() == 2
                    && c.series[0].name == "Sales" && c.series[1].name == "Costs"
                    && c.categories == QStringList({ "North", "South", "East" }))
                    ++intact;
        check(intact == added,
              QString("%1: all %2 added chart(s) have their series and labels (got %3)")
                  .arg(f).arg(added).arg(intact));

        // Pictures: exactly the ones we added, on top of the file's own. An
        // imported picture wrongly treated as app-made would be written into a
        // package that already holds it, so the count catches duplication in
        // both directions.
        const QByteArray want = makePicture().data;
        int picsAfter = 0, mineBack = 0;
        for (const auto& sheet : back) {
            picsAfter += int(sheet.images.size());
            for (const SheetImage& i : sheet.images) if (i.data == want) ++mineBack;
        }
        check(mineBack == added,
              QString("%1: %2 added picture(s) came back (got %3)")
                  .arg(f).arg(added).arg(mineBack));
        check(picsAfter == picsBefore + added,
              QString("%1: %2 picture(s) plus %3 added became %4, none duplicated")
                  .arg(f).arg(picsBefore).arg(added).arg(picsAfter));

        // Nothing else in the package may have been dropped.
        check(back.size() == sheets.size(),
              QString("%1: sheet count unchanged").arg(f));
    }
}

// A preserving save of a workbook with no app-made charts has to come out
// exactly as it did before any of this existed. The chart injection is written
// to be a no-op in that case, and this is what says so: the same bytes, not
// merely a package that still happens to work.
static void testNoChartsUnchanged(const QString& corpus, const QString& dir) {
    out << "\n[preserving] a save with no app charts changes nothing\n";
    QDir d(corpus);
    for (const QString& f : d.entryList({ "*.xlsx" }, QDir::Files, QDir::Name)) {
        const QString src = d.filePath(f);
        QFile in(src);
        if (!in.open(QIODevice::ReadOnly)) continue;
        const QByteArray original = in.readAll();
        in.close();

        std::vector<XlsxSheet> sheets;
        if (!importXlsx(src, sheets) || sheets.empty()) continue;
        if (!canPreserveXlsx(sheets, original)) continue;

        // Saved twice: once as the workbook stands, once with the charts it
        // was imported with left in place (all of them fromFile). Neither may
        // differ from the other.
        const QString a = dir + "/plain-a-" + f;
        const QString b = dir + "/plain-b-" + f;
        if (!exportXlsxPreserving(a, sheets, original)) { check(false, f + ": save A"); continue; }
        for (auto& sh : sheets) sh.charts.clear();
        if (!exportXlsxPreserving(b, sheets, original)) { check(false, f + ": save B"); continue; }

        QFile fa(a), fb(b);
        if (!fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly)) continue;
        check(fa.readAll() == fb.readAll(),
              QString("%1: imported charts add nothing to the package").arg(f));
    }
}

int main(int argc, char** argv) {
    // A GUI application, not a console one: exportXlsx() stamps the export mark
    // on every sheet, and rendering that artwork goes through QPainter. With a
    // QCoreApplication it takes the process down before the first check runs.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    QTemporaryDir tmp;
    if (!tmp.isValid()) { out << "no temp dir\n"; return 2; }

    // A second argument keeps the written workbooks somewhere they can be
    // opened by hand (or by Excel) instead of vanishing with the temp dir.
    QString dir = tmp.path();
    if (argc > 2) {
        dir = QString::fromLocal8Bit(argv[2]);
        QDir().mkpath(dir);
        out << "writing artifacts to " << dir << "\n";
    }

    testRebuild(dir);
    testPictureRebuild(dir);
    testColoursRoundTrip(dir);
    testEveryType(dir);
    if (argc > 1) {
        const QString corpus = QString::fromLocal8Bit(argv[1]);
        testNoChartsUnchanged(corpus, dir);
        testPreserving(corpus, dir);
    }
    else out << "\n[preserving] skipped (pass a directory of .xlsx files)\n";

    out << "\n" << (failures ? QString("%1 FAILURE(S)").arg(failures)
                             : QStringLiteral("all checks passed")) << "\n";
    out.flush();
    return failures ? 1 : 0;
}
