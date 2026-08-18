// ─────────────────────────────────────────────────────────────────────────────
// FileRouter.cpp  (Sprint 7)
// Content-based file type detection.
//
// Heuristic order:
//   1. Binary-format extensions → immediate return (no content scan)
//   2. Read first ~8 KB as UTF-8 text
//   3. Presentation markers (NativeOffice Impress header, JSON "slides" key,
//      known presentation XML namespaces)
//   4. Spreadsheet / CSV markers (consistent column count with commas or tabs
//      on ≥ 60 % of non-empty lines)
//   5. Default → WriterDocument (HTML, rich text, plain text)
// ─────────────────────────────────────────────────────────────────────────────
#include "FileRouter.h"

#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QStringList>
#include <QRegularExpression>

namespace NativeOffice {

// ── Maximum sample size (8 KB is plenty for heuristic sniffing) ──────────────
static constexpr qint64 SAMPLE_SIZE = 8192;

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────
DetectedFileType FileRouter::detectFileType(const QString& filePath)
{
    const QString ext = QFileInfo(filePath).suffix().toLower();

    // ── 1. Fast-path: binary formats whose content can't be usefully
    //       scanned as text.  Route by extension only.
    if (ext == "xlsx" || ext == "xlsm" || ext == "ods" || ext == "xls")
        return DetectedFileType::SpreadsheetData;
    if (ext == "pptx" || ext == "odp" || ext == "ppt")
        return DetectedFileType::PresentationData;
    // .docx is a ZIP too, so without this it fell through to the text sniffer,
    // which was reading 8 KB of compressed bytes and could in principle see a
    // consistent comma count in them and route a Word file to Sheets.
    if (ext == "docx" || ext == "doc" || ext == "odt" || ext == "rtf")
        return DetectedFileType::WriterDocument;
    // A single-column .csv has no delimiter for the sniffer to find, and would
    // otherwise open as a text document.
    if (ext == "csv" || ext == "tsv")
        return DetectedFileType::SpreadsheetData;

    // ── 2. Read a text sample from the file ──────────────────────────────
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // Can't read → fall back to Writer (it will show its own error)
        return DetectedFileType::WriterDocument;
    }

    const QByteArray raw = f.read(SAMPLE_SIZE);
    f.close();

    const QString sample = QString::fromUtf8(raw);

    // ── 3. Presentation heuristics ───────────────────────────────────────
    if (looksLikePresentation(sample))
        return DetectedFileType::PresentationData;

    // ── 3b. NativeOffice Calc .noff header (Sprint 8) ────────────────────
    if (sample.contains("NativeOffice Calc", Qt::CaseInsensitive))
        return DetectedFileType::SpreadsheetData;

    // ── 4. Spreadsheet / CSV heuristics ──────────────────────────────────
    if (looksLikeSpreadsheet(sample))
        return DetectedFileType::SpreadsheetData;

    // ── 5. Default: Writer (HTML, rich text, plain text, anything else) ──
    return DetectedFileType::WriterDocument;
}

// ─────────────────────────────────────────────────────────────────────────────
// Presentation detection
// ─────────────────────────────────────────────────────────────────────────────
bool FileRouter::looksLikePresentation(const QString& sample)
{
    // NativeOffice's own future Impress file header
    if (sample.contains("NativeOffice Impress", Qt::CaseInsensitive))
        return true;

    // JSON-based slide format: look for a "slides" key
    // (e.g.  {"slides": [...]}  or  "slides" : [  )
    if (sample.contains(QRegularExpression(R"("slides"\s*:\s*\[)")))
        return true;

    // OpenDocument Presentation namespace
    if (sample.contains("urn:oasis:names:tc:opendocument:xmlns:presentation",
                         Qt::CaseInsensitive))
        return true;

    // Microsoft Office Presentation XML namespace
    if (sample.contains("schemas.openxmlformats.org/presentationml",
                         Qt::CaseInsensitive))
        return true;

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Spreadsheet / CSV detection
//
// Strategy: split the sample into lines.  If ≥ 60 % of non-empty lines have
// a consistent number of comma-separated or tab-separated columns (≥ 2),
// classify as spreadsheet data.
// ─────────────────────────────────────────────────────────────────────────────
bool FileRouter::looksLikeSpreadsheet(const QString& sample)
{
    const QStringList lines = sample.split('\n', Qt::SkipEmptyParts);
    if (lines.size() < 2)
        return false;   // Need at least 2 lines for a table

    // Try comma first, then tab
    for (QChar delimiter : {QChar(','), QChar('\t')}) {
        int consistentLines = 0;
        int firstColCount   = -1;

        for (const QString& line : lines) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty()) continue;

            // Count columns by splitting on the delimiter
            const int cols = trimmed.split(delimiter).size();
            if (cols < 2) continue;   // Need at least 2 columns

            if (firstColCount < 0) {
                firstColCount = cols;
                consistentLines = 1;
            } else if (cols == firstColCount) {
                ++consistentLines;
            }
        }

        // Require ≥ 60 % of non-empty lines to have the same column count
        const int nonEmpty = static_cast<int>(lines.size());
        if (firstColCount >= 2
            && consistentLines >= 2
            && consistentLines * 100 / nonEmpty >= 60) {
            return true;
        }
    }

    return false;
}

} // namespace NativeOffice
