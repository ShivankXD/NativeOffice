#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SpreadsheetModel.h  (Sprint 4 → Sprint 10)
// QAbstractTableModel backing the NativeOffice Calc grid.
//
// Grid size: 100 rows × 26 columns  (configurable via NUM_ROWS / NUM_COLS)
// Columns are labelled A–Z (col 0 = A … col 25 = Z).
// Rows are labelled 1–100.
//
// Sprint 10 — Modernization Foundation:
//   • Cells now store a rich Cell (content + CellFormat) instead of a bare
//     QString, so font/colour/alignment/number-format can ride along.
//   • All edits funnel through a QUndoStack (CellsChangeCommand) so single
//     edits, range deletes and pastes are uniformly undo-able.
//   • Raw-apply hooks (applyCellRaw / notifyAllChanged) let commands replay
//     mutations without re-entering the undo machinery.
//
// Roles:
//   Qt::DisplayRole        → evaluated display value (formula result or raw text)
//   Qt::EditRole           → raw cell content (what the user typed, e.g. "=A1+B1")
//   Qt::TextAlignmentRole  → explicit format, else auto (numbers right, text left)
//   Qt::FontRole           → per-cell font (family/size/bold/italic/underline)
//   Qt::ForegroundRole     → per-cell text colour (else error red / formula blue)
//   Qt::BackgroundRole     → per-cell fill colour
//
// Formula evaluation is delegated to FormulaEngine.
// ─────────────────────────────────────────────────────────────────────────────

#include "Cell.h"
#include "ChartSpec.h"
#include "CondFormat.h"
#include "FormulaEngine.h"

#include <QAbstractTableModel>
#include <QString>
#include <QStringList>
#include <QPoint>
#include <QRect>
#include <QVector>
#include <QHash>
#include <unordered_map>
#include <utility>
#include <vector>

class QUndoStack;

namespace NativeOffice {

struct CellChange;

class SpreadsheetModel : public QAbstractTableModel {
    Q_OBJECT

public:
    static constexpr int NUM_ROWS = 100;
    static constexpr int NUM_COLS = 26;

    explicit SpreadsheetModel(QObject* parent = nullptr);

    // ── QAbstractTableModel interface ─────────────────────────────────────
    int      rowCount   (const QModelIndex& parent = {}) const override;
    int      columnCount(const QModelIndex& parent = {}) const override;
    QVariant data       (const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool     setData    (const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    QVariant headerData (int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags (const QModelIndex& index) const override;

    // ── Convenience API ───────────────────────────────────────────────────
    // Get the raw (un-evaluated) content of a cell.
    [[nodiscard]] QString rawContent(int col, int row) const;

    // Get the evaluated display string of a cell.
    [[nodiscard]] QString displayValue(int col, int row) const;

    // Full cell (content + format). Returns a default Cell for empty coords.
    [[nodiscard]] Cell cellAt(int col, int row) const;

    // Clear all cells (silent — wipes the undo history too).
    void clearAll();

    // ── Undo/redo ─────────────────────────────────────────────────────────
    [[nodiscard]] QUndoStack* undoStack() const { return m_undo; }

    // Push a single command that sets a batch of cells (used by paste/delete).
    // Records before/after automatically given the desired 'after' cells.
    void applyCellEdits(const std::vector<std::pair<QPoint, Cell>>& edits,
                        const QString& undoText);

    // Set just the content of one cell (formula-bar / in-place edit), preserving
    // existing format. Goes through the undo stack.
    void setCellContent(int col, int row, const QString& content,
                        const QString& undoText = QStringLiteral("Edit Cell"));

    // ── Row / column operations (fixed grid; data is shifted, undoable) ──────
    void insertRowAt(int row);       // shift rows ≥ row down; clear 'row'
    void deleteRowAt(int row);       // shift rows > row up; clear last row
    void insertColumnAt(int col);    // shift cols ≥ col right; clear 'col'
    void deleteColumnAt(int col);    // shift cols > col left; clear last col

    // ── Raw-apply hooks (used by CellsChangeCommand — not for general use) ──
    void applyCellRaw(int col, int row, const Cell& cell);   // mutate map, no signal
    void notifyAllChanged();                                 // emit whole-sheet dataChanged

    // Read-only access to the sparse cell store (for serialization).
    [[nodiscard]] const std::unordered_map<int, Cell>& cells() const { return m_data; }

    // Static key helper (col,row) → flat int (exposed for serialization).
    static int cellKey(int col, int row) { return row * NUM_COLS + col; }
    static int keyCol(int key) { return key % NUM_COLS; }
    static int keyRow(int key) { return key / NUM_COLS; }

    // Apply a number-format code (e.g. "0.00", "$#,##0.00", "0%") to a value.
    [[nodiscard]] static QString formatNumber(double value, const QString& code);

    // ── Merged cell ranges (left=col1, top=row1, right=col2, bottom=row2) ────
    [[nodiscard]] const QVector<QRect>& merges() const { return m_merges; }
    void  setMerges(const QVector<QRect>& m) { m_merges = m; }
    void  addMerge(const QRect& r) { if (r.isValid()) m_merges.push_back(r); }
    void  clearMerges() { m_merges.clear(); }
    void  removeMergeContaining(int col, int row);
    [[nodiscard]] QRect mergeContaining(int col, int row) const;

    // ── Custom column widths / row heights (pixels; absent = grid default) ───
    [[nodiscard]] const QHash<int, int>& colWidths()  const { return m_colWidths;  }
    [[nodiscard]] const QHash<int, int>& rowHeights() const { return m_rowHeights; }
    void setColWidth(int col, int px)  { m_colWidths.insert(col, px);  }
    void setRowHeight(int row, int px) { m_rowHeights.insert(row, px); }
    void setColWidths(const QHash<int, int>& w)  { m_colWidths = w;  }
    void setRowHeights(const QHash<int, int>& h) { m_rowHeights = h; }

    // ── Show Formulas mode (display raw content instead of evaluated value) ──
    void setShowFormulas(bool on) {
        if (m_showFormulas == on) return;
        m_showFormulas = on;
        notifyAllChanged();
    }
    [[nodiscard]] bool showFormulas() const { return m_showFormulas; }

    // ── Defined names (name → "A1" / "A1:B2" reference) ──────────────────────
    [[nodiscard]] const QHash<QString, QString>& definedNames() const { return m_definedNames; }
    void setDefinedName(const QString& name, const QString& ref) { m_definedNames.insert(name.toUpper(), ref); notifyAllChanged(); }
    void removeDefinedName(const QString& name) { m_definedNames.remove(name.toUpper()); notifyAllChanged(); }

    // ── Cell comments (cellKey → text) ───────────────────────────────────────
    [[nodiscard]] QString comment(int col, int row) const { return m_comments.value(cellKey(col, row)); }
    [[nodiscard]] bool hasComment(int col, int row) const { return m_comments.contains(cellKey(col, row)); }
    void setComment(int col, int row, const QString& text) {
        if (text.isEmpty()) m_comments.remove(cellKey(col, row));
        else m_comments.insert(cellKey(col, row), text);
        notifyAllChanged();
    }
    [[nodiscard]] const QHash<int, QString>& comments() const { return m_comments; }
    void setShowCommentMarkers(bool on) { m_showCommentMarkers = on; notifyAllChanged(); }
    [[nodiscard]] bool showCommentMarkers() const { return m_showCommentMarkers; }

    // ── Drop-down validation lists (col → allowed values) ────────────────────
    [[nodiscard]] QStringList validationList(int col) const { return m_validations.value(col); }
    void setValidationList(int col, const QStringList& items) {
        if (items.isEmpty()) m_validations.remove(col); else m_validations.insert(col, items);
    }

    // ── Charts (specs only; live widgets live in CalcModule) ─────────────────
    [[nodiscard]] const QVector<ChartSpec>& charts() const { return m_charts; }
    void setCharts(const QVector<ChartSpec>& c) { m_charts = c; }
    void addChart(const ChartSpec& c) { m_charts.push_back(c); }
    void clearCharts() { m_charts.clear(); }

    // ── Conditional formatting rules (stored in the data model) ──────────────
    // Rules are evaluated live in data() for the colour/font roles. List order is
    // priority order: index 0 is highest priority and wins each property it sets.
    [[nodiscard]] const QVector<CondFormatRule>& condRules() const { return m_condRules; }
    void setCondRules(const QVector<CondFormatRule>& r) { m_condRules = r; notifyAllChanged(); }
    void addCondRule(const CondFormatRule& r) { m_condRules.push_back(r); notifyAllChanged(); }
    void updateCondRule(int i, const CondFormatRule& r) {
        if (i >= 0 && i < m_condRules.size()) { m_condRules[i] = r; notifyAllChanged(); }
    }
    void removeCondRule(int i) {
        if (i >= 0 && i < m_condRules.size()) { m_condRules.remove(i); notifyAllChanged(); }
    }
    void clearCondRules() { m_condRules.clear(); notifyAllChanged(); }
    // Evaluate every rule covering (col,row); returns the merged styling override.
    [[nodiscard]] CondFormatRule::Result evalCondFormat(int col, int row) const;

    // ── Workbook context (multi-sheet) ──────────────────────────────────────
    void setSheetName(const QString& name) { m_sheetName = name; }
    [[nodiscard]] QString sheetName() const { return m_sheetName; }

    // Resolver for cross-sheet references: given (sheetName, col, row) returns
    // that sheet's evaluated display value. Set by the owning CalcModule.
    using CrossSheetLookup = std::function<QString(const QString&, int, int)>;
    void setCrossSheetLookup(CrossSheetLookup fn) { m_crossSheetLookup = std::move(fn); }

private:
    // Recursion-guard: tracks which cells are currently being evaluated.
    mutable std::unordered_map<int, bool> m_evaluating;

    // The sparse cell store (only non-default cells are stored).
    std::unordered_map<int, Cell> m_data;

    FormulaEngine    m_engine;
    QUndoStack*      m_undo { nullptr };
    bool             m_silent { false };   // bypass undo (load / clear)
    bool             m_showFormulas { false };
    QString          m_sheetName;
    CrossSheetLookup m_crossSheetLookup;
    QVector<QRect>   m_merges;
    QHash<int, int>  m_colWidths;
    QHash<int, int>  m_rowHeights;
    QVector<ChartSpec> m_charts;
    QVector<CondFormatRule> m_condRules;
    QHash<QString, QString> m_definedNames;
    QHash<int, QString>     m_comments;
    QHash<int, QStringList> m_validations;
    bool             m_showCommentMarkers { true };
};

} // namespace NativeOffice
