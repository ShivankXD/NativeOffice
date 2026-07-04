// ─────────────────────────────────────────────────────────────────────────────
// OutlineWidget.cpp  (Sprint 12)
// ─────────────────────────────────────────────────────────────────────────────
#include "OutlineWidget.h"
#include "SlideData.h"
#include "core/theme/ThemeManager.h"

#include <QVBoxLayout>
#include <QTreeWidget>
#include <QHeaderView>

namespace NativeOffice {

namespace {
constexpr int kRoleSlide = Qt::UserRole + 1;
constexpr int kRoleItem  = Qt::UserRole + 2;   // -1 = slide title row
}

OutlineWidget::OutlineWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("outlineWidget");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(16);
    m_tree->setAnimated(true);
    m_tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);

    layout->addWidget(m_tree);

    applyTheme();
    connect(&ThemeManager::instance(), &ThemeManager::modeChanged,
            this, [this](ThemeMode) { applyTheme(); });

    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int) {
        if (!item) return;
        emit slideSelected(item->data(0, kRoleSlide).toInt());
    });

    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int) {
        if (m_ignoreEdits || !item) return;
        const int slideIdx = item->data(0, kRoleSlide).toInt();
        const int itemIdx  = item->data(0, kRoleItem).toInt();
        emit textEdited(slideIdx, itemIdx, item->text(0));
    });
}

void OutlineWidget::applyTheme() {
    if (ThemeManager::instance().isDark()) {
        setStyleSheet(R"(
QTreeWidget {
    background-color: #0D1117;
    color: #E6E9F0;
    border: none;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-size: 13px;
}
QTreeWidget::item {
    padding: 4px 2px;
    border-radius: 4px;
}
QTreeWidget::item:hover {
    background-color: #1E2737;
}
QTreeWidget::item:selected {
    background-color: #E8372A;
    color: #FFFFFF;
}
)");
    } else {
        setStyleSheet(R"(
QTreeWidget {
    background-color: #F3F4F6;
    color: #2F3440;
    border: none;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-size: 13px;
}
QTreeWidget::item {
    padding: 4px 2px;
    border-radius: 4px;
}
QTreeWidget::item:hover {
    background-color: #E7E9EE;
}
QTreeWidget::item:selected {
    background-color: #E8372A;
    color: #FFFFFF;
}
)");
    }
}

void OutlineWidget::rebuild(const std::vector<SlideData>& deck) {
    m_ignoreEdits = true;
    m_tree->clear();

    for (int s = 0; s < static_cast<int>(deck.size()); ++s) {
        const SlideData& slide = deck[s];

        auto* titleRow = new QTreeWidgetItem(m_tree);
        titleRow->setText(0, QString("%1. %2").arg(s + 1).arg(slide.title));
        titleRow->setData(0, kRoleSlide, s);
        titleRow->setData(0, kRoleItem, -1);
        titleRow->setFlags(titleRow->flags() | Qt::ItemIsEditable);
        QFont f = titleRow->font(0);
        f.setBold(true);
        titleRow->setFont(0, f);

        for (int i = 0; i < static_cast<int>(slide.items.size()); ++i) {
            const SlideItem& it = slide.items[i];
            if (it.type != SlideItemType::TextBox || it.isPlaceholder) continue;
            if (it.text.trimmed().isEmpty()) continue;

            auto* childRow = new QTreeWidgetItem(titleRow);
            childRow->setText(0, it.text);
            childRow->setData(0, kRoleSlide, s);
            childRow->setData(0, kRoleItem, i);
            childRow->setFlags(childRow->flags() | Qt::ItemIsEditable);
        }
    }

    m_tree->expandAll();
    m_ignoreEdits = false;
}

void OutlineWidget::setActiveSlide(int slideIndex) {
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* item = m_tree->topLevelItem(i);
        if (item->data(0, kRoleSlide).toInt() == slideIndex) {
            m_tree->setCurrentItem(item);
            m_tree->scrollToItem(item);
            break;
        }
    }
}

} // namespace NativeOffice
