#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AiSheetAgent.h — fills a spreadsheet from streamed operations.
//
// Same contract as the document and slide agents: one JSON object per line,
// executed the moment its line completes, so the grid fills while the model is
// still writing.
//
// Cells are addressed the way a person would write them ("B7"), not as a pair
// of indices, because that is what the model produces reliably and what the
// user would say out loud. Formulas are passed through untouched: Calc already
// has an evaluator, and a formula is the one thing a spreadsheet assistant
// should be writing rather than a computed number it might get wrong.
//
// Rollback restores the previous contents of every cell touched, rather than
// clearing them. Generating into an existing sheet is the normal case, and
// blanking a cell that had something in it before would be data loss dressed
// up as an undo.
// ─────────────────────────────────────────────────────────────────────────────

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPoint>
#include <QString>

#include "ai/AiStreamTarget.h"

namespace NativeOffice {

class CalcModule;

class AiSheetAgent : public QObject, public AiStreamTarget {
    Q_OBJECT
public:
    explicit AiSheetAgent(QObject* parent = nullptr);

    void setTarget(CalcModule* target) { m_target = target; }
    CalcModule* target() const { return m_target; }

    // ── AiStreamTarget ──────────────────────────────────────────────────────
    void aiBegin() override;
    void aiFeed(const QString& chunk) override;
    void aiEnd() override;
    int  aiCharactersWritten() const override { return m_written; }
    bool aiCanRollback() const override { return m_state == State::Applied && !m_before.isEmpty(); }
    bool aiCanRollforward() const override { return m_state == State::RolledBack; }
    void aiRollback() override;
    void aiRollforward() override;

private:
    void takeLine(const QString& line);
    void applyOp(const QJsonObject& o);
    void writeCell(int col, int row, const QString& content);
    // "B7" to (1, 6). Returns false for anything that is not a plain reference.
    static bool parseRef(const QString& ref, int* col, int* row);

    enum class State { Idle, Applied, RolledBack };

    CalcModule* m_target { nullptr };
    QString m_pending;
    QString m_jsonCarry;
    QString m_script;                      // every op, so rollforward can replay
    // What each touched cell held before, keyed by packed column and row. The
    // first write to a cell wins, so a cell written twice still restores to
    // what the user had rather than to an intermediate value.
    QHash<int, QString> m_before;
    bool  m_live    { false };
    int   m_written { 0 };
    State m_state   { State::Idle };
};

} // namespace NativeOffice
