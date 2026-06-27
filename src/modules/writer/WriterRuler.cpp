// ─────────────────────────────────────────────────────────────────────────────
// WriterRuler.cpp  (Tier 4 — rulers with tab stops)
// ─────────────────────────────────────────────────────────────────────────────
#include "WriterRuler.h"

#include <QTextEdit>
#include <QTextCursor>
#include <QTextBlockFormat>
#include <QTextOption>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QtMath>

namespace NativeOffice {

namespace {
constexpr int    kThickness = 22;             // ruler height (H) / width (V)
constexpr double kDpi       = 96.0 / 2.54;    // px per cm at 100 %
const QColor kBg("#EDEEF2");
const QColor kMarginZone("#C9CDD6");
const QColor kPaper("#FFFFFF");
const QColor kTick("#7B8194");
const QColor kMarker("#4B5563");
const QColor kBorder("#C0C4CE");
} // namespace

WriterRuler::WriterRuler(Qt::Orientation orient, QWidget* parent)
    : QWidget(parent), m_orient(orient)
{
    setMouseTracking(true);
    if (m_orient == Qt::Horizontal) setFixedHeight(kThickness);
    else                            setFixedWidth(kThickness);
}

void WriterRuler::setEditor(QTextEdit* ed) {
    m_editor = ed;
    if (ed) connect(ed, &QTextEdit::cursorPositionChanged, this, &WriterRuler::refreshFromCursor);
    refreshFromCursor();
}

void WriterRuler::setPageGeometry(int originPx, int pageLengthPx, int marginPx, double zoom) {
    m_origin = originPx; m_pageLen = pageLengthPx; m_margin = marginPx; m_zoom = zoom;
    update();
}

void WriterRuler::refreshFromCursor() {
    if (!m_editor) { update(); return; }
    const QTextBlockFormat bf = m_editor->textCursor().blockFormat();
    m_leftMargin  = bf.leftMargin();
    m_textIndent  = bf.textIndent();
    m_rightMargin = bf.rightMargin();
    m_tabs.clear();
    const auto tabs = bf.tabPositions();
    for (const QTextOption::Tab& t : tabs) {
        int type = 0;
        switch (t.type) {
        case QTextOption::CenterTab:    type = 1; break;
        case QTextOption::RightTab:     type = 2; break;
        case QTextOption::DelimiterTab: type = 3; break;
        default:                        type = 0; break;
        }
        m_tabs.append({ t.position, type });
    }
    update();
}

// ── Apply edits back to the paragraph ────────────────────────────────────────
void WriterRuler::applyIndents(double lm, double ti, double rm) {
    if (!m_editor) return;
    m_leftMargin = qMax(0.0, lm); m_textIndent = ti; m_rightMargin = qMax(0.0, rm);
    QTextCursor c = m_editor->textCursor();
    QTextBlockFormat bf;
    bf.setLeftMargin(m_leftMargin);
    bf.setTextIndent(m_textIndent);
    bf.setRightMargin(m_rightMargin);
    c.mergeBlockFormat(bf);
    update();
}

void WriterRuler::applyTabs() {
    if (!m_editor) return;
    QList<QTextOption::Tab> tabs;
    for (const Tab& t : m_tabs) {
        QTextOption::Tab qt;
        qt.position = t.pos;
        switch (t.type) {
        case 1: qt.type = QTextOption::CenterTab; break;
        case 2: qt.type = QTextOption::RightTab; break;
        case 3: qt.type = QTextOption::DelimiterTab; qt.delimiter = QLatin1Char('.'); break;
        default: qt.type = QTextOption::LeftTab; break;
        }
        tabs.append(qt);
    }
    std::sort(tabs.begin(), tabs.end(),
              [](const QTextOption::Tab& a, const QTextOption::Tab& b){ return a.position < b.position; });
    QTextCursor c = m_editor->textCursor();
    QTextBlockFormat bf;
    bf.setTabPositions(tabs);
    c.mergeBlockFormat(bf);
    update();
}

void WriterRuler::cycleTabType() {
    m_newTabType = (m_newTabType + 1) % 4;
    update();
}

// ── Painting ─────────────────────────────────────────────────────────────────
static void drawTabGlyph(QPainter& p, int x, int baseY, int type) {
    p.setPen(QPen(kMarker, 1.4));
    switch (type) {
    case 0: // Left
        p.drawLine(x, baseY - 7, x, baseY); p.drawLine(x, baseY, x + 6, baseY); break;
    case 1: // Center
        p.drawLine(x, baseY - 7, x, baseY); p.drawLine(x - 5, baseY, x + 5, baseY); break;
    case 2: // Right
        p.drawLine(x, baseY - 7, x, baseY); p.drawLine(x - 6, baseY, x, baseY); break;
    case 3: // Decimal
        p.drawLine(x, baseY - 7, x, baseY); p.drawLine(x - 5, baseY, x + 5, baseY);
        p.setBrush(kMarker); p.drawEllipse(QPointF(x + 4, baseY - 1), 1.2, 1.2); break;
    }
}

void WriterRuler::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), kBg);

    const double pxcm = kDpi * m_zoom;
    const bool H = (m_orient == Qt::Horizontal);
    const int W = width(), Ht = height();

    // Page zones (margins gray, paper white) + cm ticks.
    if (H) {
        const int y0 = 0, y1 = Ht;
        p.fillRect(QRect(m_origin, y0, m_margin, y1 - y0), kMarginZone);
        p.fillRect(QRect(textLeftPx(), y0, m_pageLen - 2 * m_margin, y1 - y0), kPaper);
        p.fillRect(QRect(textRightPx(), y0, m_margin, y1 - y0), kMarginZone);

        p.setPen(kTick);
        QFont f = p.font(); f.setPixelSize(8); p.setFont(f);
        for (int k = -30; k <= 80; ++k) {
            const int x = textLeftPx() + int(k * pxcm);
            if (x < m_origin || x > m_origin + m_pageLen) continue;
            p.drawLine(x, 2, x, 6);
            if (k != 0) p.drawText(QRect(x - 10, 6, 20, 11), Qt::AlignCenter, QString::number(qAbs(k)));
            const int xh = x + int(pxcm / 2);   // half-cm minor tick
            if (xh <= m_origin + m_pageLen) p.drawLine(xh, 3, xh, 5);
        }
    } else {
        const int x0 = 0, x1 = W;
        p.fillRect(QRect(x0, m_origin, x1 - x0, m_margin), kMarginZone);
        p.fillRect(QRect(x0, m_origin + m_margin, x1 - x0, m_pageLen - 2 * m_margin), kPaper);
        p.fillRect(QRect(x0, m_origin + m_pageLen - m_margin, x1 - x0, m_margin), kMarginZone);

        p.setPen(kTick);
        QFont f = p.font(); f.setPixelSize(8); p.setFont(f);
        const int top = m_origin + m_margin;
        for (int k = -30; k <= 80; ++k) {
            const int y = top + int(k * pxcm);
            if (y < m_origin || y > m_origin + m_pageLen) continue;
            p.drawLine(2, y, 6, y);
            if (k != 0) p.drawText(QRect(6, y - 6, kThickness - 6, 11), Qt::AlignCenter, QString::number(qAbs(k)));
        }
    }

    // Border.
    p.setPen(kBorder);
    if (H) p.drawLine(0, Ht - 1, W, Ht - 1);
    else   p.drawLine(W - 1, 0, W - 1, Ht);

    if (!H) return;   // vertical ruler: no indent markers / tabs

    // Corner tab-type selector box (sits above the vertical ruler).
    p.fillRect(QRect(0, 0, kThickness, Ht), kBg);
    p.setPen(kBorder); p.drawLine(kThickness, 0, kThickness, Ht);
    drawTabGlyph(p, 8, Ht - 4, m_newTabType);

    if (!m_editor) return;

    // Tab stops.
    for (const Tab& t : m_tabs) drawTabGlyph(p, valueToPx(t.pos), Ht - 4, t.type);

    // Indent markers.
    const int flX = valueToPx(m_leftMargin + m_textIndent);   // first line
    const int lbX = valueToPx(m_leftMargin);                  // left body
    const int rX  = textRightPx() - int(m_rightMargin);       // right indent
    p.setPen(QPen(kMarker, 1.0)); p.setBrush(kMarker);

    // First-line indent: house pointing down at the top.
    QPainterPath fl;
    fl.moveTo(flX - 5, 1); fl.lineTo(flX + 5, 1); fl.lineTo(flX + 5, 4); fl.lineTo(flX, 9); fl.lineTo(flX - 5, 4);
    fl.closeSubpath(); p.drawPath(fl);

    // Hanging indent: triangle pointing up.
    QPainterPath hg;
    hg.moveTo(lbX, Ht - 12); hg.lineTo(lbX + 5, Ht - 7); hg.lineTo(lbX - 5, Ht - 7);
    hg.closeSubpath(); p.drawPath(hg);
    // Left-indent box beneath it (drags both).
    p.drawRect(QRect(lbX - 5, Ht - 6, 10, 4));

    // Right indent: triangle pointing up.
    QPainterPath ri;
    ri.moveTo(rX, Ht - 12); ri.lineTo(rX + 5, Ht - 7); ri.lineTo(rX - 5, Ht - 7);
    ri.closeSubpath(); p.drawPath(ri);
}

// ── Hit testing ──────────────────────────────────────────────────────────────
int WriterRuler::hitTab(const QPoint& pt) const {
    for (int i = 0; i < m_tabs.size(); ++i) {
        const int x = valueToPx(m_tabs[i].pos);
        if (qAbs(pt.x() - x) <= 5 && pt.y() >= height() - 12) return i;
    }
    return -1;
}

void WriterRuler::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) { QWidget::mousePressEvent(e); return; }
    const QPoint pt = e->pos();
    m_drag = Drag::None; m_dragTab = -1; m_dragValid = true;

    if (m_orient == Qt::Vertical) {
        const int top = m_origin + m_margin, bot = m_origin + m_pageLen - m_margin;
        if (qAbs(pt.y() - top) <= 4)      m_drag = Drag::TopMargin;
        else if (qAbs(pt.y() - bot) <= 4) m_drag = Drag::BottomMargin;
        return;
    }

    // Corner tab-type box.
    if (pt.x() < kThickness) { cycleTabType(); return; }
    if (!m_editor) return;

    // Existing tab?
    const int ti = hitTab(pt);
    if (ti >= 0) { m_drag = Drag::Tab; m_dragTab = ti; return; }

    const int H = height();
    const int flX = valueToPx(m_leftMargin + m_textIndent);
    const int lbX = valueToPx(m_leftMargin);
    const int rX  = textRightPx() - int(m_rightMargin);

    if (qAbs(pt.x() - flX) <= 6 && pt.y() <= 10)               m_drag = Drag::FirstLine;
    else if (qAbs(pt.x() - lbX) <= 6 && pt.y() >= H - 6)       m_drag = Drag::LeftIndent;
    else if (qAbs(pt.x() - lbX) <= 6 && pt.y() >= H - 13)      m_drag = Drag::Hanging;
    else if (qAbs(pt.x() - rX)  <= 6 && pt.y() >= H - 13)      m_drag = Drag::RightIndent;
    // Page-margin boundaries: grab in the middle band where no marker sits.
    else if (qAbs(pt.x() - textLeftPx())  <= 3 && pt.y() > 9 && pt.y() < H - 13) m_drag = Drag::LeftMargin;
    else if (qAbs(pt.x() - textRightPx()) <= 3 && pt.y() > 9 && pt.y() < H - 13) m_drag = Drag::RightMargin;
    // Otherwise a click in the paper strip adds a new tab.
    else if (pt.x() > textLeftPx() && pt.x() < textRightPx() && pt.y() >= H - 13) {
        m_tabs.append({ qMax(0.0, pxToValue(pt.x())), m_newTabType });
        m_drag = Drag::Tab; m_dragTab = m_tabs.size() - 1;
        applyTabs();
    }
}

void WriterRuler::mouseMoveEvent(QMouseEvent* e) {
    const QPoint pt = e->pos();

    if (m_drag == Drag::None) {
        // Hover cursor feedback.
        Qt::CursorShape cs = Qt::ArrowCursor;
        if (m_orient == Qt::Vertical) {
            const int top = m_origin + m_margin, bot = m_origin + m_pageLen - m_margin;
            if (qAbs(pt.y() - top) <= 4 || qAbs(pt.y() - bot) <= 4) cs = Qt::SplitVCursor;
        } else if (m_editor) {
            if ((qAbs(pt.x() - textLeftPx()) <= 3 || qAbs(pt.x() - textRightPx()) <= 3)
                && pt.y() > 9 && pt.y() < height() - 13) cs = Qt::SplitHCursor;
        }
        setCursor(cs);
        return;
    }

    const double maxV = m_pageLen - 2 * m_margin;
    switch (m_drag) {
    case Drag::FirstLine: {
        const double v = qBound(0.0, pxToValue(pt.x()), maxV);
        applyIndents(m_leftMargin, v - m_leftMargin, m_rightMargin);
        break;
    }
    case Drag::Hanging: {
        // Move left edge of body lines but keep the first line where it is.
        const double firstLinePos = m_leftMargin + m_textIndent;
        const double nl = qBound(0.0, pxToValue(pt.x()), maxV);
        applyIndents(nl, firstLinePos - nl, m_rightMargin);
        break;
    }
    case Drag::LeftIndent: {
        // Move both: keep the first-line offset.
        const double nl = qBound(0.0, pxToValue(pt.x()), maxV);
        applyIndents(nl, m_textIndent, m_rightMargin);
        break;
    }
    case Drag::RightIndent: {
        const double nr = qBound(0.0, double(textRightPx() - pt.x()), maxV);
        applyIndents(m_leftMargin, m_textIndent, nr);
        break;
    }
    case Drag::Tab: {
        if (m_dragTab >= 0 && m_dragTab < m_tabs.size()) {
            m_tabs[m_dragTab].pos = qBound(0.0, pxToValue(pt.x()), maxV);
            // Dragging far off the ruler vertically marks it for removal.
            m_dragValid = (pt.y() >= -4 && pt.y() <= height() + 14);
            applyTabs();
        }
        break;
    }
    case Drag::LeftMargin:
        m_margin = qBound(10, pt.x() - m_origin, m_pageLen / 3); update(); break;
    case Drag::RightMargin:
        m_margin = qBound(10, m_origin + m_pageLen - pt.x(), m_pageLen / 3); update(); break;
    case Drag::TopMargin:
        m_margin = qBound(10, pt.y() - m_origin, m_pageLen / 3); update(); break;
    case Drag::BottomMargin:
        m_margin = qBound(10, m_origin + m_pageLen - pt.y(), m_pageLen / 3); update(); break;
    default: break;
    }
}

void WriterRuler::mouseReleaseEvent(QMouseEvent* e) {
    Q_UNUSED(e);
    if (m_drag == Drag::Tab && !m_dragValid && m_dragTab >= 0 && m_dragTab < m_tabs.size()) {
        m_tabs.removeAt(m_dragTab);
        applyTabs();
    } else if (m_drag == Drag::LeftMargin || m_drag == Drag::RightMargin
            || m_drag == Drag::TopMargin || m_drag == Drag::BottomMargin) {
        // Commit the (uniform) page margin in base px.
        emit marginChangeRequested(m_margin / m_zoom);
    }
    m_drag = Drag::None; m_dragTab = -1;
}

void WriterRuler::mouseDoubleClickEvent(QMouseEvent* e) {
    if (m_orient == Qt::Horizontal && e->pos().x() >= kThickness && m_editor)
        openTabsDialog();
    else
        QWidget::mouseDoubleClickEvent(e);
}

// ── Tabs dialog ──────────────────────────────────────────────────────────────
void WriterRuler::openTabsDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("Tabs");
    auto* root = new QVBoxLayout(&dlg);

    auto* listw = new QListWidget(&dlg);
    static const char* typeName[] = { "Left", "Center", "Right", "Decimal" };
    auto refill = [&]{
        listw->clear();
        for (const Tab& t : m_tabs)
            listw->addItem(QString("%1 cm   —   %2")
                               .arg(t.pos / (kDpi * m_zoom), 0, 'f', 2)
                               .arg(typeName[t.type]));
    };
    refill();
    root->addWidget(listw, 1);

    auto* addRow = new QHBoxLayout();
    auto* posSpin = new QDoubleSpinBox(&dlg);
    posSpin->setRange(0, 60); posSpin->setSuffix(" cm"); posSpin->setDecimals(2);
    auto* typeCombo = new QComboBox(&dlg);
    typeCombo->addItems({ "Left", "Center", "Right", "Decimal" });
    auto* addBtn = new QPushButton("Set", &dlg);
    addRow->addWidget(new QLabel("Position:", &dlg));
    addRow->addWidget(posSpin); addRow->addWidget(typeCombo); addRow->addWidget(addBtn);
    root->addLayout(addRow);

    connect(addBtn, &QPushButton::clicked, &dlg, [&]{
        const double pos = posSpin->value() * kDpi * m_zoom;
        m_tabs.append({ pos, typeCombo->currentIndex() });
        applyTabs(); refill();
    });

    auto* btnRow = new QHBoxLayout();
    auto* clearBtn = new QPushButton("Clear", &dlg);
    auto* clearAllBtn = new QPushButton("Clear All", &dlg);
    btnRow->addWidget(clearBtn); btnRow->addWidget(clearAllBtn); btnRow->addStretch();
    root->addLayout(btnRow);
    connect(clearBtn, &QPushButton::clicked, &dlg, [&]{
        const int r = listw->currentRow();
        if (r >= 0 && r < m_tabs.size()) { m_tabs.removeAt(r); applyTabs(); refill(); }
    });
    connect(clearAllBtn, &QPushButton::clicked, &dlg, [&]{ m_tabs.clear(); applyTabs(); refill(); });

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    root->addWidget(bb);
    dlg.exec();
}

} // namespace NativeOffice
