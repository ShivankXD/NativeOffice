#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfAnnots.h — real PDF annotations (/Annots) with appearance streams, so
// everything drawn here renders in Adobe/Chrome/any viewer, not just ours:
//
//   Text markup: Highlight (text-snapped or area), Underline, StrikeOut
//   Shapes:      Square, Circle, Line, Arrow, Ink (freehand)
//   Text:        Note (sticky), FreeText / Text Box, Callout, plain AddText
//   Misc:        Stamp (text badge), Link (URI), FileAttachment (embedded
//                file), Caret (insert/replace-text marks), WipeOff (opaque
//                white cover), Picture (image stamp)
//
// Also: listing/removal for the comments pane, XFDF import/export, and
// outline-bookmark insertion. All coordinates in the specs use page points
// with a TOP-LEFT origin (the viewer's space); conversion to PDF's
// bottom-left space happens inside.
// ─────────────────────────────────────────────────────────────────────────────

#include "PdfOps.h"

#include <QColor>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <vector>

namespace NativeOffice::Pdf {

struct AnnotSpec {
    enum class Kind {
        Highlight, Underline, StrikeOut,
        Square, Circle, Line, Arrow, Ink,
        Note, FreeText, Callout, PlainText,
        Stamp, Link, FileAttachment, Caret, WipeOff, Picture,
    };

    Kind    kind = Kind::Square;
    int     pageIndex = 0;
    QRectF  rect;                    // bounding rect (top-left origin, page pt)
    std::vector<QRectF> quads;       // markup kinds: per-line rects
    QPolygonF ink;                   // Ink: stroke points
    QString contents;                // note/freetext/stamp text, link tooltip
    QString author;
    QColor  color   = QColor("#E8372A");
    double  opacity = 1.0;
    double  borderWidth = 1.5;
    double  fontSizePt  = 12;        // FreeText family
    QString url;                     // Link
    QString filePath;                // FileAttachment / Picture
};

// Adds all `specs` in one rebuild (one undo step). Every annotation gets an
// appearance stream.
OpResult addAnnotations(const QString& in, const QString& out,
                        const std::vector<AnnotSpec>& specs);

inline OpResult addAnnotation(const QString& in, const QString& out, const AnnotSpec& spec) {
    return addAnnotations(in, out, { spec });
}

// One row of the comments pane. `indexOnPage` counts non-widget, non-popup
// annotations on that page — the same index removeAnnotation() consumes.
struct AnnotInfo {
    int     pageIndex = 0;
    int     indexOnPage = 0;
    QString subtype;                 // "Highlight", "FreeText", …
    QString contents;
    QString author;
    QString modified;                // raw /M value, prettified by the UI
    QRectF  rect;                    // top-left origin
};

std::vector<AnnotInfo> listAnnotations(const QString& path);

OpResult removeAnnotation(const QString& in, const QString& out,
                          int pageIndex, int indexOnPage);
OpResult removeAllAnnotations(const QString& in, const QString& out);

// XFDF (XML Forms Data Format) comment interchange — what WPS/Adobe's
// Export/Import Comments produce/consume.
OpResult exportXfdf(const QString& pdfPath, const QString& xfdfOut);
OpResult importXfdf(const QString& in, const QString& xfdfPath, const QString& out);

// Rebuilds the outline with an extra bookmark appended at top level.
// (Existing entries survive as title+destination; nesting is preserved.)
OpResult addOutlineBookmark(const QString& in, const QString& out,
                            const QString& title, int pageIndex);

} // namespace NativeOffice::Pdf
