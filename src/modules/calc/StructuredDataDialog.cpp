// ─────────────────────────────────────────────────────────────────────────────
// StructuredDataDialog.cpp — see StructuredDataDialog.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "StructuredDataDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace NativeOffice {

using StructuredData::Format;
using StructuredData::ParseResult;
using StructuredData::Table;

namespace {
// Rows drawn in the preview. The preview is for confirming shape, not for
// reading the data, and building thousands of QTableWidgetItems on every
// keystroke would make typing stutter.
constexpr int kPreviewRows = 50;
}

StructuredDataDialog::StructuredDataDialog(int maxRows, int maxCols, QWidget* parent)
    : QDialog(parent)
    , m_maxRows(maxRows)
    , m_maxCols(maxCols)
{
    setWindowTitle(tr("Import JSON / YAML"));
    setMinimumSize(760, 620);
    resize(880, 700);

    // Explicit colours rather than inheriting the app sheet: the editor was
    // rendering low-contrast grey on dark, which is what made pasted text look
    // like it had not arrived at all.
    setStyleSheet(R"(
        QLabel { color:#C7CEDC; font:12px 'Segoe UI'; }
        QLabel#section { color:#8A93A6; font:600 12px 'Segoe UI'; }
        QPlainTextEdit { background:#0E131B; border:1px solid #2A3446; border-radius:6px;
            color:#E6E9F0; font:13px 'Consolas','Courier New'; padding:8px;
            selection-background-color:#2E4A7D; }
        QTableWidget { background:#0E131B; border:1px solid #242D3C; border-radius:6px;
            color:#D6DBE6; gridline-color:#20293A; font:12px 'Segoe UI'; }
        QHeaderView::section { background:#1B2331; color:#9AA4B8; border:none;
            border-right:1px solid #242D3C; padding:5px 8px; font:600 11px 'Segoe UI'; }
        QComboBox { background:#161C27; border:1px solid #2A3446; border-radius:6px;
            color:#E6E9F0; padding:5px 9px; }
        QPushButton { background:#1B2331; border:1px solid #2A3446; border-radius:7px;
            color:#E6E9F0; font:600 12px 'Segoe UI'; padding:7px 14px; }
        QPushButton:hover { background:#222C3D; border:1px solid #38455C; }
        QCheckBox { color:#C7CEDC; font:12px 'Segoe UI'; }
    )");

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(16, 16, 16, 12);
    v->setSpacing(10);

    // ── Top row: format picker, paste helper, destination ────────────────────
    auto* top = new QHBoxLayout();
    top->setSpacing(8);
    top->addWidget(new QLabel(tr("Format:"), this));
    m_format = new QComboBox(this);
    m_format->addItem(tr("Auto-detect"), -1);
    m_format->addItem(tr("JSON"), int(Format::Json));
    m_format->addItem(tr("YAML"), int(Format::Yaml));
    top->addWidget(m_format);

    auto* pasteBtn = new QPushButton(tr("Paste from clipboard"), this);
    pasteBtn->setCursor(Qt::PointingHandCursor);
    top->addWidget(pasteBtn);
    top->addStretch();

    m_atSel = new QCheckBox(tr("Insert at selection"), this);
    m_atSel->setToolTip(tr("Off: the table lands at cell A1."));
    top->addWidget(m_atSel);
    v->addLayout(top);

    // ── Input ────────────────────────────────────────────────────────────────
    m_input = new QPlainTextEdit(this);
    m_input->setPlaceholderText(
        tr("Paste JSON or YAML here.\n\n"
           "A list of records becomes one row each. Nested keys become "
           "dot-notation columns (address.city), and nested lists become "
           "indexed columns (tags.0, tags.1)."));
    m_input->setTabChangesFocus(true);
    m_input->setMinimumHeight(190);
    v->addWidget(m_input, 3);

    // ── Preview ──────────────────────────────────────────────────────────────
    auto* pv = new QLabel(tr("Preview"), this);
    pv->setObjectName(QStringLiteral("section"));
    v->addWidget(pv);
    m_preview = new QTableWidget(this);
    m_preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_preview->setSelectionMode(QAbstractItemView::NoSelection);
    m_preview->horizontalHeader()->setDefaultSectionSize(120);
    m_preview->verticalHeader()->setDefaultSectionSize(22);
    // Row numbers here are the preview's own count, not the sheet's, so they
    // are noise; hiding them also removes the stray narrow column on the left.
    m_preview->verticalHeader()->setVisible(false);
    m_preview->setMinimumHeight(150);
    v->addWidget(m_preview, 4);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    v->addWidget(m_status);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okBtn = buttons->button(QDialogButtonBox::Ok);
    m_okBtn->setText(tr("Insert"));
    m_okBtn->setEnabled(false);
    v->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(pasteBtn, &QPushButton::clicked, this, [this] {
        m_input->setPlainText(QGuiApplication::clipboard()->text());
        // Land at the top. setPlainText leaves the cursor at the end, which
        // scrolls a long document past its own opening lines and makes it look
        // like the wrong thing was pasted.
        m_input->moveCursor(QTextCursor::Start);
        m_input->ensureCursorVisible();
    });
    connect(m_format, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { reparse(); });

    // Re-parse as you type, coalesced so a long paste is not re-parsed per
    // keystroke while the text is still arriving.
    auto* debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(180);
    connect(debounce, &QTimer::timeout, this, &StructuredDataDialog::reparse);
    connect(m_input, &QPlainTextEdit::textChanged, this, [debounce] { debounce->start(); });

    setStatus(tr("Paste some JSON or YAML to get started."), false);
}

bool StructuredDataDialog::insertAtSelection() const {
    return m_atSel && m_atSel->isChecked();
}

void StructuredDataDialog::setStatus(const QString& msg, bool isError) {
    m_status->setText(msg);
    m_status->setStyleSheet(isError ? QStringLiteral("color:#F0736A;")
                                    : QStringLiteral("color:#8A93A6;"));
}

void StructuredDataDialog::reparse() {
    const QString text = m_input->toPlainText();
    m_parsed = Table();
    m_clamped = Table();

    if (text.trimmed().isEmpty()) {
        m_okBtn->setEnabled(false);
        m_preview->clear();
        m_preview->setRowCount(0);
        m_preview->setColumnCount(0);
        setStatus(tr("Paste some JSON or YAML to get started."), false);
        return;
    }

    const int sel = m_format->currentData().toInt();
    const Format fmt = (sel < 0) ? StructuredData::detectFormat(text) : Format(sel);
    const ParseResult res = StructuredData::parse(text, fmt);

    if (!res.ok()) {
        m_okBtn->setEnabled(false);
        m_preview->setRowCount(0);
        m_preview->setColumnCount(0);
        const QString where = res.line > 0 ? tr(" (line %1)").arg(res.line) : QString();
        const QString what  = (sel < 0)
            ? tr("Read as %1").arg(fmt == Format::Json ? QStringLiteral("JSON") : QStringLiteral("YAML"))
            : tr("Parse failed");
        setStatus(what + QStringLiteral(": ") + res.error + where, true);
        return;
    }

    m_parsed = res.table;

    // Clamp to the destination grid. Done here, before the preview, so what is
    // shown is exactly what will be written.
    m_clamped.headers = m_parsed.headers.mid(0, m_maxCols);
    const int rowLimit = qMax(0, m_maxRows - 1);          // row 0 holds the header
    for (int r = 0; r < m_parsed.rows.size() && r < rowLimit; ++r)
        m_clamped.rows.append(m_parsed.rows.at(r).mid(0, m_maxCols));

    updatePreview();

    const int droppedCols = m_parsed.headers.size() - m_clamped.headers.size();
    const int droppedRows = m_parsed.rows.size()    - m_clamped.rows.size();
    // Lead with how it was READ. When the grid is a surprise, that sentence is
    // the difference between "this is broken" and "ah, it saw a single record".
    QString msg = res.shape.isEmpty()
        ? tr("%1 rows, %2 columns.").arg(m_clamped.rows.size()).arg(m_clamped.headers.size())
        : tr("Read as %1: %2 rows, %3 columns.")
              .arg(res.shape).arg(m_clamped.rows.size()).arg(m_clamped.headers.size());
    if (droppedCols > 0 || droppedRows > 0) {
        QStringList lost;
        if (droppedRows > 0) lost << tr("%1 rows").arg(droppedRows);
        if (droppedCols > 0) lost << tr("%1 columns").arg(droppedCols);
        msg += QLatin1Char(' ') + tr("The sheet is %1 x %2, so %3 will not be imported.")
                                      .arg(m_maxRows).arg(m_maxCols).arg(lost.join(tr(" and ")));
        setStatus(msg, true);
    } else {
        setStatus(msg, false);
    }

    m_okBtn->setEnabled(!m_clamped.headers.isEmpty());
}

void StructuredDataDialog::updatePreview() {
    const int cols = m_clamped.headers.size();
    const int rows = qMin(m_clamped.rows.size(), kPreviewRows);

    m_preview->setColumnCount(cols);
    m_preview->setHorizontalHeaderLabels(m_clamped.headers);
    m_preview->setRowCount(rows);

    for (int r = 0; r < rows; ++r) {
        const QStringList& row = m_clamped.rows.at(r);
        for (int c = 0; c < cols; ++c)
            m_preview->setItem(r, c, new QTableWidgetItem(c < row.size() ? row.at(c) : QString()));
    }

    // Size columns to their contents, then cap. A fixed width truncated the
    // header text ("preadsheet.columns.l"), which is exactly when the user most
    // needs to read it, and a long cell should not be able to push the rest off.
    m_preview->resizeColumnsToContents();
    for (int c = 0; c < cols; ++c)
        m_preview->setColumnWidth(c, qBound(70, m_preview->columnWidth(c) + 12, 260));
}

} // namespace NativeOffice
