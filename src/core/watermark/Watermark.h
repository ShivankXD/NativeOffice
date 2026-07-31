#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Watermark.h — the "Made with NativeOffice" export watermark.
//
// One definition of the mark, used by every export path so all four formats
// produce the same artwork:
//   • Writer  (.docx footer, PDF export)
//   • Calc    (.xlsx footer + linked drawing, PDF export)
//   • Impress (.pptx per-slide picture, PDF export)
//   • PDF     (page stamp on save/save-as and on toolkit output)
//
// Policy lives in enabledForExport(): free accounts always get the mark, and
// premium accounts get it only if they deliberately switch it back on. This is
// the ONLY place that decision is made — export paths must never re-derive it,
// so there is no route to a watermark-free export for a free account.
//
// The mark is applied on export/save only. Opening a document never stamps it,
// so a file that already carries the mark from a previous save does not gain a
// second one; the stamp goes on the bytes being written, not the model.
//
// Geometry is quoted in PostScript points (1/72") because that is the unit
// PDF and OOXML both reduce to. paint() takes a scale so the same code draws
// at 300 dpi into a QPdfWriter and at whatever density an embedded PNG needs.
// ─────────────────────────────────────────────────────────────────────────────

#include <QRectF>
#include <QSizeF>
#include <QString>

class QImage;
class QPainter;

namespace NativeOffice {
namespace Watermark {

// QSettings key for the premium "Show watermark on exports" toggle. Only
// consulted when the account is premium; a free account ignores it entirely.
inline constexpr char kSettingsKey[] = "premium/watermarkOnExport";

// Where the embedded hyperlink points.
QString targetUrl();

// True when this export must carry the mark. Free tier: always. Premium: only
// when the user has switched the toggle back on.
bool enabledForExport();

// Natural size of the mark in points, at scale 1.
QSizeF sizePoints();

// Distance from the page edge to the mark, in points.
qreal marginPoints();

// Draws the mark with its bottom-right corner inset from pageRect's
// bottom-right by marginPoints(). `scale` converts points to the painter's
// device units (e.g. 300/72 for a 300 dpi QPdfWriter).
//
// Returns the exact rectangle painted, in the painter's device coordinates.
// Callers use it to size a link hotspot that matches the artwork and nothing
// more, so a click just outside the mark does not follow the link.
QRectF paint(QPainter& p, const QRectF& pageRect, qreal scale);

// Same artwork rendered standalone onto a transparent background, for formats
// that embed the mark as a picture (OOXML, and the PDFium stamp path).
// `pxPerPoint` sets the raster density; 4.0 is ~288 dpi.
QImage renderImage(qreal pxPerPoint = 4.0);

} // namespace Watermark
} // namespace NativeOffice
