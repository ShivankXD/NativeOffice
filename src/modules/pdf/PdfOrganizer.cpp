// ─────────────────────────────────────────────────────────────────────────────
// PdfOrganizer.cpp — see PdfOrganizer.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfOrganizer.h"
#include "PdfEditSession.h"
#include "core/theme/ThemeManager.h"

#include <QDropEvent>
#include <QLabel>
#include <QMenu>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace NativeOffice {

using Pdf::EditSession;

namespace {

// QListWidget with InternalMove that reports the post-drop visual order.
class ReorderGrid : public QListWidget {
public:
    std::function<void()> onReordered;

protected:
    void dropEvent(QDropEvent* ev) override {
        QListWidget::dropEvent(ev);
        if (onReordered) onReordered();
    }
};

} // namespace

PageOrganizer::PageOrganizer(EditSession* session, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
{
    setObjectName("pdfOrganizer");

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    m_hint = new QLabel(tr("Drag and drop to reorder pages. Hold Ctrl or Shift to select multiple pages."), this);
    m_hint->setObjectName("pdfOrganizerHint");
    m_hint->setAlignment(Qt::AlignHCenter);
    m_hint->setFixedHeight(34);

    auto* grid = new ReorderGrid;
    grid->setParent(this);
    m_grid = grid;
    m_grid->setObjectName("pdfOrganizerGrid");
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setIconSize(QSize(170, 226));
    m_grid->setGridSize(QSize(200, 270));
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_grid->setDragDropMode(QAbstractItemView::InternalMove);
    m_grid->setDefaultDropAction(Qt::MoveAction);
    m_grid->setMovement(QListView::Snap);
    m_grid->setSpacing(12);
    m_grid->setWordWrap(true);

    grid->onReordered = [this] {
        if (m_applyingModel) return;
        // Visual order after the drop → permutation of source pages.
        std::vector<int> order;
        order.reserve(size_t(m_grid->count()));
        for (int i = 0; i < m_grid->count(); ++i)
            order.push_back(m_grid->item(i)->data(Qt::UserRole).toInt());
        // Only emit when something actually moved.
        for (size_t i = 0; i < order.size(); ++i) {
            if (order[i] != int(i)) {
                emit reorderRequested(order);
                return;
            }
        }
    };

    connect(m_grid, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        emit pageActivated(it->data(Qt::UserRole).toInt());
    });

    m_grid->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_grid, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (!m_session->hasDocument()) return;
        const auto pages = selectedPages();
        QMenu menu(this);
        if (!pages.empty()) {
            menu.addAction(tr("Rotate Left"),  this, [this, pages] { emit rotateRequested(pages, 270); });
            menu.addAction(tr("Rotate Right"), this, [this, pages] { emit rotateRequested(pages, 90); });
            menu.addSeparator();
            menu.addAction(tr("Extract to New PDF…"), this, [this, pages] { emit extractRequested(pages); });
            menu.addAction(tr("Delete"), this, [this, pages] { emit deleteRequested(pages); });
            menu.addSeparator();
        }
        QListWidgetItem* at = m_grid->itemAt(pos);
        const int after = at ? at->data(Qt::UserRole).toInt()
                             : m_session->pageCount() - 1;
        menu.addAction(tr("Insert Blank Page After"), this,
                       [this, after] { emit insertBlankAfterRequested(after); });
        menu.exec(m_grid->mapToGlobal(pos));
    });

    v->addWidget(m_hint);
    v->addWidget(m_grid, 1);

    connect(session, &EditSession::documentChanged, this, &PageOrganizer::refresh);
    connect(&ThemeManager::instance(), &ThemeManager::modeChanged,
            this, [this](ThemeMode) { applyTheme(); });
    applyTheme();
    refresh();
}

std::vector<int> PageOrganizer::selectedPages() const {
    std::vector<int> pages;
    for (QListWidgetItem* it : m_grid->selectedItems())
        pages.push_back(it->data(Qt::UserRole).toInt());
    std::sort(pages.begin(), pages.end());
    return pages;
}

void PageOrganizer::refresh() {
    m_applyingModel = true;
    m_grid->clear();
    m_nextThumb = 0;
    if (m_session->hasDocument()) {
        const int n = m_session->pageCount();
        for (int i = 0; i < n; ++i) {
            auto* it = new QListWidgetItem(QString::number(i + 1), m_grid);
            it->setData(Qt::UserRole, i);
            it->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        }
        if (isVisible())
            QTimer::singleShot(0, this, &PageOrganizer::renderNextThumb);
    }
    m_applyingModel = false;
}

void PageOrganizer::renderNextThumb() {
    if (!m_session->hasDocument()) return;
    if (m_nextThumb >= m_grid->count()) return;
    for (int k = 0; k < 2 && m_nextThumb < m_grid->count(); ++k, ++m_nextThumb) {
        // Item order can change under drag; find the item for source page
        // m_nextThumb by its data instead of by row.
        const int page = m_nextThumb;
        const QSizeF sz = m_session->renderer()->pageSizePt(page);
        const qreal scale = sz.width() > 0 ? 170.0 / sz.width() : 0.22;
        const QImage img = m_session->renderer()->renderPage(page, scale);
        if (img.isNull()) continue;
        for (int row = 0; row < m_grid->count(); ++row) {
            QListWidgetItem* it = m_grid->item(row);
            if (it->data(Qt::UserRole).toInt() == page) {
                it->setIcon(QIcon(QPixmap::fromImage(img)));
                break;
            }
        }
    }
    if (m_nextThumb < m_grid->count())
        QTimer::singleShot(0, this, &PageOrganizer::renderNextThumb);
}

void PageOrganizer::showEvent(QShowEvent*) {
    // Thumbnails are only rasterized while the organizer is actually shown.
    if (m_nextThumb < m_grid->count())
        QTimer::singleShot(0, this, &PageOrganizer::renderNextThumb);
}

void PageOrganizer::applyTheme() {
    const auto& tm = ThemeManager::instance();
    setStyleSheet(QString(R"(
QWidget#pdfOrganizer { background: %1; }
QLabel#pdfOrganizerHint { background: %1; color: %2; font: 9pt "Segoe UI"; }
QListWidget#pdfOrganizerGrid { background: %1; border: none; color: %3; font: 9pt "Segoe UI"; }
QListWidget#pdfOrganizerGrid::item { background: transparent; border: 2px solid transparent; border-radius: 4px; }
QListWidget#pdfOrganizerGrid::item:selected { border: 2px solid #6D5BE8; background: %4; }
QListWidget#pdfOrganizerGrid::item:hover { background: %4; }
)")
        .arg(tm.isDark() ? "#171B24" : "#E8EAEF",
             tm.chromeTextMuted(), tm.chromeText(), tm.chromeHoverBg()));
}

} // namespace NativeOffice
