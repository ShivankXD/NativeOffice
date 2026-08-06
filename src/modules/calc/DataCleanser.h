#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// DataCleanser.h — column/sheet cleanup operations for Calc.
//
// Headless: QtCore only, no widgets and no AuthManager. The grid is reached
// through a read callback, so nothing here knows about SpreadsheetModel and the
// whole file can be exercised from a test harness. DataCleanserPanel drives it
// and CalcModule applies the results through the undo stack.
//
// Zero new dependencies: every transform is QRegularExpression, QString and
// QDate, all already linked.
//
// Every operation is PURE. Nothing writes to the grid; each returns the list of
// changes it would make, so the caller can push one undo command, report an
// accurate count, and (for the gated operations) decline to apply anything at
// all without having half-mutated the sheet.
// ─────────────────────────────────────────────────────────────────────────────

#include <QRect>
#include <QString>
#include <QVector>

#include <functional>

namespace NativeOffice {
namespace DataCleanser {

enum class Op {
    TrimWhitespace,     // free
    RemoveDuplicates,   // free
    StandardizeDates,   // Premium
    ExtractEmails,      // Premium
    ExtractUrls,        // Premium
};

// Which side of the paywall an operation sits on. Kept here, next to the
// operations themselves, so the tier cannot drift away from what it describes;
// the actual entitlement check lives at the call site, where AuthManager is.
[[nodiscard]] bool    requiresPremium(Op op);
[[nodiscard]] QString opName(Op op);

// One cell to rewrite. `text` is the new raw content.
struct Change {
    int     col;
    int     row;
    QString text;
};

struct Result {
    QVector<Change> changes;
    QString         summary;      // what to show the user afterwards
    bool            ok { true };  // false when the operation could not run
};

// Reads the raw (un-evaluated) content of a cell.
using CellReader = std::function<QString(int col, int row)>;

struct Options {
    // Ambiguous numeric dates (01/02/2024) have no correct reading without
    // being told which convention the sheet uses. Defaults to day-first.
    bool dayFirst { true };
    // Skips the first row of the region, so a header is neither trimmed into a
    // different string nor treated as a duplicate candidate.
    bool firstRowIsHeader { true };
};

// ── Free ────────────────────────────────────────────────────────────────────
// Trims the ends and collapses internal whitespace runs to a single space.
// Non-breaking spaces are normalised first, since they are what usually
// survives a copy-paste out of a web page and they are invisible in the grid.
[[nodiscard]] Result trimWhitespace(const CellReader& read, const QRect& region,
                                    const Options& opt);

// Drops repeated rows within the region, comparing the whole row. Survivors are
// packed upward and the freed rows are cleared.
[[nodiscard]] Result removeDuplicateRows(const CellReader& read, const QRect& region,
                                         const Options& opt);

// ── Premium ─────────────────────────────────────────────────────────────────
// Rewrites recognised dates as ISO 8601 (YYYY-MM-DD) in place. Anything that
// does not parse as a real calendar date is left exactly as it was, so a column
// of part numbers cannot be quietly turned into dates.
[[nodiscard]] Result standardizeDates(const CellReader& read, const QRect& region,
                                      const Options& opt);

// Pull every email / URL out of the region and write them into `targetCol`, one
// row per source row, multiple matches joined with "; ". Non-destructive: the
// source cells are untouched.
[[nodiscard]] Result extractEmails(const CellReader& read, const QRect& region,
                                   int targetCol, const Options& opt);
[[nodiscard]] Result extractUrls(const CellReader& read, const QRect& region,
                                 int targetCol, const Options& opt);

// Exposed for testing.
[[nodiscard]] QString toIsoDate(const QString& text, bool dayFirst); // "" if not a date
[[nodiscard]] QString tidyWhitespace(const QString& text);

} // namespace DataCleanser
} // namespace NativeOffice
