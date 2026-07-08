#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfConvert.h — the Convert tab. All conversions are self-contained (PDFium
// for parsing/rendering + Qt's QZipWriter for the OOXML packages + QPdfWriter
// for the reverse direction). No new third-party dependency.
//
//   PDF → TXT      real text extraction, page-separated.
//   PDF → Picture  each page rasterized to PNG/JPEG at a chosen DPI.
//   PDF → Word     .docx, paragraphs reconstructed from PDFium line geometry.
//   PDF → Excel    .xlsx, rows/columns inferred from character positions.
//   PDF → PPT      .pptx, one image-filled slide per page.
//   Picture → PDF  images composited one-per-page.
//   Image-only PDF each page flattened to a raster (removes selectable text).
//   Extract Text / Page / Picture  (page extraction lives in PdfOps).
//
// Text-reconstruction fidelity is line-level, not full reflow — good for
// getting editable content out of a PDF, not a pixel-perfect round-trip.
// ─────────────────────────────────────────────────────────────────────────────

#include "PdfOps.h"

#include <QStringList>

namespace NativeOffice::Pdf {

enum class RasterFormat { Png, Jpeg };

OpResult toTxt(const QString& in, const QString& out);

// Writes one image per page named "<baseName>-<n>.<ext>" into `outDir`.
// `writtenPaths` (optional) receives the created files.
OpResult toImages(const QString& in, const QString& outDir, const QString& baseName,
                  RasterFormat fmt, int dpi, QStringList* writtenPaths = nullptr);

OpResult toDocx(const QString& in, const QString& out);
OpResult toXlsx(const QString& in, const QString& out);
OpResult toPptx(const QString& in, const QString& out, int dpi);

// Composites `imagePaths` into a PDF, one image per page (auto-oriented,
// fit to a Letter page with margins).
OpResult imagesToPdf(const QStringList& imagePaths, const QString& out);

// Re-renders every page to a raster and rebuilds the PDF from those images —
// the result has no selectable text (a "flattened"/scanned-looking PDF).
OpResult toImageOnlyPdf(const QString& in, const QString& out, int dpi);

// Extracts embedded raster images to "<baseName>-p<page>-<n>.png" in `outDir`.
OpResult extractImages(const QString& in, const QString& outDir, const QString& baseName,
                       QStringList* writtenPaths = nullptr);

} // namespace NativeOffice::Pdf
