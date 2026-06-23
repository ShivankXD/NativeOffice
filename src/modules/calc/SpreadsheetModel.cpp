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
        if (fmt.fontFamily.isEmpty() && fmt.fontSize == 0
            && !fmt.bold && !fmt.italic && !fmt.underline && !fmt.strike)
            return {};   // grid default font
        QFont f(fmt.fontFamily.isEmpty() ? QStringLiteral("Calibri") : fmt.fontFamily);
        f.setPointSize(fmt.fontSize > 0 ? fmt.fontSize : 11);
        f.setBold(fmt.bold);
        f.setItalic(fmt.italic);
        f.setUnderline(fmt.underline);
        f.setStrikeOut(fmt.strike);
        return f;
    }

    case Qt::ForegroundRole: {
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

    case Qt::BackgroundRole:
        if (fmt.bgColor.isValid())
            return fmt.bgColor;
        return {};

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
QString SpreadsheetModel::formatNumber(double value, const QString& code) {
    if (code.isEmpty() || code == QLatin1String("General"))
        return QString::number(value, 'g', 10);

    // ── Date / time codes (value is an Excel serial: days since 1899-12-30) ────
    const bool dateCode = code.contains('y') || code.contains('d')
                          || code.contains(QLatin1String("mmm"));
    const bool timeCode = code.contains('h') || code.contains('s');
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
        QString qf = map.value(code.toLower());
        if (qf.isEmpty())
            qf = dateCode ? (timeCode ? "yyyy-MM-dd h:mm" : "yyyy-MM-dd") : "h:mm:ss";
        return dt.toString(qf);
    }

    const bool percent   = code.contains('%');
    const bool currency  = code.contains('$');
    const bool thousands = code.contains(',');

    // Decimal places = run of '0'/'#' immediately after the decimal point.
    int decimals = 0;
    const int dot = code.indexOf('.');
    if (dot >= 0) {
        for (int i = dot + 1; i < code.size(); ++i) {
            const QChar ch = code[i];
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
    const int key = cellKey(col, row);

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
    std::vector<std::pair<QPoint, Cell>> edits;
    for (int r = 0; r < NUM_ROWS; ++r)
        for (int c = 0; c < NUM_COLS; ++c) {
            Cell after = (r < row) ? cellAt(c, r)
                       : (r == row) ? Cell{} : cellAt(c, r - 1);
            after.content = rewriteFormula(after.content, GridOp::InsertRow, row);
            edits.push_back({QPoint(c, r), after});
        }
    applyCellEdits(edits, "Insert Row");
}

void SpreadsheetModel::deleteRowAt(int row) {
    if (row < 0 || row >= NUM_ROWS) return;
    std::vector<std::pair<QPoint, Cell>> edits;
    for (int r = 0; r < NUM_ROWS; ++r)
        for (int c = 0; c < NUM_COLS; ++c) {
            Cell after = (r < row) ? cellAt(c, r)
                       : (r == NUM_ROWS - 1) ? Cell{} : cellAt(c, r + 1);
            after.content = rewriteFormula(after.content, GridOp::DeleteRow, row);
            edits.push_back({QPoint(c, r), after});
        }
    applyCellEdits(edits, "Delete Row");
}

void SpreadsheetModel::insertColumnAt(int col) {
    if (col < 0 || col >= NUM_COLS) return;
    std::vector<std::pair<QPoint, Cell>> edits;
    for (int c = 0; c < NUM_COLS; ++c)
        for (int r = 0; r < NUM_ROWS; ++r) {
            Cell after = (c < col) ? cellAt(c, r)
                       : (c == col) ? Cell{} : cellAt(c - 1, r);
            after.content = rewriteFormula(after.content, GridOp::InsertCol, col);
            edits.push_back({QPoint(c, r), after});
        }
    applyCellEdits(edits, "Insert Column");
}

void SpreadsheetModel::deleteColumnAt(int col) {
    if (col < 0 || col >= NUM_COLS) return;
    std::vector<std::pair<QPoint, Cell>> edits;
    for (int c = 0; c < NUM_COLS; ++c)
        for (int r = 0; r < NUM_ROWS; ++r) {
            Cell after = (c < col) ? cellAt(c, r)
                       : (c == NUM_COLS - 1) ? Cell{} : cellAt(c + 1, r);
            after.content = rewriteFormula(after.content, GridOp::DeleteCol, col);
            edits.push_back({QPoint(c, r), after});
        }
    applyCellEdits(edits, "Delete Column");
}

// ── Raw-apply hooks (used by CellsChangeCommand) ──────────────────────────────
void SpreadsheetModel::applyCellRaw(int col, int row, const Cell& cell) {
    const int key = cellKey(col, row);
    if (cell.isEmpty())
        m_data.erase(key);
    else
        m_data[key] = cell;
}

void SpreadsheetModel::notifyAllChanged() {
    // Invalidate the whole sheet so dependent formula cells repaint.
    emit dataChanged(index(0, 0),
                     index(NUM_ROWS - 1, NUM_COLS - 1),
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
