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
    // The grid a workbook is allowed to use. This was 100x26, which silently
    // truncated any imported sheet with data past row 100 or column Z, and made
    // it impossible to place a chart or picture anchored beyond that. Real
    // files routinely go further, so the grid now covers column IV and 20000
    // rows. Cells are stored sparsely, so the cost of the larger grid is in the
    // view's section handling, not in memory: every full-grid loop over these
    // constants has been made proportional to the used range instead.
    // These are Excel's own limits. cellKey() is row * NUM_COLS + col, which at
    // this size is 17,179,869,184 and overflows a 32-bit int, so the key is a
    // qint64 and the maps are keyed by that.
    static constexpr int NUM_ROWS = 1048576;
    static constexpr int NUM_COLS = 16384;

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
    [[nodiscard]] const std::unordered_map<qint64, Cell>& cells() const { return m_data; }

    // Static key helper (col,row) → flat int (exposed for serialization).
    // qint64: at Excel's dimensions this product does not fit in an int, and
    // silently wrapping would alias distant cells onto each other.
    // The furthest column/row that holds anything. Grid operations are sized
    // from this rather than from NUM_COLS/NUM_ROWS: the grid is Excel-sized and
    // rebuilding all of it would mean billions of entries for one inserted row.
    void usedBounds(int& maxCol, int& maxRow) const {
        maxCol = -1; maxRow = -1;
        for (const auto& kv : m_data) {
            if (kv.second.content.isEmpty() && kv.second.format.isDefault()) continue;
            maxCol = qMax(maxCol, keyCol(kv.first));
            maxRow = qMax(maxRow, keyRow(kv.first));
        }
    }

    static qint64 cellKey(int col, int row) {
        return static_cast<qint64>(row) * NUM_COLS + col;
    }
    static int keyCol(qint64 key) { return static_cast<int>(key % NUM_COLS); }
    static int keyRow(qint64 key) { return static_cast<int>(key / NUM_COLS); }

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
    [[nodiscard]] const QHash<qint64, QString>& comments() const { return m_comments; }
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

    // Pictures sitting on the sheet, kept next to the charts for the same
    // reason: they belong to the sheet, not to the view showing it.
    [[nodiscard]] const QVector<SheetImage>& images() const { return m_images; }
    void setImages(const QVector<SheetImage>& i) { m_images = i; }
    void addImage(const SheetImage& i) { m_images.push_back(i); }
    void clearImages() { m_images.clear(); }

    // Drawn shapes: banners, buttons, callouts, rules. Kept alongside the
    // charts and pictures because they are the same kind of thing, an object
    // that belongs to the sheet rather than to a cell.
    [[nodiscard]] const QVector<SheetShape>& shapes() const { return m_shapes; }
    void setShapes(const QVector<SheetShape>& s) { m_shapes = s; }
    void clearShapes() { m_shapes.clear(); }

    // ── Conditional formatting rules (stored in the data model) ──────────────
    // Rules are evaluated live in data() for the colour/font roles. List order is
    // priority order: index 0 is highest priority and wins each property it sets.
    // A workbook stores whether the grid is drawn. The delegate paints the
    // gridlines, so it has to ask the model rather than the view.
    // Rows and columns the file marks hidden, and the zoom it was saved at.
    [[nodiscard]] const QVector<int>& hiddenCols() const { return m_hiddenCols; }
    [[nodiscard]] const QVector<int>& hiddenRows() const { return m_hiddenRows; }
    void setHiddenCols(const QVector<int>& c) { m_hiddenCols = c; }
    void setHiddenRows(const QVector<int>& r) { m_hiddenRows = r; }
    // The zoom the view is currently drawing at. It lives here because the
    // font size is served from FontRole, so this is the only place that can
    // scale the type along with the cells.
    [[nodiscard]] double viewZoom() const { return m_viewZoom; }
    void setViewZoom(double z);

    [[nodiscard]] int  zoomScale() const { return m_zoomScale; }
    void setZoomScale(int z) { if (z >= 10 && z <= 400) m_zoomScale = z; }

    // Hidden sheets keep their data (charts reference them) but get no tab.
    [[nodiscard]] bool isHidden() const { return m_hidden; }
    void setHidden(bool h) { m_hidden = h; }

    [[nodiscard]] bool showGridLines() const { return m_showGridLines; }
    void setShowGridLines(bool on) { m_showGridLines = on; notifyAllChanged(); }

    [[nodiscard]] const QVector<CondFormatRule>& condRules() const { return m_condRules; }
    void setCondRules(const QVector<CondFormatRule>& r) {
        m_condRules = r; m_cfCache.clear(); m_barRangeCache.clear(); notifyAllChanged();
    }
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
    mutable std::unordered_map<qint64, bool> m_evaluating;

    // The sparse cell store (only non-default cells are stored).
    std::unordered_map<qint64, Cell> m_data;

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
    QVector<SheetImage> m_images;
    QVector<SheetShape> m_shapes;
    QVector<CondFormatRule> m_condRules;
    // Conditional formatting is evaluated per cell, several times per repaint.
    // Both of these are pure functions of the cell data, so they are cached and
    // dropped wholesale whenever anything changes.
    mutable QHash<qint64, CondFormatRule::Result> m_cfCache;
    mutable QHash<int, QPair<double, double>>     m_barRangeCache;
    bool m_showGridLines { true };
    bool m_hidden { false };
    QVector<int> m_hiddenCols;
    QVector<int> m_hiddenRows;
    int  m_zoomScale { 100 };
    double m_viewZoom { 1.0 };
    QHash<QString, QString> m_definedNames;
    QHash<qint64, QString>  m_comments;
    QHash<int, QStringList> m_validations;
    bool             m_showCommentMarkers { true };
};

} // namespace NativeOffice
