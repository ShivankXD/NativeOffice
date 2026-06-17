#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// OutlineWidget.h  (Sprint 12)
// Text-only view of the deck: each slide's title is a top-level row, with its
// remaining text items as indented child rows. Editing a row's text pushes
// the change back into the corresponding SlideData/SlideScene text item.
// ─────────────────────────────────────────────────────────────────────────────

#include <QWidget>
#include <vector>

class QTreeWidget;
class QTreeWidgetItem;

namespace NativeOffice {

struct SlideData;

class OutlineWidget : public QWidget {
    Q_OBJECT

public:
    explicit OutlineWidget(QWidget* parent = nullptr);

    // Rebuild the whole outline from the deck (call after structural changes)
    void rebuild(const std::vector<SlideData>& deck);

    // Highlight the row for the given slide without rebuilding
    void setActiveSlide(int slideIndex);

signals:
    void slideSelected(int slideIndex);
    // Emitted when the user edits a row: slideIndex, itemIndex (-1 = title), new text
    void textEdited(int slideIndex, int itemIndex, const QString& newText);

private:
    QTreeWidget* m_tree { nullptr };
    bool         m_ignoreEdits { false };
};

} // namespace NativeOffice
