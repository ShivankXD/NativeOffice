// ─────────────────────────────────────────────────────────────────────────────
// ShapeObject.cpp: see ShapeObject.h
// ─────────────────────────────────────────────────────────────────────────────
#include "ShapeObject.h"

#include <QAbstractTextDocumentLayout>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>
#include <QTextDocument>
#include <QtMath>

namespace NativeOffice {

namespace {

// Text that carries no colour of its own has to be readable against whatever
// the shape is filled with. Office resolves this through the shape style's
// font reference; picking by luminance lands in the same place for the cases
// that matter (white on an accent-coloured button, black on a pale note) and
// never produces the white-on-white a fixed default would.
QColor readableOn(const QColor& fill) {
    if (!fill.isValid()) return QColor(Qt::black);
    const double lum = (0.299 * fill.redF() + 0.587 * fill.greenF() + 0.114 * fill.blueF());
    return lum > 0.6 ? QColor(0x20, 0x20, 0x20) : QColor(Qt::white);
}

QString alignCss(Qt::Alignment a) {
    if (a & Qt::AlignHCenter) return QStringLiteral("center");
    if (a & Qt::AlignRight)   return QStringLiteral("right");
    if (a & Qt::AlignJustify) return QStringLiteral("justify");
    return QStringLiteral("left");
}

// The line a linear gradient runs along. DrawingML measures the angle
// clockwise from the +x axis, which is also the direction screen y grows in,
// so no sign flip is needed.
QLineF gradientLine(const QRectF& r, int angleDeg) {
    const double a  = qDegreesToRadians(double(angleDeg));
    const double dx = std::cos(a), dy = std::sin(a);
    const double len = std::abs(r.width() * dx) + std::abs(r.height() * dy);
    const QPointF c = r.center();
    return QLineF(c - QPointF(dx, dy) * (len / 2.0), c + QPointF(dx, dy) * (len / 2.0));
}

} // namespace

ShapeObject::ShapeObject(const SheetShape& shape, QWidget* parent)
    : QWidget(parent), m_shape(shape) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
    if (!m_shape.fillImage.isEmpty()) m_fillPixmap.loadFromData(m_shape.fillImage);
    rebuildText();
}

ShapeObject::~ShapeObject() {
    delete m_doc;
}

void ShapeObject::setZoom(double z) {
    if (z <= 0 || qFuzzyCompare(z, m_zoom)) return;
    m_zoom = z;
    rebuildText();
    update();
}

void ShapeObject::resizeEvent(QResizeEvent*) {
    if (m_doc) m_doc->setTextWidth(qMax(8.0, width() - 8.0));
}

// ── Text ────────────────────────────────────────────────────────────────────
void ShapeObject::rebuildText() {
    delete m_doc;
    m_doc = nullptr;
    if (m_shape.text.isEmpty()) return;

    const QColor fallback = readableOn(
        m_shape.fill.isValid() ? m_shape.fill
                               : (m_shape.gradient.isEmpty() ? QColor() : m_shape.gradient.first()));

    QString html = QStringLiteral("<body style=\"margin:0;padding:0\">");
    for (const ShapeParagraph& p : m_shape.text) {
        html += QStringLiteral("<p style=\"margin:0;padding:0;text-align:%1\">")
                    .arg(alignCss(p.align));
        for (const ShapeRun& run : p.runs) {
            const QColor c = run.color.isValid() ? run.color : fallback;
            QString style = QStringLiteral("color:%1;font-size:%2pt;")
                                .arg(c.name(), QString::number(qMax(4.0, run.size * m_zoom), 'f', 1));
            if (!run.family.isEmpty())
                style += QStringLiteral("font-family:'%1';").arg(run.family.toHtmlEscaped());
            if (run.bold)      style += QStringLiteral("font-weight:bold;");
            if (run.italic)    style += QStringLiteral("font-style:italic;");
            if (run.underline) style += QStringLiteral("text-decoration:underline;");

            QString text = run.text.toHtmlEscaped();
            text.replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
            html += QStringLiteral("<span style=\"%1\">%2</span>").arg(style, text);
        }
        html += QStringLiteral("</p>");
    }
    html += QStringLiteral("</body>");

    m_doc = new QTextDocument;
    m_doc->setDocumentMargin(0);
    m_doc->setHtml(html);
    m_doc->setTextWidth(qMax(8.0, width() - 8.0));
}

// ── Geometry ────────────────────────────────────────────────────────────────
QPainterPath ShapeObject::outline(const QRectF& r) const {
    QPainterPath path;
    const double w = r.width(), h = r.height();
    const double x = r.x(), y = r.y();

    switch (m_shape.preset) {
    case ShapeGeom::RoundRect: {
        // The adjust handle is the corner radius as a fraction of the SHORT
        // side, which is why a wide banner and a tall card with the same adj
        // still look like the same shape.
        const double rad = qBound(0.0, m_shape.adjustPct / 100000.0, 0.5) * qMin(w, h);
        path.addRoundedRect(r, rad, rad);
        break;
    }
    case ShapeGeom::Ellipse:
        path.addEllipse(r);
        break;
    case ShapeGeom::Triangle:
        path.moveTo(x + w / 2, y); path.lineTo(x + w, y + h); path.lineTo(x, y + h);
        path.closeSubpath();
        break;
    case ShapeGeom::RightTriangle:
        path.moveTo(x, y); path.lineTo(x + w, y + h); path.lineTo(x, y + h);
        path.closeSubpath();
        break;
    case ShapeGeom::Diamond:
        path.moveTo(x + w / 2, y);     path.lineTo(x + w, y + h / 2);
        path.lineTo(x + w / 2, y + h); path.lineTo(x, y + h / 2);
        path.closeSubpath();
        break;
    case ShapeGeom::Line:
        path.moveTo(x, y); path.lineTo(x + w, y + h);
        break;
    case ShapeGeom::RightArrow: {
        const double head = qMin(w * 0.4, h), sh = h * 0.25;
        path.moveTo(x, y + sh);              path.lineTo(x + w - head, y + sh);
        path.lineTo(x + w - head, y);        path.lineTo(x + w, y + h / 2);
        path.lineTo(x + w - head, y + h);    path.lineTo(x + w - head, y + h - sh);
        path.lineTo(x, y + h - sh);          path.closeSubpath();
        break;
    }
    case ShapeGeom::LeftArrow: {
        const double head = qMin(w * 0.4, h), sh = h * 0.25;
        path.moveTo(x + w, y + sh);          path.lineTo(x + head, y + sh);
        path.lineTo(x + head, y);            path.lineTo(x, y + h / 2);
        path.lineTo(x + head, y + h);        path.lineTo(x + head, y + h - sh);
        path.lineTo(x + w, y + h - sh);      path.closeSubpath();
        break;
    }
    case ShapeGeom::UpArrow: {
        const double head = qMin(h * 0.4, w), sh = w * 0.25;
        path.moveTo(x + sh, y + h);          path.lineTo(x + sh, y + head);
        path.lineTo(x, y + head);            path.lineTo(x + w / 2, y);
        path.lineTo(x + w, y + head);        path.lineTo(x + w - sh, y + head);
        path.lineTo(x + w - sh, y + h);      path.closeSubpath();
        break;
    }
    case ShapeGeom::DownArrow: {
        const double head = qMin(h * 0.4, w), sh = w * 0.25;
        path.moveTo(x + sh, y);              path.lineTo(x + sh, y + h - head);
        path.lineTo(x, y + h - head);        path.lineTo(x + w / 2, y + h);
        path.lineTo(x + w, y + h - head);    path.lineTo(x + w - sh, y + h - head);
        path.lineTo(x + w - sh, y);          path.closeSubpath();
        break;
    }
    case ShapeGeom::Chevron: {
        const double n = qMin(w * 0.3, h / 2);
        path.moveTo(x, y);                   path.lineTo(x + w - n, y);
        path.lineTo(x + w, y + h / 2);       path.lineTo(x + w - n, y + h);
        path.lineTo(x, y + h);               path.lineTo(x + n, y + h / 2);
        path.closeSubpath();
        break;
    }
    case ShapeGeom::Pentagon: {
        const double n = qMin(w * 0.3, h / 2);
        path.moveTo(x, y);                   path.lineTo(x + w - n, y);
        path.lineTo(x + w, y + h / 2);       path.lineTo(x + w - n, y + h);
        path.lineTo(x, y + h);               path.closeSubpath();
        break;
    }
    case ShapeGeom::Callout: {
        // Body plus a tail. The file gives the tail's target as adjust handles
        // this parser does not read, so it points down and left, which is where
        // Office puts it by default.
        const QRectF body(x, y, w, h * 0.78);
        const double rad = qMin(12.0, qMin(body.width(), body.height()) / 4.0);
        path.addRoundedRect(body, rad, rad);
        QPainterPath tail;
        tail.moveTo(x + w * 0.20, body.bottom() - 1);
        tail.lineTo(x + w * 0.10, y + h);
        tail.lineTo(x + w * 0.36, body.bottom() - 1);
        tail.closeSubpath();
        path = path.united(tail);
        break;
    }
    case ShapeGeom::Custom: {
        // The outline arrives in 0..1 coordinates, so it stretches to whatever
        // the anchor gives the shape, exactly like every preset here.
        QTransform t;
        t.translate(x, y);
        t.scale(w, h);
        path = t.map(m_shape.customPath);
        break;
    }
    case ShapeGeom::Rect:
    default:
        path.addRect(r);
        break;
    }
    return path;
}

// ── Paint ───────────────────────────────────────────────────────────────────
void ShapeObject::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const double inset = m_shape.line.isValid() ? m_shape.lineWidth / 2.0 : 0.0;
    QRectF r = QRectF(rect()).adjusted(inset, inset, -inset, -inset);
    if (r.width() < 1 || r.height() < 1) return;

    // A mirrored shape is drawn by mirroring the painter, not by rewriting
    // every geometry: an arrow flipped horizontally is still the same outline.
    if (m_shape.flipH || m_shape.flipV || m_shape.rotation) {
        p.translate(rect().center());
        if (m_shape.rotation) p.rotate(m_shape.rotation);
        p.scale(m_shape.flipH ? -1 : 1, m_shape.flipV ? -1 : 1);
        p.translate(-rect().center());
    }

    const QPainterPath path = outline(r);

    if (m_shape.preset != ShapeGeom::Line) {
        if (m_shape.gradient.size() >= 2) {
            QLinearGradient g(gradientLine(r, m_shape.gradientAngle).p1(),
                              gradientLine(r, m_shape.gradientAngle).p2());
            for (int i = 0; i < m_shape.gradient.size(); ++i) {
                const double pos = i < m_shape.gradientPos.size()
                                       ? m_shape.gradientPos.at(i)
                                       : double(i) / double(m_shape.gradient.size() - 1);
                g.setColorAt(qBound(0.0, pos, 1.0), m_shape.gradient.at(i));
            }
            p.fillPath(path, QBrush(g));
        } else if (m_shape.fill.isValid()) {
            p.fillPath(path, QBrush(m_shape.fill));
        }

        // A picture fill is stretched to the shape and clipped to its outline,
        // so a rounded frame really does round the image inside it.
        if (!m_fillPixmap.isNull()) {
            p.save();
            p.setClipPath(path);
            p.drawPixmap(r.toRect(), m_fillPixmap);
            p.restore();
        }
    }

    if (m_shape.line.isValid()) {
        QPen pen(m_shape.line, m_shape.lineWidth * qMax(0.6, m_zoom));
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    } else if (m_shape.preset == ShapeGeom::Line) {
        // A connector with no explicit outline still has to be a visible line.
        p.setPen(QPen(QColor(0x59, 0x5E, 0x66), qMax(1.0, m_zoom)));
        p.drawPath(path);
    }

    if (!m_doc) return;

    // The painter may be mirrored for the outline; text is never mirrored.
    p.resetTransform();
    const double pad = 4.0;
    m_doc->setTextWidth(qMax(8.0, width() - 2 * pad));
    const double th = m_doc->documentLayout()->documentSize().height();
    double ty = pad;
    if      (m_shape.vAlign & Qt::AlignVCenter) ty = (height() - th) / 2.0;
    else if (m_shape.vAlign & Qt::AlignBottom)  ty = height() - th - pad;
    ty = qMax(0.0, ty);

    p.translate(pad, ty);
    QAbstractTextDocumentLayout::PaintContext cx;
    cx.palette.setColor(QPalette::Text, QColor(Qt::black));
    cx.clip = QRectF(0, 0, m_doc->textWidth(), height());
    m_doc->documentLayout()->draw(&p, cx);
}

} // namespace NativeOffice
