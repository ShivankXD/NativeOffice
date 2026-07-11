// ─────────────────────────────────────────────────────────────────────────────
// LoginGate.cpp — implementation (see header).
// ─────────────────────────────────────────────────────────────────────────────
#include "LoginGate.h"
#include "core/auth/AuthManager.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace NativeOffice {

// Home-screen palette
static const char* kBg        = "#0D1117";
static const char* kText      = "#F0F2F7";
static const char* kSubText   = "#8A93A6";
static const char* kBlue      = "#3B82F6";
static const char* kBlueHover = "#2563EB";
static const char* kGold      = "#F0B429";
static const char* kPanel     = "#161B26";
static const char* kBorder    = "#232A38";

static QLabel* gateLabel(const QString& text, int px, const QString& color,
                         bool bold, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setAlignment(Qt::AlignCenter);
    l->setWordWrap(true);
    l->setStyleSheet(QString("background:transparent; color:%1;"
                             "font:%2 %3px 'Segoe UI';")
                         .arg(color, bold ? "600" : "400", QString::number(px)));
    return l;
}

static QLabel* gateLogo(QWidget* parent) {
    auto* logo = new QLabel(parent);
    logo->setAlignment(Qt::AlignCenter);
    logo->setStyleSheet("background:transparent;");
    QPixmap pm(":/assets/nativeoffice-logo.png");
    if (!pm.isNull()) {
        // The wordmark has a baked white background — frame it as a rounded
        // tile so it sits deliberately on the dark gate.
        logo->setPixmap(pm.scaled(170, 116, Qt::KeepAspectRatio,
                                  Qt::SmoothTransformation));
        logo->setStyleSheet("background:#FFFFFF; border-radius:18px; padding:12px;");
        logo->setFixedSize(200, 146);
    } else {
        logo->setText("NativeOffice");
        logo->setStyleSheet(QString(
            "background:transparent; color:%1; font:700 28px 'Segoe UI';").arg(kText));
    }
    return logo;
}

LoginGate::LoginGate(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("NativeOffice");
    setObjectName("loginGate");
    setAttribute(Qt::WA_DeleteOnClose);
    setFixedSize(520, 600);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack);

    m_stack->addWidget(buildCheckingPage());   // 0
    m_stack->addWidget(buildSignInPage());     // 1
    m_stack->addWidget(buildWaitingPage());    // 2

    setStyleSheet(QString(R"(
        QWidget#loginGate { background:%1; }
        QPushButton#primaryBtn {
            background:%2; border:none; border-radius:10px; color:#FFFFFF;
            font:600 15px 'Segoe UI'; padding:14px 30px; }
        QPushButton#primaryBtn:hover  { background:%3; }
        QPushButton#primaryBtn:disabled { background:#25334D; color:#7B8494; }
        QPushButton#linkBtn {
            background:transparent; border:none; color:%4;
            font:13px 'Segoe UI'; text-decoration:underline; }
        QPushButton#linkBtn:hover { color:#C7CEDC; }
    )").arg(kBg, kBlue, kBlueHover, kSubText));

    auto& auth = AuthManager::instance();
    connect(&auth, &AuthManager::sessionValidated, this, [this](bool ok) {
        if (m_done) return;
        if (ok) finishGate();
        else    showSignIn();
    });
    connect(&auth, &AuthManager::deviceFlowStarted, this,
            [this](const QString& code, const QString&) {
        m_codeLabel->setText(code);
        m_dotCount = 0;
        m_dotTimer->start(500);
        m_stack->setCurrentIndex(2);
    });
    connect(&auth, &AuthManager::authenticated, this, [this]() {
        if (!m_done) finishGate();
    });
    connect(&auth, &AuthManager::deviceFlowFailed, this,
            [this](const QString& msg) {
        if (!m_done) showSignIn(msg);
    });
}

// ── Pages ─────────────────────────────────────────────────────────────────────

QWidget* LoginGate::buildCheckingPage() {
    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(48, 40, 48, 40);
    v->addStretch();
    v->addWidget(gateLogo(page), 0, Qt::AlignHCenter);
    v->addSpacing(26);
    v->addWidget(gateLabel(tr("Restoring your session…"), 15, kSubText, false, page));
    v->addStretch();
    return page;
}

QWidget* LoginGate::buildSignInPage() {
    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(48, 40, 48, 36);
    v->addStretch(3);
    v->addWidget(gateLogo(page), 0, Qt::AlignHCenter);
    v->addSpacing(24);
    v->addWidget(gateLabel(tr("Your workspace is ready."), 24, kText, true, page));
    v->addSpacing(8);
    v->addWidget(gateLabel(
        tr("Sign in once and your license, templates and premium features "
           "follow you on every device."), 14, kSubText, false, page));
    v->addSpacing(14);

    m_errorLabel = gateLabel(QString(), 13, "#E5534B", false, page);
    m_errorLabel->hide();
    v->addWidget(m_errorLabel);
    v->addSpacing(14);

    m_signInBtn = new QPushButton(tr("Sign in to continue"), page);
    m_signInBtn->setObjectName("primaryBtn");
    m_signInBtn->setCursor(Qt::PointingHandCursor);
    connect(m_signInBtn, &QPushButton::clicked, this, [this]() {
        m_errorLabel->hide();
        m_signInBtn->setEnabled(false);
        m_signInBtn->setText(tr("Opening your browser…"));
        AuthManager::instance().startDeviceFlow();
    });
    v->addWidget(m_signInBtn);

    v->addSpacing(10);
    v->addWidget(gateLabel(
        tr("We'll open nativeoffice.online in your browser — sign in with "
           "Google there and this window continues automatically."),
        12, "#5A6478", false, page));
    v->addStretch(4);
    return page;
}

QWidget* LoginGate::buildWaitingPage() {
    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(48, 40, 48, 36);
    v->addStretch(3);
    v->addWidget(gateLogo(page), 0, Qt::AlignHCenter);
    v->addSpacing(22);
    v->addWidget(gateLabel(tr("Finish signing in"), 22, kText, true, page));
    v->addSpacing(6);
    v->addWidget(gateLabel(
        tr("Check that the code in your browser matches:"),
        14, kSubText, false, page));
    v->addSpacing(14);

    m_codeLabel = new QLabel(page);
    m_codeLabel->setAlignment(Qt::AlignCenter);
    m_codeLabel->setStyleSheet(QString(
        "background:%1; border:1px solid %2; border-radius:12px;"
        "color:%3; font:600 30px 'Consolas'; letter-spacing:6px;"
        "padding:16px 8px;").arg(kPanel, kBorder, kGold));
    v->addWidget(m_codeLabel);

    v->addSpacing(14);
    m_waitStatus = gateLabel(tr("Waiting for you to finish in the browser"),
                             13, kSubText, false, page);
    v->addWidget(m_waitStatus);
    v->addSpacing(16);

    auto linkBtn = [&](const QString& text) {
        auto* b = new QPushButton(text, page);
        b->setObjectName("linkBtn");
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };

    auto* row = new QHBoxLayout();
    row->setSpacing(18);
    auto* reopen = linkBtn(tr("Open the page again"));
    auto* copy   = linkBtn(tr("Having trouble? Copy the link"));
    auto* cancel = linkBtn(tr("Cancel"));
    row->addStretch();
    row->addWidget(reopen);
    row->addWidget(copy);
    row->addWidget(cancel);
    row->addStretch();
    v->addLayout(row);

    connect(reopen, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl(AuthManager::instance().verificationUrl()));
    });
    connect(copy, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(
            AuthManager::instance().verificationUrl());
        m_waitStatus->setText(
            tr("Link copied — paste it into any browser on this computer"));
    });
    connect(cancel, &QPushButton::clicked, this, [this]() {
        AuthManager::instance().cancelDeviceFlow();
        showSignIn();
    });

    // Animated trailing dots on the waiting line.
    m_dotTimer = new QTimer(this);
    connect(m_dotTimer, &QTimer::timeout, this, [this]() {
        m_dotCount = (m_dotCount + 1) % 4;
        m_waitStatus->setText(
            tr("Waiting for you to finish in the browser") +
            QString(m_dotCount, QChar('.')));
    });

    v->addStretch(4);
    return page;
}

// ── Flow ──────────────────────────────────────────────────────────────────────

void LoginGate::begin(bool silentChecking) {
    if (const QScreen* screen = QApplication::primaryScreen())
        move(screen->geometry().center() - rect().center());

    if (AuthManager::instance().hasToken()) {
        m_stack->setCurrentIndex(0);
        // When a splash is covering startup, stay hidden and let it show
        // "Restoring your session"; otherwise show our own checking card.
        if (!silentChecking) { show(); raise(); activateWindow(); }
        AuthManager::instance().validateStoredToken();
    } else {
        showSignIn();
        // Test hook: lets automated runs drive the pairing flow without a
        // (flaky) synthetic click. No effect unless the env var is set.
        if (qEnvironmentVariableIsSet("NATIVEOFFICE_AUTOSIGNIN"))
            QTimer::singleShot(0, m_signInBtn, &QPushButton::click);
    }
}

void LoginGate::showSignIn(const QString& error) {
    m_dotTimer->stop();
    m_signInBtn->setEnabled(true);
    m_signInBtn->setText(tr("Sign in to continue"));
    if (error.isEmpty()) {
        m_errorLabel->hide();
    } else {
        m_errorLabel->setText(error);
        m_errorLabel->show();
    }
    m_stack->setCurrentIndex(1);
    // Sign-in is interactive: tell any covering splash to step aside, and make
    // sure our window is on screen (it may have stayed hidden during a silent
    // stored-session check).
    emit signInRequired();
    if (!isVisible()) { show(); raise(); activateWindow(); }
}

void LoginGate::finishGate() {
    m_done = true;
    m_dotTimer->stop();
    emit proceed();
    close();                       // WA_DeleteOnClose cleans up
}

void LoginGate::closeEvent(QCloseEvent* e) {
    // Closing the gate without signing in exits the app — the suite is gated.
    if (!m_done) {
        AuthManager::instance().cancelDeviceFlow();
        qApp->quit();
    }
    e->accept();
}

} // namespace NativeOffice
