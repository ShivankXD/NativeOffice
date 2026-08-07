#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SheetSqlPanel.h — the query bar and result preview for SheetSql.
//
// Results land in the preview first and only reach a new sheet when asked. A
// query is easy to get wrong by one clause, and silently creating a sheet per
// attempt would bury the workbook in Query1..Query14.
//
// Like DataCleanserPanel this knows nothing about entitlement; CalcModule
// decides what is allowed and reports back through setStatus().
// ─────────────────────────────────────────────────────────────────────────────

#include <QFrame>

#include "SheetSql.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace NativeOffice {

class SheetSqlPanel : public QFrame {
    Q_OBJECT

public:
    explicit SheetSqlPanel(QWidget* parent = nullptr);

    [[nodiscard]] QString query() const;
    void setAvailableTables(const QStringList& quotedNames);
    void showResult(const SheetSql::ResultTable& table, int truncatedTo);
    void setStatus(const QString& msg, bool isError = false);
    void clearResult();

signals:
    void runRequested();
    void sendToSheetRequested();
    void closeRequested();

private:
    QPlainTextEdit* m_editor  { nullptr };
    QLabel*         m_tables  { nullptr };
    QTableWidget*   m_results { nullptr };
    QLabel*         m_status  { nullptr };
    QPushButton*    m_send    { nullptr };
};

} // namespace NativeOffice
