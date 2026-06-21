// ─────────────────────────────────────────────────────────────────────────────
// SlideScene.cpp  (Sprint 5 → Sprint 12)
// ─────────────────────────────────────────────────────────────────────────────
#include "SlideScene.h"

#include <QGraphicsRectItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsTextItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsPathItem>
#include <QGraphicsDropShadowEffect>
#include <QPainterPath>
#include <QPolygonF>
#include <QStyleOptionGraphicsItem>
#include <QFocusEvent>
#include <QTextOption>
#include <QPainter>
#include <QPolygonF>
#include <QTextCharFormat>
#include <QGraphicsSceneContextMenuEvent>
#include <QMenu>
#include <QInputDialog>
#include <QLineEdit>
#include <QColorDialog>
#include <QDialog>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QLabel>
#include <QApplication>
#include <QImage>
#include <algorithm>
#include <QGraphicsSceneMouseEvent>
#include <QPen>
#include <QBrush>
#include <QLinearGradient>
#include <QFont>
#include <QColor>
#include <QTextCursor>
#include <QCursor>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QBuffer>
#include <QtMath>
#include <cmath>

namespace NativeOffice {

// Brightness (-100..100 added) + contrast (-100..100 scaled) adjustment.
static QImage adjustImage(const QImage& src, int brightness, int contrast) {
    if (brightness == 0 && contrast == 0) return src;
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const double factor = (100.0 + contrast) / 100.0;
    auto adj = [&](int v) {
        return qBound(0, static_cast<int>((v - 128) * factor + 128 + brightness), 255);
    };
    for (int y = 0; y < img.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = line[x];
            line[x] = qRgba(adj(qRed(px)), adj(qGreen(px)), adj(qBlue(px)), qAlpha(px));
        }
    }
    return img;
}

// ─────────────────────────────────────────────────────────────────────────────
// SlideImageItem — QGraphicsPixmapItem that remembers its full-res source so
// repeated resizes don't compound quality loss, plus brightness/contrast.
// ─────────────────────────────────────────────────────────────────────────────
class SlideImageItem : public QGraphicsPixmapItem {
public:
    explicit SlideImageItem(const QPixmap& original) : m_original(original) {}
    QPixmap original() const { return m_original; }

    int brightness() const { return m_brightness; }
    int contrast()   const { return m_contrast; }

    // Adjusted full-res source (used when re-scaling on resize).
    QPixmap adjustedSource() const {
        return QPixmap::fromImage(adjustImage(m_original.toImage(), m_brightness, m_contrast));
    }

    void setAdjust(int brightness, int contrast) {
        m_brightness = brightness;
        m_contrast   = contrast;
        const QSize sz = pixmap().size().isEmpty() ? m_original.size() : pixmap().size();
        setPixmap(adjustedSource().scaled(sz, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }

private:
    QPixmap m_original;
    int     m_brightness { 0 };
    int     m_contrast   { 0 };
};

// ─────────────────────────────────────────────────────────────────────────────
// SlideShapeItem — QGraphicsPathItem that remembers its ShapeKind and logical
// rect so it can rebuild its geometry when resized, and report both back when
// the scene is serialised.
// ─────────────────────────────────────────────────────────────────────────────
class SlideShapeItem : public QGraphicsPathItem {
public:
    explicit SlideShapeItem(ShapeKind kind) : m_kind(kind) {}
    void setRect(const QRectF& r) { m_rect = r; setPath(SlideScene::shapePath(m_kind, r)); }
    QRectF    rect() const { return m_rect; }
    ShapeKind kind() const { return m_kind; }
private:
    ShapeKind m_kind;
    QRectF    m_rect;
};

// ─────────────────────────────────────────────────────────────────────────────
// TableCellItem — one editable cell. Cells display read-only until the table is
// double-clicked on that cell; on focus loss they revert to read-only so a
// single click/drag moves the whole table rather than editing a cell.
// ─────────────────────────────────────────────────────────────────────────────
class TableCellItem : public QGraphicsTextItem {
public:
    using QGraphicsTextItem::QGraphicsTextItem;
protected:
    void focusOutEvent(QFocusEvent* e) override {
        QGraphicsTextItem::focusOutEvent(e);
        setTextInteractionFlags(Qt::NoTextInteraction);
        QTextCursor c = textCursor();
        c.clearSelection();
        setTextCursor(c);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// SlideTableItem — a rows×cols grid. Cells are child TableCellItems; the table
// itself owns the geometry, paints grid lines + a shaded header row, and lays
// the cells out on resize.
// ─────────────────────────────────────────────────────────────────────────────
class SlideTableItem : public QGraphicsRectItem {
public:
    SlideTableItem(int rows, int cols, const QRectF& r)
        : QGraphicsRectItem(r), m_rows(rows), m_cols(cols)
    {
        setPen(Qt::NoPen);
        setBrush(Qt::white);
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                auto* c = new TableCellItem(this);
                QFont f("Segoe UI", 14);
                if (row == 0) f.setBold(true);
                c->setFont(f);
                c->setDefaultTextColor(QColor("#1C1E26"));
                c->setTextInteractionFlags(Qt::NoTextInteraction);
                c->setFlag(QGraphicsItem::ItemIsFocusable, true);
                m_cells.push_back(c);
            }
        }
        relayout();
    }

    int rows() const { return m_rows; }
    int cols() const { return m_cols; }
    const std::vector<TableCellItem*>& cells() const { return m_cells; }

    void setRectAndLayout(const QRectF& r) { setRect(r); relayout(); }

    void relayout() {
        const QRectF r = rect();
        const qreal cw = r.width()  / m_cols;
        const qreal ch = r.height() / m_rows;
        const qreal pad = 5.0;
        for (int row = 0; row < m_rows; ++row)
            for (int col = 0; col < m_cols; ++col) {
                auto* c = m_cells[row * m_cols + col];
                c->setTextWidth(std::max(10.0, cw - 2 * pad));
                c->setPos(r.left() + col * cw + pad, r.top() + row * ch + pad);
            }
    }

    std::vector<QString> texts() const {
        std::vector<QString> t;
        t.reserve(m_cells.size());
        for (auto* c : m_cells) t.push_back(c->toPlainText());
        return t;
    }

protected:
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* e) override {
        const QRectF r = rect();
        const qreal cw = r.width()  / m_cols;
        const qreal ch = r.height() / m_rows;
        const int col = std::clamp(static_cast<int>((e->pos().x() - r.left()) / cw), 0, m_cols - 1);
        const int row = std::clamp(static_cast<int>((e->pos().y() - r.top())  / ch), 0, m_rows - 1);
        auto* c = m_cells[row * m_cols + col];
        c->setTextInteractionFlags(Qt::TextEditorInteraction);
        c->setFocus();
        QTextCursor cur = c->textCursor();
        cur.select(QTextCursor::Document);
        c->setTextCursor(cur);
        e->accept();
    }

    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override {
        const QRectF r = rect();
        const qreal cw = r.width()  / m_cols;
        const qreal ch = r.height() / m_rows;

        p->fillRect(r, Qt::white);
        p->fillRect(QRectF(r.left(), r.top(), r.width(), ch), QColor("#F0F2F6"));  // header

        QPen grid(QColor("#B9BEC9"));
        grid.setWidthF(1.0);
        p->setPen(grid);
        for (int col = 0; col <= m_cols; ++col) {
            const qreal x = r.left() + col * cw;
            p->drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        }
        for (int row = 0; row <= m_rows; ++row) {
            const qreal y = r.top() + row * ch;
            p->drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        }

        if (isSelected()) {
            QPen sel(QColor("#E8372A"));
            sel.setWidthF(1.5);
            p->setPen(sel);
            p->setBrush(Qt::NoBrush);
            p->drawRect(r);
        }
    }

private:
    int m_rows, m_cols;
    std::vector<TableCellItem*> m_cells;
};

// ─────────────────────────────────────────────────────────────────────────────
// SlideChartItem — a data chart rendered with QPainter. Double-click opens a
// small data editor (title + label/value rows).
// ─────────────────────────────────────────────────────────────────────────────
class SlideChartItem : public QGraphicsRectItem {
public:
    SlideChartItem(ChartKind k, const QRectF& r) : QGraphicsRectItem(r), m_kind(k) {}

    ChartKind kind() const { return m_kind; }
    QString   title() const { return m_title; }
    const std::vector<QString>& labels() const { return m_labels; }
    const std::vector<double>&  values() const { return m_values; }

    void setTitle(const QString& t) { m_title = t; update(); }
    void setData(const std::vector<QString>& l, const std::vector<double>& v) {
        m_labels = l; m_values = v; update();
    }
    void setRectKeep(const QRectF& r) { setRect(r); update(); }

protected:
    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override {
        SlideScene::paintChart(*p, rect(), m_kind, m_labels, m_values, m_title);
        if (isSelected()) {
            QPen sel(QColor("#E8372A")); sel.setWidthF(1.5);
            p->setPen(sel); p->setBrush(Qt::NoBrush); p->drawRect(rect());
        }
    }

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent*) override {
        QDialog dlg(QApplication::activeWindow());
        dlg.setWindowTitle("Edit Chart Data");
        dlg.resize(320, 360);
        auto* v = new QVBoxLayout(&dlg);

        auto* titleEdit = new QLineEdit(m_title, &dlg);
        titleEdit->setPlaceholderText("Chart title");
        v->addWidget(titleEdit);

        auto* table = new QTableWidget(static_cast<int>(m_labels.size()), 2, &dlg);
        table->setHorizontalHeaderLabels({ "Label", "Value" });
        table->horizontalHeader()->setStretchLastSection(true);
        for (size_t i = 0; i < m_labels.size(); ++i) {
            table->setItem(static_cast<int>(i), 0, new QTableWidgetItem(m_labels[i]));
            table->setItem(static_cast<int>(i), 1,
                           new QTableWidgetItem(QString::number(m_values[i])));
        }
        v->addWidget(table);

        auto* btnRow = new QHBoxLayout;
        auto* addBtn = new QPushButton("Add Row", &dlg);
        auto* delBtn = new QPushButton("Remove Row", &dlg);
        btnRow->addWidget(addBtn); btnRow->addWidget(delBtn); btnRow->addStretch();
        v->addLayout(btnRow);
        QObject::connect(addBtn, &QPushButton::clicked, &dlg, [table] {
            table->insertRow(table->rowCount());
        });
        QObject::connect(delBtn, &QPushButton::clicked, &dlg, [table] {
            if (table->currentRow() >= 0) table->removeRow(table->currentRow());
            else if (table->rowCount() > 0) table->removeRow(table->rowCount() - 1);
        });

        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        v->addWidget(bb);
        QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

        if (dlg.exec() != QDialog::Accepted) return;

        m_title = titleEdit->text();
        std::vector<QString> nl; std::vector<double> nv;
        for (int r = 0; r < table->rowCount(); ++r) {
            const auto* li = table->item(r, 0);
            const auto* vi = table->item(r, 1);
            nl.push_back(li ? li->text() : QString());
            nv.push_back(vi ? vi->text().toDouble() : 0.0);
        }
        if (!nl.empty()) { m_labels = nl; m_values = nv; }
        update();
    }

private:
    ChartKind m_kind;
    QString   m_title { "Chart" };
    std::vector<QString> m_labels;
    std::vector<double>  m_values;
};

// ─────────────────────────────────────────────────────────────────────────────
// SlideHandleItem — small square (or circular, for rotate) handle drawn on
// top of the currently-selected item. Lives directly in the scene (not as a
// child of the target) so its own geometry stays simple to reason about.
// ─────────────────────────────────────────────────────────────────────────────
class SlideHandleItem : public QGraphicsRectItem {
public:
    SlideHandleItem(HandleRole role, QGraphicsItem* target, SlideScene* scene)
        : QGraphicsRectItem(-5, -5, 10, 10)
        , m_role(role)
        , m_target(target)
        , m_scene(scene)
    {
        setZValue(1000.0);
        setFlag(QGraphicsItem::ItemIsSelectable, false);
        if (role == HandleRole::Rotate) {
            setBrush(QColor("#2C3140"));
            setPen(QPen(QColor("#E8372A"), 1.5));
            setCursor(Qt::OpenHandCursor);
        } else {
            setBrush(QColor("#FFFFFF"));
            setPen(QPen(QColor("#E8372A"), 1.5));
            setCursor(cursorFor(role));
        }
    }

    HandleRole role() const { return m_role; }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override {
        if (!m_target) return;
        m_pressAngle    = angleFromCenter(e->scenePos());
        m_startRotation = m_target->rotation();
        e->accept();
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override {
        if (!m_target || !m_scene) return;
        if (m_role == HandleRole::Rotate) {
            const qreal ang   = angleFromCenter(e->scenePos());
            const qreal delta = ang - m_pressAngle;
            m_target->setTransformOriginPoint(m_target->boundingRect().center());
            m_target->setRotation(m_startRotation + delta);
        } else {
            m_scene->resizeTargetTo(m_target, m_role, e->scenePos());
        }
        // Reposition (do NOT rebuild) the handles: rebuilding would delete this
        // very handle mid-drag and abort the resize/rotate.
        m_scene->positionHandles();
        e->accept();
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override {
        if (m_scene) emit m_scene->sceneModified();
        e->accept();
    }

private:
    qreal angleFromCenter(const QPointF& scenePos) const {
        const QPointF center = m_target->mapToScene(m_target->boundingRect().center());
        const QPointF v = scenePos - center;
        return qRadiansToDegrees(std::atan2(v.y(), v.x()));
    }

    static Qt::CursorShape cursorFor(HandleRole role) {
        switch (role) {
        case HandleRole::TopLeft:
        case HandleRole::BottomRight: return Qt::SizeFDiagCursor;
        case HandleRole::TopRight:
        case HandleRole::BottomLeft:  return Qt::SizeBDiagCursor;
        case HandleRole::Top:
        case HandleRole::Bottom:      return Qt::SizeVerCursor;
        case HandleRole::Left:
        case HandleRole::Right:       return Qt::SizeHorCursor;
        default:                       return Qt::ArrowCursor;
        }
    }

    HandleRole     m_role;
    QGraphicsItem* m_target;
    SlideScene*    m_scene;
    qreal          m_pressAngle    { 0.0 };
    qreal          m_startRotation { 0.0 };
};

// ── Construction ─────────────────────────────────────────────────────────────
SlideScene::SlideScene(QObject* parent)
    : QGraphicsScene(0, 0, SLIDE_W, SLIDE_H, parent)
{
    setBackgroundBrush(QColor("#E8E9ED"));  // same canvas grey as Writer

    // White slide surface — always the first item, z = -1
    auto* bg = new QGraphicsRectItem(0, 0, SLIDE_W, SLIDE_H);
    bg->setBrush(Qt::white);
    bg->setPen(Qt::NoPen);
    bg->setZValue(-1.0);
    bg->setFlag(QGraphicsItem::ItemIsSelectable, false);
    bg->setFlag(QGraphicsItem::ItemIsMovable,    false);
    addItem(bg);

    connect(this, &QGraphicsScene::selectionChanged, this, &SlideScene::onSelectionChanged);
    connect(this, &QGraphicsScene::focusItemChanged, this,
            [this](QGraphicsItem* newItem, QGraphicsItem*, Qt::FocusReason) {
        if (auto* ti = qgraphicsitem_cast<QGraphicsTextItem*>(newItem))
            m_lastTextItem = ti;
    });
}

// ── Background accessor ───────────────────────────────────────────────────────
QGraphicsRectItem* SlideScene::backgroundItem() const {
    for (auto* item : items()) {
        if (auto* r = qgraphicsitem_cast<QGraphicsRectItem*>(item)) {
            if (r->zValue() < 0) return r;
        }
    }
    return nullptr;
}

// ── Background brush (solid or top→bottom gradient) ─────────────────────────────
void SlideScene::applyBackgroundBrush() {
    auto* bg = backgroundItem();
    if (!bg) return;
    if (m_bg2.isValid()) {
        QLinearGradient grad(0, 0, 0, SLIDE_H);
        grad.setColorAt(0.0, m_bg1);
        grad.setColorAt(1.0, m_bg2);
        bg->setBrush(QBrush(grad));
    } else {
        bg->setBrush(m_bg1.isValid() ? m_bg1 : QColor(Qt::white));
    }
}

// ── Default placeholders ──────────────────────────────────────────────────────
void SlideScene::addDefaultPlaceholders() {
    // Title
    addTextBox(QPointF(80, 160), "Click to add Title",    40.0);
    // Subtitle
    addTextBox(QPointF(80, 310), "Click to add Subtitle", 22.0);
}

void SlideScene::applyLayout(SlideLayout layout) {
    m_lastTextItem = nullptr;
    // Remove selection handles FIRST. Handles are themselves scene items, so
    // the bulk delete below would free them — and clearHandles() would then
    // free the same pointers a second time (heap corruption). Clearing first
    // empties m_handles and removes the handle items from the scene.
    clearHandles();
    // Clear all remaining non-background items
    const QList<QGraphicsItem*> all = items();
    for (auto* it : all) {
        if (it != backgroundItem()) {
            removeItem(it);
            delete it;
        }
    }

    switch (layout) {
    case SlideLayout::Title:
        addTextBox(QPointF(80, 160), "Click to add Title",    40.0);
        addTextBox(QPointF(80, 310), "Click to add Subtitle", 22.0);
        break;
    case SlideLayout::TitleContent:
        addTextBox(QPointF(60, 40),  "Click to add Title",   32.0);
        addTextBox(QPointF(60, 130), "Click to add Text",    18.0);
        break;
    case SlideLayout::Blank:
    default:
        break;
    }

    emit sceneModified();
}

// ── Insert mode ───────────────────────────────────────────────────────────────
void SlideScene::setInsertMode(InsertMode mode) {
    m_insertMode = mode;
    // Change cursor for all views
    const Qt::CursorShape shape = (mode == InsertMode::None)
                                  ? Qt::ArrowCursor
                                  : Qt::CrossCursor;
    for (auto* v : views()) v->setCursor(shape);
}

void SlideScene::beginInsertShape(ShapeKind kind) {
    m_pendingShape = kind;
    setInsertMode(InsertMode::Shape);
}

void SlideScene::insertTable(int rows, int cols) {
    rows = std::clamp(rows, 1, 20);
    cols = std::clamp(cols, 1, 12);
    const qreal w = 640.0;
    const qreal h = std::min(360.0, 54.0 * rows);
    const QRectF rect((SLIDE_W - w) / 2.0, (SLIDE_H - h) / 2.0, w, h);
    auto* t = addTable(rect, rows, cols);
    clearSelection();
    if (t) t->setSelected(true);
    emit sceneModified();
}

void SlideScene::insertSmartArt(SmartArtKind kind) {
    // Build the diagram out of ordinary Shape + TextBox items so it persists,
    // exports, and is individually editable like anything else on the slide.
    auto box = [this](const QRectF& r, ShapeKind k, const QColor& fill) {
        auto* it = addShape(r, k);
        if (auto* sh = dynamic_cast<SlideShapeItem*>(it)) {
            sh->setBrush(fill);
            sh->setPen(QPen(QColor("#2C3140"), 2));
        }
    };
    auto label = [this](const QRectF& r, const QString& text, qreal fs,
                        Qt::Alignment al = Qt::AlignCenter) {
        auto* t = addTextBox(QPointF(r.left() + 6, r.center().y() - fs), text, fs);
        t->setDefaultTextColor(QColor("#1C1E26"));
        t->setTextWidth(std::max(20.0, r.width() - 12));
        QTextOption opt = t->document()->defaultTextOption();
        opt.setAlignment(al);
        t->document()->setDefaultTextOption(opt);
    };

    switch (kind) {
    case SmartArtKind::ProcessFlow: {
        const qreal xs[3] = { 55, 375, 695 };
        const QColor cols[3] = { QColor("#DBEAFE"), QColor("#DCFCE7"), QColor("#FEF3C7") };
        const char* names[3] = { "Step 1", "Step 2", "Step 3" };
        for (int i = 0; i < 3; ++i) {
            const QRectF r(xs[i], 235, 210, 90);
            box(r, ShapeKind::RoundedRect, cols[i]);
            label(r, names[i], 18);
        }
        box(QRectF(275, 262, 90, 36), ShapeKind::Arrow, QColor("#9CA3AF"));
        box(QRectF(595, 262, 90, 36), ShapeKind::Arrow, QColor("#9CA3AF"));
        break;
    }
    case SmartArtKind::Cycle: {
        struct P { qreal x, y; const char* t; };
        const P ps[4] = { { 405, 50, "Plan" }, { 740, 215, "Do" },
                          { 405, 380, "Check" }, { 70, 215, "Act" } };
        const QColor cols[4] = { QColor("#DBEAFE"), QColor("#DCFCE7"),
                                 QColor("#FEF3C7"), QColor("#FCE7F3") };
        for (int i = 0; i < 4; ++i) {
            const QRectF r(ps[i].x, ps[i].y, 150, 110);
            box(r, ShapeKind::Ellipse, cols[i]);
            label(r, ps[i].t, 18);
        }
        break;
    }
    case SmartArtKind::Hierarchy: {
        const QRectF top(370, 60, 220, 80);
        const qreal cy = 300;
        const qreal xs[3] = { 120, 390, 660 };
        for (int i = 0; i < 3; ++i) {
            const QRectF r(xs[i], cy, 180, 80);
            box(QRectF(QPointF(top.center().x(), top.bottom()),
                       QPointF(r.center().x(), cy)), ShapeKind::Line, QColor("#9CA3AF"));
            box(r, ShapeKind::RoundedRect, QColor("#DCFCE7"));
            label(r, QString("Team %1").arg(i + 1), 16);
        }
        box(top, ShapeKind::RoundedRect, QColor("#DBEAFE"));
        label(top, "Manager", 18);
        break;
    }
    case SmartArtKind::Pyramid: {
        box(QRectF(260, 70, 440, 380), ShapeKind::Triangle, QColor("#FDE68A"));
        label(QRectF(360, 110, 240, 40), "Vision", 20);
        label(QRectF(320, 230, 320, 40), "Strategy", 20);
        label(QRectF(280, 350, 400, 40), "Execution", 20);
        break;
    }
    case SmartArtKind::BulletList: {
        const QRectF title(160, 55, 640, 70);
        box(title, ShapeKind::RoundedRect, QColor("#DBEAFE"));
        label(title, "Title", 22);
        const char* items[4] = { "First point", "Second point", "Third point", "Fourth point" };
        for (int i = 0; i < 4; ++i) {
            const QRectF r(200, 155 + i * 80, 560, 60);
            box(r, ShapeKind::RoundedRect, QColor("#F1F5F9"));
            label(r, items[i], 18, Qt::AlignLeft);
        }
        break;
    }
    case SmartArtKind::Venn: {
        box(QRectF(310, 60, 340, 340), ShapeKind::Ellipse, QColor(37, 99, 235, 90));
        box(QRectF(170, 260, 340, 340), ShapeKind::Ellipse, QColor(22, 163, 74, 90));
        box(QRectF(450, 260, 340, 340), ShapeKind::Ellipse, QColor(232, 55, 42, 90));
        label(QRectF(420, 120, 120, 40), "A", 22);
        label(QRectF(240, 470, 120, 40), "B", 22);
        label(QRectF(600, 470, 120, 40), "C", 22);
        break;
    }
    }

    clearSelection();
    emit sceneModified();
}

void SlideScene::insertImage(const QByteArray& pngData) {
    QPixmap pm;
    if (!pm.loadFromData(pngData, "PNG")) return;

    // Fit within a reasonable default box, preserving aspect ratio
    QSizeF size = pm.size();
    const qreal maxW = 480.0, maxH = 360.0;
    const qreal scale = std::min({1.0, maxW / size.width(), maxH / size.height()});
    size *= scale;

    const QRectF rect(QPointF((SLIDE_W - size.width()) / 2.0,
                               (SLIDE_H - size.height()) / 2.0),
                       size);
    addImageItem(rect, pngData);
    emit sceneModified();
}

void SlideScene::insertPresetText(const QString& text, qreal fontSize,
                                  bool bold, const QColor& color) {
    // Drop it a little below the slide top, horizontally roughly centred.
    auto* item = addTextBox(QPointF(120, 120), text, fontSize);
    QFont f = item->font();
    f.setBold(bold);
    item->setFont(f);
    item->setDefaultTextColor(color);

    clearSelection();
    item->setSelected(true);
    m_lastTextItem = item;
    emit sceneModified();
}

// ── Mouse events ──────────────────────────────────────────────────────────────
void SlideScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() != Qt::LeftButton || m_insertMode == InsertMode::None) {
        QGraphicsScene::mousePressEvent(event);
        return;
    }

    m_dragStart = event->scenePos();

    if (m_insertMode == InsertMode::TextBox) {
        // Immediately place; no drag needed for text boxes
        addTextBox(m_dragStart);
        setInsertMode(InsertMode::None);
        emit insertModeLeft();
        emit sceneModified();
        return;
    }

    // For rect / ellipse / gallery shape: show a zero-size preview during drag
    const QRectF r(m_dragStart, QSizeF(1, 1));
    if (m_insertMode == InsertMode::Rectangle) {
        auto* rect = new QGraphicsRectItem(r);
        rect->setPen(QPen(QColor("#E8372A"), 2, Qt::DashLine));
        rect->setBrush(Qt::NoBrush);
        m_dragItem = rect;
    } else if (m_insertMode == InsertMode::Ellipse) {
        auto* ell = new QGraphicsEllipseItem(r);
        ell->setPen(QPen(QColor("#E8372A"), 2, Qt::DashLine));
        ell->setBrush(Qt::NoBrush);
        m_dragItem = ell;
    } else {   // InsertMode::Shape
        auto* sh = new SlideShapeItem(m_pendingShape);
        sh->setRect(r);
        sh->setPen(QPen(QColor("#E8372A"), 2, Qt::DashLine));
        sh->setBrush(Qt::NoBrush);
        m_dragItem = sh;
    }
    addItem(m_dragItem);
    event->accept();
}

void SlideScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (m_insertMode == InsertMode::None || !m_dragItem) {
        QGraphicsScene::mouseMoveEvent(event);
        // While the user drags a selected object's body, keep its handles glued
        // to it (the handles are separate scene items, not children).
        if ((event->buttons() & Qt::LeftButton) && m_handleTarget)
            positionHandles();
        return;
    }

    const QPointF cur = event->scenePos();
    const QRectF rect = QRectF(m_dragStart, cur).normalized();

    if (auto* ri = qgraphicsitem_cast<QGraphicsRectItem*>(m_dragItem))
        ri->setRect(rect);
    else if (auto* ei = qgraphicsitem_cast<QGraphicsEllipseItem*>(m_dragItem))
        ei->setRect(rect);
    else if (auto* sh = dynamic_cast<SlideShapeItem*>(m_dragItem))
        sh->setRect(rect);

    event->accept();
}

void SlideScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (m_insertMode == InsertMode::None || !m_dragItem) {
        QGraphicsScene::mouseReleaseEvent(event);
        if (m_handleTarget) positionHandles();   // settle handles after a body drag
        return;
    }

    const QPointF cur = event->scenePos();
    QRectF rect = QRectF(m_dragStart, cur).normalized();

    // Minimum size so a click (no drag) still produces a visible shape
    if (rect.width()  < 20) rect.setWidth(160);
    if (rect.height() < 20) rect.setHeight(90);

    // Remove the preview dash-line item
    removeItem(m_dragItem);
    delete m_dragItem;
    m_dragItem = nullptr;

    if (m_insertMode == InsertMode::Rectangle) {
        addRectangle(rect);
    } else if (m_insertMode == InsertMode::Ellipse) {
        addEllipse(rect);
    } else if (m_insertMode == InsertMode::Shape) {
        addShape(rect, m_pendingShape);
    }

    setInsertMode(InsertMode::None);
    emit insertModeLeft();
    emit sceneModified();
    event->accept();
}

void SlideScene::keyPressEvent(QKeyEvent* event) {
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) &&
        !selectedItems().isEmpty()) {
        // Don't eat the key while editing text (let it edit the text instead)
        bool editingText = false;
        for (auto* it : selectedItems()) {
            if (auto* ti = qgraphicsitem_cast<QGraphicsTextItem*>(it)) {
                if (ti->textInteractionFlags() & Qt::TextEditable) { editingText = true; break; }
            }
        }
        if (!editingText) {
            deleteSelectedItem();
            event->accept();
            return;
        }
    }
    QGraphicsScene::keyPressEvent(event);
}

// Apply (or clear) the standard hyperlink look (blue + underline) on a text
// item, merging into the document so it persists through save/load.
static void styleTextLink(QGraphicsTextItem* ti, bool linked) {
    QTextCursor c(ti->document());
    c.select(QTextCursor::Document);
    QTextCharFormat f;
    f.setFontUnderline(linked);
    f.setForeground(linked ? QColor("#2563EB") : QColor("#1C1E26"));
    c.mergeCharFormat(f);
    ti->setDefaultTextColor(linked ? QColor("#2563EB") : QColor("#1C1E26"));
}

void SlideScene::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    QGraphicsItem* it = itemAt(event->scenePos(), QTransform());
    while (it && it->parentItem()) it = it->parentItem();
    if (!it || it == backgroundItem() || dynamic_cast<SlideHandleItem*>(it)) {
        QGraphicsScene::contextMenuEvent(event);
        return;
    }

    clearSelection();
    it->setSelected(true);

    QMenu menu;

    // ── Format (Figma-style quick styling for the object) ───────────────
    QMenu* fmt = menu.addMenu("Format");
    QAction* aFill    = fmt->addAction("Fill Colour…");
    QAction* aOutline = fmt->addAction("Outline Colour…");
    QMenu* opac = fmt->addMenu("Opacity");
    for (int pct : { 100, 75, 50, 25, 10 }) {
        QAction* a = opac->addAction(QString("%1%").arg(pct));
        const qreal o = pct / 100.0;
        connect(a, &QAction::triggered, this, [this, o] { setSelectedOpacity(o); });
    }
    connect(aFill, &QAction::triggered, this, [this] {
        const QColor c = QColorDialog::getColor(QColor("#E8372A"), nullptr, "Fill Colour",
                                                QColorDialog::ShowAlphaChannel);
        if (c.isValid()) setSelectedFill(c);
    });
    connect(aOutline, &QAction::triggered, this, [this] {
        const QColor c = QColorDialog::getColor(QColor("#2C3140"), nullptr, "Outline Colour");
        if (c.isValid()) setSelectedOutline(c);
    });
    QAction* aShadow = menu.addAction("Toggle Shadow");
    connect(aShadow, &QAction::triggered, this, [this] { toggleSelectedShadow(); });

    // ── Animate this object ─────────────────────────────────────────────
    QMenu* anim = menu.addMenu("Animate");
    struct An { const char* label; ItemAnimation a; };
    const An ans[] = {
        { "None",                ItemAnimation::None },
        { "Entrance: Fade In",   ItemAnimation::FadeIn },
        { "Entrance: Fly Left",  ItemAnimation::FlyInLeft },
        { "Entrance: Fly Right", ItemAnimation::FlyInRight },
        { "Entrance: Fly Top",   ItemAnimation::FlyInTop },
        { "Entrance: Fly Bottom",ItemAnimation::FlyInBottom },
        { "Entrance: Zoom In",   ItemAnimation::ZoomIn },
        { "Entrance: Spin In",   ItemAnimation::SpinIn },
        { "Emphasis: Pulse",     ItemAnimation::EmphasisPulse },
        { "Emphasis: Spin",      ItemAnimation::EmphasisSpin },
        { "Emphasis: Blink",     ItemAnimation::EmphasisBlink },
        { "Exit: Fade Out",      ItemAnimation::ExitFadeOut },
        { "Exit: Fly Left",      ItemAnimation::ExitFlyLeft },
        { "Exit: Fly Right",     ItemAnimation::ExitFlyRight },
        { "Exit: Zoom Out",      ItemAnimation::ExitZoomOut },
    };
    const ItemAnimation curAnim = static_cast<ItemAnimation>(it->data(AnimationKey).toInt());
    for (const auto& an : ans) {
        QAction* a = anim->addAction(an.label);
        a->setCheckable(true);
        a->setChecked(an.a == curAnim);
        const ItemAnimation av = an.a;
        connect(a, &QAction::triggered, this, [this, av] { setSelectedAnimation(av); });
    }

    if (auto* simg = dynamic_cast<SlideImageItem*>(it)) {
        QAction* aImg = menu.addAction("Image Effects…");
        connect(aImg, &QAction::triggered, this, [this, simg] {
            const int b0 = simg->brightness(), c0 = simg->contrast();
            QDialog dlg(QApplication::activeWindow());
            dlg.setWindowTitle("Image Effects");
            dlg.resize(320, 140);
            auto* v = new QVBoxLayout(&dlg);
            auto mkRow = [&](const QString& name, int val) {
                auto* row = new QHBoxLayout;
                auto* lbl = new QLabel(name, &dlg); lbl->setFixedWidth(80);
                auto* s = new QSlider(Qt::Horizontal, &dlg);
                s->setRange(-100, 100); s->setValue(val);
                row->addWidget(lbl); row->addWidget(s);
                v->addLayout(row);
                return s;
            };
            auto* bs = mkRow("Brightness", b0);
            auto* cs = mkRow("Contrast", c0);
            auto apply = [simg, bs, cs] { simg->setAdjust(bs->value(), cs->value()); };
            connect(bs, &QSlider::valueChanged, &dlg, [apply](int) { apply(); });
            connect(cs, &QSlider::valueChanged, &dlg, [apply](int) { apply(); });
            auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
            v->addWidget(bb);
            connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
            if (dlg.exec() != QDialog::Accepted) simg->setAdjust(b0, c0);   // revert preview
            else emit sceneModified();
        });
    }
    menu.addSeparator();

    QAction* aLink   = menu.addAction("Add / Edit Hyperlink…");
    QAction* aUnlink = menu.addAction("Remove Hyperlink");
    aUnlink->setEnabled(!it->data(HyperlinkKey).toString().isEmpty());
    menu.addSeparator();
    QAction* aFront = menu.addAction("Bring to Front");
    QAction* aBack  = menu.addAction("Send to Back");
    menu.addSeparator();
    QAction* aDelete = menu.addAction("Delete");

    QAction* chosen = menu.exec(event->screenPos());
    if (chosen == aLink) {
        bool ok = false;
        const QString cur = it->data(HyperlinkKey).toString();
        const QString url = QInputDialog::getText(nullptr, "Hyperlink",
            "Address (e.g. https://example.com):", QLineEdit::Normal, cur, &ok);
        if (ok && !url.isEmpty()) {
            it->setData(HyperlinkKey, url);
            if (auto* ti = qgraphicsitem_cast<QGraphicsTextItem*>(it)) styleTextLink(ti, true);
            emit sceneModified();
        }
    } else if (chosen == aUnlink) {
        it->setData(HyperlinkKey, QString());
        if (auto* ti = qgraphicsitem_cast<QGraphicsTextItem*>(it)) styleTextLink(ti, false);
        emit sceneModified();
    } else if (chosen == aFront) {
        qreal top = 0.0;
        for (auto* o : items()) top = std::max(top, o->zValue());
        it->setZValue(top + 1.0);
        emit sceneModified();
    } else if (chosen == aBack) {
        it->setZValue(0.0);   // still above the background (z = -1)
        emit sceneModified();
    } else if (chosen == aDelete) {
        deleteSelectedItem();
    }
    event->accept();
}

QGraphicsTextItem* SlideScene::activeTextItem() const {
    if (auto* fi = qgraphicsitem_cast<QGraphicsTextItem*>(focusItem()))
        return fi;
    return m_lastTextItem;
}

void SlideScene::deleteSelectedItem() {
    const QList<QGraphicsItem*> sel = selectedItems();
    auto* bg = backgroundItem();
    for (auto* it : sel) {
        if (it == bg) continue;
        if (it == m_lastTextItem) m_lastTextItem = nullptr;
        removeItem(it);
        delete it;
    }
    clearHandles();
    emit sceneModified();
}

// Build the standard soft drop shadow used across the editor and exports.
static QGraphicsDropShadowEffect* makeSoftShadow() {
    auto* e = new QGraphicsDropShadowEffect;
    e->setBlurRadius(18.0);
    e->setOffset(4.0, 4.0);
    e->setColor(QColor(0, 0, 0, 120));
    return e;
}

bool SlideScene::hasShapeSelection() const {
    for (auto* it : selectedItems()) {
        if (it == backgroundItem()) continue;
        if (dynamic_cast<SlideShapeItem*>(it)) return true;
        if (qgraphicsitem_cast<QGraphicsRectItem*>(it)) return true;
        if (qgraphicsitem_cast<QGraphicsEllipseItem*>(it)) return true;
    }
    return false;
}

void SlideScene::setSelectedFill(const QColor& color) {
    bool changed = false;
    for (auto* it : selectedItems()) {
        if (it == backgroundItem()) continue;
        if (auto* sh = dynamic_cast<SlideShapeItem*>(it)) {
            if (sh->kind() != ShapeKind::Line) { sh->setBrush(color); changed = true; }
        } else if (auto* ri = qgraphicsitem_cast<QGraphicsRectItem*>(it)) {
            ri->setBrush(color); changed = true;
        } else if (auto* ei = qgraphicsitem_cast<QGraphicsEllipseItem*>(it)) {
            ei->setBrush(color); changed = true;
        }
    }
    if (changed) emit sceneModified();
}

void SlideScene::setSelectedOutline(const QColor& color) {
    bool changed = false;
    for (auto* it : selectedItems()) {
        if (it == backgroundItem()) continue;
        if (auto* sh = dynamic_cast<SlideShapeItem*>(it)) {
            QPen p = sh->pen(); p.setColor(color); sh->setPen(p); changed = true;
        } else if (auto* ri = qgraphicsitem_cast<QGraphicsRectItem*>(it)) {
            QPen p = ri->pen(); p.setColor(color); ri->setPen(p); changed = true;
        } else if (auto* ei = qgraphicsitem_cast<QGraphicsEllipseItem*>(it)) {
            QPen p = ei->pen(); p.setColor(color); ei->setPen(p); changed = true;
        }
    }
    if (changed) emit sceneModified();
}

void SlideScene::setSelectedOpacity(qreal opacity) {
    bool changed = false;
    for (auto* it : selectedItems()) {
        if (it == backgroundItem()) continue;
        if (dynamic_cast<SlideHandleItem*>(it)) continue;
        it->setOpacity(std::clamp(opacity, 0.0, 1.0));
        changed = true;
    }
    if (changed) emit sceneModified();
}

void SlideScene::toggleSelectedShadow() {
    bool changed = false;
    for (auto* it : selectedItems()) {
        if (it == backgroundItem()) continue;
        if (dynamic_cast<SlideHandleItem*>(it)) continue;
        if (it->graphicsEffect())
            it->setGraphicsEffect(nullptr);
        else
            it->setGraphicsEffect(makeSoftShadow());
        changed = true;
    }
    if (changed) emit sceneModified();
}

void SlideScene::setSelectedAnimation(ItemAnimation anim) {
    bool changed = false;
    for (auto* it : selectedItems()) {
        if (it == backgroundItem()) continue;
        it->setData(AnimationKey, static_cast<int>(anim));
        changed = true;
    }
    if (changed) emit sceneModified();
}

ItemAnimation SlideScene::selectedAnimation() const {
    for (auto* it : selectedItems()) {
        if (it == backgroundItem()) continue;
        return static_cast<ItemAnimation>(it->data(AnimationKey).toInt());
    }
    return ItemAnimation::None;
}

bool SlideScene::hasSelection() const {
    for (auto* it : selectedItems())
        if (it != backgroundItem()) return true;
    return false;
}

// ── Selection → handle overlay ─────────────────────────────────────────────────
void SlideScene::onSelectionChanged() {
    rebuildHandles();
    emit selectionInfoChanged();
}

void SlideScene::clearHandles() {
    for (auto* h : m_handles) {
        removeItem(h);
        delete h;
    }
    m_handles.clear();
    m_handleTarget = nullptr;
}

void SlideScene::rebuildHandles() {
    clearHandles();

    const QList<QGraphicsItem*> sel = selectedItems();
    if (sel.size() != 1) return;

    QGraphicsItem* target = sel.first();
    if (target == backgroundItem()) return;

    m_handleTarget = target;
    const QRectF r = target->boundingRect();

    const bool isText = qgraphicsitem_cast<QGraphicsTextItem*>(target) != nullptr;

    auto place = [&](HandleRole role, const QPointF& localPt) {
        auto* h = new SlideHandleItem(role, target, this);
        h->setPos(target->mapToScene(localPt));
        addItem(h);
        m_handles.push_back(h);
    };

    if (isText) {
        // Text boxes only support width-resize from the right edge
        place(HandleRole::Right,       QPointF(r.right(), r.center().y()));
        place(HandleRole::TopRight,    r.topRight());
        place(HandleRole::BottomRight, r.bottomRight());
    } else {
        place(HandleRole::TopLeft,     r.topLeft());
        place(HandleRole::Top,         QPointF(r.center().x(), r.top()));
        place(HandleRole::TopRight,    r.topRight());
        place(HandleRole::Right,       QPointF(r.right(), r.center().y()));
        place(HandleRole::BottomRight, r.bottomRight());
        place(HandleRole::Bottom,      QPointF(r.center().x(), r.bottom()));
        place(HandleRole::BottomLeft,  r.bottomLeft());
        place(HandleRole::Left,        QPointF(r.left(), r.center().y()));
    }

    place(HandleRole::Rotate, QPointF(r.center().x(), r.top() - 26));
}

void SlideScene::positionHandles() {
    if (!m_handleTarget || m_handles.empty()) return;
    const QRectF r = m_handleTarget->boundingRect();

    for (auto* h : m_handles) {
        QPointF local;
        switch (h->role()) {
        case HandleRole::TopLeft:     local = r.topLeft();                          break;
        case HandleRole::Top:         local = QPointF(r.center().x(), r.top());      break;
        case HandleRole::TopRight:    local = r.topRight();                         break;
        case HandleRole::Right:       local = QPointF(r.right(), r.center().y());    break;
        case HandleRole::BottomRight: local = r.bottomRight();                      break;
        case HandleRole::Bottom:      local = QPointF(r.center().x(), r.bottom());   break;
        case HandleRole::BottomLeft:  local = r.bottomLeft();                       break;
        case HandleRole::Left:        local = QPointF(r.left(), r.center().y());     break;
        case HandleRole::Rotate:      local = QPointF(r.center().x(), r.top() - 26); break;
        }
        h->setPos(m_handleTarget->mapToScene(local));
    }
}

void SlideScene::resizeTargetTo(QGraphicsItem* target, HandleRole role, const QPointF& scenePos) {
    if (!target) return;
    const QPointF local = target->mapFromScene(scenePos);

    if (auto* ti = qgraphicsitem_cast<QGraphicsTextItem*>(target)) {
        const qreal newWidth = std::max(40.0, local.x());
        ti->setTextWidth(newWidth);
        return;
    }

    QRectF r;
    if (auto* tb = dynamic_cast<SlideTableItem*>(target)) {
        r = tb->rect();
    } else if (auto* ch = dynamic_cast<SlideChartItem*>(target)) {
        r = ch->rect();
    } else if (auto* sh = dynamic_cast<SlideShapeItem*>(target)) {
        r = sh->rect();
    } else if (auto* ri = qgraphicsitem_cast<QGraphicsRectItem*>(target)) {
        r = ri->rect();
    } else if (auto* ei = qgraphicsitem_cast<QGraphicsEllipseItem*>(target)) {
        r = ei->rect();
    } else if (qgraphicsitem_cast<QGraphicsPixmapItem*>(target)) {
        r = target->boundingRect();
    } else {
        return;
    }

    switch (role) {
    case HandleRole::TopLeft:     r.setTopLeft(local);     break;
    case HandleRole::Top:         r.setTop(local.y());     break;
    case HandleRole::TopRight:    r.setTopRight(local);    break;
    case HandleRole::Right:       r.setRight(local.x());   break;
    case HandleRole::BottomRight: r.setBottomRight(local); break;
    case HandleRole::Bottom:      r.setBottom(local.y());  break;
    case HandleRole::BottomLeft:  r.setBottomLeft(local);  break;
    case HandleRole::Left:        r.setLeft(local.x());    break;
    default: break;
    }
    r = r.normalized();
    if (r.width()  < 20) r.setWidth(20);
    if (r.height() < 20) r.setHeight(20);

    if (auto* tb = dynamic_cast<SlideTableItem*>(target)) {
        tb->setRectAndLayout(r);
    } else if (auto* ch = dynamic_cast<SlideChartItem*>(target)) {
        ch->setRectKeep(r);
    } else if (auto* sh = dynamic_cast<SlideShapeItem*>(target)) {
        sh->setRect(r);
    } else if (auto* ri = qgraphicsitem_cast<QGraphicsRectItem*>(target)) {
        ri->setRect(r);
    } else if (auto* ei = qgraphicsitem_cast<QGraphicsEllipseItem*>(target)) {
        ei->setRect(r);
    } else if (auto* pi = qgraphicsitem_cast<QGraphicsPixmapItem*>(target)) {
        pi->setOffset(r.topLeft());
        QPixmap src;
        if (auto* simg = dynamic_cast<SlideImageItem*>(pi))
            src = simg->adjustedSource();
        else
            src = pi->pixmap();
        pi->setPixmap(src.scaled(r.size().toSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }
}

// ── Item factory helpers ──────────────────────────────────────────────────────
QGraphicsTextItem* SlideScene::addTextBox(const QPointF& pos,
                                           const QString& placeholder,
                                           qreal          fontSize) {
    auto* item = new QGraphicsTextItem;
    item->setPlainText(placeholder.isEmpty() ? "Text" : placeholder);
    item->setPos(pos);
    item->setTextWidth(800);

    QFont f("Segoe UI", static_cast<int>(fontSize));
    item->setFont(f);
    item->setDefaultTextColor(placeholder.isEmpty() ? QColor("#1C1E26")
                                                     : QColor("#9CA3AF"));

    item->setFlags(QGraphicsItem::ItemIsSelectable |
                   QGraphicsItem::ItemIsMovable    |
                   QGraphicsItem::ItemIsFocusable);
    item->setTextInteractionFlags(Qt::TextEditorInteraction);

    // Connect content change to scene modification
    connect(item->document(), &QTextDocument::contentsChanged,
            this, &SlideScene::sceneModified);

    addItem(item);
    return item;
}

QGraphicsRectItem* SlideScene::addRectangle(const QRectF& rect) {
    auto* item = new QGraphicsRectItem(rect);
    item->setPen(QPen(QColor("#2C3140"), 2));
    item->setBrush(QBrush(QColor(44, 49, 64, 40)));   // translucent charcoal fill
    item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    addItem(item);
    return item;
}

QGraphicsEllipseItem* SlideScene::addEllipse(const QRectF& rect) {
    auto* item = new QGraphicsEllipseItem(rect);
    item->setPen(QPen(QColor("#E8372A"), 2));
    item->setBrush(QBrush(QColor(232, 55, 42, 40)));  // translucent scarlet fill
    item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    addItem(item);
    return item;
}

// ── Gallery shape geometry ──────────────────────────────────────────────────
QPainterPath SlideScene::shapePath(ShapeKind kind, const QRectF& r) {
    QPainterPath p;
    const qreal x = r.x(), y = r.y(), w = r.width(), h = r.height();
    const qreal cx = r.center().x(), cy = r.center().y();

    switch (kind) {
    case ShapeKind::Rectangle:
        p.addRect(r);
        break;
    case ShapeKind::RoundedRect: {
        const qreal rad = std::min(w, h) * 0.18;
        p.addRoundedRect(r, rad, rad);
        break;
    }
    case ShapeKind::Ellipse:
        p.addEllipse(r);
        break;
    case ShapeKind::Triangle: {
        QPolygonF poly;
        poly << QPointF(cx, y) << QPointF(x + w, y + h) << QPointF(x, y + h);
        p.addPolygon(poly); p.closeSubpath();
        break;
    }
    case ShapeKind::RightTriangle: {
        QPolygonF poly;
        poly << QPointF(x, y) << QPointF(x, y + h) << QPointF(x + w, y + h);
        p.addPolygon(poly); p.closeSubpath();
        break;
    }
    case ShapeKind::Diamond: {
        QPolygonF poly;
        poly << QPointF(cx, y) << QPointF(x + w, cy) << QPointF(cx, y + h) << QPointF(x, cy);
        p.addPolygon(poly); p.closeSubpath();
        break;
    }
    case ShapeKind::Pentagon: {
        QPolygonF poly;
        for (int i = 0; i < 5; ++i) {
            const double a = -M_PI / 2 + i * 2 * M_PI / 5;
            poly << QPointF(cx + (w / 2) * std::cos(a), cy + (h / 2) * std::sin(a));
        }
        p.addPolygon(poly); p.closeSubpath();
        break;
    }
    case ShapeKind::Hexagon: {
        QPolygonF poly;
        for (int i = 0; i < 6; ++i) {
            const double a = i * M_PI / 3;
            poly << QPointF(cx + (w / 2) * std::cos(a), cy + (h / 2) * std::sin(a));
        }
        p.addPolygon(poly); p.closeSubpath();
        break;
    }
    case ShapeKind::Star5: {
        QPolygonF poly;
        for (int i = 0; i < 10; ++i) {
            const double a  = -M_PI / 2 + i * M_PI / 5;
            const qreal rx = (i % 2 == 0) ? w / 2 : w / 5;
            const qreal ry = (i % 2 == 0) ? h / 2 : h / 5;
            poly << QPointF(cx + rx * std::cos(a), cy + ry * std::sin(a));
        }
        p.addPolygon(poly); p.closeSubpath();
        break;
    }
    case ShapeKind::Arrow: {
        const qreal shaftTop = cy - h * 0.18;
        const qreal shaftBot = cy + h * 0.18;
        const qreal headX    = x + w * 0.6;
        QPolygonF poly;
        poly << QPointF(x, shaftTop) << QPointF(headX, shaftTop) << QPointF(headX, y)
             << QPointF(x + w, cy)   << QPointF(headX, y + h)    << QPointF(headX, shaftBot)
             << QPointF(x, shaftBot);
        p.addPolygon(poly); p.closeSubpath();
        break;
    }
    case ShapeKind::Chevron: {
        const qreal notch = w * 0.25;
        QPolygonF poly;
        poly << QPointF(x, y) << QPointF(x + w - notch, y) << QPointF(x + w, cy)
             << QPointF(x + w - notch, y + h) << QPointF(x, y + h) << QPointF(x + notch, cy);
        p.addPolygon(poly); p.closeSubpath();
        break;
    }
    case ShapeKind::Line:
        p.moveTo(r.topLeft());
        p.lineTo(r.bottomRight());
        break;
    case ShapeKind::Cloud: {
        QPainterPath c;
        c.addEllipse(QRectF(x,            y + h * 0.35, w * 0.45, h * 0.50));
        c.addEllipse(QRectF(x + w * 0.20, y + h * 0.10, w * 0.45, h * 0.55));
        c.addEllipse(QRectF(x + w * 0.45, y + h * 0.20, w * 0.40, h * 0.50));
        c.addEllipse(QRectF(x + w * 0.50, y + h * 0.40, w * 0.45, h * 0.50));
        c.addEllipse(QRectF(x + w * 0.25, y + h * 0.45, w * 0.50, h * 0.50));
        p = c.simplified();
        break;
    }
    case ShapeKind::Heart: {
        const qreal topY = y + h * 0.30;
        p.moveTo(cx, y + h);
        p.cubicTo(x - w * 0.10, cy,         x + w * 0.20, y, cx, topY);
        p.cubicTo(x + w * 0.80, y,          x + w * 1.10, cy, cx, y + h);
        break;
    }
    }
    return p;
}

QGraphicsItem* SlideScene::addShape(const QRectF& rect, ShapeKind kind) {
    auto* item = new SlideShapeItem(kind);
    item->setRect(rect);
    if (kind == ShapeKind::Line) {
        item->setPen(QPen(QColor("#2C3140"), 2.5));
        item->setBrush(Qt::NoBrush);
    } else {
        item->setPen(QPen(QColor("#2C3140"), 2));
        item->setBrush(QBrush(QColor(232, 55, 42, 40)));  // translucent scarlet fill
    }
    item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    addItem(item);
    return item;
}

QGraphicsItem* SlideScene::addTable(const QRectF& rect, int rows, int cols,
                                    const std::vector<QString>& texts) {
    auto* item = new SlideTableItem(rows, cols, rect);
    item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);

    const auto& cells = item->cells();
    for (size_t i = 0; i < cells.size(); ++i) {
        if (i < texts.size()) cells[i]->setPlainText(texts[i]);
        connect(cells[i]->document(), &QTextDocument::contentsChanged,
                this, &SlideScene::sceneModified);
    }
    item->relayout();
    addItem(item);
    return item;
}

// ── Chart rendering (shared by the on-slide item and PPTX rasterisation) ─────
void SlideScene::paintChart(QPainter& p, const QRectF& r, ChartKind kind,
                            const std::vector<QString>& labels,
                            const std::vector<double>& values, const QString& title) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    p.setPen(QPen(QColor("#D7DAE0"), 1));
    p.setBrush(Qt::white);
    p.drawRect(r);

    static const QColor pal[] = {
        QColor("#2563EB"), QColor("#16A34A"), QColor("#E8372A"), QColor("#F59E0B"),
        QColor("#7C3AED"), QColor("#0EA5E9"), QColor("#DB2777"), QColor("#65A30D")
    };
    const int n = static_cast<int>(std::min(labels.size(), values.size()));

    QRectF area = r.adjusted(14, 12, -14, -14);
    if (!title.isEmpty()) {
        p.setFont(QFont("Segoe UI", 12, QFont::Bold));
        p.setPen(QColor("#1C1E26"));
        p.drawText(QRectF(r.left(), r.top() + 8, r.width(), 24),
                   Qt::AlignHCenter | Qt::AlignTop, title);
        area.setTop(area.top() + 26);
    }
    if (n == 0) { p.restore(); return; }

    if (kind == ChartKind::Pie) {
        double sum = 0; for (int i = 0; i < n; ++i) sum += std::max(0.0, values[i]);
        if (sum <= 0) sum = 1;
        const qreal d = std::min(area.width() * 0.55, area.height());
        QRectF pie(area.left(), area.center().y() - d / 2, d, d);
        double start = 90.0;
        for (int i = 0; i < n; ++i) {
            const double span = std::max(0.0, values[i]) / sum * 360.0;
            p.setBrush(pal[i % 8]); p.setPen(QPen(Qt::white, 2));
            p.drawPie(pie, static_cast<int>(start * 16), static_cast<int>(-span * 16));
            start -= span;
        }
        qreal ly = area.top() + 4; const qreal lx = pie.right() + 18;
        p.setFont(QFont("Segoe UI", 9));
        for (int i = 0; i < n; ++i) {
            p.setBrush(pal[i % 8]); p.setPen(Qt::NoPen);
            p.drawRect(QRectF(lx, ly + 2, 12, 12));
            p.setPen(QColor("#1C1E26"));
            p.drawText(QRectF(lx + 18, ly, area.right() - (lx + 18), 16),
                       Qt::AlignVCenter | Qt::AlignLeft,
                       QString("%1 (%2)").arg(labels[i]).arg(values[i]));
            ly += 18;
        }
        p.restore(); return;
    }

    const qreal axisB = 18.0;
    QRectF plot = area.adjusted(4, 0, 0, -axisB);
    double maxV = 0; for (int i = 0; i < n; ++i) maxV = std::max(maxV, values[i]);
    if (maxV <= 0) maxV = 1;

    p.setPen(QPen(QColor("#C6CAD3"), 1));
    p.drawLine(plot.bottomLeft(), plot.bottomRight());
    p.setFont(QFont("Segoe UI", 8));

    if (kind == ChartKind::Bar) {
        const qreal slot = plot.width() / n;
        const qreal bw = std::max(4.0, slot * 0.6);
        for (int i = 0; i < n; ++i) {
            const qreal h = plot.height() * (std::max(0.0, values[i]) / maxV);
            const QRectF bar(plot.left() + i * slot + (slot - bw) / 2, plot.bottom() - h, bw, h);
            p.setBrush(pal[i % 8]); p.setPen(Qt::NoPen);
            p.drawRect(bar);
            p.setPen(QColor("#5A6071"));
            p.drawText(QRectF(plot.left() + i * slot, plot.bottom() + 2, slot, axisB - 2),
                       Qt::AlignHCenter | Qt::AlignTop, labels[i]);
        }
    } else {   // Line or Area
        QPolygonF pts;
        const qreal step = (n > 1) ? plot.width() / (n - 1) : 0;
        for (int i = 0; i < n; ++i) {
            const qreal x = (n > 1) ? plot.left() + i * step : plot.center().x();
            const qreal y = plot.bottom() - plot.height() * (std::max(0.0, values[i]) / maxV);
            pts << QPointF(x, y);
        }
        if (kind == ChartKind::Area) {
            QPolygonF poly = pts;
            poly << QPointF(pts.last().x(), plot.bottom())
                 << QPointF(pts.first().x(), plot.bottom());
            p.setBrush(QColor(37, 99, 235, 80)); p.setPen(Qt::NoPen);
            p.drawPolygon(poly);
        }
        p.setPen(QPen(QColor("#2563EB"), 2.5)); p.setBrush(Qt::NoBrush);
        p.drawPolyline(pts);
        p.setBrush(QColor("#2563EB")); p.setPen(Qt::NoPen);
        for (const QPointF& pt : pts) p.drawEllipse(pt, 3, 3);
        p.setPen(QColor("#5A6071"));
        for (int i = 0; i < n; ++i) {
            const qreal x = (n > 1) ? plot.left() + i * step : plot.center().x();
            p.drawText(QRectF(x - 30, plot.bottom() + 2, 60, axisB - 2),
                       Qt::AlignHCenter | Qt::AlignTop, labels[i]);
        }
    }
    p.restore();
}

QGraphicsItem* SlideScene::addChart(const QRectF& rect, ChartKind kind, const QString& title,
                                    const std::vector<QString>& labels,
                                    const std::vector<double>& values) {
    auto* item = new SlideChartItem(kind, rect);
    item->setTitle(title);
    item->setData(labels, values);
    item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    addItem(item);
    return item;
}

void SlideScene::insertChart(ChartKind kind) {
    const QRectF rect((SLIDE_W - 560) / 2.0, (SLIDE_H - 340) / 2.0, 560, 340);
    addChart(rect, kind, "Chart",
             { "Q1", "Q2", "Q3", "Q4" }, { 30.0, 55.0, 40.0, 70.0 });
    clearSelection();
    emit sceneModified();
}

QGraphicsItem* SlideScene::addImageItem(const QRectF& rect, const QByteArray& pngData) {
    QPixmap original;
    if (!original.loadFromData(pngData, "PNG")) return nullptr;

    auto* item = new SlideImageItem(original);
    item->setPixmap(original.scaled(rect.size().toSize(), Qt::IgnoreAspectRatio,
                                     Qt::SmoothTransformation));
    item->setOffset(rect.topLeft());
    item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    addItem(item);
    return item;
}

// ── Persistence ───────────────────────────────────────────────────────────────
void SlideScene::loadFromData(const SlideData& data) {
    clearHandles();
    m_lastTextItem = nullptr;

    // Clear everything except the background
    const QList<QGraphicsItem*> all = items();
    for (auto* it : all) {
        if (it != backgroundItem()) {
            removeItem(it);
            delete it;
        }
    }

    // Restore background fill (solid, or a vertical two-colour gradient)
    m_bg1 = data.background;
    m_bg2 = data.background2;
    applyBackgroundBrush();

    // Recreate items
    for (const auto& si : data.items) {
        QGraphicsItem* created = nullptr;
        switch (si.type) {
        case SlideItemType::TextBox: {
            auto* ti = addTextBox(si.rect.topLeft(), si.text, si.fontSize);
            if (ti) {
                if (!si.html.isEmpty()) ti->setHtml(si.html);
                ti->setTextWidth(si.rect.width() > 0 ? si.rect.width() : 800);
                ti->setTransformOriginPoint(ti->boundingRect().center());
                ti->setRotation(si.rotation);
                ti->setDefaultTextColor(si.isPlaceholder ? QColor("#9CA3AF") : QColor(si.penColor));
            }
            created = ti;
            break;
        }
        case SlideItemType::Rectangle:
            if (auto* ri = addRectangle(si.rect)) {
                ri->setPen(QPen(si.penColor, si.penWidth));
                ri->setBrush(si.fillColor);
                ri->setTransformOriginPoint(ri->boundingRect().center());
                ri->setRotation(si.rotation);
                created = ri;
            }
            break;
        case SlideItemType::Ellipse:
            if (auto* ei = addEllipse(si.rect)) {
                ei->setPen(QPen(si.penColor, si.penWidth));
                ei->setBrush(si.fillColor);
                ei->setTransformOriginPoint(ei->boundingRect().center());
                ei->setRotation(si.rotation);
                created = ei;
            }
            break;
        case SlideItemType::Image:
            if (auto* pi = addImageItem(si.rect, si.imageData)) {
                if (auto* simg = dynamic_cast<SlideImageItem*>(pi)) {
                    if (si.brightness != 0 || si.contrast != 0)
                        simg->setAdjust(si.brightness, si.contrast);
                }
                pi->setTransformOriginPoint(pi->boundingRect().center());
                pi->setRotation(si.rotation);
                created = pi;
            }
            break;
        case SlideItemType::Table:
            if (auto* tb = addTable(si.rect, std::max(1, si.rows), std::max(1, si.cols), si.cells)) {
                tb->setTransformOriginPoint(tb->boundingRect().center());
                tb->setRotation(si.rotation);
                created = tb;
            }
            break;
        case SlideItemType::Chart:
            if (auto* ch = addChart(si.rect, si.chartKind, si.chartTitle,
                                    si.chartLabels, si.chartValues)) {
                ch->setTransformOriginPoint(ch->boundingRect().center());
                ch->setRotation(si.rotation);
                created = ch;
            }
            break;
        case SlideItemType::Shape:
            if (auto* sh = addShape(si.rect, si.shapeKind)) {
                if (auto* ssh = dynamic_cast<SlideShapeItem*>(sh)) {
                    ssh->setPen(QPen(si.penColor, si.penWidth));
                    ssh->setBrush(si.shapeKind == ShapeKind::Line ? QBrush(Qt::NoBrush)
                                                                  : QBrush(si.fillColor));
                }
                sh->setTransformOriginPoint(sh->boundingRect().center());
                sh->setRotation(si.rotation);
                created = sh;
            }
            break;
        }
        if (created) {
            created->setData(AnimationKey, static_cast<int>(si.animation));
            if (!si.hyperlink.isEmpty()) created->setData(HyperlinkKey, si.hyperlink);
            if (si.shadow) created->setGraphicsEffect(makeSoftShadow());
            if (si.opacity < 1.0) created->setOpacity(si.opacity);
        }
    }
}

void SlideScene::saveToData(SlideData& data) const {
    data.items.clear();
    // Persist the logical background colours (the brush itself may be a
    // gradient, whose .color() is meaningless — so read our cached values).
    data.background  = m_bg1;
    data.background2 = m_bg2;

    for (auto* it : items()) {
        if (it == backgroundItem()) continue;
        if (dynamic_cast<SlideHandleItem*>(it)) continue;
        if (it->parentItem()) continue;          // table cells are saved with their table

        SlideItem si;
        si.rotation  = it->rotation();
        si.animation = static_cast<ItemAnimation>(it->data(AnimationKey).toInt());
        si.shadow    = (it->graphicsEffect() != nullptr);
        si.opacity   = it->opacity();
        si.hyperlink = it->data(HyperlinkKey).toString();

        if (auto* tb = dynamic_cast<SlideTableItem*>(it)) {
            si.type  = SlideItemType::Table;
            si.rect  = tb->rect().translated(tb->pos());
            si.rows  = tb->rows();
            si.cols  = tb->cols();
            si.cells = tb->texts();
        } else if (auto* ch = dynamic_cast<SlideChartItem*>(it)) {
            si.type        = SlideItemType::Chart;
            si.rect        = ch->rect().translated(ch->pos());
            si.chartKind   = ch->kind();
            si.chartTitle  = ch->title();
            si.chartLabels = ch->labels();
            si.chartValues = ch->values();
        } else if (auto* sh = dynamic_cast<SlideShapeItem*>(it)) {
            si.type      = SlideItemType::Shape;
            si.shapeKind = sh->kind();
            si.rect      = sh->rect().translated(sh->pos());
            si.fillColor = sh->brush().color();
            si.penColor  = sh->pen().color();
            si.penWidth  = sh->pen().widthF();
        } else if (auto* ti = qgraphicsitem_cast<QGraphicsTextItem*>(it)) {
            si.type     = SlideItemType::TextBox;
            si.rect     = QRectF(ti->pos(), QSizeF(ti->textWidth(), ti->boundingRect().height()));
            si.text     = ti->toPlainText();
            si.html     = ti->toHtml();
            si.fontSize = ti->font().pointSizeF();
            si.penColor = ti->defaultTextColor();
            si.isPlaceholder = (ti->defaultTextColor() == QColor("#9CA3AF"));
        } else if (auto* ri = qgraphicsitem_cast<QGraphicsRectItem*>(it)) {
            si.type      = SlideItemType::Rectangle;
            si.rect      = ri->rect().translated(ri->pos());
            si.fillColor = ri->brush().color();
            si.penColor  = ri->pen().color();
            si.penWidth  = ri->pen().widthF();
        } else if (auto* ei = qgraphicsitem_cast<QGraphicsEllipseItem*>(it)) {
            si.type      = SlideItemType::Ellipse;
            si.rect      = ei->rect().translated(ei->pos());
            si.fillColor = ei->brush().color();
            si.penColor  = ei->pen().color();
            si.penWidth  = ei->pen().widthF();
        } else if (auto* pi = qgraphicsitem_cast<QGraphicsPixmapItem*>(it)) {
            si.type = SlideItemType::Image;
            si.rect = QRectF(pi->pos() + pi->offset(), pi->pixmap().size());

            QPixmap src = pi->pixmap();
            if (auto* simg = dynamic_cast<SlideImageItem*>(pi)) {
                src = simg->original();
                si.brightness = simg->brightness();
                si.contrast   = simg->contrast();
            }
            QByteArray bytes;
            QBuffer buf(&bytes);
            buf.open(QIODevice::WriteOnly);
            src.save(&buf, "PNG");
            si.imageData = bytes;
        } else {
            continue;
        }
        data.items.push_back(si);
    }
}

} // namespace NativeOffice
