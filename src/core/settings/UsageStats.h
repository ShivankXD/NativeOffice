#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UsageStats.h
// Backs the "Your Activity" panel on the Home screen.
//
// The panel used to print a literal "—" for time spent and reuse the
// recent-files count for BOTH "Documents opened" and "Files edited", so the
// two rows always showed the same number and the timer never moved. This keeps
// three genuinely separate counters in QSettings:
//
//   usage/totalSeconds     cumulative foreground time across all sessions
//   usage/documentsOpened  documents opened or created
//   usage/filesEdited      documents saved
//
// Elapsed time is folded into the stored total on a one-minute timer and again
// on quit, so a crash costs at most a minute rather than the whole session.
// ─────────────────────────────────────────────────────────────────────────────

#include <QObject>
#include <QElapsedTimer>
#include <QString>

class QTimer;

namespace NativeOffice {

class UsageStats : public QObject {
    Q_OBJECT

public:
    static UsageStats& instance();

    // Begin accumulating foreground time. Call once, at startup.
    void startSession();

    // Fold the elapsed session time into the persisted total and restart the
    // session clock. Safe to call repeatedly.
    void flush();

    [[nodiscard]] qint64 totalSeconds() const;   // persisted + current session
    [[nodiscard]] QString formattedTotal() const;  // "2 h 15 min", "8 min", "—"

    void noteDocumentOpened();
    void noteFileEdited();
    [[nodiscard]] int documentsOpened() const;
    [[nodiscard]] int filesEdited() const;

signals:
    // Emitted when any counter changes so the Home panel can refresh.
    void changed();

private:
    explicit UsageStats(QObject* parent = nullptr);
    ~UsageStats() override = default;

    UsageStats(const UsageStats&)            = delete;
    UsageStats& operator=(const UsageStats&) = delete;

    QElapsedTimer m_session;
    QTimer*       m_flushTimer { nullptr };
};

} // namespace NativeOffice
