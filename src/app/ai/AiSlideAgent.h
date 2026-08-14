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
// slide says and which shape it takes, and this decides where everything sits
// on the 960x540 canvas.
//
// Rollback removes exactly the slides that were added, which is why the count
// is tracked rather than the deck being diffed.
// ─────────────────────────────────────────────────────────────────────────────

#include <QJsonObject>
#include <QObject>
#include <QString>

#include "ai/AiStreamTarget.h"

namespace NativeOffice {

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

signals:
    void progress(int slides);
    void finished(int charactersWritten);

private:
    void takeLine(const QString& line);
    void buildSlide(const QJsonObject& o);

    enum class State { Idle, Applied, RolledBack };

    ImpressModule* m_target { nullptr };
    QString m_pending;      // partial trailing line
    QString m_jsonCarry;    // an object the model spread over several lines
    QString m_script;       // every op seen, so rollforward can replay
    bool    m_live    { false };
    int     m_added   { 0 };
    int     m_written { 0 };
    State   m_state   { State::Idle };
};

} // namespace NativeOffice
