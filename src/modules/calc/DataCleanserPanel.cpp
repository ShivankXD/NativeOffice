// ─────────────────────────────────────────────────────────────────────────────
// DataCleanserPanel.cpp — see DataCleanserPanel.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "DataCleanserPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace NativeOffice {

using DataCleanser::Op;

DataCleanserPanel::DataCleanserPanel(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("cleanserPanel"));
    // Custom QFrame subclasses do not paint a stylesheet background unless this
    // is set; without it the panel is transparent and the grid shows through.
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(268);
    setStyleSheet(R"(
        QFrame#cleanserPanel { background:#141A24; border:1px solid #242D3C; border-radius:10px; }
        QLabel { background:transparent; color:#C7CEDC; font:13px 'Segoe UI'; }
        QLabel#title { color:#F4F6FB; font:600 15px 'Segoe UI'; }
        QLabel#blurb { color:#7B8494; font:11px 'Segoe UI'; }
        QPushButton { background:#1B2331; border:1px solid #2A3446; border-radius:8px;
            color:#E6E9F0; font:600 13px 'Segoe UI'; padding:9px 12px; text-align:left; }
        QPushButton:hover { background:#222C3D; border:1px solid #38455C; }
        QComboBox, QCheckBox { color:#C7CEDC; font:12px 'Segoe UI'; }
        QComboBox { background:#1B2331; border:1px solid #2A3446; border-radius:6px; padding:4px 8px; }
    )");

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(14, 12, 14, 14);
    v->setSpacing(9);

    auto* head = new QHBoxLayout();
    auto* title = new QLabel(tr("Data Cleanser"), this);
    title->setObjectName(QStringLiteral("title"));
    head->addWidget(title);
    head->addStretch();
    auto* close = new QToolButton(this);
    close->setText(QStringLiteral("x"));
    close->setCursor(Qt::PointingHandCursor);
    close->setStyleSheet("QToolButton { background:transparent; border:none; color:#8A93A6;"
                         " font:600 14px 'Segoe UI'; } QToolButton:hover { color:#FFFFFF; }");
    connect(close, &QToolButton::clicked, this, &DataCleanserPanel::closeRequested);
    head->addWidget(close);
    v->addLayout(head);

    // ── Scope ────────────────────────────────────────────────────────────────
    m_scope = new QComboBox(this);
    m_scope->addItem(tr("Apply to selection"));
    m_scope->addItem(tr("Apply to whole sheet"));
    v->addWidget(m_scope);

    m_header = new QCheckBox(tr("First row is a header"), this);
    m_header->setChecked(true);
    m_header->setToolTip(tr("Leaves the top row of the range untouched."));
    v->addWidget(m_header);

    auto* dateRow = new QHBoxLayout();
    dateRow->addWidget(new QLabel(tr("Ambiguous dates:"), this));
    m_dateOrder = new QComboBox(this);
    m_dateOrder->addItem(tr("Day first (31/12)"));
    m_dateOrder->addItem(tr("Month first (12/31)"));
    m_dateOrder->setToolTip(tr("Decides how 01/02/2024 is read. Values where one part is "
                               "greater than 12 are worked out on their own."));
    dateRow->addWidget(m_dateOrder, 1);
    v->addLayout(dateRow);

    auto* sep = new QFrame(this);
    sep->setFixedHeight(1);
    sep->setStyleSheet(QStringLiteral("background:#242D3C; border:none;"));
    v->addWidget(sep);

    // ── Actions ──────────────────────────────────────────────────────────────
    addAction(v, Op::TrimWhitespace,
              tr("Removes stray and non-breaking spaces."));
    addAction(v, Op::RemoveDuplicates,
              tr("Drops repeated rows, keeping the first."));
    addAction(v, Op::StandardizeDates,
              tr("Rewrites recognised dates as YYYY-MM-DD."));
    addAction(v, Op::ExtractEmails,
              tr("Into the first free column on the right."));
    addAction(v, Op::ExtractUrls,
              tr("Into the first free column on the right."));

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setObjectName(QStringLiteral("blurb"));
    v->addWidget(m_status);
    v->addStretch();

    refreshBadges();
}

QPushButton* DataCleanserPanel::addAction(QVBoxLayout* v, Op op, const QString& blurb) {
    const QString label = DataCleanser::opName(op);
    auto* b = new QPushButton(label, this);
    b->setCursor(Qt::PointingHandCursor);
    connect(b, &QPushButton::clicked, this, [this, op] { emit runRequested(op); });
    v->addWidget(b);

    auto* sub = new QLabel(blurb, this);
    sub->setObjectName(QStringLiteral("blurb"));
    sub->setWordWrap(true);
    sub->setContentsMargins(4, 0, 0, 4);
    v->addWidget(sub);

    m_entries.append({ b, op, label });
    return b;
}

void DataCleanserPanel::refreshBadges() {
    for (const Entry& e : m_entries) {
        const bool locked = DataCleanser::requiresPremium(e.op) && !m_premium;
        // The badge is the only difference. The button stays enabled so the
        // upsell can explain itself when it is pressed.
        e.btn->setText(locked ? e.label + QStringLiteral("   (Premium)") : e.label);
        e.btn->setStyleSheet(locked
            ? QStringLiteral("QPushButton { background:#1B2331; border:1px solid #3A3320;"
                             " border-radius:8px; color:#B9A063; font:600 13px 'Segoe UI';"
                             " padding:9px 12px; text-align:left; }"
                             "QPushButton:hover { background:#242B1C; border:1px solid #5A4C25; }")
            : QString());
    }
}

void DataCleanserPanel::setPremiumActive(bool on) {
    if (m_premium == on) return;
    m_premium = on;
    refreshBadges();
}

DataCleanser::Options DataCleanserPanel::options() const {
    DataCleanser::Options o;
    o.dayFirst         = m_dateOrder->currentIndex() == 0;
    o.firstRowIsHeader = m_header->isChecked();
    return o;
}

bool DataCleanserPanel::wholeSheet() const { return m_scope->currentIndex() == 1; }

void DataCleanserPanel::setStatus(const QString& msg, bool isError) {
    m_status->setText(msg);
    m_status->setStyleSheet(isError ? QStringLiteral("color:#F0736A; font:11px 'Segoe UI';")
                                    : QStringLiteral("color:#7B8494; font:11px 'Segoe UI';"));
}

} // namespace NativeOffice
