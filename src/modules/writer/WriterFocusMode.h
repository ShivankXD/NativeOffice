#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// WriterFocusMode.h
// Focus Mode — a distraction-free writing view for Writer.
//
// FocusOverlay is a child of WriterModule that covers the whole module and
// paints solid black over everything *except* the page, closing in from the
// edges like a scope opening. Nothing is re-laid-out when it engages: the page
// stays exactly where it was, so the document never reflows on the way in or
// out (an overlay is also the only way to guarantee no chrome bleeds through).
//
// The overlay uses setMask() rather than transparency, which does double duty:
// the page is a real hole, so clicks, selection and typing reach the document
// underneath, while every click on the black lands on the overlay and is
// swallowed — the (invisible) ribbon can't be hit by accident.
// ─────────────────────────────────────────────────────────────────────────────

#include <QWidget>
#include <QRect>
#include <QString>
#include <functional>

class QVariantAnimation;

namespace NativeOffice {

class FocusOverlay : public QWidget {
    Q_OBJECT

public:
    explicit FocusOverlay(QWidget* parent);

    // Returns the page rect to keep visible, in this widget's coordinates.
    void setHoleProvider(std::function<QRect()> fn) { m_hole = std::move(fn); }

    // Small dim hint painted top-right, e.g. "Ctrl+Shift+F to exit Focus Mode".
    void setHintText(const QString& t) { m_hint = t; }

    // Animate the curtain in (on) or out (off). Safe to call repeatedly.
    void engage(bool on);
    [[nodiscard]] bool engaged() const noexcept { return m_on; }

    // Recompute the hole (call on scroll / zoom).
    void refresh();

protected:
    void paintEvent(QPaintEvent* e) override;
    // Watches the parent so the overlay always spans it exactly — a stale
    // geometry would leave a strip of un-blacked chrome down the edge.
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void setProgress(double p);
    void syncGeometry();
    void applyMask();
    [[nodiscard]] QRect holeAt(double p) const;   // full rect → page rect

    std::function<QRect()> m_hole;
    QString            m_hint;
    QVariantAnimation* m_anim { nullptr };
    double             m_progress { 0.0 };   // 0 = clear, 1 = fully closed
    bool               m_on { false };
};

} // namespace NativeOffice
