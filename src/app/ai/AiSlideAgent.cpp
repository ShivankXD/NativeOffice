#include "AiSlideAgent.h"

#include "AiSlideLayout.h"
#include "ImpressModule.h"
#include "SlideData.h"

#include <QJsonDocument>
#include <QJsonParseError>

namespace NativeOffice {

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
    if (o.value(QStringLiteral("op")).toString() != QLatin1String("slide")) return;

    m_script += t + QLatin1Char('\n');
    buildSlide(o);
    ++m_added;
    emit progress(m_added);
}

void AiSlideAgent::buildSlide(const QJsonObject& o) {
    const SlideData s = buildSlideFromOp(o, &m_written);
    m_target->appendSlide(s);

    // A new presentation opens on one empty placeholder slide. Appending after
    // it left "Click to add Title" as slide 1 of a generated deck and made a
    // ten slide request eleven slides long. The first generated slide takes its
    // place instead.
    if (m_replaceFirst) {
        m_target->removeSlideAt(0);
        m_replaceFirst = false;
    }
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
