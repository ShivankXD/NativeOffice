// ─────────────────────────────────────────────────────────────────────────────
// StartScreen.cpp - NativeOffice home dashboard.
// ─────────────────────────────────────────────────────────────────────────────
#include "StartScreen.h"
#include "ActivityCard.h"
#include "FileSearch.h"
#include "HeroBanner.h"
#include "HomeKit.h"
#include "LucideIcons.h"
#include "TemplateArt.h"
#include "TemplateMarket.h"

#include "core/application/RecentFilesManager.h"
#include "core/application/UpdateChecker.h"
#include "core/auth/AuthManager.h"
#include "core/theme/ThemeManager.h"
#include "common/Avatars.h"
#include "settings/SettingsTray.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QRadialGradient>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>
#include <utility>

namespace NativeOffice {

namespace {

// ── Brand mark ───────────────────────────────────────────────────────────────
// The source artwork is the full lockup (mark + wordmark + strapline); crop out
// just the "N" mark, else the baked-in text turns to mush at UI sizes.
QLabel* logoMark(int h, QWidget* parent) {
    auto* l = new QLabel(parent);
    const qreal dpr = parent ? parent->devicePixelRatio() : 1.0;
    QPixmap src(":/assets/nativeoffice-logo-mark.png");
    if (!src.isNull()) {
        src = src.copy(QRect(int(src.width()  * 0.205),
                             int(src.height() * 0.115),
                             int(src.width()  * 0.600),
                             int(src.height() * 0.555)));
    }
    QPixmap scaled = src.scaledToHeight(int(h * dpr), Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    l->setPixmap(scaled);
    l->setStyleSheet("background:transparent;");
    l->setFixedSize(scaled.deviceIndependentSize().toSize());
    return l;
}

// A rounded tile carrying a Lucide glyph rather than a letter, used where a
// single letter would be wrong (PDF, Open File).
QLabel* iconTile(const char* svg, const QString& colorHex, int size, QWidget* parent) {
    const qreal dpr = parent ? parent->devicePixelRatio() : 1.0;
    QPixmap pm = badgePixmap(QString(), colorHex, size, dpr);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const int glyph = int(size * 0.54);
    p.drawPixmap(QPointF((size - glyph) / 2.0, (size - glyph) / 2.0),
                 Lucide::pixmap(svg, QStringLiteral("#FFFFFF"), glyph, dpr));
    p.end();
    auto* l = new QLabel(parent);
    l->setPixmap(pm);
    l->setFixedSize(size, size);
    l->setStyleSheet("background:transparent;");
    l->setAttribute(Qt::WA_TransparentForMouseEvents);
    return l;
}

// A create tile: the supplied 3D render cover-cropped across the whole card,
// its name and one line of description over a scrim at the foot, and a round
// arrow in the corner. Painted rather than assembled from child widgets, so
// the artwork stays crisp at any size and the text sits on the picture instead
// of in a strip beneath it.
class CreateCard : public QFrame {
public:
    CreateCard(const QString& art, const QString& title, const QString& sub,
               const QString& accent, QWidget* parent)
        : QFrame(parent), m_art(art), m_title(title), m_sub(sub), m_accent(accent) {
        setMinimumHeight(146);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
    }

    std::function<void()> onClick;

protected:
    void enterEvent(QEnterEvent*) override { m_hover = true;  update(); }
    void leaveEvent(QEvent*) override      { m_hover = false; update(); }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && onClick) { onClick(); e->accept(); return; }
        QFrame::mousePressEvent(e);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setRenderHint(QPainter::TextAntialiasing);

        const QRectF box(0, 0, width(), height());
        QPainterPath clip;
        clip.addRoundedRect(box, 15, 15);
        p.setClipPath(clip);
        p.fillRect(box, QColor(Home::kPanel));

        // Cover-crop, cached: a repaint happens on every hover.
        const QSize want = size() * devicePixelRatio();
        if (m_scaled.isNull() || m_scaled.size() != want) {
            const QPixmap src(m_art);
            if (!src.isNull()) {
                m_scaled = src.scaled(want, Qt::KeepAspectRatioByExpanding,
                                      Qt::SmoothTransformation);
                m_scaled.setDevicePixelRatio(devicePixelRatio());
            }
        }
        if (!m_scaled.isNull()) {
            const QSizeF drawn = m_scaled.deviceIndependentSize();
            p.drawPixmap(QPointF((width() - drawn.width()) / 2.0,
                                 (height() - drawn.height()) / 2.0), m_scaled);
        }

        // Scrim under the text. The renders are dark at the foot already, so
        // this only has to guarantee it rather than create it.
        QLinearGradient scrim(0, height() * 0.42, 0, height());
        QColor dark(6, 8, 14);
        dark.setAlphaF(0.0);  scrim.setColorAt(0.0, dark);
        dark.setAlphaF(0.62); scrim.setColorAt(0.62, dark);
        dark.setAlphaF(0.88); scrim.setColorAt(1.0, dark);
        p.fillRect(box, scrim);

        // A wash of the module colour, stronger under the pointer.
        QLinearGradient tint(0, 0, width(), height());
        QColor a(m_accent);
        a.setAlphaF(m_hover ? 0.20 : 0.10); tint.setColorAt(0.0, a);
        a.setAlphaF(0.0);                   tint.setColorAt(0.75, a);
        p.fillRect(box, tint);

        // ── Text ────────────────────────────────────────────────────────────
        const qreal pad = 14;
        QFont ft(QStringLiteral("Segoe UI"));
        ft.setPixelSize(15);
        ft.setWeight(QFont::DemiBold);
        p.setFont(ft);
        p.setPen(QColor(0xF2, 0xF5, 0xFA));
        const QFontMetrics fm(ft);
        const qreal titleY = height() - pad - 20;
        p.drawText(QRectF(pad, titleY - fm.ascent(), width() - pad * 2, fm.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, m_title);

        QFont fs(QStringLiteral("Segoe UI"));
        fs.setPixelSize(11);
        p.setFont(fs);
        p.setPen(QColor(0xA9, 0xB3, 0xC6));
        p.drawText(QRectF(pad, height() - pad - 15, width() - pad * 2 - 26, 15),
                   Qt::AlignLeft | Qt::AlignVCenter, m_sub);

        // ── Arrow ───────────────────────────────────────────────────────────
        const qreal r = 13;
        const QPointF c(width() - pad - r, height() - pad - r);
        p.setPen(QPen(QColor(255, 255, 255, m_hover ? 70 : 40), 1));
        p.setBrush(QColor(255, 255, 255, m_hover ? 32 : 18));
        p.drawEllipse(c, r, r);
        p.drawPixmap(QPointF(c.x() - 7, c.y() - 7),
                     Lucide::pixmap(Lucide::kArrowRight,
                                    m_hover ? QStringLiteral("#FFFFFF")
                                            : QStringLiteral("#D2D9E6"),
                                    14, devicePixelRatio()));

        // ── Frame ───────────────────────────────────────────────────────────
        p.setClipping(false);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(m_hover ? QColor(m_accent) : QColor(Home::kBorder), m_hover ? 1.5 : 1));
        p.drawRoundedRect(box.adjusted(0.75, 0.75, -0.75, -0.75), 15, 15);
    }

private:
    QString m_art, m_title, m_sub, m_accent;
    QPixmap m_scaled;
    bool    m_hover { false };
};

// A card that is nothing but a picture: rounded, cover-cropped, and lit at the
// edge under the pointer. The template cards and the assistant card are both
// finished artwork, so there is no text to lay over them.
class ArtCard : public QFrame {
public:
    ArtCard(const QString& art, int radius, QWidget* parent)
        : QFrame(parent), m_art(art), m_radius(radius) {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        setMinimumSize(60, 40);
    }

    std::function<void()> onClick;

protected:
    void enterEvent(QEnterEvent*) override { m_hover = true;  update(); }
    void leaveEvent(QEvent*) override      { m_hover = false; update(); }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && onClick) { onClick(); e->accept(); return; }
        QFrame::mousePressEvent(e);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        const QRectF box(0, 0, width(), height());
        QPainterPath clip;
        clip.addRoundedRect(box, m_radius, m_radius);
        p.setClipPath(clip);
        p.fillRect(box, QColor(Home::kPanelSoft));

        const QSize want = size() * devicePixelRatio();
        if (m_scaled.isNull() || m_scaled.size() != want) {
            const QPixmap src(m_art);
            if (!src.isNull()) {
                // Fit, not fill: the artwork carries its own title, and filling
                // cropped the words off the side of it.
                m_scaled = src.scaled(want, Qt::KeepAspectRatio,
                                      Qt::SmoothTransformation);
                m_scaled.setDevicePixelRatio(devicePixelRatio());
            }
        }
        if (!m_scaled.isNull()) {
            const QSizeF drawn = m_scaled.deviceIndependentSize();
            p.drawPixmap(QPointF((width() - drawn.width()) / 2.0,
                                 (height() - drawn.height()) / 2.0), m_scaled);
        }

        if (m_hover) {
            QColor wash(Home::kAccent);
            wash.setAlphaF(0.12);
            p.fillRect(box, wash);
        }

        p.setClipping(false);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(m_hover ? QColor(Home::kAccentSoft) : QColor(Home::kBorder),
                      m_hover ? 1.6 : 1));
        p.drawRoundedRect(box.adjusted(0.8, 0.8, -0.8, -0.8), m_radius, m_radius);
    }

private:
    QString m_art;
    QPixmap m_scaled;
    int     m_radius { 12 };
    bool    m_hover  { false };
};

// ── Quick tips ───────────────────────────────────────────────────────────────
struct Tip { QString body; };

QVector<Tip> quickTips() {
    // Rich text so a shortcut can be highlighted inside the sentence.
    auto key = [](const QString& k) {
        return QStringLiteral("<span style='color:%1;font-weight:600;'>%2</span>")
            .arg(Home::kAccentSoft, k);
    };
    return {
        { QStringLiteral("Press %1 to search every document on this computer without "
                         "leaving Home.").arg(key(QStringLiteral("Ctrl + K"))) },
        { QStringLiteral("%1 moves between open tabs, and %2 reopens the last tab you "
                         "closed.").arg(key(QStringLiteral("Ctrl + Tab")),
                                        key(QStringLiteral("Ctrl + Shift + T"))) },
        { QStringLiteral("NativeOffice saves as you work, so there is no Save button to "
                         "remember. Use %1 only when you want to choose the folder.")
              .arg(key(QStringLiteral("Save As"))) },
        { QStringLiteral("Open File reads .docx, .xlsx, .pptx, .csv, .pdf and .md, and "
                         "picks the right editor for each one on its own.") },
        { QStringLiteral("Right-click any recent file to star it, show it in Explorer, or "
                         "take it off the list.") },
        { QStringLiteral("%1 exports whatever you are working on straight to PDF, and %2 "
                         "runs a real spell check in Writer.")
              .arg(key(QStringLiteral("Ctrl + Shift + E")), key(QStringLiteral("F7"))) },
    };
}

// A card that cycles through the tips on a timer, with clickable dots.
class QuickTipsCard : public QFrame {
public:
    explicit QuickTipsCard(QWidget* parent) : QFrame(parent) {
        setObjectName("sidePanel");
        m_tips = quickTips();

        auto* v = new QVBoxLayout(this);
        v->setContentsMargins(14, 9, 14, 7);
        v->setSpacing(5);

        auto* head = new QHBoxLayout();
        head->addWidget(label600(QStringLiteral("Quick Tips"), 14, Home::kText, this));
        head->addStretch();
        auto* close = new QToolButton(this);
        close->setObjectName("tipClose");
        close->setCursor(Qt::PointingHandCursor);
        close->setFixedSize(20, 20);
        close->setIcon(Lucide::icon(Lucide::kX, Home::kFaint, 13, devicePixelRatio()));
        close->setToolTip(QStringLiteral("Hide tips for now"));
        connect(close, &QToolButton::clicked, this, [this] { hide(); });
        head->addWidget(close);
        v->addLayout(head);

        auto* body = new QHBoxLayout();
        body->setSpacing(12);
        auto* bulb = new QLabel(this);
        bulb->setFixedSize(30, 30);
        bulb->setAlignment(Qt::AlignCenter);
        bulb->setPixmap(Lucide::pixmap(Lucide::kLightbulb, Home::kAmber, 17,
                                       devicePixelRatio()));
        bulb->setStyleSheet(QString("background:rgba(245,165,36,0.13);border-radius:10px;"));
        body->addWidget(bulb, 0, Qt::AlignTop);

        m_text = new QLabel(this);
        m_text->setTextFormat(Qt::RichText);
        m_text->setWordWrap(true);
        m_text->setMinimumHeight(32);
        m_text->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        m_text->setStyleSheet(QString("color:%1;font:12px 'Segoe UI';background:transparent;")
                                  .arg(Home::kTextBody));
        body->addWidget(m_text, 1);
        v->addLayout(body);

        auto* dots = new QHBoxLayout();
        dots->setSpacing(6);
        dots->addStretch();
        for (int i = 0; i < m_tips.size(); ++i) {
            auto* d = new ClickableFrame(this);
            d->setFixedSize(6, 6);
            d->setCursor(Qt::PointingHandCursor);
            d->onClick = [this, i] { showTip(i); m_timer->start(); };
            m_dots.append(d);
            dots->addWidget(d);
        }
        dots->addStretch();
        v->addLayout(dots);

        m_timer = new QTimer(this);
        m_timer->setInterval(5'000);
        connect(m_timer, &QTimer::timeout, this,
                [this] { showTip((m_index + 1) % m_tips.size()); });
        m_timer->start();
        showTip(0);

        setStyleSheet(QString(R"(
            QFrame#sidePanel { background:%1; border:1px solid %2; border-radius:14px; }
            QToolButton#tipClose { background:transparent; border:none; }
            QToolButton#tipClose:hover { background:%3; border-radius:6px; }
        )").arg(Home::kPanel, Home::kBorder, Home::kPanelHover));
    }

private:
    void showTip(int index) {
        m_index = index;
        m_text->setText(m_tips[index].body);
        for (int i = 0; i < m_dots.size(); ++i)
            m_dots[i]->setStyleSheet(
                QString("background:%1;border-radius:3px;")
                    .arg(i == index ? Home::kAccent : QStringLiteral("#2C3346")));
    }

    QVector<Tip>              m_tips;
    QLabel*                   m_text  { nullptr };
    QVector<ClickableFrame*>  m_dots;
    QTimer*                   m_timer { nullptr };
    int                       m_index { 0 };
};

// The glowing orb on the AI card, painted rather than shipped as an image.
class AiOrb : public QWidget {
public:
    explicit AiOrb(QWidget* parent) : QWidget(parent) {
        setFixedSize(94, 94);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF c(width() / 2.0, height() / 2.0);
        const qreal r = width() * 0.30;

        // Outer glow.
        QRadialGradient glow(c, width() / 2.0);
        QColor g(Home::kAccent);
        g.setAlphaF(0.38); glow.setColorAt(0.0, g);
        g.setAlphaF(0.10); glow.setColorAt(0.55, g);
        g.setAlphaF(0.0);  glow.setColorAt(1.0, g);
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(rect());

        // Orbit rings.
        p.setBrush(Qt::NoBrush);
        for (int i = 0; i < 2; ++i) {
            QColor ring(Home::kAccentSoft);
            ring.setAlphaF(0.42 - 0.16 * i);
            p.setPen(QPen(ring, 1.4));
            p.save();
            p.translate(c);
            p.rotate(i == 0 ? -22 : 34);
            p.drawEllipse(QRectF(-r * 1.72, -r * 0.55, r * 3.44, r * 1.10));
            p.restore();
        }

        // The sphere.
        QRadialGradient body(QPointF(c.x() - r * 0.35, c.y() - r * 0.4), r * 1.8);
        body.setColorAt(0.0, QColor(0xC9, 0xBC, 0xFF));
        body.setColorAt(0.5, QColor(Home::kAccent));
        body.setColorAt(1.0, QColor(0x38, 0x2C, 0x78));
        p.setPen(Qt::NoPen);
        p.setBrush(body);
        p.drawEllipse(c, r, r);

        // Specular highlight.
        QRadialGradient spec(QPointF(c.x() - r * 0.38, c.y() - r * 0.46), r * 0.7);
        QColor white(255, 255, 255);
        white.setAlphaF(0.55); spec.setColorAt(0.0, white);
        white.setAlphaF(0.0);  spec.setColorAt(1.0, white);
        p.setBrush(spec);
        p.drawEllipse(QPointF(c.x() - r * 0.3, c.y() - r * 0.36), r * 0.55, r * 0.45);

        // Sparks.
        p.setBrush(QColor(0xE4, 0xDD, 0xFF));
        p.drawEllipse(QPointF(c.x() + r * 1.55, c.y() - r * 0.85), 2.2, 2.2);
        p.drawEllipse(QPointF(c.x() - r * 1.62, c.y() + r * 0.72), 1.7, 1.7);
        p.drawEllipse(QPointF(c.x() + r * 1.15, c.y() + r * 1.32), 1.4, 1.4);
    }
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
StartScreen::StartScreen(AppController* controller, QWidget* parent)
    : QWidget(parent), m_controller(controller) {
    setObjectName("startScreen");
    buildUi();
}

bool StartScreen::launchLocked() const {
    return UpdateChecker::instance().isScanning();
}

// ── Update pill ──────────────────────────────────────────────────────────────
QWidget* StartScreen::buildUpdateBanner() {
    auto* pill = new ClickableFrame(this);
    pill->setObjectName("updatePill");
    m_updateBanner = pill;
    pill->setFixedHeight(28);
    auto* h = new QHBoxLayout(pill);
    h->setContentsMargins(11, 0, 11, 0);
    h->setSpacing(7);

    m_updateSpin = new QLabel(pill);
    m_updateSpin->setFixedWidth(12);
    h->addWidget(m_updateSpin);

    m_updateText = new QLabel(pill);
    h->addWidget(m_updateText);

    m_spinTimer = new QTimer(this);
    connect(m_spinTimer, &QTimer::timeout, this, [this] {
        static const char* frames[] = { "◜", "◝", "◞", "◟" };
        m_updateSpin->setText(QString::fromUtf8(frames[m_spinPhase++ & 3]));
    });

    auto& u = UpdateChecker::instance();
    connect(&u, &UpdateChecker::stateChanged, this, [this] { refreshUpdateBanner(); });
    connect(&u, &UpdateChecker::downloadProgress, this, [this](int pct) {
        if (m_updateText) m_updateText->setText(tr("Installing update  %1%").arg(pct));
    });

    refreshUpdateBanner();
    return pill;
}

void StartScreen::refreshUpdateBanner() {
    if (!m_updateBanner) return;
    using S = UpdateChecker::State;
    const S s = UpdateChecker::instance().state();
    auto* pill = static_cast<ClickableFrame*>(m_updateBanner);

    const bool show = s != S::Idle && s != S::Failed;
    m_updateBanner->setVisible(show);
    if (!show) { m_spinTimer->stop(); pill->onClick = nullptr; return; }

    QString text;
    switch (s) {
    case S::Scanning:        text = tr("Checking updates"); break;
    case S::UpToDate:        text = tr("Up to date"); break;
    case S::Offline:         text = tr("Offline"); break;
    case S::UpdateAvailable: text = tr("Preparing update"); break;
    case S::Downloading:     text = tr("Installing update"); break;
    case S::ReadyToRestart:  text = tr("Restart to update"); break;
    default: break;
    }
    m_updateText->setText(text);

    const bool ready = s == S::ReadyToRestart;
    if (ready) {
        pill->onClick = [] { UpdateChecker::instance().relaunchForUpdate(); };
        pill->setStyleSheet(QString("QFrame#updatePill { background:%1;"
                                    " border:1px solid %2; border-radius:14px; }")
                                .arg(Home::kAccent, Home::kAccentSoft));
        m_updateText->setStyleSheet("background:transparent;color:#FFFFFF;"
                                    "font:700 11px 'Segoe UI';");
    } else {
        pill->onClick = nullptr;
        pill->setCursor(Qt::ArrowCursor);
        pill->setStyleSheet(QString("QFrame#updatePill { background:%1;"
                                    " border:1px solid %2; border-radius:14px; }")
                                .arg(Home::kPanelSoft, Home::kBorder));
        m_updateText->setStyleSheet(QString("background:transparent;color:%1;"
                                            "font:600 11px 'Segoe UI';").arg(Home::kMuted));
    }

    const bool spinning = s == S::Scanning || s == S::UpdateAvailable || s == S::Downloading;
    m_updateSpin->setStyleSheet(QString("background:transparent;color:%1;"
                                        "font:700 11px 'Segoe UI';").arg(Home::kMuted));
    if (spinning) {
        m_updateSpin->show();
        if (!m_spinTimer->isActive()) m_spinTimer->start(90);
    } else {
        m_spinTimer->stop();
        m_updateSpin->hide();
        m_updateSpin->clear();
    }
    m_updateBanner->adjustSize();
}

void StartScreen::openFileDialog() {
    if (launchLocked()) return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open File"), QDir::homePath(), supportedFilesFilter());
    if (!path.isEmpty()) emit fileOpenRequested(path);
}

// ─────────────────────────────────────────────────────────────────────────────
void StartScreen::buildUi() {
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildSidebar());

    auto* right = new QWidget(this);
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);
    rl->addWidget(buildTopBar());

    auto* scroll = new QScrollArea(right);
    scroll->setObjectName("bodyScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* body = new QWidget(scroll);
    body->setObjectName("bodyPane");
    auto* bl = new QHBoxLayout(body);
    bl->setContentsMargins(26, 10, 24, 4);
    bl->setSpacing(18);
    bl->addWidget(buildCenterColumn(), 1);
    bl->addWidget(buildRightColumn());
    scroll->setWidget(body);

    rl->addWidget(scroll, 1);
    rl->addWidget(buildBottomBar());
    root->addWidget(right, 1);

    setStyleSheet(QString(R"(
        QWidget#startScreen { background:%1; }
        QWidget#bodyPane { background:transparent; }
        QScrollArea#bodyScroll { background:transparent; }
        QScrollArea#bodyScroll QScrollBar:vertical { background:transparent;
            width:10px; margin:2px; border:none; }
        QScrollArea#bodyScroll QScrollBar::handle:vertical { background:#2A3244;
            border:none; border-radius:5px; min-height:30px; }
        QScrollArea#bodyScroll QScrollBar::add-line:vertical,
        QScrollArea#bodyScroll QScrollBar::sub-line:vertical { height:0; border:none;
            background:transparent; }
        QScrollArea#bodyScroll QScrollBar::add-page:vertical,
        QScrollArea#bodyScroll QScrollBar::sub-page:vertical { background:transparent; }
    )").arg(Home::kBg));

    // Dev-only capture hook (PrintWindow drops pixmap content on this machine):
    // set NATIVEOFFICE_HOME_GRAB to a .png path to have the home screen grab
    // itself shortly after startup.
    if (qEnvironmentVariableIsSet("NATIVEOFFICE_HOME_GRAB")) {
        const QString grabPath = qEnvironmentVariable("NATIVEOFFICE_HOME_GRAB");
        // NATIVEOFFICE_HOME_SHOW names a surface to open before the grab, so a
        // review can see the trays, popups and dialogs offscreen rather than
        // only the page behind them.
        const QString show = qEnvironmentVariable("NATIVEOFFICE_HOME_SHOW");
        const bool grabSettings = qEnvironmentVariableIsSet("NATIVEOFFICE_OPEN_SETTINGS")
                                  || !show.isEmpty();
        if (!show.isEmpty()) {
            QTimer::singleShot(4500, this, [this, show] {
                if      (show == QLatin1String("profile"))   showProfileTray();
                else if (show == QLatin1String("settings"))  showSettingsDialog();
                else if (show == QLatin1String("templates")) showTemplateMarket(0);
                else if (show == QLatin1String("shortcuts")) showShortcutsDialog();
                else if (show == QLatin1String("whatsnew"))  showWhatsNewDialog();
                else if (show == QLatin1String("activity")) {
                    auto* win = new ActivityWindow(ActivityLog::Range::Week, window());
                    win->setAttribute(Qt::WA_DeleteOnClose);
                    win->setModal(true);
                    win->show();
                } else if (show == QLatin1String("notifications")) {
                    showNotificationsPopup(m_search);
                } else if (show == QLatin1String("ai")) {
                    emit aiRequested();
                } else if (show.startsWith(QLatin1String("tpl:"))) {
                    const QString which = show.mid(4);
                    if (which == QLatin1String("resume"))
                        emit templateChosen(DocumentType::Writer,
                                            QStringLiteral("Professional Resume"));
                    else if (which == QLatin1String("budget"))
                        emit templateChosen(DocumentType::Calc,
                                            QStringLiteral("Monthly Budget"));
                    else if (which == QLatin1String("pitch"))
                        emit templateChosen(DocumentType::Impress,
                                            QStringLiteral("Pitch Deck"));
                    else
                        emit templateChosen(DocumentType::Writer,
                                            QStringLiteral("Project Report"));
                } else if (show == QLatin1String("createmenu")) {
                    m_hero->openCreateMenu();
                } else if (show == QLatin1String("search")) {
                    m_search->setFocus();
                    m_search->setText(qEnvironmentVariable("NATIVEOFFICE_HOME_QUERY",
                                                           QStringLiteral("doc")));
                }
                else if (show.startsWith(QLatin1String("tool:"))) {
                    const QString which = show.mid(5);
                    const Tool t = which == QLatin1String("qr")       ? Tool::QrCode
                                 : which == QLatin1String("compress") ? Tool::CompressPdf
                                 : which == QLatin1String("ocr")      ? Tool::Ocr
                                 : which == QLatin1String("toword")   ? Tool::PdfToWord
                                 : which == QLatin1String("image")    ? Tool::ImageResizer
                                                                      : Tool::MarkdownEditor;
                    emit toolRequested(t);
                }
            });
        } else if (grabSettings) {
            QTimer::singleShot(4500, this, [this] { showSettingsDialog(); });
        }
        // NATIVEOFFICE_HOME_GRAB_FULL captures the scrolled body at its natural
        // height instead of the visible slice, so a review can see the panels
        // that sit below the fold on a short screen.
        const bool grabFull = qEnvironmentVariableIsSet("NATIVEOFFICE_HOME_GRAB_FULL");
        QTimer::singleShot(6000, this, [this, grabPath, grabSettings, grabFull, scroll] {
            // A dialog opened by NATIVEOFFICE_HOME_SHOW is its own top-level
            // window, so grabbing the main window would capture the page behind
            // it. Prefer whatever modal is actually in front.
            QWidget* target = QApplication::activePopupWidget();
            if (!target) target = QApplication::activeModalWidget();
            if (!target) {
                target = grabSettings ? window()
                       : grabFull     ? scroll->widget()
                                      : static_cast<QWidget*>(this);
            }
            target->grab().save(grabPath, "PNG");
            if (scroll->widget()) {
                qInfo("[home] viewport %d, content %d, overflow %d",
                      scroll->viewport()->height(), scroll->widget()->height(),
                      scroll->widget()->height() - scroll->viewport()->height());
            }
            if (m_searchPopup) {
                // A Qt::Popup takes a keyboard grab, which is what limited the
                // search box to one character. It must be an ordinary child.
                qInfo("[home] search list isWindow=%d, box has focus=%d",
                      int(m_searchPopup->isWindow()), int(m_search->hasFocus()));
            }
        });
    }
}

void StartScreen::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (m_searchPopup && m_searchPopup->isVisible()) m_searchPopup->showUnder();

    // The Stasis sidebar takes its width out of the page, and a splitter cannot
    // shrink a child below its minimum: the page was simply clipped, so the
    // panel looked like it sat on top of Home rather than beside it. Below this
    // width the right column steps aside instead of being cut in half.
    constexpr int kDropRightColumn = 1280;
    if (m_rightColumn)  m_rightColumn->setVisible(width() >= kDropRightColumn);
    // The footer sheds its decoration before anything gets clipped.
    if (m_trustPrivate) m_trustPrivate->setVisible(width() >= 980);
}

bool StartScreen::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_search && event->type() == QEvent::KeyPress && m_searchPopup
        && m_searchPopup->isVisible()) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Down || key == Qt::Key_Up || key == Qt::Key_Escape)
            return m_searchPopup->handleKey(key);
    }
    if (watched == m_search && m_searchPopup) {
        if (event->type() == QEvent::FocusIn) FileIndex::instance().ensureStarted();
        // Now that the list is an ordinary child widget it does not close
        // itself, so it follows the search box's focus.
        if (event->type() == QEvent::FocusOut) m_searchPopup->hide();
    }
    return QWidget::eventFilter(watched, event);
}

// ── Left sidebar ─────────────────────────────────────────────────────────────
QWidget* StartScreen::buildSidebar() {
    auto* bar = new QWidget(this);
    bar->setObjectName("sidebar");
    bar->setFixedWidth(236);
    auto* v = new QVBoxLayout(bar);
    v->setContentsMargins(14, 18, 14, 14);
    v->setSpacing(3);

    // ── Brand lockup ────────────────────────────────────────────────────────
    auto* logoRow = new QWidget(bar);
    auto* lh = new QHBoxLayout(logoRow);
    lh->setContentsMargins(6, 0, 0, 12);
    lh->setSpacing(11);
    lh->addWidget(logoMark(38, logoRow), 0, Qt::AlignVCenter);
    auto* words = new QVBoxLayout();
    words->setSpacing(1);
    auto* brand = new QLabel(logoRow);
    brand->setTextFormat(Qt::RichText);
    brand->setText(QStringLiteral("<span style='color:%1;'>Native</span>"
                                  "<span style='color:%2;'>Office</span>")
                       .arg(Home::kText, Home::kAccentSoft));
    brand->setStyleSheet("background:transparent; font:700 18px 'Segoe UI';");
    words->addWidget(brand);
    words->addWidget(heading(tr("Work native. Create freely."), 10, Home::kFaint, false, logoRow));
    lh->addLayout(words);
    lh->addStretch();
    v->addWidget(logoRow);

    struct Nav { const char* icon; QString label; int action; };
    const Nav primary[] = {
        { Lucide::kHome,         tr("Home"),      0 },
        { Lucide::kFileText,     tr("Documents"), 1 },
        { Lucide::kTable,        tr("Sheets"),    2 },
        { Lucide::kPresentation, tr("Slides"),    3 },
        { Lucide::kDownload,     tr("PDF"),       5 },
    };
    const Nav secondary[] = {
        { Lucide::kFolderOpen, tr("Templates"),   4 },
        { Lucide::kTrash,      tr("Recycle Bin"), 7 },
    };

    auto addNav = [&](const Nav& n) {
        auto* item = new ClickableFrame(bar);
        const bool active = n.action == 0;
        item->setObjectName(active ? "navItemActive" : "navItem");
        auto* il = new QHBoxLayout(item);
        il->setContentsMargins(12, 0, 12, 0);
        il->setSpacing(12);
        item->setFixedHeight(40);
        il->addWidget(Lucide::label(n.icon, active ? "#FFFFFF" : Home::kMuted, 16, item));
        auto* tx = new QLabel(n.label, item);
        tx->setStyleSheet(QString("background:transparent;font:%1 13px 'Segoe UI';color:%2;")
                              .arg(active ? "600" : "400", active ? "#FFFFFF" : Home::kTextBody));
        tx->setAttribute(Qt::WA_TransparentForMouseEvents);
        il->addWidget(tx);
        il->addStretch();
        if (active) {
            auto* dot = new QLabel(item);
            dot->setFixedSize(6, 6);
            dot->setStyleSheet(QString("background:%1;border-radius:3px;").arg(Home::kAccentSoft));
            il->addWidget(dot);
        }
        const int action = n.action;
        item->onClick = [this, action] {
            if (action != 0 && action != 7 && launchLocked()) return;
            switch (action) {
            case 1: emit newDocumentRequested(DocumentType::Writer);  break;
            case 2: emit newDocumentRequested(DocumentType::Calc);    break;
            case 3: emit newDocumentRequested(DocumentType::Impress); break;
            case 4: showTemplateMarket(0); break;
            case 5: emit newDocumentRequested(DocumentType::Pdf);     break;
            case 7: openRecycleBin(); break;
            default: break;
            }
        };
        v->addWidget(item);
    };

    for (const Nav& n : primary) addNav(n);

    auto* rule = new QFrame(bar);
    rule->setFixedHeight(1);
    rule->setStyleSheet(QString("background:%1;border:none;").arg(Home::kBorderSoft));
    v->addSpacing(10);
    v->addWidget(rule);
    v->addSpacing(10);

    for (const Nav& n : secondary) addNav(n);
    v->addStretch();

    // ── Public-beta notice ──────────────────────────────────────────────────
    {
        auto* betaRow = new QWidget(bar);
        auto* bh = new QHBoxLayout(betaRow);
        bh->setContentsMargins(6, 0, 4, 6);
        bh->setSpacing(8);
        auto* pill = new QLabel(QStringLiteral("BETA"), betaRow);
        pill->setAlignment(Qt::AlignCenter);
        pill->setFixedSize(48, 20);
        pill->setStyleSheet("background:#E5484D;color:#FFFFFF;border-radius:6px;"
                            "font:700 10px 'Segoe UI';letter-spacing:1px;");
        bh->addWidget(pill);
        auto* note = new QLabel(betaRow);
        note->setTextFormat(Qt::RichText);
        note->setOpenExternalLinks(true);
        note->setText(QStringLiteral("<a href='mailto:contact@nativeoffice.online' "
                                     "style='color:%1;text-decoration:none;'>Report an issue</a>")
                          .arg(Home::kFaint));
        note->setStyleSheet("font:11px 'Segoe UI';background:transparent;");
        bh->addWidget(note);
        bh->addStretch();
        v->addWidget(betaRow);
    }

    // ── Account row ─────────────────────────────────────────────────────────
    // Replaces the old storage meter and the always-there upgrade card: the
    // plan lives behind this row, which is also where the account lives.
    {
        auto* card = new ClickableFrame(bar);
        card->setObjectName("accountRow");
        card->setFixedHeight(58);
        card->setCursor(Qt::PointingHandCursor);
        auto* ah = new QHBoxLayout(card);
        ah->setContentsMargins(10, 0, 10, 0);
        ah->setSpacing(10);

        auto* pic = new QLabel(card);
        pic->setFixedSize(34, 34);
        pic->setAttribute(Qt::WA_TransparentForMouseEvents);
        pic->setStyleSheet("background:transparent;");
        ah->addWidget(pic);

        auto* col = new QVBoxLayout();
        col->setSpacing(1);
        auto* who  = label600(QString(), 12, Home::kText, card);
        auto* plan = heading(QString(), 11, Home::kFaint, false, card);
        who->setAttribute(Qt::WA_TransparentForMouseEvents);
        plan->setAttribute(Qt::WA_TransparentForMouseEvents);
        col->addWidget(who);
        col->addWidget(plan);
        ah->addLayout(col, 1);
        ah->addWidget(Lucide::label(Lucide::kChevronDown, Home::kFaint, 14, card));

        auto refresh = [pic, who, plan, card] {
            auto& auth = AuthManager::instance();
            pic->setPixmap(roundAvatarPixmap(34, card->devicePixelRatio()));
            QString name = auth.displayName();
            if (name.isEmpty()) name = QStringLiteral("Signed out");
            who->setText(name);
            plan->setText(auth.premiumActive() ? auth.premiumPlanLabel()
                                               : QStringLiteral("Free Plan"));
        };
        refresh();
        connect(&AuthManager::instance(), &AuthManager::profileChanged, card, refresh);
        card->onClick = [this, card] { showPlanPopup(card); };
        v->addWidget(card);
    }

    {
        auto* ver = new QLabel(tr("Version ") + QCoreApplication::applicationVersion(), bar);
        ver->setStyleSheet(QString("color:%1;font:10px 'Segoe UI';background:transparent;"
                                   "padding:7px 6px 0 6px;").arg(Home::kFaint));
        v->addWidget(ver, 0, Qt::AlignLeft);
    }

    bar->setStyleSheet(QString(R"(
        QWidget#sidebar { background:%1; border-right:1px solid %2; }
        #navItem { background:transparent; border-radius:9px; }
        #navItem:hover { background:%3; }
        #navItemActive { background:#211E3A; border-radius:9px; }
        #accountRow { background:%4; border:1px solid %2; border-radius:12px; }
        #accountRow:hover { background:%3; }
    )").arg(Home::kSidebar, Home::kBorderSoft, Home::kPanelHover, Home::kPanel));
    return bar;
}

// ── Top bar ──────────────────────────────────────────────────────────────────
QWidget* StartScreen::buildTopBar() {
    auto* bar = new QWidget(this);
    bar->setObjectName("topBar");
    bar->setFixedHeight(66);
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(26, 0, 22, 0);
    h->setSpacing(16);

    // Equal minimum widths give both halves identical size hints, so the equal
    // stretch factors keep the clock on the bar's true midline.
    auto* leftBox = new QWidget(bar);
    leftBox->setMinimumWidth(432);
    auto* leftL = new QHBoxLayout(leftBox);
    leftL->setContentsMargins(0, 0, 0, 0);
    leftL->setSpacing(16);

    auto* rightBox = new QWidget(bar);
    rightBox->setMinimumWidth(432);
    auto* rightL = new QHBoxLayout(rightBox);
    rightL->setContentsMargins(0, 0, 0, 0);
    rightL->setSpacing(10);

    // ── Search ──────────────────────────────────────────────────────────────
    m_search = new QLineEdit(bar);
    m_search->setObjectName("searchBox");
    m_search->setPlaceholderText(tr("Search files, templates, tools…"));
    m_search->setFixedHeight(40);
    m_search->setFixedWidth(360);
    m_search->setTextMargins(30, 0, 62, 0);

    auto* glass = Lucide::label(Lucide::kSearch, Home::kMuted, 15, m_search);
    glass->move(11, 12);
    auto* kbd = new QLabel(QStringLiteral("Ctrl + K"), m_search);
    kbd->setStyleSheet(QString("background:%1;border-radius:5px;color:%2;"
                               "font:10px 'Segoe UI';padding:3px 7px;")
                           .arg(Home::kPanelSoft, Home::kMuted));
    kbd->setAttribute(Qt::WA_TransparentForMouseEvents);
    kbd->adjustSize();

    m_searchPopup = new SearchPopup(m_search, this);
    connect(m_searchPopup, &SearchPopup::fileChosen, this, [this](const QString& path) {
        m_search->clear();
        if (!launchLocked()) emit fileOpenRequested(path);
    });
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (!m_searchPopup) return;
        if (text.trimmed().isEmpty() && !m_searchPopup->isVisible()) return;
        if (!m_searchPopup->isVisible()) m_searchPopup->showUnder();
        m_searchPopup->setQuery(text);
    });
    // Arrow keys and Enter belong to the result list while it is open.
    m_search->installEventFilter(this);
    connect(m_search, &QLineEdit::returnPressed, this, [this] {
        if (m_searchPopup) m_searchPopup->handleKey(Qt::Key_Return);
    });

    leftL->addWidget(m_search);
    leftL->addStretch();
    h->addWidget(leftBox, 1);

    // ── Date over time, as in the reference ─────────────────────────────────
    auto* clockBox = new QWidget(bar);
    auto* cv = new QVBoxLayout(clockBox);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(1);
    auto* dateLabel = new QLabel(clockBox);
    dateLabel->setAlignment(Qt::AlignCenter);
    dateLabel->setStyleSheet(QString("background:transparent;color:%1;"
                                     "font:12px 'Segoe UI';").arg(Home::kMuted));
    auto* timeLabel = new QLabel(clockBox);
    timeLabel->setAlignment(Qt::AlignCenter);
    timeLabel->setStyleSheet(QString("background:transparent;color:%1;"
                                     "font:600 19px 'Segoe UI';").arg(Home::kText));
    cv->addWidget(dateLabel);
    cv->addWidget(timeLabel);
    auto refreshClock = [dateLabel, timeLabel] {
        const QDateTime now = QDateTime::currentDateTime();
        dateLabel->setText(now.toString(QStringLiteral("dddd, MMM d")));
        timeLabel->setText(now.toString(QStringLiteral("h:mm AP")));
    };
    refreshClock();
    auto* clockTimer = new QTimer(clockBox);
    connect(clockTimer, &QTimer::timeout, clockBox, refreshClock);
    clockTimer->start(1000);
    h->addWidget(clockBox, 0);

    // ── Right cluster ───────────────────────────────────────────────────────
    auto iconBtn = [&](const char* svg, std::function<void()> cb) {
        auto* b = new QToolButton(bar);
        b->setIcon(Lucide::icon(svg, Home::kTextBody, 17, bar->devicePixelRatio()));
        b->setIconSize(QSize(17, 17));
        b->setObjectName("topIcon");
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(34, 34);
        if (cb) connect(b, &QToolButton::clicked, this, cb);
        return b;
    };
    rightL->addStretch();
    rightL->addWidget(buildUpdateBanner(), 0, Qt::AlignVCenter);

    // AI Assistant chip: the same violet pill as the reference.
    // A QPushButton sizes itself from its text and icon, not from a layout put
    // inside it, so the chip needs its width stated or it collapses to a square.
    auto* ai = new QPushButton(bar);
    ai->setObjectName("aiChip");
    ai->setCursor(Qt::PointingHandCursor);
    ai->setFixedSize(146, 34);
    {
        auto* al = new QHBoxLayout(ai);
        al->addWidget(Lucide::label(Lucide::kSparkles, "#FFFFFF", 14, ai));
        auto* t = label600(tr("AI Assistant"), 12, "#FFFFFF", ai);
        t->setAttribute(Qt::WA_TransparentForMouseEvents);
        al->addWidget(t);
        al->addWidget(Lucide::label(Lucide::kChevronDown, "#D9D2FF", 12, ai));
    }
    connect(ai, &QPushButton::clicked, this, &StartScreen::aiRequested);
    rightL->addWidget(ai);

    auto* bellBtn = iconBtn(Lucide::kBell, nullptr);
    connect(bellBtn, &QToolButton::clicked, this,
            [this, bellBtn] { showNotificationsPopup(bellBtn); });
    rightL->addWidget(bellBtn);
    rightL->addWidget(iconBtn(Lucide::kSettings, [this] { showSettingsDialog(); }));

    auto* avatar = new QToolButton(bar);
    avatar->setObjectName("avatarBtn");
    avatar->setCursor(Qt::PointingHandCursor);
    avatar->setFixedSize(34, 34);
    avatar->setIconSize(QSize(34, 34));
    avatar->setStyleSheet("QToolButton#avatarBtn { border:none; background:transparent; }");
    auto refreshAvatar = [avatar] {
        auto& auth = AuthManager::instance();
        avatar->setIcon(QIcon(roundAvatarPixmap(34, avatar->devicePixelRatio())));
        avatar->setToolTip(auth.userEmail().isEmpty()
                               ? QStringLiteral("Account")
                               : auth.userName() + QStringLiteral("\n") + auth.userEmail());
    };
    refreshAvatar();
    connect(&AuthManager::instance(), &AuthManager::profileChanged, avatar, refreshAvatar);
    connect(avatar, &QToolButton::clicked, this, [this] { showProfileTray(); });
    rightL->addWidget(avatar);
    h->addWidget(rightBox, 1);

    // The keyboard hint sits at the right inside the field.
    kbd->move(m_search->width() - kbd->width() - 10, 11);

    bar->setStyleSheet(QString(R"(
        QWidget#topBar { background:%1; border-bottom:1px solid %2; }
        QLineEdit#searchBox { background:%3; border:1px solid %2; border-radius:11px;
            color:%4; font:13px 'Segoe UI'; }
        QLineEdit#searchBox:focus { border:1px solid %5; background:%6; }
        QToolButton#topIcon { background:%3; border:1px solid %2; border-radius:11px; }
        QToolButton#topIcon:hover { background:%7; }
        QPushButton#aiChip {
            background:qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #6D5BF0, stop:1 #9B6BF6);
            border:none; border-radius:11px; }
        QPushButton#aiChip:hover {
            background:qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #7C6CF6, stop:1 #AC7DFF); }
    )").arg(Home::kBg, Home::kBorderSoft, Home::kPanel, Home::kTextBody,
            Home::kAccent, Home::kPanelSoft, Home::kPanelHover));
    return bar;
}

// ── Bottom strip ─────────────────────────────────────────────────────────────
// Just the two promises the app makes about your files. The quotation and the
// quick-access row that used to sit here were decoration, and decoration is
// what a dashboard can least afford at the bottom of the page.
QWidget* StartScreen::buildBottomBar() {
    auto* bar = new QFrame(this);
    bar->setObjectName("bottomBar");
    bar->setFixedHeight(42);
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(26, 0, 26, 4);
    h->setSpacing(22);
    h->addStretch();

    auto trust = [&](const char* icon, const QString& color,
                     const QString& text) -> QWidget* {
        auto* w = new QWidget(bar);
        auto* wl = new QHBoxLayout(w);
        wl->setContentsMargins(0, 0, 0, 0);
        wl->setSpacing(8);
        wl->addWidget(Lucide::label(icon, color, 15, w));
        wl->addWidget(heading(text, 12, Home::kMuted, false, w));
        h->addWidget(w, 0, Qt::AlignVCenter);
        return w;
    };
    m_trustLocal   = trust(Lucide::kCircleCheck, Home::kGreen,
                           tr("All files are saved locally"));
    m_trustPrivate = trust(Lucide::kShieldCheck, Home::kAccentSoft,
                           tr("100% private & secure"));
    h->addStretch();

    bar->setStyleSheet(QString(R"(
        QFrame#bottomBar { background:%1; border-top:1px solid %2; }
    )").arg(Home::kBg, Home::kBorderSoft));
    return bar;
}

// ── Center column ────────────────────────────────────────────────────────────
QWidget* StartScreen::buildCenterColumn() {
    auto* col = new QWidget(this);
    auto* v = new QVBoxLayout(col);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(12);

    m_hero = new HeroBanner(col);
    connect(m_hero, &HeroBanner::openFileRequested, this, &StartScreen::openFileDialog);
    connect(m_hero, &HeroBanner::createRequested, this, [this](HeroBanner::Create what) {
        if (launchLocked()) return;
        switch (what) {
        case HeroBanner::Create::Document:     emit newDocumentRequested(DocumentType::Writer);  break;
        case HeroBanner::Create::Spreadsheet:  emit newDocumentRequested(DocumentType::Calc);    break;
        case HeroBanner::Create::Presentation: emit newDocumentRequested(DocumentType::Impress); break;
        case HeroBanner::Create::Markdown:     emit toolRequested(Tool::MarkdownEditor);         break;
        }
    });
    v->addWidget(m_hero);

    v->addWidget(buildCreateCards());

    auto* row = new QWidget(col);
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(16);
    m_recentRowLayout = rl;
    m_recentPanel = buildRecentPanel();
    rl->addWidget(m_recentPanel, 1);
    rl->addWidget(buildTemplatesPanel(), 1);
    v->addWidget(row, 1);
    return col;
}

QWidget* StartScreen::buildCreateCards() {
    auto* w = new QWidget(this);
    auto* g = new QHBoxLayout(w);
    g->setContentsMargins(0, 0, 0, 0);
    g->setSpacing(14);

    // Open File is not among these any more: it sits on the hero banner beside
    // Create New, which is what gives the row its breathing space.
    struct Card { const char* art; QString title, sub, accent; int action; };
    const Card cards[] = {
        { ":/assets/card-document.jpg",     tr("Document"),     tr("Write, edit and format"),
          Home::kWriter,  1 },
        { ":/assets/card-spreadsheet.jpg",  tr("Spreadsheet"),  tr("Analyse and visualise"),
          Home::kCalc,    2 },
        { ":/assets/card-presentation.jpg", tr("Presentation"), tr("Design and present"),
          Home::kImpress, 3 },
        { ":/assets/card-pdf.jpg",          tr("PDF"),          tr("Merge, convert and secure"),
          Home::kPdf,     4 },
    };

    for (const Card& c : cards) {
        auto* card = new CreateCard(QString::fromLatin1(c.art), c.title, c.sub,
                                    c.accent, w);
        const int action = c.action;
        card->onClick = [this, action] {
            if (launchLocked()) return;
            switch (action) {
            case 1: emit newDocumentRequested(DocumentType::Writer);  break;
            case 2: emit newDocumentRequested(DocumentType::Calc);    break;
            case 3: emit newDocumentRequested(DocumentType::Impress); break;
            default: emit newDocumentRequested(DocumentType::Pdf);    break;
            }
        };
        g->addWidget(card, 1);
    }
    return w;
}

// ── Recent files ─────────────────────────────────────────────────────────────
QWidget* StartScreen::buildRecentPanel() {
    auto* panel = new QFrame(this);
    panel->setObjectName("panel");
    auto* v = new QVBoxLayout(panel);
    v->setContentsMargins(16, 13, 14, 11);
    v->setSpacing(6);

    auto* head = new QHBoxLayout();
    head->addWidget(label600(tr("Recent Files"), 14, Home::kText, panel));
    head->addStretch();
    auto* browse = new ClickableFrame(panel);
    browse->setCursor(Qt::PointingHandCursor);
    auto* bl = new QHBoxLayout(browse);
    bl->setContentsMargins(6, 2, 2, 2);
    auto* browseLabel = heading(tr("View all"), 12, Home::kAccentSoft, false, browse);
    browseLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    bl->addWidget(browseLabel);
    browse->onClick = [this] { openFileDialog(); };
    head->addWidget(browse);
    v->addLayout(head);
    v->addSpacing(2);

    auto& mgr = RecentFilesManager::instance();
    const std::vector<RecentFileEntry> entries = mgr.recentFiles();

    if (entries.empty()) {
        // First run: say what would put something here rather than leaving a
        // blank rectangle.
        auto* empty = new QWidget(panel);
        auto* el = new QVBoxLayout(empty);
        el->setContentsMargins(0, 22, 0, 22);
        el->setSpacing(9);
        auto* icon = Lucide::label(Lucide::kClock, Home::kFaint, 26, empty);
        el->addWidget(icon, 0, Qt::AlignHCenter);
        auto* line = heading(tr("Nothing here yet"), 13, Home::kTextBody, false, empty);
        line->setAlignment(Qt::AlignCenter);
        el->addWidget(line);
        auto* hint = heading(tr("Create or open a file and it will show up here."),
                             11, Home::kFaint, false, empty);
        hint->setAlignment(Qt::AlignCenter);
        hint->setWordWrap(true);
        el->addWidget(hint);
        v->addWidget(empty);
        v->addStretch();
    }

    int shown = 0;
    for (const auto& e : entries) {
        if (shown++ >= 5) break;
        const FileKind kind = fileKindForModule(e.type);

        auto* rowf = new ClickableFrame(panel);
        rowf->setObjectName("recentRow");
        rowf->setFixedHeight(40);
        auto* rh = new QHBoxLayout(rowf);
        rh->setContentsMargins(8, 0, 6, 0);
        rh->setSpacing(11);
        rh->addWidget(fileBadge(kind, 30, rowf));

        auto* meta = new QVBoxLayout();
        meta->setSpacing(1);
        auto* name = new ElidedLabel(e.name, rowf);
        name->setStyleSheet(QString("color:%1;font:600 12px 'Segoe UI';"
                                    "background:transparent;").arg(Home::kText));
        name->setAttribute(Qt::WA_TransparentForMouseEvents);
        meta->addWidget(name);
        QString subText = kind.module;
        if (mgr.isFavorite(e.path)) subText += QStringLiteral("  ·  ★ Favourite");
        auto* sub = heading(subText, 11, Home::kFaint, false, rowf);
        sub->setAttribute(Qt::WA_TransparentForMouseEvents);
        meta->addWidget(sub);
        rh->addLayout(meta, 1);

        const QDateTime when = e.lastOpened;
        const QString whenText =
            when.date() == QDate::currentDate()
                ? tr("Today, %1").arg(when.toString(QStringLiteral("hh:mm")))
            : when.date() == QDate::currentDate().addDays(-1)
                ? tr("Yesterday, %1").arg(when.toString(QStringLiteral("hh:mm")))
                : when.toString(QStringLiteral("MMM d, hh:mm"));
        auto* stamp = heading(whenText, 11, Home::kFaint, false, rowf);
        stamp->setAttribute(Qt::WA_TransparentForMouseEvents);
        rh->addWidget(stamp);

        auto* more = new ClickableFrame(rowf);
        more->setFixedSize(24, 24);
        more->setCursor(Qt::PointingHandCursor);
        auto* ml = new QHBoxLayout(more);
        ml->setContentsMargins(0, 0, 0, 0);
        ml->addWidget(Lucide::label(Lucide::kMoreVertical, Home::kFaint, 14, more),
                      0, Qt::AlignCenter);
        const QString path = e.path;
        more->onClick = [this, more, path] {
            showRecentFileMenu(path, more->mapToGlobal(QPoint(0, more->height())));
        };
        rh->addWidget(more);

        rowf->onClick = [this, path] { if (!launchLocked()) emit fileOpenRequested(path); };
        rowf->onContextMenu = [this, path](const QPoint& at) { showRecentFileMenu(path, at); };
        v->addWidget(rowf);
    }

    if (!entries.empty()) {
        v->addStretch();
        auto* all = new ClickableFrame(panel);
        all->setObjectName("recentRow");
        all->setFixedHeight(30);
        all->setCursor(Qt::PointingHandCursor);
        auto* al = new QHBoxLayout(all);
        al->setContentsMargins(8, 0, 8, 0);
        al->setSpacing(10);
        al->addWidget(Lucide::label(Lucide::kFolderOpen, Home::kAccentSoft, 15, all));
        auto* t = label600(tr("Browse all files"), 12, Home::kAccentSoft, all);
        t->setAttribute(Qt::WA_TransparentForMouseEvents);
        al->addWidget(t);
        al->addStretch();
        al->addWidget(Lucide::label(Lucide::kArrowRight, Home::kAccentSoft, 14, all));
        all->onClick = [this] { openFileDialog(); };
        v->addWidget(all);
    }

    panel->setStyleSheet(QString(R"(
        QFrame#panel { background:%1; border:1px solid %2; border-radius:14px; }
        #recentRow { background:transparent; border-radius:9px; }
        #recentRow:hover { background:%3; }
    )").arg(Home::kPanel, Home::kBorder, Home::kPanelHover));
    return panel;
}

void StartScreen::refreshRecentPanel() {
    if (!m_recentRowLayout) return;
    QWidget* fresh = buildRecentPanel();
    if (m_recentPanel) {
        // replaceWidget keeps the item's stretch factor, so the two panels stay
        // evenly split.
        delete m_recentRowLayout->replaceWidget(m_recentPanel, fresh);
        m_recentPanel->deleteLater();
    } else {
        m_recentRowLayout->insertWidget(0, fresh, 1);
    }
    m_recentPanel = fresh;
}

void StartScreen::openRecycleBin() {
    if (!QProcess::startDetached("explorer.exe", { "shell:RecycleBinFolder" }))
        QMessageBox::information(this, tr("Recycle Bin"),
                                 tr("Could not open the Recycle Bin."));
}

void StartScreen::showRecentFileMenu(const QString& path, const QPoint& at) {
    auto& mgr = RecentFilesManager::instance();
    const bool fav = mgr.isFavorite(path);
    const QString name = QFileInfo(path).fileName();

    QMenu menu(this);
    menu.setStyleSheet(ThemeManager::inputDialogStyleSheet());

    QAction* favAct    = menu.addAction(fav ? tr("Remove from Favourites")
                                            : tr("Add to Favourites"));
    QAction* revealAct = menu.addAction(tr("Open file location"));
    menu.addSeparator();
    QAction* forgetAct = menu.addAction(tr("Remove from list"));
    QAction* deleteAct = menu.addAction(tr("Delete file…"));

    QAction* chosen = menu.exec(at);
    if (!chosen) return;

    if (chosen == favAct) {
        mgr.setFavorite(path, !fav);
        refreshRecentPanel();
    } else if (chosen == revealAct) {
        const QString native = QDir::toNativeSeparators(path);
        if (!QProcess::startDetached("explorer.exe", { "/select,", native }))
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    } else if (chosen == forgetAct) {
        mgr.removeFile(path);
        refreshRecentPanel();
    } else if (chosen == deleteAct) {
        if (QMessageBox::question(this, tr("Delete File"),
                tr("Move \"%1\" to the Recycle Bin?").arg(name),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        QFile f(path);
        if (f.exists() && !f.moveToTrash()) {
            QMessageBox::warning(this, tr("Delete Failed"),
                tr("Could not delete \"%1\".\n\n%2").arg(name, f.errorString()));
            return;
        }
        mgr.setFavorite(path, false);
        mgr.removeFile(path);
        refreshRecentPanel();
    }
}

// ── Templates for You ────────────────────────────────────────────────────────
// Four finished cards from the supplied sheet, one per template. The card is
// the artwork: it already carries the label, the name and the kind, so nothing
// is written under it and there are no carousel dots, since View all is the
// way to the rest.
QWidget* StartScreen::buildTemplatesPanel() {
    auto* panel = new QFrame(this);
    panel->setObjectName("panel");
    auto* v = new QVBoxLayout(panel);
    v->setContentsMargins(16, 12, 16, 12);
    v->setSpacing(9);

    auto* head = new QHBoxLayout();
    head->addWidget(label600(tr("Templates for You"), 14, Home::kText, panel));
    head->addStretch();
    auto* viewAll = new ClickableFrame(panel);
    viewAll->setCursor(Qt::PointingHandCursor);
    auto* vaL = new QHBoxLayout(viewAll);
    vaL->setContentsMargins(6, 2, 2, 2);
    auto* vaLabel = heading(tr("View all"), 12, Home::kAccentSoft, false, viewAll);
    vaLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    vaL->addWidget(vaLabel);
    viewAll->onClick = [this] { showTemplateMarket(0); };
    head->addWidget(viewAll);
    v->addLayout(head);

    auto* grid = new QGridLayout();
    grid->setSpacing(11);

    struct Card { const char* art; QString name; DocumentType type; };
    const Card cards[] = {
        { ":/assets/tpl-resume.jpg", QStringLiteral("Professional Resume"), DocumentType::Writer },
        { ":/assets/tpl-budget.jpg", QStringLiteral("Monthly Budget"),      DocumentType::Calc },
        { ":/assets/tpl-pitch.jpg",  QStringLiteral("Pitch Deck"),          DocumentType::Impress },
        { ":/assets/tpl-report.jpg", QStringLiteral("Project Report"),      DocumentType::Writer },
    };
    for (int i = 0; i < 4; ++i) {
        auto* c = new ArtCard(QString::fromLatin1(cards[i].art), 11, panel);
        c->setToolTip(tr("Open the %1 template").arg(cards[i].name));
        const DocumentType type = cards[i].type;
        const QString name = cards[i].name;
        c->onClick = [this, type, name] {
            if (!launchLocked()) emit templateChosen(type, name);
        };
        grid->addWidget(c, i / 2, i % 2);
    }
    v->addLayout(grid, 1);

    panel->setStyleSheet(QString(R"(
        QFrame#panel { background:%1; border:1px solid %2; border-radius:14px; }
    )").arg(Home::kPanel, Home::kBorder));
    return panel;
}

// ── Right column ─────────────────────────────────────────────────────────────
QWidget* StartScreen::buildRightColumn() {
    auto* col = new QWidget(this);
    col->setFixedWidth(322);
    auto* v = new QVBoxLayout(col);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(12);

    m_rightColumn = col;
    v->addWidget(new ActivityCard(col));
    v->addWidget(buildToolsCard());
    v->addWidget(buildAiCard());
    v->addWidget(buildQuickTips());
    v->addStretch();
    return col;
}

QWidget* StartScreen::buildToolsCard() {
    auto* p = new QFrame(this);
    p->setObjectName("sidePanel");
    auto* v = new QVBoxLayout(p);
    v->setContentsMargins(16, 11, 16, 11);
    v->setSpacing(8);
    {
        auto* head = new QVBoxLayout();
        head->setSpacing(1);
        head->addWidget(label600(tr("Tools"), 14, Home::kText, p));
        head->addWidget(heading(tr("Smart tools for everyday productivity"),
                                10, Home::kFaint, false, p));
        v->addLayout(head);
    }

    auto* grid = new QGridLayout();
    grid->setSpacing(8);

    struct ToolTile { const char* icon; QString label; QString color; Tool tool; QString tip; };
    const ToolTile tiles[] = {
        { Lucide::kImage,       tr("Image Resizer"),     Home::kImageKind, Tool::ImageResizer,
          tr("Resize, convert and compress pictures") },
        { Lucide::kCode,        tr("Markdown Editor"),   Home::kMarkdown,  Tool::MarkdownEditor,
          tr("Write markdown with a live preview") },
        { Lucide::kQrCode,      tr("QR Code Generator"), Home::kAccent,    Tool::QrCode,
          tr("Make a QR code for a link, Wi-Fi or contact") },
        { Lucide::kFileArchive, tr("Compress PDF"),      Home::kPdf,       Tool::CompressPdf,
          tr("Shrink a PDF without touching image quality") },
        { Lucide::kScanText,    tr("OCR / Scan"),        Home::kCalc,      Tool::Ocr,
          tr("Make a scanned PDF searchable") },
        { Lucide::kFileDown,    tr("PDF to Word"),       Home::kWriter,    Tool::PdfToWord,
          tr("Rebuild a PDF as an editable document") },
    };
    int i = 0;
    for (const ToolTile& t : tiles) {
        auto* tile = new ClickableFrame(p);
        tile->setObjectName("toolTile");
        tile->setFixedHeight(42);
        tile->setCursor(Qt::PointingHandCursor);
        tile->setToolTip(t.tip);
        auto* tl = new QHBoxLayout(tile);
        tl->setContentsMargins(7, 0, 7, 0);
        tl->setSpacing(8);
        tl->addWidget(iconTile(t.icon, t.color, 24, tile));
        auto* lab = new ElidedLabel(t.label, tile);
        lab->setStyleSheet(QString("color:%1;font:600 10px 'Segoe UI';"
                                   "background:transparent;").arg(Home::kTextBody));
        lab->setAttribute(Qt::WA_TransparentForMouseEvents);
        tl->addWidget(lab, 1);
        const Tool tool = t.tool;
        tile->onClick = [this, tool] { if (!launchLocked()) emit toolRequested(tool); };
        grid->addWidget(tile, i / 2, i % 2);
        ++i;
    }
    v->addLayout(grid);

    p->setStyleSheet(QString(R"(
        QFrame#sidePanel { background:%1; border:1px solid %2; border-radius:14px; }
        #toolTile { background:%3; border:1px solid %2; border-radius:10px; }
        #toolTile:hover { background:%4; border:1px solid %5; }
    )").arg(Home::kPanel, Home::kBorder, Home::kPanelSoft, Home::kPanelHover, Home::kAccent));
    return p;
}

QWidget* StartScreen::buildAiCard() {
    // The supplied card, used as it was drawn. Everything it says (the beta
    // tag, the button, the three promises) is in the picture, so laying widgets
    // over it would only fight with it.
    auto* card = new ArtCard(QStringLiteral(":/assets/ai-card.jpg"), 14, this);
    card->setFixedHeight(126);
    card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    card->setToolTip(tr("Open the assistant"));
    card->onClick = [this] { emit aiRequested(); };
    return card;
}

QWidget* StartScreen::buildQuickTips() {
    return new QuickTipsCard(this);
}

// ── Settings / profile ───────────────────────────────────────────────────────
void StartScreen::showSettingsDialog() {
    if (!m_settingsTray) m_settingsTray = new SettingsTray(window());
    m_settingsTray->openTray(SettingsTray::Settings);
}

void StartScreen::showProfileTray() {
    if (!m_settingsTray) m_settingsTray = new SettingsTray(window());
    m_settingsTray->openTray(SettingsTray::Profile);
}

// The sidebar account row: plan status, upgrade pitch, and the way into the
// profile. This is where the old always-on "Upgrade to Pro" card went.
void StartScreen::showPlanPopup(QWidget* anchor) {
    auto& auth = AuthManager::instance();
    const bool premium = auth.premiumActive();

    auto* pop = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint);
    pop->setObjectName("planPop");
    pop->setAttribute(Qt::WA_DeleteOnClose);
    pop->setAttribute(Qt::WA_StyledBackground, true);
    pop->setFixedWidth(286);

    auto* v = new QVBoxLayout(pop);
    v->setContentsMargins(16, 15, 16, 14);
    v->setSpacing(10);

    auto* headRow = new QHBoxLayout();
    headRow->setSpacing(10);
    auto* crown = new QLabel(pop);
    crown->setFixedSize(34, 34);
    crown->setAlignment(Qt::AlignCenter);
    crown->setPixmap(Lucide::pixmap(Lucide::kCrown, "#F5C453", 18, devicePixelRatio()));
    crown->setStyleSheet("background:rgba(245,196,83,0.15);border-radius:10px;");
    headRow->addWidget(crown);
    auto* headCol = new QVBoxLayout();
    headCol->setSpacing(1);
    headCol->addWidget(label600(premium ? auth.premiumPlanLabel()
                                        : tr("NativeOffice Pro"), 13, Home::kText, pop));
    headCol->addWidget(heading(premium ? tr("Thank you for supporting the app.")
                                       : tr("Unlock the full power"),
                               11, Home::kFaint, false, pop));
    headRow->addLayout(headCol, 1);
    v->addLayout(headRow);

    if (!premium) {
        const QStringList perks = {
            tr("No export watermark"),
            tr("Advanced PDF tools"),
            tr("Premium export defaults"),
            tr("Priority support"),
        };
        for (const QString& perk : perks) {
            auto* row = new QWidget(pop);
            auto* rl = new QHBoxLayout(row);
            rl->setContentsMargins(2, 0, 0, 0);
            rl->setSpacing(9);
            rl->addWidget(Lucide::label(Lucide::kCircleCheck, Home::kGreen, 14, row));
            rl->addWidget(heading(perk, 12, Home::kTextBody, false, row));
            rl->addStretch();
            v->addWidget(row);
        }

        auto* upgrade = new QPushButton(tr("Upgrade to Pro"), pop);
        upgrade->setObjectName("upgradeBtn");
        upgrade->setCursor(Qt::PointingHandCursor);
        upgrade->setFixedHeight(38);
        connect(upgrade, &QPushButton::clicked, pop, [pop] {
            AuthManager::instance().openPremiumPage();
            pop->close();
        });
        v->addWidget(upgrade);
    }

    auto* rule = new QFrame(pop);
    rule->setFixedHeight(1);
    rule->setStyleSheet(QString("background:%1;border:none;").arg(Home::kBorder));
    v->addWidget(rule);

    auto linkRow = [&](const char* icon, const QString& text, std::function<void()> cb) {
        auto* row = new ClickableFrame(pop);
        row->setObjectName("planRow");
        row->setFixedHeight(34);
        row->setCursor(Qt::PointingHandCursor);
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(8, 0, 8, 0);
        rl->setSpacing(10);
        rl->addWidget(Lucide::label(icon, Home::kMuted, 15, row));
        auto* lab = heading(text, 12, Home::kTextBody, false, row);
        lab->setAttribute(Qt::WA_TransparentForMouseEvents);
        rl->addWidget(lab);
        rl->addStretch();
        row->onClick = [pop, cb] { pop->close(); cb(); };
        v->addWidget(row);
    };
    linkRow(Lucide::kUser, tr("Profile and account"), [this] { showProfileTray(); });
    linkRow(Lucide::kSettings, tr("Settings"), [this] { showSettingsDialog(); });
    linkRow(Lucide::kKeyboard, tr("Keyboard shortcuts"), [this] { showShortcutsDialog(); });
    linkRow(Lucide::kSparkles, tr("What's new"), [this] { showWhatsNewDialog(); });

    pop->setStyleSheet(QString(R"(
        QFrame#planPop { background:%1; border:1px solid %2; border-radius:14px; }
        #planRow { background:transparent; border-radius:8px; }
        #planRow:hover { background:%3; }
        QPushButton#upgradeBtn { background:qlineargradient(x1:0,y1:0,x2:1,y2:0,
            stop:0 #E0A93B, stop:1 #F0C558); border:none; border-radius:9px;
            color:#221A05; font:700 12px 'Segoe UI'; }
        QPushButton#upgradeBtn:hover { background:qlineargradient(x1:0,y1:0,x2:1,y2:0,
            stop:0 #EFBA4C, stop:1 #FFD76D); }
    )").arg(Home::kPanel, Home::kBorder, Home::kPanelHover));

    pop->ensurePolished();
    pop->layout()->activate();
    pop->adjustSize();
    pop->move(anchor->mapToGlobal(QPoint(0, -pop->height() - 8)));
    pop->show();
}

// ── Notifications ────────────────────────────────────────────────────────────
void StartScreen::showNotificationsPopup(QWidget* anchor) {
    // Toggle without flicker: a Qt::Popup dismisses itself on the very press
    // that opens the bell again, so the release would otherwise re-open it.
    if (m_notifPopup) { m_notifPopup->close(); return; }
    if (QDateTime::currentMSecsSinceEpoch() - m_notifClosedMs < 250) return;

    auto* pop = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint);
    pop->setObjectName("notifPop");
    pop->setAttribute(Qt::WA_DeleteOnClose);
    pop->setAttribute(Qt::WA_StyledBackground, true);
    pop->setFixedWidth(392);
    m_notifPopup = pop;
    connect(pop, &QObject::destroyed, this,
            [this] { m_notifClosedMs = QDateTime::currentMSecsSinceEpoch(); });

    auto* v = new QVBoxLayout(pop);
    v->setContentsMargins(8, 8, 8, 10);
    v->setSpacing(2);

    // Header strip.
    auto* head = new QWidget(pop);
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(10, 6, 6, 8);
    hl->setSpacing(9);
    hl->addWidget(Lucide::label(Lucide::kBell, Home::kAccentSoft, 16, head));
    hl->addWidget(label600(tr("Notifications"), 14, Home::kText, head));
    hl->addStretch();
    auto* count = new QLabel(pop);
    count->setAlignment(Qt::AlignCenter);
    count->setFixedHeight(20);
    count->setStyleSheet(QString("background:rgba(124,108,246,0.18);color:%1;"
                                 "border-radius:6px;padding:0 8px;"
                                 "font:700 10px 'Segoe UI';").arg(Home::kAccentSoft));
    hl->addWidget(count);
    v->addWidget(head);

    auto* rule = new QFrame(pop);
    rule->setFixedHeight(1);
    rule->setStyleSheet(QString("background:%1;border:none;").arg(Home::kBorderSoft));
    v->addWidget(rule);
    v->addSpacing(4);

    auto* scroll = new QScrollArea(pop);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMaximumHeight(430);
    auto* list = new QWidget(scroll);
    list->setObjectName("notifList");
    auto* lv = new QVBoxLayout(list);
    lv->setContentsMargins(0, 0, 6, 0);
    lv->setSpacing(3);

    int items = 0;
    auto item = [&](const char* icon, const QString& tint, const QString& title,
                    const QString& body, const QString& when, const QString& url) {
        ++items;
        auto* row = new ClickableFrame(list);
        row->setObjectName("notifItem");
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(10, 10, 10, 10);
        rl->setSpacing(11);

        auto* dot = new QLabel(row);
        dot->setFixedSize(32, 32);
        dot->setAlignment(Qt::AlignCenter);
        dot->setPixmap(Lucide::pixmap(icon, tint, 16, devicePixelRatio()));
        QColor c(tint);
        dot->setStyleSheet(QString("background:rgba(%1,%2,%3,0.14);border-radius:10px;")
                               .arg(c.red()).arg(c.green()).arg(c.blue()));
        dot->setAttribute(Qt::WA_TransparentForMouseEvents);
        rl->addWidget(dot, 0, Qt::AlignTop);

        auto* tv = new QVBoxLayout();
        tv->setSpacing(3);
        auto* topRow = new QHBoxLayout();
        topRow->setSpacing(8);
        auto* t = label600(title, 12, Home::kText, row);
        t->setAttribute(Qt::WA_TransparentForMouseEvents);
        topRow->addWidget(t, 1);
        auto* stamp = heading(when, 10, Home::kFaint, false, row);
        stamp->setAttribute(Qt::WA_TransparentForMouseEvents);
        topRow->addWidget(stamp, 0, Qt::AlignTop);
        tv->addLayout(topRow);
        auto* b = heading(body, 11, Home::kMuted, false, row);
        b->setWordWrap(true);
        b->setAttribute(Qt::WA_TransparentForMouseEvents);
        tv->addWidget(b);
        rl->addLayout(tv, 1);

        if (!url.isEmpty()) {
            row->setCursor(Qt::PointingHandCursor);
            row->onClick = [pop, url] { QDesktopServices::openUrl(QUrl(url)); pop->close(); };
        }
        lv->addWidget(row);
    };

    item(Lucide::kSparkles, Home::kAccentSoft, tr("A brand new Home"),
         tr("The dashboard has been rebuilt: a greeting that follows the time of day, "
            "search that really searches your files, an activity graph with history, "
            "and six tools a click away."),
         tr("Now"), QString());
    item(Lucide::kRepeat, Home::kGreen, tr("Your work saves itself"),
         tr("Autosave is on in Writer, Sheets, Slides, PDF and Markdown. Save and Save As "
            "are still in the File menu when you want to choose a folder."),
         QStringLiteral("1.6.5"), QString());
    item(Lucide::kPresentation, Home::kImpress, tr("A taller workspace"),
         tr("The Windows title bar is gone and the tab strip took its place, so the ribbon "
            "and page start higher in every mode."),
         QStringLiteral("1.6.4"), QString());
    item(Lucide::kCrown, Home::kAmber, tr("Editing is free for everyone"),
         tr("No read-only mode. Free exports carry a small \"Made with NativeOffice\" mark; "
            "Premium removes it."),
         QString(), QStringLiteral("https://nativeoffice.online/premium"));
    item(Lucide::kStar, Home::kBlue, tr("Our family is growing"),
         tr("The NativeOffice community grows every day. Thanks for being part of it."),
         QString(), QString());
    item(Lucide::kHelp, Home::kMuted, tr("Questions or feature requests?"),
         tr("We would love to hear from you at contact@nativeoffice.online."),
         QString(), QStringLiteral("mailto:contact@nativeoffice.online"));
    item(Lucide::kShieldCheck, Home::kAccentSoft, tr("Privacy Policy and Terms"),
         tr("How NativeOffice handles your data, and the terms that govern its use."),
         QString(), QStringLiteral("https://nativeoffice.online/privacy"));

    count->setText(QString::number(items));
    lv->addStretch();
    scroll->setWidget(list);
    v->addWidget(scroll, 1);

    pop->setStyleSheet(QString(R"(
        QFrame#notifPop { background:%1; border:1px solid %2; border-radius:14px; }
        QWidget#notifList { background:transparent; }
        QScrollArea { background:transparent; }
        #notifItem { background:%3; border:1px solid transparent; border-radius:10px; }
        #notifItem:hover { background:%4; border:1px solid %2; }
        QScrollBar:vertical { background:transparent; width:8px; margin:2px; }
        QScrollBar::handle:vertical { background:#2A3244; border-radius:4px; min-height:26px; }
        QScrollBar::add-line, QScrollBar::sub-line { height:0; }
    )").arg(Home::kPanel, Home::kBorder, Home::kPanelSoft, Home::kPanelHover));

    // Compute the final size BEFORE showing so the popup doesn't pop in at one
    // size and resize on screen.
    pop->ensurePolished();
    pop->layout()->activate();
    pop->adjustSize();
    const QPoint bottomRight =
        anchor->mapToGlobal(QPoint(anchor->width(), anchor->height() + 8));
    pop->move(bottomRight - QPoint(pop->width(), 0));
    pop->show();
}

// ── Templates ────────────────────────────────────────────────────────────────
void StartScreen::showTemplateMarket(int category) {
    TemplateMarket market(this, category);
    connect(&market, &TemplateMarket::templateChosen, this,
            [this](DocumentType type, const QString& name) {
                if (!launchLocked()) emit templateChosen(type, name);
            });
    market.exec();
}

// ── Keyboard shortcuts ───────────────────────────────────────────────────────
void StartScreen::showShortcutsDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Keyboard Shortcuts"));
    dlg.resize(700, 660);
    dlg.setStyleSheet(QString(R"(
        QDialog { background:%1; }
        QScrollArea { background:transparent; border:none; }
        QScrollArea > QWidget > QWidget { background:transparent; }
    )").arg(Home::kBg));

    auto* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(24, 20, 24, 16);
    root->setSpacing(12);
    root->addWidget(heading(tr("Keyboard Shortcuts"), 20, Home::kText, true, &dlg));

    auto* scroll = new QScrollArea(&dlg);
    scroll->setWidgetResizable(true);
    auto* page = new QWidget(scroll);
    auto* pv = new QVBoxLayout(page);
    pv->setContentsMargins(0, 4, 12, 4);
    pv->setSpacing(6);

    auto section = [&](const QString& title) {
        pv->addSpacing(10);
        pv->addWidget(heading(title, 14, Home::kAccentSoft, true, page));
        pv->addSpacing(2);
    };
    auto row = [&](const QString& keys, const QString& what) {
        auto* r = new QWidget(page);
        auto* h = new QHBoxLayout(r);
        h->setContentsMargins(0, 2, 0, 2);
        h->setSpacing(14);
        auto* chip = new QLabel(keys, r);
        chip->setStyleSheet(QString("background:%1; color:%2; border-radius:5px;"
                                    "padding:3px 10px; font:600 11px 'Segoe UI';")
                                .arg(Home::kPanelSoft, Home::kTextBody));
        chip->setFixedWidth(155);
        chip->setAlignment(Qt::AlignCenter);
        h->addWidget(chip);
        h->addWidget(heading(what, 12, Home::kMuted, false, r));
        h->addStretch();
        pv->addWidget(r);
    };

    section(tr("Everywhere"));
    row("Ctrl + K",           tr("Search your files from Home"));
    row("Ctrl + N",           tr("New document"));
    row("Ctrl + O",           tr("Open a file"));
    row("Ctrl + S",           tr("Save"));
    row("Ctrl + Shift + S",   tr("Save As"));
    row("Ctrl + Z / Y",       tr("Undo / redo"));
    row("Ctrl + X / C / V",   tr("Cut / copy / paste"));
    row("Ctrl + A",           tr("Select all"));
    row("Ctrl + T / W",       tr("New tab / close tab"));
    row("Ctrl + Tab",         tr("Next tab"));
    row("Ctrl + Shift + T",   tr("Reopen the last closed tab"));

    section(tr("Writer (Documents)"));
    row("Ctrl + B / I / U",   tr("Bold / italic / underline"));
    row("Ctrl + F",           tr("Find"));
    row("Ctrl + H",           tr("Find and replace"));
    row("Ctrl + P",           tr("Print"));
    row("F7",                 tr("Spelling check"));
    row("Ctrl + ] / [",       tr("Grow / shrink font size"));
    row("Ctrl + L / E / R / J", tr("Align left / center / right / justify"));
    row("Ctrl + 1 / 5 / 2",   tr("Line spacing 1.0 / 1.5 / 2.0"));
    row("Ctrl + Alt + 1-3",   tr("Apply Heading 1 to 3"));
    row("Ctrl + Shift + N",   tr("Back to Normal style"));
    row("Ctrl + Shift + L",   tr("Bulleted list"));
    row("Ctrl + M",           tr("Increase indent (Shift to decrease)"));
    row("Ctrl + K",           tr("Insert hyperlink"));
    row("Ctrl + Enter",       tr("Page break"));
    row("Ctrl + Space",       tr("Clear formatting"));
    row("Shift + F3",         tr("Change case"));
    row("Ctrl + Shift + F",   tr("Focus mode"));
    row("Ctrl + Shift + E",   tr("Export to PDF"));

    section(tr("Sheets (Spreadsheets)"));
    row("Ctrl + B / I / U",   tr("Bold / italic / underline"));
    row("Ctrl + D",           tr("Fill down"));
    row("Ctrl + R",           tr("Fill right"));
    row("Delete",             tr("Clear selected cells"));

    section(tr("Slides (Presentations)"));
    row("F5",                 tr("Start slide show"));
    row("Shift + F5",         tr("Slide show from current slide"));
    row("Esc",                tr("Exit slide show"));
    row("→ / Space",          tr("Next slide"));
    row("← / Backspace",      tr("Previous slide"));
    row("B",                  tr("Black screen during show"));
    row("Ctrl + M",           tr("New slide"));
    row("Delete",             tr("Delete selected object"));
    row("Ctrl + Shift + E",   tr("Export to PDF"));

    section(tr("PDF"));
    row("Ctrl + F",           tr("Find in document"));
    row("Ctrl + P",           tr("Print"));
    row("Esc",                tr("Exit read mode"));

    pv->addStretch();
    scroll->setWidget(page);
    root->addWidget(scroll, 1);

    auto* closeBtn = new QPushButton(tr("Close"), &dlg);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(QString("QPushButton { background:%1; color:#FFFFFF; border:none;"
                                    "border-radius:8px; padding:8px 22px;"
                                    "font:600 12px 'Segoe UI'; }"
                                    "QPushButton:hover { background:%2; }")
                                .arg(Home::kAccent, Home::kAccentSoft));
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    auto* br = new QHBoxLayout();
    br->addStretch();
    br->addWidget(closeBtn);
    root->addLayout(br);

    dlg.exec();
}

// ── What's new ───────────────────────────────────────────────────────────────
void StartScreen::showWhatsNewDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("What's New"));
    dlg.resize(540, 500);
    dlg.setStyleSheet(QString("QDialog { background:%1; }").arg(Home::kBg));

    auto* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(24, 20, 24, 16);
    root->setSpacing(14);
    root->addWidget(heading(tr("What's New in NativeOffice"), 20, Home::kText, true, &dlg));

    auto item = [&](const char* icon, const QString& title, const QString& body) {
        auto* card = new QFrame(&dlg);
        card->setObjectName("newsCard");
        card->setStyleSheet(QString("#newsCard { background:%1; border:1px solid %2;"
                                    "border-radius:11px; }").arg(Home::kPanel, Home::kBorder));
        auto* h = new QHBoxLayout(card);
        h->setContentsMargins(14, 12, 14, 12);
        h->setSpacing(12);
        h->addWidget(Lucide::label(icon, Home::kAccentSoft, 18, card), 0, Qt::AlignTop);
        auto* tv = new QVBoxLayout();
        tv->setSpacing(3);
        tv->addWidget(label600(title, 13, Home::kText, card));
        auto* b = heading(body, 12, Home::kMuted, false, card);
        b->setWordWrap(true);
        tv->addWidget(b);
        h->addLayout(tv, 1);
        root->addWidget(card);
    };

    item(Lucide::kHome, tr("A completely new Home"),
         tr("A greeting band that changes with the time of day, working search across "
            "your own files, an activity graph with real history, and a tools shelf."));
    item(Lucide::kQrCode, tr("Three more tools"),
         tr("QR Code Generator, Compress PDF, OCR and PDF to Word now have pages of "
            "their own, straight from the Tools card."));
    item(Lucide::kFolderOpen, tr("A template marketplace"),
         tr("Every template in one browsable place, with a preview of the document you "
            "are about to open rather than a generic icon."));
    item(Lucide::kRepeat, tr("Your work saves itself"),
         tr("Autosave is on everywhere. Save As is still there when you want to choose "
            "where a document lives."));

    root->addStretch();

    auto* closeBtn = new QPushButton(tr("Nice!"), &dlg);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(QString("QPushButton { background:%1; color:#FFFFFF; border:none;"
                                    "border-radius:8px; padding:8px 24px;"
                                    "font:600 12px 'Segoe UI'; }"
                                    "QPushButton:hover { background:%2; }")
                                .arg(Home::kAccent, Home::kAccentSoft));
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    auto* br = new QHBoxLayout();
    br->addStretch();
    br->addWidget(closeBtn);
    root->addLayout(br);

    dlg.exec();
}

} // namespace NativeOffice
