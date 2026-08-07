#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// CalcHistoryPanel.h — version list, commit box and diff view for DocHistory.
//
// Committing and rolling back are free. Only the visual diff is gated: losing
// work is the thing a user needs protection from, and putting a paywall in
// front of "get my document back" would be indefensible.
//
// Knows nothing about entitlement or about SpreadsheetModel. CalcModule builds
// the snapshots, enforces the gate, and feeds results back in.
// ─────────────────────────────────────────────────────────────────────────────

#include <QFrame>
#include <QVector>

#include "core/history/DocHistory.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;

namespace NativeOffice {

class CalcHistoryPanel : public QFrame {
    Q_OBJECT

public:
    explicit CalcHistoryPanel(QWidget* parent = nullptr);

    void setVersions(const QVector<SnapshotInfo>& list);
    void showDiff(const QVector<DocChange>& changes, const QString& caption);
    void clearDiff();
    void setStatus(const QString& msg, bool isError = false);

    // Ids the user has selected, oldest first. Empty when nothing is selected.
    [[nodiscard]] QVector<int> selectedIds() const;
    [[nodiscard]] QString      message() const;
    void clearMessage();

signals:
    void commitRequested();
    void rollbackRequested();
    void compareRequested();
    void closeRequested();

private:
    QLineEdit*    m_message  { nullptr };
    QListWidget*  m_versions { nullptr };
    QTableWidget* m_diff     { nullptr };
    QLabel*       m_diffCap  { nullptr };
    QLabel*       m_status   { nullptr };
};

} // namespace NativeOffice
