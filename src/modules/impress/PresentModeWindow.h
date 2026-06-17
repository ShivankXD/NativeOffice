#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PresentModeWindow.h  (Sprint 12 → 13)
// Fullscreen "Slide Show" window. Builds its OWN scenes from the slide data so
// the show never mutates the editor's live scenes (no spurious dirty/undo while
// animating). Supports slide transitions (Fade/Push/Wipe/Zoom), per-object
// entrance animations, click / arrow-key navigation, Esc to exit, B to black
// out, and Home/End to jump.
// ─────────────────────────────────────────────────────────────────────────────

#include "SlideData.h"

#include <QWidget>
#include <QHash>
#include <QList>
#include <QPointF>
#include <functional>
#include <vector>

class QLabel;
class QGraphicsItem;
class QVariantAnimation;

namespace NativeOffice {

class SlideScene;

class PresentModeWindow : public QWidget {
    Q_OBJECT

public:
    explicit PresentModeWindow(const std::vector<SlideData>& deck,
                               int startIndex,
                               QWidget* parent = nullptr);
    ~PresentModeWindow() override;

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void goTo(int index, bool animate = true);
    void next();
    void prev();
    void toggleBlack();
    void updateCounter();

    QPixmap renderScene(SlideScene* scene) const;
    QPixmap compositeFrame(const QPixmap& oldPm, const QPixmap& newPm,
                           SlideTransition type, double t) const;
    void runTransition(const QPixmap& oldPm, const QPixmap& newPm,
                       SlideTransition type, std::function<void()> onDone);

    [[nodiscard]] bool sceneHasAnimations(SlideScene* scene) const;
    void setItemsToStart(SlideScene* scene);
    void playObjectAnimations(SlideScene* scene);
    void finishPendingAnimations();
    [[nodiscard]] ItemAnimation animOf(QGraphicsItem* item) const;

    std::vector<SlideScene*>     m_scenes;        // owned (parented to this)
    std::vector<SlideTransition> m_transitions;
    int                          m_index { 0 };
    bool                         m_black { false };
    bool                         m_firstShown { false };

    QLabel* m_slideLabel { nullptr };
    QLabel* m_counter    { nullptr };

    QVariantAnimation* m_transitionAnim { nullptr };
    QVariantAnimation* m_objectAnim     { nullptr };

    struct OrigState { QPointF pos; qreal opacity; qreal scale; };
    QList<QGraphicsItem*>            m_animItems;
    QHash<QGraphicsItem*, OrigState> m_orig;
};

} // namespace NativeOffice
