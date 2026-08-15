#include "AiSlideImage.h"

#include <QBuffer>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

namespace NativeOffice {

namespace {

// Scales to fill and cuts the overflow. The vertical crop sits at 42% rather
// than the middle: in almost any photograph the subject is above centre, and
// centring the crop is what decapitates people in a wide banner.
QImage coverCrop(const QImage& src, const QSize& target) {
    if (src.isNull() || target.isEmpty()) return {};

    const qreal sx = qreal(target.width())  / src.width();
    const qreal sy = qreal(target.height()) / src.height();
    const qreal s  = qMax(sx, sy);

    const QSize scaled(qMax(target.width(),  qRound(src.width()  * s)),
                       qMax(target.height(), qRound(src.height() * s)));
    const QImage big = src.scaled(scaled, Qt::IgnoreAspectRatio,
                                  Qt::SmoothTransformation);

    const int x = (big.width()  - target.width())  / 2;
    const int y = qRound((big.height() - target.height()) * 0.42);
    return big.copy(QRect(QPoint(qMax(0, x), qMax(0, y)), target));
}

// Rec. 709 luminance, which is the one that matches how a person reads a
// photograph's brightness. The naive average makes reds far too dark and turns
// a duotone of anything with skin in it to mud.
inline qreal luma(QRgb p) {
    return (0.2126 * qRed(p) + 0.7152 * qGreen(p) + 0.0722 * qBlue(p)) / 255.0;
}

void applyDuotone(QImage& img, const QColor& shadow, const QColor& highlight) {
    for (int y = 0; y < img.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            // A little contrast is pushed in first. Mapping raw luminance onto
            // two colours flattens the picture into a single midtone smear;
            // steepening it keeps the shape of the subject.
            qreal t = luma(row[x]);
            t = qBound<qreal>(0, (t - 0.5) * 1.25 + 0.5, 1);
            row[x] = qRgb(qRound(shadow.red()   + (highlight.red()   - shadow.red())   * t),
                          qRound(shadow.green() + (highlight.green() - shadow.green()) * t),
                          qRound(shadow.blue()  + (highlight.blue()  - shadow.blue())  * t));
        }
    }
}

// A gradient stop list that fades to nothing over the last third. Two stops
// alone give a visible hard edge where the scrim ends; three read as light.
void scrim(QPainter& p, const QRectF& r, Qt::Edge from, const QColor& dark,
           int strongAlpha) {
    QLinearGradient g;
    switch (from) {
    case Qt::BottomEdge: g = QLinearGradient(r.left(), r.bottom(), r.left(), r.top()); break;
    case Qt::LeftEdge:   g = QLinearGradient(r.left(), r.top(),    r.right(), r.top()); break;
    default:             g = QLinearGradient(r.left(), r.top(),    r.left(), r.bottom()); break;
    }
    QColor c0 = dark; c0.setAlpha(strongAlpha);
    QColor c1 = dark; c1.setAlpha(qRound(strongAlpha * 0.55));
    QColor c2 = dark; c2.setAlpha(0);
    g.setColorAt(0.0,  c0);
    g.setColorAt(0.38, c1);
    g.setColorAt(0.85, c2);
    p.fillRect(r, g);
}

} // namespace

QByteArray composeSlideImage(const QByteArray& source, const QSize& target,
                             ImageTreatment treatment, const DeckTheme& theme,
                             qreal cornerRadius) {
    if (source.isEmpty() || target.isEmpty()) return {};

    QImage src;
    if (!src.loadFromData(source)) return {};
    if (src.isNull() || src.width() < 8 || src.height() < 8) return {};

    // A transparent source has to be composited, not flattened. Converting
    // straight to RGB32 throws the alpha away and keeps whatever was stored in
    // the colour channels underneath it, which for a great many PNGs is the
    // editor's own transparency checkerboard. One went out on a finished deck:
    // a full-bleed anatomy illustration with a grey chequered grid across the
    // whole slide, baked into the exported file where no later fix could reach
    // it. Painting the source over a solid ground blends by alpha properly and
    // leaves the theme's colour wherever the picture is see-through.
    if (src.hasAlphaChannel()) {
        const bool onDark = treatment != ImageTreatment::Plain
                         && treatment != ImageTreatment::Tinted;
        QImage flat(src.size(), QImage::Format_RGB32);
        flat.fill(onDark ? theme.deep : theme.paper);
        QPainter fp(&flat);
        fp.setRenderHint(QPainter::SmoothPixmapTransform, true);
        fp.drawImage(0, 0, src);
        fp.end();
        src = flat;
    } else {
        src = src.convertToFormat(QImage::Format_RGB32);
    }

    QImage img = coverCrop(src, target);
    if (img.isNull()) return {};

    if (treatment == ImageTreatment::Duotone) {
        // The highlight is pulled toward white so the bright end of the picture
        // stays bright. Mapping straight onto the accent leaves the whole frame
        // sitting at one saturation and looking like a colour cast rather than
        // a treatment.
        applyDuotone(img, mixed(theme.deep, QColor(Qt::black), 0.35),
                     mixed(theme.accent, QColor(Qt::white), 0.42));
    }

    // Anything with alpha in it (rounded corners) has to leave RGB32 behind.
    QImage out = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QRectF r(QPointF(0, 0), QSizeF(target));
    {
        QPainter p(&out);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QColor deepInk = mixed(theme.deep, QColor(Qt::black), 0.5);

        switch (treatment) {
        case ImageTreatment::ScrimBottom:
            scrim(p, r, Qt::BottomEdge, deepInk, 232);
            break;
        case ImageTreatment::ScrimSide:
            scrim(p, r, Qt::LeftEdge, deepInk, 228);
            break;
        case ImageTreatment::ScrimFull:
            // Heavy, and deliberately so. This is the treatment behind a title
            // set across the middle of the frame, and at anything lighter the
            // first bright photograph that came back made the words unreadable
            // and the slide look like a stock image with text dropped on it.
            p.fillRect(r, alpha(deepInk, 178));
            scrim(p, r, Qt::TopEdge,    deepInk, 80);
            scrim(p, r, Qt::BottomEdge, deepInk, 80);
            break;
        case ImageTreatment::Duotone:
            // The mapping already carries the theme, but its highlights still
            // come back bright enough to fight white type, so the whole frame
            // is taken down a step and the foot of it further still.
            p.fillRect(r, alpha(deepInk, 96));
            scrim(p, r, Qt::BottomEdge, deepInk, 140);
            break;
        case ImageTreatment::Tinted:
            // On a pale slide a raw photograph is the one element not sharing
            // the palette. A soft-light pass of the accent puts it back in the
            // same room without washing the picture out.
            p.setCompositionMode(QPainter::CompositionMode_SoftLight);
            p.fillRect(r, alpha(theme.accent, 90));
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            break;
        case ImageTreatment::Plain:
            break;
        }

        if (cornerRadius > 0.5) {
            // Cut the corners out of the alpha channel rather than covering
            // them with the slide colour, which would only work on a flat
            // background and shows as four pale wedges over anything else.
            QPainterPath keep;
            keep.addRoundedRect(r, cornerRadius, cornerRadius);
            QPainterPath cut;
            cut.addRect(r);
            cut = cut.subtracted(keep);
            p.setCompositionMode(QPainter::CompositionMode_Clear);
            p.fillPath(cut, Qt::black);
        }
    }

    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    if (!out.save(&buf, "PNG")) return {};
    return png;
}

} // namespace NativeOffice
