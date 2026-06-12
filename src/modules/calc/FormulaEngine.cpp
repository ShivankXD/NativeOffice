// ─────────────────────────────────────────────────────────────────────────────
// FormulaEngine.cpp  (Sprint 4 → Sprint 9)
// Recursive-descent arithmetic evaluator for NativeOffice Calc.
//
// Sprint 9 additions:
//   - expandRange()  — expands "A1:A5" into individual cell addresses
//   - parseFuncCall() — dispatches SUM(), AVERAGE() (case-insensitive)
//   - resolveRange()  — resolves a cell range into a vector of doubles
//   - Updated parseFactor() with function-call lookahead
// ─────────────────────────────────────────────────────────────────────────────
#include "FormulaEngine.h"

#include <QStringList>
#include <QChar>
#include <cmath>
#include <stdexcept>
#include <algorithm>

namespace NativeOffice {

// ─────────────────────────────────────────────────────────────────────────────
// Static helpers
// ─────────────────────────────────────────────────────────────────────────────
QString FormulaEngine::colLabel(int col) {
    // Excel-style: 0→A, 25→Z, 26→AA, 51→AZ, 52→BA …
    QString label;
    col += 1;   // 1-based for the algorithm
    while (col > 0) {
        int rem = (col - 1) % 26;
        label.prepend(QChar('A' + rem));
        col = (col - 1) / 26;
    }
    return label;
}

QString FormulaEngine::rowLabel(int row) {
    return QString::number(row + 1);
}

QString FormulaEngine::cellAddress(int col, int row) {
    return colLabel(col) + rowLabel(row);
}

bool FormulaEngine::parseCellRef(const QString& ref, int& col, int& row) {
    // ref must match [A-Za-z]+[0-9]+
    int i = 0;
    while (i < ref.size() && ref[i].isLetter()) ++i;
    if (i == 0 || i >= ref.size()) return false;

    // Parse the letter part
    const QString letters = ref.left(i).toUpper();
    int c = 0;
    for (QChar ch : letters) {
        c = c * 26 + (ch.toLatin1() - 'A' + 1);
    }
    col = c - 1;   // 0-based

    // Parse the number part
    bool ok = false;
    const int r = ref.mid(i).toInt(&ok);
    if (!ok || r < 1) return false;
    row = r - 1;   // 0-based

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sprint 9: Expand a range string like "A1:A5" into individual cell addresses
// ─────────────────────────────────────────────────────────────────────────────
QStringList FormulaEngine::expandRange(const QString& rangeStr) {
    const int colonIdx = rangeStr.indexOf(':');
    if (colonIdx < 0) return {};

    const QString startStr = rangeStr.left(colonIdx).trimmed();
    const QString endStr   = rangeStr.mid(colonIdx + 1).trimmed();

    int col1 = 0, row1 = 0, col2 = 0, row2 = 0;
    if (!parseCellRef(startStr, col1, row1)) return {};
    if (!parseCellRef(endStr,   col2, row2)) return {};

    // Normalise so we iterate from min to max in both dimensions
    if (col1 > col2) std::swap(col1, col2);
    if (row1 > row2) std::swap(row1, row2);

    QStringList result;
    result.reserve((col2 - col1 + 1) * (row2 - row1 + 1));

    for (int c = col1; c <= col2; ++c) {
        for (int r = row1; r <= row2; ++r) {
            result.append(cellAddress(c, r));
        }
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public entry point
// ─────────────────────────────────────────────────────────────────────────────
QString FormulaEngine::evaluate(const QString&    input,
                                 const CellLookup& lookup) const {
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) return {};

    // Not a formula — return as-is
    if (!trimmed.startsWith('=')) return trimmed;

    // Strip the leading '='
    const QString expr = trimmed.mid(1).trimmed();
    if (expr.isEmpty()) return {};

    Parser p{ expr, 0, lookup, *this };
    double result = p.parseExpr();

    if (p.error) return "#ERR";

    // Format: drop the decimal part if it's an integer result
    if (std::isinf(result) || std::isnan(result)) return "#ERR";
    if (result == std::floor(result) && std::abs(result) < 1e15)
        return QString::number(static_cast<long long>(result));
    return QString::number(result, 'g', 10);
}

// ─────────────────────────────────────────────────────────────────────────────
// Parser helpers
// ─────────────────────────────────────────────────────────────────────────────
void FormulaEngine::Parser::skipWs() {
    while (pos < src.size() && src[pos].isSpace()) ++pos;
}

char FormulaEngine::Parser::peek() const {
    if (pos >= src.size()) return '\0';
    return src[pos].toLatin1();
}

char FormulaEngine::Parser::consume() {
    if (pos >= src.size()) return '\0';
    return src[pos++].toLatin1();
}

bool FormulaEngine::Parser::match(char c) {
    skipWs();
    if (peek() == c) { consume(); return true; }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Grammar:  expr = term { ('+' | '-') term }
// ─────────────────────────────────────────────────────────────────────────────
double FormulaEngine::Parser::parseExpr() {
    double val = parseTerm();
    while (!error) {
        skipWs();
        const char op = peek();
        if (op != '+' && op != '-') break;
        consume();
        const double rhs = parseTerm();
        val = (op == '+') ? val + rhs : val - rhs;
    }
    return val;
}

// ─────────────────────────────────────────────────────────────────────────────
// Grammar:  term = factor { ('*' | '/') factor }
// ─────────────────────────────────────────────────────────────────────────────
double FormulaEngine::Parser::parseTerm() {
    double val = parseFactor();
    while (!error) {
        skipWs();
        const char op = peek();
        if (op != '*' && op != '/') break;
        consume();
        const double rhs = parseFactor();
        if (op == '/') {
            if (rhs == 0.0) { error = true; return 0.0; }
            val = val / rhs;
        } else {
            val = val * rhs;
        }
    }
    return val;
}

// ─────────────────────────────────────────────────────────────────────────────
// Grammar:  factor = number | funcCall | cellRef | '(' expr ')'
//                  | unary '-' factor
//
// Sprint 9: When a letter-sequence is followed by '(', it's a function call.
//           Otherwise it falls through to the existing cellRef path.
// ─────────────────────────────────────────────────────────────────────────────
double FormulaEngine::Parser::parseFactor() {
    skipWs();
    if (error) return 0.0;

    const char c = peek();

    // Parenthesised sub-expression
    if (c == '(') {
        consume();
        double val = parseExpr();
        if (!match(')')) { error = true; return 0.0; }
        return val;
    }

    // Unary minus
    if (c == '-') {
        consume();
        return -parseFactor();
    }

    // Letter → could be a function call (SUM, AVERAGE) or a cell reference (A1)
    if (QChar(c).isLetter()) {
        // Save position so we can backtrack if it's not a function call
        const int savedPos = pos;

        // Consume all letters
        while (pos < src.size() && src[pos].isLetter()) ++pos;
        const QString ident = src.mid(savedPos, pos - savedPos);

        // Peek ahead (skipping whitespace) for '('
        const int afterLetters = pos;
        skipWs();

        if (peek() == '(') {
            // It's a function call like SUM(...) or AVERAGE(...)
            return parseFuncCall(ident);
        }

        // Not a function call — backtrack and parse as a cell reference
        pos = savedPos;
        return parseCellRef();
    }

    // Number
    if (QChar(c).isDigit() || c == '.') {
        return parseNumber();
    }

    error = true;
    return 0.0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Parse a numeric literal (integer or decimal)
// ─────────────────────────────────────────────────────────────────────────────
double FormulaEngine::Parser::parseNumber() {
    int start = pos;
    while (pos < src.size() && (src[pos].isDigit() || src[pos] == '.'))
        ++pos;
    if (pos == start) { error = true; return 0.0; }

    bool ok = false;
    const double val = src.mid(start, pos - start).toDouble(&ok);
    if (!ok) { error = true; return 0.0; }
    return val;
}

// ─────────────────────────────────────────────────────────────────────────────
// Parse a cell reference like A1, BC23 and resolve it via lookup
// ─────────────────────────────────────────────────────────────────────────────
double FormulaEngine::Parser::parseCellRef() {
    int start = pos;
    // Consume letters
    while (pos < src.size() && src[pos].isLetter()) ++pos;
    // Consume digits
    while (pos < src.size() && src[pos].isDigit()) ++pos;

    const QString token = src.mid(start, pos - start);

    int col = 0, row = 0;
    if (!FormulaEngine::parseCellRef(token, col, row)) {
        error = true;
        return 0.0;
    }

    // Get the raw cell content via the lookup callback, then recursively evaluate
    const QString raw = lookup(col, row).trimmed();
    if (raw.isEmpty()) return 0.0;

    // Recursive evaluation (handles =A1+B1 where A1 itself is a formula)
    const QString evaled = engine.evaluate(raw, lookup);

    bool ok = false;
    const double val = evaled.toDouble(&ok);
    if (!ok) { error = true; return 0.0; }
    return val;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sprint 9: Resolve a cell range (e.g. "A1" to "A5") into numeric values.
// Non-numeric cells (empty or text) are silently skipped — matching Excel
// behavior where SUM ignores text cells.
// ─────────────────────────────────────────────────────────────────────────────
QVector<double> FormulaEngine::Parser::resolveRange(const QString& startRef,
                                                     const QString& endRef) {
    const QStringList cells = FormulaEngine::expandRange(startRef + ":" + endRef);
    QVector<double> values;
    values.reserve(cells.size());

    for (const QString& addr : cells) {
        int col = 0, row = 0;
        if (!FormulaEngine::parseCellRef(addr, col, row)) continue;

        const QString raw = lookup(col, row).trimmed();
        if (raw.isEmpty()) continue;   // skip empty cells

        // Recursively evaluate (handles formulas within the range)
        const QString evaled = engine.evaluate(raw, lookup);
        if (evaled.startsWith('#')) {
            // Propagate errors (#ERR, #CIRC) up
            error = true;
            return {};
        }

        bool ok = false;
        const double val = evaled.toDouble(&ok);
        if (ok) values.append(val);
        // Non-numeric text cells are silently skipped (Excel behavior)
    }
    return values;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sprint 9: Parse a function call — SUM(range) or AVERAGE(range)
// The function name has already been consumed; pos is sitting just before '('.
// Function names are case-insensitive.
// ─────────────────────────────────────────────────────────────────────────────
double FormulaEngine::Parser::parseFuncCall(const QString& funcName) {
    const QString upperName = funcName.toUpper();

    // Consume '('
    if (!match('(')) { error = true; return 0.0; }

    // Parse the argument — expect a cell range (e.g. A1:A5)
    // Read the start cell reference
    skipWs();
    int refStart = pos;
    while (pos < src.size() && src[pos].isLetter()) ++pos;
    while (pos < src.size() && src[pos].isDigit())  ++pos;
    const QString startRef = src.mid(refStart, pos - refStart);

    // Expect ':'
    skipWs();
    if (!match(':')) { error = true; return 0.0; }

    // Read the end cell reference
    skipWs();
    refStart = pos;
    while (pos < src.size() && src[pos].isLetter()) ++pos;
    while (pos < src.size() && src[pos].isDigit())  ++pos;
    const QString endRef = src.mid(refStart, pos - refStart);

    // Validate both references
    int c1 = 0, r1 = 0, c2 = 0, r2 = 0;
    if (!FormulaEngine::parseCellRef(startRef, c1, r1) ||
        !FormulaEngine::parseCellRef(endRef,   c2, r2)) {
        error = true;
        return 0.0;
    }

    // Resolve the range into numeric values
    const QVector<double> values = resolveRange(startRef, endRef);
    if (error) return 0.0;

    // Consume ')'
    if (!match(')')) { error = true; return 0.0; }

    // ── Dispatch to the requested function ─────────────────────────────
    if (upperName == "SUM") {
        double sum = 0.0;
        for (double v : values) sum += v;
        return sum;
    }

    if (upperName == "AVERAGE") {
        if (values.isEmpty()) {
            error = true;   // No numeric cells → #ERR
            return 0.0;
        }
        double sum = 0.0;
        for (double v : values) sum += v;
        return sum / static_cast<double>(values.size());
    }

    // Unknown function name
    error = true;
    return 0.0;
}

} // namespace NativeOffice

