#include "AiSlideLayout.h"

#include <QFont>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QPair>
#include <QStringList>

namespace NativeOffice {

namespace {

constexpr qreal M = 64.0;            // page margin
constexpr qreal kDocMargin = 10.0;   // the margin a QTextDocument keeps inside
                                     // the width it is handed

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

// ── measurement ─────────────────────────────────────────────────────────────
// Qt measures in pixels and the canvas is authored in the same units the scene
// uses, so a size given here maps to a pixel size directly. Nothing sets a CSS
// line-height anywhere in this file: the measurement below assumes the default
// leading, and a stylesheet that changes it silently invalidates every height
// on the slide.

qreal textHeight(const QString& plain, qreal width, qreal pt, bool bold,
                 const QString& family) {
    QFont f(family);
    f.setPixelSize(qMax<int>(1, qRound(pt)));
    f.setBold(bold);
    const QFontMetricsF fm(f);
    const QRectF br = fm.boundingRect(QRectF(0, 0, qMax<qreal>(8, width - kDocMargin), 10000),
                                      Qt::TextWordWrap, plain);
    // A little slack: QTextDocument lays out with its own leading, and being a
    // pixel or two generous costs nothing while being short causes a collision.
    return br.height() * 1.08 + 2;
}

qreal textWidthOf(const QString& plain, qreal pt, bool bold, const QString& family) {
    QFont f(family);
    f.setPixelSize(qMax<int>(1, qRound(pt)));
    f.setBold(bold);
    return QFontMetricsF(f).horizontalAdvance(plain);
}

// The largest size at which `plain` still fits `maxHeight` at this width. Used
// for every headline: a long title shrinks rather than running off the slide or
// pushing the content below it out of the way.
qreal fitOne(const QString& plain, qreal width, qreal maxHeight, qreal startPt,
             qreal minPt, const QString& family, bool bold = true) {
    for (qreal pt = startPt; pt > minPt; pt -= 1.0)
        if (textHeight(plain, width, pt, bold, family) <= maxHeight) return pt;
    return minPt;
}

// The largest size at which every block fits the space available. Returning the
// size rather than clipping means a slide with one long bullet stays readable
// instead of losing its last line.
qreal fitAll(const QStringList& blocks, qreal width, qreal available,
             qreal startPt, qreal minPt, qreal gap, const QString& family) {
    for (qreal pt = startPt; pt >= minPt; pt -= 1.0) {
        qreal total = 0;
        for (const QString& b : blocks) total += textHeight(b, width, pt, false, family) + gap;
        if (total - gap <= available) return pt;
    }
    return minPt;
}

// ── item builders ───────────────────────────────────────────────────────────

SlideItem textItem(const QRectF& r, const QString& md, qreal pt, bool bold,
                   const QColor& ink, const QString& family,
                   Qt::Alignment align = Qt::AlignLeft, int vAlign = 0) {
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
        "margin:0; font-family:'%1'; font-size:%2px; color:%3;"
        " font-weight:%4; text-align:%5;")
        .arg(family, QString::number(pt), ink.name(),
             bold ? QStringLiteral("700") : QStringLiteral("400"),
             align.testFlag(Qt::AlignHCenter) ? QStringLiteral("center")
                                              : QStringLiteral("left"));
    it.html = QStringLiteral("<p style=\"%1\">%2</p>").arg(css, inlineHtml(md));
    return it;
}

SlideItem shape(const QRectF& r, const QColor& fill,
                ShapeKind kind = ShapeKind::Rectangle, qreal radiusPx = 0) {
    SlideItem it;
    it.type      = SlideItemType::Shape;
    it.shapeKind = kind;
    it.rect      = r;
    it.fillColor = fill;
    it.penColor  = fill;
    it.penWidth  = 0;
    if (kind == ShapeKind::RoundedRect) {
        // cornerAdj is a fraction of the shorter side, which is what the file
        // format stores. Working in pixels here keeps a small card and a wide
        // banner looking equally rounded instead of the banner turning into a
        // stadium.
        const qreal shorter = qMax<qreal>(1, qMin(r.width(), r.height()));
        it.cornerAdj = qBound<qreal>(0.0, radiusPx / shorter, 0.5);
    }
    return it;
}

// An outlined shape rather than a filled one. A large filled disc at low alpha
// reads as a smudge on the slide; the same disc as a hairline ring reads as a
// deliberate mark, which is the whole difference between decoration and dirt.
SlideItem ring(const QRectF& r, const QColor& stroke, qreal width) {
    SlideItem it;
    it.type      = SlideItemType::Shape;
    it.shapeKind = ShapeKind::Ellipse;
    it.rect      = r;
    it.fillColor = Qt::transparent;
    it.penColor  = stroke;
    it.penWidth  = width;
    return it;
}

// A sized stand-in for a photograph that has not arrived. It is drawn in the
// theme's own colours, so a deck generated without a network still looks like
// a deck with deliberate colour blocks in it rather than one full of holes.
// The concentric rings matter: a flat rectangle of tinted colour reads as a
// missing image, the same shape with a mark on it reads as a colour panel.
void placeholderInto(SlideData& s, const QRectF& r, const DeckTheme& t, bool onDark,
                     qreal radiusPx, bool locked) {
    const QColor fill = onDark ? mixed(t.deep2, t.accent, 0.20)
                               : mixed(t.paperTint, t.accent, 0.14);
    SlideItem ph = shape(r, fill,
                         radiusPx > 0.5 ? ShapeKind::RoundedRect : ShapeKind::Rectangle,
                         radiusPx);
    ph.locked = locked;
    s.items.push_back(ph);

    const qreal d = qMin(r.width(), r.height()) * 0.46;
    const QPointF c = r.center();
    SlideItem mark = ring(QRectF(c.x() - d / 2, c.y() - d / 2, d, d),
                          alpha(onDark ? t.paper : t.ink, 34), 2);
    mark.locked = locked;
    s.items.push_back(mark);
}

// Records a wanted picture and leaves a placeholder in its place. The panel
// becomes the photograph when it lands; the ring drawn over it has to be
// retired at the same moment, or it sits on top of the picture as a stray
// circle. Both indices travel with the request so the agent can do exactly
// that without knowing anything about how the slide was composed.
void wantImage(SlideData& s, QVector<SlideImageRequest>* reqs, const QString& query,
               const QRectF& r, const DeckTheme& t, bool onDark,
               ImageTreatment treatment, qreal radiusPx, bool locked) {
    const int fill = int(s.items.size());
    placeholderInto(s, r, t, onDark, radiusPx, locked);
    if (!reqs || query.trimmed().isEmpty()) return;
    SlideImageRequest rq;
    rq.query     = query.trimmed();
    rq.itemIndex = fill;
    rq.markIndex = fill + 1;
    rq.size      = r.size().toSize();
    rq.treatment = treatment;
    rq.radius    = radiusPx;
    reqs->push_back(rq);
}

// ── deck furniture ──────────────────────────────────────────────────────────

// The decoration a light slide carries. Every one of these stays outside the
// area the text is laid out in: the content column runs from x=64 to x=896 and
// from y=34 down, so anything drawn here lives in the margins or bleeds off the
// canvas. A motif that strays inside is not decoration, it is a stain under the
// text, which is exactly what the first attempt looked like.
void motif(SlideData& s, const DeckTheme& t, int ordinal) {
    switch (t.motif) {
    case DeckMotif::None:
        break;
    case DeckMotif::GradientWash:
        s.background2 = t.paperTint;
        break;
    case DeckMotif::CornerArc: {
        // Alternating corners, so consecutive slides are not identical. Both
        // are mostly off-canvas: only the sweep of the curve shows.
        const bool top = (ordinal % 2) == 0;
        const QRectF disc = top ? QRectF(kSlideW - 130, -280, 460, 460)
                                : QRectF(kSlideW - 110, kSlideH - 170, 420, 420);
        s.items.push_back(shape(disc, alpha(t.accent, 20), ShapeKind::Ellipse));
        s.items.push_back(ring(disc.adjusted(-24, -24, 24, 24),
                               alpha(t.accent2, 70), 2));
        break;
    }
    case DeckMotif::DiagonalBand: {
        // Raked bars in the right-hand margin. Rotation is about the centre of
        // the bar, so a tall bar at a steep angle throws its ends a long way
        // sideways: at 15 degrees over 820 pixels the first version reached
        // x=814, which is eighty pixels inside the content column. Eight
        // degrees over 620 keeps every part of every bar past x=873, and the
        // widest text box on any slide stops at 866.
        for (int i = 0; i < 3; ++i) {
            SlideItem band = shape(QRectF(kSlideW - 44 + i * 15, -40, 6, 620),
                                   alpha(i == 1 ? t.accent2 : t.accent, 95));
            band.rotation = 8;
            s.items.push_back(band);
        }
        break;
    }
    case DeckMotif::DotGrid: {
        // A narrow column of dots outside the text margin on the right edge.
        // Anything wider than this runs under the content.
        for (int col = 0; col < 2; ++col)
            for (int row = 0; row < 13; ++row)
                s.items.push_back(shape(
                    QRectF(kSlideW - 44 + col * 18, 120 + row * 20, 5, 5),
                    alpha(t.accent, 70), ShapeKind::Ellipse));
        break;
    }
    case DeckMotif::SideRule:
        s.items.push_back(shape(QRectF(0, 0, 34, kSlideH), mixed(t.paper, t.accent, 0.13)));
        s.items.push_back(shape(QRectF(34, 0, 3, 186), t.accent));
        break;
    case DeckMotif::Frame:
        s.items.push_back(shape(QRectF(M, 34, kSlideW - M * 2, 1),
                                mixed(t.paper, t.ink, 0.22)));
        s.items.push_back(shape(QRectF(M, kSlideH - 34, kSlideW - M * 2, 1),
                                mixed(t.paper, t.ink, 0.22)));
        s.items.push_back(shape(QRectF(M, 34, 40, 3), t.accent));
        break;
    }
}

// The same idea on a dark slide: concentric rings bleeding off one corner,
// moved around by the slide's position so a run of dark slides is not a run of
// identical ones. Rings rather than filled discs, for the reason above: a
// filled ellipse at low alpha on near-black looks like a compression artefact.
void darkMotif(SlideData& s, const DeckTheme& t, int ordinal) {
    static const QRectF spots[3] = {
        QRectF(kSlideW - 250, -300, 640, 640),
        QRectF(-320, kSlideH - 280, 600, 600),
        QRectF(kSlideW - 380, kSlideH - 220, 660, 660),
    };
    const QRectF r = spots[qAbs(ordinal) % 3];
    // Outlines only. The filled disc underneath these, even at alpha 16, came
    // out as a brown wash across the lower corner that read as a smudge on the
    // photograph rather than as part of the design.
    s.items.push_back(ring(r, alpha(t.accent, 72), 2));
    s.items.push_back(ring(r.adjusted(78, 78, -78, -78), alpha(t.accent2, 52), 1.5));
}

void setDark(SlideData& s, const DeckTheme& t) {
    s.background  = t.deep;
    s.background2 = t.deep2;
    s.layout      = SlideLayout::Title;
}

void setLight(SlideData& s, const DeckTheme& t) {
    s.background  = t.paper;
    s.background2 = QColor();
    s.layout      = SlideLayout::TitleContent;
}

// The surface a slide is laid on, and the ink that goes with it.
//
// Every ordinary slide used to be the same pale card, so a deck came out as one
// dark title, one dark section break and eight identical light slides in a row.
// A deck that never changes surface reads as a template with the words swapped
// out, which is the loudest single thing about a generated one. Every fourth
// content slide is now laid on the deep surface instead, which changes the
// rhythm of the whole deck without any slide having to ask for it.
struct Surface {
    bool   dark { false };
    QColor bg1, bg2, head, body, muted, panel, panelEdge;
};

Surface surfaceFor(const DeckTheme& t, int ordinal) {
    Surface su;
    su.dark = ordinal > 0 && (ordinal % 4) == 0;
    if (su.dark) {
        su.bg1 = t.deep;   su.bg2 = t.deep2;
        su.head = t.paper; su.body = t.onDeep;
        su.muted = mixed(t.onDeep, t.deep, 0.42);
        su.panel = alpha(t.paper, 18);
        su.panelEdge = alpha(t.paper, 38);
    } else {
        su.bg1 = t.paper;  su.bg2 = QColor();
        su.head = t.ink;   su.body = t.body;
        su.muted = t.muted;
        su.panel = t.paperTint;
        su.panelEdge = mixed(t.paper, t.ink, 0.12);
    }
    return su;
}

// Lays the surface down and puts the decoration that belongs to it on top.
void applySurface(SlideData& s, const DeckTheme& t, const Surface& su, int ordinal) {
    s.background  = su.bg1;
    s.background2 = su.bg2;
    s.layout      = su.dark ? SlideLayout::Title : SlideLayout::TitleContent;
    if (su.dark) darkMotif(s, t, ordinal);
    else         motif(s, t, ordinal);
}

// A small capitalised label. Every professionally set deck has one of these
// over its headings; it is the cheapest single thing that separates a slide
// from a word processor page.
qreal kicker(SlideData& s, const DeckTheme& t, const QString& text, qreal x,
             qreal y, qreal w, const QColor& ink, Qt::Alignment align) {
    if (text.trimmed().isEmpty()) return y;
    const QString k = t.eyebrowUpper ? text.toUpper() : text;
    const qreal h = textHeight(k, w, 12, true, t.bodyFont);
    s.items.push_back(textItem(QRectF(x, y, w, h), k, 12, true, ink, t.bodyFont, align));
    return y + h + 9;
}

// The opening slide's label, set in a solid accent pill rather than as loose
// coloured type. Over a photograph, small accent text lands on whatever the
// picture happens to have there and half the time cannot be read at all; the
// first render put "ENGINEERING BRIEF" across a number plate. A filled pill
// carries its own background and is legible over anything.
qreal pillKicker(SlideData& s, const DeckTheme& t, const QString& text, qreal y) {
    if (text.trimmed().isEmpty()) return y;
    const QString k = t.eyebrowUpper ? text.toUpper() : text;
    const qreal tw = textWidthOf(k, 13, true, t.bodyFont);
    const qreal pw = tw + 36;
    const qreal ph = 28;
    const qreal x  = (kSlideW - pw) / 2;
    s.items.push_back(shape(QRectF(x, y, pw, ph), t.accent, ShapeKind::RoundedRect,
                            ph / 2));
    s.items.push_back(textItem(QRectF(x, y, pw, ph), k, 13, true, t.paper, t.bodyFont,
                               Qt::AlignHCenter, 1));
    return y + ph;
}

// Heading block for an ordinary slide: optional kicker, the title at whatever
// size it fits in two lines, and a short accent rule under it. Returns the y
// the content below should start at.
// Three treatments, chosen by where the slide sits in the deck. One heading
// style repeated for ten slides is most of what makes a deck feel like a form;
// rotating between a ruled heading, a barred one and a labelled one costs
// nothing and gives the deck a pulse. The variant is picked from the ordinal so
// the same deck always comes out the same way.
qreal header(SlideData& s, const DeckTheme& t, const Surface& su,
             const QString& kickerText, const QString& title, int ordinal) {
    const int variant = qAbs(ordinal) % 3;
    const qreal tw = kSlideW - M * 2 - 46;
    const QString plain = inlinePlain(title);

    // Two lines at the theme's heading size, and it shrinks rather than taking
    // a third.
    const qreal cap = textHeight(QStringLiteral("Hg"), 4000, t.headPt, true,
                                 t.headFont) * 2.15;

    if (variant == 1) {
        // A full-height accent bar beside the heading block.
        const qreal x = M + 20;
        const qreal w = tw - 20;
        qreal y = 52;
        y = kicker(s, t, kickerText, x, y, w, t.accent, Qt::AlignLeft);
        const qreal top = y;
        if (!plain.isEmpty()) {
            const qreal pt = fitOne(plain, w, cap, t.headPt, 17, t.headFont);
            const qreal th = textHeight(plain, w, pt, true, t.headFont);
            s.items.push_back(textItem(QRectF(x, y, w, th), title, pt, true,
                                       su.head, t.headFont));
            y += th;
        }
        s.items.push_back(shape(QRectF(M, 52, 5, qMax<qreal>(28, y - 52)), t.accent));
        Q_UNUSED(top);
        return y + 26;
    }

    if (variant == 2) {
        // The label in a solid pill, and no rule under the heading.
        qreal y = 50;
        if (!kickerText.trimmed().isEmpty()) {
            const QString k = t.eyebrowUpper ? kickerText.toUpper() : kickerText;
            const qreal kw = textWidthOf(k, 11.5, true, t.bodyFont) + 26;
            s.items.push_back(shape(QRectF(M, y, kw, 22), t.accent,
                                    ShapeKind::RoundedRect, 11));
            s.items.push_back(textItem(QRectF(M, y, kw, 22), k, 11.5, true,
                                       t.paper, t.bodyFont, Qt::AlignHCenter, 1));
            y += 22 + 12;
        }
        if (!plain.isEmpty()) {
            const qreal pt = fitOne(plain, tw, cap, t.headPt, 17, t.headFont);
            const qreal th = textHeight(plain, tw, pt, true, t.headFont);
            s.items.push_back(textItem(QRectF(M, y, tw, th), title, pt, true,
                                       su.head, t.headFont));
            y += th;
        }
        return y + 28;
    }

    qreal y = 52;
    y = kicker(s, t, kickerText, M, y, tw, t.accent, Qt::AlignLeft);
    if (!plain.isEmpty()) {
        const qreal pt = fitOne(plain, tw, cap, t.headPt, 17, t.headFont);
        const qreal th = textHeight(plain, tw, pt, true, t.headFont);
        s.items.push_back(textItem(QRectF(M, y, tw, th), title, pt, true,
                                   su.head, t.headFont));
        y += th + 13;
    }
    s.items.push_back(shape(QRectF(M, y, 54, 4), t.accent));
    return y + 4 + 22;
}

// ── bullets ─────────────────────────────────────────────────────────────────

// A hard theme uses a square marker and a soft one a disc. It is a detail
// nobody names and everybody notices when it is wrong.
void bulletMarker(SlideData& s, const DeckTheme& t, qreal x, qreal y, qreal pt) {
    const qreal d = qMax<qreal>(6, pt * 0.38);
    const QRectF r(x, y + pt * 0.40, d, d);
    if (t.radius < 6) s.items.push_back(shape(r, t.accent));
    else              s.items.push_back(shape(r, t.accent, ShapeKind::Ellipse));
}

// What a bullet list will occupy, before anything is placed. Needed by every
// layout that centres its content: you cannot centre a block whose height you
// only discover by drawing it.
qreal bulletsHeight(const QStringList& items, qreal w, qreal pt, qreal gap,
                    const DeckTheme& t) {
    const qreal indent = qMax<qreal>(20, pt * 1.25);
    qreal h = 0;
    for (const QString& raw : items)
        h += textHeight(inlinePlain(raw), w - indent, pt, false, t.bodyFont) + gap;
    return h > 0 ? h - gap : 0;
}

// Lays a measured bullet list into a column and returns the height it used.
qreal bulletColumn(SlideData& s, const DeckTheme& t, const QStringList& items,
                   qreal x, qreal y, qreal w, qreal pt, qreal gap,
                   const QColor& ink) {
    const qreal indent = qMax<qreal>(20, pt * 1.25);
    qreal cy = y;
    for (const QString& raw : items) {
        const qreal h = textHeight(inlinePlain(raw), w - indent, pt, false, t.bodyFont);
        bulletMarker(s, t, x + 2, cy, pt);
        s.items.push_back(textItem(QRectF(x + indent, cy, w - indent, h + 6),
                                   raw, pt, false, ink, t.bodyFont));
        cy += h + gap;
    }
    return cy - y;
}

// ── field access ────────────────────────────────────────────────────────────

QString str(const QJsonObject& o, const char* key) {
    return o.value(QLatin1String(key)).toString();
}

QStringList list(const QJsonObject& o, const char* key, int* written) {
    QStringList out;
    for (const QJsonValue& v : o.value(QLatin1String(key)).toArray()) {
        const QString s = v.toString();
        if (s.trimmed().isEmpty()) continue;
        if (written) *written += s.size();
        out << s;
    }
    return out;
}

// "3.2s | 0 to 60 mph" split into its two halves. Used by every layout that
// takes a label and a value in one string, which is most of the new ones: one
// flat list of strings is far more reliable to get out of a model than a list
// of objects with two keys each.
QPair<QString, QString> pairOf(const QString& raw) {
    const int bar = raw.indexOf(QLatin1Char('|'));
    if (bar < 0) return { raw.trimmed(), QString() };
    return { raw.left(bar).trimmed(), raw.mid(bar + 1).trimmed() };
}

} // namespace

// ── public helpers ──────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────

SlideData buildSlideFromOp(const QJsonObject& o, const SlideBuildContext& ctx,
                           int* written, QVector<SlideImageRequest>* reqs) {
    const DeckTheme& t = ctx.theme;

    const QString layout = str(o, "layout");
    const QString title  = str(o, "title");
    const QString sub    = str(o, "subtitle");
    const QString body   = str(o, "body");
    const QString kick   = str(o, "kicker");
    const QString image  = str(o, "image");

    SlideData s;
    s.title = inlinePlain(title).isEmpty() ? QStringLiteral("Slide") : inlinePlain(title);
    s.notes = str(o, "notes");
    if (written) *written += title.size() + sub.size() + body.size() + s.notes.size();

    const QStringList bullets = list(o, "bullets", written);

    // ── opening slide ───────────────────────────────────────────────────────
    if (layout == QLatin1String("title")) {
        setDark(s, t);
        const bool hasImage = !image.isEmpty();
        if (hasImage)
            wantImage(s, reqs, image, QRectF(0, 0, kSlideW, kSlideH), t, true,
                      ImageTreatment::ScrimFull, 0, true);
        else
            darkMotif(s, t, 0);

        const qreal tw = kSlideW - M * 2 - 40;
        const QString plain = inlinePlain(title);
        const qreal pt = fitOne(plain, tw, 190, t.titlePt, 28, t.headFont);
        const qreal th = textHeight(plain, tw, pt, true, t.headFont);
        const qreal sh = sub.isEmpty() ? 0 : textHeight(inlinePlain(sub), tw, 19, false, t.bodyFont);
        const qreal kh = kick.isEmpty() ? 0 : 28 + 24;

        const qreal blockH = kh + th + 14 + 3 + (sub.isEmpty() ? 0 : 18 + sh);
        qreal y = (kSlideH - blockH) / 2 - 8;

        if (!kick.isEmpty()) y = pillKicker(s, t, kick, y) + 24;
        s.items.push_back(textItem(QRectF(M + 20, y, tw, th), title, pt, true,
                                   t.paper, t.headFont, Qt::AlignHCenter));
        y += th + 14;
        s.items.push_back(shape(QRectF(kSlideW / 2 - 34, y, 68, 3), t.accent));
        if (!sub.isEmpty())
            s.items.push_back(textItem(QRectF(M + 20, y + 18, tw, sh), sub, 19, false,
                                       t.onDeep, t.bodyFont, Qt::AlignHCenter));

        // A hard rule along the foot, so the opening slide is anchored rather
        // than being text floating in a void.
        s.items.push_back(shape(QRectF(0, kSlideH - 7, kSlideW * 0.62, 7), t.accent));
        s.items.push_back(shape(QRectF(kSlideW * 0.62, kSlideH - 7,
                                       kSlideW * 0.38, 7), t.accent2));
        return s;
    }

    // ── section divider ─────────────────────────────────────────────────────
    if (layout == QLatin1String("section")) {
        setDark(s, t);
        if (!image.isEmpty())
            wantImage(s, reqs, image, QRectF(0, 0, kSlideW, kSlideH), t, true,
                      ImageTreatment::Duotone, 0, true);
        else
            darkMotif(s, t, ctx.ordinal);

        // The part number, set very large and very quiet behind the heading.
        // It lives entirely to the right of where the title can reach: at the
        // first attempt the two overlapped and the numeral read as a smear
        // across the last word.
        const QString num = QStringLiteral("%1").arg(qMax(1, ctx.ordinal), 2, 10,
                                                     QLatin1Char('0'));
        s.items.push_back(textItem(QRectF(kSlideW - 260, kSlideH / 2 - 128, 220, 230),
                                   num, 150, true, alpha(t.paper, 34), t.headFont,
                                   Qt::AlignHCenter));

        const qreal tw = kSlideW * 0.55;
        const QString plain = inlinePlain(title);
        const qreal pt = fitOne(plain, tw, 150, qMax<qreal>(34, t.headPt + 10), 22,
                                t.headFont);
        const qreal th = textHeight(plain, tw, pt, true, t.headFont);
        const qreal sh = sub.isEmpty() ? 0 : textHeight(inlinePlain(sub), tw, 17, false,
                                                        t.bodyFont);
        const qreal blockH = th + (sub.isEmpty() ? 0 : 14 + sh);
        const qreal y = (kSlideH - blockH) / 2;
        s.items.push_back(shape(QRectF(M, y, 7, blockH), t.accent));
        s.items.push_back(textItem(QRectF(M + 28, y, tw, th), title, pt, true,
                                   t.paper, t.headFont));
        if (!sub.isEmpty())
            s.items.push_back(textItem(QRectF(M + 28, y + th + 14, tw, sh), sub, 17,
                                       false, t.onDeep, t.bodyFont));
        return s;
    }

    // ── full-bleed picture with the headline over it ────────────────────────
    if (layout == QLatin1String("imagefull")) {
        setDark(s, t);
        wantImage(s, reqs, image.isEmpty() ? inlinePlain(title) : image,
                  QRectF(0, 0, kSlideW, kSlideH), t, true,
                  ImageTreatment::ScrimBottom, 0, true);

        const qreal tw = kSlideW * 0.68;
        const QString plain = inlinePlain(title);
        const qreal pt = fitOne(plain, tw, 132, qMax<qreal>(38, t.headPt + 12), 24,
                                t.headFont);
        const qreal th = textHeight(plain, tw, pt, true, t.headFont);
        const qreal sh = sub.isEmpty() ? 0 : textHeight(inlinePlain(sub), tw, 17,
                                                        false, t.bodyFont);
        const qreal blockH = th + (sub.isEmpty() ? 0 : 12 + sh);
        qreal y = kSlideH - M - blockH;

        // The rule sits above the kicker, not between the kicker and the title.
        // Placing it at a fixed offset above the title is what put it straight
        // through the middle of the kicker on every slide that had one.
        if (!kick.isEmpty()) {
            const qreal kh = textHeight(kick.toUpper(), tw, 12, true, t.bodyFont) + 9;
            y -= kh;
            s.items.push_back(shape(QRectF(M, y - 22, 54, 4), t.accent));
            kicker(s, t, kick, M, y, tw, t.accent, Qt::AlignLeft);
            y += kh;
        } else {
            s.items.push_back(shape(QRectF(M, y - 22, 54, 4), t.accent));
        }
        s.items.push_back(textItem(QRectF(M, y, tw, th), title, pt, true, t.paper,
                                   t.headFont));
        if (!sub.isEmpty())
            s.items.push_back(textItem(QRectF(M, y + th + 12, tw, sh), sub, 17, false,
                                       t.onDeep, t.bodyFont));
        return s;
    }

    // ── half picture, half argument ─────────────────────────────────────────
    if (layout == QLatin1String("imageleft") || layout == QLatin1String("imageright")) {
        setLight(s, t);
        const bool left = layout == QLatin1String("imageleft");
        const qreal iw = kSlideW * 0.44;
        const QRectF ir = left ? QRectF(0, 0, iw, kSlideH)
                               : QRectF(kSlideW - iw, 0, iw, kSlideH);
        // The picture bleeds off two edges. A photograph inset on all four
        // sides reads as clip art; one that runs off the slide reads as design.
        wantImage(s, reqs, image.isEmpty() ? inlinePlain(title) : image, ir, t, false,
                  ImageTreatment::Plain, 0, true);

        const qreal tx = left ? iw + 52 : M;
        const qreal tw = kSlideW - iw - 52 - M;
        const qreal top = 62;
        const qreal avail = kSlideH - 62 - top;

        // The column is measured whole and then centred against the picture.
        // Starting it at a fixed height left short slides with the text pinned
        // to the top and a third of the panel empty under it, which is what a
        // slide looks like when nobody laid it out.
        const QString plain = inlinePlain(title);
        const qreal kh = kick.isEmpty() ? 0
                       : textHeight(kick.toUpper(), tw, 12, true, t.bodyFont) + 9;
        const qreal pt = fitOne(plain, tw, 118, t.headPt, 18, t.headFont);
        const qreal th = textHeight(plain, tw, pt, true, t.headFont);
        const qreal bh = body.isEmpty() ? 0
                       : textHeight(inlinePlain(body), tw, 16, false, t.bodyFont) + 18;

        QStringList plainBullets;
        for (const QString& b : bullets) plainBullets << inlinePlain(b);
        const qreal fixed = kh + th + 13 + 4 + 26 + bh;
        const qreal bpt = bullets.isEmpty() ? 0
                        : fitAll(plainBullets, tw - 24, avail - fixed, 16, 11, 13,
                                 t.bodyFont);
        const qreal listH = bullets.isEmpty() ? 0
                          : bulletsHeight(bullets, tw, bpt, 13, t);

        qreal y = top + qMax<qreal>(0, (avail - (fixed + listH)) / 2);
        y = kicker(s, t, kick, tx, y, tw, t.accent, Qt::AlignLeft);
        s.items.push_back(textItem(QRectF(tx, y, tw, th), title, pt, true, t.ink,
                                   t.headFont));
        y += th + 13;
        s.items.push_back(shape(QRectF(tx, y, 54, 4), t.accent));
        y += 26 + 4;

        if (!body.isEmpty()) {
            const qreal h = textHeight(inlinePlain(body), tw, 16, false, t.bodyFont);
            s.items.push_back(textItem(QRectF(tx, y, tw, h + 6), body, 16, false,
                                       t.body, t.bodyFont));
            y += h + 18;
        }
        if (!bullets.isEmpty())
            bulletColumn(s, t, bullets, tx, y, tw, bpt, 13, t.body);
        return s;
    }

    // ── a row of pictures ───────────────────────────────────────────────────
    if (layout == QLatin1String("imagegrid")) {
        const Surface su = surfaceFor(t, ctx.ordinal);
        applySurface(s, t, su, ctx.ordinal);
        const qreal top = header(s, t, su, kick, title, ctx.ordinal);

        QStringList queries = list(o, "images", nullptr);
        if (queries.isEmpty()) for (const QString& b : bullets) queries << pairOf(b).first;
        // qMin, never qBound: clamping an empty list up to a minimum of two is
        // how a layout ends up indexing entries that are not there.
        const int n = qMin(4, int(queries.size()));
        if (n < 1) return s;

        const qreal gap = 20;
        const qreal fullW = kSlideW - M * 2;
        const qreal cw = (fullW - gap * (n - 1)) / n;
        // Captions are measured first, because the pictures get whatever is
        // left. Sizing the pictures first and then discovering the captions do
        // not fit is how a caption ends up half off the bottom of the slide.
        qreal capH = 0;
        for (int i = 0; i < n && i < bullets.size(); ++i)
            capH = qMax(capH, textHeight(inlinePlain(pairOf(bullets.at(i)).second.isEmpty()
                                                     ? bullets.at(i)
                                                     : pairOf(bullets.at(i)).second),
                                         cw, 13, false, t.bodyFont));
        const qreal ih = qMax<qreal>(120, kSlideH - M - top - (capH > 0 ? capH + 14 : 0));

        for (int i = 0; i < n; ++i) {
            const qreal x = M + i * (cw + gap);
            wantImage(s, reqs, queries.at(i), QRectF(x, top, cw, ih), t, su.dark,
                      ImageTreatment::Plain, t.radius, false);
            if (i < bullets.size()) {
                const QPair<QString, QString> p = pairOf(bullets.at(i));
                const QString cap = p.second.isEmpty() ? p.first : p.second;
                const qreal h = textHeight(inlinePlain(cap), cw, 13, false, t.bodyFont);
                s.items.push_back(textItem(QRectF(x, top + ih + 12, cw, h + 4), cap,
                                           13, false, su.muted, t.bodyFont));
            }
        }
        return s;
    }

    // ── cards ───────────────────────────────────────────────────────────────
    if (layout == QLatin1String("cards")) {
        const Surface su = surfaceFor(t, ctx.ordinal);
        applySurface(s, t, su, ctx.ordinal);
        const qreal top = header(s, t, su, kick, title, ctx.ordinal);

        const int n = qMin(4, int(bullets.size()));
        if (n < 1) return s;
        const qreal gap = 20;
        const qreal fullW = kSlideW - M * 2;
        const qreal cw = (fullW - gap * (n - 1)) / n;
        const qreal avail = kSlideH - M - top;
        const qreal pad = qMin<qreal>(22, cw * 0.12);

        // One size for every card, chosen so the wordiest one fits. Cards that
        // each pick their own size look like four different slides.
        qreal headPt = n >= 4 ? 16 : 18;
        qreal textPt = n >= 4 ? 12.5 : 14;
        qreal worst = 0;
        for (; textPt > 9.5; textPt -= 0.5) {
            worst = 0;
            for (int i = 0; i < n; ++i) {
                const QPair<QString, QString> p = pairOf(bullets.at(i));
                qreal need = textHeight(inlinePlain(p.first), cw - pad * 2, headPt,
                                        true, t.headFont) + 10;
                if (!p.second.isEmpty())
                    need += textHeight(inlinePlain(p.second), cw - pad * 2, textPt,
                                       false, t.bodyFont);
                worst = qMax(worst, need);
            }
            if (worst <= avail - pad * 2 - 12) break;
        }

        // The card is as tall as its content needs, with a floor so a row of
        // short cards still reads as a row of cards rather than a strip of
        // labels, and the row is then centred in what is left. Stretching every
        // card to the bottom margin is what produced four columns of empty grey
        // with a sentence at the top of each.
        const qreal ch = qBound<qreal>(qMin<qreal>(avail * 0.66, avail),
                                       worst + pad * 2 + 26, avail);
        const qreal cy = top + (avail - ch) / 2;

        for (int i = 0; i < n; ++i) {
            const QPair<QString, QString> p = pairOf(bullets.at(i));
            const qreal x = M + i * (cw + gap);
            s.items.push_back(shape(QRectF(x, cy, cw, ch), su.panel,
                                    ShapeKind::RoundedRect, t.radius));
            // A rule along the top edge rather than a filled header band: it
            // marks the card without turning four cards into four boxes of
            // colour competing with the photograph on the next slide.
            s.items.push_back(shape(QRectF(x, cy, cw, 4), t.accent));

            qreal ty = cy + pad + 4;
            const qreal hh = textHeight(inlinePlain(p.first), cw - pad * 2, headPt,
                                        true, t.headFont);
            s.items.push_back(textItem(QRectF(x + pad, ty, cw - pad * 2, hh), p.first,
                                       headPt, true, su.head, t.headFont));
            ty += hh + 10;
            if (!p.second.isEmpty()) {
                const qreal bh = textHeight(inlinePlain(p.second), cw - pad * 2,
                                            textPt, false, t.bodyFont);
                s.items.push_back(textItem(QRectF(x + pad, ty, cw - pad * 2, bh + 4),
                                           p.second, textPt, false, su.body, t.bodyFont));
            }
        }
        return s;
    }

    // ── timeline ────────────────────────────────────────────────────────────
    if (layout == QLatin1String("timeline")) {
        const Surface su = surfaceFor(t, ctx.ordinal);
        applySurface(s, t, su, ctx.ordinal);
        const qreal top = header(s, t, su, kick, title, ctx.ordinal);

        const int n = qMin(5, int(bullets.size()));
        if (n < 2) return s;
        const qreal fullW = kSlideW - M * 2;
        const qreal cw = fullW / n;
        const qreal dot = 38;
        const qreal textW = cw - 18;
        const qreal band = kSlideH - M - top;

        qreal titlePt = 17, textPt = 14;
        qreal worst = 0;
        for (; textPt > 9.5; textPt -= 0.5) {
            worst = 0;
            for (int i = 0; i < n; ++i) {
                const QPair<QString, QString> p = pairOf(bullets.at(i));
                qreal need = textHeight(inlinePlain(p.first), textW, titlePt, true,
                                        t.headFont) + 7;
                if (!p.second.isEmpty())
                    need += textHeight(inlinePlain(p.second), textW, textPt, false,
                                       t.bodyFont);
                worst = qMax(worst, need);
            }
            if (worst <= band - dot - 22) break;
        }

        // Centre the whole run, markers and captions together, in the space
        // under the heading, and put a panel behind it. Five small markers and
        // a hairline adrift in an otherwise empty half of the slide read as an
        // unfinished slide; the panel gives the row something to sit on.
        const qreal blockH = dot + 22 + worst;
        const qreal panelH = qMin(band, blockH + 76);
        const qreal panelY = top + (band - panelH) / 2;
        s.items.push_back(shape(QRectF(M - 14, panelY, fullW + 28, panelH),
                                su.panel, ShapeKind::RoundedRect, t.radius));
        const qreal lineY = panelY + (panelH - blockH) / 2 + dot / 2;

        // The connector stops at the outer markers rather than running the full
        // width, which would leave a stub of line hanging past the last step.
        s.items.push_back(shape(QRectF(M + cw / 2, lineY - 1.5,
                                       fullW - cw, 3), mixed(su.bg1, t.accent, 0.45)));

        for (int i = 0; i < n; ++i) {
            const QPair<QString, QString> p = pairOf(bullets.at(i));
            const qreal cx = M + i * cw + cw / 2;
            s.items.push_back(shape(QRectF(cx - dot / 2, lineY - dot / 2, dot, dot),
                                    t.accent, ShapeKind::Ellipse));
            const QString num = QString::number(i + 1);
            const qreal nh = textHeight(num, dot, 15, true, t.headFont);
            s.items.push_back(textItem(QRectF(cx - dot / 2, lineY - nh / 2, dot, nh),
                                       num, 15, true, t.paper, t.headFont,
                                       Qt::AlignHCenter));

            qreal cy = lineY + dot / 2 + 22;
            const qreal hh = textHeight(inlinePlain(p.first), textW, titlePt, true,
                                        t.headFont);
            s.items.push_back(textItem(QRectF(cx - textW / 2, cy, textW, hh), p.first,
                                       titlePt, true, su.head, t.headFont,
                                       Qt::AlignHCenter));
            cy += hh + 7;
            if (!p.second.isEmpty()) {
                const qreal bh = textHeight(inlinePlain(p.second), textW, textPt,
                                            false, t.bodyFont);
                s.items.push_back(textItem(QRectF(cx - textW / 2, cy, textW, bh + 4),
                                           p.second, textPt, false, su.body,
                                           t.bodyFont, Qt::AlignHCenter));
            }
        }
        return s;
    }

    // ── two things, side by side ────────────────────────────────────────────
    if (layout == QLatin1String("compare")) {
        const Surface su = surfaceFor(t, ctx.ordinal);
        applySurface(s, t, su, ctx.ordinal);
        const qreal top = header(s, t, su, kick, title, ctx.ordinal);

        const QStringList leftItems  = list(o, "left",  written);
        const QStringList rightItems = list(o, "right", written);
        const QString leftTitle  = str(o, "leftTitle");
        const QString rightTitle = str(o, "rightTitle");
        if (written) *written += leftTitle.size() + rightTitle.size();

        const qreal gap = 26;
        const qreal pw = (kSlideW - M * 2 - gap) / 2;
        const qreal avail = kSlideH - M - top;
        const qreal band = 46;
        const qreal pad = 20;

        const QColor fills[2]  = { su.panel, mixed(su.bg1, t.accent, 0.14) };
        const QColor bands[2]  = { mixed(su.head, su.bg1, 0.22), t.accent };
        const QString heads[2] = { leftTitle, rightTitle };
        const QStringList cols[2] = { leftItems, rightItems };

        // Both panels take the height the fuller of the two needs, so they read
        // as a pair, and the pair is centred under the heading.
        const qreal listW = pw - pad * 2;
        qreal pt = 15;
        qreal tallest = 0;
        for (; pt > 10; pt -= 0.5) {
            tallest = 0;
            for (int c = 0; c < 2; ++c)
                tallest = qMax(tallest, bulletsHeight(cols[c], listW, pt, 13, t));
            if (tallest <= avail - band - pad * 2) break;
        }
        const qreal ph = qBound<qreal>(qMin<qreal>(avail * 0.72, avail),
                                       band + pad * 2 + tallest, avail);
        const qreal py = top + (avail - ph) / 2;

        for (int c = 0; c < 2; ++c) {
            const qreal x = M + c * (pw + gap);
            s.items.push_back(shape(QRectF(x, py, pw, ph), fills[c],
                                    ShapeKind::RoundedRect, t.radius));
            s.items.push_back(shape(QRectF(x, py, pw, band), bands[c],
                                    ShapeKind::RoundedRect, t.radius));
            // Square off the bottom of the header band, so it sits inside the
            // panel instead of floating as a separate rounded pill.
            s.items.push_back(shape(QRectF(x, py + band / 2, pw, band / 2), bands[c]));
            const qreal hh = textHeight(inlinePlain(heads[c]), listW, 16, true,
                                        t.headFont);
            s.items.push_back(textItem(QRectF(x + pad, py + (band - hh) / 2,
                                              listW, hh), heads[c], 16, true,
                                       t.paper, t.headFont));
            if (!cols[c].isEmpty())
                bulletColumn(s, t, cols[c], x + pad, py + band + pad, listW, pt,
                             13, su.body);
        }
        return s;
    }

    // ── one sentence, said loudly ───────────────────────────────────────────
    if (layout == QLatin1String("statement")) {
        setDark(s, t);
        if (!image.isEmpty())
            wantImage(s, reqs, image, QRectF(0, 0, kSlideW, kSlideH), t, true,
                      ImageTreatment::ScrimFull, 0, true);
        else
            darkMotif(s, t, ctx.ordinal);

        const QString line = body.isEmpty() ? title : body;
        const qreal tw = kSlideW - M * 2 - 60;
        const qreal pt = fitOne(inlinePlain(line), tw, 250, 46, 22, t.headFont);
        const qreal th = textHeight(inlinePlain(line), tw, pt, true, t.headFont);
        const qreal sh = sub.isEmpty() ? 0 : textHeight(inlinePlain(sub), tw, 16,
                                                        false, t.bodyFont);
        const qreal blockH = 4 + 26 + th + (sub.isEmpty() ? 0 : 20 + sh);
        qreal y = (kSlideH - blockH) / 2;
        s.items.push_back(shape(QRectF(M, y, 76, 4), t.accent));
        y += 26;
        s.items.push_back(textItem(QRectF(M, y, tw, th), line, pt, true, t.paper,
                                   t.headFont));
        y += th + 20;
        if (!sub.isEmpty())
            s.items.push_back(textItem(QRectF(M, y, tw, sh), sub, 16, false,
                                       t.onDeep, t.bodyFont));
        return s;
    }

    // ── pull quote ──────────────────────────────────────────────────────────
    if (layout == QLatin1String("quote")) {
        setDark(s, t);
        if (!image.isEmpty())
            wantImage(s, reqs, image, QRectF(0, 0, kSlideW, kSlideH), t, true,
                      ImageTreatment::Duotone, 0, true);
        else
            darkMotif(s, t, ctx.ordinal);

        // No oversized quotation mark behind the text. Set at display size in a
        // sans face it renders as two solid bars, which reads as a graphics
        // fault rather than as punctuation; the curly marks around the line
        // itself do the job without the risk.
        const QString q = QString(QChar(0x201C)) + (body.isEmpty() ? title : body)
                        + QString(QChar(0x201D));
        const qreal qw = kSlideW - M * 2 - 80;
        const qreal pt = fitOne(inlinePlain(q), qw, 250, 38, 16, t.headFont, false);
        const qreal qh = textHeight(inlinePlain(q), qw, pt, false, t.headFont);
        const qreal ah = sub.isEmpty() ? 0 : textHeight(inlinePlain(sub),
                                                        kSlideW - M * 2, 14, true,
                                                        t.bodyFont);
        const qreal blockH = qh + (sub.isEmpty() ? 0 : 30 + ah);
        const qreal y = (kSlideH - blockH) / 2 + 12;
        s.items.push_back(textItem(QRectF(M + 40, y, qw, qh), q, pt, false, t.paper,
                                   t.headFont, Qt::AlignHCenter));
        if (!sub.isEmpty()) {
            s.items.push_back(shape(QRectF(kSlideW / 2 - 20, y + qh + 13, 40, 3),
                                    t.accent));
            s.items.push_back(textItem(QRectF(M, y + qh + 30, kSlideW - M * 2, ah),
                                       t.eyebrowUpper ? sub.toUpper() : sub, 14, true,
                                       t.onDeep, t.bodyFont, Qt::AlignHCenter));
        }
        return s;
    }

    // ── metrics: a few big numbers ──────────────────────────────────────────
    // Given as bullets of "value | label". Big figures are what a company deck
    // actually opens a results section with, and they carry far better from the
    // back of a room than a sentence does.
    if (layout == QLatin1String("metrics")) {
        setDark(s, t);
        darkMotif(s, t, ctx.ordinal);
        Surface su;
        su.dark = true;   su.bg1  = t.deep;   su.head  = t.paper;
        su.body = t.onDeep; su.muted = t.onDeep; su.panel = alpha(t.paper, 20);
        const qreal top = header(s, t, su, kick, title, ctx.ordinal);

        const int n = qMin(4, int(bullets.size()));
        if (n < 1) return s;
        const qreal gap = 22;
        const qreal cw = (kSlideW - M * 2 - gap * (n - 1)) / n;
        const qreal ch = qMin<qreal>(214, kSlideH - M - top);
        const qreal cy = top + qMax<qreal>(0, (kSlideH - M - top - ch) / 2);

        // One size for every figure on the slide. Letting each card pick its own
        // produced four numbers at four sizes, which looks like four slides
        // rather than one.
        //
        // The size is the largest at which all of them sit on a single line, but
        // it will not go below the floor to achieve that. One verbose value
        // ("340 kilometers per hour" where the others say "2.8 seconds") would
        // otherwise drag every figure on the slide down to something nobody can
        // read from the back of a room. Past the floor the long one wraps, which
        // is measured and allowed for below.
        constexpr qreal kFigureFloor = 27;
        qreal vpt = 46;
        for (; vpt > kFigureFloor; vpt -= 1) {
            bool fits = true;
            for (int i = 0; i < n && fits; ++i)
                fits = textWidthOf(pairOf(bullets.at(i)).first, vpt, true, t.headFont)
                       <= cw - 28 - kDocMargin;
            if (fits) break;
        }

        // Figures and labels line up across the row rather than each card
        // centring its own pair. Centring them individually put the four labels
        // at four different heights the moment one figure wrapped, and a row of
        // numbers that does not share a baseline looks accidental.
        qreal maxVh = 0, maxLh = 0;
        for (int i = 0; i < n; ++i) {
            const QPair<QString, QString> p = pairOf(bullets.at(i));
            maxVh = qMax(maxVh, textHeight(p.first, cw - 28, vpt, true, t.headFont));
            if (!p.second.isEmpty())
                maxLh = qMax(maxLh, textHeight(p.second, cw - 28, 13, false, t.bodyFont));
        }
        const qreal blockH = maxVh + (maxLh > 0 ? 12 + maxLh : 0);
        const qreal ty = cy + (ch - blockH) / 2;

        for (int i = 0; i < n; ++i) {
            const QPair<QString, QString> p = pairOf(bullets.at(i));
            const qreal x = M + i * (cw + gap);
            s.items.push_back(shape(QRectF(x, cy, cw, ch), alpha(t.paper, 20),
                                    ShapeKind::RoundedRect, t.radius));
            s.items.push_back(shape(QRectF(x, cy, cw, 3), t.accent));

            // Centred inside the shared figure band, so a one-line number sits
            // level with a two-line one instead of hanging from its top.
            s.items.push_back(textItem(QRectF(x + 14, ty, cw - 28, maxVh), p.first, vpt,
                                       true, t.paper, t.headFont, Qt::AlignHCenter, 1));
            if (!p.second.isEmpty())
                s.items.push_back(textItem(QRectF(x + 14, ty + maxVh + 12, cw - 28, maxLh),
                                           p.second, 13, false, t.onDeep, t.bodyFont,
                                           Qt::AlignHCenter));
        }
        return s;
    }

    // ── chart ───────────────────────────────────────────────────────────────
    // A real chart item, not a picture of one: it is drawn by the same code
    // that draws a chart the user inserts, so it stays sharp and editable.
    if (layout == QLatin1String("chart")) {
        const Surface su = surfaceFor(t, ctx.ordinal);
        applySurface(s, t, su, ctx.ordinal);
        const qreal top = header(s, t, su, kick, title, ctx.ordinal);

        SlideItem ch;
        ch.type = SlideItemType::Chart;
        const QString kind = str(o, "chart").toLower();
        ch.chartKind = kind == QLatin1String("line") ? ChartKind::Line
                     : kind == QLatin1String("pie")  ? ChartKind::Pie
                                                     : ChartKind::Bar;
        for (const QJsonValue& v : o.value(QStringLiteral("labels")).toArray())
            ch.chartLabels.push_back(v.toString());
        for (const QJsonValue& v : o.value(QStringLiteral("values")).toArray())
            ch.chartValues.push_back(v.toDouble());
        // The chart wears the deck's colours rather than a default palette,
        // which is the difference between a chart on the slide and a chart
        // pasted onto it.
        ch.chartSeries.push_back(ch.chartValues);
        for (int i = 0; i < int(ch.chartValues.size()); ++i)
            ch.chartSeriesColors.push_back(
                mixed(t.accent, t.accent2,
                      ch.chartValues.size() > 1
                          ? qreal(i) / qreal(ch.chartValues.size() - 1) : 0.0));
        ch.chartShowValues = true;
        ch.chartShowLegend = false;
        ch.fillColor = su.dark ? mixed(t.deep, t.paper, 0.10) : t.paper;
        ch.penColor  = mixed(su.bg1, su.head, 0.16);

        const bool hasBody = !body.isEmpty();
        ch.rect = hasBody ? QRectF(M, top, kSlideW * 0.55, kSlideH - top - M)
                          : QRectF(M, top, kSlideW - M * 2, kSlideH - top - M);
        s.items.push_back(ch);
        if (hasBody) {
            const qreal x = M + kSlideW * 0.55 + 28;
            const qreal w = kSlideW - x - M;
            const qreal h = textHeight(inlinePlain(body), w, 15, false, t.bodyFont);
            s.items.push_back(textItem(QRectF(x, top + 6, w, h + 6), body, 15, false,
                                       su.body, t.bodyFont));
        }
        return s;
    }

    // ── content, the workhorse ──────────────────────────────────────────────
    const Surface su = surfaceFor(t, ctx.ordinal);
    applySurface(s, t, su, ctx.ordinal);
    qreal y = header(s, t, su, kick, title, ctx.ordinal);

    const bool twoCol = layout == QLatin1String("twocolumn") && bullets.size() >= 4;
    const qreal fullW = kSlideW - M * 2;

    if (!body.isEmpty()) {
        const qreal h = textHeight(inlinePlain(body), fullW, 17, false, t.bodyFont);
        s.items.push_back(textItem(QRectF(M, y, fullW, h + 8), body, 17, false,
                                   su.body, t.bodyFont));
        y += h + 22;
    }

    // Every bullet is measured at its real width, and the type steps down until
    // the set fits. The previous fixed row height is exactly what made long
    // bullets overlap the ones beneath them.
    if (bullets.isEmpty()) return s;

    QStringList plain;
    for (const QString& b : bullets) plain << inlinePlain(b);

    const qreal available = kSlideH - y - M;
    const qreal gap  = twoCol ? 14 : 16;
    const qreal colW = twoCol ? (fullW - 40) / 2 : fullW - 30;
    const int   perCol = twoCol ? (plain.size() + 1) / 2 : plain.size();
    const qreal pt = fitAll(twoCol ? plain.mid(0, perCol) : plain, colW - 24,
                            available, 18, 11, gap, t.bodyFont);

    // Three short bullets under a heading leave most of the slide empty, and
    // pinned to the top they look abandoned rather than airy. The block is
    // nudged down toward the optical centre when it does not fill the space,
    // without ever being centred so far that it loses its heading.
    const qreal used = bulletsHeight(twoCol ? bullets.mid(0, perCol) : bullets,
                                     colW, pt, gap, t);
    if (used < available * 0.74) y += (available - used) * 0.34;

    if (twoCol) {
        bulletColumn(s, t, bullets.mid(0, perCol), M, y, colW, pt, gap, su.body);
        bulletColumn(s, t, bullets.mid(perCol), M + colW + 40, y, colW, pt, gap, su.body);
    } else {
        bulletColumn(s, t, bullets, M, y, colW, pt, gap, su.body);
    }
    return s;
}

} // namespace NativeOffice
