// ─────────────────────────────────────────────────────────────────────────────
// SettingsTray.cpp — see SettingsTray.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "SettingsTray.h"
#include "common/Avatars.h"
#include "core/auth/AuthManager.h"
#include "core/watermark/Watermark.h"
#include "core/settings/ActivityLog.h"
#include "core/settings/UsageStats.h"
#include "startscreen/HomeKit.h"
#include "startscreen/LucideIcons.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QEasingCurve>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace NativeOffice {

namespace {

QLabel* text(const QString& s, int px, const QString& color, bool bold, QWidget* parent) {
    auto* l = new QLabel(s, parent);
    l->setStyleSheet(QString("background:transparent; color:%1; font:%2 %3px 'Segoe UI';")
                         .arg(color, bold ? "600" : "400", QString::number(px)));
    return l;
}

QFrame* divider(QWidget* parent) {
    auto* d = new QFrame(parent);
    d->setFixedHeight(1);
    d->setStyleSheet("background:#1B212C; border:none;");
    return d;
}

// "label ............ value" row.
QWidget* infoRow(const QString& label, QLabel*& valueOut, QWidget* parent) {
    auto* row = new QWidget(parent);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(text(label, 13, "#8A93A6", false, row));
    h->addStretch();
    valueOut = text(QString(), 13, "#E6E9F0", true, row);
    h->addWidget(valueOut);
    return row;
}

// A full-width action button with a Lucide icon on the left.
QPushButton* actionButton(const char* svg, const QString& label, QWidget* parent) {
    auto* b = new QPushButton(label, parent);
    b->setIcon(Lucide::icon(svg, "#AEB6C6", 16, parent->devicePixelRatio()));
    b->setIconSize(QSize(16, 16));
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

} // namespace

SettingsTray::SettingsTray(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("settingsTray");
    setAttribute(Qt::WA_StyledBackground, false);
    hide();
    if (parent) parent->installEventFilter(this);

    m_panel = qobject_cast<QFrame*>(buildPanel());

    m_anim = new QPropertyAnimation(m_panel, "pos", this);
    m_anim->setDuration(240);
    connect(m_anim, &QPropertyAnimation::finished, this, [this] {
        if (!m_open) hide();
    });
}

// ── Panel: [ left nav ] | [ header + stacked pages ] ─────────────────────────
QWidget* SettingsTray::buildPanel() {
    auto* panel = new QFrame(this);
    panel->setObjectName("settingsPanel");
    panel->setStyleSheet(R"(
        QFrame#settingsPanel { background:#0D1117; border-left:1px solid #1B212C; }
        QLabel { background:transparent; }
        QCheckBox { color:#C7CEDC; font:13px 'Segoe UI'; spacing:9px; }
        QCheckBox::indicator { width:17px; height:17px; border-radius:5px;
            border:1px solid #2A3242; background:#12161F; }
        QCheckBox::indicator:checked { background:#6D5BE8; border:1px solid #6D5BE8;
            image:url(:/assets/check-white.png); }
        QCheckBox::indicator:hover { border:1px solid #6D5BE8; }
        QSpinBox { background:#12161F; border:1px solid #232A38; border-radius:8px;
            color:#E2E6EE; padding:5px 8px; font:13px 'Segoe UI'; min-width:78px; }
        QPushButton { background:#141A24; border:1px solid #212A38; border-radius:11px;
            color:#E6E9F0; font:600 13px 'Segoe UI'; padding:11px 14px; text-align:left; }
        QPushButton:hover { background:#1B2331; border:1px solid #2E3A50; }
        QPushButton#dangerBtn { background:transparent; color:#F0736A; border:1px solid #3A2226; }
        QPushButton#dangerBtn:hover { background:#2A1518; border:1px solid #5A2B2B; }
        QScrollArea, QScrollArea > QWidget > QWidget { background:transparent; border:none; }
        QScrollBar:vertical { background:transparent; width:8px; margin:2px; }
        QScrollBar::handle:vertical { background:#2A3040; min-height:30px; border-radius:4px; }
        QScrollBar::handle:vertical:hover { background:#3A4258; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
    )");

    auto* h = new QHBoxLayout(panel);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    h->addWidget(buildSidebar());

    // ── Right side: header + stacked pages ───────────────────────────────────
    auto* right = new QWidget(panel);
    auto* rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(0);

    auto* header = new QWidget(right);
    auto* hh = new QHBoxLayout(header);
    hh->setContentsMargins(22, 18, 14, 12);
    m_title = text("Settings", 18, "#F4F6FB", true, header);
    hh->addWidget(m_title);
    hh->addStretch();
    auto* closeBtn = new QToolButton(header);
    closeBtn->setText(QString::fromUtf8("→"));
    closeBtn->setToolTip(tr("Hide"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFixedSize(30, 30);
    closeBtn->setStyleSheet(
        "QToolButton { background:#141A24; border:1px solid #212A38; border-radius:8px;"
        "  color:#AEB6C6; font:16px 'Segoe UI'; }"
        "QToolButton:hover { background:#1B2331; color:#FFFFFF; }");
    connect(closeBtn, &QToolButton::clicked, this, &SettingsTray::closeTray);
    hh->addWidget(closeBtn);
    rv->addWidget(header);

    auto* scroll = new QScrollArea(right);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumWidth(0);          // let the panel keep its fixed width…
    right->setMinimumWidth(0);           // …instead of the content forcing it wider
    m_stack = new QStackedWidget(scroll);
    m_stack->addWidget(buildProfilePage());   // index 0 = Profile
    m_stack->addWidget(buildSettingsPage());  // index 1 = Settings
    m_stack->addWidget(buildPremiumPage());   // index 2 = Premium
    scroll->setWidget(m_stack);
    rv->addWidget(scroll, 1);

    h->addWidget(right, 1);
    return panel;
}

// ── Left navigation (Profile / Settings) ─────────────────────────────────────
QWidget* SettingsTray::buildSidebar() {
    auto* bar = new QWidget(m_panel);
    bar->setObjectName("traySidebar");
    bar->setFixedWidth(134);
    bar->setStyleSheet(R"(
        QWidget#traySidebar { background:#0A0D12; border-right:1px solid #171D28; }
        QToolButton { background:transparent; border:none; border-radius:10px;
            color:#9AA4B8; font:600 13px 'Segoe UI'; padding:9px 12px; text-align:left; }
        QToolButton:hover { background:#141A24; color:#D6DBE6; }
        QToolButton:checked { background:#1A1733; color:#B7A6FF; }
    )");

    auto* v = new QVBoxLayout(bar);
    v->setContentsMargins(12, 20, 12, 16);
    v->setSpacing(6);
    v->addWidget(text("  NativeOffice", 11, "#5A6373", true, bar));
    v->addSpacing(6);

    auto mkNav = [&](const char* svg, const QString& label) {
        auto* b = new QToolButton(bar);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setIcon(Lucide::icon(svg, "#9AA4B8", 17, bar->devicePixelRatio()));
        b->setIconSize(QSize(17, 17));
        b->setText("  " + label);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        v->addWidget(b);
        return b;
    };
    m_navProfile  = mkNav(Lucide::kUser,     tr("Profile"));
    m_navSettings = mkNav(Lucide::kSettings, tr("Settings"));
    m_navPremium  = mkNav(Lucide::kSparkle,  tr("Premium"));

    auto* group = new QButtonGroup(bar);
    group->setExclusive(true);
    group->addButton(m_navProfile,  Profile);
    group->addButton(m_navSettings, Settings);
    group->addButton(m_navPremium,  Premium);
    connect(m_navProfile,  &QToolButton::clicked, this, [this] { selectView(Profile); });
    connect(m_navSettings, &QToolButton::clicked, this, [this] { selectView(Settings); });
    connect(m_navPremium,  &QToolButton::clicked, this, [this] { selectView(Premium); });

    v->addStretch();
    return bar;
}

// ── Profile page ─────────────────────────────────────────────────────────────
// A banner with the account photo, the plan it is on, what the account has
// actually done in the app, and the handful of actions that belong to it. The
// old page was a stack of identical full-width grey buttons with no hierarchy;
// this gives the identity the top of the page and demotes the links to rows.
QWidget* SettingsTray::buildProfilePage() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(20, 4, 20, 20);
    v->setSpacing(14);

    auto& auth = AuthManager::instance();

    // ── Identity banner ─────────────────────────────────────────────────────
    auto* banner = new QFrame(page);
    banner->setObjectName("idBanner");
    banner->setStyleSheet(
        "QFrame#idBanner { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        "  stop:0 #1B1739, stop:0.55 #141826, stop:1 #241C3C);"
        "  border:1px solid #2E2857; border-radius:16px; }");
    auto* bv = new QVBoxLayout(banner);
    bv->setContentsMargins(20, 18, 20, 18);
    bv->setSpacing(13);

    auto* top = new QHBoxLayout();
    top->setSpacing(15);

    // The photo sits inside a soft ring, so it reads as a portrait rather than
    // as a circle floating on the gradient.
    auto* ring = new QFrame(banner);
    ring->setFixedSize(78, 78);
    ring->setStyleSheet("background:rgba(124,108,246,0.14);"
                        "border:1px solid rgba(156,143,255,0.42);"
                        "border-radius:39px;");
    auto* ringL = new QHBoxLayout(ring);
    ringL->setContentsMargins(0, 0, 0, 0);
    auto* avatar = new QLabel(ring);
    avatar->setFixedSize(64, 64);
    avatar->setStyleSheet("background:transparent;border:none;");
    ringL->addWidget(avatar, 0, Qt::AlignCenter);
    top->addWidget(ring, 0, Qt::AlignTop);

    auto* idCol = new QVBoxLayout();
    idCol->setSpacing(3);
    auto* nameLbl  = text(QString(), 19, "#F4F6FB", true, banner);
    auto* emailLbl = text(QString(), 12, "#8A93A6", false, banner);
    idCol->addWidget(nameLbl);
    idCol->addWidget(emailLbl);
    idCol->addSpacing(6);
    auto* planBadge = new QLabel(banner);
    planBadge->setFixedHeight(23);
    planBadge->setAlignment(Qt::AlignCenter);
    { auto* pr = new QHBoxLayout(); pr->setContentsMargins(0, 0, 0, 0);
      pr->addWidget(planBadge, 0, Qt::AlignLeft); pr->addStretch(); idCol->addLayout(pr); }
    top->addLayout(idCol, 1);
    top->setAlignment(Qt::AlignTop);
    bv->addLayout(top);

    bv->addWidget(divider(banner));

    QLabel *memberVal = nullptr, *occVal = nullptr;
    bv->addWidget(infoRow(tr("Member since"), memberVal, banner));
    bv->addWidget(infoRow(tr("Occupation"),   occVal,    banner));
    v->addWidget(banner);

    // ── What this account has actually done ─────────────────────────────────
    auto* stats = new QGridLayout();
    stats->setSpacing(10);
    int statIndex = 0;
    auto statTile = [&](const char* icon, const QString& caption, const QString& value) {
        auto* f = new QFrame(page);
        f->setObjectName("statTile");
        auto* fv = new QVBoxLayout(f);
        fv->setContentsMargins(12, 10, 12, 10);
        fv->setSpacing(4);
        auto* head = new QHBoxLayout();
        head->setSpacing(6);
        head->addWidget(Lucide::label(icon, "#7E8799", 13, f));
        head->addWidget(text(caption, 10, "#7E8799", false, f));
        head->addStretch();
        fv->addLayout(head);
        fv->addWidget(text(value, 16, "#F0F2F7", true, f));
        stats->addWidget(f, statIndex / 2, statIndex % 2);
        ++statIndex;
    };
    {
        auto& usage = UsageStats::instance();
        statTile(Lucide::kFileText, tr("Opened"), QString::number(usage.documentsOpened()));
        statTile(Lucide::kPencil,   tr("Edited"), QString::number(usage.filesEdited()));
        statTile(Lucide::kTimer,    tr("Time"),   usage.formattedTotal());
        statTile(Lucide::kRepeat,   tr("Streak"),
                 tr("%1 d").arg(ActivityLog::instance().currentStreakDays()));
    }
    v->addLayout(stats);

    // ── Plan card ───────────────────────────────────────────────────────────
    auto* planCard = new QFrame(page);
    planCard->setObjectName("planCard");
    auto* pv = new QVBoxLayout(planCard);
    pv->setContentsMargins(18, 16, 18, 16);
    pv->setSpacing(11);

    auto* planHead = new QHBoxLayout();
    planHead->setSpacing(11);
    auto* crown = new QLabel(planCard);
    crown->setFixedSize(36, 36);
    crown->setAlignment(Qt::AlignCenter);
    crown->setPixmap(Lucide::pixmap(Lucide::kCrown, "#F5C453", 18, page->devicePixelRatio()));
    crown->setStyleSheet("background:rgba(245,196,83,0.14);border-radius:11px;");
    planHead->addWidget(crown, 0, Qt::AlignTop);
    auto* planCol = new QVBoxLayout();
    planCol->setSpacing(2);
    auto* planTitle = text(QString(), 14, "#F0F2F7", true, planCard);
    auto* planSub   = text(QString(), 11, "#7E8799", false, planCard);
    planSub->setWordWrap(true);
    planCol->addWidget(planTitle);
    planCol->addWidget(planSub);
    planHead->addLayout(planCol, 1);
    pv->addLayout(planHead);

    auto* perkBox = new QWidget(planCard);
    auto* perkV = new QVBoxLayout(perkBox);
    perkV->setContentsMargins(0, 0, 0, 0);
    perkV->setSpacing(6);
    const QStringList perks = { tr("No export watermark"), tr("Advanced PDF tools"),
                                tr("Premium export defaults"), tr("Priority support") };
    for (const QString& perk : perks) {
        auto* r = new QWidget(perkBox);
        auto* rl = new QHBoxLayout(r);
        rl->setContentsMargins(2, 0, 0, 0);
        rl->setSpacing(9);
        rl->addWidget(Lucide::label(Lucide::kCircleCheck, "#22C55E", 14, r));
        rl->addWidget(text(perk, 12, "#C6CCDA", false, r));
        rl->addStretch();
        perkV->addWidget(r);
    }
    pv->addWidget(perkBox);

    auto* planBtn = new QPushButton(planCard);
    planBtn->setObjectName("planCta");
    planBtn->setCursor(Qt::PointingHandCursor);
    planBtn->setFixedHeight(40);
    connect(planBtn, &QPushButton::clicked, page, [] {
        auto& a = AuthManager::instance();
        a.premiumActive() ? a.openAccountPage() : a.openPremiumPage();
    });
    pv->addWidget(planBtn);
    v->addWidget(planCard);

    // ── Account links ───────────────────────────────────────────────────────
    auto* rows = new QFrame(page);
    rows->setObjectName("rowGroup");
    auto* rowsV = new QVBoxLayout(rows);
    rowsV->setContentsMargins(6, 6, 6, 6);
    rowsV->setSpacing(2);

    auto linkRow = [&](const char* icon, const QString& label, const QString& hint,
                       std::function<void()> cb) {
        auto* r = new ClickableFrame(rows);
        r->setObjectName("acctRow");
        r->setFixedHeight(46);
        r->setCursor(Qt::PointingHandCursor);
        auto* rl = new QHBoxLayout(r);
        rl->setContentsMargins(11, 0, 11, 0);
        rl->setSpacing(12);
        rl->addWidget(Lucide::label(icon, "#9AA4B8", 16, r));
        auto* col = new QVBoxLayout();
        col->setSpacing(0);
        auto* l1 = text(label, 13, "#E6E9F0", true, r);
        l1->setAttribute(Qt::WA_TransparentForMouseEvents);
        col->addWidget(l1);
        auto* l2 = text(hint, 11, "#6B7280", false, r);
        l2->setAttribute(Qt::WA_TransparentForMouseEvents);
        col->addWidget(l2);
        rl->addLayout(col, 1);
        rl->addWidget(Lucide::label(Lucide::kChevronRight, "#5A6373", 15, r));
        r->onClick = std::move(cb);
        rowsV->addWidget(r);
    };
    linkRow(Lucide::kPen, tr("Edit profile"), tr("Name, photo and occupation"),
            [] { AuthManager::instance().openAccountPage(); });
    linkRow(Lucide::kKey, tr("Activate a product key"), tr("Redeem a Premium key"),
            [] { AuthManager::instance().openActivateKeyPage(); });
    linkRow(Lucide::kUser, tr("Manage account"), tr("Billing and sign-in on the website"),
            [] { AuthManager::instance().openAccountPage(); });
    v->addWidget(rows);

    {
        // Word-wrapped: a single long line here is wider than the tray and the
        // page's scroll area has no horizontal bar, so it would just be clipped.
        auto* note = text(tr("Your profile lives on nativeoffice.online. Changes there "
                             "sync back to this computer on their own."),
                          11, "#6B7280", false, page);
        note->setWordWrap(true);
        v->addWidget(note);
    }
    v->addStretch();

    // ── Sign out ────────────────────────────────────────────────────────────
    auto* out = actionButton(Lucide::kLogOut, tr("Sign out"), page);
    out->setObjectName("dangerBtn");
    out->setIcon(Lucide::icon(Lucide::kLogOut, "#F0736A", 16, page->devicePixelRatio()));
    connect(out, &QPushButton::clicked, this, [this] {
        const auto btn = QMessageBox::question(this, tr("Sign Out"),
            tr("Sign out of NativeOffice on this computer?\n"
               "You'll be asked to sign in again next time."),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (btn != QMessageBox::Yes) return;
        closeTray();
        AuthManager::instance().signOut();
    });
    v->addWidget(out);

    page->setStyleSheet(R"(
        QFrame#statTile { background:#111621; border:1px solid #1F2735; border-radius:12px; }
        QFrame#planCard { background:#111621; border:1px solid #2A2350; border-radius:14px; }
        QFrame#rowGroup { background:#111621; border:1px solid #1F2735; border-radius:14px; }
        #acctRow { background:transparent; border-radius:10px; }
        #acctRow:hover { background:#1B2331; }
        QPushButton#planCta { background:qlineargradient(x1:0,y1:0,x2:1,y2:0,
            stop:0 #E0A93B, stop:1 #F0C558); border:none; border-radius:10px;
            color:#221A05; font:700 13px 'Segoe UI'; text-align:center; }
        QPushButton#planCta:hover { background:qlineargradient(x1:0,y1:0,x2:1,y2:0,
            stop:0 #EFBA4C, stop:1 #FFD76D); }
    )");

    auto refresh = [avatar, nameLbl, emailLbl, planBadge, memberVal, occVal,
                    planTitle, planSub, planBtn, perkBox] {
        auto& a = AuthManager::instance();
        avatar->setPixmap(roundAvatarPixmap(64, avatar->devicePixelRatio()));
        nameLbl->setText(a.userName().isEmpty() ? QStringLiteral("Not signed in") : a.userName());
        emailLbl->setText(a.userEmail());

        const bool premium = a.premiumActive();
        planBadge->setText(premium ? a.premiumPlanLabel().toUpper() : QStringLiteral("FREE PLAN"));
        planBadge->setStyleSheet(QString(
            "border-radius:11px; padding:2px 12px; font:700 10px 'Segoe UI'; letter-spacing:1px;"
            "background:%1; color:%2; border:1px solid %3;")
            .arg(premium ? "#211B3D" : "#161B26",
                 premium ? "#C9BCFF" : "#8A93A6",
                 premium ? "#3B2F70" : "#232A38"));

        memberVal->setText(a.joinedAt().isValid() ? a.joinedAt().toString("MMMM yyyy")
                                                  : QStringLiteral("Not set"));
        occVal->setText(a.occupation().isEmpty() ? QStringLiteral("Not set") : a.occupation());

        planTitle->setText(premium ? a.premiumPlanLabel()
                                   : QCoreApplication::translate("SettingsTray",
                                                                 "NativeOffice Pro"));
        if (premium) {
            const QDateTime until = a.premiumUntil();
            planSub->setText(until.isValid()
                ? QCoreApplication::translate("SettingsTray",
                      "Active until %1. Thank you for supporting the app.")
                      .arg(until.toString(QStringLiteral("d MMMM yyyy")))
                : QCoreApplication::translate("SettingsTray",
                      "Active. Thank you for supporting the app."));
        } else {
            planSub->setText(QCoreApplication::translate("SettingsTray",
                "Unlock the full power of NativeOffice."));
        }
        perkBox->setVisible(!premium);
        planBtn->setText(premium
            ? QCoreApplication::translate("SettingsTray", "Manage subscription")
            : QCoreApplication::translate("SettingsTray", "Upgrade to Pro"));
    };
    refresh();
    connect(&auth, &AuthManager::profileChanged,     page, refresh);
    connect(&auth, &AuthManager::entitlementChanged, page, [refresh](bool) { refresh(); });

    return page;
}

// ── Settings page (app preferences; auto-save) ───────────────────────────────
QWidget* SettingsTray::buildSettingsPage() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(22, 6, 22, 22);
    v->setSpacing(13);
    QSettings st;

    v->addWidget(text(tr("General"), 13, "#8A93A6", true, page));
    auto addCheck = [&](const QString& label, const QString& key, bool def) {
        auto* c = new QCheckBox(label, page);
        c->setChecked(st.value(key, def).toBool());
        connect(c, &QCheckBox::toggled, page, [key](bool on) { QSettings().setValue(key, on); });
        v->addWidget(c);
    };
    addCheck(tr("Show splash screen on startup"), "app/showSplash", true);

    v->addSpacing(6);
    v->addWidget(text(tr("Writer"), 13, "#8A93A6", true, page));
    addCheck(tr("Spell check on by default"),    "writer/spellDefault", true);
    addCheck(tr("AutoCorrect as you type"),      "writer/autoCorrect",  true);
    addCheck(tr("Show rulers in new documents"), "writer/rulers",       true);

    auto addSpin = [&](const QString& label, const QString& key, int def,
                       int lo, int hi, int step, const QString& suffix) {
        auto* row = new QHBoxLayout();
        row->addWidget(text(label, 13, "#C7CEDC", false, page));
        row->addStretch();
        auto* s = new QSpinBox(page);
        s->setRange(lo, hi); s->setSingleStep(step); s->setSuffix(suffix);
        s->setValue(st.value(key, def).toInt());
        connect(s, QOverload<int>::of(&QSpinBox::valueChanged), page,
                [key](int val) { QSettings().setValue(key, val); });
        row->addWidget(s);
        v->addLayout(row);
    };
    addSpin(tr("Autosave interval"), "writer/autosaveSec", 20, 5, 300, 5, " s");
    addSpin(tr("Default zoom"),      "writer/defaultZoom", 100, 50, 300, 10, " %");

    v->addSpacing(4);
    v->addWidget(text(tr("Preferences apply to newly opened documents and save "
                         "automatically."), 11, "#6B7280", false, page));
    v->addStretch();
    return page;
}

// ── Premium page (export defaults; entitlement gated; auto-save) ─────────────
// These controls used to live on a Premium tab of the old SettingsDialog (since
// deleted), which nothing had opened since the tray replaced it, so they shipped
// unreachable. Same keys and same defaults, rehomed onto a pane in the UI.
QWidget* SettingsTray::buildPremiumPage() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(22, 6, 22, 22);
    v->setSpacing(13);
    QSettings st;

    // Free accounts see the controls, disabled, under the upgrade route rather
    // than not seeing them at all: it makes the offer legible and matches how
    // the rest of this panel behaves.
    auto* lockNote = text(tr("Buy Premium to unlock these."), 13, "#E0B341", true, page);
    v->addWidget(lockNote);
    auto* buy = actionButton(Lucide::kSparkle, tr("Buy Premium"), page);
    connect(buy, &QPushButton::clicked, page, [] { AuthManager::instance().openPremiumPage(); });
    v->addWidget(buy);
    v->addSpacing(4);

    v->addWidget(text(tr("Exports"), 13, "#8A93A6", true, page));

    auto* wmChk = new QCheckBox(tr("Show watermark on exports"), page);
    wmChk->setChecked(st.value(QLatin1String(Watermark::kSettingsKey), false).toBool());
    v->addWidget(wmChk);
    v->addWidget(text(tr("Free exports always carry the \"Made with NativeOffice\" mark."),
                      11, "#6B7280", false, page));

    // Combo rows follow the spin-row shape used on the Settings pane.
    auto addCombo = [&](const QString& label,
                        const QList<QPair<QString, QVariant>>& items,
                        const QString& key, const QVariant& def) {
        auto* row = new QHBoxLayout();
        row->addWidget(text(label, 13, "#C7CEDC", false, page));
        row->addStretch();
        auto* c = new QComboBox(page);
        for (const auto& it : items) c->addItem(it.first, it.second);
        const QVariant cur = QSettings().value(key, def);
        const int idx = c->findData(cur);
        c->setCurrentIndex(idx >= 0 ? idx : 0);
        // Written only for an entitled account. The controls are disabled for
        // free users anyway, but skipping the write means a free user can never
        // leave a value behind that would quietly take effect on upgrade.
        connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), page, [c, key](int) {
            if (AuthManager::instance().premiumActive())
                QSettings().setValue(key, c->currentData());
        });
        row->addWidget(c);
        v->addLayout(row);
        return c;
    };

    connect(wmChk, &QCheckBox::toggled, page, [](bool on) {
        if (AuthManager::instance().premiumActive())
            QSettings().setValue(QLatin1String(Watermark::kSettingsKey), on);
    });

    v->addSpacing(6);
    v->addWidget(text(tr("Default save formats"), 13, "#8A93A6", true, page));
    // .noff is no longer a format anyone is offered. See the filters in
    // main.cpp for why. Documents can still be saved as HTML and sheets as CSV,
    // but the DEFAULT is always the format the rest of the world reads.
    auto* docCombo = addCombo(tr("Documents"),
        {{ tr("Word document (.docx)"), "docx" }, { tr("Web page (.html)"), "html" }},
        "premium/defaultDocFormat", "docx");
    auto* sheetCombo = addCombo(tr("Spreadsheets"),
        {{ tr("Excel workbook (.xlsx)"), "xlsx" }, { tr("CSV file (.csv)"), "csv" }},
        "premium/defaultSheetFormat", "xlsx");
    auto* deckCombo = addCombo(tr("Presentations"),
        {{ tr("PowerPoint presentation (.pptx)"), "pptx" }},
        "premium/defaultDeckFormat", "pptx");

    v->addSpacing(6);
    v->addWidget(text(tr("PDF export"), 13, "#8A93A6", true, page));
    auto* dpiCombo = addCombo(tr("Quality"),
        {{ tr("Standard, 150 dpi"), 150 }, { tr("High, 300 dpi"), 300 }, { tr("Print, 600 dpi"), 600 }},
        "premium/pdfExportDpi", 300);
    auto* compCombo = addCombo(tr("Compression"),
        {{ tr("Light, fastest"), "light" }, { tr("Balanced"), "balanced" },
         { tr("Maximum, smallest file"), "max" }},
        "premium/pdfCompressLevel", "balanced");

    v->addSpacing(4);
    v->addWidget(text(tr("Preferences apply to new exports and save automatically."),
                      11, "#6B7280", false, page));
    v->addStretch();

    // Live entitlement: buying Premium in the browser unlocks these without a
    // restart, and signing out re-locks them.
    const QList<QWidget*> gated { wmChk, docCombo, sheetCombo, deckCombo, dpiCombo, compCombo };
    auto applyLock = [gated, lockNote, buy] {
        const bool premium = AuthManager::instance().premiumActive();
        for (QWidget* w : gated) w->setEnabled(premium);
        lockNote->setVisible(!premium);
        buy->setVisible(!premium);
    };
    applyLock();
    connect(&AuthManager::instance(), &AuthManager::entitlementChanged, page,
            [applyLock](bool) { applyLock(); });

    return page;
}

void SettingsTray::selectView(View v) {
    m_view = v;
    m_stack->setCurrentIndex(v == Profile ? 0 : (v == Settings ? 1 : 2));
    m_navProfile->setChecked(v == Profile);
    m_navSettings->setChecked(v == Settings);
    m_navPremium->setChecked(v == Premium);
    m_title->setText(v == Profile ? tr("Profile")
                                  : (v == Settings ? tr("Settings") : tr("Premium")));
}

// ── Open / close ──────────────────────────────────────────────────────────────
int SettingsTray::panelX(bool opened) const {
    return opened ? width() - m_panelW : width();
}

void SettingsTray::openTray(View view) {
    if (parentWidget()) setGeometry(parentWidget()->rect());
    m_panelW = qMin(540, width() - 48);     // always leave a gutter on the left
    m_panel->setMaximumWidth(m_panelW);
    m_panel->setFixedWidth(m_panelW);
    m_panel->setGeometry(panelX(false), 0, m_panelW, height());
    selectView(view);
    show();
    raise();
    setFocus();
    m_open = true;
    m_anim->stop();
    m_anim->setStartValue(QPoint(panelX(false), 0));
    m_anim->setEndValue(QPoint(panelX(true), 0));
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    m_anim->start();
}

void SettingsTray::closeTray() {
    if (!m_open) return;
    m_open = false;
    m_anim->stop();
    m_anim->setStartValue(m_panel->pos());
    m_anim->setEndValue(QPoint(panelX(false), 0));
    m_anim->setEasingCurve(QEasingCurve::InCubic);
    m_anim->start();
}

// ── Overlay behaviour ─────────────────────────────────────────────────────────
void SettingsTray::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(6, 9, 15, 135));
}

void SettingsTray::mousePressEvent(QMouseEvent* e) {
    if (!m_panel->geometry().contains(e->pos())) closeTray();
    else QWidget::mousePressEvent(e);
}

void SettingsTray::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) { closeTray(); return; }
    QWidget::keyPressEvent(e);
}

void SettingsTray::resizeEvent(QResizeEvent*) {
    m_panelW = qMin(540, width() - 48);
    m_panel->setMaximumWidth(m_panelW);
    m_panel->setFixedWidth(m_panelW);
    m_panel->setGeometry(panelX(m_open), 0, m_panelW, height());
}

bool SettingsTray::eventFilter(QObject* o, QEvent* e) {
    if (o == parentWidget() && e->type() == QEvent::Resize && isVisible())
        setGeometry(parentWidget()->rect());
    return QWidget::eventFilter(o, e);
}

} // namespace NativeOffice
