#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// XlsxIo.h  (Sprint 15)
// Best-effort Microsoft Excel (.xlsx) reader/writer for NativeOffice Calc.
//
// .xlsx is a ZIP package of OOXML SpreadsheetML parts. This module is
// self-contained: it bundles a raw-DEFLATE inflater + ZIP reader (for import)
// and a STORED (uncompressed) ZIP writer (for export) — no external deps.
//
// Imported: sheet names + order, cell values (text/number/bool) and formulas,
// cell formatting, merged cells, column widths and row heights, plus the charts
// and pictures drawn over the cells (see XlsxDrawing.h).
// Exported: everything above except charts and pictures.
// Formulas are written as <f> (Excel recalculates on open).
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>
#include <vector>
#include <utility>

#include "Cell.h"
#include "ChartSpec.h"
#include "CondFormat.h"

namespace NativeOffice {

struct XlsxCell {
    int        col;        // 0-based
    int        row;        // 0-based
    QString    content;    // raw value, or "=FORMULA"
    CellFormat format;     // font/fill/alignment/number-format/borders
    int        xfIndex { -1 };  // the file's own style index for this cell
};

struct XlsxSheet {
    QString               name;
    std::vector<XlsxCell> cells;
    std::vector<QString>  merges;       // merged ranges, e.g. "A1:B2"
    std::vector<std::pair<int, int>> colWidths;   // (col, pixels)
    std::vector<std::pair<int, int>> rowHeights;  // (row, pixels)
    // Objects drawn over the cells rather than in them.
    std::vector<ChartSpec>  charts;
    std::vector<SheetImage> images;
    std::vector<SheetShape> shapes;

    // How the sheet was last being looked at. A workbook stores this, and
    // ignoring it is why a file authored at 70% with the grid switched off
    // opened looking nothing like it does everywhere else.
    int  zoomScale     { 100 };
    bool showGridLines { true };
    bool hidden        { false };   // the workbook marks this sheet hidden
    // Conditional-format rules, already resolved to concrete colours.
    std::vector<CondFormatRule> condRules;
    std::vector<int> hiddenCols;
    std::vector<int> hiddenRows;
};

// Parse the .xlsx at `path` into `outSheets`. Returns true on success (at least
// one sheet read). On failure `outSheets` is cleared.
bool importXlsx(const QString& path, std::vector<XlsxSheet>& outSheets);

// Write `sheets` to a .xlsx package at `path`. Returns true on success.
//
// Rebuilds the whole package from the cell data, so anything the app does not
// model (charts, pictures, pivot tables, the theme) is not in the result.
bool exportXlsx(const QString& path, const std::vector<XlsxSheet>& sheets);

// Write `sheets` back into a copy of the package they came from.
//
// Every part of `original` is carried across untouched except each worksheet's
// <sheetData>, which is regenerated from `sheets`. Charts, pictures, pivot
// tables, defined names and the theme therefore survive a save, and styles.xml
// is left alone because cells re-use the style index they were imported with.
//
// Returns false when the workbook cannot be updated this way (the sheet set
// changed, or a cell needs a style the original file has no entry for); the
// caller should fall back to exportXlsx().
// Whether exportXlsxPreserving() would succeed for this workbook: the same
// checks, run without writing anything. A caller that wants to tell the user
// what a save is about to lose has to know this before it overwrites the file.
bool canPreserveXlsx(const std::vector<XlsxSheet>& sheets,
                     const QByteArray& original);

bool exportXlsxPreserving(const QString& path,
                          const std::vector<XlsxSheet>& sheets,
                          const QByteArray& original);

} // namespace NativeOffice
