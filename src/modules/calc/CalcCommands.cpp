// ─────────────────────────────────────────────────────────────────────────────
// CalcCommands.cpp  (Sprint 10 — Undo/Redo)
// ─────────────────────────────────────────────────────────────────────────────
#include "CalcCommands.h"
#include "SpreadsheetModel.h"

namespace NativeOffice {

CellsChangeCommand::CellsChangeCommand(SpreadsheetModel*       model,
                                       std::vector<CellChange> changes,
                                       const QString&          text,
                                       QUndoCommand*           parent)
    : QUndoCommand(text, parent)
    , m_model(model)
    , m_changes(std::move(changes))
{}

void CellsChangeCommand::redo() {
    for (const auto& ch : m_changes)
        m_model->applyCellRaw(ch.col, ch.row, ch.after);
    m_model->notifyAllChanged();
}

void CellsChangeCommand::undo() {
    for (const auto& ch : m_changes)
        m_model->applyCellRaw(ch.col, ch.row, ch.before);
    m_model->notifyAllChanged();
}

} // namespace NativeOffice
