// ─────────────────────────────────────────────────────────────────────────────
// UsageStats.cpp — see UsageStats.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "UsageStats.h"
#include "ActivityLog.h"

#include <QSettings>
#include <QTimer>

namespace NativeOffice {

namespace {
constexpr auto kTotalSeconds = "usage/totalSeconds";
constexpr auto kDocsOpened   = "usage/documentsOpened";
constexpr auto kFilesEdited  = "usage/filesEdited";
constexpr int  kFlushMs      = 60 * 1000;
}

UsageStats& UsageStats::instance() {
    static UsageStats inst;
    return inst;
}

UsageStats::UsageStats(QObject* parent) : QObject(parent) {}

void UsageStats::startSession() {
    if (m_session.isValid()) return;      // already running
    m_session.start();

    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(kFlushMs);
    connect(m_flushTimer, &QTimer::timeout, this, [this] {
        flush();
        emit changed();                   // keeps an open Home tab ticking
    });
    m_flushTimer->start();
}

void UsageStats::flush() {
    if (!m_session.isValid()) return;
    const qint64 elapsed = m_session.elapsed() / 1000;
    if (elapsed <= 0) return;

    QSettings st;
    st.setValue(kTotalSeconds, st.value(kTotalSeconds, 0).toLongLong() + elapsed);
    // The same seconds, bucketed per day, so the activity window can show how
    // long each day actually ran rather than one lifetime total.
    ActivityLog::instance().addSeconds(elapsed);
    m_session.restart();
}

qint64 UsageStats::totalSeconds() const {
    const qint64 stored = QSettings().value(kTotalSeconds, 0).toLongLong();
    return stored + (m_session.isValid() ? m_session.elapsed() / 1000 : 0);
}

QString UsageStats::formattedTotal() const {
    const qint64 s = totalSeconds();
    if (s < 60)    return QStringLiteral("under a minute");
    const qint64 mins = s / 60;
    if (mins < 60) return QStringLiteral("%1 min").arg(mins);
    const qint64 hours = mins / 60;
    const qint64 rem   = mins % 60;
    return rem == 0 ? QStringLiteral("%1 h").arg(hours)
                    : QStringLiteral("%1 h %2 min").arg(hours).arg(rem);
}

void UsageStats::noteDocumentOpened() {
    QSettings st;
    st.setValue(kDocsOpened, st.value(kDocsOpened, 0).toInt() + 1);
    ActivityLog::instance().noteAction();
    emit changed();
}

void UsageStats::noteFileEdited() {
    QSettings st;
    st.setValue(kFilesEdited, st.value(kFilesEdited, 0).toInt() + 1);
    ActivityLog::instance().noteAction();
    emit changed();
}

int UsageStats::documentsOpened() const {
    return QSettings().value(kDocsOpened, 0).toInt();
}

int UsageStats::filesEdited() const {
    return QSettings().value(kFilesEdited, 0).toInt();
}

} // namespace NativeOffice
