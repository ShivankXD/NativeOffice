// ─────────────────────────────────────────────────────────────────────────────
// StartScreen.cpp — NativeOffice home dashboard (dark theme).
// ─────────────────────────────────────────────────────────────────────────────
#include "StartScreen.h"
#include "core/application/RecentFilesManager.h"

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
#include <QPixmap>
#include <QTime>
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

} // namespace

StartScreen::StartScreen(AppController* controller, QWidget* parent)
    : QWidget(parent), m_controller(controller)
{
    setObjectName("startScreen");
    buildUi();
}

void StartScreen::openFileDialog() {
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
}

// ── Left sidebar ──────────────────────────────────────────────────────────────
QWidget* StartScreen::buildSidebar() {
    auto* bar = new QWidget(this);
    bar->setObjectName("sidebar");
    bar->setFixedWidth(230);
    auto* v = new QVBoxLayout(bar);
    v->setContentsMargins(14, 18, 14, 18);
    v->setSpacing(4);

    // Logo
    auto* logoRow = new QWidget(bar);
    auto* lh = new QHBoxLayout(logoRow);
    lh->setContentsMargins(6, 0, 0, 8); lh->setSpacing(10);
    lh->addWidget(badge("N", "#3B82F6", 30, logoRow));
    lh->addWidget(heading("NativeOffice", 16, "#E6E9F0", true, logoRow));
    lh->addStretch();
    v->addWidget(logoRow);
    v->addSpacing(8);

    struct Nav { QString icon, label; int action; };  // action: -1 none, 0 home, see below
    const Nav items[] = {
        {"⌂","Home", 0}, {"📄","Documents", 1}, {"▦","Sheets", 2}, {"▤","Slides", 3},
        {"⤓","PDF Tools", -1}, {"🗎","Templates", 4}, {"☁","Cloud Drive", -1},
        {"👥","Shared with me", -1}, {"★","Favorites", -1}, {"🗑","Recycle Bin", -1},
    };
    for (const Nav& n : items) {
        auto* item = new ClickableFrame(bar);
        item->setObjectName(n.action == 0 ? "navItemActive" : "navItem");
        auto* il = new QHBoxLayout(item);
        il->setContentsMargins(12, 0, 12, 0); il->setSpacing(12);
        item->setFixedHeight(40);
        auto* ic = new QLabel(n.icon, item);
        ic->setStyleSheet("font:15px 'Segoe UI Emoji';background:transparent;color:#9AA4B8;");
        auto* tx = new QLabel(n.label, item);
        tx->setStyleSheet(QString("background:transparent;font:13px 'Segoe UI';color:%1;")
                              .arg(n.action == 0 ? "#FFFFFF" : "#AEB6C6"));
        il->addWidget(ic); il->addWidget(tx); il->addStretch();
        const int action = n.action;
        item->onClick = [this, action]{
            switch (action) {
            case 1: emit newDocumentRequested(DocumentType::Writer);  break;
            case 2: emit newDocumentRequested(DocumentType::Calc);    break;
            case 3: emit newDocumentRequested(DocumentType::Impress); break;
            case 4: showTemplatesDialog(0); break;
            default: break;
            }
        };
        v->addWidget(item);
    }
    v->addStretch();

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
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(28, 0, 24, 0); h->setSpacing(16);

    auto* search = new QLineEdit(bar);
    search->setObjectName("searchBox");
    search->setPlaceholderText("   Search files, templates, tools…");
    search->setFixedHeight(40);
    search->setMaximumWidth(520);
    auto* kbd = new QLabel("Ctrl + K", search);
    kbd->setStyleSheet("background:#1B2230;border-radius:5px;color:#8A93A6;"
                       "font:11px 'Segoe UI';padding:2px 8px;");
    auto* sl = new QHBoxLayout(search);
    sl->setContentsMargins(0, 0, 8, 0); sl->addStretch(); sl->addWidget(kbd);

    h->addStretch();
    h->addWidget(search);
    h->addStretch();

    auto iconBtn = [&](const QString& glyph, std::function<void()> cb) {
        auto* b = new QToolButton(bar);
        b->setText(glyph);
        b->setObjectName("topIcon");
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(38, 38);
        if (cb) connect(b, &QToolButton::clicked, this, cb);
        return b;
    };
    h->addWidget(iconBtn("🔔", nullptr));
    h->addWidget(iconBtn("?", nullptr));
    h->addWidget(iconBtn("⚙", [this]{ emit settingsRequested(); }));
    h->addWidget(badge("S", "#2563EB", 38, bar));   // avatar

    bar->setStyleSheet(R"(
        QWidget#topBar { background:#0D1117; border-bottom:1px solid #1B212C; }
        QLineEdit#searchBox { background:#161B26; border:1px solid #232A38;
            border-radius:10px; color:#C7CEDC; padding-left:10px; font:13px 'Segoe UI'; }
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
    const int hr = QTime::currentTime().hour();
    const QString greet = hr < 5  ? "Burning the midnight oil, Shivank 🌙"
                        : hr < 12 ? "Good morning, Shivank ☀️"
                        : hr < 17 ? "Good afternoon, Shivank 👋"
                                  : "Good evening, Shivank 🌆";
    wcol->addWidget(heading(greet, 26, "#F0F2F7", true, welcome));
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
    v->addWidget(heading("Quick Actions", 16, "#E6E9F0", true, col));
    v->addWidget(buildQuickActions());

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
            if (action == 0) openFileDialog();
            else emit newDocumentRequested(action == 1 ? DocumentType::Writer
                                          : action == 2 ? DocumentType::Calc
                                                        : DocumentType::Impress);
        };
        g->addWidget(card, 1);
    }
    w->setStyleSheet(R"(
        #createCard { background:#141A24; border:1px solid #222A38; border-radius:14px; }
        #createCard:hover { border:1px solid #3B82F6; background:#161D29; }
    )");
    return w;
}

QWidget* StartScreen::buildQuickActions() {
    auto* w = new QWidget(this);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0); h->setSpacing(14);

    struct QA { QString icon, label, color; int action; };  // action: see lambda
    const QA qas[] = {
        {"🗎","New from Template","#7C5CFC", 0},
        {"⤒","Import from Device","#2563EB", 1},
        {"📕","PDF to Word","#DC2626", 2},
        {"✦","AI Document","#22C55E", 3},
        {"⋯","More Tools","#64748B", 4},
    };
    for (const QA& q : qas) {
        auto* a = new ClickableFrame(w);
        a->setObjectName("quickAction");
        a->setFixedHeight(46);
        auto* al = new QHBoxLayout(a);
        al->setContentsMargins(13, 0, 12, 0); al->setSpacing(9);
        al->addWidget(badge(q.icon, q.color, 26, a));
        auto* lbl = heading(q.label, 12, "#CCD3E0", false, a);
        lbl->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);  // let the row compress
        al->addWidget(lbl, 1);
        const int action = q.action;
        a->onClick = [this, action]{
            switch (action) {
            case 0: showTemplatesDialog(0); break;            // New from template
            case 1: openFileDialog(); break;                  // Import from device
            case 2: openFileDialog(); break;                  // PDF to Word → import
            case 3: emit newDocumentRequested(DocumentType::Writer); break;  // AI doc → Writer (AI tools live there)
            case 4: showTemplatesDialog(0); break;            // More tools → templates
            }
        };
        h->addWidget(a, 1);
    }
    w->setStyleSheet(R"(
        #quickAction { background:#141A24; border:1px solid #222A38; border-radius:10px; }
        #quickAction:hover { background:#1A2230; border:1px solid #2E3950; }
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
        rowf->onClick = [this, path]{ emit fileOpenRequested(path); };
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
        cv->addWidget(thumb);
        auto* foot = new QVBoxLayout(); foot->setContentsMargins(10, 8, 10, 8); foot->setSpacing(1);
        foot->addWidget(heading(t.title, 12, "#E2E6EE", true, c));
        foot->addWidget(heading(t.sub, 11, "#7B8494", false, c));
        cv->addLayout(foot);
        const DocumentType type = t.type;
        const QString name = t.title;
        c->onClick = [this, type, name]{ emit templateChosen(type, name); };
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
    auto twoCol = [&](QWidget* parent, const QString& icon, const QString& left,
                      const QString& right, const QString& rightColor) {
        auto* row = new QWidget(parent);
        auto* h = new QHBoxLayout(row); h->setContentsMargins(0, 0, 0, 0); h->setSpacing(10);
        auto* ic = new QLabel(icon, row);
        ic->setStyleSheet("font:14px 'Segoe UI Emoji';background:transparent;color:#9AA4B8;");
        h->addWidget(ic);
        h->addWidget(heading(left, 12, "#C3CAD8", false, row));
        h->addStretch();
        h->addWidget(heading(right, 12, rightColor, false, row));
        return row;
    };

    // Get Started
    { auto [p, pv] = makePanel("Get Started");
      pv->addWidget(twoCol(p, "▶", "Take a tour", "3 min", "#7B8494"));
      pv->addWidget(twoCol(p, "⌨", "Keyboard shortcuts", "View all", "#3B82F6"));
      pv->addWidget(twoCol(p, "✦", "What's new", "See updates", "#3B82F6"));
      v->addWidget(p); }

    // Your Activity
    { auto [p, pv] = makePanel("Your Activity");
      const int recent = int(RecentFilesManager::instance().recentFiles().size());
      pv->addWidget(twoCol(p, "🗎", "Documents opened", QString::number(recent), "#E6E9F0"));
      pv->addWidget(twoCol(p, "✎", "Files edited", QString::number(recent), "#E6E9F0"));
      pv->addWidget(twoCol(p, "⏱", "Time spent", "—", "#E6E9F0"));
      v->addWidget(p); }

    // Sync Status
    { auto [p, pv] = makePanel("Sync Status");
      auto* s = heading("All files are stored locally on this device.", 12, "#9AA4B8", false, p);
      s->setWordWrap(true);
      pv->addWidget(s);
      v->addWidget(p); }

    v->addStretch();
    col->setStyleSheet(R"(
        QFrame#sidePanel { background:#12161F; border:1px solid #202836; border-radius:14px; }
    )");
    return col;
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
            thumb->setStyleSheet(QString("background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
                "stop:0 %1,stop:1 #11151d);border-top-left-radius:10px;border-top-right-radius:10px;")
                .arg(cat.accent + "33"));   // translucent accent
            auto* tl = new QVBoxLayout(thumb); tl->setContentsMargins(12, 12, 12, 12);
            tl->addWidget(badge(cat.type == DocumentType::Calc ? "S"
                              : cat.type == DocumentType::Impress ? "P" : "W",
                              cat.accent, 28, thumb), 0, Qt::AlignLeft | Qt::AlignTop);
            tl->addStretch();
            cv->addWidget(thumb);
            auto* lbl = heading(name, 12, "#E2E6EE", false, c);
            lbl->setWordWrap(true);
            lbl->setContentsMargins(10, 8, 10, 10);
            cv->addWidget(lbl);
            const DocumentType type = cat.type;
            const bool blank = name.startsWith("Blank");
            const QString tplName = name;
            c->onClick = [this, type, blank, tplName, &dlg]{
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
