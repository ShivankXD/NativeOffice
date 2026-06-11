#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// CalcModule.h  (Sprint 8)
// NativeOffice Spreadsheet Engine — full layout + file persistence.
//
// Layout (top → bottom):
//   ┌──────────────────────────────────────────────────────────────┐
//   │  CalcToolbar  (Charcoal, 48 px — future formatting btns) │
//   ├──────┬───────────────────────────────────────────────────────┤
//   │ Name │  FormulaBar  (QLineEdit showing raw cell content)  │
//   │ Box  │                                                   │
//   ├──────┴───────────────────────────────────────────────────────┤
//   │  QTableView  with CalcHeaderView (col A–Z, row 1–100)    │
//   └──────────────────────────────────────────────────────────────┘
//
// Sprint 8 additions:
//   • saveToPath(path)  — writes JSON cell data to .noff file
//   • loadFromPath(path)— reads JSON (or CSV fallback) into the grid
//   • isDirty / markClean / titleString — dirty-state tracking
//   • documentModified / filePathChanged signals
//
// Public signals:
//   cellSelected(address)  – e.g. "B3" when the user clicks B3
//   documentModified()     – emitted on first edit after save/load
//   filePathChanged(path)  – emitted after successful save/load
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

    // ── File state ────────────────────────────────────────────────────────
    [[nodiscard]] QString currentFilePath() const noexcept { return m_currentPath; }
    [[nodiscard]] bool    isDirty()         const noexcept { return m_dirty; }
    [[nodiscard]] QString titleString()     const;

    // ── File I/O ──────────────────────────────────────────────────────────
    bool saveToPath(const QString& path);
    bool loadFromPath(const QString& path);
    void markClean();

signals:
    void cellSelected(const QString& address);
    void documentModified();
    void filePathChanged(const QString& newPath);

private slots:
    void onSelectionChanged();
    void onFormulaBarReturnPressed();
    void onFormulaBarTextEdited(const QString& text);
    void onModelDataChanged();

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

    // ── File state (Sprint 8) ─────────────────────────────────────────────
    QString m_currentPath;
    bool    m_dirty        { false };
    bool    m_ignoreChange { false };
};

} // namespace NativeOffice
