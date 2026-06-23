// ─────────────────────────────────────────────────────────────────────────────
// FormulaEngine.cpp  (Sprint 4 → Sprint 10)
// Value-based recursive-descent evaluator with a function registry.
//
// Sprint 10 additions:
//   • Value model (number / text / boolean / error) — errors propagate as values
//   • Operators: & (concat), comparisons, ^ (power), unary +/-
//   • Function dispatch for the Priority-1 function set (string / math / logic /
//     totals), each supporting multiple arguments and ranges.
// ─────────────────────────────────────────────────────────────────────────────
#include "FormulaEngine.h"

#include <QStringList>
#include <QChar>
#include <QRegularExpression>
#include <QDate>
#include <QDateTime>
#include <cmath>
#include <algorithm>

namespace NativeOffice {

// ─────────────────────────────────────────────────────────────────────────────
// Static address helpers
// ─────────────────────────────────────────────────────────────────────────────
QString FormulaEngine::colLabel(int col) {
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
    int i = 0;
    while (i < ref.size() && ref[i].isLetter()) ++i;
    if (i == 0 || i >= ref.size()) return false;

    const QString letters = ref.left(i).toUpper();
    int c = 0;
    for (QChar ch : letters)
        c = c * 26 + (ch.toLatin1() - 'A' + 1);
    col = c - 1;

    bool ok = false;
    const int r = ref.mid(i).toInt(&ok);
    if (!ok || r < 1) return false;
    row = r - 1;
    return true;
}

QStringList FormulaEngine::expandRange(const QString& rangeStr) {
    const int colonIdx = rangeStr.indexOf(':');
    if (colonIdx < 0) return {};

    const QString startStr = rangeStr.left(colonIdx).trimmed();
    const QString endStr   = rangeStr.mid(colonIdx + 1).trimmed();

    int col1 = 0, row1 = 0, col2 = 0, row2 = 0;
    if (!parseCellRef(startStr, col1, row1)) return {};
    if (!parseCellRef(endStr,   col2, row2)) return {};

    if (col1 > col2) std::swap(col1, col2);
    if (row1 > row2) std::swap(row1, row2);

    QStringList result;
    result.reserve((col2 - col1 + 1) * (row2 - row1 + 1));
    for (int c = col1; c <= col2; ++c)
        for (int r = row1; r <= row2; ++r)
            result.append(cellAddress(c, r));
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Value coercion helpers
// ─────────────────────────────────────────────────────────────────────────────
double FormulaEngine::toNumber(const Value& v, bool& ok) {
    ok = true;
    switch (v.kind) {
    case Value::Number:
    case Value::Boolean:
        return v.num;
    case Value::Text: {
        if (v.text.isEmpty()) return 0.0;        // empty text → 0
        bool good = false;
        const double d = v.text.toDouble(&good);
        if (good) return d;
        ok = false;
        return 0.0;
    }
    case Value::Error:
    default:
        ok = false;
        return 0.0;
    }
}

QString FormulaEngine::toText(const Value& v) {
    switch (v.kind) {
    case Value::Text:    return v.text;
    case Value::Error:   return v.text;
    case Value::Boolean: return v.num != 0.0 ? "TRUE" : "FALSE";
    case Value::Number:
    default: {
        const double n = v.num;
        if (n == std::floor(n) && std::abs(n) < 1e15)
            return QString::number(static_cast<long long>(n));
        return QString::number(n, 'g', 10);
    }
    }
}

QString FormulaEngine::formatValue(const Value& v) const {
    switch (v.kind) {
    case Value::Error:   return v.text.isEmpty() ? "#ERR" : v.text;
    case Value::Boolean: return v.num != 0.0 ? "TRUE" : "FALSE";
    case Value::Text:    return v.text;
    case Value::Number:
    default: {
        const double n = v.num;
        if (std::isinf(n) || std::isnan(n)) return "#NUM!";
        if (n == std::floor(n) && std::abs(n) < 1e15)
            return QString::number(static_cast<long long>(n));
        return QString::number(n, 'g', 10);
    }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Public entry points
// ─────────────────────────────────────────────────────────────────────────────
QString FormulaEngine::evaluate(const QString&    input,
                                 const CellLookup& lookup) const {
    const Value v = evaluateValue(input, lookup);
    return formatValue(v);
}

FormulaEngine::Value
FormulaEngine::evaluateValue(const QString& input, const CellLookup& lookup) const {
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) return Value::empty();

    // Recursion guard (circular refs / runaway chains).
    if (m_evalDepth > 256) return Value::error("#CIRC");

    // Not a formula → a literal number or text.
    if (!trimmed.startsWith('=')) {
        bool ok = false;
        const double d = trimmed.toDouble(&ok);
        if (ok) return Value::number(d);
        return Value::string(trimmed);
    }

    const QString expr = trimmed.mid(1).trimmed();
    if (expr.isEmpty()) return Value::empty();

    ++m_evalDepth;
    Parser p{ expr, 0, lookup, *this };
    Value result = p.parseExpr();
    p.skipWs();
    if (!p.syntaxError && p.pos < expr.size())
        p.syntaxError = true;          // trailing garbage
    --m_evalDepth;

    if (p.syntaxError) return Value::error("#ERR");
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Parser low-level helpers
// ─────────────────────────────────────────────────────────────────────────────
void FormulaEngine::Parser::skipWs() {
    while (pos < src.size() && src[pos].isSpace()) ++pos;
}
QChar FormulaEngine::Parser::peekCh() const {
    return (pos < src.size()) ? src[pos] : QChar('\0');
}
char FormulaEngine::Parser::peek() const {
    return (pos < src.size()) ? src[pos].toLatin1() : '\0';
}
char FormulaEngine::Parser::consume() {
    return (pos < src.size()) ? src[pos++].toLatin1() : '\0';
}
bool FormulaEngine::Parser::match(char c) {
    skipWs();
    if (peek() == c) { consume(); return true; }
    return false;
}

QString FormulaEngine::Parser::readRefToken() {
    skipWs();
    const int start = pos;
    while (pos < src.size() && src[pos].isLetter()) ++pos;
    const int afterLetters = pos;
    while (pos < src.size() && src[pos].isDigit()) ++pos;
    if (afterLetters == start || pos == afterLetters) {
        pos = start;          // not a valid ref token
        return {};
    }
    return src.mid(start, pos - start);
}

// ─────────────────────────────────────────────────────────────────────────────
// Grammar
// ─────────────────────────────────────────────────────────────────────────────
FormulaEngine::Value FormulaEngine::Parser::parseExpr() {
    return parseConcat();
}

FormulaEngine::Value FormulaEngine::Parser::parseConcat() {
    Value lhs = parseCompare();
    while (!syntaxError) {
        skipWs();
        if (peek() != '&') break;
        consume();
        Value rhs = parseCompare();
        if (lhs.isError()) return lhs;
        if (rhs.isError()) return rhs;
        lhs = Value::string(engine.toText(lhs) + engine.toText(rhs));
    }
    return lhs;
}

FormulaEngine::Value FormulaEngine::Parser::parseCompare() {
    Value lhs = parseAddSub();
    skipWs();
    const char c = peek();
    QString op;
    if (c == '=') { consume(); op = "="; }
    else if (c == '<') {
        consume();
        if (peek() == '>') { consume(); op = "<>"; }
        else if (peek() == '=') { consume(); op = "<="; }
        else op = "<";
    }
    else if (c == '>') {
        consume();
        if (peek() == '=') { consume(); op = ">="; }
        else op = ">";
    }
    if (op.isEmpty()) return lhs;

    Value rhs = parseAddSub();
    if (lhs.isError()) return lhs;
    if (rhs.isError()) return rhs;

    // Numeric comparison when both coerce to numbers; else text comparison.
    bool okL = false, okR = false;
    const double a = FormulaEngine::toNumber(lhs, okL);
    const double b = FormulaEngine::toNumber(rhs, okR);
    int cmp;
    if (okL && okR)
        cmp = (a < b) ? -1 : (a > b ? 1 : 0);
    else
        cmp = QString::compare(engine.toText(lhs), engine.toText(rhs),
                               Qt::CaseInsensitive);

    bool res = false;
    if      (op == "=")  res = (cmp == 0);
    else if (op == "<>") res = (cmp != 0);
    else if (op == "<")  res = (cmp <  0);
    else if (op == "<=") res = (cmp <= 0);
    else if (op == ">")  res = (cmp >  0);
    else if (op == ">=") res = (cmp >= 0);
    return Value::boolean(res);
}

FormulaEngine::Value FormulaEngine::Parser::parseAddSub() {
    Value lhs = parseMulDiv();
    while (!syntaxError) {
        skipWs();
        const char op = peek();
        if (op != '+' && op != '-') break;
        consume();
        Value rhs = parseMulDiv();
        if (lhs.isError()) return lhs;
        if (rhs.isError()) return rhs;
        bool okL = false, okR = false;
        const double a = FormulaEngine::toNumber(lhs, okL);
        const double b = FormulaEngine::toNumber(rhs, okR);
        if (!okL || !okR) return Value::error("#VALUE!");
        lhs = Value::number(op == '+' ? a + b : a - b);
    }
    return lhs;
}

FormulaEngine::Value FormulaEngine::Parser::parseMulDiv() {
    Value lhs = parsePower();
    while (!syntaxError) {
        skipWs();
        const char op = peek();
        if (op != '*' && op != '/') break;
        consume();
        Value rhs = parsePower();
        if (lhs.isError()) return lhs;
        if (rhs.isError()) return rhs;
        bool okL = false, okR = false;
        const double a = FormulaEngine::toNumber(lhs, okL);
        const double b = FormulaEngine::toNumber(rhs, okR);
        if (!okL || !okR) return Value::error("#VALUE!");
        if (op == '/') {
            if (b == 0.0) return Value::error("#DIV/0!");
            lhs = Value::number(a / b);
        } else {
            lhs = Value::number(a * b);
        }
    }
    return lhs;
}

FormulaEngine::Value FormulaEngine::Parser::parsePower() {
    Value lhs = parseUnary();
    skipWs();
    if (peek() == '^') {
        consume();
        Value rhs = parsePower();   // right-associative
        if (lhs.isError()) return lhs;
        if (rhs.isError()) return rhs;
        bool okL = false, okR = false;
        const double a = FormulaEngine::toNumber(lhs, okL);
        const double b = FormulaEngine::toNumber(rhs, okR);
        if (!okL || !okR) return Value::error("#VALUE!");
        return Value::number(std::pow(a, b));
    }
    return lhs;
}

FormulaEngine::Value FormulaEngine::Parser::parseUnary() {
    skipWs();
    const char c = peek();
    if (c == '-' || c == '+') {
        consume();
        Value v = parseUnary();
        if (v.isError()) return v;
        if (c == '+') return v;
        bool ok = false;
        const double n = FormulaEngine::toNumber(v, ok);
        if (!ok) return Value::error("#VALUE!");
        return Value::number(-n);
    }
    return parsePrimary();
}

FormulaEngine::Value FormulaEngine::Parser::parsePrimary() {
    skipWs();
    const char c = peek();

    if (c == '(') {
        consume();
        Value v = parseExpr();
        if (!match(')')) { syntaxError = true; return Value::error("#ERR"); }
        return v;
    }

    if (c == '"')
        return parseString();

    if (QChar(c).isLetter()) {
        // Identifier → function call, TRUE/FALSE, sheet-qualified ref, or cell ref.
        const int saved = pos;
        while (pos < src.size() && (src[pos].isLetterOrNumber() || src[pos] == '_'))
            ++pos;
        const QString word     = src.mid(saved, pos - saved);
        const int     afterWord = pos;

        skipWs();
        const char nx = peek();

        if (nx == '(')
            return parseFuncCall(word.toUpper());

        if (nx == '!') {
            // Sheet-qualified reference: Sheet2!A1
            consume();
            const QString tok = readRefToken();
            if (tok.isEmpty()) { syntaxError = true; return Value::error("#ERR"); }
            return parseRefToValue(word, tok);
        }

        const QString up = word.toUpper();
        if (up == "TRUE")  { pos = afterWord; return Value::boolean(true);  }
        if (up == "FALSE") { pos = afterWord; return Value::boolean(false); }

        // Plain cell reference on the current sheet.
        pos = saved;
        const QString tok = readRefToken();
        if (tok.isEmpty()) { syntaxError = true; return Value::error("#ERR"); }
        return parseRefToValue(QString(), tok);
    }

    if (QChar(c).isDigit() || c == '.')
        return parseNumber();

    syntaxError = true;
    return Value::error("#ERR");
}

FormulaEngine::Value FormulaEngine::Parser::parseNumber() {
    const int start = pos;
    while (pos < src.size() && (src[pos].isDigit() || src[pos] == '.'))
        ++pos;
    // optional scientific notation
    if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
        ++pos;
        if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) ++pos;
        while (pos < src.size() && src[pos].isDigit()) ++pos;
    }
    bool ok = false;
    const double v = src.mid(start, pos - start).toDouble(&ok);
    if (!ok) { syntaxError = true; return Value::error("#ERR"); }
    return Value::number(v);
}

FormulaEngine::Value FormulaEngine::Parser::parseString() {
    consume();   // opening quote
    QString out;
    while (pos < src.size()) {
        const QChar ch = src[pos++];
        if (ch == '"') {
            // "" → escaped quote
            if (pos < src.size() && src[pos] == '"') { out += '"'; ++pos; continue; }
            return Value::string(out);
        }
        out += ch;
    }
    syntaxError = true;   // unterminated string
    return Value::error("#ERR");
}

// Resolve a single cell reference to a Value. 'lookup' returns the already
// evaluated display string, so we parse it directly (no engine recursion).
FormulaEngine::Value
FormulaEngine::Parser::parseRefToValue(const QString& sheet, const QString& token) {
    int col = 0, row = 0;
    if (!FormulaEngine::parseCellRef(token, col, row))
        return Value::error("#REF!");
    const QString val = lookup(sheet, col, row);
    if (val.isEmpty()) return Value::empty();
    if (val.startsWith('#')) return Value::error(val);     // propagate #CIRC, #REF! …
    bool ok = false;
    const double d = val.toDouble(&ok);
    if (ok) return Value::number(d);
    if (val == QLatin1String("TRUE"))  return Value::boolean(true);
    if (val == QLatin1String("FALSE")) return Value::boolean(false);
    return Value::string(val);
}

// Read an optional "Sheet!" qualifier followed by a cell-ref token.
QString FormulaEngine::Parser::readQualifiedRef(QString& sheet) {
    skipWs();
    int p = pos;
    while (p < src.size() && (src[p].isLetterOrNumber() || src[p] == '_')) ++p;
    if (p < src.size() && src[p] == '!') {
        sheet = src.mid(pos, p - pos);
        pos = p + 1;                 // consume "Sheet!"
        return readRefToken();
    }
    sheet.clear();
    return readRefToken();
}

// ─────────────────────────────────────────────────────────────────────────────
// Argument parsing — a (possibly sheet-qualified) range A1:B2, or a scalar expr.
// ─────────────────────────────────────────────────────────────────────────────
FormulaEngine::Arg FormulaEngine::Parser::parseArg() {
    skipWs();
    const int saved = pos;

    // Try to detect a range: [Sheet!]ref ':' ref
    QString sheet;
    const QString first = readQualifiedRef(sheet);
    if (!first.isEmpty()) {
        skipWs();
        if (peek() == ':') {
            consume();
            QString sheet2;
            const QString second = readQualifiedRef(sheet2);  // sheet2 usually empty
            if (!second.isEmpty()) {
                Arg arg;
                arg.isRange = true;
                // Resolve the rectangle so we can build a structured 2D grid
                // (lookups need it) plus the flat non-empty list (aggregates).
                int c1 = 0, r1 = 0, c2 = 0, r2 = 0;
                if (FormulaEngine::parseCellRef(first, c1, r1)
                    && FormulaEngine::parseCellRef(second, c2, r2)) {
                    if (c1 > c2) std::swap(c1, c2);
                    if (r1 > r2) std::swap(r1, r2);
                    auto toVal = [&](const QString& val) -> Value {
                        if (val.isEmpty()) return Value::empty();
                        if (val.startsWith('#')) return Value::error(val);
                        bool ok = false; const double d = val.toDouble(&ok);
                        return ok ? Value::number(d) : Value::string(val);
                    };
                    for (int r = r1; r <= r2; ++r) {
                        QVector<Value> rowVals;
                        for (int c = c1; c <= c2; ++c) {
                            const Value v = toVal(lookup(sheet, c, r));
                            rowVals.push_back(v);
                            if (!(v.kind == Value::Text && v.text.isEmpty()))
                                arg.values.push_back(v);   // flat list skips empties
                        }
                        arg.grid.push_back(rowVals);
                    }
                }
                return arg;
            }
        }
    }

    // Not a range → scalar expression.
    pos = saved;
    Arg arg;
    arg.isRange = false;
    arg.values.push_back(parseExpr());
    return arg;
}

FormulaEngine::Value FormulaEngine::Parser::parseFuncCall(const QString& name) {
    if (!match('(')) { syntaxError = true; return Value::error("#ERR"); }

    QVector<Arg> args;
    skipWs();
    if (peek() != ')') {
        for (;;) {
            args.push_back(parseArg());
            if (syntaxError) return Value::error("#ERR");
            skipWs();
            if (peek() == ',') { consume(); continue; }
            break;
        }
    }
    if (!match(')')) { syntaxError = true; return Value::error("#ERR"); }

    return FormulaEngine::callFunction(name, args);
}

// ─────────────────────────────────────────────────────────────────────────────
// Function registry / dispatch
//
// Value / Arg are private nested types, so the dispatch is a static member with
// access to them. A flat if-chain keeps the table readable and easy to extend.
// ─────────────────────────────────────────────────────────────────────────────
FormulaEngine::Value
FormulaEngine::callFunction(const QString& fn, QVector<Arg>& args) {
    auto firstScalar = [&](int i) -> Value {
        if (i < args.size() && !args[i].values.isEmpty())
            return args[i].values[0];
        return Value::empty();
    };

    // Collect every value across all args (ranges flattened).
    auto allValues = [&]() -> QVector<Value> {
        QVector<Value> out;
        for (const Arg& a : args)
            for (const Value& v : a.values)
                out.push_back(v);
        return out;
    };
    // Collect numeric values only (skip text/empty); propagate first error.
    auto numbers = [&](bool& hadError, Value& err) -> QVector<double> {
        QVector<double> out;
        hadError = false;
        for (const Arg& a : args)
            for (const Value& v : a.values) {
                if (v.isError()) { hadError = true; err = v; return {}; }
                if (v.kind == Value::Text) {
                    if (v.text.isEmpty()) continue;
                    bool ok = false; const double d = v.text.toDouble(&ok);
                    if (ok) out.push_back(d);
                    // non-numeric text skipped (Excel behaviour for SUM etc.)
                } else {
                    out.push_back(v.num);  // Number or Boolean
                }
            }
        return out;
    };

    auto reqNum = [&](const Value& v, bool& ok) { return FormulaEngine::toNumber(v, ok); };

    // ── Totals ────────────────────────────────────────────────────────────
    if (fn == "SUM") {
        bool e=false; Value err; const auto v = numbers(e, err);
        if (e) return err;
        double s=0; for (double x : v) s += x; return Value::number(s);
    }
    if (fn == "AVERAGE") {
        bool e=false; Value err; const auto v = numbers(e, err);
        if (e) return err;
        if (v.isEmpty()) return Value::error("#DIV/0!");
        double s=0; for (double x : v) s += x; return Value::number(s / v.size());
    }
    if (fn == "SUBTOTAL") {
        // SUBTOTAL(function_num, range...) — support 9=SUM,1=AVERAGE,2=COUNT,
        // 4=MAX,5=MIN as the common cases.
        if (args.isEmpty()) return Value::error("#VALUE!");
        bool ok=false; const int code = (int)reqNum(firstScalar(0), ok);
        if (!ok) return Value::error("#VALUE!");
        QVector<Value> rest; // values after first arg
        for (int i=1;i<args.size();++i) for (const Value& v: args[i].values) rest.push_back(v);
        QVector<double> nums; for (const Value& v: rest) {
            if (v.isError()) return v;
            bool g=false; double d=FormulaEngine::toNumber(v,g); if (g && v.kind!=Value::Text) nums.push_back(d);
            else if (v.kind==Value::Text && !v.text.isEmpty()) { bool gg=false; double dd=v.text.toDouble(&gg); if (gg) nums.push_back(dd); }
        }
        const int c = code > 100 ? code - 100 : code;
        if (c==9||c==1||c==2||c==4||c==5) {
            if (c==2) return Value::number(nums.size());
            if (nums.isEmpty()) return c==1 ? Value::error("#DIV/0!") : Value::number(0);
            if (c==9){double s=0;for(double x:nums)s+=x;return Value::number(s);}
            if (c==1){double s=0;for(double x:nums)s+=x;return Value::number(s/nums.size());}
            if (c==4) return Value::number(*std::max_element(nums.begin(),nums.end()));
            if (c==5) return Value::number(*std::min_element(nums.begin(),nums.end()));
        }
        return Value::error("#VALUE!");
    }

    // ── Math ──────────────────────────────────────────────────────────────
    if (fn == "MIN" || fn == "MAX") {
        bool e=false; Value err; const auto v = numbers(e, err);
        if (e) return err;
        if (v.isEmpty()) return Value::number(0);
        double r = v[0];
        for (double x : v) r = (fn=="MIN") ? std::min(r,x) : std::max(r,x);
        return Value::number(r);
    }
    if (fn == "COUNT") {
        // count numeric values
        int n=0;
        for (const Arg& a : args) for (const Value& v : a.values) {
            if (v.kind==Value::Number || v.kind==Value::Boolean) ++n;
            else if (v.kind==Value::Text && !v.text.isEmpty()) { bool ok=false; v.text.toDouble(&ok); if (ok) ++n; }
        }
        return Value::number(n);
    }
    if (fn == "COUNTA") {
        int n=0;
        for (const Arg& a : args) for (const Value& v : a.values) {
            if (v.kind==Value::Text && v.text.isEmpty()) continue;   // empty
            ++n;
        }
        return Value::number(n);
    }
    if (fn == "COUNTIF") {
        if (args.size() < 2) return Value::error("#VALUE!");
        const Value crit = firstScalar(1);
        const QString cs = FormulaEngine::toText(crit).trimmed();
        // Parse optional leading operator
        QString op = "="; QString rhs = cs;
        for (const QString& o : {QStringLiteral(">="),QStringLiteral("<="),QStringLiteral("<>"),QStringLiteral(">"),QStringLiteral("<"),QStringLiteral("=")}) {
            if (cs.startsWith(o)) { op=o; rhs=cs.mid(o.size()).trimmed(); break; }
        }
        bool rhsNum=false; const double rn = rhs.toDouble(&rhsNum);
        int n=0;
        for (const Value& v : args[0].values) {
            if (v.kind==Value::Text && v.text.isEmpty()) continue;
            bool isNum=false; double vn=0;
            if (v.kind==Value::Number||v.kind==Value::Boolean){isNum=true;vn=v.num;}
            else { vn=v.text.toDouble(&isNum); }
            bool match=false;
            if (rhsNum && isNum) {
                if (op=="=") match=(vn==rn); else if(op=="<>")match=(vn!=rn);
                else if(op==">")match=(vn>rn); else if(op=="<")match=(vn<rn);
                else if(op==">=")match=(vn>=rn); else if(op=="<=")match=(vn<=rn);
            } else {
                const int cmp = QString::compare(FormulaEngine::toText(v), rhs, Qt::CaseInsensitive);
                if (op=="=") match=(cmp==0); else if(op=="<>")match=(cmp!=0);
                else if(op==">")match=(cmp>0); else if(op=="<")match=(cmp<0);
                else if(op==">=")match=(cmp>=0); else if(op=="<=")match=(cmp<=0);
            }
            if (match) ++n;
        }
        return Value::number(n);
    }
    if (fn == "MOD") {
        if (args.size()<2) return Value::error("#VALUE!");
        bool a=false,b=false; double x=reqNum(firstScalar(0),a), y=reqNum(firstScalar(1),b);
        if(!a||!b) return Value::error("#VALUE!");
        if (y==0.0) return Value::error("#DIV/0!");
        return Value::number(x - y*std::floor(x/y));
    }
    if (fn == "ABS")  { bool ok=false; double x=reqNum(firstScalar(0),ok); return ok?Value::number(std::abs(x)):Value::error("#VALUE!"); }
    if (fn == "SQRT") { bool ok=false; double x=reqNum(firstScalar(0),ok); if(!ok)return Value::error("#VALUE!"); if(x<0)return Value::error("#NUM!"); return Value::number(std::sqrt(x)); }
    if (fn == "POWER"){ if(args.size()<2)return Value::error("#VALUE!"); bool a=false,b=false; double x=reqNum(firstScalar(0),a),y=reqNum(firstScalar(1),b); return (a&&b)?Value::number(std::pow(x,y)):Value::error("#VALUE!"); }
    if (fn == "ROUND"){
        if(args.isEmpty())return Value::error("#VALUE!");
        bool a=false; double x=reqNum(firstScalar(0),a); if(!a)return Value::error("#VALUE!");
        int digits=0; if(args.size()>=2){bool b=false;digits=(int)reqNum(firstScalar(1),b);}
        const double f=std::pow(10.0,digits);
        return Value::number(std::round(x*f)/f);
    }

    // ── Logic ───────────────────────────────────────────────────────────────
    if (fn == "IF") {
        if (args.size() < 2) return Value::error("#VALUE!");
        const Value cond = firstScalar(0);
        if (cond.isError()) return cond;
        bool ok=false; const double c = FormulaEngine::toNumber(cond, ok);
        const bool truth = ok ? (c != 0.0) : false;
        if (truth) return firstScalar(1);
        return args.size() >= 3 ? firstScalar(2) : Value::boolean(false);
    }
    if (fn == "AND" || fn == "OR") {
        const auto vals = allValues();
        if (vals.isEmpty()) return Value::error("#VALUE!");
        bool acc = (fn == "AND");
        for (const Value& v : vals) {
            if (v.isError()) return v;
            bool ok=false; const double n = FormulaEngine::toNumber(v, ok);
            if (!ok) continue;
            const bool b = (n != 0.0);
            acc = (fn=="AND") ? (acc && b) : (acc || b);
        }
        return Value::boolean(acc);
    }
    if (fn == "NOT") {
        const Value v = firstScalar(0);
        if (v.isError()) return v;
        bool ok=false; const double n = FormulaEngine::toNumber(v, ok);
        if (!ok) return Value::error("#VALUE!");
        return Value::boolean(n == 0.0);
    }
    if (fn == "IFERROR") {
        if (args.isEmpty()) return Value::error("#VALUE!");
        const Value v = firstScalar(0);
        if (v.isError()) return args.size() >= 2 ? firstScalar(1) : Value::empty();
        return v;
    }

    // ── String ────────────────────────────────────────────────────────────
    if (fn == "CONCATENATE" || fn == "CONCAT") {
        QString out;
        for (const Arg& a : args) for (const Value& v : a.values) {
            if (v.isError()) return v;
            out += FormulaEngine::toText(v);
        }
        return Value::string(out);
    }
    if (fn == "LEN") {
        return Value::number(FormulaEngine::toText(firstScalar(0)).size());
    }
    if (fn == "UPPER") return Value::string(FormulaEngine::toText(firstScalar(0)).toUpper());
    if (fn == "LOWER") return Value::string(FormulaEngine::toText(firstScalar(0)).toLower());
    if (fn == "TRIM")  return Value::string(FormulaEngine::toText(firstScalar(0)).simplified());
    if (fn == "LEFT" || fn == "RIGHT") {
        const QString s = FormulaEngine::toText(firstScalar(0));
        int n = 1;
        if (args.size() >= 2) { bool ok=false; n=(int)reqNum(firstScalar(1),ok); if(!ok)return Value::error("#VALUE!"); }
        if (n < 0) return Value::error("#VALUE!");
        return Value::string(fn=="LEFT" ? s.left(n) : s.right(n));
    }
    if (fn == "MID") {
        const QString s = FormulaEngine::toText(firstScalar(0));
        if (args.size()<3) return Value::error("#VALUE!");
        bool a=false,b=false; int start=(int)reqNum(firstScalar(1),a); int len=(int)reqNum(firstScalar(2),b);
        if(!a||!b||start<1||len<0) return Value::error("#VALUE!");
        return Value::string(s.mid(start-1, len));
    }
    if (fn == "FIND" || fn == "SEARCH") {
        if (args.size()<2) return Value::error("#VALUE!");
        const QString needle = FormulaEngine::toText(firstScalar(0));
        const QString hay    = FormulaEngine::toText(firstScalar(1));
        int from = 1;
        if (args.size()>=3){bool ok=false;from=(int)reqNum(firstScalar(2),ok);}
        if (from < 1) return Value::error("#VALUE!");
        const auto cs = (fn=="FIND") ? Qt::CaseSensitive : Qt::CaseInsensitive;
        const int idx = hay.indexOf(needle, from-1, cs);
        if (idx < 0) return Value::error("#VALUE!");
        return Value::number(idx + 1);
    }
    if (fn == "SUBSTITUTE") {
        if (args.size()<3) return Value::error("#VALUE!");
        QString s   = FormulaEngine::toText(firstScalar(0));
        const QString oldT = FormulaEngine::toText(firstScalar(1));
        const QString newT = FormulaEngine::toText(firstScalar(2));
        if (oldT.isEmpty()) return Value::string(s);
        if (args.size() >= 4) {
            bool ok=false; const int which = (int)FormulaEngine::toNumber(firstScalar(3), ok);
            if (ok && which >= 1) {
                int occ = 0, idx = 0;
                while ((idx = s.indexOf(oldT, idx)) >= 0) {
                    if (++occ == which) { s.replace(idx, oldT.size(), newT); break; }
                    idx += oldT.size();
                }
                return Value::string(s);
            }
        }
        return Value::string(s.replace(oldT, newT));
    }

    // ── Date & time (Excel serial: days since 1899-12-30) ──────────────────────
    const QDate kEpoch(1899, 12, 30);
    auto serialToDate = [&](double s) { return kEpoch.addDays(static_cast<qint64>(std::floor(s))); };
    if (fn == "TODAY") {
        return Value::number(kEpoch.daysTo(QDate::currentDate()));
    }
    if (fn == "NOW") {
        const QDateTime n = QDateTime::currentDateTime();
        const double days = kEpoch.daysTo(n.date());
        const double frac = n.time().msecsSinceStartOfDay() / 86400000.0;
        return Value::number(days + frac);
    }
    if (fn == "DATE") {
        if (args.size() < 3) return Value::error("#VALUE!");
        bool a=false,b=false,c=false;
        const int y=(int)reqNum(firstScalar(0),a), m=(int)reqNum(firstScalar(1),b), d=(int)reqNum(firstScalar(2),c);
        if (!a||!b||!c) return Value::error("#VALUE!");
        QDate dt(y < 1900 ? y + 1900 : y, 1, 1);
        dt = dt.addMonths(m - 1).addDays(d - 1);
        if (!dt.isValid()) return Value::error("#NUM!");
        return Value::number(kEpoch.daysTo(dt));
    }
    if (fn == "YEAR" || fn == "MONTH" || fn == "DAY") {
        bool ok=false; const double s = reqNum(firstScalar(0), ok);
        if (!ok) return Value::error("#VALUE!");
        const QDate d = serialToDate(s);
        if (!d.isValid()) return Value::error("#NUM!");
        if (fn == "YEAR")  return Value::number(d.year());
        if (fn == "MONTH") return Value::number(d.month());
        return Value::number(d.day());
    }
    if (fn == "WEEKDAY") {
        bool ok=false; const double s = reqNum(firstScalar(0), ok);
        if (!ok) return Value::error("#VALUE!");
        const QDate d = serialToDate(s);
        if (!d.isValid()) return Value::error("#NUM!");
        return Value::number(d.dayOfWeek() % 7 + 1);   // 1 = Sunday (Excel default)
    }

    // ── Lookup & reference ─────────────────────────────────────────────────────
    auto valuesEqual = [](const Value& a, const Value& b) -> bool {
        const bool an = (a.kind == Value::Number || a.kind == Value::Boolean);
        const bool bn = (b.kind == Value::Number || b.kind == Value::Boolean);
        if (an && bn) return a.num == b.num;
        return QString::compare(FormulaEngine::toText(a), FormulaEngine::toText(b),
                                Qt::CaseInsensitive) == 0;
    };
    if (fn == "VLOOKUP" || fn == "HLOOKUP") {
        if (args.size() < 3) return Value::error("#VALUE!");
        const Value key = firstScalar(0);
        const Arg& tbl = args[1];
        if (tbl.grid.isEmpty() || tbl.grid[0].isEmpty()) return Value::error("#N/A");
        bool ok=false; const int idx = (int)reqNum(firstScalar(2), ok);
        if (!ok || idx < 1) return Value::error("#VALUE!");
        const int rows = tbl.grid.size(), cols = tbl.grid[0].size();
        if (fn == "VLOOKUP") {
            if (idx > cols) return Value::error("#REF!");
            for (int r = 0; r < rows; ++r)
                if (valuesEqual(tbl.grid[r][0], key)) return tbl.grid[r][idx - 1];
        } else {
            if (idx > rows) return Value::error("#REF!");
            for (int c = 0; c < cols; ++c)
                if (valuesEqual(tbl.grid[0][c], key)) return tbl.grid[idx - 1][c];
        }
        return Value::error("#N/A");
    }
    if (fn == "INDEX") {
        if (args.size() < 2) return Value::error("#VALUE!");
        const Arg& a = args[0];
        if (a.grid.isEmpty()) return Value::error("#REF!");
        bool ok=false; const int rr = (int)reqNum(firstScalar(1), ok);
        if (!ok) return Value::error("#VALUE!");
        int cc = 1;
        if (args.size() >= 3) { bool o=false; cc = (int)reqNum(firstScalar(2), o); }
        // Single-row array: a lone index selects the column.
        if (a.grid.size() == 1 && args.size() < 3) { cc = rr; }
        const int rIdx = (a.grid.size() == 1 && args.size() < 3) ? 1 : rr;
        if (rIdx < 1 || rIdx > a.grid.size()) return Value::error("#REF!");
        const auto& row = a.grid[rIdx - 1];
        if (cc < 1 || cc > row.size()) return Value::error("#REF!");
        return row[cc - 1];
    }
    if (fn == "MATCH") {
        if (args.size() < 2) return Value::error("#VALUE!");
        const Value key = firstScalar(0);
        const Arg& a = args[1];
        if (a.grid.isEmpty()) return Value::error("#N/A");
        int pos = 0;
        for (const auto& row : a.grid)
            for (const Value& v : row) {
                ++pos;
                if (valuesEqual(v, key)) return Value::number(pos);
            }
        return Value::error("#N/A");
    }

    // ── More math / stats ──────────────────────────────────────────────────────
    if (fn == "PRODUCT") {
        bool e=false; Value err; const auto v = numbers(e, err);
        if (e) return err;
        if (v.isEmpty()) return Value::number(0);
        double p = 1; for (double x : v) p *= x; return Value::number(p);
    }
    if (fn == "INT") {
        bool ok=false; const double x = reqNum(firstScalar(0), ok);
        return ok ? Value::number(std::floor(x)) : Value::error("#VALUE!");
    }
    if (fn == "ROUNDUP" || fn == "ROUNDDOWN") {
        bool ok=false; const double x = reqNum(firstScalar(0), ok);
        if (!ok) return Value::error("#VALUE!");
        int digits = 0;
        if (args.size() >= 2) { bool o=false; digits = (int)reqNum(firstScalar(1), o); }
        const double f = std::pow(10.0, digits);
        const double y = x * f;
        const double r = (fn == "ROUNDUP")
            ? (y < 0 ? std::floor(y) : std::ceil(y))
            : (y < 0 ? std::ceil(y)  : std::floor(y));
        return Value::number(r / f);
    }
    if (fn == "SUMIF") {
        if (args.size() < 2) return Value::error("#VALUE!");
        const QString cs = FormulaEngine::toText(firstScalar(1)).trimmed();
        QString op = "="; QString rhs = cs;
        for (const QString& o : {QStringLiteral(">="),QStringLiteral("<="),QStringLiteral("<>"),
                                 QStringLiteral(">"),QStringLiteral("<"),QStringLiteral("=")}) {
            if (cs.startsWith(o)) { op=o; rhs=cs.mid(o.size()).trimmed(); break; }
        }
        bool rhsNum=false; const double rn = rhs.toDouble(&rhsNum);
        auto flatAll = [](const Arg& a) -> QVector<Value> {
            QVector<Value> f;
            if (!a.grid.isEmpty()) { for (const auto& r : a.grid) for (const Value& v : r) f.push_back(v); }
            else f = a.values;
            return f;
        };
        const QVector<Value> crange = flatAll(args[0]);
        const QVector<Value> srange = (args.size() >= 3) ? flatAll(args[2]) : crange;
        double sum = 0;
        for (int i = 0; i < crange.size(); ++i) {
            const Value& v = crange[i];
            bool isNum=false; double vn=0;
            if (v.kind==Value::Number||v.kind==Value::Boolean) { isNum=true; vn=v.num; }
            else { vn = v.text.toDouble(&isNum); }
            bool match=false;
            if (rhsNum && isNum) {
                if (op=="=") match=(vn==rn); else if(op=="<>")match=(vn!=rn);
                else if(op==">")match=(vn>rn); else if(op=="<")match=(vn<rn);
                else if(op==">=")match=(vn>=rn); else if(op=="<=")match=(vn<=rn);
            } else {
                const int cmp = QString::compare(FormulaEngine::toText(v), rhs, Qt::CaseInsensitive);
                if (op=="=") match=(cmp==0); else if(op=="<>")match=(cmp!=0);
                else if(op==">")match=(cmp>0); else if(op=="<")match=(cmp<0);
                else if(op==">=")match=(cmp>=0); else if(op=="<=")match=(cmp<=0);
            }
            if (match && i < srange.size()) {
                const Value& sv = srange[i];
                bool sn=false; double sd=0;
                if (sv.kind==Value::Number||sv.kind==Value::Boolean) { sn=true; sd=sv.num; }
                else { sd = sv.text.toDouble(&sn); }
                if (sn) sum += sd;
            }
        }
        return Value::number(sum);
    }

    return Value::error("#NAME?");
}

} // namespace NativeOffice
