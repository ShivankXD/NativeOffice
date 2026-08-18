// ─────────────────────────────────────────────────────────────────────────────
// ActivityCard.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "ActivityCard.h"
#include "HomeKit.h"
#include "LucideIcons.h"
#include "core/settings/UsageStats.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include <cmath>

namespace NativeOffice {

namespace {

QString formatSeconds(qint64 s) {
    if (s < 60)  return QStringLiteral("under a minute");
    const qint64 mins = s / 60;
    if (mins < 60) return QStringLiteral("%1 min").arg(mins);
    const qint64 h = mins / 60, rem = mins % 60;
    return rem == 0 ? QStringLiteral("%1 h").arg(h)
                    : QStringLiteral("%1 h %2 min").arg(h).arg(rem);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// ActivitySpark
// ─────────────────────────────────────────────────────────────────────────────
ActivitySpark::ActivitySpark(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(58);
    setAttribute(Qt::WA_TranslucentBackground);
    reload();
}

void ActivitySpark::setRange(ActivityLog::Range r) { m_range = r; reload(); }

void ActivitySpark::setCompact(bool on) {
    m_compact = on;
    setMinimumHeight(on ? 58 : 260);
    update();
}

void ActivitySpark::reload() {
    m_points = ActivityLog::instance().series(m_range);
    m_hover = -1;
    update();
}

QRectF ActivitySpark::plotRect() const {
    // Room above for the hover bubble, and below for the axis labels in the
    // large (window) presentation.
    const qreal top    = m_compact ? 26 : 42;
    const qreal bottom = m_compact ? 10 : 30;
    return QRectF(6, top, qMax<qreal>(10, width() - 12),
                  qMax<qreal>(10, height() - top - bottom));
}

QVector<QPointF> ActivitySpark::plotPoints(const QRectF& plot, int peak) const {
    QVector<QPointF> pts;
    const int n = m_points.size();
    if (n == 0) return pts;
    pts.reserve(n);
    const qreal step = n > 1 ? plot.width() / (n - 1) : 0;
    for (int i = 0; i < n; ++i) {
        const qreal frac = peak > 0 ? qreal(m_points[i].actions) / peak : 0.0;
        // Keep an empty series off the very bottom edge, so the line still
        // reads as a line rather than as a border.
        pts.append(QPointF(plot.left() + step * i,
                           plot.bottom() - frac * (plot.height() - 8) - 4));
    }
    return pts;
}

void ActivitySpark::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    const QRectF plot = plotRect();
    int peak = 0;
    for (const auto& pt : m_points) peak = qMax(peak, pt.actions);

    // ── Grid ────────────────────────────────────────────────────────────────
    p.setPen(QPen(QColor(255, 255, 255, 14), 1));
    const int gridLines = m_compact ? 3 : 5;
    for (int i = 0; i <= gridLines; ++i) {
        const qreal y = plot.top() + plot.height() * i / gridLines;
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }

    // A brand-new install has no history: a flat line along the floor reads as
    // a broken chart, so say what is actually going on instead.
    if (peak == 0) {
        QFont f("Segoe UI");
        f.setPixelSize(m_compact ? 11 : 13);
        p.setFont(f);
        p.setPen(QColor(Home::kFaint));
        p.drawText(plot, Qt::AlignCenter,
                   tr("No activity recorded for this range yet"));
        return;
    }

    const QVector<QPointF> pts = plotPoints(plot, peak);
    if (pts.size() < 2) return;

    // ── Area under the line ─────────────────────────────────────────────────
    QPainterPath area;
    area.moveTo(pts.first().x(), plot.bottom());
    for (const QPointF& q : pts) area.lineTo(q);
    area.lineTo(pts.last().x(), plot.bottom());
    area.closeSubpath();

    QLinearGradient fill(0, plot.top(), 0, plot.bottom());
    QColor a(Home::kAccent);
    a.setAlphaF(0.34);  fill.setColorAt(0.0, a);
    a.setAlphaF(0.02);  fill.setColorAt(1.0, a);
    p.fillPath(area, fill);

    // ── The line ────────────────────────────────────────────────────────────
    QPainterPath line;
    line.moveTo(pts.first());
    for (int i = 1; i < pts.size(); ++i) line.lineTo(pts[i]);
    p.setPen(QPen(QColor(Home::kAccentSoft), m_compact ? 1.9 : 2.4,
                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(line);

    // ── Dots ────────────────────────────────────────────────────────────────
    // A 24-point day series would be a bead necklace at card size, so only the
    // sparser ranges get a dot on every bucket.
    const bool dots = m_compact ? (pts.size() <= 14) : true;
    if (dots) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(Home::kAccentSoft));
        const qreal r = m_compact ? 2.6 : 3.4;
        for (const QPointF& q : pts) p.drawEllipse(q, r, r);
    }

    // ── Axis labels (window only) ───────────────────────────────────────────
    if (!m_compact) {
        QFont f("Segoe UI");
        f.setPixelSize(10);
        p.setFont(f);
        p.setPen(QColor(Home::kFaint));
        const int stride = qMax(1, m_points.size() / 8);
        for (int i = 0; i < m_points.size(); i += stride) {
            // Clamped inside the widget: the first and last labels are centred
            // on points that sit on the plot edges, so they would be cut in half.
            QRectF box(pts[i].x() - 34, plot.bottom() + 8, 68, 16);
            box.moveLeft(qBound<qreal>(0, box.left(), width() - box.width()));
            p.drawText(box, Qt::AlignCenter, m_points[i].label);
        }
    }

    // ── Hover ───────────────────────────────────────────────────────────────
    if (m_hover >= 0 && m_hover < pts.size()) {
        const QPointF h = pts[m_hover];

        p.setPen(QPen(QColor(255, 255, 255, 28), 1, Qt::DashLine));
        p.drawLine(QPointF(h.x(), plot.top()), QPointF(h.x(), plot.bottom()));

        p.setPen(QPen(QColor(Home::kPanel), 2));
        p.setBrush(QColor(Home::kAccentSoft));
        p.drawEllipse(h, m_compact ? 4.4 : 5.4, m_compact ? 4.4 : 5.4);

        // Bubble: the bucket label over its count, clamped inside the widget.
        const QString l1 = m_points[m_hover].label;
        const int n = m_points[m_hover].actions;
        const QString l2 = n == 1 ? tr("1 action") : tr("%1 actions").arg(n);
        QFont f1("Segoe UI"); f1.setPixelSize(11); f1.setWeight(QFont::DemiBold);
        QFont f2("Segoe UI"); f2.setPixelSize(11);
        const QFontMetrics m1(f1), m2(f2);
        const qreal w = qMax(m1.horizontalAdvance(l1), m2.horizontalAdvance(l2)) + 20;
        const qreal hh = 38;
        qreal x = h.x() - w / 2;
        x = qBound<qreal>(2, x, width() - w - 2);
        const qreal y = qMax<qreal>(2, h.y() - hh - 12);

        QRectF bub(x, y, w, hh);
        QPainterPath bp;
        bp.addRoundedRect(bub, 8, 8);
        p.setPen(QPen(QColor(Home::kBorder), 1));
        p.setBrush(QColor(28, 33, 48, 246));
        p.drawPath(bp);

        p.setFont(f1);
        p.setPen(QColor(Home::kText));
        p.drawText(bub.adjusted(0, 5, 0, 0), Qt::AlignHCenter | Qt::AlignTop, l1);
        p.setFont(f2);
        p.setPen(QColor(Home::kMuted));
        p.drawText(bub.adjusted(0, 0, 0, -5), Qt::AlignHCenter | Qt::AlignBottom, l2);
    }
}

void ActivitySpark::mouseMoveEvent(QMouseEvent* e) {
    const QRectF plot = plotRect();
    const int n = m_points.size();
    if (n < 2) return;
    const qreal step = plot.width() / (n - 1);
    const int idx = qBound(0, int(std::lround((e->position().x() - plot.left()) / step)), n - 1);
    if (idx != m_hover) { m_hover = idx; update(); }
}

void ActivitySpark::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) emit clicked();
    QWidget::mousePressEvent(e);
}

void ActivitySpark::leaveEvent(QEvent*) {
    if (m_hover != -1) { m_hover = -1; update(); }
}

// ─────────────────────────────────────────────────────────────────────────────
// ActivityCard
// ─────────────────────────────────────────────────────────────────────────────
ActivityCard::ActivityCard(QWidget* parent) : QFrame(parent) {
    setObjectName("sidePanel");
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(16, 12, 16, 10);
    v->setSpacing(7);

    // Header: title + range pill.
    auto* head = new QHBoxLayout();
    head->setSpacing(8);
    head->addWidget(label600(tr("Your Activity"), 14, Home::kText, this));
    head->addStretch();

    auto* pill = new ClickableFrame(this);
    pill->setObjectName("rangePill");
    pill->setFixedHeight(26);
    pill->setCursor(Qt::PointingHandCursor);
    auto* pl = new QHBoxLayout(pill);
    pl->setContentsMargins(10, 0, 8, 0);
    pl->setSpacing(6);
    m_rangeLabel = heading(ActivityLog::rangeName(m_range), 11, Home::kTextBody, false, pill);
    m_rangeLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    pl->addWidget(m_rangeLabel);
    pl->addWidget(Lucide::label(Lucide::kChevronDown, Home::kMuted, 13, pill));
    pill->onClick = [this] { chooseRange(); };
    head->addWidget(pill);
    v->addLayout(head);

    // Chart.
    m_spark = new ActivitySpark(this);
    connect(m_spark, &ActivitySpark::clicked, this, [this] {
        ActivityWindow win(m_range, window());
        win.exec();
        m_spark->reload();
        refreshCounters();
    });
    v->addWidget(m_spark);

    // Counters.
    auto row = [&](const char* icon, const QString& text, const QString& valueColor,
                   QLabel** out) {
        auto* r = new QWidget(this);
        auto* h = new QHBoxLayout(r);
        h->setContentsMargins(2, 1, 2, 1);
        h->setSpacing(10);
        h->addWidget(Lucide::label(icon, Home::kMuted, 15, r));
        h->addWidget(heading(text, 12, Home::kTextBody, false, r));
        h->addStretch();
        auto* value = label600(QString(), 12, valueColor, r);
        *out = value;
        h->addWidget(value);
        v->addWidget(r);
    };
    row(Lucide::kFileText, tr("Files opened"), Home::kText,   &m_opened);
    row(Lucide::kPencil,   tr("Files edited"), Home::kText,   &m_edited);
    row(Lucide::kClock,    tr("Time spent"),   Home::kAccentSoft, &m_spent);

    refreshCounters();
    connect(&UsageStats::instance(), &UsageStats::changed, this,
            &ActivityCard::refreshCounters);
    connect(&ActivityLog::instance(), &ActivityLog::changed, this,
            [this] { m_spark->reload(); });

    setStyleSheet(QString(R"(
        QFrame#sidePanel { background:%1; border:1px solid %2; border-radius:14px; }
        #rangePill { background:%3; border:1px solid %2; border-radius:8px; }
        #rangePill:hover { background:%4; }
    )").arg(Home::kPanel, Home::kBorder, Home::kPanelSoft, Home::kPanelHover));
}

void ActivityCard::refreshCounters() {
    auto& usage = UsageStats::instance();
    m_opened->setText(QString::number(usage.documentsOpened()));
    m_edited->setText(QString::number(usage.filesEdited()));
    m_spent->setText(usage.formattedTotal());
}

void ActivityCard::chooseRange() {
    QMenu menu(this);
    menu.setStyleSheet(QString(R"(
        QMenu { background:%1; border:1px solid %2; border-radius:10px; padding:6px; }
        QMenu::item { color:%3; font:12px 'Segoe UI'; padding:7px 22px 7px 12px;
            border-radius:7px; }
        QMenu::item:selected { background:%4; color:#FFFFFF; }
    )").arg(Home::kPanel, Home::kBorder, Home::kTextBody, Home::kPanelHover));

    const ActivityLog::Range ranges[] = {
        ActivityLog::Range::Day, ActivityLog::Range::Week,
        ActivityLog::Range::Month, ActivityLog::Range::Quarter };
    for (ActivityLog::Range r : ranges) {
        QAction* a = menu.addAction(ActivityLog::rangeName(r));
        connect(a, &QAction::triggered, this, [this, r] {
            m_range = r;
            m_rangeLabel->setText(ActivityLog::rangeName(r));
            m_spark->setRange(r);
        });
    }
    menu.exec(m_rangeLabel->mapToGlobal(QPoint(-10, m_rangeLabel->height() + 12)));
}

// ─────────────────────────────────────────────────────────────────────────────
// ActivityWindow
// ─────────────────────────────────────────────────────────────────────────────
ActivityWindow::ActivityWindow(ActivityLog::Range initial, QWidget* parent)
    : QDialog(parent) {
    setObjectName("activityWindow");
    setWindowTitle(tr("Your Activity"));
    resize(880, 600);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(28, 24, 28, 24);
    root->setSpacing(18);

    auto* head = new QHBoxLayout();
    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(3);
    titleCol->addWidget(heading(tr("Your Activity"), 22, Home::kText, true, this));
    titleCol->addWidget(heading(tr("Every document you open and save, over time."),
                                13, Home::kMuted, false, this));
    head->addLayout(titleCol);
    head->addStretch();
    root->addLayout(head);

    // Range tabs.
    auto* tabs = new QHBoxLayout();
    tabs->setSpacing(8);
    const ActivityLog::Range ranges[] = {
        ActivityLog::Range::Day, ActivityLog::Range::Week,
        ActivityLog::Range::Month, ActivityLog::Range::Quarter };
    for (ActivityLog::Range r : ranges) {
        auto* t = new ClickableFrame(this);
        t->setObjectName("rangeTab");
        t->setFixedHeight(34);
        t->setCursor(Qt::PointingHandCursor);
        auto* tl = new QHBoxLayout(t);
        tl->setContentsMargins(16, 0, 16, 0);
        auto* lab = heading(ActivityLog::rangeName(r), 12, Home::kTextBody, false, t);
        lab->setAttribute(Qt::WA_TransparentForMouseEvents);
        tl->addWidget(lab);
        t->onClick = [this, r] { select(r); };
        t->setProperty("range", int(r));
        tabs->addWidget(t);
        m_tabs.append(t);
    }
    tabs->addStretch();
    root->addLayout(tabs);

    // Chart.
    auto* chartCard = new QFrame(this);
    chartCard->setObjectName("chartCard");
    auto* cv = new QVBoxLayout(chartCard);
    cv->setContentsMargins(14, 14, 14, 10);
    m_spark = new ActivitySpark(chartCard);
    m_spark->setCompact(false);
    cv->addWidget(m_spark, 1);
    root->addWidget(chartCard, 1);

    // Summary tiles.
    auto* stats = new QHBoxLayout();
    stats->setSpacing(14);
    auto tile = [&](const char* icon, const QString& caption, QLabel** out) {
        auto* f = new QFrame(this);
        f->setObjectName("statTile");
        auto* fv = new QVBoxLayout(f);
        fv->setContentsMargins(16, 13, 16, 13);
        fv->setSpacing(5);
        auto* top = new QHBoxLayout();
        top->setSpacing(8);
        top->addWidget(Lucide::label(icon, Home::kMuted, 14, f));
        top->addWidget(heading(caption, 11, Home::kMuted, false, f));
        top->addStretch();
        fv->addLayout(top);
        auto* value = heading(QStringLiteral("0"), 21, Home::kText, true, f);
        *out = value;
        fv->addWidget(value);
        stats->addWidget(f, 1);
    };
    tile(Lucide::kTrendingUp, tr("Actions in range"), &m_total);
    tile(Lucide::kSparkles,   tr("Busiest bucket"),   &m_peak);
    tile(Lucide::kClock,      tr("Time in range"),    &m_time);
    tile(Lucide::kRepeat,     tr("Current streak"),   &m_streak);
    root->addLayout(stats);

    setStyleSheet(QString(R"(
        QDialog#activityWindow { background:%1; }
        QFrame#chartCard { background:%2; border:1px solid %3; border-radius:14px; }
        QFrame#statTile  { background:%2; border:1px solid %3; border-radius:12px; }
        #rangeTab { background:%4; border:1px solid %3; border-radius:9px; }
        #rangeTab:hover { background:%5; }
        #rangeTabOn { background:#22203C; border:1px solid %6; border-radius:9px; }
    )").arg(Home::kBg, Home::kPanel, Home::kBorder, Home::kPanelSoft,
            Home::kPanelHover, Home::kAccent));

    select(initial);
}

void ActivityWindow::select(ActivityLog::Range r) {
    m_spark->setRange(r);

    for (QWidget* t : m_tabs) {
        const bool on = ActivityLog::Range(t->property("range").toInt()) == r;
        t->setObjectName(on ? "rangeTabOn" : "rangeTab");
        t->style()->unpolish(t);
        t->style()->polish(t);
    }

    auto& log = ActivityLog::instance();
    m_total->setText(QString::number(log.totalActions(r)));
    const ActivityLog::Point best = log.busiest(r);
    m_peak->setText(best.actions > 0
                        ? QStringLiteral("%1 · %2").arg(best.label).arg(best.actions)
                        : QStringLiteral("—"));
    m_time->setText(formatSeconds(log.secondsIn(r)));
    const int streak = log.currentStreakDays();
    m_streak->setText(streak == 1 ? tr("1 day") : tr("%1 days").arg(streak));
}

} // namespace NativeOffice
