#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SpreadsheetModel.h  (Sprint 4)
// QAbstractTableModel backing the NativeOffice Calc grid.
//
// Grid size: 100 rows × 26 columns  (configurable via NUM_ROWS / NUM_COLS)
// Columns are labelled A–Z (col 0 = A … col 25 = Z).
// Rows are labelled 1–100.
//
// Roles:
//   Qt::DisplayRole  → evaluated display value (formula result or raw text)
//   Qt::EditRole     → raw cell content (what the user typed, e.g. "=A1+B1")
//   Qt::TextAlignmentRole → numbers right-aligned, text left-aligned
//
// Formula evaluation is delegated to FormulaEngine.
// ─────────────────────────────────────────────────────────────────────────────

#include "FormulaEngine.h"

#include <QAbstractTableModel>
#include <QString>
#include <unordered_map>

namespace NativeOffice {

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

    // Clear all cells.
    void clearAll();

private:
    // Flat key from (col, row) for the hash map
    static int cellKey(int col, int row) { return row * NUM_COLS + col; }

    // Recursion-guard: tracks which cells are currently being evaluated.
    // Uses mutable so we can mark/unmark inside const evaluate().
    mutable std::unordered_map<int, bool> m_evaluating;

    // The raw cell data store (only non-empty cells are stored)
    std::unordered_map<int, QString> m_data;

    FormulaEngine m_engine;
};

} // namespace NativeOffice
