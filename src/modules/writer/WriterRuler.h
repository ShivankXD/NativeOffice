#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// WriterRuler.h  (Tier 4 — rulers with tab stops)
// A Word/WPS-style ruler. One class serves both orientations:
//   • Horizontal — cm scale, page-margin zones, the four draggable paragraph
//     indent markers (first-line, hanging, left, right), and tab stops you can
//     click to add, drag to move, drag off to remove. A corner box cycles the
//     tab type (Left / Center / Right / Decimal); double-click opens a Tabs
//     dialog. Tab stops are written to the paragraph's QTextBlockFormat so the
//     layout actually honours the Tab key.
//   • Vertical — cm scale with the top/bottom page-margin zones; the boundaries
//     drag to change the (uniform) page margin.
//
// The owner (WriterModule) keeps the ruler aligned with the page by feeding it
// the page origin / length / margin (all in on-screen, already-zoomed px).
// ─────────────────────────────────────────────────────────────────────────────

#include <QWidget>
#include <QPointer>

class QTextEdit;

namespace NativeOffice {

class WriterRuler : public QWidget {
    Q_OBJECT

public:
    explicit WriterRuler(Qt::Orientation orient, QWidget* parent = nullptr);

    void setEditor(QTextEdit* ed);
    // Page placement inside this ruler, in on-screen px (origin can be negative
    // when scrolled). 'margin' is the uniform document margin at the current zoom.
    void setPageGeometry(int originPx, int pageLengthPx, int marginPx, double zoom);

public slots:
    void refreshFromCursor();   // re-read the current paragraph's indents / tabs

signals:
    // A page-margin boundary was dragged; px is the new uniform margin in base
    // (100%) px, ready for WriterModule::setPageMargin().
    void marginChangeRequested(double basePx);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;

private:
    enum class Drag { None, FirstLine, Hanging, LeftIndent, RightIndent,
                      Tab, LeftMargin, RightMargin, TopMargin, BottomMargin };

    // Geometry helpers (horizontal). value = layout px measured from text-left.
    int   textLeftPx()  const { return m_origin + m_margin; }
    int   textRightPx() const { return m_origin + m_pageLen - m_margin; }
    int   valueToPx(double v) const { return textLeftPx() + int(v); }
    double pxToValue(int x)   const { return x - textLeftPx(); }

    void  applyIndents(double leftMargin, double textIndent, double rightMargin);
    void  applyTabs();
    int   hitTab(const QPoint& p) const;     // index in m_tabs, or -1
    void  openTabsDialog();
    void  cycleTabType();

    Qt::Orientation m_orient;
    QPointer<QTextEdit> m_editor;

    int    m_origin   { 0 };     // page origin (x for H, y for V), screen px
    int    m_pageLen  { 794 };   // page width/height, screen px (zoomed)
    int    m_margin   { 60 };    // document margin, screen px (zoomed)
    double m_zoom     { 1.0 };

    // Current paragraph indents (layout px = current-zoom px).
    double m_leftMargin  { 0 };
    double m_textIndent  { 0 };
    double m_rightMargin { 0 };

    // Tab stops of the current paragraph: position (layout px from text-left) + type.
    struct Tab { double pos; int type; };    // type: 0 L, 1 C, 2 R, 3 Decimal
    QList<Tab> m_tabs;
    int        m_newTabType { 0 };           // type applied to newly added tabs

    Drag   m_drag      { Drag::None };
    int    m_dragTab   { -1 };
    bool   m_dragValid { false };            // becomes false → tab removed on release
};

} // namespace NativeOffice
