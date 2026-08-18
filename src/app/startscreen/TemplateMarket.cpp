// ─────────────────────────────────────────────────────────────────────────────
// TemplateMarket.cpp: see TemplateMarket.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "TemplateMarket.h"
#include "HomeKit.h"
#include "LucideIcons.h"
#include "TemplateArt.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

namespace NativeOffice {

namespace {

constexpr int kCardW = 224;
constexpr int kThumbH = 152;

struct Category { QString label; const char* icon; };

const QVector<Category>& categories() {
    static const QVector<Category> cats = {
        { QStringLiteral("All templates"), Lucide::kFolderOpen },
        { QStringLiteral("Documents"),     Lucide::kFileText },
        { QStringLiteral("Spreadsheets"),  Lucide::kTable },
        { QStringLiteral("Presentations"), Lucide::kPresentation },
        { QStringLiteral("Work"),          Lucide::kBookOpen },
        { QStringLiteral("Personal"),      Lucide::kStar },
        { QStringLiteral("School"),        Lucide::kPencil },
        { QStringLiteral("Finance"),       Lucide::kTrendingUp },
    };
    return cats;
}

bool matchesCategory(const TemplateEntry& t, int index) {
    switch (index) {
    case 0: return true;
    case 1: return t.type == DocumentType::Writer;
    case 2: return t.type == DocumentType::Calc;
    case 3: return t.type == DocumentType::Impress;
    default: break;
    }
    return t.collection == categories()[index].label;
}

QString typeName(DocumentType t) {
    switch (t) {
    case DocumentType::Calc:    return QStringLiteral("Spreadsheet");
    case DocumentType::Impress: return QStringLiteral("Presentation");
    case DocumentType::Pdf:     return QStringLiteral("PDF");
    case DocumentType::Writer:  break;
    }
    return QStringLiteral("Document");
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Catalogue
// ─────────────────────────────────────────────────────────────────────────────
const QVector<TemplateEntry>& templateCatalogue() {
    static const QVector<TemplateEntry> all = {
        // ── Documents ───────────────────────────────────────────────────────
        { QStringLiteral("Professional Resume"), DocumentType::Writer, QStringLiteral("Work"),
          QStringLiteral("Clean two-column CV with a skills sidebar") },
        { QStringLiteral("Modern Resume"), DocumentType::Writer, QStringLiteral("Work"),
          QStringLiteral("A bolder take, built around a headline") },
        { QStringLiteral("Cover Letter"), DocumentType::Writer, QStringLiteral("Work"),
          QStringLiteral("One page, addressed and ready to edit") },
        { QStringLiteral("Business Letter"), DocumentType::Writer, QStringLiteral("Work"),
          QStringLiteral("Formal letterhead layout with block addressing") },
        { QStringLiteral("Project Report"), DocumentType::Writer, QStringLiteral("Work"),
          QStringLiteral("Title band, findings, and a summary table") },
        { QStringLiteral("Meeting Notes"), DocumentType::Writer, QStringLiteral("Work"),
          QStringLiteral("Attendees, decisions, and action items") },
        { QStringLiteral("Newsletter"), DocumentType::Writer, QStringLiteral("Work"),
          QStringLiteral("Masthead and two-column body") },
        { QStringLiteral("Invoice Letter"), DocumentType::Writer, QStringLiteral("Finance"),
          QStringLiteral("Itemised bill with totals and terms") },
        { QStringLiteral("To-Do List"), DocumentType::Writer, QStringLiteral("Personal"),
          QStringLiteral("Checklist with priority and due dates") },
        { QStringLiteral("Academic Essay"), DocumentType::Writer, QStringLiteral("School"),
          QStringLiteral("Title page, body, and a references section") },
        { QStringLiteral("Press Release"), DocumentType::Writer, QStringLiteral("Work"),
          QStringLiteral("Dateline, boilerplate, and media contact") },

        // ── Spreadsheets ────────────────────────────────────────────────────
        { QStringLiteral("Monthly Budget"), DocumentType::Calc, QStringLiteral("Finance"),
          QStringLiteral("Income against categorised spending") },
        { QStringLiteral("Invoice"), DocumentType::Calc, QStringLiteral("Finance"),
          QStringLiteral("Line items, tax, and an auto total") },
        { QStringLiteral("Expense Tracker"), DocumentType::Calc, QStringLiteral("Finance"),
          QStringLiteral("Day-by-day log with running totals") },
        { QStringLiteral("Sales Dashboard"), DocumentType::Calc, QStringLiteral("Work"),
          QStringLiteral("Headline tiles over a regional breakdown") },
        { QStringLiteral("Inventory List"), DocumentType::Calc, QStringLiteral("Work"),
          QStringLiteral("Stock on hand, reorder points, and value") },
        { QStringLiteral("Loan Calculator"), DocumentType::Calc, QStringLiteral("Finance"),
          QStringLiteral("Repayment schedule from rate and term") },
        { QStringLiteral("Timesheet"), DocumentType::Calc, QStringLiteral("Work"),
          QStringLiteral("Hours by day with a weekly total") },
        { QStringLiteral("Grade Book"), DocumentType::Calc, QStringLiteral("School"),
          QStringLiteral("Weighted marks per student") },
        { QStringLiteral("Habit Tracker"), DocumentType::Calc, QStringLiteral("Personal"),
          QStringLiteral("A month of ticks and a streak count") },
        { QStringLiteral("Attendance Sheet"), DocumentType::Calc, QStringLiteral("School"),
          QStringLiteral("Roll call grid with presence totals") },
        { QStringLiteral("Savings Goal"), DocumentType::Calc, QStringLiteral("Finance"),
          QStringLiteral("Target, contributions, and progress") },

        // ── Presentations ───────────────────────────────────────────────────
        { QStringLiteral("Pitch Deck"), DocumentType::Impress, QStringLiteral("Work"),
          QStringLiteral("Problem, solution, market, traction, ask") },
        { QStringLiteral("Business Review"), DocumentType::Impress, QStringLiteral("Work"),
          QStringLiteral("Quarterly highlights, metrics, and risks") },
        { QStringLiteral("Project Plan"), DocumentType::Impress, QStringLiteral("Work"),
          QStringLiteral("Objectives, timeline, team, and risks") },
        { QStringLiteral("Portfolio"), DocumentType::Impress, QStringLiteral("Personal"),
          QStringLiteral("Selected work with a process section") },
        { QStringLiteral("Marketing Plan"), DocumentType::Impress, QStringLiteral("Work"),
          QStringLiteral("Goals, audience, channels, and budget") },
        { QStringLiteral("Company Profile"), DocumentType::Impress, QStringLiteral("Work"),
          QStringLiteral("Who you are, what you do, the numbers") },
        { QStringLiteral("Product Roadmap"), DocumentType::Impress, QStringLiteral("Work"),
          QStringLiteral("Now, next, and later, with owners") },
        { QStringLiteral("Training Deck"), DocumentType::Impress, QStringLiteral("School"),
          QStringLiteral("Agenda, modules, and a recap") },
    };
    return all;
}

QVector<TemplateEntry> featuredTemplates() {
    // The four on the Home card: one of each shape, matching the reference.
    static const QStringList picks = {
        QStringLiteral("Professional Resume"), QStringLiteral("Monthly Budget"),
        QStringLiteral("Pitch Deck"), QStringLiteral("Project Report") };
    QVector<TemplateEntry> out;
    for (const QString& p : picks)
        for (const TemplateEntry& t : templateCatalogue())
            if (t.name == p) { out.append(t); break; }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// TemplateMarket
// ─────────────────────────────────────────────────────────────────────────────
TemplateMarket::TemplateMarket(QWidget* parent, int initialCategory)
    : QDialog(parent), m_category(qBound(0, initialCategory, categories().size() - 1)) {
    setObjectName("tplMarket");
    setWindowTitle(tr("Templates"));
    resize(1262, 752);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Category rail ───────────────────────────────────────────────────────
    auto* rail = new QWidget(this);
    rail->setObjectName("tplRail");
    rail->setFixedWidth(228);
    auto* rl = new QVBoxLayout(rail);
    rl->setContentsMargins(16, 22, 16, 18);
    rl->setSpacing(3);

    rl->addWidget(heading(tr("Templates"), 17, Home::kText, true, rail));
    rl->addSpacing(14);

    for (int i = 0; i < categories().size(); ++i) {
        if (i == 4) {
            rl->addSpacing(12);
            auto* cap = heading(tr("COLLECTIONS"), 10, Home::kFaint, true, rail);
            cap->setContentsMargins(10, 0, 0, 4);
            rl->addWidget(cap);
        }
        auto* item = new ClickableFrame(rail);
        item->setObjectName("railItem");
        item->setFixedHeight(38);
        auto* il = new QHBoxLayout(item);
        il->setContentsMargins(11, 0, 11, 0);
        il->setSpacing(11);
        il->addWidget(Lucide::label(categories()[i].icon, Home::kMuted, 15, item));
        auto* lab = heading(categories()[i].label, 12, Home::kTextBody, false, item);
        lab->setAttribute(Qt::WA_TransparentForMouseEvents);
        il->addWidget(lab);
        il->addStretch();
        item->onClick = [this, i] { selectCategory(i); };
        rl->addWidget(item);
        m_railItems.append(item);
    }
    rl->addStretch();

    auto* note = heading(tr("More templates are on the way. Anything you build can be "
                            "saved as your own."), 11, Home::kFaint, false, rail);
    note->setWordWrap(true);
    rl->addWidget(note);
    root->addWidget(rail);

    // ── Main column ─────────────────────────────────────────────────────────
    auto* main = new QWidget(this);
    auto* mv = new QVBoxLayout(main);
    mv->setContentsMargins(28, 22, 24, 20);
    mv->setSpacing(16);

    auto* head = new QHBoxLayout();
    head->setSpacing(14);
    auto* titles = new QVBoxLayout();
    titles->setSpacing(3);
    titles->addWidget(heading(tr("Start from a template"), 21, Home::kText, true, main));
    m_count = heading(QString(), 12, Home::kMuted, false, main);
    titles->addWidget(m_count);
    head->addLayout(titles);
    head->addStretch();

    m_search = new QLineEdit(main);
    m_search->setObjectName("tplSearch");
    m_search->setPlaceholderText(tr("Search templates…"));
    m_search->setFixedSize(280, 38);
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, [this] { rebuildGrid(); });
    head->addWidget(m_search, 0, Qt::AlignVCenter);
    mv->addLayout(head);

    m_scroll = new QScrollArea(main);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_gridHost = new QWidget(m_scroll);
    m_gridHost->setObjectName("tplGridHost");
    auto* hostLayout = new QVBoxLayout(m_gridHost);
    hostLayout->setContentsMargins(0, 0, 8, 8);
    hostLayout->setSpacing(0);
    m_grid = new QGridLayout();
    m_grid->setSpacing(18);
    m_grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    hostLayout->addLayout(m_grid);
    hostLayout->addStretch();
    m_scroll->setWidget(m_gridHost);
    mv->addWidget(m_scroll, 1);

    root->addWidget(main, 1);

    setStyleSheet(QString(R"(
        QDialog#tplMarket { background:%1; }
        QWidget#tplRail { background:%2; border-right:1px solid %3; }
        QWidget#tplGridHost { background:transparent; }
        QScrollArea { background:transparent; }
        #railItem { background:transparent; border-radius:9px; }
        #railItem:hover { background:%4; }
        #railItemOn { background:#221F3C; border-radius:9px; }
        QLineEdit#tplSearch { background:%5; border:1px solid %3; border-radius:10px;
            color:%6; padding:0 12px; font:13px 'Segoe UI'; }
        QLineEdit#tplSearch:focus { border:1px solid %7; }
        #tplCard { background:%2; border:1px solid %3; border-radius:13px; }
        #tplCard:hover { background:%4; border:1px solid %7; }
        QScrollBar:vertical { background:transparent; width:10px; margin:2px; }
        QScrollBar::handle:vertical { background:#2A3244; border-radius:5px; min-height:30px; }
        QScrollBar::add-line, QScrollBar::sub-line { height:0; }
    )").arg(Home::kBg, Home::kPanel, Home::kBorder, Home::kPanelHover, Home::kPanelSoft,
            Home::kTextBody, Home::kAccent));

    selectCategory(m_category);
}

void TemplateMarket::selectCategory(int index) {
    m_category = index;
    for (int i = 0; i < m_railItems.size(); ++i) {
        m_railItems[i]->setObjectName(i == index ? "railItemOn" : "railItem");
        m_railItems[i]->style()->unpolish(m_railItems[i]);
        m_railItems[i]->style()->polish(m_railItems[i]);
    }
    rebuildGrid();
}

int TemplateMarket::columnsForWidth() const {
    // Measured from the dialog, not from the scroll viewport: the viewport has
    // no useful width until the first real layout pass, and the grid is first
    // built from the constructor.
    constexpr int kChrome = 228 + 28 + 24 + 12;   // rail + margins + scrollbar
    return qBound(1, (width() - kChrome + 18) / (kCardW + 18), 6);
}

void TemplateMarket::resizeEvent(QResizeEvent* e) {
    QDialog::resizeEvent(e);
    if (m_grid && columnsForWidth() != m_columns) rebuildGrid();
}

void TemplateMarket::rebuildGrid() {
    // Tear the previous cards down; QGridLayout has no clear().
    while (QLayoutItem* it = m_grid->takeAt(0)) {
        if (QWidget* w = it->widget()) w->deleteLater();
        delete it;
    }

    const QString needle = m_search ? m_search->text().trimmed() : QString();
    const qreal dpr = devicePixelRatio();

    int shown = 0;
    const int columns = columnsForWidth();
    m_columns = columns;
    for (const TemplateEntry& t : templateCatalogue()) {
        if (!matchesCategory(t, m_category)) continue;
        if (!needle.isEmpty()
            && !t.name.contains(needle, Qt::CaseInsensitive)
            && !t.blurb.contains(needle, Qt::CaseInsensitive)
            && !t.collection.contains(needle, Qt::CaseInsensitive))
            continue;

        auto* card = new ClickableFrame(m_gridHost);
        card->setObjectName("tplCard");
        card->setFixedWidth(kCardW);
        auto* cv = new QVBoxLayout(card);
        cv->setContentsMargins(12, 12, 12, 12);
        cv->setSpacing(10);

        auto* thumb = new QLabel(card);
        thumb->setFixedSize(kCardW - 24, kThumbH);
        thumb->setPixmap(TemplateArt::preview(t.name, t.type,
                                              QSize(kCardW - 24, kThumbH), dpr));
        thumb->setAttribute(Qt::WA_TransparentForMouseEvents);
        thumb->setStyleSheet("background:transparent;");
        cv->addWidget(thumb);

        auto* name = label600(t.name, 13, Home::kText, card);
        name->setAttribute(Qt::WA_TransparentForMouseEvents);
        cv->addWidget(name);

        auto* blurb = heading(t.blurb, 11, Home::kMuted, false, card);
        blurb->setWordWrap(true);
        blurb->setFixedHeight(30);
        blurb->setAttribute(Qt::WA_TransparentForMouseEvents);
        cv->addWidget(blurb);

        auto* footer = new QHBoxLayout();
        footer->setSpacing(6);
        const FileKind kind = fileKindForModule(
            t.type == DocumentType::Calc    ? QStringLiteral("Calc")
          : t.type == DocumentType::Impress ? QStringLiteral("Impress")
                                            : QStringLiteral("Writer"));
        footer->addWidget(badge(kind.letter, kind.color, 18, card));
        auto* kindLabel = heading(typeName(t.type), 11, Home::kFaint, false, card);
        kindLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        footer->addWidget(kindLabel);
        footer->addStretch();
        auto* use = heading(tr("Use"), 11, Home::kAccentSoft, false, card);
        use->setAttribute(Qt::WA_TransparentForMouseEvents);
        footer->addWidget(use);
        footer->addWidget(Lucide::label(Lucide::kArrowRight, Home::kAccentSoft, 13, card));
        cv->addLayout(footer);

        const DocumentType type = t.type;
        const QString name2 = t.name;
        card->onClick = [this, type, name2] {
            emit templateChosen(type, name2);
            accept();
        };

        m_grid->addWidget(card, shown / columns, shown % columns);
        ++shown;
    }

    if (shown == 0) {
        auto* empty = heading(tr("Nothing here matches \"%1\".").arg(needle),
                              13, Home::kMuted, false, m_gridHost);
        m_grid->addWidget(empty, 0, 0);
    }
    m_count->setText(shown == 1 ? tr("1 template ready to open")
                                : tr("%1 templates ready to open").arg(shown));
}

} // namespace NativeOffice
