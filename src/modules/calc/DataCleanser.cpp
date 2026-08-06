// ─────────────────────────────────────────────────────────────────────────────
// DataCleanser.cpp — see DataCleanser.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "DataCleanser.h"

#include <QDate>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace NativeOffice {
namespace DataCleanser {

namespace {

// A formula is the user's own logic; rewriting its text would change what the
// sheet computes. Every operation skips these.
inline bool isFormula(const QString& s) { return s.startsWith(QLatin1Char('=')); }

// Patterns are built once. QRegularExpression compiles lazily and caches, but
// static also keeps them off the per-cell path entirely.
const QRegularExpression& reIso() {
    static const QRegularExpression r(QStringLiteral("^(\\d{4})-(\\d{1,2})-(\\d{1,2})$"));
    return r;
}
// Three numeric parts separated by / - or . (a mixed separator is not a date).
const QRegularExpression& reNumericDate() {
    static const QRegularExpression r(
        QStringLiteral("^(\\d{1,4})([/\\-.])(\\d{1,2})\\2(\\d{1,4})$"));
    return r;
}
const QRegularExpression& reEmail() {
    static const QRegularExpression r(
        QStringLiteral("[A-Za-z0-9._%+\\-]+@[A-Za-z0-9](?:[A-Za-z0-9\\-]*[A-Za-z0-9])?"
                       "(?:\\.[A-Za-z0-9](?:[A-Za-z0-9\\-]*[A-Za-z0-9])?)*\\.[A-Za-z]{2,}"));
    return r;
}
const QRegularExpression& reUrl() {
    static const QRegularExpression r(
        QStringLiteral("(?:https?://|ftp://|www\\.)[^\\s<>\"'()\\[\\]]+"));
    return r;
}

// Two-digit years: the POSIX pivot. 00-68 is this century, 69-99 the last one.
int expandYear(int y) {
    if (y >= 100) return y;
    return y <= 68 ? 2000 + y : 1900 + y;
}

// Month names, matched case-insensitively against full and 3-letter forms.
// Deliberately English-only and hard-coded rather than QLocale-driven: the
// locale can change under the user, and a sheet's contents do not.
int monthFromName(const QString& name) {
    static const QStringList kFull = {
        QStringLiteral("january"), QStringLiteral("february"), QStringLiteral("march"),
        QStringLiteral("april"),   QStringLiteral("may"),      QStringLiteral("june"),
        QStringLiteral("july"),    QStringLiteral("august"),   QStringLiteral("september"),
        QStringLiteral("october"), QStringLiteral("november"), QStringLiteral("december")
    };
    const QString n = name.toLower();
    for (int i = 0; i < kFull.size(); ++i) {
        if (n == kFull.at(i) || n == kFull.at(i).left(3)) return i + 1;
        // "sept" is common enough to be worth accepting.
        if (i == 8 && n == QLatin1String("sept")) return 9;
    }
    return 0;
}

// Textual forms: "12 Jan 2024", "Jan 12, 2024", "12 January 2024".
QString textualDate(const QString& s) {
    static const QRegularExpression dayFirstRe(
        QStringLiteral("^(\\d{1,2})[ \\-]([A-Za-z]{3,9})\\.?[ \\-,]+(\\d{2,4})$"));
    static const QRegularExpression monthFirstRe(
        QStringLiteral("^([A-Za-z]{3,9})\\.?[ \\-]+(\\d{1,2})(?:st|nd|rd|th)?[ ,]+(\\d{2,4})$"));

    auto build = [](int d, int m, int y) -> QString {
        if (m == 0) return {};
        const QDate date(expandYear(y), m, d);
        return date.isValid() ? date.toString(QStringLiteral("yyyy-MM-dd")) : QString();
    };

    auto m1 = dayFirstRe.match(s);
    if (m1.hasMatch())
        return build(m1.captured(1).toInt(), monthFromName(m1.captured(2)), m1.captured(3).toInt());

    auto m2 = monthFirstRe.match(s);
    if (m2.hasMatch())
        return build(m2.captured(2).toInt(), monthFromName(m2.captured(1)), m2.captured(3).toInt());

    return {};
}

// Rows in the region, honouring firstRowIsHeader.
inline int firstDataRow(const QRect& region, const Options& opt) {
    return region.top() + (opt.firstRowIsHeader ? 1 : 0);
}

// Shared driver for the two extractors: same shape, different pattern and noun.
Result extractWith(const CellReader& read, const QRect& region, int targetCol,
                   const Options& opt, const QRegularExpression& re,
                   const QString& noun, bool trimTrailingPunctuation) {
    Result res;
    if (targetCol < 0) {
        res.ok = false;
        res.summary = QStringLiteral("There is no free column to the right to put the "
                                     "results in. Clear a column and try again.");
        return res;
    }

    int found = 0, rowsWith = 0;
    for (int row = firstDataRow(region, opt); row <= region.bottom(); ++row) {
        QStringList hits;
        for (int col = region.left(); col <= region.right(); ++col) {
            if (col == targetCol) continue;          // never read our own output
            const QString cell = read(col, row);
            if (cell.isEmpty() || isFormula(cell)) continue;
            auto it = re.globalMatch(cell);
            while (it.hasNext()) {
                QString m = it.next().captured(0);
                if (trimTrailingPunctuation) {
                    // A URL at the end of a sentence swallows the punctuation;
                    // strip what cannot legally end one.
                    while (!m.isEmpty()
                           && QStringLiteral(".,;:!?").contains(m.back()))
                        m.chop(1);
                }
                if (!m.isEmpty() && !hits.contains(m)) hits << m;
            }
        }
        if (hits.isEmpty()) continue;
        found += hits.size();
        ++rowsWith;
        res.changes.append({ targetCol, row, hits.join(QStringLiteral("; ")) });
    }

    res.summary = found == 0
        ? QStringLiteral("No %1 found in the selection.").arg(noun)
        : QStringLiteral("Found %1 %2 across %3 row(s).").arg(found).arg(noun).arg(rowsWith);
    return res;
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// Tier + naming
// ═════════════════════════════════════════════════════════════════════════════

bool requiresPremium(Op op) {
    switch (op) {
        case Op::TrimWhitespace:
        case Op::RemoveDuplicates:
            return false;
        case Op::StandardizeDates:
        case Op::ExtractEmails:
        case Op::ExtractUrls:
            return true;
    }
    return false;
}

QString opName(Op op) {
    switch (op) {
        case Op::TrimWhitespace:   return QStringLiteral("Trim Whitespace");
        case Op::RemoveDuplicates: return QStringLiteral("Remove Duplicates");
        case Op::StandardizeDates: return QStringLiteral("Standardize Dates");
        case Op::ExtractEmails:    return QStringLiteral("Extract Emails");
        case Op::ExtractUrls:      return QStringLiteral("Extract URLs");
    }
    return {};
}

// ═════════════════════════════════════════════════════════════════════════════
// Transforms
// ═════════════════════════════════════════════════════════════════════════════

QString tidyWhitespace(const QString& text) {
    QString s = text;
    // U+00A0 and the other fixed-width spaces survive a web copy-paste and are
    // invisible in the grid, so they are folded to a plain space before the
    // collapse rather than being left as "not whitespace".
    for (const QChar c : { QChar(0x00A0), QChar(0x2007), QChar(0x202F), QChar(0x2009) })
        s.replace(c, QLatin1Char(' '));
    static const QRegularExpression runs(QStringLiteral("\\s+"));
    return s.replace(runs, QStringLiteral(" ")).trimmed();
}

QString toIsoDate(const QString& text, bool dayFirst) {
    const QString s = text.trimmed();
    if (s.isEmpty()) return {};

    // Already ISO: accept it, but normalise 2024-1-5 to 2024-01-05 and reject
    // an impossible day so the column ends up genuinely uniform.
    if (const auto m = reIso().match(s); m.hasMatch()) {
        const QDate d(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
        return d.isValid() ? d.toString(QStringLiteral("yyyy-MM-dd")) : QString();
    }

    if (const auto m = reNumericDate().match(s); m.hasMatch()) {
        const int a = m.captured(1).toInt();
        const int b = m.captured(3).toInt();
        const int c = m.captured(4).toInt();

        int y = 0, mo = 0, d = 0;
        if (m.captured(1).size() == 4) {
            y = a; mo = b; d = c;                    // 2024/01/15
        } else {
            y = expandYear(c);
            // One component over 12 can only be the day, which settles the
            // ambiguity without guessing. Otherwise fall back to the policy.
            if (a > 12)      { d = a; mo = b; }
            else if (b > 12) { mo = a; d = b; }
            else if (dayFirst) { d = a; mo = b; }
            else               { mo = a; d = b; }
        }
        const QDate date(y, mo, d);
        return date.isValid() ? date.toString(QStringLiteral("yyyy-MM-dd")) : QString();
    }

    return textualDate(s);
}

Result trimWhitespace(const CellReader& read, const QRect& region, const Options& opt) {
    Result res;
    int changed = 0;
    for (int row = firstDataRow(region, opt); row <= region.bottom(); ++row) {
        for (int col = region.left(); col <= region.right(); ++col) {
            const QString cell = read(col, row);
            if (cell.isEmpty() || isFormula(cell)) continue;
            const QString tidy = tidyWhitespace(cell);
            if (tidy == cell) continue;
            res.changes.append({ col, row, tidy });
            ++changed;
        }
    }
    res.summary = changed == 0 ? QStringLiteral("Nothing to trim: no stray whitespace found.")
                               : QStringLiteral("Trimmed %1 cell(s).").arg(changed);
    return res;
}

Result removeDuplicateRows(const CellReader& read, const QRect& region, const Options& opt) {
    Result res;
    const int start = firstDataRow(region, opt);

    QSet<QString> seen;
    QVector<QStringList> keep;
    for (int row = start; row <= region.bottom(); ++row) {
        QStringList cells;
        for (int col = region.left(); col <= region.right(); ++col)
            cells << read(col, row);

        // A wholly blank row is spacing, not data; leaving it out would silently
        // pack the region upward for reasons the user did not ask for.
        bool blank = true;
        for (const QString& c : cells) if (!c.isEmpty()) { blank = false; break; }
        if (blank) continue;

        // Unit Separator cannot appear in cell text, so it cannot make two
        // different rows collide the way a comma or a tab could.
        const QString key = cells.join(QChar(0x1F));
        if (seen.contains(key)) continue;
        seen.insert(key);
        keep.append(cells);
    }

    int outRow = start;
    for (const QStringList& cells : keep) {
        for (int i = 0; i < cells.size(); ++i) {
            const int col = region.left() + i;
            if (read(col, outRow) != cells.at(i))
                res.changes.append({ col, outRow, cells.at(i) });
        }
        ++outRow;
    }
    for (int row = outRow; row <= region.bottom(); ++row)      // clear what is freed
        for (int col = region.left(); col <= region.right(); ++col)
            if (!read(col, row).isEmpty())
                res.changes.append({ col, row, QString() });

    const int removed = int(keep.size()) > 0 || start <= region.bottom()
                            ? (region.bottom() - start + 1) - int(keep.size())
                            : 0;
    res.summary = removed <= 0 ? QStringLiteral("No duplicate rows found.")
                               : QStringLiteral("Removed %1 duplicate row(s).").arg(removed);
    return res;
}

Result standardizeDates(const CellReader& read, const QRect& region, const Options& opt) {
    Result res;
    int changed = 0, skipped = 0;
    for (int row = firstDataRow(region, opt); row <= region.bottom(); ++row) {
        for (int col = region.left(); col <= region.right(); ++col) {
            const QString cell = read(col, row);
            if (cell.isEmpty() || isFormula(cell)) continue;
            const QString iso = toIsoDate(cell, opt.dayFirst);
            if (iso.isEmpty()) { ++skipped; continue; }   // not a date: leave alone
            if (iso == cell) continue;                    // already correct
            res.changes.append({ col, row, iso });
            ++changed;
        }
    }
    if (changed == 0) {
        res.summary = QStringLiteral("No dates needed changing.");
    } else {
        res.summary = QStringLiteral("Standardized %1 date(s) to YYYY-MM-DD.").arg(changed);
        if (skipped > 0)
            res.summary += QStringLiteral(" %1 cell(s) were not dates and were left alone.")
                               .arg(skipped);
    }
    return res;
}

Result extractEmails(const CellReader& read, const QRect& region, int targetCol,
                     const Options& opt) {
    return extractWith(read, region, targetCol, opt, reEmail(),
                       QStringLiteral("email address(es)"), false);
}

Result extractUrls(const CellReader& read, const QRect& region, int targetCol,
                   const Options& opt) {
    return extractWith(read, region, targetCol, opt, reUrl(),
                       QStringLiteral("URL(s)"), true);
}

} // namespace DataCleanser
} // namespace NativeOffice
