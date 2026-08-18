// ─────────────────────────────────────────────────────────────────────────────
// HeroBanner.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "HeroBanner.h"
#include "HomeKit.h"
#include "LucideIcons.h"
#include "core/auth/AuthManager.h"

#include <QAction>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

namespace NativeOffice {

namespace {

constexpr int kRadius = 16;

// Where the photo starts to show through. Left of this the band is the panel
// colour; right of it the picture is at full strength.
constexpr qreal kScrimEnd = 0.62;

} // namespace

HeroBanner::HeroBanner(QWidget* parent) : QFrame(parent) {
    setObjectName("heroBanner");
    setAttribute(Qt::WA_StyledBackground, false);   // painted by paintEvent
    setMinimumHeight(172);
    setMaximumHeight(172);

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(32, 22, 32, 22);
    v->setSpacing(0);

    m_greeting = new QLabel(this);
    m_greeting->setTextFormat(Qt::RichText);
    m_greeting->setStyleSheet("background:transparent;font:700 30px 'Segoe UI';");
    v->addWidget(m_greeting);

    v->addSpacing(7);
    m_subtitle = heading(QStringLiteral("Where ideas become documents."), 14,
                         "#AEB6C6", false, this);
    v->addWidget(m_subtitle);

    v->addStretch();

    // ── Actions ─────────────────────────────────────────────────────────────
    auto* row = new QWidget(this);
    row->setAttribute(Qt::WA_TranslucentBackground);
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(12);

    auto* create = new QPushButton(row);
    m_createBtn = create;
    create->setObjectName("heroCreate");
    create->setCursor(Qt::PointingHandCursor);
    // Stated explicitly: a QPushButton takes its size from its text and icon,
    // not from a layout placed inside it.
    create->setFixedSize(192, 44);
    {
        // The plus, the label and the chevron are laid out inside the button so
        // the chevron can sit hard right behind a hairline divider, as in the
        // reference. The whole button opens the menu, chevron included.
        auto* bl = new QHBoxLayout(create);
        bl->setContentsMargins(16, 0, 14, 0);
        bl->setSpacing(9);
        bl->addWidget(Lucide::label(Lucide::kPlus, "#FFFFFF", 17, create));
        auto* t = label600(QStringLiteral("Create New"), 14, "#FFFFFF", create);
        t->setAttribute(Qt::WA_TransparentForMouseEvents);
        bl->addWidget(t);
        bl->addStretch();
        auto* sep = new QFrame(create);
        sep->setFixedSize(1, 20);
        sep->setStyleSheet("background:rgba(255,255,255,0.28);border:none;");
        sep->setAttribute(Qt::WA_TransparentForMouseEvents);
        bl->addWidget(sep);
        bl->addWidget(Lucide::label(Lucide::kChevronDown, "#FFFFFF", 16, create));
    }
    connect(create, &QPushButton::clicked, this, &HeroBanner::showCreateMenu);
    rl->addWidget(create);

    auto* open = new QPushButton(row);
    open->setObjectName("heroOpen");
    open->setCursor(Qt::PointingHandCursor);
    open->setFixedSize(158, 44);
    {
        auto* bl = new QHBoxLayout(open);
        bl->setContentsMargins(18, 0, 18, 0);
        bl->setSpacing(9);
        bl->addStretch();
        bl->addWidget(Lucide::label(Lucide::kFolderOpen, "#DCE1EC", 17, open));
        auto* t = label600(QStringLiteral("Open File"), 14, "#DCE1EC", open);
        t->setAttribute(Qt::WA_TransparentForMouseEvents);
        bl->addWidget(t);
        bl->addStretch();
    }
    connect(open, &QPushButton::clicked, this, &HeroBanner::openFileRequested);
    rl->addWidget(open);

    rl->addStretch();
    v->addWidget(row);

    setStyleSheet(QString(R"(
        QPushButton#heroCreate {
            background:qlineargradient(x1:0,y1:0,x2:1,y2:1,
                stop:0 #6D5BF0, stop:1 #8B6CF8);
            border:none; border-radius:12px;
        }
        QPushButton#heroCreate:hover {
            background:qlineargradient(x1:0,y1:0,x2:1,y2:1,
                stop:0 #7C6CF6, stop:1 #9C7EFF);
        }
        QPushButton#heroOpen {
            background:rgba(18,22,33,0.72);
            border:1px solid rgba(255,255,255,0.14);
            border-radius:12px;
        }
        QPushButton#heroOpen:hover { background:rgba(30,36,52,0.86); }
        QMenu#createMenu { background:%1; border:1px solid %2; border-radius:12px;
            padding:7px; }
        QMenu#createMenu::item { background:transparent; color:%3;
            font:13px 'Segoe UI'; padding:9px 18px 9px 12px; border-radius:8px; }
        QMenu#createMenu::item:selected { background:%4; color:#FFFFFF; }
        QMenu#createMenu::icon { padding-left:10px; }
    )").arg(Home::kPanel, Home::kBorder, Home::kTextBody, Home::kPanelHover));

    refresh();

    // The picture and the greeting follow the clock, not the launch time.
    auto* tick = new QTimer(this);
    connect(tick, &QTimer::timeout, this, &HeroBanner::refresh);
    tick->start(60'000);

    connect(&AuthManager::instance(), &AuthManager::profileChanged,
            this, &HeroBanner::refresh);
}

HeroBanner::Slot HeroBanner::slotForNow() {
    const int hr = QTime::currentTime().hour();
    if (hr >= 5  && hr < 12) return Slot::Morning;
    if (hr >= 12 && hr < 17) return Slot::Afternoon;
    if (hr >= 17 && hr < 21) return Slot::Evening;
    return Slot::Night;
}

QString HeroBanner::imageForSlot(Slot s) {
    switch (s) {
    case Slot::Morning:   return QStringLiteral(":/assets/hero-morning.jpg");
    case Slot::Afternoon: return QStringLiteral(":/assets/hero-afternoon.jpg");
    case Slot::Evening:   return QStringLiteral(":/assets/hero-evening.jpg");
    case Slot::Night:     break;
    }
    return QStringLiteral(":/assets/hero-night.jpg");
}

QString HeroBanner::greetingForSlot(Slot s) {
    switch (s) {
    case Slot::Morning:   return QStringLiteral("Good morning");
    case Slot::Afternoon: return QStringLiteral("Good afternoon");
    case Slot::Evening:   return QStringLiteral("Good evening");
    case Slot::Night:     break;
    }
    return QStringLiteral("Good night");
}

void HeroBanner::refresh() {
    const Slot now = slotForNow();
    if (!m_slotValid || now != m_slot) {
        m_slot = now;
        m_slotValid = true;
        m_art = QPixmap(imageForSlot(now));
        m_scaled = QPixmap();          // force a re-crop at the next paint
        update();
    }

    QString who = AuthManager::instance().displayName();
    if (who.isEmpty()) who = QStringLiteral("there");
    const QString text =
        QStringLiteral("<span style='color:%1;'>%2, </span>"
                       "<span style='color:%3;'>%4</span>"
                       "<span style='color:%1;'> \xF0\x9F\x91\x8B</span>")
            .arg(Home::kText, greetingForSlot(m_slot), Home::kAccentSoft, who.toHtmlEscaped());
    if (m_greeting->text() != text) m_greeting->setText(text);
}

void HeroBanner::showCreateMenu() {
    QMenu menu(this);
    menu.setObjectName("createMenu");
    menu.setStyleSheet(styleSheet());
    const qreal dpr = devicePixelRatio();

    struct Row { const char* icon; QString color; QString text; Create what; };
    const Row rows[] = {
        { Lucide::kFileText,     Home::kWriter,   QStringLiteral("Create new document"),     Create::Document },
        { Lucide::kTable,        Home::kCalc,     QStringLiteral("Create new spreadsheet"),  Create::Spreadsheet },
        { Lucide::kPresentation, Home::kImpress,  QStringLiteral("Create a presentation"),   Create::Presentation },
        { Lucide::kCode,         Home::kMarkdown, QStringLiteral("Create a markdown"),       Create::Markdown },
    };
    for (const Row& r : rows) {
        auto* a = menu.addAction(QIcon(Lucide::pixmap(r.icon, r.color, 17, dpr)), r.text);
        const Create what = r.what;
        connect(a, &QAction::triggered, this, [this, what] { emit createRequested(what); });
    }

    menu.setMinimumWidth(m_createBtn->width());
    menu.exec(m_createBtn->mapToGlobal(QPoint(0, m_createBtn->height() + 8)));
}

void HeroBanner::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const QRectF box(0, 0, width(), height());
    QPainterPath clip;
    clip.addRoundedRect(box, kRadius, kRadius);
    p.setClipPath(clip);

    // Base, so the band still reads as a card if the art is missing.
    p.fillRect(box, QColor(Home::kPanel));

    if (!m_art.isNull()) {
        // Cover-crop: scale so the image covers the band, then centre it. The
        // result is cached because a repaint happens on every hover.
        const QSize want = size() * devicePixelRatio();
        if (m_scaled.isNull() || m_scaled.size() != want) {
            m_scaled = m_art.scaled(want, Qt::KeepAspectRatioByExpanding,
                                    Qt::SmoothTransformation);
            m_scaled.setDevicePixelRatio(devicePixelRatio());
        }
        const QSizeF drawn = m_scaled.deviceIndependentSize();
        p.drawPixmap(QPointF((width()  - drawn.width())  / 2.0,
                             (height() - drawn.height()) / 2.0), m_scaled);
    }

    // Left-to-right scrim: opaque panel colour under the text, clear over the
    // picture. Without it the greeting sits on whatever the photo happens to
    // have at that width.
    QColor solid(Home::kPanel);
    QLinearGradient g(0, 0, width(), 0);
    g.setColorAt(0.00, solid);
    QColor mid = solid; mid.setAlphaF(0.90);
    g.setColorAt(0.34, mid);
    QColor soft = solid; soft.setAlphaF(0.45);
    g.setColorAt(kScrimEnd * 0.85, soft);
    QColor clear = solid; clear.setAlphaF(0.0);
    g.setColorAt(kScrimEnd, clear);
    p.fillRect(box, g);

    // A whisper of the brand violet along the bottom edge ties the band to the
    // Create New button beneath it.
    QLinearGradient tint(0, height() * 0.55, 0, height());
    QColor v(Home::kAccent); v.setAlphaF(0.0);
    tint.setColorAt(0.0, v);
    v.setAlphaF(0.10);
    tint.setColorAt(1.0, v);
    p.fillRect(box, tint);

    p.setClipping(false);
    p.setPen(QPen(QColor(Home::kBorder), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
}

} // namespace NativeOffice
