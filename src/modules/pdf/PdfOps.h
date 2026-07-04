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

} // namespace NativeOffice::Pdf
