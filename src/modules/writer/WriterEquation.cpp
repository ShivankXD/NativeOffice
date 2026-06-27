// ─────────────────────────────────────────────────────────────────────────────
// WriterEquation.cpp  (Tier 4 — equation editor)
// ─────────────────────────────────────────────────────────────────────────────
#include "WriterEquation.h"

#include <QPainter>
#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <functional>
#include <cmath>

namespace NativeOffice {

namespace {

// A laid-out fragment: size relative to its baseline + how to paint it.
struct Box {
    double w = 0, ascent = 0, descent = 0;
    std::function<void(QPainter&, double x, double base)> draw;
    double height() const { return ascent + descent; }
};

QFont mathFont(double px, bool italic) {
    QFont f("Cambria Math");
    f.setStyleHint(QFont::Serif);
    if (f.family() != "Cambria Math") f.setFamily("STIX Two Math");
    f.setPixelSize(qMax(4, int(std::round(px))));
    f.setItalic(italic);
    return f;
}

const QHash<QString, QString>& cmdMap() {
    static const QHash<QString, QString> m = {
        {"alpha","α"},{"beta","β"},{"gamma","γ"},{"delta","δ"},{"epsilon","ε"},
        {"theta","θ"},{"lambda","λ"},{"mu","μ"},{"pi","π"},{"rho","ρ"},{"sigma","σ"},
        {"phi","φ"},{"omega","ω"},{"Delta","Δ"},{"Sigma","Σ"},{"Omega","Ω"},{"Pi","Π"},
        {"sum","∑"},{"prod","∏"},{"int","∫"},{"infty","∞"},{"partial","∂"},{"nabla","∇"},
        {"times","×"},{"div","÷"},{"cdot","·"},{"pm","±"},{"mp","∓"},
        {"leq","≤"},{"le","≤"},{"geq","≥"},{"ge","≥"},{"neq","≠"},{"approx","≈"},
        {"rightarrow","→"},{"to","→"},{"leftarrow","←"},{"Rightarrow","⇒"},
        {"in","∈"},{"forall","∀"},{"exists","∃"},{"sqrt","√"},
    };
    return m;
}

Box textBox(const QString& s, double px) {
    const bool italic = (s.size() == 1 && s.at(0).isLetter());
    const QFont f = mathFont(px, italic);
    const QFontMetricsF fm(f);
    Box b;
    b.w = fm.horizontalAdvance(s);
    b.ascent = fm.ascent();
    b.descent = fm.descent();
    b.draw = [s, f](QPainter& p, double x, double base) {
        p.setFont(f);
        p.drawText(QPointF(x, base), s);
    };
    return b;
}

Box hbox(const QList<Box>& items) {
    Box b;
    for (const Box& it : items) {
        b.w += it.w;
        b.ascent = qMax(b.ascent, it.ascent);
        b.descent = qMax(b.descent, it.descent);
    }
    QList<Box> copy = items;
    b.draw = [copy](QPainter& p, double x, double base) {
        double cx = x;
        for (const Box& it : copy) { if (it.draw) it.draw(p, cx, base); cx += it.w; }
    };
    return b;
}

Box fracBox(Box num, Box den, double px) {
    const double gap = qMax(2.0, px * 0.12);
    const double axis = px * 0.30;            // bar height above the baseline
    const double pad = qMax(3.0, px * 0.18);
    Box b;
    b.w = qMax(num.w, den.w) + 2 * pad;
    b.ascent = axis + gap + num.height();
    b.descent = gap + den.height() - axis;
    if (b.descent < den.height()) b.descent = den.height();
    b.draw = [=](QPainter& p, double x, double base) {
        const double barY = base - axis;
        const double cx = x + b.w / 2.0;
        // numerator centred above the bar
        const double numBase = barY - gap - num.descent;
        if (num.draw) num.draw(p, cx - num.w / 2.0, numBase);
        // denominator centred below the bar
        const double denBase = barY + gap + den.ascent;
        if (den.draw) den.draw(p, cx - den.w / 2.0, denBase);
        QPen pen = p.pen(); pen.setWidthF(qMax(1.0, px * 0.05));
        p.setPen(pen);
        p.drawLine(QPointF(x + 1, barY), QPointF(x + b.w - 1, barY));
    };
    return b;
}

Box sqrtBox(Box inner, double px) {
    const double lead = px * 0.55;
    const double top = qMax(2.0, px * 0.10);
    Box b;
    b.w = inner.w + lead + 4;
    b.ascent = inner.ascent + top + 2;
    b.descent = inner.descent;
    b.draw = [=](QPainter& p, double x, double base) {
        QPen pen = p.pen(); pen.setWidthF(qMax(1.0, px * 0.05));
        p.setPen(pen);
        const double topY = base - b.ascent + 1;
        const double botY = base + inner.descent;
        const double x0 = x;
        // radical: small dip then up to the overbar, then across the top.
        p.drawLine(QPointF(x0, base - inner.ascent * 0.4),
                   QPointF(x0 + lead * 0.35, botY));
        p.drawLine(QPointF(x0 + lead * 0.35, botY),
                   QPointF(x0 + lead * 0.65, topY));
        p.drawLine(QPointF(x0 + lead * 0.65, topY),
                   QPointF(x + b.w, topY));
        if (inner.draw) inner.draw(p, x + lead + 2, base);
    };
    return b;
}

Box attachScripts(Box baseBox, const Box* sup, const Box* sub, double px) {
    if (!sup && !sub) return baseBox;
    const double sw = qMax(sup ? sup->w : 0.0, sub ? sub->w : 0.0);
    const double shift = px * 0.45;
    Box b;
    b.w = baseBox.w + sw + 1;
    b.ascent = baseBox.ascent;
    b.descent = baseBox.descent;
    double supTop = 0, subBot = 0;
    if (sup) { supTop = baseBox.ascent * 0.55 + sup->ascent; b.ascent = qMax(b.ascent, supTop); }
    if (sub) { subBot = baseBox.descent * 0.4 + sub->descent + px * 0.15; b.descent = qMax(b.descent, subBot); }
    Box bb = baseBox; Box su = sup ? *sup : Box(); Box sb = sub ? *sub : Box();
    const bool hasSup = sup, hasSub = sub;
    b.draw = [=](QPainter& p, double x, double base) {
        if (bb.draw) bb.draw(p, x, base);
        const double sx = x + bb.w + 1;
        if (hasSup && su.draw) su.draw(p, sx, base - shift);
        if (hasSub && sb.draw) sb.draw(p, sx, base + shift * 0.8 + su.descent);
    };
    return b;
}

// ── Parser ───────────────────────────────────────────────────────────────────
struct Parser {
    const QString s;
    int i = 0;

    QChar peek() const { return i < s.size() ? s.at(i) : QChar(); }

    Box parseSeq(double px, bool stopAtBrace) {
        QList<Box> items;
        while (i < s.size()) {
            const QChar c = s.at(i);
            if (stopAtBrace && c == '}') break;
            if (c == '^' || c == '_') { ++i; parseAtomInto(items, px, c); continue; }
            Box atom = parseAtom(px);
            // optional scripts following this atom
            Box sup, sub; bool hasSup = false, hasSub = false;
            while (peek() == '^' || peek() == '_') {
                const QChar sc = s.at(i); ++i;
                Box scr = parseAtom(px * 0.7);
                if (sc == '^') { sup = scr; hasSup = true; } else { sub = scr; hasSub = true; }
            }
            if (hasSup || hasSub)
                atom = attachScripts(atom, hasSup ? &sup : nullptr, hasSub ? &sub : nullptr, px);
            items << atom;
        }
        return hbox(items);
    }

    // Handles a leading ^/_ with no base (rare) by attaching to an empty base.
    void parseAtomInto(QList<Box>& items, double px, QChar sc) {
        Box base = items.isEmpty() ? textBox(" ", px) : items.takeLast();
        Box scr = parseAtom(px * 0.7);
        items << attachScripts(base, sc == '^' ? &scr : nullptr, sc == '_' ? &scr : nullptr, px);
    }

    Box parseGroup(double px) {           // assumes current char is '{'
        ++i;                              // consume '{'
        Box b = parseSeq(px, true);
        if (peek() == '}') ++i;           // consume '}'
        return b;
    }

    Box parseAtom(double px) {
        const QChar c = peek();
        if (c == '{') return parseGroup(px);
        if (c == '\\') {
            ++i;
            QString cmd;
            while (i < s.size() && s.at(i).isLetter()) cmd += s.at(i++);
            if (cmd == "frac") {
                Box n = (peek() == '{') ? parseGroup(px) : parseAtom(px);
                Box d = (peek() == '{') ? parseGroup(px) : parseAtom(px);
                return fracBox(n, d, px);
            }
            if (cmd == "sqrt") {
                Box inner = (peek() == '{') ? parseGroup(px) : parseAtom(px);
                return sqrtBox(inner, px);
            }
            auto it = cmdMap().constFind(cmd);
            if (it != cmdMap().constEnd()) return textBox(it.value(), px);
            return textBox(cmd, px);
        }
        // a single ordinary character
        ++i;
        return textBox(QString(c), px);
    }
};

} // namespace

QImage EquationRenderer::render(const QString& expr, int pixelSize, const QColor& color) {
    Parser parser{ expr, 0 };
    const Box root = parser.parseSeq(pixelSize, false);

    const int pad = 6;
    const int w = qMax(1, int(std::ceil(root.w)) + 2 * pad);
    const int h = qMax(1, int(std::ceil(root.height())) + 2 * pad);
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setPen(color);
    if (root.draw) root.draw(p, pad, pad + root.ascent);
    p.end();
    return img;
}

} // namespace NativeOffice
