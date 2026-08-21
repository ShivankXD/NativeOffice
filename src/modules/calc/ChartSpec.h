#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ChartSpec.h  (Sprint 26 — Charts)
// Lightweight, widget-free description of the objects that sit on a sheet, so
// the model can store them without depending on Qt Widgets / Qt Charts.
//
// Two ways of describing a chart live here side by side:
//
//   * range:  one contiguous block of cells, scanned for a header row and a
//              category column. This is what a chart built inside NativeOffice
//              uses, and it stays the simplest thing that works.
//   * series: an explicit list of series, each with its own value range. Real
//              Excel charts are built this way: the categories come from one
//              range and every series from another, and those ranges are not
//              adjacent (a chart often plots row 7 and row 12 of a table and
//              nothing in between). A contiguous rectangle cannot express that,
//              so imported charts fill in `series` and leave `range` empty.
//
// When `series` is non-empty it wins; otherwise the chart falls back to `range`.
// ─────────────────────────────────────────────────────────────────────────────
#include <QByteArray>
#include <QColor>
#include <QPainterPath>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>

namespace NativeOffice {

enum class ChartType { Column = 0, Bar, Line, Area, Pie, Scatter, Doughnut };

// ─────────────────────────────────────────────────────────────────────────────
// Where an object sits, expressed the way a spreadsheet thinks about it: two
// corners, each one a cell plus an offset inside that cell. This is OOXML's
// twoCellAnchor, and it is what makes an object span a block of cells and keep
// spanning it when rows are resized, columns are widened or the sheet is
// zoomed. Pixel geometry cannot do that.
// ─────────────────────────────────────────────────────────────────────────────
struct CellAnchor {
    int fromCol { -1 }, fromRow { -1 };
    int fromDx  { 0 },  fromDy  { 0 };   // offset inside the from cell, pixels
    int toCol   { -1 }, toRow   { -1 };
    int toDx    { 0 },  toDy    { 0 };   // offset inside the to cell, pixels

    [[nodiscard]] bool hasFrom() const { return fromCol >= 0 && fromRow >= 0; }
    // A two-corner anchor resizes with the cells it spans. A one-corner anchor
    // keeps the object's own size and only tracks its top-left cell.
    [[nodiscard]] bool hasTo()   const { return toCol   >= 0 && toRow   >= 0; }
};

// One plotted series. `cache` holds the values Excel last wrote into the file:
// it is the fallback for a reference this workbook cannot resolve (a range on
// a sheet that failed to import, or a cross-workbook link).
struct ChartSeries {
    QString         name;
    QRect           valRange;    // cells on `sheet`, empty when unresolved
    QString         sheet;       // sheet the reference points at
    // Some producers give the series name as a reference to a header cell
    // rather than as literal text, and write no cached copy of it. The name
    // then has to be read out of the sheet once the workbook is loaded.
    QRect           nameRange;
    QString         nameSheet;
    QVector<double> cache;
    QColor          color;       // explicit series colour from the file
    // Pie and doughnut charts colour each slice separately, so the file gives
    // per-point colours (c:dPt) rather than one colour for the series.
    QVector<QColor> pointColors;
};

// Serializable description of a chart (what + where).
struct ChartSpec {
    ChartType type { ChartType::Column };
    QRect     range;     // contiguous data range in cells, for in-app charts
    QRect     geom;      // widget geometry in viewport pixels (pre-anchor files)

    CellAnchor          anchor;      // preferred placement when it has a from
    // A chart is usually the whole anchored object, but it can be one member of
    // a group (a chart with a caption box over it, say), so it carries the same
    // sub-rect every other drawn object does. See SheetShape::frac.
    QRectF              frac { 0, 0, 1, 1 };
    QString             title;
    QStringList         categories;  // cached category labels
    QRect               catRange;    // cells the categories come from
    QString             catSheet;
    QVector<ChartSeries> series;     // when non-empty, plotted instead of `range`

    // How the file wants the chart presented. These were previously guessed,
    // which is why every chart got a bottom legend, an invented title and
    // labels on every pie slice.
    bool    hasTitle    { false };   // false => draw no title at all
    bool    showLegend  { true };
    char    legendPos   { 'b' };     // r / l / t / b
    bool    showDataLabels { false };
    QString valueAxisFormat;         // number-format code for the value axis

    // True when this chart was read out of an .xlsx. It decides whether a
    // save can keep it: the exporter cannot write a chart part, so the only
    // chart that survives is one whose part is already in the package being
    // put back. A chart built in the app has no part to put back.
    bool    fromFile { false };

    [[nodiscard]] bool isExplicit() const { return !series.isEmpty(); }
};

// A picture dropped on a sheet. The bytes are kept with the sheet so the image
// survives a save/reload without depending on the original file still existing.
struct SheetImage {
    CellAnchor anchor;
    QRect      geom;     // fallback geometry when the anchor has no `to`
    QByteArray data;     // encoded image (png/jpeg/gif as it came out of the file)
    QRectF     frac { 0, 0, 1, 1 };   // sub-rect of the anchor (see SheetShape)
    bool       fromFile { false };    // read out of an .xlsx (see ChartSpec)
};

// ─────────────────────────────────────────────────────────────────────────────
// Drawn shapes: the banners, buttons, callouts and rules that sit on a sheet
// alongside its charts and pictures. A template workbook is mostly shapes, so
// skipping them left the sheet looking stripped even when every chart and
// picture had been imported correctly.
// ─────────────────────────────────────────────────────────────────────────────
enum class ShapeGeom {
    Rect = 0, RoundRect, Ellipse, Triangle, RightTriangle, Diamond,
    Line, RightArrow, LeftArrow, UpArrow, DownArrow, Chevron, Pentagon,
    Callout,          // wedge callouts: the body, with a tail toward the target
    Custom,           // a freeform outline, carried in SheetShape::customPath
};

// One stretch of text with its own formatting. A shape's caption is not
// uniformly styled: the corpus has buttons whose label is one run in Bahnschrift
// and whose icon is a second run in Webdings, and collapsing the two would turn
// the icon into a stray letter.
struct ShapeRun {
    QString text;
    QString family;
    qreal   size   { 11.0 };   // points
    bool    bold   { false };
    bool    italic { false };
    bool    underline { false };
    QColor  color;
};

struct ShapeParagraph {
    QVector<ShapeRun> runs;
    Qt::Alignment     align { Qt::AlignLeft };
};

struct SheetShape {
    CellAnchor anchor;                 // placement of the anchored object
    QRect      geom;                   // fallback pixel geometry

    // Where this shape sits INSIDE the anchored object, as a fraction of it.
    // A shape anchored on its own is the whole object, so the default covers
    // everything. Grouped shapes carry their share: a group maps its children
    // through chOff/chExt onto its own extent, and expressing the result as a
    // fraction means the group keeps its internal layout at any cell size or
    // zoom without the renderer having to know a group exists.
    QRectF frac { 0, 0, 1, 1 };

    ShapeGeom preset { ShapeGeom::Rect };
    int       adjustPct { 16667 };     // roundRect corner, in 1/100000 of the short side

    // A freeform's outline, in 0..1 coordinates so it scales to whatever size
    // the anchor gives the shape. Only meaningful when preset is Custom.
    QPainterPath customPath;

    QColor          fill;              // invalid => not filled
    QVector<QColor> gradient;          // >= 2 stops => gradient fill, `fill` unused
    QVector<qreal>  gradientPos;       // 0..1, parallel to `gradient`
    int             gradientAngle { 90 };   // degrees clockwise from +x

    // A shape can be filled with a picture instead of a colour. The bytes live
    // here for the same reason SheetImage carries its own: the sheet has to
    // survive without the file it came from.
    QByteArray fillImage;

    QColor line;                       // invalid => no outline
    qreal  lineWidth { 1.0 };

    QVector<ShapeParagraph> text;
    Qt::Alignment vAlign { Qt::AlignVCenter };
    bool   flipH { false }, flipV { false };
    int    rotation { 0 };             // degrees clockwise

    [[nodiscard]] bool isVisible() const {
        return fill.isValid() || !gradient.isEmpty() || line.isValid()
               || !text.isEmpty() || !fillImage.isEmpty();
    }
};

QString   chartTypeName(ChartType t);
ChartType chartTypeFromInt(int v);

} // namespace NativeOffice
