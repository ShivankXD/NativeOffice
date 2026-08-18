#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ActivityCard.h — the "Your Activity" panel and the window behind it.
//
//   ActivitySpark   the line chart itself. Hovering snaps to the nearest
//                   bucket, lifts its dot and floats a labelled bubble over
//                   it ("Aug 15 / 7 actions"); clicking anywhere on the plot
//                   opens the full window.
//   ActivityCard    the home-screen card: title, range pill, spark, and the
//                   three counters (files opened / files edited / time spent).
//   ActivityWindow  a resizable dialog with the same chart drawn large, range
//                   tabs for 24 hours / 7 days / 30 days / 12 weeks, and the
//                   totals for whichever range is showing.
//
// Everything reads ActivityLog, so the graph is real history rather than a
// decorative squiggle, and it survives restarts.
// ─────────────────────────────────────────────────────────────────────────────

#include "core/settings/ActivityLog.h"

#include <QDialog>
#include <QFrame>
#include <QVector>
#include <QWidget>

class QLabel;

namespace NativeOffice {

class ActivitySpark : public QWidget {
    Q_OBJECT

public:
    explicit ActivitySpark(QWidget* parent = nullptr);

    void setRange(ActivityLog::Range r);
    void setCompact(bool on);          // card size vs window size styling
    void reload();

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    [[nodiscard]] QVector<QPointF> plotPoints(const QRectF& plot, int peak) const;
    [[nodiscard]] QRectF plotRect() const;

    QVector<ActivityLog::Point> m_points;
    ActivityLog::Range m_range { ActivityLog::Range::Week };
    int  m_hover   { -1 };
    bool m_compact { true };
};

class ActivityCard : public QFrame {
    Q_OBJECT

public:
    explicit ActivityCard(QWidget* parent = nullptr);

private:
    void refreshCounters();
    void chooseRange();

    ActivitySpark* m_spark  { nullptr };
    QLabel*        m_opened { nullptr };
    QLabel*        m_edited { nullptr };
    QLabel*        m_spent  { nullptr };
    QLabel*        m_rangeLabel { nullptr };
    ActivityLog::Range m_range { ActivityLog::Range::Week };
};

class ActivityWindow : public QDialog {
    Q_OBJECT

public:
    explicit ActivityWindow(ActivityLog::Range initial, QWidget* parent = nullptr);

private:
    void select(ActivityLog::Range r);

    ActivitySpark* m_spark { nullptr };
    QLabel*        m_total { nullptr };
    QLabel*        m_peak  { nullptr };
    QLabel*        m_time  { nullptr };
    QLabel*        m_streak{ nullptr };
    QVector<QWidget*> m_tabs;
};

} // namespace NativeOffice
