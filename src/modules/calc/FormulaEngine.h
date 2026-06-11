#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// FormulaEngine.h  (Sprint 4)
// Stateless formula evaluator for NativeOffice Calc.
//
// Supported syntax (case-insensitive cell refs):
//   Plain text / numbers  → returned as-is
//   =<expr>               → evaluated arithmetic expression
//
// Expression grammar (recursive descent):
//   expr   = term   { ('+' | '-') term   }
//   term   = factor { ('*' | '/') factor }
//   factor = number | cellRef | '(' expr ')'
//   number = ['-'] digit+ ['.' digit+]
//   cellRef= [A-Za-z]+ [0-9]+   (e.g. A1, BC3, Z100)
//
// Cell references are resolved via a callback so the engine stays decoupled
// from any particular model or storage backend.
//
// On error (divide-by-zero, unknown ref, parse failure) returns "#ERR".
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>
#include <functional>

namespace NativeOffice {

class FormulaEngine {
public:
    // Callback type: given column-index (0-based) and row-index (0-based),
    // return the raw string stored in that cell (not the evaluated value,
    // to avoid infinite recursion — the engine recurses itself when needed).
    using CellLookup = std::function<QString(int col, int row)>;

    FormulaEngine() = default;

    // Evaluate 'input' in the context of 'lookup'.
    // If input does not start with '=', the raw input is returned.
    [[nodiscard]] QString evaluate(const QString&    input,
                                   const CellLookup& lookup) const;

    // Parse a cell reference string like "A1", "BC23" into (col, row) indices.
    // col and row are 0-based.  Returns false if the string is not a valid ref.
    [[nodiscard]] static bool parseCellRef(const QString& ref, int& col, int& row);

    // Convert a 0-based column index to its letter label (0→"A", 25→"Z", 26→"AA").
    [[nodiscard]] static QString colLabel(int col);

    // Convert a 0-based row index to its display label (0→"1", 99→"100").
    [[nodiscard]] static QString rowLabel(int row);

    // Convert a (col, row) pair to a cell address string, e.g. (0,0) → "A1".
    [[nodiscard]] static QString cellAddress(int col, int row);

private:
    // ── Recursive-descent parser internals ────────────────────────────────
    struct Parser {
        const QString&    src;
        int               pos  { 0 };
        const CellLookup& lookup;
        const FormulaEngine& engine;
        bool              error { false };

        double parseExpr();
        double parseTerm();
        double parseFactor();
        double parseNumber();
        double parseCellRef();
        void   skipWs();
        char   peek() const;
        char   consume();
        bool   match(char c);
    };
};

} // namespace NativeOffice
