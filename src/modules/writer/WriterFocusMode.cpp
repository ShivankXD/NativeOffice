// ─────────────────────────────────────────────────────────────────────────────
// WriterFocusMode.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "WriterFocusMode.h"

#include <QVariantAnimation>
#include <QEasingCurve>
#include <QTimer>
#include <QPainter>
#include <QPaintEvent>
#include <QRegion>
#include <QFont>
#include <QColor>
#include <cmath>

namespace NativeOffice {

namespace {
constexpr int    kDurationMs = 950;    // "slowly, like a curtain"
constexpr double kInkLead    = 1.7;    // black reaches full opacity before the
                                       // aperture finishes closing
} // namespace

FocusOverlay::FocusOverlay(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("writerFocusOverlay");
    // Paint straight over whatever is beneath without erasing it first, so the
    // chrome visibly darkens through the black instead of snapping away.
    setAttribute(Qt::WA_NoSystemBackground, true);
    setFocusPolicy(Qt::NoFocus);          // typing keeps going to the document
    setCursor(Qt::ArrowCursor);
    hide();

    // The overlay is outside the parent's layout, so nothing resizes it for us.
    // Track the parent directly instead of relying on the owner to call refresh()
    // — the parent can grow after we engage (window shown, tab activated), and a
    // missed resize shows up as chrome bleeding down the uncovered edge.
    parent->installEventFilter(this);

    // The page's geometry settles across several layout passes, and Qt gives no
    // single reliable "done" signal: a zoom resizes the paper, then re-centres it
    // one or more passes later. Cutting the hole from any one event caught the
    // new width against the old x and let the desk show through the curtain.
    // So don't chase events — while engaged, re-check the page against the hole
    // and re-cut only when it actually differs. A rect compare a few times a
    // second is free, and it is self-correcting whatever the layout does.
    m_settle = new QTimer(this);
    m_settle->setInterval(60);
    connect(m_settle, &QTimer::timeout, this, [this] {
        if (holeAt(m_progress) != m_lastHole) { applyMask(); update(); }
    });

    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(kDurationMs);
    m_anim->setEasingCurve(QEasingCurve::InOutCubic);
    connect(m_anim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& v) { setProgress(v.toDouble()); });
    connect(m_anim, &QVariantAnimation::finished, this, [this] {
        if (!m_on) hide();                // fully lifted → stop intercepting clicks
    });
}

void FocusOverlay::engage(bool on) {
    if (m_on == on && m_anim->state() != QAbstractAnimation::Running) return;
    m_on = on;
    m_anim->stop();
    if (on) {
        // Cover the whole module and sit above every sibling — this is what
        // guarantees nothing bleeds through at the edges.
        syncGeometry();
        show();
        raise();
        m_settle->start();          // keep the hole locked onto the page
    } else {
        m_settle->stop();
    }
    // Scale the duration by the distance left to travel, so toggling mid-fade
    // reverses smoothly instead of restarting from the beginning.
    const double target = on ? 1.0 : 0.0;
    m_anim->setDuration(int(kDurationMs * qAbs(target - m_progress)) + 1);
    m_anim->setStartValue(m_progress);
    m_anim->setEndValue(target);
    m_anim->start();
}

bool FocusOverlay::eventFilter(QObject* o, QEvent* e) {
    if (o == parentWidget() && e->type() == QEvent::Resize) {
        syncGeometry();
        update();
    }
    return QWidget::eventFilter(o, e);
}

void FocusOverlay::syncGeometry() {
    QWidget* p = parentWidget();
    if (!p) return;
    if (geometry() != p->rect()) setGeometry(p->rect());
    applyMask();
}

void FocusOverlay::refresh() {
    if (!isVisible()) return;
    syncGeometry();
    update();
}

void FocusOverlay::setProgress(double p) {
    m_progress = p;
    applyMask();
    update();
}

QRect FocusOverlay::holeAt(double p) const {
    const QRect full = rect();
    QRect page = m_hole ? m_hole() : QRect();
    page = page.intersected(full);
    if (page.isEmpty()) page = full;      // nothing to spotlight → no hole to cut

    // The aperture starts as the whole window and closes in onto the page.
    const auto lerp = [p](int a, int b) { return int(std::lround(a + (b - a) * p)); };
    return QRect(QPoint(lerp(full.left(),  page.left()),  lerp(full.top(),    page.top())),
                 QPoint(lerp(full.right(), page.right()), lerp(full.bottom(), page.bottom())));
}

void FocusOverlay::applyMask() {
    m_lastHole = holeAt(m_progress);
    // Mask = everything except the page. Qt uses this for hit-testing too, so the
    // page stays fully interactive while the black swallows every other click.
    setMask(QRegion(rect()).subtracted(QRegion(m_lastHole)));
}

void FocusOverlay::paintEvent(QPaintEvent* e) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int alpha = int(qBound(0.0, m_progress * kInkLead, 1.0) * 255.0);
    // The mask already clips this to everything outside the page.
    p.fillRect(e->rect(), QColor(0, 0, 0, alpha));

    // Shortcut hint, top-right, fading in once the curtain is nearly shut so it
    // never competes with the animation.
    if (m_hint.isEmpty() || m_progress < 0.55) return;
    const double t = qBound(0.0, (m_progress - 0.55) / 0.45, 1.0);
    QFont f("Segoe UI", 9);
    f.setLetterSpacing(QFont::PercentageSpacing, 102);
    p.setFont(f);
    p.setPen(QColor(150, 156, 166, int(t * 235)));
    const QRect r = rect().adjusted(0, 14, -18, 0);
    p.drawText(r, Qt::AlignTop | Qt::AlignRight, m_hint);
}

} // namespace NativeOffice
