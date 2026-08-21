// ─────────────────────────────────────────────────────────────────────────────
// XlsxDrawing.cpp
// See XlsxDrawing.h. Two parsers live here: the drawing part (placement) and
// the chart part (content).
// ─────────────────────────────────────────────────────────────────────────────
#include "XlsxDrawing.h"

#include <QMap>
#include <QPainterPath>
#include <QtMath>
#include <QXmlStreamReader>

namespace NativeOffice {

int emuToPx(qint64 emu) {
    // 914400 EMU per inch, 96 px per inch.
    return static_cast<int>(emu / 9525);
}

namespace {

// The drawing and chart schemas use prefixes (xdr:, a:, c:, r:) that differ
// between producers, so every comparison here is on the local name.
inline QString localName(const QXmlStreamReader& r) { return r.name().toString(); }

// True when `path` holds `name` at any depth.
bool inside(const QStringList& path, const char* name) {
    return path.contains(QLatin1String(name));
}

// ── A1 references ────────────────────────────────────────────────────────────
// A chart reference looks like  'Sheet Name'!$B$7:$M$7  or  Sheet1!$A$1.
// Splits off the sheet name and turns the range into a QRect of cell indices.
bool parseChartRef(const QString& ref, QString& sheet, QRect& out) {
    if (ref.isEmpty()) return false;
    QString body = ref;
    sheet.clear();

    const int bang = ref.lastIndexOf('!');
    if (bang >= 0) {
        sheet = ref.left(bang);
        body  = ref.mid(bang + 1);
        if (sheet.size() >= 2 && sheet.startsWith('\'') && sheet.endsWith('\'')) {
            sheet = sheet.mid(1, sheet.size() - 2);
            // An apostrophe inside a quoted sheet name is written doubled.
            sheet.replace(QLatin1String("''"), QLatin1String("'"));
        }
    }
    body.remove('$');
    if (body.isEmpty()) return false;

    auto cell = [](const QString& s, int& col, int& row) {
        int i = 0, c = 0;
        while (i < s.size() && s[i].isLetter()) {
            c = c * 26 + (s[i].toUpper().toLatin1() - 'A' + 1); ++i;
        }
        if (i == 0) return false;
        bool ok = false;
        const int r = s.mid(i).toInt(&ok);
        if (!ok || r < 1) return false;
        col = c - 1; row = r - 1;
        return true;
    };

    const int colon = body.indexOf(':');
    int c1 = 0, r1 = 0, c2 = 0, r2 = 0;
    if (colon < 0) {
        if (!cell(body, c1, r1)) return false;
        c2 = c1; r2 = r1;
    } else {
        if (!cell(body.left(colon), c1, r1))  return false;
        if (!cell(body.mid(colon + 1), c2, r2)) return false;
    }
    out = QRect(QPoint(qMin(c1, c2), qMin(r1, r2)),
                QPoint(qMax(c1, c2), qMax(r1, r2)));
    return true;
}

// ── Colours ──────────────────────────────────────────────────────────────────
// A DrawingML colour is a base plus a stack of transforms, and the base is more
// often a reference into the workbook theme than a literal RGB value. Reading
// only srgbClr, as this used to, left most shapes with no colour at all: across
// the test corpus schemeClr outnumbers srgbClr better than two to one.
bool isColorElement(const QString& n) {
    return n == QLatin1String("srgbClr")  || n == QLatin1String("schemeClr")
        || n == QLatin1String("sysClr")   || n == QLatin1String("prstClr")
        || n == QLatin1String("scrgbClr");
}

// The theme names two colours per role. A drawing refers to them by their
// mapped names, so tx1/bg1/tx2/bg2 have to be translated back.
QColor themeColor(const ThemeColors& theme, QString name) {
    if      (name == QLatin1String("tx1")) name = QStringLiteral("dk1");
    else if (name == QLatin1String("bg1")) name = QStringLiteral("lt1");
    else if (name == QLatin1String("tx2")) name = QStringLiteral("dk2");
    else if (name == QLatin1String("bg2")) name = QStringLiteral("lt2");
    return theme.value(name);
}

// One colour transform. Office writes these as percentages scaled by 1000, so
// lumMod val="67000" means "two thirds of the luminance".
QColor applyColorMod(QColor c, const QString& name, QStringView valText) {
    const double v = valText.endsWith(QLatin1Char('%'))
                         ? valText.chopped(1).toDouble() / 100.0
                         : valText.toDouble() / 100000.0;
    float h = 0, sat = 0, l = 0, a = 1;
    if (name == QLatin1String("lumMod") || name == QLatin1String("lumOff")
        || name == QLatin1String("satMod")) {
        c.getHslF(&h, &sat, &l, &a);
        if (h < 0) h = 0;                       // grey: Qt reports no hue
    }
    if      (name == QLatin1String("lumMod")) c.setHslF(h, sat, qBound(0.0f, float(l * v), 1.0f), a);
    else if (name == QLatin1String("lumOff")) c.setHslF(h, sat, qBound(0.0f, float(l + v), 1.0f), a);
    else if (name == QLatin1String("satMod")) c.setHslF(h, qBound(0.0f, float(sat * v), 1.0f), l, a);
    else if (name == QLatin1String("shade"))
        c = QColor(int(c.red() * v), int(c.green() * v), int(c.blue() * v), c.alpha());
    else if (name == QLatin1String("tint")) {
        auto mix = [v](int ch) { return int(ch * v + 255 * (1.0 - v)); };
        c = QColor(mix(c.red()), mix(c.green()), mix(c.blue()), c.alpha());
    }
    else if (name == QLatin1String("alpha")) c.setAlphaF(float(qBound(0.0, v, 1.0)));
    return c;
}

// Resolve the colour element the reader is sitting on, consuming its subtree.
QColor readColorElement(QXmlStreamReader& r, const ThemeColors& theme) {
    const QString kind = localName(r);
    const auto    at   = r.attributes();
    const QString val  = at.value(QLatin1String("val")).toString();
    QColor c;

    if (kind == QLatin1String("srgbClr")) {
        if (val.size() == 6) c = QColor(QLatin1Char('#') + val);
    } else if (kind == QLatin1String("schemeClr")) {
        c = themeColor(theme, val);
    } else if (kind == QLatin1String("sysClr")) {
        // lastClr is what the authoring app resolved it to, which is a better
        // answer than this machine's window colours.
        const QString last = at.value(QLatin1String("lastClr")).toString();
        if (last.size() == 6)                      c = QColor(QLatin1Char('#') + last);
        else if (val == QLatin1String("window"))   c = QColor(Qt::white);
        else                                       c = QColor(Qt::black);
    } else if (kind == QLatin1String("prstClr")) {
        c = QColor(val);
    } else if (kind == QLatin1String("scrgbClr")) {
        auto pc = [&at](const char* k) {
            return int(qBound(0.0, at.value(QLatin1String(k)).toDouble() / 100000.0, 1.0) * 255);
        };
        c = QColor(pc("r"), pc("g"), pc("b"));
    }

    int depth = 1;
    while (!r.atEnd() && depth > 0) {
        const auto tok = r.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            ++depth;
            if (c.isValid())
                c = applyColorMod(c, localName(r), r.attributes().value(QLatin1String("val")));
        } else if (tok == QXmlStreamReader::EndElement) {
            --depth;
        }
    }
    return c;
}

// The first colour anywhere inside the element the reader is sitting on,
// consuming it. Used for solidFill wrappers and for a chart series' spPr.
QColor readFirstColor(QXmlStreamReader& r, const ThemeColors& theme) {
    QColor found;
    int depth = 1;
    while (!r.atEnd() && depth > 0) {
        const auto tok = r.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            if (!found.isValid() && isColorElement(localName(r))) {
                found = readColorElement(r, theme);   // consumes its own subtree
                continue;
            }
            ++depth;
        } else if (tok == QXmlStreamReader::EndElement) {
            --depth;
        }
    }
    return found;
}

// ── Cached values ────────────────────────────────────────────────────────────
// A cache is a sparse list of <c:pt idx="n"><c:v>value</c:v></c:pt>. Excel omits
// blank points entirely, so read into a map keyed by idx and flatten afterwards
// rather than assuming the points arrive dense and in order.
struct PtCache {
    QMap<int, QString> pts;

    [[nodiscard]] QStringList strings() const {
        QStringList out;
        if (pts.isEmpty()) return out;
        const int n = pts.lastKey() + 1;
        for (int i = 0; i < n; ++i) out << pts.value(i);
        return out;
    }
    [[nodiscard]] QVector<double> numbers() const {
        QVector<double> out;
        if (pts.isEmpty()) return out;
        const int n = pts.lastKey() + 1;
        out.reserve(n);
        for (int i = 0; i < n; ++i) out << pts.value(i).toDouble();
        return out;
    }
};

ChartType chartTypeFor(const QString& element, const QString& barDir) {
    if (element == QLatin1String("barChart") || element == QLatin1String("bar3DChart"))
        return barDir == QLatin1String("bar") ? ChartType::Bar : ChartType::Column;
    if (element == QLatin1String("lineChart")  || element == QLatin1String("line3DChart"))  return ChartType::Line;
    if (element == QLatin1String("areaChart")  || element == QLatin1String("area3DChart"))  return ChartType::Area;
    if (element == QLatin1String("pieChart")   || element == QLatin1String("pie3DChart"))   return ChartType::Pie;
    if (element == QLatin1String("doughnutChart"))                                          return ChartType::Doughnut;
    if (element == QLatin1String("scatterChart") || element == QLatin1String("bubbleChart"))return ChartType::Scatter;
    return ChartType::Column;
}

bool isPlotElement(const QString& n) {
    return n.endsWith(QLatin1String("Chart"))
           && (n == QLatin1String("barChart")      || n == QLatin1String("bar3DChart")
            || n == QLatin1String("lineChart")     || n == QLatin1String("line3DChart")
            || n == QLatin1String("areaChart")     || n == QLatin1String("area3DChart")
            || n == QLatin1String("pieChart")      || n == QLatin1String("pie3DChart")
            || n == QLatin1String("doughnutChart") || n == QLatin1String("scatterChart")
            || n == QLatin1String("bubbleChart")   || n == QLatin1String("radarChart")
            || n == QLatin1String("stockChart")    || n == QLatin1String("surfaceChart"));
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Chart part
// ─────────────────────────────────────────────────────────────────────────────
bool parseChartPart(const QByteArray& chartXml, ChartSpec& spec,
                    const ThemeColors& theme) {
    QXmlStreamReader r(chartXml);
    QStringList path;

    bool        haveType = false;
    QString     pendingBarDir;
    QString     plotElement;

    ChartSeries cur;
    bool        inSer = false;
    PtCache     catCache, valCache;
    QString     catRef, valRef;
    QString     txLiteral, txCachedName, txRef;
    int         pendingDptIdx = -1;
    QMap<int, QColor> dptColors;
    bool        sawLegend = false;
    bool        titleDeleted = false;

    auto commitSeries = [&] {
        // A series with neither live reference nor cached values plots nothing.
        if (valRef.isEmpty() && valCache.pts.isEmpty()) return;

        cur.name = !txLiteral.isEmpty() ? txLiteral : txCachedName;
        // No literal and no cached name: the name is a reference to a header
        // cell, to be read once the sheet it points at exists.
        if (cur.name.isEmpty() && !txRef.isEmpty()) {
            QRect nr; QString ns;
            if (parseChartRef(txRef, ns, nr)) { cur.nameRange = nr; cur.nameSheet = ns; }
        }
        cur.cache = valCache.numbers();
        QRect rect; QString sheet;
        if (parseChartRef(valRef, sheet, rect)) { cur.valRange = rect; cur.sheet = sheet; }

        if (!dptColors.isEmpty()) {
            const int n = dptColors.lastKey() + 1;
            cur.pointColors.reserve(n);
            for (int i = 0; i < n; ++i) cur.pointColors << dptColors.value(i);
        }

        // Categories are shared by every series; take the first one that has them.
        if (spec.categories.isEmpty() && !catCache.pts.isEmpty())
            spec.categories = catCache.strings();
        if (spec.catRange.isNull() && !catRef.isEmpty()) {
            QRect cr; QString cs;
            if (parseChartRef(catRef, cs, cr)) { spec.catRange = cr; spec.catSheet = cs; }
        }

        spec.series.push_back(cur);
        cur = ChartSeries{};
        catCache = PtCache{}; valCache = PtCache{};
        catRef.clear(); valRef.clear();
        txLiteral.clear(); txCachedName.clear(); txRef.clear();
        dptColors.clear();
        pendingDptIdx = -1;
    };

    while (!r.atEnd()) {
        const auto tok = r.readNext();

        if (tok == QXmlStreamReader::StartElement) {
            const QString n = localName(r);
            path.push_back(n);

            if (isPlotElement(n) && !haveType) { plotElement = n; haveType = true; }

            // Presentation, all of it optional and all of it previously guessed.
            if (n == QLatin1String("autoTitleDeleted")
                && r.attributes().value(QLatin1String("val")) != QLatin1String("0"))
                titleDeleted = true;
            if (n == QLatin1String("legend")) sawLegend = true;
            if (n == QLatin1String("legendPos")) {
                const QString v = r.attributes().value(QLatin1String("val")).toString();
                if (!v.isEmpty()) spec.legendPos = v.at(0).toLatin1();
            }
            // Any explicit "show this label" turns labels on. Excel writes a
            // dLbls block with everything set to 0 when it wants none.
            if ((n == QLatin1String("showVal") || n == QLatin1String("showCatName")
                 || n == QLatin1String("showPercent"))
                && r.attributes().value(QLatin1String("val")) == QLatin1String("1"))
                spec.showDataLabels = true;
            // The value axis number format, so a currency axis reads as currency.
            if (n == QLatin1String("numFmt") && inside(path, "valAx")
                && spec.valueAxisFormat.isEmpty())
                spec.valueAxisFormat =
                    r.attributes().value(QLatin1String("formatCode")).toString();

            // A combo chart holds several plot elements; the first one decides
            // the type, so only its direction counts.
            if (n == QLatin1String("barDir") && pendingBarDir.isEmpty())
                pendingBarDir = r.attributes().value(QLatin1String("val")).toString();

            if (n == QLatin1String("ser") && inside(path, "plotArea")) {
                inSer = true;
                cur = ChartSeries{};
                catCache = PtCache{}; valCache = PtCache{};
                catRef.clear(); valRef.clear();
                txLiteral.clear(); txCachedName.clear(); txRef.clear();
                dptColors.clear();
            }

            // Chart title. Axis titles live under catAx/valAx/serAx, so exclude
            // those and anything inside the plot area.
            if (n == QLatin1String("t") && inside(path, "title")
                && !inside(path, "plotArea") && !inside(path, "catAx")
                && !inside(path, "valAx")    && !inside(path, "serAx")) {
                spec.title += r.readElementText(QXmlStreamReader::IncludeChildElements);
                path.removeLast();
                continue;
            }

            if (!inSer) continue;

            // Per-slice colours: <c:dPt><c:idx val="n"/><c:spPr>…</c:spPr></c:dPt>
            if (n == QLatin1String("idx") && inside(path, "dPt"))
                pendingDptIdx = r.attributes().value(QLatin1String("val")).toInt();

            if (n == QLatin1String("spPr")) {
                const bool forPoint = inside(path, "dPt");
                const QColor c = readFirstColor(r, theme);
                path.removeLast();
                if (c.isValid()) {
                    if (forPoint && pendingDptIdx >= 0) dptColors.insert(pendingDptIdx, c);
                    else if (!forPoint && !cur.color.isValid()) cur.color = c;
                }
                continue;
            }

            if (n == QLatin1String("f")) {
                const QString ref = r.readElementText(QXmlStreamReader::IncludeChildElements);
                if      (inside(path, "tx"))                         txRef  = ref;
                else if (inside(path, "cat") || inside(path, "xVal")) catRef = ref;
                else if (inside(path, "val") || inside(path, "yVal")) valRef = ref;
                path.removeLast();
                continue;
            }

            if (n == QLatin1String("pt")) {
                const int idx = r.attributes().value(QLatin1String("idx")).toInt();
                // Read the point's <c:v> without leaving the stack unbalanced.
                QString v;
                int depth = 1;
                while (!r.atEnd() && depth > 0) {
                    const auto t2 = r.readNext();
                    if (t2 == QXmlStreamReader::StartElement) {
                        if (localName(r) == QLatin1String("v"))
                            v = r.readElementText(QXmlStreamReader::IncludeChildElements);
                        else ++depth;
                    } else if (t2 == QXmlStreamReader::EndElement) {
                        --depth;
                    }
                }
                path.removeLast();
                if (inside(path, "tx"))                              txCachedName = v;
                else if (inside(path, "cat") || inside(path, "xVal")) catCache.pts.insert(idx, v);
                else if (inside(path, "val") || inside(path, "yVal")) valCache.pts.insert(idx, v);
                continue;
            }

            // <c:tx><c:v>Name</c:v></c:tx> is the literal form, no reference.
            if (n == QLatin1String("v") && inside(path, "tx") && !inside(path, "pt")) {
                txLiteral = r.readElementText(QXmlStreamReader::IncludeChildElements);
                path.removeLast();
                continue;
            }

        } else if (tok == QXmlStreamReader::EndElement) {
            const QString n = localName(r);
            if (n == QLatin1String("ser") && inSer) { commitSeries(); inSer = false; }
            if (!path.isEmpty()) path.removeLast();
        }
    }

    spec.type  = chartTypeFor(plotElement, pendingBarDir);
    spec.title = spec.title.trimmed();
    // A chart has a title only when the file gives it one. Excel marks a
    // removed title with autoTitleDeleted, and inventing "<Type> Chart" in its
    // place put a caption on charts that are meant to have none.
    spec.hasTitle   = !titleDeleted && !spec.title.isEmpty();
    if (!spec.hasTitle) spec.title.clear();
    spec.showLegend = sawLegend;
    return !spec.series.isEmpty();
}


// ─────────────────────────────────────────────────────────────────────────────
// Theme part
// ─────────────────────────────────────────────────────────────────────────────
ThemeColors parseThemeColors(const QByteArray& themeXml) {
    ThemeColors out;
    if (themeXml.isEmpty()) return out;

    static const QStringList kSlots = {
        QStringLiteral("dk1"), QStringLiteral("lt1"), QStringLiteral("dk2"),
        QStringLiteral("lt2"), QStringLiteral("accent1"), QStringLiteral("accent2"),
        QStringLiteral("accent3"), QStringLiteral("accent4"), QStringLiteral("accent5"),
        QStringLiteral("accent6"), QStringLiteral("hlink"), QStringLiteral("folHlink"),
    };

    QXmlStreamReader r(themeXml);
    bool    inScheme = false;
    QString slot;
    while (!r.atEnd()) {
        const auto tok = r.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            const QString n = localName(r);
            if (n == QLatin1String("clrScheme")) { inScheme = true; continue; }
            if (!inScheme) continue;
            if (kSlots.contains(n)) { slot = n; continue; }
            if (!slot.isEmpty() && isColorElement(n)) {
                const QColor c = readColorElement(r, {});
                if (c.isValid()) out.insert(slot, c);
                slot.clear();
            }
        } else if (tok == QXmlStreamReader::EndElement
                   && localName(r) == QLatin1String("clrScheme")) {
            break;   // the rest of the theme is fonts and effects
        }
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Drawing part
//
// A drawing is a tree, not a list: an anchor holds objects, and one of those
// objects can be a group holding more. The old parser read it as a list and
// kept one chart and one picture per anchor, so a group of seventeen objects
// (media_heavy has one) came out as at most two.
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// Maps a rectangle in some container's child coordinate space onto a fraction
// of the anchored object. A group declares the space its members are drawn in
// (chOff/chExt) and the extent it occupies (off/ext); expressing the result as
// a fraction is what lets the renderer stay ignorant of groups and still keep
// their internal layout at any cell size or zoom.
struct Mapper {
    QRectF box { 0, 0, 1, 1 };
    qint64 ox { 0 }, oy { 0 }, cx { 0 }, cy { 0 };   // cx/cy 0 => no child space

    [[nodiscard]] QRectF map(qint64 x, qint64 y, qint64 w, qint64 h) const {
        // No child space means the object is anchored directly: the anchor
        // decides where it goes and its own xfrm is decoration.
        if (cx <= 0 || cy <= 0) return box;
        return QRectF(box.x() + (double(x - ox) / double(cx)) * box.width(),
                      box.y() + (double(y - oy) / double(cy)) * box.height(),
                      (double(w) / double(cx)) * box.width(),
                      (double(h) / double(cy)) * box.height());
    }
};

struct DrawCtx {
    const QHash<QString, QString>&                   rels;
    const std::function<QByteArray(const QString&)>& fetch;
    const ThemeColors&                               theme;
    std::vector<ChartSpec>&                          charts;
    std::vector<SheetImage>&                         images;
    std::vector<SheetShape>&                         shapes;

    CellAnchor anchor;
    qint64     extCx { 0 }, extCy { 0 }, posX { 0 }, posY { 0 };

    // A two-corner anchor sizes itself from the cells it spans. Anything else
    // needs a pixel size, which is what xdr:ext carries.
    [[nodiscard]] QRect objectGeom() const {
        if (!anchor.hasTo() && (extCx > 0 || extCy > 0))
            return QRect(emuToPx(posX), emuToPx(posY), emuToPx(extCx), emuToPx(extCy));
        return {};
    }
};

// ── Small subtree readers. Each one consumes the element the reader sits on. ─
void readOffExt(QXmlStreamReader& r, qint64& x, qint64& y, qint64& cx, qint64& cy,
                qint64* chX = nullptr, qint64* chY = nullptr,
                qint64* chCx = nullptr, qint64* chCy = nullptr) {
    int depth = 1;
    while (!r.atEnd() && depth > 0) {
        const auto tok = r.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            ++depth;
            const QString n = localName(r);
            const auto    a = r.attributes();
            if (n == QLatin1String("off")) {
                x = a.value(QLatin1String("x")).toLongLong();
                y = a.value(QLatin1String("y")).toLongLong();
            } else if (n == QLatin1String("ext")) {
                cx = a.value(QLatin1String("cx")).toLongLong();
                cy = a.value(QLatin1String("cy")).toLongLong();
            } else if (n == QLatin1String("chOff") && chX) {
                *chX = a.value(QLatin1String("x")).toLongLong();
                *chY = a.value(QLatin1String("y")).toLongLong();
            } else if (n == QLatin1String("chExt") && chCx) {
                *chCx = a.value(QLatin1String("cx")).toLongLong();
                *chCy = a.value(QLatin1String("cy")).toLongLong();
            }
        } else if (tok == QXmlStreamReader::EndElement) {
            --depth;
        }
    }
}

void skipSubtree(QXmlStreamReader& r) {
    int depth = 1;
    while (!r.atEnd() && depth > 0) {
        const auto tok = r.readNext();
        if      (tok == QXmlStreamReader::StartElement) ++depth;
        else if (tok == QXmlStreamReader::EndElement)   --depth;
    }
}

// prstGeom's adjust handle. For a rounded rectangle it is the corner radius as
// 1/100000 of the shorter side, and it varies a lot: the corpus has everything
// from a barely eased 8000 to a fully rounded 50000.
void readAdjust(QXmlStreamReader& r, int& adj) {
    int depth = 1;
    while (!r.atEnd() && depth > 0) {
        const auto tok = r.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            ++depth;
            if (localName(r) == QLatin1String("gd")) {
                const auto a = r.attributes();
                if (a.value(QLatin1String("name")).startsWith(QLatin1String("adj"))) {
                    const QString f = a.value(QLatin1String("fmla")).toString();
                    if (f.startsWith(QLatin1String("val "))) {
                        bool ok = false;
                        const int v = QStringView(f).mid(4).toInt(&ok);
                        if (ok) adj = v;
                    }
                }
            }
        } else if (tok == QXmlStreamReader::EndElement) {
            --depth;
        }
    }
}

// A freeform outline. The path declares the coordinate space it is drawn in
// (w/h); normalising to 0..1 here means the renderer never has to know.
void readCustomGeometry(QXmlStreamReader& r, SheetShape& out) {
    QPainterPath path;
    double sx = 0, sy = 0;                 // scale from path space to 0..1
    QPointF cur;
    QVector<QPointF> pts;                  // points collected for the current verb
    QString verb;

    auto toUnit = [&](qint64 x, qint64 y) {
        return QPointF(sx > 0 ? double(x) * sx : 0.0, sy > 0 ? double(y) * sy : 0.0);
    };
    auto finishVerb = [&] {
        if (verb == QLatin1String("moveTo") && pts.size() >= 1) {
            cur = pts.first();  path.moveTo(cur);
        } else if (verb == QLatin1String("lnTo") && pts.size() >= 1) {
            cur = pts.first();  path.lineTo(cur);
        } else if (verb == QLatin1String("cubicBezTo") && pts.size() >= 3) {
            path.cubicTo(pts.at(0), pts.at(1), pts.at(2));  cur = pts.at(2);
        } else if (verb == QLatin1String("quadBezTo") && pts.size() >= 2) {
            path.quadTo(pts.at(0), pts.at(1));              cur = pts.at(1);
        }
        pts.clear();
        verb.clear();
    };

    int depth = 1;
    while (!r.atEnd() && depth > 0) {
        const auto tok = r.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            ++depth;
            const QString n = localName(r);
            const auto    a = r.attributes();

            if (n == QLatin1String("path")) {
                const double w = a.value(QLatin1String("w")).toDouble();
                const double h = a.value(QLatin1String("h")).toDouble();
                sx = w > 0 ? 1.0 / w : 0.0;
                sy = h > 0 ? 1.0 / h : 0.0;
            } else if (n == QLatin1String("moveTo") || n == QLatin1String("lnTo")
                       || n == QLatin1String("cubicBezTo") || n == QLatin1String("quadBezTo")) {
                finishVerb();
                verb = n;
            } else if (n == QLatin1String("pt")) {
                pts.push_back(toUnit(a.value(QLatin1String("x")).toLongLong(),
                                     a.value(QLatin1String("y")).toLongLong()));
            } else if (n == QLatin1String("arcTo")) {
                finishVerb();
                // Office measures the angles clockwise in 60000ths of a degree
                // with y growing downward; Qt measures counter-clockwise, hence
                // the sign flips. The arc starts at the current point, so its
                // centre is that point stepped back along the start angle.
                const double wR = a.value(QLatin1String("wR")).toDouble() * (sx > 0 ? sx : 0.0);
                const double hR = a.value(QLatin1String("hR")).toDouble() * (sy > 0 ? sy : 0.0);
                const double st = a.value(QLatin1String("stAng")).toDouble() / 60000.0;
                const double sw = a.value(QLatin1String("swAng")).toDouble() / 60000.0;
                if (wR > 0 && hR > 0) {
                    const QPointF centre(cur.x() - wR * std::cos(qDegreesToRadians(st)),
                                         cur.y() - hR * std::sin(qDegreesToRadians(st)));
                    const QRectF box(centre.x() - wR, centre.y() - hR, 2 * wR, 2 * hR);
                    if (path.elementCount() == 0) path.moveTo(cur);
                    path.arcTo(box, -st, -sw);
                    cur = path.currentPosition();
                }
            } else if (n == QLatin1String("close")) {
                finishVerb();
                path.closeSubpath();
            }
        } else if (tok == QXmlStreamReader::EndElement) {
            const QString n = localName(r);
            if (n == QLatin1String("moveTo") || n == QLatin1String("lnTo")
                || n == QLatin1String("cubicBezTo") || n == QLatin1String("quadBezTo"))
                finishVerb();
            --depth;
        }
    }
    finishVerb();

    if (path.elementCount() > 1) {
        out.preset      = ShapeGeom::Custom;
        out.customPath = path;
    }
}

void readGradient(QXmlStreamReader& r, const ThemeColors& theme, SheetShape& out) {
    int depth = 1;
    int pos = -1;
    while (!r.atEnd() && depth > 0) {
        const auto tok = r.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            const QString n = localName(r);
            if (isColorElement(n) && pos >= 0) {
                const QColor c = readColorElement(r, theme);
                if (c.isValid()) { out.gradient << c; out.gradientPos << pos / 100000.0; }
                pos = -1;
                continue;
            }
            ++depth;
            if (n == QLatin1String("gs"))
                pos = r.attributes().value(QLatin1String("pos")).toInt();
            else if (n == QLatin1String("lin")) {
                // ang counts 60000ths of a degree clockwise from the +x axis.
                const double deg = r.attributes().value(QLatin1String("ang")).toDouble() / 60000.0;
                out.gradientAngle = int(deg) % 360;
            }
        } else if (tok == QXmlStreamReader::EndElement) {
            --depth;
        }
    }
}

void readLine(QXmlStreamReader& r, const ThemeColors& theme, SheetShape& out) {
    const qint64 w = r.attributes().value(QLatin1String("w")).toLongLong();
    const double px = w > 0 ? double(w) / 9525.0 : 1.0;

    int    depth = 1;
    bool   none  = false;
    QColor c;
    while (!r.atEnd() && depth > 0) {
        const auto tok = r.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            const QString n = localName(r);
            if (n == QLatin1String("solidFill")) { c = readFirstColor(r, theme); continue; }
            ++depth;
            if (n == QLatin1String("noFill")) none = true;
        } else if (tok == QXmlStreamReader::EndElement) {
            --depth;
        }
    }
    if (!none && c.isValid()) { out.line = c; out.lineWidth = qMax(1.0, px); }
}

// A run's properties live partly in attributes (already read) and partly in
// children: the typeface and the text colour.
void readRunProps(QXmlStreamReader& r, const ThemeColors& theme, ShapeRun& run) {
    int depth = 1;
    while (!r.atEnd() && depth > 0) {
        const auto tok = r.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            const QString n = localName(r);
            if (n == QLatin1String("solidFill")) {
                const QColor c = readFirstColor(r, theme);
                if (c.isValid()) run.color = c;
                continue;
            }
            ++depth;
            if (n == QLatin1String("latin")) {
                const QString tf = r.attributes().value(QLatin1String("typeface")).toString();
                // "+mn-lt" and "+mj-lt" point back at the theme's font scheme;
                // leaving the family empty lets the app's default stand in.
                if (!tf.isEmpty() && !tf.startsWith(QLatin1Char('+'))) run.family = tf;
            }
        } else if (tok == QXmlStreamReader::EndElement) {
            --depth;
        }
    }
}

ShapeGeom shapeGeomFor(const QString& prst) {
    static const QHash<QString, ShapeGeom> kMap = {
        { QStringLiteral("rect"),                   ShapeGeom::Rect },
        { QStringLiteral("flowChartProcess"),       ShapeGeom::Rect },
        { QStringLiteral("snip1Rect"),              ShapeGeom::Rect },
        { QStringLiteral("snip2SameRect"),          ShapeGeom::Rect },
        { QStringLiteral("plaque"),                 ShapeGeom::RoundRect },
        { QStringLiteral("roundRect"),              ShapeGeom::RoundRect },
        { QStringLiteral("round1Rect"),             ShapeGeom::RoundRect },
        { QStringLiteral("round2SameRect"),         ShapeGeom::RoundRect },
        { QStringLiteral("round2DiagRect"),         ShapeGeom::RoundRect },
        { QStringLiteral("ellipse"),                ShapeGeom::Ellipse },
        { QStringLiteral("flowChartConnector"),     ShapeGeom::Ellipse },
        { QStringLiteral("triangle"),               ShapeGeom::Triangle },
        { QStringLiteral("rtTriangle"),             ShapeGeom::RightTriangle },
        { QStringLiteral("diamond"),                ShapeGeom::Diamond },
        { QStringLiteral("line"),                   ShapeGeom::Line },
        { QStringLiteral("straightConnector1"),     ShapeGeom::Line },
        { QStringLiteral("bentConnector3"),         ShapeGeom::Line },
        { QStringLiteral("rightArrow"),             ShapeGeom::RightArrow },
        { QStringLiteral("leftArrow"),              ShapeGeom::LeftArrow },
        { QStringLiteral("upArrow"),                ShapeGeom::UpArrow },
        { QStringLiteral("downArrow"),              ShapeGeom::DownArrow },
        { QStringLiteral("chevron"),                ShapeGeom::Chevron },
        { QStringLiteral("homePlate"),              ShapeGeom::Pentagon },
        { QStringLiteral("pentagon"),               ShapeGeom::Pentagon },
        { QStringLiteral("wedgeRectCallout"),       ShapeGeom::Callout },
        { QStringLiteral("wedgeRoundRectCallout"),  ShapeGeom::Callout },
        { QStringLiteral("wedgeEllipseCallout"),    ShapeGeom::Callout },
    };
    return kMap.value(prst, ShapeGeom::Rect);
}

// ── One shape ───────────────────────────────────────────────────────────────
// Consumes the sp / cxnSp element the reader sits on.
void parseShapeElement(QXmlStreamReader& r, const Mapper& m, const DrawCtx& ctx,
                       SheetShape& out) {
    const ThemeColors& theme = ctx.theme;
    int         depth = 1;
    QStringList path;                       // element names below the shape
    qint64      x = 0, y = 0, cx = 0, cy = 0;
    bool        haveXfrm = false;

    ShapeParagraph para;
    ShapeRun       run;

    auto flushRun = [&] {
        if (!run.text.isEmpty()) para.runs.push_back(run);
        run = ShapeRun{};
    };
    auto flushPara = [&] {
        flushRun();
        if (!para.runs.isEmpty()) out.text.push_back(para);
        para = ShapeParagraph{};
    };

    while (!r.atEnd() && depth > 0) {
        const auto tok = r.readNext();

        if (tok == QXmlStreamReader::StartElement) {
            const QString n      = localName(r);
            const QString parent = path.isEmpty() ? QString() : path.constLast();

            // The shape's own properties. Scoping these to spPr matters: `ln`
            // and `solidFill` also appear under txBody, where they mean the
            // text's colour rather than the shape's.
            if (parent == QLatin1String("spPr")) {
                if (n == QLatin1String("solidFill")) { out.fill = readFirstColor(r, theme); continue; }
                if (n == QLatin1String("gradFill"))  { readGradient(r, theme, out);         continue; }
                if (n == QLatin1String("blipFill")) {
                    QString rid;
                    int d = 1;
                    while (!r.atEnd() && d > 0) {
                        const auto t2 = r.readNext();
                        if (t2 == QXmlStreamReader::StartElement) {
                            ++d;
                            if (localName(r) == QLatin1String("blip"))
                                for (const auto& at : r.attributes())
                                    if (at.name() == QLatin1String("embed")) rid = at.value().toString();
                        } else if (t2 == QXmlStreamReader::EndElement) {
                            --d;
                        }
                    }
                    if (!rid.isEmpty()) {
                        const QString part = ctx.rels.value(rid);
                        if (!part.isEmpty()) out.fillImage = ctx.fetch(part);
                    }
                    continue;
                }
                if (n == QLatin1String("ln"))        { readLine(r, theme, out);             continue; }
                if (n == QLatin1String("xfrm")) {
                    const auto a = r.attributes();
                    out.rotation = int(a.value(QLatin1String("rot")).toDouble() / 60000.0);
                    out.flipH    = a.value(QLatin1String("flipH")) == QLatin1String("1");
                    out.flipV    = a.value(QLatin1String("flipV")) == QLatin1String("1");
                    readOffExt(r, x, y, cx, cy);
                    haveXfrm = true;
                    continue;
                }
                if (n == QLatin1String("prstGeom")) {
                    out.preset = shapeGeomFor(r.attributes().value(QLatin1String("prst")).toString());
                    readAdjust(r, out.adjustPct);
                    continue;
                }
                if (n == QLatin1String("custGeom")) {
                    // Falls back to the box only when the path list turns out
                    // to be empty or unreadable.
                    out.preset = ShapeGeom::Rect;
                    readCustomGeometry(r, out);
                    continue;
                }
            }

            // A list style is a table of defaults for levels the shape does not
            // use. Reading it would overwrite the run being built with the
            // defaults of level nine.
            if (n == QLatin1String("lstStyle") || n == QLatin1String("endParaRPr")
                || n == QLatin1String("extLst")) {
                skipSubtree(r);
                continue;
            }

            if (n == QLatin1String("bodyPr")) {
                const auto a = r.attributes().value(QLatin1String("anchor"));
                out.vAlign = a == QLatin1String("t") ? Qt::AlignTop
                           : a == QLatin1String("b") ? Qt::AlignBottom
                                                     : Qt::AlignVCenter;
            } else if (n == QLatin1String("p") && inside(path, "txBody")) {
                flushPara();
            } else if (n == QLatin1String("pPr")) {
                const auto a = r.attributes().value(QLatin1String("algn"));
                para.align = a == QLatin1String("ctr")  ? Qt::AlignHCenter
                           : a == QLatin1String("r")    ? Qt::AlignRight
                           : a == QLatin1String("just") ? Qt::AlignJustify
                                                        : Qt::AlignLeft;
            } else if ((n == QLatin1String("r") || n == QLatin1String("fld"))
                       && inside(path, "txBody")) {
                flushRun();
            } else if (n == QLatin1String("rPr") && inside(path, "txBody")) {
                const auto a  = r.attributes();
                const int  sz = a.value(QLatin1String("sz")).toInt();
                if (sz > 0) run.size = sz / 100.0;
                run.bold   = a.value(QLatin1String("b")) == QLatin1String("1");
                run.italic = a.value(QLatin1String("i")) == QLatin1String("1");
                const auto u = a.value(QLatin1String("u"));
                run.underline = !u.isEmpty() && u != QLatin1String("none");
                readRunProps(r, theme, run);
                continue;
            } else if (n == QLatin1String("t") && inside(path, "txBody")) {
                run.text += r.readElementText(QXmlStreamReader::IncludeChildElements);
                continue;
            } else if (n == QLatin1String("br") && inside(path, "txBody")) {
                run.text += QLatin1Char('\n');
            }

            ++depth;
            path.push_back(n);

        } else if (tok == QXmlStreamReader::EndElement) {
            const QString n = localName(r);
            if      (n == QLatin1String("p"))                              flushPara();
            else if (n == QLatin1String("r") || n == QLatin1String("fld")) flushRun();
            --depth;
            if (!path.isEmpty()) path.removeLast();
        }
    }
    flushPara();

    out.frac = haveXfrm ? m.map(x, y, cx, cy) : m.box;
}

void parsePicture(QXmlStreamReader& r, const Mapper& m, DrawCtx& ctx) {
    int     depth = 1;
    QString rid;
    qint64  x = 0, y = 0, cx = 0, cy = 0;
    bool    haveXfrm = false;

    while (!r.atEnd() && depth > 0) {
        const auto tok = r.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            const QString n = localName(r);
            if (n == QLatin1String("xfrm")) {
                readOffExt(r, x, y, cx, cy);
                haveXfrm = true;
                continue;
            }
            ++depth;
            if (n == QLatin1String("blip")) {
                for (const auto& a : r.attributes())
                    if (a.name() == QLatin1String("embed") || a.name() == QLatin1String("link"))
                        rid = a.value().toString();
            }
        } else if (tok == QXmlStreamReader::EndElement) {
            --depth;
        }
    }
    if (rid.isEmpty()) return;

    const QString part = ctx.rels.value(rid);
    if (part.isEmpty()) return;
    const QByteArray bytes = ctx.fetch(part);
    if (bytes.isEmpty()) return;

    SheetImage img;
    img.anchor = ctx.anchor;
    img.geom   = ctx.objectGeom();
    img.frac   = haveXfrm ? m.map(x, y, cx, cy) : m.box;
    img.data   = bytes;
    img.fromFile = true;
    ctx.images.push_back(img);
}

void parseGraphicFrame(QXmlStreamReader& r, const Mapper& m, DrawCtx& ctx) {
    int     depth = 1;
    QString rid;
    qint64  x = 0, y = 0, cx = 0, cy = 0;
    bool    haveXfrm = false;

    while (!r.atEnd() && depth > 0) {
        const auto tok = r.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            const QString n = localName(r);
            if (n == QLatin1String("xfrm")) {
                readOffExt(r, x, y, cx, cy);
                haveXfrm = true;
                continue;
            }
            ++depth;
            if (n == QLatin1String("chart")) {
                for (const auto& a : r.attributes())
                    if (a.name() == QLatin1String("id")) rid = a.value().toString();
            }
        } else if (tok == QXmlStreamReader::EndElement) {
            --depth;
        }
    }
    if (rid.isEmpty()) return;   // a diagram or an embedded object, not a chart

    const QString part = ctx.rels.value(rid);
    if (part.isEmpty()) return;
    const QByteArray bytes = ctx.fetch(part);
    ChartSpec spec;
    if (bytes.isEmpty() || !parseChartPart(bytes, spec, ctx.theme)) return;

    spec.anchor = ctx.anchor;
    spec.geom   = ctx.objectGeom();
    spec.frac   = haveXfrm ? m.map(x, y, cx, cy) : m.box;
    spec.fromFile = true;
    ctx.charts.push_back(spec);
}

// An anchor or a group: both hold objects, and a group also declares the
// coordinate space those objects are drawn in.
void parseContainer(QXmlStreamReader& r, const Mapper& parent, DrawCtx& ctx) {
    Mapper child = parent;
    int    depth = 1;
    bool   inFrom = false, inTo = false;

    while (!r.atEnd() && depth > 0) {
        const auto tok = r.readNext();

        if (tok == QXmlStreamReader::StartElement) {
            const QString n = localName(r);

            if (n == QLatin1String("grpSpPr")) {
                qint64 x = 0, y = 0, cx = 0, cy = 0, chX = 0, chY = 0, chCx = 0, chCy = 0;
                readOffExt(r, x, y, cx, cy, &chX, &chY, &chCx, &chCy);
                child.box = parent.map(x, y, cx, cy);
                child.ox  = chX; child.oy = chY;
                // A group with no declared child space draws its members in its
                // own, so fall back to that rather than to no mapping at all.
                child.cx = chCx > 0 ? chCx : cx;
                child.cy = chCy > 0 ? chCy : cy;
                continue;
            }
            if (n == QLatin1String("grpSp"))        { parseContainer(r, child, ctx);     continue; }
            if (n == QLatin1String("pic"))          { parsePicture(r, child, ctx);       continue; }
            if (n == QLatin1String("graphicFrame")) { parseGraphicFrame(r, child, ctx);  continue; }
            if (n == QLatin1String("sp") || n == QLatin1String("cxnSp")) {
                SheetShape sh;
                parseShapeElement(r, child, ctx, sh);
                if (sh.isVisible()) {
                    sh.anchor = ctx.anchor;
                    sh.geom   = ctx.objectGeom();
                    ctx.shapes.push_back(sh);
                }
                continue;
            }

            // Anchor placement. These only occur at the anchor level, so a
            // group never has to be told to ignore them.
            if      (n == QLatin1String("from")) { inFrom = true;  inTo = false; }
            else if (n == QLatin1String("to"))   { inTo   = true;  inFrom = false; }
            else if (n == QLatin1String("col") || n == QLatin1String("colOff")
                     || n == QLatin1String("row") || n == QLatin1String("rowOff")) {
                const qint64 v =
                    r.readElementText(QXmlStreamReader::IncludeChildElements).toLongLong();
                CellAnchor& a = ctx.anchor;
                if (inFrom) {
                    if      (n == QLatin1String("col"))    a.fromCol = int(v);
                    else if (n == QLatin1String("row"))    a.fromRow = int(v);
                    else if (n == QLatin1String("colOff")) a.fromDx  = emuToPx(v);
                    else                                   a.fromDy  = emuToPx(v);
                } else if (inTo) {
                    if      (n == QLatin1String("col"))    a.toCol = int(v);
                    else if (n == QLatin1String("row"))    a.toRow = int(v);
                    else if (n == QLatin1String("colOff")) a.toDx  = emuToPx(v);
                    else                                   a.toDy  = emuToPx(v);
                }
                continue;   // readElementText consumed the end tag
            }
            else if (n == QLatin1String("ext")) {
                const auto a = r.attributes();
                if (a.hasAttribute(QLatin1String("cx"))) ctx.extCx = a.value(QLatin1String("cx")).toLongLong();
                if (a.hasAttribute(QLatin1String("cy"))) ctx.extCy = a.value(QLatin1String("cy")).toLongLong();
            }
            else if (n == QLatin1String("pos")) {
                const auto a = r.attributes();
                ctx.posX = a.value(QLatin1String("x")).toLongLong();
                ctx.posY = a.value(QLatin1String("y")).toLongLong();
            }

            ++depth;

        } else if (tok == QXmlStreamReader::EndElement) {
            const QString n = localName(r);
            if      (n == QLatin1String("from")) inFrom = false;
            else if (n == QLatin1String("to"))   inTo   = false;
            --depth;
        }
    }
}

} // namespace

void parseSheetDrawing(const QByteArray& drawingXml,
                       const QHash<QString, QString>& rels,
                       const std::function<QByteArray(const QString&)>& fetch,
                       std::vector<ChartSpec>& outCharts,
                       std::vector<SheetImage>& outImages,
                       std::vector<SheetShape>& outShapes,
                       const ThemeColors& theme) {
    QXmlStreamReader r(drawingXml);
    DrawCtx ctx { rels, fetch, theme, outCharts, outImages, outShapes, {}, 0, 0, 0, 0 };

    while (!r.atEnd()) {
        if (r.readNext() != QXmlStreamReader::StartElement) continue;
        const QString n = localName(r);
        if (n == QLatin1String("twoCellAnchor") || n == QLatin1String("oneCellAnchor")
            || n == QLatin1String("absoluteAnchor")) {
            ctx.anchor = CellAnchor{};
            ctx.extCx = ctx.extCy = ctx.posX = ctx.posY = 0;
            parseContainer(r, Mapper{}, ctx);
        }
    }
}

} // namespace NativeOffice
