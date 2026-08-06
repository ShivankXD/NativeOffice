#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// DataCleanserPanel.h — the side panel that drives DataCleanser.
//
// Deliberately knows nothing about entitlement. It is told whether Premium is
// active (setPremiumActive) and only decides how to draw the lock badges; the
// real check happens in CalcModule where AuthManager lives, so a panel that was
// somehow shown in the wrong state still cannot unlock anything.
//
// Gated actions stay clickable rather than greyed out. A disabled button is a
// dead end; a live one that explains the offer is how the rest of the app
// treats Premium, and it means the user finds out what the feature does before
// deciding whether to pay for it.
// ─────────────────────────────────────────────────────────────────────────────

#include <QFrame>

#include "DataCleanser.h"

// Forward declarations must sit at global scope. Writing "class QVBoxLayout*"
// inline in the member signature below would instead declare
// NativeOffice::QVBoxLayout and shadow the real one in every translation unit
// that includes this header.
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace NativeOffice {

class DataCleanserPanel : public QFrame {
    Q_OBJECT

public:
    explicit DataCleanserPanel(QWidget* parent = nullptr);

    [[nodiscard]] DataCleanser::Options options() const;
    [[nodiscard]] bool wholeSheet() const;

    void setStatus(const QString& msg, bool isError = false);
    void setPremiumActive(bool on);

signals:
    void runRequested(NativeOffice::DataCleanser::Op op);
    void closeRequested();

private:
    QPushButton* addAction(QVBoxLayout* v, DataCleanser::Op op, const QString& blurb);
    void         refreshBadges();

    QComboBox*  m_scope    { nullptr };
    QCheckBox*  m_header   { nullptr };
    QComboBox*  m_dateOrder{ nullptr };
    QLabel*     m_status   { nullptr };
    bool        m_premium  { false };

    struct Entry { QPushButton* btn; DataCleanser::Op op; QString label; };
    QVector<Entry> m_entries;
};

} // namespace NativeOffice
