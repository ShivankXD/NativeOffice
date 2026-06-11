#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SlideThumbnailWidget.h  (Sprint 5)
// A clickable widget that renders a miniature preview of one slide.
// Used inside SlidePanelWidget to populate the left sidebar.
//
// The thumbnail renders the slide's QGraphicsScene at a fixed thumbnail
// resolution (THUMB_W × THUMB_H) using QGraphicsScene::render().
// ─────────────────────────────────────────────────────────────────────────────

#include <QWidget>
#include <QPixmap>

namespace NativeOffice {

class SlideScene;

class SlideThumbnailWidget : public QWidget {
    Q_OBJECT

public:
    static constexpr int THUMB_W = 160;
    static constexpr int THUMB_H = 90;

    explicit SlideThumbnailWidget(int slideIndex, SlideScene* scene,
                                  QWidget* parent = nullptr);

    [[nodiscard]] int   slideIndex() const noexcept { return m_slideIndex; }
    [[nodiscard]] bool  isActive()   const noexcept { return m_active; }

    // Called when the scene changes — regenerates the preview pixmap
    void refresh();

    // Marks/unmarks this thumbnail as the active (selected) slide
    void setActive(bool active);

signals:
    void clicked(int slideIndex);

protected:
    void paintEvent  (QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent  (QEnterEvent* event) override;
    void leaveEvent  (QEvent*      event) override;

private:
    int        m_slideIndex;
    SlideScene* m_scene     { nullptr };
    QPixmap    m_pixmap;
    bool       m_active     { false };
    bool       m_hovered    { false };
};

} // namespace NativeOffice
