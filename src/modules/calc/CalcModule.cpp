// ─────────────────────────────────────────────────────────────────────────────
// CalcModule.cpp  (Sprint 8)
// Full spreadsheet UI: formula bar + themed QTableView + SpreadsheetModel.
// Sprint 8: JSON-based file persistence (.noff) with CSV fallback.
// ─────────────────────────────────────────────────────────────────────────────
#include "CalcModule.h"

#include <QTemporaryFile>
#include "SpreadsheetModel.h"
#include "CalcHeaderView.h"
#include "CalcIcons.h"
#include "ChartObject.h"
#include "ShapeObject.h"
#include "FormulaEngine.h"
#include "Cell.h"
#include "XlsxIo.h"
#include "XlsxDrawingWriter.h"
#include "StructuredData.h"
#include "StructuredDataDialog.h"
#include "DataCleanser.h"
#include "DataCleanserPanel.h"
#include "SheetSql.h"
#include "SheetSqlPanel.h"
#include "CalcHistoryPanel.h"
#include "core/auth/AuthManager.h"
#include "core/history/DocHistory.h"
#include "core/theme/ThemeManager.h"
#include "core/common/BrandBar.h"
#include "core/watermark/WatermarkPdf.h"
#include "core/settings/ExportPrefs.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableView>
#include <QLineEdit>
#include <QLabel>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QItemSelection>
#include <QAbstractItemView>
#include <QKeyEvent>
#include <QBuffer>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QFont>
#include <QSizePolicy>
#include <QFrame>
// Sprint 10: undo/redo + clipboard
#include <QAction>
#include <QUndoStack>
#include <QUndoGroup>
#include <QInputDialog>
#include <QCursor>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QPainter>
#include <QTimer>
#include <QScopeGuard>
#include <QScrollBar>
#include <QEvent>
#include <QKeySequence>
#include <QSet>
// Sprint 11: formatting toolbar
#include <QToolButton>
#include <QToolTip>
#include <QPalette>
#include <QPolygon>
#include <QPixmap>
#include <QImage>
#include <QProcess>
#include <QComboBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QStackedWidget>
#include <QScrollArea>
#include <QColorDialog>
#include <QFileDialog>
#include <QPdfWriter>
#include <QPageSize>
#include <QPageLayout>
#include <QMarginsF>
#include <QPrinter>
#include <QPrintPreviewDialog>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QFontMetrics>
#include <QTextOption>
#include <QMenu>
#include <QLocale>
// Markdown / Pandas / Conditional formatting features
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QRadioButton>
#include <QFormLayout>
#include <QButtonGroup>
#include <QPropertyAnimation>
// Sprint 13: find/replace
#include <QDialog>
#include <QCheckBox>
#include <QPushButton>
#include <QListWidget>
#include <QMessageBox>
#include <QApplication>
#include <QStringList>
#include <QPoint>
#include <QSize>
#include <vector>
#include <utility>
#include <climits>
#include <algorithm>
#include <functional>
#include <memory>
#include <QGridLayout>
#include <QStringList>
// Sprint 8: file persistence
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace NativeOffice {

// ─────────────────────────────────────────────────────────────────────────────
// MarchingAntsOverlay — transparent child of the table viewport that draws an
// animated dashed rectangle around the copied/cut range. Repaints on a timer
// (animating the dash phase) and recomputes its rect from visualRect() every
// paint, so it tracks scrolling for free.
// ─────────────────────────────────────────────────────────────────────────────
class MarchingAntsOverlay : public QWidget {
public:
    explicit MarchingAntsOverlay(QTableView* view)
        : QWidget(view->viewport()), m_view(view)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        hide();
        m_timer = new QTimer(this);
        m_timer->setInterval(120);
        connect(m_timer, &QTimer::timeout, this, [this]{ m_phase = (m_phase + 1) % 16; update(); });
    }

    void setRange(const QRect& cells, bool cut) {
        m_cells = cells;
        m_cut   = cut;
        if (cells.isNull()) {
            hide();
            m_timer->stop();
        } else {
            resize(parentWidget()->size());
            show();
            raise();
            m_timer->start();
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (m_cells.isNull()) return;
        const QModelIndex tl = m_view->model()->index(m_cells.top(),    m_cells.left());
        const QModelIndex br = m_view->model()->index(m_cells.bottom(), m_cells.right());
        QRect r = m_view->visualRect(tl).united(m_view->visualRect(br));
        if (!r.isValid()) return;
        r.adjust(0, 0, -1, -1);

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        QPen pen(m_cut ? QColor("#E8372A") : QColor("#1C7C3F"));
        pen.setWidth(2);
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern({4, 4});
        pen.setDashOffset(m_phase);
        p.setPen(pen);
        p.drawRect(r);
    }

private:
    QTableView* m_view;
    QTimer*     m_timer { nullptr };
    QRect       m_cells;
    bool        m_cut   { false };
    int         m_phase { 0 };
};

// ─────────────────────────────────────────────────────────────────────────────
// SelectionOverlay — transparent child of the viewport that draws Excel's
// signature crisp green outline around the active selection range, plus the
// little fill-handle square at the bottom-right corner. Recomputed from the
// selection model every paint, so it tracks scrolling and resizing for free.
// ─────────────────────────────────────────────────────────────────────────────
class SelectionOverlay : public QWidget {
public:
    explicit SelectionOverlay(QTableView* view)
        : QWidget(view->viewport()), m_view(view)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        resize(view->viewport()->size());
    }

protected:
    void paintEvent(QPaintEvent*) override {
        auto* sm = m_view->selectionModel();
        if (!sm) return;

        // Bounding rectangle of the selection (fall back to the current cell).
        int c1 = INT_MAX, r1 = INT_MAX, c2 = -1, r2 = -1;
        const QModelIndexList sel = sm->selectedIndexes();
        if (sel.isEmpty()) {
            const QModelIndex cur = m_view->currentIndex();
            if (!cur.isValid()) return;
            c1 = c2 = cur.column();
            r1 = r2 = cur.row();
        } else {
            for (const QModelIndex& i : sel) {
                c1 = std::min(c1, i.column()); c2 = std::max(c2, i.column());
                r1 = std::min(r1, i.row());    r2 = std::max(r2, i.row());
            }
        }

        const QModelIndex tl = m_view->model()->index(r1, c1);
        const QModelIndex br = m_view->model()->index(r2, c2);
        QRect rect = m_view->visualRect(tl).united(m_view->visualRect(br));
        if (!rect.isValid()) return;
        rect.adjust(0, 0, -1, -1);

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);

        const QColor kAccent("#107C41");      // Excel green
        QPen pen(kAccent);
        pen.setWidth(2);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect);

        // Fill handle (small filled square at the bottom-right corner).
        const int hs = 6;
        QRect handle(rect.right() - hs/2, rect.bottom() - hs/2, hs, hs);
        p.fillRect(handle, kAccent);
        p.setPen(QPen(Qt::white, 1));
        p.drawRect(handle);
    }

private:
    QTableView* m_view;
};

// ─────────────────────────────────────────────────────────────────────────────
// FreezableTableView — a QTableView that reserves a strip of space along the
// top and/or left for the frozen row/column views to sit in.
//
// The frozen views used to be children drawn ON TOP of the grid, so the band
// always covered a band's worth of body content: freeze the top row, scroll
// down, and the row underneath the band was hidden rather than skipped.
// Reserving real space puts the band beside the body instead of over it.
//
// The reservation goes through the headers rather than through a direct
// setViewportMargins call. QTableView::updateGeometries() derives the margins
// from the header sizes and then sizes the scrollbars against the viewport it
// just produced, so setting the margins behind its back loses twice over: the
// next base pass takes them straight back, and the scroll range stays computed
// for a viewport a band too tall, which puts the last rows out of reach.
// Widening the header bounds for one extra base pass makes the base class do
// both jobs itself. The headers are then shrunk back to their natural size at
// the widget edge, leaving the band between them and the body.
//
// Reserving an axis needs that axis's header, so a band is only ever added
// next to a visible header. Calc shows both.
// ─────────────────────────────────────────────────────────────────────────────
class FreezableTableView : public QTableView {
public:
    using QTableView::QTableView;

    // width/height are the reserved strip in pixels; cols/rows are how many
    // leading columns and rows the band is showing.
    void setFrozenBand(int width, int height, int cols, int rows) {
        if (m_bandW == width && m_bandH == height
            && m_cols == cols && m_rows == rows) return;
        m_bandW = width;
        m_bandH = height;
        m_cols  = cols;
        m_rows  = rows;
        updateGeometries();
    }

    // True while the strip is being reserved, when the headers are stretched
    // across it and every size reads as natural + band. Anyone placing widgets
    // against this view's geometry has to sit that out and wait for
    // onBandSettled, or it lands them a whole band off.
    bool isReserving() const { return m_reserving; }

    // Run once the reserved strip has settled.
    std::function<void()> onBandSettled;

protected:
    void updateGeometries() override {
        // The guard comes first: the extra pass below re-enters here, and
        // letting the base class run again would reset the margins it set.
        if (m_reserving) return;
        QTableView::updateGeometries();
        if (m_bandW == 0 && m_bandH == 0) return;      // nothing frozen

        m_reserving = true;

        QHeaderView* vh = verticalHeader();
        QHeaderView* hh = horizontalHeader();
        const int vw = vh->isHidden() ? 0 : vh->width();
        const int hy = hh->isHidden() ? 0 : hh->height();

        // Both bounds move, not just the minimum: the row header is given a
        // fixed width, which pins the maximum too, and the base class clamps
        // the margin to it.
        const int minW = vh->minimumWidth(),  maxW = vh->maximumWidth();
        const int minH = hh->minimumHeight(), maxH = hh->maximumHeight();
        if (vw) { vh->setMinimumWidth (vw + m_bandW); vh->setMaximumWidth (vw + m_bandW); }
        if (hy) { hh->setMinimumHeight(hy + m_bandH); hh->setMaximumHeight(hy + m_bandH); }
        QTableView::updateGeometries();
        if (vw) { vh->setMinimumWidth (minW); vh->setMaximumWidth (maxW); }
        if (hy) { hh->setMinimumHeight(minH); hh->setMaximumHeight(maxH); }

        // That pass stretched the headers across the whole reserved strip. Put
        // them back at the widget edge so the band sits on their inner side.
        const QRect vg = viewport()->geometry();
        if (vw) vh->setGeometry(vg.left() - m_bandW - vw, vg.top(), vw, vg.height());
        if (hy) hh->setGeometry(vg.left(), vg.top() - m_bandH - hy, vg.width(), hy);

        // The band is already showing the frozen rows and columns, so take them
        // out of the body's range. Both scroll modes are per-item here, so the
        // value is the first visible row/column. Without this the body starts
        // back at row 1 at the top of the scroll and the frozen row is drawn
        // twice. The base pass above resets the range, hence doing it after.
        verticalScrollBar()->setMinimum(m_rows);
        horizontalScrollBar()->setMinimum(m_cols);

        m_reserving = false;
        if (onBandSettled) onBandSettled();
    }

private:
    int  m_bandW     { 0 };
    int  m_bandH     { 0 };
    int  m_cols      { 0 };
    int  m_rows      { 0 };
    bool m_reserving { false };
};

// ─────────────────────────────────────────────────────────────────────────────
// FloatingItem — a draggable/closable frame over the grid that holds arbitrary
// content (an image label or a text box). Used by Insert ▸ Pictures / Text Box.
// ─────────────────────────────────────────────────────────────────────────────
class FloatingItem : public QFrame {
public:
    std::function<void()> onMoved;              // fired after a drag / resize finishes
    std::function<void(QWidget*)> onSelected;   // fired when clicked (module deselects others)

    void setSelected(bool on) {
        // Transparent either way: a white card behind a picture turned every
        // image with an alpha channel into a floating white box.
        setStyleSheet(on
            ? QStringLiteral("QFrame#floatItem{background:transparent;border:1px solid #1A73E8;}")
            : QStringLiteral("QFrame#floatItem{background:transparent;border:none;}"));
        if (m_moveGrip) m_moveGrip->setVisible(on);
        if (m_grip)     m_grip->setVisible(on);
    }

    FloatingItem(QWidget* content, const QRect& geom, QWidget* parent)
        : QFrame(parent)
    {
        setObjectName("floatItem");
        setAutoFillBackground(true);
        setAttribute(Qt::WA_NoMousePropagation);   // swallow clicks, no fall-through
        setCursor(Qt::SizeAllCursor);              // drag the body to move
        setStyleSheet("QFrame#floatItem{background:transparent;border:none;}");
        auto* v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);

        if (content) { content->setParent(this); v->addWidget(content); }

        // No close button, for the same reason the chart has none: it made the
        // object look like a window. Delete removes the selected object.

        // Top-left move handle (lets even an editable text box be dragged).
        m_moveGrip = new QWidget(this);
        m_moveGrip->setFixedSize(14, 14);
        m_moveGrip->setCursor(Qt::SizeAllCursor);
        m_moveGrip->setStyleSheet("background:rgba(182,187,194,160);border-radius:2px;");

        m_grip = new QWidget(this);
        m_grip->setFixedSize(14, 14);
        m_grip->setCursor(Qt::SizeFDiagCursor);
        m_grip->setStyleSheet("background:transparent;");

        setMinimumSize(60, 44);
        setGeometry(geom);
        m_moveGrip->installEventFilter(this);
        m_grip->installEventFilter(this);
        setSelected(false);        // bare object until clicked (Excel/WPS style)
    }
protected:
    void resizeEvent(QResizeEvent*) override {
        if (m_moveGrip) m_moveGrip->move(2, 2);
        if (m_grip)     { m_grip->move(width() - 14, height() - 14); m_grip->raise(); }
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            setSelected(true); if (onSelected) onSelected(this);
            m_drag = true; m_press = e->globalPosition().toPoint(); m_start = geometry(); raise();
        }
        e->accept();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (m_drag) { moveBy(e->globalPosition().toPoint()); }
        e->accept();
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (m_drag) { m_drag = false; if (onMoved) onMoved(); }
        e->accept();
    }
    void mouseDoubleClickEvent(QMouseEvent* e) override { e->accept(); }
    bool eventFilter(QObject* w, QEvent* e) override {
        if (w == m_moveGrip) {
            if (e->type() == QEvent::MouseButtonPress) {
                setSelected(true); if (onSelected) onSelected(this);
                m_drag = true; m_press = static_cast<QMouseEvent*>(e)->globalPosition().toPoint();
                m_start = geometry(); raise(); return true;
            } else if (e->type() == QEvent::MouseMove && m_drag) {
                moveBy(static_cast<QMouseEvent*>(e)->globalPosition().toPoint()); return true;
            } else if (e->type() == QEvent::MouseButtonRelease && m_drag) {
                m_drag = false; if (onMoved) onMoved(); return true;
            }
        }
        if (w == m_grip) {
            if (e->type() == QEvent::MouseButtonPress) {
                m_resize = true; m_press = static_cast<QMouseEvent*>(e)->globalPosition().toPoint();
                m_start = geometry(); return true;
            } else if (e->type() == QEvent::MouseMove && m_resize) {
                const QPoint d = static_cast<QMouseEvent*>(e)->globalPosition().toPoint() - m_press;
                resize(std::max(60, m_start.width() + d.x()), std::max(44, m_start.height() + d.y()));
                return true;
            } else if (e->type() == QEvent::MouseButtonRelease && m_resize) {
                m_resize = false; if (onMoved) onMoved(); return true;
            }
        }
        return QFrame::eventFilter(w, e);
    }
private:
    void moveBy(const QPoint& globalPos) {
        QRect g = m_start; g.moveTopLeft(m_start.topLeft() + (globalPos - m_press));
        if (parentWidget()) {
            const QRect pr = parentWidget()->rect();
            if (g.left() < 0) g.moveLeft(0);
            if (g.top()  < 0) g.moveTop(0);
            if (g.right()  > pr.right())  g.moveRight(pr.right());
            if (g.bottom() > pr.bottom()) g.moveBottom(pr.bottom());
        }
        setGeometry(g);
    }
    QWidget*     m_moveGrip { nullptr };
    QWidget*     m_grip     { nullptr };
    bool   m_drag   { false };
    bool   m_resize { false };
    QPoint m_press;
    QRect  m_start;
};

// ─────────────────────────────────────────────────────────────────────────────
// CalcItemDelegate — paints each cell from the model's format roles. A custom
// delegate is required because a QTableView::item stylesheet rule makes Qt's
// stylesheet style ignore the model's BackgroundRole; here we paint fill, text
// colour, font and alignment ourselves for full Excel fidelity. Gridlines are
// still drawn by the view (setShowGrid).
// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// Text overflow, the way a spreadsheet does it
//
// A label longer than its column spills across the empty cells beside it rather
// than being cut off. Every spreadsheet behaves this way, and without it a
// sheet title reads as "St" and a caption as "Exam ...", which is exactly what
// NativeOffice showed next to Excel on the same workbook.
//
// The spill cannot just be drawn wider by the cell that owns the text. Cells
// paint left to right and each fills its own background first, so anything the
// owner drew past its edge is erased by the neighbour a moment later. Instead
// EVERY cell asks whether a cell beside it owns text reaching this far and
// draws that text itself, clipped to its own rectangle. The pieces line up
// because each lays the text out in the same rectangle: the one spanning the
// whole spill.
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// How far the scan looks, both for an owner and for room to spill into. Bounded
// because this runs for every painted cell, and twenty-four columns is already
// wider than any label still meant to be read.
constexpr int kSpillScan = 24;

struct Spill {
    int     owner { -1 };   // column owning the text, -1 when nothing spills
    QRect   rect;           // rectangle the text is laid out in
    QString text;
    int     align { 0 };
    QFont   font;
};

// Left-ish alignment spills right, right-ish spills left, centred spills both
// ways, which is what a title centred over a block does.
bool leftish(int a)  { return !(a & (Qt::AlignRight | Qt::AlignHCenter)); }
bool rightish(int a) { return  (a & Qt::AlignRight)   != 0; }
bool centred(int a)  { return  (a & Qt::AlignHCenter) != 0; }

Spill spillFor(const QTableView* view, const SpreadsheetModel* model,
               int row, int col, const QFont& baseFont) {
    Spill none;
    if (!view || !model) return none;

    auto textAt  = [&](int c) { return model->index(row, c).data(Qt::DisplayRole).toString(); };
    auto empty   = [&](int c) { return textAt(c).isEmpty(); };
    auto alignAt = [&](int c) {
        const int a = model->index(row, c).data(Qt::TextAlignmentRole).toInt();
        return a ? a : int(Qt::AlignLeft | Qt::AlignVCenter);
    };

    // Which cell owns the text painted through this one?
    int owner = -1;
    if (!empty(col)) {
        owner = col;
    } else {
        // Nothing here, so look outward. The first non-empty cell in each
        // direction is the only candidate: anything past it is blocked.
        for (int c = col - 1; c >= 0 && col - c <= kSpillScan; --c) {
            if (empty(c)) continue;
            const int a = alignAt(c);
            if (leftish(a) || centred(a)) owner = c;
            break;
        }
        if (owner < 0)
            for (int c = col + 1; c - col <= kSpillScan; ++c) {
                if (empty(c)) continue;
                const int a = alignAt(c);
                if (rightish(a) || centred(a)) owner = c;
                break;
            }
    }
    if (owner < 0) return none;

    const Cell ocell = model->cellAt(owner, row);
    if (ocell.format.wrap) return none;              // wrapped text stays put
    if (view->columnSpan(row, owner) > 1) return none;  // a merge already has room

    Spill sp;
    sp.text  = textAt(owner);
    sp.align = alignAt(owner);
    sp.font  = baseFont;
    const QVariant fv = model->index(row, owner).data(Qt::FontRole);
    if (fv.isValid()) sp.font = fv.value<QFont>();

    const int indentPx = std::max(0, ocell.format.indent) * 12;
    const int needed   = QFontMetrics(sp.font).horizontalAdvance(sp.text) + 6 + indentPx;
    const int ownWidth = view->columnWidth(owner);
    if (needed <= ownWidth) return none;             // it fits, nothing spills

    int left  = view->columnViewportPosition(owner);
    int right = left + ownWidth;
    if (leftish(sp.align) || centred(sp.align))
        for (int c = owner + 1; c - owner <= kSpillScan && right - left < needed; ++c) {
            if (!empty(c)) break;
            right = view->columnViewportPosition(c) + view->columnWidth(c);
        }
    if (rightish(sp.align) || centred(sp.align))
        for (int c = owner - 1; c >= 0 && owner - c <= kSpillScan && right - left < needed; --c) {
            if (!empty(c)) break;
            left = view->columnViewportPosition(c);
        }

    // Never wider than the text needs, or a centred label drifts off its cell.
    if (right - left > needed) {
        if      (rightish(sp.align)) left  = right - needed;
        else if (!centred(sp.align)) right = left  + needed;
    }

    sp.owner = owner;
    sp.rect  = QRect(left, view->rowViewportPosition(row),
                     right - left, view->rowHeight(row));
    return sp;
}

} // namespace

class CalcItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override {
        p->save();

        const auto* model = qobject_cast<const SpreadsheetModel*>(idx.model());
        const CellFormat fmt = model ? model->cellAt(idx.column(), idx.row()).format
                                     : CellFormat{};

        // ── Background ───────────────────────────────────────────────────────
        // Always fill (white by default) so the view's own current-cell focus
        // artifact (a stray blue caret/line) never shows through plain cells.
        QColor bg = QColor("#FFFFFF");
        const QVariant bgv = idx.data(Qt::BackgroundRole);
        if (bgv.canConvert<QColor>() && bgv.value<QColor>().isValid())
            bg = bgv.value<QColor>();
        p->fillRect(opt.rect, bg);

        // Data bar: a proportional bar behind the value. Drawn after the fill
        // and before the text so the number stays readable on top of it.
        if (model) {
            const CondFormatRule::Result cf = model->evalCondFormat(idx.column(), idx.row());
            if (cf.barColor.isValid() && cf.barFraction > 0.0) {
                QRect bar = opt.rect.adjusted(1, 2, -1, -2);
                bar.setWidth(int(bar.width() * cf.barFraction));
                p->fillRect(bar, cf.barColor);
            }
        }

        if (opt.state & QStyle::State_Selected)
            p->fillRect(opt.rect, QColor(16, 124, 65, 28));   // green selection tint

        // ── Borders ──────────────────────────────────────────────────────────
        {
            const int l = opt.rect.left();
            const int t = opt.rect.top();
            const int r = opt.rect.right();
            const int b = opt.rect.bottom();

            // Default grid, drawn on the cell's own right and bottom edges so
            // neighbouring cells share the line rather than leaving a seam.
            // A sheet can switch the grid off (showGridLines="0"), which
            // dashboard-style workbooks nearly always do; drawing it anyway put
            // a mesh over artwork that is meant to read as a solid panel.
            const auto* sm = qobject_cast<const SpreadsheetModel*>(idx.model());
            if (!sm || sm->showGridLines()) {
                p->setPen(QColor("#E2E2E2"));
                p->drawLine(r, t, r, b);
                p->drawLine(l, b, r, b);
            }

            // User borders paint over the grid on the same coordinates, so an
            // all-borders block reads as one table instead of loose boxes.
            if (fmt.borderEdges) {
                QPen bp(fmt.borderColor.isValid() ? fmt.borderColor : QColor("#000000"));
                bp.setWidth(1);
                p->setPen(bp);
                if (fmt.borderEdges & CellFormat::BTop)    p->drawLine(l, t, r, t);
                if (fmt.borderEdges & CellFormat::BBottom) p->drawLine(l, b, r, b);
                if (fmt.borderEdges & CellFormat::BLeft)   p->drawLine(l, t, l, b);
                if (fmt.borderEdges & CellFormat::BRight)  p->drawLine(r, t, r, b);
            }
        }

        // ── Text ─────────────────────────────────────────────────────────────
        const QString text = idx.data(Qt::DisplayRole).toString();
        if (!text.isEmpty()) {
            QFont f = opt.font;
            const QVariant fv = idx.data(Qt::FontRole);
            if (fv.isValid()) f = fv.value<QFont>();
            p->setFont(f);

            const QVariant fgv = idx.data(Qt::ForegroundRole);
            const QColor fg = fgv.canConvert<QColor>() ? fgv.value<QColor>() : QColor();
            p->setPen(fg.isValid() ? fg : QColor("#1C1E26"));

            int align = idx.data(Qt::TextAlignmentRole).toInt();
            if (align == 0) align = int(Qt::AlignLeft | Qt::AlignVCenter);

            const int indentPx = std::max(0, fmt.indent) * 12;
            const QRect r = opt.rect.adjusted(3 + indentPx, 0, -3, 0);
            if (fmt.wrap) {
                QTextOption to;
                to.setAlignment(Qt::Alignment(align));
                to.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
                p->drawText(QRectF(r), text, to);
            } else {
                // Spills into the empty cells beside it when it does not fit,
                // and is cut off only when there is nowhere left to go.
                const Spill sp = spillFor(qobject_cast<const QTableView*>(opt.widget),
                                          model, idx.row(), idx.column(), f);
                if (sp.owner == idx.column()) {
                    p->setClipRect(opt.rect);
                    p->drawText(sp.rect.adjusted(3 + indentPx, 0, -3, 0), sp.align, sp.text);
                    p->setClipping(false);
                } else {
                    const QString shown =
                        QFontMetrics(f).elidedText(text, Qt::ElideRight, r.width());
                    p->drawText(r, align, shown);
                }
            }
        }

        // A cell with nothing in it still has to draw the part of a neighbour's
        // label that reaches this far; see the note above spillFor().
        if (text.isEmpty() && !fmt.wrap && model) {
            const Spill sp = spillFor(qobject_cast<const QTableView*>(opt.widget),
                                      model, idx.row(), idx.column(), opt.font);
            if (sp.owner >= 0 && sp.owner != idx.column()) {
                const Cell oc = model->cellAt(sp.owner, idx.row());
                const QVariant fgv =
                    model->index(idx.row(), sp.owner).data(Qt::ForegroundRole);
                const QColor fg = fgv.canConvert<QColor>() ? fgv.value<QColor>() : QColor();
                p->setClipRect(opt.rect);
                p->setFont(sp.font);
                p->setPen(fg.isValid() ? fg : QColor("#1C1E26"));
                p->drawText(sp.rect.adjusted(3 + std::max(0, oc.format.indent) * 12, 0, -3, 0),
                            sp.align, sp.text);
                p->setClipping(false);
            }
        }

        // ── Comment marker (small red triangle, top-right) ─────────────────────
        if (model && model->showCommentMarkers()
            && model->hasComment(idx.column(), idx.row())) {
            const QRect rr = opt.rect;
            QPolygon tri;
            tri << QPoint(rr.right() - 6, rr.top() + 1)
                << QPoint(rr.right() - 1, rr.top() + 1)
                << QPoint(rr.right() - 1, rr.top() + 6);
            p->setPen(Qt::NoPen);
            p->setBrush(QColor("#E8372A"));
            p->drawPolygon(tri);
        }

        p->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        const QSize base = QStyledItemDelegate::sizeHint(opt, idx);
        const auto* model = qobject_cast<const SpreadsheetModel*>(idx.model());
        if (!model) return base;
        const Cell c = model->cellAt(idx.column(), idx.row());
        const QString text = idx.data(Qt::DisplayRole).toString();
        if (!c.format.wrap || text.isEmpty()) return base;

        int w = base.width();
        if (const auto* view = qobject_cast<const QTableView*>(parent()))
            w = view->columnWidth(idx.column());
        QFont f = opt.font;
        const QVariant fv = idx.data(Qt::FontRole);
        if (fv.isValid()) f = fv.value<QFont>();
        const QRect br = QFontMetrics(f).boundingRect(
            QRect(0, 0, std::max(8, w - 8), 100000), Qt::TextWordWrap, text);
        return QSize(w, std::max(base.height(), br.height() + 4));
    }

    // Excel-style in-cell editor: clean white field with a green outline and a
    // dark text caret (the default editor showed a stray blue cursor line).
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& opt,
                          const QModelIndex& idx) const override {
        // Drop-down validation: if the column has an allowed-value list, edit
        // with a combo box instead of a free text field.
        if (const auto* model = qobject_cast<const SpreadsheetModel*>(idx.model())) {
            const QStringList items = model->validationList(idx.column());
            if (!items.isEmpty()) {
                auto* cb = new QComboBox(parent);
                cb->addItems(items);
                cb->setEditable(false);
                return cb;
            }
        }
        QWidget* ed = QStyledItemDelegate::createEditor(parent, opt, idx);
        if (auto* le = qobject_cast<QLineEdit*>(ed)) {
            le->setFrame(false);
            le->setStyleSheet(
                "QLineEdit{border:1px solid #107C41;background:#FFFFFF;"
                "color:#1C1E26;padding:0 2px;margin:0;}");
            // Caret colour follows QPalette::Text — set AFTER the stylesheet so it
            // isn't reset back to the system accent (the stray blue caret).
            QPalette pal = le->palette();
            pal.setColor(QPalette::Text,            QColor("#1C1E26"));
            pal.setColor(QPalette::Base,            QColor("#FFFFFF"));
            pal.setColor(QPalette::Highlight,       QColor("#107C41"));
            pal.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
            le->setPalette(pal);
        }
        return ed;
    }
};

// Internal MIME type carrying serialized cells (content + format).
static const char* kCellsMime = "application/x-nativeoffice-cells";

// ── CellFormat ⇄ JSON ─────────────────────────────────────────────────────────
static QJsonObject formatToJson(const CellFormat& f) {
    QJsonObject o;
    if (!f.fontFamily.isEmpty())   o["ff"] = f.fontFamily;
    if (f.fontSize > 0)            o["fs"] = f.fontSize;
    if (f.bold)                    o["b"]  = true;
    if (f.italic)                  o["i"]  = true;
    if (f.underline)               o["u"]  = true;
    if (f.strike)                  o["st"] = true;
    if (f.textColor.isValid())     o["fg"] = f.textColor.name(QColor::HexArgb);
    if (f.bgColor.isValid())       o["bg"] = f.bgColor.name(QColor::HexArgb);
    if (f.hAlign)                  o["ha"] = f.hAlign;
    if (f.vAlign)                  o["va"] = f.vAlign;
    if (f.wrap)                    o["w"]  = true;
    if (f.indent)                  o["in"] = f.indent;
    if (!f.numberFormat.isEmpty()) o["nf"] = f.numberFormat;
    if (f.borderEdges)             o["bd"] = f.borderEdges;
    if (f.borderColor.isValid())   o["bc"] = f.borderColor.name(QColor::HexArgb);
    return o;
}

static CellFormat jsonToFormat(const QJsonObject& o) {
    CellFormat f;
    f.fontFamily   = o.value("ff").toString();
    f.fontSize     = o.value("fs").toInt(0);
    f.bold         = o.value("b").toBool(false);
    f.italic       = o.value("i").toBool(false);
    f.underline    = o.value("u").toBool(false);
    f.strike       = o.value("st").toBool(false);
    if (o.contains("fg")) f.textColor = QColor(o.value("fg").toString());
    if (o.contains("bg")) f.bgColor   = QColor(o.value("bg").toString());
    f.hAlign       = o.value("ha").toInt(0);
    f.vAlign       = o.value("va").toInt(0);
    f.wrap         = o.value("w").toBool(false);
    f.indent       = o.value("in").toInt(0);
    f.numberFormat = o.value("nf").toString();
    f.borderEdges  = o.value("bd").toInt(0);
    if (o.contains("bc")) f.borderColor = QColor(o.value("bc").toString());
    return f;
}

// ── Merge range ⇄ "A1:B2" ─────────────────────────────────────────────────────
static QString mergeToRef(const QRect& r) {
    return FormulaEngine::cellAddress(r.left(),  r.top()) + ":"
         + FormulaEngine::cellAddress(r.right(), r.bottom());
}
static QRect refToMerge(const QString& ref) {
    const int colon = ref.indexOf(':');
    if (colon < 0) return {};
    int c1 = 0, r1 = 0, c2 = 0, r2 = 0;
    if (!FormulaEngine::parseCellRef(ref.left(colon).trimmed(), c1, r1)) return {};
    if (!FormulaEngine::parseCellRef(ref.mid(colon + 1).trimmed(), c2, r2)) return {};
    return QRect(QPoint(qMin(c1, c2), qMin(r1, r2)),
                 QPoint(qMax(c1, c2), qMax(r1, r2)));
}

// ═════════════════════════════════════════════════════════════════════════════
// Data-export features (Markdown / Pandas) — file-local helpers & widgets
// ═════════════════════════════════════════════════════════════════════════════

// Parse "A1" or "A1:C3" into a normalized QRect (left,top .. right,bottom).
static QRect parseRangeRef(const QString& ref) {
    const QString s = ref.trimmed();
    if (s.isEmpty()) return {};
    if (!s.contains(':')) {
        int c = 0, r = 0;
        if (!FormulaEngine::parseCellRef(s, c, r)) return {};
        return QRect(c, r, 1, 1);
    }
    return refToMerge(s);   // handles the "A1:C3" form
}

// Build a GitHub-flavored Markdown table from the model's range. The first row
// of the range is the header; the separator row matches each column's width
// (minimum 3 dashes). Pipes inside cells are escaped; newlines flattened.
static QString rangeToMarkdown(const SpreadsheetModel* m, const QRect& r) {
    const int rows = r.height(), cols = r.width();
    QVector<QVector<QString>> grid(rows, QVector<QString>(cols));
    QVector<int> widths(cols, 3);
    for (int rr = 0; rr < rows; ++rr)
        for (int cc = 0; cc < cols; ++cc) {
            QString v = m->displayValue(r.left() + cc, r.top() + rr);
            v.replace('\\', "\\\\").replace('|', "\\|");
            v.replace('\n', ' ').replace('\r', ' ');
            grid[rr][cc] = v;
            widths[cc] = std::max(widths[cc], int(v.size()));
        }

    auto rowLine = [&](const QVector<QString>& cells) {
        QString line = "|";
        for (int cc = 0; cc < cols; ++cc)
            line += " " + cells[cc].leftJustified(widths[cc], ' ') + " |";
        return line;
    };

    QStringList lines;
    lines << rowLine(grid[0]);                       // header row
    QString sep = "|";
    for (int cc = 0; cc < cols; ++cc)
        sep += " " + QString(widths[cc], '-') + " |";
    lines << sep;                                    // separator row
    for (int rr = 1; rr < rows; ++rr)
        lines << rowLine(grid[rr]);
    return lines.join('\n');
}

// Python string literal: wrap in double quotes, escaping backslash & quote.
static QString pyStr(const QString& s) {
    QString t = s;
    t.replace('\\', "\\\\").replace('"', "\\\"").replace('\n', "\\n").replace('\r', "");
    return "\"" + t + "\"";
}

// A single CSV field: quote when it contains a comma, quote, or newline.
static QString csvField(const QString& s) {
    if (s.contains(',') || s.contains('"') || s.contains('\n') || s.contains('\r')) {
        QString t = s; t.replace('"', "\"\"");
        return "\"" + t + "\"";
    }
    return s;
}

// Build pandas code from the model's range. The first row supplies column names.
// csvMode=false → inline pd.DataFrame({...}); csvMode=true → CSV + read_csv.
// Columns whose body values are all numeric become number lists; others become
// string lists. Empty cells → None (inline) / "" (CSV).
static QString rangeToPandas(const SpreadsheetModel* m, const QRect& r, bool csvMode) {
    const int rows = r.height(), cols = r.width();
    auto val = [&](int cc, int rr) { return m->displayValue(r.left() + cc, r.top() + rr); };

    QStringList headers;
    for (int cc = 0; cc < cols; ++cc) {
        QString h = val(cc, 0);
        if (h.trimmed().isEmpty()) h = QString("Column%1").arg(cc + 1);
        headers << h;
    }

    if (csvMode) {
        QStringList lines;
        QStringList hcells;
        for (int cc = 0; cc < cols; ++cc) hcells << csvField(headers[cc]);
        lines << hcells.join(',');
        for (int rr = 1; rr < rows; ++rr) {
            QStringList cells;
            for (int cc = 0; cc < cols; ++cc) cells << csvField(val(cc, rr));
            lines << cells.join(',');
        }
        QString out;
        out += "import pandas as pd\n";
        out += "import io\n\n";
        out += "csv_data = \"\"\"" + lines.join('\n') + "\"\"\"\n\n";
        out += "df = pd.read_csv(io.StringIO(csv_data))\n";
        return out;
    }

    // ── Inline DataFrame ──────────────────────────────────────────────────────
    QString out;
    out += "import pandas as pd\n\n";
    out += "df = pd.DataFrame({\n";
    for (int cc = 0; cc < cols; ++cc) {
        bool numeric = (rows > 1);
        for (int rr = 1; rr < rows; ++rr) {
            const QString v = val(cc, rr);
            if (v.isEmpty()) continue;
            bool ok = false; v.toDouble(&ok);
            if (!ok) { numeric = false; break; }
        }
        QStringList items;
        for (int rr = 1; rr < rows; ++rr) {
            const QString v = val(cc, rr);
            if (v.isEmpty())     items << "None";
            else if (numeric)    items << v;
            else                 items << pyStr(v);
        }
        QString line = "    " + pyStr(headers[cc]) + ": [" + items.join(", ") + "]";
        if (cc != cols - 1) line += ",";
        out += line + "\n";
    }
    out += "})\n";
    return out;
}

// Minimal Python syntax highlighter: keywords, strings, numbers, comments.
class PythonHighlighter : public QSyntaxHighlighter {
public:
    explicit PythonHighlighter(QTextDocument* doc) : QSyntaxHighlighter(doc) {
        m_kw.setForeground(QColor("#569CD6"));  m_kw.setFontWeight(QFont::Bold);
        m_str.setForeground(QColor("#CE9178"));
        m_num.setForeground(QColor("#B5CEA8"));
        m_com.setForeground(QColor("#6A9955")); m_com.setFontItalic(true);
        for (const QString& k : {QStringLiteral("import"), QStringLiteral("from"),
                                 QStringLiteral("as"), QStringLiteral("def"),
                                 QStringLiteral("return"), QStringLiteral("None"),
                                 QStringLiteral("True"), QStringLiteral("False")})
            m_keywords << QRegularExpression("\\b" + k + "\\b");
    }
protected:
    void highlightBlock(const QString& text) override {
        // numbers
        QRegularExpression numRe("\\b[0-9]+(\\.[0-9]+)?\\b");
        for (auto it = numRe.globalMatch(text); it.hasNext(); ) {
            const auto mm = it.next();
            setFormat(mm.capturedStart(), mm.capturedLength(), m_num);
        }
        // keywords
        for (const QRegularExpression& re : m_keywords)
            for (auto it = re.globalMatch(text); it.hasNext(); ) {
                const auto mm = it.next();
                setFormat(mm.capturedStart(), mm.capturedLength(), m_kw);
            }
        // strings (single line; override numbers/keywords within)
        QRegularExpression strRe("\"[^\"]*\"|'[^']*'");
        for (auto it = strRe.globalMatch(text); it.hasNext(); ) {
            const auto mm = it.next();
            setFormat(mm.capturedStart(), mm.capturedLength(), m_str);
        }
        // comment to end of line
        const int h = text.indexOf('#');
        if (h >= 0) setFormat(h, text.size() - h, m_com);
    }
private:
    QTextCharFormat m_kw, m_str, m_num, m_com;
    QVector<QRegularExpression> m_keywords;
};

// Non-blocking floating panel showing generated pandas code with a mode toggle,
// Expand, Copy and Close. Uses std::function callbacks (file-local; no moc).
class PandasCodeWidget : public QFrame {
public:
    std::function<void()> onModeChanged;
    std::function<void()> onCopy;
    std::function<void()> onClose;

    explicit PandasCodeWidget(QWidget* parent) : QFrame(parent) {
        setObjectName("pandasPanel");
        setAutoFillBackground(true);
        setAttribute(Qt::WA_NoMousePropagation);
        setStyleSheet("QFrame#pandasPanel{background:#1E1E2E;border:1px solid #3A3D4A;"
                      "border-radius:6px;}");
        setMinimumSize(300, 180);

        auto* v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);

        // ── Title bar (drag handle) ──────────────────────────────────────────
        m_bar = new QWidget(this);
        m_bar->setObjectName("pandasBar");
        m_bar->setCursor(Qt::SizeAllCursor);
        m_bar->setStyleSheet("QWidget#pandasBar{background:#2A2C3A;"
                             "border-top-left-radius:6px;border-top-right-radius:6px;}");
        auto* hb = new QHBoxLayout(m_bar);
        hb->setContentsMargins(10, 5, 5, 5);
        hb->setSpacing(6);
        auto* title = new QLabel("Pandas Export", m_bar);
        title->setStyleSheet("color:#E6E6EF;font-weight:bold;font-size:11px;background:transparent;");
        hb->addWidget(title);
        hb->addStretch(1);

        auto mkBtn = [this](const QString& text, const QString& tip) {
            auto* b = new QToolButton(m_bar);
            b->setText(text);
            b->setToolTip(tip);
            b->setCursor(Qt::ArrowCursor);
            b->setStyleSheet("QToolButton{border:none;color:#C9CBD6;background:transparent;"
                             "font-size:11px;padding:2px 6px;border-radius:3px;}"
                             "QToolButton:hover{background:#3D4051;color:#FFFFFF;}");
            return b;
        };
        m_expandBtn = mkBtn("Expand", "Expand / Restore the panel");
        m_copyBtn   = mkBtn("Copy",   "Copy code to clipboard");
        m_closeBtn  = mkBtn("\xE2\x9C\x95", "Close");
        hb->addWidget(m_expandBtn);
        hb->addWidget(m_copyBtn);
        hb->addWidget(m_closeBtn);
        v->addWidget(m_bar);

        // ── Mode toggle row ──────────────────────────────────────────────────
        auto* modeRow = new QWidget(this);
        modeRow->setStyleSheet("background:#23232F;");
        auto* mr = new QHBoxLayout(modeRow);
        mr->setContentsMargins(10, 4, 10, 4);
        mr->setSpacing(12);
        auto* lbl = new QLabel("Output:", modeRow);
        lbl->setStyleSheet("color:#9AA0AC;font-size:10px;background:transparent;");
        m_inlineBtn = new QRadioButton("Inline DataFrame", modeRow);
        m_csvBtn    = new QRadioButton("CSV + read_csv", modeRow);
        m_inlineBtn->setChecked(true);
        const QString rbStyle = "QRadioButton{color:#D7DAE3;font-size:10px;background:transparent;}";
        m_inlineBtn->setStyleSheet(rbStyle);
        m_csvBtn->setStyleSheet(rbStyle);
        mr->addWidget(lbl);
        mr->addWidget(m_inlineBtn);
        mr->addWidget(m_csvBtn);
        mr->addStretch(1);
        v->addWidget(modeRow);

        // ── Code view ────────────────────────────────────────────────────────
        m_edit = new QPlainTextEdit(this);
        m_edit->setReadOnly(true);
        m_edit->setLineWrapMode(QPlainTextEdit::NoWrap);
        QFont mono("Consolas");
        mono.setStyleHint(QFont::Monospace);
        mono.setPointSize(10);
        m_edit->setFont(mono);
        m_edit->setStyleSheet("QPlainTextEdit{background:#1E1E2E;color:#D4D4D4;border:none;"
                              "selection-background-color:#264F78;padding:4px;}");
        new PythonHighlighter(m_edit->document());
        v->addWidget(m_edit, 1);

        connect(m_inlineBtn, &QRadioButton::toggled, this, [this](bool){ if (onModeChanged) onModeChanged(); });
        connect(m_copyBtn,   &QToolButton::clicked,  this, [this]{ if (onCopy) onCopy(); });
        connect(m_closeBtn,  &QToolButton::clicked,  this, [this]{ if (onClose) onClose(); });
        connect(m_expandBtn, &QToolButton::clicked,  this, [this]{ toggleExpand(); });

        m_bar->installEventFilter(this);
    }

    void setCode(const QString& code) {
        if (m_edit->toPlainText() != code) m_edit->setPlainText(code);
    }
    [[nodiscard]] QString code() const { return m_edit->toPlainText(); }
    [[nodiscard]] bool csvMode() const { return m_csvBtn->isChecked(); }

protected:
    void mousePressEvent(QMouseEvent* e) override { e->accept(); }  // no fall-through
    void mouseDoubleClickEvent(QMouseEvent* e) override { e->accept(); }

    bool eventFilter(QObject* w, QEvent* e) override {
        if (w == m_bar) {
            if (e->type() == QEvent::MouseButtonPress) {
                m_drag = true;
                m_press = static_cast<QMouseEvent*>(e)->globalPosition().toPoint();
                m_start = geometry(); raise(); return true;
            } else if (e->type() == QEvent::MouseMove && m_drag) {
                const QPoint d = static_cast<QMouseEvent*>(e)->globalPosition().toPoint() - m_press;
                QRect g = m_start; g.moveTopLeft(m_start.topLeft() + d);
                if (parentWidget()) {
                    const QRect pr = parentWidget()->rect();
                    if (g.left() < 0) g.moveLeft(0);
                    if (g.top()  < 0) g.moveTop(0);
                    if (g.right()  > pr.right())  g.moveRight(pr.right());
                    if (g.bottom() > pr.bottom()) g.moveBottom(pr.bottom());
                }
                setGeometry(g); return true;
            } else if (e->type() == QEvent::MouseButtonRelease && m_drag) {
                m_drag = false; return true;
            }
        }
        return QFrame::eventFilter(w, e);
    }

private:
    void toggleExpand() {
        if (!parentWidget()) return;
        if (!m_expanded) {
            m_normalGeom = geometry();
            const QRect pr = parentWidget()->rect();
            setGeometry(pr.adjusted(24, 24, -24, -24));
            m_expandBtn->setText("Restore");
            m_expanded = true;
        } else {
            setGeometry(m_normalGeom);
            m_expandBtn->setText("Expand");
            m_expanded = false;
        }
        raise();
    }

    QWidget*        m_bar       { nullptr };
    QPlainTextEdit* m_edit      { nullptr };
    QRadioButton*   m_inlineBtn { nullptr };
    QRadioButton*   m_csvBtn    { nullptr };
    QToolButton*    m_expandBtn { nullptr };
    QToolButton*    m_copyBtn   { nullptr };
    QToolButton*    m_closeBtn  { nullptr };
    bool   m_drag     { false };
    bool   m_expanded { false };
    QPoint m_press;
    QRect  m_start;
    QRect  m_normalGeom;
};

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────
CalcModule::CalcModule(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    buildActions();
    applyStyles();
    connect(&ThemeManager::instance(), &ThemeManager::modeChanged,
            this, [this](ThemeMode) { applyStyles(); });
    setObjectName("calcModule");

    // Selection + marching-ants overlays, both tracking scroll & resize.
    m_selOverlay = new SelectionOverlay(m_tableView);
    m_selOverlay->show();
    m_ants = new MarchingAntsOverlay(m_tableView);
    m_ants->raise();                       // copy border sits above selection box
    m_tableView->viewport()->installEventFilter(this);

    auto repaintOverlays = [this]{ m_selOverlay->update(); m_ants->update(); updateFrozenViews(); repositionFloatingObjects(); };
    connect(m_tableView->horizontalScrollBar(), &QScrollBar::valueChanged, this, repaintOverlays);
    connect(m_tableView->verticalScrollBar(),   &QScrollBar::valueChanged, this, repaintOverlays);

    // A freshly-built, untouched spreadsheet must start CLEAN: any modification
    // signal fired while assembling the initial empty grid is not a real user
    // edit. Without this the tab shows a spurious "*" and closing an untouched
    // sheet wrongly prompts to save. A real edit afterwards re-dirties it.
    QTimer::singleShot(0, this, [this]{
        if (m_currentPath.isEmpty()) { m_dirty = false; emit documentModified(); }
        // The counter starts here, after the initial grid has settled, so
        // building an empty workbook is not mistaken for unsaved work.
        m_editSeq = 0;
        if (m_recovery) m_recovery->setSource([this]{ return m_editSeq; },
                                              [this]{ return buildNoffBytes(); });
    });

    // Crash recovery. Thirty seconds rather than Writer's twenty: a workbook
    // serializes to a great deal more than a page of text, and the snapshot is
    // skipped entirely when nothing has changed.
    m_recovery = new CrashRecovery(QStringLiteral("calc"), this);
    m_recovery->setSource([this]{ return m_editSeq; },
                          [this]{ return buildNoffBytes(); });
    m_recovery->start(30);
    // Offered once the window is up, so the prompt has something to sit on.
    QTimer::singleShot(600, this, [this]{ offerCrashRecovery(); });
}

// Offer back whatever a session that did not close normally left behind.
//
// The snapshot is loaded through loadFromPath() like any other .noff, from a
// temporary file, rather than through a second parser written for this: the
// loader is long and has been fixed a lot, and a private copy of it would be
// the one nobody remembers to fix.
void CalcModule::offerCrashRecovery() {
    if (!m_recovery) return;
    QByteArray bytes;
    if (!m_recovery->offerLeftover(bytes)) return;

    QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/noff-recover-XXXXXX.noff"));
    tmp.setAutoRemove(true);
    if (!tmp.open()) return;
    if (tmp.write(bytes) != bytes.size()) return;
    tmp.flush();
    const QString tmpPath = tmp.fileName();
    tmp.close();

    const QString bound = m_currentPath;    // recovery keeps the document's path
    if (!loadFromPath(tmpPath)) return;
    m_currentPath = bound;
    m_dirty       = true;                   // recovered work is still unsaved
    ++m_editSeq;
    m_recovery->setDocumentPath(bound);
    emit documentModified();
    emit filePathChanged(bound);
}

// ─────────────────────────────────────────────────────────────────────────────
// UI Construction
// ─────────────────────────────────────────────────────────────────────────────
void CalcModule::buildUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ── Formula bar row ───────────────────────────────────────────────────
    auto* fbarRow = new QWidget(this);
    fbarRow->setObjectName("formulaBarRow");
    fbarRow->setFixedHeight(36);

    auto* fbarLayout = new QHBoxLayout(fbarRow);
    fbarLayout->setContentsMargins(0, 0, 0, 0);
    fbarLayout->setSpacing(0);

    // Name box (shows current cell address, e.g. "A1")
    m_nameBox = new QLabel("A1", fbarRow);
    m_nameBox->setObjectName("nameBox");
    m_nameBox->setFixedWidth(64);
    m_nameBox->setAlignment(Qt::AlignCenter);

    // Vertical separator
    auto* sep = new QFrame(fbarRow);
    sep->setFrameShape(QFrame::VLine);
    sep->setObjectName("fbarSep");
    sep->setFixedWidth(1);

    // "fx" function icon label
    auto* fxLabel = new QLabel(" fx ", fbarRow);
    fxLabel->setObjectName("fxLabel");
    fxLabel->setFixedWidth(36);
    fxLabel->setAlignment(Qt::AlignCenter);

    // Vertical separator
    auto* sep2 = new QFrame(fbarRow);
    sep2->setFrameShape(QFrame::VLine);
    sep2->setObjectName("fbarSep");
    sep2->setFixedWidth(1);

    // The formula / content edit field
    m_formulaBar = new QLineEdit(fbarRow);
    m_formulaBar->setObjectName("formulaBar");
    m_formulaBar->setPlaceholderText("Type a value or formula starting with =");
    m_formulaBar->setClearButtonEnabled(true);

    fbarLayout->addWidget(m_nameBox);
    fbarLayout->addWidget(sep);
    fbarLayout->addWidget(fxLabel);
    fbarLayout->addWidget(sep2);
    fbarLayout->addWidget(m_formulaBar, 1);

    // ── Grid + first sheet ──────────────────────────────────────────────────
    m_undoGroup = new QUndoGroup(this);
    m_model = createSheet("Sheet1");
    m_activeSheet = 0;

    auto* grid = new FreezableTableView(this);
    // Place the frozen bands only once the reserved strip has settled. Anything
    // that asks for a refresh mid-reserve is turned away by updateFrozenViews
    // and answered here instead.
    grid->onBandSettled = [this]{ updateFrozenViews(); };
    m_tableView = grid;
    m_tableView->setObjectName("calcGrid");
    m_tableView->setModel(m_model);
    m_undoGroup->setActiveStack(m_model->undoStack());

    // Custom themed header views
    m_colHeader = new CalcHeaderView(Qt::Horizontal, m_tableView);
    m_rowHeader = new CalcHeaderView(Qt::Vertical,   m_tableView);

    m_tableView->setHorizontalHeader(m_colHeader);
    m_tableView->setVerticalHeader(m_rowHeader);

    // Custom delegate paints cell fills / fonts / colours from the model.
    m_tableView->setItemDelegate(new CalcItemDelegate(m_tableView));

    // Enter moves down, the way every spreadsheet behaves. Qt commits the
    // in-cell editor and leaves the current index exactly where it was, so
    // typing a column of numbers overwrote the same cell every time. The
    // stale formula bar was the same bug: it refreshes from onSelectionChanged,
    // which never fired because the selection never moved.
    // SubmitModelCache is the hint QStyledItemDelegate uses for Enter
    // specifically; focus-loss commits use NoHint, so this stays out of their way.
    connect(m_tableView->itemDelegate(), &QAbstractItemDelegate::closeEditor, this,
            [this](QWidget*, QAbstractItemDelegate::EndEditHint hint) {
        if (hint != QAbstractItemDelegate::SubmitModelCache) return;
        const QModelIndex cur = m_tableView->currentIndex();
        if (!cur.isValid()) return;
        const int nextRow = std::min(cur.row() + 1, SpreadsheetModel::NUM_ROWS - 1);
        const int col = cur.column();
        // Queued: the view is still finishing its own close-editor handling.
        // Only advance if nothing else moved the cursor in the meantime, so a
        // click landing right after the commit is not undone by this.
        QTimer::singleShot(0, this, [this, nextRow, col, cur] {
            if (m_tableView->currentIndex() != cur) return;
            m_tableView->setCurrentIndex(m_model->index(nextRow, col));
        });
    });

    // Belt and braces for the formula bar: if the active cell's content changes
    // without the selection moving, resync it rather than showing the old text.
    connect(m_model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& tl, const QModelIndex& br) {
        if (m_updatingFormulaBar) return;
        const QModelIndex cur = m_tableView->currentIndex();
        if (!cur.isValid()) return;
        if (cur.row() < tl.row() || cur.row() > br.row()
            || cur.column() < tl.column() || cur.column() > br.column()) return;
        m_updatingFormulaBar = true;
        m_formulaBar->setText(m_model->rawContent(cur.column(), cur.row()));
        m_updatingFormulaBar = false;
    });

    // Column widths: default 64px, row heights: default 20px (compact WPS look)
    m_colHeader->setDefaultSectionSize(64);
    m_rowHeader->setDefaultSectionSize(20);
    m_rowHeader->setFixedWidth(40);

    // Grid interaction
    m_tableView->setSelectionMode(QAbstractItemView::ContiguousSelection);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_tableView->setTabKeyNavigation(true);
    m_tableView->setEditTriggers(QAbstractItemView::DoubleClicked
                                 | QAbstractItemView::AnyKeyPressed
                                 | QAbstractItemView::EditKeyPressed);
    // The delegate paints the grid, not the view. With the view drawing it,
    // every item rect was inset by the grid pixel and Qt clips each cell to its
    // own rect, so a cell's border could never meet its neighbour's: All Borders
    // came out as detached blocks with a light seam between them. Owning every
    // pixel of the cell makes adjacent cells contiguous.
    m_tableView->setShowGrid(false);

    // ── Header + grid context menus (row/column operations) ──────────────────
    m_rowHeader->setContextMenuPolicy(Qt::CustomContextMenu);
    m_colHeader->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_rowHeader, &QWidget::customContextMenuRequested,
            this, &CalcModule::onRowHeaderMenu);
    connect(m_colHeader, &QWidget::customContextMenuRequested,
            this, &CalcModule::onColHeaderMenu);
    connect(m_tableView, &QWidget::customContextMenuRequested,
            this, &CalcModule::onGridContextMenu);

    // Double-click a header boundary to auto-fit that column/row.
    connect(m_colHeader, &QHeaderView::sectionHandleDoubleClicked,
            this, [this](int i){ m_tableView->resizeColumnToContents(i); });
    connect(m_rowHeader, &QHeaderView::sectionHandleDoubleClicked,
            this, [this](int i){ m_tableView->resizeRowToContents(i); });

    // Capture manual (and auto-fit) resizes into the active sheet model.
    connect(m_colHeader, &QHeaderView::sectionResized, this,
            [this](int idx, int, int newSize){
                updateFrozenViews();
                if (m_applyingSizes) return;
                m_model->setColWidth(idx, newSize);
                markDirty();
            });
    connect(m_rowHeader, &QHeaderView::sectionResized, this,
            [this](int idx, int, int newSize){
                updateFrozenViews();
                if (m_applyingSizes) return;
                m_model->setRowHeight(idx, newSize);
                markDirty();
            });

    // ── Ribbon (tabbed toolbar, above the formula bar) ──────────────────────
    buildRibbon();

    // ── Sheet tab bar (bottom) ──────────────────────────────────────────────
    m_tabBar = new QWidget(this);
    m_tabBar->setObjectName("calcTabBar");
    m_tabBar->setFixedHeight(30);
    m_tabBarLayout = new QHBoxLayout(m_tabBar);
    m_tabBarLayout->setContentsMargins(6, 0, 6, 0);
    m_tabBarLayout->setSpacing(2);

    // ── Assemble ──────────────────────────────────────────────────────────
    // The table's large vertical sizeHint would otherwise inflate the layout and
    // push the bottom tab bar off the window — make it yield to the fixed bars.
    m_tableView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);

    rootLayout->addWidget(new BrandBar(this));   // unified full-width brand tray
    rootLayout->addWidget(m_ribbon);
    rootLayout->addWidget(fbarRow);
    rootLayout->addWidget(m_tableView, 1);
    rootLayout->addWidget(m_tabBar);

    // ── Connect signals ───────────────────────────────────────────────────
    connectActiveView();

    connect(m_formulaBar, &QLineEdit::returnPressed,
            this, &CalcModule::onFormulaBarReturnPressed);

    connect(m_formulaBar, &QLineEdit::textEdited,
            this, &CalcModule::onFormulaBarTextEdited);

    rebuildTabBar();

    // Select A1 by default
    const QModelIndex first = m_model->index(0, 0);
    m_tableView->setCurrentIndex(first);
    m_tableView->scrollTo(first);
}

// ─────────────────────────────────────────────────────────────────────────────
// Slots
// ─────────────────────────────────────────────────────────────────────────────
void CalcModule::onSelectionChanged() {
    if (m_updatingFormulaBar) return;

    const QModelIndex cur = m_tableView->currentIndex();
    if (!cur.isValid()) return;

    // Format painter: if armed, paint the captured format onto this new
    // selection, then disarm (one-shot, like clicking it once in Excel).
    if (m_painterArmed) {
        m_painterArmed = false;
        if (m_formatPainterBtn) m_formatPainterBtn->setChecked(false);
        const CellFormat pf = m_painterFmt;
        applyFormatToSelection([pf](CellFormat& f){ f = pf; }, "Format Painter");
    }

    // Update name box
    const QString addr = FormulaEngine::cellAddress(cur.column(), cur.row());
    m_nameBox->setText(addr);
    emit cellSelected(addr);

    // Update formula bar with raw content
    m_updatingFormulaBar = true;
    m_formulaBar->setText(m_model->rawContent(cur.column(), cur.row()));
    m_formulaBar->setCursorPosition(m_formulaBar->text().length());
    m_updatingFormulaBar = false;

    // Highlight the selected column and row in the headers
    m_colHeader->setHighlightedSections({cur.column()});
    m_rowHeader->setHighlightedSections({cur.row()});

    // Redraw the Excel selection box.
    if (m_selOverlay) m_selOverlay->update();

    // Reflect the active cell's formatting in the toolbar.
    updateToolbarFromCell();

    // Live-refresh the pandas-code panel for the new selection (if open).
    updatePandasCode();
}

void CalcModule::onFormulaBarReturnPressed() {
    const QModelIndex cur = m_tableView->currentIndex();
    if (!cur.isValid()) return;

    m_model->setData(cur, m_formulaBar->text(), Qt::EditRole);

    // Move to the cell below (Excel-like behaviour)
    const int nextRow = std::min(cur.row() + 1, SpreadsheetModel::NUM_ROWS - 1);
    const QModelIndex next = m_model->index(nextRow, cur.column());
    m_tableView->setCurrentIndex(next);
    m_tableView->setFocus();
}

void CalcModule::onFormulaBarTextEdited(const QString& /*text*/) {
    // Live-preview while the user types: update the cell immediately
    // but don't move focus — only commit on Enter.
    // (Currently we commit on Enter in returnPressed.)
}

void CalcModule::onModelDataChanged() {
    refreshChartsData();                 // keep live charts in sync with the data
    if (m_ignoreChange) return;
    if (!m_dirty) {
        m_dirty = true;
        emit documentModified();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-sheet management
// ─────────────────────────────────────────────────────────────────────────────
SpreadsheetModel* CalcModule::createSheet(const QString& name) {
    auto* m = new SpreadsheetModel(this);
    m->setSheetName(name);
    m->setCrossSheetLookup([this](const QString& sn, int c, int r) -> QString {
        SpreadsheetModel* s = sheetByName(sn);
        return s ? s->displayValue(c, r) : QString("#REF!");
    });
    connect(m, &SpreadsheetModel::dataChanged, this, &CalcModule::onModelDataChanged);
    if (m_undoGroup) m_undoGroup->addStack(m->undoStack());
    m_sheets.append(m);
    return m;
}

SpreadsheetModel* CalcModule::sheetByName(const QString& name) const {
    for (auto* s : m_sheets)
        if (QString::compare(s->sheetName(), name, Qt::CaseInsensitive) == 0)
            return s;
    return nullptr;
}

void CalcModule::connectActiveView() {
    // setModel() recreates the selection model, so reconnect each switch.
    connect(m_tableView->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this, &CalcModule::onSelectionChanged);
    // currentChanged as well: dragging a selection moves the active cell
    // without always emitting selectionChanged, which left the name box and
    // the formula bar disagreeing about which cell you were on.
    connect(m_tableView->selectionModel(),
            &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex&, const QModelIndex&) { onSelectionChanged(); });
}

CalcModule::~CalcModule() {
    // Order matters. QObject's destroyed() signal fires from ~QObject, which is
    // the last base destructor to run, while the hashes below are members of
    // CalcModule and are already destroyed by then. A floating object deleted
    // during ~QWidget would therefore run a handler that reads freed memory -
    // reproducibly a crash on close for any workbook containing pictures.
    for (QWidget* w : m_floatingItems) if (w) w->disconnect(this);
    for (ChartObject* c : m_chartObjs)  if (c) c->disconnect(this);
    for (ShapeObject* s : m_shapeObjs)  if (s) s->disconnect(this);
    m_floatingItems.clear();
    m_chartObjs.clear();
    m_shapeObjs.clear();
    m_imageData.clear();
    m_objAnchors.clear();
    m_objFrac.clear();
    m_objFallback.clear();
    m_selectedObj = nullptr;
}

void CalcModule::switchToSheet(int index) {
    if (index < 0 || index >= m_sheets.size()) return;
    // Rebuilding the view raises model signals that look like edits. Nothing
    // here changes a cell, so the dirty flag is held still for the duration and
    // restored at the end.
    const bool wasDirty = m_dirty;
    const bool prevIgnore = m_ignoreChange;
    m_ignoreChange = true;
    const QScopeGuard restoreDirty([this, wasDirty, prevIgnore] {
        m_ignoreChange = prevIgnore;
        m_dirty = wasDirty;
    });

    m_activeSheet = index;
    m_model = m_sheets[index];
    m_tableView->setModel(m_model);
    m_undoGroup->setActiveStack(m_model->undoStack());
    connectActiveView();
    clearMarchingAnts();
    clearFilters();              // filters don't carry across sheets
    setFreeze(0, 0);             // freeze views hold the old selection model
    for (QWidget* w : m_floatingItems) w->deleteLater();
    m_floatingItems.clear();
    m_objAnchors.clear();

    applyMerges();
    applySizes();

    // Grow rows with wrapped cells that have no explicit stored height.
    //
    // Measured over the used columns only. resizeRowToContents() would ask the
    // delegate for a size hint on every column of the grid, which is 16384 cell
    // evaluations per wrapped row and made a sheet switch take a minute.
    QSet<int> wrapRows;
    const auto& data = m_model->cells();
    for (auto it = data.begin(); it != data.end(); ++it)
        if (it->second.format.wrap) wrapRows.insert(SpreadsheetModel::keyRow(it->first));

    int usedCol = -1, usedRow = -1;
    m_model->usedBounds(usedCol, usedRow);
    m_applyingSizes = true;
    if (usedCol >= 0) {
        QStyleOptionViewItem opt;
        opt.initFrom(m_tableView);
        QAbstractItemDelegate* del = m_tableView->itemDelegate();
        for (int row : wrapRows) {
            if (m_model->rowHeights().contains(row)) continue;
            int h = m_tableView->verticalHeader()->defaultSectionSize();
            for (int c = 0; c <= usedCol; ++c) {
                const QModelIndex ix = m_model->index(row, c);
                opt.rect = QRect(0, 0, m_tableView->columnWidth(c), 0);
                h = qMax(h, del->sizeHint(opt, ix).height());
            }
            m_tableView->setRowHeight(row, h);
        }
    }
    m_applyingSizes = false;

    const QModelIndex first = m_model->index(0, 0);
    m_tableView->setCurrentIndex(first);
    m_tableView->scrollTo(first);

    rebuildTabBar();
    rebuildChartObjects();               // recreate this sheet's chart widgets
    onSelectionChanged();
}

void CalcModule::addSheet(const QString& name) {
    createSheet(name);
    switchToSheet(m_sheets.size() - 1);
    markDirty();
}

void CalcModule::deleteSheet(int index) {
    if (m_sheets.size() <= 1 || index < 0 || index >= m_sheets.size()) return;
    SpreadsheetModel* s = m_sheets[index];
    if (m_tableView->model() == s) m_tableView->setModel(nullptr);
    m_undoGroup->removeStack(s->undoStack());
    m_sheets.removeAt(index);
    s->deleteLater();

    if (m_activeSheet >= m_sheets.size()) m_activeSheet = m_sheets.size() - 1;
    switchToSheet(m_activeSheet);
    markDirty();
}

void CalcModule::renameSheet(int index, const QString& name) {
    if (index < 0 || index >= m_sheets.size() || name.isEmpty()) return;
    if (sheetByName(name)) return;                 // names must be unique
    m_sheets[index]->setSheetName(name);
    rebuildTabBar();
    markDirty();
}

void CalcModule::onSheetTabMenu(int index, const QPoint& pos) {
    QMenu menu(this);
    QAction* ren = menu.addAction("Rename…");
    QAction* del = menu.addAction("Delete");
    del->setEnabled(m_sheets.size() > 1);
    const QWidget* w = qobject_cast<QWidget*>(sender());
    const QAction* a = menu.exec(w ? w->mapToGlobal(pos) : QCursor::pos());
    if (a == ren) {
        bool ok = false;
        const QString n = QInputDialog::getText(this, "Rename Sheet", "Name:",
                                                QLineEdit::Normal,
                                                m_sheets[index]->sheetName(), &ok);
        if (ok && !n.isEmpty()) renameSheet(index, n);
    } else if (a == del) {
        deleteSheet(index);
    }
}

void CalcModule::rebuildTabBar() {
    if (!m_tabBarLayout) return;
    // Clear existing tab buttons.
    QLayoutItem* it;
    while ((it = m_tabBarLayout->takeAt(0)) != nullptr) {
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }

    for (int i = 0; i < m_sheets.size(); ++i) {
        // A hidden sheet keeps its data (charts reference it) but gets no tab,
        // unless it is somehow the active one, which would leave the user with
        // no way back.
        if (m_sheets[i]->isHidden() && i != m_activeSheet) continue;
        auto* b = new QToolButton(m_tabBar);
        b->setText(m_sheets[i]->sheetName());
        b->setObjectName(i == m_activeSheet ? "sheetTabActive" : "sheetTab");
        b->setFocusPolicy(Qt::NoFocus);
        b->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(b, &QToolButton::clicked, this, [this, i]{ switchToSheet(i); });
        connect(b, &QToolButton::customContextMenuRequested,
                this, [this, i](const QPoint& p){ onSheetTabMenu(i, p); });
        m_tabBarLayout->addWidget(b);
    }

    auto* add = new QToolButton(m_tabBar);
    add->setText("+");
    add->setObjectName("sheetAddBtn");
    add->setToolTip("Add Sheet");
    add->setFocusPolicy(Qt::NoFocus);
    connect(add, &QToolButton::clicked, this,
            [this]{ addSheet(QString("Sheet%1").arg(m_sheets.size() + 1)); });
    m_tabBarLayout->addWidget(add);
    m_tabBarLayout->addStretch(1);
}

void CalcModule::markDirty() {
    // Bumped even when the document is already dirty: documentModified() only
    // fires on the first edit after a save, so it cannot tell crash recovery
    // that there is something new to snapshot. This counter can.
    ++m_editSeq;
    if (!m_dirty) {
        m_dirty = true;
        emit documentModified();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
QString CalcModule::currentAddress() const {
    const QModelIndex cur = m_tableView->currentIndex();
    if (!cur.isValid()) return "A1";
    return FormulaEngine::cellAddress(cur.column(), cur.row());
}

QRect CalcModule::selectedRect() const {
    const QModelIndexList sel = m_tableView->selectionModel()->selectedIndexes();
    if (sel.isEmpty()) {
        const QModelIndex cur = m_tableView->currentIndex();
        if (!cur.isValid()) return QRect(0, 0, 0, 0);
        return QRect(cur.column(), cur.row(), 1, 1);
    }
    int c1 = INT_MAX, r1 = INT_MAX, c2 = 0, r2 = 0;
    for (const QModelIndex& i : sel) {
        c1 = std::min(c1, i.column()); c2 = std::max(c2, i.column());
        r1 = std::min(r1, i.row());    r2 = std::max(r2, i.row());
    }
    return QRect(QPoint(c1, r1), QPoint(c2, r2));   // left,top .. right,bottom
}

// ─────────────────────────────────────────────────────────────────────────────
// Edit actions
// ─────────────────────────────────────────────────────────────────────────────
void CalcModule::buildActions() {
    // Undo/redo follow the active sheet via the QUndoGroup.
    m_undoAct = m_undoGroup->createUndoAction(this, "&Undo");
    m_undoAct->setShortcut(QKeySequence::Undo);              // Ctrl+Z
    m_redoAct = m_undoGroup->createRedoAction(this, "&Redo");
    m_redoAct->setShortcuts({QKeySequence::Redo,
                             QKeySequence("Ctrl+Y")});        // Ctrl+Y / Ctrl+Shift+Z

    m_cutAct = new QAction("Cu&t", this);
    m_cutAct->setShortcut(QKeySequence::Cut);                // Ctrl+X
    connect(m_cutAct, &QAction::triggered, this, &CalcModule::cutSelection);

    m_copyAct = new QAction("&Copy", this);
    m_copyAct->setShortcut(QKeySequence::Copy);              // Ctrl+C
    connect(m_copyAct, &QAction::triggered, this, &CalcModule::copySelection);

    m_pasteAct = new QAction("&Paste", this);
    m_pasteAct->setShortcut(QKeySequence::Paste);            // Ctrl+V
    connect(m_pasteAct, &QAction::triggered, this, &CalcModule::pasteClipboard);

    m_deleteAct = new QAction("&Delete", this);
    m_deleteAct->setShortcut(QKeySequence::Delete);          // Del
    connect(m_deleteAct, &QAction::triggered, this, &CalcModule::deleteSelection);

    // Make the shortcuts fire whenever focus is anywhere inside the module.
    for (QAction* a : {m_undoAct, m_redoAct, m_cutAct, m_copyAct, m_pasteAct, m_deleteAct}) {
        a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        addAction(a);
    }

    // Find / Replace shortcuts (Ctrl+F / Ctrl+H).
    auto* findSc = new QShortcut(QKeySequence::Find, this);
    findSc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findSc, &QShortcut::activated, this, &CalcModule::showFindDialog);
    auto* replSc = new QShortcut(QKeySequence::Replace, this);
    replSc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(replSc, &QShortcut::activated, this, &CalcModule::showReplaceDialog);

    // Fill Down / Fill Right (Excel: Ctrl+D / Ctrl+R).
    auto* fillD = new QShortcut(QKeySequence("Ctrl+D"), this);
    fillD->setContext(Qt::WidgetWithChildrenShortcut);
    connect(fillD, &QShortcut::activated, this, &CalcModule::fillDown);
    auto* fillR = new QShortcut(QKeySequence("Ctrl+R"), this);
    fillR->setContext(Qt::WidgetWithChildrenShortcut);
    connect(fillR, &QShortcut::activated, this, &CalcModule::fillRight);
}

// ─────────────────────────────────────────────────────────────────────────────
// Formatting toolbar
// ─────────────────────────────────────────────────────────────────────────────
static void styleColorButton(QToolButton* b, const QColor& c) {
    // ID selector keeps the colour swatch from being overridden by the
    // ribbon's generic "QWidget#calcRibbon QToolButton" rule.
    const QString id = b->objectName();
    b->setStyleSheet(QString(
        "QToolButton#%1{border:1px solid #C8C8C8;border-bottom:3px solid %2;"
        "border-radius:3px;background:#FFFFFF;font-weight:700;color:#1C1E26;}"
        "QToolButton#%1:hover{background:#ECECEC;}").arg(id, c.name()));
}

// Decimal places encoded in a number-format code ("0.00" → 2).
static int decimalsOfCode(const QString& code) {
    const int dot = code.indexOf('.');
    if (dot < 0) return 0;
    int d = 0;
    for (int i = dot + 1; i < code.size(); ++i) {
        const QChar c = code[i];
        if (c == '0' || c == '#') ++d; else break;
    }
    return d;
}

// Rebuild a number-format code from its flags + decimal count.
static QString buildNumberCode(bool currency, bool percent, bool thousands, int decimals) {
    QString out;
    if (currency) out += '$';
    out += thousands ? "#,##0" : "0";
    if (decimals > 0) out += '.' + QString(decimals, '0');
    if (percent) out += '%';
    return out;
}

void CalcModule::buildRibbon() {
    m_ribbon = new QWidget(this);
    m_ribbon->setObjectName("calcRibbon");
    m_ribbon->setFixedHeight(140);

    auto* rootV = new QVBoxLayout(m_ribbon);
    rootV->setContentsMargins(0, 0, 0, 0);
    rootV->setSpacing(0);

    // ── Tab strip ─────────────────────────────────────────────────────────────
    auto* tabs = new QWidget(m_ribbon);
    tabs->setObjectName("ribbonTabs");
    tabs->setFixedHeight(32);
    auto* tabsLay = new QHBoxLayout(tabs);
    tabsLay->setContentsMargins(10, 0, 10, 0);
    tabsLay->setSpacing(2);

    m_ribbonStack = new QStackedWidget(m_ribbon);
    m_ribbonTabBtns.clear();

    auto makeTab = [&](const QString& text, int page) {
        auto* t = new QToolButton(tabs);
        t->setText(text);
        t->setObjectName("ribbonTab");
        t->setCheckable(true);
        t->setFocusPolicy(Qt::NoFocus);
        connect(t, &QToolButton::clicked, this, [this, page]{
            m_ribbonStack->setCurrentIndex(page);
            for (int i = 0; i < m_ribbonTabBtns.size(); ++i)
                m_ribbonTabBtns[i]->setChecked(i == page);
        });
        tabsLay->addWidget(t);
        m_ribbonTabBtns.push_back(t);
        return t;
    };
    makeTab("Home", 0);  makeTab("Insert", 1);  makeTab("Page Layout", 2);
    makeTab("Formulas", 3);  makeTab("Data", 4);  makeTab("Review", 5);
    makeTab("View", 6);  makeTab("Tools", 7);  makeTab("Smart Toolbox", 8);
    m_ribbonTabBtns[0]->setChecked(true);
    tabsLay->addStretch(1);

    rootV->addWidget(tabs);
    rootV->addWidget(m_ribbonStack, 1);

    // ── Layout helpers ─────────────────────────────────────────────────────────
    // A labelled group: returns the inner content row (HBox). Big buttons go
    // straight in; dense clusters add a 2-row sub-grid via cluster2().
    auto group = [&](QHBoxLayout* pageLay, const QString& title) -> QHBoxLayout* {
        auto* g  = new QWidget(m_ribbon);
        auto* gv = new QVBoxLayout(g);
        gv->setContentsMargins(6, 3, 6, 2);
        gv->setSpacing(2);
        auto* rowW = new QWidget(g);
        auto* row  = new QHBoxLayout(rowW);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(3);
        auto* lbl = new QLabel(title, g);
        lbl->setObjectName("groupLabel");
        lbl->setAlignment(Qt::AlignHCenter);
        gv->addWidget(rowW, 1);
        gv->addWidget(lbl);
        pageLay->addWidget(g);
        auto* sep = new QFrame(m_ribbon);
        sep->setFrameShape(QFrame::VLine);
        sep->setObjectName("ribbonSep");
        pageLay->addWidget(sep);
        return row;
    };
    // Balance a multi-word label onto (at most) two lines so it fits a narrow
    // icon-over-text button without eliding — the WPS look.
    auto wrap2 = [](const QString& label) -> QString {
        if (label.length() <= 7 || !label.contains(' ')) return label;
        const int mid = label.length() / 2;
        int best = -1, bestDist = 999;
        for (int i = 0; i < label.length(); ++i)
            if (label[i] == ' ' && qAbs(i - mid) < bestDist) { bestDist = qAbs(i - mid); best = i; }
        if (best < 0) return label;
        QString out = label;
        out[best] = '\n';
        return out;
    };
    // Large icon-over-text button (the WPS "primary action" look).
    auto big = [&](const QString& icon, const QString& label, const QString& tip,
                   bool checkable = false) {
        auto* b = new QToolButton(m_ribbon);
        b->setObjectName("ribbonBig");
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setIcon(calcIcon(icon));
        b->setIconSize(QSize(20, 20));
        b->setText(wrap2(label));
        b->setToolTip(tip.isEmpty() ? label : tip);
        b->setCheckable(checkable);
        b->setFocusPolicy(Qt::NoFocus);
        // Size the button to fit its widest text line so labels never elide.
        { QFont mf("Segoe UI"); mf.setPixelSize(11);
          const QFontMetrics fm(mf);
          int wmax = 0;
          for (const QString& line : b->text().split('\n'))
              wmax = std::max(wmax, fm.horizontalAdvance(line));
          b->setMinimumWidth(std::min(120, std::max(52, wmax + 14))); }
        return b;
    };
    // Small icon-only button (dense clusters).
    auto small = [&](const QString& icon, const QString& tip, bool checkable = false) {
        auto* b = new QToolButton(m_ribbon);
        b->setObjectName("ribbonSmall");
        b->setIcon(calcIcon(icon));
        b->setIconSize(QSize(16, 16));
        b->setToolTip(tip);
        b->setCheckable(checkable);
        b->setFocusPolicy(Qt::NoFocus);
        b->setFixedSize(24, 24);
        return b;
    };
    // Small text-letter toggle (B / I / U / S — conventional Office letterforms).
    auto letter = [&](const QString& t, const QString& tip,
                      bool b_, bool i_, bool u_, bool s_) {
        auto* b = new QToolButton(m_ribbon);
        b->setObjectName("ribbonLetter");
        b->setText(t);
        b->setToolTip(tip);
        b->setCheckable(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setFixedSize(24, 24);
        QFont f = b->font();
        f.setPointSize(10); f.setBold(b_); f.setItalic(i_);
        f.setUnderline(u_); f.setStrikeOut(s_);
        b->setFont(f);
        return b;
    };
    // A 2-row column-major cluster of small controls inside a group row.
    auto cluster2 = [&](QHBoxLayout* row) {
        auto* w = new QWidget(m_ribbon);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setHorizontalSpacing(2);
        g->setVerticalSpacing(2);
        row->addWidget(w);
        auto idx = std::make_shared<int>(0);
        return [g, idx](QWidget* cell) {
            g->addWidget(cell, (*idx) % 2, (*idx) / 2);
            ++(*idx);
            return cell;
        };
    };
    // Honest-info big button (features needing external services / not built).
    auto soonBig = [&](QHBoxLayout* into, const QString& icon, const QString& label,
                       const QString& tip = QString()) {
        auto* b = big(icon, label, tip);
        const QString name = tip.isEmpty() ? label : tip;
        connect(b, &QToolButton::clicked, this, [this, name]{
            featureInfo(name, name + " needs a feature that isn't part of this build "
                                     "(typically an online service or a subsystem not yet added).");
        });
        into->addWidget(b);
        return b;
    };
    // Big button wired to an action.
    auto act = [&](QHBoxLayout* into, const QString& icon, const QString& label,
                   const QString& tip, std::function<void()> fn) {
        auto* b = big(icon, label, tip);
        connect(b, &QToolButton::clicked, this, std::move(fn));
        into->addWidget(b);
        return b;
    };
    // Big button with a drop-down of functions → startFunctionEntry.
    auto fnBig = [&](QHBoxLayout* into, const QString& icon, const QString& label,
                     const QString& tip, const QStringList& fns) {
        auto* b = big(icon, label, tip);
        b->setPopupMode(QToolButton::InstantPopup);
        auto* m = new QMenu(b);
        for (const QString& fn : fns)
            connect(m->addAction(fn + "(…)"), &QAction::triggered, this,
                    [this, fn]{ startFunctionEntry(fn); });
        b->setMenu(m);
        into->addWidget(b);
        return b;
    };
    // Menu item with an icon (request 3: icons on sub-options of sub-options).
    auto mi = [&](QMenu* m, const QString& icon, const QString& text) -> QAction* {
        return m->addAction(calcIcon(icon), text);
    };
    // Wired small icon button placed into a cluster-add lambda.
    auto sIn = [&](const std::function<QWidget*(QWidget*)>& add, const QString& icon,
                   const QString& tip, std::function<void()> fn) {
        auto* b = small(icon, tip);
        connect(b, &QToolButton::clicked, this, std::move(fn));
        add(b);
        return b;
    };
    (void)mi; (void)sIn;
    auto newPage = [&]() -> QHBoxLayout* {
        auto* area = new QScrollArea(m_ribbonStack);
        area->setObjectName("ribbonScroll");
        area->setWidgetResizable(true);
        area->setFrameShape(QFrame::NoFrame);
        area->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* page = new QWidget(area);
        auto* pl = new QHBoxLayout(page);
        pl->setContentsMargins(6, 1, 6, 1);
        pl->setSpacing(2);
        area->setWidget(page);
        m_ribbonStack->addWidget(area);
        return pl;
    };

    // ══ HOME PAGE ════════════════════════════════════════════════════════════
    auto curRow = [this]{ const QModelIndex c = m_tableView->currentIndex(); return c.isValid() ? c.row() : 0; };
    auto curCol = [this]{ const QModelIndex c = m_tableView->currentIndex(); return c.isValid() ? c.column() : 0; };
    {
        QHBoxLayout* pl = newPage();

        // ── Clipboard ──────────────────────────────────────────────────────────
        QHBoxLayout* clip = group(pl, "Clipboard");
        auto* pasteBtn = big("paste", "Paste", "Paste (Ctrl+V)");
        connect(pasteBtn, &QToolButton::clicked, this, &CalcModule::pasteClipboard);
        clip->addWidget(pasteBtn);
        auto addClip = cluster2(clip);
        auto* cutBtn  = small("cut",  "Cut (Ctrl+X)");
        auto* copyBtn = small("copy", "Copy (Ctrl+C)");
        m_formatPainterBtn = small("format-painter",
                                   "Format Painter — copy this cell's formatting, then click a target", true);
        connect(cutBtn,  &QToolButton::clicked, this, &CalcModule::cutSelection);
        connect(copyBtn, &QToolButton::clicked, this, &CalcModule::copySelection);
        connect(m_formatPainterBtn, &QToolButton::clicked, this, &CalcModule::onFormatPainterClicked);
        addClip(cutBtn); addClip(copyBtn); addClip(m_formatPainterBtn);

        // ── Font ───────────────────────────────────────────────────────────────
        QHBoxLayout* font = group(pl, "Font");
        auto* fw = new QWidget(m_ribbon);
        auto* fg = new QGridLayout(fw);
        fg->setContentsMargins(0, 0, 0, 0);
        fg->setHorizontalSpacing(2); fg->setVerticalSpacing(2);
        // Plain QComboBox instead of QFontComboBox: QFontComboBox touches every
        // installed font up front, which stalls window creation on font-heavy
        // machines (same fix as the Writer ribbon).
        m_fontCombo = new QComboBox(m_ribbon);
        m_fontCombo->setObjectName("fontCombo");
        m_fontCombo->setEditable(true);
        m_fontCombo->setInsertPolicy(QComboBox::NoInsert);
        m_fontCombo->setMaximumWidth(132);
        m_fontCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_fontCombo->setMinimumContentsLength(10);
        m_fontCombo->addItems(QFontDatabase::families());
        m_fontCombo->setCurrentText("Calibri");
        m_fontCombo->setFocusPolicy(Qt::ClickFocus);
        connect(m_fontCombo, &QComboBox::currentTextChanged, this, [this](const QString& fam){
            if (m_updatingToolbar || fam.isEmpty()) return;
            onFontFamilyChanged();
        });
        m_sizeCombo = new QComboBox(m_ribbon);
        m_sizeCombo->setObjectName("sizeCombo");
        m_sizeCombo->setEditable(true);
        m_sizeCombo->setFixedWidth(48);
        m_sizeCombo->setFocusPolicy(Qt::ClickFocus);
        for (int s : {8,9,10,11,12,14,16,18,20,24,28,32,36,48,72}) m_sizeCombo->addItem(QString::number(s));
        m_sizeCombo->setCurrentText("11");
        connect(m_sizeCombo, &QComboBox::currentTextChanged, this, [this](const QString&){ onFontSizeChanged(); });
        auto* growBtn   = small("font-grow",   "Increase Font Size");
        auto* shrinkBtn = small("font-shrink", "Decrease Font Size");
        connect(growBtn,   &QToolButton::clicked, this, &CalcModule::onIncreaseFont);
        connect(shrinkBtn, &QToolButton::clicked, this, &CalcModule::onDecreaseFont);
        fg->addWidget(m_fontCombo, 0, 0, 1, 4);
        fg->addWidget(m_sizeCombo, 0, 4);
        fg->addWidget(growBtn,     0, 5);
        fg->addWidget(shrinkBtn,   0, 6);
        m_boldBtn      = letter("B", "Bold (Ctrl+B)",      true,  false, false, false);
        m_italicBtn    = letter("I", "Italic (Ctrl+I)",    false, true,  false, false);
        m_underlineBtn = letter("U", "Underline (Ctrl+U)", false, false, true,  false);
        m_strikeBtn    = letter("S", "Strikethrough",      false, false, false, true);
        connect(m_boldBtn,      &QToolButton::toggled, this, &CalcModule::onBoldToggled);
        connect(m_italicBtn,    &QToolButton::toggled, this, &CalcModule::onItalicToggled);
        connect(m_underlineBtn, &QToolButton::toggled, this, &CalcModule::onUnderlineToggled);
        connect(m_strikeBtn,    &QToolButton::toggled, this, &CalcModule::onStrikeToggled);
        m_textColorBtn = small("font-color", "Text Colour");
        m_textColorBtn->setObjectName("textColorBtn");
        styleColorButton(m_textColorBtn, m_lastTextColor);
        connect(m_textColorBtn, &QToolButton::clicked, this, &CalcModule::onTextColorClicked);
        m_fillColorBtn = small("fill-color", "Fill Colour");
        m_fillColorBtn->setObjectName("fillColorBtn");
        styleColorButton(m_fillColorBtn, m_lastFillColor);
        connect(m_fillColorBtn, &QToolButton::clicked, this, &CalcModule::onFillColorClicked);
        m_borderBtn = small("borders", "Borders");
        m_borderBtn->setPopupMode(QToolButton::InstantPopup);
        {
            auto* menu = new QMenu(m_borderBtn);
            menu->addAction("All Borders")    ->setData(int(CellFormat::BAll));
            menu->addAction("Outside Borders")->setData(-1);
            menu->addAction("Bottom Border")  ->setData(int(CellFormat::BBottom));
            menu->addAction("Top Border")     ->setData(int(CellFormat::BTop));
            menu->addSeparator();
            menu->addAction("No Border")      ->setData(0);
            connect(menu, &QMenu::triggered, this, &CalcModule::onBorderMenu);
            m_borderBtn->setMenu(menu);
        }
        m_clearFmtBtn = small("clear-format", "Clear Formatting");
        connect(m_clearFmtBtn, &QToolButton::clicked, this, &CalcModule::onClearFormatting);
        fg->addWidget(m_boldBtn,      1, 0);
        fg->addWidget(m_italicBtn,    1, 1);
        fg->addWidget(m_underlineBtn, 1, 2);
        fg->addWidget(m_strikeBtn,    1, 3);
        fg->addWidget(m_textColorBtn, 1, 4);
        fg->addWidget(m_fillColorBtn, 1, 5);
        fg->addWidget(m_borderBtn,    1, 6);
        fg->addWidget(m_clearFmtBtn,  1, 7);
        font->addWidget(fw);

        // ── Alignment ────────────────────────────────────────────────────────────
        QHBoxLayout* al = group(pl, "Alignment");
        auto* aw = new QWidget(m_ribbon);
        auto* ag = new QGridLayout(aw);
        ag->setContentsMargins(0, 0, 0, 0);
        ag->setHorizontalSpacing(2); ag->setVerticalSpacing(2);
        m_alignTopBtn = small("valign-top",    "Align Top",    true);
        m_alignMidBtn = small("valign-middle", "Align Middle", true);
        m_alignBotBtn = small("valign-bottom", "Align Bottom", true);
        connect(m_alignTopBtn, &QToolButton::clicked, this, &CalcModule::onVAlignClicked);
        connect(m_alignMidBtn, &QToolButton::clicked, this, &CalcModule::onVAlignClicked);
        connect(m_alignBotBtn, &QToolButton::clicked, this, &CalcModule::onVAlignClicked);
        m_wrapBtn = small("wrap", "Wrap Text", true);
        connect(m_wrapBtn, &QToolButton::toggled, this, &CalcModule::onWrapToggled);
        auto* orientBtn = small("orientation", "Orientation");
        orientBtn->setPopupMode(QToolButton::InstantPopup);
        {
            auto* om = new QMenu(orientBtn);
            for (const char* o : {"Angle Counterclockwise", "Angle Clockwise",
                                  "Vertical Text", "Rotate Text Up", "Rotate Text Down"}) {
                const QString name = QString::fromUtf8(o);
                connect(om->addAction(name), &QAction::triggered, this, [this, name]{ notImplemented(name); });
            }
            orientBtn->setMenu(om);
        }
        m_alignLeftBtn   = small("halign-left",   "Align Left",   true);
        m_alignCenterBtn = small("halign-center", "Align Center", true);
        m_alignRightBtn  = small("halign-right",  "Align Right",  true);
        connect(m_alignLeftBtn,   &QToolButton::clicked, this, &CalcModule::onAlignClicked);
        connect(m_alignCenterBtn, &QToolButton::clicked, this, &CalcModule::onAlignClicked);
        connect(m_alignRightBtn,  &QToolButton::clicked, this, &CalcModule::onAlignClicked);
        auto* indDec = small("indent-dec", "Decrease Indent");
        auto* indInc = small("indent-inc", "Increase Indent");
        connect(indDec, &QToolButton::clicked, this, &CalcModule::onDecreaseIndent);
        connect(indInc, &QToolButton::clicked, this, &CalcModule::onIncreaseIndent);
        m_mergeBtn = small("merge", "Merge && Center (toggle)");
        connect(m_mergeBtn, &QToolButton::clicked, this, &CalcModule::onMergeClicked);
        ag->addWidget(m_alignTopBtn,    0, 0);
        ag->addWidget(m_alignMidBtn,    0, 1);
        ag->addWidget(m_alignBotBtn,    0, 2);
        ag->addWidget(m_wrapBtn,        0, 3);
        ag->addWidget(orientBtn,        0, 4);
        ag->addWidget(m_alignLeftBtn,   1, 0);
        ag->addWidget(m_alignCenterBtn, 1, 1);
        ag->addWidget(m_alignRightBtn,  1, 2);
        ag->addWidget(indDec,           1, 3);
        ag->addWidget(indInc,           1, 4);
        ag->addWidget(m_mergeBtn,       1, 5);
        al->addWidget(aw);

        // ── Number ────────────────────────────────────────────────────────────────
        QHBoxLayout* num = group(pl, "Number");
        auto* nw = new QWidget(m_ribbon);
        auto* ng = new QGridLayout(nw);
        ng->setContentsMargins(0, 0, 0, 0);
        ng->setHorizontalSpacing(2); ng->setVerticalSpacing(2);
        m_numFmtCombo = new QComboBox(m_ribbon);
        m_numFmtCombo->setObjectName("numFmtCombo");
        m_numFmtCombo->setFixedWidth(118);
        m_numFmtCombo->setFocusPolicy(Qt::ClickFocus);
        m_numFmtCombo->addItem("General",    QString(""));
        m_numFmtCombo->addItem("Number",     QString("0"));
        m_numFmtCombo->addItem("Number .00", QString("0.00"));
        m_numFmtCombo->addItem("Currency",   QString("$#,##0.00"));
        m_numFmtCombo->addItem("Percent",    QString("0%"));
        m_numFmtCombo->addItem("Comma",      QString("#,##0.00"));
        m_numFmtCombo->addItem("Date",       QString("yyyy-mm-dd"));
        m_numFmtCombo->addItem("Date (D/M/Y)", QString("d/m/yyyy"));
        m_numFmtCombo->addItem("Time",       QString("h:mm"));
        connect(m_numFmtCombo, &QComboBox::currentIndexChanged, this, [this](int){ onNumberFormatChanged(); });
        m_currencyBtn = small("currency", "Currency Format");
        m_percentBtn  = small("percent",  "Percent Format");
        m_commaBtn    = small("comma",    "Comma Style");
        m_incDecBtn   = small("dec-inc",  "Increase Decimals");
        m_decDecBtn   = small("dec-dec",  "Decrease Decimals");
        connect(m_currencyBtn, &QToolButton::clicked, this, &CalcModule::onCurrencyClicked);
        connect(m_percentBtn,  &QToolButton::clicked, this, &CalcModule::onPercentClicked);
        connect(m_commaBtn,    &QToolButton::clicked, this, &CalcModule::onCommaClicked);
        connect(m_incDecBtn,   &QToolButton::clicked, this, &CalcModule::onIncreaseDecimals);
        connect(m_decDecBtn,   &QToolButton::clicked, this, &CalcModule::onDecreaseDecimals);
        ng->addWidget(m_numFmtCombo, 0, 0, 1, 5);
        ng->addWidget(m_currencyBtn, 1, 0);
        ng->addWidget(m_percentBtn,  1, 1);
        ng->addWidget(m_commaBtn,    1, 2);
        ng->addWidget(m_incDecBtn,   1, 3);
        ng->addWidget(m_decDecBtn,   1, 4);
        num->addWidget(nw);

        // ── Styles ────────────────────────────────────────────────────────────────
        QHBoxLayout* styles = group(pl, "Styles");
        act(styles, "cond-format", "Conditional", "Conditional Formatting",
            [this]{ showConditionalFormatDialog(); });
        soonBig(styles, "format-table", "Format Table", "Format as Table");
        soonBig(styles, "cell-styles",  "Cell Styles");

        // ── Cells ─────────────────────────────────────────────────────────────────
        QHBoxLayout* cellsG = group(pl, "Cells");
        {
            auto* ins = big("insert-cells", "Insert", "Insert cells, rows, columns or a sheet");
            ins->setPopupMode(QToolButton::InstantPopup);
            auto* m = new QMenu(ins);
            connect(mi(m, "insert-cells", "Insert Row Above"),   &QAction::triggered, this, [this, curRow]{ m_model->insertRowAt(curRow()); });
            connect(mi(m, "insert-cells", "Insert Row Below"),   &QAction::triggered, this, [this, curRow]{ m_model->insertRowAt(curRow() + 1); });
            connect(mi(m, "insert-cells", "Insert Column Left"), &QAction::triggered, this, [this, curCol]{ m_model->insertColumnAt(curCol()); });
            connect(mi(m, "insert-cells", "Insert Column Right"),&QAction::triggered, this, [this, curCol]{ m_model->insertColumnAt(curCol() + 1); });
            m->addSeparator();
            connect(mi(m, "table", "Insert Sheet"),       &QAction::triggered, this, [this]{ addSheet(QString("Sheet%1").arg(m_sheets.size() + 1)); });
            ins->setMenu(m);
            cellsG->addWidget(ins);
        }
        {
            auto* del = big("delete-cells", "Delete", "Delete rows, columns or a sheet");
            del->setPopupMode(QToolButton::InstantPopup);
            auto* m = new QMenu(del);
            connect(mi(m, "delete-cells", "Delete Row"),    &QAction::triggered, this, [this, curRow]{ m_model->deleteRowAt(curRow()); });
            connect(mi(m, "delete-cells", "Delete Column"), &QAction::triggered, this, [this, curCol]{ m_model->deleteColumnAt(curCol()); });
            m->addSeparator();
            connect(mi(m, "delete-cells", "Delete Sheet"),  &QAction::triggered, this, [this]{ deleteSheet(m_activeSheet); });
            del->setMenu(m);
            cellsG->addWidget(del);
        }
        {
            auto* fmt = big("format-cells", "Format", "Row height / column width / autofit");
            fmt->setPopupMode(QToolButton::InstantPopup);
            auto* m = new QMenu(fmt);
            connect(mi(m, "format-cells", "Row Height…"), &QAction::triggered, this, [this, curRow]{
                bool ok=false; const int h = QInputDialog::getInt(this, "Row Height", "Height (px):",
                    m_tableView->rowHeight(curRow()), 8, 600, 1, &ok);
                if (ok) { m_tableView->setRowHeight(curRow(), h); m_model->setRowHeight(curRow(), h); markDirty(); }
            });
            connect(mi(m, "format-cells", "Column Width…"), &QAction::triggered, this, [this, curCol]{
                bool ok=false; const int w = QInputDialog::getInt(this, "Column Width", "Width (px):",
                    m_tableView->columnWidth(curCol()), 8, 1000, 1, &ok);
                if (ok) { m_tableView->setColumnWidth(curCol(), w); m_model->setColWidth(curCol(), w); markDirty(); }
            });
            m->addSeparator();
            connect(mi(m, "format-cells", "AutoFit Row Height"),  &QAction::triggered, this, [this, curRow]{ m_tableView->resizeRowToContents(curRow()); });
            connect(mi(m, "format-cells", "AutoFit Column Width"),&QAction::triggered, this, [this, curCol]{ m_tableView->resizeColumnToContents(curCol()); });
            fmt->setMenu(m);
            cellsG->addWidget(fmt);
        }

        // ── Editing ────────────────────────────────────────────────────────────────
        QHBoxLayout* ed = group(pl, "Editing");
        {
            auto* sum = big("autosum", "AutoSum", "AutoSum");
            sum->setPopupMode(QToolButton::MenuButtonPopup);
            connect(sum, &QToolButton::clicked, this, [this]{ insertFunction("SUM"); });
            auto* m = new QMenu(sum);
            for (const char* fn : {"SUM", "AVERAGE", "COUNT", "MAX", "MIN"}) {
                const QString name = QString::fromUtf8(fn);
                connect(mi(m, "sigma", name), &QAction::triggered, this, [this, name]{ insertFunction(name); });
            }
            sum->setMenu(m);
            ed->addWidget(sum);
        }
        auto addEd = cluster2(ed);
        {
            auto* fill = small("fill", "Fill");
            fill->setPopupMode(QToolButton::InstantPopup);
            auto* m = new QMenu(fill);
            connect(mi(m, "fill", "Fill Down (Ctrl+D)"),  &QAction::triggered, this, [this]{ fillDown(); });
            connect(mi(m, "fill", "Fill Right (Ctrl+R)"), &QAction::triggered, this, [this]{ fillRight(); });
            fill->setMenu(m);
            addEd(fill);
        }
        {
            auto* clr = small("clear", "Clear");
            clr->setPopupMode(QToolButton::InstantPopup);
            auto* m = new QMenu(clr);
            connect(mi(m, "clear",        "Clear All"),      &QAction::triggered, this, [this]{ onClearFormatting(); deleteSelection(); });
            connect(mi(m, "clear-format", "Clear Formats"),  &QAction::triggered, this, &CalcModule::onClearFormatting);
            connect(mi(m, "delete-cells", "Clear Contents"), &QAction::triggered, this, &CalcModule::deleteSelection);
            clr->setMenu(m);
            addEd(clr);
        }
        {
            auto* sf = small("sort-filter", "Sort & Filter");
            sf->setPopupMode(QToolButton::InstantPopup);
            auto* m = new QMenu(sf);
            connect(mi(m, "sort-az", "Sort A → Z"), &QAction::triggered, this, [this]{ sortByColumn(true); });
            connect(mi(m, "sort-za", "Sort Z → A"), &QAction::triggered, this, [this]{ sortByColumn(false); });
            connect(mi(m, "filter",  "Filter"),     &QAction::triggered, this, [this]{ showColumnFilter(); });
            sf->setMenu(m);
            addEd(sf);
        }
        {
            auto* fs = small("find", "Find & Select");
            fs->setPopupMode(QToolButton::InstantPopup);
            auto* m = new QMenu(fs);
            connect(mi(m, "find",     "Find… (Ctrl+F)"),    &QAction::triggered, this, &CalcModule::showFindDialog);
            connect(mi(m, "find",     "Replace… (Ctrl+H)"), &QAction::triggered, this, &CalcModule::showReplaceDialog);
            connect(mi(m, "generic",  "Go To…"),            &QAction::triggered, this, [this]{ insertTextValue("Go To", "Cell (e.g. B5):"); });
            fs->setMenu(m);
            addEd(fs);
        }

        pl->addStretch(1);
    }

    // Vertical stack of checkboxes inside a group.
    auto checkColumn = [&](QHBoxLayout* into) {
        auto* w = new QWidget(m_ribbon);
        auto* v = new QVBoxLayout(w);
        v->setContentsMargins(2, 1, 2, 1);
        v->setSpacing(4);
        into->addWidget(w);
        return v;
    };

    // ══ INSERT PAGE ══════════════════════════════════════════════════════════
    {
        QHBoxLayout* pl = newPage();

        QHBoxLayout* tables = group(pl, "Tables");
        act(tables, "pivot-table", "Pivot\nTable", "Summarise the table (group & total) on a new sheet", [this]{ pivotSummary(false); });
        act(tables, "pivot-chart", "Pivot\nChart", "Summarise the table and chart it", [this]{ pivotSummary(true); });
        act(tables, "table",       "Table",        "Format the selection as a banded table", [this]{ formatAsTable(); });

        QHBoxLayout* ill = group(pl, "Illustrations");
        act(ill, "picture",   "Pictures",   "Insert a picture from a file", [this]{ insertImageObject(); });
        act(ill, "camera",    "Screenshot", "Insert an image", [this]{ insertImageObject(); });
        act(ill, "shapes",    "Shapes",     "Insert an image/shape", [this]{ insertImageObject(); });
        act(ill, "icons-lib", "Icons",      "Insert an icon image", [this]{ insertImageObject(); });

        QHBoxLayout* textG = group(pl, "Text");
        act(textG, "wordart",     "WordArt",  "Style the selection as bold coloured WordArt", [this]{ applyWordArt(); });
        act(textG, "textbox",     "Text\nBox","Insert a floating text box", [this]{ insertTextBoxObject(); });
        act(textG, "file-object", "Object",   "Insert a file path / object reference", [this]{ insertTextValue("Insert Object", "File path or text:"); });

        QHBoxLayout* charts = group(pl, "Charts");
        {
            auto* cb = big("chart", "Chart", "Insert a chart from the selected cells");
            cb->setPopupMode(QToolButton::MenuButtonPopup);
            connect(cb, &QToolButton::clicked, this, [this]{ insertChart(ChartType::Column); });
            auto* m = new QMenu(cb);
            struct { const char* label; const char* icon; ChartType t; } items[] = {
                {"Column","chart",ChartType::Column}, {"Bar","chart",ChartType::Bar},
                {"Line","chart-line",ChartType::Line}, {"Area","chart-line",ChartType::Area},
                {"Pie","chart-pie",ChartType::Pie}, {"Scatter","chart-scatter",ChartType::Scatter},
            };
            for (auto& it : items) {
                const ChartType t = it.t;
                connect(mi(m, it.icon, QString::fromUtf8(it.label)), &QAction::triggered,
                        this, [this, t]{ insertChart(t); });
            }
            cb->setMenu(m);
            charts->addWidget(cb);
        }
        act(charts, "chart-line",    "Line",    "Insert a line chart",    [this]{ insertChart(ChartType::Line); });
        act(charts, "chart-pie",     "Pie",     "Insert a pie chart",     [this]{ insertChart(ChartType::Pie); });
        act(charts, "chart-scatter", "Scatter", "Insert a scatter chart", [this]{ insertChart(ChartType::Scatter); });

        QHBoxLayout* spark = group(pl, "Sparklines");
        act(spark, "sparkline", "Sparkline", "Insert a mini line chart of the selection", [this]{ insertChart(ChartType::Line); });

        QHBoxLayout* sym = group(pl, "Symbols");
        act(sym, "symbol",   "Symbol",   "Insert a symbol",   [this]{ insertSymbolDialog(); });
        act(sym, "equation", "Equation", "Insert an equation", [this]{ insertTextValue("Equation", "Equation text:"); });
        act(sym, "latex",    "LaTeX",    "Insert LaTeX text",  [this]{ insertTextValue("LaTeX", "LaTeX text:"); });

        QHBoxLayout* linksG = group(pl, "Links");
        act(linksG, "link", "Link", "Insert a hyperlink into the cell", [this]{ insertHyperlink(); });

        QHBoxLayout* media = group(pl, "Media");
        act(media, "camera", "Camera", "Insert an image", [this]{ insertImageObject(); });
        act(media, "forms",  "Forms",  "Online forms (not in this build)", [this]{ featureInfo("Forms", "Online forms aren't part of this build."); });

        pl->addStretch(1);
    }

    // ══ PAGE LAYOUT PAGE ══════════════════════════════════════════════════════
    {
        QHBoxLayout* pl = newPage();

        QHBoxLayout* print = group(pl, "Print");
        act(print, "print-preview", "Print\nPreview", "Preview / print the sheet", [this]{ printPreview(); });
        {
            auto* pa = big("print-area", "Print\nArea", "Print Area");
            pa->setPopupMode(QToolButton::InstantPopup);
            auto* m = new QMenu(pa);
            connect(mi(m, "print-area", "Set Print Area"),   &QAction::triggered, this, [this]{ setPrintArea(); });
            connect(mi(m, "clear",      "Clear Print Area"), &QAction::triggered, this, [this]{ clearPrintArea(); });
            pa->setMenu(m);
            print->addWidget(pa);
        }

        QHBoxLayout* setup = group(pl, "Page Setup");
        act(setup, "margins",      "Margins",      "Print margins", [this]{ featureInfo("Margins", "Page margins for printing aren't configurable yet."); });
        act(setup, "page-orient",  "Orientation",  "Page orientation", [this]{ featureInfo("Orientation", "PDF/print currently uses landscape."); });
        act(setup, "page-size",    "Size",         "Page size", [this]{ featureInfo("Size", "PDF/print currently uses A4."); });
        act(setup, "print-titles", "Print\nTitles","Repeat header rows", [this]{ featureInfo("Print Titles", "Repeating print titles aren't supported yet."); });

        QHBoxLayout* opts = group(pl, "Sheet Options");
        act(opts, "header-footer", "Header\n& Footer", "Header and footer", [this]{ featureInfo("Header & Footer", "Headers/footers aren't supported yet."); });
        auto* col = checkColumn(opts);
        auto* glChk = new QCheckBox("Gridlines", m_ribbon);
        glChk->setChecked(true);
        connect(glChk, &QCheckBox::toggled, this, &CalcModule::onToggleGridlines);
        col->addWidget(glChk);
        auto* hdChk = new QCheckBox("Headings", m_ribbon);
        hdChk->setChecked(true);
        connect(hdChk, &QCheckBox::toggled, this, &CalcModule::onToggleHeadings);
        col->addWidget(hdChk);

        QHBoxLayout* brk = group(pl, "Page Break");
        act(brk, "page-break",   "Break\nPreview", "Page break preview", [this]{ printPreview(); });
        act(brk, "insert-break", "Insert\nBreak",  "Insert a page break", [this]{ featureInfo("Insert Page Break", "Manual page breaks aren't supported yet."); });

        QHBoxLayout* themesG = group(pl, "Themes");
        act(themesG, "themes",     "Themes",     "Workbook theme", [this]{ featureInfo("Themes", "Workbook themes aren't supported yet."); });
        act(themesG, "background", "Background", "Set the sheet background colour", [this]{ setSheetBackground(); });

        pl->addStretch(1);
    }

    // ══ FORMULAS PAGE ════════════════════════════════════════════════════════
    {
        QHBoxLayout* pl = newPage();

        QHBoxLayout* lib = group(pl, "Function Library");
        act(lib, "fx", "Insert\nFunction", "Start a formula", [this]{
            m_formulaBar->setText("="); m_formulaBar->setFocus();
            m_formulaBar->setCursorPosition(1);
        });
        {
            auto* sum = big("autosum", "AutoSum", "AutoSum");
            sum->setPopupMode(QToolButton::MenuButtonPopup);
            connect(sum, &QToolButton::clicked, this, [this]{ insertFunction("SUM"); });
            auto* m = new QMenu(sum);
            for (const char* fn : {"SUM", "AVERAGE", "COUNT", "MAX", "MIN"}) {
                const QString name = QString::fromUtf8(fn);
                connect(mi(m, "sigma", name), &QAction::triggered, this, [this, name]{ insertFunction(name); });
            }
            sum->setMenu(m);
            lib->addWidget(sum);
        }
        fnBig(lib, "recently-used", "Recent",    "Recently Used", {"SUM","AVERAGE","IF","COUNT","MAX"});
        fnBig(lib, "fn-financial",  "Financial", "Financial",     {"PMT","FV","PV","NPV","RATE","IRR"});
        fnBig(lib, "fn-logical",    "Logical",   "Logical",       {"IF","AND","OR","NOT","IFERROR","TRUE","FALSE"});
        fnBig(lib, "fn-text",       "Text",      "Text",          {"CONCATENATE","LEFT","RIGHT","MID","LEN","UPPER","LOWER","TRIM","FIND","SEARCH","SUBSTITUTE","TEXT"});
        fnBig(lib, "fn-datetime",   "Date &\nTime", "Date & Time",{"TODAY","NOW","DATE","DAY","MONTH","YEAR","WEEKDAY"});
        fnBig(lib, "fn-lookup",     "Lookup",    "Lookup & Reference", {"VLOOKUP","HLOOKUP","INDEX","MATCH","LOOKUP"});
        fnBig(lib, "fn-math",       "Math",      "Math & Trig",   {"SUM","ABS","SQRT","POWER","ROUND","ROUNDUP","ROUNDDOWN","MOD","PRODUCT","SUMIF","INT"});
        fnBig(lib, "fn-more",       "More",      "More Functions",{"AVERAGE","COUNT","COUNTA","COUNTIF","MIN","MAX","SUBTOTAL"});

        QHBoxLayout* names = group(pl, "Defined Names");
        act(names, "name-manager",   "Name\nManager", "Manage defined names", [this]{ nameManager(); });
        act(names, "define-name",    "Define\nName",  "Name the current selection", [this]{ defineName(); });
        act(names, "use-in-formula", "Use in\nFormula","Insert a name into the formula bar", [this]{ useNameInFormula(); });

        QHBoxLayout* audit = group(pl, "Formula Auditing");
        act(audit, "trace-prec",    "Trace\nPrec.", "Select cells this formula refers to", [this]{ traceReferences(false); });
        act(audit, "trace-dep",     "Trace\nDep.",  "Select cells that refer to this cell", [this]{ traceReferences(true); });
        act(audit, "remove-arrows", "Remove\nArrows","Clear the trace selection", [this]{ removeTraceArrows(); });
        m_showFormulasBtn = big("show-formulas", "Show\nFormulas", "Toggle raw formulas / results", true);
        connect(m_showFormulasBtn, &QToolButton::toggled, this, &CalcModule::onShowFormulasToggled);
        audit->addWidget(m_showFormulasBtn);
        act(audit, "error-check", "Error\nCheck",  "Find the first error on the sheet", [this]{ errorCheck(); });
        act(audit, "evaluate",    "Evaluate",      "Show the active formula's result", [this]{ evaluateFormula(); });

        QHBoxLayout* calc = group(pl, "Calculation");
        act(calc, "calc-now",   "Calculate\nNow",  "Recompute every formula (F9)", [this]{ onCalculateNow(); });
        act(calc, "calc-sheet", "Calc\nSheet",     "Recompute this sheet", [this]{ onCalculateNow(); });

        pl->addStretch(1);
    }

    // ══ DATA PAGE ════════════════════════════════════════════════════════════
    {
        QHBoxLayout* pl = newPage();

        QHBoxLayout* sortG = group(pl, "Sort & Filter");
        act(sortG, "sort-az", "Sort ↑",  "Sort ascending by the active column", [this]{ sortByColumn(true); });
        act(sortG, "sort-za", "Sort ↓",  "Sort descending by the active column", [this]{ sortByColumn(false); });
        act(sortG, "filter",  "Filter",  "Filter the active column by value", [this]{ showColumnFilter(); });
        act(sortG, "show-all","Show\nAll","Clear all filters", [this]{ clearFilters(); });
        act(sortG, "reapply", "Reapply", "Re-apply the current filters", [this]{ applyFilters(); });

        QHBoxLayout* dt = group(pl, "Data Tools");
        act(dt, "clear", "Data\nCleanser", "Trim, de-duplicate, standardize dates, extract emails and URLs",
            [this]{ showDataCleanser(); });
        act(dt, "calc-sheet", "SQL", "Query your sheets with SQL",
            [this]{ showSqlPanel(); });
        act(dt, "auto-backup", "Version\nHistory", "Save, restore and compare versions of this workbook",
            [this]{ showHistoryPanel(); });
        act(dt, "highlight-dup", "Highlight\nDup.", "Highlight duplicate values in the selection", [this]{ highlightDuplicates(); });
        act(dt, "manage-dup",    "Remove\nDup.",    "Remove duplicate rows", [this]{ removeDuplicates(); });
        act(dt, "text-columns",  "Text to\nCols",   "Split the active column by a delimiter", [this]{ textToColumns(); });
        act(dt, "validation",    "Validation",      "Set a drop-down list for the column", [this]{ setDropdownValidation(); });
        {
            auto* fill = big("fill", "Fill", "Fill down or right");
            fill->setPopupMode(QToolButton::InstantPopup);
            auto* m = new QMenu(fill);
            connect(mi(m, "fill", "Fill Down (Ctrl+D)"),  &QAction::triggered, this, [this]{ fillDown(); });
            connect(mi(m, "fill", "Fill Right (Ctrl+R)"), &QAction::triggered, this, [this]{ fillRight(); });
            fill->setMenu(m);
            dt->addWidget(fill);
        }

        QHBoxLayout* outline = group(pl, "Outline");
        act(outline, "group",    "Group",   "Hide the selected rows", [this]{ hideSelectedRows(); });
        act(outline, "ungroup",  "Ungroup", "Show all hidden rows", [this]{ unhideAllRows(); });
        act(outline, "subtotal", "Subtotal","Automatic subtotals (not yet)", [this]{ featureInfo("Subtotal", "Automatic subtotals aren't supported yet."); });

        QHBoxLayout* getG = group(pl, "Get & Transform");
        act(getG, "get-data", "Get\nData",   "Import a CSV into the sheet", [this]{ importData(); });
        act(getG, "table", "JSON /\nYAML", "Paste JSON or YAML and turn it into a table",
            [this]{ importStructuredData(); });
        act(getG, "table", "Copy as\nJSON", "Copy the selection as a JSON array (first row = field names)",
            [this]{ copySelectionAsJson(); });
        act(getG, "table", "Copy as\nYAML", "Copy the selection as YAML (first row = field names)",
            [this]{ copySelectionAsYaml(); });
        act(getG, "refresh",  "Refresh\nAll", "Recompute all formulas", [this]{ onCalculateNow(); });
        act(getG, "what-if",  "What-If",      "Goal Seek / scenarios (not yet)", [this]{ featureInfo("What-If Analysis", "Goal Seek / scenarios aren't supported yet."); });

        QHBoxLayout* findG = group(pl, "Find");
        act(findG, "find", "Find", "Find (Ctrl+F)", [this]{ showFindDialog(); });

        pl->addStretch(1);
    }

    // ══ REVIEW PAGE ════════════════════════════════════════════════════════════
    {
        QHBoxLayout* pl = newPage();

        QHBoxLayout* proof = group(pl, "Proofing");
        act(proof, "spelling",  "Spelling",  "Spell check (not in this build)", [this]{ featureInfo("Spelling", "A spell-checker isn't included in this build."); });
        act(proof, "thesaurus", "Thesaurus", "Thesaurus (not in this build)", [this]{ featureInfo("Thesaurus", "A thesaurus isn't included in this build."); });

        QHBoxLayout* cm = group(pl, "Comments");
        act(cm, "new-comment",    "New\nComment", "Add or edit a comment on the cell", [this]{ addEditComment(); });
        act(cm, "delete-comment", "Delete",       "Delete the cell's comment", [this]{ deleteComment(); });
        act(cm, "prev",           "Previous",     "Go to the previous comment", [this]{ gotoComment(false); });
        act(cm, "next",           "Next",         "Go to the next comment", [this]{ gotoComment(true); });
        act(cm, "show-comments",  "Show",         "Show/hide comment markers", [this]{ toggleShowComments(); });

        QHBoxLayout* protect = group(pl, "Protect");
        act(protect, "protect-sheet", "Protect\nSheet", "Make the sheet read-only", [this]{ toggleProtectSheet(); });
        act(protect, "lock-cell",     "Lock\nCell",     "Toggle sheet protection", [this]{ toggleProtectSheet(); });
        act(protect, "protect-book",  "Protect\nBook",  "Workbook protection (not yet)", [this]{ featureInfo("Protect Workbook", "Workbook-structure protection isn't supported yet."); });
        act(protect, "share",         "Share",          "Co-authoring (needs an online service)", [this]{ featureInfo("Share Workbook", "Co-authoring needs an online service not in this build."); });

        pl->addStretch(1);
    }

    // ══ VIEW PAGE ══════════════════════════════════════════════════════════════
    {
        QHBoxLayout* pl = newPage();

        QHBoxLayout* views = group(pl, "Workbook Views");
        act(views, "view-normal",     "Normal",          "Reset zoom to 100%", [this]{ m_zoom = 1.0; resetZoom(); });
        act(views, "view-pagelayout", "Page\nLayout",    "Print-style preview", [this]{ printPreview(); });
        act(views, "eye",             "Eye\nProtection", "Tint the sheet a soft green", [this]{ toggleEyeProtection(); });
        act(views, "highlight-rc",    "Highlight\nRow/Col","Highlight the active row & column header", [this]{ toggleHighlightActive(); });

        QHBoxLayout* show = group(pl, "Show");
        auto* sc = checkColumn(show);
        auto* fbChk = new QCheckBox("Formula Bar", m_ribbon);
        fbChk->setChecked(true);
        connect(fbChk, &QCheckBox::toggled, this, [this](bool on){
            if (m_formulaBar && m_formulaBar->parentWidget())
                m_formulaBar->parentWidget()->setVisible(on);
        });
        sc->addWidget(fbChk);
        auto* glChk2 = new QCheckBox("Gridlines", m_ribbon);
        glChk2->setChecked(true);
        connect(glChk2, &QCheckBox::toggled, this, &CalcModule::onToggleGridlines);
        sc->addWidget(glChk2);
        auto* hdChk2 = new QCheckBox("Headings", m_ribbon);
        hdChk2->setChecked(true);
        connect(hdChk2, &QCheckBox::toggled, this, &CalcModule::onToggleHeadings);
        sc->addWidget(hdChk2);

        QHBoxLayout* zoom = group(pl, "Zoom");
        act(zoom, "zoom-in",  "Zoom\nIn",  "Zoom in", [this]{ zoomBy(+10); });
        act(zoom, "zoom-out", "Zoom\nOut", "Zoom out", [this]{ zoomBy(-10); });
        act(zoom, "zoom-100", "100%",      "Reset zoom", [this]{ m_zoom = 1.0; resetZoom(); });

        QHBoxLayout* winG = group(pl, "Window");
        {
            auto* fz = big("freeze", "Freeze\nPanes", "Keep rows/columns visible while scrolling");
            fz->setPopupMode(QToolButton::InstantPopup);
            auto* m = new QMenu(fz);
            connect(mi(m, "freeze", "Freeze Panes (at selection)"), &QAction::triggered, this, [this]{
                const QModelIndex c = m_tableView->currentIndex();
                setFreeze(c.isValid() ? c.row() : 0, c.isValid() ? c.column() : 0);
            });
            connect(mi(m, "freeze", "Freeze Top Row"),      &QAction::triggered, this, [this]{ setFreeze(1, 0); });
            connect(mi(m, "freeze", "Freeze First Column"), &QAction::triggered, this, [this]{ setFreeze(0, 1); });
            m->addSeparator();
            connect(mi(m, "clear",  "Unfreeze Panes"),      &QAction::triggered, this, [this]{ setFreeze(0, 0); });
            fz->setMenu(m);
            winG->addWidget(fz);
        }
        {
            auto* fsBtn = big("full-screen", "Full\nScreen", "Toggle full screen", true);
            connect(fsBtn, &QToolButton::toggled, this, [this](bool on){
                if (auto* w = this->window()) { if (on) w->showFullScreen(); else w->showNormal(); } });
            winG->addWidget(fsBtn);
        }
        act(winG, "new-window", "New\nWindow", "Open another NativeOffice window", [this]{ openNewWindow(); });

        pl->addStretch(1);
    }

    // ══ TOOLS PAGE ══════════════════════════════════════════════════════════════
    {
        QHBoxLayout* pl = newPage();

        QHBoxLayout* conv = group(pl, "Convert & Export");
        act(conv, "export-pdf",   "Export\nto PDF", "Export the sheet to a PDF file", [this]{ exportToPdf(); });
        act(conv, "export-pic",   "Export\nPicture","Export the sheet as a PNG image", [this]{ exportToImage(); });
        act(conv, "extract-text", "Extract\nText",  "Export the sheet as a .txt file", [this]{ exportToText(); });

        QHBoxLayout* sheetT = group(pl, "Sheet");
        act(sheetT, "merge-sheet", "Merge\nSheets", "Combine every sheet into a new one", [this]{ mergeAllSheets(); });
        act(sheetT, "split-sheet", "Split\nSheet",  "Split a sheet into files (not yet)", [this]{ featureInfo("Split Sheet", "Splitting a sheet into files isn't supported yet."); });

        pl->addStretch(1);
    }

    // ══ SMART TOOLBOX PAGE ════════════════════════════════════════════════════════
    {
        QHBoxLayout* pl = newPage();

        QHBoxLayout* sb = group(pl, "Smart Toolbox");
        act(sb, "smart-insert", "Insert\nRow", "Insert a row at the cursor", [this]{ const QModelIndex c = m_tableView->currentIndex(); m_model->insertRowAt(c.isValid()?c.row():0); });
        act(sb, "fill",         "Fill\nDown",  "Fill down", [this]{ fillDown(); });
        act(sb, "delete-cells", "Delete",      "Delete the selection", [this]{ deleteSelection(); });
        act(sb, "format-table", "Format\nTable","Format the selection as a table", [this]{ formatAsTable(); });
        act(sb, "calculator",   "Calculate",   "Recompute formulas", [this]{ onCalculateNow(); });

        QHBoxLayout* adv = group(pl, "Advanced");
        act(adv, "manage-dup",   "Remove\nDup.",  "Remove duplicate rows", [this]{ removeDuplicates(); });
        act(adv, "highlight-dup","Highlight\nDup.","Highlight duplicates", [this]{ highlightDuplicates(); });
        act(adv, "text-columns", "Text to\nCols", "Split a column by a delimiter", [this]{ textToColumns(); });
        act(adv, "merge-sheet",  "Merge\nSheets", "Combine all sheets", [this]{ mergeAllSheets(); });

        pl->addStretch(1);
    }

    // Keyboard shortcuts for the toggles (Ctrl+B/I/U).
    auto addToggleSc = [this](const QKeySequence& k, QToolButton* b) {
        auto* sc = new QShortcut(k, this);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, b, [b]{ b->toggle(); });
    };
    addToggleSc(QKeySequence("Ctrl+B"), m_boldBtn);
    addToggleSc(QKeySequence("Ctrl+I"), m_italicBtn);
    addToggleSc(QKeySequence("Ctrl+U"), m_underlineBtn);

}

// Insert =FN(range) into the active cell, auto-detecting a contiguous numeric
// run directly above (preferred) or to the left of the cell.
void CalcModule::insertFunction(const QString& fn) {
    const QModelIndex cur = m_tableView->currentIndex();
    if (!cur.isValid()) return;
    const int col = cur.column(), row = cur.row();

    auto numeric = [this](int c, int r) {
        const QString d = m_model->displayValue(c, r);
        if (d.isEmpty()) return false;
        bool ok = false; d.toDouble(&ok); return ok;
    };

    QString formula;
    int top = row;
    while (top - 1 >= 0 && numeric(col, top - 1)) --top;
    if (top < row) {
        formula = QString("=%1(%2:%3)").arg(fn,
            FormulaEngine::cellAddress(col, top), FormulaEngine::cellAddress(col, row - 1));
    } else {
        int left = col;
        while (left - 1 >= 0 && numeric(left - 1, row)) --left;
        formula = (left < col)
            ? QString("=%1(%2:%3)").arg(fn, FormulaEngine::cellAddress(left, row),
                                            FormulaEngine::cellAddress(col - 1, row))
            : QString("=%1()").arg(fn);
    }
    m_model->setCellContent(col, row, formula, "Insert Function");
    onSelectionChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
// Charts
// ─────────────────────────────────────────────────────────────────────────────
ChartObject* CalcModule::createChartObject(const ChartSpec& spec) {
    auto* obj = new ChartObject(m_model, spec, m_tableView->viewport());
    // An imported chart often plots a different sheet than the one it sits on.
    obj->setSheetResolver([this](const QString& name) -> SpreadsheetModel* {
        return sheetByName(name);
    });
    // Placement comes from the anchor when the chart has one; a chart made in
    // the app before anchors existed still has only pixel geometry.
    if (spec.anchor.hasFrom() && spec.frac != QRectF(0, 0, 1, 1))
        m_objFrac.insert(obj, spec.frac);
    m_objFallback.insert(obj, spec.geom);
    obj->setGeometry(placedGeometry(obj, spec.anchor));
    obj->rebuild();     // resolver and geometry are both set now
    obj->show();
    obj->raise();
    connect(obj, &ChartObject::closed, this, [this](ChartObject* c){
        m_chartObjs.removeAll(c);
        m_objAnchors.remove(c);
        c->deleteLater();
        syncChartSpecs();
        markDirty();
    });
    connect(obj, &ChartObject::geometryEdited, this, [this, obj]{
        anchorWidget(obj);          // re-anchor after a drag/resize
        syncChartSpecs();
        markDirty();
    });
    connect(obj, &ChartObject::selected, this, [this](ChartObject* c){ selectFloatingObject(c); });
    m_chartObjs.push_back(obj);
    if (spec.anchor.hasFrom()) m_objAnchors.insert(obj, spec.anchor);
    else                       anchorWidget(obj);
    return obj;
}

void CalcModule::insertChart(ChartType type) {
    const QRect r = selectedRect();
    if (r.width() == 0 || r.height() == 0) return;

    ChartSpec spec;
    spec.type  = type;
    spec.range = r;
    // Place the new chart near the top-left of the visible viewport.
    const int vw = m_tableView->viewport()->width();
    const int w = std::min(440, std::max(260, vw - 80));
    spec.geom = QRect(36 + 18 * m_chartObjs.size() % 120,
                      28 + 18 * m_chartObjs.size() % 120, w, 290);

    ChartObject* obj = createChartObject(spec);
    selectFloatingObject(obj);     // newly inserted object starts selected
    syncChartSpecs();
    markDirty();
}

void CalcModule::addChartAt(ChartType type, const QRect& range, const QRect& geom) {
    if (range.width() <= 0 || range.height() <= 0) return;
    ChartSpec spec;
    spec.type  = type;
    spec.range = range;
    spec.geom  = geom;
    createChartObject(spec);
    syncChartSpecs();
}

void CalcModule::addChartExplicit(ChartType type, const QRect& catRange,
                                  const QRect& valRange, const QString& seriesName,
                                  const QString& title, const QRect& geom) {
    if (catRange.isNull() || valRange.isNull()) return;
    ChartSpec spec;
    spec.type     = type;
    spec.geom     = geom;
    spec.title    = title;
    spec.hasTitle = !title.isEmpty();
    spec.catRange = catRange;
    spec.showLegend = true;
    spec.legendPos  = 'b';
    spec.showDataLabels = true;

    ChartSeries ser;
    ser.name     = seriesName;
    ser.valRange = valRange;
    spec.series.push_back(ser);

    createChartObject(spec);
    syncChartSpecs();
}

void CalcModule::setTemplateColumnWidths(const QHash<int, int>& widthsPx) {
    if (!m_model) return;
    for (auto it = widthsPx.begin(); it != widthsPx.end(); ++it)
        m_model->setColWidth(it.key(), it.value());
    applySizes();
}

void CalcModule::rebuildChartObjects() {
    // Tear down the live widgets for the previous sheet.
    for (ChartObject* c : m_chartObjs) { m_objAnchors.remove(c); c->deleteLater(); }
    m_chartObjs.clear();
    if (!m_model) return;
    for (const ChartSpec& s : m_model->charts())
        createChartObject(s);
    rebuildImageObjects();
    // Shapes last so they sit over the charts and pictures they annotate,
    // which is the order the file draws them in.
    rebuildShapeObjects();
}

// ── Pictures on the sheet ───────────────────────────────────────────────────
QWidget* CalcModule::createImageObject(const SheetImage& img) {
    QPixmap pm;
    if (!pm.loadFromData(img.data)) return nullptr;

    auto* lbl = new QLabel;
    lbl->setPixmap(pm);
    lbl->setScaledContents(true);
    lbl->setStyleSheet("background:transparent;");
    lbl->setAttribute(Qt::WA_TransparentForMouseEvents);   // body drags the object

    // Where it goes: the anchor if it has one, otherwise its own size.
    const QRect fallback = img.geom.isEmpty()
                               ? QRect(40, 30, pm.width(), pm.height())
                               : img.geom;
    auto* item = new FloatingItem(lbl, fallback, m_tableView->viewport());
    if (img.frac != QRectF(0, 0, 1, 1)) m_objFrac.insert(item, img.frac);
    m_objFallback.insert(item, fallback);
    const QRect g = img.anchor.hasFrom() ? placedGeometry(item, img.anchor) : fallback;
    // An imported picture is often a small icon; the default floor is sized for
    // a text box and would inflate it.
    item->setMinimumSize(8, 8);
    item->setGeometry(g);
    item->show(); item->raise();
    m_floatingItems.push_back(item);
    m_imageData.insert(item, img.data);
    if (img.fromFile) m_objFromFile.insert(item);
    if (img.anchor.hasFrom()) m_objAnchors.insert(item, img.anchor);
    else                      anchorWidget(item);
    item->onMoved = [this, item]{
        // Once the user has moved it by hand it is no longer positioned by the
        // group it came from, so the sub-rect has to go or the next reposition
        // would snap it back inside the group's old share of the anchor.
        m_objFrac.remove(item);
        m_objFallback.remove(item);
        anchorWidget(item); syncImageSpecs(); markDirty();
    };
    item->onSelected = [this](QWidget* o){ selectFloatingObject(o); };
    connect(item, &QObject::destroyed, this, [this](QObject* o){
        auto* w = static_cast<QWidget*>(o);
        m_objAnchors.remove(w);
        m_objFrac.remove(w);
        m_objFallback.remove(w);
        m_imageData.remove(w);
        m_objFromFile.remove(w);
        m_floatingItems.removeAll(w);
    });
    return item;
}

void CalcModule::rebuildImageObjects() {
    for (QWidget* w : m_floatingItems) { m_objAnchors.remove(w); w->deleteLater(); }
    m_floatingItems.clear();
    m_imageData.clear();
    m_objFromFile.clear();
    if (!m_model) return;
    for (const SheetImage& img : m_model->images())
        createImageObject(img);
}

// ── Shapes on the sheet ─────────────────────────────────────────────────────
// What a cell reference like "$AP$26" or "'Sheet 2'!$B$4" currently displays.
//
// Used by linked text boxes. Returns an empty string when the reference cannot
// be read, which the caller treats as "leave the shape as it is" rather than as
// an error: a broken link should not blank a shape that has something to show.
QString CalcModule::valueAtRef(const QString& ref) const {
    QString body = ref;
    QString sheet;
    const int bang = ref.lastIndexOf(QLatin1Char('!'));
    if (bang >= 0) {
        sheet = ref.left(bang);
        body  = ref.mid(bang + 1);
        if (sheet.size() >= 2 && sheet.startsWith(QLatin1Char('\''))
            && sheet.endsWith(QLatin1Char('\''))) {
            sheet = sheet.mid(1, sheet.size() - 2);
            sheet.replace(QLatin1String("''"), QLatin1String("'"));
        }
    }
    body.remove(QLatin1Char('$'));
    if (body.isEmpty()) return {};

    int i = 0, col = 0;
    while (i < body.size() && body.at(i).isLetter()) {
        col = col * 26 + (body.at(i).toUpper().toLatin1() - 'A' + 1);
        ++i;
    }
    if (i == 0) return {};
    bool ok = false;
    const int row = body.mid(i).toInt(&ok);
    if (!ok || row < 1) return {};

    const SpreadsheetModel* m = sheet.isEmpty() ? m_model : sheetByName(sheet);
    if (!m) m = m_model;
    if (!m) return {};
    // Through the model's DisplayRole, not displayValue(): the number format is
    // applied there, and a linked text box showing a ratio has to read "83%"
    // the way the sheet does, not "0.826086956521739".
    return m->index(row - 1, col - 1).data(Qt::DisplayRole).toString();
}

void CalcModule::rebuildShapeObjects() {
    for (ShapeObject* s : m_shapeObjs) {
        m_objAnchors.remove(s);
        m_objFrac.remove(s);
        m_objFallback.remove(s);
        s->deleteLater();
    }
    m_shapeObjs.clear();
    if (!m_model) return;

    for (const SheetShape& shIn : m_model->shapes()) {
        SheetShape sh = shIn;
        // A linked text box carries a cell reference instead of its text. It is
        // resolved here rather than in the parser because only the workbook can
        // say what the cell holds, and the reference may name another sheet.
        if (!sh.textLink.isEmpty() && !sh.text.isEmpty()) {
            const QString shown = valueAtRef(sh.textLink);
            if (!shown.isEmpty()) sh.text[0].runs[0].text = shown;
        }
        auto* w = new ShapeObject(sh, m_tableView->viewport());
        const QRect fallback = sh.geom.isEmpty() ? QRect(40, 30, 220, 90) : sh.geom;
        m_objAnchors.insert(w, sh.anchor);
        m_objFallback.insert(w, fallback);
        if (sh.frac != QRectF(0, 0, 1, 1)) m_objFrac.insert(w, sh.frac);
        w->setZoom(m_zoom > 0 ? m_zoom : 1.0);
        w->setGeometry(sh.anchor.hasFrom() ? placedGeometry(w, sh.anchor) : fallback);
        w->show();
        m_shapeObjs.push_back(w);
        connect(w, &QObject::destroyed, this, [this](QObject* o) {
            auto* victim = static_cast<QWidget*>(o);
            m_objAnchors.remove(victim);
            m_objFrac.remove(victim);
            m_objFallback.remove(victim);
        });
    }
}

// Dev capture aid: bring a cell into view so an object anchored below the fold
// can be captured where it actually sits.
void CalcModule::scrollToCell(int col, int row) {
    if (!m_model || !m_tableView) return;
    // PositionAtTop covers the vertical axis; the horizontal one is handled by
    // the same call, so the scrollbar must not be nudged again afterwards (in
    // per-item mode its value is an index, not a pixel offset).
    m_tableView->scrollTo(m_model->index(row, col), QAbstractItemView::PositionAtTop);
    repositionFloatingObjects();
}

void CalcModule::devCtrlWheel(int notches) {
    if (!m_tableView) return;
    QWidget* vp = m_tableView->viewport();
    const QPoint  centre = vp->rect().center();
    const QPoint  global = vp->mapToGlobal(centre);

    // Deliver it the way the window system does: to whatever widget is actually
    // on top at that point, not straight to the viewport. If an overlay or a
    // floating object is intercepting the wheel, this is what shows it up.
    QWidget* target = QApplication::widgetAt(global);
    if (!target) target = vp;
    qWarning("[calc] wheel target: %s (%s)%s",
             target->metaObject()->className(),
             qUtf8Printable(target->objectName().isEmpty()
                                ? QStringLiteral("-") : target->objectName()),
             target == vp ? "  == viewport" : "  != viewport");

    const QPointF local = target->mapFromGlobal(global);
    QWheelEvent we(local, global, QPoint(0, 0),
                   QPoint(0, notches * 120), Qt::NoButton,
                   Qt::ControlModifier, Qt::NoScrollPhase, false);
    const bool handled = QApplication::sendEvent(target, &we);
    qWarning("[calc] wheel sent, handled=%d accepted=%d zoom now %.2f",
             int(handled), int(we.isAccepted()), m_zoom);
}

// Dev aid: dump one cell's raw content and what it evaluates to.
void CalcModule::dumpCell(const QString& ref) const {
    if (!m_model) return;
    int col = 0, row = 0, i = 0, c = 0;
    while (i < ref.size() && ref[i].isLetter()) { c = c*26 + (ref[i].toUpper().toLatin1()-'A'+1); ++i; }
    col = c - 1; row = ref.mid(i).toInt() - 1;
    if (col < 0 || row < 0) return;
    qWarning("[cell] %s raw=<%s> shown=<%s>",
             qUtf8Printable(ref),
             qUtf8Printable(m_model->rawContent(col, row)),
             qUtf8Printable(m_model->displayValue(col, row)));
}

// How many drawn objects an .xlsx write would leave behind.
//
// Charts and pictures are both written now, whichever path the save takes: one
// the app added gets its parts built for it, and the file's own are kept in the
// package a preserving save puts back (or rebuilt when it cannot be). The only
// chart that does not survive is one that plots nothing.
//
// Drawn shapes are the exception, and they can only ever be the file's own: the
// Shapes and Icons buttons insert a picture, so the app never makes one. They
// survive by putting the package back and are lost when it cannot be.
int CalcModule::objectsLostOnXlsxSave() const {
    std::vector<XlsxSheet> out;
    buildXlsxSheets(out);
    const bool preserved = canPreserveXlsx(out, m_originalXlsx);

    int lost = 0;
    for (const XlsxSheet& xs : out)
        for (const ChartSpec& c : xs.charts)
            if (!chartIsWritable(c, xs.cellText)) ++lost;
    if (!preserved)
        for (SpreadsheetModel* m : m_sheets)
            if (m) lost += int(m->shapes().size());
    return lost;
}

// Dev capture aid: what is actually sitting on the active sheet right now.
// Reports the live widgets, not the model, so an object that failed to be
// created or landed off-screen shows up as such rather than being assumed fine.
void CalcModule::dumpSheetObjects() const {
    const QSize vp = m_tableView ? m_tableView->viewport()->size() : QSize();
    qWarning("[calc] grid %d cols x %d rows (last cell %s%d)",
             SpreadsheetModel::NUM_COLS, SpreadsheetModel::NUM_ROWS,
             qUtf8Printable(QStringLiteral("XFD")), SpreadsheetModel::NUM_ROWS);
    qWarning("[calc] sheet %d of %d  viewport %dx%d  zoom %.2f",
             m_activeSheet + 1, int(m_sheets.size()), vp.width(), vp.height(), m_zoom);
    qWarning("[calc] model: %d chart(s), %d image(s), %d shape(s)",
             m_model ? m_model->charts().size() : 0,
             m_model ? m_model->images().size() : 0,
             m_model ? m_model->shapes().size() : 0);
    {
        std::vector<XlsxSheet> probe;
        buildXlsxSheets(probe);
        qWarning("[calc] .xlsx save: package preserved %s, %d object(s) lost",
                 canPreserveXlsx(probe, m_originalXlsx) ? "yes" : "no",
                 objectsLostOnXlsxSave());
    }

    int onScreen = 0;
    for (ChartObject* c : m_chartObjs) {
        const QRect g = c->geometry();
        const bool vis = c->isVisible() && g.intersects(QRect(QPoint(0, 0), vp));
        if (vis) ++onScreen;
        const CellAnchor a = c->anchor();
        qWarning("[calc]   chart %-9s geom %4d,%4d %4dx%-4d anchor (%d,%d)->(%d,%d) %s",
                 qUtf8Printable(chartTypeName(c->type())),
                 g.x(), g.y(), g.width(), g.height(),
                 a.fromCol, a.fromRow, a.toCol, a.toRow,
                 vis ? "VISIBLE" : "offscreen");
    }
    for (QWidget* w : m_floatingItems) {
        if (!m_imageData.contains(w)) continue;
        const QRect g = w->geometry();
        const bool vis = w->isVisible() && g.intersects(QRect(QPoint(0, 0), vp));
        if (vis) ++onScreen;
        const CellAnchor a = m_objAnchors.value(w);
        qWarning("[calc]   image           geom %4d,%4d %4dx%-4d anchor (%d,%d)->(%d,%d) %s",
                 g.x(), g.y(), g.width(), g.height(),
                 a.fromCol, a.fromRow, a.toCol, a.toRow,
                 vis ? "VISIBLE" : "offscreen");
    }
    for (ShapeObject* w : m_shapeObjs) {
        const QRect g = w->geometry();
        const bool vis = w->isVisible() && g.intersects(QRect(QPoint(0, 0), vp));
        if (vis) ++onScreen;
        const SheetShape& sh = w->shape();
        QString caption;
        for (const ShapeParagraph& para : sh.text)
            for (const ShapeRun& run : para.runs) caption += run.text;
        caption = caption.simplified().left(24);
        qWarning("[calc]   shape g%-2d       geom %4d,%4d %4dx%-4d fill %-9s line %-9s %s %s",
                 int(sh.preset), g.x(), g.y(), g.width(), g.height(),
                 qUtf8Printable(sh.fill.isValid() ? sh.fill.name()
                                : (sh.gradient.isEmpty() ? QStringLiteral("none")
                                                         : sh.gradient.first().name() + QStringLiteral("~"))),
                 qUtf8Printable(sh.line.isValid() ? sh.line.name() : QStringLiteral("none")),
                 vis ? "VISIBLE  " : "offscreen",
                 qUtf8Printable(caption));
    }
    qWarning("[calc] widgets: %d chart(s), %d picture(s), %d shape(s), %d within the viewport",
             int(m_chartObjs.size()), int(m_imageData.size()),
             int(m_shapeObjs.size()), onScreen);
}

void CalcModule::syncImageSpecs() {
    if (!m_model) return;
    QVector<SheetImage> out;
    for (QWidget* w : m_floatingItems) {
        if (!m_imageData.contains(w)) continue;    // text boxes are not pictures
        SheetImage img;
        img.data     = m_imageData.value(w);
        img.anchor   = m_objAnchors.value(w);
        img.geom     = w->geometry();
        img.fromFile = m_objFromFile.contains(w);
        // A picture that came out of a group covers only part of its anchor.
        // Dropping that here put it back at full size on the next rebuild.
        const QRectF f = m_objFrac.value(w);
        if (!f.isNull() && f.width() > 0 && f.height() > 0) img.frac = f;
        out.push_back(img);
    }
    m_model->setImages(out);
}

void CalcModule::refreshChartsData() {
    for (ChartObject* c : m_chartObjs) c->rebuild();
}

void CalcModule::syncChartSpecs() {
    if (!m_model) return;
    QVector<ChartSpec> specs;
    specs.reserve(m_chartObjs.size());
    for (ChartObject* c : m_chartObjs) specs.push_back(c->spec());
    m_model->setCharts(specs);
}

// ─────────────────────────────────────────────────────────────────────────────
// AutoFilter
// ─────────────────────────────────────────────────────────────────────────────
namespace {
// Used data bounds of the active model (returns false when empty).
bool usedRange(const SpreadsheetModel* m, int& c1, int& r1, int& c2, int& r2) {
    c1 = INT_MAX; r1 = INT_MAX; c2 = -1; r2 = -1;
    const auto& data = m->cells();
    for (auto it = data.begin(); it != data.end(); ++it) {
        const int col = SpreadsheetModel::keyCol(it->first);
        const int row = SpreadsheetModel::keyRow(it->first);
        c1 = std::min(c1, col); c2 = std::max(c2, col);
        r1 = std::min(r1, row); r2 = std::max(r2, row);
    }
    return c2 >= 0;
}
} // namespace

void CalcModule::applyFilters() {
    int c1, r1, c2, r2;
    if (!usedRange(m_model, c1, r1, c2, r2)) return;
    const int headerRow = r1;                       // top used row = header
    // Only the used range can be filtered, and only rows whose state actually
    // changes are touched: setRowHidden relayouts the header every call.
    for (int row = headerRow + 1; row <= r2; ++row) {
        bool hidden = false;
        for (auto it = m_columnFilters.begin(); it != m_columnFilters.end() && !hidden; ++it)
            if (!it.value().contains(m_model->displayValue(it.key(), row)))
                hidden = true;
        if (m_tableView->isRowHidden(row) != hidden) {
            m_tableView->setRowHidden(row, hidden);
            if (hidden) m_hiddenRows.insert(row);
            else        m_hiddenRows.remove(row);
        }
    }
}

void CalcModule::clearFilters() {
    m_columnFilters.clear();
    unhideAllRows();
}

void CalcModule::showColumnFilter() {
    int c1, r1, c2, r2;
    if (!usedRange(m_model, c1, r1, c2, r2)) return;
    const QModelIndex cur = m_tableView->currentIndex();
    const int col = (cur.isValid() && cur.column() >= c1 && cur.column() <= c2)
                        ? cur.column() : c1;
    const int headerRow = r1;

    // Distinct display values below the header.
    QStringList values;
    QSet<QString> seen;
    for (int row = headerRow + 1; row <= r2; ++row) {
        const QString v = m_model->displayValue(col, row);
        if (!seen.contains(v)) { seen.insert(v); values << v; }
    }
    if (values.isEmpty()) return;
    values.sort(Qt::CaseInsensitive);

    const QString colName = m_model->displayValue(col, headerRow);
    QDialog dlg(this);
    dlg.setWindowTitle(QString("Filter — %1").arg(colName.isEmpty()
                       ? FormulaEngine::colLabel(col) : colName));
    auto* v = new QVBoxLayout(&dlg);
    auto* list = new QListWidget(&dlg);
    const QSet<QString> allowed = m_columnFilters.value(col);
    const bool hasFilter = m_columnFilters.contains(col);
    for (const QString& val : values) {
        auto* item = new QListWidgetItem(val.isEmpty() ? "(blank)" : val, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState((!hasFilter || allowed.contains(val)) ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, val);
    }
    v->addWidget(new QLabel("Show rows where the value is:", &dlg));
    v->addWidget(list, 1);
    auto* btnRow = new QHBoxLayout();
    auto* allBtn  = new QPushButton("Select All", &dlg);
    auto* noneBtn = new QPushButton("Clear", &dlg);
    auto* ok      = new QPushButton("OK", &dlg);
    auto* cancel  = new QPushButton("Cancel", &dlg);
    btnRow->addWidget(allBtn); btnRow->addWidget(noneBtn); btnRow->addStretch(1);
    btnRow->addWidget(ok); btnRow->addWidget(cancel);
    v->addLayout(btnRow);
    connect(allBtn,  &QPushButton::clicked, &dlg, [list]{ for (int i=0;i<list->count();++i) list->item(i)->setCheckState(Qt::Checked); });
    connect(noneBtn, &QPushButton::clicked, &dlg, [list]{ for (int i=0;i<list->count();++i) list->item(i)->setCheckState(Qt::Unchecked); });
    connect(ok,     &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    dlg.resize(280, 360);

    if (dlg.exec() != QDialog::Accepted) return;

    QSet<QString> chosen;
    int checked = 0;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->checkState() == Qt::Checked) {
            chosen.insert(list->item(i)->data(Qt::UserRole).toString());
            ++checked;
        }
    if (checked == list->count())
        m_columnFilters.remove(col);        // all selected → no filter on this column
    else
        m_columnFilters.insert(col, chosen);
    applyFilters();
    markDirty();
}

// ─────────────────────────────────────────────────────────────────────────────
// Freeze panes — table views pinned in a strip reserved along the top / left.
// The strip is real space taken out of the grid's viewport (see
// FreezableTableView), not an overlay, so the frozen band never covers the row
// or column sitting underneath it.
// ─────────────────────────────────────────────────────────────────────────────
// Width of the rule drawn along a band's inner edge to mark the freeze.
static constexpr int kFreezeLine = 2;

// A band view is pinned: updateFrozenViews is the only thing allowed to say
// which rows and columns it shows. It shares the grid's selection model so the
// band highlights with it, and that means currentChanged reaches it and it
// scrolls itself to the current cell, which slides the band off its frozen
// rows. Swallowing scrollTo keeps it where it was put.
class FrozenBandView : public QTableView {
public:
    using QTableView::QTableView;
    void scrollTo(const QModelIndex&, ScrollHint = EnsureVisible) override {}
};

QTableView* CalcModule::makeFrozenView(bool rightLine, bool bottomLine) {
    auto* v = new FrozenBandView(m_tableView);
    v->setObjectName("frozenView");
    v->setModel(m_model);
    v->setSelectionModel(m_tableView->selectionModel());
    v->setItemDelegate(new CalcItemDelegate(v));
    v->setFocusPolicy(Qt::NoFocus);
    // Real headers, so a band can carry the row numbers or column letters that
    // belong to it. They stay hidden until updateFrozenViews works out which
    // band owns which strip.
    v->setHorizontalHeader(new CalcHeaderView(Qt::Horizontal, v));
    v->setVerticalHeader  (new CalcHeaderView(Qt::Vertical,   v));
    v->horizontalHeader()->setVisible(false);
    v->verticalHeader()->setVisible(false);
    v->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    v->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    v->setFrameShape(QFrame::NoFrame);
    v->setShowGrid(false);          // the delegate draws the grid, as in the main view

    const QString rule = QStringLiteral("%1px solid #B6BBC2;").arg(kFreezeLine);
    QString css = QStringLiteral(
        "QTableView#frozenView{border:none;gridline-color:#E2E2E2;background:#FFFFFF;");
    if (rightLine)  css += QStringLiteral("border-right:")  + rule;
    if (bottomLine) css += QStringLiteral("border-bottom:") + rule;
    css += QLatin1Char('}');
    v->setStyleSheet(css);
    return v;
}

void CalcModule::setFreeze(int rows, int cols) {
    m_freezeRows = std::max(0, rows);
    m_freezeCols = std::max(0, cols);

    auto drop = [](QTableView*& v){ if (v) { v->deleteLater(); v = nullptr; } };
    if (m_freezeRows == 0 && m_freezeCols == 0) {
        drop(m_frozenTop); drop(m_frozenLeft); drop(m_frozenCorner);
        // Give the reserved strip back to the grid. static_cast, not
        // qobject_cast: m_tableView is always a FreezableTableView (one
        // creation site) and the class is local to this file, so it carries no
        // Q_OBJECT for qobject_cast to use.
        static_cast<FreezableTableView*>(m_tableView)->setFrozenBand(0, 0, 0, 0);
        return;
    }

    if (m_freezeRows > 0 && !m_frozenTop) m_frozenTop = makeFrozenView(false, true);
    if (m_freezeRows == 0) drop(m_frozenTop);
    if (m_freezeCols > 0 && !m_frozenLeft) m_frozenLeft = makeFrozenView(true, false);
    if (m_freezeCols == 0) drop(m_frozenLeft);
    if (m_freezeRows > 0 && m_freezeCols > 0 && !m_frozenCorner) m_frozenCorner = makeFrozenView(true, true);
    if ((m_freezeRows == 0 || m_freezeCols == 0)) drop(m_frozenCorner);

    updateFrozenViews();
}

void CalcModule::updateFrozenViews() {
    if (!m_frozenTop && !m_frozenLeft && !m_frozenCorner) return;

    // Reserving the strip resizes the viewport, which lands here again through
    // the grid's own signals. Both of those moments have to be sat out: while
    // the reserve is in flight the headers are stretched across the strip and
    // read a whole band too large, and placing the bands against that geometry
    // is what made the frozen labels disappear after a window resize. The
    // grid's onBandSettled hook calls back once the geometry is final.
    auto* grid = static_cast<FreezableTableView*>(m_tableView);
    if (grid->isReserving() || m_placingFrozen) return;
    m_placingFrozen = true;
    const QScopeGuard done([this]{ m_placingFrozen = false; });

    // Runs on every scroll, so it copies the defaults and only the sections
    // that actually differ rather than walking the whole grid each time.
    auto syncSizes = [&](QTableView* v){
        v->horizontalHeader()->setDefaultSectionSize(
            m_tableView->horizontalHeader()->defaultSectionSize());
        v->verticalHeader()->setDefaultSectionSize(
            m_tableView->verticalHeader()->defaultSectionSize());
        const auto& cw = m_model->colWidths();
        for (auto it = cw.begin(); it != cw.end(); ++it)
            if (it.key() >= 0 && it.key() < SpreadsheetModel::NUM_COLS)
                v->setColumnWidth(it.key(), m_tableView->columnWidth(it.key()));
        const auto& rh = m_model->rowHeights();
        for (auto it = rh.begin(); it != rh.end(); ++it)
            if (it.key() >= 0 && it.key() < SpreadsheetModel::NUM_ROWS)
                v->setRowHeight(it.key(), m_tableView->rowHeight(it.key()));
    };

    int fcW = 0; for (int c = 0; c < m_freezeCols; ++c) fcW += m_tableView->columnWidth(c);
    int frH = 0; for (int r = 0; r < m_freezeRows; ++r) frH += m_tableView->rowHeight(r);

    // The freeze rule is chrome that needs its own pixels: a band draws it as a
    // border, which insets that band's contents, so it has to be reserved too
    // or the frozen cells come out short and stop lining up with the body.
    const int bandW = fcW ? fcW + kFreezeLine : 0;
    const int bandH = frH ? frH + kFreezeLine : 0;

    // Reserve the strip first, then read the viewport geometry: the bands live
    // in that reserved space, beside the body rather than on top of it, so no
    // row or column is hidden underneath them.
    grid->setFrozenBand(bandW, bandH, m_freezeCols, m_freezeRows);

    const QRect vg = m_tableView->viewport()->geometry();
    const int   vw = m_rowHeader->isHidden() ? 0 : m_rowHeader->width();
    const int   hh = m_colHeader->isHidden() ? 0 : m_colHeader->height();

    // Which view carries the labels for a band:
    //
    //         vw     fcW      body        With only one axis frozen, that
    //      +------+-------+-----------+   band's own view carries them. With
    //   hh | corn | cols  | colHeader |   both, the corner view carries both
    //      +------+-------+-----------+   strips instead, because a band's
    //  frH | rows |corner | frozenTop |   header has to sit against its own
    //      +------+-------+-----------+   cells and the other band would be
    //      | rowH | frzLt | viewport  |   sitting in between.
    //      +------+-------+-----------+
    const bool rowsFrozen = frH > 0;
    const bool colsFrozen = fcW > 0;

    const QModelIndex cur = m_tableView->currentIndex();
    auto syncHighlight = [&](QTableView* v){
        auto* h = static_cast<CalcHeaderView*>(v->horizontalHeader());
        auto* r = static_cast<CalcHeaderView*>(v->verticalHeader());
        if (!h->isHidden()) h->setHighlightedSections({cur.column()});
        if (!r->isHidden()) r->setHighlightedSections({cur.row()});
    };

    if (m_frozenTop) {
        syncSizes(m_frozenTop);
        const bool ownsRowNumbers = !colsFrozen;
        m_frozenTop->verticalHeader()->setVisible(ownsRowNumbers);
        if (ownsRowNumbers) m_frozenTop->verticalHeader()->setFixedWidth(vw);
        m_frozenTop->setGeometry(ownsRowNumbers ? vg.left() - vw   : vg.left(),
                                 vg.top() - bandH,
                                 ownsRowNumbers ? vw + vg.width()  : vg.width(),
                                 bandH);
        m_frozenTop->horizontalScrollBar()->setValue(m_tableView->horizontalScrollBar()->value());
        m_frozenTop->verticalScrollBar()->setValue(0);
        syncHighlight(m_frozenTop);
        m_frozenTop->raise(); m_frozenTop->show();
    }
    if (m_frozenLeft) {
        syncSizes(m_frozenLeft);
        const bool ownsColLetters = !rowsFrozen;
        m_frozenLeft->horizontalHeader()->setVisible(ownsColLetters);
        if (ownsColLetters) m_frozenLeft->horizontalHeader()->setFixedHeight(hh);
        m_frozenLeft->setGeometry(vg.left() - bandW,
                                  ownsColLetters ? vg.top() - hh    : vg.top(),
                                  bandW,
                                  ownsColLetters ? hh + vg.height() : vg.height());
        m_frozenLeft->verticalScrollBar()->setValue(m_tableView->verticalScrollBar()->value());
        m_frozenLeft->horizontalScrollBar()->setValue(0);
        syncHighlight(m_frozenLeft);
        m_frozenLeft->raise(); m_frozenLeft->show();
    }
    if (m_frozenCorner) {
        syncSizes(m_frozenCorner);
        m_frozenCorner->horizontalHeader()->setVisible(true);
        m_frozenCorner->verticalHeader()->setVisible(true);
        m_frozenCorner->horizontalHeader()->setFixedHeight(hh);
        m_frozenCorner->verticalHeader()->setFixedWidth(vw);
        m_frozenCorner->setGeometry(vg.left() - bandW - vw, vg.top() - bandH - hh,
                                    vw + bandW, hh + bandH);
        m_frozenCorner->horizontalScrollBar()->setValue(0);
        m_frozenCorner->verticalScrollBar()->setValue(0);
        syncHighlight(m_frozenCorner);
        m_frozenCorner->raise(); m_frozenCorner->show();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Export / print  (renders the active sheet's used range, fit to one page)

// ─────────────────────────────────────────────────────────────────────────────
// Sprint 28 — real functionality for the former "coming soon" buttons
// ─────────────────────────────────────────────────────────────────────────────
void CalcModule::featureInfo(const QString& name, const QString& detail) {
    QMessageBox::information(this, name, detail);
}

// ── Floating-object anchoring (objects sit ON the cells) ────────────────────
// An object is placed the way a spreadsheet places it: by the cells its two
// corners fall in, not by viewport pixels. That is what makes a chart span a
// block of cells and keep spanning it while the sheet scrolls, its columns are
// resized or its zoom changes.
CellAnchor CalcModule::geometryToAnchor(const QRect& g) const {
    CellAnchor a;
    const double z = m_zoom > 0 ? m_zoom : 1.0;

    auto cellAt = [&](int x, int y, int& col, int& row, int& dx, int& dy) {
        col = m_tableView->columnAt(x);
        row = m_tableView->rowAt(y);
        // A corner past the last populated column/row still has to land
        // somewhere sensible, so it clamps to the edge rather than going -1.
        if (col < 0) col = qMax(0, m_tableView->columnAt(qMax(0, x - 1)));
        if (row < 0) row = qMax(0, m_tableView->rowAt(qMax(0, y - 1)));
        if (col < 0) col = 0;
        if (row < 0) row = 0;
        dx = int((x - m_tableView->horizontalHeader()->sectionViewportPosition(col)) / z);
        dy = int((y - m_tableView->verticalHeader()->sectionViewportPosition(row)) / z);
    };

    cellAt(g.left(),  g.top(),    a.fromCol, a.fromRow, a.fromDx, a.fromDy);
    cellAt(g.right(), g.bottom(), a.toCol,   a.toRow,   a.toDx,   a.toDy);
    return a;
}

QRect CalcModule::anchorGeometry(const CellAnchor& a, const QRect& fallback) const {
    if (!a.hasFrom() || !m_model) return fallback;
    const double z = m_zoom > 0 ? m_zoom : 1.0;

    // Section positions come from the headers, not from columnViewportPosition:
    // the view's helper is only dependable for columns that are currently on
    // screen, and an object anchored off to the right resolved both of its
    // corners to the same x, collapsing it to its minimum size.
    const QHeaderView* hh = m_tableView->horizontalHeader();
    const QHeaderView* vh = m_tableView->verticalHeader();

    const int x = hh->sectionViewportPosition(a.fromCol) + int(a.fromDx * z);
    const int y = vh->sectionViewportPosition(a.fromRow) + int(a.fromDy * z);

    if (a.hasTo()) {
        const int x2 = hh->sectionViewportPosition(a.toCol) + int(a.toDx * z);
        const int y2 = vh->sectionViewportPosition(a.toRow) + int(a.toDy * z);
        // Never collapse to nothing: a corner cell that is scrolled out of
        // reach can report a position that would invert the rectangle.
        return QRect(x, y, qMax(8, x2 - x), qMax(8, y2 - y));
    }
    // One-corner anchor: keep the object's own size, scaled by the zoom.
    const QSize s = fallback.isEmpty() ? QSize(420, 280) : fallback.size();
    return QRect(x, y, int(s.width() * z), int(s.height() * z));
}

// Where an object actually goes. anchorGeometry() gives the whole anchored
// object's box; a member of a group covers only a fraction of that box, and the
// fraction has to be re-applied every time the box changes (scroll, resize,
// zoom) or the group's internal layout comes apart.
QRect CalcModule::placedGeometry(QWidget* w, const CellAnchor& a) const {
    const QRect base = anchorGeometry(a, m_objFallback.value(w, w->geometry()));
    const QRectF f   = m_objFrac.value(w);
    if (f.isNull() || f.width() <= 0 || f.height() <= 0) return base;
    return QRect(base.x() + qRound(f.x() * base.width()),
                 base.y() + qRound(f.y() * base.height()),
                 qMax(2, qRound(f.width()  * base.width())),
                 qMax(2, qRound(f.height() * base.height())));
}

void CalcModule::anchorWidget(QWidget* w) {
    if (!w) return;
    const CellAnchor a = geometryToAnchor(w->geometry());
    m_objAnchors.insert(w, a);
    // Charts carry their anchor in the spec so it is saved with the sheet.
    if (auto* c = qobject_cast<ChartObject*>(w)) c->setAnchor(a);
}

void CalcModule::repositionFloatingObjects() {
    // A shape's caption is sized in points, so it only tracks the sheet if the
    // zoom is pushed into it. This runs on every scroll and zoom, which is
    // exactly when that can have changed.
    const double z = m_zoom > 0 ? m_zoom : 1.0;
    for (ShapeObject* s : m_shapeObjs) if (s) s->setZoom(z);

    for (auto it = m_objAnchors.begin(); it != m_objAnchors.end(); ++it) {
        QWidget* w = it.key();
        if (!w) continue;
        const QRect g = placedGeometry(w, it.value());
        if (g != w->geometry()) w->setGeometry(g);
    }
}

void CalcModule::selectFloatingObject(QWidget* obj) {
    m_selectedObj = obj;
    for (ChartObject* c : m_chartObjs) c->setSelected(c == obj);
    for (QWidget* w : m_floatingItems)
        static_cast<FloatingItem*>(w)->setSelected(w == obj);
}

// Delete on a selected object removes it. Returns false when nothing was
// selected, so the key falls through to clearing the selected cells.
bool CalcModule::deleteSelectedObject() {
    QWidget* obj = m_selectedObj;
    if (!obj) return false;
    m_selectedObj = nullptr;

    if (auto* c = qobject_cast<ChartObject*>(obj)) {
        m_chartObjs.removeAll(c);
        m_objAnchors.remove(c);
        c->deleteLater();
        syncChartSpecs();
    } else {
        m_floatingItems.removeAll(obj);
        m_objAnchors.remove(obj);
        m_imageData.remove(obj);
        obj->deleteLater();
        syncImageSpecs();
    }
    markDirty();
    return true;
}

// ── Insert ───────────────────────────────────────────────────────────────────
void CalcModule::insertImagePixmap(const QPixmap& src) {
    if (src.isNull()) return;
    QPixmap pm = src.scaled(QSize(300, 220), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    auto* lbl = new QLabel;
    lbl->setPixmap(pm);
    lbl->setScaledContents(true);
    lbl->setStyleSheet("background:#FFFFFF;");
    lbl->setAttribute(Qt::WA_TransparentForMouseEvents);   // body drags the object
    const QRect g(40, 30, pm.width() + 4, pm.height() + 4);
    // Keep the encoded bytes so the picture is saved with the sheet and comes
    // back on a sheet switch, rather than living only in this widget.
    QByteArray png;
    { QBuffer buf(&png); buf.open(QIODevice::WriteOnly); pm.save(&buf, "PNG"); }

    auto* item = new FloatingItem(lbl, g, m_tableView->viewport());
    item->show(); item->raise();
    m_floatingItems.push_back(item);
    m_imageData.insert(item, png);
    anchorWidget(item);
    item->onMoved = [this, item]{ anchorWidget(item); syncImageSpecs(); markDirty(); };
    item->onSelected = [this](QWidget* o){ selectFloatingObject(o); };
    connect(item, &QObject::destroyed, this, [this](QObject* o){
        auto* w = static_cast<QWidget*>(o);
        m_objAnchors.remove(w);
        m_imageData.remove(w);
        m_floatingItems.removeAll(w);
        syncImageSpecs();
    });
    selectFloatingObject(item);
    syncImageSpecs();
    markDirty();
}

void CalcModule::insertImageObject() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Insert Picture", QString(), "Images (*.png *.jpg *.jpeg *.bmp *.gif)");
    if (path.isEmpty()) return;
    QPixmap pm(path);
    if (pm.isNull()) { featureInfo("Insert Picture", "Could not load that image."); return; }
    insertImagePixmap(pm);
}

void CalcModule::insertTextBoxObject() {
    auto* edit = new QLineEdit;
    edit->setPlaceholderText("Type here…");
    edit->setStyleSheet("background:#FFFFFF;border:none;padding:2px;");
    auto* item = new FloatingItem(edit, QRect(60, 40, 170, 44), m_tableView->viewport());
    item->show(); item->raise();
    m_floatingItems.push_back(item);
    anchorWidget(item);
    item->onMoved = [this, item]{ anchorWidget(item); };
    item->onSelected = [this](QWidget* o){ selectFloatingObject(o); };
    connect(item, &QObject::destroyed, this, [this](QObject* o){ m_objAnchors.remove(static_cast<QWidget*>(o)); });
    selectFloatingObject(item);
    edit->setFocus();
    markDirty();
}

void CalcModule::formatAsTable() {
    const QRect r = selectedRect();
    if (r.width() == 0 || r.height() == 0) return;
    std::vector<std::pair<QPoint, Cell>> edits;
    for (int row = r.top(); row <= r.bottom(); ++row)
        for (int col = r.left(); col <= r.right(); ++col) {
            Cell c = m_model->cellAt(col, row);
            c.format.borderEdges = CellFormat::BAll;
            c.format.borderColor = QColor("#9CB7A6");
            if (row == r.top()) {
                c.format.bold = true;
                c.format.textColor = QColor("#FFFFFF");
                c.format.bgColor = QColor("#107C41");
                c.format.hAlign = Qt::AlignHCenter;
            } else {
                c.format.bgColor = ((row - r.top()) % 2) ? QColor("#E9F2EC") : QColor("#FFFFFF");
            }
            edits.push_back({QPoint(col, row), c});
        }
    m_model->applyCellEdits(edits, "Format as Table");
}

void CalcModule::insertHyperlink() {
    const QModelIndex cur = m_tableView->currentIndex();
    if (!cur.isValid()) return;
    bool ok = false;
    const QString url = QInputDialog::getText(this, "Insert Link", "URL or text:",
                                              QLineEdit::Normal, "https://", &ok);
    if (!ok || url.isEmpty()) return;
    Cell c = m_model->cellAt(cur.column(), cur.row());
    c.content = url;
    c.format.textColor = QColor("#2563EB");
    c.format.underline = true;
    m_model->applyCellEdits({{QPoint(cur.column(), cur.row()), c}}, "Insert Link");
}

void CalcModule::applyWordArt() {
    applyFormatToSelection([](CellFormat& f){
        f.bold = true; f.fontSize = 22; f.textColor = QColor("#2563EB");
    }, "WordArt");
}

void CalcModule::insertTextValue(const QString& title, const QString& prompt) {
    const QModelIndex cur = m_tableView->currentIndex();
    if (!cur.isValid()) return;
    bool ok = false;
    const QString v = QInputDialog::getText(this, title, prompt, QLineEdit::Normal, QString(), &ok);
    if (!ok || v.isEmpty()) return;
    m_model->setCellContent(cur.column(), cur.row(), v, title);
    onSelectionChanged();
}

void CalcModule::pivotSummary(bool alsoChart) {
    int c1, r1, c2, r2;
    if (!usedRange(m_model, c1, r1, c2, r2) || c2 <= c1) {
        featureInfo("PivotTable", "Select a table with at least a label column and a value column.");
        return;
    }
    QStringList order;
    QHash<QString, double> sums;
    for (int row = r1 + 1; row <= r2; ++row) {
        const QString key = m_model->displayValue(c1, row);
        if (key.isEmpty()) continue;
        bool num = false; const double v = m_model->displayValue(c1 + 1, row).toDouble(&num);
        if (!order.contains(key)) order << key;
        sums[key] += num ? v : 0.0;
    }
    if (order.isEmpty()) { featureInfo("PivotTable", "No data to summarise."); return; }
    addSheet(QString("Pivot%1").arg(m_sheets.size() + 1));   // switches to the new sheet
    m_model->setCellContent(0, 0, "Group", "Pivot");
    m_model->setCellContent(1, 0, "Total", "Pivot");
    int rr = 1;
    for (const QString& k : order) {
        m_model->setCellContent(0, rr, k, "Pivot");
        m_model->setCellContent(1, rr, QString::number(sums[k]), "Pivot");
        ++rr;
    }
    if (alsoChart) {
        m_tableView->selectionModel()->select(
            QItemSelection(m_model->index(0, 0), m_model->index(rr - 1, 1)),
            QItemSelectionModel::ClearAndSelect);
        insertChart(ChartType::Column);
    }
    onSelectionChanged();
}

// ── Page layout ──────────────────────────────────────────────────────────────
void CalcModule::setPrintArea() {
    m_printRange = selectedRect();
    featureInfo("Print Area", "Print area set to the current selection. It will be used for PDF / print.");
}
void CalcModule::clearPrintArea() {
    m_printRange = QRect();
    featureInfo("Print Area", "Print area cleared — the whole used range will print.");
}
void CalcModule::setSheetBackground() {
    const QColor c = QColorDialog::getColor(QColor("#FFFFFF"), this, "Sheet Background");
    if (!c.isValid()) return;
    m_tableView->viewport()->setStyleSheet(QString("background:%1;").arg(c.name()));
    markDirty();
}

// ── Formulas: named ranges & auditing ────────────────────────────────────────
void CalcModule::defineName() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Define Name", "Name:",
                                               QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    const QRect r = selectedRect();
    const QString ref = FormulaEngine::cellAddress(r.left(), r.top())
        + (r.width() > 1 || r.height() > 1
               ? ":" + FormulaEngine::cellAddress(r.right(), r.bottom()) : QString());
    m_model->setDefinedName(name.trimmed(), ref);
    featureInfo("Define Name", QString("'%1' now refers to %2.").arg(name.trimmed(), ref));
}
void CalcModule::nameManager() {
    const auto& names = m_model->definedNames();
    QStringList lines;
    for (auto it = names.begin(); it != names.end(); ++it)
        lines << QString("%1  →  %2").arg(it.key(), it.value());
    QDialog dlg(this);
    dlg.setWindowTitle("Name Manager");
    auto* v = new QVBoxLayout(&dlg);
    auto* list = new QListWidget(&dlg);
    list->addItems(lines.isEmpty() ? QStringList{"(no names defined)"} : lines);
    v->addWidget(list, 1);
    auto* row = new QHBoxLayout();
    auto* add = new QPushButton("New…", &dlg);
    auto* del = new QPushButton("Delete", &dlg);
    auto* close = new QPushButton("Close", &dlg);
    row->addWidget(add); row->addWidget(del); row->addStretch(1); row->addWidget(close);
    v->addLayout(row);
    connect(add, &QPushButton::clicked, &dlg, [this, &dlg]{ dlg.accept(); defineName(); });
    connect(del, &QPushButton::clicked, &dlg, [this, list]{
        const auto* it = list->currentItem();
        if (!it) return;
        const QString nm = it->text().section("  →", 0, 0).trimmed();
        m_model->removeDefinedName(nm);
        delete list->takeItem(list->currentRow());
    });
    connect(close, &QPushButton::clicked, &dlg, &QDialog::reject);
    dlg.resize(320, 280);
    dlg.exec();
}
void CalcModule::useNameInFormula() {
    const auto& names = m_model->definedNames();
    if (names.isEmpty()) { featureInfo("Use in Formula", "No names defined yet — use Define Name first."); return; }
    QStringList keys; for (auto it = names.begin(); it != names.end(); ++it) keys << it.key();
    bool ok = false;
    const QString name = QInputDialog::getItem(this, "Use in Formula", "Name:", keys, 0, false, &ok);
    if (!ok || name.isEmpty()) return;
    m_formulaBar->setText(m_formulaBar->text() + name);
    m_formulaBar->setFocus();
}
void CalcModule::traceReferences(bool dependents) {
    const QModelIndex cur = m_tableView->currentIndex();
    if (!cur.isValid()) return;
    auto* sm = m_tableView->selectionModel();
    QItemSelection sel;
    const QRegularExpression refRe("([A-Za-z]{1,2})([0-9]{1,3})");
    if (!dependents) {
        const QString f = m_model->rawContent(cur.column(), cur.row());
        if (!f.startsWith('=')) { featureInfo("Trace Precedents", "The active cell has no formula."); return; }
        auto it = refRe.globalMatch(f);
        while (it.hasNext()) {
            const auto m = it.next();
            int c = 0, r = 0;
            if (FormulaEngine::parseCellRef(m.captured(0), c, r))
                sel.select(m_model->index(r, c), m_model->index(r, c));
        }
    } else {
        const QString addr = FormulaEngine::cellAddress(cur.column(), cur.row());
        const auto& data = m_model->cells();
        for (auto i = data.begin(); i != data.end(); ++i) {
            const QString f = i->second.content;
            if (f.startsWith('=') && f.contains(QRegularExpression("\\b" + addr + "\\b", QRegularExpression::CaseInsensitiveOption))) {
                const int c = SpreadsheetModel::keyCol(i->first), r = SpreadsheetModel::keyRow(i->first);
                sel.select(m_model->index(r, c), m_model->index(r, c));
            }
        }
    }
    if (sel.isEmpty()) { featureInfo(dependents ? "Trace Dependents" : "Trace Precedents", "None found."); return; }
    sm->select(sel, QItemSelectionModel::ClearAndSelect);
}
void CalcModule::removeTraceArrows() {
    const QModelIndex cur = m_tableView->currentIndex();
    if (cur.isValid()) m_tableView->selectionModel()->select(cur, QItemSelectionModel::ClearAndSelect);
}
void CalcModule::errorCheck() {
    int c1, r1, c2, r2;
    if (!usedRange(m_model, c1, r1, c2, r2)) return;
    for (int row = r1; row <= r2; ++row)
        for (int col = c1; col <= c2; ++col)
            if (m_model->displayValue(col, row).startsWith('#')) {
                const QModelIndex mi = m_model->index(row, col);
                m_tableView->setCurrentIndex(mi);
                featureInfo("Error Checking", QString("Error in %1: %2")
                    .arg(FormulaEngine::cellAddress(col, row), m_model->displayValue(col, row)));
                return;
            }
    featureInfo("Error Checking", "No errors found on this sheet.");
}
void CalcModule::evaluateFormula() {
    const QModelIndex cur = m_tableView->currentIndex();
    if (!cur.isValid()) return;
    const QString f = m_model->rawContent(cur.column(), cur.row());
    featureInfo("Evaluate Formula", QString("%1\n\n=  %2")
        .arg(f.isEmpty() ? "(empty)" : f, m_model->displayValue(cur.column(), cur.row())));
}

// ── Data ─────────────────────────────────────────────────────────────────────
void CalcModule::highlightDuplicates() {
    const QRect r = selectedRect();
    QHash<QString, int> seen;
    std::vector<std::pair<QPoint, Cell>> edits;
    for (int row = r.top(); row <= r.bottom(); ++row)
        for (int col = r.left(); col <= r.right(); ++col) {
            const QString v = m_model->displayValue(col, row);
            if (v.isEmpty()) continue;
            if (++seen[v] >= 2) {
                Cell c = m_model->cellAt(col, row);
                c.format.bgColor = QColor("#FFE08A");
                edits.push_back({QPoint(col, row), c});
            }
        }
    if (edits.empty()) { featureInfo("Highlight Duplicates", "No duplicates in the selection."); return; }
    m_model->applyCellEdits(edits, "Highlight Duplicates");
}
void CalcModule::removeDuplicates() {
    int c1, r1, c2, r2;
    if (!usedRange(m_model, c1, r1, c2, r2)) return;
    const int headerRow = r1;
    QSet<QString> seen;
    std::vector<std::vector<Cell>> keep;
    for (int row = headerRow + 1; row <= r2; ++row) {
        QString key;
        std::vector<Cell> cells;
        for (int col = c1; col <= c2; ++col) {
            const Cell cell = m_model->cellAt(col, row);
            cells.push_back(cell);
            key += cell.content + '';
        }
        if (seen.contains(key)) continue;
        seen.insert(key);
        keep.push_back(cells);
    }
    std::vector<std::pair<QPoint, Cell>> edits;
    int outRow = headerRow + 1;
    for (const auto& cells : keep) {
        for (int j = 0; j < (int)cells.size(); ++j)
            edits.push_back({QPoint(c1 + j, outRow), cells[j]});
        ++outRow;
    }
    for (int row = outRow; row <= r2; ++row)               // clear the freed rows
        for (int col = c1; col <= c2; ++col)
            edits.push_back({QPoint(col, row), Cell{}});
    const int removed = (r2 - headerRow) - (int)keep.size();
    m_model->applyCellEdits(edits, "Remove Duplicates");
    featureInfo("Remove Duplicates", QString("Removed %1 duplicate row(s).").arg(removed));
}
void CalcModule::textToColumns() {
    const QModelIndex cur = m_tableView->currentIndex();
    if (!cur.isValid()) return;
    bool ok = false;
    const QString delim = QInputDialog::getText(this, "Text to Columns",
        "Delimiter:", QLineEdit::Normal, ",", &ok);
    if (!ok || delim.isEmpty()) return;
    int c1, r1, c2, r2;
    if (!usedRange(m_model, c1, r1, c2, r2)) return;
    const int col = cur.column();
    std::vector<std::pair<QPoint, Cell>> edits;
    for (int row = r1; row <= r2; ++row) {
        const QString v = m_model->rawContent(col, row);
        if (v.isEmpty()) continue;
        const QStringList parts = v.split(delim);
        for (int j = 0; j < parts.size(); ++j) {
            Cell c = (j == 0) ? m_model->cellAt(col, row) : Cell{};
            c.content = parts[j].trimmed();
            edits.push_back({QPoint(col + j, row), c});
        }
    }
    m_model->applyCellEdits(edits, "Text to Columns");
}
void CalcModule::setDropdownValidation() {
    const QModelIndex cur = m_tableView->currentIndex();
    if (!cur.isValid()) return;
    bool ok = false;
    const QString csv = QInputDialog::getText(this, "Data Validation",
        "Allowed values (comma-separated):", QLineEdit::Normal, QString(), &ok);
    if (!ok) return;
    QStringList items;
    for (const QString& s : csv.split(',')) if (!s.trimmed().isEmpty()) items << s.trimmed();
    m_model->setValidationList(cur.column(), items);
    featureInfo("Data Validation", items.isEmpty()
        ? "Validation cleared for this column."
        : QString("Column %1 now uses a drop-down list.").arg(FormulaEngine::colLabel(cur.column())));
}
void CalcModule::importData() {
    const QString path = QFileDialog::getOpenFileName(this, "Get Data", QString(),
                                                      "CSV / Text (*.csv *.txt *.tsv)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    const QString text = QString::fromUtf8(f.readAll());
    f.close();
    const QChar delim = path.endsWith(".tsv", Qt::CaseInsensitive) ? '\t' : ',';
    const QStringList rows = text.split('\n', Qt::SkipEmptyParts);
    std::vector<std::pair<QPoint, Cell>> edits;
    for (int r = 0; r < rows.size() && r < SpreadsheetModel::NUM_ROWS; ++r) {
        const QStringList cols = rows[r].split(delim);
        for (int c = 0; c < cols.size() && c < SpreadsheetModel::NUM_COLS; ++c) {
            Cell cell; cell.content = cols[c].trimmed();
            if (!cell.content.isEmpty()) edits.push_back({QPoint(c, r), cell});
        }
    }
    if (!edits.empty()) m_model->applyCellEdits(edits, "Get Data");
}
void CalcModule::hideSelectedRows() {
    const QRect r = selectedRect();
    for (int row = r.top(); row <= r.bottom(); ++row) {
        m_tableView->setRowHidden(row, true);
        m_hiddenRows.insert(row);
    }
}
void CalcModule::unhideAllRows() {
    for (int row : m_hiddenRows) m_tableView->setRowHidden(row, false);
    m_hiddenRows.clear();
}

// ── Review: comments & protection ────────────────────────────────────────────
void CalcModule::addEditComment() {
    const QModelIndex cur = m_tableView->currentIndex();
    if (!cur.isValid()) return;
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(this, "Comment", "Comment:",
        m_model->comment(cur.column(), cur.row()), &ok);
    if (!ok) return;
    m_model->setComment(cur.column(), cur.row(), text);
    markDirty();
}
void CalcModule::deleteComment() {
    const QModelIndex cur = m_tableView->currentIndex();
    if (cur.isValid()) { m_model->setComment(cur.column(), cur.row(), QString()); markDirty(); }
}
void CalcModule::toggleShowComments() {
    m_model->setShowCommentMarkers(!m_model->showCommentMarkers());
}
void CalcModule::gotoComment(bool forward) {
    const auto& cm = m_model->comments();
    if (cm.isEmpty()) { featureInfo("Comments", "There are no comments on this sheet."); return; }
    QList<qint64> keys = cm.keys();
    std::sort(keys.begin(), keys.end());
    const QModelIndex cur = m_tableView->currentIndex();
    const qint64 curKey = cur.isValid()
        ? SpreadsheetModel::cellKey(cur.column(), cur.row()) : -1;
    qint64 target = -1;
    if (forward) { for (qint64 k : keys) if (k > curKey) { target = k; break; } if (target < 0) target = keys.first(); }
    else { for (int i = keys.size() - 1; i >= 0; --i) if (keys[i] < curKey) { target = keys[i]; break; } if (target < 0) target = keys.last(); }
    m_tableView->setCurrentIndex(m_model->index(SpreadsheetModel::keyRow(target),
                                                SpreadsheetModel::keyCol(target)));
}
void CalcModule::toggleProtectSheet() {
    m_sheetProtected = !m_sheetProtected;
    m_tableView->setEditTriggers(m_sheetProtected
        ? QAbstractItemView::NoEditTriggers
        : (QAbstractItemView::DoubleClicked | QAbstractItemView::AnyKeyPressed
           | QAbstractItemView::EditKeyPressed));
    featureInfo("Protect Sheet", m_sheetProtected
        ? "Sheet is now protected (read-only)." : "Sheet protection removed.");
}

// ── View ─────────────────────────────────────────────────────────────────────
void CalcModule::zoomBy(int deltaPercent) {
    m_zoom = std::clamp(m_zoom + deltaPercent / 100.0, 0.5, 2.0);
    resetZoom();   // re-apply
}
void CalcModule::resetZoom() {
    // Anchors are stored in unzoomed cell space, so the objects have to be
    // laid out again after the row/column sizes change below.
    if (m_zoom <= 0) m_zoom = 1.0;
    QFont vf("Calibri"); vf.setPointSizeF(11.0 * m_zoom);
    m_tableView->setFont(vf);
    // The cell text is sized from the model, so it has to be told as well.
    if (m_model) m_model->setViewZoom(m_zoom);
    // A header refuses to size a section below its own minimum, which is
    // derived from the font. Zoomed-out rows are legitimately a few pixels
    // tall, so the floor is lifted out of the way.
    m_tableView->verticalHeader()->setMinimumSectionSize(1);
    m_tableView->horizontalHeader()->setMinimumSectionSize(1);
    m_applyingSizes = true;
    // Default first, then only the rows and columns that carry an override.
    m_tableView->horizontalHeader()->setDefaultSectionSize(int(64 * m_zoom));
    m_tableView->verticalHeader()->setDefaultSectionSize(int(20 * m_zoom));
    const auto& zcw = m_model->colWidths();
    for (auto it = zcw.begin(); it != zcw.end(); ++it)
        if (it.key() >= 0 && it.key() < SpreadsheetModel::NUM_COLS)
            m_tableView->setColumnWidth(it.key(), int(it.value() * m_zoom));
    const auto& zrh = m_model->rowHeights();
    for (auto it = zrh.begin(); it != zrh.end(); ++it)
        if (it.key() >= 0 && it.key() < SpreadsheetModel::NUM_ROWS)
            m_tableView->setRowHeight(it.key(), int(it.value() * m_zoom));
    m_applyingSizes = false;
    updateFrozenViews();
    repositionFloatingObjects();
}
void CalcModule::toggleHighlightActive() {
    m_highlightActive = !m_highlightActive;
    if (m_selOverlay) m_selOverlay->setProperty("crosshair", m_highlightActive);
    featureInfo("Highlight Row/Column", m_highlightActive
        ? "The active row and column header are highlighted as you move."
        : "Highlight turned off.");
}
void CalcModule::toggleEyeProtection() {
    m_eyeProtection = !m_eyeProtection;
    m_tableView->viewport()->setStyleSheet(
        m_eyeProtection ? "background:#E6EFE0;" : "background:#FFFFFF;");
}
void CalcModule::openNewWindow() {
    QProcess::startDetached(QApplication::applicationFilePath(), {});
}

// ── Tools ────────────────────────────────────────────────────────────────────
void CalcModule::exportToImage() {
    QString path = QFileDialog::getSaveFileName(this, "Export to Picture", "Sheet.png",
                                                "PNG Image (*.png)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".png", Qt::CaseInsensitive)) path += ".png";
    int c1, r1, c2, r2;
    if (!usedRange(m_model, c1, r1, c2, r2)) { featureInfo("Export to Picture", "Sheet is empty."); return; }
    int w = 42, h = 22;
    for (int c = c1; c <= c2; ++c) w += m_tableView->columnWidth(c);
    for (int r = r1; r <= r2; ++r) h += m_tableView->rowHeight(r);
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(Qt::white);
    { QPainter p(&img); renderSheet(p, QRectF(0, 0, w, h)); }
    img.save(path);
    featureInfo("Export to Picture", QString("Saved to:\n%1").arg(path));
}
void CalcModule::exportToText() {
    QString path = QFileDialog::getSaveFileName(this, "Extract Text", "Sheet.txt",
                                                "Text (*.txt)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".txt", Qt::CaseInsensitive)) path += ".txt";
    int c1, r1, c2, r2;
    if (!usedRange(m_model, c1, r1, c2, r2)) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream ts(&f);
    for (int row = r1; row <= r2; ++row) {
        QStringList parts;
        for (int col = c1; col <= c2; ++col) parts << m_model->displayValue(col, row);
        ts << parts.join('\t') << '\n';
    }
    f.close();
    featureInfo("Extract Text", QString("Saved to:\n%1").arg(path));
}
void CalcModule::mergeAllSheets() {
    if (m_sheets.size() < 2) { featureInfo("Merge Sheets", "There is only one sheet."); return; }
    struct Row { QString sheet; QVector<Cell> cells; int c1; };
    std::vector<std::pair<QPoint, Cell>> out;
    int destRow = 1;
    out.push_back({QPoint(0, 0), [&]{ Cell c; c.content = "Sheet"; c.format.bold = true; return c; }()});
    for (auto* s : m_sheets) {
        int c1, r1, c2, r2;
        if (!usedRange(s, c1, r1, c2, r2)) continue;
        for (int row = r1; row <= r2; ++row) {
            Cell tag; tag.content = s->sheetName();
            out.push_back({QPoint(0, destRow), tag});
            for (int col = c1; col <= c2; ++col)
                out.push_back({QPoint(col - c1 + 1, destRow), s->cellAt(col, row)});
            ++destRow;
        }
    }
    addSheet(QString("Merged%1").arg(m_sheets.size() + 1));   // switches
    m_model->applyCellEdits(out, "Merge Sheets");
    onSelectionChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
// Export / print  (renders the active sheet's used range, fit to one page)
// ─────────────────────────────────────────────────────────────────────────────
void CalcModule::renderSheet(QPainter& p, const QRectF& target) {
    // Print range (explicit print area, else the used range).
    int c1, r1, c2, r2;
    if (m_printRange.isValid() && m_printRange.width() > 0) {
        c1 = m_printRange.left(); r1 = m_printRange.top();
        c2 = m_printRange.right(); r2 = m_printRange.bottom();
    } else if (!usedRange(m_model, c1, r1, c2, r2)) {
        p.drawText(target, Qt::AlignCenter, "(empty sheet)");
        return;
    }

    const int hdrW = 42, hdrH = 22;
    QVector<int> colW; int gridW = hdrW;
    for (int c = c1; c <= c2; ++c) { const int w = m_tableView->columnWidth(c); colW << w; gridW += w; }
    QVector<int> rowH; int gridH = hdrH;
    for (int r = r1; r <= r2; ++r) { const int h = m_tableView->rowHeight(r); rowH << h; gridH += h; }

    // Fit the whole grid onto the page. Layout is in screen-pixel units and all
    // fonts use PIXEL sizes so they scale 1:1 with this transform (point sizes
    // would otherwise render at the device's DPI and overflow the cells).
    const double scale = std::min(target.width() / gridW, target.height() / gridH);

    p.save();
    p.translate(target.left(), target.top());
    p.scale(scale, scale);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor gridLine("#C9CCD1"), hdrBg("#F2F3F5"), hdrFg("#5A5F66");

    // Column header band.
    {
        int x = hdrW;
        p.setPen(Qt::NoPen);
        p.setBrush(hdrBg);
        p.drawRect(0, 0, gridW, hdrH);
        p.setPen(hdrFg);
        QFont hf("Segoe UI"); hf.setPixelSize(11); p.setFont(hf);
        for (int i = 0; i < colW.size(); ++i) {
            p.drawText(QRect(x, 0, colW[i], hdrH), Qt::AlignCenter,
                       FormulaEngine::colLabel(c1 + i));
            x += colW[i];
        }
    }

    // Rows (with row-number header gutter).
    int y = hdrH;
    for (int i = 0; i < rowH.size(); ++i) {
        const int row = r1 + i;
        // Row-number gutter.
        p.setPen(Qt::NoPen); p.setBrush(hdrBg);
        p.drawRect(0, y, hdrW, rowH[i]);
        p.setPen(hdrFg);
        { QFont hf("Segoe UI"); hf.setPixelSize(11); p.setFont(hf);
          p.drawText(QRect(0, y, hdrW, rowH[i]), Qt::AlignCenter, QString::number(row + 1)); }

        int x = hdrW;
        for (int j = 0; j < colW.size(); ++j) {
            const int col = c1 + j;
            const QRect cellRect(x, y, colW[j], rowH[i]);
            const QModelIndex idx = m_model->index(row, col);

            const QVariant bg = idx.data(Qt::BackgroundRole);
            if (bg.canConvert<QColor>() && bg.value<QColor>().isValid()) {
                p.setPen(Qt::NoPen); p.setBrush(bg.value<QColor>());
                p.drawRect(cellRect);
            }
            const QString text = idx.data(Qt::DisplayRole).toString();
            if (!text.isEmpty()) {
                QFont f = idx.data(Qt::FontRole).isValid()
                              ? idx.data(Qt::FontRole).value<QFont>() : QFont("Calibri");
                f.setPixelSize((f.pointSize() > 0 ? f.pointSize() : 11) + 2);
                p.setFont(f);
                const QVariant fg = idx.data(Qt::ForegroundRole);
                p.setPen(fg.canConvert<QColor>() && fg.value<QColor>().isValid()
                             ? fg.value<QColor>() : QColor("#1C1E26"));
                int align = idx.data(Qt::TextAlignmentRole).toInt();
                if (align == 0) align = int(Qt::AlignLeft | Qt::AlignVCenter);
                p.drawText(cellRect.adjusted(3, 1, -3, -1), align, text);
            }
            x += colW[j];
        }
        y += rowH[i];
    }

    // Gridlines.
    p.setPen(gridLine); p.setBrush(Qt::NoBrush);
    // Vertical lines: left edge, gutter edge, then each column boundary.
    { int x = 0; p.drawLine(x, 0, x, gridH);
      x = hdrW;  p.drawLine(x, 0, x, gridH);
      for (int i = 0; i < colW.size(); ++i) { x += colW[i]; p.drawLine(x, 0, x, gridH); }
    }
    // Horizontal lines: top edge, header band, then each row boundary.
    { int yy = 0; p.drawLine(0, yy, gridW, yy);
      yy = hdrH;  p.drawLine(0, yy, gridW, yy);
      for (int i = 0; i < rowH.size(); ++i) { yy += rowH[i]; p.drawLine(0, yy, gridW, yy); }
    }
    p.restore();
}

void CalcModule::exportToPdf() {
    QString def = m_currentPath.isEmpty()
        ? QStringLiteral("Sheet.pdf")
        : QFileInfo(m_currentPath).completeBaseName() + ".pdf";
    QString path = QFileDialog::getSaveFileName(this, "Export to PDF", def, "PDF Files (*.pdf)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".pdf", Qt::CaseInsensitive)) path += ".pdf";

    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageOrientation(QPageLayout::Landscape);
    writer.setPageMargins(QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter);
    // Premium "PDF export quality". Previously this used QPdfWriter's default.
    writer.setResolution(NativeOffice::ExportPrefs::pdfExportDpi());

    QPainter p(&writer);
    if (!p.isActive()) {
        QMessageBox::warning(this, "Export to PDF", "Could not write the PDF (is the file open?).");
        return;
    }
    renderSheet(p, QRectF(0, 0, writer.width(), writer.height()));
    p.end();

    // Applied after the painter closes the file: QPdfWriter cannot carry the
    // link annotation, so the mark is stamped in a second PDFium pass.
    NativeOffice::Watermark::stampIfRequired(path);

    QMessageBox::information(this, "Export to PDF", QString("Saved to:\n%1").arg(path));
}

void CalcModule::printPreview() {
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageOrientation(QPageLayout::Landscape);
    QPrintPreviewDialog dlg(&printer, this);
    dlg.setWindowTitle("Print Preview");
    connect(&dlg, &QPrintPreviewDialog::paintRequested, this, [this](QPrinter* pr){
        QPainter p(pr);
        renderSheet(p, QRectF(0, 0, pr->width(), pr->height()));
    });
    dlg.resize(900, 650);
    dlg.exec();
}

// The range a function should default to: the current multi-cell selection if
// there is one, otherwise the contiguous run of numbers directly above the
// active cell, otherwise the run directly to its left. This is the guess Excel
// makes for AutoSum, and it is what makes picking a function from the library
// feel like it did something.
QString CalcModule::suggestedFunctionRange() const {
    const QModelIndex cur = m_tableView->currentIndex();
    if (!cur.isValid()) return {};

    const QRect sel = selectedRect();
    if (sel.width() > 1 || sel.height() > 1)
        return FormulaEngine::cellAddress(sel.left(),  sel.top()) + ":"
             + FormulaEngine::cellAddress(sel.right(), sel.bottom());

    auto usable = [this](int c, int r) {
        const QString raw = m_model->rawContent(c, r).trimmed();
        if (raw.isEmpty()) return false;
        if (raw.startsWith('=')) return true;      // a formula counts as a value
        bool ok = false;
        raw.toDouble(&ok);
        return ok;
    };

    const int c = cur.column();
    const int r = cur.row();

    int top = r;
    while (top - 1 >= 0 && usable(c, top - 1)) --top;
    if (top < r)
        return FormulaEngine::cellAddress(c, top) + ":"
             + FormulaEngine::cellAddress(c, r - 1);

    int left = c;
    while (left - 1 >= 0 && usable(left - 1, r)) --left;
    if (left < c)
        return FormulaEngine::cellAddress(left, r) + ":"
             + FormulaEngine::cellAddress(c - 1, r);

    return {};
}

// Begin typing "=FN(" into the formula bar, prefilled with the range the user
// most likely means. Previously this inserted a bare "=SUM(" and left them to
// type the range and the closing bracket by hand, which is why picking a
// function from the library was reported as doing nothing.
void CalcModule::startFunctionEntry(const QString& fn) {
    const QModelIndex cur = m_tableView->currentIndex();
    if (!cur.isValid()) return;

    const QString head = "=" + fn + "(";
    const QString arg  = suggestedFunctionRange();
    const QString text = head + arg + ")";

    m_updatingFormulaBar = true;
    m_formulaBar->setText(text);
    m_updatingFormulaBar = false;
    m_formulaBar->setFocus();

    if (arg.isEmpty())
        m_formulaBar->setCursorPosition(head.length());   // caret inside the brackets
    else
        m_formulaBar->setSelection(head.length(), arg.length());  // overtype to change it
}

// Transient "coming soon" tip for features that are not implemented yet.
void CalcModule::notImplemented(const QString& feature) {
    QToolTip::showText(QCursor::pos(),
                       feature + " — coming soon in NativeOffice Calc", this);
}

// Sort the used data region by the active column. Auto-skips a text header row.
// Whole rows (content + format) move together; formula refs are not adjusted.
void CalcModule::sortByColumn(bool ascending) {
    int c1 = INT_MAX, r1 = INT_MAX, c2 = -1, r2 = -1;
    const auto& data = m_model->cells();
    for (auto it = data.begin(); it != data.end(); ++it) {
        const int col = SpreadsheetModel::keyCol(it->first);
        const int row = SpreadsheetModel::keyRow(it->first);
        c1 = std::min(c1, col); c2 = std::max(c2, col);
        r1 = std::min(r1, row); r2 = std::max(r2, row);
    }
    if (c2 < 0 || r2 <= r1) return;                  // empty or single-row

    const QModelIndex cur = m_tableView->currentIndex();
    const int keyCol = (cur.isValid() && cur.column() >= c1 && cur.column() <= c2)
                           ? cur.column() : c1;

    // Treat the top row as a header if it has values but none are numeric.
    bool anyVal = false, anyNum = false;
    for (int c = c1; c <= c2; ++c) {
        const QString d = m_model->displayValue(c, r1);
        if (d.isEmpty()) continue;
        anyVal = true;
        bool ok = false; d.toDouble(&ok); if (ok) anyNum = true;
    }
    const int startRow = (anyVal && !anyNum) ? r1 + 1 : r1;
    if (r2 <= startRow) return;

    struct RowRec { QString keyText; bool keyNum; double keyVal; std::vector<Cell> cells; };
    std::vector<RowRec> rows;
    for (int row = startRow; row <= r2; ++row) {
        RowRec rec;
        rec.keyText = m_model->displayValue(keyCol, row);
        bool ok = false;
        rec.keyVal = rec.keyText.toDouble(&ok);
        rec.keyNum = ok && !rec.keyText.isEmpty();
        for (int c = c1; c <= c2; ++c) rec.cells.push_back(m_model->cellAt(c, row));
        rows.push_back(std::move(rec));
    }
    std::stable_sort(rows.begin(), rows.end(),
        [ascending](const RowRec& a, const RowRec& b) {
            int cmp;
            if (a.keyNum && b.keyNum)
                cmp = (a.keyVal < b.keyVal) ? -1 : (a.keyVal > b.keyVal ? 1 : 0);
            else
                cmp = QString::compare(a.keyText, b.keyText, Qt::CaseInsensitive);
            return ascending ? cmp < 0 : cmp > 0;
        });

    std::vector<std::pair<QPoint, Cell>> edits;
    const int width = c2 - c1 + 1;
    for (int i = 0; i < (int)rows.size(); ++i)
        for (int j = 0; j < width; ++j)
            edits.push_back({QPoint(c1 + j, startRow + i), rows[i].cells[j]});
    m_model->applyCellEdits(edits, ascending ? "Sort Ascending" : "Sort Descending");
}

// ─────────────────────────────────────────────────────────────────────────────
// Fill
// ─────────────────────────────────────────────────────────────────────────────
// Copy the 'source' block into 'dest', repeating the source pattern (modulo) for
// cells outside the source. A single source cell therefore just copies down/across.
void CalcModule::fillRange(const QRect& source, const QRect& dest) {
    if (!source.isValid() || source.width() == 0 || source.height() == 0) return;

    const bool vertical = dest.height() > source.height();

    // A single row or column of evenly spaced plain numbers continues as a
    // series, the way Excel does: dragging 10, 20, 30 should give 40, 50, 60,
    // not repeat 10, 20, 30. Everything else (text, formulas, mixed content,
    // multi-column blocks) repeats the source block, which is the old
    // behaviour and still the right answer for those.
    bool   series = false;
    double step   = 0.0;
    double last   = 0.0;
    {
        const int n     = vertical ? source.height() : source.width();
        const int thick = vertical ? source.width()  : source.height();
        if (n >= 2 && thick == 1) {
            QVector<double> vals;
            bool allNumeric = true;
            for (int i = 0; i < n && allNumeric; ++i) {
                const int c = vertical ? source.left()     : source.left() + i;
                const int r = vertical ? source.top() + i  : source.top();
                const QString raw = m_model->rawContent(c, r).trimmed();
                bool ok = false;
                const double v = raw.toDouble(&ok);
                if (raw.isEmpty() || raw.startsWith('=') || !ok) allNumeric = false;
                else vals << v;
            }
            if (allNumeric && vals.size() == n) {
                step   = vals[1] - vals[0];
                series = true;
                for (int i = 2; i < vals.size(); ++i)
                    if (qAbs((vals[i] - vals[i - 1]) - step) > 1e-9) { series = false; break; }
                last = vals.last();
            }
        }
    }

    std::vector<std::pair<QPoint, Cell>> edits;
    for (int row = dest.top(); row <= dest.bottom(); ++row)
        for (int col = dest.left(); col <= dest.right(); ++col) {
            if (source.contains(col, row)) continue;        // leave the source intact
            if (col < 0 || col >= SpreadsheetModel::NUM_COLS
                || row < 0 || row >= SpreadsheetModel::NUM_ROWS) continue;

            const int sc = source.left() + ((col - source.left()) % source.width());
            const int sr = source.top()  + ((row - source.top())  % source.height());
            Cell c = m_model->cellAt(sc, sr);

            if (series) {
                // Carry the formatting of the last source cell, replace the value.
                const int k = vertical ? (row - source.bottom()) : (col - source.right());
                c = m_model->cellAt(vertical ? source.left() : source.right(),
                                    vertical ? source.bottom() : source.top());
                c.content = QString::number(last + step * k, 'g', 15);
            }
            edits.push_back({QPoint(col, row), c});
        }
    if (!edits.empty()) m_model->applyCellEdits(edits, "Fill");

    // The fill does not move the selection, so nothing else refreshes the
    // formula bar for the active cell.
    syncFormulaBarToCurrent();
}

// Single place that pushes the active cell's raw content into the formula bar.
void CalcModule::syncFormulaBarToCurrent() {
    if (!m_formulaBar || !m_tableView) return;
    const QModelIndex cur = m_tableView->currentIndex();
    if (!cur.isValid()) return;
    m_updatingFormulaBar = true;
    m_formulaBar->setText(m_model->rawContent(cur.column(), cur.row()));
    m_updatingFormulaBar = false;
}

void CalcModule::fillDown() {
    const QRect r = selectedRect();
    if (r.height() <= 1) return;
    fillRange(QRect(r.left(), r.top(), r.width(), 1), r);     // top row → down
}

void CalcModule::fillRight() {
    const QRect r = selectedRect();
    if (r.width() <= 1) return;
    fillRange(QRect(r.left(), r.top(), 1, r.height()), r);    // left column → right
}

// ─────────────────────────────────────────────────────────────────────────────
// Format application
// ─────────────────────────────────────────────────────────────────────────────
void CalcModule::applyFormatToSelection(const std::function<void(CellFormat&)>& fn,
                                        const QString& undoText) {
    if (m_updatingToolbar) return;
    const QRect r = selectedRect();
    if (r.width() == 0 || r.height() == 0) return;

    std::vector<std::pair<QPoint, Cell>> edits;
    for (int row = r.top(); row <= r.bottom(); ++row)
        for (int col = r.left(); col <= r.right(); ++col) {
            Cell c = m_model->cellAt(col, row);
            fn(c.format);
            edits.push_back({QPoint(col, row), c});
        }
    m_model->applyCellEdits(edits, undoText);
}

void CalcModule::onBoldToggled(bool on) {
    applyFormatToSelection([on](CellFormat& f){ f.bold = on; }, "Bold");
}
void CalcModule::onItalicToggled(bool on) {
    applyFormatToSelection([on](CellFormat& f){ f.italic = on; }, "Italic");
}
void CalcModule::onUnderlineToggled(bool on) {
    applyFormatToSelection([on](CellFormat& f){ f.underline = on; }, "Underline");
}
void CalcModule::onStrikeToggled(bool on) {
    applyFormatToSelection([on](CellFormat& f){ f.strike = on; }, "Strikethrough");
}
void CalcModule::onIncreaseFont() {
    const QModelIndex cur = m_tableView->currentIndex();
    int sz = (cur.isValid() ? m_model->cellAt(cur.column(), cur.row()).format.fontSize : 0);
    if (sz <= 0) sz = 11;
    const int ns = std::min(409, sz + 1);
    applyFormatToSelection([ns](CellFormat& f){ f.fontSize = ns; }, "Increase Font Size");
    if (m_sizeCombo) { m_updatingToolbar = true; m_sizeCombo->setCurrentText(QString::number(ns)); m_updatingToolbar = false; }
}
void CalcModule::onDecreaseFont() {
    const QModelIndex cur = m_tableView->currentIndex();
    int sz = (cur.isValid() ? m_model->cellAt(cur.column(), cur.row()).format.fontSize : 0);
    if (sz <= 0) sz = 11;
    const int ns = std::max(1, sz - 1);
    applyFormatToSelection([ns](CellFormat& f){ f.fontSize = ns; }, "Decrease Font Size");
    if (m_sizeCombo) { m_updatingToolbar = true; m_sizeCombo->setCurrentText(QString::number(ns)); m_updatingToolbar = false; }
}
void CalcModule::onIncreaseIndent() {
    applyFormatToSelection([](CellFormat& f){ f.indent = std::min(15, f.indent + 1); }, "Increase Indent");
}
void CalcModule::onDecreaseIndent() {
    applyFormatToSelection([](CellFormat& f){ f.indent = std::max(0, f.indent - 1); }, "Decrease Indent");
}
void CalcModule::onFormatPainterClicked() {
    if (!m_formatPainterBtn) return;
    if (m_formatPainterBtn->isChecked()) {
        // Capture the active cell's format; arm for the next selection.
        const QModelIndex cur = m_tableView->currentIndex();
        m_painterFmt   = cur.isValid() ? m_model->cellAt(cur.column(), cur.row()).format
                                       : CellFormat{};
        m_painterArmed = true;
    } else {
        m_painterArmed = false;
    }
}
void CalcModule::onFontFamilyChanged() {
    const QString fam = m_fontCombo->currentText();
    applyFormatToSelection([fam](CellFormat& f){ f.fontFamily = fam; }, "Font");
}
void CalcModule::onFontSizeChanged() {
    bool ok = false;
    const int sz = m_sizeCombo->currentText().toInt(&ok);
    if (!ok || sz <= 0) return;
    applyFormatToSelection([sz](CellFormat& f){ f.fontSize = sz; }, "Font Size");
}
void CalcModule::onTextColorClicked() {
    const QColor c = QColorDialog::getColor(m_lastTextColor, this, "Text Colour");
    if (!c.isValid()) return;
    m_lastTextColor = c;
    styleColorButton(m_textColorBtn, c);
    applyFormatToSelection([c](CellFormat& f){ f.textColor = c; }, "Text Colour");
}
void CalcModule::onFillColorClicked() {
    const QColor c = QColorDialog::getColor(m_lastFillColor, this, "Fill Colour");
    if (!c.isValid()) return;
    m_lastFillColor = c;
    styleColorButton(m_fillColorBtn, c);
    applyFormatToSelection([c](CellFormat& f){ f.bgColor = c; }, "Fill Colour");
}
void CalcModule::onAlignClicked() {
    auto* btn = qobject_cast<QToolButton*>(sender());
    if (!btn) return;
    int align = 0;
    if (btn->isChecked()) {
        if      (btn == m_alignLeftBtn)   align = Qt::AlignLeft;
        else if (btn == m_alignCenterBtn) align = Qt::AlignHCenter;
        else                              align = Qt::AlignRight;
    }
    m_updatingToolbar = true;
    m_alignLeftBtn  ->setChecked(align == Qt::AlignLeft);
    m_alignCenterBtn->setChecked(align == Qt::AlignHCenter);
    m_alignRightBtn ->setChecked(align == Qt::AlignRight);
    m_updatingToolbar = false;
    applyFormatToSelection([align](CellFormat& f){ f.hAlign = align; }, "Align");
}
void CalcModule::onVAlignClicked() {
    auto* btn = qobject_cast<QToolButton*>(sender());
    if (!btn) return;
    int align = 0;
    if (btn->isChecked()) {
        if      (btn == m_alignTopBtn) align = Qt::AlignTop;
        else if (btn == m_alignMidBtn) align = Qt::AlignVCenter;
        else                           align = Qt::AlignBottom;
    }
    m_updatingToolbar = true;
    m_alignTopBtn->setChecked(align == Qt::AlignTop);
    m_alignMidBtn->setChecked(align == Qt::AlignVCenter);
    m_alignBotBtn->setChecked(align == Qt::AlignBottom);
    m_updatingToolbar = false;
    applyFormatToSelection([align](CellFormat& f){ f.vAlign = align; }, "Vertical Align");
}

void CalcModule::onWrapToggled(bool on) {
    applyFormatToSelection([on](CellFormat& f){ f.wrap = on; }, "Wrap Text");
    // Grow/shrink rows in the selection to fit wrapped content.
    const QRect r = selectedRect();
    for (int row = r.top(); row <= r.bottom(); ++row)
        m_tableView->resizeRowToContents(row);
}

void CalcModule::applyMerges() {
    m_tableView->clearSpans();
    for (const QRect& r : m_model->merges())
        m_tableView->setSpan(r.top(), r.left(), r.height(), r.width());
}

void CalcModule::applySizes() {
    if (!m_model) return;
    m_applyingSizes = true;

    // Whatever the previous sheet hid has to be shown again before this one's
    // hidden sections are applied, or hiding accumulates across sheet switches.
    for (int r : m_hiddenRows)        m_tableView->setRowHidden(r, false);
    for (int c : m_hiddenColsApplied) m_tableView->setColumnHidden(c, false);
    m_hiddenRows.clear();
    m_hiddenColsApplied.clear();

    for (int c : m_model->hiddenCols())
        if (c >= 0 && c < SpreadsheetModel::NUM_COLS) {
            m_tableView->setColumnHidden(c, true);
            m_hiddenColsApplied.insert(c);
        }
    for (int r : m_model->hiddenRows())
        if (r >= 0 && r < SpreadsheetModel::NUM_ROWS) {
            m_tableView->setRowHidden(r, true);
            m_hiddenRows.insert(r);
        }

    // The sheet's own saved zoom drives the section sizes. Applied here rather
    // than by calling resetZoom(): that released this guard mid-way and the
    // resulting section-resize signals fed back into layout without settling.
    m_zoom = m_model->zoomScale() / 100.0;
    if (m_zoom <= 0) m_zoom = 1.0;
    m_model->setViewZoom(m_zoom);

    QFont vf(QStringLiteral("Calibri"));
    vf.setPointSizeF(11.0 * m_zoom);
    m_tableView->setFont(vf);
    m_tableView->verticalHeader()->setMinimumSectionSize(1);
    m_tableView->horizontalHeader()->setMinimumSectionSize(1);
    m_tableView->horizontalHeader()->setDefaultSectionSize(int(64 * m_zoom));
    m_tableView->verticalHeader()->setDefaultSectionSize(int(20 * m_zoom));

    const auto& cw2 = m_model->colWidths();
    for (auto it = cw2.begin(); it != cw2.end(); ++it)
        if (it.key() >= 0 && it.key() < SpreadsheetModel::NUM_COLS)
            m_tableView->setColumnWidth(it.key(), int(it.value() * m_zoom));
    const auto& rh2 = m_model->rowHeights();
    for (auto it = rh2.begin(); it != rh2.end(); ++it)
        if (it.key() >= 0 && it.key() < SpreadsheetModel::NUM_ROWS)
            m_tableView->setRowHeight(it.key(), int(it.value() * m_zoom));

    m_applyingSizes = false;
    updateFrozenViews();
    repositionFloatingObjects();
}

void CalcModule::onMergeClicked() {
    const QModelIndex cur = m_tableView->currentIndex();
    if (!cur.isValid()) return;

    const QRect existing = m_model->mergeContaining(cur.column(), cur.row());
    if (existing.isValid()) {
        m_model->removeMergeContaining(cur.column(), cur.row());
    } else {
        const QRect r = selectedRect();
        if (r.width() <= 1 && r.height() <= 1) return;     // nothing to merge
        m_model->addMerge(r);
        // Merge & center: centre the anchor (top-left) cell.
        Cell c = m_model->cellAt(r.left(), r.top());
        c.format.hAlign = Qt::AlignHCenter;
        c.format.vAlign = Qt::AlignVCenter;
        std::vector<std::pair<QPoint, Cell>> edits{ {QPoint(r.left(), r.top()), c} };
        m_model->applyCellEdits(edits, "Merge Cells");
    }
    applyMerges();
    markDirty();
    updateToolbarFromCell();
}

void CalcModule::onClearFormatting() {
    applyFormatToSelection([](CellFormat& f){ f = CellFormat{}; }, "Clear Formatting");
}

// ── Formulas / View actions (Sprint 24) ────────────────────────────────────────
void CalcModule::onShowFormulasToggled(bool on) {
    if (m_model) m_model->setShowFormulas(on);
}
void CalcModule::onCalculateNow() {
    // Force a full re-evaluation of every formula on the active sheet.
    if (m_model) m_model->notifyAllChanged();
}
void CalcModule::onToggleGridlines(bool on) {
    if (m_tableView) m_tableView->setShowGrid(on);
}
void CalcModule::onToggleHeadings(bool on) {
    if (m_colHeader) m_colHeader->setVisible(on);
    if (m_rowHeader) m_rowHeader->setVisible(on);
}
void CalcModule::insertSymbolDialog() {
    const QModelIndex cur = m_tableView->currentIndex();
    if (!cur.isValid()) return;
    static const QStringList kSymbols = {
        "€", "£", "¥", "¢", "©", "®", "™", "°", "±", "×", "÷", "≈", "≠", "≤", "≥",
        "→", "←", "↑", "↓", "•", "★", "✓", "✗", "α", "β", "π", "Σ", "Δ", "Ω", "µ"
    };
    bool ok = false;
    const QString sym = QInputDialog::getItem(this, "Symbol", "Insert symbol:",
                                              kSymbols, 0, false, &ok);
    if (!ok || sym.isEmpty()) return;
    QString content = m_model->rawContent(cur.column(), cur.row());
    m_model->setCellContent(cur.column(), cur.row(), content + sym, "Insert Symbol");
    onSelectionChanged();
}

// ── Number format ─────────────────────────────────────────────────────────────
void CalcModule::onNumberFormatChanged() {
    if (m_updatingToolbar) return;
    const QString code = m_numFmtCombo->currentData().toString();
    applyFormatToSelection([code](CellFormat& f){ f.numberFormat = code; }, "Number Format");
}
void CalcModule::onCurrencyClicked() {
    applyFormatToSelection([](CellFormat& f){ f.numberFormat = "$#,##0.00"; }, "Currency");
}
void CalcModule::onPercentClicked() {
    applyFormatToSelection([](CellFormat& f){ f.numberFormat = "0%"; }, "Percent");
}
void CalcModule::onCommaClicked() {
    applyFormatToSelection([](CellFormat& f){ f.numberFormat = "#,##0.00"; }, "Comma Style");
}
void CalcModule::onIncreaseDecimals() {
    const QModelIndex cur = m_tableView->currentIndex();
    const QString code = cur.isValid()
        ? m_model->cellAt(cur.column(), cur.row()).format.numberFormat : QString();
    const QString nc = buildNumberCode(code.contains('$'), code.contains('%'),
                                       code.contains(','), decimalsOfCode(code) + 1);
    applyFormatToSelection([nc](CellFormat& f){ f.numberFormat = nc; }, "Increase Decimals");
}
void CalcModule::onDecreaseDecimals() {
    const QModelIndex cur = m_tableView->currentIndex();
    const QString code = cur.isValid()
        ? m_model->cellAt(cur.column(), cur.row()).format.numberFormat : QString();
    const int dec = std::max(0, decimalsOfCode(code) - 1);
    const QString nc = buildNumberCode(code.contains('$'), code.contains('%'),
                                       code.contains(','), dec);
    applyFormatToSelection([nc](CellFormat& f){ f.numberFormat = nc; }, "Decrease Decimals");
}

// ── Borders ───────────────────────────────────────────────────────────────────
void CalcModule::onBorderMenu(QAction* act) {
    const int data = act->data().toInt();   // edge mask, 0 = none, -1 = outside
    const QRect r = selectedRect();
    if (r.width() == 0 || r.height() == 0) return;

    std::vector<std::pair<QPoint, Cell>> edits;
    for (int row = r.top(); row <= r.bottom(); ++row)
        for (int col = r.left(); col <= r.right(); ++col) {
            Cell c = m_model->cellAt(col, row);
            CellFormat& f = c.format;

            if (data == 0) {                       // No Border
                f.borderEdges = 0;
                f.borderColor = QColor();
            } else if (data == -1) {               // Outside perimeter
                int e = 0;
                if (row == r.top())    e |= CellFormat::BTop;
                if (row == r.bottom()) e |= CellFormat::BBottom;
                if (col == r.left())   e |= CellFormat::BLeft;
                if (col == r.right())  e |= CellFormat::BRight;
                f.borderEdges = e;
            } else if (data == CellFormat::BAll) {  // All Borders
                f.borderEdges = CellFormat::BAll;
            } else if (data == CellFormat::BBottom) {
                if (row == r.bottom()) f.borderEdges |= CellFormat::BBottom;
            } else if (data == CellFormat::BTop) {
                if (row == r.top())    f.borderEdges |= CellFormat::BTop;
            }

            if (f.borderEdges && !f.borderColor.isValid())
                f.borderColor = QColor("#000000");
            if (!f.borderEdges)
                f.borderColor = QColor();

            edits.push_back({QPoint(col, row), c});
        }
    m_model->applyCellEdits(edits, "Borders");
}

// ─────────────────────────────────────────────────────────────────────────────
// Row / column context menus
// ─────────────────────────────────────────────────────────────────────────────
void CalcModule::onRowHeaderMenu(const QPoint& pos) {
    const int row = m_rowHeader->logicalIndexAt(pos);
    if (row < 0) return;
    QMenu menu(this);
    QAction* above = menu.addAction("Insert Row Above");
    QAction* below = menu.addAction("Insert Row Below");
    menu.addSeparator();
    QAction* del   = menu.addAction("Delete Row");
    const QAction* a = menu.exec(m_rowHeader->mapToGlobal(pos));
    if      (a == above) m_model->insertRowAt(row);
    else if (a == below) m_model->insertRowAt(row + 1);
    else if (a == del)   m_model->deleteRowAt(row);
}

void CalcModule::onColHeaderMenu(const QPoint& pos) {
    const int col = m_colHeader->logicalIndexAt(pos);
    if (col < 0) return;
    QMenu menu(this);
    QAction* left  = menu.addAction("Insert Column Left");
    QAction* right = menu.addAction("Insert Column Right");
    menu.addSeparator();
    QAction* del   = menu.addAction("Delete Column");
    const QAction* a = menu.exec(m_colHeader->mapToGlobal(pos));
    if      (a == left)  m_model->insertColumnAt(col);
    else if (a == right) m_model->insertColumnAt(col + 1);
    else if (a == del)   m_model->deleteColumnAt(col);
}

void CalcModule::onGridContextMenu(const QPoint& pos) {
    const QModelIndex idx = m_tableView->indexAt(pos);
    const QModelIndex cur = m_tableView->currentIndex();
    const int row = idx.isValid() ? idx.row()    : (cur.isValid() ? cur.row()    : 0);
    const int col = idx.isValid() ? idx.column() : (cur.isValid() ? cur.column() : 0);

    QMenu menu(this);
    menu.addAction(m_cutAct);
    menu.addAction(m_copyAct);
    menu.addAction(m_pasteAct);
    menu.addAction(m_deleteAct);
    menu.addSeparator();
    QAction* insRow = menu.addAction("Insert Row");
    QAction* delRow = menu.addAction("Delete Row");
    QAction* insCol = menu.addAction("Insert Column");
    QAction* delCol = menu.addAction("Delete Column");
    menu.addSeparator();
    QAction* mdCopy  = menu.addAction(calcIcon("table"), "Copy as Markdown Table");
    QAction* jsonCopy = menu.addAction(calcIcon("table"), "Copy as JSON");
    QAction* yamlCopy = menu.addAction(calcIcon("table"), "Copy as YAML");
    QAction* pandas  = menu.addAction(calcIcon("sigma"), "Export as Pandas Code");
    QAction* condFmt = menu.addAction(calcIcon("fill"),  "Conditional Formatting…");

    const QAction* a = menu.exec(m_tableView->viewport()->mapToGlobal(pos));
    if      (a == insRow)  m_model->insertRowAt(row);
    else if (a == delRow)  m_model->deleteRowAt(row);
    else if (a == insCol)  m_model->insertColumnAt(col);
    else if (a == delCol)  m_model->deleteColumnAt(col);
    else if (a == mdCopy)  copySelectionAsMarkdown();
    else if (a == jsonCopy) copySelectionAsJson();
    else if (a == yamlCopy) copySelectionAsYaml();
    else if (a == pandas)  exportSelectionAsPandas();
    else if (a == condFmt) showConditionalFormatDialog();
}

// ═════════════════════════════════════════════════════════════════════════════
// Feature 1 — Copy as Markdown Table
// ═════════════════════════════════════════════════════════════════════════════
void CalcModule::copySelectionAsMarkdown() {
    const QRect r = selectedRect();
    if (r.width() == 0 || r.height() == 0) return;
    QGuiApplication::clipboard()->setText(rangeToMarkdown(m_model, r));
    showToast("Copied as Markdown!");
}

// ═════════════════════════════════════════════════════════════════════════════
// JSON / YAML converter (see StructuredData.h)
// ═════════════════════════════════════════════════════════════════════════════

// Selection → Table, taking the first selected row as the field names. Same
// convention as the Markdown export above, so the two behave alike.
static StructuredData::Table rangeToTable(const SpreadsheetModel* m, const QRect& r) {
    StructuredData::Table t;
    for (int c = r.left(); c <= r.right(); ++c)
        t.headers << m->displayValue(c, r.top());
    for (int row = r.top() + 1; row <= r.bottom(); ++row) {
        QStringList vals;
        for (int c = r.left(); c <= r.right(); ++c)
            vals << m->displayValue(c, row);
        // Skip rows that are entirely blank rather than emitting empty records.
        bool anything = false;
        for (const QString& v : vals) if (!v.isEmpty()) { anything = true; break; }
        if (anything) t.rows.append(vals);
    }
    return t;
}

void CalcModule::importStructuredData() {
    StructuredDataDialog dlg(SpreadsheetModel::NUM_ROWS, SpreadsheetModel::NUM_COLS, this);
    if (dlg.exec() != QDialog::Accepted) return;

    const StructuredData::Table t = dlg.table();
    if (t.headers.isEmpty()) return;

    // Anchor: the selection's top-left, or A1. Clamped so a selection near the
    // right edge cannot push the table off the grid entirely.
    int baseCol = 0, baseRow = 0;
    if (dlg.insertAtSelection()) {
        const QRect sel = selectedRect();
        baseCol = qBound(0, sel.left(), SpreadsheetModel::NUM_COLS - 1);
        baseRow = qBound(0, sel.top(),  SpreadsheetModel::NUM_ROWS - 1);
    }

    std::vector<std::pair<QPoint, Cell>> edits;
    for (int c = 0; c < t.headers.size(); ++c) {
        const int col = baseCol + c;
        if (col >= SpreadsheetModel::NUM_COLS) break;
        Cell cell;
        cell.content = t.headers.at(c);
        cell.format.bold = true;              // header row reads as a header
        edits.push_back({ QPoint(col, baseRow), cell });
    }
    for (int rIdx = 0; rIdx < t.rows.size(); ++rIdx) {
        const int row = baseRow + 1 + rIdx;
        if (row >= SpreadsheetModel::NUM_ROWS) break;
        const QStringList& vals = t.rows.at(rIdx);
        for (int c = 0; c < vals.size(); ++c) {
            const int col = baseCol + c;
            if (col >= SpreadsheetModel::NUM_COLS) break;
            Cell cell;
            // A leading '=' would otherwise be evaluated as a formula, which is
            // not what a JSON string value means.
            cell.content = vals.at(c).startsWith(QLatin1Char('='))
                               ? QLatin1Char('\'') + vals.at(c)
                               : vals.at(c);
            edits.push_back({ QPoint(col, row), cell });
        }
    }

    if (edits.empty()) return;
    m_model->applyCellEdits(edits, "Import JSON/YAML");
    showToast(QString("Imported %1 rows.").arg(t.rows.size()));
}

void CalcModule::copySelectionAsJson() {
    const QRect r = selectedRect();
    if (r.width() == 0 || r.height() < 2) {
        featureInfo("Copy as JSON",
                    "Select at least two rows: the first is used as the field names.");
        return;
    }
    QGuiApplication::clipboard()->setText(StructuredData::toJson(rangeToTable(m_model, r)));
    showToast("Copied as JSON!");
}

void CalcModule::copySelectionAsYaml() {
    const QRect r = selectedRect();
    if (r.width() == 0 || r.height() < 2) {
        featureInfo("Copy as YAML",
                    "Select at least two rows: the first is used as the field names.");
        return;
    }
    QGuiApplication::clipboard()->setText(StructuredData::toYaml(rangeToTable(m_model, r)));
    showToast("Copied as YAML!");
}

// ═════════════════════════════════════════════════════════════════════════════
// Data Cleanser (see DataCleanser.h)
// ═════════════════════════════════════════════════════════════════════════════

bool CalcModule::requirePremiumFor(const QString& featureName) {
    if (AuthManager::instance().premiumActive()) return true;

    QMessageBox box(this);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(featureName);
    box.setText(featureName + tr(" is a Premium feature."));
    box.setInformativeText(
        tr("Trim Whitespace and Remove Duplicates are free for everyone. Date "
           "standardization and the email and URL extractors need Premium."));
    QPushButton* buy = box.addButton(tr("See Premium"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == buy) AuthManager::instance().openPremiumPage();
    return false;
}

void CalcModule::showDataCleanser() {
    if (!m_cleanserPanel) {
        auto* p = new DataCleanserPanel(this);
        m_cleanserPanel = p;
        connect(p, &DataCleanserPanel::runRequested, this, &CalcModule::runCleanserOp);
        connect(p, &DataCleanserPanel::closeRequested, this, [this] {
            if (!m_cleanserPanel) return;
            DataCleanserPanel* dead = m_cleanserPanel;
            m_cleanserPanel = nullptr;
            dead->deleteLater();
        });
        // Live entitlement, so buying Premium in the browser unlocks the gated
        // actions without reopening the panel.
        auto& auth = AuthManager::instance();
        p->setPremiumActive(auth.premiumActive());
        connect(&auth, &AuthManager::entitlementChanged, p,
                [p](bool on) { p->setPremiumActive(on); });
    }
    placeFloatingPanels();
    m_cleanserPanel->show();
    m_cleanserPanel->raise();
}

void CalcModule::runCleanserOp(DataCleanser::Op op) {
    if (!m_cleanserPanel) return;
    const QString name = DataCleanser::opName(op);

    if (DataCleanser::requiresPremium(op) && !requirePremiumFor(name)) return;

    // Region: the selection, or the whole used range.
    QRect region;
    if (m_cleanserPanel->wholeSheet()) {
        int c1, r1, c2, r2;
        if (!usedRange(m_model, c1, r1, c2, r2)) {
            m_cleanserPanel->setStatus(tr("The sheet is empty."), true);
            return;
        }
        region = QRect(QPoint(c1, r1), QPoint(c2, r2));
    } else {
        region = selectedRect();
        if (region.width() <= 0 || region.height() <= 0) {
            m_cleanserPanel->setStatus(tr("Select some cells first."), true);
            return;
        }
        // A single cell almost always means "I clicked somewhere", not "clean
        // exactly this one cell", so widen to the used range rather than doing
        // something the user will not notice.
        if (region.width() == 1 && region.height() == 1) {
            int c1, r1, c2, r2;
            if (usedRange(m_model, c1, r1, c2, r2))
                region = QRect(QPoint(c1, r1), QPoint(c2, r2));
        }
    }

    const DataCleanser::Options opt = m_cleanserPanel->options();
    auto reader = [this](int col, int row) { return m_model->rawContent(col, row); };

    DataCleanser::Result res;
    switch (op) {
        case DataCleanser::Op::TrimWhitespace:
            res = DataCleanser::trimWhitespace(reader, region, opt);
            break;
        case DataCleanser::Op::RemoveDuplicates:
            res = DataCleanser::removeDuplicateRows(reader, region, opt);
            break;
        case DataCleanser::Op::StandardizeDates:
            res = DataCleanser::standardizeDates(reader, region, opt);
            break;
        case DataCleanser::Op::ExtractEmails:
        case DataCleanser::Op::ExtractUrls: {
            // Results go one column right of the region, provided that column
            // is free and on the grid. Overwriting data to save a click is not
            // a trade worth making.
            const int target = region.right() + 1;
            int usable = -1;
            if (target < SpreadsheetModel::NUM_COLS) {
                bool empty = true;
                for (int row = region.top(); row <= region.bottom(); ++row)
                    if (!m_model->rawContent(target, row).isEmpty()) { empty = false; break; }
                if (empty) usable = target;
            }
            res = (op == DataCleanser::Op::ExtractEmails)
                      ? DataCleanser::extractEmails(reader, region, usable, opt)
                      : DataCleanser::extractUrls(reader, region, usable, opt);
            break;
        }
    }

    if (!res.ok) { m_cleanserPanel->setStatus(res.summary, true); return; }
    if (res.changes.isEmpty()) { m_cleanserPanel->setStatus(res.summary, false); return; }

    // One undo command for the whole operation: a cleanup that took twenty
    // presses of Ctrl+Z to reverse would be worse than not having it.
    std::vector<std::pair<QPoint, Cell>> edits;
    edits.reserve(res.changes.size());
    for (const DataCleanser::Change& ch : res.changes) {
        Cell cell = m_model->cellAt(ch.col, ch.row);   // keep the formatting
        cell.content = ch.text;
        edits.push_back({ QPoint(ch.col, ch.row), cell });
    }
    m_model->applyCellEdits(edits, name);
    m_cleanserPanel->setStatus(res.summary, false);
    showToast(res.summary);
}

// ═════════════════════════════════════════════════════════════════════════════
// SQL on Sheets (see SheetSql.h)
// ═════════════════════════════════════════════════════════════════════════════

namespace {

// Every sheet that has something in it, flattened into the engine's shape.
// The top used row is the header and everything below it is data, matching how
// the Markdown/JSON exporters and the cleanser already read a sheet.
QVector<SheetSql::SourceSheet> collectSheets(const QVector<SpreadsheetModel*>& models) {
    QVector<SheetSql::SourceSheet> out;
    for (SpreadsheetModel* m : models) {
        int c1, r1, c2, r2;
        if (!usedRange(m, c1, r1, c2, r2)) continue;

        SheetSql::SourceSheet s;
        s.name = m->sheetName();
        if (s.name.isEmpty()) continue;
        for (int c = c1; c <= c2; ++c) s.headers << m->displayValue(c, r1);

        for (int row = r1 + 1; row <= r2; ++row) {
            QStringList vals;
            bool anything = false;
            for (int c = c1; c <= c2; ++c) {
                const QString v = m->displayValue(c, row);
                if (!v.isEmpty()) anything = true;
                vals << v;
            }
            if (anything) s.rows.append(vals);   // blank rows are spacing, not data
        }
        out.append(s);
    }
    return out;
}

} // namespace

void CalcModule::showSqlPanel() {
    if (!m_sqlPanel) {
        auto* p = new SheetSqlPanel(this);
        m_sqlPanel = p;
        connect(p, &SheetSqlPanel::runRequested, this, &CalcModule::runSqlQuery);
        connect(p, &SheetSqlPanel::sendToSheetRequested, this, &CalcModule::sendSqlResultToSheet);
        connect(p, &SheetSqlPanel::closeRequested, this, [this] {
            if (!m_sqlPanel) return;
            SheetSqlPanel* dead = m_sqlPanel;
            m_sqlPanel = nullptr;
            m_sqlResult = SheetSql::ResultTable{};
            dead->deleteLater();
        });
    }
    placeFloatingPanels();

    // Show the table names exactly as they must be typed, quoting the ones that
    // are not bare identifiers, so "Q1 Sales" does not look like a syntax error.
    QStringList names;
    for (const SheetSql::SourceSheet& s : collectSheets(m_sheets)) {
        static const QRegularExpression bare(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
        names << (bare.match(s.name).hasMatch() ? s.name : SheetSql::quoteIdentifier(s.name));
    }
    m_sqlPanel->setAvailableTables(names);
    m_sqlPanel->show();
    m_sqlPanel->raise();
}

void CalcModule::runSqlQuery() {
    if (!m_sqlPanel) return;
    const QString sql = m_sqlPanel->query().trimmed();
    if (sql.isEmpty()) { m_sqlPanel->setStatus(tr("Type a query first."), true); return; }

    const QVector<SheetSql::SourceSheet> sheets = collectSheets(m_sheets);
    if (sheets.isEmpty()) {
        m_sqlPanel->setStatus(tr("There is no data to query yet."), true);
        return;
    }

    // Entitlement is decided from the query's shape BEFORE it runs, so a gated
    // query costs nothing and nothing half-happens.
    const SheetSql::QueryInfo info = SheetSql::analyze(sql, sheets);
    if (!info.valid) { m_sqlPanel->setStatus(info.reason, true); return; }

    if (info.multiTable && !requirePremiumFor(tr("Multi-sheet JOIN queries"))) {
        m_sqlPanel->setStatus(
            tr("Queries across more than one sheet need Premium. Single-sheet "
               "SELECT, WHERE, ORDER BY and aggregates are free."), true);
        return;
    }
    if (info.rowsInvolved > SheetSql::kFreeRowLimit
        && !requirePremiumFor(tr("Queries over large sheets"))) {
        m_sqlPanel->setStatus(
            tr("This query touches %1 rows. The free tier covers up to %2.")
                .arg(info.rowsInvolved).arg(SheetSql::kFreeRowLimit), true);
        return;
    }

    const SheetSql::QueryResult res = SheetSql::run(sheets, sql);
    if (!res.ok()) {
        m_sqlResult = SheetSql::ResultTable{};
        m_sqlPanel->clearResult();
        m_sqlPanel->setStatus(res.error, true);
        return;
    }
    m_sqlResult = res.table;
    m_sqlPanel->showResult(res.table, res.truncatedTo);
}

void CalcModule::sendSqlResultToSheet() {
    if (!m_sqlPanel || m_sqlResult.headers.isEmpty()) return;

    // Unique name, so repeated runs do not collide with an existing sheet.
    QString name;
    for (int n = 1; ; ++n) {
        name = QStringLiteral("Query%1").arg(n);
        if (!sheetByName(name)) break;
    }
    addSheet(name);                       // also switches to it, so m_model is the new sheet

    const int maxRows = SpreadsheetModel::NUM_ROWS;
    const int maxCols = SpreadsheetModel::NUM_COLS;

    std::vector<std::pair<QPoint, Cell>> edits;
    for (int c = 0; c < m_sqlResult.headers.size() && c < maxCols; ++c) {
        Cell cell;
        cell.content = m_sqlResult.headers.at(c);
        cell.format.bold = true;
        edits.push_back({ QPoint(c, 0), cell });
    }
    int written = 0;
    for (int r = 0; r < m_sqlResult.rows.size() && r + 1 < maxRows; ++r) {
        const QStringList& row = m_sqlResult.rows.at(r);
        for (int c = 0; c < row.size() && c < maxCols; ++c) {
            Cell cell;
            // A value beginning with '=' is data, not a formula to evaluate.
            cell.content = row.at(c).startsWith(QLatin1Char('='))
                               ? QLatin1Char('\'') + row.at(c) : row.at(c);
            edits.push_back({ QPoint(c, r + 1), cell });
        }
        ++written;
    }
    if (!edits.empty()) m_model->applyCellEdits(edits, QStringLiteral("SQL Result"));

    const int droppedRows = m_sqlResult.rows.size() - written;
    const int droppedCols = qMax(0, m_sqlResult.headers.size() - maxCols);
    QString msg = tr("Wrote %1 row(s) to %2.").arg(written).arg(name);
    if (droppedRows > 0 || droppedCols > 0) {
        msg += QLatin1Char(' ') + tr("The sheet is %1 x %2, so %3 row(s) and %4 column(s) "
                                     "did not fit.")
                                      .arg(maxRows).arg(maxCols).arg(droppedRows).arg(droppedCols);
        m_sqlPanel->setStatus(msg, true);
    } else {
        m_sqlPanel->setStatus(msg, false);
    }
    showToast(msg);
}

// ═════════════════════════════════════════════════════════════════════════════
// Version history (see core/history/DocHistory.h)
// ═════════════════════════════════════════════════════════════════════════════

// Workbook -> the generic key/value model. One key per non-empty cell, named
// "SheetName!col,row", so a delta between two versions is literally the set of
// cells that changed and the diff view can name each one.
//
// Empty cells are omitted rather than stored blank: a 100x26 grid is 2,600
// cells and all but a handful are usually empty, so storing them would make
// every snapshot the same large size and defeat the point of deltas.
DocSnapshot CalcModule::workbookSnapshot() const {
    DocSnapshot snap;
    for (SpreadsheetModel* m : m_sheets) {
        const QString sheet = m->sheetName();
        const auto& cells = m->cells();
        for (auto it = cells.begin(); it != cells.end(); ++it) {
            const Cell& cell = it->second;
            if (cell.content.isEmpty()) continue;
            snap.insert(QStringLiteral("%1!%2,%3")
                            .arg(sheet)
                            .arg(SpreadsheetModel::keyCol(it->first))
                            .arg(SpreadsheetModel::keyRow(it->first)),
                        cell.content);
        }
        // A sheet with no content still has to exist after a restore, so it is
        // recorded explicitly rather than inferred from the cell keys.
        snap.insert(QStringLiteral("%1!#sheet").arg(sheet), QStringLiteral("1"));
    }
    return snap;
}

void CalcModule::applyWorkbookSnapshot(const DocSnapshot& snap) {
    static const QRegularExpression keyRe(QStringLiteral("^(.*)!(\\d+),(\\d+)$"));

    // Group the snapshot by sheet first, so each sheet is written once.
    QHash<QString, QVector<QPair<QPoint, QString>>> bySheet;
    QStringList sheetOrder;
    for (auto it = snap.begin(); it != snap.end(); ++it) {
        if (it.key().endsWith(QStringLiteral("!#sheet"))) {
            const QString name = it.key().left(it.key().size() - 7);
            if (!sheetOrder.contains(name)) sheetOrder << name;
            continue;
        }
        const auto m = keyRe.match(it.key());
        if (!m.hasMatch()) continue;
        const QString name = m.captured(1);
        if (!sheetOrder.contains(name)) sheetOrder << name;
        bySheet[name].append({ QPoint(m.captured(2).toInt(), m.captured(3).toInt()), it.value() });
    }

    for (const QString& name : sheetOrder) {
        SpreadsheetModel* model = sheetByName(name);
        if (!model) { createSheet(name); model = sheetByName(name); }
        if (!model) continue;

        // Clear then rewrite, as one undoable step per sheet: a restore that
        // could not be undone would be its own way of losing work.
        std::vector<std::pair<QPoint, Cell>> edits;
        const auto& existing = model->cells();
        for (auto it = existing.begin(); it != existing.end(); ++it) {
            if (it->second.content.isEmpty()) continue;
            edits.push_back({ QPoint(SpreadsheetModel::keyCol(it->first),
                                     SpreadsheetModel::keyRow(it->first)), Cell{} });
        }
        for (const auto& cv : bySheet.value(name)) {
            Cell cell = model->cellAt(cv.first.x(), cv.first.y());
            cell.content = cv.second;
            edits.push_back({ cv.first, cell });
        }
        if (!edits.empty()) model->applyCellEdits(edits, QStringLiteral("Restore Version"));
    }
    refreshChartsData();
    markDirty();
}

void CalcModule::showHistoryPanel() {
    if (!m_historyPanel) {
        auto* p = new CalcHistoryPanel(this);
        m_historyPanel = p;
        connect(p, &CalcHistoryPanel::commitRequested,   this, &CalcModule::commitVersion);
        connect(p, &CalcHistoryPanel::rollbackRequested, this, &CalcModule::rollbackToVersion);
        connect(p, &CalcHistoryPanel::compareRequested,  this, &CalcModule::compareVersions);
        connect(p, &CalcHistoryPanel::closeRequested, this, [this] {
            if (!m_historyPanel) return;
            CalcHistoryPanel* dead = m_historyPanel;
            m_historyPanel = nullptr;
            dead->deleteLater();
        });
        // Live entitlement, so buying Premium in the browser drops the badge
        // without reopening the panel.
        auto& auth = AuthManager::instance();
        p->setPremiumActive(auth.premiumActive());
        connect(&auth, &AuthManager::entitlementChanged, p,
                [p](bool on) { p->setPremiumActive(on); });
    }
    placeFloatingPanels();

    DocHistory hist(currentFilePath());
    if (!hist.isUsable()) {
        m_historyPanel->setVersions({});
        m_historyPanel->setStatus(hist.lastError(), true);
    } else {
        m_historyPanel->setVersions(hist.snapshots());
    }
    m_historyPanel->show();
    m_historyPanel->raise();
}

void CalcModule::commitVersion() {
    if (!m_historyPanel) return;
    DocHistory hist(currentFilePath());
    if (!hist.isUsable()) { m_historyPanel->setStatus(hist.lastError(), true); return; }

    const int id = hist.commit(workbookSnapshot(), m_historyPanel->message());
    if (id < 0) { m_historyPanel->setStatus(hist.lastError(), true); return; }
    if (id == 0) {
        m_historyPanel->setStatus(tr("Nothing has changed since the last version."), false);
        return;
    }
    m_historyPanel->clearMessage();
    m_historyPanel->setVersions(hist.snapshots());
    m_historyPanel->setStatus(tr("Saved version %1.").arg(id), false);
    showToast(tr("Version %1 saved.").arg(id));
}

void CalcModule::rollbackToVersion() {
    if (!m_historyPanel) return;
    const QVector<int> ids = m_historyPanel->selectedIds();
    if (ids.size() != 1) {
        m_historyPanel->setStatus(tr("Select exactly one version to restore."), true);
        return;
    }
    DocHistory hist(currentFilePath());
    if (!hist.isUsable()) { m_historyPanel->setStatus(hist.lastError(), true); return; }

    // Restoring overwrites what is on screen, so it is confirmed. The current
    // state is committed first, which means the restore is itself reversible.
    const auto btn = QMessageBox::question(this, tr("Restore version"),
        tr("Replace the current contents with version %1?\n\n"
           "The document as it is now is saved as a version first, so you can "
           "come back to it.").arg(ids.first()),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (btn != QMessageBox::Yes) return;

    hist.commit(workbookSnapshot(), tr("Before restoring v%1").arg(ids.first()));

    bool ok = false;
    const DocSnapshot snap = hist.reconstruct(ids.first(), &ok);
    if (!ok) { m_historyPanel->setStatus(hist.lastError(), true); return; }

    applyWorkbookSnapshot(snap);
    m_historyPanel->setVersions(hist.snapshots());
    m_historyPanel->clearDiff();
    m_historyPanel->setStatus(tr("Restored version %1.").arg(ids.first()), false);
    showToast(tr("Restored version %1.").arg(ids.first()));
}

void CalcModule::compareVersions() {
    if (!m_historyPanel) return;
    if (!requirePremiumFor(tr("Visual version comparison"))) {
        m_historyPanel->setStatus(
            tr("Comparing versions needs Premium. Saving and restoring versions "
               "are free and always will be."), true);
        return;
    }

    const QVector<int> ids = m_historyPanel->selectedIds();
    if (ids.isEmpty() || ids.size() > 2) {
        m_historyPanel->setStatus(
            tr("Select one version to compare with the document as it is now, "
               "or two to compare with each other."), true);
        return;
    }
    DocHistory hist(currentFilePath());
    if (!hist.isUsable()) { m_historyPanel->setStatus(hist.lastError(), true); return; }

    bool ok = false;
    QVector<DocChange> changes;
    QString caption;
    if (ids.size() == 1) {
        changes = hist.diffAgainst(ids.first(), workbookSnapshot(), &ok);
        caption = tr("v%1 compared with the document now:").arg(ids.first());
    } else {
        changes = hist.diff(ids.first(), ids.last(), &ok);
        caption = tr("v%1 compared with v%2:").arg(ids.first()).arg(ids.last());
    }
    if (!ok) { m_historyPanel->setStatus(hist.lastError(), true); return; }
    m_historyPanel->showDiff(changes, caption);
    m_historyPanel->setStatus(QString(), false);
}

// ═════════════════════════════════════════════════════════════════════════════
// Feature 3 — Export Selection as Pandas Python Code (live, non-blocking panel)
// ═════════════════════════════════════════════════════════════════════════════
void CalcModule::exportSelectionAsPandas() {
    const QRect r = selectedRect();
    if (r.width() == 0 || r.height() == 0) return;

    if (!m_pandasPanel) {
        auto* w = new PandasCodeWidget(this);
        m_pandasPanel = w;
        w->onModeChanged = [this]{ updatePandasCode(); };
        w->onCopy = [this]{
            auto* p = static_cast<PandasCodeWidget*>(m_pandasPanel);
            QGuiApplication::clipboard()->setText(p->code());
            showToast("Pandas code copied!");
        };
        w->onClose = [this]{
            QWidget* p = m_pandasPanel;
            m_pandasPanel = nullptr;
            if (p) p->deleteLater();
        };
    }
    placeFloatingPanels();
    updatePandasCode();
    m_pandasPanel->show();
    m_pandasPanel->raise();
}

void CalcModule::updatePandasCode() {
    if (!m_pandasPanel) return;
    auto* p = static_cast<PandasCodeWidget*>(m_pandasPanel);
    const QRect r = selectedRect();
    if (r.width() == 0 || r.height() == 0) return;
    p->setCode(rangeToPandas(m_model, r, p->csvMode()));
}

// ── Toast / snackbar ─────────────────────────────────────────────────────────
void CalcModule::showToast(const QString& message) {
    if (!m_toast) {
        m_toast = new QLabel(this);
        m_toast->setObjectName("calcToast");
        m_toast->setAlignment(Qt::AlignCenter);
        m_toast->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_toast->setStyleSheet(
            "QLabel#calcToast{background:rgba(32,34,44,235);color:#FFFFFF;"
            "padding:9px 20px;border-radius:18px;font-size:12px;font-weight:bold;}");
        m_toast->hide();
    }
    m_toast->setText(message);
    m_toast->adjustSize();
    m_toast->move((width() - m_toast->width()) / 2,
                  std::max(8, height() - m_toast->height() - 48));
    m_toast->raise();
    m_toast->show();
    QTimer::singleShot(1900, m_toast, [this]{ if (m_toast) m_toast->hide(); });
}

// ═════════════════════════════════════════════════════════════════════════════
// Feature 2 — Conditional Formatting with formula rules (add / manage)
// ═════════════════════════════════════════════════════════════════════════════
void CalcModule::showConditionalFormatDialog() {
    if (m_condDialog) {
        m_condDialog->show();
        m_condDialog->raise();
        m_condDialog->activateWindow();
        return;
    }

    auto* dlg = new QDialog(this);
    m_condDialog = dlg;
    dlg->setWindowTitle("Conditional Formatting");
    dlg->setModal(false);
    dlg->resize(520, 420);
    connect(dlg, &QObject::destroyed, this, [this]{ m_condDialog = nullptr; });

    auto* root = new QVBoxLayout(dlg);

    // ── Existing rules list (Manage Rules) ────────────────────────────────────
    root->addWidget(new QLabel("Rules (top = highest priority):", dlg));
    auto* list = new QListWidget(dlg);
    list->setStyleSheet("QListWidget{border:1px solid #D4D4D4;}");
    root->addWidget(list, 1);

    // ── Editor form ───────────────────────────────────────────────────────────
    auto* form = new QFormLayout;
    auto* rangeEdit   = new QLineEdit(dlg);
    auto* formulaEdit = new QLineEdit(dlg);
    formulaEdit->setPlaceholderText("e.g.  =A1<0   or   =$C1=\"Done\"");

    // Colour state lives in shared QColors so the picker lambdas can mutate them.
    auto fillColor = std::make_shared<QColor>();
    auto textColor = std::make_shared<QColor>();

    auto* fillBtn = new QToolButton(dlg);  fillBtn->setText("No fill");  fillBtn->setMinimumWidth(120);
    auto* textBtn = new QToolButton(dlg);  textBtn->setText("Automatic"); textBtn->setMinimumWidth(120);
    auto* boldChk = new QCheckBox("Bold when rule matches", dlg);

    auto swatch = [](QToolButton* b, const QColor& c, const QString& none) {
        if (c.isValid()) {
            b->setText(c.name().toUpper());
            const bool dark = c.lightness() < 130;
            b->setStyleSheet(QString("QToolButton{background:%1;color:%2;border:1px solid #999;"
                                     "padding:3px 8px;}").arg(c.name(), dark ? "#FFF" : "#000"));
        } else {
            b->setText(none);
            b->setStyleSheet("QToolButton{border:1px solid #999;padding:3px 8px;}");
        }
    };

    form->addRow("Apply to range:", rangeEdit);
    form->addRow("Formula rule:",   formulaEdit);
    form->addRow("Fill colour:",    fillBtn);
    form->addRow("Text colour:",    textBtn);
    form->addRow(QString(),         boldChk);
    root->addLayout(form);

    // ── Action buttons ────────────────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout;
    auto* addBtn    = new QPushButton("Add Rule", dlg);
    auto* updateBtn = new QPushButton("Update", dlg);
    auto* deleteBtn = new QPushButton("Delete", dlg);
    auto* closeBtn  = new QPushButton("Close", dlg);
    addBtn->setDefault(true);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(updateBtn);
    btnRow->addWidget(deleteBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);

    // ── Helpers shared by the connections ─────────────────────────────────────
    auto refresh = [this, list] {
        list->clear();
        const auto& rules = m_model->condRules();
        for (const CondFormatRule& r : rules) {
            auto* item = new QListWidgetItem(
                QString("%1   →   %2").arg(mergeToRef(r.range), r.formula));
            if (r.bgColor.isValid())   item->setBackground(r.bgColor);
            if (r.textColor.isValid()) item->setForeground(r.textColor);
            if (r.bold) { QFont f = item->font(); f.setBold(true); item->setFont(f); }
            list->addItem(item);
        }
    };

    // Prefill from the current selection.
    auto prefill = [this, rangeEdit, formulaEdit] {
        const QRect r = selectedRect();
        if (r.width() > 0 && r.height() > 0) {
            rangeEdit->setText(mergeToRef(r));
            if (formulaEdit->text().isEmpty())
                formulaEdit->setText("=" + FormulaEngine::cellAddress(r.left(), r.top()) + ">0");
        }
    };

    swatch(fillBtn, *fillColor, "No fill");
    swatch(textBtn, *textColor, "Automatic");
    prefill();
    refresh();

    // Colour pickers (Ctrl/right-click clears back to none).
    connect(fillBtn, &QToolButton::clicked, this, [=] {
        const QColor c = QColorDialog::getColor(
            fillColor->isValid() ? *fillColor : QColor("#FFF2CC"),
            m_condDialog, "Fill Colour");
        if (c.isValid()) { *fillColor = c; swatch(fillBtn, c, "No fill"); }
    });
    connect(textBtn, &QToolButton::clicked, this, [=] {
        const QColor c = QColorDialog::getColor(
            textColor->isValid() ? *textColor : QColor("#E8372A"),
            m_condDialog, "Text Colour");
        if (c.isValid()) { *textColor = c; swatch(textBtn, c, "Automatic"); }
    });

    // Build a rule from the form (returns false if invalid).
    auto buildRule = [=](CondFormatRule& rule) -> bool {
        const QRect range = parseRangeRef(rangeEdit->text());
        if (!range.isValid() || range.width() == 0 || range.height() == 0) {
            showToast("Enter a valid range, e.g. B1:B10");
            return false;
        }
        QString formula = formulaEdit->text().trimmed();
        if (formula.isEmpty()) { showToast("Enter a formula rule"); return false; }
        if (!formula.startsWith('=')) formula.prepend('=');
        rule.range     = range;
        rule.formula   = formula;
        rule.bgColor   = *fillColor;
        rule.textColor = *textColor;
        rule.bold      = boldChk->isChecked();
        return true;
    };

    connect(addBtn, &QPushButton::clicked, this, [=] {
        CondFormatRule rule;
        if (!buildRule(rule)) return;
        m_model->addCondRule(rule);
        markDirty();
        refresh();
        list->setCurrentRow(m_model->condRules().size() - 1);
        showToast("Rule added");
    });

    connect(updateBtn, &QPushButton::clicked, this, [=] {
        const int i = list->currentRow();
        if (i < 0) { showToast("Select a rule to update"); return; }
        CondFormatRule rule;
        if (!buildRule(rule)) return;
        m_model->updateCondRule(i, rule);
        markDirty();
        refresh();
        list->setCurrentRow(i);
    });

    connect(deleteBtn, &QPushButton::clicked, this, [=] {
        const int i = list->currentRow();
        if (i < 0) { showToast("Select a rule to delete"); return; }
        m_model->removeCondRule(i);
        markDirty();
        refresh();
    });

    // Clicking a rule loads it into the editor for editing.
    connect(list, &QListWidget::currentRowChanged, this, [=](int i) {
        const auto& rules = m_model->condRules();
        if (i < 0 || i >= rules.size()) return;
        const CondFormatRule& r = rules[i];
        rangeEdit->setText(mergeToRef(r.range));
        formulaEdit->setText(r.formula);
        *fillColor = r.bgColor;
        *textColor = r.textColor;
        swatch(fillBtn, r.bgColor, "No fill");
        swatch(textBtn, r.textColor, "Automatic");
        boldChk->setChecked(r.bold);
    });

    connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::close);

    dlg->show();
    dlg->raise();
}

// ─────────────────────────────────────────────────────────────────────────────
// Find / Replace
// ─────────────────────────────────────────────────────────────────────────────
void CalcModule::buildFindDialog() {
    if (m_findDialog) return;

    m_findDialog = new QDialog(this);
    m_findDialog->setWindowTitle("Find and Replace");
    m_findDialog->setModal(false);

    auto* v = new QVBoxLayout(m_findDialog);

    // Find row
    auto* findRow = new QWidget(m_findDialog);
    auto* fl = new QHBoxLayout(findRow);
    fl->setContentsMargins(0, 0, 0, 0);
    auto* findLbl = new QLabel("Find:", findRow);
    findLbl->setFixedWidth(64);
    m_findEdit = new QLineEdit(findRow);
    auto* findNextBtn = new QPushButton("Find Next", findRow);
    auto* findPrevBtn = new QPushButton("Find Prev", findRow);
    fl->addWidget(findLbl);
    fl->addWidget(m_findEdit, 1);
    fl->addWidget(findPrevBtn);
    fl->addWidget(findNextBtn);
    v->addWidget(findRow);

    // Replace row (hidden in Find-only mode)
    m_replaceRow = new QWidget(m_findDialog);
    auto* rl = new QHBoxLayout(m_replaceRow);
    rl->setContentsMargins(0, 0, 0, 0);
    auto* replLbl = new QLabel("Replace:", m_replaceRow);
    replLbl->setFixedWidth(64);
    m_replaceEdit = new QLineEdit(m_replaceRow);
    auto* replBtn    = new QPushButton("Replace", m_replaceRow);
    auto* replAllBtn = new QPushButton("Replace All", m_replaceRow);
    rl->addWidget(replLbl);
    rl->addWidget(m_replaceEdit, 1);
    rl->addWidget(replBtn);
    rl->addWidget(replAllBtn);
    v->addWidget(m_replaceRow);

    m_matchCase = new QCheckBox("Match case", m_findDialog);
    v->addWidget(m_matchCase);

    connect(findNextBtn, &QPushButton::clicked, this, [this]{ findNext(true);  });
    connect(findPrevBtn, &QPushButton::clicked, this, [this]{ findNext(false); });
    connect(m_findEdit,  &QLineEdit::returnPressed, this, [this]{ findNext(true); });
    connect(replBtn,     &QPushButton::clicked, this, &CalcModule::replaceCurrent);
    connect(replAllBtn,  &QPushButton::clicked, this, &CalcModule::replaceAll);
}

void CalcModule::showFindDialog() {
    buildFindDialog();
    m_replaceRow->hide();
    m_findDialog->setWindowTitle("Find");
    m_findDialog->show();
    m_findDialog->raise();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void CalcModule::showReplaceDialog() {
    buildFindDialog();
    m_replaceRow->show();
    m_findDialog->setWindowTitle("Find and Replace");
    m_findDialog->show();
    m_findDialog->raise();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

bool CalcModule::findNext(bool forward) {
    const QString needle = m_findEdit ? m_findEdit->text() : QString();
    if (needle.isEmpty()) return false;
    const auto cs = (m_matchCase && m_matchCase->isChecked())
                        ? Qt::CaseSensitive : Qt::CaseInsensitive;

    const int C = SpreadsheetModel::NUM_COLS;
    const int total = SpreadsheetModel::NUM_ROWS * C;
    const QModelIndex cur = m_tableView->currentIndex();
    const int start = cur.isValid() ? cur.row() * C + cur.column() : -1;

    for (int step = 1; step <= total; ++step) {
        int idx = forward ? (start + step) : (start - step);
        idx = ((idx % total) + total) % total;     // wrap-around
        const int row = idx / C, col = idx % C;
        if (m_model->rawContent(col, row).contains(needle, cs)) {
            const QModelIndex mi = m_model->index(row, col);
            m_tableView->setCurrentIndex(mi);
            m_tableView->scrollTo(mi);
            return true;
        }
    }
    QApplication::beep();
    return false;
}

void CalcModule::replaceCurrent() {
    const QString needle = m_findEdit->text();
    if (needle.isEmpty()) return;
    const auto cs = m_matchCase->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;

    const QModelIndex cur = m_tableView->currentIndex();
    if (cur.isValid()) {
        QString content = m_model->rawContent(cur.column(), cur.row());
        const int i = content.indexOf(needle, 0, cs);
        if (i >= 0) {
            content.replace(i, needle.length(), m_replaceEdit->text());
            m_model->setCellContent(cur.column(), cur.row(), content, "Replace");
        }
    }
    findNext(true);
}

void CalcModule::replaceAll() {
    const QString needle = m_findEdit->text();
    if (needle.isEmpty()) return;
    const QString repl = m_replaceEdit->text();
    const auto cs = m_matchCase->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;

    std::vector<std::pair<QPoint, Cell>> edits;
    // Cells are stored sparsely, so this walks what exists rather than every
    // position in the grid.
    const auto& data = m_model->cells();
    for (auto it = data.begin(); it != data.end(); ++it) {
        Cell c = it->second;
        if (!c.content.contains(needle, cs)) continue;
        c.content.replace(needle, repl, cs);
        edits.push_back({QPoint(SpreadsheetModel::keyCol(it->first),
                                SpreadsheetModel::keyRow(it->first)), c});
    }
    m_model->applyCellEdits(edits, "Replace All");
    QWidget* parent = m_findDialog ? static_cast<QWidget*>(m_findDialog)
                                   : static_cast<QWidget*>(this);
    QMessageBox::information(parent, "Replace All",
                            QString("Replaced in %1 cell(s).").arg(edits.size()));
}

void CalcModule::updateToolbarFromCell() {
    if (!m_fontCombo) return;
    const QModelIndex cur = m_tableView->currentIndex();
    const CellFormat f = cur.isValid()
                             ? m_model->cellAt(cur.column(), cur.row()).format
                             : CellFormat{};

    m_updatingToolbar = true;
    m_fontCombo->setCurrentText(f.fontFamily.isEmpty() ? "Calibri" : f.fontFamily);
    m_sizeCombo->setCurrentText(QString::number(f.fontSize > 0 ? f.fontSize : 11));
    m_boldBtn->setChecked(f.bold);
    m_italicBtn->setChecked(f.italic);
    m_underlineBtn->setChecked(f.underline);
    if (m_strikeBtn) m_strikeBtn->setChecked(f.strike);
    m_alignLeftBtn  ->setChecked(f.hAlign == Qt::AlignLeft);
    m_alignCenterBtn->setChecked(f.hAlign == Qt::AlignHCenter);
    m_alignRightBtn ->setChecked(f.hAlign == Qt::AlignRight);
    m_alignTopBtn   ->setChecked(f.vAlign == Qt::AlignTop);
    m_alignMidBtn   ->setChecked(f.vAlign == Qt::AlignVCenter);
    m_alignBotBtn   ->setChecked(f.vAlign == Qt::AlignBottom);
    m_wrapBtn       ->setChecked(f.wrap);

    // Reflect the number format (fall back to General when unrecognised).
    int fmtIdx = m_numFmtCombo->findData(f.numberFormat);
    if (fmtIdx < 0) fmtIdx = 0;
    m_numFmtCombo->setCurrentIndex(fmtIdx);

    m_updatingToolbar = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Clipboard
// ─────────────────────────────────────────────────────────────────────────────
void CalcModule::setClipboardRange(const QRect& rect, bool isCut) {
    m_clipRange = rect;
    m_clipIsCut = isCut;
    m_ants->setRange(rect, isCut);
}

void CalcModule::clearMarchingAnts() {
    m_clipRange = QRect();
    m_clipIsCut = false;
    m_ants->setRange(QRect(), false);
}

void CalcModule::copySelection() {
    const QRect r = selectedRect();
    if (r.width() == 0 || r.height() == 0) return;

    // Build TSV (raw content) for interop + a JSON block for in-app fidelity.
    QString tsv;
    QJsonArray cellsJson;
    for (int row = r.top(); row <= r.bottom(); ++row) {
        QStringList rowParts;
        for (int col = r.left(); col <= r.right(); ++col) {
            const Cell c = m_model->cellAt(col, row);
            // TSV: escape tabs/newlines minimally
            QString text = c.content;
            text.replace('\t', ' ').replace('\n', ' ');
            rowParts << text;

            if (!c.isEmpty()) {
                QJsonObject o;
                o["dr"] = row - r.top();
                o["dc"] = col - r.left();
                if (!c.content.isEmpty()) o["content"] = c.content;
                if (!c.format.isDefault()) o["fmt"] = formatToJson(c.format);
                cellsJson.append(o);
            }
        }
        tsv += rowParts.join('\t');
        if (row != r.bottom()) tsv += '\n';
    }

    QJsonObject block;
    block["rows"]  = r.height();
    block["cols"]  = r.width();
    block["cells"] = cellsJson;

    auto* mime = new QMimeData;
    mime->setText(tsv);
    mime->setData(kCellsMime, QJsonDocument(block).toJson(QJsonDocument::Compact));
    QGuiApplication::clipboard()->setMimeData(mime);

    setClipboardRange(r, /*isCut=*/false);
}

void CalcModule::cutSelection() {
    copySelection();
    // Excel-style deferred cut: data stays until paste; just flag it.
    setClipboardRange(selectedRect(), /*isCut=*/true);
}

void CalcModule::pasteClipboard() {
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    if (!mime) return;

    // ── Image from the system clipboard → floating picture on the grid ──────
    if (mime->hasImage()) {
        const QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) { insertImagePixmap(QPixmap::fromImage(img)); return; }
    }

    const QModelIndex cur = m_tableView->currentIndex();
    if (!cur.isValid()) return;
    const int anchorCol = cur.column();
    const int anchorRow = cur.row();

    std::vector<std::pair<QPoint, Cell>> edits;

    if (mime->hasFormat(kCellsMime)) {
        // ── In-app block: restore content + format ──────────────────────────
        const QJsonObject block =
            QJsonDocument::fromJson(mime->data(kCellsMime)).object();
        const int rows = block.value("rows").toInt();
        const int cols = block.value("cols").toInt();

        // Clear the whole target rectangle first (so empty source cells erase).
        for (int dr = 0; dr < rows; ++dr)
            for (int dc = 0; dc < cols; ++dc) {
                const int tc = anchorCol + dc, tr = anchorRow + dr;
                if (tc < SpreadsheetModel::NUM_COLS && tr < SpreadsheetModel::NUM_ROWS)
                    edits.push_back({QPoint(tc, tr), Cell{}});
            }
        for (const QJsonValue& v : block.value("cells").toArray()) {
            const QJsonObject o = v.toObject();
            const int tc = anchorCol + o.value("dc").toInt();
            const int tr = anchorRow + o.value("dr").toInt();
            if (tc >= SpreadsheetModel::NUM_COLS || tr >= SpreadsheetModel::NUM_ROWS)
                continue;
            Cell c;
            c.content = o.value("content").toString();
            if (o.contains("fmt")) c.format = jsonToFormat(o.value("fmt").toObject());
            edits.push_back({QPoint(tc, tr), c});
        }
    } else if (mime->hasText()) {
        // ── Plain text / TSV: content only ──────────────────────────────────
        const QStringList lines = mime->text().split('\n');
        for (int dr = 0; dr < lines.size(); ++dr) {
            const QStringList parts = lines[dr].split('\t');
            for (int dc = 0; dc < parts.size(); ++dc) {
                const int tc = anchorCol + dc, tr = anchorRow + dr;
                if (tc >= SpreadsheetModel::NUM_COLS || tr >= SpreadsheetModel::NUM_ROWS)
                    continue;
                Cell c = m_model->cellAt(tc, tr);
                c.content = parts[dc];
                edits.push_back({QPoint(tc, tr), c});
            }
        }
    } else {
        return;
    }

    // If this was a cut, also clear the source cells that aren't overwritten.
    if (m_clipIsCut && !m_clipRange.isNull()) {
        const QRect tgt(QPoint(anchorCol, anchorRow),
                        QSize(m_clipRange.width(), m_clipRange.height()));
        for (int row = m_clipRange.top(); row <= m_clipRange.bottom(); ++row)
            for (int col = m_clipRange.left(); col <= m_clipRange.right(); ++col)
                if (!tgt.contains(col, row))
                    edits.push_back({QPoint(col, row), Cell{}});
    }

    m_model->applyCellEdits(edits, m_clipIsCut ? "Move Cells" : "Paste");
    clearMarchingAnts();
}

void CalcModule::deleteSelection() {
    const QRect r = selectedRect();
    if (r.width() == 0 || r.height() == 0) return;

    std::vector<std::pair<QPoint, Cell>> edits;
    for (int row = r.top(); row <= r.bottom(); ++row)
        for (int col = r.left(); col <= r.right(); ++col)
            edits.push_back({QPoint(col, row), Cell{}});
    m_model->applyCellEdits(edits, "Clear Cells");
    clearMarchingAnts();
}

// ─────────────────────────────────────────────────────────────────────────────
// Event filter — keep the marching-ants overlay sized to the viewport.
// ─────────────────────────────────────────────────────────────────────────────
// Every clamp below is a max(8, ...) so a panel can never be pushed off the left
// or top edge. Those clamps only do their job because this runs again on each
// resize; computed once at construction they just preserve the old window's
// geometry. See the note on the declaration in CalcModule.h.
void CalcModule::placeFloatingPanels() {
    if (m_cleanserPanel) {
        const int pw = m_cleanserPanel->width();       // fixed at construction
        m_cleanserPanel->setGeometry(std::max(8, width() - pw - 24), 96, pw,
                                     std::max(360, height() - 160));
    }
    if (m_sqlPanel) {
        const int pw = std::min(720, std::max(420, width()  - 80));
        const int ph = std::min(460, std::max(320, height() - 120));
        m_sqlPanel->setGeometry(std::max(8, (width() - pw) / 2),
                                std::max(8, height() - ph - 40), pw, ph);
    }
    if (m_historyPanel) {
        const int pw = std::min(640, std::max(400, width()  - 80));
        const int ph = std::min(560, std::max(360, height() - 100));
        m_historyPanel->setGeometry(std::max(8, (width() - pw) / 2), 60, pw, ph);
    }
    if (m_pandasPanel) {
        const int pw = 460, ph = 320;
        m_pandasPanel->setGeometry(std::max(8, width()  - pw - 24),
                                   std::max(8, height() - ph - 60), pw, ph);
    }
}

void CalcModule::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    placeFloatingPanels();
}

// Grab area for the fill handle, given the visual rect of the bottom-right
// selected cell. Deliberately larger than the 6px square that gets painted:
// the drawn handle is a visual cue, the grab area is what has to be hittable.
QRect CalcModule::fillHandleGrabRect(const QRect& bottomRightCell) {
    if (!bottomRightCell.isValid()) return {};
    return QRect(bottomRightCell.right() - 5, bottomRightCell.bottom() - 5, 12, 12);
}

bool CalcModule::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_tableView->viewport()) {
        switch (event->type()) {
        case QEvent::KeyPress: {
            // An object is selected: Delete removes it instead of wiping the
            // cells it happens to be sitting over.
            auto* ke = static_cast<QKeyEvent*>(event);
            if ((ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace)
                && m_selectedObj && deleteSelectedObject())
                return true;
            break;
        }
        case QEvent::Wheel: {
            // Ctrl+wheel zooms, the way Excel and Google Sheets do. Without the
            // modifier the event falls through to normal scrolling.
            auto* we = static_cast<QWheelEvent*>(event);
            if (!(we->modifiers() & Qt::ControlModifier)) break;
            const int steps = we->angleDelta().y();
            if (steps == 0) return true;
            // One notch is 120 units, which works out at 10%. A trackpad sends
            // smaller deltas, so the step is scaled rather than treated as a
            // whole notch, and clamped so a fast flick cannot jump the range.
            const int pct = qBound(-25, steps / 12, 25);
            zoomBy(pct != 0 ? pct : (steps > 0 ? 1 : -1));
            return true;      // swallow it, or the grid scrolls as well
        }
        case QEvent::Resize: {
            const QSize s = m_tableView->viewport()->size();
            if (m_ants)       m_ants->resize(s);
            if (m_selOverlay) m_selOverlay->resize(s);
            updateFrozenViews();
            repositionFloatingObjects();
            break;
        }
        case QEvent::MouseButtonPress: {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                selectFloatingObject(nullptr);         // clicking the grid deselects objects
                const QRect sel = selectedRect();
                const QRect br = m_tableView->visualRect(
                    m_model->index(sel.bottom(), sel.right()));
                // Excel's fill handle is drawn small but its grab area is not.
                // A 6px manhattan radius meant most attempts to drag it landed
                // on the cell instead, so the drag just extended the selection
                // and autofill looked like it did nothing at all.
                if (fillHandleGrabRect(br).contains(me->position().toPoint())) {
                    m_filling = true;
                    m_fillSource = sel;
                    return true;                       // begin fill drag
                }
            }
            break;
        }
        case QEvent::MouseMove: {
            if (m_filling) return true;                // swallow during drag
            // Crosshair over the handle, so it is discoverable as a grab point
            // rather than looking like decoration.
            auto* me = static_cast<QMouseEvent*>(event);
            const QRect sel = selectedRect();
            const QRect br  = m_tableView->visualRect(
                m_model->index(sel.bottom(), sel.right()));
            const bool onHandle = fillHandleGrabRect(br).contains(me->position().toPoint());
            if (onHandle != m_overFillHandle) {
                m_overFillHandle = onHandle;
                if (onHandle) m_tableView->viewport()->setCursor(Qt::CrossCursor);
                else          m_tableView->viewport()->unsetCursor();
            }
            break;
        }
        case QEvent::MouseButtonRelease:
            if (m_filling) {
                auto* me = static_cast<QMouseEvent*>(event);
                m_filling = false;
                const QModelIndex idx = m_tableView->indexAt(me->position().toPoint());
                if (idx.isValid()) {
                    const int dRow = idx.row()    - m_fillSource.bottom();
                    const int dCol = idx.column() - m_fillSource.right();
                    QRect dest = m_fillSource;
                    if (std::abs(dRow) >= std::abs(dCol) && dRow > 0) dest.setBottom(idx.row());
                    else if (dCol > 0)                                dest.setRight(idx.column());
                    if (dest != m_fillSource) fillRange(m_fillSource, dest);
                }
                return true;
            }
            break;
        default: break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ─────────────────────────────────────────────────────────────────────────────
// Styling
// ─────────────────────────────────────────────────────────────────────────────
void CalcModule::applyStyles() {
    const bool dark = ThemeManager::instance().isDark();
    const QString chromeA = dark ? QStringLiteral(R"CSSA_D(
/* ── Module root ─────────────────────────────────────────────────── */
QWidget#calcModule {
    background-color: #12161F;
}

/* ── Ribbon (tabbed toolbar) ─────────────────────────────────────── */
QWidget#calcRibbon {
    background-color: #0D1117;
    border-bottom: 1px solid #2A3344;
}
/* Tab strip */
QWidget#ribbonTabs {
    background-color: #0D1117;
    border-bottom: 1px solid #2A3344;
}
QWidget#ribbonTabs QToolButton#ribbonTab {
    border: none;
    border-radius: 0;
    background: transparent;
    color: #9AA4B8;
    font-size: 13px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-weight: 500;
    padding: 6px 18px;
}
QWidget#ribbonTabs QToolButton#ribbonTab:hover {
    background: #1E2737;
    color: #E6E9F0;
}
QWidget#ribbonTabs QToolButton#ribbonTab:checked {
    background: #12161F;
    color: #E6E9F0;
    font-weight: 700;
    border-bottom: 2px solid #107C41;
}
/* Tool buttons inside the ribbon pages (base) */
QWidget#calcRibbon QToolButton {
    border: 1px solid transparent;
    border-radius: 4px;
    background: transparent;
    color: #E6E9F0;
    font-size: 11px;
}
QWidget#calcRibbon QToolButton:hover {
    background: #123322;
    border: 1px solid #1F5C3C;
}
QWidget#calcRibbon QToolButton:checked {
    background: #164A30;
    border: 1px solid #107C41;
}
/* Large icon-over-text buttons (primary actions) */
QToolButton#ribbonBig {
    padding: 3px 5px 2px 5px;
    font-size: 11px;
    font-family: "Segoe UI", "Inter", sans-serif;
    color: #E6E9F0;
}
QToolButton#ribbonBig::menu-indicator {
    subcontrol-origin: padding;
    subcontrol-position: bottom center;
    bottom: 1px;
    width: 7px; height: 7px;
}
/* Small icon-only buttons in dense clusters */
QToolButton#ribbonSmall { padding: 0; }
/* B / I / U / S letter toggles */
QToolButton#ribbonLetter {
    color: #E6E9F0;
    font-family: "Segoe UI", "Calibri", serif;
}
QWidget#calcRibbon QComboBox,
QWidget#calcRibbon QFontComboBox {
    background: #12161F;
    border: 1px solid #2A3344;
    border-radius: 3px;
    padding: 2px 4px;
    color: #E6E9F0;
    font-size: 12px;
}
QWidget#calcRibbon QComboBox:focus,
QWidget#calcRibbon QFontComboBox:focus {
    border: 1px solid #107C41;
}
QLabel#groupLabel {
    color: #9AA4B8;
    font-size: 9px;
    font-family: "Segoe UI", sans-serif;
}
QWidget#calcRibbon QCheckBox {
    color: #C3CAD8;
    font-size: 11px;
    font-family: "Segoe UI", sans-serif;
    spacing: 4px;
}
QScrollArea#ribbonScroll {
    background: transparent;
    border: none;
}
QScrollArea#ribbonScroll > QWidget > QWidget { background: transparent; }
QFrame#ribbonSep {
    color: #2A3344;
    background-color: #2A3344;
    max-width: 1px;
    margin: 4px 2px;
}

/* ── Formula bar row (clean white) ───────────────────────────────── */
QWidget#formulaBarRow {
    background-color: #12161F;
    border-bottom: 1px solid #2A3344;
}

/* ── Name box ───────────────────────────────────────────────────── */
QLabel#nameBox {
    color: #E6E9F0;
    font-size: 12px;
    font-weight: 700;
    font-family: "Segoe UI", "Inter", monospace;
    background: #17233B;
    border: 1px solid #2A3344;
    border-radius: 4px;
    padding: 0 8px;
}

/* ── fx label ───────────────────────────────────────────────────── */
QLabel#fxLabel {
    color: #9AA4B8;
    font-size: 12px;
    font-style: italic;
    font-family: "Segoe UI", serif;
    background: transparent;
}

/* ── Separators ─────────────────────────────────────────────────── */
QFrame#fbarSep {
    background-color: #2A3344;
    border: none;
}

/* ── Formula bar input ──────────────────────────────────────────── */
QLineEdit#formulaBar {
    background-color: #12161F;
    color: #E6E9F0;
    border: none;
    border-left: none;
    padding: 4px 10px;
    font-size: 13px;
    font-family: "Segoe UI", "Consolas", monospace;
    selection-background-color: #107C41;
    selection-color: #FFFFFF;
}
QLineEdit#formulaBar:focus {
    background-color: #12161F;
}
QLineEdit#formulaBar::placeholder {
    color: #6B7688;
}
)CSSA_D") : QStringLiteral(R"CSSA_L(
/* ── Module root ─────────────────────────────────────────────────── */
QWidget#calcModule {
    background-color: #FFFFFF;
}

/* ── Ribbon (tabbed toolbar) ─────────────────────────────────────── */
QWidget#calcRibbon {
    background-color: #F6F6F6;
    border-bottom: 1px solid #D4D4D4;
}
/* Tab strip */
QWidget#ribbonTabs {
    background-color: #F7F8FA;
    border-bottom: 1px solid #E3E5EA;
}
QWidget#ribbonTabs QToolButton#ribbonTab {
    border: none;
    border-radius: 0;
    background: transparent;
    color: #5A6071;
    font-size: 13px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-weight: 500;
    padding: 6px 18px;
}
QWidget#ribbonTabs QToolButton#ribbonTab:hover {
    background: #E7E9EE;
    color: #1C1E26;
}
QWidget#ribbonTabs QToolButton#ribbonTab:checked {
    background: #FFFFFF;
    color: #1C1E26;
    font-weight: 700;
    border-bottom: 2px solid #107C41;
}
/* Tool buttons inside the ribbon pages (base) */
QWidget#calcRibbon QToolButton {
    border: 1px solid transparent;
    border-radius: 4px;
    background: transparent;
    color: #3A3D42;
    font-size: 11px;
}
QWidget#calcRibbon QToolButton:hover {
    background: #EAF2EC;
    border: 1px solid #BFD8C8;
}
QWidget#calcRibbon QToolButton:checked {
    background: #D6EBDD;
    border: 1px solid #107C41;
}
/* Large icon-over-text buttons (primary actions) */
QToolButton#ribbonBig {
    padding: 3px 5px 2px 5px;
    font-size: 11px;
    font-family: "Segoe UI", "Inter", sans-serif;
    color: #3A3F4B;
}
QToolButton#ribbonBig::menu-indicator {
    subcontrol-origin: padding;
    subcontrol-position: bottom center;
    bottom: 1px;
    width: 7px; height: 7px;
}
/* Small icon-only buttons in dense clusters */
QToolButton#ribbonSmall { padding: 0; }
/* B / I / U / S letter toggles */
QToolButton#ribbonLetter {
    color: #3A3D42;
    font-family: "Segoe UI", "Calibri", serif;
}
QWidget#calcRibbon QComboBox,
QWidget#calcRibbon QFontComboBox {
    background: #FFFFFF;
    border: 1px solid #C8C8C8;
    border-radius: 3px;
    padding: 2px 4px;
    color: #1C1E26;
    font-size: 12px;
}
QWidget#calcRibbon QComboBox:focus,
QWidget#calcRibbon QFontComboBox:focus {
    border: 1px solid #107C41;
}
QLabel#groupLabel {
    color: #8A8A8A;
    font-size: 9px;
    font-family: "Segoe UI", sans-serif;
}
QWidget#calcRibbon QCheckBox {
    color: #444444;
    font-size: 11px;
    font-family: "Segoe UI", sans-serif;
    spacing: 4px;
}
QScrollArea#ribbonScroll {
    background: transparent;
    border: none;
}
QScrollArea#ribbonScroll > QWidget > QWidget { background: transparent; }
QFrame#ribbonSep {
    color: #E0E0E0;
    background-color: #E0E0E0;
    max-width: 1px;
    margin: 4px 2px;
}

/* ── Formula bar row (clean white) ───────────────────────────────── */
QWidget#formulaBarRow {
    background-color: #FFFFFF;
    border-bottom: 1px solid #E2E4E9;
}

/* ── Name box ───────────────────────────────────────────────────── */
QLabel#nameBox {
    color: #1C1E26;
    font-size: 12px;
    font-weight: 700;
    font-family: "Segoe UI", "Inter", monospace;
    background: #F5F6F8;
    border: 1px solid #E2E4E9;
    border-radius: 4px;
    padding: 0 8px;
}

/* ── fx label ───────────────────────────────────────────────────── */
QLabel#fxLabel {
    color: #8A90A0;
    font-size: 12px;
    font-style: italic;
    font-family: "Segoe UI", serif;
    background: transparent;
}

/* ── Separators ─────────────────────────────────────────────────── */
QFrame#fbarSep {
    background-color: #E2E4E9;
    border: none;
}

/* ── Formula bar input ──────────────────────────────────────────── */
QLineEdit#formulaBar {
    background-color: #FFFFFF;
    color: #1C1E26;
    border: none;
    border-left: none;
    padding: 4px 10px;
    font-size: 13px;
    font-family: "Segoe UI", "Consolas", monospace;
    selection-background-color: #107C41;
    selection-color: #FFFFFF;
}
QLineEdit#formulaBar:focus {
    background-color: #FFFFFF;
}
QLineEdit#formulaBar::placeholder {
    color: #AEB4C0;
}
)CSSA_L");
    // Grid (QTableView) — Excel look. Intentionally NOT theme-dependent:
    // the sheet canvas stays white/paper-like in both modes, same as
    // Writer's page and Impress's slide canvas.
    const QString canvas = QStringLiteral(R"CSSB(
/* ── Grid (QTableView) — Excel look ──────────────────────────────── */
QTableView#calcGrid {
    background-color: #FFFFFF;
    gridline-color: #E2E2E2;          /* soft thin Excel gridline */
    border: none;
    outline: 0;                        /* no dotted focus rectangle */
    selection-background-color: #E6EFE9;
    selection-color: #1C1E26;
    font-size: 11px;
    font-family: "Calibri", "Segoe UI", "Inter", sans-serif;
}

/* Cell fills / fonts / colours / selection are painted by CalcItemDelegate;
   no ::item rules here, since a stylesheet item rule would make Qt ignore the
   model's BackgroundRole. The native gridline still draws the cell edges. */

/* Corner button (top-left intersection of headers) — light Excel header */
QTableView#calcGrid QAbstractButton {
    background-color: #F5F5F5;
    border: none;
    border-right:  1px solid #D4D4D4;
    border-bottom: 1px solid #D4D4D4;
}
QTableView#calcGrid QAbstractButton:hover {
    background-color: #ECECEC;
}
)CSSB");
    const QString chromeC = dark ? QStringLiteral(R"CSSC_D(
/* ── Sheet tab bar (bottom, Excel style) ─────────────────────────── */
QWidget#calcTabBar {
    background-color: #0D1117;
    border-top: 1px solid #2A3344;
}
QToolButton#sheetTab, QToolButton#sheetTabActive {
    border: 1px solid #2A3344;
    border-bottom: none;
    border-top-left-radius: 4px;
    border-top-right-radius: 4px;
    padding: 3px 14px;
    margin-top: 4px;
    color: #C3CAD8;
    font-size: 12px;
    font-family: "Segoe UI", sans-serif;
}
QToolButton#sheetTab {
    background-color: #0D1117;
}
QToolButton#sheetTabActive {
    background-color: #12161F;
    color: #107C41;
    font-weight: 700;
    border-top: 2px solid #107C41;
}
QToolButton#sheetTab:hover {
    background-color: #17233B;
}
QToolButton#sheetAddBtn {
    border: none;
    background: transparent;
    color: #107C41;
    font-size: 16px;
    font-weight: 700;
    padding: 2px 8px;
    margin-top: 4px;
}
QToolButton#sheetAddBtn:hover {
    background-color: #123322;
    border-radius: 4px;
}
)CSSC_D") : QStringLiteral(R"CSSC_L(
/* ── Sheet tab bar (bottom, Excel style) ─────────────────────────── */
QWidget#calcTabBar {
    background-color: #F0F0F0;
    border-top: 1px solid #D4D4D4;
}
QToolButton#sheetTab, QToolButton#sheetTabActive {
    border: 1px solid #D4D4D4;
    border-bottom: none;
    border-top-left-radius: 4px;
    border-top-right-radius: 4px;
    padding: 3px 14px;
    margin-top: 4px;
    color: #444444;
    font-size: 12px;
    font-family: "Segoe UI", sans-serif;
}
QToolButton#sheetTab {
    background-color: #E1E1E1;
}
QToolButton#sheetTabActive {
    background-color: #FFFFFF;
    color: #107C41;
    font-weight: 700;
    border-top: 2px solid #107C41;
}
QToolButton#sheetTab:hover {
    background-color: #EAEAEA;
}
QToolButton#sheetAddBtn {
    border: none;
    background: transparent;
    color: #107C41;
    font-size: 16px;
    font-weight: 700;
    padding: 2px 8px;
    margin-top: 4px;
}
QToolButton#sheetAddBtn:hover {
    background-color: #E2E8E4;
    border-radius: 4px;
}
)CSSC_L");
    setStyleSheet(chromeA + canvas + chromeC);
}

// ─────────────────────────────────────────────────────────────────────────────
// File I/O  (Sprint 8)
// ─────────────────────────────────────────────────────────────────────────────
QString CalcModule::titleString() const {
    const QString base = m_currentPath.isEmpty()
                             ? "Untitled Spreadsheet"
                             : QFileInfo(m_currentPath).fileName();
    return (m_dirty ? "* " : "") + base + " — NativeOffice Calc";
}

void CalcModule::markClean() {
    m_dirty = false;
}

// Serialize one sheet's sparse cells into a JSON array.
static QJsonArray cellsToJson(const SpreadsheetModel* m) {
    QJsonArray cellsArray;
    const auto& data = m->cells();
    for (auto it = data.begin(); it != data.end(); ++it) {
        const int key = it->first;
        const Cell& c = it->second;
        QJsonObject cell;
        cell["col"]   = SpreadsheetModel::keyCol(key);
        cell["row"]   = SpreadsheetModel::keyRow(key);
        cell["value"] = c.content;                  // back-compat key
        if (!c.format.isDefault())
            cell["fmt"] = formatToJson(c.format);
        cellsArray.append(cell);
    }
    return cellsArray;
}

// Load a JSON cell array into a model (raw, no undo).
static void jsonToCells(SpreadsheetModel* m, const QJsonArray& cells) {
    for (const auto& cellVal : cells) {
        const QJsonObject cell = cellVal.toObject();
        const int col = cell["col"].toInt();
        const int row = cell["row"].toInt();
        const QString v = cell.contains("content") ? cell["content"].toString()
                                                    : cell["value"].toString();
        if (col >= 0 && col < SpreadsheetModel::NUM_COLS
            && row >= 0 && row < SpreadsheetModel::NUM_ROWS
            && (!v.isEmpty() || cell.contains("fmt"))) {
            Cell c;
            c.content = v;
            if (cell.contains("fmt")) c.format = jsonToFormat(cell["fmt"].toObject());
            m->applyCellRaw(col, row, c);
        }
    }
}

void CalcModule::buildXlsxSheets(std::vector<XlsxSheet>& out) const {
    for (auto* s : m_sheets) {
        XlsxSheet xs;
        xs.name = s->sheetName();
        const auto& data = s->cells();
        for (auto it = data.begin(); it != data.end(); ++it)
            // A cell that still wears the style it was imported with can
            // be written with that same index, which is what lets the
            // original styles.xml (and the rest of the package) be kept.
            xs.cells.push_back({SpreadsheetModel::keyCol(it->first),
                                SpreadsheetModel::keyRow(it->first),
                                it->second.content,
                                it->second.format,
                                it->second.keepsOriginalStyle()
                                    ? it->second.xfIndex : -1});
        for (const QRect& m : s->merges()) xs.merges.push_back(mergeToRef(m));
        const auto& cw = s->colWidths();
        for (auto it = cw.begin(); it != cw.end(); ++it)
            xs.colWidths.push_back({it.key(), it.value()});
        const auto& rh = s->rowHeights();
        for (auto it = rh.begin(); it != rh.end(); ++it)
            xs.rowHeights.push_back({it.key(), it.value()});

        // Charts drawn over the cells, and the reader the writer needs to make
        // sense of them. Without these the exporter had no chart data at all,
        // so every .xlsx write dropped charts whatever the exporter could do.
        const QVector<ChartSpec>& cs = s->charts();
        xs.charts.assign(cs.begin(), cs.end());
        const QVector<SheetImage>& im = s->images();
        xs.images.assign(im.begin(), im.end());
        // Shapes went the same way charts did before 1.7.5: read on import,
        // never handed to the writer, so a rebuilt package lost every banner,
        // button and rule on the sheet. The preserving path hid it, because
        // there the original drawing part goes back untouched.
        const QVector<SheetShape>& sp = s->shapes();
        xs.shapes.assign(sp.begin(), sp.end());
        xs.cellText = [s](int col, int row) { return s->displayValue(col, row); };

        out.push_back(std::move(xs));
    }
}

bool CalcModule::saveToPath(const QString& path) {
    // ── .xlsx export ─────────────────────────────────────────────────────────
    if (path.endsWith(".xlsx", Qt::CaseInsensitive)) {
        syncChartSpecs();      // capture the active sheet's live chart geometries
        std::vector<XlsxSheet> out;
        buildXlsxSheets(out);
        // Preferred: put the original package back with only the cells
        // replaced, so charts and pictures survive. Falls back to a full
        // rebuild when the workbook cannot be updated that way.
        if (!exportXlsxPreserving(path, out, m_originalXlsx)
            && !exportXlsx(path, out))
            return false;
        m_currentPath = path;
        m_dirty       = false;
        if (m_recovery) { m_recovery->setDocumentPath(path); m_recovery->discard(); }
        emit filePathChanged(path);
        return true;
    }

    // ── .csv export (active sheet) ───────────────────────────────────────────
    if (path.endsWith(".csv", Qt::CaseInsensitive)) {
        QFile cf(path);
        if (!cf.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
            return false;
        // Determine the used bounds.
        int maxCol = -1, maxRow = -1;
        const auto& data = m_model->cells();
        for (auto it = data.begin(); it != data.end(); ++it) {
            maxCol = std::max(maxCol, SpreadsheetModel::keyCol(it->first));
            maxRow = std::max(maxRow, SpreadsheetModel::keyRow(it->first));
        }
        QTextStream cout(&cf);
        cout.setEncoding(QStringConverter::Utf8);
        for (int row = 0; row <= maxRow; ++row) {
            QStringList fields;
            for (int col = 0; col <= maxCol; ++col) {
                QString v = m_model->displayValue(col, row);
                if (v.contains(',') || v.contains('"') || v.contains('\n')) {
                    v.replace('"', "\"\"");
                    v = '"' + v + '"';
                }
                fields << v;
            }
            cout << fields.join(',') << '\n';
        }
        cf.close();
        m_currentPath = path;
        m_dirty       = false;
        if (m_recovery) { m_recovery->setDocumentPath(path); m_recovery->discard(); }
        emit filePathChanged(path);
        return true;
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;

    const QByteArray bytes = buildNoffBytes();
    // Compared against -1, not against the length: the file is open in text
    // mode, so on Windows each newline becomes two bytes on the way out and the
    // count coming back is not the count going in.
    if (f.write(bytes) < 0) { f.close(); return false; }
    f.close();

    m_currentPath = path;
    m_dirty       = false;
    if (m_recovery) {
        m_recovery->setDocumentPath(path);
        m_recovery->discard();   // an explicit save supersedes any snapshot
    }
    emit filePathChanged(path);
    return true;
}

// The whole workbook as .noff bytes.
//
// Split out of saveToPath() because crash recovery needs the same bytes without
// a file to put them in, and a second serializer would drift from this one.
QByteArray CalcModule::buildNoffBytes() {
    syncChartSpecs();          // capture the active sheet's live chart geometries
    QJsonArray sheetsArr;
    for (auto* s : m_sheets) {
        QJsonObject sheetObj;
        sheetObj["name"]  = s->sheetName();
        sheetObj["cells"] = cellsToJson(s);
        if (!s->merges().isEmpty()) {
            QJsonArray mergesArr;
            for (const QRect& m : s->merges()) mergesArr.append(mergeToRef(m));
            sheetObj["merges"] = mergesArr;
        }
        if (!s->colWidths().isEmpty()) {
            QJsonObject cw;
            const auto& w = s->colWidths();
            for (auto it = w.begin(); it != w.end(); ++it)
                cw[QString::number(it.key())] = it.value();
            sheetObj["colw"] = cw;
        }
        if (!s->rowHeights().isEmpty()) {
            QJsonObject rh;
            const auto& h = s->rowHeights();
            for (auto it = h.begin(); it != h.end(); ++it)
                rh[QString::number(it.key())] = it.value();
            sheetObj["rowh"] = rh;
        }
        if (!s->charts().isEmpty()) {
            QJsonArray ca;
            for (const ChartSpec& cs : s->charts()) {
                QJsonObject co;
                co["t"]  = int(cs.type);
                co["c1"] = cs.range.left();  co["r1"] = cs.range.top();
                co["c2"] = cs.range.right(); co["r2"] = cs.range.bottom();
                co["x"]  = cs.geom.x();      co["y"]  = cs.geom.y();
                co["w"]  = cs.geom.width();  co["h"]  = cs.geom.height();
                ca.append(co);
            }
            sheetObj["charts"] = ca;
        }
        if (!s->condRules().isEmpty()) {
            QJsonArray cfa;
            for (const CondFormatRule& cr : s->condRules()) {
                QJsonObject o;
                o["range"]   = mergeToRef(cr.range);
                o["formula"] = cr.formula;
                if (cr.bgColor.isValid())   o["bg"] = cr.bgColor.name(QColor::HexArgb);
                if (cr.textColor.isValid()) o["fg"] = cr.textColor.name(QColor::HexArgb);
                if (cr.bold)                o["bold"] = true;
                cfa.append(o);
            }
            sheetObj["condfmt"] = cfa;
        }
        sheetsArr.append(sheetObj);
    }

    QJsonObject root;
    root["type"]        = "calc";
    root["version"]     = 2;
    root["activeSheet"] = m_activeSheet;
    root["sheets"]      = sheetsArr;

    return QByteArray("<!-- NativeOffice Calc Spreadsheet (.noff) -->\n")
         + QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool CalcModule::loadFromPath(const QString& path) {
    const bool isXlsx = path.endsWith(".xlsx", Qt::CaseInsensitive);
    // Snapshots follow the document that is actually open.
    if (m_recovery) m_recovery->setDocumentPath(path);

    // Read .xlsx (binary) up front so we can bail before tearing down on failure.
    std::vector<XlsxSheet> xlsxSheets;
    QString content;
    QJsonObject root;

    if (isXlsx) {
        if (!importXlsx(path, xlsxSheets)) return false;
        // Kept for a preserving save.
        {
            QFile src(path);
            if (src.open(QIODevice::ReadOnly)) { m_originalXlsx = src.readAll(); src.close(); }
            else                                 m_originalXlsx.clear();
        }
    } else {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
        QTextStream in(&f);
        in.setEncoding(QStringConverter::Utf8);
        content = in.readAll();
        f.close();
        content.remove("<!-- NativeOffice Calc Spreadsheet (.noff) -->\n");
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &err);
        root = doc.isObject() ? doc.object() : QJsonObject();
    }

    m_ignoreChange = true;

    // Tear down the current sheets.
    m_tableView->setModel(nullptr);
    for (auto* s : m_sheets) { m_undoGroup->removeStack(s->undoStack()); s->deleteLater(); }
    m_sheets.clear();
    m_activeSheet = 0;

    if (isXlsx) {
        // ── .xlsx import ─────────────────────────────────────────────────
        for (const XlsxSheet& xs : xlsxSheets) {
            QString name = xs.name.isEmpty() || sheetByName(xs.name)
                               ? QString("Sheet%1").arg(m_sheets.size() + 1) : xs.name;
            SpreadsheetModel* m = createSheet(name);
            for (const XlsxCell& c : xs.cells)
                if (c.col >= 0 && c.col < SpreadsheetModel::NUM_COLS
                    && c.row >= 0 && c.row < SpreadsheetModel::NUM_ROWS
                    && (!c.content.isEmpty() || !c.format.isDefault()))
                    m->applyCellRaw(c.col, c.row,
                                    Cell{c.content, c.format, c.xfIndex, c.format});
            QVector<QRect> mg;
            for (const QString& ref : xs.merges) {
                const QRect r = refToMerge(ref);
                if (r.isValid()) mg.push_back(r);
            }
            m->setMerges(mg);
            QHash<int, int> cw, rh;
            for (const auto& p : xs.colWidths)  cw.insert(p.first, p.second);
            for (const auto& p : xs.rowHeights) rh.insert(p.first, p.second);
            m->setColWidths(cw);
            m->setRowHeights(rh);
            // Objects drawn over the cells. Without this the workbook opened
            // showing only the grid and every chart and picture vanished.
            m->setCharts(QVector<ChartSpec>(xs.charts.begin(), xs.charts.end()));
            m->setImages(QVector<SheetImage>(xs.images.begin(), xs.images.end()));
            m->setShapes(QVector<SheetShape>(xs.shapes.begin(), xs.shapes.end()));
            m->setShowGridLines(xs.showGridLines);
            m->setHidden(xs.hidden);
            m->setCondRules(QVector<CondFormatRule>(xs.condRules.begin(), xs.condRules.end()));
            // Hidden rows and columns are part of the layout: a dashboard uses
            // them as spacers, and showing them shifts everything sideways.
            m->setHiddenCols(QVector<int>(xs.hiddenCols.begin(), xs.hiddenCols.end()));
            m->setHiddenRows(QVector<int>(xs.hiddenRows.begin(), xs.hiddenRows.end()));
            m->setZoomScale(xs.zoomScale);
        }
    } else if (root.contains("sheets")) {
        // ── v2 multi-sheet ───────────────────────────────────────────────
        const QJsonArray sheets = root["sheets"].toArray();
        for (const auto& sv : sheets) {
            const QJsonObject so = sv.toObject();
            QString name = so["name"].toString();
            if (name.isEmpty() || sheetByName(name))
                name = QString("Sheet%1").arg(m_sheets.size() + 1);
            SpreadsheetModel* m = createSheet(name);
            jsonToCells(m, so["cells"].toArray());
            if (so.contains("merges")) {
                QVector<QRect> mg;
                for (const QJsonValue& mv : so["merges"].toArray()) {
                    const QRect r = refToMerge(mv.toString());
                    if (r.isValid()) mg.push_back(r);
                }
                m->setMerges(mg);
            }
            if (so.contains("colw")) {
                QHash<int, int> cw;
                const QJsonObject o = so["colw"].toObject();
                for (auto it = o.begin(); it != o.end(); ++it)
                    cw.insert(it.key().toInt(), it.value().toInt());
                m->setColWidths(cw);
            }
            if (so.contains("rowh")) {
                QHash<int, int> rh;
                const QJsonObject o = so["rowh"].toObject();
                for (auto it = o.begin(); it != o.end(); ++it)
                    rh.insert(it.key().toInt(), it.value().toInt());
                m->setRowHeights(rh);
            }
            if (so.contains("charts")) {
                QVector<ChartSpec> cs;
                for (const QJsonValue& cv : so["charts"].toArray()) {
                    const QJsonObject co = cv.toObject();
                    ChartSpec s;
                    s.type  = chartTypeFromInt(co["t"].toInt());
                    s.range = QRect(QPoint(co["c1"].toInt(), co["r1"].toInt()),
                                    QPoint(co["c2"].toInt(), co["r2"].toInt()));
                    s.geom  = QRect(co["x"].toInt(), co["y"].toInt(),
                                    co["w"].toInt(), co["h"].toInt());
                    cs.push_back(s);
                }
                m->setCharts(cs);
            }
            if (so.contains("condfmt")) {
                QVector<CondFormatRule> rules;
                for (const QJsonValue& cv : so["condfmt"].toArray()) {
                    const QJsonObject o = cv.toObject();
                    CondFormatRule r;
                    r.range   = parseRangeRef(o["range"].toString());
                    r.formula = o["formula"].toString();
                    if (o.contains("bg")) r.bgColor   = QColor(o["bg"].toString());
                    if (o.contains("fg")) r.textColor = QColor(o["fg"].toString());
                    r.bold = o.value("bold").toBool();
                    if (r.range.isValid() && !r.formula.isEmpty()) rules.push_back(r);
                }
                m->setCondRules(rules);
            }
        }
        m_activeSheet = root.value("activeSheet").toInt(0);
    } else if (root.contains("cells")) {
        // ── v1 single-sheet ──────────────────────────────────────────────
        jsonToCells(createSheet("Sheet1"), root["cells"].toArray());
    } else {
        // ── CSV fallback ─────────────────────────────────────────────────
        SpreadsheetModel* m = createSheet("Sheet1");
        const QStringList lines = content.split('\n', Qt::SkipEmptyParts);
        for (int row = 0; row < lines.size() && row < SpreadsheetModel::NUM_ROWS; ++row) {
            const QStringList cols = lines[row].split(',');
            for (int col = 0; col < cols.size() && col < SpreadsheetModel::NUM_COLS; ++col) {
                const QString v = cols[col].trimmed();
                if (!v.isEmpty()) m->applyCellRaw(col, row, Cell{v, CellFormat{}});
            }
        }
    }

    if (m_sheets.isEmpty()) createSheet("Sheet1");
    if (m_activeSheet < 0 || m_activeSheet >= m_sheets.size()) m_activeSheet = 0;

    for (auto* s : m_sheets) { s->notifyAllChanged(); s->undoStack()->clear(); }
    m_ignoreChange = false;

    // Land on the first sheet the user is meant to see.
    for (int i = 0; i < m_sheets.size(); ++i)
        if (!m_sheets[i]->isHidden()) { m_activeSheet = i; break; }
    switchToSheet(m_activeSheet);

    m_currentPath = path;
    m_dirty       = false;
    emit filePathChanged(path);
    // Opening a workbook must not leave it marked as modified. Rebuilding the
    // sheets emits model signals after m_ignoreChange is cleared, which set the
    // dirty flag on a file nobody had edited, and autosave then rewrote it
    // through the exporter, quietly degrading an .xlsx that came from Excel.
    for (int delay : { 0, 250, 700 }) {
        QTimer::singleShot(delay, this, [this] {
            if (!m_dirty) return;
            m_dirty = false;
            emit documentModified();
        });
    }
    return true;
}

void CalcModule::setReadOnly(bool on) {
    const QAbstractItemView::EditTriggers triggers = on
        ? QAbstractItemView::NoEditTriggers
        : (QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
           | QAbstractItemView::AnyKeyPressed);
    for (QTableView* v : { m_tableView, m_frozenTop, m_frozenLeft, m_frozenCorner })
        if (v) v->setEditTriggers(triggers);
    if (m_ribbon) m_ribbon->setEnabled(!on);
}

} // namespace NativeOffice
