// ─────────────────────────────────────────────────────────────────────────────
// SettingsTray.cpp — see SettingsTray.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "SettingsTray.h"
#include "common/Avatars.h"
#include "core/auth/AuthManager.h"
#include "startscreen/LucideIcons.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QEasingCurve>
#include <QFrame>
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
            image:url(none); }
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
    scroll->setWidget(m_stack);
    rv->addWidget(scroll, 1);

    h->addWidget(right, 1);
    return panel;
}

// ── Left navigation (Profile / Settings) ─────────────────────────────────────
QWidget* SettingsTray::buildSidebar() {
    auto* bar = new QWidget(m_panel);
    bar->setObjectName("traySidebar");
    bar->setFixedWidth(124);
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

    auto* group = new QButtonGroup(bar);
    group->setExclusive(true);
    group->addButton(m_navProfile,  Profile);
    group->addButton(m_navSettings, Settings);
    connect(m_navProfile,  &QToolButton::clicked, this, [this] { selectView(Profile); });
    connect(m_navSettings, &QToolButton::clicked, this, [this] { selectView(Settings); });

    v->addStretch();
    return bar;
}

// ── Profile page (avatar / plan / account actions / sign out) ────────────────
QWidget* SettingsTray::buildProfilePage() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(22, 6, 22, 22);
    v->setSpacing(14);

    auto& auth = AuthManager::instance();

    // Identity card.
    auto* card = new QFrame(page);
    card->setStyleSheet("QFrame { background:#111621; border:1px solid #1F2735; border-radius:16px; }");
    auto* cv = new QVBoxLayout(card);
    cv->setContentsMargins(18, 18, 18, 18);
    cv->setSpacing(12);

    auto* top = new QHBoxLayout();
    top->setSpacing(14);
    auto* avatar = new QLabel(card);
    avatar->setFixedSize(60, 60);
    top->addWidget(avatar, 0, Qt::AlignTop);
    auto* idCol = new QVBoxLayout();
    idCol->setSpacing(2);
    auto* nameLbl  = text(QString(), 16, "#F4F6FB", true, card);
    auto* emailLbl = text(QString(), 12, "#8A93A6", false, card);
    idCol->addWidget(nameLbl);
    idCol->addWidget(emailLbl);
    auto* planBadge = new QLabel(card);
    planBadge->setFixedHeight(22);
    planBadge->setAlignment(Qt::AlignCenter);
    idCol->addSpacing(4);
    { auto* pr = new QHBoxLayout(); pr->setContentsMargins(0,0,0,0);
      pr->addWidget(planBadge, 0, Qt::AlignLeft); pr->addStretch(); idCol->addLayout(pr); }
    top->addLayout(idCol, 1);
    top->setAlignment(Qt::AlignTop);
    cv->addLayout(top);
    cv->addWidget(divider(card));

    QLabel *memberVal = nullptr, *occVal = nullptr;
    cv->addWidget(infoRow(tr("Member since"), memberVal, card));
    cv->addWidget(infoRow(tr("Occupation"),   occVal,    card));
    v->addWidget(card);

    auto refresh = [avatar, nameLbl, emailLbl, planBadge, memberVal, occVal] {
        auto& a = AuthManager::instance();
        avatar->setPixmap(roundAvatarPixmap(60, avatar->devicePixelRatio()));
        nameLbl->setText(a.userName().isEmpty() ? QStringLiteral("Not signed in") : a.userName());
        emailLbl->setText(a.userEmail());
        const bool premium = a.premiumActive();
        planBadge->setText(premium ? a.premiumPlanLabel().toUpper() : QStringLiteral("FREE PLAN"));
        planBadge->setStyleSheet(QString(
            "border-radius:11px; padding:2px 12px; font:700 10px 'Segoe UI'; letter-spacing:1px;"
            "background:%1; color:%2; border:1px solid %3;")
            .arg(premium ? "#1E1A33" : "#161B26",
                 premium ? "#B7A6FF" : "#8A93A6",
                 premium ? "#332A5C" : "#232A38"));
        memberVal->setText(a.joinedAt().isValid() ? a.joinedAt().toString("MMMM yyyy") : QStringLiteral("—"));
        occVal->setText(a.occupation().isEmpty() ? QStringLiteral("Not set") : a.occupation());
    };
    refresh();
    connect(&auth, &AuthManager::profileChanged,     page, refresh);
    connect(&auth, &AuthManager::entitlementChanged, page, [refresh](bool) { refresh(); });

    // Account actions.
    auto* edit = actionButton(Lucide::kPen, tr("Edit profile"), page);
    connect(edit, &QPushButton::clicked, page, [] { AuthManager::instance().openAccountPage(); });
    v->addWidget(edit);

    auto* key = actionButton(Lucide::kKey, tr("Activate a product key"), page);
    connect(key, &QPushButton::clicked, page, [] { AuthManager::instance().openActivateKeyPage(); });
    v->addWidget(key);

    auto* manage = actionButton(Lucide::kSparkle,
        auth.premiumActive() ? tr("Manage account") : tr("Buy Premium"), page);
    connect(manage, &QPushButton::clicked, page, [] {
        auto& a = AuthManager::instance();
        a.premiumActive() ? a.openAccountPage() : a.openPremiumPage();
    });
    v->addWidget(manage);

    v->addWidget(text(tr("Profile is managed on nativeoffice.online — changes sync back "
                         "automatically."), 11, "#6B7280", false, page));
    v->addStretch();

    // Sign out — only on the Profile pane.
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

void SettingsTray::selectView(View v) {
    m_view = v;
    m_stack->setCurrentIndex(v == Profile ? 0 : 1);
    m_navProfile->setChecked(v == Profile);
    m_navSettings->setChecked(v == Settings);
    m_title->setText(v == Profile ? tr("Profile") : tr("Settings"));
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
