// ─────────────────────────────────────────────────────────────────────────────
// FormulaEngine.cpp  (Sprint 4)
// Recursive-descent arithmetic evaluator for NativeOffice Calc.
// ─────────────────────────────────────────────────────────────────────────────
#include "FormulaEngine.h"

#include <QStringList>
#include <QChar>
#include <cmath>
#include <stdexcept>

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
// Grammar:  factor = number | cellRef | '(' expr ')'
//                  | unary '-' factor
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

    // Cell reference: starts with a letter
    if (QChar(c).isLetter()) {
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

} // namespace NativeOffice
