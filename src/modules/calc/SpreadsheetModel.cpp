// ─────────────────────────────────────────────────────────────────────────────
// SpreadsheetModel.cpp  (Sprint 4)
// ─────────────────────────────────────────────────────────────────────────────
#include "SpreadsheetModel.h"
#include "FormulaEngine.h"

#include <QColor>

namespace NativeOffice {

SpreadsheetModel::SpreadsheetModel(QObject* parent)
    : QAbstractTableModel(parent)
{}

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

    switch (role) {
    case Qt::DisplayRole: {
        const QString display = displayValue(col, row);
        return display.isEmpty() ? QVariant{} : display;
    }
    case Qt::EditRole:
        return rawContent(col, row);

    case Qt::TextAlignmentRole: {
        // Numbers right-aligned; text left-aligned; errors centered
        const QString display = displayValue(col, row);
        if (display.startsWith('#'))         // error token
            return QVariant(int(Qt::AlignCenter | Qt::AlignVCenter));
        bool isNum = false;
        display.toDouble(&isNum);
        if (isNum)
            return QVariant(int(Qt::AlignRight | Qt::AlignVCenter));
        return QVariant(int(Qt::AlignLeft | Qt::AlignVCenter));
    }

    case Qt::ForegroundRole: {
        const QString display = displayValue(col, row);
        if (display.startsWith('#'))         // error → red
            return QColor("#E8372A");
        bool isNum = false;
        display.toDouble(&isNum);
        if (isNum && rawContent(col, row).startsWith('='))
            return QColor("#2563EB");        // formula result → blue
        return QColor("#1C1E26");            // text → charcoal
    }

    default:
        return {};
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Edit
// ─────────────────────────────────────────────────────────────────────────────
bool SpreadsheetModel::setData(const QModelIndex& index,
                                const QVariant&    value,
                                int                role) {
    if (!index.isValid() || role != Qt::EditRole) return false;

    const int     key  = cellKey(index.column(), index.row());
    const QString text = value.toString();

    if (text.isEmpty())
        m_data.erase(key);
    else
        m_data[key] = text;

    // Invalidate the whole sheet so dependent formula cells repaint
    emit dataChanged(this->index(0, 0),
                     this->index(NUM_ROWS - 1, NUM_COLS - 1),
                     {Qt::DisplayRole, Qt::ForegroundRole, Qt::TextAlignmentRole});
    return true;
}

Qt::ItemFlags SpreadsheetModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

// ─────────────────────────────────────────────────────────────────────────────
// Raw / display helpers
// ─────────────────────────────────────────────────────────────────────────────
QString SpreadsheetModel::rawContent(int col, int row) const {
    const int key = cellKey(col, row);
    auto it = m_data.find(key);
    return (it != m_data.end()) ? it->second : QString{};
}

QString SpreadsheetModel::displayValue(int col, int row) const {
    const int key = cellKey(col, row);

    // Circular-reference guard
    if (m_evaluating[key]) return "#CIRC";
    m_evaluating[key] = true;

    const QString raw = rawContent(col, row);

    // Build a lookup that delegates to this model (for cell references in formulas)
    const FormulaEngine::CellLookup lookup = [this](int c, int r) -> QString {
        return rawContent(c, r);
    };

    const QString result = m_engine.evaluate(raw, lookup);
    m_evaluating[key]    = false;
    return result;
}

void SpreadsheetModel::clearAll() {
    m_data.clear();
    emit dataChanged(index(0, 0),
                     index(NUM_ROWS - 1, NUM_COLS - 1));
}

} // namespace NativeOffice
