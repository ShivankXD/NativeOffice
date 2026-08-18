#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TemplateMarket.h: the full template browser behind "View all".
//
// Replaces the old three-tab dialog with a marketplace: a category rail, a
// live search box, and a scrollable grid of cards whose thumbnails are painted
// miniatures of the documents themselves (TemplateArt).
//
// The catalogue lives in one table here so the Home panel and the marketplace
// can never drift apart, and so a template added to the app appears in both by
// editing a single list. Every entry maps to a template the editors actually
// fill in, nothing in the grid opens an empty document pretending to be one.
// ─────────────────────────────────────────────────────────────────────────────

#include "core/application/AppController.h"

#include <QDialog>
#include <QString>
#include <QVector>

class QLineEdit;
class QGridLayout;
class QScrollArea;
class QLabel;

namespace NativeOffice {

struct TemplateEntry {
    QString      name;
    DocumentType type;
    QString      collection;   // "Work" | "Personal" | "School" | "Finance"
    QString      blurb;        // one line, shown under the name
};

// The whole catalogue, in display order.
const QVector<TemplateEntry>& templateCatalogue();

// The handful shown on the Home card.
QVector<TemplateEntry> featuredTemplates();

class TemplateMarket : public QDialog {
    Q_OBJECT

public:
    // `initialType` preselects a category rail entry; pass Writer for "all".
    explicit TemplateMarket(QWidget* parent = nullptr, int initialCategory = 0);

signals:
    void templateChosen(DocumentType type, const QString& name);
    void blankRequested(DocumentType type);

protected:
    // The grid reflows when the window is resized, so a wider dialog shows more
    // cards per row instead of clipping the last column.
    void resizeEvent(QResizeEvent*) override;

private:
    void rebuildGrid();
    void selectCategory(int index);
    [[nodiscard]] int columnsForWidth() const;

    QLineEdit*   m_search   { nullptr };
    QGridLayout* m_grid     { nullptr };
    QWidget*     m_gridHost { nullptr };
    QScrollArea* m_scroll   { nullptr };
    QLabel*      m_count    { nullptr };
    QVector<QWidget*> m_railItems;
    int m_category { 0 };      // 0 all, 1 documents, 2 sheets, 3 slides, 4+ collections
    int m_columns  { 0 };      // last laid-out column count
};

} // namespace NativeOffice
