// ─────────────────────────────────────────────────────────────────────────────
// Watermark.cpp — artwork and policy for the export watermark.
// ─────────────────────────────────────────────────────────────────────────────
#include "Watermark.h"

#include "auth/AuthManager.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QRect>
#include <QSettings>
#include <QtMath>

namespace NativeOffice {
namespace Watermark {

namespace {

// ── Design constants, in points ──────────────────────────────────────────────
// Sized to sit quietly in a page corner: readable at 100% but not competing
// with the content. The reference artwork is "Made with" in grey, the brand
// mark, then "Native" in ink and "Office" in the brand gradient.
constexpr qreal kMadeWithPt = 8.0;    // "Made with" type size
constexpr qreal kWordmarkPt = 10.0;   // "NativeOffice" type size
constexpr qreal kIconPt     = 13.0;   // brand mark, square
constexpr qreal kGapPt      = 4.0;    // "Made with" → icon
constexpr qreal kGap2Pt     = 3.0;    // icon → wordmark
constexpr qreal kMarginPt   = 24.0;   // page edge → mark

const char kGrey[]  = "#8A93A6";      // "Made with"
const char kInk[]   = "#141A26";      // "Native"
const char kGrad1[] = "#4F8BFF";      // "Office" gradient start
const char kGrad2[] = "#8B5CF6";      // "Office" gradient end

QFont madeWithFont(qreal scale) {
    QFont f(QStringLiteral("Segoe UI"));
    f.setPixelSize(qMax(1, qRound(kMadeWithPt * scale)));
    f.setWeight(QFont::Normal);
    return f;
}

QFont wordmarkFont(qreal scale) {
    QFont f(QStringLiteral("Segoe UI"));
    f.setPixelSize(qMax(1, qRound(kWordmarkPt * scale)));
    f.setBold(true);
    return f;
}

const QPixmap& brandMark() {
    // The shipped asset is a full lockup (mark + wordmark + strapline). Squeezed
    // into a 13pt square its wordmark turns to mush, so crop the "N" mark out of
    // it exactly the way BrandBar does. Fractions measured against the 1254x1254
    // artwork; keep the two in step if that file is ever replaced.
    static const QPixmap mark = [] {
        const QPixmap full(QStringLiteral(":/assets/nativeoffice-logo-mark.png"));
        if (full.isNull()) return QPixmap();
        return full.copy(QRect(int(full.width()  * 0.205),
                               int(full.height() * 0.115),
                               int(full.width()  * 0.600),
                               int(full.height() * 0.555)));
    }();
    return mark;
}

// Lays the mark out at a given scale and reports the piece geometry, so paint()
// and sizePoints() can never disagree about how wide the artwork is.
struct Layout {
    qreal madeW = 0, nativeW = 0, officeW = 0, iconW = 0;
    qreal width = 0, height = 0;
    qreal textBaselineTop = 0;   // top of the wordmark box
};

Layout layoutAt(qreal scale) {
    Layout L;
    const QFontMetricsF mfm(madeWithFont(scale));
    const QFontMetricsF wfm(wordmarkFont(scale));

    L.madeW   = mfm.horizontalAdvance(QStringLiteral("Made with"));
    L.nativeW = wfm.horizontalAdvance(QStringLiteral("Native"));
    L.officeW = wfm.horizontalAdvance(QStringLiteral("Office"));
    L.iconW   = kIconPt * scale;

    L.width  = L.madeW + kGapPt * scale + L.iconW + kGap2Pt * scale
             + L.nativeW + L.officeW;
    L.height = qMax(L.iconW, wfm.height());
    L.textBaselineTop = 0;
    return L;
}

} // namespace

QString targetUrl() { return QStringLiteral("https://nativeoffice.online"); }

bool enabledForExport() {
    // Free accounts always carry the mark. There is deliberately no setting,
    // export option or editor affordance that can turn this off, so a free
    // export cannot be produced without it.
    if (!AuthManager::instance().premiumActive()) return true;

    // Premium: off unless the user has switched it back on. Defaulting to false
    // means an account that upgrades stops watermarking immediately, with no
    // visit to Settings, and an existing premium account sees no change at all.
    return QSettings().value(QLatin1String(kSettingsKey), false).toBool();
}

QSizeF sizePoints() {
    const Layout L = layoutAt(1.0);
    return QSizeF(L.width, L.height);
}

qreal marginPoints() { return kMarginPt; }

QRectF paint(QPainter& p, const QRectF& pageRect, qreal scale) {
    const Layout L = layoutAt(scale);
    const qreal margin = kMarginPt * scale;

    const qreal right  = pageRect.right()  - margin;
    const qreal bottom = pageRect.bottom() - margin;
    const QRectF box(right - L.width, bottom - L.height, L.width, L.height);

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    qreal x = box.left();

    // "Made with"
    p.setFont(madeWithFont(scale));
    p.setPen(QColor(QLatin1String(kGrey)));
    p.drawText(QRectF(x, box.top(), L.madeW, box.height()),
               Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Made with"));
    x += L.madeW + kGapPt * scale;

    // Brand mark
    const QPixmap& mark = brandMark();
    if (!mark.isNull()) {
        const QRectF icon(x, box.center().y() - L.iconW / 2.0, L.iconW, L.iconW);
        p.drawPixmap(icon, mark, QRectF(mark.rect()));
    }
    x += L.iconW + kGap2Pt * scale;

    // "Native" in ink, "Office" in the brand gradient.
    p.setFont(wordmarkFont(scale));
    p.setPen(QColor(QLatin1String(kInk)));
    p.drawText(QRectF(x, box.top(), L.nativeW, box.height()),
               Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Native"));
    x += L.nativeW;

    QLinearGradient g(x, box.top(), x + L.officeW, box.bottom());
    g.setColorAt(0.0, QColor(QLatin1String(kGrad1)));
    g.setColorAt(1.0, QColor(QLatin1String(kGrad2)));
    p.setPen(QPen(QBrush(g), 0));
    p.drawText(QRectF(x, box.top(), L.officeW, box.height()),
               Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Office"));

    p.restore();
    return box;
}

QImage renderImage(qreal pxPerPoint) {
    const Layout L = layoutAt(pxPerPoint);
    // One pixel of bleed so antialiased edges are not clipped.
    const int w = qMax(1, qCeil(L.width) + 2);
    const int h = qMax(1, qCeil(L.height) + 2);

    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter p(&img);
    // paint() insets by its own margin, so hand it a page big enough that the
    // mark lands exactly on this canvas.
    const qreal margin = kMarginPt * pxPerPoint;
    paint(p, QRectF(0, 0, w + margin - 1, h + margin - 1), pxPerPoint);
    p.end();
    return img;
}

} // namespace Watermark
} // namespace NativeOffice
