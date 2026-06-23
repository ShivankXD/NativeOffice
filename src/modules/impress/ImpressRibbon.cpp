// ─────────────────────────────────────────────────────────────────────────────
// ImpressRibbon.cpp  (Sprint 12 → Sprint 13: WPS-style grouped ribbon)
// ─────────────────────────────────────────────────────────────────────────────
#include "ImpressRibbon.h"
#include "ImpressModule.h"   // ImpressViewMode

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QToolButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QStackedWidget>
#include <QMenu>
#include <QWidgetAction>
#include <QPainterPath>
#include <QDialog>
#include <QSpinBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QFrame>
#include <QFont>
#include <QFontDatabase>
#include <QColorDialog>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QLinearGradient>
#include <QIcon>
#include <QDir>
#include <QPolygonF>
#include <functional>

namespace NativeOffice {

namespace {

// Monochrome icon tint for the light ribbon (dark slate, à la PowerPoint/WPS).
const QColor kIconColor("#3A3F4B");

// Paint a crisp monochrome icon so we never depend on a glyph being
// present in the system font. Drawn on a 40×40 canvas, displayed at 20×20.
QIcon paintIcon(const std::function<void(QPainter&)>& draw) {
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(kIconColor);
    pen.setWidthF(2.4);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    draw(p);
    p.end();
    return QIcon(pm);
}

QIcon newSlideIcon() {
    return paintIcon([](QPainter& p) {
        p.drawRoundedRect(QRectF(7, 9, 20, 15), 2, 2);     // slide
        p.drawLine(QPointF(30, 22), QPointF(30, 34));       // plus (vertical)
        p.drawLine(QPointF(24, 28), QPointF(36, 28));       // plus (horizontal)
    });
}

QIcon duplicateIcon() {
    return paintIcon([](QPainter& p) {
        p.drawRoundedRect(QRectF(8, 8, 18, 18), 2, 2);
        p.drawRoundedRect(QRectF(16, 16, 18, 18), 2, 2);
    });
}

QIcon deleteIcon() {
    return paintIcon([](QPainter& p) {
        p.drawLine(QPointF(9, 13), QPointF(31, 13));        // lid
        p.drawLine(QPointF(16, 13), QPointF(16, 10));       // handle
        p.drawLine(QPointF(24, 13), QPointF(24, 10));
        p.drawLine(QPointF(16, 10), QPointF(24, 10));
        p.drawLine(QPointF(12, 13), QPointF(14, 33));       // body
        p.drawLine(QPointF(28, 13), QPointF(26, 33));
        p.drawLine(QPointF(14, 33), QPointF(26, 33));
        p.drawLine(QPointF(20, 16), QPointF(20, 30));       // center stripe
    });
}

QIcon bulletIcon(bool numbered) {
    return paintIcon([numbered](QPainter& p) {
        QPen pen(kIconColor);
        pen.setWidthF(2.2);
        pen.setCapStyle(Qt::RoundCap);
        const int ys[3] = { 12, 21, 30 };
        for (int i = 0; i < 3; ++i) {
            // text lines
            p.setPen(pen);
            p.drawLine(QPointF(17, ys[i]), QPointF(33, ys[i]));
            // marker
            if (numbered) {
                QFont f("Segoe UI", 7, QFont::Bold);
                p.setFont(f);
                p.drawText(QRectF(5, ys[i] - 7, 11, 14),
                           Qt::AlignRight | Qt::AlignVCenter,
                           QString::number(i + 1));
            } else {
                p.setBrush(kIconColor);
                p.setPen(Qt::NoPen);
                p.drawEllipse(QPointF(9, ys[i]), 2.2, 2.2);
            }
        }
    });
}

QIcon indentIcon(bool increase) {
    return paintIcon([increase](QPainter& p) {
        QPen pen(kIconColor);
        pen.setWidthF(2.2);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        const int ys[4] = { 11, 18, 25, 32 };
        for (int i = 0; i < 4; ++i) {
            const int x0 = (i == 1 || i == 2) ? 18 : 8;
            p.drawLine(QPointF(x0, ys[i]), QPointF(33, ys[i]));
        }
        // arrow between the indented lines
        p.setBrush(kIconColor);
        p.setPen(Qt::NoPen);
        QPolygonF tri;
        if (increase)
            tri << QPointF(8, 17) << QPointF(8, 26) << QPointF(14, 21.5);
        else
            tri << QPointF(14, 17) << QPointF(14, 26) << QPointF(8, 21.5);
        p.drawPolygon(tri);
    });
}

// Paint a unicode/emoji glyph as an icon. Geometric glyphs pick up the
// monochrome ribbon tint; colour-emoji glyphs render in their own colours.
QIcon glyphIcon(const QString& glyph, int px = 20) {
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont f("Segoe UI Emoji");
    f.setPixelSize(px);
    p.setFont(f);
    p.setPen(kIconColor);
    p.drawText(QRectF(0, 0, 40, 40), Qt::AlignCenter, glyph);
    p.end();
    return QIcon(pm);
}

QIcon templatesIcon() {
    return paintIcon([](QPainter& p) {
        p.drawRoundedRect(QRectF(6, 8, 12, 10), 1.5, 1.5);
        p.drawRoundedRect(QRectF(22, 8, 12, 10), 1.5, 1.5);
        p.drawRoundedRect(QRectF(6, 22, 12, 10), 1.5, 1.5);
        p.drawRoundedRect(QRectF(22, 22, 12, 10), 1.5, 1.5);
    });
}

QIcon tableIcon() {
    return paintIcon([](QPainter& p) {
        p.drawRect(QRectF(7, 9, 26, 22));
        p.drawLine(QPointF(7, 17),  QPointF(33, 17));
        p.drawLine(QPointF(7, 25),  QPointF(33, 25));
        p.drawLine(QPointF(16, 9),  QPointF(16, 31));
        p.drawLine(QPointF(25, 9),  QPointF(25, 31));
    });
}

QIcon chartKindIcon(ChartKind k) {
    return paintIcon([k](QPainter& p) {
        p.drawLine(QPointF(9, 8),  QPointF(9, 32));    // axes
        p.drawLine(QPointF(9, 32), QPointF(34, 32));
        p.setBrush(QColor(58, 63, 75, 60));
        switch (k) {
        case ChartKind::Bar:
            p.drawRect(QRectF(13, 22, 5, 10));
            p.drawRect(QRectF(21, 16, 5, 16));
            p.drawRect(QRectF(29, 12, 5, 20));
            break;
        case ChartKind::Line:
            p.drawPolyline(QPolygonF({ QPointF(12, 28), QPointF(19, 18),
                                       QPointF(26, 23), QPointF(33, 12) }));
            break;
        case ChartKind::Area: {
            QPolygonF a({ QPointF(12, 28), QPointF(19, 18), QPointF(26, 23),
                          QPointF(33, 13), QPointF(33, 32), QPointF(12, 32) });
            p.drawPolygon(a);
            break;
        }
        case ChartKind::Pie:
            p.drawEllipse(QRectF(13, 11, 20, 20));
            p.drawLine(QPointF(23, 21), QPointF(23, 11));
            p.drawLine(QPointF(23, 21), QPointF(33, 21));
            break;
        }
    });
}

// Paint a gallery shape into an icon, reusing the scene's own path geometry so
// the ribbon preview matches exactly what gets placed on the slide.
QIcon shapeKindIcon(ShapeKind k) {
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(kIconColor);
    pen.setWidthF(2.2);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(k == ShapeKind::Line ? QBrush(Qt::NoBrush) : QBrush(QColor(58, 63, 75, 45)));
    p.drawPath(SlideScene::shapePath(k, QRectF(8, 9, 24, 22)));
    p.end();
    return QIcon(pm);
}

// ── Office-grade vector icons (painted line art, no font glyphs) ─────────────
QIcon pictureIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(6, 9, 28, 22), 2, 2);
    p.drawEllipse(QPointF(13, 16), 2.6, 2.6);
    QPolygonF m; m << QPointF(8, 29) << QPointF(16, 20) << QPointF(22, 25)
                   << QPointF(27, 20) << QPointF(33, 29);
    p.drawPolyline(m);
}); }

QIcon textBoxIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(6, 10, 28, 20), 2, 2);
    p.drawLine(QPointF(14, 16), QPointF(26, 16));
    p.drawLine(QPointF(20, 16), QPointF(20, 25));
}); }

QIcon rectShapeIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(8, 11, 24, 18), 2.5, 2.5);
}); }

QIcon ellipseShapeIcon() { return paintIcon([](QPainter& p) {
    p.drawEllipse(QRectF(8, 10, 24, 20));
}); }

QIcon fillIcon() { return paintIcon([](QPainter& p) {
    p.setBrush(QColor(58, 63, 75, 45));
    p.drawRoundedRect(QRectF(9, 12, 22, 18), 3, 3);
    p.setBrush(kIconColor); p.setPen(Qt::NoPen);
    QPainterPath d; d.moveTo(31, 7); d.cubicTo(35, 12, 35, 16, 31, 16);
    d.cubicTo(27, 16, 27, 12, 31, 7);
    p.drawPath(d);
}); }

QIcon outlineIcon() { return paintIcon([](QPainter& p) {
    QPen pen = p.pen(); pen.setWidthF(3.4); p.setPen(pen);
    p.drawRoundedRect(QRectF(9, 11, 22, 18), 3, 3);
}); }

QIcon shadowIcon() { return paintIcon([](QPainter& p) {
    p.setBrush(QColor(58, 63, 75, 55)); p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(14, 15, 18, 15), 3, 3);
    QPen pen(kIconColor); pen.setWidthF(2.4); p.setPen(pen); p.setBrush(Qt::white);
    p.drawRoundedRect(QRectF(8, 9, 18, 15), 3, 3);
}); }

QIcon smartArtIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(14, 7, 12, 8), 1.5, 1.5);
    p.drawRoundedRect(QRectF(5, 26, 11, 7), 1.5, 1.5);
    p.drawRoundedRect(QRectF(24, 26, 11, 7), 1.5, 1.5);
    p.drawLine(QPointF(20, 15), QPointF(20, 20));
    p.drawLine(QPointF(10, 20), QPointF(30, 20));
    p.drawLine(QPointF(10, 20), QPointF(10, 26));
    p.drawLine(QPointF(30, 20), QPointF(30, 26));
}); }

QIcon wordArtIcon() { return paintIcon([](QPainter& p) {
    QPen pen = p.pen(); pen.setWidthF(3.0); p.setPen(pen);
    p.drawLine(QPointF(9, 32), QPointF(20, 7));
    p.drawLine(QPointF(20, 7), QPointF(31, 32));
    p.drawLine(QPointF(13, 23), QPointF(27, 23));
}); }

QIcon symbolIcon() { return paintIcon([](QPainter& p) {
    p.drawArc(QRectF(11, 8, 18, 18), -20 * 16, 220 * 16);
    p.drawLine(QPointF(10, 30), QPointF(17, 28));
    p.drawLine(QPointF(30, 30), QPointF(23, 28));
}); }

QIcon slideNumberIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(10, 7, 20, 26), 2, 2);
    p.drawLine(QPointF(17, 13), QPointF(15, 27));
    p.drawLine(QPointF(24, 13), QPointF(22, 27));
    p.drawLine(QPointF(13, 19), QPointF(27, 19));
    p.drawLine(QPointF(12, 23), QPointF(26, 23));
}); }

QIcon dateTimeIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(7, 9, 26, 24), 2, 2);
    p.drawLine(QPointF(7, 16), QPointF(33, 16));
    p.drawLine(QPointF(13, 6), QPointF(13, 11));
    p.drawLine(QPointF(27, 6), QPointF(27, 11));
    p.setBrush(kIconColor); p.setPen(Qt::NoPen);
    for (int yy : { 22, 28 }) for (int xx : { 13, 20, 27 }) p.drawEllipse(QPointF(xx, yy), 1.5, 1.5);
}); }

QIcon commentsIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(7, 8, 26, 17), 4, 4);
    p.drawLine(QPointF(13, 25), QPointF(12, 31));
    p.drawLine(QPointF(12, 31), QPointF(20, 25));
    p.setBrush(kIconColor); p.setPen(Qt::NoPen);
    for (int xx : { 14, 20, 26 }) p.drawEllipse(QPointF(xx, 16.5), 1.4, 1.4);
}); }

QIcon undoIcon() { return paintIcon([](QPainter& p) {
    QPainterPath path; path.moveTo(12, 18); path.cubicTo(17, 9, 28, 10, 30, 21);
    p.drawPath(path);
    p.setBrush(kIconColor); p.setPen(Qt::NoPen);
    QPolygonF h; h << QPointF(7, 15) << QPointF(16, 14) << QPointF(11, 23);
    p.drawPolygon(h);
}); }

QIcon redoIcon() { return paintIcon([](QPainter& p) {
    QPainterPath path; path.moveTo(28, 18); path.cubicTo(23, 9, 12, 10, 10, 21);
    p.drawPath(path);
    p.setBrush(kIconColor); p.setPen(Qt::NoPen);
    QPolygonF h; h << QPointF(33, 15) << QPointF(24, 14) << QPointF(29, 23);
    p.drawPolygon(h);
}); }

QIcon transitionIcon(SlideTransition t) { return paintIcon([t](QPainter& p) {
    switch (t) {
    case SlideTransition::None:
        p.drawLine(QPointF(12, 20), QPointF(28, 20)); break;
    case SlideTransition::Fade: {
        p.drawRoundedRect(QRectF(9, 10, 22, 20), 2, 2);
        p.setBrush(QColor(58, 63, 75, 55)); p.setPen(Qt::NoPen);
        QPainterPath tri; tri.moveTo(9, 30); tri.lineTo(31, 30); tri.lineTo(9, 10); tri.closeSubpath();
        p.drawPath(tri); break;
    }
    case SlideTransition::Push: {
        p.drawRect(QRectF(8, 12, 11, 16));
        p.drawLine(QPointF(22, 20), QPointF(31, 20));
        p.setBrush(kIconColor); p.setPen(Qt::NoPen);
        QPolygonF a; a << QPointF(28, 16) << QPointF(33, 20) << QPointF(28, 24); p.drawPolygon(a); break;
    }
    case SlideTransition::Wipe: {
        p.drawRect(QRectF(9, 11, 22, 18));
        p.setBrush(QColor(58, 63, 75, 55)); p.setPen(Qt::NoPen);
        p.drawRect(QRectF(9, 11, 11, 18)); break;
    }
    case SlideTransition::Zoom: {
        p.drawEllipse(QRectF(9, 9, 15, 15));
        p.drawLine(QPointF(21, 21), QPointF(31, 31)); break;
    }
    case SlideTransition::Cut: {
        p.drawLine(QPointF(12, 12), QPointF(28, 26));
        p.drawLine(QPointF(28, 14), QPointF(12, 28));
        p.drawEllipse(QRectF(9, 25, 6, 6));
        p.drawEllipse(QRectF(25, 25, 6, 6)); break;
    }
    case SlideTransition::Cover: {
        p.drawRect(QRectF(8, 13, 15, 15));
        p.setBrush(Qt::white);
        p.drawRect(QRectF(18, 9, 15, 15)); break;
    }
    case SlideTransition::Uncover: {
        p.drawRect(QRectF(17, 13, 15, 15));
        p.setBrush(Qt::white);
        p.drawRect(QRectF(7, 9, 15, 15)); break;
    }
    case SlideTransition::Dissolve: {
        p.setBrush(kIconColor); p.setPen(Qt::NoPen);
        for (int yy = 12; yy <= 28; yy += 4)
            for (int xx = 12; xx <= 28; xx += 4) p.drawEllipse(QPointF(xx, yy), 1.3, 1.3);
        break;
    }
    case SlideTransition::Blinds:
        for (int xx = 11; xx <= 29; xx += 6) p.drawLine(QPointF(xx, 10), QPointF(xx, 30));
        break;
    }
}); }

QIcon animIcon(ItemAnimation a) { return paintIcon([a](QPainter& p) {
    auto head = [&](const QPolygonF& h) { p.setBrush(kIconColor); p.setPen(Qt::NoPen); p.drawPolygon(h); };
    switch (a) {
    case ItemAnimation::None:
        p.drawLine(QPointF(12, 20), QPointF(28, 20)); break;
    case ItemAnimation::FadeIn:
        p.setBrush(QColor(58, 63, 75, 50));
        p.drawRoundedRect(QRectF(11, 11, 18, 18), 2, 2); break;
    case ItemAnimation::FlyInLeft:
        p.drawRoundedRect(QRectF(21, 14, 12, 12), 2, 2);
        p.drawLine(QPointF(6, 20), QPointF(16, 20));
        head(QPolygonF() << QPointF(13, 16) << QPointF(18, 20) << QPointF(13, 24)); break;
    case ItemAnimation::FlyInRight:
        p.drawRoundedRect(QRectF(7, 14, 12, 12), 2, 2);
        p.drawLine(QPointF(34, 20), QPointF(24, 20));
        head(QPolygonF() << QPointF(27, 16) << QPointF(22, 20) << QPointF(27, 24)); break;
    case ItemAnimation::FlyInTop:
        p.drawRoundedRect(QRectF(14, 21, 12, 12), 2, 2);
        p.drawLine(QPointF(20, 6), QPointF(20, 16));
        head(QPolygonF() << QPointF(16, 13) << QPointF(20, 18) << QPointF(24, 13)); break;
    case ItemAnimation::FlyInBottom:
        p.drawRoundedRect(QRectF(14, 7, 12, 12), 2, 2);
        p.drawLine(QPointF(20, 34), QPointF(20, 24));
        head(QPolygonF() << QPointF(16, 27) << QPointF(20, 22) << QPointF(24, 27)); break;
    case ItemAnimation::ZoomIn:
        p.drawRoundedRect(QRectF(8, 8, 24, 24), 2, 2);
        p.drawRoundedRect(QRectF(15, 15, 10, 10), 1.5, 1.5); break;
    case ItemAnimation::SpinIn:
    case ItemAnimation::EmphasisSpin: {
        QPainterPath path; path.arcMoveTo(QRectF(11, 11, 18, 18), 60);
        path.arcTo(QRectF(11, 11, 18, 18), 60, 250);
        p.drawPath(path);
        head(QPolygonF() << QPointF(23, 7) << QPointF(31, 10) << QPointF(25, 15)); break;
    }
    case ItemAnimation::EmphasisPulse:
        p.drawEllipse(QRectF(16, 16, 8, 8));
        p.drawEllipse(QRectF(9, 9, 22, 22)); break;
    case ItemAnimation::EmphasisBlink:
        p.drawLine(QPointF(20, 8), QPointF(20, 32));
        p.drawLine(QPointF(8, 20), QPointF(32, 20));
        p.drawLine(QPointF(12, 12), QPointF(28, 28));
        p.drawLine(QPointF(28, 12), QPointF(12, 28)); break;
    case ItemAnimation::ExitFadeOut: {
        QPen pen = p.pen(); pen.setStyle(Qt::DashLine); p.setPen(pen);
        p.drawRoundedRect(QRectF(11, 11, 18, 18), 2, 2); break;
    }
    case ItemAnimation::ExitFlyLeft:
        p.drawRoundedRect(QRectF(18, 14, 12, 12), 2, 2);
        p.drawLine(QPointF(16, 20), QPointF(5, 20));
        head(QPolygonF() << QPointF(9, 16) << QPointF(4, 20) << QPointF(9, 24)); break;
    case ItemAnimation::ExitFlyRight:
        p.drawRoundedRect(QRectF(10, 14, 12, 12), 2, 2);
        p.drawLine(QPointF(24, 20), QPointF(35, 20));
        head(QPolygonF() << QPointF(31, 16) << QPointF(36, 20) << QPointF(31, 24)); break;
    case ItemAnimation::ExitZoomOut:
        p.drawRoundedRect(QRectF(13, 13, 14, 14), 1.5, 1.5);
        p.drawRoundedRect(QRectF(7, 7, 26, 26), 2, 2); break;
    }
}); }

// Whole-slide animation icons: a slide outline plus an effect indicator.
QIcon slideAnimIcon(SlideAnimation a) { return paintIcon([a](QPainter& p) {
    auto slide = [&] { p.drawRoundedRect(QRectF(9, 12, 22, 16), 2, 2); };
    auto head  = [&](const QPolygonF& h) { p.setBrush(kIconColor); p.setPen(Qt::NoPen); p.drawPolygon(h); };
    switch (a) {
    case SlideAnimation::None:
        p.drawLine(QPointF(12, 20), QPointF(28, 20)); break;
    case SlideAnimation::FadeIn: {
        QPen pen = p.pen(); pen.setStyle(Qt::DashLine); p.setPen(pen); slide(); break;
    }
    case SlideAnimation::ZoomIn:
        p.drawRoundedRect(QRectF(7, 10, 26, 20), 2, 2);
        p.drawRoundedRect(QRectF(15, 16, 10, 8), 1, 1); break;
    case SlideAnimation::WhirlIn: {
        QPainterPath path; path.arcMoveTo(QRectF(11, 11, 18, 18), 90);
        path.arcTo(QRectF(11, 11, 18, 18), 90, 300); p.drawPath(path);
        head(QPolygonF() << QPointF(18, 7) << QPointF(25, 10) << QPointF(19, 15)); break;
    }
    case SlideAnimation::SpiralIn:
        p.drawArc(QRectF(9, 9, 22, 22), 0, 360 * 16);
        p.drawArc(QRectF(14, 14, 12, 12), 30 * 16, 300 * 16); break;
    case SlideAnimation::FlyInLeft:
        slide(); p.drawLine(QPointF(3, 20), QPointF(8, 20));
        head(QPolygonF() << QPointF(6, 17) << QPointF(10, 20) << QPointF(6, 23)); break;
    case SlideAnimation::FlyInRight:
        slide(); p.drawLine(QPointF(37, 20), QPointF(32, 20));
        head(QPolygonF() << QPointF(34, 17) << QPointF(30, 20) << QPointF(34, 23)); break;
    case SlideAnimation::FlyInTop:
        slide(); p.drawLine(QPointF(20, 3), QPointF(20, 8));
        head(QPolygonF() << QPointF(17, 6) << QPointF(20, 10) << QPointF(23, 6)); break;
    case SlideAnimation::FlyInBottom:
        slide(); p.drawLine(QPointF(20, 37), QPointF(20, 32));
        head(QPolygonF() << QPointF(17, 34) << QPointF(20, 30) << QPointF(23, 34)); break;
    case SlideAnimation::Bounce:
        slide(); p.drawArc(QRectF(12, 4, 16, 12), 0, 180 * 16);
        head(QPolygonF() << QPointF(17, 10) << QPointF(20, 14) << QPointF(23, 10)); break;
    case SlideAnimation::RiseUp:
        slide(); p.drawLine(QPointF(20, 36), QPointF(20, 30));
        head(QPolygonF() << QPointF(16, 33) << QPointF(20, 28) << QPointF(24, 33)); break;
    case SlideAnimation::Drop:
        slide(); p.drawLine(QPointF(20, 2), QPointF(20, 9));
        head(QPolygonF() << QPointF(16, 6) << QPointF(20, 11) << QPointF(24, 6)); break;
    }
}); }

// A small thumbnail for Design "theme" tiles: a vertical top→bottom gradient
// so the tile previews exactly what the two-colour theme applies to the slide.
QIcon swatchThumb(const QColor& top, const QColor& bottom) {
    QPixmap pm(80, 56);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(0, 0, 0, 56);
    g.setColorAt(0.0, top);
    g.setColorAt(1.0, bottom);
    p.fillRect(pm.rect(), g);
    p.setPen(QPen(QColor(0, 0, 0, 40)));
    p.drawRect(0, 0, 79, 55);
    p.end();
    return QIcon(pm);
}

// mode: 0 = left, 1 = center, 2 = right, 3 = justify
QIcon alignIcon(int mode) {
    return paintIcon([mode](QPainter& p) {
        QPen pen(kIconColor);
        pen.setWidthF(2.2);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        const int ys[4]     = { 11, 18, 25, 32 };
        const int widths[4] = { 26, 16, 22, 14 };
        for (int i = 0; i < 4; ++i) {
            const int w = (mode == 3) ? 26 : widths[i];
            int x0, x1;
            if      (mode == 0) { x0 = 7;       x1 = 7 + w; }
            else if (mode == 2) { x1 = 33;      x0 = 33 - w; }
            else if (mode == 1) { x0 = 20 - w/2; x1 = 20 + w/2; }
            else                { x0 = 7;       x1 = 33; }
            p.drawLine(QPointF(x0, ys[i]), QPointF(x1, ys[i]));
        }
    });
}

} // namespace

ImpressRibbon::ImpressRibbon(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("impressRibbon");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Tab strip ───────────────────────────────────────────────────────
    auto* tabRow = new QWidget(this);
    tabRow->setObjectName("ribbonTabRow");
    tabRow->setFixedHeight(32);
    auto* tabLayout = new QHBoxLayout(tabRow);
    tabLayout->setContentsMargins(10, 0, 10, 0);
    tabLayout->setSpacing(2);

    m_tabGroup = new QButtonGroup(this);
    m_tabGroup->setExclusive(true);

    const QStringList tabNames = {
        "Home", "Insert", "Design", "Transitions",
        "Animations", "Slide Show", "Review", "View"
    };
    for (int i = 0; i < tabNames.size(); ++i) {
        auto* btn = makeTabButton(tabNames[i]);
        m_tabGroup->addButton(btn, i);
        tabLayout->addWidget(btn);
    }
    tabLayout->addStretch();
    m_tabGroup->button(0)->setChecked(true);

    // ── Stacked content ─────────────────────────────────────────────────
    m_stack = new QStackedWidget(this);
    m_stack->setObjectName("ribbonStack");
    m_stack->setFixedHeight(92);
    m_stack->addWidget(buildHomeTab());
    m_stack->addWidget(buildInsertTab());
    m_stack->addWidget(buildDesignTab());
    m_stack->addWidget(buildTransitionsTab());
    m_stack->addWidget(buildAnimationsTab());
    m_stack->addWidget(buildSlideShowTab());
    m_stack->addWidget(buildReviewTab());
    m_stack->addWidget(buildViewTab());

    connect(m_tabGroup, &QButtonGroup::idClicked, m_stack, &QStackedWidget::setCurrentIndex);

    root->addWidget(tabRow);
    root->addWidget(m_stack);

    applyStyles();
}

// ─────────────────────────────────────────────────────────────────────────────
// Home tab — Slides · Font · Paragraph groups
// ─────────────────────────────────────────────────────────────────────────────
QWidget* ImpressRibbon::buildHomeTab() {
    auto* tab = new QWidget(this);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2);
    layout->setSpacing(0);

    // Undo / redo now live in the always-visible top brand bar (so they work
    // regardless of which ribbon tab is active), not here in the Home tab.

    // ── Slides group ────────────────────────────────────────────────────
    auto* btnNew = makeCmdBtn("New Slide", "Add a new slide", 104);
    btnNew->setIcon(newSlideIcon());
    btnNew->setIconSize(QSize(20, 20));
    btnNew->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(btnNew, &QToolButton::clicked, this, &ImpressRibbon::newSlideRequested);

    auto* layoutCombo = new QComboBox(tab);
    layoutCombo->setObjectName("ribbonCombo");
    layoutCombo->addItem("Title Slide", static_cast<int>(SlideLayout::Title));
    layoutCombo->addItem("Title + Content", static_cast<int>(SlideLayout::TitleContent));
    layoutCombo->addItem("Blank", static_cast<int>(SlideLayout::Blank));
    layoutCombo->setFixedWidth(126);
    layoutCombo->setToolTip("Slide layout");
    connect(layoutCombo, &QComboBox::activated, this, [this, layoutCombo](int idx) {
        emit layoutSelected(static_cast<SlideLayout>(layoutCombo->itemData(idx).toInt()));
    });

    auto* btnDup = makeIconBtn(duplicateIcon(), "Duplicate this slide");
    auto* btnDel = makeIconBtn(deleteIcon(),    "Delete this slide");
    connect(btnDup, &QToolButton::clicked, this, &ImpressRibbon::duplicateSlideRequested);
    connect(btnDel, &QToolButton::clicked, this, &ImpressRibbon::deleteSlideRequested);
    layout->addWidget(makeGroup("Slides", { btnNew, layoutCombo, btnDup, btnDel }));
    layout->addWidget(makeSeparator());

    // ── Font group ──────────────────────────────────────────────────────
    m_fontCombo = new QComboBox(tab);
    m_fontCombo->setObjectName("ribbonCombo");
    m_fontCombo->setFixedWidth(140);
    m_fontCombo->setToolTip("Font family");
    const QFontDatabase fdb;
    for (const QString& f : fdb.families()) m_fontCombo->addItem(f);
    const int segoeIdx = m_fontCombo->findText("Segoe UI");
    m_fontCombo->setCurrentIndex(segoeIdx >= 0 ? segoeIdx : 0);
    connect(m_fontCombo, &QComboBox::currentTextChanged, this, [this](const QString& f) {
        if (!m_syncing) emit fontFamilyChanged(f);
    });

    m_sizeCombo = new QComboBox(tab);
    m_sizeCombo->setObjectName("ribbonCombo");
    m_sizeCombo->setEditable(true);
    m_sizeCombo->setFixedWidth(54);
    m_sizeCombo->setToolTip("Font size");
    for (int s : {8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 36, 40, 48, 60, 72})
        m_sizeCombo->addItem(QString::number(s));
    m_sizeCombo->setCurrentText("18");
    connect(m_sizeCombo, &QComboBox::currentTextChanged, this, [this](const QString& s) {
        if (m_syncing) return;
        bool ok = false; const int pt = s.toInt(&ok);
        if (ok && pt > 0) emit fontSizeChanged(pt);
    });

    m_btnBold      = makeToolBtn("B", "Bold  (Ctrl+B)", true);
    m_btnItalic    = makeToolBtn("I", "Italic  (Ctrl+I)", true);
    m_btnUnderline = makeToolBtn("U", "Underline  (Ctrl+U)", true);
    m_btnStrike    = makeToolBtn("S", "Strikethrough", true);
    m_btnBold->setFont(QFont("Segoe UI", 11, QFont::Bold));
    QFont it("Segoe UI", 11); it.setItalic(true); m_btnItalic->setFont(it);
    QFont ul("Segoe UI", 11); ul.setUnderline(true); m_btnUnderline->setFont(ul);
    QFont sk("Segoe UI", 11); sk.setStrikeOut(true); m_btnStrike->setFont(sk);
    connect(m_btnBold,      &QToolButton::toggled, this, [this](bool c){ if (!m_syncing) emit boldToggled(c); });
    connect(m_btnItalic,    &QToolButton::toggled, this, [this](bool c){ if (!m_syncing) emit italicToggled(c); });
    connect(m_btnUnderline, &QToolButton::toggled, this, [this](bool c){ if (!m_syncing) emit underlineToggled(c); });
    connect(m_btnStrike,    &QToolButton::toggled, this, [this](bool c){ if (!m_syncing) emit strikeToggled(c); });

    m_btnColor = makeToolBtn("A", "Font colour");
    connect(m_btnColor, &QToolButton::clicked, this, [this]() {
        const QColor c = QColorDialog::getColor(m_textColor, this, "Font Colour");
        if (c.isValid()) { m_textColor = c; emit textColorChanged(c); }
    });

    layout->addWidget(makeGroup("Font", {
        m_fontCombo, m_sizeCombo, m_btnBold, m_btnItalic,
        m_btnUnderline, m_btnStrike, m_btnColor }));
    layout->addWidget(makeSeparator());

    // ── Paragraph group ─────────────────────────────────────────────────
    m_btnAlignLeft    = makeIconBtn(alignIcon(0), "Align left", true);
    m_btnAlignCenter  = makeIconBtn(alignIcon(1), "Align center", true);
    m_btnAlignRight   = makeIconBtn(alignIcon(2), "Align right", true);
    m_btnAlignJustify = makeIconBtn(alignIcon(3), "Justify", true);
    connect(m_btnAlignLeft,    &QToolButton::clicked, this, [this]{ emit alignChanged(Qt::AlignLeft); });
    connect(m_btnAlignCenter,  &QToolButton::clicked, this, [this]{ emit alignChanged(Qt::AlignHCenter); });
    connect(m_btnAlignRight,   &QToolButton::clicked, this, [this]{ emit alignChanged(Qt::AlignRight); });
    connect(m_btnAlignJustify, &QToolButton::clicked, this, [this]{ emit alignChanged(Qt::AlignJustify); });

    auto* btnBullets = makeIconBtn(bulletIcon(false), "Bullet list");
    auto* btnNumbers = makeIconBtn(bulletIcon(true),  "Numbered list");
    connect(btnBullets, &QToolButton::clicked, this, &ImpressRibbon::bulletsRequested);
    connect(btnNumbers, &QToolButton::clicked, this, &ImpressRibbon::numberingRequested);

    auto* btnIndentDec = makeIconBtn(indentIcon(false), "Decrease indent");
    auto* btnIndentInc = makeIconBtn(indentIcon(true),  "Increase indent");
    connect(btnIndentInc, &QToolButton::clicked, this, [this]{ emit indentRequested(+1); });
    connect(btnIndentDec, &QToolButton::clicked, this, [this]{ emit indentRequested(-1); });

    auto* spacingCombo = new QComboBox(tab);
    spacingCombo->setObjectName("ribbonCombo");
    spacingCombo->setFixedWidth(64);
    spacingCombo->setToolTip("Line spacing");
    for (double v : {1.0, 1.15, 1.5, 2.0}) spacingCombo->addItem(QString::number(v) + "×", v);
    connect(spacingCombo, &QComboBox::activated, this, [this, spacingCombo](int idx) {
        emit lineSpacingChanged(spacingCombo->itemData(idx).toDouble());
    });

    layout->addWidget(makeGroup("Paragraph", {
        m_btnAlignLeft, m_btnAlignCenter, m_btnAlignRight, m_btnAlignJustify,
        btnBullets, btnNumbers, btnIndentDec, btnIndentInc, spacingCombo }));

    layout->addStretch();
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// Insert tab
// ─────────────────────────────────────────────────────────────────────────────
QWidget* ImpressRibbon::buildInsertTab() {
    auto* tab = new QWidget(this);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2);
    layout->setSpacing(0);

    // ── Slides ──────────────────────────────────────────────────────────
    auto* btnNew = makeBigBtn(newSlideIcon(), "New\nSlide", "Add a new slide");
    auto* btnDup = makeBigBtn(duplicateIcon(), "Duplicate", "Duplicate this slide");
    connect(btnNew, &QToolButton::clicked, this, &ImpressRibbon::newSlideRequested);
    connect(btnDup, &QToolButton::clicked, this, &ImpressRibbon::duplicateSlideRequested);
    layout->addWidget(makeGroup("Slides", { btnNew, btnDup }));
    layout->addWidget(makeSeparator());

    // ── Images ──────────────────────────────────────────────────────────
    auto* btnImage = makeBigBtn(pictureIcon(), "Pictures",
                                "Insert an image from a file");
    connect(btnImage, &QToolButton::clicked, this, &ImpressRibbon::insertImageRequested);
    layout->addWidget(makeGroup("Images", { btnImage }));
    layout->addWidget(makeSeparator());

    // ── Shapes (interactive insert modes) ───────────────────────────────
    m_shapeGroup = new QButtonGroup(this);
    m_shapeGroup->setExclusive(true);
    auto* btnText    = makeBigBtn(textBoxIcon(),     "Text\nBox",  "Insert a text box", true);
    auto* btnRect    = makeBigBtn(rectShapeIcon(),    "Rectangle", "Insert a rectangle", true);
    auto* btnEllipse = makeBigBtn(ellipseShapeIcon(), "Ellipse",   "Insert an ellipse",   true);
    m_shapeGroup->addButton(btnText,    static_cast<int>(InsertMode::TextBox));
    m_shapeGroup->addButton(btnRect,    static_cast<int>(InsertMode::Rectangle));
    m_shapeGroup->addButton(btnEllipse, static_cast<int>(InsertMode::Ellipse));
    connect(m_shapeGroup, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
        if (checked) emit insertModeChanged(static_cast<InsertMode>(id));
    });

    // ── Shapes gallery (drop-down grid of preset shapes) ────────────────
    auto* btnGallery = makeBigBtn(shapeKindIcon(ShapeKind::Star5), "More\nShapes",
                                  "Choose from the shape gallery");
    btnGallery->setPopupMode(QToolButton::InstantPopup);
    auto* shapeMenu = new QMenu(btnGallery);
    auto* galleryGrid = new QWidget(shapeMenu);
    auto* gl = new QGridLayout(galleryGrid);
    gl->setContentsMargins(8, 8, 8, 8);
    gl->setSpacing(4);
    struct GShape { ShapeKind kind; const char* tip; };
    const GShape gallery[] = {
        { ShapeKind::Rectangle,     "Rectangle" },
        { ShapeKind::RoundedRect,   "Rounded rectangle" },
        { ShapeKind::Ellipse,       "Ellipse" },
        { ShapeKind::Triangle,      "Triangle" },
        { ShapeKind::RightTriangle, "Right triangle" },
        { ShapeKind::Diamond,       "Diamond" },
        { ShapeKind::Pentagon,      "Pentagon" },
        { ShapeKind::Hexagon,       "Hexagon" },
        { ShapeKind::Star5,         "5-point star" },
        { ShapeKind::Arrow,         "Block arrow" },
        { ShapeKind::Chevron,       "Chevron" },
        { ShapeKind::Line,          "Line" },
        { ShapeKind::Cloud,         "Cloud" },
        { ShapeKind::Heart,         "Heart" },
    };
    int gi = 0;
    for (const auto& gs : gallery) {
        auto* tile = new QToolButton(galleryGrid);
        tile->setObjectName("ribbonToolBtn");
        tile->setIcon(shapeKindIcon(gs.kind));
        tile->setIconSize(QSize(26, 26));
        tile->setFixedSize(40, 40);
        tile->setToolTip(gs.tip);
        tile->setCursor(Qt::PointingHandCursor);
        const ShapeKind kind = gs.kind;
        connect(tile, &QToolButton::clicked, this, [this, shapeMenu, kind] {
            emit shapeInsertRequested(kind);
            shapeMenu->hide();
        });
        gl->addWidget(tile, gi / 5, gi % 5);
        ++gi;
    }
    auto* wa = new QWidgetAction(shapeMenu);
    wa->setDefaultWidget(galleryGrid);
    shapeMenu->addAction(wa);
    btnGallery->setMenu(shapeMenu);

    layout->addWidget(makeGroup("Shapes", { btnText, btnRect, btnEllipse, btnGallery }));
    layout->addWidget(makeSeparator());

    // ── Shape Style (applies to the selected shape) ─────────────────────
    auto* btnFill = makeBigBtn(fillIcon(), "Fill",
                               "Fill colour of the selected shape");
    auto* btnOutline = makeBigBtn(outlineIcon(), "Outline",
                                  "Outline colour of the selected shape");
    auto* btnShadow = makeBigBtn(shadowIcon(), "Shadow",
                                 "Toggle a drop shadow on the selected object");
    connect(btnFill, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(QColor("#E8372A"), this, "Shape Fill",
                                                QColorDialog::ShowAlphaChannel);
        if (c.isValid()) emit shapeFillRequested(c);
    });
    connect(btnOutline, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(QColor("#2C3140"), this, "Shape Outline");
        if (c.isValid()) emit shapeOutlineRequested(c);
    });
    connect(btnShadow, &QToolButton::clicked, this, &ImpressRibbon::shadowToggleRequested);
    layout->addWidget(makeGroup("Shape Style", { btnFill, btnOutline, btnShadow }));
    layout->addWidget(makeSeparator());

    // ── SmartArt (diagram templates) ────────────────────────────────────
    auto* btnSmart = makeBigBtn(smartArtIcon(), "Smart\nArt", "Insert a diagram");
    btnSmart->setPopupMode(QToolButton::InstantPopup);
    auto* smartMenu = new QMenu(btnSmart);
    struct SA { const char* label; SmartArtKind kind; };
    const SA sas[] = {
        { "Process Flow", SmartArtKind::ProcessFlow },
        { "Cycle",        SmartArtKind::Cycle },
        { "Hierarchy",    SmartArtKind::Hierarchy },
        { "Pyramid",      SmartArtKind::Pyramid },
        { "Bullet List",  SmartArtKind::BulletList },
        { "Venn",         SmartArtKind::Venn },
    };
    for (const auto& sa : sas) {
        const SmartArtKind kind = sa.kind;
        smartMenu->addAction(sa.label, this, [this, kind] { emit smartArtRequested(kind); });
    }
    btnSmart->setMenu(smartMenu);
    layout->addWidget(makeGroup("SmartArt", { btnSmart }));
    layout->addWidget(makeSeparator());

    // ── Tables ──────────────────────────────────────────────────────────
    auto* btnTable = makeBigBtn(tableIcon(), "Table", "Insert a table");
    connect(btnTable, &QToolButton::clicked, this, [this] {
        QDialog dlg(this);
        dlg.setWindowTitle("Insert Table");
        auto* form = new QFormLayout(&dlg);
        auto* rowsSpin = new QSpinBox(&dlg);
        rowsSpin->setRange(1, 20); rowsSpin->setValue(3);
        auto* colsSpin = new QSpinBox(&dlg);
        colsSpin->setRange(1, 12); colsSpin->setValue(3);
        form->addRow("Rows:", rowsSpin);
        form->addRow("Columns:", colsSpin);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() == QDialog::Accepted)
            emit insertTableRequested(rowsSpin->value(), colsSpin->value());
    });
    layout->addWidget(makeGroup("Tables", { btnTable }));
    layout->addWidget(makeSeparator());

    // ── Charts ──────────────────────────────────────────────────────────
    struct Ch { const char* label; ChartKind kind; };
    const Ch charts[] = {
        { "Bar",  ChartKind::Bar },
        { "Line", ChartKind::Line },
        { "Pie",  ChartKind::Pie },
        { "Area", ChartKind::Area },
    };
    QList<QWidget*> chBtns;
    for (const auto& c : charts) {
        auto* b = makeBigBtn(chartKindIcon(c.kind), c.label, QString("Insert a %1 chart").arg(c.label));
        const ChartKind kind = c.kind;
        connect(b, &QToolButton::clicked, this, [this, kind] { emit chartRequested(kind); });
        chBtns.append(b);
    }
    layout->addWidget(makeGroup("Charts", chBtns));
    layout->addWidget(makeSeparator());

    // ── Text ────────────────────────────────────────────────────────────
    auto* btnWordArt = makeBigBtn(wordArtIcon(),     "WordArt",   "Insert decorative WordArt text");
    auto* btnSymbol  = makeBigBtn(symbolIcon(),      "Symbol",    "Insert a symbol");
    auto* btnNumber  = makeBigBtn(slideNumberIcon(), "Slide\nNumber", "Insert the slide number");
    auto* btnDate    = makeBigBtn(dateTimeIcon(),    "Date &\nTime", "Insert today's date");
    connect(btnWordArt, &QToolButton::clicked, this, &ImpressRibbon::wordArtRequested);
    connect(btnSymbol,  &QToolButton::clicked, this, [this]{ emit symbolRequested(QString::fromUtf8("★")); });
    connect(btnNumber,  &QToolButton::clicked, this, &ImpressRibbon::slideNumberRequested);
    connect(btnDate,    &QToolButton::clicked, this, &ImpressRibbon::dateTimeRequested);
    layout->addWidget(makeGroup("Text", { btnWordArt, btnSymbol, btnNumber, btnDate }));
    layout->addWidget(makeSeparator());

    // ── Comments ────────────────────────────────────────────────────────
    auto* btnComment = makeBigBtn(commentsIcon(), "Notes",
                                  "Show or hide the speaker notes panel");
    connect(btnComment, &QToolButton::clicked, this, &ImpressRibbon::notesToggleRequested);
    layout->addWidget(makeGroup("Comments", { btnComment }));

    layout->addStretch();
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// Design tab — quick background colour swatches
// ─────────────────────────────────────────────────────────────────────────────
QWidget* ImpressRibbon::buildDesignTab() {
    auto* tab = new QWidget(this);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2);
    layout->setSpacing(0);

    // ── Themes (apply a two-colour vertical gradient background) ────────
    struct Theme { QColor top; QColor bottom; const char* name; };
    const Theme themes[] = {
        { "#FFFFFF", "#DBEAFE", "Office" },   // white → light blue
        { "#FFFFFF", "#DCFCE7", "Mint"   },   // white → light green
        { "#E8372A", "#1A1F2E", "Ember"  },   // scarlet → near-black
        { "#3B4252", "#1A1F2E", "Slate"  },   // slate → near-black
        { "#FFF7ED", "#FDBA74", "Amber"  },   // cream → amber
        { "#EFF6FF", "#2563EB", "Sky"    },   // pale → blue
    };
    auto* themeRow = new QWidget(tab);
    auto* trl = new QHBoxLayout(themeRow);
    trl->setContentsMargins(0, 0, 0, 0);
    trl->setSpacing(6);
    for (const auto& th : themes) {
        auto* btn = new QToolButton(themeRow);
        btn->setObjectName("ribbonThemeBtn");
        btn->setIcon(swatchThumb(th.top, th.bottom));
        btn->setIconSize(QSize(56, 38));
        btn->setFixedSize(64, 50);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(QString("%1 theme").arg(th.name));
        const QColor top = th.top, bottom = th.bottom;
        connect(btn, &QToolButton::clicked, this, [this, top, bottom]{
            if (m_designAllBtn && m_designAllBtn->isChecked()) emit designThemeAllRequested(top, bottom);
            else emit designThemeSelected(top, bottom);
        });
        trl->addWidget(btn);
    }
    layout->addWidget(makeGroup("Themes", { themeRow }));
    layout->addWidget(makeSeparator());

    // ── Templates ───────────────────────────────────────────────────────
    auto* btnTemplates = makeBigBtn(templatesIcon(), "Templates",
                                    "Browse and apply a ready-made slide design");
    connect(btnTemplates, &QToolButton::clicked, this, &ImpressRibbon::templatesRequested);
    layout->addWidget(makeGroup("Templates", { btnTemplates }));
    layout->addWidget(makeSeparator());

    // ── Background swatches + custom ────────────────────────────────────
    auto* swatchRow = new QWidget(tab);
    auto* swl = new QHBoxLayout(swatchRow);
    swl->setContentsMargins(0, 0, 0, 0);
    swl->setSpacing(5);
    const QList<QColor> swatches = {
        "#FFFFFF", "#F5F6FA", "#2C3140", "#1A1F2E",
        "#E8372A", "#EA580C", "#16A34A", "#2563EB"
    };
    for (const QColor& c : swatches) {
        auto* btn = new QToolButton(swatchRow);
        btn->setFixedSize(26, 26);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(c.name());
        btn->setStyleSheet(QString("QToolButton { background-color: %1; border: 1px solid #C6CAD3; border-radius: 4px; } QToolButton:hover { border: 2px solid #E8372A; }")
                            .arg(c.name()));
        connect(btn, &QToolButton::clicked, this, [this, c]{
            if (m_designAllBtn && m_designAllBtn->isChecked()) emit designColorAllRequested(c);
            else emit designColorSelected(c);
        });
        swl->addWidget(btn);
    }

    auto* btnCustom = makeCmdBtn(QString::fromUtf8("⊕  Custom…"), "Pick a custom background colour", 90);
    connect(btnCustom, &QToolButton::clicked, this, [this]() {
        const QColor c = QColorDialog::getColor(Qt::white, this, "Slide Background");
        if (!c.isValid()) return;
        if (m_designAllBtn && m_designAllBtn->isChecked()) emit designColorAllRequested(c);
        else emit designColorSelected(c);
    });

    m_designAllBtn = makeCmdBtn("Apply to All", "Apply the chosen background or theme to every slide", 104, true);

    layout->addWidget(makeGroup("Background", { swatchRow, btnCustom, m_designAllBtn }));
    layout->addWidget(makeSeparator());

    // ── Layout ──────────────────────────────────────────────────────────
    auto* layoutCombo = new QComboBox(tab);
    layoutCombo->setObjectName("ribbonCombo");
    layoutCombo->addItem("Title Slide",      static_cast<int>(SlideLayout::Title));
    layoutCombo->addItem("Title + Content",  static_cast<int>(SlideLayout::TitleContent));
    layoutCombo->addItem("Blank",            static_cast<int>(SlideLayout::Blank));
    layoutCombo->setFixedWidth(140);
    layoutCombo->setToolTip("Apply a slide layout");
    connect(layoutCombo, &QComboBox::activated, this, [this, layoutCombo](int idx) {
        emit layoutSelected(static_cast<SlideLayout>(layoutCombo->itemData(idx).toInt()));
    });
    layout->addWidget(makeGroup("Layout", { layoutCombo }));

    layout->addStretch();
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// Transitions tab
// ─────────────────────────────────────────────────────────────────────────────
QWidget* ImpressRibbon::buildTransitionsTab() {
    auto* tab = new QWidget(this);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2);
    layout->setSpacing(0);

    m_transitionGroup = new QButtonGroup(this);
    m_transitionGroup->setExclusive(true);
    struct TItem { const char* label; const char* glyph; SlideTransition t; };
    const TItem items[] = {
        { "None",     "—",            SlideTransition::None     },
        { "Fade",     "◐",            SlideTransition::Fade     },
        { "Push",     "⬅",            SlideTransition::Push     },
        { "Wipe",     "▧",            SlideTransition::Wipe     },
        { "Zoom",     "⌕",            SlideTransition::Zoom     },
        { "Cut",      "✂",            SlideTransition::Cut      },
        { "Cover",    "⧉",            SlideTransition::Cover    },
        { "Uncover",  "▤",            SlideTransition::Uncover  },
        { "Dissolve", "▒",            SlideTransition::Dissolve },
        { "Blinds",   "☰",            SlideTransition::Blinds   },
    };
    QList<QWidget*> tBtns;
    for (const auto& it : items) {
        auto* b = makeBigBtn(transitionIcon(it.t), it.label,
                             QString("%1 transition").arg(it.label), true);
        b->setMinimumWidth(56);
        m_transitionGroup->addButton(b, static_cast<int>(it.t));
        tBtns.append(b);
    }
    m_transitionGroup->button(static_cast<int>(SlideTransition::None))->setChecked(true);
    connect(m_transitionGroup, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
        if (checked) emit transitionSelected(static_cast<SlideTransition>(id));
    });
    layout->addWidget(makeGroup("Transition to This Slide", tBtns));
    layout->addWidget(makeSeparator());

    auto* btnAll = makeCmdBtn(QString::fromUtf8("⧉  Apply to All"), "Apply this transition to every slide", 120);
    connect(btnAll, &QToolButton::clicked, this, [this]() {
        const int id = m_transitionGroup->checkedId();
        if (id >= 0) emit transitionApplyAllRequested(static_cast<SlideTransition>(id));
    });
    layout->addWidget(makeGroup("Timing", { btnAll }));
    layout->addStretch();
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// Animations tab — whole-slide entrance animations. Clicking one applies it to
// the CURRENT slide (the slide animates in when it appears in the show). To
// animate a single object, right-click it on the canvas → "Animate".
// ─────────────────────────────────────────────────────────────────────────────
QWidget* ImpressRibbon::buildAnimationsTab() {
    auto* tab = new QWidget(this);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2);
    layout->setSpacing(0);

    struct SAItem { const char* label; const char* tip; SlideAnimation a; };
    const SAItem items[] = {
        { "None",       "No slide animation",            SlideAnimation::None },
        { "Fade\nIn",   "Whole slide fades in",          SlideAnimation::FadeIn },
        { "Zoom\nIn",   "Whole slide zooms in",          SlideAnimation::ZoomIn },
        { "Whirl\nIn",  "Whirlwind in (rotate + scale)", SlideAnimation::WhirlIn },
        { "Spiral",     "Spirals in",                    SlideAnimation::SpiralIn },
        { "Fly\nLeft",  "Slide flies in from the left",  SlideAnimation::FlyInLeft },
        { "Fly\nRight", "Slide flies in from the right", SlideAnimation::FlyInRight },
        { "Fly\nTop",   "Slide flies in from the top",   SlideAnimation::FlyInTop },
        { "Fly\nBottom","Slide flies in from the bottom",SlideAnimation::FlyInBottom },
        { "Bounce",     "Drops in with a bounce",        SlideAnimation::Bounce },
        { "Rise\nUp",   "Rises up while fading in",      SlideAnimation::RiseUp },
        { "Drop",       "Drops down into place",         SlideAnimation::Drop },
    };
    QList<QWidget*> btns;
    for (const auto& it : items) {
        auto* b = makeBigBtn(slideAnimIcon(it.a), it.label, it.tip);
        b->setMinimumWidth(56);
        const SlideAnimation a = it.a;
        connect(b, &QToolButton::clicked, this, [this, a]{ emit slideAnimationSelected(a); });
        btns.append(b);
    }
    layout->addWidget(makeGroup("Animate this slide", btns));
    layout->addStretch();
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// Slide Show tab
// ─────────────────────────────────────────────────────────────────────────────
QWidget* ImpressRibbon::buildSlideShowTab() {
    auto* tab = new QWidget(this);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2);
    layout->setSpacing(0);

    auto* btnFromStart   = makeCmdBtn("▶  From Beginning", "Start the slide show from slide 1", 140);
    auto* btnFromCurrent = makeCmdBtn("▶  From Current Slide", "Start the slide show from the active slide", 162);

    connect(btnFromStart,   &QToolButton::clicked, this, &ImpressRibbon::slideShowFromBeginningRequested);
    connect(btnFromCurrent, &QToolButton::clicked, this, &ImpressRibbon::slideShowFromCurrentRequested);

    layout->addWidget(makeGroup("Start Slide Show", { btnFromStart, btnFromCurrent }));
    layout->addWidget(makeSeparator());

    auto* btnPresenter = makeCmdBtn(QString::fromUtf8("🖥  Presenter View"),
                                    "Start the show with a presenter console (notes, timer, next slide)", 168);
    connect(btnPresenter, &QToolButton::clicked, this, &ImpressRibbon::presenterViewRequested);
    layout->addWidget(makeGroup("Presenter", { btnPresenter }));
    layout->addStretch();
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// Review tab
// ─────────────────────────────────────────────────────────────────────────────
QWidget* ImpressRibbon::buildReviewTab() {
    auto* tab = new QWidget(this);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2);
    layout->setSpacing(0);

    auto* btnNotes = makeBigBtn(textBoxIcon(), "Speaker\nNotes", "Show or hide the speaker notes panel");
    connect(btnNotes, &QToolButton::clicked, this, &ImpressRibbon::notesToggleRequested);
    layout->addWidget(makeGroup("Notes", { btnNotes }));
    layout->addWidget(makeSeparator());

    auto* btnComments = makeBigBtn(commentsIcon(), "Comments", "Show or hide review comments");
    connect(btnComments, &QToolButton::clicked, this, &ImpressRibbon::commentsToggleRequested);
    layout->addWidget(makeGroup("Comments", { btnComments }));
    layout->addStretch();
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// View tab
// ─────────────────────────────────────────────────────────────────────────────
QWidget* ImpressRibbon::buildViewTab() {
    auto* tab = new QWidget(this);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2);
    layout->setSpacing(0);

    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    auto* btnNormal  = makeCmdBtn("Normal",  "Normal editing view", 78, true);
    auto* btnOutline = makeCmdBtn("Outline", "Outline view", 78, true);
    auto* btnSorter  = makeCmdBtn("Sorter",  "Slide Sorter view", 78, true);
    btnNormal->setChecked(true);
    group->addButton(btnNormal,  static_cast<int>(ImpressViewMode::Normal));
    group->addButton(btnOutline, static_cast<int>(ImpressViewMode::Outline));
    group->addButton(btnSorter,  static_cast<int>(ImpressViewMode::SlideSorter));
    connect(group, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
        if (checked) emit viewModeChanged(static_cast<ImpressViewMode>(id));
    });

    layout->addWidget(makeGroup("Presentation Views", { btnNormal, btnOutline, btnSorter }));
    layout->addStretch();
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
QToolButton* ImpressRibbon::makeTabButton(const QString& label) {
    auto* btn = new QToolButton(this);
    btn->setText(label);
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setObjectName("ribbonTabBtn");
    return btn;
}

QToolButton* ImpressRibbon::makeToolBtn(const QString& text, const QString& tooltip, bool checkable) {
    auto* btn = new QToolButton(this);
    btn->setText(text);
    btn->setToolTip(tooltip);
    btn->setCheckable(checkable);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setObjectName("ribbonToolBtn");
    btn->setFixedSize(32, 32);
    return btn;
}

QToolButton* ImpressRibbon::makeIconBtn(const QIcon& icon, const QString& tooltip, bool checkable) {
    auto* btn = new QToolButton(this);
    btn->setIcon(icon);
    btn->setIconSize(QSize(20, 20));
    btn->setToolTip(tooltip);
    btn->setCheckable(checkable);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setObjectName("ribbonToolBtn");
    btn->setFixedSize(32, 32);
    return btn;
}

QToolButton* ImpressRibbon::makeCmdBtn(const QString& text, const QString& tooltip,
                                        int minWidth, bool checkable) {
    auto* btn = new QToolButton(this);
    btn->setText(text);
    btn->setToolTip(tooltip);
    btn->setCheckable(checkable);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setObjectName("ribbonCmdBtn");
    btn->setFixedHeight(32);
    btn->setMinimumWidth(minWidth);
    return btn;
}

QToolButton* ImpressRibbon::makeBigBtn(const QIcon& icon, const QString& text,
                                        const QString& tooltip, bool checkable) {
    auto* btn = new QToolButton(this);
    btn->setIcon(icon);
    btn->setIconSize(QSize(24, 24));
    btn->setText(text);
    btn->setToolTip(tooltip.isEmpty() ? text : tooltip);
    btn->setCheckable(checkable);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setObjectName("ribbonBigBtn");
    btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btn->setFixedHeight(62);
    btn->setMinimumWidth(54);
    return btn;
}

QWidget* ImpressRibbon::makeGroup(const QString& name, const QList<QWidget*>& widgets) {
    auto* group = new QWidget(this);
    auto* v = new QVBoxLayout(group);
    v->setContentsMargins(6, 0, 6, 0);
    v->setSpacing(2);

    auto* row = new QWidget(group);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(3);
    for (QWidget* w : widgets) {
        w->setParent(row);
        h->addWidget(w);
    }
    h->addStretch();

    auto* label = new QLabel(name, group);
    label->setObjectName("ribbonGroupLabel");
    label->setAlignment(Qt::AlignHCenter);

    v->addWidget(row);
    v->addWidget(label);
    return group;
}

QWidget* ImpressRibbon::makeSeparator() {
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedWidth(1);
    sep->setObjectName("ribbonSep");
    return sep;
}

void ImpressRibbon::resetInsertMode() {
    if (m_shapeGroup) {
        if (auto* checked = m_shapeGroup->checkedButton()) {
            m_shapeGroup->setExclusive(false);
            checked->setChecked(false);
            m_shapeGroup->setExclusive(true);
        }
    }
}

void ImpressRibbon::setUndoRedoEnabled(bool undoEnabled, bool redoEnabled) {
    // Undo/redo buttons were moved to the brand bar; keep this null-safe in case
    // anything still calls it.
    if (m_btnUndo) m_btnUndo->setEnabled(undoEnabled);
    if (m_btnRedo) m_btnRedo->setEnabled(redoEnabled);
}

void ImpressRibbon::syncCharFormat(bool bold, bool italic, bool underline, bool strike,
                                   const QString& family, int pointSize, const QColor& color) {
    m_syncing = true;
    m_btnBold->setChecked(bold);
    m_btnItalic->setChecked(italic);
    m_btnUnderline->setChecked(underline);
    m_btnStrike->setChecked(strike);
    if (!family.isEmpty()) {
        const int idx = m_fontCombo->findText(family);
        if (idx >= 0) m_fontCombo->setCurrentIndex(idx);
    }
    if (pointSize > 0) m_sizeCombo->setCurrentText(QString::number(pointSize));
    if (color.isValid()) m_textColor = color;
    m_syncing = false;
}

void ImpressRibbon::syncAlignment(Qt::Alignment align) {
    m_btnAlignLeft->setChecked(align.testFlag(Qt::AlignLeft));
    m_btnAlignCenter->setChecked(align.testFlag(Qt::AlignHCenter));
    m_btnAlignRight->setChecked(align.testFlag(Qt::AlignRight));
    m_btnAlignJustify->setChecked(align.testFlag(Qt::AlignJustify));
}

// ─────────────────────────────────────────────────────────────────────────────
// Styling
// ─────────────────────────────────────────────────────────────────────────────
void ImpressRibbon::applyStyles() {
    // The pure-CSS border-triangle for ::down-arrow renders inconsistently
    // across Qt styles (it showed as a solid square), so generate a real
    // arrow PNG once and reference it from the stylesheet.
    const QString arrowPath = QDir(QDir::tempPath()).filePath("nativeoffice_combo_arrow.png");
    {
        QPixmap pm(20, 12);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(90, 96, 110));
        p.setPen(Qt::NoPen);
        QPolygonF tri;
        tri << QPointF(5, 4) << QPointF(15, 4) << QPointF(10, 10);
        p.drawPolygon(tri);
        p.end();
        pm.save(arrowPath, "PNG");
    }

    setStyleSheet(QString(R"(
QWidget#impressRibbon {
    background-color: #FFFFFF;
    border-bottom: 1px solid #D7DAE0;
}
QWidget#ribbonTabRow {
    background-color: #F3F4F6;
    border-bottom: 1px solid #E2E4E9;
}
QToolButton#ribbonTabBtn {
    color: #5A6071;
    background: transparent;
    border: none;
    border-radius: 0;
    padding: 6px 16px;
    font-size: 12px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-weight: 500;
}
QToolButton#ribbonTabBtn:hover {
    color: #1C1E26;
    background: #E7E9EE;
}
QToolButton#ribbonTabBtn:checked {
    color: #1C1E26;
    background-color: #FFFFFF;
    border-bottom: 2px solid #E8372A;
    font-weight: 700;
}
QStackedWidget#ribbonStack {
    background-color: #FFFFFF;
}
QLabel#ribbonGroupLabel {
    color: #9097A3;
    font-size: 10px;
    font-family: "Segoe UI", "Inter", sans-serif;
}
QToolButton#ribbonToolBtn {
    color: #3A3F4B;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    font-size: 14px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-weight: 500;
}
QToolButton#ribbonToolBtn:hover {
    background: #ECEEF2;
    color: #1C1E26;
    border-color: #DCDFE6;
}
QToolButton#ribbonToolBtn:checked {
    background-color: #FCE4E2;
    color: #C0271C;
    border-color: #E8372A;
    font-weight: 700;
}
QToolButton#ribbonToolBtn:pressed {
    background-color: #F6D2CE;
}
QToolButton#ribbonToolBtn:disabled {
    color: #C2C6CE;
}
QToolButton#ribbonBigBtn {
    color: #3A3F4B;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 4px 4px;
    font-size: 11px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-weight: 500;
}
QToolButton#ribbonBigBtn:hover {
    background: #ECEEF2;
    color: #1C1E26;
    border-color: #DCDFE6;
}
QToolButton#ribbonBigBtn:checked {
    background-color: #FCE4E2;
    color: #C0271C;
    border-color: #E8372A;
    font-weight: 700;
}
QToolButton#ribbonBigBtn:pressed {
    background-color: #F6D2CE;
}
QToolButton#ribbonThemeBtn {
    background: #FFFFFF;
    border: 1px solid #D5D8DF;
    border-radius: 6px;
    padding: 2px;
}
QToolButton#ribbonThemeBtn:hover {
    border: 2px solid #E8372A;
}
QToolButton#ribbonCmdBtn {
    color: #2F3440;
    background: #F5F6FA;
    border: 1px solid #D5D8DF;
    border-radius: 6px;
    padding: 5px 12px;
    font-size: 13px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-weight: 500;
}
QToolButton#ribbonCmdBtn:hover {
    background: #ECEEF3;
    color: #1C1E26;
    border-color: #B9BEC9;
}
QToolButton#ribbonCmdBtn:checked {
    background-color: #FCE4E2;
    color: #C0271C;
    border-color: #E8372A;
    font-weight: 600;
}
QToolButton#ribbonCmdBtn:pressed {
    background-color: #F6D2CE;
}
QComboBox#ribbonCombo {
    background-color: #FFFFFF;
    color: #1C1E26;
    border: 1px solid #D5D8DF;
    border-radius: 6px;
    padding: 4px 6px;
    min-height: 22px;
    font-size: 12px;
    font-family: "Segoe UI", "Inter", sans-serif;
}
QComboBox#ribbonCombo:hover {
    border-color: #9CA3AF;
    background-color: #FAFBFC;
}
QComboBox#ribbonCombo:focus {
    border-color: #E8372A;
}
QComboBox#ribbonCombo::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: center right;
    border: none;
    width: 16px;
}
QComboBox#ribbonCombo::down-arrow {
    image: url("%1");
    width: 10px;
    height: 6px;
}
QComboBox QAbstractItemView {
    background-color: #FFFFFF;
    color: #1C1E26;
    border: 1px solid #D5D8DF;
    border-radius: 6px;
    selection-background-color: #E8372A;
    selection-color: #FFFFFF;
    outline: none;
    padding: 4px;
}
QComboBox QAbstractItemView::item {
    height: 26px;
    padding-left: 6px;
    border-radius: 4px;
}
QFrame#ribbonSep {
    background-color: #E2E4E9;
    border: none;
    margin: 6px 2px 18px 2px;
}
)").arg(arrowPath));
}

} // namespace NativeOffice
