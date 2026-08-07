#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// StructuredData.h — JSON / YAML <-> tabular conversion for Calc.
//
// Headless on purpose: this file pulls in QtCore only, no widgets. The dialog
// that drives it lives in StructuredDataDialog, and CalcModule owns the three
// entry points. Nothing else in Calc depends on this, so the whole feature can
// be dropped by deleting these four files and the calls in CalcModule.
//
// Zero new dependencies. JSON goes through QJsonDocument (QtCore, already
// linked); YAML uses the hand-rolled subset parser below rather than yaml-cpp.
// Both formats land in the SAME QJsonValue tree, so the flattening rules are
// written once and cannot drift between them.
//
// ── Finding the table ───────────────────────────────────────────────────────
// The shape is worked out before any flattening, because real input is rarely
// a bare array of records:
//
//   {"spreadsheet": {...}}          wrappers are peeled off (single-key
//   {"result": [...]}               objects, and objects where exactly one
//   {"count": 4, "items": [...]}    key holds a non-empty array)
//
//   {"columns": [...],              an explicit table: columns become the
//    "rows": [[...], [...]]}        header, rows become rows. Rows may be
//                                   arrays or objects keyed by column name.
//                                   Also accepts headers/fields/cols and
//                                   data/values/records as key spellings.
//
//   [[...], [...]]                  a matrix. The first row is the header when
//                                   it reads like one (all non-empty text with
//                                   rows beneath), else Column1..N is generated
//                                   so nothing is eaten by a header that was
//                                   never there.
//
// Only when none of those match does it fall through to the record rules below.
// Without this step the very common {"columns":…, "rows":…} shape flattened to
// ONE row of dot-indexed columns, which is what the rules say and useless.
//
// ── Flattening rules (records) ──────────────────────────────────────────────
// Root array   -> one row per element. This is the common shape (a list of
//                 records) and the only one where "rows" is obviously right.
// Root object  -> a single row.
// Nested object-> dot-notation columns:  {"a":{"b":1}}      -> column "a.b"
// Nested array -> indexed columns:       {"t":["x","y"]}    -> "t.0", "t.1"
//
// Nested arrays deliberately do NOT expand into extra rows: a record with two
// nested arrays would need a cross product, and there is no honest answer for
// how to pair them up. Indexed columns keep one input record on one row, which
// is what makes the round trip back out predictable.
//
// Columns are the union of every row's keys, so a field missing from some
// records still gets a column and those cells stay blank.
//
// COLUMN ORDER IS ALPHABETICAL, not source order. QJsonObject keeps its keys
// sorted, so by the time a document is parsed the original order is already
// gone, for JSON and YAML alike (the YAML parser builds QJsonObjects too).
// Preserving source order would mean not using QJsonDocument, i.e. hand-writing
// an order-preserving JSON parser, which is a poor trade against the binary
// budget for a cosmetic gain. Sorted is at least stable and predictable.
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>
#include <QStringList>
#include <QVector>

class QJsonValue;

namespace NativeOffice {
namespace StructuredData {

// A rectangular table: one header row plus data rows. Rows are padded to
// headers.size() by the parser, so callers never have to bounds-check.
struct Table {
    QStringList          headers;
    QVector<QStringList> rows;

    [[nodiscard]] bool isEmpty() const { return headers.isEmpty() && rows.isEmpty(); }
    [[nodiscard]] int  columns() const { return headers.size(); }
};

enum class Format { Json, Yaml };

struct ParseResult {
    Table   table;
    QString error;         // empty when the parse succeeded
    int     line { -1 };   // 1-based line the error is on, -1 when not known
    // How the input was interpreted, in words, e.g. "a table with named
    // columns". Shown in the dialog: flattening decisions are invisible in the
    // result, and telling the user which rule fired is what makes a surprising
    // grid explainable rather than just wrong.
    QString shape;

    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

// Cheap structural guess so the dialog can offer "Auto". Looks at the first
// meaningful character: '{' or '[' means JSON, anything else means YAML.
// JSON is a subset of YAML in spirit but not in this parser, so when the guess
// matters the user can always override it.
[[nodiscard]] Format detectFormat(const QString& text);

[[nodiscard]] ParseResult parse(const QString& text, Format fmt);
[[nodiscard]] ParseResult parseJson(const QString& text);
[[nodiscard]] ParseResult parseYaml(const QString& text);

// ── Emitting ────────────────────────────────────────────────────────────────
// Both take the first row of `t` as the field names. Cell text is type-inferred
// on the way out (see inferScalar) so "42" emits as a number and "true" as a
// bool, which is what makes the output usable as real JSON/YAML rather than a
// grid of quoted strings.
[[nodiscard]] QString toJson(const Table& t, bool pretty = true);
[[nodiscard]] QString toYaml(const Table& t);

// Exposed for the emitters and for testing: turn cell text into the JSON value
// it most likely represents (null / bool / number / string).
[[nodiscard]] QJsonValue inferScalar(const QString& text);

} // namespace StructuredData
} // namespace NativeOffice
