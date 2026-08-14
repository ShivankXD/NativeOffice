#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AiSourcesStrip.h — the row directly above the composer.
//
// Holds two things that both belong next to what you are about to type: where
// the last answer came from, and the control that takes it back.
//
// Sources collapse the way a reference list should. One is named in full and
// the rest become "+9 others", because a stack of ten links above the input box
// pushes the conversation off screen and none of them get read anyway. The
// count is the honest summary; clicking it opens the full list.
//
// Rollback sits at the right of the same row rather than in the transcript. It
// applies to the document, not to a message, and the document is what the user
// is looking at while deciding whether to keep it.
// ─────────────────────────────────────────────────────────────────────────────

#include <QVector>
#include <QWidget>

#include "ai/AiTypes.h"

class QHBoxLayout;
class QLabel;
class QPushButton;

namespace NativeOffice {

class AiSourcesStrip : public QWidget {
    Q_OBJECT
public:
    explicit AiSourcesStrip(QWidget* parent = nullptr);

    // Replaces the shown sources. An empty list hides that half of the row.
    void setSources(const QVector<AiSource>& sources);
    void clearSources();

    // Shows or hides the rollback control, and which direction it currently
    // goes. Hidden when there is nothing to undo.
    void setRollbackVisible(bool on, bool rolledBack);

signals:
    void rollbackClicked();

private:
    void refresh();
    void showAllSources();

    QVector<AiSource> m_sources;
    QHBoxLayout* m_row       { nullptr };
    QWidget*     m_sourceBox { nullptr };
    QLabel*      m_first     { nullptr };
    QPushButton* m_more      { nullptr };
    QPushButton* m_rollback  { nullptr };
};

} // namespace NativeOffice
