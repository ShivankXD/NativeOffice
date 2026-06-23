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
#include <QRect>
#include <QColor>
#include <QVector>
#include <QHash>
#include <QSet>
#include <functional>

#include "Cell.h"
#include "ChartSpec.h"

class QTableView;
class QLineEdit;
class QLabel;
class QAction;
class QToolButton;
class QComboBox;
class QFontComboBox;
class QDialog;
class QCheckBox;
class QUndoGroup;
class QHBoxLayout;
class QStackedWidget;

namespace NativeOffice {

class SpreadsheetModel;
class CalcHeaderView;
class MarchingAntsOverlay;
class SelectionOverlay;
class ChartObject;

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

    // ── Edit actions (for the window's Edit menu) ──────────────────────────
    [[nodiscard]] QAction* undoAction()  const noexcept { return m_undoAct;  }
    [[nodiscard]] QAction* redoAction()  const noexcept { return m_redoAct;  }
    [[nodiscard]] QAction* cutAction()   const noexcept { return m_cutAct;   }
    [[nodiscard]] QAction* copyAction()  const noexcept { return m_copyAct;  }
    [[nodiscard]] QAction* pasteAction() const noexcept { return m_pasteAct; }
    [[nodiscard]] QAction* deleteAction()const noexcept { return m_deleteAct;}

signals:
    void cellSelected(const QString& address);
    void documentModified();
    void filePathChanged(const QString& newPath);

private slots:
    void onSelectionChanged();
    void onFormulaBarReturnPressed();
    void onFormulaBarTextEdited(const QString& text);
    void onModelDataChanged();

    // ── Clipboard ──────────────────────────────────────────────────────────
    void copySelection();
    void cutSelection();
    void pasteClipboard();
    void deleteSelection();

    // ── Formatting toolbar ───────────────────────────────────────────────────
    void onBoldToggled(bool on);
    void onItalicToggled(bool on);
    void onUnderlineToggled(bool on);
    void onStrikeToggled(bool on);
    void onFontFamilyChanged();
    void onFontSizeChanged();
    void onIncreaseFont();
    void onDecreaseFont();
    void onIncreaseIndent();
    void onDecreaseIndent();
    void onFormatPainterClicked();
    void onTextColorClicked();
    void onFillColorClicked();
    void onAlignClicked();
    void onVAlignClicked();
    void onWrapToggled(bool on);
    void onMergeClicked();
    void onClearFormatting();
    void onNumberFormatChanged();
    void onCurrencyClicked();
    void onPercentClicked();
    void onCommaClicked();
    void onIncreaseDecimals();
    void onDecreaseDecimals();
    void onBorderMenu(QAction* act);

    // ── Row / column context menus ───────────────────────────────────────────
    void onRowHeaderMenu(const QPoint& pos);
    void onColHeaderMenu(const QPoint& pos);
    void onGridContextMenu(const QPoint& pos);

    // ── Find / Replace ───────────────────────────────────────────────────────
    void showFindDialog();
    void showReplaceDialog();

    // ── New ribbon actions (Sprint 24 — full WPS-style ribbon) ────────────────
    void onShowFormulasToggled(bool on);
    void onCalculateNow();
    void onToggleGridlines(bool on);
    void onToggleHeadings(bool on);
    void insertSymbolDialog();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUi();
    void buildActions();
    void buildRibbon();
    void applyStyles();

    // ── Freeze panes ───────────────────────────────────────────────────────────
    void setFreeze(int rows, int cols);   // freeze the top 'rows' / left 'cols'
    void updateFrozenViews();             // re-sync geometry/sizes/scroll
    QTableView* makeFrozenView();

    // ── AutoFilter ─────────────────────────────────────────────────────────────
    void showColumnFilter();   // checklist filter for the active column
    void applyFilters();       // hide rows not matching every active column filter
    void clearFilters();       // remove all filters, unhide everything

    // ── Sprint 28: real functionality for the former "coming soon" buttons ──────
    // Insert
    void insertImageObject();                 // floating picture from a file
    void insertTextBoxObject();               // floating text box
    void formatAsTable();                     // banded table styling on selection
    void insertHyperlink();                   // URL into the active cell
    void applyWordArt();                      // bold coloured large font on selection
    void insertTextValue(const QString& title, const QString& prompt); // generic input → cell
    void pivotSummary(bool alsoChart = false);// group-by-sum of the used range → new sheet
    // Page Layout
    void setPrintArea();                      // active selection becomes the print range
    void clearPrintArea();
    void setSheetBackground();                // pick a sheet fill colour
    // Formulas — named ranges & auditing
    void nameManager();
    void defineName();
    void useNameInFormula();
    void traceReferences(bool dependents);    // highlight precedents / dependents
    void removeTraceArrows();
    void errorCheck();
    void evaluateFormula();
    // Data
    void highlightDuplicates();
    void removeDuplicates();
    void textToColumns();
    void setDropdownValidation();
    void importData();                        // import a CSV into the sheet
    void hideSelectedRows();                  // Group / Hide Detail
    void unhideAllRows();                     // Ungroup / Show Detail
    // Review — comments & protection
    void addEditComment();
    void deleteComment();
    void toggleShowComments();
    void gotoComment(bool forward);
    void toggleProtectSheet();
    // View
    void zoomBy(int deltaPercent);
    void resetZoom();
    void toggleHighlightActive();
    void toggleEyeProtection();
    void openNewWindow();
    // Tools
    void exportToImage();                     // PNG of the sheet
    void exportToText();                      // tab-separated .txt
    void mergeAllSheets();                    // combine every sheet into a new one
    // Honest message for features needing external services / not yet built.
    void featureInfo(const QString& name, const QString& detail);

    // ── Export / print ─────────────────────────────────────────────────────────
    void exportToPdf();       // ask for a path, render the active sheet to PDF
    void printPreview();      // show a print-preview dialog of the active sheet
    // Paint the active sheet's used range into 'target' (fit-to-page).
    void renderSheet(class QPainter& p, const QRectF& target);

    // ── Charts ───────────────────────────────────────────────────────────────
    // Create a chart of 'type' from the current selection (undo not tracked).
    void insertChart(ChartType type);
    // Recreate live chart widgets from the active sheet's stored specs.
    void rebuildChartObjects();
    // Rebuild each live chart's series after the underlying data changed.
    void refreshChartsData();
    // Copy live chart geometries/types back into the active model's specs.
    void syncChartSpecs();
    ChartObject* createChartObject(const ChartSpec& spec);

    // Insert =FN(range) into the active cell, auto-detecting the range above/left.
    void insertFunction(const QString& fn);

    // Begin typing "=FN(" into the formula bar (function-category menus).
    void startFunctionEntry(const QString& fn);

    // Show a transient "coming soon" tooltip for not-yet-implemented features.
    void notImplemented(const QString& feature);

    // Sort the used data region by the active column (undoable).
    void sortByColumn(bool ascending);

    // Fill: copy a source block into a destination range (undoable).
    void fillRange(const QRect& source, const QRect& dest);
    void fillDown();
    void fillRight();

    // Apply a format transform to every cell in the current selection (undoable).
    void applyFormatToSelection(const std::function<void(CellFormat&)>& fn,
                                const QString& undoText);
    // Reflect the active cell's format in the toolbar controls.
    void updateToolbarFromCell();

    // Apply the active sheet's merge ranges to the view (setSpan).
    void applyMerges();
    // Apply the active sheet's custom column widths / row heights to the view.
    void applySizes();

    // Find/Replace internals.
    void buildFindDialog();
    bool findNext(bool forward);
    void replaceCurrent();
    void replaceAll();

    // ── Multi-sheet ──────────────────────────────────────────────────────────
    SpreadsheetModel* createSheet(const QString& name);
    SpreadsheetModel* sheetByName(const QString& name) const;
    void switchToSheet(int index);
    void addSheet(const QString& name);
    void deleteSheet(int index);
    void renameSheet(int index, const QString& name);
    void rebuildTabBar();
    void connectActiveView();
    void onSheetTabMenu(int index, const QPoint& pos);
    void markDirty();

    // Converts current selection to a display address (e.g. "B3")
    [[nodiscard]] QString currentAddress() const;

    // Bounding rectangle of the current selection as (left=col1, top=row1,
    // right=col2, bottom=row2). Falls back to the current cell.
    [[nodiscard]] QRect selectedRect() const;

    void setClipboardRange(const QRect& rect, bool isCut);
    void clearMarchingAnts();

    QTableView*      m_tableView  { nullptr };
    SpreadsheetModel* m_model     { nullptr };   // active sheet
    CalcHeaderView*   m_colHeader { nullptr };
    CalcHeaderView*   m_rowHeader { nullptr };

    // ── Sheets ────────────────────────────────────────────────────────────────
    QVector<SpreadsheetModel*> m_sheets;
    int          m_activeSheet  { 0 };
    QUndoGroup*  m_undoGroup     { nullptr };
    QWidget*     m_tabBar        { nullptr };
    QHBoxLayout* m_tabBarLayout  { nullptr };

    QLabel*    m_nameBox      { nullptr };   // shows cell address (A1, B3 …)
    QLineEdit* m_formulaBar   { nullptr };   // formula input

    // ── Ribbon (tabbed toolbar) ───────────────────────────────────────────────
    QWidget*        m_ribbon      { nullptr };
    QStackedWidget* m_ribbonStack { nullptr };
    QVector<QToolButton*> m_ribbonTabBtns;   // tab strip buttons (one per page)

    // ── Formatting widgets ────────────────────────────────────────────────────
    QFontComboBox* m_fontCombo     { nullptr };
    QComboBox*     m_sizeCombo     { nullptr };
    QToolButton*   m_boldBtn       { nullptr };
    QToolButton*   m_italicBtn     { nullptr };
    QToolButton*   m_underlineBtn  { nullptr };
    QToolButton*   m_strikeBtn     { nullptr };
    QToolButton*   m_textColorBtn  { nullptr };
    QToolButton*   m_fillColorBtn  { nullptr };
    QToolButton*   m_alignLeftBtn  { nullptr };
    QToolButton*   m_alignCenterBtn{ nullptr };
    QToolButton*   m_alignRightBtn { nullptr };
    QToolButton*   m_alignTopBtn   { nullptr };
    QToolButton*   m_alignMidBtn   { nullptr };
    QToolButton*   m_alignBotBtn   { nullptr };
    QToolButton*   m_wrapBtn       { nullptr };
    QToolButton*   m_mergeBtn      { nullptr };
    QToolButton*   m_borderBtn     { nullptr };
    QComboBox*     m_numFmtCombo   { nullptr };
    QToolButton*   m_currencyBtn   { nullptr };
    QToolButton*   m_percentBtn    { nullptr };
    QToolButton*   m_commaBtn      { nullptr };
    QToolButton*   m_incDecBtn     { nullptr };
    QToolButton*   m_decDecBtn     { nullptr };
    QToolButton*   m_clearFmtBtn   { nullptr };
    QToolButton*   m_showFormulasBtn { nullptr };
    bool   m_updatingToolbar { false };      // guard against feedback loops

    // ── Format painter ────────────────────────────────────────────────────────
    QToolButton* m_formatPainterBtn { nullptr };
    bool         m_painterArmed { false };
    CellFormat   m_painterFmt;
    QColor m_lastTextColor   { QColor("#1C1E26") };
    QColor m_lastFillColor   { QColor("#FFF2CC") };

    // ── Find / Replace widgets ────────────────────────────────────────────────
    QDialog*   m_findDialog   { nullptr };
    QLineEdit* m_findEdit     { nullptr };
    QLineEdit* m_replaceEdit  { nullptr };
    QCheckBox* m_matchCase    { nullptr };
    QWidget*   m_replaceRow   { nullptr };

    // ── Edit actions ───────────────────────────────────────────────────────
    QAction* m_undoAct   { nullptr };
    QAction* m_redoAct   { nullptr };
    QAction* m_cutAct    { nullptr };
    QAction* m_copyAct   { nullptr };
    QAction* m_pasteAct  { nullptr };
    QAction* m_deleteAct { nullptr };

    // ── Clipboard / marching ants ────────────────────────────────────────────
    MarchingAntsOverlay* m_ants { nullptr };
    SelectionOverlay*    m_selOverlay { nullptr };
    QRect m_clipRange;                       // copied/cut source rect (cells)
    bool  m_clipIsCut { false };

    bool m_updatingFormulaBar { false };     // guard against feedback loops
    bool m_applyingSizes      { false };     // guard programmatic col/row resizes

    // ── Drag-to-fill state ────────────────────────────────────────────────────
    bool  m_filling     { false };
    QRect m_fillSource;                      // selection rect when the drag began

    // ── Charts (live widgets for the active sheet) ─────────────────────────────
    QVector<ChartObject*> m_chartObjs;

    // ── AutoFilter: column → set of allowed display values ─────────────────────
    QHash<int, QSet<QString>> m_columnFilters;

    // ── Sprint 28 state ────────────────────────────────────────────────────────
    QVector<QWidget*> m_floatingItems;   // floating pictures / text boxes
    QRect   m_printRange;                 // explicit print area (empty = used range)
    double  m_zoom { 1.0 };
    bool    m_highlightActive { false };  // cross-hair highlight of active row/col
    bool    m_eyeProtection   { false };
    bool    m_sheetProtected  { false };

    // ── Freeze panes (overlay table views pinned at the top / left) ────────────
    QTableView* m_frozenTop    { nullptr };
    QTableView* m_frozenLeft   { nullptr };
    QTableView* m_frozenCorner { nullptr };
    int m_freezeRows { 0 };
    int m_freezeCols { 0 };

    // ── File state (Sprint 8) ─────────────────────────────────────────────
    QString m_currentPath;
    bool    m_dirty        { false };
    bool    m_ignoreChange { false };
};

} // namespace NativeOffice
