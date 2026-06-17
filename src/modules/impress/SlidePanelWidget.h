#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SlidePanelWidget.h  (Sprint 5)
// Left sidebar panel that lists all slide thumbnails and provides a
// "+" button to add new blank slides.
//
// Layout:
//   ┌──────────────────────────┐
//   │  [+] Add Slide  (button) │  ← fixed header
//   ├──────────────────────────┤
//   │  [thumb 1]  (active)     │  ┐
//   │  [thumb 2]               │  │ QScrollArea
//   │  [thumb 3]               │  ┘
//   └──────────────────────────┘
// ─────────────────────────────────────────────────────────────────────────────

#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <vector>

namespace NativeOffice {

class SlideScene;
class SlideThumbnailWidget;

class SlidePanelWidget : public QWidget {
    Q_OBJECT

public:
    explicit SlidePanelWidget(QWidget* parent = nullptr);

    // Add a thumbnail entry for the given scene at the given index
    void addSlide(int slideIndex, SlideScene* scene);

    // Refresh the thumbnail pixmap for a specific slide (call after scene edit)
    void refreshSlide(int slideIndex);

    // Mark one slide as the active (highlighted) one
    void setActiveSlide(int slideIndex);

    // Remove all thumbnails (used when loading a new file)
    void clear();

    [[nodiscard]] int  slideCount() const noexcept;

    // Compute the insertion index for a vertical drop position (used by the
    // internal drop-enabled list container)
    [[nodiscard]] int dropIndexForY(int y) const;

signals:
    void slideClicked(int slideIndex);
    void addSlideRequested();
    void duplicateRequested(int slideIndex);
    void deleteRequested(int slideIndex);
    void reorderRequested(int fromIndex, int toIndex);

private:
    QScrollArea*                          m_scroll    { nullptr };
    QWidget*                              m_listWidget{ nullptr };
    QVBoxLayout*                          m_listLayout{ nullptr };
    std::vector<SlideThumbnailWidget*>    m_thumbnails;
    int                                   m_activeIdx { 0 };
};

} // namespace NativeOffice
