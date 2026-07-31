// ─────────────────────────────────────────────────────────────────────────────
// SettingsDialog.cpp — implementation (see header).
// ─────────────────────────────────────────────────────────────────────────────
#include "SettingsDialog.h"
#include "common/Avatars.h"
#include "core/auth/AuthManager.h"
#include "core/watermark/Watermark.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace NativeOffice {

namespace {

QLabel* text(const QString& s, int px, const QString& color, bool bold,
             QWidget* parent) {
    auto* l = new QLabel(s, parent);
    l->setStyleSheet(QString("background:transparent; color:%1;"
                             "font:%2 %3px 'Segoe UI';")
                         .arg(color, bold ? "600" : "400", QString::number(px)));
    return l;
}

QLabel* sectionTitle(const QString& s, QWidget* parent) {
    return text(s, 18, "#F0F2F7", true, parent);
}

} // namespace

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName("settingsDialog");
    setWindowTitle("Settings — NativeOffice");
    setMinimumSize(720, 520);
    resize(840, 580);
    setSizeGripEnabled(true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);

    // ── Section sidebar ──────────────────────────────────────────────────
    m_nav = new QListWidget(this);
    m_nav->setObjectName("settingsNav");
    m_nav->setFixedWidth(196);
    m_nav->setFrameShape(QFrame::NoFrame);
    m_nav->addItem("👤   Account");
    m_nav->addItem("⚙   General");
    m_nav->addItem("📝   Writer");
    m_nav->addItem("★   Premium");           // last: it sits below the free settings
    for (int i = 0; i < m_nav->count(); ++i)
        m_nav->item(i)->setSizeHint(QSize(0, 44));

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(buildAccountPage());
    m_pages->addWidget(buildGeneralPage());
    m_pages->addWidget(buildWriterPage());
    m_pages->addWidget(buildPremiumPage());
    connect(m_nav, &QListWidget::currentRowChanged,
            m_pages, &QStackedWidget::setCurrentIndex);
    m_nav->setCurrentRow(0);

    body->addWidget(m_nav);
    body->addWidget(m_pages, 1);
    root->addLayout(body, 1);

    // ── Bottom bar ───────────────────────────────────────────────────────
    auto* bottom = new QWidget(this);
    bottom->setObjectName("settingsBottom");
    auto* bl = new QHBoxLayout(bottom);
    bl->setContentsMargins(20, 12, 20, 12);
    bl->addWidget(text("Changes apply to newly opened documents.",
                       11, "#7B8494", false, bottom));
    bl->addStretch();
    auto* bb = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, bottom);
    bl->addWidget(bb);
    root->addWidget(bottom);

    connect(bb, &QDialogButtonBox::accepted, this, [this]() {
        save();
        accept();
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setStyleSheet(R"(
        QDialog#settingsDialog { background:#0D1117; }
        QListWidget#settingsNav {
            background:#0A0D13; border-right:1px solid #1B212C;
            color:#AEB6C6; font:14px 'Segoe UI'; outline:none; padding-top:10px; }
        QListWidget#settingsNav::item { padding-left:16px; border-radius:8px; margin:2px 8px; }
        QListWidget#settingsNav::item:hover    { background:#161C28; }
        QListWidget#settingsNav::item:selected { background:#17233B; color:#FFFFFF; }
        QWidget#settingsBottom { background:#0A0D13; border-top:1px solid #1B212C; }
        QLineEdit, QSpinBox { background:#161B26; border:1px solid #232A38; border-radius:8px;
            color:#E2E6EE; padding:6px 10px; font:13px 'Segoe UI'; }
        QCheckBox { color:#C3CAD8; font:13px 'Segoe UI'; }
        QCheckBox::indicator { width:16px; height:16px; }
        QPushButton { background:#17233B; border:1px solid #3B82F6; border-radius:8px;
            color:#FFFFFF; font:13px 'Segoe UI'; padding:8px 18px; }
        QPushButton:hover { background:#1E2E4D; }
        QPushButton#dangerBtn { background:transparent; border:1px solid #5A2B2B; color:#E5534B; }
        QPushButton#dangerBtn:hover { background:#2A1518; }
    )");
}

// ── Account ───────────────────────────────────────────────────────────────────

QWidget* SettingsDialog::buildAccountPage() {
    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(28, 24, 28, 24);
    v->setSpacing(16);
    v->addWidget(sectionTitle("Account", page));

    auto& auth = AuthManager::instance();

    // Profile card: photo + identity + plan.
    auto* card = new QFrame(page);
    card->setObjectName("acctCard");
    card->setStyleSheet("#acctCard { background:#12161F; border:1px solid #202836;"
                        "border-radius:14px; }");
    auto* ch = new QHBoxLayout(card);
    ch->setContentsMargins(20, 18, 20, 18);
    ch->setSpacing(16);

    auto* avatar = new QLabel(card);
    avatar->setFixedSize(64, 64);
    avatar->setStyleSheet("background:transparent;");
    ch->addWidget(avatar, 0, Qt::AlignTop);

    auto* idCol = new QVBoxLayout();
    idCol->setSpacing(3);
    auto* nameLbl  = text(QString(), 17, "#F0F2F7", true, card);
    auto* emailLbl = text(QString(), 13, "#8A93A6", false, card);
    auto* metaLbl  = text(QString(), 12, "#7B8494", false, card);
    idCol->addWidget(nameLbl);
    idCol->addWidget(emailLbl);
    idCol->addWidget(metaLbl);
    idCol->addStretch();
    ch->addLayout(idCol, 1);

    auto* planLbl = new QLabel(card);
    planLbl->setFixedHeight(26);
    ch->addWidget(planLbl, 0, Qt::AlignTop);
    v->addWidget(card);

    // Everything below refreshes live: the profile syncs from the website.
    auto refresh = [avatar, nameLbl, emailLbl, metaLbl, planLbl]() {
        auto& a = AuthManager::instance();
        avatar->setPixmap(roundAvatarPixmap(64, avatar->devicePixelRatio()));
        nameLbl->setText(a.userName().isEmpty() ? QStringLiteral("Not signed in")
                                                : a.userName());
        emailLbl->setText(a.userEmail());
        const bool premium = a.premiumActive();
        QStringList meta;
        if (!a.occupation().isEmpty()) meta << a.occupation();
        if (a.joinedAt().isValid())
            meta << QStringLiteral("Member since ")
                        + a.joinedAt().toString("MMMM yyyy");
        // Show the expiry date beside the plan for time-limited premium.
        if (premium && a.premiumUntil().isValid())
            meta << QStringLiteral("Expires ")
                        + a.premiumUntil().toString("MMMM d, yyyy");
        metaLbl->setText(meta.join(QStringLiteral("  ·  ")));

        // e.g. "PREMIUM · 1-YEAR", "PREMIUM · LIFETIME", or "FREE PLAN".
        const QString planText = premium ? a.premiumPlanLabel().toUpper()
                                         : QStringLiteral("FREE PLAN");
        planLbl->setText(planText);
        planLbl->setStyleSheet(QString(
            "background:%1; color:%2; border:1px solid %3; border-radius:13px;"
            "padding:2px 12px; font:600 11px 'Segoe UI'; letter-spacing:1px;")
            .arg(premium ? "#173321" : "#161B26",
                 premium ? "#3FB68B" : "#8A93A6",
                 premium ? "#245C3B" : "#232A38"));
    };
    refresh();
    connect(&auth, &AuthManager::profileChanged,     page, refresh);
    connect(&auth, &AuthManager::entitlementChanged, page,
            [refresh](bool) { refresh(); });

    v->addWidget(text("Your profile is managed on nativeoffice.online — changes "
                      "sync back to the app automatically.",
                      12, "#7B8494", false, page));

    // Browser entry points.
    auto* row = new QHBoxLayout();
    row->setSpacing(8);
    auto* editBtn = new QPushButton("✏️  Edit Profile", page);
    auto* buyBtn  = new QPushButton(auth.premiumActive()
                                        ? "Manage Account" : "✨  Buy Premium", page);
    auto* keyBtn  = new QPushButton("🔑  Activate Key", page);
    for (auto* b : { editBtn, buyBtn, keyBtn }) {
        b->setCursor(Qt::PointingHandCursor);
        row->addWidget(b);
    }
    row->addStretch();
    v->addLayout(row);

    connect(editBtn, &QPushButton::clicked, page,
            []() { AuthManager::instance().openAccountPage(); });
    connect(buyBtn, &QPushButton::clicked, page, []() {
        auto& a = AuthManager::instance();
        a.premiumActive() ? a.openAccountPage() : a.openPremiumPage();
    });
    connect(keyBtn, &QPushButton::clicked, page,
            []() { AuthManager::instance().openActivateKeyPage(); });

    v->addStretch();

    auto* outRow = new QHBoxLayout();
    auto* outBtn = new QPushButton("Sign Out", page);
    outBtn->setObjectName("dangerBtn");
    outBtn->setCursor(Qt::PointingHandCursor);
    outRow->addWidget(outBtn);
    outRow->addStretch();
    v->addLayout(outRow);

    connect(outBtn, &QPushButton::clicked, this, [this]() {
        const auto btn = QMessageBox::question(this, "Sign Out",
            "Sign out of NativeOffice on this computer?\n"
            "You'll be asked to sign in again next time.",
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (btn != QMessageBox::Yes) return;
        reject();                 // close settings before the gate reappears
        AuthManager::instance().signOut();
    });

    return page;
}

// ── General ───────────────────────────────────────────────────────────────────

QWidget* SettingsDialog::buildGeneralPage() {
    QSettings st;
    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(28, 24, 28, 24);
    v->setSpacing(16);
    v->addWidget(sectionTitle("General", page));

    m_splashChk = new QCheckBox("Show splash screen on startup", page);
    m_splashChk->setChecked(st.value("app/showSplash", true).toBool());
    v->addWidget(m_splashChk);

    v->addWidget(text("Licensing and sign-in are handled per account — see the "
                      "Account section.", 12, "#7B8494", false, page));
    v->addStretch();
    return page;
}

// ── Premium ───────────────────────────────────────────────────────────────────
// Sits after every free-tier section. Free accounts see the controls, disabled,
// under a line explaining why, plus the same upgrade route the Account page
// uses. Showing them greyed rather than hiding them matches how the rest of
// this dialog works and makes the offer legible.

QWidget* SettingsDialog::buildPremiumPage() {
    QSettings st;
    auto& auth = AuthManager::instance();
    const bool premium = auth.premiumActive();

    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(28, 24, 28, 24);
    v->setSpacing(16);
    v->addWidget(sectionTitle("Premium Settings", page));

    if (!premium) {
        v->addWidget(text("Buy Premium to unlock these.", 13, "#E0B341", true, page));
        auto* buy = new QPushButton("Buy Premium", page);
        buy->setCursor(Qt::PointingHandCursor);
        connect(buy, &QPushButton::clicked, this,
                [] { AuthManager::instance().openPremiumPage(); });
        auto* row = new QHBoxLayout();
        row->addWidget(buy);
        row->addStretch();
        v->addLayout(row);
    }

    m_wmChk = new QCheckBox("Show watermark on exports", page);
    // Off unless deliberately switched on, which is why an upgrade takes effect
    // with no visit to this page.
    m_wmChk->setChecked(st.value(QLatin1String(Watermark::kSettingsKey), false).toBool());
    v->addWidget(m_wmChk);
    v->addWidget(text("Free exports always carry the \"Made with NativeOffice\" mark.",
                      12, "#7B8494", false, page));

    auto* form = new QFormLayout();
    form->setSpacing(10);
    auto mkLbl = [&](const QString& t) { return text(t, 13, "#C3CAD8", false, page); };

    m_docFmtCombo = new QComboBox(page);
    m_docFmtCombo->addItem("Word document (.docx)", "docx");
    m_docFmtCombo->addItem("NativeOffice document (.noff)", "noff");
    m_docFmtCombo->setCurrentIndex(
        st.value("premium/defaultDocFormat", "docx").toString() == "noff" ? 1 : 0);
    form->addRow(mkLbl("Default document format"), m_docFmtCombo);

    m_sheetFmtCombo = new QComboBox(page);
    m_sheetFmtCombo->addItem("Excel workbook (.xlsx)", "xlsx");
    m_sheetFmtCombo->addItem("NativeOffice sheet (.noff)", "noff");
    m_sheetFmtCombo->setCurrentIndex(
        st.value("premium/defaultSheetFormat", "xlsx").toString() == "noff" ? 1 : 0);
    form->addRow(mkLbl("Default spreadsheet format"), m_sheetFmtCombo);

    m_deckFmtCombo = new QComboBox(page);
    m_deckFmtCombo->addItem("PowerPoint presentation (.pptx)", "pptx");
    m_deckFmtCombo->addItem("NativeOffice deck (.noff)", "noff");
    m_deckFmtCombo->setCurrentIndex(
        st.value("premium/defaultDeckFormat", "pptx").toString() == "noff" ? 1 : 0);
    form->addRow(mkLbl("Default presentation format"), m_deckFmtCombo);

    m_pdfQualCombo = new QComboBox(page);
    m_pdfQualCombo->addItem("Standard - 150 dpi", 150);
    m_pdfQualCombo->addItem("High - 300 dpi", 300);
    m_pdfQualCombo->addItem("Print - 600 dpi", 600);
    {
        const int dpi = st.value("premium/pdfExportDpi", 300).toInt();
        m_pdfQualCombo->setCurrentIndex(dpi >= 600 ? 2 : (dpi <= 150 ? 0 : 1));
    }
    form->addRow(mkLbl("PDF export quality"), m_pdfQualCombo);

    m_pdfCompCombo = new QComboBox(page);
    m_pdfCompCombo->addItem("Light - fastest", "light");
    m_pdfCompCombo->addItem("Balanced", "balanced");
    m_pdfCompCombo->addItem("Maximum - smallest file", "max");
    {
        const QString lvl = st.value("premium/pdfCompressLevel", "balanced").toString();
        m_pdfCompCombo->setCurrentIndex(lvl == "light" ? 0 : (lvl == "max" ? 2 : 1));
    }
    form->addRow(mkLbl("PDF compression"), m_pdfCompCombo);

    v->addLayout(form);

    if (!premium) {
        for (QWidget* w : QList<QWidget*>{ m_wmChk, m_docFmtCombo, m_sheetFmtCombo,
                                           m_deckFmtCombo, m_pdfQualCombo, m_pdfCompCombo })
            w->setEnabled(false);
    }

    v->addStretch();
    return page;
}

// ── Writer ────────────────────────────────────────────────────────────────────

QWidget* SettingsDialog::buildWriterPage() {
    QSettings st;
    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(28, 24, 28, 24);
    v->setSpacing(16);
    v->addWidget(sectionTitle("Writer", page));

    m_spellChk = new QCheckBox("Spell check on by default", page);
    m_spellChk->setChecked(st.value("writer/spellDefault", true).toBool());
    v->addWidget(m_spellChk);

    m_autoChk = new QCheckBox("AutoCorrect as you type", page);
    m_autoChk->setChecked(st.value("writer/autoCorrect", true).toBool());
    v->addWidget(m_autoChk);

    m_rulersChk = new QCheckBox("Show rulers in new documents", page);
    m_rulersChk->setChecked(st.value("writer/rulers", true).toBool());
    v->addWidget(m_rulersChk);

    auto* form = new QFormLayout();
    form->setSpacing(10);
    auto mkLbl = [&](const QString& t) { return text(t, 13, "#C3CAD8", false, page); };

    m_autosaveSpin = new QSpinBox(page);
    m_autosaveSpin->setRange(5, 300);
    m_autosaveSpin->setSuffix(" s");
    m_autosaveSpin->setValue(st.value("writer/autosaveSec", 20).toInt());
    form->addRow(mkLbl("Autosave interval"), m_autosaveSpin);

    m_zoomSpin = new QSpinBox(page);
    m_zoomSpin->setRange(50, 300);
    m_zoomSpin->setSuffix(" %");
    m_zoomSpin->setSingleStep(10);
    m_zoomSpin->setValue(st.value("writer/defaultZoom", 100).toInt());
    form->addRow(mkLbl("Default zoom"), m_zoomSpin);

    v->addLayout(form);
    v->addStretch();
    return page;
}

void SettingsDialog::save() {
    QSettings st;
    st.setValue("app/showSplash",      m_splashChk->isChecked());
    st.setValue("writer/spellDefault", m_spellChk->isChecked());
    st.setValue("writer/autoCorrect",  m_autoChk->isChecked());
    st.setValue("writer/autosaveSec",  m_autosaveSpin->value());
    st.setValue("writer/defaultZoom",  m_zoomSpin->value());
    st.setValue("writer/rulers",       m_rulersChk->isChecked());

    // Premium values are written only for an entitled account. The controls are
    // disabled for free users anyway, but skipping the write means a free user
    // cannot leave a value behind that would quietly take effect on upgrade —
    // in particular a watermark toggle that should start off.
    if (AuthManager::instance().premiumActive()) {
        st.setValue(QLatin1String(Watermark::kSettingsKey), m_wmChk->isChecked());
        st.setValue("premium/defaultDocFormat",   m_docFmtCombo->currentData().toString());
        st.setValue("premium/defaultSheetFormat", m_sheetFmtCombo->currentData().toString());
        st.setValue("premium/defaultDeckFormat",  m_deckFmtCombo->currentData().toString());
        st.setValue("premium/pdfExportDpi",       m_pdfQualCombo->currentData().toInt());
        st.setValue("premium/pdfCompressLevel",   m_pdfCompCombo->currentData().toString());
    }
}

} // namespace NativeOffice
