#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// FormulaEngine.h  (Sprint 4 → Sprint 10)
// Value-based formula evaluator for NativeOffice Calc.
//
// Supported syntax (case-insensitive cell refs and function names):
//   Plain text / numbers  → returned as-is
//   =<expr>               → evaluated expression
//
// Expression grammar (recursive descent, lowest → highest precedence):
//   expr     = concat
//   concat   = compare { '&' compare }                  (text concatenation)
//   compare  = addsub  { ('='|'<>'|'<='|'>='|'<'|'>') addsub }
//   addsub   = muldiv  { ('+'|'-') muldiv }
//   muldiv   = power   { ('*'|'/') power }
//   power    = unary   { '^' unary }
//   unary    = ('-'|'+') unary | primary
//   primary  = number | string | TRUE | FALSE
//            | funcCall | cellRef | '(' expr ')'
//   funcCall = IDENT '(' [ arg { ',' arg } ] ')'
//   arg      = range | expr
//   range    = cellRef ':' cellRef
//
// Values are numbers, text, booleans, or error tokens. Computational errors
// propagate as values so functions like IFERROR can inspect them:
//   #DIV/0!  divide by zero
//   #VALUE!  wrong value type (e.g. text where a number is needed)
//   #N/A     value not available
//   #NAME?   unknown function name
//   #NUM!    numeric overflow / domain error (e.g. SQRT of negative)
//   #REF!    invalid cell reference
//   #ERR     generic parse failure
//   #CIRC    circular reference (detected in SpreadsheetModel, not here)
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace NativeOffice {

class FormulaEngine {
public:
    // Resolve a cell reference to its already-EVALUATED display value.
    // 'sheet' is empty for the current sheet, or a sheet name for cross-sheet
    // references (e.g. Sheet2!A1). Returning the evaluated value (rather than
    // raw content) means the engine never self-recurses — the model's
    // displayValue() drives recursion and per-cell circular detection.
    using CellLookup = std::function<QString(const QString& sheet, int col, int row)>;

    FormulaEngine() = default;

    // Evaluate 'input' in the context of 'lookup'.
    // If input does not start with '=', the raw input is returned.
    [[nodiscard]] QString evaluate(const QString&    input,
                                   const CellLookup& lookup) const;

    // ── Static address helpers ────────────────────────────────────────────
    [[nodiscard]] static bool parseCellRef(const QString& ref, int& col, int& row);
    [[nodiscard]] static QString colLabel(int col);
    [[nodiscard]] static QString rowLabel(int row);
    [[nodiscard]] static QString cellAddress(int col, int row);
    [[nodiscard]] static QStringList expandRange(const QString& rangeStr);

private:
    // ── Value model ───────────────────────────────────────────────────────
    struct Value {
        enum Kind { Number, Text, Boolean, Error };
        Kind    kind { Number };
        double  num  { 0.0 };
        QString text;            // payload for Text / Error (token)

        static Value number(double n)        { return {Number,  n, {}}; }
        static Value boolean(bool b)         { return {Boolean, b ? 1.0 : 0.0, {}}; }
        static Value string(const QString& s){ return {Text,    0.0, s}; }
        static Value error(const QString& e) { return {Error,   0.0, e}; }
        static Value empty()                 { return {Text,    0.0, QString()}; }

        bool isError() const { return kind == Error; }
    };

    // A function argument: either a flattened range or a single scalar value.
    struct Arg {
        bool           isRange { false };
        QVector<Value> values;          // range: every non-empty cell; scalar: one
        // 2D grid of the range INCLUDING empties (rows × cols), for lookup
        // functions (VLOOKUP/HLOOKUP/INDEX/MATCH). Empty for scalar args.
        QVector<QVector<Value>> grid;
    };

    // Number-formatting of a final Value into the display string.
    [[nodiscard]] QString formatValue(const Value& v) const;

    // Evaluate a raw cell string (or formula) to a Value (used for recursion).
    [[nodiscard]] Value evaluateValue(const QString& input,
                                      const CellLookup& lookup) const;

    // ── Recursive-descent parser ──────────────────────────────────────────
    struct Parser {
        const QString&       src;
        int                  pos { 0 };
        const CellLookup&    lookup;
        const FormulaEngine& engine;
        bool                 syntaxError { false };   // malformed input → #ERR

        Value parseExpr();
        Value parseConcat();
        Value parseCompare();
        Value parseAddSub();
        Value parseMulDiv();
        Value parsePower();
        Value parseUnary();
        Value parsePrimary();

        Value parseNumber();
        Value parseString();
        Value parseRefToValue(const QString& sheet, const QString& token);
        Value parseFuncCall(const QString& name);
        Arg   parseArg();

        void   skipWs();
        QChar  peekCh() const;
        char   peek() const;
        char   consume();
        bool   match(char c);
        QString readRefToken();                  // [A-Za-z]+[0-9]+ or empty
        QString readQualifiedRef(QString& sheet);// optional "Sheet!" prefix + ref
    };

    // Function dispatch. Returns Error("#NAME?") for unknown names.
    static Value callFunction(const QString& upperName, QVector<Arg>& args);

    // Coerce a Value to a number; sets err to a #VALUE!/#DIV etc. on failure.
    static double toNumber(const Value& v, bool& ok);
    static QString toText(const Value& v);

    // Recursion guard against circular references / runaway chains.
    mutable int m_evalDepth { 0 };
};

} // namespace NativeOffice
