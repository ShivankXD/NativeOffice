#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfPanels.h — the chrome around the Viewer canvas:
//   • Sidebar    — WPS-style icon strip + collapsible panel with Bookmarks
//                  (outline tree), Thumbnails (lazily rendered), Comments
//                  (annotation list; populated by the Comment feature).
//   • StatusBar  — page navigation, zoom controls, fit buttons.
// Both follow ThemeManager chrome colors and re-skin on mode change.
// ─────────────────────────────────────────────────────────────────────────────

#include "PdfRenderer.h"

#include <QWidget>
#include <vector>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QSlider;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace NativeOffice::Pdf {

class EditSession;

// ── Sidebar ─────────────────────────────────────────────────────────────────
class Sidebar : public QWidget {
    Q_OBJECT

public:
    enum class Panel { Bookmarks = 0, Thumbnails = 1, Comments = 2 };

    explicit Sidebar(EditSession* session, QWidget* parent = nullptr);

    // The comments panel is owned here but filled by the annotation feature.
    [[nodiscard]] QListWidget* commentsList() const { return m_comments; }
    void setCurrentPage(int pageIndex);          // sync thumbnail selection

signals:
    void pageActivated(int pageIndex);           // thumbnail/bookmark clicked

public slots:
    void refresh();                              // rebuild bookmarks + thumbnails

private:
    void applyTheme();
    void togglePanel(int panelId);
    void renderNextThumb();                      // chunked lazy thumbnail loop
    void addOutlineNodes(QTreeWidgetItem* parent, const std::vector<OutlineNode>& nodes);

    EditSession*    m_session;
    QWidget*        m_iconStrip  { nullptr };
    QStackedWidget* m_panelStack { nullptr };
    QToolButton*    m_btns[3]    { nullptr, nullptr, nullptr };

    QTreeWidget* m_bookmarks { nullptr };
    QListWidget* m_thumbs    { nullptr };
    QListWidget* m_comments  { nullptr };

    int  m_nextThumb = 0;                        // next page to rasterize
    bool m_panelOpen = false;
};

// ── Status bar ──────────────────────────────────────────────────────────────
class StatusBar : public QWidget {
    Q_OBJECT

public:
    explicit StatusBar(QWidget* parent = nullptr);

    void setPageInfo(int current, int total);    // 0-based current
    void setZoomPercent(int pct);

signals:
    void pageJumpRequested(int pageIndex);       // 0-based
    void prevPageRequested();
    void nextPageRequested();
    void zoomChanged(int pct);
    void zoomInRequested();
    void zoomOutRequested();
    void fitWidthRequested();
    void fitPageRequested();

private:
    void applyTheme();

    QToolButton* m_prev     { nullptr };
    QToolButton* m_next     { nullptr };
    QLineEdit*   m_pageEdit { nullptr };
    QLabel*      m_pageTotal{ nullptr };
    QToolButton* m_zoomOut  { nullptr };
    QToolButton* m_zoomIn   { nullptr };
    QSlider*     m_slider   { nullptr };
    QLabel*      m_zoomLabel{ nullptr };
    QToolButton* m_fitW     { nullptr };
    QToolButton* m_fitP     { nullptr };
    int m_total = 0;
    bool m_syncing = false;
};

} // namespace NativeOffice::Pdf
