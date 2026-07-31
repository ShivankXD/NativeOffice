#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// WatermarkPdf.h — stamps the export watermark into a finished PDF.
//
// One entry point for every PDF the app writes, whatever produced it:
// QTextDocument::print into a QPdfWriter (Writer), a manual QPainter page loop
// (Calc, Impress), an image import (PdfConvert), or a PDFium save (the PDF
// module and its toolkit).
//
// Stamping afterwards rather than during painting is deliberate. QPdfWriter
// can draw the artwork but exposes no annotation API whatsoever, so the link
// always required reopening the file with PDFium regardless. Doing the artwork
// in the same pass means one implementation instead of two, and the mark comes
// out identical to the one embedded in the OOXML formats.
//
// The link is a Link annotation carrying a URI action, its rect set to the
// artwork's exact bounding box with border width forced to zero. That makes it
// invisible and confines the hit area to the mark itself, so a click just
// outside it — or a drag, or a text selection ending near the corner — does
// not follow the link.
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>

namespace NativeOffice {
namespace Watermark {

// Draws the artwork on every page of an existing PDF and attaches the link.
// Pages too small to carry the mark are left alone. Returns false and leaves
// the file untouched on failure, so a stamping problem never costs the user
// their export.
bool stampFile(const QString& pdfPath);

// Convenience: stamps only when the current account's plan calls for it.
// Export paths call this so the policy check lives in one place.
bool stampIfRequired(const QString& pdfPath);

} // namespace Watermark
} // namespace NativeOffice
