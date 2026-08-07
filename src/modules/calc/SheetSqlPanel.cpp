// ─────────────────────────────────────────────────────────────────────────────
// SheetSqlPanel.cpp — see SheetSqlPanel.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "SheetSqlPanel.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace NativeOffice {

namespace {
// The preview exists to show shape and spot mistakes, not to be read in full;
// the whole result still goes to the sheet.
constexpr int kPreviewRows = 200;
}

SheetSqlPanel::SheetSqlPanel(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("sqlPanel"));
    // Without this a QFrame subclass ignores its stylesheet background.
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(R"(
        QFrame#sqlPanel { background:#141A24; border:1px solid #242D3C; border-radius:10px; }
        QLabel { background:transparent; color:#C7CEDC; font:12px 'Segoe UI'; }
        QLabel#title { color:#F4F6FB; font:600 15px 'Segoe UI'; }
        QLabel#tables { color:#7B8494; font:11px 'Consolas','Segoe UI'; }
        QPlainTextEdit { background:#0E131B; border:1px solid #2A3446; border-radius:6px;
            color:#E6E9F0; font:13px 'Consolas','Courier New'; padding:6px; }
        QPushButton { background:#1B2331; border:1px solid #2A3446; border-radius:7px;
            color:#E6E9F0; font:600 12px 'Segoe UI'; padding:7px 14px; }
        QPushButton:hover { background:#222C3D; border:1px solid #38455C; }
        QPushButton:disabled { color:#5A6373; border:1px solid #222A38; }
        QTableWidget { background:#0E131B; border:1px solid #242D3C; border-radius:6px;
            color:#D6DBE6; gridline-color:#20293A; font:12px 'Segoe UI'; }
        QHeaderView::section { background:#1B2331; color:#9AA4B8; border:none;
            border-right:1px solid #242D3C; padding:4px 6px; font:600 11px 'Segoe UI'; }
    )");

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(14, 12, 14, 12);
    v->setSpacing(8);

    auto* head = new QHBoxLayout();
    auto* title = new QLabel(tr("SQL on Sheets"), this);
    title->setObjectName(QStringLiteral("title"));
    head->addWidget(title);
    head->addStretch();
    auto* close = new QToolButton(this);
    close->setText(QStringLiteral("x"));
    close->setCursor(Qt::PointingHandCursor);
    close->setStyleSheet("QToolButton { background:transparent; border:none; color:#8A93A6;"
                         " font:600 14px 'Segoe UI'; } QToolButton:hover { color:#FFFFFF; }");
    connect(close, &QToolButton::clicked, this, &SheetSqlPanel::closeRequested);
    head->addWidget(close);
    v->addLayout(head);

    m_tables = new QLabel(this);
    m_tables->setObjectName(QStringLiteral("tables"));
    m_tables->setWordWrap(true);
    v->addWidget(m_tables);

    m_editor = new QPlainTextEdit(this);
    m_editor->setPlaceholderText(
        tr("SELECT * FROM Sheet1 WHERE amount > 100 ORDER BY amount DESC"));
    m_editor->setFixedHeight(84);
    m_editor->setTabChangesFocus(true);
    v->addWidget(m_editor);

    auto* row = new QHBoxLayout();
    auto* run = new QPushButton(tr("Run  (Ctrl+Enter)"), this);
    run->setCursor(Qt::PointingHandCursor);
    connect(run, &QPushButton::clicked, this, &SheetSqlPanel::runRequested);
    row->addWidget(run);

    m_send = new QPushButton(tr("Put results in a new sheet"), this);
    m_send->setCursor(Qt::PointingHandCursor);
    m_send->setEnabled(false);
    connect(m_send, &QPushButton::clicked, this, &SheetSqlPanel::sendToSheetRequested);
    row->addWidget(m_send);
    row->addStretch();
    v->addLayout(row);

    m_results = new QTableWidget(this);
    m_results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_results->setSelectionMode(QAbstractItemView::NoSelection);
    m_results->horizontalHeader()->setDefaultSectionSize(120);
    m_results->verticalHeader()->setDefaultSectionSize(20);
    m_results->verticalHeader()->setVisible(false);
    v->addWidget(m_results, 1);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    v->addWidget(m_status);

    // Ctrl+Enter from inside the editor, which is where the cursor will be.
    auto* runSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    runSc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(runSc, &QShortcut::activated, this, &SheetSqlPanel::runRequested);
    auto* runSc2 = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Enter), this);
    runSc2->setContext(Qt::WidgetWithChildrenShortcut);
    connect(runSc2, &QShortcut::activated, this, &SheetSqlPanel::runRequested);

    setStatus(tr("Each sheet is a table. The header row supplies the column names."));
}

QString SheetSqlPanel::query() const { return m_editor->toPlainText(); }

void SheetSqlPanel::setAvailableTables(const QStringList& quotedNames) {
    m_tables->setText(quotedNames.isEmpty()
        ? tr("No sheets with a header row yet.")
        : tr("Tables:  ") + quotedNames.join(QStringLiteral("   ")));
}

void SheetSqlPanel::clearResult() {
    m_results->clear();
    m_results->setRowCount(0);
    m_results->setColumnCount(0);
    m_send->setEnabled(false);
}

void SheetSqlPanel::showResult(const SheetSql::ResultTable& table, int truncatedTo) {
    const int cols = table.headers.size();
    const int rows = qMin(table.rows.size(), kPreviewRows);
    m_results->setColumnCount(cols);
    m_results->setHorizontalHeaderLabels(table.headers);
    m_results->setRowCount(rows);
    for (int r = 0; r < rows; ++r) {
        const QStringList& row = table.rows.at(r);
        for (int c = 0; c < cols; ++c)
            m_results->setItem(r, c,
                new QTableWidgetItem(c < row.size() ? row.at(c) : QString()));
    }
    m_send->setEnabled(!table.headers.isEmpty());

    QString msg = tr("%1 row(s), %2 column(s).").arg(table.rows.size()).arg(cols);
    if (table.rows.size() > rows)
        msg += QLatin1Char(' ') + tr("Showing the first %1.").arg(rows);
    if (truncatedTo >= 0)
        msg += QLatin1Char(' ') + tr("The query returned more than %1 rows and was cut off.")
                                      .arg(truncatedTo);
    setStatus(msg, false);
}

void SheetSqlPanel::setStatus(const QString& msg, bool isError) {
    m_status->setText(msg);
    m_status->setStyleSheet(isError ? QStringLiteral("color:#F0736A; font:12px 'Segoe UI';")
                                    : QStringLiteral("color:#7B8494; font:12px 'Segoe UI';"));
}

} // namespace NativeOffice
