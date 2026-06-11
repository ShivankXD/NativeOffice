#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// CalcModule.h  (Sprint 4)
// NativeOffice Spreadsheet Engine — full layout.
//
// Layout (top → bottom):
//   ┌──────────────────────────────────────────────────────────┐
//   │  CalcToolbar  (Charcoal, 48 px — future formatting btns) │
//   ├──────┬───────────────────────────────────────────────────┤
//   │ Name │  FormulaBar  (QLineEdit showing raw cell content)  │
//   │ Box  │                                                   │
//   ├──────┴───────────────────────────────────────────────────┤
//   │  QTableView  with CalcHeaderView (col A–Z, row 1–100)    │
//   └──────────────────────────────────────────────────────────┘
//
// Public signals:
//   cellSelected(address)  – e.g. "B3" when the user clicks B3
// ─────────────────────────────────────────────────────────────────────────────

#include <QWidget>
#include <QString>
#include <QModelIndex>

class QTableView;
class QLineEdit;
class QLabel;

namespace NativeOffice {

class SpreadsheetModel;
class CalcHeaderView;

class CalcModule : public QWidget {
    Q_OBJECT

public:
    explicit CalcModule(QWidget* parent = nullptr);

    [[nodiscard]] SpreadsheetModel* model() const noexcept { return m_model; }

signals:
    void cellSelected(const QString& address);

private slots:
    void onSelectionChanged();
    void onFormulaBarReturnPressed();
    void onFormulaBarTextEdited(const QString& text);

private:
    void buildUi();
    void applyStyles();

    // Converts current selection to a display address (e.g. "B3")
    [[nodiscard]] QString currentAddress() const;

    QTableView*      m_tableView  { nullptr };
    SpreadsheetModel* m_model     { nullptr };
    CalcHeaderView*   m_colHeader { nullptr };
    CalcHeaderView*   m_rowHeader { nullptr };

    QLabel*    m_nameBox      { nullptr };   // shows cell address (A1, B3 …)
    QLineEdit* m_formulaBar   { nullptr };   // formula input

    bool m_updatingFormulaBar { false };     // guard against feedback loops
};

} // namespace NativeOffice
