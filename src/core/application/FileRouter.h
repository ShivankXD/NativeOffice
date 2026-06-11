#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// FileRouter.h  (Sprint 7)
// Stateless utility that inspects file content to decide which NativeOffice
// module (Writer, Calc, Impress) should open the file.
//
// Strategy:
//   1. Binary formats (.xlsx, .pptx, …) are routed by extension alone.
//   2. Text-based files are sampled (first ~8 KB) and classified by
//      content heuristics: presentation markers → Impress,
//      tabular/CSV structure → Calc, everything else → Writer.
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>

namespace NativeOffice {

enum class DetectedFileType {
    WriterDocument,    // HTML, rich text, plain text → WriterModule
    SpreadsheetData,   // CSV / TSV tabular data     → CalcModule
    PresentationData   // Slide / presentation data  → ImpressModule
};

class FileRouter {
public:
    /// Reads up to ~8 KB of the file and determines the best module.
    /// Falls back to extension-based routing if content is ambiguous.
    static DetectedFileType detectFileType(const QString& filePath);

private:
    // Sub-heuristics applied to the sampled text
    static bool looksLikePresentation(const QString& sample);
    static bool looksLikeSpreadsheet(const QString& sample);
};

} // namespace NativeOffice
