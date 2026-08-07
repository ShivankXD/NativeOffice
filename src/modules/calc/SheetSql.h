#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SheetSql.h — run SQL against the workbook's sheets.
//
// Each sheet becomes a table (sheet name = table name, header row = column
// names), the query runs against an in-memory SQLite database built fresh for
// that query, and the result comes back as a table the caller can drop into a
// new sheet.
//
// ── ENGINE CHOICE: SQLite, via Qt6::Sql ─────────────────────────────────────
// Cost: 2.19 MB added to the install (Qt6Sql.dll 304 KB + the bundled
// qsqlite.dll 1,936 KB). Against a 95.6 MB install that is +2.3%, and the
// "small, native, not 1.5 GB" story is untouched at ~97.8 MB.
//
// Chosen over a hand-written SQL-subset interpreter (which would have cost
// perhaps 40 KB) for two reasons that are not really about size:
//
//   1. JOINs are the Premium hook for this feature. A hand-rolled subset ships
//      without them by definition, so the paid half of the feature would not
//      exist, and the hardest part of a query engine (join execution, and the
//      NULL semantics around it) would still be waiting to be written. That is
//      building the free tier twice and deferring the revenue.
//
//   2. SQL is a language users already know, and this is a DATA tool. Subtly
//      wrong answers on NULL propagation, type affinity, GROUP BY/HAVING or
//      LIKE are worse than not shipping the feature: a wrong number in a
//      spreadsheet is not obviously wrong. SQLite gets all of that right for
//      free and is the most-tested database in existence.
//
// Vendoring the SQLite amalgamation directly would land nearer 500-700 KB with
// aggressive SQLITE_OMIT_* flags, i.e. cheaper than Qt6::Sql. It is a
// reasonable future optimisation and this file would not change: only the two
// functions that open the connection would. It was not done now because it
// means carrying 8 MB of third-party source for a first cut.
//
// To reverse the decision entirely: delete these four SheetSql files, the
// showSqlPanel/runSqlQuery calls in CalcModule, and Qt6::Sql from
// src/modules/calc/CMakeLists.txt.
//
// ── Safety ──────────────────────────────────────────────────────────────────
// Only a single SELECT (or WITH ... SELECT) is accepted. The database is
// in-memory and discarded after every query, but ATTACH could still reach the
// filesystem and PRAGMA could change engine behaviour, so both are rejected
// outright rather than relied on being harmless. See isQuerySafe().
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>
#include <QStringList>
#include <QVector>

namespace NativeOffice {
namespace SheetSql {

// One sheet, flattened for the engine. `headers` comes from the sheet's header
// row; `rows` are the data rows below it.
struct SourceSheet {
    QString              name;
    QStringList          headers;
    QVector<QStringList> rows;
};

struct ResultTable {
    QStringList          headers;
    QVector<QStringList> rows;
};

struct QueryResult {
    ResultTable table;
    QString     error;
    int         truncatedTo { -1 };   // >=0 when the result was capped
    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

// What a query needs, worked out WITHOUT running it, so the entitlement
// decision can be made before any work happens.
struct QueryInfo {
    QStringList tables;                 // tables named after FROM / JOIN
    bool        multiTable  { false };  // a JOIN, or more than one table
    int         rowsInvolved { 0 };     // total data rows across those tables
    bool        valid       { true };   // false when it is not a plain SELECT
    QString     reason;                 // why it is not valid
};

// Free-tier ceiling on how many rows a query may touch.
//
// Currently unreachable: SpreadsheetModel::NUM_ROWS is 100, so a workbook
// cannot present anywhere near this many rows. It is wired up rather than
// omitted so the gate starts working by itself if the grid ever grows, and set
// to a number that would actually mean something then, instead of an invented
// small limit that would only punish people today.
inline constexpr int kFreeRowLimit = 5000;

// Hard cap on returned rows, so a cross join cannot try to materialise
// millions of rows into a 100-row sheet.
inline constexpr int kMaxResultRows = 5000;

[[nodiscard]] QueryInfo analyze(const QString& sql, const QVector<SourceSheet>& sheets);

// Builds the database, runs the query, tears the connection down again.
[[nodiscard]] QueryResult run(const QVector<SourceSheet>& sheets, const QString& sql);

// True when `sql` is a single SELECT/WITH with no statement separator abuse.
[[nodiscard]] bool isQuerySafe(const QString& sql, QString* why = nullptr);

// The identifier a sheet is registered under, and whether it needs quoting in
// a query. Exposed so the panel can show the user exactly what to type.
[[nodiscard]] QString quoteIdentifier(const QString& name);

} // namespace SheetSql
} // namespace NativeOffice
