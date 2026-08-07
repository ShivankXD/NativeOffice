// ─────────────────────────────────────────────────────────────────────────────
// SheetSql.cpp — see SheetSql.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "SheetSql.h"

#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
#include <QVariant>

namespace NativeOffice {
namespace SheetSql {

namespace {

// Column storage class, decided by looking at every value in the column. This
// is what makes "WHERE age > 30", ORDER BY and SUM behave like arithmetic
// instead of string comparison, which is the single most common way a
// sheets-to-SQL bridge goes quietly wrong.
enum class ColType { Integer, Real, Text };

// A value is only treated as a number when it round-trips exactly. "00403" and
// "1.10" are therefore text, so identifiers keep their shape.
bool isExactInt(const QString& s, qlonglong& out) {
    bool ok = false;
    const qlonglong v = s.toLongLong(&ok);
    if (!ok || QString::number(v) != s) return false;
    out = v;
    return true;
}
bool isExactReal(const QString& s, double& out) {
    bool ok = false;
    const double v = s.toDouble(&ok);
    if (!ok || QString::number(v) != s) return false;
    out = v;
    return true;
}

ColType inferColumn(const QVector<QStringList>& rows, int col) {
    bool sawAny = false, allInt = true, allReal = true;
    for (const QStringList& r : rows) {
        if (col >= r.size()) continue;
        const QString s = r.at(col).trimmed();
        if (s.isEmpty()) continue;               // NULL: does not constrain the type
        sawAny = true;
        qlonglong i = 0; double d = 0;
        if (!isExactInt(s, i))  allInt = false;
        if (!isExactReal(s, d)) allReal = false;
        if (!allInt && !allReal) break;
    }
    if (!sawAny) return ColType::Text;
    if (allInt)  return ColType::Integer;
    if (allReal) return ColType::Real;
    return ColType::Text;
}

// SQLite identifier quoting: wrap in double quotes, doubling any inside. Used
// for every table and column name, so a sheet called "Q1 Sales" or a header
// called "total (£)" needs no special-casing anywhere else.
QString quoteId(const QString& raw) {
    QString s = raw;
    s.replace(QLatin1String("\""), QLatin1String("\"\""));
    return QLatin1Char('"') + s + QLatin1Char('"');
}

// Header row -> column names. Blank headers get a positional name, and repeats
// are suffixed, because SQLite rejects a table with two identical columns and
// a real sheet very often has both.
QStringList columnNames(const SourceSheet& sheet) {
    QStringList out;
    QSet<QString> used;
    for (int i = 0; i < sheet.headers.size(); ++i) {
        QString name = sheet.headers.at(i).trimmed();
        if (name.isEmpty()) name = QStringLiteral("column%1").arg(i + 1);
        QString candidate = name;
        int n = 2;
        while (used.contains(candidate.toLower()))
            candidate = QStringLiteral("%1_%2").arg(name).arg(n++);
        used.insert(candidate.toLower());
        out << candidate;
    }
    return out;
}

// Strip one layer of identifier quoting.
QString unquoteId(QString t) {
    if (t.startsWith(QLatin1Char('"')) && t.endsWith(QLatin1Char('"')) && t.size() >= 2)
        return t.mid(1, t.size() - 2).replace(QLatin1String("\"\""), QLatin1String("\""));
    if (t.size() >= 2
        && ((t.startsWith(QLatin1Char('[')) && t.endsWith(QLatin1Char(']')))
         || (t.startsWith(QLatin1Char('`')) && t.endsWith(QLatin1Char('`')))))
        return t.mid(1, t.size() - 2);
    return t;
}

// Names appearing after FROM or JOIN. Scanning the whole query for sheet names
// instead would false-positive on a column that shares a name with a sheet,
// which would push a perfectly ordinary single-sheet query behind the paywall.
//
// The FROM branch must read the WHOLE comma-separated list, not just the first
// entry: "FROM People, Orders" is a cross join by another name, and reading
// only "People" would let a multi-sheet query through the gate as single-sheet.
QStringList tablesReferenced(const QString& sql) {
    // Words that end the table list. "on"/"using" and the join-type words matter
    // because they follow a table reference directly.
    static const QSet<QString> kStop = {
        QStringLiteral("where"),  QStringLiteral("group"),  QStringLiteral("order"),
        QStringLiteral("having"), QStringLiteral("limit"),  QStringLiteral("offset"),
        QStringLiteral("union"),  QStringLiteral("intersect"), QStringLiteral("except"),
        QStringLiteral("join"),   QStringLiteral("inner"),  QStringLiteral("left"),
        QStringLiteral("right"),  QStringLiteral("full"),   QStringLiteral("cross"),
        QStringLiteral("natural"),QStringLiteral("on"),     QStringLiteral("using"),
        QStringLiteral("window"), QStringLiteral("select"),
    };
    // One table reference: a quoted or bare identifier.
    static const QRegularExpression refRe(
        QStringLiteral("^\\s*(\"(?:[^\"]|\"\")+\"|\\[[^\\]]+\\]|`[^`]+`|[A-Za-z_][A-Za-z0-9_]*)"));
    static const QRegularExpression fromRe(QStringLiteral("\\bfrom\\b"),
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression joinRe(
        QStringLiteral("\\bjoin\\s+(\"(?:[^\"]|\"\")+\"|\\[[^\\]]+\\]|`[^`]+`|[A-Za-z_][A-Za-z0-9_]*)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression wordRe(QStringLiteral("[A-Za-z_][A-Za-z0-9_]*"));

    QStringList out;
    auto add = [&out](QString t) {
        t = unquoteId(std::move(t));
        if (!t.isEmpty() && !out.contains(t, Qt::CaseInsensitive)) out << t;
    };

    // ── FROM lists ──────────────────────────────────────────────────────────
    auto fit = fromRe.globalMatch(sql);
    while (fit.hasNext()) {
        const int start = fit.next().capturedEnd();
        // Cut the list at the first stop word, or at a parenthesis: a subquery
        // has its own FROM, which this same loop picks up separately.
        int end = sql.size();
        auto wit = wordRe.globalMatch(sql, start);
        while (wit.hasNext()) {
            const auto wm = wit.next();
            // Ignore words inside quotes; only bare words can be keywords.
            if (start < sql.size() && kStop.contains(wm.captured(0).toLower())) {
                end = wm.capturedStart();
                break;
            }
        }
        const int paren = sql.indexOf(QLatin1Char('('), start);
        if (paren >= 0 && paren < end) end = paren;
        const int close = sql.indexOf(QLatin1Char(')'), start);
        if (close >= 0 && close < end) end = close;

        const QString list = sql.mid(start, end - start);
        for (const QString& piece : list.split(QLatin1Char(','))) {
            const auto m = refRe.match(piece);
            if (m.hasMatch()) add(m.captured(1));   // first token; drops any alias
        }
    }

    // ── JOIN targets ────────────────────────────────────────────────────────
    auto jit = joinRe.globalMatch(sql);
    while (jit.hasNext()) add(jit.next().captured(1));

    return out;
}

// Strip string literals and comments before keyword scanning, so a query
// selecting the text 'attach' is not mistaken for an ATTACH statement.
QString stripLiteralsAndComments(const QString& sql) {
    QString out;
    out.reserve(sql.size());
    bool inStr = false, inId = false, inLine = false, inBlock = false;
    for (int i = 0; i < sql.size(); ++i) {
        const QChar c = sql.at(i);
        const QChar n = (i + 1 < sql.size()) ? sql.at(i + 1) : QChar();
        if (inLine)  { if (c == QLatin1Char('\n')) { inLine = false; out += c; } continue; }
        if (inBlock) { if (c == QLatin1Char('*') && n == QLatin1Char('/')) { inBlock = false; ++i; } continue; }
        if (inStr)   { if (c == QLatin1Char('\'')) inStr = false; out += QLatin1Char(' '); continue; }
        if (inId)    { if (c == QLatin1Char('"'))  inId  = false; out += c; continue; }

        if (c == QLatin1Char('-') && n == QLatin1Char('-')) { inLine = true; ++i; continue; }
        if (c == QLatin1Char('/') && n == QLatin1Char('*')) { inBlock = true; ++i; continue; }
        if (c == QLatin1Char('\'')) { inStr = true; out += QLatin1Char(' '); continue; }
        if (c == QLatin1Char('"'))  { inId  = true; out += c; continue; }
        out += c;
    }
    return out;
}

} // namespace

QString quoteIdentifier(const QString& name) { return quoteId(name); }

bool isQuerySafe(const QString& sql, QString* why) {
    const QString bare = stripLiteralsAndComments(sql).trimmed();
    auto fail = [why](const QString& msg) { if (why) *why = msg; return false; };

    if (bare.isEmpty()) return fail(QStringLiteral("The query is empty."));

    // One statement only. A trailing semicolon is fine; anything after it is not.
    const int semi = bare.indexOf(QLatin1Char(';'));
    if (semi >= 0 && !bare.mid(semi + 1).trimmed().isEmpty())
        return fail(QStringLiteral("Only one statement can be run at a time."));

    static const QRegularExpression startsSelect(
        QStringLiteral("^\\s*(?:select|with)\\b"), QRegularExpression::CaseInsensitiveOption);
    if (!startsSelect.match(bare).hasMatch())
        return fail(QStringLiteral("Only SELECT queries are allowed here. "
                                   "Use the sheet itself to change data."));

    // ATTACH reaches the filesystem and PRAGMA changes engine behaviour, so
    // neither is left to chance even though the database is in memory.
    static const QRegularExpression banned(
        QStringLiteral("\\b(attach|detach|pragma|vacuum|insert|update|delete|drop|alter|create|replace)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = banned.match(bare);
    if (m.hasMatch())
        return fail(QStringLiteral("'%1' is not allowed in a sheet query.")
                        .arg(m.captured(1).toUpper()));

    return true;
}

QueryInfo analyze(const QString& sql, const QVector<SourceSheet>& sheets) {
    QueryInfo info;

    QString why;
    if (!isQuerySafe(sql, &why)) {
        info.valid = false;
        info.reason = why;
        return info;
    }

    const QString bare = stripLiteralsAndComments(sql);
    const QStringList named = tablesReferenced(bare);

    for (const QString& t : named)
        for (const SourceSheet& s : sheets)
            if (s.name.compare(t, Qt::CaseInsensitive) == 0) {
                if (!info.tables.contains(s.name)) {
                    info.tables << s.name;
                    info.rowsInvolved += s.rows.size();
                }
                break;
            }

    static const QRegularExpression joinRe(QStringLiteral("\\bjoin\\b"),
                                           QRegularExpression::CaseInsensitiveOption);
    // Either an explicit JOIN, or more than one sheet in play (which covers the
    // old-style comma join and subqueries against a second sheet).
    info.multiTable = joinRe.match(bare).hasMatch() || info.tables.size() > 1;
    return info;
}

QueryResult run(const QVector<SourceSheet>& sheets, const QString& sql) {
    QueryResult res;

    QString why;
    if (!isQuerySafe(sql, &why)) { res.error = why; return res; }

    // A unique connection name per call: two queries must never share state,
    // and the connection is removed again below so nothing accumulates.
    const QString conn = QStringLiteral("sheetsql_")
                       + QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(QStringLiteral(":memory:"));
        if (!db.open()) {
            res.error = QStringLiteral("Could not start the query engine: %1")
                            .arg(db.lastError().text());
            QSqlDatabase::removeDatabase(conn);
            return res;
        }

        // ── Build the tables ────────────────────────────────────────────────
        for (const SourceSheet& sheet : sheets) {
            if (sheet.headers.isEmpty()) continue;
            const QStringList cols = columnNames(sheet);

            QVector<ColType> types;
            types.reserve(cols.size());
            QStringList defs;
            for (int c = 0; c < cols.size(); ++c) {
                const ColType t = inferColumn(sheet.rows, c);
                types.append(t);
                defs << quoteId(cols.at(c)) + QLatin1Char(' ')
                        + (t == ColType::Integer ? QStringLiteral("INTEGER")
                         : t == ColType::Real    ? QStringLiteral("REAL")
                                                 : QStringLiteral("TEXT"));
            }

            QSqlQuery q(db);
            if (!q.exec(QStringLiteral("CREATE TABLE %1 (%2)")
                            .arg(quoteId(sheet.name), defs.join(QStringLiteral(", "))))) {
                res.error = QStringLiteral("Could not build table for sheet '%1': %2")
                                .arg(sheet.name, q.lastError().text());
                db.close();
                QSqlDatabase::removeDatabase(conn);
                return res;
            }

            if (sheet.rows.isEmpty()) continue;
            QStringList marks;
            for (int i = 0; i < cols.size(); ++i) marks << QStringLiteral("?");
            QSqlQuery ins(db);
            ins.prepare(QStringLiteral("INSERT INTO %1 VALUES (%2)")
                            .arg(quoteId(sheet.name), marks.join(QStringLiteral(", "))));

            // One transaction for the whole sheet: without it SQLite commits
            // per row and loading is orders of magnitude slower.
            db.transaction();
            for (const QStringList& row : sheet.rows) {
                for (int c = 0; c < cols.size(); ++c) {
                    const QString cell = c < row.size() ? row.at(c).trimmed() : QString();
                    if (cell.isEmpty()) { ins.addBindValue(QVariant()); continue; }
                    qlonglong i = 0; double d = 0;
                    if (types.at(c) == ColType::Integer && isExactInt(cell, i))
                        ins.addBindValue(i);
                    else if (types.at(c) == ColType::Real && isExactReal(cell, d))
                        ins.addBindValue(d);
                    else
                        ins.addBindValue(cell);
                }
                if (!ins.exec()) {
                    db.rollback();
                    res.error = QStringLiteral("Could not load sheet '%1': %2")
                                    .arg(sheet.name, ins.lastError().text());
                    db.close();
                    QSqlDatabase::removeDatabase(conn);
                    return res;
                }
            }
            db.commit();
        }

        // ── Run it ──────────────────────────────────────────────────────────
        QSqlQuery q(db);
        if (!q.exec(sql)) {
            res.error = q.lastError().databaseText().trimmed();
            if (res.error.isEmpty()) res.error = q.lastError().text().trimmed();
            db.close();
            QSqlDatabase::removeDatabase(conn);
            return res;
        }

        const QSqlRecord rec = q.record();
        for (int c = 0; c < rec.count(); ++c) res.table.headers << rec.fieldName(c);

        int n = 0;
        while (q.next()) {
            if (n >= kMaxResultRows) { res.truncatedTo = kMaxResultRows; break; }
            QStringList row;
            row.reserve(rec.count());
            for (int c = 0; c < rec.count(); ++c) {
                const QVariant v = q.value(c);
                // NULL becomes an empty cell rather than the text "NULL",
                // matching what an empty cell means everywhere else in Calc.
                row << (v.isNull() ? QString() : v.toString());
            }
            res.table.rows.append(row);
            ++n;
        }
        db.close();
    }
    // Must be outside the scope above: removeDatabase warns if any QSqlDatabase
    // referencing the connection is still alive.
    QSqlDatabase::removeDatabase(conn);
    return res;
}

} // namespace SheetSql
} // namespace NativeOffice
