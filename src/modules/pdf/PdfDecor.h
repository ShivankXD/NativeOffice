#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfDecor.h — page decorations: Watermark (text/image, plain or tiled),
// Background color, Page Numbers, Header & Footer.
//
// All of these are content-stream appends (or prepends, for underlays) using
// the base-14 Helvetica font — no font embedding, no new dependencies. Every
// stream this module adds carries a private /NativeOfficeDecor tag naming its
// kind, so decorations can be found again and removed/updated cleanly by
// removeDecor() without touching the original page content.
// ─────────────────────────────────────────────────────────────────────────────

#include "PdfOps.h"

#include <QColor>
#include <QRectF>
#include <QString>
#include <map>
#include <vector>

namespace NativeOffice::Pdf {

// Decoration kinds (the /NativeOfficeDecor tag value).
enum class DecorKind { Watermark, Background, PageNumber, HeaderFooter };

struct TextWatermarkSpec {
    QString text        = QStringLiteral("CONFIDENTIAL");
    double  fontSizePt  = 52;
    QColor  color       = QColor(128, 128, 128);
    double  opacity     = 0.35;     // 0..1
    double  rotationDeg = 45;       // counter-clockwise, WPS-style diagonal
    bool    tiled       = false;    // repeat across the page
};

struct ImageWatermarkSpec {
    QString imagePath;
    double  opacity     = 0.35;
    double  rotationDeg = 0;
    double  scalePct    = 50;       // relative to page width
    bool    tiled       = false;
};

struct PageNumberSpec {
    enum class Position { BottomCenter, BottomLeft, BottomRight, TopCenter, TopLeft, TopRight };
    enum class Format   { Plain, PageOfTotal, DashNDash };   // "3" | "Page 3 of 12" | "- 3 -"
    Position position   = Position::BottomCenter;
    Format   format     = Format::Plain;
    int      startAt    = 1;        // number the first page gets
    double   fontSizePt = 10;
    QColor   color      = QColor(60, 60, 60);
};

struct HeaderFooterSpec {
    // Macros &[Page], &[Pages] and &[Date] are expanded per page.
    QString headerLeft, headerCenter, headerRight;
    QString footerLeft, footerCenter, footerRight;
    double  fontSizePt = 9;
    QColor  color      = QColor(60, 60, 60);
    double  marginPt   = 28;        // distance from the page edge
};

// Empty `pages` = all pages (0-based indices otherwise).
OpResult addTextWatermark(const QString& in, const QString& out,
                          const TextWatermarkSpec& spec, const std::vector<int>& pages = {});
OpResult addImageWatermark(const QString& in, const QString& out,
                           const ImageWatermarkSpec& spec, const std::vector<int>& pages = {});
OpResult setBackground(const QString& in, const QString& out,
                       const QColor& color, const std::vector<int>& pages = {});
OpResult addPageNumbers(const QString& in, const QString& out, const PageNumberSpec& spec);
OpResult addHeaderFooter(const QString& in, const QString& out, const HeaderFooterSpec& spec);

// Removes every decoration stream of `kind` from all pages.
OpResult removeDecor(const QString& in, const QString& out, DecorKind kind);

// True if any page carries a decoration of `kind` (drives Update/Delete UI).
bool hasDecor(const QString& path, DecorKind kind);

// Width of `text` in points when set in Helvetica at `fontSizePt` (AFM
// metrics for WinAnsi). Exposed for the dialogs' live previews.
double helveticaTextWidthPt(const QString& text, double fontSizePt);

// ── OCR text layer ───────────────────────────────────────────────────────────
// One recognized word with its box in page points (top-left origin).
struct OcrWord {
    QString text;
    QRectF  box;
};

// Overlays an INVISIBLE text layer (render mode 3) with each word placed at
// its recognized position — this is what makes a scanned PDF searchable and
// copyable. Pages without entries are copied through untouched. Tagged like
// the other decorations, so re-running OCR replaces the old layer.
OpResult addInvisibleTextLayer(const QString& in, const QString& out,
                               const std::map<int, std::vector<OcrWord>>& wordsByPage);
bool hasOcrLayer(const QString& path);

} // namespace NativeOffice::Pdf
