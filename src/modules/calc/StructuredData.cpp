// ─────────────────────────────────────────────────────────────────────────────
// StructuredData.cpp — see StructuredData.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "StructuredData.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QHash>
#include <QSet>

namespace NativeOffice {
namespace StructuredData {

namespace {

// ── Scalar text -> JSON value ───────────────────────────────────────────────
// Order matters: empty and the null spellings first, then bools, then numbers,
// then give up and call it a string. Integers are kept integral so a row of ids
// does not come back out as 1.0.
QJsonValue scalarFromText(const QString& raw) {
    const QString s = raw.trimmed();
    if (s.isEmpty()) return QJsonValue();
    if (s == QLatin1String("~") || s.compare(QLatin1String("null"), Qt::CaseInsensitive) == 0)
        return QJsonValue();
    if (s.compare(QLatin1String("true"),  Qt::CaseInsensitive) == 0) return true;
    if (s.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0) return false;

    bool ok = false;
    const qlonglong i = s.toLongLong(&ok);
    // Guard the round trip: "007" and "1e5" parse as numbers but must stay text,
    // otherwise a zip code or a part number silently loses its shape.
    if (ok && QString::number(i) == s) return QJsonValue(static_cast<double>(i));
    const double d = s.toDouble(&ok);
    if (ok && QString::number(d) == s) return QJsonValue(d);

    return s;
}

// ── JSON value -> display text ──────────────────────────────────────────────
// What lands in a cell. Doubles drop a trailing ".0" so integers read as
// integers in the grid.
QString textFromScalar(const QJsonValue& v) {
    switch (v.type()) {
        case QJsonValue::Null:   return QString();
        case QJsonValue::Bool:   return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        case QJsonValue::String: return v.toString();
        case QJsonValue::Double: {
            const double d = v.toDouble();
            if (d == static_cast<qlonglong>(d) && qAbs(d) < 1e15)
                return QString::number(static_cast<qlonglong>(d));
            return QString::number(d, 'g', 15);
        }
        default: return QString();
    }
}

// ── Flattening ──────────────────────────────────────────────────────────────
// Walks one record and writes leaf values into `out`, recording newly seen
// column names in `order` so the header keeps input order rather than the
// arbitrary order of a hash.
// `seen` spans every record, `out` is per-record. Both are needed: testing
// membership against `out` alone would re-append a column for each record that
// carries it, giving a header of a|b|a|b|a|b.
void flattenInto(const QJsonValue& v, const QString& prefix,
                 QHash<QString, QString>& out, QStringList& order, QSet<QString>& seen) {
    auto note = [&order, &seen](const QString& key) {
        if (!seen.contains(key)) { seen.insert(key); order.append(key); }
    };

    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) {
            const QString key = prefix.isEmpty() ? it.key() : prefix + QLatin1Char('.') + it.key();
            flattenInto(it.value(), key, out, order, seen);
        }
        // An empty object still deserves its column, otherwise the field
        // vanishes from the header entirely.
        if (o.isEmpty() && !prefix.isEmpty()) { note(prefix); out.insert(prefix, QString()); }
        return;
    }
    if (v.isArray()) {
        const QJsonArray a = v.toArray();
        for (int i = 0; i < a.size(); ++i) {
            const QString key = prefix.isEmpty() ? QString::number(i)
                                                 : prefix + QLatin1Char('.') + QString::number(i);
            flattenInto(a.at(i), key, out, order, seen);
        }
        if (a.isEmpty() && !prefix.isEmpty()) { note(prefix); out.insert(prefix, QString()); }
        return;
    }
    // Leaf. A scalar at the very root has no name, so it gets one.
    const QString key = prefix.isEmpty() ? QStringLiteral("value") : prefix;
    note(key);
    out.insert(key, textFromScalar(v));
}

// Turn a parsed document into a Table.
Table tableFromJson(const QJsonValue& root) {
    QVector<QJsonValue> records;
    if (root.isArray()) {
        const QJsonArray a = root.toArray();
        records.reserve(a.size());
        for (const QJsonValue& v : a) records.append(v);
    } else {
        records.append(root);          // single object (or bare scalar) = one row
    }

    QStringList     order;                               // column names
    QSet<QString>   seen;                                // guards against repeats
    QVector<QHash<QString, QString>> flatRows;
    flatRows.reserve(records.size());
    for (const QJsonValue& rec : records) {
        QHash<QString, QString> flat;
        flattenInto(rec, QString(), flat, order, seen);
        flatRows.append(flat);
    }

    Table t;
    t.headers = order;
    t.rows.reserve(flatRows.size());
    for (const QHash<QString, QString>& flat : flatRows) {
        QStringList row;
        row.reserve(order.size());
        // Padded against the union of columns, so a record missing a field
        // leaves a blank cell instead of shifting every later column left.
        for (const QString& key : order) row.append(flat.value(key));
        t.rows.append(row);
    }
    return t;
}

// ═════════════════════════════════════════════════════════════════════════════
// Minimal YAML subset parser
// ═════════════════════════════════════════════════════════════════════════════
// SUPPORTED
//   key: value                    mappings, nested by indentation
//   - item                        block sequences (scalars, maps, nested)
//   - key: value                  sequences of maps, including the compact form
//                                 where the first key sits on the dash line
//   "quoted" / 'quoted'           quoted scalars, with \" \\ \n \t in double
//   [a, b, c]  /  {a: 1, b: 2}    flow sequences and mappings, nestable
//   # comment                     to end of line, unless inside quotes
//   ---  /  ...                   document markers (skipped, first doc only)
//   true/false, null, ~, numbers  recognised and typed
//
// NOT SUPPORTED (deliberately; these are what yaml-cpp would cost ~1MB for)
//   anchors and aliases           &anchor  *alias  <<: merge keys
//   tags                          !!str  !!int  !Custom
//   block scalars                 |  >  and their chomping indicators
//   multiple documents            everything after the second --- is ignored
//   complex/explicit keys         ? key
//   multi-line plain scalars      a value continued on the next line
//   date/timestamp typing         left as strings, which Calc formats anyway
//
// Unsupported syntax is not silently mangled: anchors, tags and block scalars
// are reported as errors with a line number so the user knows why, rather than
// getting a table full of junk.

struct YamlError {
    QString message;
    int     line { -1 };
};

// One physical line, pre-stripped of its comment, with its indent measured.
struct YamlLine {
    int     indent { 0 };
    QString text;
    int     number { 0 };   // 1-based, for error messages
};

// Strip an unquoted trailing comment. Quote state has to be tracked or a '#'
// inside a value ("colour: #ff0000") would truncate it.
QString stripComment(const QString& s) {
    bool inS = false, inD = false;
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('\'') && !inD) inS = !inS;
        else if (c == QLatin1Char('"') && !inS) inD = !inD;
        else if (c == QLatin1Char('#') && !inS && !inD) {
            // Only a comment when preceded by whitespace or at line start.
            if (i == 0 || s.at(i - 1).isSpace()) return s.left(i);
        }
    }
    return s;
}

// Unquote and unescape a scalar token.
QString unquote(const QString& raw) {
    const QString s = raw.trimmed();
    if (s.size() >= 2) {
        if (s.startsWith(QLatin1Char('\'')) && s.endsWith(QLatin1Char('\''))) {
            // Single quotes: the only escape is '' for a literal quote.
            return s.mid(1, s.size() - 2).replace(QLatin1String("''"), QLatin1String("'"));
        }
        if (s.startsWith(QLatin1Char('"')) && s.endsWith(QLatin1Char('"'))) {
            const QString body = s.mid(1, s.size() - 2);
            QString out;
            out.reserve(body.size());
            for (int i = 0; i < body.size(); ++i) {
                if (body.at(i) == QLatin1Char('\\') && i + 1 < body.size()) {
                    const QChar n = body.at(++i);
                    if      (n == QLatin1Char('n')) out += QLatin1Char('\n');
                    else if (n == QLatin1Char('t')) out += QLatin1Char('\t');
                    // Anything else is passed through, which covers an escaped
                    // quote and an escaped backslash.
                    else                            out += n;
                } else {
                    out += body.at(i);
                }
            }
            return out;
        }
    }
    return s;
}

bool isQuoted(const QString& s) {
    const QString t = s.trimmed();
    return t.size() >= 2
        && ((t.startsWith(QLatin1Char('"'))  && t.endsWith(QLatin1Char('"')))
         || (t.startsWith(QLatin1Char('\'')) && t.endsWith(QLatin1Char('\''))));
}

// A scalar token becomes a typed value, except when it was quoted, where the
// user has explicitly said "this is a string".
QJsonValue yamlScalar(const QString& raw) {
    if (isQuoted(raw)) return unquote(raw);
    return scalarFromText(raw);
}

// ── Flow style ("[1, 2]" / "{a: 1}") ────────────────────────────────────────
// Hand-scanned rather than handed to QJsonDocument, because YAML flow allows
// unquoted keys and values that are not legal JSON. `pos` walks the string.
QJsonValue parseFlow(const QString& s, int& pos, YamlError& err);

void skipFlowSpace(const QString& s, int& pos) {
    while (pos < s.size() && s.at(pos).isSpace()) ++pos;
}

// Read one flow token up to a delimiter at this nesting level.
QString readFlowToken(const QString& s, int& pos) {
    skipFlowSpace(s, pos);
    const int start = pos;
    bool inS = false, inD = false;
    while (pos < s.size()) {
        const QChar c = s.at(pos);
        if (c == QLatin1Char('\'') && !inD) inS = !inS;
        else if (c == QLatin1Char('"') && !inS) inD = !inD;
        else if (!inS && !inD) {
            if (c == QLatin1Char(',') || c == QLatin1Char(']') || c == QLatin1Char('}')
                || c == QLatin1Char(':'))
                break;
        }
        ++pos;
    }
    return s.mid(start, pos - start).trimmed();
}

QJsonValue parseFlowValue(const QString& s, int& pos, YamlError& err) {
    skipFlowSpace(s, pos);
    if (pos < s.size() && (s.at(pos) == QLatin1Char('[') || s.at(pos) == QLatin1Char('{')))
        return parseFlow(s, pos, err);
    return yamlScalar(readFlowToken(s, pos));
}

QJsonValue parseFlow(const QString& s, int& pos, YamlError& err) {
    skipFlowSpace(s, pos);
    if (pos >= s.size()) { err.message = QStringLiteral("Unexpected end of flow value."); return {}; }

    const QChar open = s.at(pos);
    if (open == QLatin1Char('[')) {
        ++pos;
        QJsonArray arr;
        skipFlowSpace(s, pos);
        if (pos < s.size() && s.at(pos) == QLatin1Char(']')) { ++pos; return arr; }
        while (pos < s.size()) {
            arr.append(parseFlowValue(s, pos, err));
            if (!err.message.isEmpty()) return {};
            skipFlowSpace(s, pos);
            if (pos >= s.size()) break;
            if (s.at(pos) == QLatin1Char(',')) { ++pos; continue; }
            if (s.at(pos) == QLatin1Char(']')) { ++pos; return arr; }
            break;
        }
        err.message = QStringLiteral("Unterminated flow sequence: missing ']'.");
        return {};
    }

    if (open == QLatin1Char('{')) {
        ++pos;
        QJsonObject obj;
        skipFlowSpace(s, pos);
        if (pos < s.size() && s.at(pos) == QLatin1Char('}')) { ++pos; return obj; }
        while (pos < s.size()) {
            const QString key = unquote(readFlowToken(s, pos));
            skipFlowSpace(s, pos);
            if (pos < s.size() && s.at(pos) == QLatin1Char(':')) ++pos;
            obj.insert(key, parseFlowValue(s, pos, err));
            if (!err.message.isEmpty()) return {};
            skipFlowSpace(s, pos);
            if (pos >= s.size()) break;
            if (s.at(pos) == QLatin1Char(',')) { ++pos; continue; }
            if (s.at(pos) == QLatin1Char('}')) { ++pos; return obj; }
            break;
        }
        err.message = QStringLiteral("Unterminated flow mapping: missing '}'.");
        return {};
    }

    err.message = QStringLiteral("Expected '[' or '{'.");
    return {};
}

// Parse a value that appeared to the right of "key:" or "-".
QJsonValue parseInlineValue(const QString& raw, YamlError& err, int lineNo) {
    const QString s = raw.trimmed();
    if (s.isEmpty()) return {};

    if (s.startsWith(QLatin1Char('&')) || s.startsWith(QLatin1Char('*'))) {
        err.message = QStringLiteral("Anchors and aliases (& and *) are not supported.");
        err.line = lineNo;
        return {};
    }
    if (s.startsWith(QLatin1Char('!'))) {
        err.message = QStringLiteral("Tags (!type) are not supported.");
        err.line = lineNo;
        return {};
    }
    if (s == QLatin1String("|") || s == QLatin1String(">")
        || s.startsWith(QLatin1String("|-")) || s.startsWith(QLatin1String(">-"))) {
        err.message = QStringLiteral("Block scalars (| and >) are not supported.");
        err.line = lineNo;
        return {};
    }

    if (s.startsWith(QLatin1Char('[')) || s.startsWith(QLatin1Char('{'))) {
        int pos = 0;
        const QJsonValue v = parseFlow(s, pos, err);
        if (!err.message.isEmpty() && err.line < 0) err.line = lineNo;
        return v;
    }
    return yamlScalar(s);
}

// Split "key: value" at the first structural colon. Returns -1 when the line is
// not a mapping entry. A colon only separates when followed by space or EOL,
// so "12:30" and "http://x" stay whole.
int mappingColon(const QString& s) {
    bool inS = false, inD = false;
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('\'') && !inD) inS = !inS;
        else if (c == QLatin1Char('"') && !inS) inD = !inD;
        else if (c == QLatin1Char('#') && !inS && !inD) break;
        else if (c == QLatin1Char(':') && !inS && !inD) {
            if (i + 1 >= s.size() || s.at(i + 1).isSpace()) return i;
        }
        // A flow value on the right can contain colons; stop looking once one
        // opens, since the split point must come before it.
        else if ((c == QLatin1Char('[') || c == QLatin1Char('{')) && !inS && !inD) break;
    }
    return -1;
}

class YamlParser {
public:
    explicit YamlParser(const QVector<YamlLine>& lines) : m_lines(lines) {}

    QJsonValue parseDocument(YamlError& err) {
        if (m_lines.isEmpty()) return {};
        int idx = 0;
        return parseBlock(idx, m_lines.first().indent, err);
    }

private:
    const QVector<YamlLine>& m_lines;

    [[nodiscard]] bool atEnd(int idx) const { return idx >= m_lines.size(); }

    // Parse every line at exactly `indent` (and their deeper children) into one
    // value. Returns when a line dedents below `indent`.
    QJsonValue parseBlock(int& idx, int indent, YamlError& err) {
        if (atEnd(idx)) return {};
        if (m_lines[idx].text.startsWith(QLatin1String("- ")) || m_lines[idx].text == QLatin1String("-"))
            return parseSequence(idx, indent, err);
        return parseMapping(idx, indent, err);
    }

    QJsonValue parseSequence(int& idx, int indent, YamlError& err) {
        QJsonArray arr;
        while (!atEnd(idx)) {
            const YamlLine& ln = m_lines[idx];
            if (ln.indent < indent) break;
            if (ln.indent > indent) {          // deeper content without a parent dash
                err.message = QStringLiteral("Unexpected indentation in sequence.");
                err.line = ln.number;
                return {};
            }
            if (!ln.text.startsWith(QLatin1Char('-'))) break;

            const QString rest = ln.text.mid(1).trimmed();
            const int dashCol = ln.indent + 1;
            ++idx;

            if (rest.isEmpty()) {
                // Value lives on the following, deeper lines.
                if (!atEnd(idx) && m_lines[idx].indent > indent) {
                    arr.append(parseBlock(idx, m_lines[idx].indent, err));
                    if (!err.message.isEmpty()) return {};
                } else {
                    arr.append(QJsonValue());
                }
                continue;
            }

            // Compact form: "- key: value" starts a map whose remaining keys are
            // indented to line up with the text after the dash.
            const int colon = mappingColon(rest);
            if (colon >= 0) {
                QJsonObject obj;
                const QString key = unquote(rest.left(colon));
                const QString val = rest.mid(colon + 1).trimmed();
                if (val.isEmpty()) {
                    if (!atEnd(idx) && m_lines[idx].indent > indent) {
                        obj.insert(key, parseBlock(idx, m_lines[idx].indent, err));
                        if (!err.message.isEmpty()) return {};
                    } else {
                        obj.insert(key, QJsonValue());
                    }
                } else {
                    obj.insert(key, parseInlineValue(val, err, ln.number));
                    if (!err.message.isEmpty()) return {};
                }
                // Sibling keys of the same record.
                while (!atEnd(idx) && m_lines[idx].indent >= dashCol
                       && !m_lines[idx].text.startsWith(QLatin1Char('-'))) {
                    const QJsonValue more = parseMapping(idx, m_lines[idx].indent, err);
                    if (!err.message.isEmpty()) return {};
                    const QJsonObject mo = more.toObject();
                    for (auto it = mo.begin(); it != mo.end(); ++it) obj.insert(it.key(), it.value());
                }
                arr.append(obj);
                continue;
            }

            arr.append(parseInlineValue(rest, err, ln.number));
            if (!err.message.isEmpty()) return {};
        }
        return arr;
    }

    QJsonValue parseMapping(int& idx, int indent, YamlError& err) {
        QJsonObject obj;
        while (!atEnd(idx)) {
            const YamlLine& ln = m_lines[idx];
            if (ln.indent < indent) break;
            if (ln.text.startsWith(QLatin1Char('-')) && ln.indent == indent) break;
            if (ln.indent > indent) {
                err.message = QStringLiteral("Unexpected indentation.");
                err.line = ln.number;
                return {};
            }

            const int colon = mappingColon(ln.text);
            if (colon < 0) {
                err.message = QStringLiteral("Expected 'key: value'.");
                err.line = ln.number;
                return {};
            }
            const QString key = unquote(ln.text.left(colon));
            const QString val = ln.text.mid(colon + 1).trimmed();
            ++idx;

            if (!val.isEmpty()) {
                obj.insert(key, parseInlineValue(val, err, ln.number));
                if (!err.message.isEmpty()) return {};
                continue;
            }
            // Nested block, or an explicitly empty value.
            if (!atEnd(idx) && m_lines[idx].indent > indent) {
                obj.insert(key, parseBlock(idx, m_lines[idx].indent, err));
                if (!err.message.isEmpty()) return {};
            } else if (!atEnd(idx) && m_lines[idx].indent == indent
                       && m_lines[idx].text.startsWith(QLatin1Char('-'))) {
                // A sequence may sit at the parent's indent, which is legal YAML.
                obj.insert(key, parseSequence(idx, indent, err));
                if (!err.message.isEmpty()) return {};
            } else {
                obj.insert(key, QJsonValue());
            }
        }
        return obj;
    }
};

// Tokenise: drop blanks, comments and document markers; measure indentation.
// Tabs are rejected outright because YAML forbids them for indentation and
// silently treating one as N spaces would misnest the document.
QVector<YamlLine> tokenizeYaml(const QString& text, YamlError& err) {
    QVector<YamlLine> out;
    const QStringList raw = text.split(QLatin1Char('\n'));
    bool sawDocEnd = false;

    for (int i = 0; i < raw.size(); ++i) {
        QString line = raw.at(i);
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);

        const QString stripped = stripComment(line);
        if (stripped.trimmed().isEmpty()) continue;

        const QString t = stripped.trimmed();
        if (t == QLatin1String("---")) {
            if (!out.isEmpty()) sawDocEnd = true;   // second document: stop here
            continue;
        }
        if (t == QLatin1String("...")) { sawDocEnd = true; continue; }
        if (sawDocEnd) continue;

        int indent = 0;
        while (indent < stripped.size() && stripped.at(indent) == QLatin1Char(' ')) ++indent;
        if (indent < stripped.size() && stripped.at(indent) == QLatin1Char('\t')) {
            err.message = QStringLiteral("Tabs cannot be used for indentation in YAML.");
            err.line = i + 1;
            return {};
        }

        out.append(YamlLine{ indent, stripped.mid(indent).trimmed(), i + 1 });
    }
    return out;
}

// ── Emitting helpers ────────────────────────────────────────────────────────
// A YAML scalar needs quoting when it would otherwise be read back as some
// other type, or when it contains structural characters.
// Note what this does NOT do: quote things that type-infer to a number or a
// bool. A cell holding 36 should emit as `age: 36`, matching what toJson emits
// for the same cell; quoting it would make the two exporters disagree about the
// same sheet. Text that must stay text is already safe, because scalarFromText
// only accepts a number when it round-trips exactly, so "00403" and "1.10" come
// back as strings on their own.
bool yamlNeedsQuotes(const QString& s) {
    if (s.isEmpty()) return true;
    if (s != s.trimmed()) return true;
    static const QString kSpecial = QStringLiteral(":#-?*&!|>%@`,[]{}\"'\n\t");
    for (const QChar c : s) if (kSpecial.contains(c)) return true;
    return false;
}

QString yamlQuote(const QString& s) {
    QString out = s;
    out.replace(QLatin1String("\\"), QLatin1String("\\\\"));
    out.replace(QLatin1String("\""), QLatin1String("\\\""));
    out.replace(QLatin1String("\n"), QLatin1String("\\n"));
    out.replace(QLatin1String("\t"), QLatin1String("\\t"));
    return QLatin1Char('"') + out + QLatin1Char('"');
}

QString yamlScalarOut(const QString& cell) {
    if (cell.isEmpty()) return QStringLiteral("null");
    return yamlNeedsQuotes(cell) ? yamlQuote(cell) : cell;
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// Public API
// ═════════════════════════════════════════════════════════════════════════════

QJsonValue inferScalar(const QString& text) { return scalarFromText(text); }

Format detectFormat(const QString& text) {
    for (const QChar c : text) {
        if (c.isSpace()) continue;
        return (c == QLatin1Char('{') || c == QLatin1Char('[')) ? Format::Json : Format::Yaml;
    }
    return Format::Yaml;
}

ParseResult parse(const QString& text, Format fmt) {
    return fmt == Format::Json ? parseJson(text) : parseYaml(text);
}

ParseResult parseJson(const QString& text) {
    ParseResult res;
    if (text.trimmed().isEmpty()) {
        res.error = QStringLiteral("Nothing to import: the text is empty.");
        return res;
    }

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &perr);
    if (perr.error != QJsonParseError::NoError) {
        res.error = perr.errorString();
        // offset is a byte index; convert to a line number for the dialog.
        const int upto = qBound(0, perr.offset, text.toUtf8().size());
        res.line = QString::fromUtf8(text.toUtf8().left(upto)).count(QLatin1Char('\n')) + 1;
        return res;
    }

    const QJsonValue root = doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
    res.table = tableFromJson(root);
    if (res.table.headers.isEmpty())
        res.error = QStringLiteral("Parsed successfully, but there were no fields to put in a table.");
    return res;
}

ParseResult parseYaml(const QString& text) {
    ParseResult res;
    if (text.trimmed().isEmpty()) {
        res.error = QStringLiteral("Nothing to import: the text is empty.");
        return res;
    }

    YamlError err;
    const QVector<YamlLine> lines = tokenizeYaml(text, err);
    if (!err.message.isEmpty()) { res.error = err.message; res.line = err.line; return res; }
    if (lines.isEmpty()) {
        res.error = QStringLiteral("Nothing to import: the text has no YAML content.");
        return res;
    }

    YamlParser parser(lines);
    const QJsonValue root = parser.parseDocument(err);
    if (!err.message.isEmpty()) { res.error = err.message; res.line = err.line; return res; }

    res.table = tableFromJson(root);
    if (res.table.headers.isEmpty())
        res.error = QStringLiteral("Parsed successfully, but there were no fields to put in a table.");
    return res;
}

QString toJson(const Table& t, bool pretty) {
    QJsonArray arr;
    for (const QStringList& row : t.rows) {
        QJsonObject obj;
        for (int c = 0; c < t.headers.size(); ++c) {
            const QString key = t.headers.at(c);
            if (key.isEmpty()) continue;
            obj.insert(key, scalarFromText(c < row.size() ? row.at(c) : QString()));
        }
        arr.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(
        pretty ? QJsonDocument::Indented : QJsonDocument::Compact)).trimmed();
}

QString toYaml(const Table& t) {
    QString out;
    for (const QStringList& row : t.rows) {
        bool first = true;
        for (int c = 0; c < t.headers.size(); ++c) {
            const QString key = t.headers.at(c);
            if (key.isEmpty()) continue;
            const QString val = yamlScalarOut(c < row.size() ? row.at(c) : QString());
            // The first field of a record rides on the dash line, the rest are
            // indented to match it.
            out += (first ? QStringLiteral("- ") : QStringLiteral("  "));
            out += (yamlNeedsQuotes(key) ? yamlQuote(key) : key);
            out += QStringLiteral(": ") + val + QLatin1Char('\n');
            first = false;
        }
        if (first) out += QStringLiteral("- {}\n");   // a row with no fields
    }
    return out;
}

} // namespace StructuredData
} // namespace NativeOffice
