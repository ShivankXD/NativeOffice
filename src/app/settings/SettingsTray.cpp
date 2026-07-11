// ─────────────────────────────────────────────────────────────────────────────
// SettingsTray.cpp — see SettingsTray.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "SettingsTray.h"
#include "common/Avatars.h"
#include "core/auth/AuthManager.h"

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
#include <QToolButton>
#include <QVBoxLayout>

namespace NativeOffice {

namespace {

QLabel* text(const QString& s, int px, const QString& color, bool bold,
             QWidget* parent) {
    auto* l = new QLabel(s, parent);
    l->setStyleSheet(QString("background:transparent; color:%1; font:%2 %3px 'Segoe UI';")
                         .arg(color, bold ? "600" : "400", QString::number(px)));
    return l;
}

// A thin horizontal divider line.
QFrame* divider(QWidget* parent) {
    auto* d = new QFrame(parent);
    d->setFixedHeight(1);
    d->setStyleSheet("background:#1B212C; border:none;");
    return d;
}

// A compact "label ........ value" row, matching the website account tray.
QWidget* infoRow(const QString& label, const QString& value, QWidget* parent) {
    auto* row = new QWidget(parent);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(text(label, 13, "#8A93A6", false, row));
    h->addStretch();
    h->addWidget(text(value, 13, "#E6E9F0", true, row));
    return row;
}

} // namespace

SettingsTray::SettingsTray(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("settingsTray");
    setAttribute(Qt::WA_StyledBackground, false);   // we paint the scrim ourselves
    hide();
    if (parent) parent->installEventFilter(this);   // track parent resizes

    // ── The docked panel ─────────────────────────────────────────────────────
    m_panel = new QFrame(this);
    m_panel->setObjectName("settingsPanel");
    m_panel->setStyleSheet(R"(
        QFrame#settingsPanel { background:#0D1117; border-left:1px solid #1B212C; }
        QLabel { background:transparent; }
        QCheckBox { color:#C3CAD8; font:13px 'Segoe UI'; spacing:8px; }
        QCheckBox::indicator { width:16px; height:16px; }
        QSpinBox { background:#161B26; border:1px solid #232A38; border-radius:8px;
            color:#E2E6EE; padding:5px 8px; font:13px 'Segoe UI'; }
        QPushButton { background:#161C28; border:1px solid #232A38; border-radius:9px;
            color:#E6E9F0; font:13px 'Segoe UI'; padding:9px 14px; text-align:left; }
        QPushButton:hover { background:#1E2737; border:1px solid #2E3A50; }
        QPushButton#dangerBtn { color:#E5534B; }
        QPushButton#dangerBtn:hover { background:#2A1518; border:1px solid #5A2B2B; }
        QScrollArea, QScrollArea > QWidget > QWidget { background:transparent; border:none; }
        QScrollBar:vertical { background:transparent; width:8px; margin:0; }
        QScrollBar::handle:vertical { background:#2A3040; min-height:30px; border-radius:4px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
    )");

    auto* pv = new QVBoxLayout(m_panel);
    pv->setContentsMargins(0, 0, 0, 0);
    pv->setSpacing(0);

    // Header with title + close button.
    auto* header = new QWidget(m_panel);
    auto* hh = new QHBoxLayout(header);
    hh->setContentsMargins(22, 18, 14, 14);
    hh->addWidget(text("Account", 18, "#F0F2F7", true, header));
    hh->addStretch();
    auto* closeBtn = new QToolButton(header);
    closeBtn->setText(QString::fromUtf8("→"));   // slide back out to the right
    closeBtn->setToolTip(tr("Hide"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFixedSize(30, 30);
    closeBtn->setStyleSheet(
        "QToolButton { background:#161C28; border:1px solid #232A38; border-radius:8px;"
        "  color:#AEB6C6; font:16px 'Segoe UI'; }"
        "QToolButton:hover { background:#1E2737; color:#FFFFFF; }");
    connect(closeBtn, &QToolButton::clicked, this, &SettingsTray::closeTray);
    hh->addWidget(closeBtn);
    pv->addWidget(header);

    // Scrollable content below the header.
    auto* scroll = new QScrollArea(m_panel);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(buildContent());
    pv->addWidget(scroll, 1);

    // ── Slide animation on the panel geometry ────────────────────────────────
    m_anim = new QPropertyAnimation(m_panel, "pos", this);
    m_anim->setDuration(230);
    connect(m_anim, &QPropertyAnimation::finished, this, [this] {
        if (!m_open) hide();     // finished sliding out → remove the overlay
    });
}

int SettingsTray::panelX(bool opened) const {
    return opened ? width() - m_panelW : width();
}

QWidget* SettingsTray::buildContent() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(22, 4, 22, 22);
    v->setSpacing(14);

    auto& auth = AuthManager::instance();

    // ── Identity: avatar + name + email ──────────────────────────────────────
    auto* idRow = new QHBoxLayout();
    idRow->setSpacing(14);
    auto* avatar = new QLabel(page);
    avatar->setFixedSize(56, 56);
    idRow->addWidget(avatar, 0, Qt::AlignTop);
    auto* idCol = new QVBoxLayout();
    idCol->setSpacing(2);
    auto* nameLbl  = text(QString(), 16, "#F0F2F7", true, page);
    auto* emailLbl = text(QString(), 12, "#8A93A6", false, page);
    idCol->addWidget(nameLbl);
    idCol->addWidget(emailLbl);
    idCol->addStretch();
    idRow->addLayout(idCol, 1);
    v->addLayout(idRow);

    v->addWidget(divider(page));

    // ── Plan / Member since / Occupation (website tray style) ────────────────
    auto* planRow = new QWidget(page);
    auto* ph = new QHBoxLayout(planRow);
    ph->setContentsMargins(0, 0, 0, 0);
    ph->addWidget(text("Plan", 13, "#8A93A6", false, planRow));
    ph->addStretch();
    auto* planVal = text(QString(), 13, "#9D8CFF", true, planRow);
    ph->addWidget(planVal);
    v->addWidget(planRow);

    auto* memberRow = infoRow("Member since", "—", page);
    auto* memberVal = memberRow->findChildren<QLabel*>().last();
    v->addWidget(memberRow);
    auto* occRow = infoRow("Occupation", "Not set", page);
    auto* occVal = occRow->findChildren<QLabel*>().last();
    v->addWidget(occRow);

    // Live refresh from the synced profile.
    auto refresh = [avatar, nameLbl, emailLbl, planVal, memberVal, occVal] {
        auto& a = AuthManager::instance();
        avatar->setPixmap(roundAvatarPixmap(56, avatar->devicePixelRatio()));
        nameLbl->setText(a.userName().isEmpty() ? QStringLiteral("Not signed in") : a.userName());
        emailLbl->setText(a.userEmail());
        const bool premium = a.premiumActive();
        planVal->setText(premium ? a.premiumPlanLabel() : QStringLiteral("Free"));
        planVal->setStyleSheet(QString("background:transparent; font:600 13px 'Segoe UI'; color:%1;")
                                   .arg(premium ? "#9D8CFF" : "#8A93A6"));
        memberVal->setText(a.joinedAt().isValid()
                               ? a.joinedAt().toString("MMMM yyyy") : QStringLiteral("—"));
        occVal->setText(a.occupation().isEmpty() ? QStringLiteral("Not set") : a.occupation());
    };
    refresh();
    connect(&auth, &AuthManager::profileChanged,     page, refresh);
    connect(&auth, &AuthManager::entitlementChanged, page, [refresh](bool) { refresh(); });

    v->addSpacing(2);

    // ── Account actions (browser) ────────────────────────────────────────────
    auto actionBtn = [&](const QString& label, std::function<void()> slot) {
        auto* b = new QPushButton(label, page);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, page, std::move(slot));
        v->addWidget(b);
        return b;
    };
    actionBtn(tr("Activate a product key"),
              [] { AuthManager::instance().openActivateKeyPage(); });
    actionBtn(tr("Edit profile"),
              [] { AuthManager::instance().openAccountPage(); });
    actionBtn(auth.premiumActive() ? tr("Manage account") : tr("Buy Premium"), [] {
        auto& a = AuthManager::instance();
        a.premiumActive() ? a.openAccountPage() : a.openPremiumPage();
    });

    v->addWidget(divider(page));

    // ── Preferences (auto-save on change) ────────────────────────────────────
    v->addWidget(text("Preferences", 14, "#C3CAD8", true, page));
    QSettings st;

    auto addCheck = [&](const QString& label, const QString& key, bool def) {
        auto* c = new QCheckBox(label, page);
        c->setChecked(st.value(key, def).toBool());
        connect(c, &QCheckBox::toggled, page,
                [key](bool on) { QSettings().setValue(key, on); });
        v->addWidget(c);
        return c;
    };
    addCheck(tr("Show splash screen on startup"), "app/showSplash",     true);
    addCheck(tr("Spell check on by default"),     "writer/spellDefault", true);
    addCheck(tr("AutoCorrect as you type"),       "writer/autoCorrect",  true);
    addCheck(tr("Show rulers in new documents"),  "writer/rulers",       true);

    auto addSpin = [&](const QString& label, const QString& key, int def,
                       int lo, int hi, int step, const QString& suffix) {
        auto* row = new QHBoxLayout();
        row->addWidget(text(label, 13, "#C3CAD8", false, page));
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

    v->addWidget(text("Profile is managed on nativeoffice.online — changes sync back "
                      "automatically.", 11, "#6B7280", false, page));

    v->addWidget(divider(page));

    // ── Sign out ─────────────────────────────────────────────────────────────
    auto* outBtn = new QPushButton(tr("Sign out"), page);
    outBtn->setObjectName("dangerBtn");
    outBtn->setCursor(Qt::PointingHandCursor);
    connect(outBtn, &QPushButton::clicked, this, [this] {
        const auto btn = QMessageBox::question(this, tr("Sign Out"),
            tr("Sign out of NativeOffice on this computer?\n"
               "You'll be asked to sign in again next time."),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (btn != QMessageBox::Yes) return;
        closeTray();
        AuthManager::instance().signOut();
    });
    v->addWidget(outBtn);

    v->addStretch();
    return page;
}

// ── Open / close ──────────────────────────────────────────────────────────────
void SettingsTray::openTray() {
    if (parentWidget()) setGeometry(parentWidget()->rect());
    m_panelW = qMin(400, int(width() * 0.9));
    m_panel->setFixedWidth(m_panelW);   // content min-size must not widen it off-screen
    m_panel->setGeometry(panelX(false), 0, m_panelW, height());
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
    p.fillRect(rect(), QColor(6, 9, 15, 130));   // dim the content behind the panel
}

void SettingsTray::mousePressEvent(QMouseEvent* e) {
    // A press on the scrim (outside the panel) dismisses the tray. Presses on
    // the panel go to its children and never reach here.
    if (!m_panel->geometry().contains(e->pos())) closeTray();
    else QWidget::mousePressEvent(e);
}

void SettingsTray::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) { closeTray(); return; }
    QWidget::keyPressEvent(e);
}

void SettingsTray::resizeEvent(QResizeEvent*) {
    m_panelW = qMin(400, int(width() * 0.9));
    m_panel->setFixedWidth(m_panelW);
    m_panel->setGeometry(panelX(m_open), 0, m_panelW, height());
}

// The overlay isn't in the parent's layout, so it won't auto-resize when the
// window grows/maximises. Mirror the parent's size while we're on screen so the
// panel always stays docked to the true right edge.
bool SettingsTray::eventFilter(QObject* o, QEvent* e) {
    if (o == parentWidget() && e->type() == QEvent::Resize && isVisible())
        setGeometry(parentWidget()->rect());   // triggers our resizeEvent → repositions panel
    return QWidget::eventFilter(o, e);
}

} // namespace NativeOffice
