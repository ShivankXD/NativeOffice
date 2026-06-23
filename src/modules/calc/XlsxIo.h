#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// XlsxIo.h  (Sprint 15)
// Best-effort Microsoft Excel (.xlsx) reader/writer for NativeOffice Calc.
//
// .xlsx is a ZIP package of OOXML SpreadsheetML parts. This module is
// self-contained: it bundles a raw-DEFLATE inflater + ZIP reader (for import)
// and a STORED (uncompressed) ZIP writer (for export) — no external deps.
//
// Imported / exported: sheet names + order, cell values (text/number/bool) and
// formulas. NOT yet handled: cell formatting/styles, merged cells, charts.
// Formulas are written as <f> (Excel recalculates on open).
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>
#include <vector>
#include <utility>

#include "Cell.h"

namespace NativeOffice {

struct XlsxCell {
    int        col;        // 0-based
    int        row;        // 0-based
    QString    content;    // raw value, or "=FORMULA"
    CellFormat format;     // font/fill/alignment/number-format/borders
};

struct XlsxSheet {
    QString               name;
    std::vector<XlsxCell> cells;
    std::vector<QString>  merges;       // merged ranges, e.g. "A1:B2"
    std::vector<std::pair<int, int>> colWidths;   // (col, pixels)
    std::vector<std::pair<int, int>> rowHeights;  // (row, pixels)
};

// Parse the .xlsx at `path` into `outSheets`. Returns true on success (at least
// one sheet read). On failure `outSheets` is cleared.
bool importXlsx(const QString& path, std::vector<XlsxSheet>& outSheets);

// Write `sheets` to a .xlsx package at `path`. Returns true on success.
bool exportXlsx(const QString& path, const std::vector<XlsxSheet>& sheets);

} // namespace NativeOffice
