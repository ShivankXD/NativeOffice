// ─────────────────────────────────────────────────────────────────────────────
// TemplateArt.cpp — see TemplateArt.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "TemplateArt.h"
#include "HomeKit.h"

#include <QCryptographicHash>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace NativeOffice::TemplateArt {

namespace {

// The shapes a preview can be built from. Inferred from the template name so
// the caller never has to say.
enum class Layout {
    Resume,      // side column + portrait block
    Letter,      // address block + long paragraph
    Report,      // title band + paragraphs + a small bar chart
    Notes,       // checklist rows
    Newsletter,  // two columns with a masthead
    Table,       // ruled grid
    Budget,      // grid + donut
    Dashboard,   // tiles + line chart
    DeckTitle,   // big title slide
    DeckChart    // slide with bars
};

Layout layoutFor(const QString& nameIn, DocumentType type) {
    const QString n = nameIn.toLower();
    auto has = [&n](const char* s) { return n.contains(QLatin1String(s)); };

    if (type == DocumentType::Impress)
        return (has("chart") || has("review") || has("report") || has("roadmap")
                || has("plan") || has("analysis")) ? Layout::DeckChart : Layout::DeckTitle;

    if (type == DocumentType::Calc) {
        if (has("budget") || has("saving") || has("expense") || has("loan")) return Layout::Budget;
        if (has("dashboard") || has("sales") || has("tracker") || has("analytics"))
            return Layout::Dashboard;
        return Layout::Table;
    }

    if (has("resume") || has("cv") || has("curriculum")) return Layout::Resume;
    if (has("letter") || has("cover")) return Layout::Letter;
    if (has("note") || has("to-do") || has("todo") || has("checklist")
        || has("agenda") || has("minutes")) return Layout::Notes;
    if (has("newsletter") || has("press") || has("flyer")) return Layout::Newsletter;
    return Layout::Report;
}

// Stable pseudo-randomness from the name, so a template always looks the same
// but two templates rarely look alike.
quint32 seedOf(const QString& name) {
    const QByteArray h = QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Md5);
    return (quint8(h[0]) << 24) | (quint8(h[1]) << 16) | (quint8(h[2]) << 8) | quint8(h[3]);
}

struct Rng {
    quint32 s;
    explicit Rng(quint32 seed) : s(seed ? seed : 0x9E3779B9u) {}
    quint32 next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    // 0..n-1
    int pick(int n) { return int(next() % quint32(n)); }
    qreal frac() { return (next() & 0xFFFF) / 65535.0; }
};

QColor accentFor(DocumentType type, quint32 seed) {
    QColor base(type == DocumentType::Calc    ? Home::kCalc
              : type == DocumentType::Impress ? Home::kImpress
              : type == DocumentType::Pdf     ? Home::kPdf
                                              : Home::kWriter);
    // A small hue nudge keeps a wall of templates from looking monotone while
    // still reading as "this is a spreadsheet" / "this is a document".
    int h, s, v;
    base.getHsv(&h, &s, &v);
    h = (h + int(seed % 26) - 13 + 360) % 360;
    base.setHsv(h, s, v);
    return base;
}

void ruledLines(QPainter& p, const QRectF& area, int count, qreal gap,
                const QColor& color, Rng& rng, qreal thickness = 1.6) {
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    qreal y = area.top();
    for (int i = 0; i < count && y + thickness <= area.bottom(); ++i) {
        const qreal w = area.width() * (0.55 + 0.45 * rng.frac());
        p.drawRoundedRect(QRectF(area.left(), y, w, thickness), thickness / 2, thickness / 2);
        y += gap;
    }
}

void drawPage(QPainter& p, const QRectF& page) {
    QPainterPath path;
    path.addRoundedRect(page, 3, 3);
    p.fillPath(path, QColor(0xFF, 0xFF, 0xFF));
    p.setPen(QPen(QColor(0, 0, 0, 26), 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

void paintResume(QPainter& p, const QRectF& page, const QColor& accent, Rng& rng) {
    drawPage(p, page);
    const qreal colW = page.width() * 0.34;
    QRectF side(page.left(), page.top(), colW, page.height());
    p.fillRect(side, QColor(accent.red(), accent.green(), accent.blue(), 34));

    // Portrait disc + name lines.
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    const qreal r = colW * 0.26;
    p.drawEllipse(QPointF(side.center().x(), side.top() + r + page.height() * 0.09), r, r);

    QRectF sideText(side.left() + colW * 0.14, side.top() + page.height() * 0.34,
                    colW * 0.72, page.height() * 0.5);
    ruledLines(p, sideText, 9, page.height() * 0.055,
               QColor(accent.red(), accent.green(), accent.blue(), 120), rng, 1.4);

    QRectF body(page.left() + colW + page.width() * 0.06, page.top() + page.height() * 0.10,
                page.width() - colW - page.width() * 0.12, page.height() * 0.8);
    p.setBrush(accent);
    p.drawRoundedRect(QRectF(body.left(), body.top(), body.width() * 0.55, 4), 2, 2);
    QRectF rest(body.left(), body.top() + page.height() * 0.10, body.width(),
                body.height() - page.height() * 0.10);
    ruledLines(p, rest, 12, page.height() * 0.062, QColor(0x99, 0xA1, 0xB0), rng);
}

void paintLetter(QPainter& p, const QRectF& page, const QColor& accent, Rng& rng) {
    drawPage(p, page);
    const qreal m = page.width() * 0.12;
    QRectF inner = page.adjusted(m, page.height() * 0.10, -m, -page.height() * 0.10);

    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    p.drawRoundedRect(QRectF(inner.left(), inner.top(), inner.width() * 0.34, 4), 2, 2);

    QRectF addr(inner.left(), inner.top() + page.height() * 0.11, inner.width() * 0.5,
                page.height() * 0.16);
    ruledLines(p, addr, 3, page.height() * 0.055, QColor(0xA8, 0xAF, 0xBC), rng, 1.4);

    QRectF body(inner.left(), inner.top() + page.height() * 0.33, inner.width(),
                page.height() * 0.5);
    ruledLines(p, body, 9, page.height() * 0.058, QColor(0x99, 0xA1, 0xB0), rng);
}

void paintReport(QPainter& p, const QRectF& page, const QColor& accent, Rng& rng) {
    drawPage(p, page);

    QRectF band(page.left(), page.top(), page.width(), page.height() * 0.19);
    QPainterPath bandPath;
    bandPath.addRoundedRect(band, 3, 3);
    QLinearGradient g(band.topLeft(), band.topRight());
    g.setColorAt(0.0, accent);
    g.setColorAt(1.0, accent.lighter(126));
    p.fillPath(bandPath, g);
    p.fillRect(QRectF(band.left(), band.bottom() - 3, band.width(), 3), accent.lighter(126));

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 220));
    p.drawRoundedRect(QRectF(page.left() + page.width() * 0.08, band.center().y() - 3,
                             page.width() * 0.45, 5), 2.5, 2.5);

    const qreal m = page.width() * 0.08;
    QRectF body(page.left() + m, band.bottom() + page.height() * 0.07,
                page.width() - 2 * m, page.height() * 0.36);
    ruledLines(p, body, 7, page.height() * 0.052, QColor(0x99, 0xA1, 0xB0), rng);

    // A small bar chart at the foot, the thing that says "report".
    QRectF chart(page.left() + m, page.bottom() - page.height() * 0.26,
                 page.width() - 2 * m, page.height() * 0.18);
    p.setPen(QPen(QColor(0xD5, 0xDA, 0xE3), 1));
    p.drawLine(chart.bottomLeft(), chart.bottomRight());
    p.setPen(Qt::NoPen);
    const int bars = 5;
    const qreal bw = chart.width() / (bars * 1.9);
    for (int i = 0; i < bars; ++i) {
        const qreal h = chart.height() * (0.28 + 0.72 * rng.frac());
        p.setBrush(i % 2 ? accent.lighter(135) : accent);
        p.drawRoundedRect(QRectF(chart.left() + i * bw * 1.9, chart.bottom() - h, bw, h),
                          1.2, 1.2);
    }
}

void paintNotes(QPainter& p, const QRectF& page, const QColor& accent, Rng& rng) {
    drawPage(p, page);
    const qreal m = page.width() * 0.11;
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    p.drawRoundedRect(QRectF(page.left() + m, page.top() + page.height() * 0.11,
                             page.width() * 0.42, 4), 2, 2);

    qreal y = page.top() + page.height() * 0.26;
    const qreal gap = page.height() * 0.093;
    for (int i = 0; y + 6 < page.bottom() - page.height() * 0.06; ++i, y += gap) {
        const bool done = (rng.next() & 3) == 0;
        p.setBrush(done ? accent : QColor(0xFF, 0xFF, 0xFF));
        p.setPen(QPen(done ? accent : QColor(0xC3, 0xC9, 0xD4), 1.2));
        p.drawRoundedRect(QRectF(page.left() + m, y, 6, 6), 1.5, 1.5);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x99, 0xA1, 0xB0));
        p.drawRoundedRect(QRectF(page.left() + m + 11, y + 2,
                                 (page.width() - m * 2 - 11) * (0.45 + 0.5 * rng.frac()), 2.4),
                          1.2, 1.2);
    }
}

void paintNewsletter(QPainter& p, const QRectF& page, const QColor& accent, Rng& rng) {
    drawPage(p, page);
    const qreal m = page.width() * 0.08;

    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    p.drawRoundedRect(QRectF(page.left() + m, page.top() + page.height() * 0.09,
                             page.width() - 2 * m, 6), 3, 3);
    p.setBrush(QColor(0xDD, 0xE2, 0xEA));
    p.drawRect(QRectF(page.left() + m, page.top() + page.height() * 0.21,
                      page.width() - 2 * m, page.height() * 0.22));
    p.setBrush(accent.lighter(150));
    p.drawEllipse(QPointF(page.center().x(), page.top() + page.height() * 0.32),
                  page.width() * 0.07, page.width() * 0.07);

    const qreal colW = (page.width() - 2 * m - page.width() * 0.05) / 2;
    for (int c = 0; c < 2; ++c) {
        QRectF col(page.left() + m + c * (colW + page.width() * 0.05),
                   page.top() + page.height() * 0.5, colW, page.height() * 0.4);
        ruledLines(p, col, 8, page.height() * 0.052, QColor(0x99, 0xA1, 0xB0), rng, 1.5);
    }
}

void paintGrid(QPainter& p, const QRectF& page, const QColor& accent, Rng& rng,
               bool withDonut, bool withTiles) {
    drawPage(p, page);

    QRectF header(page.left(), page.top(), page.width(), page.height() * 0.11);
    p.fillRect(header, accent);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 210));
    p.drawRoundedRect(QRectF(page.left() + page.width() * 0.05, header.center().y() - 2,
                             page.width() * 0.34, 4), 2, 2);

    if (withTiles) {
        const qreal tw = (page.width() - page.width() * 0.16) / 3;
        for (int i = 0; i < 3; ++i) {
            QRectF t(page.left() + page.width() * 0.05 + i * (tw + page.width() * 0.03),
                     header.bottom() + page.height() * 0.06, tw, page.height() * 0.16);
            p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 30));
            p.drawRoundedRect(t, 3, 3);
            p.setBrush(accent);
            p.drawRoundedRect(QRectF(t.left() + 4, t.center().y() - 2, t.width() * 0.5, 4),
                              2, 2);
        }
    }

    const qreal top = withTiles ? page.top() + page.height() * 0.40
                                : header.bottom() + page.height() * 0.05;
    QRectF grid(page.left() + page.width() * 0.05, top,
                page.width() * (withDonut ? 0.52 : 0.90),
                page.bottom() - top - page.height() * 0.07);

    p.setPen(QPen(QColor(0xE2, 0xE6, 0xEC), 1));
    const int rows = 6, cols = 4;
    for (int r = 0; r <= rows; ++r) {
        const qreal y = grid.top() + grid.height() * r / rows;
        p.drawLine(QPointF(grid.left(), y), QPointF(grid.right(), y));
    }
    for (int c = 0; c <= cols; ++c) {
        const qreal x = grid.left() + grid.width() * c / cols;
        p.drawLine(QPointF(x, grid.top()), QPointF(x, grid.bottom()));
    }
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 60));
    for (int i = 0; i < 4; ++i) {
        const int r = rng.pick(rows), c = rng.pick(cols);
        p.drawRect(QRectF(grid.left() + grid.width() * c / cols + 1,
                          grid.top() + grid.height() * r / rows + 1,
                          grid.width() / cols - 2, grid.height() / rows - 2));
    }

    if (withDonut) {
        QRectF pie(page.right() - page.width() * 0.38, grid.top() + grid.height() * 0.12,
                   page.width() * 0.30, page.width() * 0.30);
        int start = 90 * 16;
        const QColor wedges[3] = { accent, accent.lighter(140), accent.darker(125) };
        for (int i = 0; i < 3; ++i) {
            const int span = int((i == 2 ? 1.0 : 0.22 + 0.3 * rng.frac()) * 360 * 16);
            p.setBrush(wedges[i]);
            p.drawPie(pie, start, i == 2 ? (90 * 16 + 360 * 16 - start) % (360 * 16) : span);
            start += span;
        }
        p.setBrush(QColor(0xFF, 0xFF, 0xFF));
        p.drawEllipse(pie.center(), pie.width() * 0.22, pie.height() * 0.22);
    }
}

void paintDeck(QPainter& p, const QRectF& page, const QColor& accent, Rng& rng,
               bool withChart) {
    // Slides are dark in the app's own deck themes, so the thumbnail is too.
    QPainterPath path;
    path.addRoundedRect(page, 3, 3);
    QLinearGradient g(page.topLeft(), page.bottomRight());
    g.setColorAt(0.0, QColor(0x14, 0x18, 0x24));
    g.setColorAt(1.0, QColor(0x24, 0x1B, 0x16));
    p.fillPath(path, g);
    p.setPen(QPen(QColor(255, 255, 255, 30), 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    p.drawRoundedRect(QRectF(page.left() + page.width() * 0.09,
                             page.top() + page.height() * 0.18,
                             page.width() * 0.14, 4), 2, 2);
    p.setBrush(QColor(0xF2, 0xF4, 0xF8));
    p.drawRoundedRect(QRectF(page.left() + page.width() * 0.09,
                             page.top() + page.height() * 0.30,
                             page.width() * 0.62, 7), 3.5, 3.5);
    p.setBrush(QColor(255, 255, 255, 120));
    p.drawRoundedRect(QRectF(page.left() + page.width() * 0.09,
                             page.top() + page.height() * 0.45,
                             page.width() * 0.44, 3.5), 1.75, 1.75);

    if (withChart) {
        QRectF chart(page.left() + page.width() * 0.09, page.bottom() - page.height() * 0.30,
                     page.width() * 0.82, page.height() * 0.20);
        const int bars = 6;
        const qreal bw = chart.width() / (bars * 1.7);
        for (int i = 0; i < bars; ++i) {
            const qreal h = chart.height() * (0.25 + 0.75 * rng.frac());
            p.setBrush(i % 2 ? accent.lighter(130) : accent);
            p.drawRoundedRect(QRectF(chart.left() + i * bw * 1.7, chart.bottom() - h, bw, h),
                              1.2, 1.2);
        }
    } else {
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 60));
        p.drawEllipse(QPointF(page.right() - page.width() * 0.16,
                              page.bottom() - page.height() * 0.20),
                      page.width() * 0.13, page.width() * 0.13);
    }
}

} // namespace

QPixmap preview(const QString& name, DocumentType type, QSize size, qreal dpr) {
    QPixmap pm(int(size.width() * dpr), int(size.height() * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    const quint32 seed = seedOf(name);
    Rng rng(seed);
    const QColor accent = accentFor(type, seed);
    const Layout layout = layoutFor(name, type);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    // Card ground: a soft wash of the accent so the white page floats on it.
    const QRectF box(0, 0, size.width(), size.height());
    QPainterPath ground;
    ground.addRoundedRect(box, 8, 8);
    QLinearGradient bg(box.topLeft(), box.bottomRight());
    bg.setColorAt(0.0, QColor(accent.red(), accent.green(), accent.blue(), 46));
    bg.setColorAt(1.0, QColor(0x10, 0x14, 0x1E));
    p.fillPath(ground, bg);
    p.setClipPath(ground);

    // The page is portrait for documents/sheets and landscape for slides.
    QRectF page;
    if (type == DocumentType::Impress) {
        const qreal w = size.width() * 0.80;
        const qreal h = w * 9.0 / 16.0;
        page = QRectF((size.width() - w) / 2, size.height() * 0.5 - h / 2, w, h);
    } else {
        const qreal h = size.height() * 0.86;
        const qreal w = h * 0.74;
        page = QRectF(size.width() * 0.5 - w / 2, size.height() * 0.5 - h / 2, w, h);
        // Slight lift so the page reads as a sheet on a surface.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 60));
        p.drawRoundedRect(page.adjusted(2, 3, 2, 3), 3, 3);
    }

    switch (layout) {
    case Layout::Resume:     paintResume(p, page, accent, rng); break;
    case Layout::Letter:     paintLetter(p, page, accent, rng); break;
    case Layout::Report:     paintReport(p, page, accent, rng); break;
    case Layout::Notes:      paintNotes(p, page, accent, rng); break;
    case Layout::Newsletter: paintNewsletter(p, page, accent, rng); break;
    case Layout::Table:      paintGrid(p, page, accent, rng, false, false); break;
    case Layout::Budget:     paintGrid(p, page, accent, rng, true,  false); break;
    case Layout::Dashboard:  paintGrid(p, page, accent, rng, false, true);  break;
    case Layout::DeckTitle:  paintDeck(p, page, accent, rng, false); break;
    case Layout::DeckChart:  paintDeck(p, page, accent, rng, true);  break;
    }

    p.end();
    return pm;
}

} // namespace NativeOffice::TemplateArt
