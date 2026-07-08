#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// BrandBar.h — the unified brand tray shown above the ribbon in every editor
// (Writer, Calc, Impress, PDF). One implementation so all modules stay
// pixel-identical: the NativeOffice brand mark + two-tone wordmark on the
// left, soft decorative art on the right, and a Free/Premium plan pill pinned
// to the far right edge. Follows the chrome light/dark mode and updates live
// when the user's entitlement changes.
// ─────────────────────────────────────────────────────────────────────────────

#include <QPixmap>
#include <QWidget>

namespace NativeOffice {

class BrandBar : public QWidget {
    Q_OBJECT
public:
    explicit BrandBar(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QPixmap m_mark;               // brand mark cropped out of the full logo art
    bool    m_premium { false };
};

} // namespace NativeOffice
