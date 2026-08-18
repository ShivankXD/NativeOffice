#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ActivityLog.h — the history behind the Home screen's "Your Activity" graph.
//
// UsageStats keeps three running totals, which is enough to print three
// numbers but not enough to draw a line. This keeps the same events bucketed
// in time so the panel can plot the last 24 hours, 7 days, 30 days or 12
// weeks, and so hovering a point can name the day and the count.
//
// Storage is one compact JSON object in QSettings ("activity/log"):
//     { "h2026081714": 3,          hourly action counts
//       "s2026-08-17": 5400 }      seconds of foreground time per day
// Buckets older than kRetentionDays are dropped on write, so the value stays
// small no matter how long the app has been installed.
//
// "Action" means one document opened or one file saved — the same two events
// UsageStats counts, recorded through it so no call site changes.
// ─────────────────────────────────────────────────────────────────────────────

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVector>

namespace NativeOffice {

class ActivityLog : public QObject {
    Q_OBJECT

public:
    enum class Range {
        Day,       // 24 hourly buckets ending at the current hour
        Week,      // 7 daily buckets ending today
        Month,     // 30 daily buckets
        Quarter    // 12 weekly buckets
    };

    struct Point {
        QDateTime when;      // start of the bucket
        int       actions {0};
        QString   label;     // "Aug 15" / "14:00" / "Wk of Aug 11"
    };

    static ActivityLog& instance();

    // One opened/saved event, bucketed into the current hour.
    void noteAction();
    // Foreground seconds, bucketed into today.
    void addSeconds(qint64 seconds);

    [[nodiscard]] QVector<Point> series(Range r) const;
    [[nodiscard]] int            totalActions(Range r) const;
    [[nodiscard]] qint64         secondsIn(Range r) const;
    // Longest run of consecutive days with at least one action, ending today
    // or yesterday. Shown in the full activity window.
    [[nodiscard]] int            currentStreakDays() const;
    // The busiest bucket in the range, for the window's summary line.
    [[nodiscard]] Point          busiest(Range r) const;

    static QString rangeName(Range r);

signals:
    void changed();

private:
    explicit ActivityLog(QObject* parent = nullptr);
    ~ActivityLog() override = default;

    ActivityLog(const ActivityLog&)            = delete;
    ActivityLog& operator=(const ActivityLog&) = delete;
};

} // namespace NativeOffice
