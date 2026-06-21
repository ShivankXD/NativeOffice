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
#include <QTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
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

bool isExitAnim(ItemAnimation a) {
    return a == ItemAnimation::ExitFadeOut || a == ItemAnimation::ExitFlyLeft
        || a == ItemAnimation::ExitFlyRight || a == ItemAnimation::ExitZoomOut;
}
}

PresentModeWindow::PresentModeWindow(const std::vector<SlideData>& deck,
                                     int startIndex,
                                     bool presenter,
                                     QWidget* parent)
    : QWidget(parent, Qt::Window)
    , m_index(qBound(0, startIndex, static_cast<int>(deck.size()) - 1))
    , m_presenter(presenter)
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
        m_slideAnims.push_back(data.slideAnimation);
        m_notes.push_back(data.notes);
    }

    m_slideLabel = new QLabel(this);
    m_slideLabel->setAlignment(Qt::AlignCenter);
    m_slideLabel->setStyleSheet("background: black;");

    m_counter = new QLabel(this);
    m_counter->setStyleSheet(
        "color: rgba(255,255,255,0.55); background: rgba(0,0,0,0.35);"
        "border-radius: 10px; padding: 3px 10px; font-family: 'Segoe UI'; font-size: 13px;");
    m_counter->setAttribute(Qt::WA_TransparentForMouseEvents);

    // Audience window goes on the primary screen; the presenter console (if any)
    // goes on the secondary screen.
    const QList<QScreen*> screens = QGuiApplication::screens();
    if (!screens.isEmpty())
        setGeometry(screens.first()->geometry());

    if (m_presenter) buildConsole();

    showFullScreen();
    raise();
    activateWindow();
}

PresentModeWindow::~PresentModeWindow() {
    delete m_console;   // top-level, not auto-parented
}

// ─────────────────────────────────────────────────────────────────────────────
// Presenter console — notes, next-slide preview, elapsed timer, controls
// ─────────────────────────────────────────────────────────────────────────────
void PresentModeWindow::buildConsole() {
    m_console = new QWidget(nullptr, Qt::Window);
    m_console->setWindowTitle("Presenter View — NativeOffice Impress");
    m_console->setStyleSheet("background:#1A1F2E; color:#E8E9ED; font-family:'Segoe UI';");

    auto* root = new QVBoxLayout(m_console);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    auto* top = new QHBoxLayout;
    m_consoleCur = new QLabel(m_console);
    m_consoleCur->setMinimumSize(520, 300);
    m_consoleCur->setAlignment(Qt::AlignCenter);
    m_consoleCur->setStyleSheet("background:black; border:1px solid #3B4252;");

    auto* side = new QVBoxLayout;
    m_consoleTimer = new QLabel("00:00", m_console);
    m_consoleTimer->setStyleSheet("font-size:30px; font-weight:600;");
    m_consoleCounter = new QLabel("1 / 1", m_console);
    m_consoleCounter->setStyleSheet("font-size:14px; color:#9CA3AF;");
    auto* nextLbl = new QLabel("Next slide", m_console);
    nextLbl->setStyleSheet("color:#9CA3AF; font-size:12px;");
    m_consoleNext = new QLabel(m_console);
    m_consoleNext->setMinimumSize(260, 150);
    m_consoleNext->setAlignment(Qt::AlignCenter);
    m_consoleNext->setStyleSheet("background:black; border:1px solid #3B4252;");
    side->addWidget(m_consoleTimer);
    side->addWidget(m_consoleCounter);
    side->addSpacing(10);
    side->addWidget(nextLbl);
    side->addWidget(m_consoleNext);
    side->addStretch();
    top->addWidget(m_consoleCur, 3);
    top->addLayout(side, 1);
    root->addLayout(top, 3);

    auto* notesLbl = new QLabel("Speaker notes", m_console);
    notesLbl->setStyleSheet("color:#9CA3AF; font-size:12px;");
    m_consoleNotes = new QTextEdit(m_console);
    m_consoleNotes->setReadOnly(true);
    m_consoleNotes->setStyleSheet("background:#2C3140; color:#E8E9ED; border:none; font-size:16px; padding:6px;");
    root->addWidget(notesLbl);
    root->addWidget(m_consoleNotes, 2);

    auto* btns = new QHBoxLayout;
    auto* prevB = new QPushButton(QString::fromUtf8("◀  Prev"), m_console);
    auto* nextB = new QPushButton(QString::fromUtf8("Next  ▶"), m_console);
    auto* endB  = new QPushButton("End Show", m_console);
    const QString bs = "QPushButton{background:#3B4252;color:#fff;border:none;padding:9px 18px;"
                       "border-radius:6px;font-size:14px;}QPushButton:hover{background:#4C566A;}";
    prevB->setStyleSheet(bs); nextB->setStyleSheet(bs); endB->setStyleSheet(bs);
    btns->addWidget(prevB); btns->addWidget(nextB); btns->addStretch(); btns->addWidget(endB);
    root->addLayout(btns);
    connect(prevB, &QPushButton::clicked, this, [this] { prev(); });
    connect(nextB, &QPushButton::clicked, this, [this] { next(); });
    connect(endB,  &QPushButton::clicked, this, [this] { close(); });

    m_elapsedTimer = new QTimer(this);
    m_elapsedTimer->setInterval(1000);
    connect(m_elapsedTimer, &QTimer::timeout, this, [this] {
        ++m_elapsedSecs;
        m_consoleTimer->setText(QString("%1:%2")
            .arg(m_elapsedSecs / 60, 2, 10, QChar('0'))
            .arg(m_elapsedSecs % 60, 2, 10, QChar('0')));
    });
    m_elapsedTimer->start();

    const QList<QScreen*> screens = QGuiApplication::screens();
    if (screens.size() > 1) {
        m_console->setGeometry(screens[1]->geometry());
        m_console->showFullScreen();
    } else {
        m_console->resize(1000, 660);
        m_console->show();   // single monitor: console is a normal window
    }
    m_console->raise();
}

void PresentModeWindow::updateConsole() {
    if (!m_presenter || !m_console || m_index < 0) return;

    m_consoleCur->setPixmap(renderScene(m_scenes[m_index])
        .scaled(m_consoleCur->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

    if (m_index + 1 < static_cast<int>(m_scenes.size())) {
        m_consoleNext->setPixmap(renderScene(m_scenes[m_index + 1])
            .scaled(m_consoleNext->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        m_consoleNext->setPixmap(QPixmap());
        m_consoleNext->setText("End of show");
    }

    m_consoleNotes->setPlainText(m_index < static_cast<int>(m_notes.size())
                                     ? m_notes[m_index] : QString());
    m_consoleCounter->setText(QString("%1 / %2")
        .arg(m_index + 1).arg(static_cast<int>(m_scenes.size())));
}

void PresentModeWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!m_firstShown) {
        m_firstShown = true;
        // Defer first render until geometry is final (fullscreen settled).
        QTimer::singleShot(0, this, [this] {
            m_slideLabel->setGeometry(rect());
            // Play the first slide's entrance (whole-slide + per-object) animation
            // when the show opens, just like PowerPoint. There is no previous
            // slide, so any slide *transition* is correctly skipped (null oldPm).
            goTo(m_index, /*animate=*/true);
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

QPixmap PresentModeWindow::compositeFrame(const QSize& sz, const QPixmap& oldPm,
                                          const QPixmap& newPm, SlideTransition type, double t) {
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
        m_slideLabel->setPixmap(compositeFrame(size(), oldPm, newPm, type, v.toDouble()));
    });
    connect(m_transitionAnim, &QVariantAnimation::finished, this,
            [this, newPm, onDone]() {
        if (!m_black) m_slideLabel->setPixmap(newPm);
        if (onDone) onDone();
    });
    m_transitionAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

// ── Whole-slide entrance animation ───────────────────────────────────────────
namespace {
double easeOutBounce(double t) {
    if (t < 1.0 / 2.75)        return 7.5625 * t * t;
    else if (t < 2.0 / 2.75) { t -= 1.5 / 2.75;  return 7.5625 * t * t + 0.75; }
    else if (t < 2.5 / 2.75) { t -= 2.25 / 2.75; return 7.5625 * t * t + 0.9375; }
    else                     { t -= 2.625 / 2.75; return 7.5625 * t * t + 0.984375; }
}
}

QPixmap PresentModeWindow::compositeSlideAnim(const QSize& sz, const QPixmap& oldPm,
                                              const QPixmap& newPm, SlideAnimation type, double t) {
    const int W = sz.width(), H = sz.height();
    QPixmap out(sz);
    out.fill(Qt::black);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    if (!oldPm.isNull()) p.drawPixmap(0, 0, oldPm);   // backdrop

    const QPointF c(W / 2.0, H / 2.0);
    auto centred = [&](double rotDeg, double scale) {
        QTransform tr;
        tr.translate(c.x(), c.y());
        tr.rotate(rotDeg);
        tr.scale(scale, scale);
        tr.translate(-c.x(), -c.y());
        p.setTransform(tr);
    };

    switch (type) {
    case SlideAnimation::FadeIn:
        p.setOpacity(t); p.drawPixmap(0, 0, newPm); break;
    case SlideAnimation::ZoomIn:
        p.setOpacity(t); centred(0, 0.2 + 0.8 * t); p.drawPixmap(0, 0, newPm); break;
    case SlideAnimation::WhirlIn:
        p.setOpacity(t); centred(-360.0 * (1.0 - t), std::max(0.05, t)); p.drawPixmap(0, 0, newPm); break;
    case SlideAnimation::SpiralIn:
        p.setOpacity(t); centred(-720.0 * (1.0 - t), std::max(0.05, t)); p.drawPixmap(0, 0, newPm); break;
    case SlideAnimation::FlyInLeft:
        p.drawPixmap(static_cast<int>(-(1.0 - t) * W), 0, newPm); break;
    case SlideAnimation::FlyInRight:
        p.drawPixmap(static_cast<int>((1.0 - t) * W), 0, newPm); break;
    case SlideAnimation::FlyInTop:
        p.drawPixmap(0, static_cast<int>(-(1.0 - t) * H), newPm); break;
    case SlideAnimation::FlyInBottom:
        p.drawPixmap(0, static_cast<int>((1.0 - t) * H), newPm); break;
    case SlideAnimation::Bounce:
        p.drawPixmap(0, static_cast<int>(-(1.0 - easeOutBounce(t)) * H), newPm); break;
    case SlideAnimation::RiseUp:
        p.setOpacity(t); p.drawPixmap(0, static_cast<int>((1.0 - t) * H * 0.35), newPm); break;
    case SlideAnimation::Drop: {
        const double e = 1.0 - (1.0 - t) * (1.0 - t);   // ease-out
        p.drawPixmap(0, static_cast<int>(-(1.0 - e) * H), newPm); break;
    }
    case SlideAnimation::None:
    default:
        p.drawPixmap(0, 0, newPm); break;
    }
    return out;
}

void PresentModeWindow::runSlideAnimation(const QPixmap& oldPm, const QPixmap& newPm,
                                          SlideAnimation type, std::function<void()> onDone) {
    if (m_transitionAnim) { m_transitionAnim->stop(); m_transitionAnim->deleteLater(); }
    m_transitionAnim = new QVariantAnimation(this);
    m_transitionAnim->setStartValue(0.0);
    m_transitionAnim->setEndValue(1.0);
    m_transitionAnim->setDuration(kTransitionMs + 150);

    connect(m_transitionAnim, &QVariantAnimation::valueChanged, this,
            [this, oldPm, newPm, type](const QVariant& v) {
        if (m_black) return;
        m_slideLabel->setPixmap(compositeSlideAnim(size(), oldPm, newPm, type, v.toDouble()));
    });
    connect(m_transitionAnim, &QVariantAnimation::finished, this,
            [this, newPm, onDone]() {
        if (!m_black) m_slideLabel->setPixmap(newPm);
        if (onDone) onDone();
    });
    m_transitionAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

// ── Per-object entrance animations ───────────────────────────────────────────
void PresentModeWindow::applyItemAnimFrame(QGraphicsItem* it, ItemAnimation a,
                                           const ItemNatural& o, double t) {
    switch (a) {
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
    case ItemAnimation::ExitFadeOut:
        it->setOpacity(o.opacity * (1.0 - t));
        break;
    case ItemAnimation::ExitFlyLeft:
        it->setPos(o.pos - QPointF(SlideScene::SLIDE_W * 0.6 * t, 0));
        it->setOpacity(o.opacity * (1.0 - t));
        break;
    case ItemAnimation::ExitFlyRight:
        it->setPos(o.pos + QPointF(SlideScene::SLIDE_W * 0.6 * t, 0));
        it->setOpacity(o.opacity * (1.0 - t));
        break;
    case ItemAnimation::ExitZoomOut:
        it->setScale(o.scale * (1.0 - 0.8 * t));
        it->setOpacity(o.opacity * (1.0 - t));
        break;
    default:
        break;
    }
}

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
            applyItemAnimFrame(it, animOf(it), { o.pos, o.opacity, o.scale, o.rotation }, t);
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

bool PresentModeWindow::sceneHasExitAnimations(SlideScene* scene) const {
    for (auto* it : scene->items()) {
        if (it->zValue() < 0) continue;
        if (isExitAnim(animOf(it))) return true;
    }
    return false;
}

void PresentModeWindow::playExitAnimations(SlideScene* scene, std::function<void()> onDone) {
    m_animItems.clear();
    m_orig.clear();
    for (auto* it : scene->items()) {
        if (it->zValue() < 0) continue;
        if (!isExitAnim(animOf(it))) continue;
        m_orig.insert(it, { it->pos(), it->opacity(), it->scale(), it->rotation() });
        m_animItems.append(it);
        if (animOf(it) == ItemAnimation::ExitZoomOut)
            it->setTransformOriginPoint(it->boundingRect().center());
    }
    if (m_animItems.isEmpty()) { if (onDone) onDone(); return; }

    if (m_objectAnim) { m_objectAnim->stop(); m_objectAnim->deleteLater(); }
    m_objectAnim = new QVariantAnimation(this);
    m_objectAnim->setStartValue(0.0);
    m_objectAnim->setEndValue(1.0);
    m_objectAnim->setDuration(kObjectMs);

    connect(m_objectAnim, &QVariantAnimation::valueChanged, this, [this, scene](const QVariant& v) {
        const double t = v.toDouble();
        for (auto* it : m_animItems) {
            const OrigState& o = m_orig[it];
            applyItemAnimFrame(it, animOf(it), { o.pos, o.opacity, o.scale, o.rotation }, t);
        }
        if (!m_black) m_slideLabel->setPixmap(renderScene(scene));
    });
    connect(m_objectAnim, &QVariantAnimation::finished, this, [this, onDone]() {
        finishPendingAnimations();   // restore objects so returning to the slide is clean
        if (onDone) onDone();
    });
    m_objectAnim->start(QAbstractAnimation::DeleteWhenStopped);
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
    const SlideAnimation slideAnim = (index < static_cast<int>(m_slideAnims.size()))
                                      ? m_slideAnims[index] : SlideAnimation::None;
    const bool doSlideAnim  = animate && slideAnim != SlideAnimation::None;
    // A whole-slide animation takes the place of a slide transition.
    const bool doTransition = animate && !doSlideAnim && !oldPm.isNull()
                              && trans != SlideTransition::None;
    const bool doObjectAnim = animate && sceneHasAnimations(scene);

    if (doObjectAnim) setItemsToStart(scene);
    const QPixmap newPm = renderScene(scene);

    if (m_black) { m_slideLabel->setPixmap(QPixmap()); return; }

    if (doSlideAnim) {
        runSlideAnimation(oldPm, newPm, slideAnim, [this, scene, doObjectAnim] {
            if (doObjectAnim) playObjectAnimations(scene);
        });
    } else if (doTransition) {
        runTransition(oldPm, newPm, trans, [this, scene, doObjectAnim] {
            if (doObjectAnim) playObjectAnimations(scene);
        });
    } else {
        m_slideLabel->setPixmap(newPm);
        if (doObjectAnim) playObjectAnimations(scene);
    }

    updateConsole();
}

void PresentModeWindow::next() {
    if (m_index + 1 >= static_cast<int>(m_scenes.size())) return;
    SlideScene* scene = m_scenes[m_index];
    if (sceneHasExitAnimations(scene)) {
        playExitAnimations(scene, [this] { goTo(m_index + 1, true); });
    } else {
        goTo(m_index + 1, true);
    }
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
