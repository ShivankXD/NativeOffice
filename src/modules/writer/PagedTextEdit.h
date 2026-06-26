#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PagedTextEdit.h  (Sprint 17 — real pagination)
// A QTextEdit that renders its document as discrete A4-style pages: the
// document's page size is set so content reflows into real pages (respecting
// manual page breaks), the widget sizes itself to the full multi-page height so
// the surrounding scroll area scrolls through every page, and paintEvent draws
// the gray gaps between pages, soft per-page shadows and footer page numbers.
//
// Crucially the text is NOT offset, so QTextEdit's own cursor, selection,
// hit-testing, IME and editing all keep working unchanged.
// ─────────────────────────────────────────────────────────────────────────────

#include <QTextEdit>
#include <QColor>

namespace NativeOffice {

class PagedTextEdit : public QTextEdit {
    Q_OBJECT

public:
    explicit PagedTextEdit(QWidget* parent = nullptr);

    // Page geometry in *device* px (already scaled for the current zoom).
    void setPageMetrics(double pageW, double pageH, double margin);

    // false → continuous "web layout" (no pages, no gaps).
    void setPaged(bool on);
    [[nodiscard]] bool isPaged() const noexcept { return m_paged; }

    // Colour of the paper (gaps always use the desk colour).
    void setPaperColor(const QColor& c);

    [[nodiscard]] int  pageCountValue() const;

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    void syncHeight();

    double m_pageW   { 794.0 };
    double m_pageH   { 1123.0 };
    double m_margin  { 60.0 };
    bool   m_paged   { true };
    QColor m_paper   { "#FFFFFF" };
    QColor m_desk    { "#E8E9ED" };
};

} // namespace NativeOffice
