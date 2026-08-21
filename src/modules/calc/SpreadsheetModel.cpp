// ─────────────────────────────────────────────────────────────────────────────
// SpreadsheetModel.cpp  (Sprint 4 → Sprint 10)
// Cell store now holds rich Cells (content + format); all edits funnel through
// a QUndoStack via CellsChangeCommand.
// ─────────────────────────────────────────────────────────────────────────────
#include "SpreadsheetModel.h"
#include "FormulaEngine.h"
#include "CalcCommands.h"

#include <QColor>
#include <QFont>
#include <QUndoStack>
#include <QLocale>
#include <QDateTime>
#include <QDate>
#include <QTime>
#include <QHash>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace NativeOffice {

namespace {

// Grid-mutation kinds for formula-reference rewriting.
enum class GridOp { InsertRow, DeleteRow, InsertCol, DeleteCol };

// Adjust one (col,row) reference for a grid op. 'index' is the 0-based row/col
// being inserted/deleted. Returns the new address, or "#REF!" if it was deleted.
QString adjustRef(int col, int row, GridOp op, int index) {
    switch (op) {
        case GridOp::InsertRow: if (row >= index) ++row; break;
        case GridOp::DeleteRow: if (row == index) return "#REF!"; if (row > index) --row; break;
        case GridOp::InsertCol: if (col >= index) ++col; break;
        case GridOp::DeleteCol: if (col == index) return "#REF!"; if (col > index) --col; break;
    }
    return FormulaEngine::cellAddress(col, row);
}

// Rewrite the cell references inside a formula ("=...") for a grid op. Skips
// string literals, function names, and (conservatively) sheet-qualified refs.
QString rewriteFormula(const QString& formula, GridOp op, int index) {
    if (!formula.startsWith('=')) return formula;
    QString out = "=";
    const QString& s = formula;
    int i = 1;
    while (i < s.size()) {
        const QChar ch = s[i];
        if (ch == '"') {                              // string literal
            out += ch; ++i;
            while (i < s.size()) {
                out += s[i];
                if (s[i] == '"') {
                    ++i;
                    if (i < s.size() && s[i] == '"') { out += s[i]; ++i; continue; }
                    break;
                }
                ++i;
            }
            continue;
        }
        if (ch.isLetter()) {
            const int start = i;
            while (i < s.size() && s[i].isLetter()) ++i;
            const QString letters = s.mid(start, i - start);
            int j = i;
            while (j < s.size() && s[j].isSpace()) ++j;
            if (j < s.size() && s[j] == '(') {        // function name
                out += letters;
                continue;
            }
            if (j < s.size() && s[j] == '!') {        // sheet-qualified ref → leave as-is
                out += letters + s.mid(i, j - i) + '!';
                i = j + 1;
                const int rs = i;
                while (i < s.size() && s[i].isLetter()) ++i;
                while (i < s.size() && s[i].isDigit())  ++i;
                out += s.mid(rs, i - rs);
                continue;
            }
            if (i < s.size() && s[i].isDigit()) {     // unqualified cell ref
                const int ds = i;
                while (i < s.size() && s[i].isDigit()) ++i;
                const QString token = letters + s.mid(ds, i - ds);
                int col = 0, row = 0;
                out += FormulaEngine::parseCellRef(token, col, row)
                           ? adjustRef(col, row, op, index) : token;
                continue;
            }
            out += letters;                           // TRUE/FALSE/bare name
            continue;
        }
        out += ch; ++i;
    }
    return out;
}

// Shift the *relative* cell references in a conditional-format formula by
// (dCol,dRow). A '$' prefix locks that component (absolute) and is stripped from
// the output so the existing FormulaEngine — which has no '$' support — can
// evaluate the resulting concrete address. Skips string literals and function
// names; leaves TRUE/FALSE and defined names verbatim.
QString shiftCondFormula(const QString& formula, int dCol, int dRow) {
    if (!formula.startsWith('=')) return formula;
    const QString& s = formula;
    QString out = "=";
    int i = 1;
    while (i < s.size()) {
        const QChar ch = s[i];
        if (ch == '"') {                               // string literal
            out += ch; ++i;
            while (i < s.size()) {
                out += s[i];
                if (s[i] == '"') {
                    ++i;
                    if (i < s.size() && s[i] == '"') { out += s[i]; ++i; continue; }
                    break;
                }
                ++i;
            }
            continue;
        }
        // Possible cell reference: optional '$', letters, optional '$', digits.
        if (ch == '$' || ch.isLetter()) {
            const int start = i;
            bool colAbs = false, rowAbs = false;
            int j = i;
            if (s[j] == '$') { colAbs = true; ++j; }
            const int ls = j;
            while (j < s.size() && s[j].isLetter()) ++j;
            const QString letters = s.mid(ls, j - ls);

            // Function name (no '$', letters followed by '(') → leave verbatim.
            if (!colAbs && !letters.isEmpty()) {
                int k = j;
                while (k < s.size() && s[k].isSpace()) ++k;
                if (k < s.size() && s[k] == '(') { out += letters; i = j; continue; }
            }

            int j2 = j;
            if (j2 < s.size() && s[j2] == '$') { rowAbs = true; ++j2; }
            const int ds = j2;
            while (j2 < s.size() && s[j2].isDigit()) ++j2;
            const QString digits = s.mid(ds, j2 - ds);

            int col = 0, row = 0;
            if (!letters.isEmpty() && !digits.isEmpty()
                && FormulaEngine::parseCellRef(letters + digits, col, row)) {
                if (!colAbs) col += dCol;
                if (!rowAbs) row += dRow;
                col = std::clamp(col, 0, SpreadsheetModel::NUM_COLS - 1);
                row = std::clamp(row, 0, SpreadsheetModel::NUM_ROWS - 1);
                out += FormulaEngine::cellAddress(col, row);
                i = j2;
                continue;
            }

            // Not a cell ref (TRUE/FALSE, a defined name, a stray '$').
            if (!letters.isEmpty()) { out += letters; i = j; }
            else { out += s[start]; i = start + 1; }
            continue;
        }
        out += ch; ++i;
    }
    return out;
}

} // namespace

SpreadsheetModel::SpreadsheetModel(QObject* parent)
    : QAbstractTableModel(parent)
    , m_undo(new QUndoStack(this))
{
    m_undo->setUndoLimit(100);   // Priority 1: 100-step history
}

// ─────────────────────────────────────────────────────────────────────────────
// Dimensions
// ─────────────────────────────────────────────────────────────────────────────
int SpreadsheetModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : NUM_ROWS;
}

int SpreadsheetModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : NUM_COLS;
}

// ─────────────────────────────────────────────────────────────────────────────
// Header data  (column: A–Z, row: 1–100)
// ─────────────────────────────────────────────────────────────────────────────
QVariant SpreadsheetModel::headerData(int section,
                                       Qt::Orientation orientation,
                                       int role) const {
    if (role == Qt::DisplayRole) {
        if (orientation == Qt::Horizontal)
            return FormulaEngine::colLabel(section);   // A, B, … Z
        else
            return QString::number(section + 1);       // 1, 2, … 100
    }

    if (role == Qt::TextAlignmentRole)
        return Qt::AlignCenter;

    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell data
// ─────────────────────────────────────────────────────────────────────────────
QVariant SpreadsheetModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};

    const int col = index.column();
    const int row = index.row();
    const Cell  c   = cellAt(col, row);
    const auto& fmt = c.format;

    switch (role) {
    case Qt::DisplayRole: {
        // "Show Formulas" mode: render the raw content (e.g. "=A1+B1") verbatim.
        if (m_showFormulas) return c.content.isEmpty() ? QVariant{} : c.content;
        const QString display = displayValue(col, row);
        if (display.isEmpty()) return QVariant{};
        // Apply the cell's number format when the value is numeric.
        const QString nf = fmt.numberFormat;
        if (!nf.isEmpty() && nf != QLatin1String("General")
            && !display.startsWith('#')) {
            bool isNum = false;
            const double v = display.toDouble(&isNum);
            if (isNum) return formatNumber(v, nf);
        }
        return display;
    }
    case Qt::EditRole:
        return c.content;

    case Qt::TextAlignmentRole: {
        // Explicit alignment wins; otherwise auto (numbers right, text left).
        int h = fmt.hAlign;
        int v = fmt.vAlign ? fmt.vAlign : int(Qt::AlignVCenter);
        if (h == 0) {
            const QString display = displayValue(col, row);
            if (display.startsWith('#')) {
                h = Qt::AlignCenter;
            } else {
                bool isNum = false;
                display.toDouble(&isNum);
                h = isNum ? int(Qt::AlignRight) : int(Qt::AlignLeft);
            }
        }
        return QVariant(h | v);
    }

    case Qt::FontRole: {
        const CondFormatRule::Result cf = evalCondFormat(col, row);
        const bool boldEff = fmt.bold || (cf.matched && cf.bold);
        if (fmt.fontFamily.isEmpty() && fmt.fontSize == 0
            && !boldEff && !fmt.italic && !fmt.underline && !fmt.strike)
            return {};   // grid default font
        QFont f(fmt.fontFamily.isEmpty() ? QStringLiteral("Calibri") : fmt.fontFamily);
        // Scaled by the view zoom: a sheet saved at 70% has to draw its text
        // at 70% too, or the type overflows the cells it is meant to sit in.
        const double pt = (fmt.fontSize > 0 ? fmt.fontSize : 11) * m_viewZoom;
        f.setPointSizeF(qMax(1.0, pt));
        f.setBold(boldEff);
        f.setItalic(fmt.italic);
        f.setUnderline(fmt.underline);
        f.setStrikeOut(fmt.strike);
        return f;
    }

    case Qt::ForegroundRole: {
        // Conditional formatting overrides the cell's own colour when matched.
        const CondFormatRule::Result cf = evalCondFormat(col, row);
        if (cf.matched && cf.textColor.isValid())
            return cf.textColor;
        if (fmt.textColor.isValid())
            return fmt.textColor;
        const QString display = displayValue(col, row);
        if (display.startsWith('#'))         // error → red
            return QColor("#E8372A");
        bool isNum = false;
        display.toDouble(&isNum);
        if (isNum && c.content.startsWith('='))
            return QColor("#2563EB");        // formula result → blue
        return QColor("#1C1E26");            // text → charcoal
    }

    case Qt::BackgroundRole: {
        // Conditional formatting fill wins over the cell's own fill.
        const CondFormatRule::Result cf = evalCondFormat(col, row);
        if (cf.matched && cf.bgColor.isValid())
            return cf.bgColor;
        if (fmt.bgColor.isValid())
            return fmt.bgColor;
        return {};
    }

    case Qt::ToolTipRole: {
        const QString cm = comment(col, row);
        return cm.isEmpty() ? QVariant{} : cm;
    }

    default:
        return {};
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Edit (in-place delegate / programmatic) → routed through the undo stack
// ─────────────────────────────────────────────────────────────────────────────
bool SpreadsheetModel::setData(const QModelIndex& index,
                                const QVariant&    value,
                                int                role) {
    if (!index.isValid() || role != Qt::EditRole) return false;
    setCellContent(index.column(), index.row(), value.toString());
    return true;
}

Qt::ItemFlags SpreadsheetModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

// ─────────────────────────────────────────────────────────────────────────────
// Raw / display helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// Split a format code into its sections. Excel allows up to four
// (positive;negative;zero;text) and only one of them applies to a given value.
// The split has to skip separators inside quoted literals and inside the
// bracketed colour/condition tokens.
QStringList numFmtSections(const QString& code) {
    QStringList out;
    QString cur;
    for (int i = 0; i < code.size(); ++i) {
        const QChar c = code.at(i);
        if (c == QLatin1Char('"')) {
            cur += c;
            for (++i; i < code.size(); ++i) { cur += code.at(i);
                                              if (code.at(i) == QLatin1Char('"')) break; }
            continue;
        }
        if (c == QLatin1Char('[')) {
            cur += c;
            for (++i; i < code.size(); ++i) { cur += code.at(i);
                                              if (code.at(i) == QLatin1Char(']')) break; }
            continue;
        }
        if (c == QLatin1Char('\\')) { cur += c; if (++i < code.size()) cur += code.at(i); continue; }
        if (c == QLatin1Char(';')) { out << cur; cur.clear(); continue; }
        cur += c;
    }
    out << cur;
    return out;
}

// Strip everything from a section that says nothing about the value's type:
// bracketed colour/locale/condition tokens, quoted literal text, and
// backslash-escaped characters. What is left is the actual format pattern.
//
// This is the whole reason a currency format was being read as a date: [Red]
// contains a 'd', and the date test was looking at the raw string.
QString numFmtPattern(const QString& section) {
    QString out;
    for (int i = 0; i < section.size(); ++i) {
        const QChar c = section.at(i);
        if (c == QLatin1Char('[')) { while (i < section.size()
                                            && section.at(i) != QLatin1Char(']')) ++i; continue; }
        if (c == QLatin1Char('"')) { for (++i; i < section.size()
                                          && section.at(i) != QLatin1Char('"'); ++i) {} continue; }
        if (c == QLatin1Char('\\')) { ++i; continue; }
        out += c;
    }
    return out;
}

} // namespace

QString SpreadsheetModel::formatNumber(double value, const QString& code) {
    if (code.isEmpty() || code == QLatin1String("General"))
        return QString::number(value, 'g', 10);

    // Pick the section that applies to this value.
    const QStringList sections = numFmtSections(code);
    int si = 0;
    if (value < 0 && sections.size() > 1)       si = 1;
    else if (value == 0 && sections.size() > 2) si = 2;
    const QString section = sections.value(si);
    // An empty section means "show nothing", which is how ";;" hides a cell.
    if (section.trimmed().isEmpty()) return QString();

    // The pattern with colour tokens and literal text removed.
    const QString pat = numFmtPattern(section);

    // ── Date / time codes (value is an Excel serial: days since 1899-12-30) ────
    const bool dateCode = pat.contains('y') || pat.contains('d')
                          || pat.contains(QLatin1String("mmm"));
    const bool timeCode = pat.contains('h') || pat.contains('s');
    if (dateCode || timeCode) {
        const QDateTime base(QDate(1899, 12, 30), QTime(0, 0));
        const QDateTime dt = base.addMSecs(static_cast<qint64>(std::llround(value * 86400000.0)));
        static const QHash<QString, QString> map = {
            {"yyyy-mm-dd","yyyy-MM-dd"}, {"yyyy/mm/dd","yyyy/MM/dd"},
            {"mm/dd/yyyy","MM/dd/yyyy"}, {"m/d/yyyy","M/d/yyyy"},
            {"dd/mm/yyyy","dd/MM/yyyy"}, {"d/m/yyyy","d/M/yyyy"},
            {"d-mmm-yyyy","d-MMM-yyyy"}, {"dd-mmm-yy","dd-MMM-yy"},
            {"d mmm yyyy","d MMM yyyy"}, {"mmm yyyy","MMM yyyy"},
            {"mmmm yyyy","MMMM yyyy"},   {"mmm-yy","MMM-yy"},
            {"h:mm","h:mm"}, {"hh:mm","hh:mm"}, {"h:mm:ss","h:mm:ss"},
            {"hh:mm:ss","hh:mm:ss"}, {"h:mm am/pm","h:mm AP"},
        };
        QString qf = map.value(pat.toLower().trimmed());
        if (qf.isEmpty()) qf = map.value(code.toLower());
        if (qf.isEmpty())
            qf = dateCode ? (timeCode ? "yyyy-MM-dd h:mm" : "yyyy-MM-dd") : "h:mm:ss";
        return dt.toString(qf);
    }

    const bool percent   = pat.contains('%');
    // The currency symbol is a quoted literal ("$"), so it is looked for in the
    // section rather than in the stripped pattern.
    const bool currency  = section.contains('$');
    const bool thousands = pat.contains(',');

    // Decimal places = run of '0'/'#' immediately after the decimal point.
    int decimals = 0;
    const int dot = pat.indexOf('.');
    if (dot >= 0) {
        for (int i = dot + 1; i < pat.size(); ++i) {
            const QChar ch = pat[i];
            if (ch == '0' || ch == '#') ++decimals;
            else break;
        }
    }

    double val = percent ? value * 100.0 : value;
    const bool neg = val < 0.0;
    const double av = std::abs(val);

    const QString num = thousands
        ? QLocale(QLocale::English).toString(av, 'f', decimals)
        : QString::number(av, 'f', decimals);

    QString out;
    if (neg)      out += '-';
    if (currency) out += '$';
    out += num;
    if (percent)  out += '%';
    return out;
}

QRect SpreadsheetModel::mergeContaining(int col, int row) const {
    for (const QRect& r : m_merges)
        if (r.contains(col, row)) return r;
    return {};
}

void SpreadsheetModel::removeMergeContaining(int col, int row) {
    for (int i = 0; i < m_merges.size(); ++i)
        if (m_merges[i].contains(col, row)) { m_merges.removeAt(i); return; }
}

Cell SpreadsheetModel::cellAt(int col, int row) const {
    auto it = m_data.find(cellKey(col, row));
    return (it != m_data.end()) ? it->second : Cell{};
}

QString SpreadsheetModel::rawContent(int col, int row) const {
    auto it = m_data.find(cellKey(col, row));
    return (it != m_data.end()) ? it->second.content : QString{};
}

QString SpreadsheetModel::displayValue(int col, int row) const {
    // qint64 for the same reason as applyCellRaw: an int wraps at this grid size.
    const qint64 key = cellKey(col, row);

    // Circular-reference guard
    if (m_evaluating[key]) return "#CIRC";
    m_evaluating[key] = true;

    QString raw = rawContent(col, row);

    // Substitute defined names (whole-word, case-insensitive) with their refs so
    // formulas like =SUM(Sales) work. Only inside formulas.
    if (raw.startsWith('=') && !m_definedNames.isEmpty()) {
        for (auto it = m_definedNames.begin(); it != m_definedNames.end(); ++it) {
            QRegularExpression re("\\b" + QRegularExpression::escape(it.key()) + "\\b",
                                  QRegularExpression::CaseInsensitiveOption);
            raw.replace(re, it.value());
        }
    }

    // Lookup returns the *evaluated* value of a referenced cell. Same-sheet refs
    // recurse through displayValue (per-cell circular guard above); cross-sheet
    // refs go through the workbook resolver installed by CalcModule.
    const FormulaEngine::CellLookup lookup =
        [this](const QString& sheet, int c, int r) -> QString {
            if (sheet.isEmpty()) return displayValue(c, r);
            return m_crossSheetLookup ? m_crossSheetLookup(sheet, c, r)
                                      : QString("#REF!");
        };

    const QString result = m_engine.evaluate(raw, lookup);
    m_evaluating[key]    = false;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Conditional formatting
// ─────────────────────────────────────────────────────────────────────────────
// Evaluate every rule covering (col,row). Each rule's formula has its relative
// references shifted by the cell's offset from the rule's range top-left, then
// is run through the same FormulaEngine as ordinary cells. A rule matches when
// its formula yields TRUE (or a non-zero number). List order is priority order
// (index 0 highest); the first rule to set a property wins it.
void SpreadsheetModel::setViewZoom(double z) {
    if (z <= 0) z = 1.0;
    if (qFuzzyCompare(m_viewZoom, z)) return;
    m_viewZoom = z;
    notifyAllChanged();          // every cell has to be re-measured and repainted
}

CondFormatRule::Result SpreadsheetModel::evalCondFormat(int col, int row) const {
    CondFormatRule::Result res;
    if (m_condRules.isEmpty()) return res;

    const qint64 key = cellKey(col, row);
    const auto cached = m_cfCache.constFind(key);
    if (cached != m_cfCache.constEnd()) return *cached;

    const FormulaEngine::CellLookup lookup =
        [this](const QString& sheet, int c, int r) -> QString {
            if (sheet.isEmpty()) return displayValue(c, r);
            return m_crossSheetLookup ? m_crossSheetLookup(sheet, c, r)
                                      : QString("#REF!");
        };

    for (int ruleIdx = 0; ruleIdx < m_condRules.size(); ++ruleIdx) {
        const CondFormatRule& rule = m_condRules.at(ruleIdx);
        if (!rule.range.contains(col, row)) continue;

        // A data bar is sized from the value, not from a condition.
        if (rule.barColor.isValid()) {
            bool ok = false;
            const double v = displayValue(col, row).toDouble(&ok);
            if (!ok) continue;
            double lo = rule.barMin, hi = rule.barMax;
            if (hi <= lo) {
                // No explicit range in the file: scale against the extremes of
                // the cells the rule covers, which is what "automatic" means.
                // Computed once per rule, not once per cell: doing it per cell
                // made a repaint quadratic in the size of the banded range.
                const auto hit = m_barRangeCache.constFind(ruleIdx);
                if (hit != m_barRangeCache.constEnd()) {
                    lo = hit->first; hi = hit->second;
                } else {
                    lo = 0.0; hi = 0.0;
                    for (int r2 = rule.range.top(); r2 <= rule.range.bottom(); ++r2)
                        for (int c2 = rule.range.left(); c2 <= rule.range.right(); ++c2) {
                            bool o = false;
                            const double d = displayValue(c2, r2).toDouble(&o);
                            if (!o) continue;
                            lo = qMin(lo, d);
                            hi = qMax(hi, d);
                        }
                    m_barRangeCache.insert(ruleIdx, qMakePair(lo, hi));
                }
            }
            if (hi <= lo) continue;
            res.matched     = true;
            res.barColor    = rule.barColor;
            res.barFraction = qBound(0.0, (v - lo) / (hi - lo), 1.0);
            continue;
            
        }

        if (!rule.formula.startsWith('=')) continue;

        const int dCol = col - rule.range.left();
        const int dRow = row - rule.range.top();
        const QString concrete = shiftCondFormula(rule.formula, dCol, dRow);
        const QString r = m_engine.evaluate(concrete, lookup).trimmed();

        bool truthy = (r.compare(QStringLiteral("TRUE"), Qt::CaseInsensitive) == 0);
        if (!truthy) {
            bool isNum = false;
            const double v = r.toDouble(&isNum);
            if (isNum && v != 0.0) truthy = true;
        }
        if (!truthy) continue;

        res.matched = true;
        if (rule.bgColor.isValid()   && !res.bgColor.isValid())   res.bgColor   = rule.bgColor;
        if (rule.textColor.isValid() && !res.textColor.isValid()) res.textColor = rule.textColor;
        if (rule.bold) res.bold = true;
    }
    m_cfCache.insert(key, res);
    return res;
}

// ─────────────────────────────────────────────────────────────────────────────
// Mutation API
// ─────────────────────────────────────────────────────────────────────────────
void SpreadsheetModel::setCellContent(int col, int row,
                                      const QString& content,
                                      const QString& undoText) {
    Cell before = cellAt(col, row);
    if (before.content == content) return;   // no-op

    Cell after = before;
    after.content = content;

    std::vector<CellChange> changes;
    changes.push_back({col, row, before, after});
    m_undo->push(new CellsChangeCommand(this, std::move(changes), undoText));
}

void SpreadsheetModel::applyCellEdits(
        const std::vector<std::pair<QPoint, Cell>>& edits,
        const QString& undoText) {
    std::vector<CellChange> changes;
    changes.reserve(edits.size());
    for (const auto& [pt, after] : edits) {
        const int col = pt.x();
        const int row = pt.y();
        Cell before = cellAt(col, row);
        if (before == after) continue;
        changes.push_back({col, row, before, after});
    }
    if (changes.empty()) return;
    m_undo->push(new CellsChangeCommand(this, std::move(changes), undoText));
}

// ── Row / column operations ───────────────────────────────────────────────────
// Fixed grid: we shift cell data (last row/col falls off the end) and push it as
// one undoable CellsChangeCommand. 'after' values are read from the current
// snapshot via cellAt(); applyCellEdits records the 'before' and filters no-ops.
// Each op rebuilds the whole grid: cell data is shifted AND every formula's
// references are rewritten (so e.g. =SUM(A1:A5) tracks an inserted row).
// applyCellEdits records before/after and filters no-ops.
void SpreadsheetModel::insertRowAt(int row) {
    if (row < 0 || row >= NUM_ROWS) return;
    int maxCol, maxRow;
    usedBounds(maxCol, maxRow);
    if (maxCol < 0) return;                       // empty sheet, nothing to shift
    const int lastRow = qMin(maxRow + 1, NUM_ROWS - 1);
    std::vector<std::pair<QPoint, Cell>> edits;
    for (int r = 0; r <= lastRow; ++r)
        for (int c = 0; c <= maxCol; ++c) {
            Cell after = (r < row) ? cellAt(c, r)
                       : (r == row) ? Cell{} : cellAt(c, r - 1);
            after.content = rewriteFormula(after.content, GridOp::InsertRow, row);
            edits.push_back({QPoint(c, r), after});
        }
    applyCellEdits(edits, "Insert Row");
}

void SpreadsheetModel::deleteRowAt(int row) {
    if (row < 0 || row >= NUM_ROWS) return;
    int maxCol, maxRow;
    usedBounds(maxCol, maxRow);
    if (maxCol < 0) return;
    std::vector<std::pair<QPoint, Cell>> edits;
    for (int r = 0; r <= maxRow; ++r)
        for (int c = 0; c <= maxCol; ++c) {
            Cell after = (r < row) ? cellAt(c, r)
                       : (r >= maxRow) ? Cell{} : cellAt(c, r + 1);
            after.content = rewriteFormula(after.content, GridOp::DeleteRow, row);
            edits.push_back({QPoint(c, r), after});
        }
    applyCellEdits(edits, "Delete Row");
}

void SpreadsheetModel::insertColumnAt(int col) {
    if (col < 0 || col >= NUM_COLS) return;
    int maxCol, maxRow;
    usedBounds(maxCol, maxRow);
    if (maxCol < 0) return;
    const int lastCol = qMin(maxCol + 1, NUM_COLS - 1);
    std::vector<std::pair<QPoint, Cell>> edits;
    for (int c = 0; c <= lastCol; ++c)
        for (int r = 0; r <= maxRow; ++r) {
            Cell after = (c < col) ? cellAt(c, r)
                       : (c == col) ? Cell{} : cellAt(c - 1, r);
            after.content = rewriteFormula(after.content, GridOp::InsertCol, col);
            edits.push_back({QPoint(c, r), after});
        }
    applyCellEdits(edits, "Insert Column");
}

void SpreadsheetModel::deleteColumnAt(int col) {
    if (col < 0 || col >= NUM_COLS) return;
    int maxCol, maxRow;
    usedBounds(maxCol, maxRow);
    if (maxCol < 0) return;
    std::vector<std::pair<QPoint, Cell>> edits;
    for (int c = 0; c <= maxCol; ++c)
        for (int r = 0; r <= maxRow; ++r) {
            Cell after = (c < col) ? cellAt(c, r)
                       : (c >= maxCol) ? Cell{} : cellAt(c + 1, r);
            after.content = rewriteFormula(after.content, GridOp::DeleteCol, col);
            edits.push_back({QPoint(c, r), after});
        }
    applyCellEdits(edits, "Delete Column");
}

// ── Raw-apply hooks (used by CellsChangeCommand) ──────────────────────────────
void SpreadsheetModel::applyCellRaw(int col, int row, const Cell& cell) {
    // qint64: at the full grid size row * NUM_COLS + col does not fit in an int,
    // and truncating it here would alias distant cells onto one another.
    const qint64 key = cellKey(col, row);
    if (cell.isEmpty())
        m_data.erase(key);
    else
        m_data[key] = cell;
    if (!m_cfCache.isEmpty())      m_cfCache.clear();
    if (!m_barRangeCache.isEmpty()) m_barRangeCache.clear();
}

void SpreadsheetModel::notifyAllChanged() {
    // The conditional-format caches are pure functions of the cell data, so
    // they die with it.
    m_cfCache.clear();
    m_barRangeCache.clear();
    // Invalidate the sheet so dependent formula cells repaint.
    //
    // Bounded to the range that actually holds something. This used to span the
    // whole grid, which was harmless when the grid was 100x26 but is 17 billion
    // cells now: the view walked it on every recalculation and a sheet took a
    // minute to open. Conditional-format ranges are included because a rule can
    // paint a cell that holds no value of its own.
    int maxCol, maxRow;
    usedBounds(maxCol, maxRow);
    for (const CondFormatRule& r : m_condRules) {
        maxCol = qMax(maxCol, r.range.right());
        maxRow = qMax(maxRow, r.range.bottom());
    }
    if (maxCol < 0 || maxRow < 0) return;          // nothing to repaint
    maxCol = qMin(maxCol, NUM_COLS - 1);
    maxRow = qMin(maxRow, NUM_ROWS - 1);

    emit dataChanged(index(0, 0),
                     index(maxRow, maxCol),
                     {Qt::DisplayRole, Qt::EditRole, Qt::ForegroundRole,
                      Qt::BackgroundRole, Qt::FontRole, Qt::TextAlignmentRole});
}

void SpreadsheetModel::clearAll() {
    beginResetModel();
    m_data.clear();
    endResetModel();
    m_undo->clear();
}

} // namespace NativeOffice
