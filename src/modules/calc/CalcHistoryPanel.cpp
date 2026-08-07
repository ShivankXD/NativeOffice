// ─────────────────────────────────────────────────────────────────────────────
// CalcHistoryPanel.cpp — see CalcHistoryPanel.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "CalcHistoryPanel.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace NativeOffice {

namespace {
constexpr int kMaxDiffRows = 500;

QString humanSize(qint64 bytes) {
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}
}

CalcHistoryPanel::CalcHistoryPanel(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("historyPanel"));
    setAttribute(Qt::WA_StyledBackground, true);   // else the stylesheet background is ignored
    setStyleSheet(R"(
        QFrame#historyPanel { background:#141A24; border:1px solid #242D3C; border-radius:10px; }
        QLabel { background:transparent; color:#C7CEDC; font:12px 'Segoe UI'; }
        QLabel#title { color:#F4F6FB; font:600 15px 'Segoe UI'; }
        QLineEdit { background:#0E131B; border:1px solid #2A3446; border-radius:6px;
            color:#E6E9F0; font:13px 'Segoe UI'; padding:7px 9px; }
        QPushButton { background:#1B2331; border:1px solid #2A3446; border-radius:7px;
            color:#E6E9F0; font:600 12px 'Segoe UI'; padding:7px 13px; }
        QPushButton:hover { background:#222C3D; border:1px solid #38455C; }
        QPushButton:disabled { color:#5A6373; border:1px solid #222A38; }
        QListWidget, QTableWidget { background:#0E131B; border:1px solid #242D3C;
            border-radius:6px; color:#D6DBE6; font:12px 'Segoe UI'; }
        QListWidget::item { padding:5px 7px; border-bottom:1px solid #1A2130; }
        QListWidget::item:selected { background:#1E2740; color:#FFFFFF; }
        QHeaderView::section { background:#1B2331; color:#9AA4B8; border:none;
            border-right:1px solid #242D3C; padding:4px 6px; font:600 11px 'Segoe UI'; }
    )");

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(14, 12, 14, 12);
    v->setSpacing(8);

    auto* head = new QHBoxLayout();
    auto* title = new QLabel(tr("Version History"), this);
    title->setObjectName(QStringLiteral("title"));
    head->addWidget(title);
    head->addStretch();
    auto* close = new QToolButton(this);
    close->setText(QStringLiteral("x"));
    close->setCursor(Qt::PointingHandCursor);
    close->setStyleSheet("QToolButton { background:transparent; border:none; color:#8A93A6;"
                         " font:600 14px 'Segoe UI'; } QToolButton:hover { color:#FFFFFF; }");
    connect(close, &QToolButton::clicked, this, &CalcHistoryPanel::closeRequested);
    head->addWidget(close);
    v->addLayout(head);

    // ── Commit ───────────────────────────────────────────────────────────────
    auto* commitRow = new QHBoxLayout();
    m_message = new QLineEdit(this);
    m_message->setPlaceholderText(tr("What changed?"));
    commitRow->addWidget(m_message, 1);
    auto* commit = new QPushButton(tr("Save version"), this);
    commit->setCursor(Qt::PointingHandCursor);
    connect(commit, &QPushButton::clicked, this, &CalcHistoryPanel::commitRequested);
    connect(m_message, &QLineEdit::returnPressed, this, &CalcHistoryPanel::commitRequested);
    commitRow->addWidget(commit);
    v->addLayout(commitRow);

    // ── Versions ─────────────────────────────────────────────────────────────
    m_versions = new QListWidget(this);
    m_versions->setSelectionMode(QAbstractItemView::ExtendedSelection);
    v->addWidget(m_versions, 2);

    auto* actions = new QHBoxLayout();
    auto* rollback = new QPushButton(tr("Restore this version"), this);
    rollback->setCursor(Qt::PointingHandCursor);
    connect(rollback, &QPushButton::clicked, this, &CalcHistoryPanel::rollbackRequested);
    actions->addWidget(rollback);

    auto* compare = new QPushButton(tr("Compare  (Premium)"), this);
    compare->setCursor(Qt::PointingHandCursor);
    connect(compare, &QPushButton::clicked, this, &CalcHistoryPanel::compareRequested);
    actions->addWidget(compare);
    actions->addStretch();
    v->addLayout(actions);

    v->addWidget(new QLabel(tr("Select one version to compare it with the document as it is "
                               "now, or two to compare them with each other."), this));

    // ── Diff ─────────────────────────────────────────────────────────────────
    m_diffCap = new QLabel(this);
    m_diffCap->setWordWrap(true);
    m_diffCap->setVisible(false);
    v->addWidget(m_diffCap);

    m_diff = new QTableWidget(this);
    m_diff->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_diff->setSelectionMode(QAbstractItemView::NoSelection);
    m_diff->setColumnCount(4);
    m_diff->setHorizontalHeaderLabels(
        { tr("Change"), tr("Where"), tr("Before"), tr("After") });
    m_diff->horizontalHeader()->setDefaultSectionSize(130);
    m_diff->verticalHeader()->setVisible(false);
    m_diff->setVisible(false);
    v->addWidget(m_diff, 3);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    v->addWidget(m_status);
}

void CalcHistoryPanel::setVersions(const QVector<SnapshotInfo>& list) {
    m_versions->clear();
    // Newest first: that is the one people reach for.
    for (int i = list.size() - 1; i >= 0; --i) {
        const SnapshotInfo& s = list.at(i);
        auto* item = new QListWidgetItem(
            QStringLiteral("v%1   %2   %3   (%4)")
                .arg(s.id)
                .arg(s.when.toString(QStringLiteral("MMM d, HH:mm")))
                .arg(s.message)
                .arg(humanSize(s.bytes)),
            m_versions);
        item->setData(Qt::UserRole, s.id);
    }
    if (list.isEmpty())
        setStatus(tr("No versions saved yet. Type a note and press Save version."));
}

QVector<int> CalcHistoryPanel::selectedIds() const {
    QVector<int> ids;
    for (QListWidgetItem* it : m_versions->selectedItems())
        ids.append(it->data(Qt::UserRole).toInt());
    std::sort(ids.begin(), ids.end());
    return ids;
}

QString CalcHistoryPanel::message() const { return m_message->text(); }
void    CalcHistoryPanel::clearMessage()  { m_message->clear(); }

void CalcHistoryPanel::clearDiff() {
    m_diff->setRowCount(0);
    m_diff->setVisible(false);
    m_diffCap->setVisible(false);
}

void CalcHistoryPanel::showDiff(const QVector<DocChange>& changes, const QString& caption) {
    const int rows = qMin(changes.size(), kMaxDiffRows);
    m_diff->setRowCount(rows);
    for (int r = 0; r < rows; ++r) {
        const DocChange& c = changes.at(r);
        const QString kind = c.kind == DocChange::Added    ? tr("Added")
                           : c.kind == DocChange::Removed  ? tr("Removed")
                                                           : tr("Changed");
        auto* kindItem = new QTableWidgetItem(kind);
        kindItem->setForeground(c.kind == DocChange::Added   ? QColor("#3FB68B")
                              : c.kind == DocChange::Removed ? QColor("#F0736A")
                                                             : QColor("#E0B341"));
        m_diff->setItem(r, 0, kindItem);
        m_diff->setItem(r, 1, new QTableWidgetItem(c.key));
        m_diff->setItem(r, 2, new QTableWidgetItem(c.before));
        m_diff->setItem(r, 3, new QTableWidgetItem(c.after));
    }
    QString cap = caption + QStringLiteral("  ");
    cap += changes.isEmpty() ? tr("No differences.")
                             : tr("%1 change(s).").arg(changes.size());
    if (changes.size() > rows) cap += QLatin1Char(' ') + tr("Showing the first %1.").arg(rows);
    m_diffCap->setText(cap);
    m_diffCap->setVisible(true);
    m_diff->setVisible(true);
}

void CalcHistoryPanel::setStatus(const QString& msg, bool isError) {
    m_status->setText(msg);
    m_status->setStyleSheet(isError ? QStringLiteral("color:#F0736A; font:12px 'Segoe UI';")
                                    : QStringLiteral("color:#7B8494; font:12px 'Segoe UI';"));
}

} // namespace NativeOffice
