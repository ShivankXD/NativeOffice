#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfOps.h — Merge / Split / Compress, built on PdfDocument (reader) and
// PdfWriter (always emits a fresh classic xref, regardless of the inputs'
// format). Every operation:
//   • validates all inputs up front via PdfDocument::open() (rejects
//     encrypted/malformed files with a specific reason, writes nothing),
//   • after writing the output, re-opens it through PdfDocument and checks
//     the page count — if that self-check fails, the output file is deleted
//     and the operation is reported as failed (never leaves a corrupt or
//     partial file on disk).
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>
#include <QStringList>

namespace NativeOffice::Pdf {

struct OpResult {
    bool ok = false;
    QString message;   // empty on success; user-facing reason on failure
};

// Concatenates all pages of `inputPaths`, in order, into one file at `outputPath`.
OpResult mergePdfs(const QStringList& inputPaths, const QString& outputPath);

// Extracts pages [startPage, endPage] (1-based, inclusive) of `inputPath`
// into a new file at `outputPath`.
OpResult splitPdf(const QString& inputPath, int startPage, int endPage, const QString& outputPath);

// Best-effort size reduction: re-flates (max compression level) every stream
// already using /FlateDecode. Streams under any other/no filter (e.g.
// DCTDecode/JPEG images) are left untouched — no re-encoding, no quality
// loss, no risk of misinterpreting a filter we don't handle.
OpResult compressPdf(const QString& inputPath, const QString& outputPath);

// Returns the page count of a PDF, or -1 with `error` set if it can't be
// opened (used by the UI to validate a split range before running).
int pdfPageCountOrError(const QString& path, QString& error);

// ── Structural page operations (all page indices are 0-based) ───────────────
// All of these rebuild the document through the same Copier machinery as
// merge/split, preserving catalog-level extras (outlines, forms, names…)
// where they can't dangle.

// Adds `degreesCW` (90/180/270) to /Rotate of `pages` (empty = all pages).
OpResult rotatePages(const QString& inputPath, const std::vector<int>& pages,
                     int degreesCW, const QString& outputPath);

// Removes `pages`. At least one page must remain.
OpResult deletePages(const QString& inputPath, const std::vector<int>& pages,
                     const QString& outputPath);

// Reorders pages: `newOrder` is a permutation of 0..n-1 (output page i is
// input page newOrder[i]).
OpResult reorderPages(const QString& inputPath, const std::vector<int>& newOrder,
                      const QString& outputPath);

// New document containing exactly `pages`, in the given order.
OpResult extractPages(const QString& inputPath, const std::vector<int>& pages,
                      const QString& outputPath);

// Inserts one blank page (dimensions in points) so it lands at `atIndex`.
OpResult insertBlankPage(const QString& inputPath, int atIndex,
                         double widthPt, double heightPt, const QString& outputPath);

// Inserts all pages of `insertPdfPath` so the first lands at `atIndex`.
OpResult insertPdfAt(const QString& inputPath, const QString& insertPdfPath,
                     int atIndex, const QString& outputPath);

// Replaces pages [fromPage..toPage] with all pages of `srcPdfPath`.
OpResult replacePages(const QString& inputPath, int fromPage, int toPage,
                      const QString& srcPdfPath, const QString& outputPath);

// Sets /CropBox of `pages` (empty = all). The rect is in PDF page
// coordinates (origin bottom-left), clamped to the media box by viewers.
OpResult setCropBox(const QString& inputPath, const std::vector<int>& pages,
                    double x0, double y0, double x1, double y1,
                    const QString& outputPath);

// Resizes `pages` (empty = all) to the new size in points, scaling page
// content to fit (anisotropic).
OpResult resizePages(const QString& inputPath, const std::vector<int>& pages,
                     double widthPt, double heightPt, const QString& outputPath);

} // namespace NativeOffice::Pdf
