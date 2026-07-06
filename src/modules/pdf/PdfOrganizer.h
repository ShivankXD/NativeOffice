#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfOrganizer.h — the Page tab's central view: a grid of page thumbnails
// with drag-to-reorder, multi-select (Ctrl/Shift), a context menu, and
// per-page rotate/delete. Mirrors WPS's "Drag and drop to reorder pages"
// screen. Purely a view: every edit is emitted as a request and executed by
// PdfModule through the EditSession.
// ─────────────────────────────────────────────────────────────────────────────

#include <QListWidget>
#include <vector>

class QLabel;
class QShowEvent;

namespace NativeOffice {

namespace Pdf { class EditSession; }

class PageOrganizer : public QWidget {
    Q_OBJECT

public:
    explicit PageOrganizer(Pdf::EditSession* session, QWidget* parent = nullptr);

    // Pages currently selected in the grid (0-based, sorted). Empty if none.
    [[nodiscard]] std::vector<int> selectedPages() const;

signals:
    void reorderRequested(const std::vector<int>& newOrder);
    void rotateRequested(const std::vector<int>& pages, int degreesCW);
    void deleteRequested(const std::vector<int>& pages);
    void extractRequested(const std::vector<int>& pages);
    void insertBlankAfterRequested(int pageIndex);
    void pageActivated(int pageIndex);       // double-click → jump to viewer

public slots:
    void refresh();

protected:
    void showEvent(QShowEvent* ev) override;

private:
    void applyTheme();
    void renderNextThumb();

    Pdf::EditSession* m_session;
    QLabel*      m_hint { nullptr };
    QListWidget* m_grid { nullptr };
    int m_nextThumb = 0;
    bool m_applyingModel = false;             // guard while rebuilding items
};

} // namespace NativeOffice
