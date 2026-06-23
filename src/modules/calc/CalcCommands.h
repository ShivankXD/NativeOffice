#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// CalcCommands.h  (Sprint 10 — Undo/Redo)
// QUndoCommand subclasses for the NativeOffice Calc grid.
//
// All mutations to the grid funnel through CellsChangeCommand so that single
// edits, range deletes, and pastes are uniformly undo-able.  A command records
// the before/after Cell for every affected coordinate and replays them on
// redo()/undo() via SpreadsheetModel's raw-apply hooks.
// ─────────────────────────────────────────────────────────────────────────────

#include "Cell.h"

#include <QUndoCommand>
#include <QString>
#include <vector>

namespace NativeOffice {

class SpreadsheetModel;

struct CellChange {
    int  col;
    int  row;
    Cell before;
    Cell after;
};

class CellsChangeCommand : public QUndoCommand {
public:
    CellsChangeCommand(SpreadsheetModel*       model,
                       std::vector<CellChange> changes,
                       const QString&          text,
                       QUndoCommand*           parent = nullptr);

    void undo() override;
    void redo() override;

private:
    SpreadsheetModel*       m_model;
    std::vector<CellChange> m_changes;
};

} // namespace NativeOffice
