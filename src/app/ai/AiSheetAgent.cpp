#include "AiSheetAgent.h"

#include "CalcModule.h"
#include "SpreadsheetModel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

namespace NativeOffice {

namespace {
// Packs a cell address into one int so it can key a hash. The grid is 26 by
// 100, so this cannot collide.
inline int key(int col, int row) { return row * 1000 + col; }
} // namespace

AiSheetAgent::AiSheetAgent(QObject* parent) : QObject(parent) {}

bool AiSheetAgent::parseRef(const QString& ref, int* col, int* row) {
    const QString t = ref.trimmed().toUpper();
    int i = 0, c = 0;
    while (i < t.size() && t.at(i).isLetter()) {
        c = c * 26 + (t.at(i).unicode() - 'A' + 1);
        ++i;
    }
    if (i == 0 || i >= t.size()) return false;
    bool ok = false;
    const int r = t.mid(i).toInt(&ok);
    if (!ok || r < 1) return false;
    *col = c - 1;                       // A is column 0
    *row = r - 1;                       // row 1 is index 0
    return *col >= 0 && *col < SpreadsheetModel::NUM_COLS
        && *row >= 0 && *row < SpreadsheetModel::NUM_ROWS;
}

void AiSheetAgent::aiBegin() {
    if (!m_target) return;
    m_pending.clear();
    m_jsonCarry.clear();
    m_script.clear();
    m_before.clear();
    m_written = 0;
    m_live    = true;
    m_state   = State::Applied;
}

void AiSheetAgent::aiFeed(const QString& chunk) {
    if (!m_live || !m_target) return;
    m_pending += chunk;
    int nl;
    while ((nl = m_pending.indexOf(QLatin1Char('\n'))) >= 0) {
        QString line = m_pending.left(nl);
        m_pending.remove(0, nl + 1);
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        takeLine(line);
    }
}

void AiSheetAgent::aiEnd() {
    if (!m_live) return;
    if (!m_pending.trimmed().isEmpty()) takeLine(m_pending);
    m_pending.clear();
    m_live = false;
}

void AiSheetAgent::takeLine(const QString& raw) {
    QString t = raw.trimmed();
    if (t.isEmpty()) return;

    if (!m_jsonCarry.isEmpty()) {
        m_jsonCarry += QLatin1Char('\n') + raw;
        if (m_jsonCarry.count(QLatin1Char('{')) > m_jsonCarry.count(QLatin1Char('}')))
            return;
        t = m_jsonCarry.trimmed();
        m_jsonCarry.clear();
    } else if (t.startsWith(QLatin1Char('{'))
               && t.count(QLatin1Char('{')) > t.count(QLatin1Char('}'))) {
        m_jsonCarry = raw;
        return;
    }
    if (!t.startsWith(QLatin1Char('{'))) return;

    QJsonParseError err{};
    const QJsonDocument d = QJsonDocument::fromJson(t.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !d.isObject()) return;
    m_script += t + QLatin1Char('\n');
    applyOp(d.object());
}

void AiSheetAgent::writeCell(int col, int row, const QString& content) {
    SpreadsheetModel* m = m_target->model();
    if (!m) return;
    const int k = key(col, row);
    // Only the first write to a cell records the original, so a cell written
    // twice in one generation still rolls back to what the user had.
    if (!m_before.contains(k)) m_before.insert(k, m->rawContent(col, row));
    m->setCellContent(col, row, content, QStringLiteral("Stasis"));
    m_written += content.size();
}

void AiSheetAgent::applyOp(const QJsonObject& o) {
    const QString op = o.value(QStringLiteral("op")).toString();

    if (op == QLatin1String("cell")) {
        int c = 0, r = 0;
        if (!parseRef(o.value(QStringLiteral("ref")).toString(), &c, &r)) return;
        writeCell(c, r, o.value(QStringLiteral("value")).toVariant().toString());
        return;
    }

    // A block of values laid out from a starting cell. This is what a table
    // actually is, and sending one op per cell for a 200-cell sheet would be
    // both slower and far more likely to be cut off part way.
    if (op == QLatin1String("rows")) {
        int c0 = 0, r0 = 0;
        if (!parseRef(o.value(QStringLiteral("start")).toString(), &c0, &r0)) return;
        const QJsonArray rows = o.value(QStringLiteral("rows")).toArray();
        for (int i = 0; i < rows.size(); ++i) {
            const QJsonArray cells = rows.at(i).toArray();
            for (int j = 0; j < cells.size(); ++j) {
                const int c = c0 + j, r = r0 + i;
                if (c >= SpreadsheetModel::NUM_COLS || r >= SpreadsheetModel::NUM_ROWS)
                    continue;
                writeCell(c, r, cells.at(j).toVariant().toString());
            }
        }
    }
}

void AiSheetAgent::aiRollback() {
    if (!aiCanRollback()) return;
    SpreadsheetModel* m = m_target->model();
    if (!m) return;
    // Restores what was there, rather than clearing. Generating into a sheet
    // that already had data is the normal case, and blanking those cells would
    // be data loss wearing an undo's clothes.
    for (auto it = m_before.constBegin(); it != m_before.constEnd(); ++it) {
        const int r = it.key() / 1000, c = it.key() % 1000;
        m->setCellContent(c, r, it.value(), QStringLiteral("Rollback Stasis"));
    }
    m_state = State::RolledBack;
}

void AiSheetAgent::aiRollforward() {
    if (!aiCanRollforward() || !m_target) return;
    const QString script = m_script;
    aiBegin();
    aiFeed(script);
    aiEnd();
}

} // namespace NativeOffice
