// ─────────────────────────────────────────────────────────────────────────────
// CalcModule.cpp  (Sprint 4)
// Full spreadsheet UI: formula bar + themed QTableView + SpreadsheetModel.
// ─────────────────────────────────────────────────────────────────────────────
#include "CalcModule.h"
#include "SpreadsheetModel.h"
#include "CalcHeaderView.h"
#include "FormulaEngine.h"
#include "core/theme/ThemeManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableView>
#include <QLineEdit>
#include <QLabel>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QAbstractItemView>
#include <QKeyEvent>
#include <QFont>
#include <QSizePolicy>
#include <QFrame>

namespace NativeOffice {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────
CalcModule::CalcModule(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    applyStyles();
    setObjectName("calcModule");
}

// ─────────────────────────────────────────────────────────────────────────────
// UI Construction
// ─────────────────────────────────────────────────────────────────────────────
void CalcModule::buildUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ── Formula bar row ───────────────────────────────────────────────────
    auto* fbarRow = new QWidget(this);
    fbarRow->setObjectName("formulaBarRow");
    fbarRow->setFixedHeight(36);

    auto* fbarLayout = new QHBoxLayout(fbarRow);
    fbarLayout->setContentsMargins(0, 0, 0, 0);
    fbarLayout->setSpacing(0);

    // Name box (shows current cell address, e.g. "A1")
    m_nameBox = new QLabel("A1", fbarRow);
    m_nameBox->setObjectName("nameBox");
    m_nameBox->setFixedWidth(64);
    m_nameBox->setAlignment(Qt::AlignCenter);

    // Vertical separator
    auto* sep = new QFrame(fbarRow);
    sep->setFrameShape(QFrame::VLine);
    sep->setObjectName("fbarSep");
    sep->setFixedWidth(1);

    // "fx" function icon label
    auto* fxLabel = new QLabel(" fx ", fbarRow);
    fxLabel->setObjectName("fxLabel");
    fxLabel->setFixedWidth(36);
    fxLabel->setAlignment(Qt::AlignCenter);

    // Vertical separator
    auto* sep2 = new QFrame(fbarRow);
    sep2->setFrameShape(QFrame::VLine);
    sep2->setObjectName("fbarSep");
    sep2->setFixedWidth(1);

    // The formula / content edit field
    m_formulaBar = new QLineEdit(fbarRow);
    m_formulaBar->setObjectName("formulaBar");
    m_formulaBar->setPlaceholderText("Type a value or formula starting with =");
    m_formulaBar->setClearButtonEnabled(true);

    fbarLayout->addWidget(m_nameBox);
    fbarLayout->addWidget(sep);
    fbarLayout->addWidget(fxLabel);
    fbarLayout->addWidget(sep2);
    fbarLayout->addWidget(m_formulaBar, 1);

    // ── Grid ──────────────────────────────────────────────────────────────
    m_model = new SpreadsheetModel(this);

    m_tableView = new QTableView(this);
    m_tableView->setObjectName("calcGrid");
    m_tableView->setModel(m_model);

    // Custom themed header views
    m_colHeader = new CalcHeaderView(Qt::Horizontal, m_tableView);
    m_rowHeader = new CalcHeaderView(Qt::Vertical,   m_tableView);

    m_tableView->setHorizontalHeader(m_colHeader);
    m_tableView->setVerticalHeader(m_rowHeader);

    // Column widths: default 80px, row heights: default 22px
    m_colHeader->setDefaultSectionSize(80);
    m_rowHeader->setDefaultSectionSize(22);
    m_rowHeader->setFixedWidth(48);

    // Grid interaction
    m_tableView->setSelectionMode(QAbstractItemView::ContiguousSelection);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_tableView->setTabKeyNavigation(true);
    m_tableView->setEditTriggers(QAbstractItemView::DoubleClicked
                                 | QAbstractItemView::AnyKeyPressed
                                 | QAbstractItemView::EditKeyPressed);
    m_tableView->setShowGrid(true);

    // ── Assemble ──────────────────────────────────────────────────────────
    rootLayout->addWidget(fbarRow);
    rootLayout->addWidget(m_tableView, 1);

    // ── Connect signals ───────────────────────────────────────────────────
    connect(m_tableView->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this, &CalcModule::onSelectionChanged);

    connect(m_formulaBar, &QLineEdit::returnPressed,
            this, &CalcModule::onFormulaBarReturnPressed);

    connect(m_formulaBar, &QLineEdit::textEdited,
            this, &CalcModule::onFormulaBarTextEdited);

    // Select A1 by default
    const QModelIndex first = m_model->index(0, 0);
    m_tableView->setCurrentIndex(first);
    m_tableView->scrollTo(first);
}

// ─────────────────────────────────────────────────────────────────────────────
// Slots
// ─────────────────────────────────────────────────────────────────────────────
void CalcModule::onSelectionChanged() {
    if (m_updatingFormulaBar) return;

    const QModelIndex cur = m_tableView->currentIndex();
    if (!cur.isValid()) return;

    // Update name box
    const QString addr = FormulaEngine::cellAddress(cur.column(), cur.row());
    m_nameBox->setText(addr);
    emit cellSelected(addr);

    // Update formula bar with raw content
    m_updatingFormulaBar = true;
    m_formulaBar->setText(m_model->rawContent(cur.column(), cur.row()));
    m_formulaBar->setCursorPosition(m_formulaBar->text().length());
    m_updatingFormulaBar = false;

    // Highlight the selected column and row in the headers
    m_colHeader->setHighlightedSections({cur.column()});
    m_rowHeader->setHighlightedSections({cur.row()});
}

void CalcModule::onFormulaBarReturnPressed() {
    const QModelIndex cur = m_tableView->currentIndex();
    if (!cur.isValid()) return;

    m_model->setData(cur, m_formulaBar->text(), Qt::EditRole);

    // Move to the cell below (Excel-like behaviour)
    const int nextRow = std::min(cur.row() + 1, SpreadsheetModel::NUM_ROWS - 1);
    const QModelIndex next = m_model->index(nextRow, cur.column());
    m_tableView->setCurrentIndex(next);
    m_tableView->setFocus();
}

void CalcModule::onFormulaBarTextEdited(const QString& /*text*/) {
    // Live-preview while the user types: update the cell immediately
    // but don't move focus — only commit on Enter.
    // (Currently we commit on Enter in returnPressed.)
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
QString CalcModule::currentAddress() const {
    const QModelIndex cur = m_tableView->currentIndex();
    if (!cur.isValid()) return "A1";
    return FormulaEngine::cellAddress(cur.column(), cur.row());
}

// ─────────────────────────────────────────────────────────────────────────────
// Styling
// ─────────────────────────────────────────────────────────────────────────────
void CalcModule::applyStyles() {
    const auto& t = ThemeManager::instance().theme();

    setStyleSheet(QString(R"(
/* ── Module root ─────────────────────────────────────────────────── */
QWidget#calcModule {
    background-color: #FFFFFF;
}

/* ── Formula bar row ─────────────────────────────────────────────── */
QWidget#formulaBarRow {
    background-color: %1;
    border-bottom: 1px solid %2;
}

/* ── Name box ───────────────────────────────────────────────────── */
QLabel#nameBox {
    color: #FFFFFF;
    font-size: 12px;
    font-weight: 700;
    font-family: "Segoe UI", "Inter", monospace;
    background: transparent;
    padding: 0 8px;
}

/* ── fx label ───────────────────────────────────────────────────── */
QLabel#fxLabel {
    color: %3;
    font-size: 12px;
    font-style: italic;
    font-family: "Segoe UI", serif;
    background: transparent;
}

/* ── Separators ─────────────────────────────────────────────────── */
QFrame#fbarSep {
    background-color: rgba(255,255,255,0.12);
    border: none;
}

/* ── Formula bar input ──────────────────────────────────────────── */
QLineEdit#formulaBar {
    background-color: rgba(255,255,255,0.08);
    color: #FFFFFF;
    border: none;
    border-left: none;
    padding: 4px 10px;
    font-size: 13px;
    font-family: "Segoe UI", "Consolas", monospace;
    selection-background-color: %3;
    selection-color: #FFFFFF;
}
QLineEdit#formulaBar:focus {
    background-color: rgba(255,255,255,0.14);
}
QLineEdit#formulaBar::placeholder {
    color: rgba(255,255,255,0.35);
}

/* ── Grid (QTableView) ───────────────────────────────────────────── */
QTableView#calcGrid {
    background-color: #FFFFFF;
    alternate-background-color: #FAFAFA;
    gridline-color: #E5E7EB;
    border: none;
    selection-background-color: #DBEAFE;
    selection-color: #1C1E26;
    font-size: 12px;
    font-family: "Segoe UI", "Inter", sans-serif;
}

QTableView#calcGrid::item {
    padding: 0 6px;
    border-right: 1px solid #E5E7EB;
    border-bottom: 1px solid #E5E7EB;
}

QTableView#calcGrid::item:selected {
    background-color: %4;
    color: #1C1E26;
    border: 1px solid %3;
}

QTableView#calcGrid::item:focus {
    background-color: %5;
    border: 2px solid %3;
    color: #1C1E26;
}

/* Corner button (top-left intersection of headers) */
QTableView#calcGrid QAbstractButton {
    background-color: %1;
    border: none;
    border-right:  1px solid %2;
    border-bottom: 1px solid %2;
}
QTableView#calcGrid QAbstractButton:hover {
    background-color: %6;
}
)")
    .arg(ThemeManager::cssColor(t.primary))      // %1 formula bar / header bg
    .arg(ThemeManager::cssColor(t.accent))        // %2 dark border
    .arg(ThemeManager::cssColor(t.secondary))     // %3 scarlet accent
    .arg("#DBEAFE")                               // %4 selection bg (light blue)
    .arg("#EFF6FF")                               // %5 focused cell bg
    .arg(ThemeManager::cssColor(t.sidebarHover))  // %6 corner hover
    );
}

} // namespace NativeOffice
