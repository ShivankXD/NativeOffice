#include "AiSlideAgent.h"

#include "AiImageFetcher.h"
#include "ImpressModule.h"
#include "SlideData.h"

#include <QJsonDocument>
#include <QJsonParseError>

namespace NativeOffice {

AiSlideAgent::AiSlideAgent(QObject* parent)
    : QObject(parent), m_fetcher(new AiImageFetcher(this)) {
    m_theme = themeFor(QString(), QString());
    connect(m_fetcher, &AiImageFetcher::ready, this, &AiSlideAgent::placeImage);
}

void AiSlideAgent::aiBegin() {
    if (!m_target) return;
    m_pending.clear();
    m_jsonCarry.clear();
    m_script.clear();
    m_placed.clear();
    m_images.clear();
    m_fetcher->abandonAll();
    m_added   = 0;
    m_written = 0;
    m_live    = true;
    m_state   = State::Applied;
    m_themeSettled = false;
    m_theme = themeFor(QString(), QString());
    // Only a deck the user has not touched is replaced. Generating into a deck
    // that already has slides must add to it, never overwrite the first one.
    m_replaceFirst = m_target->deckIsPristine();
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
    const QString op = o.value(QStringLiteral("op")).toString();

    if (op == QLatin1String("theme")) {
        m_script += t + QLatin1Char('\n');
        const bool late = !m_placed.isEmpty();
        chooseTheme(o.value(QStringLiteral("name")).toString(), QString());
        // A theme named after the deck has started is still honoured, and the
        // slides already on the canvas are re-laid in it. The alternative is a
        // deck whose opening two slides belong to a different design.
        if (late) rebuildAll();
        return;
    }
    if (op != QLatin1String("slide")) return;

    m_script += t + QLatin1Char('\n');
    buildSlide(o);
    ++m_added;
    emit progress(m_added);
}

void AiSlideAgent::chooseTheme(const QString& named, const QString& headline,
                               const QString& context) {
    m_theme = themeFor(named, headline, context);
    m_themeSettled = true;
}

void AiSlideAgent::buildSlide(const QJsonObject& o) {
    if (!m_themeSettled) {
        // No theme op arrived, so the deck's own opening slide decides. What
        // the deck is called carries the subject; the body and the speaker's
        // notes are supporting evidence and are weighted as such, because a
        // stray mention of a platform or a system in the notes should not
        // outvote the title.
        const QString headline = o.value(QStringLiteral("title")).toString()
            + QLatin1Char(' ') + o.value(QStringLiteral("subtitle")).toString()
            + QLatin1Char(' ') + o.value(QStringLiteral("kicker")).toString();
        const QString context = o.value(QStringLiteral("body")).toString()
            + QLatin1Char(' ') + o.value(QStringLiteral("notes")).toString();
        chooseTheme(o.value(QStringLiteral("theme")).toString(), headline, context);
    }

    SlideBuildContext ctx;
    ctx.theme   = m_theme;
    ctx.ordinal = int(m_placed.size()) + 1;

    QVector<SlideImageRequest> wanted;
    const SlideData s = buildSlideFromOp(o, ctx, &m_written, &wanted);

    int idx = m_target->appendSlide(s);

    // A new presentation opens on one empty placeholder slide. Appending after
    // it left "Click to add Title" as slide 1 of a generated deck and made a
    // ten slide request eleven slides long. The first generated slide takes its
    // place instead, which shifts it down one in the deck.
    if (m_replaceFirst) {
        m_target->removeSlideAt(0);
        m_replaceFirst = false;
        idx = qMax(0, idx - 1);
    }

    Placed p;
    p.deckIndex = idx;
    p.op        = o;
    p.data      = s;
    m_placed.push_back(p);

    const int slot = int(m_placed.size()) - 1;
    for (const SlideImageRequest& rq : wanted) {
        const quint64 token = m_nextToken++;
        m_images.insert(token, PendingImage{ slot, rq.itemIndex, rq.markIndex,
                                             rq.size, rq.treatment, rq.radius });
        m_fetcher->request(token, rq.query, rq.size.width());
    }
}

// Re-lays every slide this generation placed, in the theme now in force. Only
// reached when the model named its theme after it had already started.
void AiSlideAgent::rebuildAll() {
    if (!m_target) return;
    m_images.clear();
    m_fetcher->abandonAll();

    for (int slot = 0; slot < m_placed.size(); ++slot) {
        SlideBuildContext ctx;
        ctx.theme   = m_theme;
        ctx.ordinal = slot + 1;

        QVector<SlideImageRequest> wanted;
        // Nothing is counted again: these characters were tallied when the
        // slide was first built, and charging the user twice for one deck
        // because the model changed its mind about the colours is not on.
        m_placed[slot].data = buildSlideFromOp(m_placed.at(slot).op, ctx, nullptr,
                                               &wanted);
        m_target->replaceSlide(m_placed.at(slot).deckIndex, m_placed.at(slot).data);

        for (const SlideImageRequest& rq : wanted) {
            const quint64 token = m_nextToken++;
            m_images.insert(token, PendingImage{ slot, rq.itemIndex, rq.markIndex,
                                                 rq.size, rq.treatment, rq.radius });
            m_fetcher->request(token, rq.query, rq.size.width());
        }
    }
}

void AiSlideAgent::placeImage(quint64 token, const QByteArray& bytes) {
    const auto it = m_images.constFind(token);
    if (it == m_images.constEnd()) return;
    const PendingImage p = it.value();
    m_images.erase(it);

    if (bytes.isEmpty() || !m_target) return;                 // placeholder stays
    if (p.slot < 0 || p.slot >= m_placed.size()) return;
    SlideData& data = m_placed[p.slot].data;
    if (p.itemIndex < 0 || p.itemIndex >= int(data.items.size())) return;

    const QByteArray png = composeSlideImage(bytes, p.size, p.treatment, m_theme,
                                             p.radius);
    if (png.isEmpty()) return;

    // The placeholder becomes the picture. Its rectangle is already exactly the
    // size the image was composed to, so nothing moves on the slide when it
    // lands: the colour block is simply replaced by the photograph.
    SlideItem& item = data.items[size_t(p.itemIndex)];
    item.type      = SlideItemType::Image;
    item.imageData = png;

    // The mark drawn on the placeholder goes with it. Left behind it becomes a
    // ring sitting on top of the photograph.
    if (p.markIndex >= 0 && p.markIndex < int(data.items.size())) {
        SlideItem& mark = data.items[size_t(p.markIndex)];
        mark.penColor  = Qt::transparent;
        mark.fillColor = Qt::transparent;
    }
    m_target->replaceSlide(m_placed.at(p.slot).deckIndex, data);
}

void AiSlideAgent::aiRollback() {
    if (!aiCanRollback()) return;
    m_fetcher->abandonAll();
    m_images.clear();
    m_placed.clear();
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
