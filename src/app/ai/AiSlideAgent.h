#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AiSlideAgent.h — builds a deck from streamed operations, one slide at a time.
//
// The same contract as the document agent: one JSON object per line, executed
// the moment its line completes, so slides appear in the panel while the model
// is still writing. Nothing is staged in the chat.
//
// A slide is laid out here rather than described by the model. Asking a
// language model for coordinates produces overlapping boxes and text running
// off the canvas; asking it for a layout name, a title and some bullets
// produces something that can be placed properly. So the model chooses what a
// slide says and which shape it takes, and AiSlideLayout decides where
// everything sits on the 960x540 canvas.
//
// Two things are deck-wide rather than per-slide, and both live here.
//
// The theme. It is settled once, from the model's own choice or from the
// subject of the opening slide, and every slide after it inherits the same
// palette and type. If the model names a theme late, the slides already placed
// are rebuilt, because a deck whose first two slides are a different colour
// from the rest is worse than one that took a moment to settle.
//
// The pictures. They arrive over the network long after the slide that wanted
// them was placed, so each is composed and dropped into its slide as it lands.
//
// Rollback removes exactly the slides that were added, which is why the count
// is tracked rather than the deck being diffed.
// ─────────────────────────────────────────────────────────────────────────────

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include "AiDeckTheme.h"
#include "AiSlideImage.h"
#include "AiSlideLayout.h"
#include "ai/AiStreamTarget.h"

namespace NativeOffice {

class AiImageFetcher;
class ImpressModule;

class AiSlideAgent : public QObject, public AiStreamTarget {
    Q_OBJECT
public:
    explicit AiSlideAgent(QObject* parent = nullptr);

    // The deck this agent writes into. Set once by the shell per Impress tab.
    void setTarget(ImpressModule* target) { m_target = target; }
    ImpressModule* target() const { return m_target; }

    // ── AiStreamTarget ────────────────────────────────────────────────────────
    void aiBegin() override;
    void aiFeed(const QString& chunk) override;
    void aiEnd() override;
    int  aiCharactersWritten() const override { return m_written; }
    bool aiCanRollback() const override { return m_state == State::Applied && m_added > 0; }
    bool aiCanRollforward() const override { return m_state == State::RolledBack; }
    void aiRollback() override;
    void aiRollforward() override;

    bool live() const { return m_live; }
    int  slidesAdded() const { return m_added; }
    QString themeName() const { return m_theme.name; }

signals:
    void progress(int slides);
    void finished(int charactersWritten);

private:
    void takeLine(const QString& line);
    void buildSlide(const QJsonObject& o);
    void chooseTheme(const QString& named, const QString& headline,
                     const QString& context = QString());
    void rebuildAll();
    void placeImage(quint64 token, const QByteArray& bytes);

    enum class State { Idle, Applied, RolledBack };

    // One slide this generation put on the canvas, kept so a picture landing
    // later or a theme arriving late can rebuild it without re-parsing.
    struct Placed {
        int         deckIndex { -1 };
        QJsonObject op;
        SlideData   data;
    };

    // A picture that has been asked for and not yet arrived.
    struct PendingImage {
        int            slot      { -1 };   // index into m_placed
        int            itemIndex { -1 };
        int            markIndex { -1 };
        QSize          size;
        ImageTreatment treatment { ImageTreatment::Plain };
        qreal          radius    { 0 };
    };

    ImpressModule*  m_target  { nullptr };
    AiImageFetcher* m_fetcher { nullptr };

    QString m_pending;      // partial trailing line
    QString m_jsonCarry;    // an object the model spread over several lines
    QString m_script;       // every op seen, so rollforward can replay
    bool    m_live         { false };
    bool    m_replaceFirst { false };  // swap out the blank opening slide
    int     m_added        { 0 };
    int     m_written      { 0 };
    State   m_state        { State::Idle };

    DeckTheme m_theme;
    bool      m_themeSettled { false };

    QVector<Placed>            m_placed;
    QHash<quint64, PendingImage> m_images;
    quint64                    m_nextToken { 1 };
};

} // namespace NativeOffice
