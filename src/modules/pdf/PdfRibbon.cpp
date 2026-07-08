// ─────────────────────────────────────────────────────────────────────────────
// PdfRibbon.cpp — see PdfRibbon.h. Layout mirrors WPS PDF's ribbon tabs
// (flat rows of icon-over-label buttons, a small Pan/Select pair at the left
// of viewer-centric tabs, dropdown menus where WPS has them).
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfRibbon.h"
#include "core/theme/ThemeManager.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

namespace NativeOffice {

namespace {

const QColor kIconColor("#3A3F4B");

QIcon paintIcon(const std::function<void(QPainter&)>& draw) {
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(kIconColor);
    pen.setWidthF(2.2);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    draw(p);
    p.end();
    return QIcon(pm);
}

// A little "document" outline used as the base of many icons.
void drawDoc(QPainter& p, const QRectF& r = QRectF(9, 7, 17, 24)) {
    p.drawRoundedRect(r, 2, 2);
}

QIcon panIcon() {
    return paintIcon([](QPainter& p) {
        // simplified open hand
        p.drawLine(QPointF(14, 22), QPointF(14, 13));
        p.drawLine(QPointF(18, 21), QPointF(18, 10));
        p.drawLine(QPointF(22, 21), QPointF(22, 11));
        p.drawLine(QPointF(26, 22), QPointF(26, 14));
        QPainterPath palm;
        palm.moveTo(14, 22);
        palm.cubicTo(14, 30, 18, 33, 21, 33);
        palm.cubicTo(26, 33, 28, 29, 28, 24);
        palm.lineTo(28, 17);
        p.drawPath(palm);
    });
}

QIcon selectIcon() {
    return paintIcon([](QPainter& p) {
        QPolygonF cur;
        cur << QPointF(13, 8) << QPointF(13, 30) << QPointF(19, 24)
            << QPointF(23, 32) << QPointF(26, 30) << QPointF(22, 23)
            << QPointF(29, 22);
        p.drawPolygon(cur);
    });
}

QIcon editContentIcon() {
    return paintIcon([](QPainter& p) {
        drawDoc(p);
        p.drawLine(QPointF(13, 14), QPointF(22, 14));
        p.drawLine(QPointF(13, 19), QPointF(22, 19));
        p.drawLine(QPointF(23, 33), QPointF(33, 23));   // pencil
        p.drawLine(QPointF(33, 23), QPointF(30, 20));
        p.drawLine(QPointF(30, 20), QPointF(20, 30));
        p.drawLine(QPointF(20, 30), QPointF(23, 33));
    });
}

QIcon addTextIcon() {
    return paintIcon([](QPainter& p) {
        p.setFont(QFont("Segoe UI", 14, QFont::DemiBold));
        p.drawText(QRectF(4, 6, 22, 26), Qt::AlignCenter, "A");
        p.drawLine(QPointF(29, 16), QPointF(29, 32));
        p.drawLine(QPointF(24, 16), QPointF(34, 16));
    });
}

QIcon addPictureIcon() {
    return paintIcon([](QPainter& p) {
        p.drawRoundedRect(QRectF(7, 9, 26, 22), 2, 2);
        p.drawEllipse(QPointF(14, 16), 2.5, 2.5);
        p.drawPolyline(QPolygonF({ QPointF(9, 29), QPointF(18, 20),
                                   QPointF(24, 26), QPointF(28, 22), QPointF(33, 27) }));
    });
}

QIcon convertIcon(const QString& tag) {
    return paintIcon([tag](QPainter& p) {
        drawDoc(p, QRectF(7, 7, 15, 21));
        QPolygonF arrow;
        arrow << QPointF(23, 18) << QPointF(29, 18) << QPointF(29, 15)
              << QPointF(34, 20) << QPointF(29, 25) << QPointF(29, 22)
              << QPointF(23, 22);
        p.setBrush(QColor(58, 63, 75, 40));
        p.drawPolygon(arrow);
        p.setBrush(Qt::NoBrush);
        p.setFont(QFont("Segoe UI", 8, QFont::Bold));
        p.drawText(QRectF(5, 24, 20, 14), Qt::AlignCenter, tag);
    });
}

QIcon splitMergeIcon(bool merge) {
    return paintIcon([merge](QPainter& p) {
        if (merge) {
            p.drawRoundedRect(QRectF(7, 7, 12, 16), 2, 2);
            p.drawRoundedRect(QRectF(21, 7, 12, 16), 2, 2);
            p.drawLine(QPointF(20, 27), QPointF(20, 34));
            p.drawLine(QPointF(16, 30), QPointF(20, 34));
            p.drawLine(QPointF(24, 30), QPointF(20, 34));
        } else {
            p.drawRoundedRect(QRectF(13, 6, 14, 18), 2, 2);
            p.drawLine(QPointF(12, 29), QPointF(6, 35));
            p.drawLine(QPointF(28, 29), QPointF(34, 35));
        }
    });
}

QIcon highlightIcon() {
    return paintIcon([](QPainter& p) {
        p.drawLine(QPointF(10, 26), QPointF(24, 12));
        p.drawLine(QPointF(24, 12), QPointF(28, 16));
        p.drawLine(QPointF(28, 16), QPointF(14, 30));
        p.drawLine(QPointF(14, 30), QPointF(10, 26));
        p.setBrush(QColor(255, 200, 0, 140));
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(8, 32, 24, 4));
    });
}

QIcon textCommentIcon() {
    return paintIcon([](QPainter& p) {
        p.drawRoundedRect(QRectF(7, 8, 26, 18), 3, 3);
        p.drawPolyline(QPolygonF({ QPointF(13, 26), QPointF(13, 33), QPointF(20, 26) }));
        p.setFont(QFont("Segoe UI", 8, QFont::Bold));
        p.drawText(QRectF(7, 8, 26, 18), Qt::AlignCenter, "A");
    });
}

QIcon textBoxIcon() {
    return paintIcon([](QPainter& p) {
        p.drawRect(QRectF(7, 10, 26, 20));
        p.setFont(QFont("Segoe UI", 10, QFont::DemiBold));
        p.drawText(QRectF(7, 10, 26, 20), Qt::AlignCenter, "A");
    });
}

QIcon calloutIcon() {
    return paintIcon([](QPainter& p) {
        p.drawRoundedRect(QRectF(14, 8, 20, 14), 2, 2);
        p.drawLine(QPointF(14, 20), QPointF(7, 32));
        p.drawLine(QPointF(7, 32), QPointF(16, 22));
    });
}

QIcon noteIcon() {
    return paintIcon([](QPainter& p) {
        p.drawRoundedRect(QRectF(8, 8, 24, 24), 2, 2);
        p.drawLine(QPointF(20, 14), QPointF(20, 26));
        p.drawLine(QPointF(14, 20), QPointF(26, 20));
    });
}

QIcon underlineIcon() {
    return paintIcon([](QPainter& p) {
        p.setFont(QFont("Segoe UI", 14));
        p.drawText(QRectF(8, 4, 24, 26), Qt::AlignCenter, "A");
        p.drawLine(QPointF(10, 33), QPointF(30, 33));
    });
}

QIcon strikeIcon() {
    return paintIcon([](QPainter& p) {
        p.setFont(QFont("Segoe UI", 14));
        p.drawText(QRectF(8, 4, 24, 28), Qt::AlignCenter, "A");
        p.drawLine(QPointF(8, 19), QPointF(32, 19));
    });
}

QIcon shapesIcon() {
    return paintIcon([](QPainter& p) {
        p.drawRect(QRectF(7, 7, 13, 13));
        p.drawEllipse(QRectF(20, 20, 13, 13));
        p.drawLine(QPointF(24, 7), QPointF(33, 16));
    });
}

QIcon drawIcon() {
    return paintIcon([](QPainter& p) {
        QPainterPath path;
        path.moveTo(7, 30);
        path.cubicTo(13, 12, 19, 34, 25, 16);
        path.cubicTo(28, 9, 32, 12, 33, 10);
        p.drawPath(path);
    });
}

QIcon stampIcon() {
    return paintIcon([](QPainter& p) {
        p.drawEllipse(QRectF(14, 7, 12, 10));
        p.drawLine(QPointF(17, 17), QPointF(15, 24));
        p.drawLine(QPointF(23, 17), QPointF(25, 24));
        p.drawRoundedRect(QRectF(9, 24, 22, 6), 2, 2);
        p.drawLine(QPointF(7, 34), QPointF(33, 34));
    });
}

QIcon linkIcon() {
    return paintIcon([](QPainter& p) {
        p.drawRoundedRect(QRectF(6, 16, 14, 8), 4, 4);
        p.drawRoundedRect(QRectF(20, 16, 14, 8), 4, 4);
        p.drawLine(QPointF(15, 20), QPointF(25, 20));
    });
}

QIcon bookmarkAddIcon() {
    return paintIcon([](QPainter& p) {
        p.drawPolyline(QPolygonF({ QPointF(10, 7), QPointF(24, 7), QPointF(24, 31),
                                   QPointF(17, 25), QPointF(10, 31), QPointF(10, 7) }));
        p.drawLine(QPointF(30, 12), QPointF(30, 22));
        p.drawLine(QPointF(25, 17), QPointF(35, 17));
    });
}

QIcon wipeIcon() {
    return paintIcon([](QPainter& p) {
        // eraser
        QPainterPath body;
        body.moveTo(10, 26); body.lineTo(22, 12); body.lineTo(31, 20);
        body.lineTo(19, 34); body.closeSubpath();
        p.drawPath(body);
        p.drawLine(QPointF(15, 20), QPointF(25, 29));
        p.drawLine(QPointF(10, 34), QPointF(33, 34));
    });
}

QIcon cropIcon() {
    return paintIcon([](QPainter& p) {
        p.drawLine(QPointF(13, 6), QPointF(13, 27));
        p.drawLine(QPointF(13, 27), QPointF(34, 27));
        p.drawLine(QPointF(6, 13), QPointF(27, 13));
        p.drawLine(QPointF(27, 13), QPointF(27, 34));
    });
}

QIcon pageIcon(const std::function<void(QPainter&)>& badge) {
    return paintIcon([badge](QPainter& p) {
        drawDoc(p, QRectF(8, 7, 17, 23));
        badge(p);
    });
}

QIcon insertPagesIcon()  { return pageIcon([](QPainter& p) {
    p.drawLine(QPointF(30, 22), QPointF(30, 34));
    p.drawLine(QPointF(24, 28), QPointF(36, 28)); }); }
QIcon deletePagesIcon()  { return pageIcon([](QPainter& p) {
    p.drawLine(QPointF(25, 24), QPointF(35, 33));
    p.drawLine(QPointF(35, 24), QPointF(25, 33)); }); }
QIcon extractPageIcon()  { return pageIcon([](QPainter& p) {
    p.drawLine(QPointF(30, 22), QPointF(30, 33));
    p.drawLine(QPointF(26, 29), QPointF(30, 33));
    p.drawLine(QPointF(34, 29), QPointF(30, 33)); }); }
QIcon replacePagesIcon() { return pageIcon([](QPainter& p) {
    p.drawLine(QPointF(24, 26), QPointF(35, 26));
    p.drawLine(QPointF(31, 22), QPointF(35, 26));
    p.drawLine(QPointF(24, 31), QPointF(35, 31));
    p.drawLine(QPointF(28, 35), QPointF(24, 31)); }); }
QIcon pageSizeIcon()     { return paintIcon([](QPainter& p) {
    p.drawRect(QRectF(7, 7, 26, 26));
    p.drawLine(QPointF(12, 20), QPointF(28, 20));
    p.drawLine(QPointF(12, 20), QPointF(16, 16));
    p.drawLine(QPointF(12, 20), QPointF(16, 24));
    p.drawLine(QPointF(28, 20), QPointF(24, 16));
    p.drawLine(QPointF(28, 20), QPointF(24, 24)); }); }

QIcon rotateIcon(bool left, bool all = false) {
    return paintIcon([left, all](QPainter& p) {
        QRectF arc(10, 10, 20, 20);
        p.drawArc(arc, left ? 30 * 16 : -210 * 16, 240 * 16);
        QPolygonF tri;
        if (left) tri << QPointF(8, 12) << QPointF(16, 12) << QPointF(12, 19);
        else      tri << QPointF(24, 12) << QPointF(32, 12) << QPointF(28, 19);
        p.setBrush(kIconColor);
        p.setPen(Qt::NoPen);
        p.drawPolygon(tri);
        if (all) {
            p.setPen(QPen(kIconColor, 2.2));
            p.drawRect(QRectF(16, 24, 12, 12));
        }
    });
}

QIcon compressIcon() {
    return paintIcon([](QPainter& p) {
        drawDoc(p, QRectF(10, 5, 20, 14));
        p.drawLine(QPointF(20, 23), QPointF(20, 35));
        p.drawLine(QPointF(15, 30), QPointF(20, 35));
        p.drawLine(QPointF(25, 30), QPointF(20, 35));
        p.drawLine(QPointF(10, 35), QPointF(30, 35));
    });
}

QIcon printIcon() {
    return paintIcon([](QPainter& p) {
        p.drawRect(QRectF(12, 6, 16, 8));
        p.drawRoundedRect(QRectF(7, 14, 26, 12), 2, 2);
        p.drawRect(QRectF(12, 26, 16, 8));
    });
}

QIcon formIcon() {
    return paintIcon([](QPainter& p) {
        drawDoc(p, QRectF(7, 6, 26, 28));
        p.drawRect(QRectF(11, 11, 5, 5));
        p.drawLine(QPointF(19, 13.5), QPointF(29, 13.5));
        p.drawRect(QRectF(11, 20, 5, 5));
        p.drawLine(QPointF(19, 22.5), QPointF(29, 22.5));
        p.drawLine(QPointF(12, 22), QPointF(14, 25));
        p.drawLine(QPointF(14, 25), QPointF(17, 19));
    });
}

QIcon signatureIcon() {
    return paintIcon([](QPainter& p) {
        QPainterPath s;
        s.moveTo(7, 28);
        s.cubicTo(12, 14, 18, 14, 16, 22);
        s.cubicTo(14, 30, 22, 28, 26, 18);
        p.drawPath(s);
        p.drawLine(QPointF(7, 33), QPointF(33, 33));
    });
}

QIcon encryptIcon() {
    return paintIcon([](QPainter& p) {
        p.drawRoundedRect(QRectF(10, 18, 20, 15), 2, 2);
        p.drawArc(QRectF(14, 8, 12, 14), 0, 180 * 16);
        p.drawLine(QPointF(14, 15), QPointF(14, 18));
        p.drawLine(QPointF(26, 15), QPointF(26, 18));
        p.drawLine(QPointF(20, 23), QPointF(20, 28));
    });
}

QIcon certIcon() {
    return paintIcon([](QPainter& p) {
        p.drawEllipse(QRectF(13, 6, 14, 14));
        p.drawEllipse(QRectF(17, 10, 6, 6));
        p.drawLine(QPointF(16, 19), QPointF(13, 33));
        p.drawLine(QPointF(24, 19), QPointF(27, 33));
        p.drawLine(QPointF(13, 33), QPointF(20, 29));
        p.drawLine(QPointF(27, 33), QPointF(20, 29));
    });
}

QIcon timestampIcon() {
    return paintIcon([](QPainter& p) {
        p.drawEllipse(QRectF(8, 8, 24, 24));
        p.drawLine(QPointF(20, 13), QPointF(20, 20));
        p.drawLine(QPointF(20, 20), QPointF(26, 24));
    });
}

QIcon validateIcon() {
    return paintIcon([](QPainter& p) {
        QPainterPath shield;
        shield.moveTo(20, 6);
        shield.lineTo(32, 10);
        shield.lineTo(30, 26);
        shield.lineTo(20, 34);
        shield.lineTo(10, 26);
        shield.lineTo(8, 10);
        shield.closeSubpath();
        p.drawPath(shield);
        p.drawPolyline(QPolygonF({ QPointF(14, 20), QPointF(18, 24), QPointF(26, 14) }));
    });
}

QIcon watermarkIcon() {
    return paintIcon([](QPainter& p) {
        drawDoc(p, QRectF(8, 6, 24, 28));
        p.save();
        p.translate(20, 20);
        p.rotate(-35);
        p.setFont(QFont("Segoe UI", 7, QFont::Bold));
        p.drawText(QRectF(-14, -7, 28, 14), Qt::AlignCenter, "DRAFT");
        p.restore();
    });
}

QIcon backgroundIcon() {
    return paintIcon([](QPainter& p) {
        p.drawRoundedRect(QRectF(7, 7, 26, 26), 2, 2);
        p.setBrush(QColor(58, 63, 75, 50));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(7, 22, 26, 11), 2, 2);
    });
}

QIcon pageNumberIcon() {
    return paintIcon([](QPainter& p) {
        drawDoc(p, QRectF(8, 6, 24, 28));
        p.setFont(QFont("Segoe UI", 8, QFont::Bold));
        p.drawText(QRectF(8, 24, 24, 10), Qt::AlignCenter, "1");
    });
}

QIcon headerFooterIcon() {
    return paintIcon([](QPainter& p) {
        p.drawRoundedRect(QRectF(8, 6, 24, 28), 2, 2);
        p.drawLine(QPointF(11, 11), QPointF(29, 11));
        p.drawLine(QPointF(11, 29), QPointF(29, 29));
    });
}

QIcon findIcon() {
    return paintIcon([](QPainter& p) {
        p.drawEllipse(QRectF(9, 9, 15, 15));
        p.drawLine(QPointF(22, 22), QPointF(32, 32));
    });
}

QIcon ocrIcon() {
    return paintIcon([](QPainter& p) {
        // scan corners
        p.drawPolyline(QPolygonF({ QPointF(12, 7), QPointF(7, 7), QPointF(7, 12) }));
        p.drawPolyline(QPolygonF({ QPointF(28, 7), QPointF(33, 7), QPointF(33, 12) }));
        p.drawPolyline(QPolygonF({ QPointF(7, 28), QPointF(7, 33), QPointF(12, 33) }));
        p.drawPolyline(QPolygonF({ QPointF(33, 28), QPointF(33, 33), QPointF(28, 33) }));
        p.setFont(QFont("Segoe UI", 9, QFont::Bold));
        p.drawText(QRectF(7, 7, 26, 26), Qt::AlignCenter, "A");
    });
}

QIcon translateIcon() {
    return paintIcon([](QPainter& p) {
        p.setFont(QFont("Segoe UI", 11, QFont::DemiBold));
        p.drawText(QRectF(4, 4, 18, 18), Qt::AlignCenter, "A");
        p.drawText(QRectF(19, 17, 18, 20), Qt::AlignCenter, "文");
    });
}

QIcon readModeIcon() {
    return paintIcon([](QPainter& p) {
        QPainterPath book;
        book.moveTo(20, 10);
        book.cubicTo(15, 6, 9, 7, 7, 9);
        book.lineTo(7, 30);
        book.cubicTo(9, 28, 15, 27, 20, 31);
        book.cubicTo(25, 27, 31, 28, 33, 30);
        book.lineTo(33, 9);
        book.cubicTo(31, 7, 25, 6, 20, 10);
        book.lineTo(20, 31);
        p.drawPath(book);
    });
}

QIcon attachmentIcon() {
    return paintIcon([](QPainter& p) {
        QPainterPath clip;
        clip.moveTo(26, 10);
        clip.cubicTo(22, 5, 14, 6, 12, 12);
        clip.lineTo(9, 26);
        clip.cubicTo(8, 33, 18, 35, 20, 28);
        clip.lineTo(23, 14);
        p.drawPath(clip);
    });
}

QIcon caretIcon(bool insert) {
    return paintIcon([insert](QPainter& p) {
        p.drawPolyline(QPolygonF({ QPointF(12, 30), QPointF(20, 18), QPointF(28, 30) }));
        if (insert) p.drawLine(QPointF(20, 18), QPointF(20, 8));
        else        p.drawLine(QPointF(10, 10), QPointF(30, 10));
    });
}

QIcon commentsPaneIcon(int kind) {   // 0 hide, 1 manage, 2 export, 3 import
    return paintIcon([kind](QPainter& p) {
        p.drawRoundedRect(QRectF(7, 8, 22, 16), 3, 3);
        p.drawPolyline(QPolygonF({ QPointF(12, 24), QPointF(12, 30), QPointF(18, 24) }));
        switch (kind) {
        case 0: p.drawLine(QPointF(28, 28), QPointF(36, 36));
                p.drawLine(QPointF(36, 28), QPointF(28, 36)); break;
        case 1: p.drawLine(QPointF(28, 30), QPointF(36, 30));
                p.drawLine(QPointF(28, 34), QPointF(36, 34)); break;
        case 2: p.drawLine(QPointF(32, 26), QPointF(32, 36));
                p.drawLine(QPointF(28, 32), QPointF(32, 36));
                p.drawLine(QPointF(36, 32), QPointF(32, 36)); break;
        default:p.drawLine(QPointF(32, 36), QPointF(32, 26));
                p.drawLine(QPointF(28, 30), QPointF(32, 26));
                p.drawLine(QPointF(36, 30), QPointF(32, 26)); break;
        }
    });
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// construction
// ─────────────────────────────────────────────────────────────────────────────

PdfRibbon::PdfRibbon(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("pdfRibbon");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* tabRow = new QWidget(this);
    tabRow->setObjectName("ribbonTabRow");
    tabRow->setFixedHeight(32);
    auto* tabLayout = new QHBoxLayout(tabRow);
    tabLayout->setContentsMargins(10, 0, 10, 0);
    tabLayout->setSpacing(2);

    // ☰ File menu — leftmost, before the tabs (like WPS).
    auto* fileBtn = new QToolButton(tabRow);
    fileBtn->setObjectName("ribbonFileBtn");
    fileBtn->setText(tr("☰ File"));
    fileBtn->setPopupMode(QToolButton::InstantPopup);
    fileBtn->setCursor(Qt::PointingHandCursor);
    auto* fileMenu = new QMenu(fileBtn);
    fileMenu->addAction(tr("Open…"), QKeySequence::Open, this,
                        [this] { emit action(PdfAction::OpenFile); });
    fileMenu->addAction(tr("Save"), QKeySequence::Save, this,
                        [this] { emit action(PdfAction::SaveFile); });
    fileMenu->addAction(tr("Save As…"), QKeySequence::SaveAs, this,
                        [this] { emit action(PdfAction::SaveFileAs); });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Undo"), QKeySequence::Undo, this,
                        [this] { emit action(PdfAction::UndoEdit); });
    fileMenu->addAction(tr("Redo"), QKeySequence::Redo, this,
                        [this] { emit action(PdfAction::RedoEdit); });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Print…"), QKeySequence::Print, this,
                        [this] { emit action(PdfAction::Print); });
    fileBtn->setMenu(fileMenu);
    tabLayout->addWidget(fileBtn);
    tabLayout->addSpacing(6);

    m_tabGroup = new QButtonGroup(this);
    m_tabGroup->setExclusive(true);

    const QStringList tabNames = {
        tr("Home"), tr("Edit"), tr("Page"), tr("Comment"),
        tr("Tool"), tr("Fill && Sign"), tr("Protect"), tr("Convert")
    };
    for (int i = 0; i < tabNames.size(); ++i) {
        auto* btn = makeTabButton(tabNames[i]);
        m_tabGroup->addButton(btn, i);
        tabLayout->addWidget(btn);
    }
    tabLayout->addStretch();
    m_tabGroup->button(0)->setChecked(true);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName("ribbonStack");
    m_stack->setFixedHeight(92);
    m_stack->addWidget(buildHomeTab());
    for (int i = 1; i < 8; ++i)
        m_stack->addWidget(new QWidget(this));
    m_tabBuilt = { true, false, false, false, false, false, false, false };

    connect(m_tabGroup, &QButtonGroup::idClicked, this, [this](int id) {
        ensureTabBuilt(id);
        m_stack->setCurrentIndex(id);
        emit tabChanged(id);
    });

    root->addWidget(tabRow);
    root->addWidget(m_stack);

    applyStyles();
    connect(&ThemeManager::instance(), &ThemeManager::modeChanged,
            this, [this](ThemeMode) { applyStyles(); });
}

void PdfRibbon::ensureTabBuilt(int id) {
    if (id < 0 || id >= m_tabBuilt.size() || m_tabBuilt[id]) return;
    QWidget* built = nullptr;
    switch (id) {
    case 1: built = buildEditTab();     break;
    case 2: built = buildPageTab();     break;
    case 3: built = buildCommentTab();  break;
    case 4: built = buildToolTab();     break;
    case 5: built = buildFillSignTab(); break;
    case 6: built = buildProtectTab();  break;
    case 7: built = buildConvertTab();  break;
    default: return;
    }
    QWidget* placeholder = m_stack->widget(id);
    m_stack->removeWidget(placeholder);
    placeholder->deleteLater();
    m_stack->insertWidget(id, built);
    m_tabBuilt[id] = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// building blocks
// ─────────────────────────────────────────────────────────────────────────────

QToolButton* PdfRibbon::makeTabButton(const QString& text) {
    auto* b = new QToolButton(this);
    b->setObjectName("ribbonTab");
    b->setText(text);
    b->setCheckable(true);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QToolButton* PdfRibbon::makeBigBtn(const QString& text, const QIcon& icon,
                                   PdfAction a, const QString& tip, bool enabled) {
    auto* b = new QToolButton(this);
    b->setObjectName("ribbonBigBtn");
    b->setText(text);
    b->setIcon(icon);
    b->setIconSize(QSize(22, 22));
    b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    b->setToolTip(tip.isEmpty() ? text : tip);
    b->setEnabled(enabled);
    connect(b, &QToolButton::clicked, this, [this, a] { emit action(a); });
    return b;
}

QToolButton* PdfRibbon::makeBigMenuBtn(const QString& text, const QIcon& icon) {
    auto* b = new QToolButton(this);
    b->setObjectName("ribbonBigBtn");
    b->setText(text);
    b->setIcon(icon);
    b->setIconSize(QSize(22, 22));
    b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    b->setPopupMode(QToolButton::InstantPopup);
    return b;
}

QWidget* PdfRibbon::makeToolPair(QToolButton*& panBtn, QToolButton*& selBtn,
                                 const QString& panLabel) {
    auto* w = new QWidget(this);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(4, 6, 4, 6);
    v->setSpacing(4);

    auto mk = [this](const QString& text, const QIcon& icon) {
        auto* b = new QToolButton(this);
        b->setObjectName("ribbonSmallBtn");
        b->setText(text);
        b->setIcon(icon);
        b->setIconSize(QSize(16, 16));
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setCheckable(true);
        return b;
    };
    panBtn = mk(panLabel, panIcon());
    selBtn = mk(tr("Select Tool"), selectIcon());
    connect(panBtn, &QToolButton::clicked, this, [this] { emit action(PdfAction::PanTool); });
    connect(selBtn, &QToolButton::clicked, this, [this] { emit action(PdfAction::SelectTool); });
    m_panBtns << panBtn;
    m_selBtns << selBtn;

    v->addWidget(panBtn);
    v->addWidget(selBtn);
    return w;
}

QWidget* PdfRibbon::makeSeparator() {
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedWidth(1);
    sep->setObjectName("ribbonSep");
    return sep;
}

void PdfRibbon::setZoomPercent(int percent) {
    if (m_zoomCombo && !m_zoomCombo->hasFocus())
        m_zoomCombo->setCurrentText(QString::number(percent) + "%");
}

void PdfRibbon::syncToolMode(bool panActive) {
    for (auto* b : m_panBtns) b->setChecked(panActive);
    for (auto* b : m_selBtns) b->setChecked(!panActive);
}

// ─────────────────────────────────────────────────────────────────────────────
// tabs
// ─────────────────────────────────────────────────────────────────────────────

QWidget* PdfRibbon::buildHomeTab() {
    auto* tab = new QWidget(this);
    auto* h = new QHBoxLayout(tab);
    h->setContentsMargins(8, 2, 8, 6);
    h->setSpacing(4);

    QToolButton *pan = nullptr, *sel = nullptr;
    h->addWidget(makeToolPair(pan, sel, tr("Pan")));
    sel->setChecked(true);
    h->addWidget(makeSeparator());

    h->addWidget(makeBigBtn(tr("Edit Content"), editContentIcon(), PdfAction::EditContent,
                            tr("Inline editing of existing PDF text — coming soon"), false));
    h->addWidget(makeBigBtn(tr("Add Text"), addTextIcon(), PdfAction::AddText));
    h->addWidget(makeBigBtn(tr("Add Picture"), addPictureIcon(), PdfAction::AddPicture));
    h->addWidget(makeSeparator());

    auto* conv = makeBigMenuBtn(tr("PDF Converter"), convertIcon("⇄"));
    auto* convMenu = new QMenu(conv);
    convMenu->addAction(tr("PDF to Word…"),  this, [this] { emit action(PdfAction::ToWord); });
    convMenu->addAction(tr("PDF to Excel…"), this, [this] { emit action(PdfAction::ToExcel); });
    convMenu->addAction(tr("PDF to PPT…"),   this, [this] { emit action(PdfAction::ToPpt); });
    convMenu->addAction(tr("PDF to TXT…"),   this, [this] { emit action(PdfAction::ToText); });
    convMenu->addSeparator();
    convMenu->addAction(tr("Picture to PDF…"), this, [this] { emit action(PdfAction::PictureToPdf); });
    convMenu->addAction(tr("To Image-only PDF…"), this, [this] { emit action(PdfAction::ToImageOnlyPdf); });
    conv->setMenu(convMenu);
    h->addWidget(conv);

    h->addWidget(makeBigBtn(tr("PDF to Picture"), convertIcon("PNG"), PdfAction::ToPicture));

    auto* sm = makeBigMenuBtn(tr("Split and Merge"), splitMergeIcon(true));
    auto* smMenu = new QMenu(sm);
    smMenu->addAction(tr("Merge PDFs…"), this, [this] { emit action(PdfAction::MergePdf); });
    smMenu->addAction(tr("Split PDF…"),  this, [this] { emit action(PdfAction::SplitPdf); });
    sm->setMenu(smMenu);
    h->addWidget(sm);
    h->addWidget(makeSeparator());

    auto* hl = makeBigMenuBtn(tr("Highlight"), highlightIcon());
    auto* hlMenu = new QMenu(hl);
    hlMenu->addAction(tr("Highlight Text"), this, [this] { emit action(PdfAction::HighlightText); });
    hlMenu->addAction(tr("Highlight Area"), this, [this] { emit action(PdfAction::HighlightArea); });
    hl->setMenu(hlMenu);
    h->addWidget(hl);

    h->addWidget(makeBigBtn(tr("Text comment"), textCommentIcon(), PdfAction::TextComment));
    h->addWidget(makeBigBtn(tr("Text Box"), textBoxIcon(), PdfAction::TextBox));

    auto* sign = makeBigMenuBtn(tr("Sign"), signatureIcon());
    auto* signMenu = new QMenu(sign);
    signMenu->addAction(tr("Add Signature…"), this, [this] { emit action(PdfAction::AddSignature); });
    signMenu->addAction(tr("Add Initials…"),  this, [this] { emit action(PdfAction::AddInitials); });
    signMenu->addSeparator();
    signMenu->addAction(tr("Certificate Signature…"), this, [this] { emit action(PdfAction::CertSign); });
    sign->setMenu(signMenu);
    h->addWidget(sign);
    h->addWidget(makeSeparator());

    h->addWidget(makeBigBtn(tr("Extract Text"), convertIcon("TXT"), PdfAction::ExtractText));
    h->addWidget(makeBigBtn(tr("OCR PDF"), ocrIcon(), PdfAction::OcrPdf));
    h->addWidget(makeBigBtn(tr("Find"), findIcon(), PdfAction::Find, tr("Find text (Ctrl+F)")));
    h->addWidget(makeSeparator());

    // zoom cluster
    auto* zoomBox = new QWidget(tab);
    auto* zv = new QVBoxLayout(zoomBox);
    zv->setContentsMargins(4, 6, 4, 6);
    zv->setSpacing(4);
    m_zoomCombo = new QComboBox(zoomBox);
    m_zoomCombo->setObjectName("ribbonCombo");
    m_zoomCombo->setEditable(true);
    m_zoomCombo->setFixedWidth(76);
    for (int pct : { 50, 75, 100, 125, 150, 200, 300, 400 })
        m_zoomCombo->addItem(QString::number(pct) + "%");
    m_zoomCombo->setCurrentText("100%");
    connect(m_zoomCombo, &QComboBox::activated, this, [this](int) {
        QString t = m_zoomCombo->currentText();
        t.remove('%');
        bool ok = false;
        const int pct = t.trimmed().toInt(&ok);
        if (ok && pct > 0) emit zoomPercentRequested(pct);
    });
    auto* zoomRow = new QWidget(zoomBox);
    auto* zh = new QHBoxLayout(zoomRow);
    zh->setContentsMargins(0, 0, 0, 0);
    zh->setSpacing(2);
    auto* zOut = new QToolButton(zoomRow);
    zOut->setObjectName("ribbonSmallBtn");
    zOut->setText(QStringLiteral("−"));
    zOut->setToolTip(tr("Zoom out"));
    auto* zIn = new QToolButton(zoomRow);
    zIn->setObjectName("ribbonSmallBtn");
    zIn->setText(QStringLiteral("+"));
    zIn->setToolTip(tr("Zoom in"));
    connect(zOut, &QToolButton::clicked, this, [this] { emit action(PdfAction::ZoomOut); });
    connect(zIn,  &QToolButton::clicked, this, [this] { emit action(PdfAction::ZoomIn); });
    zh->addWidget(zOut);
    zh->addWidget(zIn);
    zv->addWidget(m_zoomCombo);
    zv->addWidget(zoomRow);
    h->addWidget(zoomBox);

    h->addWidget(makeBigBtn(tr("Rotate All Pages"), rotateIcon(false, true), PdfAction::RotateAllPages));
    h->addWidget(makeBigBtn(tr("Read Mode"), readModeIcon(), PdfAction::ReadMode,
                            tr("Hide the ribbon for distraction-free reading (Esc restores)")));

    h->addStretch();
    return tab;
}

QWidget* PdfRibbon::buildEditTab() {
    auto* tab = new QWidget(this);
    auto* h = new QHBoxLayout(tab);
    h->setContentsMargins(8, 2, 8, 6);
    h->setSpacing(4);

    QToolButton *pan = nullptr, *sel = nullptr;
    h->addWidget(makeToolPair(pan, sel, tr("Hand Tool")));
    h->addWidget(makeSeparator());

    h->addWidget(makeBigBtn(tr("Edit Content"), editContentIcon(), PdfAction::EditContent,
                            tr("Inline editing of existing PDF text — coming soon"), false));
    h->addWidget(makeBigBtn(tr("Add Text"), addTextIcon(), PdfAction::AddText));
    h->addWidget(makeBigBtn(tr("Add Picture"), addPictureIcon(), PdfAction::AddPicture));
    h->addWidget(makeBigBtn(tr("Wipe Off"), wipeIcon(), PdfAction::WipeOff,
                            tr("Cover an area with white (visual redaction)")));
    h->addWidget(makeSeparator());

    auto* shapes = makeBigMenuBtn(tr("Draw Shapes"), shapesIcon());
    auto* shMenu = new QMenu(shapes);
    shMenu->addAction(tr("Rectangle"), this, [this] { emit action(PdfAction::ShapeRect); });
    shMenu->addAction(tr("Ellipse"),   this, [this] { emit action(PdfAction::ShapeEllipse); });
    shMenu->addAction(tr("Line"),      this, [this] { emit action(PdfAction::ShapeLine); });
    shMenu->addAction(tr("Arrow"),     this, [this] { emit action(PdfAction::ShapeArrow); });
    shapes->setMenu(shMenu);
    h->addWidget(shapes);

    auto* stamp = makeBigMenuBtn(tr("Stamp"), stampIcon());
    auto* stMenu = new QMenu(stamp);
    for (const QString& s : { tr("APPROVED"), tr("REJECTED"), tr("DRAFT"),
                              tr("FINAL"), tr("CONFIDENTIAL"), tr("REVIEWED"),
                              tr("RECEIVED"), tr("VOID") })
        stMenu->addAction(s, this, [this, s] { emit stampSelected(s); });
    stamp->setMenu(stMenu);
    h->addWidget(stamp);

    h->addWidget(makeBigBtn(tr("Link"), linkIcon(), PdfAction::AddLink,
                            tr("Drag an area, then enter the URL it opens")));
    h->addWidget(makeBigBtn(tr("Attachment"), attachmentIcon(), PdfAction::AttachFile));
    h->addWidget(makeBigBtn(tr("Add Bookmark"), bookmarkAddIcon(), PdfAction::AddBookmark));
    h->addWidget(makeSeparator());

    h->addWidget(makeBigBtn(tr("Crop Pages"), cropIcon(), PdfAction::CropPages));
    h->addWidget(makeBigBtn(tr("Split Pages"), splitMergeIcon(false), PdfAction::SplitPdf));
    h->addWidget(makeSeparator());

    h->addWidget(makeBigBtn(tr("Watermark"), watermarkIcon(), PdfAction::Watermark));
    h->addWidget(makeBigBtn(tr("Background"), backgroundIcon(), PdfAction::Background));
    h->addWidget(makeBigBtn(tr("Page Number"), pageNumberIcon(), PdfAction::PageNumber));
    h->addWidget(makeBigBtn(tr("Header and Footer"), headerFooterIcon(), PdfAction::HeaderFooter));

    h->addStretch();
    return tab;
}

QWidget* PdfRibbon::buildPageTab() {
    auto* tab = new QWidget(this);
    auto* h = new QHBoxLayout(tab);
    h->setContentsMargins(8, 2, 8, 6);
    h->setSpacing(4);

    auto* ins = makeBigMenuBtn(tr("Insert Pages"), insertPagesIcon());
    auto* insMenu = new QMenu(ins);
    insMenu->addAction(tr("Blank Page…"),     this, [this] { emit action(PdfAction::InsertBlankPage); });
    insMenu->addAction(tr("From PDF File…"),  this, [this] { emit action(PdfAction::InsertFromFile); });
    ins->setMenu(insMenu);
    h->addWidget(ins);

    h->addWidget(makeBigBtn(tr("Delete Pages"), deletePagesIcon(), PdfAction::DeletePages));
    h->addWidget(makeBigBtn(tr("Extract Page"), extractPageIcon(), PdfAction::ExtractPageBtn));
    h->addWidget(makeBigBtn(tr("Replace Pages"), replacePagesIcon(), PdfAction::ReplacePages));
    h->addWidget(makeBigBtn(tr("Crop Pages"), cropIcon(), PdfAction::CropPages));
    h->addWidget(makeBigBtn(tr("Split Pages"), splitMergeIcon(false), PdfAction::SplitPdf));
    h->addWidget(makeBigBtn(tr("Page Size"), pageSizeIcon(), PdfAction::PageSizeDlg));
    h->addWidget(makeSeparator());

    h->addWidget(makeBigBtn(tr("Merge PDF"), splitMergeIcon(true), PdfAction::MergePdf));
    h->addWidget(makeBigBtn(tr("Split PDF"), splitMergeIcon(false), PdfAction::SplitPdf));
    h->addWidget(makeSeparator());

    h->addWidget(makeBigBtn(tr("Rotate Left"), rotateIcon(true), PdfAction::RotateLeft));
    h->addWidget(makeBigBtn(tr("Rotate Right"), rotateIcon(false), PdfAction::RotateRight));
    h->addWidget(makeBigBtn(tr("Rotate All Pages"), rotateIcon(false, true), PdfAction::RotateAllPages));

    h->addStretch();
    return tab;
}

QWidget* PdfRibbon::buildCommentTab() {
    auto* tab = new QWidget(this);
    auto* h = new QHBoxLayout(tab);
    h->setContentsMargins(8, 2, 8, 6);
    h->setSpacing(4);

    QToolButton *pan = nullptr, *sel = nullptr;
    h->addWidget(makeToolPair(pan, sel, tr("Pan")));
    h->addWidget(makeSeparator());

    h->addWidget(makeBigBtn(tr("Highlight Text"), highlightIcon(), PdfAction::HighlightText,
                            tr("Drag over text to highlight it")));
    h->addWidget(makeBigBtn(tr("Highlight Area"), backgroundIcon(), PdfAction::HighlightArea));
    h->addWidget(makeBigBtn(tr("Draw"), drawIcon(), PdfAction::InkDraw));
    h->addWidget(makeBigBtn(tr("Text Comment"), textCommentIcon(), PdfAction::TextComment));
    h->addWidget(makeBigBtn(tr("Text Box"), textBoxIcon(), PdfAction::TextBox));
    h->addWidget(makeBigBtn(tr("Callout"), calloutIcon(), PdfAction::Callout));

    auto* shapes = makeBigMenuBtn(tr("Shapes"), shapesIcon());
    auto* shMenu = new QMenu(shapes);
    shMenu->addAction(tr("Rectangle"), this, [this] { emit action(PdfAction::ShapeRect); });
    shMenu->addAction(tr("Ellipse"),   this, [this] { emit action(PdfAction::ShapeEllipse); });
    shMenu->addAction(tr("Line"),      this, [this] { emit action(PdfAction::ShapeLine); });
    shMenu->addAction(tr("Arrow"),     this, [this] { emit action(PdfAction::ShapeArrow); });
    shapes->setMenu(shMenu);
    h->addWidget(shapes);

    h->addWidget(makeBigBtn(tr("Note"), noteIcon(), PdfAction::NoteAnnot));
    h->addWidget(makeBigBtn(tr("Underline"), underlineIcon(), PdfAction::Underline));
    h->addWidget(makeBigBtn(tr("Strikethrough"), strikeIcon(), PdfAction::Strikethrough));
    h->addWidget(makeBigBtn(tr("Replace Text"), caretIcon(false), PdfAction::ReplaceTextAnnot,
                            tr("Mark text for replacement (strikethrough + note)")));
    h->addWidget(makeBigBtn(tr("Insert Text"), caretIcon(true), PdfAction::InsertTextAnnot,
                            tr("Insertion caret with a note")));
    h->addWidget(makeBigBtn(tr("Attachment"), attachmentIcon(), PdfAction::AttachFile));
    h->addWidget(makeSeparator());

    h->addWidget(makeBigBtn(tr("Hide Comments"), commentsPaneIcon(0), PdfAction::HideComments));
    h->addWidget(makeBigBtn(tr("Manage Comments"), commentsPaneIcon(1), PdfAction::ManageComments));
    h->addWidget(makeBigBtn(tr("Export Comments"), commentsPaneIcon(2), PdfAction::ExportComments));
    h->addWidget(makeBigBtn(tr("Import Comments"), commentsPaneIcon(3), PdfAction::ImportComments));

    h->addStretch();
    return tab;
}

QWidget* PdfRibbon::buildToolTab() {
    auto* tab = new QWidget(this);
    auto* h = new QHBoxLayout(tab);
    h->setContentsMargins(8, 2, 8, 6);
    h->setSpacing(4);

    QToolButton *pan = nullptr, *sel = nullptr;
    h->addWidget(makeToolPair(pan, sel, tr("Hand Tool")));
    h->addWidget(makeSeparator());

    h->addWidget(makeBigBtn(tr("PDF Compressor"), compressIcon(), PdfAction::Compress));
    h->addWidget(makeBigBtn(tr("Print"), printIcon(), PdfAction::Print, tr("Print (Ctrl+P)")));
    h->addWidget(makeSeparator());
    h->addWidget(makeBigBtn(tr("Parallel Translate"), translateIcon(), PdfAction::Translate,
                            tr("Coming soon")));

    h->addStretch();
    return tab;
}

QWidget* PdfRibbon::buildFillSignTab() {
    auto* tab = new QWidget(this);
    auto* h = new QHBoxLayout(tab);
    h->setContentsMargins(8, 2, 8, 6);
    h->setSpacing(4);

    h->addWidget(makeBigBtn(tr("Fill out Form"), formIcon(), PdfAction::FillForm,
                            tr("Click a form field to type into it")));
    h->addWidget(makeBigBtn(tr("Highlight Form Fields"), backgroundIcon(), PdfAction::HighlightFields));
    h->addWidget(makeSeparator());

    h->addWidget(makeBigBtn(tr("Add Signature"), signatureIcon(), PdfAction::AddSignature));
    h->addWidget(makeBigBtn(tr("Add Initials"), addTextIcon(), PdfAction::AddInitials));
    h->addWidget(makeSeparator());

    h->addWidget(makeBigBtn(tr("Certificate Signature"), certIcon(), PdfAction::CertSign));

    h->addStretch();
    return tab;
}

QWidget* PdfRibbon::buildProtectTab() {
    auto* tab = new QWidget(this);
    auto* h = new QHBoxLayout(tab);
    h->setContentsMargins(8, 2, 8, 6);
    h->setSpacing(4);

    QToolButton *pan = nullptr, *sel = nullptr;
    h->addWidget(makeToolPair(pan, sel, tr("Hand Tool")));
    h->addWidget(makeSeparator());

    h->addWidget(makeBigBtn(tr("Encrypt"), encryptIcon(), PdfAction::Encrypt));
    h->addWidget(makeBigBtn(tr("Certificate Signature"), certIcon(), PdfAction::CertSign));
    h->addWidget(makeBigBtn(tr("Timestamp"), timestampIcon(), PdfAction::Timestamp));
    h->addWidget(makeBigBtn(tr("Manage Certificates"), certIcon(), PdfAction::ManageCerts));
    h->addWidget(makeBigBtn(tr("Validate Signature"), validateIcon(), PdfAction::ValidateSigs));
    h->addWidget(makeSeparator());
    h->addWidget(makeBigBtn(tr("Watermark"), watermarkIcon(), PdfAction::Watermark));

    h->addStretch();
    return tab;
}

QWidget* PdfRibbon::buildConvertTab() {
    auto* tab = new QWidget(this);
    auto* h = new QHBoxLayout(tab);
    h->setContentsMargins(8, 2, 8, 6);
    h->setSpacing(4);

    QToolButton *pan = nullptr, *sel = nullptr;
    h->addWidget(makeToolPair(pan, sel, tr("Pan")));
    h->addWidget(makeSeparator());

    h->addWidget(makeBigBtn(tr("PDF to Word"),  convertIcon("W"),   PdfAction::ToWord));
    h->addWidget(makeBigBtn(tr("PDF to Excel"), convertIcon("X"),   PdfAction::ToExcel));
    h->addWidget(makeBigBtn(tr("PDF to PPT"),   convertIcon("P"),   PdfAction::ToPpt));
    h->addWidget(makeBigBtn(tr("PDF to Picture"), convertIcon("PNG"), PdfAction::ToPicture));
    h->addWidget(makeBigBtn(tr("PDF to TXT"),   convertIcon("TXT"), PdfAction::ToText));
    h->addWidget(makeSeparator());

    h->addWidget(makeBigBtn(tr("Picture to PDF"), addPictureIcon(), PdfAction::PictureToPdf));
    h->addWidget(makeBigBtn(tr("To Image-only PDF"), convertIcon("IMG"), PdfAction::ToImageOnlyPdf));
    h->addWidget(makeSeparator());

    h->addWidget(makeBigBtn(tr("Extract Text"), convertIcon("TXT"), PdfAction::ExtractText));
    h->addWidget(makeBigBtn(tr("Extract Page"), extractPageIcon(), PdfAction::ExtractPageBtn));
    h->addWidget(makeBigBtn(tr("Extract Picture"), addPictureIcon(), PdfAction::ExtractPicture));

    h->addStretch();
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// styling
// ─────────────────────────────────────────────────────────────────────────────

void PdfRibbon::applyStyles() {
    const auto& tm = ThemeManager::instance();
    setStyleSheet(QString(R"(
QWidget#pdfRibbon { background: %1; }
QWidget#ribbonTabRow { background: %1; border-bottom: 1px solid %2; }
QToolButton#ribbonTab {
    background: transparent; border: none; border-radius: 5px;
    padding: 5px 14px; color: %3; font: 10pt "Segoe UI";
}
QToolButton#ribbonTab:hover { background: %4; }
QToolButton#ribbonTab:checked { color: #6D5BE8; font-weight: 600; }
QToolButton#ribbonFileBtn {
    background: transparent; border: none; border-radius: 5px;
    padding: 5px 10px; color: %3; font: 10pt "Segoe UI";
}
QToolButton#ribbonFileBtn:hover { background: %4; }
QToolButton#ribbonFileBtn::menu-indicator { image: none; }
QStackedWidget#ribbonStack { background: %5; border-bottom: 1px solid %2; }
QToolButton#ribbonBigBtn {
    background: transparent; border: none; border-radius: 6px;
    padding: 4px 6px; color: %3; font: 8.5pt "Segoe UI";
    min-width: 52px; min-height: 66px;
}
QToolButton#ribbonBigBtn:hover { background: %4; }
QToolButton#ribbonBigBtn:pressed { background: %6; }
QToolButton#ribbonBigBtn:disabled { color: %7; }
QToolButton#ribbonBigBtn::menu-indicator { image: none; }
QToolButton#ribbonSmallBtn {
    background: transparent; border: none; border-radius: 4px;
    padding: 3px 8px; color: %3; font: 9pt "Segoe UI";
}
QToolButton#ribbonSmallBtn:hover { background: %4; }
QToolButton#ribbonSmallBtn:checked { background: %6; border: 1px solid %2; }
QFrame#ribbonSep { background: %2; margin: 10px 3px; }
QComboBox#ribbonCombo {
    background: %5; border: 1px solid %2; border-radius: 4px;
    padding: 2px 6px; color: %3; font: 9pt "Segoe UI";
}
QComboBox#ribbonCombo QAbstractItemView {
    background: %5; color: %3; border: 1px solid %2;
    selection-background-color: %4;
}
)")
        .arg(tm.chromeBg(), tm.chromeBorder(), tm.chromeText(), tm.chromeHoverBg(),
             tm.chromePanelBg(), tm.chromeActiveBg(), tm.chromeTextMuted()));
}

} // namespace NativeOffice
