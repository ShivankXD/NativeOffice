#include "AiSlideAgent.h"

#include "ImpressModule.h"
#include "SlideData.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

namespace NativeOffice {

namespace {

// The canvas the deck is authored against.
constexpr qreal W = 960.0;
constexpr qreal H = 540.0;
constexpr qreal MARGIN = 64.0;

// One palette, applied consistently. A deck where every slide invents its own
// colours looks generated; a deck that holds a single accent looks designed.
const QColor kInk    (0x14, 0x17, 0x20);
const QColor kBodyInk(0x39, 0x40, 0x52);
const QColor kPaper  (0xFF, 0xFF, 0xFF);
const QColor kDeep   (0x1A, 0x16, 0x33);   // section / title background
const QColor kDeep2  (0x2E, 0x22, 0x5E);   // gradient partner
const QColor kAccent (0x7C, 0x5C, 0xFF);

QColor readColour(const QJsonObject& o, const char* key, const QColor& fallback) {
    const QString s = o.value(QLatin1String(key)).toString();
    if (s.isEmpty()) return fallback;
    const QColor c(s);
    return c.isValid() ? c : fallback;
}

SlideItem textBox(const QRectF& r, const QString& text, qreal pt,
                  bool bold, const QColor& ink, int vAlign = 0,
                  Qt::Alignment align = Qt::AlignLeft) {
    SlideItem it;
    it.type      = SlideItemType::TextBox;
    it.rect      = r;
    it.text      = text;
    it.fontSize  = pt;
    it.penColor  = ink;
    it.fillColor = Qt::transparent;
    it.penWidth  = 0;
    it.vAlign    = vAlign;
    // The scene renders `html` when present; it is what carries weight, colour
    // and alignment, since SlideItem has no fields for them.
    const QString css = QStringLiteral(
        "font-size:%1pt; color:%2; font-weight:%3; text-align:%4;"
        " font-family:'Segoe UI';")
        .arg(pt).arg(ink.name(),
             bold ? QStringLiteral("700") : QStringLiteral("400"),
             align.testFlag(Qt::AlignHCenter) ? QStringLiteral("center")
                                              : QStringLiteral("left"));
    it.html = QStringLiteral("<div style=\"%1\">%2</div>")
                  .arg(css, text.toHtmlEscaped());
    return it;
}

SlideItem accentBar(qreal x, qreal y, qreal w, qreal h, const QColor& c) {
    SlideItem it;
    it.type      = SlideItemType::Shape;
    it.shapeKind = ShapeKind::Rectangle;
    it.rect      = QRectF(x, y, w, h);
    it.fillColor = c;
    it.penColor  = c;
    it.penWidth  = 0;
    return it;
}

} // namespace

AiSlideAgent::AiSlideAgent(QObject* parent) : QObject(parent) {}

void AiSlideAgent::aiBegin() {
    if (!m_target) return;
    m_pending.clear();
    m_jsonCarry.clear();
    m_script.clear();
    m_added   = 0;
    m_written = 0;
    m_live    = true;
    m_state   = State::Applied;
}

void AiSlideAgent::aiFeed(const QString& chunk) {
    if (!m_live || !m_target) return;
    m_pending += chunk;
    int nl;
    while ((nl = m_pending.indexOf(QLatin1Char('\n'))) >= 0) {
        QString line = m_pending.left(nl);
        m_pending.remove(0, nl + 1);
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        takeLine(line);
    }
}

void AiSlideAgent::aiEnd() {
    if (!m_live) return;
    if (!m_pending.trimmed().isEmpty()) takeLine(m_pending);
    m_pending.clear();
    m_live = false;
    emit finished(m_written);
}

void AiSlideAgent::takeLine(const QString& raw) {
    QString t = raw.trimmed();
    if (t.isEmpty()) return;

    // Same brace-carry as the document agent: a pretty-printed object is still
    // one slide, not several broken fragments.
    if (!m_jsonCarry.isEmpty()) {
        m_jsonCarry += QLatin1Char('\n') + raw;
        if (m_jsonCarry.count(QLatin1Char('{')) > m_jsonCarry.count(QLatin1Char('}')))
            return;
        t = m_jsonCarry.trimmed();
        m_jsonCarry.clear();
    } else if (t.startsWith(QLatin1Char('{'))
               && t.count(QLatin1Char('{')) > t.count(QLatin1Char('}'))) {
        m_jsonCarry = raw;
        return;
    }
    if (!t.startsWith(QLatin1Char('{'))) return;    // prose has no slide meaning

    QJsonParseError err{};
    const QJsonDocument d = QJsonDocument::fromJson(t.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !d.isObject()) return;
    const QJsonObject o = d.object();
    if (o.value(QStringLiteral("op")).toString() != QLatin1String("slide")) return;

    m_script += t + QLatin1Char('\n');
    buildSlide(o);
    ++m_added;
    emit progress(m_added);
}

void AiSlideAgent::buildSlide(const QJsonObject& o) {
    const QString layout = o.value(QStringLiteral("layout")).toString();
    const QString title  = o.value(QStringLiteral("title")).toString();
    const QString sub    = o.value(QStringLiteral("subtitle")).toString();
    const QString body   = o.value(QStringLiteral("body")).toString();
    const QJsonArray bullets = o.value(QStringLiteral("bullets")).toArray();
    const QColor accent = readColour(o, "accent", kAccent);

    SlideData s;
    s.notes = o.value(QStringLiteral("notes")).toString();
    m_written += title.size() + sub.size() + body.size() + s.notes.size();

    const bool dark = layout == QLatin1String("title")
                   || layout == QLatin1String("section")
                   || layout == QLatin1String("quote");

    if (dark) {
        s.background  = kDeep;
        s.background2 = kDeep2;              // vertical gradient
        s.layout      = SlideLayout::Title;
    } else {
        s.background = kPaper;
        s.layout     = SlideLayout::TitleContent;
    }

    if (layout == QLatin1String("title")) {
        // Opening slide: everything optically centred, with a short accent rule
        // between title and subtitle so it does not read as two loose lines.
        s.items.push_back(textBox(QRectF(MARGIN, 180, W - MARGIN * 2, 110),
                                  title, 40, true, kPaper, 1, Qt::AlignHCenter));
        s.items.push_back(accentBar(W / 2 - 40, 300, 80, 4, accent));
        if (!sub.isEmpty())
            s.items.push_back(textBox(QRectF(MARGIN, 320, W - MARGIN * 2, 60),
                                      sub, 18, false, QColor(0xC7, 0xC2, 0xE8),
                                      0, Qt::AlignHCenter));
    } else if (layout == QLatin1String("section")) {
        s.items.push_back(accentBar(MARGIN, 232, 6, 76, accent));
        s.items.push_back(textBox(QRectF(MARGIN + 26, 232, W - MARGIN * 2 - 26, 80),
                                  title, 34, true, kPaper, 1));
        if (!sub.isEmpty())
            s.items.push_back(textBox(QRectF(MARGIN + 26, 316, W - MARGIN * 2 - 26, 50),
                                      sub, 16, false, QColor(0xB9, 0xB3, 0xE0)));
    } else if (layout == QLatin1String("quote")) {
        s.items.push_back(textBox(QRectF(MARGIN + 30, 170, W - MARGIN * 2 - 60, 160),
                                  QStringLiteral("“") + (body.isEmpty() ? title : body)
                                      + QStringLiteral("”"),
                                  28, false, kPaper, 1, Qt::AlignHCenter));
        if (!sub.isEmpty())
            s.items.push_back(textBox(QRectF(MARGIN, 350, W - MARGIN * 2, 40),
                                      QStringLiteral("— ") + sub, 16, false,
                                      QColor(0xB9, 0xB3, 0xE0), 0, Qt::AlignHCenter));
    } else {
        // Content slide. A thin accent rule under the title anchors the page and
        // gives every body slide the same skeleton.
        s.items.push_back(textBox(QRectF(MARGIN, 56, W - MARGIN * 2, 60),
                                  title, 28, true, kInk));
        s.items.push_back(accentBar(MARGIN, 120, 56, 4, accent));

        qreal y = 152;
        if (!body.isEmpty()) {
            s.items.push_back(textBox(QRectF(MARGIN, y, W - MARGIN * 2, 90),
                                      body, 17, false, kBodyInk));
            y += 100;
        }
        // Bullets are separate boxes rather than one block of text, so a long
        // point wraps inside its own row instead of pushing the others down
        // into an overlap.
        const int n = bullets.size();
        const qreal avail = H - y - MARGIN;
        const qreal rowH = n > 0 ? qMin<qreal>(56.0, avail / n) : 0;
        for (int i = 0; i < n; ++i) {
            const QString item = bullets.at(i).toString();
            m_written += item.size();
            s.items.push_back(accentBar(MARGIN + 2, y + rowH / 2 - 3, 6, 6, accent));
            s.items.push_back(textBox(QRectF(MARGIN + 22, y, W - MARGIN * 2 - 22, rowH),
                                      item, 17, false, kBodyInk, 1));
            y += rowH;
        }
    }

    m_target->appendSlide(s);
}

void AiSlideAgent::aiRollback() {
    if (!aiCanRollback()) return;
    m_target->removeTrailingSlides(m_added);
    m_state = State::RolledBack;
}

void AiSlideAgent::aiRollforward() {
    if (!aiCanRollforward() || !m_target) return;
    const QString script = m_script;
    aiBegin();
    aiFeed(script);
    aiEnd();
}

} // namespace NativeOffice
