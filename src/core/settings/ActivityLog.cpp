// ─────────────────────────────────────────────────────────────────────────────
// ActivityLog.cpp: see ActivityLog.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "ActivityLog.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace NativeOffice {

namespace {

constexpr auto kKey            = "activity/log";
constexpr int  kRetentionDays  = 120;

QString hourKey(const QDateTime& t) {
    return QStringLiteral("h") + t.toString(QStringLiteral("yyyyMMddHH"));
}
QString dayKey(const QDate& d) {
    return QStringLiteral("s") + d.toString(Qt::ISODate);
}

QJsonObject load() {
    const QByteArray raw = QSettings().value(kKey).toByteArray();
    if (raw.isEmpty()) return {};
    return QJsonDocument::fromJson(raw).object();
}

// Writes back, dropping anything older than the retention window. Both key
// shapes carry their date in a sortable prefix, so a lexicographic compare
// against the cutoff is enough.
void store(QJsonObject obj) {
    const QDate cutoff = QDate::currentDate().addDays(-kRetentionDays);
    const QString hourCut = QStringLiteral("h") + cutoff.toString(QStringLiteral("yyyyMMdd"));
    const QString dayCut  = QStringLiteral("s") + cutoff.toString(Qt::ISODate);

    for (const QString& k : obj.keys()) {
        if (k.startsWith(QLatin1Char('h')) && k.left(9) < hourCut) obj.remove(k);
        else if (k.startsWith(QLatin1Char('s')) && k < dayCut)     obj.remove(k);
    }
    QSettings().setValue(kKey, QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

} // namespace

ActivityLog& ActivityLog::instance() {
    static ActivityLog inst;
    return inst;
}

ActivityLog::ActivityLog(QObject* parent) : QObject(parent) {}

void ActivityLog::noteAction() {
    QJsonObject obj = load();
    const QString k = hourKey(QDateTime::currentDateTime());
    obj[k] = obj.value(k).toInt() + 1;
    store(obj);
    emit changed();
}

void ActivityLog::addSeconds(qint64 seconds) {
    if (seconds <= 0) return;
    QJsonObject obj = load();
    const QString k = dayKey(QDate::currentDate());
    obj[k] = double(qint64(obj.value(k).toDouble()) + seconds);
    store(obj);
    // No changed() here: this fires every minute and would rebuild the graph
    // for a value the graph does not plot.
}

QVector<ActivityLog::Point> ActivityLog::series(Range r) const {
    const QJsonObject obj = load();
    QVector<Point> out;

    const QDateTime now = QDateTime::currentDateTime();

    switch (r) {
    case Range::Day: {
        QDateTime cursor(now.date(), QTime(now.time().hour(), 0));
        for (int i = 23; i >= 0; --i) {
            const QDateTime slot = cursor.addSecs(-3600LL * i);
            out.append({ slot, obj.value(hourKey(slot)).toInt(),
                         slot.toString(QStringLiteral("h AP")) });
        }
        break;
    }
    case Range::Week:
    case Range::Month: {
        const int days = (r == Range::Week) ? 7 : 30;
        for (int i = days - 1; i >= 0; --i) {
            const QDate d = now.date().addDays(-i);
            int sum = 0;
            const QString prefix = QStringLiteral("h") + d.toString(QStringLiteral("yyyyMMdd"));
            for (int h = 0; h < 24; ++h)
                sum += obj.value(prefix + QStringLiteral("%1").arg(h, 2, 10, QLatin1Char('0'))).toInt();
            out.append({ QDateTime(d, QTime(0, 0)), sum,
                         d.toString(QStringLiteral("MMM d")) });
        }
        break;
    }
    case Range::Quarter: {
        for (int w = 11; w >= 0; --w) {
            const QDate end   = now.date().addDays(-7 * w);
            const QDate start = end.addDays(-6);
            int sum = 0;
            for (QDate d = start; d <= end; d = d.addDays(1)) {
                const QString prefix = QStringLiteral("h") + d.toString(QStringLiteral("yyyyMMdd"));
                for (int h = 0; h < 24; ++h)
                    sum += obj.value(prefix + QStringLiteral("%1").arg(h, 2, 10, QLatin1Char('0'))).toInt();
            }
            out.append({ QDateTime(start, QTime(0, 0)), sum,
                         QStringLiteral("Wk of ") + start.toString(QStringLiteral("MMM d")) });
        }
        break;
    }
    }
    return out;
}

int ActivityLog::totalActions(Range r) const {
    int sum = 0;
    for (const Point& p : series(r)) sum += p.actions;
    return sum;
}

qint64 ActivityLog::secondsIn(Range r) const {
    const QJsonObject obj = load();
    const QDate today = QDate::currentDate();
    const int days = r == Range::Day ? 1 : r == Range::Week ? 7
                   : r == Range::Month ? 30 : 84;
    qint64 sum = 0;
    for (int i = 0; i < days; ++i)
        sum += qint64(obj.value(dayKey(today.addDays(-i))).toDouble());
    return sum;
}

int ActivityLog::currentStreakDays() const {
    const QJsonObject obj = load();
    auto activeOn = [&obj](const QDate& d) {
        const QString prefix = QStringLiteral("h") + d.toString(QStringLiteral("yyyyMMdd"));
        for (int h = 0; h < 24; ++h)
            if (obj.value(prefix + QStringLiteral("%1").arg(h, 2, 10, QLatin1Char('0'))).toInt() > 0)
                return true;
        return false;
    };

    QDate d = QDate::currentDate();
    // A streak counted at 9 a.m. should not be broken just because nothing has
    // happened yet today, so start from yesterday when today is still empty.
    if (!activeOn(d)) d = d.addDays(-1);

    int streak = 0;
    while (activeOn(d) && streak < kRetentionDays) { ++streak; d = d.addDays(-1); }
    return streak;
}

ActivityLog::Point ActivityLog::busiest(Range r) const {
    Point best;
    for (const Point& p : series(r))
        if (p.actions > best.actions) best = p;
    return best;
}

QString ActivityLog::rangeName(Range r) {
    switch (r) {
    case Range::Day:     return QStringLiteral("Today");
    case Range::Week:    return QStringLiteral("This Week");
    case Range::Month:   return QStringLiteral("This Month");
    case Range::Quarter: break;
    }
    return QStringLiteral("Last 12 Weeks");
}

} // namespace NativeOffice
