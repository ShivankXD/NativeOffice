// ─────────────────────────────────────────────────────────────────────────────
// PresentModeWindow.cpp  (Sprint 12 → 13)
// ─────────────────────────────────────────────────────────────────────────────
#include "PresentModeWindow.h"
#include "SlideScene.h"

#include <QLabel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QShowEvent>
#include <QPainter>
#include <QVariantAnimation>
#include <QTimer>
#include <QGuiApplication>
#include <QScreen>
#include <QGraphicsItem>
#include <QDesktopServices>
#include <QUrl>
#include <QtMath>
#include <algorithm>

namespace NativeOffice {

namespace {
constexpr int kTransitionMs = 500;
constexpr int kObjectMs     = 600;
}

PresentModeWindow::PresentModeWindow(const std::vector<SlideData>& deck,
                                     int startIndex,
                                     QWidget* parent)
    : QWidget(parent, Qt::Window)
    , m_index(qBound(0, startIndex, static_cast<int>(deck.size()) - 1))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setStyleSheet("background-color: black;");
    setCursor(Qt::BlankCursor);
    setFocusPolicy(Qt::StrongFocus);

    // Build private scenes from the data so we never touch the editor's scenes.
    for (const auto& data : deck) {
        auto* scene = new SlideScene(this);
        scene->loadFromData(data);
        m_scenes.push_back(scene);
        m_transitions.push_back(data.transition);
    }

    m_slideLabel = new QLabel(this);
    m_slideLabel->setAlignment(Qt::AlignCenter);
    m_slideLabel->setStyleSheet("background: black;");

    m_counter = new QLabel(this);
    m_counter->setStyleSheet(
        "color: rgba(255,255,255,0.55); background: rgba(0,0,0,0.35);"
        "border-radius: 10px; padding: 3px 10px; font-family: 'Segoe UI'; font-size: 13px;");
    m_counter->setAttribute(Qt::WA_TransparentForMouseEvents);

    if (auto* scr = QGuiApplication::primaryScreen())
        setGeometry(scr->geometry());

    showFullScreen();
    raise();
    activateWindow();
}

PresentModeWindow::~PresentModeWindow() = default;

void PresentModeWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!m_firstShown) {
        m_firstShown = true;
        // Defer first render until geometry is final (fullscreen settled).
        QTimer::singleShot(0, this, [this] {
            m_slideLabel->setGeometry(rect());
            goTo(m_index, /*animate=*/false);
        });
    }
}

// ── Rendering ─────────────────────────────────────────────────────────────────
QPixmap PresentModeWindow::renderScene(SlideScene* scene) const {
    const QSize target = size().isEmpty() ? QSize(1280, 720) : size();
    QPixmap pm(target);
    pm.fill(Qt::black);
    if (!scene) return pm;

    const qreal aspect = SlideScene::SLIDE_W / SlideScene::SLIDE_H;
    QRectF dest(0, 0, target.width(), target.height());
    if (dest.width() / dest.height() > aspect) {
        const qreal w = dest.height() * aspect;
        dest.setLeft((dest.width() - w) / 2.0);
        dest.setWidth(w);
    } else {
        const qreal h = dest.width() / aspect;
        dest.setTop((dest.height() - h) / 2.0);
        dest.setHeight(h);
    }

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::TextAntialiasing);
    scene->render(&p, dest, QRectF(0, 0, SlideScene::SLIDE_W, SlideScene::SLIDE_H));
    return pm;
}

QPixmap PresentModeWindow::compositeFrame(const QPixmap& oldPm, const QPixmap& newPm,
                                          SlideTransition type, double t) const {
    const QSize sz = size();
    const int W = sz.width(), H = sz.height();
    QPixmap out(sz);
    out.fill(Qt::black);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    switch (type) {
    case SlideTransition::Push: {
        const int dx = static_cast<int>(t * W);
        p.drawPixmap(-dx, 0, oldPm);
        p.drawPixmap(W - dx, 0, newPm);
        break;
    }
    case SlideTransition::Wipe: {
        p.drawPixmap(0, 0, oldPm);
        p.setClipRect(QRectF(0, 0, W * t, H));
        p.drawPixmap(0, 0, newPm);
        break;
    }
    case SlideTransition::Zoom: {
        p.setOpacity(1.0 - t);
        p.drawPixmap(0, 0, oldPm);
        p.setOpacity(t);
        const double s = 0.6 + 0.4 * t;
        const int w = static_cast<int>(W * s), h = static_cast<int>(H * s);
        p.drawPixmap(QRect((W - w) / 2, (H - h) / 2, w, h), newPm);
        break;
    }
    case SlideTransition::Cut:
        // Hard cut: show the old frame for the first half, then snap to new.
        p.drawPixmap(0, 0, t < 0.5 ? oldPm : newPm);
        break;
    case SlideTransition::Cover: {
        // New slide slides in from the right, covering the stationary old one.
        const int dx = static_cast<int>((1.0 - t) * W);
        p.drawPixmap(0, 0, oldPm);
        p.drawPixmap(dx, 0, newPm);
        break;
    }
    case SlideTransition::Uncover: {
        // Old slide slides out to the left, revealing the stationary new one.
        const int dx = static_cast<int>(t * W);
        p.drawPixmap(0, 0, newPm);
        p.drawPixmap(-dx, 0, oldPm);
        break;
    }
    case SlideTransition::Blinds: {
        // Six vertical blinds: each column reveals the new slide left-to-right.
        p.drawPixmap(0, 0, oldPm);
        const int strips = 6;
        const double stripW = static_cast<double>(W) / strips;
        for (int i = 0; i < strips; ++i) {
            const double x = i * stripW;
            p.setClipRect(QRectF(x, 0, stripW * t, H));
            p.drawPixmap(0, 0, newPm);
        }
        p.setClipping(false);
        break;
    }
    case SlideTransition::Dissolve: {
        // Deterministic block dissolve: a growing share of an 32×18 grid of
        // tiles flips to the new slide as t advances.
        p.drawPixmap(0, 0, oldPm);
        const int cols = 32, rows = 18;
        const double cw = static_cast<double>(W) / cols;
        const double ch = static_cast<double>(H) / rows;
        unsigned seed = 0x9E3779B9u;
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                seed = seed * 1664525u + 1013904223u;          // LCG
                const double threshold = (seed >> 8) / 16777216.0;
                if (threshold <= t) {
                    const QRectF cell(c * cw, r * ch, cw + 1, ch + 1);
                    p.setClipRect(cell);
                    p.drawPixmap(0, 0, newPm);
                }
            }
        }
        p.setClipping(false);
        break;
    }
    case SlideTransition::Fade:
    default:
        p.drawPixmap(0, 0, newPm);
        p.setOpacity(1.0 - t);
        p.drawPixmap(0, 0, oldPm);
        break;
    }
    return out;
}

void PresentModeWindow::runTransition(const QPixmap& oldPm, const QPixmap& newPm,
                                      SlideTransition type, std::function<void()> onDone) {
    if (m_transitionAnim) { m_transitionAnim->stop(); m_transitionAnim->deleteLater(); }
    m_transitionAnim = new QVariantAnimation(this);
    m_transitionAnim->setStartValue(0.0);
    m_transitionAnim->setEndValue(1.0);
    m_transitionAnim->setDuration(kTransitionMs);

    connect(m_transitionAnim, &QVariantAnimation::valueChanged, this,
            [this, oldPm, newPm, type](const QVariant& v) {
        if (m_black) return;
        m_slideLabel->setPixmap(compositeFrame(oldPm, newPm, type, v.toDouble()));
    });
    connect(m_transitionAnim, &QVariantAnimation::finished, this,
            [this, newPm, onDone]() {
        if (!m_black) m_slideLabel->setPixmap(newPm);
        if (onDone) onDone();
    });
    m_transitionAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

// ── Per-object entrance animations ───────────────────────────────────────────
ItemAnimation PresentModeWindow::animOf(QGraphicsItem* item) const {
    return static_cast<ItemAnimation>(item->data(SlideScene::AnimationKey).toInt());
}

bool PresentModeWindow::sceneHasAnimations(SlideScene* scene) const {
    for (auto* it : scene->items()) {
        if (it->zValue() < 0) continue;                 // background
        if (animOf(it) != ItemAnimation::None) return true;
    }
    return false;
}

void PresentModeWindow::setItemsToStart(SlideScene* scene) {
    m_animItems.clear();
    m_orig.clear();
    for (auto* it : scene->items()) {
        if (it->zValue() < 0) continue;
        const ItemAnimation a = animOf(it);
        if (a == ItemAnimation::None) continue;

        m_orig.insert(it, { it->pos(), it->opacity(), it->scale(), it->rotation() });
        m_animItems.append(it);

        switch (a) {
        case ItemAnimation::FadeIn:
            it->setOpacity(0.0);
            break;
        case ItemAnimation::FlyInLeft:
            it->setPos(it->pos() - QPointF(SlideScene::SLIDE_W * 0.6, 0));
            break;
        case ItemAnimation::FlyInRight:
            it->setPos(it->pos() + QPointF(SlideScene::SLIDE_W * 0.6, 0));
            break;
        case ItemAnimation::FlyInTop:
            it->setPos(it->pos() - QPointF(0, SlideScene::SLIDE_H * 0.6));
            break;
        case ItemAnimation::FlyInBottom:
            it->setPos(it->pos() + QPointF(0, SlideScene::SLIDE_H * 0.6));
            break;
        case ItemAnimation::ZoomIn:
            it->setTransformOriginPoint(it->boundingRect().center());
            it->setScale(0.2);
            it->setOpacity(0.0);
            break;
        case ItemAnimation::SpinIn:
            it->setTransformOriginPoint(it->boundingRect().center());
            it->setRotation(-180.0);
            it->setScale(0.3);
            it->setOpacity(0.0);
            break;
        case ItemAnimation::EmphasisPulse:
        case ItemAnimation::EmphasisSpin:
            // Emphasis effects start from the object's natural state.
            it->setTransformOriginPoint(it->boundingRect().center());
            break;
        case ItemAnimation::EmphasisBlink:
        default: break;
        }
    }
}

void PresentModeWindow::playObjectAnimations(SlideScene* scene) {
    if (m_animItems.isEmpty()) return;

    if (m_objectAnim) { m_objectAnim->stop(); m_objectAnim->deleteLater(); }
    m_objectAnim = new QVariantAnimation(this);
    m_objectAnim->setStartValue(0.0);
    m_objectAnim->setEndValue(1.0);
    m_objectAnim->setDuration(kObjectMs);

    connect(m_objectAnim, &QVariantAnimation::valueChanged, this,
            [this, scene](const QVariant& v) {
        const double t = v.toDouble();
        for (auto* it : m_animItems) {
            const OrigState& o = m_orig[it];
            switch (animOf(it)) {
            case ItemAnimation::FadeIn:
                it->setOpacity(o.opacity * t);
                break;
            case ItemAnimation::FlyInLeft: {
                const QPointF start = o.pos - QPointF(SlideScene::SLIDE_W * 0.6, 0);
                it->setPos(start + (o.pos - start) * t);
                break;
            }
            case ItemAnimation::FlyInRight: {
                const QPointF start = o.pos + QPointF(SlideScene::SLIDE_W * 0.6, 0);
                it->setPos(start + (o.pos - start) * t);
                break;
            }
            case ItemAnimation::FlyInTop: {
                const QPointF start = o.pos - QPointF(0, SlideScene::SLIDE_H * 0.6);
                it->setPos(start + (o.pos - start) * t);
                break;
            }
            case ItemAnimation::FlyInBottom: {
                const QPointF start = o.pos + QPointF(0, SlideScene::SLIDE_H * 0.6);
                it->setPos(start + (o.pos - start) * t);
                break;
            }
            case ItemAnimation::ZoomIn:
                it->setScale(0.2 + 0.8 * t);
                it->setOpacity(o.opacity * t);
                break;
            case ItemAnimation::SpinIn:
                it->setRotation(-180.0 * (1.0 - t));
                it->setScale(0.3 + 0.7 * t);
                it->setOpacity(o.opacity * t);
                break;
            case ItemAnimation::EmphasisPulse:
                it->setScale(o.scale * (1.0 + 0.3 * std::sin(M_PI * t)));
                break;
            case ItemAnimation::EmphasisSpin:
                it->setRotation(o.rotation + 360.0 * t);
                break;
            case ItemAnimation::EmphasisBlink:
                it->setOpacity(o.opacity * (0.5 + 0.5 * std::cos(2.0 * M_PI * t)));
                break;
            default: break;
            }
        }
        if (!m_black) m_slideLabel->setPixmap(renderScene(scene));
    });
    connect(m_objectAnim, &QVariantAnimation::finished, this, [this, scene]() {
        finishPendingAnimations();
        if (!m_black) m_slideLabel->setPixmap(renderScene(scene));
    });
    m_objectAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void PresentModeWindow::finishPendingAnimations() {
    if (m_objectAnim) { m_objectAnim->stop(); m_objectAnim = nullptr; }
    for (auto* it : m_animItems) {
        if (!m_orig.contains(it)) continue;
        const OrigState& o = m_orig[it];
        it->setPos(o.pos);
        it->setOpacity(o.opacity);
        it->setScale(o.scale);
        it->setRotation(o.rotation);
    }
    m_animItems.clear();
    m_orig.clear();
}

// ── Navigation ────────────────────────────────────────────────────────────────
void PresentModeWindow::goTo(int index, bool animate) {
    if (index < 0 || index >= static_cast<int>(m_scenes.size())) return;

    finishPendingAnimations();
    const QPixmap oldPm = m_slideLabel->pixmap();

    m_index = index;
    SlideScene* scene = m_scenes[index];
    updateCounter();

    const SlideTransition trans = (index < static_cast<int>(m_transitions.size()))
                                      ? m_transitions[index] : SlideTransition::None;
    const bool doTransition = animate && !oldPm.isNull() && trans != SlideTransition::None;
    const bool doObjectAnim = animate && sceneHasAnimations(scene);

    if (doObjectAnim) setItemsToStart(scene);
    const QPixmap newPm = renderScene(scene);

    if (m_black) { m_slideLabel->setPixmap(QPixmap()); return; }

    if (doTransition) {
        runTransition(oldPm, newPm, trans, [this, scene, doObjectAnim] {
            if (doObjectAnim) playObjectAnimations(scene);
        });
    } else {
        m_slideLabel->setPixmap(newPm);
        if (doObjectAnim) playObjectAnimations(scene);
    }
}

void PresentModeWindow::next() {
    if (m_index + 1 < static_cast<int>(m_scenes.size())) goTo(m_index + 1, true);
}

void PresentModeWindow::prev() {
    if (m_index - 1 >= 0) goTo(m_index - 1, /*animate=*/false);
}

void PresentModeWindow::toggleBlack() {
    m_black = !m_black;
    if (m_black) {
        m_slideLabel->setPixmap(QPixmap());
        m_slideLabel->setStyleSheet("background: black;");
    } else {
        m_slideLabel->setPixmap(renderScene(m_scenes[m_index]));
    }
}

void PresentModeWindow::updateCounter() {
    m_counter->setText(QString("%1 / %2")
                       .arg(m_index + 1).arg(static_cast<int>(m_scenes.size())));
    m_counter->adjustSize();
    m_counter->move(width() - m_counter->width() - 24,
                    height() - m_counter->height() - 20);
    m_counter->raise();
}

// ── Events ────────────────────────────────────────────────────────────────────
void PresentModeWindow::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Escape:    close();                return;
    case Qt::Key_Right:
    case Qt::Key_Down:
    case Qt::Key_Space:
    case Qt::Key_PageDown:   next();                 return;
    case Qt::Key_Left:
    case Qt::Key_Up:
    case Qt::Key_PageUp:
    case Qt::Key_Backspace:  prev();                 return;
    case Qt::Key_Home:       goTo(0, false);         return;
    case Qt::Key_End:        goTo(static_cast<int>(m_scenes.size()) - 1, false); return;
    case Qt::Key_B:
    case Qt::Key_Period:     toggleBlack();          return;
    default: break;
    }
    QWidget::keyPressEvent(event);
}

void PresentModeWindow::mousePressEvent(QMouseEvent* event) {
    if (m_black) { toggleBlack(); return; }
    if (event->button() == Qt::RightButton) { prev(); return; }

    // Left click: if it landed on a hyperlinked object, follow the link instead
    // of advancing the show.
    if (m_index >= 0 && m_index < static_cast<int>(m_scenes.size())) {
        SlideScene* scene = m_scenes[m_index];
        const QSize tsz = size();
        const qreal aspect = SlideScene::SLIDE_W / SlideScene::SLIDE_H;
        QRectF dest(0, 0, tsz.width(), tsz.height());
        if (dest.width() / dest.height() > aspect) {
            const qreal w = dest.height() * aspect;
            dest.setLeft((dest.width() - w) / 2.0); dest.setWidth(w);
        } else {
            const qreal h = dest.width() / aspect;
            dest.setTop((dest.height() - h) / 2.0); dest.setHeight(h);
        }
        const QPointF cp = event->position();
        if (dest.contains(cp)) {
            const qreal sx = (cp.x() - dest.left()) / dest.width()  * SlideScene::SLIDE_W;
            const qreal sy = (cp.y() - dest.top())  / dest.height() * SlideScene::SLIDE_H;
            QGraphicsItem* it = scene->itemAt(QPointF(sx, sy), QTransform());
            while (it && it->parentItem()) it = it->parentItem();
            if (it) {
                const QString link = it->data(SlideScene::HyperlinkKey).toString();
                if (!link.isEmpty()) {
                    QDesktopServices::openUrl(QUrl::fromUserInput(link));
                    return;
                }
            }
        }
    }
    next();
}

void PresentModeWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_slideLabel) {
        m_slideLabel->setGeometry(rect());
        if (m_firstShown && !m_black && m_index < static_cast<int>(m_scenes.size()))
            m_slideLabel->setPixmap(renderScene(m_scenes[m_index]));
    }
    updateCounter();
}

} // namespace NativeOffice
