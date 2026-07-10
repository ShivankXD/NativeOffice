// ─────────────────────────────────────────────────────────────────────────────
// StartScreen.cpp — NativeOffice home dashboard (dark theme).
// ─────────────────────────────────────────────────────────────────────────────
#include "StartScreen.h"
#include "core/application/RecentFilesManager.h"
#include "core/application/UpdateChecker.h"
#include "core/auth/AuthManager.h"
#include "core/theme/ThemeManager.h"
#include "common/Avatars.h"
#include "settings/SettingsDialog.h"
#include "LucideIcons.h"

#include <QMessageBox>

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QFrame>
#include <QScrollArea>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QDialog>
#include <QStackedWidget>
#include <QButtonGroup>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QSvgRenderer>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QTime>
#include <QDateTime>
#include <QTimer>
#include <QSettings>
#include <QSpinBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QUrl>
#include <functional>
#include <utility>

namespace NativeOffice {

namespace {

// ── A frame the whole of which responds to a left click. ──────────────────────
class ClickableFrame : public QFrame {
public:
    using QFrame::QFrame;
    std::function<void()> onClick;
protected:
    void enterEvent(QEnterEvent*) override { if (onClick) setCursor(Qt::PointingHandCursor); }
    void mousePressEvent(QMouseEvent* e) override {
        // Consume the click when handled, so it doesn't bubble to a clickable
        // ancestor (e.g. a template card inside the clickable templates panel).
        if (e->button() == Qt::LeftButton && onClick) { onClick(); e->accept(); return; }
        QFrame::mousePressEvent(e);
    }
};

// A small rounded "app badge" label (a coloured square with a letter/glyph).
QLabel* badge(const QString& text, const QString& color, int size = 30, QWidget* parent = nullptr) {
    auto* b = new QLabel(text, parent);
    b->setAlignment(Qt::AlignCenter);
    b->setFixedSize(size, size);
    b->setStyleSheet(QString("background:%1;border-radius:8px;color:#FFFFFF;"
                             "font:700 %2px 'Segoe UI';").arg(color).arg(size <= 24 ? 11 : 14));
    return b;
}

// The real, transparent brand mark at a crisp hi-dpi size (same
// device-pixel-ratio trick as Avatars.h's roundAvatarPixmap()). The source
// artwork is the full lockup (mark + wordmark + strapline); crop out just the
// "N" mark, else the baked-in text turns to mush at UI sizes.
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

QLabel* heading(const QString& text, int px, const QString& color, bool bold, QWidget* p = nullptr) {
    auto* l = new QLabel(text, p);
    l->setStyleSheet(QString("color:%1;font:%2 %3px 'Segoe UI';background:transparent;")
                         .arg(color).arg(bold ? "700" : "400").arg(px));
    return l;
}

// Render an inline SVG illustration into a transparent QLabel at the given
// height (clicks pass through to the parent card).
QLabel* svgArt(const char* svg, int h, QWidget* parent) {
    const QByteArray data(svg);
    QSvgRenderer r(data);
    const QSize def = r.defaultSize();
    const int w = (def.height() > 0) ? def.width() * h / def.height() : h;
    QPixmap pm(w * 2, h * 2);                  // 2× for crispness on hi-dpi
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    r.render(&p);
    p.end();
    pm.setDevicePixelRatio(2.0);
    auto* l = new QLabel(parent);
    l->setPixmap(pm);
    l->setStyleSheet("background:transparent;");
    l->setAttribute(Qt::WA_TransparentForMouseEvents);
    return l;
}

// ── Illustrations for the four "create" tiles ────────────────────────────────
constexpr const char* kArtDocument = R"SVG(
<svg viewBox="0 0 150 110" xmlns="http://www.w3.org/2000/svg">
  <rect x="46" y="14" width="70" height="90" rx="6" fill="#1b2740" stroke="#3b82f6" stroke-width="2"/>
  <rect x="32" y="22" width="70" height="90" rx="6" fill="#22314f" stroke="#60a5fa" stroke-width="2"/>
  <rect x="42" y="34" width="38" height="6" rx="3" fill="#9cc3ff"/>
  <rect x="42" y="48" width="50" height="4" rx="2" fill="#6f93c8"/>
  <rect x="42" y="58" width="50" height="4" rx="2" fill="#6f93c8"/>
  <rect x="42" y="68" width="40" height="4" rx="2" fill="#6f93c8"/>
  <rect x="42" y="78" width="46" height="4" rx="2" fill="#6f93c8"/>
  <rect x="42" y="88" width="30" height="4" rx="2" fill="#6f93c8"/>
</svg>)SVG";

constexpr const char* kArtSpreadsheet = R"SVG(
<svg viewBox="0 0 150 110" xmlns="http://www.w3.org/2000/svg">
  <rect x="33" y="16" width="84" height="82" rx="6" fill="#102a20" stroke="#22c55e" stroke-width="2"/>
  <rect x="33" y="16" width="84" height="17" fill="#16a34a"/>
  <line x1="61" y1="16" x2="61" y2="98" stroke="#2f6f53" stroke-width="1.5"/>
  <line x1="89" y1="16" x2="89" y2="98" stroke="#2f6f53" stroke-width="1.5"/>
  <line x1="33" y1="49" x2="117" y2="49" stroke="#2f6f53" stroke-width="1.5"/>
  <line x1="33" y1="65" x2="117" y2="65" stroke="#2f6f53" stroke-width="1.5"/>
  <line x1="33" y1="81" x2="117" y2="81" stroke="#2f6f53" stroke-width="1.5"/>
  <rect x="37" y="52" width="20" height="10" rx="2" fill="#1e5e44"/>
  <rect x="93" y="68" width="20" height="10" rx="2" fill="#1e5e44"/>
</svg>)SVG";

constexpr const char* kArtPresentation = R"SVG(
<svg viewBox="0 0 150 110" xmlns="http://www.w3.org/2000/svg">
  <rect x="27" y="18" width="96" height="72" rx="6" fill="#3a2418" stroke="#fb923c" stroke-width="2"/>
  <rect x="38" y="28" width="46" height="7" rx="3.5" fill="#fdba74"/>
  <rect x="40" y="72" width="10" height="10" fill="#fb923c"/>
  <rect x="55" y="62" width="10" height="20" fill="#fb923c"/>
  <rect x="70" y="52" width="10" height="30" fill="#fdba74"/>
  <rect x="85" y="58" width="10" height="24" fill="#fb923c"/>
  <line x1="38" y1="82" x2="112" y2="82" stroke="#7c4a2b" stroke-width="1.5"/>
  <rect x="71" y="90" width="8" height="9" fill="#7c4a2b"/>
  <rect x="60" y="99" width="30" height="4" rx="2" fill="#7c4a2b"/>
</svg>)SVG";

constexpr const char* kArtOpenFile = R"SVG(
<svg viewBox="0 0 150 110" xmlns="http://www.w3.org/2000/svg">
  <rect x="48" y="20" width="46" height="40" rx="4" fill="#2a2440" stroke="#a78bfa" stroke-width="2"/>
  <rect x="55" y="29" width="30" height="4" rx="2" fill="#c4b5fd"/>
  <rect x="55" y="39" width="30" height="3" rx="1.5" fill="#8b7bc0"/>
  <rect x="55" y="47" width="22" height="3" rx="1.5" fill="#8b7bc0"/>
  <path d="M28 50 l6 -7 h22 l5 7 h33 v34 a3 3 0 0 1 -3 3 H31 a3 3 0 0 1 -3 -3 z"
        fill="#6d4ed6" stroke="#a78bfa" stroke-width="2" stroke-linejoin="round"/>
  <path d="M27 56 h90 l-9 30 a3 3 0 0 1 -3 2 H21 z"
        fill="#8b6df0" stroke="#a78bfa" stroke-width="1.5" stroke-linejoin="round"/>
</svg>)SVG";

constexpr const char* kArtPdf = R"SVG(
<svg viewBox="0 0 150 110" xmlns="http://www.w3.org/2000/svg">
  <rect x="46" y="12" width="70" height="90" rx="6" fill="#3a1414" stroke="#f87171" stroke-width="2"/>
  <rect x="42" y="30" width="50" height="4" rx="2" fill="#fca5a5"/>
  <rect x="42" y="44" width="50" height="4" rx="2" fill="#c9807e"/>
  <rect x="42" y="58" width="34" height="4" rx="2" fill="#c9807e"/>
  <rect x="55" y="70" width="34" height="18" rx="3" fill="#ef4444"/>
  <path d="M64 74 v10 M60 78 h8" stroke="#3a1414" stroke-width="2" stroke-linecap="round"/>
  <path d="M72 74 v10 h4 a4 4 0 0 0 0 -10 z" fill="none" stroke="#3a1414" stroke-width="2"/>
</svg>)SVG";

} // namespace

StartScreen::StartScreen(AppController* controller, QWidget* parent)
    : QWidget(parent), m_controller(controller)
{
    setObjectName("startScreen");
    buildUi();
}

// While the startup update scan is running, launch actions are suppressed so
// the user can't open an editor before we know whether an update is pending.
bool StartScreen::launchLocked() const {
    return UpdateChecker::instance().isScanning();
}

// Compact update-status pill, shown at the top-right of the home page only.
// Mirrors UpdateChecker: a spinner while scanning/downloading, a short notice
// when up to date or offline, and — when an update is ready — a clickable
// "Restart to update" pill.
QWidget* StartScreen::buildUpdateBanner() {
    auto* pill = new ClickableFrame(this);
    pill->setObjectName("updatePill");
    m_updateBanner = pill;
    pill->setFixedHeight(30);
    auto* h = new QHBoxLayout(pill);
    h->setContentsMargins(12, 0, 12, 0);
    h->setSpacing(7);

    m_updateSpin = new QLabel(pill);
    m_updateSpin->setFixedWidth(12);
    h->addWidget(m_updateSpin);

    m_updateText = new QLabel(pill);
    h->addWidget(m_updateText);

    // Spinner animation.
    m_spinTimer = new QTimer(this);
    connect(m_spinTimer, &QTimer::timeout, this, [this] {
        static const char* frames[] = { "◜", "◝", "◞", "◟" };
        m_updateSpin->setText(QString::fromUtf8(frames[m_spinPhase++ & 3]));
    });

    // React to state + progress changes for the app's lifetime.
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

    // Idle/Failed aren't worth showing; everything else is user-relevant.
    const bool show = s != S::Idle && s != S::Failed;
    m_updateBanner->setVisible(show);
    if (!show) { m_spinTimer->stop(); pill->onClick = nullptr; return; }

    QString text;
    switch (s) {
    case S::Scanning:        text = tr("Scanning for updates"); break;
    case S::UpToDate:        text = tr("You're on the latest version"); break;
    case S::Offline:         text = tr("Offline — update check unavailable"); break;
    case S::UpdateAvailable: text = tr("Preparing update"); break;
    case S::Downloading:     text = tr("Installing update"); break;
    case S::ReadyToRestart:  text = tr("Restart to update"); break;
    default: break;
    }
    m_updateText->setText(text);

    const bool ready = s == S::ReadyToRestart;
    // A ready update turns the pill into a blue call-to-action button.
    if (ready) {
        pill->onClick = [] { UpdateChecker::instance().relaunchForUpdate(); };
        pill->setStyleSheet("QFrame#updatePill { background:#1D4ED8;"
                            " border:1px solid #2E5BE0; border-radius:15px; }");
        m_updateText->setStyleSheet("background:transparent;color:#FFFFFF;font:700 12px 'Segoe UI';");
    } else {
        pill->onClick = nullptr;
        pill->setCursor(Qt::ArrowCursor);
        pill->setStyleSheet("QFrame#updatePill { background:#161B26;"
                            " border:1px solid #263042; border-radius:15px; }");
        m_updateText->setStyleSheet("background:transparent;color:#C7CEDC;font:600 12px 'Segoe UI';");
    }

    const bool spinning = s == S::Scanning || s == S::UpdateAvailable || s == S::Downloading;
    m_updateSpin->setStyleSheet("background:transparent;color:#C7CEDC;font:700 12px 'Segoe UI';");
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
        this, "Open Document", QDir::homePath(),
        "All Supported Files (*.docx *.noff *.html *.txt *.csv *.tsv "
        "*.pptx *.odp *.xlsx *.ods);;All Files (*)");
    if (!path.isEmpty()) emit fileOpenRequested(path);
}

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

    // Scrollable body so the dashboard works on small windows.
    auto* scroll = new QScrollArea(right);
    scroll->setObjectName("bodyScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* body = new QWidget(scroll);
    auto* bl = new QHBoxLayout(body);
    bl->setContentsMargins(36, 28, 36, 28);
    bl->setSpacing(24);
    bl->addWidget(buildCenterColumn(), 1);
    bl->addWidget(buildRightColumn());
    scroll->setWidget(body);

    rl->addWidget(scroll, 1);
    root->addWidget(right, 1);

    setStyleSheet(R"(
        QWidget#startScreen { background:#0D1117; }
        QScrollArea#bodyScroll { background:transparent; }
        QScrollBar:vertical { background:transparent; width:10px; margin:2px; }
        QScrollBar::handle:vertical { background:#2A3240; border-radius:5px; min-height:30px; }
        QScrollBar::add-line, QScrollBar::sub-line { height:0; }
    )");

    // Dev-only capture hook (PrintWindow drops pixmap content on this
    // machine): set NATIVEOFFICE_HOME_GRAB to a .png path to have the home
    // screen grab itself shortly after startup.
    if (qEnvironmentVariableIsSet("NATIVEOFFICE_HOME_GRAB")) {
        const QString grabPath = qEnvironmentVariable("NATIVEOFFICE_HOME_GRAB");
        QTimer::singleShot(6000, this, [this, grabPath] {
            grab().save(grabPath, "PNG");
        });
    }
}

// ── Left sidebar ──────────────────────────────────────────────────────────────
QWidget* StartScreen::buildSidebar() {
    auto* bar = new QWidget(this);
    bar->setObjectName("sidebar");
    bar->setFixedWidth(230);
    auto* v = new QVBoxLayout(bar);
    v->setContentsMargins(14, 18, 14, 18);
    v->setSpacing(4);

    // Logo — two-tone wordmark ("Office" in the logo's violet) beside a
    // larger brand mark.
    auto* logoRow = new QWidget(bar);
    auto* lh = new QHBoxLayout(logoRow);
    lh->setContentsMargins(4, 0, 0, 10); lh->setSpacing(11);
    lh->addWidget(logoMark(40, logoRow));
    auto* brand = new QLabel(logoRow);
    brand->setTextFormat(Qt::RichText);
    brand->setText("<span style='color:#F0F2F7;'>Native</span>"
                   "<span style='color:#8B7CF7;'>Office</span>");
    brand->setStyleSheet("background:transparent; font:700 20px 'Segoe UI';");
    lh->addWidget(brand);
    lh->addStretch();
    v->addWidget(logoRow);
    v->addSpacing(8);

    struct Nav { const char* icon; QString label; int action; };  // action: -1 none, 0 home, see below
    const Nav items[] = {
        {Lucide::kHome,         "Home",      0},
        {Lucide::kFileText,     "Documents", 1},
        {Lucide::kTable,        "Sheets",    2},
        {Lucide::kPresentation, "Slides",    3},
        {Lucide::kDownload,     "PDF",       5},
        {Lucide::kFolderOpen,   "Templates", 4},
        {Lucide::kStar,         "Favorites", -1},
        {Lucide::kTrash,        "Recycle Bin", -1},
    };
    for (const Nav& n : items) {
        auto* item = new ClickableFrame(bar);
        item->setObjectName(n.action == 0 ? "navItemActive" : "navItem");
        auto* il = new QHBoxLayout(item);
        il->setContentsMargins(12, 0, 12, 0); il->setSpacing(12);
        item->setFixedHeight(40);
        auto* ic = Lucide::label(n.icon, n.action == 0 ? "#FFFFFF" : "#9AA4B8", 16, item);
        auto* tx = new QLabel(n.label, item);
        tx->setStyleSheet(QString("background:transparent;font:13px 'Segoe UI';color:%1;")
                              .arg(n.action == 0 ? "#FFFFFF" : "#AEB6C6"));
        il->addWidget(ic); il->addWidget(tx); il->addStretch();
        const int action = n.action;
        item->onClick = [this, action]{
            if (launchLocked()) return;
            switch (action) {
            case 1: emit newDocumentRequested(DocumentType::Writer);  break;
            case 2: emit newDocumentRequested(DocumentType::Calc);    break;
            case 3: emit newDocumentRequested(DocumentType::Impress); break;
            case 4: showTemplatesDialog(0); break;
            case 5: emit newDocumentRequested(DocumentType::Pdf);     break;
            default: break;
            }
        };
        v->addWidget(item);
    }
    v->addStretch();

    // ── Public-beta notice ──────────────────────────────────────────────────
    // A small red BETA pill with a short note, so users know this isn't the
    // final release and how to report problems.
    {
        auto* betaBox = new QWidget(bar);
        auto* bv = new QVBoxLayout(betaBox);
        bv->setContentsMargins(4, 10, 4, 2);
        bv->setSpacing(6);

        auto* pill = new QLabel("BETA", betaBox);
        pill->setAlignment(Qt::AlignCenter);
        pill->setFixedSize(54, 22);
        pill->setStyleSheet("background:#E5484D;color:#FFFFFF;border-radius:6px;"
                            "font:700 11px 'Segoe UI';letter-spacing:1px;");
        bv->addWidget(pill, 0, Qt::AlignLeft);

        auto* note = new QLabel(betaBox);
        note->setTextFormat(Qt::RichText);
        note->setWordWrap(true);
        note->setOpenExternalLinks(true);
        note->setText("Public beta — not the final product. Found an issue? "
                      "Email <a href='mailto:contact@nativeoffice.online' "
                      "style='color:#8B7CF7;text-decoration:none;'>"
                      "contact@nativeoffice.online</a>.");
        note->setStyleSheet("color:#7B8494;font:11px 'Segoe UI';background:transparent;");
        bv->addWidget(note);

        v->addWidget(betaBox);
    }

    bar->setStyleSheet(R"(
        QWidget#sidebar { background:#0A0D13; border-right:1px solid #1B212C; }
        #navItem { background:transparent; border-radius:8px; }
        #navItem:hover { background:#161C28; }
        #navItemActive { background:#17233B; border-radius:8px; }
    )");
    return bar;
}

// ── Top bar ───────────────────────────────────────────────────────────────────
QWidget* StartScreen::buildTopBar() {
    auto* bar = new QWidget(this);
    bar->setObjectName("topBar");
    bar->setFixedHeight(64);
    // Three sections with equal-stretch outer halves so the clock lands on the
    // bar's true midline regardless of how wide the search box or icon
    // cluster are: [search … stretch] [clock] [stretch … icons].
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(28, 0, 24, 0); h->setSpacing(16);

    // Equal minimum widths give both halves identical size hints, so the
    // equal stretch factors keep them the same width at every window size —
    // which pins the clock to the bar's true midline.
    auto* leftBox = new QWidget(bar);
    leftBox->setMinimumWidth(420);
    auto* leftL = new QHBoxLayout(leftBox);
    leftL->setContentsMargins(0, 0, 0, 0); leftL->setSpacing(16);

    auto* rightBox = new QWidget(bar);
    rightBox->setMinimumWidth(420);
    auto* rightL = new QHBoxLayout(rightBox);
    rightL->setContentsMargins(0, 0, 0, 0); rightL->setSpacing(16);

    auto* search = new QLineEdit(bar);
    search->setObjectName("searchBox");
    search->setPlaceholderText("   Search files, templates, tools…");
    search->setFixedHeight(40);
    search->setFixedWidth(320);
    auto* kbd = new QLabel("Ctrl + K", search);
    kbd->setStyleSheet("background:#1B2230;border-radius:5px;color:#8A93A6;"
                       "font:11px 'Segoe UI';padding:2px 8px;");
    auto* sl = new QHBoxLayout(search);
    sl->setContentsMargins(0, 0, 8, 0); sl->addStretch(); sl->addWidget(kbd);

    leftL->addWidget(search);
    leftL->addStretch();
    h->addWidget(leftBox, 1);

    // Live clock — sits between the two equal-width halves, i.e. centered.
    auto* clock = new QLabel(bar);
    clock->setObjectName("liveClock");
    clock->setAlignment(Qt::AlignCenter);
    auto refreshClock = [clock]() {
        clock->setText(QDateTime::currentDateTime().toString("ddd, MMM d · h:mm AP"));
    };
    refreshClock();
    auto* clockTimer = new QTimer(clock);
    connect(clockTimer, &QTimer::timeout, clock, refreshClock);
    clockTimer->start(1000);

    h->addWidget(clock, 0);

    auto iconBtn = [&](const char* svg, std::function<void()> cb) {
        auto* b = new QToolButton(bar);
        b->setIcon(Lucide::icon(svg, "#AEB6C6", 17, bar->devicePixelRatio()));
        b->setIconSize(QSize(17, 17));
        b->setObjectName("topIcon");
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(38, 38);
        if (cb) connect(b, &QToolButton::clicked, this, cb);
        return b;
    };
    rightL->addStretch();
    // Compact update-status pill, top-right of the home page.
    rightL->addWidget(buildUpdateBanner(), 0, Qt::AlignVCenter);
    auto* bellBtn = iconBtn(Lucide::kBell, nullptr);
    connect(bellBtn, &QToolButton::clicked, this,
            [this, bellBtn] { showNotificationsPopup(bellBtn); });
    rightL->addWidget(bellBtn);
    rightL->addWidget(iconBtn(Lucide::kSettings, [this]{ showSettingsDialog(); }));

    // Account avatar — the real profile photo (cached by AuthManager), with a
    // coloured-initial fallback. Clicking it opens Settings → Account.
    auto* avatar = new QToolButton(bar);
    avatar->setObjectName("avatarBtn");
    avatar->setCursor(Qt::PointingHandCursor);
    avatar->setFixedSize(38, 38);
    avatar->setIconSize(QSize(38, 38));
    avatar->setStyleSheet("QToolButton#avatarBtn { border:none; background:transparent; }");
    auto refreshAvatar = [avatar]() {
        auto& auth = AuthManager::instance();
        avatar->setIcon(QIcon(roundAvatarPixmap(
            38, avatar->devicePixelRatio())));
        avatar->setToolTip(auth.userEmail().isEmpty()
                               ? QStringLiteral("Account")
                               : auth.userName() + QStringLiteral("\n") + auth.userEmail());
    };
    refreshAvatar();
    connect(&AuthManager::instance(), &AuthManager::profileChanged,
            avatar, refreshAvatar);
    connect(avatar, &QToolButton::clicked, this, [this]{ showSettingsDialog(); });
    rightL->addWidget(avatar);
    h->addWidget(rightBox, 1);

    bar->setStyleSheet(R"(
        QWidget#topBar { background:#0D1117; border-bottom:1px solid #1B212C; }
        QLineEdit#searchBox { background:#161B26; border:1px solid #232A38;
            border-radius:10px; color:#C7CEDC; padding-left:10px; font:13px 'Segoe UI'; }
        QLabel#liveClock { background:transparent; color:#AEB6C6; font:600 13px 'Segoe UI'; }
        QToolButton#topIcon { background:#161B26; border:1px solid #232A38; border-radius:10px;
            color:#AEB6C6; font:15px 'Segoe UI Emoji'; }
        QToolButton#topIcon:hover { background:#1E2737; }
    )");
    return bar;
}

// ── Center column ─────────────────────────────────────────────────────────────
QWidget* StartScreen::buildCenterColumn() {
    auto* col = new QWidget(this);
    auto* v = new QVBoxLayout(col);
    v->setContentsMargins(0, 0, 0, 0); v->setSpacing(22);

    // Welcome row + Import button
    auto* welcome = new QWidget(col);
    auto* wl = new QHBoxLayout(welcome);
    wl->setContentsMargins(0, 0, 0, 0); wl->setSpacing(8);
    auto* wcol = new QVBoxLayout(); wcol->setSpacing(4);
    // Greeting uses the signed-in account's first name and refreshes when the
    // profile syncs (the home page is built before sign-in completes).
    auto* greetLabel = heading(QString(), 26, "#F0F2F7", true, welcome);
    auto refreshGreeting = [greetLabel]() {
        QString who = AuthManager::instance().displayName();
        if (who.isEmpty()) who = QStringLiteral("there");
        const int hr = QTime::currentTime().hour();
        greetLabel->setText(
              hr < 5  ? QString("Burning the midnight oil, %1 🌙").arg(who)
            : hr < 12 ? QString("Good morning, %1 ☀️").arg(who)
            : hr < 17 ? QString("Good afternoon, %1 👋").arg(who)
                      : QString("Good evening, %1 🌆").arg(who));
    };
    refreshGreeting();
    connect(&AuthManager::instance(), &AuthManager::profileChanged,
            greetLabel, refreshGreeting);
    wcol->addWidget(greetLabel);
    wcol->addWidget(heading("What would you like to create today?", 14, "#8A93A6", false, welcome));
    wl->addLayout(wcol); wl->addStretch();
    auto* importBtn = new QPushButton("⤒  Import File", welcome);
    importBtn->setObjectName("importBtn");
    importBtn->setCursor(Qt::PointingHandCursor);
    importBtn->setFixedHeight(40);
    connect(importBtn, &QPushButton::clicked, this, &StartScreen::openFileDialog);
    wl->addWidget(importBtn, 0, Qt::AlignTop);
    v->addWidget(welcome);

    v->addWidget(buildCreateCards());

    auto* row = new QWidget(col);
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0); rl->setSpacing(20);
    rl->addWidget(buildRecentPanel(), 1);
    rl->addWidget(buildTemplatesPanel(), 1);
    v->addWidget(row);
    v->addStretch();

    col->setStyleSheet(R"(
        QPushButton#importBtn { background:#161B26; border:1px solid #2A3344; border-radius:10px;
            color:#D7DCE6; font:13px 'Segoe UI'; padding:0 16px; }
        QPushButton#importBtn:hover { background:#1E2737; }
    )");
    return col;
}

QWidget* StartScreen::buildCreateCards() {
    auto* w = new QWidget(this);
    auto* g = new QHBoxLayout(w);
    g->setContentsMargins(0, 0, 0, 0); g->setSpacing(18);

    struct Card { QString letter, title, sub, color, grad1, grad2; int action; const char* art; };
    const Card cards[] = {
        {"W","Document","Create a new document","#2563EB","#21314f","#16203a", 1, kArtDocument},
        {"S","Spreadsheet","Create a new spreadsheet","#16A34A","#16352a","#102a20", 2, kArtSpreadsheet},
        {"P","Presentation","Create a new presentation","#EA580C","#3a2418","#2a1810", 3, kArtPresentation},
        {"P","PDF","Merge, split & compress PDFs","#DC2626","#3a1414","#2a1010", 4, kArtPdf},
        {"📁","Open File","Browse and open files","#7C5CFC","#2a2440","#1d1a33", 0, kArtOpenFile},
    };
    for (const Card& c : cards) {
        auto* card = new ClickableFrame(w);
        card->setObjectName("createCard");
        card->setMinimumHeight(200);
        auto* cv = new QVBoxLayout(card);
        cv->setContentsMargins(0, 0, 0, 0); cv->setSpacing(0);

        auto* thumb = new QFrame(card);
        thumb->setFixedHeight(120);
        thumb->setStyleSheet(QString("background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
            "stop:0 %1, stop:1 %2);border-top-left-radius:14px;border-top-right-radius:14px;")
            .arg(c.grad1, c.grad2));
        auto* tv = new QVBoxLayout(thumb);
        tv->setContentsMargins(16, 16, 16, 16);
        tv->addWidget(badge(c.letter, c.color, 40, thumb), 0, Qt::AlignLeft | Qt::AlignTop);
        tv->addStretch();
        // Illustration anchored bottom-right of the thumbnail.
        tv->addWidget(svgArt(c.art, 82, thumb), 0, Qt::AlignRight | Qt::AlignBottom);
        cv->addWidget(thumb);

        auto* foot = new QWidget(card);
        auto* fl = new QVBoxLayout(foot);
        fl->setContentsMargins(16, 12, 16, 14); fl->setSpacing(3);
        fl->addWidget(heading(c.title, 15, "#EAEDF3", true, foot));
        fl->addWidget(heading(c.sub, 12, "#8A93A6", false, foot));
        cv->addWidget(foot);

        const int action = c.action;
        card->onClick = [this, action]{
            if (launchLocked()) return;
            if (action == 0) openFileDialog();
            else emit newDocumentRequested(action == 1 ? DocumentType::Writer
                                          : action == 2 ? DocumentType::Calc
                                          : action == 3 ? DocumentType::Impress
                                                        : DocumentType::Pdf);
        };
        g->addWidget(card, 1);
    }
    w->setStyleSheet(R"(
        #createCard { background:#141A24; border:1px solid #222A38; border-radius:14px; }
        #createCard:hover { border:1px solid #3B82F6; background:#161D29; }
    )");
    return w;
}

QWidget* StartScreen::buildRecentPanel() {
    auto* panel = new QFrame(this);
    panel->setObjectName("panel");
    auto* v = new QVBoxLayout(panel);
    v->setContentsMargins(18, 16, 18, 16); v->setSpacing(12);

    auto* head = new QHBoxLayout();
    head->addWidget(heading("Recent Files", 15, "#EAEDF3", true, panel));
    head->addStretch();
    auto* browse = new ClickableFrame(panel);
    auto* bl = new QHBoxLayout(browse);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->addWidget(heading("Browse…", 12, "#3B82F6", false, browse));
    browse->onClick = [this]{ openFileDialog(); };
    head->addWidget(browse);
    v->addLayout(head);

    // Real recent files from the manager.
    const auto entries = RecentFilesManager::instance().recentFiles();
    if (entries.empty()) {
        v->addWidget(heading("No recent files yet — create or open one to get started.",
                             12, "#6B7280", false, panel));
    }
    int shown = 0;
    for (const auto& e : entries) {
        if (shown++ >= 5) break;
        auto* rowf = new ClickableFrame(panel);
        rowf->setObjectName("recentRow");
        rowf->setFixedHeight(48);
        auto* rh = new QHBoxLayout(rowf);
        rh->setContentsMargins(8, 0, 8, 0); rh->setSpacing(12);
        const QString col = e.type == "Calc" ? "#16A34A" : e.type == "Impress" ? "#EA580C" : "#2563EB";
        const QString ltr = e.type == "Calc" ? "S" : e.type == "Impress" ? "P" : "W";
        rh->addWidget(badge(ltr, col, 30, rowf));
        auto* meta = new QVBoxLayout(); meta->setSpacing(1);
        meta->addWidget(heading(e.name, 13, "#DCE1EA", false, rowf));
        meta->addWidget(heading(e.type, 11, "#7B8494", false, rowf));
        rh->addLayout(meta); rh->addStretch();
        rh->addWidget(heading(e.lastOpened.toString("MMM d, hh:mm"), 11, "#7B8494", false, rowf));
        const QString path = e.path;
        rowf->onClick = [this, path]{ if (launchLocked()) return; emit fileOpenRequested(path); };
        v->addWidget(rowf);
    }
    v->addStretch();

    panel->setStyleSheet(R"(
        QFrame#panel { background:#12161F; border:1px solid #202836; border-radius:14px; }
        #recentRow { background:transparent; border-radius:8px; }
        #recentRow:hover { background:#1A2230; }
    )");
    return panel;
}

QWidget* StartScreen::buildTemplatesPanel() {
    auto* panel = new ClickableFrame(this);
    panel->setObjectName("panel");
    auto* v = new QVBoxLayout(panel);
    v->setContentsMargins(18, 16, 18, 16); v->setSpacing(12);

    auto* head = new QHBoxLayout();
    head->addWidget(heading("Templates for You", 15, "#EAEDF3", true, panel));
    head->addStretch();
    auto* viewAllTpl = new ClickableFrame(panel);
    auto* vaL = new QHBoxLayout(viewAllTpl);
    vaL->setContentsMargins(0, 0, 0, 0);
    vaL->addWidget(heading("View all", 12, "#3B82F6", false, viewAllTpl));
    viewAllTpl->onClick = [this]{ showTemplatesDialog(0); };
    head->addWidget(viewAllTpl);
    v->addLayout(head);

    auto* grid = new QGridLayout();
    grid->setSpacing(12);
    struct T { QString title, sub, grad1, grad2; DocumentType type; };
    const T ts[] = {
        {"Professional Resume","Document","#2a2440","#1d1a33", DocumentType::Writer},
        {"Monthly Budget","Spreadsheet","#16352a","#102a20", DocumentType::Calc},
        {"Pitch Deck","Presentation","#3a2418","#2a1810", DocumentType::Impress},
        {"Project Report","Document","#21314f","#16203a", DocumentType::Writer},
    };
    int i = 0;
    for (const T& t : ts) {
        auto* c = new ClickableFrame(panel);
        c->setObjectName("tplCard");
        c->setMinimumHeight(120);
        auto* cv = new QVBoxLayout(c);
        cv->setContentsMargins(0, 0, 0, 0); cv->setSpacing(0);
        auto* thumb = new QFrame(c);
        thumb->setFixedHeight(78);
        thumb->setStyleSheet(QString("background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
            "stop:0 %1,stop:1 %2);border-top-left-radius:10px;border-top-right-radius:10px;")
            .arg(t.grad1, t.grad2));
        auto* tv = new QHBoxLayout(thumb);
        tv->setContentsMargins(10, 4, 10, 2);
        tv->addStretch();
        tv->addWidget(svgArt(t.type == DocumentType::Calc ? kArtSpreadsheet
                           : t.type == DocumentType::Impress ? kArtPresentation
                           : kArtDocument, 64, thumb), 0, Qt::AlignRight | Qt::AlignVCenter);
        cv->addWidget(thumb);
        auto* foot = new QVBoxLayout(); foot->setContentsMargins(10, 8, 10, 8); foot->setSpacing(1);
        foot->addWidget(heading(t.title, 12, "#E2E6EE", true, c));
        foot->addWidget(heading(t.sub, 11, "#7B8494", false, c));
        cv->addLayout(foot);
        const DocumentType type = t.type;
        const QString name = t.title;
        c->onClick = [this, type, name]{ if (launchLocked()) return; emit templateChosen(type, name); };
        grid->addWidget(c, i / 2, i % 2);
        ++i;
    }
    v->addLayout(grid);
    v->addStretch();

    // Clicking anywhere on the panel header area opens the gallery too.
    panel->onClick = [this]{ showTemplatesDialog(0); };

    panel->setStyleSheet(R"(
        QFrame#panel { background:#12161F; border:1px solid #202836; border-radius:14px; }
        #tplCard { background:#161B26; border:1px solid #232B3A; border-radius:10px; }
        #tplCard:hover { border:1px solid #3B82F6; }
    )");
    return panel;
}

// ── Right column (Get Started, Activity, Sync) ────────────────────────────────
QWidget* StartScreen::buildRightColumn() {
    auto* col = new QWidget(this);
    col->setFixedWidth(300);
    auto* v = new QVBoxLayout(col);
    v->setContentsMargins(0, 0, 0, 0); v->setSpacing(20);

    auto makePanel = [&](const QString& title) {
        auto* p = new QFrame(col);
        p->setObjectName("sidePanel");
        auto* pv = new QVBoxLayout(p);
        pv->setContentsMargins(18, 16, 18, 16); pv->setSpacing(12);
        if (!title.isEmpty()) pv->addWidget(heading(title, 14, "#E6E9F0", true, p));
        return std::make_pair(p, pv);
    };
    auto twoCol = [&](QWidget* parent, const char* icon, const QString& left,
                      const QString& right, const QString& rightColor,
                      std::function<void()> onClick = nullptr) -> QWidget* {
        auto* row = new ClickableFrame(parent);
        row->setObjectName(onClick ? "sideRowLink" : "sideRow");
        if (onClick) {
            row->setCursor(Qt::PointingHandCursor);
            row->onClick = std::move(onClick);
        }
        auto* h = new QHBoxLayout(row); h->setContentsMargins(6, 4, 6, 4); h->setSpacing(10);
        h->addWidget(Lucide::label(icon, "#9AA4B8", 14, row));
        h->addWidget(heading(left, 12, "#C3CAD8", false, row));
        h->addStretch();
        h->addWidget(heading(right, 12, rightColor, false, row));
        return row;
    };

    // Get Started
    { auto [p, pv] = makePanel("Get Started");
      pv->addWidget(twoCol(p, Lucide::kPlay, "Take a tour", "3 min", "#7B8494"));
      pv->addWidget(twoCol(p, Lucide::kKeyboard, "Keyboard shortcuts", "View all", "#3B82F6",
                           [this] { showShortcutsDialog(); }));
      pv->addWidget(twoCol(p, Lucide::kSparkles, "What's new", "See updates", "#3B82F6",
                           [this] { showWhatsNewDialog(); }));
      v->addWidget(p); }

    // Your Activity
    { auto [p, pv] = makePanel("Your Activity");
      const int recent = int(RecentFilesManager::instance().recentFiles().size());
      pv->addWidget(twoCol(p, Lucide::kFileText, "Documents opened", QString::number(recent), "#E6E9F0"));
      pv->addWidget(twoCol(p, Lucide::kPencil, "Files edited", QString::number(recent), "#E6E9F0"));
      pv->addWidget(twoCol(p, Lucide::kTimer, "Time spent", "—", "#E6E9F0"));
      v->addWidget(p); }

    // Tools
    { auto [p, pv] = makePanel("Tools");
      pv->addWidget(twoCol(p, Lucide::kImage, "Image Resizer", "Open", "#3B82F6",
                           [this] { if (launchLocked()) return; emit imageResizerRequested(); }));
      pv->addWidget(twoCol(p, Lucide::kCode, "Markdown Editor", "Open", "#3B82F6",
                           [this] { if (launchLocked()) return; emit markdownEditorRequested(); }));
      v->addWidget(p); }

    v->addStretch();
    col->setStyleSheet(R"(
        QFrame#sidePanel { background:#12161F; border:1px solid #202836; border-radius:14px; }
        #sideRow { background:transparent; }
        #sideRowLink { background:transparent; border-radius:8px; }
        #sideRowLink:hover { background:#161C28; }
    )");
    return col;
}

// ── Settings ── the full sectioned window lives in settings/SettingsDialog ───────
void StartScreen::showSettingsDialog() {
    SettingsDialog dlg(this);
    dlg.exec();
}

// ── Notifications ── dropdown anchored under the bell icon ────────────────────
void StartScreen::showNotificationsPopup(QWidget* anchor) {
    auto* pop = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint);
    pop->setObjectName("notifPop");
    pop->setAttribute(Qt::WA_DeleteOnClose);
    pop->setAttribute(Qt::WA_StyledBackground, true);
    pop->setFixedWidth(360);

    auto* v = new QVBoxLayout(pop);
    v->setContentsMargins(14, 12, 14, 12);
    v->setSpacing(4);
    v->addWidget(heading("Notifications", 14, "#F0F2F7", true, pop));
    v->addSpacing(4);

    auto item = [&](const char* icon, const QString& title, const QString& body,
                    const QString& url) {
        auto* row = new ClickableFrame(pop);
        row->setObjectName("notifItem");
        auto* hl = new QHBoxLayout(row);
        hl->setContentsMargins(10, 8, 10, 8);
        hl->setSpacing(10);
        hl->addWidget(Lucide::label(icon, "#8B7CF7", 16, row), 0, Qt::AlignTop);
        auto* tv = new QVBoxLayout();
        tv->setSpacing(2);
        tv->addWidget(heading(title, 12, "#E6E9F0", true, row));
        auto* b = heading(body, 11, "#9AA4B8", false, row);
        b->setWordWrap(true);
        tv->addWidget(b);
        hl->addLayout(tv, 1);
        if (!url.isEmpty()) {
            row->setCursor(Qt::PointingHandCursor);
            row->onClick = [pop, url] {
                QDesktopServices::openUrl(QUrl(url));
                pop->close();
            };
        }
        v->addWidget(row);
    };

    item(Lucide::kStar, "Our family is growing",
         "The NativeOffice community is increasing day by day — "
         "thanks for being part of this family!",
         QString());
    item(Lucide::kHelp, "Queries or feature requests?",
         "We'd love to hear from you — mail us at contact@nativeoffice.online.",
         QStringLiteral("mailto:contact@nativeoffice.online"));
    item(Lucide::kFileText, "Privacy Policy",
         "Read how NativeOffice handles your data.",
         QStringLiteral("https://nativeoffice.online/privacy"));
    item(Lucide::kFileText, "Terms of Service",
         "The terms that govern your use of NativeOffice.",
         QStringLiteral("https://nativeoffice.online/terms"));

    pop->setStyleSheet(R"(
        QFrame#notifPop { background:#12161F; border:1px solid #2A3344; border-radius:12px; }
        #notifItem { background:transparent; border-radius:8px; }
        #notifItem:hover { background:#161C28; }
    )");
    pop->adjustSize();
    const QPoint bottomRight =
        anchor->mapToGlobal(QPoint(anchor->width(), anchor->height() + 8));
    pop->move(bottomRight - QPoint(pop->width(), 0));
    pop->show();
}

// ── Keyboard shortcuts ── the ones the app actually implements ────────────────
void StartScreen::showShortcutsDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("Keyboard Shortcuts");
    dlg.resize(680, 640);
    dlg.setStyleSheet(R"(
        QDialog { background:#0D1117; }
        QScrollArea { background:transparent; border:none; }
        QScrollArea > QWidget > QWidget { background:transparent; }
    )");

    auto* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(24, 20, 24, 16);
    root->setSpacing(12);
    root->addWidget(heading("Keyboard Shortcuts", 20, "#F0F2F7", true, &dlg));

    auto* scroll = new QScrollArea(&dlg);
    scroll->setWidgetResizable(true);
    auto* page = new QWidget(scroll);
    auto* pv = new QVBoxLayout(page);
    pv->setContentsMargins(0, 4, 12, 4);
    pv->setSpacing(6);

    auto section = [&](const QString& title) {
        pv->addSpacing(10);
        pv->addWidget(heading(title, 14, "#8B7CF7", true, page));
        pv->addSpacing(2);
    };
    auto row = [&](const QString& keys, const QString& what) {
        auto* r = new QWidget(page);
        auto* h = new QHBoxLayout(r);
        h->setContentsMargins(0, 2, 0, 2);
        h->setSpacing(14);
        auto* chip = new QLabel(keys, r);
        chip->setStyleSheet("background:#1B2230; color:#C3CAD8; border-radius:5px;"
                            "padding:3px 10px; font:600 11px 'Segoe UI';");
        chip->setFixedWidth(150);
        chip->setAlignment(Qt::AlignCenter);
        h->addWidget(chip);
        h->addWidget(heading(what, 12, "#AEB6C6", false, r));
        h->addStretch();
        pv->addWidget(r);
    };

    section("Everywhere");
    row("Ctrl + N",           "New document");
    row("Ctrl + O",           "Open a file");
    row("Ctrl + S",           "Save");
    row("Ctrl + Shift + S",   "Save As");
    row("Ctrl + Z",           "Undo");
    row("Ctrl + Y",           "Redo");
    row("Ctrl + X / C / V",   "Cut / Copy / Paste");
    row("Ctrl + A",           "Select all");
    row("Ctrl + W",           "Close document");

    section("Writer (Documents)");
    row("Ctrl + B / I / U",   "Bold / Italic / Underline");
    row("Ctrl + F",           "Find");
    row("Ctrl + H",           "Find and replace");
    row("Ctrl + P",           "Print");
    row("F7",                 "Spelling check");
    row("Ctrl + ] / [",       "Grow / shrink font size");
    row("Ctrl + L / E / R / J", "Align left / center / right / justify");
    row("Ctrl + 1 / 5 / 2",   "Line spacing 1.0 / 1.5 / 2.0");
    row("Ctrl + Alt + 1-3",   "Apply Heading 1–3");
    row("Ctrl + Shift + N",   "Back to Normal style");
    row("Ctrl + Shift + L",   "Bulleted list");
    row("Ctrl + M",           "Increase indent (Shift to decrease)");
    row("Ctrl + K",           "Insert hyperlink");
    row("Ctrl + Enter",       "Page break");
    row("Ctrl + Space",       "Clear formatting");
    row("Shift + F3",         "Change case");
    row("Ctrl + Shift + E",   "Export to PDF");

    section("Sheets (Spreadsheets)");
    row("Ctrl + B / I / U",   "Bold / Italic / Underline");
    row("Ctrl + D",           "Fill down");
    row("Ctrl + R",           "Fill right");
    row("Delete",             "Clear selected cells");

    section("Slides (Presentations)");
    row("F5",                 "Start slide show");
    row("Shift + F5",         "Slide show from current slide");
    row("Esc",                "Exit slide show");
    row("→ / Space",          "Next slide");
    row("← / Backspace",      "Previous slide");
    row("B",                  "Black screen during show");
    row("Ctrl + M",           "New slide");
    row("Delete",             "Delete selected object");
    row("Ctrl + Shift + E",   "Export to PDF");

    section("PDF");
    row("Ctrl + F",           "Find in document");
    row("Ctrl + P",           "Print");
    row("Esc",                "Exit read mode");

    pv->addStretch();
    scroll->setWidget(page);
    root->addWidget(scroll, 1);

    auto* closeBtn = new QPushButton("Close", &dlg);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet("QPushButton { background:#6D5BE8; color:#FFFFFF; border:none;"
                            "border-radius:8px; padding:8px 22px; font:600 12px 'Segoe UI'; }"
                            "QPushButton:hover { background:#7E6DF0; }");
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    auto* br = new QHBoxLayout();
    br->addStretch();
    br->addWidget(closeBtn);
    root->addLayout(br);

    dlg.exec();
}

// ── What's new ── recent release highlights ───────────────────────────────────
void StartScreen::showWhatsNewDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("What's New");
    dlg.resize(520, 480);
    dlg.setStyleSheet("QDialog { background:#0D1117; }");

    auto* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(24, 20, 24, 16);
    root->setSpacing(14);
    root->addWidget(heading("What's New in NativeOffice", 20, "#F0F2F7", true, &dlg));

    auto item = [&](const char* icon, const QString& title, const QString& body) {
        auto* card = new QFrame(&dlg);
        card->setObjectName("newsCard");
        card->setStyleSheet("#newsCard { background:#12161F; border:1px solid #202836;"
                            "border-radius:10px; }");
        auto* h = new QHBoxLayout(card);
        h->setContentsMargins(14, 12, 14, 12);
        h->setSpacing(12);
        h->addWidget(Lucide::label(icon, "#8B7CF7", 18, card), 0, Qt::AlignTop);
        auto* tv = new QVBoxLayout();
        tv->setSpacing(3);
        tv->addWidget(heading(title, 13, "#E6E9F0", true, card));
        auto* b = heading(body, 12, "#9AA4B8", false, card);
        b->setWordWrap(true);
        tv->addWidget(b);
        h->addLayout(tv, 1);
        root->addWidget(card);
    };

    item(Lucide::kDownload, "PDF editor has arrived",
         "A full PDF workspace: view, edit, annotate, fill forms, protect with "
         "passwords, sign, convert and OCR — right inside NativeOffice.");
    item(Lucide::kKeyboard, "Full keyboard shortcut suite",
         "Familiar shortcuts now work across Writer, Sheets, Slides and PDF. "
         "See “Keyboard shortcuts” in Get Started for the full list.");
    item(Lucide::kSparkles, "A fresh, unified look",
         "New logo tray in every editor, a clean white theme with violet "
         "accents, and your Free/Premium plan badge at a glance.");
    item(Lucide::kTimer, "Coming soon: Image Resizer",
         "Resize and compress images without leaving NativeOffice. "
         "It’s on the way!");

    root->addStretch();

    auto* closeBtn = new QPushButton("Nice!", &dlg);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet("QPushButton { background:#6D5BE8; color:#FFFFFF; border:none;"
                            "border-radius:8px; padding:8px 24px; font:600 12px 'Segoe UI'; }"
                            "QPushButton:hover { background:#7E6DF0; }");
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    auto* br = new QHBoxLayout();
    br->addStretch();
    br->addWidget(closeBtn);
    root->addLayout(br);

    dlg.exec();
}

// ── Templates gallery dialog ──────────────────────────────────────────────────
void StartScreen::showTemplatesDialog(int initialCategory) {
    QDialog dlg(this);
    dlg.setObjectName("tplDialog");
    dlg.setWindowTitle("Templates");
    dlg.resize(820, 620);
    auto* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(24, 20, 24, 20); root->setSpacing(16);

    root->addWidget(heading("Choose a Template", 20, "#F0F2F7", true, &dlg));

    // Category selector.
    auto* catRow = new QHBoxLayout(); catRow->setSpacing(10);
    auto* catGroup = new QButtonGroup(&dlg);
    catGroup->setExclusive(true);
    const QStringList cats = { "📄  Word Templates", "▦  Spreadsheet Templates", "▤  PowerPoint Templates" };
    for (int i = 0; i < cats.size(); ++i) {
        auto* b = new QToolButton(&dlg);
        b->setText(cats[i]); b->setCheckable(true); b->setCursor(Qt::PointingHandCursor);
        b->setObjectName("catBtn"); b->setFixedHeight(38);
        catGroup->addButton(b, i);
        catRow->addWidget(b);
    }
    catRow->addStretch();
    root->addLayout(catRow);

    auto* stack = new QStackedWidget(&dlg);
    root->addWidget(stack, 1);

    struct Cat { DocumentType type; QString accent; QStringList names; };
    const Cat data[] = {
        { DocumentType::Writer, "#2563EB",
          {"Blank Document","Professional Resume","Modern Resume","Cover Letter",
           "Business Letter","Project Report","Meeting Notes","Newsletter",
           "Invoice Letter","To-Do List","Academic Essay","Press Release"} },
        { DocumentType::Calc, "#16A34A",
          {"Blank Spreadsheet","Monthly Budget","Invoice","Expense Tracker",
           "Sales Dashboard","Inventory List","Loan Calculator","Timesheet",
           "Grade Book","Habit Tracker","Attendance Sheet","Savings Goal"} },
        { DocumentType::Impress, "#EA580C",
          {"Blank Presentation","Pitch Deck","Business Review","Project Plan",
           "Portfolio","Marketing Plan","Company Profile","Product Roadmap",
           "Training Deck"} },
    };
    for (const Cat& cat : data) {
        auto* pageScroll = new QScrollArea(stack);
        pageScroll->setWidgetResizable(true);
        pageScroll->setFrameShape(QFrame::NoFrame);
        pageScroll->setStyleSheet("background:transparent;");
        auto* page = new QWidget(pageScroll);
        auto* pv = new QVBoxLayout(page);
        pv->setContentsMargins(0, 4, 6, 0); pv->setSpacing(0);
        auto* grid = new QGridLayout();
        grid->setSpacing(14);
        int i = 0;
        for (const QString& name : cat.names) {
            auto* c = new ClickableFrame(page);
            c->setObjectName("galleryCard");
            c->setMinimumSize(160, 140);
            auto* cv = new QVBoxLayout(c);
            cv->setContentsMargins(0, 0, 0, 0); cv->setSpacing(0);
            auto* thumb = new QFrame(c);
            thumb->setFixedHeight(96);
            // Qt parses 8-digit hex as #AARGGBB, so the alpha goes FIRST —
            // appending it ("#2563EB"+"33") silently shifted the channels and
            // painted the Word thumbs green.
            thumb->setStyleSheet(QString("background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
                "stop:0 %1,stop:1 #11151d);border-top-left-radius:10px;border-top-right-radius:10px;")
                .arg("#33" + cat.accent.mid(1)));   // translucent accent
            auto* tl = new QHBoxLayout(thumb); tl->setContentsMargins(12, 10, 12, 6);
            tl->addWidget(badge(cat.type == DocumentType::Calc ? "S"
                              : cat.type == DocumentType::Impress ? "P" : "W",
                              cat.accent, 28, thumb), 0, Qt::AlignLeft | Qt::AlignTop);
            tl->addStretch();
            tl->addWidget(svgArt(cat.type == DocumentType::Calc ? kArtSpreadsheet
                               : cat.type == DocumentType::Impress ? kArtPresentation
                               : kArtDocument, 74, thumb), 0, Qt::AlignRight | Qt::AlignBottom);
            cv->addWidget(thumb);
            auto* lbl = heading(name, 12, "#E2E6EE", false, c);
            lbl->setWordWrap(true);
            lbl->setContentsMargins(10, 8, 10, 10);
            cv->addWidget(lbl);
            const DocumentType type = cat.type;
            const bool blank = name.startsWith("Blank");
            const QString tplName = name;
            c->onClick = [this, type, blank, tplName, &dlg]{
                if (launchLocked()) return;
                if (blank) emit newDocumentRequested(type);
                else       emit templateChosen(type, tplName);
                dlg.accept();
            };
            grid->addWidget(c, i / 4, i % 4);
            ++i;
        }
        pv->addLayout(grid);
        pv->addStretch();
        pageScroll->setWidget(page);
        stack->addWidget(pageScroll);
    }

    connect(catGroup, &QButtonGroup::idClicked, stack, &QStackedWidget::setCurrentIndex);
    catGroup->button(qBound(0, initialCategory, 2))->setChecked(true);
    stack->setCurrentIndex(qBound(0, initialCategory, 2));

    dlg.setStyleSheet(R"(
        QDialog#tplDialog { background:#0D1117; }
        QToolButton#catBtn { background:#161B26; border:1px solid #232A38; border-radius:9px;
            color:#AEB6C6; font:12px 'Segoe UI'; padding:0 16px; }
        QToolButton#catBtn:hover { background:#1E2737; }
        QToolButton#catBtn:checked { background:#17233B; border:1px solid #3B82F6; color:#FFFFFF; }
        #galleryCard { background:#141A24; border:1px solid #232B3A; border-radius:10px; }
        #galleryCard:hover { border:1px solid #3B82F6; background:#161D29; }
    )");
    dlg.exec();
}

} // namespace NativeOffice
