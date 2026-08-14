#include "AiSlideLayout.h"

#include <QFont>
#include <QFontMetricsF>
#include <QJsonArray>

namespace NativeOffice {

namespace {

constexpr qreal M = 64.0;                 // page margin

// One palette held across the deck. A slide that invents its own colours looks
// generated; a deck that holds a single accent looks designed.
const QColor kInk    (0x11, 0x14, 0x1C);
const QColor kBody   (0x3C, 0x44, 0x57);
const QColor kMuted  (0x8A, 0x93, 0xA6);
const QColor kPaper  (0xFF, 0xFF, 0xFF);
const QColor kDeep   (0x14, 0x11, 0x2E);
const QColor kDeep2  (0x30, 0x22, 0x66);
const QColor kOnDeep (0xE9, 0xE6, 0xFA);

QColor readColour(const QJsonObject& o, const char* key, const QColor& fallback) {
    const QColor c(o.value(QLatin1String(key)).toString());
    return c.isValid() ? c : fallback;
}

// ── inline markdown ─────────────────────────────────────────────────────────

struct Run { QString text; bool bold{false}; bool italic{false}; bool code{false}; };

QVector<Run> runsOf(const QString& src) {
    QVector<Run> out;
    Run cur;
    bool b = false, i = false, c = false;
    for (int k = 0; k < src.size(); ) {
        const QChar ch = src.at(k);
        const bool dbl = (k + 1 < src.size()) && src.at(k + 1) == ch;
        if (!c && ch == QLatin1Char('*') && dbl) {
            if (!cur.text.isEmpty()) { out.append(cur); cur = Run{}; }
            b = !b; cur.bold = b; cur.italic = i; cur.code = c; k += 2; continue;
        }
        if (!c && (ch == QLatin1Char('*') || ch == QLatin1Char('_'))) {
            bool marker = true;
            if (ch == QLatin1Char('_')) {
                const QChar p = k > 0 ? src.at(k - 1) : QChar(' ');
                const QChar n = (k + 1 < src.size()) ? src.at(k + 1) : QChar(' ');
                marker = !(p.isLetterOrNumber() && n.isLetterOrNumber());
            }
            if (marker) {
                if (!cur.text.isEmpty()) { out.append(cur); cur = Run{}; }
                i = !i; cur.bold = b; cur.italic = i; cur.code = c; ++k; continue;
            }
        }
        if (ch == QLatin1Char('`')) {
            if (!cur.text.isEmpty()) { out.append(cur); cur = Run{}; }
            c = !c; cur.bold = b; cur.italic = i; cur.code = c; ++k; continue;
        }
        cur.text += ch;
        cur.bold = b; cur.italic = i; cur.code = c;
        ++k;
    }
    if (!cur.text.isEmpty()) out.append(cur);
    return out;
}

// A text box with measured, wrapped content. Returns the item and reports the
// height the text actually needs at this width and size.
SlideItem textItem(const QRectF& r, const QString& md, qreal pt, bool bold,
                   const QColor& ink, Qt::Alignment align = Qt::AlignLeft,
                   int vAlign = 0, qreal lineSpacing = 1.25) {
    SlideItem it;
    it.type      = SlideItemType::TextBox;
    it.rect      = r;
    it.text      = inlinePlain(md);
    it.fontSize  = pt;
    it.penColor  = ink;
    it.fillColor = Qt::transparent;
    it.penWidth  = 0;
    it.vAlign    = vAlign;

    const QString css = QStringLiteral(
        "margin:0; font-family:'Segoe UI'; font-size:%1px; color:%2;"
        " font-weight:%3; text-align:%4;")
        .arg(QString::number(pt), ink.name(),
             bold ? QStringLiteral("700") : QStringLiteral("400"),
             align.testFlag(Qt::AlignHCenter) ? QStringLiteral("center")
                                              : QStringLiteral("left"));
    Q_UNUSED(lineSpacing);
    it.html = QStringLiteral("<p style=\"%1\">%2</p>").arg(css, inlineHtml(md));
    return it;
}

SlideItem shape(const QRectF& r, const QColor& fill, ShapeKind kind = ShapeKind::Rectangle) {
    SlideItem it;
    it.type      = SlideItemType::Shape;
    it.shapeKind = kind;
    it.rect      = r;
    it.fillColor = fill;
    it.penColor  = fill;
    it.penWidth  = 0;
    return it;
}

// Height one block of text needs at a given width and point size. Qt measures
// in pixels and the canvas is authored in the same units the scene uses, so
// point size maps to pixel size directly here.
// The margin a QTextDocument keeps inside the width it is handed.
constexpr qreal kDocMargin = 10.0;

qreal textHeight(const QString& plain, qreal width, qreal pt, bool bold) {
    QFont f(QStringLiteral("Segoe UI"));
    f.setPixelSize(qMax<int>(1, qRound(pt)));
    f.setBold(bold);
    const QFontMetricsF fm(f);
    const QRectF br = fm.boundingRect(QRectF(0, 0, qMax<qreal>(8, width - kDocMargin), 10000),
                                      Qt::TextWordWrap, plain);
    // A little slack: QTextDocument lays out with its own leading, and being a
    // pixel or two generous costs nothing while being short causes a collision.
    return br.height() * 1.08 + 2;
}

// Steps the type down until every block fits the space available. Returning the
// size rather than clipping means a slide with one long bullet stays readable
// instead of losing its last line.
qreal fitSize(const QStringList& blocks, qreal width, qreal available,
              qreal startPt, qreal minPt, qreal gap) {
    for (qreal pt = startPt; pt >= minPt; pt -= 1.0) {
        qreal total = 0;
        for (const QString& b : blocks) total += textHeight(b, width, pt, false) + gap;
        if (total <= available) return pt;
    }
    return minPt;
}

} // namespace

QString inlineHtml(const QString& md) {
    QString out;
    for (const Run& r : runsOf(md)) {
        QString t = r.text.toHtmlEscaped();
        if (r.code)   t = QStringLiteral("<code>") + t + QStringLiteral("</code>");
        if (r.italic) t = QStringLiteral("<i>") + t + QStringLiteral("</i>");
        if (r.bold)   t = QStringLiteral("<b>") + t + QStringLiteral("</b>");
        out += t;
    }
    return out;
}

QString inlinePlain(const QString& md) {
    QString out;
    for (const Run& r : runsOf(md)) out += r.text;
    return out;
}

SlideData buildSlideFromOp(const QJsonObject& o, int* written) {
    const QString layout = o.value(QStringLiteral("layout")).toString();
    const QString title  = o.value(QStringLiteral("title")).toString();
    const QString sub    = o.value(QStringLiteral("subtitle")).toString();
    const QString body   = o.value(QStringLiteral("body")).toString();
    const QJsonArray bullets = o.value(QStringLiteral("bullets")).toArray();
    const QColor accent = readColour(o, "accent", QColor(0x7C, 0x5C, 0xFF));

    SlideData s;
    s.notes = o.value(QStringLiteral("notes")).toString();
    if (written) *written += title.size() + sub.size() + body.size() + s.notes.size();

    const bool dark = layout == QLatin1String("title")
                   || layout == QLatin1String("section")
                   || layout == QLatin1String("quote")
                   || layout == QLatin1String("metrics");

    if (dark) {
        s.background  = kDeep;
        s.background2 = kDeep2;
        s.layout      = SlideLayout::Title;
    } else {
        s.background = kPaper;
        s.layout     = SlideLayout::TitleContent;
    }
    const QColor headInk = dark ? kPaper : kInk;
    const QColor bodyInk = dark ? kOnDeep : kBody;

    // ── title ───────────────────────────────────────────────────────────────
    if (layout == QLatin1String("title")) {
        // A wide accent wash behind the lower third, so the opening slide has
        // some structure rather than being text floating in a void.
        s.items.push_back(shape(QRectF(0, kSlideH - 8, kSlideW, 8), accent));
        const qreal tw = kSlideW - M * 2;
        qreal pt = 54;
        while (pt > 30 && textHeight(inlinePlain(title), tw, pt, true) > 150) pt -= 2;
        const qreal th = textHeight(inlinePlain(title), tw, pt, true);
        const qreal sh = sub.isEmpty() ? 0 : textHeight(inlinePlain(sub), tw, 19, false);
        const qreal blockH = th + (sub.isEmpty() ? 0 : 30 + sh);
        qreal ty = (kSlideH - blockH) / 2 - 10;
        s.items.push_back(textItem(QRectF(M, ty, tw, th), title, pt, true, kPaper,
                                   Qt::AlignHCenter));
        ty += th + 12;
        s.items.push_back(shape(QRectF(kSlideW / 2 - 34, ty, 68, 3), accent));
        if (!sub.isEmpty())
            s.items.push_back(textItem(QRectF(M, ty + 18, tw, sh), sub, 19,
                                       false, kOnDeep, Qt::AlignHCenter));
        return s;
    }

    // ── section divider ─────────────────────────────────────────────────────
    if (layout == QLatin1String("section")) {
        const qreal tw = kSlideW - M * 2 - 28;
        const qreal th = textHeight(inlinePlain(title), tw, 38, true);
        const qreal sh = sub.isEmpty() ? 0 : textHeight(inlinePlain(sub), tw, 17, false);
        const qreal blockH = th + (sub.isEmpty() ? 0 : 14 + sh);
        const qreal ty = (kSlideH - blockH) / 2;
        s.items.push_back(shape(QRectF(M, ty, 7, blockH), accent));
        s.items.push_back(textItem(QRectF(M + 28, ty, tw, th), title, 38, true, kPaper));
        if (!sub.isEmpty())
            s.items.push_back(textItem(QRectF(M + 28, ty + th + 14, tw, sh), sub, 17,
                                       false, kOnDeep));
        return s;
    }

    // ── pull quote ──────────────────────────────────────────────────────────
    if (layout == QLatin1String("quote")) {
        const QString q = QStringLiteral("“") + (body.isEmpty() ? title : body)
                        + QStringLiteral("”");
        const qreal qw = kSlideW - M * 2 - 80;
        qreal pt = 32;
        while (pt > 16 && textHeight(inlinePlain(q), qw, pt, false) > 220) pt -= 2;
        const qreal qh = textHeight(inlinePlain(q), qw, pt, false);
        const qreal ah = sub.isEmpty() ? 0 : textHeight(sub, kSlideW - M * 2, 16, false);
        const qreal blockH = qh + (sub.isEmpty() ? 0 : 26 + ah);
        const qreal ty = (kSlideH - blockH) / 2;
        s.items.push_back(textItem(QRectF(M + 40, ty, qw, qh), q, pt, false, kPaper,
                                   Qt::AlignHCenter));
        if (!sub.isEmpty())
            s.items.push_back(textItem(QRectF(M, ty + qh + 26, kSlideW - M * 2, ah),
                                       QStringLiteral("— ") + sub, 16, false,
                                       kOnDeep, Qt::AlignHCenter));
        return s;
    }

    // ── metrics: a few big numbers ──────────────────────────────────────────
    // Given as bullets of "value | label". Big figures are what a company deck
    // actually opens a results section with, and they carry far better from the
    // back of a room than a sentence does.
    if (layout == QLatin1String("metrics")) {
        s.items.push_back(textItem(QRectF(M, 74, kSlideW - M * 2, 60), title, 30,
                                   true, kPaper));
        s.items.push_back(shape(QRectF(M, 140, 56, 4), accent));
        const int n = qMin(4, int(bullets.size()));
        if (n > 0) {
            const qreal gap = 22;
            const qreal cw = (kSlideW - M * 2 - gap * (n - 1)) / n;
            for (int i = 0; i < n; ++i) {
                const QString raw = bullets.at(i).toString();
                if (written) *written += raw.size();
                const QString value = raw.section(QLatin1Char('|'), 0, 0).trimmed();
                const QString label = raw.section(QLatin1Char('|'), 1).trimmed();
                const qreal x = M + i * (cw + gap);
                s.items.push_back(shape(QRectF(x, 196, cw, 150),
                                        QColor(255, 255, 255, 18)));
                s.items.push_back(shape(QRectF(x, 196, cw, 3), accent));
                // A figure that wraps is not a figure any more: "1,277 kg" broke
                // across two lines and the second ran through the label under
                // it. Shrink until it fits on one line, measuring the advance
                // rather than the wrapped height, which is the only measure
                // that can tell the difference.
                qreal vpt = 44;
                for (; vpt > 16; vpt -= 2) {
                    QFont vf(QStringLiteral("Segoe UI"));
                    vf.setPixelSize(qRound(vpt));
                    vf.setBold(true);
                    if (QFontMetricsF(vf).horizontalAdvance(value)
                        <= cw - 28 - kDocMargin) break;
                }
                const qreal vh = textHeight(value, cw - 28, vpt, true);
                s.items.push_back(textItem(QRectF(x + 14, 224, cw - 28, vh),
                                           value, vpt, true, kPaper, Qt::AlignHCenter));
                if (!label.isEmpty()) {
                    const qreal lh = textHeight(label, cw - 28, 13, false);
                    s.items.push_back(textItem(QRectF(x + 14, 224 + vh + 12, cw - 28, lh),
                                               label, 13, false, kOnDeep,
                                               Qt::AlignHCenter));
                }
            }
        }
        return s;
    }

    // ── chart ───────────────────────────────────────────────────────────────
    // A real chart item, not a picture of one: it is drawn by the same code
    // that draws a chart the user inserts, so it stays sharp and editable.
    if (layout == QLatin1String("chart")) {
        s.items.push_back(textItem(QRectF(M, 56, kSlideW - M * 2, 56), title, 28,
                                   true, kInk));
        s.items.push_back(shape(QRectF(M, 120, 56, 4), accent));

        SlideItem ch;
        ch.type = SlideItemType::Chart;
        const QString kind = o.value(QStringLiteral("chart")).toString().toLower();
        ch.chartKind = kind == QLatin1String("line") ? ChartKind::Line
                     : kind == QLatin1String("pie")  ? ChartKind::Pie
                                                     : ChartKind::Bar;
        ch.chartTitle = QString();
        for (const QJsonValue& v : o.value(QStringLiteral("labels")).toArray())
            ch.chartLabels.push_back(v.toString());
        for (const QJsonValue& v : o.value(QStringLiteral("values")).toArray())
            ch.chartValues.push_back(v.toDouble());
        ch.chartShowValues = true;
        ch.chartShowLegend = false;
        ch.fillColor = kPaper;
        ch.penColor  = QColor(0xD9, 0xDD, 0xE7);

        const bool hasBody = !body.isEmpty();
        ch.rect = hasBody ? QRectF(M, 156, kSlideW * 0.56, kSlideH - 156 - M)
                          : QRectF(M, 156, kSlideW - M * 2, kSlideH - 156 - M);
        s.items.push_back(ch);
        if (hasBody) {
            const qreal x = M + kSlideW * 0.56 + 28;
            s.items.push_back(textItem(QRectF(x, 168, kSlideW - x - M, 260),
                                       body, 16, false, kBody));
        }
        return s;
    }

    // ── content, the workhorse ──────────────────────────────────────────────
    s.items.push_back(textItem(QRectF(M, 56, kSlideW - M * 2, 60), title, 28,
                               true, kInk));
    s.items.push_back(shape(QRectF(M, 120, 56, 4), accent));

    const bool twoCol = layout == QLatin1String("twocolumn") && bullets.size() >= 4;
    qreal y = 152;
    const qreal fullW = kSlideW - M * 2;

    if (!body.isEmpty()) {
        const qreal h = textHeight(inlinePlain(body), fullW, 17, false);
        s.items.push_back(textItem(QRectF(M, y, fullW, h + 8), body, 17, false, kBody));
        y += h + 22;
    }

    // Every bullet is measured at its real width, and the type steps down until
    // the set fits. The previous fixed row height is exactly what made long
    // bullets overlap the ones beneath them.
    QStringList plain;
    for (const QJsonValue& v : bullets) plain << inlinePlain(v.toString());

    const int n = plain.size();
    if (n == 0) return s;

    const qreal available = kSlideH - y - M;
    const qreal gap = twoCol ? 14 : 16;
    const qreal colW = twoCol ? (fullW - 40) / 2 : fullW - 30;
    const int perCol = twoCol ? (n + 1) / 2 : n;

    QStringList firstCol = plain.mid(0, perCol);
    const qreal pt = fitSize(twoCol ? firstCol : plain, colW, available, 18, 12, gap);
    const qreal bulletDot = 7;

    qreal cy = y;
    qreal cx = M;
    for (int i = 0; i < n; ++i) {
        if (twoCol && i == perCol) { cy = y; cx = M + colW + 40; }
        if (written) *written += plain.at(i).size();
        const qreal h = textHeight(plain.at(i), colW - 22, pt, false);
        s.items.push_back(shape(QRectF(cx + 2, cy + pt * 0.42, bulletDot, bulletDot),
                                accent, ShapeKind::Ellipse));
        s.items.push_back(textItem(QRectF(cx + 22, cy, colW - 22, h + 6),
                                   bullets.at(i).toString(), pt, false, kBody));
        cy += h + gap;
    }
    return s;
}

} // namespace NativeOffice
