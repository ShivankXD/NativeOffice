// ─────────────────────────────────────────────────────────────────────────────
// RecentFilesWidget.cpp  (Sprint 3)
// Clickable recent-files list backed by RecentFilesManager.
// ─────────────────────────────────────────────────────────────────────────────
#include "RecentFilesWidget.h"
#include "core/theme/ThemeManager.h"
#include "core/application/RecentFilesManager.h"

#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QFont>
#include <QFrame>
#include <QSizePolicy>
#include <QDateTime>
#include <QMouseEvent>
#include <QMenu>
#include <QAction>
#include <QCursor>

namespace NativeOffice {

// ─────────────────────────────────────────────────────────────────────────────
// Clickable row widget — a thin wrapper that emits clicked() on mouse release
// ─────────────────────────────────────────────────────────────────────────────
class ClickableRow : public QWidget {
    Q_OBJECT
public:
    explicit ClickableRow(QWidget* parent = nullptr) : QWidget(parent) {
        setMouseTracking(true);
    }
signals:
    void clicked();
    void removeRequested();
protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton)  emit clicked();
        if (e->button() == Qt::RightButton) showContextMenu(e->globalPosition().toPoint());
        QWidget::mousePressEvent(e);
    }
    void enterEvent(QEnterEvent* e) override {
        setStyleSheet("QWidget { background-color: #F5F6FA; border-radius: 10px; }");
        QWidget::enterEvent(e);
    }
    void leaveEvent(QEvent* e) override {
        setStyleSheet("QWidget { background-color: transparent; }");
        QWidget::leaveEvent(e);
    }
private:
    void showContextMenu(const QPoint& pos) {
        QMenu menu(this);
        auto* rmAct = menu.addAction("Remove from list");
        connect(rmAct, &QAction::triggered, this, &ClickableRow::removeRequested);
        menu.exec(pos);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// RecentFilesWidget
// ─────────────────────────────────────────────────────────────────────────────
RecentFilesWidget::RecentFilesWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    setObjectName("recentFilesWidget");

    // Load from persistent storage immediately
    refreshFromManager();

    // Auto-refresh whenever the list changes (e.g. after a Save)
    connect(&RecentFilesManager::instance(), &RecentFilesManager::listChanged,
            this, &RecentFilesWidget::refreshFromManager);
}

void RecentFilesWidget::refreshFromManager() {
    const auto entries = RecentFilesManager::instance().recentFiles();
    std::vector<RecentFile> files;
    files.reserve(entries.size());
    for (const auto& e : entries) {
        files.push_back({e.path, e.name, e.type, e.lastOpened});
    }
    setRecentFiles(files);
}

void RecentFilesWidget::buildUi() {
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(32, 24, 32, 24);
    m_layout->setSpacing(0);

    // ── Section header ─────────────────────────────────────────────────────
    auto* headerRow  = new QHBoxLayout();
    auto* heading    = new QLabel("Recent", this);
    heading->setObjectName("recentHeading");

    auto* showAllBtn = new QPushButton("Clear all", this);
    showAllBtn->setObjectName("seeAllBtn");
    showAllBtn->setCursor(Qt::PointingHandCursor);
    showAllBtn->setFlat(true);
    connect(showAllBtn, &QPushButton::clicked, this, []() {
        RecentFilesManager::instance().clearAll();
    });

    headerRow->addWidget(heading);
    headerRow->addStretch();
    headerRow->addWidget(showAllBtn);

    // ── Scroll area ────────────────────────────────────────────────────────
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName("recentScrollArea");
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    m_listWidget = new QWidget(m_scrollArea);
    m_listWidget->setObjectName("recentListWidget");
    m_listLayout = new QVBoxLayout(m_listWidget);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(2);
    m_listLayout->addStretch();

    m_scrollArea->setWidget(m_listWidget);

    m_layout->addLayout(headerRow);
    m_layout->addSpacing(16);
    m_layout->addWidget(m_scrollArea, 1);

    setStyleSheet(R"(
QWidget#recentFilesWidget {
    background-color: #FFFFFF;
}
QLabel#recentHeading {
    font-size: 20px;
    font-weight: 700;
    color: #1C1E26;
    font-family: "Segoe UI", "Inter", sans-serif;
}
QPushButton#seeAllBtn {
    font-size: 13px;
    font-weight: 500;
    color: #E8372A;
    background: transparent;
    border: none;
    font-family: "Segoe UI", "Inter", sans-serif;
    padding: 0;
}
QPushButton#seeAllBtn:hover {
    color: #C0271C;
    text-decoration: underline;
}
QScrollArea#recentScrollArea {
    background: transparent;
    border: none;
}
QWidget#recentListWidget {
    background: transparent;
}
QLabel#fileName {
    font-size: 14px;
    font-weight: 600;
    color: #1C1E26;
    font-family: "Segoe UI", "Inter", sans-serif;
    background: transparent;
}
QLabel#fileMeta {
    font-size: 12px;
    color: #6B7280;
    font-family: "Segoe UI", "Inter", sans-serif;
    background: transparent;
}
QLabel#fileTypeBadge {
    font-size: 11px;
    font-weight: 600;
    border-radius: 4px;
    padding: 2px 7px;
}
)");
}

void RecentFilesWidget::setRecentFiles(const std::vector<RecentFile>& files) {
    m_files = files;
    populateList();
}

void RecentFilesWidget::populateList() {
    // Remove old rows, keep the trailing stretch
    while (m_listLayout->count() > 1) {
        auto* item = m_listLayout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    if (m_files.empty()) {
        // ── Empty state ────────────────────────────────────────────────────
        auto* ph = new QWidget(m_listWidget);
        ph->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        auto* pl = new QVBoxLayout(ph);
        pl->setAlignment(Qt::AlignCenter);
        pl->setSpacing(12);

        auto* icon = new QLabel("📄", ph);
        icon->setAlignment(Qt::AlignCenter);
        icon->setStyleSheet("font-size: 48px; background: transparent;");

        auto* msg  = new QLabel(
            "No recent files yet.\nOpen or create a document to get started.", ph);
        msg->setAlignment(Qt::AlignCenter);
        msg->setWordWrap(true);
        msg->setStyleSheet(R"(
            font-size: 14px; color: #9CA3AF;
            font-family: "Segoe UI", "Inter", sans-serif;
            background: transparent;
        )");

        pl->addStretch();
        pl->addWidget(icon);
        pl->addWidget(msg);
        pl->addStretch();

        m_listLayout->insertWidget(0, ph);
        return;
    }

    int idx = 0;
    for (const auto& file : m_files) {
        auto* row = makeFileRow(file);
        m_listLayout->insertWidget(idx++, row);

        auto* sep = new QFrame(m_listWidget);
        sep->setFrameShape(QFrame::HLine);
        sep->setFixedHeight(1);
        sep->setStyleSheet("background: #F3F4F6; border: none;");
        m_listLayout->insertWidget(idx++, sep);
    }
}

QWidget* RecentFilesWidget::makeFileRow(const RecentFile& file) {
    auto* row = new ClickableRow(m_listWidget);
    row->setCursor(Qt::PointingHandCursor);
    row->setFixedHeight(60);

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(14);

    // ── Type badge ────────────────────────────────────────────────────────
    QString badgeColor, badgeBg;
    if (file.type == "Writer") {
        badgeColor = "#1D4ED8"; badgeBg = "#DBEAFE";
    } else if (file.type == "Calc") {
        badgeColor = "#15803D"; badgeBg = "#DCFCE7";
    } else {
        badgeColor = "#C2410C"; badgeBg = "#FFEDD5";
    }

    auto* badge = new QLabel(file.type, row);
    badge->setObjectName("fileTypeBadge");
    badge->setFixedWidth(80);
    badge->setAlignment(Qt::AlignCenter);
    badge->setAttribute(Qt::WA_TransparentForMouseEvents);
    badge->setStyleSheet(
        QString("color: %1; background-color: %2; border-radius: 4px;"
                "font-size: 11px; font-weight: 600; padding: 2px 7px;")
            .arg(badgeColor, badgeBg));

    // ── File name + path + date ────────────────────────────────────────────
    auto* textCol    = new QWidget(row);
    textCol->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* textLayout = new QVBoxLayout(textCol);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    auto* nameLabel = new QLabel(file.name, textCol);
    nameLabel->setObjectName("fileName");

    // Show abbreviated path + date
    const QString dirPart = file.path.section('/', 0, -2).section('\\', 0, -2);
    auto* metaLabel = new QLabel(
        dirPart + "  •  " + file.lastOpened.toString("MMM d, yyyy"), textCol);
    metaLabel->setObjectName("fileMeta");
    metaLabel->setToolTip(file.path);

    textLayout->addWidget(nameLabel);
    textLayout->addWidget(metaLabel);

    // ── Open-arrow indicator ───────────────────────────────────────────────
    auto* arrow = new QLabel("›", row);
    arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
    arrow->setStyleSheet("color: #D1D5DB; font-size: 20px; background: transparent;");

    layout->addWidget(badge);
    layout->addWidget(textCol, 1);
    layout->addWidget(arrow);

    // ── Signals ───────────────────────────────────────────────────────────
    const QString path = file.path;
    connect(row, &ClickableRow::clicked, this, [this, path]() {
        emit fileSelected(path);
    });
    connect(row, &ClickableRow::removeRequested, this, [path]() {
        RecentFilesManager::instance().removeFile(path);
    });

    return row;
}

} // namespace NativeOffice

// Required for the MOC to pick up the nested ClickableRow Q_OBJECT
#include "RecentFilesWidget.moc"
