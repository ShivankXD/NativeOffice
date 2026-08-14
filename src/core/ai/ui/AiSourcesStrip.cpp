#include "AiSourcesStrip.h"

#include <QDesktopServices>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

namespace NativeOffice {

namespace {

// A source's shortest honest label. The domain is the useful part when a page
// title is missing or is a headline long enough to fill the row on its own.
QString shortLabel(const AiSource& s) {
    const QString t = s.title.trimmed();
    if (t.isEmpty()) return s.domain;
    if (t.size() <= 34) return t;
    return t.left(32).trimmed() + QStringLiteral("...");
}

QString domainOf(const AiSource& s) {
    if (!s.domain.isEmpty()) return s.domain;
    const QString host = QUrl(s.url).host();
    return host.startsWith(QLatin1String("www.")) ? host.mid(4) : host;
}

} // namespace

AiSourcesStrip::AiSourcesStrip(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("background:transparent;"));

    m_row = new QHBoxLayout(this);
    m_row->setContentsMargins(2, 0, 2, 6);
    m_row->setSpacing(8);

    // ── sources half ────────────────────────────────────────────────────────
    m_sourceBox = new QWidget(this);
    m_sourceBox->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* sh = new QHBoxLayout(m_sourceBox);
    sh->setContentsMargins(0, 0, 0, 0);
    sh->setSpacing(6);

    auto* dot = new QLabel(QStringLiteral("●"), m_sourceBox);
    dot->setStyleSheet(QStringLiteral(
        "color:#5FB37E; font:8px 'Segoe UI'; background:transparent;"));

    m_first = new QLabel(m_sourceBox);
    m_first->setStyleSheet(QStringLiteral(
        "color:#8A94A8; font:11px 'Segoe UI'; background:transparent;"));

    m_more = new QPushButton(m_sourceBox);
    m_more->setCursor(Qt::PointingHandCursor);
    m_more->setFocusPolicy(Qt::NoFocus);
    m_more->setFixedHeight(20);
    m_more->setStyleSheet(QStringLiteral(
        "QPushButton { color:#9B8CFF; font:11px 'Segoe UI'; background:transparent;"
        "  border:none; padding:0 2px; text-align:left; }"
        "QPushButton:hover { color:#C4B5FD; text-decoration:underline; }"));
    connect(m_more, &QPushButton::clicked, this, &AiSourcesStrip::showAllSources);

    sh->addWidget(dot, 0);
    sh->addWidget(m_first, 0);
    sh->addWidget(m_more, 0);
    sh->addStretch(1);

    // ── rollback half ───────────────────────────────────────────────────────
    m_rollback = new QPushButton(this);
    m_rollback->setCursor(Qt::PointingHandCursor);
    m_rollback->setFocusPolicy(Qt::NoFocus);
    m_rollback->setFixedHeight(24);
    m_rollback->setStyleSheet(QStringLiteral(
        "QPushButton { color:#C3CAD8; font:11px 'Segoe UI';"
        "  background:rgba(255,255,255,0.06);"
        "  border:1px solid rgba(255,255,255,0.14);"
        "  border-radius:7px; padding:0 10px; }"
        "QPushButton:hover { color:#FFFFFF; border-color:rgba(124,92,255,0.60);"
        "  background:rgba(124,92,255,0.16); }"));
    connect(m_rollback, &QPushButton::clicked, this, &AiSourcesStrip::rollbackClicked);

    m_row->addWidget(m_sourceBox, 1);
    m_row->addWidget(m_rollback, 0);

    m_sourceBox->hide();
    m_rollback->hide();
    hide();                          // the row costs no height until it has something
}

void AiSourcesStrip::setSources(const QVector<AiSource>& sources) {
    m_sources = sources;
    refresh();
}

void AiSourcesStrip::clearSources() {
    m_sources.clear();
    refresh();
}

void AiSourcesStrip::setRollbackVisible(bool on, bool rolledBack) {
    m_rollback->setVisible(on);
    if (on) {
        m_rollback->setText(rolledBack ? QStringLiteral("↷  Rollforward")
                                       : QStringLiteral("↶  Rollback"));
        m_rollback->setToolTip(rolledBack
            ? QStringLiteral("Put back what Stasis wrote")
            : QStringLiteral("Undo what Stasis wrote"));
    }
    refresh();
}

void AiSourcesStrip::refresh() {
    const bool haveSources = !m_sources.isEmpty();
    m_sourceBox->setVisible(haveSources);

    if (haveSources) {
        m_first->setText(shortLabel(m_sources.first()));
        m_first->setToolTip(m_sources.first().url);
        const int rest = m_sources.size() - 1;
        m_more->setVisible(rest > 0);
        if (rest > 0) {
            m_more->setText(QStringLiteral("+%1 %2")
                                .arg(rest)
                                .arg(rest == 1 ? QStringLiteral("other")
                                               : QStringLiteral("others")));
            m_more->setToolTip(QStringLiteral("Show all %1 sources").arg(m_sources.size()));
        }
    }
    // The whole row disappears when neither half has anything to say, rather
    // than sitting there as an empty gap above the composer.
    setVisible(haveSources || m_rollback->isVisible());
}

void AiSourcesStrip::showAllSources() {
    if (m_sources.isEmpty()) return;

    QDialog dlg(this);
    dlg.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dlg.setAttribute(Qt::WA_TranslucentBackground);
    dlg.setModal(true);
    dlg.setFixedWidth(qMax(360, width() - 20));

    auto* outer = new QVBoxLayout(&dlg);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* card = new QWidget(&dlg);
    card->setObjectName(QStringLiteral("srcCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setStyleSheet(QStringLiteral(
        "#srcCard { background:#161B25; border:1px solid rgba(255,255,255,0.12);"
        "  border-radius:12px; }"));
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(16, 14, 16, 14);
    v->setSpacing(10);

    auto* head = new QLabel(QStringLiteral("%1 sources").arg(m_sources.size()), card);
    head->setStyleSheet(QStringLiteral(
        "color:#E7EAF1; font:600 13px 'Segoe UI'; background:transparent;"));
    v->addWidget(head);

    auto* scroll = new QScrollArea(card);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMaximumHeight(340);
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea { background:transparent; border:none; }"
        "QScrollBar:vertical { width:7px; background:transparent; }"
        "QScrollBar::handle:vertical { background:#333C4D; border-radius:3px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"));
    auto* list = new QWidget(scroll);
    list->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* lv = new QVBoxLayout(list);
    lv->setContentsMargins(0, 0, 0, 0);
    lv->setSpacing(4);

    for (int i = 0; i < m_sources.size(); ++i) {
        const AiSource& s = m_sources.at(i);
        auto* row = new QPushButton(list);
        row->setCursor(Qt::PointingHandCursor);
        row->setFocusPolicy(Qt::NoFocus);
        row->setToolTip(s.url);
        // Numbered, so a citation in the text can be matched to the list.
        row->setText(QStringLiteral("%1.  %2\n     %3")
                         .arg(i + 1)
                         .arg(s.title.isEmpty() ? domainOf(s) : s.title,
                              domainOf(s)));
        row->setStyleSheet(QStringLiteral(
            "QPushButton { color:#C7CEDC; font:11.5px 'Segoe UI'; text-align:left;"
            "  background:rgba(255,255,255,0.04); border:1px solid transparent;"
            "  border-radius:8px; padding:7px 10px; }"
            "QPushButton:hover { background:rgba(124,92,255,0.14);"
            "  border-color:rgba(124,92,255,0.40); color:#FFFFFF; }"));
        const QString url = s.url;
        connect(row, &QPushButton::clicked, &dlg, [url] {
            if (!url.isEmpty()) QDesktopServices::openUrl(QUrl(url));
        });
        lv->addWidget(row);
    }
    lv->addStretch(1);
    scroll->setWidget(list);
    v->addWidget(scroll);

    auto* close = new QPushButton(QStringLiteral("Close"), card);
    close->setCursor(Qt::PointingHandCursor);
    close->setFixedHeight(28);
    close->setStyleSheet(QStringLiteral(
        "QPushButton { color:#AAB3C4; font:12px 'Segoe UI'; background:transparent;"
        "  border:1px solid rgba(255,255,255,0.14); border-radius:7px; padding:0 14px; }"
        "QPushButton:hover { color:#FFFFFF; border-color:rgba(255,255,255,0.30); }"));
    connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
    auto* footer = new QHBoxLayout;
    footer->addStretch(1);
    footer->addWidget(close);
    v->addLayout(footer);

    outer->addWidget(card);
    dlg.exec();
}

} // namespace NativeOffice
