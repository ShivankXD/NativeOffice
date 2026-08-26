// ─────────────────────────────────────────────────────────────────────────────
// WriterRibbon.cpp  (Sprint 14)
// WPS/Word-style tabbed ribbon implementation for NativeOffice Writer.
// ─────────────────────────────────────────────────────────────────────────────
#include "WriterRibbon.h"
#include "WriterTableOps.h"
#include "WriterListOps.h"
#include "WriterStyles.h"
#include "WriterEquation.h"
#include "WriterAi.h"
#include "WriterCollab.h"
#include "PagedTextEdit.h"
#include "core/watermark/WatermarkPdf.h"
#include "core/settings/ExportPrefs.h"
#include "SpellChecker.h"
#include "DocxIo.h"
#include <QTextDocumentFragment>
#include "core/theme/ThemeManager.h"
#include <QApplication>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QToolButton>
#include <QComboBox>
#include <QButtonGroup>
#include <QStackedWidget>
#include <QLabel>
#include <QFrame>
#include <QMenu>
#include <QWidgetAction>
#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QColorDialog>
#include <QMessageBox>
#include <QRegularExpression>
#include <QFontDatabase>
#include <QCompleter>
#include <QShortcut>
#include <QClipboard>
#include <QApplication>
#include <QGuiApplication>
#include <QTextEdit>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTextBlockFormat>
#include <QTextListFormat>
#include <QTextList>
#include <QTextTable>
#include <QTextFrame>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextOption>
#include <QTextDocument>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QPdfWriter>
#include <QPageSize>
#include <QPageLayout>
#include <QPrinter>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <memory>
#include <functional>
#include <QListWidget>
#include <QDateTime>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QImage>
#include <QBuffer>
#include <QByteArray>
#include <QLinearGradient>
#include <QSpinBox>
#include <QPainter>
#include <QPixmap>
#include <QToolTip>
#include <QCursor>
#include <QIcon>
#include <QFontMetrics>
#include <QtMath>
#include <QStyledItemDelegate>
#include <QListView>
#include <QListWidget>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QPolygonF>
#include <QPainterPath>
#include <QEvent>
#include <QMouseEvent>
#include <QDir>
#include <QStringList>
#include <algorithm>
#include <functional>

namespace NativeOffice {

namespace {

const QColor kIconColor("#3A3F4B");

// Block property tagging a caption paragraph with its kind ("Figure"/"Table"/…),
// so "Update Fields" can renumber them in document order.
constexpr int kCaptionKindProp = QTextFormat::UserProperty + 20;
// Index entries are stored as a character-format property on the marked run.
// They used to be written into the document as literal "{index:...}" text,
// which meant marking a selection pasted the whole selection back in.
constexpr int kIndexTermProp   = QTextFormat::UserProperty + 21;

// Collect the bookmark (anchor) names present in a document.
QStringList collectBookmarks(QTextDocument* doc) {
    QStringList names;
    for (QTextBlock b = doc->begin(); b != doc->end(); b = b.next()) {
        for (auto it = b.begin(); it != b.end(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid()) continue;
            for (const QString& a : frag.charFormat().anchorNames())
                if (!a.isEmpty() && !names.contains(a)) names << a;
        }
    }
    return names;
}

// Renders each font-picker row in its own typeface, but lazily — the font is
// only built when a row is actually painted/measured, so opening the editor
// doesn't have to load every installed font up front.
class FontPreviewDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void initStyleOption(QStyleOptionViewItem* opt, const QModelIndex& idx) const override {
        QStyledItemDelegate::initStyleOption(opt, idx);
        opt->font = QFont(idx.data(Qt::DisplayRole).toString());
        opt->fontMetrics = QFontMetrics(opt->font);
    }
};

// Paint a crisp monochrome icon on a 40×40 canvas (displayed ~20×20).
QIcon paintIcon(const std::function<void(QPainter&)>& draw) {
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(kIconColor);
    pen.setWidthF(2.4);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    draw(p);
    p.end();
    return QIcon(pm);
}

// mode: 0 left, 1 center, 2 right, 3 justify
QIcon alignIcon(int mode) {
    return paintIcon([mode](QPainter& p) {
        const int ys[4]     = { 11, 18, 25, 32 };
        const int widths[4] = { 26, 16, 22, 14 };
        for (int i = 0; i < 4; ++i) {
            const int w = (mode == 3) ? 26 : widths[i];
            int x0, x1;
            if      (mode == 0) { x0 = 7;        x1 = 7 + w; }
            else if (mode == 2) { x1 = 33;       x0 = 33 - w; }
            else if (mode == 1) { x0 = 20 - w/2; x1 = 20 + w/2; }
            else                { x0 = 7;        x1 = 33; }
            p.drawLine(QPointF(x0, ys[i]), QPointF(x1, ys[i]));
        }
    });
}

QIcon bulletIcon(bool numbered) {
    return paintIcon([numbered](QPainter& p) {
        const int ys[3] = { 12, 21, 30 };
        for (int i = 0; i < 3; ++i) {
            p.setPen(QPen(kIconColor, 2.2, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(17, ys[i]), QPointF(33, ys[i]));
            if (numbered) {
                QFont f("Segoe UI", 7, QFont::Bold);
                p.setFont(f);
                p.drawText(QRectF(4, ys[i] - 7, 11, 14),
                           Qt::AlignRight | Qt::AlignVCenter, QString::number(i + 1));
            } else {
                p.setBrush(kIconColor); p.setPen(Qt::NoPen);
                p.drawEllipse(QPointF(9, ys[i]), 2.2, 2.2);
            }
        }
    });
}

QIcon indentIcon(bool increase) {
    return paintIcon([increase](QPainter& p) {
        const int ys[4] = { 11, 18, 25, 32 };
        for (int i = 0; i < 4; ++i) {
            const int x0 = (i == 1 || i == 2) ? 18 : 8;
            p.drawLine(QPointF(x0, ys[i]), QPointF(33, ys[i]));
        }
        p.setBrush(kIconColor); p.setPen(Qt::NoPen);
        QPolygonF tri;
        if (increase) tri << QPointF(8, 17) << QPointF(8, 26) << QPointF(14, 21.5);
        else          tri << QPointF(14, 17) << QPointF(14, 26) << QPointF(8, 21.5);
        p.drawPolygon(tri);
    });
}

QIcon pasteIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(10, 9, 20, 26), 2, 2);          // clipboard board
    p.setBrush(kIconColor); p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(15, 6, 10, 6), 2, 2);           // clip
    p.setBrush(Qt::white);
    QPen pen(kIconColor); pen.setWidthF(2.2); p.setPen(pen);
    p.drawRoundedRect(QRectF(17, 17, 17, 18), 2, 2);         // pasted page
}); }

QIcon cutIcon() { return paintIcon([](QPainter& p) {
    p.drawEllipse(QRectF(7, 24, 9, 9));
    p.drawEllipse(QRectF(24, 24, 9, 9));
    p.drawLine(QPointF(15, 25), QPointF(31, 9));
    p.drawLine(QPointF(25, 25), QPointF(9, 9));
}); }

QIcon copyIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(8, 8, 17, 20), 2, 2);
    p.drawRoundedRect(QRectF(16, 14, 17, 20), 2, 2);
}); }

QIcon painterIcon() { return paintIcon([](QPainter& p) {
    p.setBrush(QColor(58, 63, 75, 55)); p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(8, 8, 18, 9), 2, 2);            // brush body
    QPen pen(kIconColor); pen.setWidthF(2.4); p.setPen(pen); p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(8, 8, 18, 9), 2, 2);
    p.drawRect(QRectF(22, 11, 8, 4));                        // ferrule
    p.drawLine(QPointF(26, 15), QPointF(26, 24));            // handle
    p.drawLine(QPointF(26, 24), QPointF(20, 30));
    p.drawLine(QPointF(20, 30), QPointF(20, 35));
}); }

QIcon clearFmtIcon() { return paintIcon([](QPainter& p) {
    QFont f("Segoe UI", 17, QFont::Bold); p.setFont(f);
    p.setPen(kIconColor);
    p.drawText(QRectF(4, 2, 26, 34), Qt::AlignCenter, "A");
    p.setPen(QPen(QColor("#E8372A"), 2.4, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(24, 28), QPointF(36, 16));
    p.drawLine(QPointF(24, 16), QPointF(36, 28));
}); }

QIcon sortIcon() { return paintIcon([](QPainter& p) {
    const int ys[3] = { 12, 21, 30 };
    const int ws[3] = { 10, 16, 22 };
    for (int i = 0; i < 3; ++i)
        p.drawLine(QPointF(8, ys[i]), QPointF(8 + ws[i], ys[i]));
    p.drawLine(QPointF(31, 9), QPointF(31, 32));
    p.setBrush(kIconColor); p.setPen(Qt::NoPen);
    QPolygonF tri; tri << QPointF(27, 27) << QPointF(35, 27) << QPointF(31, 34);
    p.drawPolygon(tri);
}); }

QIcon marksIcon() { return paintIcon([](QPainter& p) {
    QFont f("Segoe UI", 22, QFont::Bold); p.setFont(f);
    p.setPen(kIconColor);
    p.drawText(QRectF(0, 0, 40, 40), Qt::AlignCenter, QString::fromUtf8("¶"));
}); }

QIcon rulerIcon() { return paintIcon([](QPainter& p) {
    p.drawRect(QRectF(6, 14, 28, 12));
    for (int x : { 11, 16, 21, 26, 31 }) p.drawLine(QPointF(x, 14), QPointF(x, x % 10 == 6 ? 22 : 19));
}); }

QIcon autoCorrectIcon() { return paintIcon([](QPainter& p) {
    QFont f("Segoe UI", 16, QFont::Bold); p.setFont(f);
    p.drawText(QRectF(2, 4, 26, 32), Qt::AlignCenter, "A");
    p.setPen(QPen(QColor("#1AA463"), 3.0));
    p.drawLine(QPointF(24, 26), QPointF(29, 31)); p.drawLine(QPointF(29, 31), QPointF(36, 20));
}); }

QIcon aiIcon() { return paintIcon([](QPainter& p) {
    // A small sparkle / star — the common "AI" glyph.
    p.setBrush(kIconColor); p.setPen(Qt::NoPen);
    QPolygonF star;
    const QPointF c(20, 20);
    for (int i = 0; i < 8; ++i) {
        const double ang = i * M_PI / 4.0;
        const double r = (i % 2 == 0) ? 13.0 : 5.0;
        star << QPointF(c.x() + r * std::cos(ang), c.y() + r * std::sin(ang));
    }
    p.drawPolygon(star);
    p.setBrush(QColor("#FFFFFF"));
    p.drawEllipse(c, 3.0, 3.0);
}); }

QIcon printIcon() { return paintIcon([](QPainter& p) {
    p.drawRect(QRectF(8, 16, 24, 12));               // printer body
    p.drawRect(QRectF(12, 7, 16, 9));                // paper in
    p.drawRect(QRectF(12, 24, 16, 9));               // paper out
    p.setBrush(kIconColor); p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(28, 20), 1.4, 1.4);
}); }

QIcon templateIcon() { return paintIcon([](QPainter& p) {
    p.drawRect(QRectF(9, 6, 22, 28));                // page
    p.drawLine(QPointF(13, 12), QPointF(27, 12));
    p.drawLine(QPointF(13, 17), QPointF(27, 17));
    p.drawLine(QPointF(13, 22), QPointF(23, 22));
}); }

QIcon mailMergeIcon() { return paintIcon([](QPainter& p) {
    p.drawRect(QRectF(7, 11, 26, 18));               // envelope
    p.drawLine(QPointF(7, 11), QPointF(20, 21));
    p.drawLine(QPointF(33, 11), QPointF(20, 21));
}); }

QIcon navPaneIcon() { return paintIcon([](QPainter& p) {
    p.drawRect(QRectF(6, 7, 11, 26));                // side panel
    p.drawLine(QPointF(20, 12), QPointF(34, 12));
    p.drawLine(QPointF(20, 20), QPointF(34, 20));
    p.drawLine(QPointF(20, 28), QPointF(30, 28));
}); }

QIcon collabIcon() { return paintIcon([](QPainter& p) {
    // Two overlapping person glyphs.
    p.setBrush(kIconColor); p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(15, 14), 5, 5);
    p.drawEllipse(QPointF(26, 16), 4, 4);
    QPainterPath a; a.moveTo(6, 33); a.arcTo(6, 22, 18, 22, 0, 180); p.drawPath(a);
    QPainterPath b; b.moveTo(20, 33); b.arcTo(20, 25, 15, 18, 0, 180); p.drawPath(b);
}); }

QIcon compareIcon() { return paintIcon([](QPainter& p) {
    p.drawRect(QRectF(5, 8, 13, 24));                // left page
    p.drawRect(QRectF(22, 8, 13, 24));               // right page
    p.setPen(QPen(QColor("#16A34A"), 2.0));
    p.drawLine(QPointF(24, 15), QPointF(32, 15));
    p.setPen(QPen(QColor("#C0271C"), 2.0));
    p.drawLine(QPointF(8, 22), QPointF(15, 22));
}); }

QIcon lineSpacingIcon() { return paintIcon([](QPainter& p) {
    for (int y : { 11, 18, 25, 32 }) p.drawLine(QPointF(16, y), QPointF(33, y));
    p.drawLine(QPointF(9, 9), QPointF(9, 34));
    p.setBrush(kIconColor); p.setPen(Qt::NoPen);
    QPolygonF up;   up   << QPointF(6, 14) << QPointF(12, 14) << QPointF(9, 9);
    QPolygonF down; down << QPointF(6, 29) << QPointF(12, 29) << QPointF(9, 34);
    p.drawPolygon(up); p.drawPolygon(down);
}); }

QIcon shadingIcon() { return paintIcon([](QPainter& p) {
    p.setBrush(QColor(58, 63, 75, 50)); p.setPen(Qt::NoPen);
    p.drawRect(QRectF(8, 9, 24, 18));
    QPen pen(kIconColor); pen.setWidthF(2.2); p.setPen(pen);
    p.drawLine(QPointF(10, 33), QPointF(30, 33));
}); }

QIcon bordersIcon() { return paintIcon([](QPainter& p) {
    QPen dotted(kIconColor); dotted.setWidthF(1.6); dotted.setStyle(Qt::DotLine);
    p.setPen(dotted);
    p.drawLine(QPointF(8, 8), QPointF(32, 8));
    p.drawLine(QPointF(8, 20), QPointF(32, 20));
    p.drawLine(QPointF(8, 8), QPointF(8, 32));
    p.drawLine(QPointF(32, 8), QPointF(32, 32));
    QPen solid(kIconColor); solid.setWidthF(2.6); p.setPen(solid);
    p.drawLine(QPointF(8, 32), QPointF(32, 32));
}); }

QIcon findIcon() { return paintIcon([](QPainter& p) {
    p.drawEllipse(QRectF(9, 9, 16, 16));
    p.drawLine(QPointF(23, 23), QPointF(33, 33));
}); }

QIcon caseIcon() { return paintIcon([](QPainter& p) {
    QFont f("Segoe UI", 15, QFont::Bold); p.setFont(f);
    p.setPen(kIconColor);
    p.drawText(QRectF(2, 6, 20, 28), Qt::AlignCenter, "A");
    QFont f2("Segoe UI", 11, QFont::Bold); p.setFont(f2);
    p.drawText(QRectF(20, 12, 18, 24), Qt::AlignCenter, "a");
}); }

QIcon growIcon(bool grow) { return paintIcon([grow](QPainter& p) {
    QFont big("Segoe UI", grow ? 18 : 13, QFont::Bold);
    p.setFont(big); p.setPen(kIconColor);
    p.drawText(QRectF(2, 2, 24, 36), Qt::AlignCenter, "A");
    p.setBrush(kIconColor); p.setPen(Qt::NoPen);
    if (grow) { QPolygonF t; t << QPointF(28, 14) << QPointF(36, 14) << QPointF(32, 8); p.drawPolygon(t); }
    else      { QPolygonF t; t << QPointF(28, 26) << QPointF(36, 26) << QPointF(32, 32); p.drawPolygon(t); }
}); }

// ── Insert-tab icons ─────────────────────────────────────────────────────────
QIcon insTableIcon() { return paintIcon([](QPainter& p) {
    p.drawRect(QRectF(7, 9, 26, 22));
    p.drawLine(QPointF(7, 17),  QPointF(33, 17));
    p.drawLine(QPointF(7, 25),  QPointF(33, 25));
    p.drawLine(QPointF(16, 9),  QPointF(16, 31));
    p.drawLine(QPointF(25, 9),  QPointF(25, 31));
}); }

// Small grid preview drawn with the requested rows × cols (for menu presets).
QIcon tablePresetIcon(int rows, int cols) {
    return paintIcon([rows, cols](QPainter& p) {
        const QRectF box(7, 9, 26, 22);
        p.setPen(QPen(kIconColor, 1.8));
        p.drawRect(box);
        for (int c = 1; c < cols; ++c) {
            const double x = box.left() + c * box.width() / cols;
            p.drawLine(QPointF(x, box.top()), QPointF(x, box.bottom()));
        }
        for (int r = 1; r < rows; ++r) {
            const double y = box.top() + r * box.height() / rows;
            p.drawLine(QPointF(box.left(), y), QPointF(box.right(), y));
        }
    });
}

QIcon insPictureIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(6, 9, 28, 22), 2, 2);
    p.drawEllipse(QPointF(13, 16), 2.6, 2.6);
    QPolygonF m; m << QPointF(8, 29) << QPointF(16, 20) << QPointF(22, 25)
                   << QPointF(27, 20) << QPointF(33, 29);
    p.drawPolyline(m);
}); }

QIcon insShapesIcon() { return paintIcon([](QPainter& p) {
    p.drawRect(QRectF(7, 8, 14, 14));
    p.setBrush(QColor(58, 63, 75, 40)); p.setPen(QPen(kIconColor, 2.4));
    p.drawEllipse(QRectF(19, 18, 14, 14));
}); }

QIcon insPageBreakIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(9, 6, 22, 11), 2, 2);
    p.drawRoundedRect(QRectF(9, 23, 22, 11), 2, 2);
    QPen dash(QColor("#E8372A")); dash.setWidthF(2.0); dash.setStyle(Qt::DashLine);
    p.setPen(dash); p.drawLine(QPointF(6, 20), QPointF(34, 20));
}); }

QIcon insBlankPageIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(11, 6, 18, 28), 2, 2);
    p.setBrush(kIconColor); p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(30, 30), 5, 5);
    p.setPen(QPen(Qt::white, 1.6));
    p.drawLine(QPointF(30, 27), QPointF(30, 33));
    p.drawLine(QPointF(27, 30), QPointF(33, 30));
}); }

QIcon insHrIcon() { return paintIcon([](QPainter& p) {
    p.setPen(QPen(kIconColor, 2.6, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(6, 20), QPointF(34, 20));
    p.setPen(QPen(QColor(58,63,75,90), 1.4));
    p.drawLine(QPointF(10, 12), QPointF(30, 12));
    p.drawLine(QPointF(10, 28), QPointF(30, 28));
}); }

QIcon insLinkIcon() { return paintIcon([](QPainter& p) {
    QPen pen(kIconColor); pen.setWidthF(2.6); p.setPen(pen);
    p.drawArc(QRectF(6, 14, 16, 16), 30 * 16, 180 * 16);
    p.drawArc(QRectF(18, 10, 16, 16), 210 * 16, 180 * 16);
    p.drawLine(QPointF(16, 20), QPointF(24, 20));
}); }

QIcon insBookmarkIcon() { return paintIcon([](QPainter& p) {
    p.setBrush(QColor(58, 63, 75, 40));
    QPolygonF bm; bm << QPointF(12, 6) << QPointF(28, 6) << QPointF(28, 34)
                     << QPointF(20, 27) << QPointF(12, 34);
    p.drawPolygon(bm);
}); }

QIcon insPageNumIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(8, 7, 24, 26), 2, 2);
    QFont f("Segoe UI", 13, QFont::Bold); p.setFont(f);
    p.setPen(kIconColor); p.drawText(QRectF(8, 7, 24, 26), Qt::AlignCenter, "#");
}); }

QIcon insTextBoxIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(6, 10, 28, 20), 2, 2);
    p.drawLine(QPointF(13, 17), QPointF(27, 17));
    p.drawLine(QPointF(20, 17), QPointF(20, 24));
}); }

QIcon insWordArtIcon() { return paintIcon([](QPainter& p) {
    QFont f("Georgia", 22, QFont::Bold); f.setItalic(true); p.setFont(f);
    QLinearGradient g(6, 6, 34, 34); g.setColorAt(0, QColor("#E8372A")); g.setColorAt(1, QColor("#2C3140"));
    p.setPen(QPen(QBrush(g), 1)); p.drawText(QRectF(0, 0, 40, 40), Qt::AlignCenter, "A");
}); }

QIcon insDropCapIcon() { return paintIcon([](QPainter& p) {
    QFont big("Georgia", 26, QFont::Bold); p.setFont(big); p.setPen(kIconColor);
    p.drawText(QRectF(4, 2, 22, 36), Qt::AlignVCenter | Qt::AlignLeft, "A");
    p.setPen(QPen(QColor(58,63,75,120), 1.6));
    for (int y : { 12, 18, 24, 30 }) p.drawLine(QPointF(26, y), QPointF(35, y));
}); }

QIcon insDateTimeIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(7, 9, 26, 24), 2, 2);
    p.drawLine(QPointF(7, 16), QPointF(33, 16));
    p.drawLine(QPointF(13, 6), QPointF(13, 11));
    p.drawLine(QPointF(27, 6), QPointF(27, 11));
    p.setPen(QPen(kIconColor, 1.8));
    p.drawLine(QPointF(20, 20), QPointF(20, 26));
    p.drawLine(QPointF(20, 26), QPointF(25, 26));
}); }

QIcon insSymbolIcon() { return paintIcon([](QPainter& p) {
    QFont f("Segoe UI", 20, QFont::Bold); p.setFont(f); p.setPen(kIconColor);
    p.drawText(QRectF(0, 0, 40, 40), Qt::AlignCenter, QString::fromUtf8("Ω"));
}); }

// "A" (or highlighter) with a coloured underline bar that reflects the colour.
QIcon colorBarIcon(const QString& glyph, const QColor& bar, bool highlighter) {
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    if (highlighter) {
        // little marker tip
        QPen pen(kIconColor); pen.setWidthF(2.2); p.setPen(pen);
        p.setBrush(QColor(255, 255, 255, 0));
        p.drawLine(QPointF(11, 9), QPointF(27, 25));
        p.drawLine(QPointF(9, 11), QPointF(25, 27));
        p.setBrush(QColor(58, 63, 75, 40)); p.setPen(Qt::NoPen);
        QPolygonF tip; tip << QPointF(9, 11) << QPointF(11, 9) << QPointF(27, 25) << QPointF(25, 27);
        p.drawPolygon(tip);
    } else {
        QFont f("Segoe UI", 19, QFont::Bold);
        p.setFont(f);
        p.setPen(kIconColor);
        p.drawText(QRectF(0, -2, 40, 36), Qt::AlignCenter, glyph);
    }
    p.fillRect(QRectF(7, 32, 26, 5), bar);
    p.end();
    return QIcon(pm);
}

// ── Menu sub-option icons ────────────────────────────────────────────────────
QIcon listStyleIcon(int style) {
    return paintIcon([style](QPainter& p) {
        const int ys[3] = { 12, 21, 30 };
        for (int i = 0; i < 3; ++i) {
            p.setPen(QPen(kIconColor, 2.0, Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            p.drawLine(QPointF(18, ys[i]), QPointF(33, ys[i]));
            switch (static_cast<QTextListFormat::Style>(style)) {
            case QTextListFormat::ListDisc:
                p.setBrush(kIconColor); p.setPen(Qt::NoPen);
                p.drawEllipse(QPointF(10, ys[i]), 2.4, 2.4); break;
            case QTextListFormat::ListCircle:
                p.setBrush(Qt::NoBrush); p.setPen(QPen(kIconColor, 1.4));
                p.drawEllipse(QPointF(10, ys[i]), 2.4, 2.4); break;
            case QTextListFormat::ListSquare:
                p.setBrush(kIconColor); p.setPen(Qt::NoPen);
                p.drawRect(QRectF(7.6, ys[i] - 2.2, 4.4, 4.4)); break;
            default: {
                QFont f("Segoe UI", 7, QFont::Bold); p.setFont(f); p.setPen(kIconColor);
                QString m;
                switch (static_cast<QTextListFormat::Style>(style)) {
                case QTextListFormat::ListDecimal:    m = QString::number(i + 1); break;
                case QTextListFormat::ListLowerAlpha: m = QString(QChar('a' + i)); break;
                case QTextListFormat::ListUpperAlpha: m = QString(QChar('A' + i)); break;
                case QTextListFormat::ListLowerRoman: { static const char* r[] = {"i","ii","iii"}; m = r[i]; break; }
                case QTextListFormat::ListUpperRoman: { static const char* r[] = {"I","II","III"}; m = r[i]; break; }
                default: m = QString::number(i + 1);
                }
                p.drawText(QRectF(0, ys[i] - 7, 14, 14), Qt::AlignRight | Qt::AlignVCenter, m);
            }
            }
        }
    });
}

QIcon ulStyleIcon(int style) {
    return paintIcon([style](QPainter& p) {
        QFont f("Segoe UI", 15, QFont::Bold); p.setFont(f); p.setPen(kIconColor);
        p.drawText(QRectF(8, 0, 24, 26), Qt::AlignCenter, "U");
        QPen pen(kIconColor); pen.setWidthF(2.4); pen.setCapStyle(Qt::RoundCap);
        const auto s = static_cast<QTextCharFormat::UnderlineStyle>(style);
        if (s == QTextCharFormat::WaveUnderline) {
            p.setPen(pen);
            QPainterPath path; path.moveTo(8, 32);
            for (int x = 8; x <= 32; x += 4)
                path.quadTo(x + 1, 28, x + 2, 32);
            p.drawPath(path);
        } else {
            switch (s) {
            case QTextCharFormat::DotLine:      pen.setStyle(Qt::DotLine); break;
            case QTextCharFormat::DashUnderline:pen.setStyle(Qt::DashLine); break;
            case QTextCharFormat::DashDotLine:  pen.setStyle(Qt::DashDotLine); break;
            default:                            pen.setStyle(Qt::SolidLine); break;
            }
            p.setPen(pen);
            p.drawLine(QPointF(8, 32), QPointF(32, 32));
        }
    });
}

QIcon plainTextIcon() { return paintIcon([](QPainter& p) {
    QFont f("Segoe UI", 18, QFont::Bold); p.setFont(f); p.setPen(kIconColor);
    p.drawText(QRectF(2, 2, 36, 36), Qt::AlignCenter, "T");
}); }

QIcon selectAllIcon() { return paintIcon([](QPainter& p) {
    QPen dash(kIconColor); dash.setStyle(Qt::DashLine); dash.setWidthF(1.8);
    p.setPen(dash); p.drawRect(QRectF(8, 9, 24, 22));
}); }

QIcon selectSimilarIcon() { return paintIcon([](QPainter& p) {
    p.setBrush(QColor(58, 63, 75, 45)); p.setPen(QPen(kIconColor, 1.8));
    p.drawRect(QRectF(7, 10, 11, 9));
    p.drawRect(QRectF(22, 10, 11, 9));
    p.drawRect(QRectF(7, 23, 11, 9));
}); }

QIcon coverPageIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(10, 6, 20, 28), 2, 2);
    p.setBrush(QColor(58, 63, 75, 45)); p.setPen(Qt::NoPen);
    p.drawRect(QRectF(14, 11, 12, 5));
    p.setPen(QPen(kIconColor, 1.4));
    for (int y : { 22, 26, 30 }) p.drawLine(QPointF(14, y), QPointF(26, y));
}); }

QIcon headerIcon(bool footer) { return paintIcon([footer](QPainter& p) {
    p.drawRoundedRect(QRectF(8, 6, 24, 28), 2, 2);
    p.setBrush(QColor(58, 63, 75, 55)); p.setPen(Qt::NoPen);
    if (footer) p.drawRect(QRectF(8, 28, 24, 6));
    else        p.drawRect(QRectF(8, 6, 24, 6));
    p.setPen(QPen(QColor(58,63,75,120), 1.3));
    for (int y : { 16, 20, 24 }) p.drawLine(QPointF(12, y), QPointF(28, y));
}); }

QIcon equationIcon() { return paintIcon([](QPainter& p) {
    QFont f("Cambria Math", 20, QFont::Bold); f.setItalic(true); p.setFont(f);
    p.setPen(kIconColor);
    p.drawText(QRectF(0, 0, 40, 40), Qt::AlignCenter, QString::fromUtf8("√x"));
}); }

QIcon chartKindIcon(int kind) { return paintIcon([kind](QPainter& p) {
    p.drawLine(QPointF(9, 8),  QPointF(9, 32));
    p.drawLine(QPointF(9, 32), QPointF(34, 32));
    p.setBrush(QColor(232, 55, 42, 90)); p.setPen(QPen(kIconColor, 1.6));
    switch (kind) {
    case 0:  // bar
        p.drawRect(QRectF(13, 22, 5, 10));
        p.drawRect(QRectF(21, 16, 5, 16));
        p.drawRect(QRectF(29, 12, 5, 20)); break;
    case 1:  // line
        p.setPen(QPen(QColor("#E8372A"), 2.2));
        p.drawPolyline(QPolygonF({ QPointF(12,28), QPointF(19,18), QPointF(26,23), QPointF(33,12) })); break;
    case 2:  // pie
        p.drawEllipse(QRectF(13, 11, 20, 20));
        p.drawLine(QPointF(23, 21), QPointF(23, 11));
        p.drawLine(QPointF(23, 21), QPointF(33, 21)); break;
    }
}); }

// ── Page Layout / References / Review / View / Tools icons ───────────────────
QIcon marginsIcon() { return paintIcon([](QPainter& p) {
    p.drawRect(QRectF(7, 7, 26, 26));
    QPen dash(QColor(58,63,75,140)); dash.setStyle(Qt::DashLine); dash.setWidthF(1.4); p.setPen(dash);
    p.drawRect(QRectF(12, 12, 16, 16));
}); }

QIcon orientationIcon(bool landscape) { return paintIcon([landscape](QPainter& p) {
    if (landscape) p.drawRoundedRect(QRectF(5, 11, 30, 18), 2, 2);
    else           p.drawRoundedRect(QRectF(11, 5, 18, 30), 2, 2);
    QPen thin(QColor(58,63,75,120)); thin.setWidthF(1.2); p.setPen(thin);
    if (landscape) for (int y : {16, 20, 24}) p.drawLine(QPointF(10, y), QPointF(30, y));
    else           for (int y : {12, 17, 22, 27}) p.drawLine(QPointF(15, y), QPointF(25, y));
}); }

QIcon pageSizeIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(9, 6, 22, 28), 2, 2);
    QFont f("Segoe UI", 7, QFont::Bold); p.setFont(f);
    p.drawText(QRectF(9, 6, 22, 28), Qt::AlignCenter, "A4");
}); }

QIcon pageColorIcon() { return paintIcon([](QPainter& p) {
    p.setBrush(QColor(232, 55, 42, 70)); p.setPen(QPen(kIconColor, 1.8));
    p.drawRoundedRect(QRectF(9, 7, 22, 26), 2, 2);
    p.fillRect(QRectF(9, 28, 22, 5), QColor("#E8372A"));
}); }

QIcon indentSideIcon(bool right) { return paintIcon([right](QPainter& p) {
    const int ys[4] = { 11, 18, 25, 32 };
    for (int i = 0; i < 4; ++i) p.drawLine(QPointF(8, ys[i]), QPointF(32, ys[i]));
    p.setBrush(kIconColor); p.setPen(Qt::NoPen);
    QPolygonF tri;
    if (right) tri << QPointF(30, 16) << QPointF(30, 24) << QPointF(36, 20);
    else       tri << QPointF(10, 16) << QPointF(10, 24) << QPointF(4, 20);
    p.drawPolygon(tri);
}); }

QIcon spacingIcon(bool before) { return paintIcon([before](QPainter& p) {
    for (int y : { 14, 20, 26 }) p.drawLine(QPointF(16, y), QPointF(33, y));
    p.setBrush(kIconColor); p.setPen(Qt::NoPen);
    if (before) { QPolygonF t; t << QPointF(7, 12) << QPointF(13, 12) << QPointF(10, 7); p.drawPolygon(t);
                  p.setPen(QPen(kIconColor, 1.6)); p.drawLine(QPointF(10, 7), QPointF(10, 16)); }
    else        { QPolygonF t; t << QPointF(7, 28) << QPointF(13, 28) << QPointF(10, 33); p.drawPolygon(t);
                  p.setPen(QPen(kIconColor, 1.6)); p.drawLine(QPointF(10, 24), QPointF(10, 33)); }
}); }

QIcon tocIcon() { return paintIcon([](QPainter& p) {
    const int ys[4] = { 10, 17, 24, 31 };
    p.setBrush(kIconColor);
    for (int i = 0; i < 4; ++i) {
        p.setPen(Qt::NoPen); p.drawEllipse(QPointF(8, ys[i]), 1.6, 1.6);
        p.setPen(QPen(kIconColor, 1.8));
        p.drawLine(QPointF(13, ys[i]), QPointF(24 + i % 2 * 6, ys[i]));
        p.drawLine(QPointF(31, ys[i]), QPointF(33, ys[i]));
    }
}); }

QIcon footnoteIcon() { return paintIcon([](QPainter& p) {
    QFont f("Segoe UI", 15, QFont::Bold); p.setFont(f); p.setPen(kIconColor);
    p.drawText(QRectF(4, 4, 20, 24), Qt::AlignCenter, "A");
    QFont s("Segoe UI", 9, QFont::Bold); p.setFont(s);
    p.drawText(QRectF(22, 6, 14, 14), Qt::AlignCenter, "1");
    p.setPen(QPen(QColor(58,63,75,120), 1.2));
    p.drawLine(QPointF(7, 30), QPointF(20, 30));
}); }

QIcon citationIcon() { return paintIcon([](QPainter& p) {
    QFont f("Georgia", 26, QFont::Bold); p.setFont(f); p.setPen(kIconColor);
    p.drawText(QRectF(0, -4, 40, 40), Qt::AlignCenter, QString::fromUtf8("”"));
}); }

QIcon bibliographyIcon() { return paintIcon([](QPainter& p) {
    p.drawRect(QRectF(8, 7, 9, 26));
    p.drawRect(QRectF(18, 7, 9, 26));
    QPainterPath bk; bk.moveTo(28, 9); bk.lineTo(34, 11); bk.lineTo(31, 34); bk.lineTo(25, 32); bk.closeSubpath();
    p.drawPath(bk);
}); }

QIcon captionIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(7, 7, 26, 18), 2, 2);
    p.drawEllipse(QPointF(13, 14), 2.2, 2.2);
    QPolygonF m; m << QPointF(9, 22) << QPointF(15, 16) << QPointF(20, 20) << QPointF(31, 12);
    p.drawPolyline(m);
    p.setPen(QPen(QColor(58,63,75,140), 1.4));
    p.drawLine(QPointF(9, 31), QPointF(31, 31));
}); }

QIcon spellingIcon() { return paintIcon([](QPainter& p) {
    QFont f("Segoe UI", 15, QFont::Bold); p.setFont(f); p.setPen(kIconColor);
    p.drawText(QRectF(4, 0, 24, 28), Qt::AlignCenter, "ab");
    QPen wave(QColor("#E8372A")); wave.setWidthF(1.8); p.setPen(wave);
    QPainterPath path; path.moveTo(6, 32);
    for (int x = 6; x <= 30; x += 4) path.quadTo(x + 1, 28, x + 2, 32);
    p.drawPath(path);
    p.setPen(QPen(QColor("#16A34A"), 2.2, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(30, 12), QPointF(33, 16)); p.drawLine(QPointF(33, 16), QPointF(38, 7));
}); }

QIcon wordCountIcon() { return paintIcon([](QPainter& p) {
    QFont f("Segoe UI", 13, QFont::Bold); p.setFont(f); p.setPen(kIconColor);
    p.drawText(QRectF(2, 2, 36, 22), Qt::AlignCenter, "123");
    p.setPen(QPen(QColor(58,63,75,150), 1.6));
    for (int y : { 28, 33 }) p.drawLine(QPointF(8, y), QPointF(32, y));
}); }

QIcon commentInsIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(6, 8, 28, 17), 4, 4);
    p.drawLine(QPointF(13, 25), QPointF(12, 31));
    p.drawLine(QPointF(12, 31), QPointF(20, 25));
    p.setBrush(kIconColor); p.setPen(Qt::NoPen);
    for (int x : { 14, 20, 26 }) p.drawEllipse(QPointF(x, 16.5), 1.4, 1.4);
}); }

QIcon trackChangesIcon() { return paintIcon([](QPainter& p) {
    QPainterPath path; path.moveTo(8, 26); path.lineTo(24, 10);
    p.setPen(QPen(kIconColor, 2.4)); p.drawPath(path);
    p.setBrush(kIconColor); p.setPen(Qt::NoPen);
    QPolygonF nib; nib << QPointF(24, 10) << QPointF(30, 16) << QPointF(8, 26) << QPointF(8, 26);
    QPolygonF tip; tip << QPointF(22, 8) << QPointF(28, 14) << QPointF(31, 9) << QPointF(25, 5);
    p.drawPolygon(tip);
    p.setPen(QPen(QColor("#E8372A"), 2.0, Qt::SolidLine, Qt::RoundCap)); p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(7, 33), QPointF(20, 33));
}); }

QIcon layoutViewIcon(bool web) { return paintIcon([web](QPainter& p) {
    if (web) {
        p.drawRoundedRect(QRectF(5, 9, 30, 22), 2, 2);
        p.setPen(QPen(QColor(58,63,75,130), 1.2));
        for (int y : {15, 19, 23, 27}) p.drawLine(QPointF(9, y), QPointF(31, y));
    } else {
        p.drawRoundedRect(QRectF(11, 5, 18, 30), 2, 2);
        p.setBrush(QColor(58,63,75,40)); p.setPen(QPen(kIconColor, 1.8));
        p.drawRect(QRectF(15, 10, 10, 20));
    }
}); }

QIcon zoomIcon(int kind) { return paintIcon([kind](QPainter& p) {  // 0=in 1=out 2=100%
    p.drawEllipse(QRectF(7, 7, 18, 18));
    p.drawLine(QPointF(23, 23), QPointF(33, 33));
    if (kind == 2) {
        QFont f("Segoe UI", 6, QFont::Bold); p.setFont(f);
        p.drawText(QRectF(7, 7, 18, 18), Qt::AlignCenter, "100");
    } else {
        p.setPen(QPen(kIconColor, 2.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(11, 16), QPointF(21, 16));
        if (kind == 0) p.drawLine(QPointF(16, 11), QPointF(16, 21));
    }
}); }

QIcon statsIcon() { return paintIcon([](QPainter& p) {
    p.drawLine(QPointF(8, 8),  QPointF(8, 32));
    p.drawLine(QPointF(8, 32), QPointF(34, 32));
    p.setBrush(QColor(58, 63, 75, 70)); p.setPen(QPen(kIconColor, 1.4));
    p.drawRect(QRectF(12, 22, 5, 10));
    p.drawRect(QRectF(20, 15, 5, 17));
    p.drawRect(QRectF(28, 19, 5, 13));
}); }

QIcon textDirIcon() { return paintIcon([](QPainter& p) {
    QFont f("Segoe UI", 15, QFont::Bold); p.setFont(f); p.setPen(kIconColor);
    p.drawText(QRectF(2, 2, 24, 28), Qt::AlignCenter, "A");
    p.setPen(QPen(kIconColor, 2.0, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(8, 33), QPointF(32, 33));
    p.setBrush(kIconColor); p.setPen(Qt::NoPen);
    QPolygonF a; a << QPointF(32, 29) << QPointF(32, 37) << QPointF(37, 33); p.drawPolygon(a);
}); }

QIcon pageBordersIcon() { return paintIcon([](QPainter& p) {
    QPen thick(kIconColor); thick.setWidthF(2.6); p.setPen(thick);
    p.drawRect(QRectF(8, 8, 24, 24));
    QPen thin(QColor(58,63,75,120)); thin.setWidthF(1.0); thin.setStyle(Qt::DashLine); p.setPen(thin);
    for (int y : {14, 19, 24}) p.drawLine(QPointF(12, y), QPointF(28, y));
}); }

QIcon breaksIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(9, 6, 22, 10), 2, 2);
    p.drawRoundedRect(QRectF(9, 24, 22, 10), 2, 2);
    QPen dash(QColor("#E8372A")); dash.setWidthF(2.0); dash.setStyle(Qt::DashLine); p.setPen(dash);
    p.drawLine(QPointF(6, 20), QPointF(34, 20));
}); }

QIcon columnsIcon() { return paintIcon([](QPainter& p) {
    p.drawRect(QRectF(8, 8, 24, 24));
    p.drawLine(QPointF(20, 8), QPointF(20, 32));
}); }

QIcon watermarkIcon() { return paintIcon([](QPainter& p) {
    QFont f("Georgia", 11, QFont::Bold); f.setItalic(true); p.setFont(f);
    p.setPen(QColor(58, 63, 75, 110));
    p.save(); p.translate(20, 22); p.rotate(-30);
    p.drawText(QRectF(-18, -10, 36, 20), Qt::AlignCenter, "A");
    p.restore();
    p.setPen(QPen(QColor(58,63,75,120), 1.2));
    p.drawRect(QRectF(7, 7, 26, 26));
}); }

QIcon endnoteIcon() { return paintIcon([](QPainter& p) {
    QFont f("Segoe UI", 14, QFont::Bold); p.setFont(f); p.setPen(kIconColor);
    p.drawText(QRectF(2, 4, 22, 22), Qt::AlignCenter, "A");
    QFont s("Segoe UI", 9, QFont::Bold); p.setFont(s); p.setPen(QColor("#7C3AED"));
    p.drawText(QRectF(22, 6, 14, 14), Qt::AlignCenter, "e");
    p.setPen(QPen(QColor(58,63,75,120), 1.2));
    for (int y : {29, 33}) p.drawLine(QPointF(7, y), QPointF(20, y));
}); }

QIcon tofIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(6, 8, 12, 10), 1.5, 1.5);
    p.drawEllipse(QPointF(10, 12), 1.6, 1.6);
    p.setPen(QPen(kIconColor, 1.6));
    for (int y : {10, 15, 20, 25, 30}) p.drawLine(QPointF(22, y), QPointF(34, y));
}); }

QIcon crossRefIcon() { return paintIcon([](QPainter& p) {
    p.setPen(QPen(kIconColor, 2.4));
    p.drawArc(QRectF(6, 13, 15, 15), 30 * 16, 180 * 16);
    p.drawArc(QRectF(19, 13, 15, 15), 210 * 16, 180 * 16);
    p.drawLine(QPointF(15, 20), QPointF(25, 20));
}); }

QIcon markEntryIcon() { return paintIcon([](QPainter& p) {
    p.setBrush(QColor(224, 231, 255)); p.setPen(QPen(QColor("#3730A3"), 1.8));
    p.drawRect(QRectF(8, 11, 18, 14));
    p.setPen(QPen(QColor("#3730A3"), 2.0, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(28, 9), QPointF(33, 14)); p.drawLine(QPointF(33, 9), QPointF(28, 14));
}); }

QIcon indexIcon() { return paintIcon([](QPainter& p) {
    p.setPen(QPen(kIconColor, 1.8));
    for (int y : {10, 16, 22, 28}) {
        p.setBrush(kIconColor); p.setPen(Qt::NoPen);
        p.drawText(QRectF(6, y - 6, 8, 12), Qt::AlignLeft | Qt::AlignVCenter,
                   QString(QChar('A' + (y - 10) / 6)));
        p.setPen(QPen(kIconColor, 1.6)); p.drawLine(QPointF(16, y), QPointF(33, y));
    }
}); }

QIcon deleteCommentIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(6, 8, 24, 16), 4, 4);
    p.drawLine(QPointF(12, 24), QPointF(11, 30)); p.drawLine(QPointF(11, 30), QPointF(18, 24));
    p.setPen(QPen(QColor("#E8372A"), 2.2, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(14, 12), QPointF(22, 20)); p.drawLine(QPointF(22, 12), QPointF(14, 20));
}); }

QIcon navArrowIcon(bool next) { return paintIcon([next](QPainter& p) {
    p.setBrush(kIconColor); p.setPen(Qt::NoPen);
    QPolygonF a;
    if (next) a << QPointF(15, 10) << QPointF(15, 30) << QPointF(28, 20);
    else      a << QPointF(25, 10) << QPointF(25, 30) << QPointF(12, 20);
    p.drawPolygon(a);
}); }

QIcon acceptIcon() { return paintIcon([](QPainter& p) {
    p.setPen(QPen(QColor("#16A34A"), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(QPolygonF({ QPointF(9, 21), QPointF(17, 29), QPointF(32, 11) }));
}); }

QIcon rejectIcon() { return paintIcon([](QPainter& p) {
    p.setPen(QPen(QColor("#E8372A"), 3.0, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(11, 11), QPointF(29, 29)); p.drawLine(QPointF(29, 11), QPointF(11, 29));
}); }

QIcon restrictIcon() { return paintIcon([](QPainter& p) {
    p.setBrush(QColor(58,63,75,40)); p.setPen(QPen(kIconColor, 2.0));
    p.drawRoundedRect(QRectF(9, 18, 22, 16), 2, 2);
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(13, 7, 14, 18), 0, 180 * 16);
}); }

QIcon fullScreenIcon() { return paintIcon([](QPainter& p) {
    p.setPen(QPen(kIconColor, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(QPolygonF({ QPointF(8, 14), QPointF(8, 8), QPointF(14, 8) }));
    p.drawPolyline(QPolygonF({ QPointF(26, 8), QPointF(32, 8), QPointF(32, 14) }));
    p.drawPolyline(QPolygonF({ QPointF(32, 26), QPointF(32, 32), QPointF(26, 32) }));
    p.drawPolyline(QPolygonF({ QPointF(14, 32), QPointF(8, 32), QPointF(8, 26) }));
}); }

// Shared by the Home and View copies of the button. It names the shortcut, so
// hovering teaches it *before* Focus Mode is on — once it is, the ribbon is
// behind the curtain and there's nothing left to hover.
static const char* const kFocusModeTip =
    "Focus Mode (Ctrl+Shift+F)\n"
    "Fades everything except the page to black so only your document is left.\n"
    "Press Ctrl+Shift+F to turn it on or off.";

// A lit page inside a darkened aperture — the Focus Mode curtain in miniature.
QIcon focusModeIcon() { return paintIcon([](QPainter& p) {
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(kIconColor));
    QPainterPath outer; outer.addRoundedRect(QRectF(6, 6, 28, 28), 4, 4);
    QPainterPath hole;  hole.addRect(QRectF(15, 12, 10, 16));
    p.drawPath(outer.subtracted(hole));
    p.setPen(QPen(kIconColor, 1.4));
    p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(17.5, 17), QPointF(22.5, 17));
    p.drawLine(QPointF(17.5, 20), QPointF(22.5, 20));
    p.drawLine(QPointF(17.5, 23), QPointF(20.5, 23));
}); }

QIcon readModeIcon() { return paintIcon([](QPainter& p) {
    p.setPen(QPen(kIconColor, 1.8));
    QPainterPath l; l.moveTo(20, 10); l.cubicTo(15, 7, 9, 8, 7, 10); l.lineTo(7, 30);
    l.cubicTo(9, 28, 15, 27, 20, 30);
    QPainterPath r; r.moveTo(20, 10); r.cubicTo(25, 7, 31, 8, 33, 10); r.lineTo(33, 30);
    r.cubicTo(31, 28, 25, 27, 20, 30);
    p.drawPath(l); p.drawPath(r); p.drawLine(QPointF(20, 10), QPointF(20, 30));
}); }

QIcon pageWidthIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(7, 10, 26, 20), 2, 2);
    p.setPen(QPen(kIconColor, 2.0, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(11, 20), QPointF(29, 20));
    p.setBrush(kIconColor); p.setPen(Qt::NoPen);
    QPolygonF l; l << QPointF(11, 16) << QPointF(11, 24) << QPointF(6, 20);
    QPolygonF r; r << QPointF(29, 16) << QPointF(29, 24) << QPointF(34, 20);
    p.drawPolygon(l); p.drawPolygon(r);
}); }

QIcon onePageIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(12, 6, 16, 28), 2, 2);
}); }

QIcon multiPageIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(6, 8, 12, 24), 2, 2);
    p.drawRoundedRect(QRectF(22, 8, 12, 24), 2, 2);
}); }

QIcon eyeProtectIcon() { return paintIcon([](QPainter& p) {
    p.setBrush(QColor(22, 163, 74, 60)); p.setPen(QPen(QColor("#16A34A"), 2.0));
    QPainterPath eye; eye.moveTo(6, 20); eye.quadTo(20, 8, 34, 20); eye.quadTo(20, 32, 6, 20);
    p.drawPath(eye);
    p.setBrush(QColor("#16A34A")); p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(20, 20), 4, 4);
}); }

QIcon exportPdfIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(9, 6, 22, 28), 2, 2);
    p.setBrush(QColor("#E8372A")); p.setPen(Qt::NoPen);
    p.drawRect(QRectF(7, 20, 26, 11));
    QFont f("Segoe UI", 6, QFont::Bold); p.setFont(f); p.setPen(Qt::white);
    p.drawText(QRectF(7, 20, 26, 11), Qt::AlignCenter, "PDF");
}); }

QIcon wordIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(9, 6, 22, 28), 2, 2);
    p.setBrush(QColor("#2563EB")); p.setPen(Qt::NoPen);
    p.drawRect(QRectF(7, 20, 26, 11));
    QFont f("Segoe UI", 7, QFont::Bold); p.setFont(f); p.setPen(Qt::white);
    p.drawText(QRectF(7, 20, 26, 11), Qt::AlignCenter, "W");
}); }

QIcon exportPicIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(6, 9, 28, 22), 2, 2);
    p.setBrush(QColor("#16A34A")); p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(13, 16), 2.6, 2.6);
    QPolygonF m; m << QPointF(8, 29) << QPointF(16, 20) << QPointF(22, 25)
                   << QPointF(27, 20) << QPointF(33, 29);
    p.setPen(QPen(kIconColor, 2.0)); p.setBrush(Qt::NoBrush); p.drawPolyline(m);
}); }

QIcon extractTextIcon() { return paintIcon([](QPainter& p) {
    p.drawRoundedRect(QRectF(9, 6, 22, 28), 2, 2);
    p.setPen(QPen(QColor(58,63,75,150), 1.6));
    for (int y : {13, 18, 23, 28}) p.drawLine(QPointF(13, y), QPointF(27, y));
    p.setPen(QPen(QColor("#2563EB"), 2.0, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(24, 31), QPointF(34, 31));
    p.setBrush(QColor("#2563EB")); p.setPen(Qt::NoPen);
    QPolygonF a; a << QPointF(30, 27) << QPointF(30, 35) << QPointF(35, 31); p.drawPolygon(a);
}); }

// ── Table editing icons ──────────────────────────────────────────────────────
QIcon tblGrid() { // shared base grid
    return paintIcon([](QPainter& p) {
        p.drawRect(QRectF(7, 9, 26, 22));
        p.drawLine(QPointF(7, 17), QPointF(33, 17));
        p.drawLine(QPointF(7, 25), QPointF(33, 25));
        p.drawLine(QPointF(16, 9), QPointF(16, 31));
        p.drawLine(QPointF(25, 9), QPointF(25, 31));
    });
}

QIcon tblInsRow(bool below) { return paintIcon([below](QPainter& p) {
    p.drawRect(QRectF(7, 12, 26, 16));
    p.drawLine(QPointF(7, 20), QPointF(33, 20));
    p.drawLine(QPointF(16, 12), QPointF(16, 28));
    p.drawLine(QPointF(25, 12), QPointF(25, 28));
    p.setPen(QPen(QColor("#16A34A"), 2.2, Qt::SolidLine, Qt::RoundCap));
    const double y = below ? 33 : 7;
    p.drawLine(QPointF(17, y), QPointF(23, y));
    p.drawLine(QPointF(20, y - 3), QPointF(20, y + 3));
}); }

QIcon tblInsCol(bool right) { return paintIcon([right](QPainter& p) {
    p.drawRect(QRectF(12, 9, 16, 22));
    p.drawLine(QPointF(20, 9), QPointF(20, 31));
    p.drawLine(QPointF(12, 17), QPointF(28, 17));
    p.drawLine(QPointF(12, 25), QPointF(28, 25));
    p.setPen(QPen(QColor("#16A34A"), 2.2, Qt::SolidLine, Qt::RoundCap));
    const double x = right ? 33 : 7;
    p.drawLine(QPointF(x, 17), QPointF(x, 23));
    p.drawLine(QPointF(x - 3, 20), QPointF(x + 3, 20));
}); }

QIcon tblDelete() { return paintIcon([](QPainter& p) {
    p.drawRect(QRectF(7, 9, 26, 22));
    p.drawLine(QPointF(7, 20), QPointF(33, 20));
    p.drawLine(QPointF(20, 9), QPointF(20, 31));
    p.setPen(QPen(QColor("#E8372A"), 2.4, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(13, 13), QPointF(27, 27));
    p.drawLine(QPointF(27, 13), QPointF(13, 27));
}); }

QIcon tblMerge() { return paintIcon([](QPainter& p) {
    p.drawRect(QRectF(7, 10, 26, 20));
    p.setPen(QPen(kIconColor, 1.4, Qt::DashLine));
    p.drawLine(QPointF(20, 10), QPointF(20, 30));
    p.setPen(QPen(kIconColor, 2.0, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(kIconColor);
    QPolygonF l; l << QPointF(17, 17) << QPointF(17, 23) << QPointF(12, 20); p.drawPolygon(l);
    QPolygonF r; r << QPointF(23, 17) << QPointF(23, 23) << QPointF(28, 20); p.drawPolygon(r);
}); }

QIcon tblSplit() { return paintIcon([](QPainter& p) {
    p.drawRect(QRectF(7, 10, 26, 20));
    p.drawLine(QPointF(20, 10), QPointF(20, 30));
    p.setBrush(kIconColor); p.setPen(Qt::NoPen);
    QPolygonF l; l << QPointF(15, 17) << QPointF(15, 23) << QPointF(10, 20); p.drawPolygon(l);
    QPolygonF r; r << QPointF(25, 17) << QPointF(25, 23) << QPointF(30, 20); p.drawPolygon(r);
}); }

QIcon tblShade() { return paintIcon([](QPainter& p) {
    p.setBrush(QColor(58,63,75,50)); p.setPen(QPen(kIconColor, 1.6));
    p.drawRect(QRectF(7, 9, 13, 11));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(20, 9, 13, 11));
    p.drawRect(QRectF(7, 20, 13, 11));
    p.drawRect(QRectF(20, 20, 13, 11));
}); }

QIcon tblBorders() { return paintIcon([](QPainter& p) {
    QPen thick(kIconColor); thick.setWidthF(2.4); p.setPen(thick);
    p.drawRect(QRectF(7, 9, 26, 22));
    QPen thin(kIconColor); thin.setWidthF(1.0); p.setPen(thin);
    p.drawLine(QPointF(7, 20), QPointF(33, 20));
    p.drawLine(QPointF(20, 9), QPointF(20, 31));
}); }

QIcon tblHeader() { return paintIcon([](QPainter& p) {
    p.setBrush(QColor("#2C3140")); p.setPen(QPen(kIconColor, 1.4));
    p.drawRect(QRectF(7, 9, 26, 7));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(7, 16, 26, 15));
    p.drawLine(QPointF(7, 23), QPointF(33, 23));
    p.drawLine(QPointF(20, 16), QPointF(20, 31));
}); }

QIcon tblStyle(int variant) { return paintIcon([variant](QPainter& p) {
    p.setPen(QPen(kIconColor, 1.4));
    if (variant == 0) {            // grid
        p.drawRect(QRectF(7, 9, 26, 22));
        for (int y : {16, 23}) p.drawLine(QPointF(7, y), QPointF(33, y));
        for (int x : {16, 25}) p.drawLine(QPointF(x, 9), QPointF(x, 31));
    } else if (variant == 1) {     // header
        p.setBrush(QColor("#2C3140")); p.drawRect(QRectF(7, 9, 26, 7));
        p.setBrush(Qt::NoBrush); p.drawRect(QRectF(7, 16, 26, 15));
        p.drawLine(QPointF(7, 23), QPointF(33, 23));
    } else {                        // banded
        p.drawRect(QRectF(7, 9, 26, 22));
        p.setBrush(QColor("#E5E7EB")); p.setPen(Qt::NoPen);
        p.drawRect(QRectF(8, 16, 24, 7));
        p.setPen(QPen(kIconColor, 1.4)); p.setBrush(Qt::NoBrush);
        for (int y : {16, 23}) p.drawLine(QPointF(7, y), QPointF(33, y));
    }
}); }

QIcon tblAlign(int mode) { return paintIcon([mode](QPainter& p) {
    p.drawRect(QRectF(7, 9, 26, 22));
    p.setPen(QPen(kIconColor, 1.8, Qt::SolidLine, Qt::RoundCap));
    int x0 = 11, x1 = 22;
    if (mode == 1) { x0 = 14; x1 = 26; }
    else if (mode == 2) { x0 = 18; x1 = 29; }
    for (int y : {15, 20, 25}) p.drawLine(QPointF(x0, y), QPointF(x1, y));
}); }

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────
WriterRibbon::WriterRibbon(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("writerRibbon");

    m_styles = new WriterStyleManager();
    m_ai     = new WriterAi(this);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Tab strip ───────────────────────────────────────────────────────────
    auto* tabRow = new QWidget(this);
    tabRow->setObjectName("ribbonTabRow");
    tabRow->setFixedHeight(30);
    auto* tabLayout = new QHBoxLayout(tabRow);
    tabLayout->setContentsMargins(10, 0, 10, 0);
    tabLayout->setSpacing(2);

    m_tabGroup = new QButtonGroup(this);
    m_tabGroup->setExclusive(true);

    const QStringList tabNames = {
        "Home", "Insert", "Page Layout", "References", "Review", "View", "Tools", "Table"
    };
    for (int i = 0; i < tabNames.size(); ++i) {
        auto* btn = makeTabButton(tabNames[i]);
        btn->installEventFilter(this);          // double-click → collapse
        m_tabGroup->addButton(btn, i);
        tabLayout->addWidget(btn);
    }
    tabLayout->addStretch();

    // Collapse control. Double-clicking a tab already collapsed the ribbon, but
    // nothing on screen said so, and testers asked for the ribbon to stop taking
    // a quarter of the window. This is the Word-style chevron at the right end.
    m_collapseBtn = new QToolButton(tabRow);
    m_collapseBtn->setObjectName("ribbonCollapseBtn");
    m_collapseBtn->setCursor(Qt::PointingHandCursor);
    m_collapseBtn->setAutoRaise(true);
    m_collapseBtn->setText(QStringLiteral("⌃"));   // up chevron
    m_collapseBtn->setToolTip("Collapse the ribbon  (or double-click a tab)");
    connect(m_collapseBtn, &QToolButton::clicked, this, [this] {
        setRibbonCollapsed(!m_collapsed);
    });
    tabLayout->addWidget(m_collapseBtn);

    m_tabGroup->button(0)->setChecked(true);

    // The "Table" tab is contextual — hidden until the cursor is inside a table.
    m_tableTabBtn = qobject_cast<QToolButton*>(m_tabGroup->button(7));
    if (m_tableTabBtn) {
        m_tableTabBtn->setObjectName("ribbonTableTabBtn");
        m_tableTabBtn->setVisible(false);
    }

    // ── Stacked content ─────────────────────────────────────────────────────
    m_stack = new QStackedWidget(this);
    m_stack->setObjectName("ribbonStack");
    // 100px left two-line button captions ("Text/Direction", "Page/Color")
    // colliding with the group caption underneath, which is what testers saw as
    // labels bleeding and cut off along the bottom of the ribbon.
    m_stack->setFixedHeight(110);
    // Only the Home tab is built eagerly; the other seven build on first click.
    // This cuts document-open time sharply — the full ribbon is by far the most
    // expensive part of constructing a Writer window.
    m_stack->addWidget(buildHomeTab());        // 0 — Home
    for (int i = 1; i <= 7; ++i)
        m_stack->addWidget(new QWidget(this)); // placeholders, replaced on demand
    m_tabBuilt = { true, false, false, false, false, false, false, false };

    connect(m_tabGroup, &QButtonGroup::idClicked, this, [this](int id) {
        ensureTabBuilt(id);
        m_stack->setCurrentIndex(id);
        if (m_collapsed) {                       // a click re-opens a collapsed ribbon
            m_collapsed = false;
            m_stack->setVisible(true);
        }
    });

    root->addWidget(tabRow);
    root->addWidget(m_stack);

    applyStyles();
    connect(&ThemeManager::instance(), &ThemeManager::modeChanged,
            this, [this](ThemeMode) { applyStyles(); });
}

// ─────────────────────────────────────────────────────────────────────────────
// Editor wiring
// ─────────────────────────────────────────────────────────────────────────────
void WriterRibbon::attachEditor(QTextEdit* editor) {
    if (m_editor == editor) return;
    m_editor = editor;

    // Tier 5 — collaboration engine binds to the editor.
    m_collab = new WriterCollab(m_editor, this);
    connect(m_collab, &WriterCollab::statusChanged, this, [this](const QString& s){
        if (m_collabStatus) m_collabStatus->setText(s);
    });

    connect(m_editor, &QTextEdit::cursorPositionChanged,
            this, &WriterRibbon::syncToCurrentFormat);
    connect(m_editor, &QTextEdit::currentCharFormatChanged,
            this, [this](const QTextCharFormat&) { syncToCurrentFormat(); });

    // Format painter: the paint happens on mouse release, handled in
    // eventFilter(). It used to hang off selectionChanged, which fires on
    // every mouse-move of a drag — so it painted half-finished selections,
    // and mergeCharFormat() re-emitted selectionChanged while the flag was
    // still armed, recursing until the stack ran out.
    // Mouse events land on the viewport, key events on the editor itself.
    m_editor->viewport()->installEventFilter(this);
    m_editor->installEventFilter(this);

    // ── Word/WPS-standard keyboard shortcuts ────────────────────────────────
    // Scoped to the editor (WidgetWithChildrenShortcut) so multiple Writer tabs
    // in one window never produce ambiguous-shortcut conflicts.
    auto addSc = [this](const QKeySequence& seq, std::function<void()> fn) {
        auto* sc = new QShortcut(seq, m_editor);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, this, std::move(fn));
    };

    // Find / Replace / Print
    addSc(QKeySequence::Find,    [this]{ openFindReplace(); });
    addSc(QKeySequence::Replace, [this]{ openFindReplace(); });
    addSc(QKeySequence::Print,   [this]{ printDocument(); });

    // Character formatting (the buttons already own the merge logic and stay
    // visually in sync via their toggled/clicked handlers).
    addSc(QKeySequence::Bold,      [this]{ if (m_btnBold)      m_btnBold->click(); });
    addSc(QKeySequence::Italic,    [this]{ if (m_btnItalic)    m_btnItalic->click(); });
    addSc(QKeySequence::Underline, [this]{ if (m_btnUnderline) m_btnUnderline->click(); });
    addSc(QKeySequence("Ctrl+="),       [this]{ if (m_btnSub)   m_btnSub->click(); });
    addSc(QKeySequence("Ctrl+Shift+="), [this]{ if (m_btnSuper) m_btnSuper->click(); });
    addSc(QKeySequence("Ctrl+Shift+."), [this]{ adjustFontSize(+1); });   // Ctrl+Shift+>
    addSc(QKeySequence("Ctrl+Shift+,"), [this]{ adjustFontSize(-1); });   // Ctrl+Shift+<
    addSc(QKeySequence("Ctrl+]"),       [this]{ adjustFontSize(+1); });
    addSc(QKeySequence("Ctrl+["),       [this]{ adjustFontSize(-1); });
    addSc(QKeySequence("Ctrl+Space"),   [this]{ clearFormatting(); });
    addSc(QKeySequence("Shift+F3"),     [this]{
        applyChangeCase(m_caseCycle);                 // 0 UPPER → 1 lower → 2 Title
        m_caseCycle = (m_caseCycle + 1) % 3;
    });

    // Paragraph formatting
    addSc(QKeySequence("Ctrl+L"), [this]{ setAlignment(Qt::AlignLeft); });
    addSc(QKeySequence("Ctrl+E"), [this]{ setAlignment(Qt::AlignHCenter); });
    addSc(QKeySequence("Ctrl+R"), [this]{ setAlignment(Qt::AlignRight); });
    addSc(QKeySequence("Ctrl+J"), [this]{ setAlignment(Qt::AlignJustify); });
    addSc(QKeySequence("Ctrl+1"), [this]{ applyLineSpacing(1.0); });
    addSc(QKeySequence("Ctrl+5"), [this]{ applyLineSpacing(1.5); });
    addSc(QKeySequence("Ctrl+2"), [this]{ applyLineSpacing(2.0); });
    addSc(QKeySequence("Ctrl+M"),        [this]{ changeIndent(+1); });
    addSc(QKeySequence("Ctrl+Shift+M"),  [this]{ changeIndent(-1); });
    addSc(QKeySequence("Ctrl+Shift+L"),  [this]{ applyBullets(QTextListFormat::ListDisc); });

    // Styles
    addSc(QKeySequence("Ctrl+Shift+N"), [this]{ applyParagraphStyle(WriterStyle::Normal); });
    addSc(QKeySequence("Ctrl+Alt+1"),   [this]{ applyParagraphStyle(WriterStyle::Heading1); });
    addSc(QKeySequence("Ctrl+Alt+2"),   [this]{ applyParagraphStyle(WriterStyle::Heading2); });
    addSc(QKeySequence("Ctrl+Alt+3"),   [this]{ applyParagraphStyle(WriterStyle::Heading3); });

    // Insert / tools
    addSc(QKeySequence("Ctrl+K"),      [this]{ insertHyperlink(); });
    addSc(QKeySequence("Ctrl+Return"), [this]{ insertPageBreak(); });
    addSc(QKeySequence("F7"),          [this]{ showSpellingDialog(); });

    syncToCurrentFormat();
}

// ─────────────────────────────────────────────────────────────────────────────
// Home tab
// ─────────────────────────────────────────────────────────────────────────────
QWidget* WriterRibbon::buildHomeTab() {
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("ribbonScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto* tab = new QWidget(scroll);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2);
    layout->setSpacing(0);

    // ══ GROUP 1 — Clipboard ════════════════════════════════════════════════
    m_btnPainter = makeBigBtn(painterIcon(), "Format\nPainter",
                              "Copy formatting, then select target text to apply", true);
    connect(m_btnPainter, &QToolButton::toggled, this, &WriterRibbon::toggleFormatPainter);

    auto* btnPaste = makeBigBtn(pasteIcon(), "Paste", "Paste (Ctrl+V)");
    btnPaste->setPopupMode(QToolButton::MenuButtonPopup);
    connect(btnPaste, &QToolButton::clicked, this, [this] { if (m_editor) m_editor->paste(); });
    auto* pasteMenu = new QMenu(btnPaste);
    pasteMenu->addAction(pasteIcon(), "Paste", this, [this] { if (m_editor) m_editor->paste(); });
    pasteMenu->addAction(pasteIcon(), "Paste Special…", this, [this] { pasteAsPlainText(); });
    pasteMenu->addAction(plainTextIcon(), "Paste as Plain Text", this, [this] { pasteAsPlainText(); });
    btnPaste->setMenu(pasteMenu);

    auto* btnCut  = makeIconBtn(cutIcon(),  "Cut (Ctrl+X)");
    auto* btnCopy = makeIconBtn(copyIcon(), "Copy (Ctrl+C)");
    connect(btnCut,  &QToolButton::clicked, this, [this] { if (m_editor) m_editor->cut(); });
    connect(btnCopy, &QToolButton::clicked, this, [this] { if (m_editor) m_editor->copy(); });
    auto* cutCopyCol = new QWidget(tab);
    auto* ccl = new QVBoxLayout(cutCopyCol);
    ccl->setContentsMargins(0, 0, 0, 0); ccl->setSpacing(2);
    ccl->addWidget(btnCut); ccl->addWidget(btnCopy);

    layout->addWidget(makeGroup("Clipboard", { m_btnPainter, btnPaste, cutCopyCol }));
    layout->addWidget(makeSeparator());

    // ══ GROUP 2 — Font ═════════════════════════════════════════════════════
    m_fontCombo = new QComboBox(tab);
    m_fontCombo->setObjectName("ribbonCombo");
    m_fontCombo->setEditable(true);
    m_fontCombo->setInsertPolicy(QComboBox::NoInsert);
    m_fontCombo->setFixedWidth(150);
    m_fontCombo->setToolTip("Font family");
    // Render each row in its own typeface, but lazily (see FontPreviewDelegate)
    // so the editor opens instantly instead of loading every installed font.
    // Render each row in its own typeface, lazily (see FontPreviewDelegate).
    // The combo must NOT auto-size to its contents, otherwise it measures every
    // row up front — which on font-heavy machines means loading hundreds of
    // fonts and a multi-second stall before the editor even appears.
    m_fontCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_fontCombo->setMinimumContentsLength(14);
    m_fontCombo->setItemDelegate(new FontPreviewDelegate(m_fontCombo));
    if (auto* v = qobject_cast<QListView*>(m_fontCombo->view())) v->setUniformItemSizes(true);
    {
        const QFontDatabase fdb;
        m_fontCombo->addItems(fdb.families());
        const int idx = m_fontCombo->findText("Segoe UI");
        m_fontCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    if (m_fontCombo->completer())
        m_fontCombo->completer()->setCompletionMode(QCompleter::PopupCompletion);
    // The popup is not bound to the 150px combo: font names are long, and at
    // combo width they were unreadable.
    if (auto* v = m_fontCombo->view()) v->setMinimumWidth(300);

    // Apply on commit, never mid-typing. This was wired to currentTextChanged,
    // which fires on every keystroke, and mergeFormatOnSelection() hands focus
    // back to the document — so typing a font name put the first letter in the
    // box and every letter after it into the document.
    auto applyFontFamily = [this] {
        if (m_syncing) return;
        const QString fam = m_fontCombo->currentText().trimmed();
        if (fam.isEmpty()) return;
        QTextCharFormat fmt; fmt.setFontFamilies({fam});
        mergeFormatOnSelection(fmt);
    };
    connect(m_fontCombo, &QComboBox::activated, this,
            [applyFontFamily](int) { applyFontFamily(); });
    if (auto* le = m_fontCombo->lineEdit())
        connect(le, &QLineEdit::returnPressed, this, applyFontFamily);

    m_sizeCombo = new QComboBox(tab);
    m_sizeCombo->setObjectName("ribbonCombo");
    m_sizeCombo->setEditable(true);
    m_sizeCombo->setFixedWidth(54);
    m_sizeCombo->setToolTip("Font size");
    for (int s : {8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 28, 32, 36, 48, 60, 72})
        m_sizeCombo->addItem(QString::number(s));
    m_sizeCombo->setCurrentText("12");
    // Commit-only, same reasoning as the font box. Per-keystroke application
    // also meant typing "14" briefly applied size 1 to the selection.
    auto applyFontSize = [this] {
        if (m_syncing) return;
        bool ok = false;
        const int pt = m_sizeCombo->currentText().trimmed().toInt(&ok);
        if (!ok || pt <= 0 || pt > 409) return;   // 409pt is Word's ceiling
        QTextCharFormat fmt; fmt.setFontPointSize(pt);
        mergeFormatOnSelection(fmt);
    };
    connect(m_sizeCombo, &QComboBox::activated, this,
            [applyFontSize](int) { applyFontSize(); });
    if (auto* le = m_sizeCombo->lineEdit())
        connect(le, &QLineEdit::returnPressed, this, applyFontSize);

    auto* btnGrow   = makeIconBtn(growIcon(true),  "Increase font size");
    auto* btnShrink = makeIconBtn(growIcon(false), "Decrease font size");
    connect(btnGrow,   &QToolButton::clicked, this, [this] { adjustFontSize(+1); });
    connect(btnShrink, &QToolButton::clicked, this, [this] { adjustFontSize(-1); });

    auto* btnCase = makeIconBtn(caseIcon(), "Change Case");
    btnCase->setPopupMode(QToolButton::InstantPopup);
    auto* caseMenu = new QMenu(btnCase);
    caseMenu->addAction(caseIcon(), "UPPERCASE",      this, [this] { applyChangeCase(0); });
    caseMenu->addAction(caseIcon(), "lowercase",      this, [this] { applyChangeCase(1); });
    caseMenu->addAction(caseIcon(), "Title Case",     this, [this] { applyChangeCase(2); });
    caseMenu->addAction(caseIcon(), "Sentence case",  this, [this] { applyChangeCase(3); });
    caseMenu->addAction(caseIcon(), "tOGGLE cASE",    this, [this] { applyChangeCase(4); });
    btnCase->setMenu(caseMenu);

    auto* btnClear = makeIconBtn(clearFmtIcon(), "Clear all formatting");
    connect(btnClear, &QToolButton::clicked, this, &WriterRibbon::clearFormatting);

    // Row of font-name controls + the B/I/U etc. cluster, stacked in two rows.
    auto* fontTop = new QWidget(tab);
    auto* ftl = new QHBoxLayout(fontTop);
    ftl->setContentsMargins(0, 0, 0, 0); ftl->setSpacing(3);
    ftl->addWidget(m_fontCombo);
    ftl->addWidget(m_sizeCombo);
    ftl->addWidget(btnGrow);
    ftl->addWidget(btnShrink);
    ftl->addWidget(btnCase);
    ftl->addWidget(btnClear);

    m_btnBold      = makeToolBtn("B", "Bold (Ctrl+B)", true);
    m_btnItalic    = makeToolBtn("I", "Italic (Ctrl+I)", true);
    m_btnUnderline = makeToolBtn("U", "Underline (Ctrl+U)", true);
    m_btnStrike    = makeToolBtn("S", "Strikethrough", true);
    m_btnBold->setFont(QFont("Segoe UI", 11, QFont::Bold));
    { QFont f("Segoe UI", 11); f.setItalic(true);    m_btnItalic->setFont(f); }
    { QFont f("Segoe UI", 11); f.setUnderline(true); m_btnUnderline->setFont(f); }
    { QFont f("Segoe UI", 11); f.setStrikeOut(true); m_btnStrike->setFont(f); }

    connect(m_btnBold, &QToolButton::toggled, this, [this](bool c) {
        if (m_syncing) return;
        QTextCharFormat fmt; fmt.setFontWeight(c ? QFont::Bold : QFont::Normal);
        mergeFormatOnSelection(fmt);
    });
    connect(m_btnItalic, &QToolButton::toggled, this, [this](bool c) {
        if (m_syncing) return;
        QTextCharFormat fmt; fmt.setFontItalic(c); mergeFormatOnSelection(fmt);
    });
    connect(m_btnStrike, &QToolButton::toggled, this, [this](bool c) {
        if (m_syncing) return;
        QTextCharFormat fmt; fmt.setFontStrikeOut(c); mergeFormatOnSelection(fmt);
    });

    // Underline button doubles as a dropdown for underline styles.
    m_btnUnderline->setPopupMode(QToolButton::MenuButtonPopup);
    connect(m_btnUnderline, &QToolButton::clicked, this, [this] {
        if (m_syncing) return;
        const bool on = m_btnUnderline->isChecked();
        QTextCharFormat fmt; fmt.setFontUnderline(on);
        if (on) fmt.setUnderlineStyle(QTextCharFormat::SingleUnderline);
        mergeFormatOnSelection(fmt);
    });
    auto* ulMenu = new QMenu(m_btnUnderline);
    ulMenu->addAction(ulStyleIcon(QTextCharFormat::SingleUnderline), "Single",   this, [this] { applyUnderlineStyle(QTextCharFormat::SingleUnderline); });
    ulMenu->addAction(ulStyleIcon(QTextCharFormat::DotLine),         "Dotted",   this, [this] { applyUnderlineStyle(QTextCharFormat::DotLine); });
    ulMenu->addAction(ulStyleIcon(QTextCharFormat::DashUnderline),   "Dashed",   this, [this] { applyUnderlineStyle(QTextCharFormat::DashUnderline); });
    ulMenu->addAction(ulStyleIcon(QTextCharFormat::DashDotLine),     "Dash-Dot", this, [this] { applyUnderlineStyle(QTextCharFormat::DashDotLine); });
    ulMenu->addAction(ulStyleIcon(QTextCharFormat::WaveUnderline),   "Wavy",     this, [this] { applyUnderlineStyle(QTextCharFormat::WaveUnderline); });
    m_btnUnderline->setMenu(ulMenu);

    m_btnSub   = makeToolBtn("x₂", "Subscript", true);
    m_btnSuper = makeToolBtn("x²", "Superscript", true);
    connect(m_btnSub,   &QToolButton::toggled, this, [this](bool c) {
        if (m_syncing) return; toggleVerticalAlign(c ? 2 : 0);
    });
    connect(m_btnSuper, &QToolButton::toggled, this, [this](bool c) {
        if (m_syncing) return; toggleVerticalAlign(c ? 1 : 0);
    });

    // Font colour (split: apply / palette)
    m_btnFontColor = makeIconBtn(colorBarIcon("A", m_fontColor, false), "Font Colour");
    m_btnFontColor->setPopupMode(QToolButton::MenuButtonPopup);
    connect(m_btnFontColor, &QToolButton::clicked, this, [this] {
        QTextCharFormat fmt; fmt.setForeground(m_fontColor); mergeFormatOnSelection(fmt);
    });
    {
        auto* menu = new QMenu(m_btnFontColor);
        auto* grid = new QWidget(menu);
        auto* gl = new QGridLayout(grid);
        gl->setContentsMargins(8, 8, 8, 8); gl->setSpacing(4);
        const char* palette[] = {
            "#000000","#1C1E26","#2C3140","#6B7280","#9CA3AF","#FFFFFF",
            "#E8372A","#EA580C","#F59E0B","#16A34A","#2563EB","#7C3AED",
            "#B91C1C","#92400E","#065F46","#1E3A8A","#581C87","#374151" };
        int i = 0;
        for (const char* hex : palette) {
            auto* sw = new QToolButton(grid);
            sw->setFixedSize(22, 22);
            sw->setCursor(Qt::PointingHandCursor);
            sw->setToolTip(hex);
            sw->setStyleSheet(QString("QToolButton{background:%1;border:1px solid #C6CAD3;border-radius:3px;}"
                                      "QToolButton:hover{border:2px solid #6D5BE8;}").arg(hex));
            const QColor c(hex);
            connect(sw, &QToolButton::clicked, this, [this, c, menu] {
                m_fontColor = c;
                m_btnFontColor->setIcon(colorBarIcon("A", c, false));
                QTextCharFormat fmt; fmt.setForeground(c); mergeFormatOnSelection(fmt);
                menu->hide();
            });
            gl->addWidget(sw, i / 6, i % 6); ++i;
        }
        auto* wa = new QWidgetAction(menu); wa->setDefaultWidget(grid);
        menu->addAction(wa);
        menu->addAction(colorBarIcon("A", m_fontColor, false), "More Colours…", this, [this] {
            const QColor c = QColorDialog::getColor(m_fontColor, this, "Font Colour");
            if (!c.isValid()) return;
            m_fontColor = c;
            m_btnFontColor->setIcon(colorBarIcon("A", c, false));
            QTextCharFormat fmt; fmt.setForeground(c); mergeFormatOnSelection(fmt);
        });
        m_btnFontColor->setMenu(menu);
    }

    // Highlight colour
    m_btnHighlight = makeIconBtn(colorBarIcon({}, m_highlightColor, true), "Text Highlight Colour");
    m_btnHighlight->setPopupMode(QToolButton::MenuButtonPopup);
    connect(m_btnHighlight, &QToolButton::clicked, this, [this] {
        QTextCharFormat fmt; fmt.setBackground(m_highlightColor); mergeFormatOnSelection(fmt);
    });
    {
        auto* menu = new QMenu(m_btnHighlight);
        auto* grid = new QWidget(menu);
        auto* gl = new QGridLayout(grid);
        gl->setContentsMargins(8, 8, 8, 8); gl->setSpacing(4);
        const char* palette[] = {
            "#FFF27A","#FCE94F","#A7F3D0","#BAE6FD","#FBCFE8","#FDBA74",
            "#86EFAC","#93C5FD","#D8B4FE","#FCA5A5","#E5E7EB","#FFFFFF" };
        int i = 0;
        for (const char* hex : palette) {
            auto* sw = new QToolButton(grid);
            sw->setFixedSize(22, 22);
            sw->setCursor(Qt::PointingHandCursor);
            sw->setToolTip(hex);
            sw->setStyleSheet(QString("QToolButton{background:%1;border:1px solid #C6CAD3;border-radius:3px;}"
                                      "QToolButton:hover{border:2px solid #6D5BE8;}").arg(hex));
            const QColor c(hex);
            connect(sw, &QToolButton::clicked, this, [this, c, menu] {
                m_highlightColor = c;
                m_btnHighlight->setIcon(colorBarIcon({}, c, true));
                QTextCharFormat fmt; fmt.setBackground(c); mergeFormatOnSelection(fmt);
                menu->hide();
            });
            gl->addWidget(sw, i / 6, i % 6); ++i;
        }
        auto* wa = new QWidgetAction(menu); wa->setDefaultWidget(grid);
        menu->addAction(wa);
        menu->addAction(colorBarIcon({}, Qt::white, true), "No Colour", this, [this] {
            QTextCharFormat fmt; fmt.setBackground(Qt::transparent); mergeFormatOnSelection(fmt);
        });
        m_btnHighlight->setMenu(menu);
    }

    auto* fontBottom = new QWidget(tab);
    auto* fbl = new QHBoxLayout(fontBottom);
    fbl->setContentsMargins(0, 0, 0, 0); fbl->setSpacing(2);
    fbl->addWidget(m_btnBold);
    fbl->addWidget(m_btnItalic);
    fbl->addWidget(m_btnUnderline);
    fbl->addWidget(m_btnStrike);
    fbl->addWidget(m_btnSub);
    fbl->addWidget(m_btnSuper);
    fbl->addWidget(m_btnFontColor);
    fbl->addWidget(m_btnHighlight);

    auto* fontCol = new QWidget(tab);
    auto* fcl = new QVBoxLayout(fontCol);
    fcl->setContentsMargins(0, 0, 0, 0); fcl->setSpacing(3);
    fcl->addWidget(fontTop);
    fcl->addWidget(fontBottom);

    layout->addWidget(makeGroup("Font", { fontCol }));
    layout->addWidget(makeSeparator());

    // ══ GROUP 3 — Paragraph ════════════════════════════════════════════════
    auto* btnBullets = makeIconBtn(bulletIcon(false), "Bullets");
    btnBullets->setPopupMode(QToolButton::MenuButtonPopup);
    connect(btnBullets, &QToolButton::clicked, this, [this] { applyBullets(QTextListFormat::ListDisc); });
    {
        auto* m = new QMenu(btnBullets);
        m->addAction(listStyleIcon(QTextListFormat::ListDisc),   "Disc",   this, [this] { applyBullets(QTextListFormat::ListDisc); });
        m->addAction(listStyleIcon(QTextListFormat::ListCircle), "Circle", this, [this] { applyBullets(QTextListFormat::ListCircle); });
        m->addAction(listStyleIcon(QTextListFormat::ListSquare), "Square", this, [this] { applyBullets(QTextListFormat::ListSquare); });
        m->addSeparator();
        m->addAction("Multilevel List (•  ◦  ▪)", this, [this] { applyMultilevelList(false); });
        m->addAction("Define New Bullet…", this, [this] { customizeList(); });
        btnBullets->setMenu(m);
    }

    auto* btnNumber = makeIconBtn(bulletIcon(true), "Numbering");
    btnNumber->setPopupMode(QToolButton::MenuButtonPopup);
    connect(btnNumber, &QToolButton::clicked, this, [this] { applyNumbering(QTextListFormat::ListDecimal); });
    {
        auto* m = new QMenu(btnNumber);
        m->addAction(listStyleIcon(QTextListFormat::ListDecimal),    "1. 2. 3.",   this, [this] { applyNumbering(QTextListFormat::ListDecimal); });
        m->addAction(listStyleIcon(QTextListFormat::ListLowerAlpha), "a. b. c.",   this, [this] { applyNumbering(QTextListFormat::ListLowerAlpha); });
        m->addAction(listStyleIcon(QTextListFormat::ListUpperAlpha), "A. B. C.",   this, [this] { applyNumbering(QTextListFormat::ListUpperAlpha); });
        m->addAction(listStyleIcon(QTextListFormat::ListLowerRoman), "i. ii. iii.", this, [this] { applyNumbering(QTextListFormat::ListLowerRoman); });
        m->addAction(listStyleIcon(QTextListFormat::ListUpperRoman), "I. II. III.", this, [this] { applyNumbering(QTextListFormat::ListUpperRoman); });
        m->addSeparator();
        m->addAction("Multilevel List (1.  a.  i.)", this, [this] { applyMultilevelList(true); });
        m->addAction("Restart Numbering at 1",  this, [this] { restartNumbering(); });
        m->addAction("Continue Previous List",  this, [this] { continueNumbering(); });
        m->addAction("Define Number Format…",   this, [this] { customizeList(); });
        btnNumber->setMenu(m);
    }

    auto* btnIndentDec = makeIconBtn(indentIcon(false), "Decrease indent");
    auto* btnIndentInc = makeIconBtn(indentIcon(true),  "Increase indent");
    connect(btnIndentDec, &QToolButton::clicked, this, [this] { changeIndent(-1); });
    connect(btnIndentInc, &QToolButton::clicked, this, [this] { changeIndent(+1); });

    auto* btnSort = makeIconBtn(sortIcon(), "Sort paragraphs");
    btnSort->setPopupMode(QToolButton::MenuButtonPopup);
    connect(btnSort, &QToolButton::clicked, this, [this] { sortParagraphs(true); });
    {
        auto* m = new QMenu(btnSort);
        m->addAction(sortIcon(), "Sort A → Z", this, [this] { sortParagraphs(true); });
        m->addAction(sortIcon(), "Sort Z → A", this, [this] { sortParagraphs(false); });
        btnSort->setMenu(m);
    }

    m_btnMarks = makeIconBtn(marksIcon(), "Show/Hide formatting marks", true);
    connect(m_btnMarks, &QToolButton::toggled, this, [this](bool c) { toggleFormattingMarks(c); });

    m_btnAlignLeft    = makeIconBtn(alignIcon(0), "Align left", true);
    m_btnAlignCenter  = makeIconBtn(alignIcon(1), "Center", true);
    m_btnAlignRight   = makeIconBtn(alignIcon(2), "Align right", true);
    m_btnAlignJustify = makeIconBtn(alignIcon(3), "Justify", true);
    connect(m_btnAlignLeft,    &QToolButton::clicked, this, [this] { setAlignment(Qt::AlignLeft | Qt::AlignAbsolute); });
    connect(m_btnAlignCenter,  &QToolButton::clicked, this, [this] { setAlignment(Qt::AlignHCenter); });
    connect(m_btnAlignRight,   &QToolButton::clicked, this, [this] { setAlignment(Qt::AlignRight | Qt::AlignAbsolute); });
    connect(m_btnAlignJustify, &QToolButton::clicked, this, [this] { setAlignment(Qt::AlignJustify); });
    // Exclusive: selecting one alignment clears the others (fixes the glitch
    // where the previously-active alignment button stayed highlighted).
    auto* alignGroup = new QButtonGroup(this);
    alignGroup->setExclusive(true);
    alignGroup->addButton(m_btnAlignLeft);
    alignGroup->addButton(m_btnAlignCenter);
    alignGroup->addButton(m_btnAlignRight);
    alignGroup->addButton(m_btnAlignJustify);

    auto* btnSpacing = makeIconBtn(lineSpacingIcon(), "Line spacing");
    btnSpacing->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(btnSpacing);
        for (double v : {1.0, 1.15, 1.5, 2.0})
            m->addAction(lineSpacingIcon(), QString::number(v), this, [this, v] { applyLineSpacing(v); });
        m->addSeparator();
        m->addAction(lineSpacingIcon(), "Custom…", this, [this] {
            bool ok = false;
            const double v = QInputDialog::getDouble(this, "Line Spacing",
                                "Multiple:", 1.0, 0.5, 5.0, 2, &ok);
            if (ok) applyLineSpacing(v);
        });
        btnSpacing->setMenu(m);
    }

    auto* btnShading = makeIconBtn(shadingIcon(), "Shading (paragraph background)");
    btnShading->setPopupMode(QToolButton::InstantPopup);
    {
        auto* menu = new QMenu(btnShading);
        auto* grid = new QWidget(menu);
        auto* gl = new QGridLayout(grid);
        gl->setContentsMargins(8, 8, 8, 8); gl->setSpacing(4);
        const char* palette[] = {
            "#F3F4F6","#FEF3C7","#DCFCE7","#DBEAFE","#FCE7F3","#FFE4E6",
            "#E5E7EB","#FDE68A","#BBF7D0","#BFDBFE","#F5D0FE","#FECACA" };
        int i = 0;
        for (const char* hex : palette) {
            auto* sw = new QToolButton(grid);
            sw->setFixedSize(22, 22); sw->setCursor(Qt::PointingHandCursor); sw->setToolTip(hex);
            sw->setStyleSheet(QString("QToolButton{background:%1;border:1px solid #C6CAD3;border-radius:3px;}"
                                      "QToolButton:hover{border:2px solid #6D5BE8;}").arg(hex));
            const QColor c(hex);
            connect(sw, &QToolButton::clicked, this, [this, c, menu] { applyShading(c); menu->hide(); });
            gl->addWidget(sw, i / 6, i % 6); ++i;
        }
        auto* wa = new QWidgetAction(menu); wa->setDefaultWidget(grid);
        menu->addAction(wa);
        menu->addAction(shadingIcon(), "No Shading", this, [this] { applyShading(Qt::transparent); });
        btnShading->setMenu(menu);
    }

    auto* btnBorders = makeIconBtn(bordersIcon(), "Borders");
    btnBorders->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(btnBorders);
        m->addAction(bordersIcon(), "Bottom Border",   this, [this] { applyBorder(1); });
        m->addAction(bordersIcon(), "Top Border",      this, [this] { applyBorder(2); });
        m->addAction(insHrIcon(),   "Horizontal Line", this, [this] { applyBorder(3); });
        btnBorders->setMenu(m);
    }

    auto* paraTop = new QWidget(tab);
    auto* ptl = new QHBoxLayout(paraTop);
    ptl->setContentsMargins(0, 0, 0, 0); ptl->setSpacing(2);
    ptl->addWidget(btnBullets); ptl->addWidget(btnNumber);
    ptl->addWidget(btnIndentDec); ptl->addWidget(btnIndentInc);
    ptl->addWidget(btnSort); ptl->addWidget(m_btnMarks);

    auto* paraBottom = new QWidget(tab);
    auto* pbl = new QHBoxLayout(paraBottom);
    pbl->setContentsMargins(0, 0, 0, 0); pbl->setSpacing(2);
    pbl->addWidget(m_btnAlignLeft); pbl->addWidget(m_btnAlignCenter);
    pbl->addWidget(m_btnAlignRight); pbl->addWidget(m_btnAlignJustify);
    pbl->addWidget(btnSpacing); pbl->addWidget(btnShading); pbl->addWidget(btnBorders);

    auto* paraCol = new QWidget(tab);
    auto* pcl = new QVBoxLayout(paraCol);
    pcl->setContentsMargins(0, 0, 0, 0); pcl->setSpacing(3);
    pcl->addWidget(paraTop); pcl->addWidget(paraBottom);

    layout->addWidget(makeGroup("Paragraph", { paraCol }));
    layout->addWidget(makeSeparator());

    // ══ GROUP 4 — Styles ═══════════════════════════════════════════════════
    // The gallery is data-driven: chips are (re)built from the style manager, so
    // user-created styles appear and deleted ones disappear. Right-click a chip
    // to modify / update / rename / delete it.
    m_styleGroup = new QButtonGroup(this);
    m_styleGroup->setExclusive(true);
    m_styleGrid  = new QWidget(tab);
    auto* sgl = new QGridLayout(m_styleGrid);
    sgl->setContentsMargins(0, 0, 0, 0); sgl->setSpacing(3);

    m_moreStylesBtn = makeIconBtn(paintIcon([](QPainter& p) {
        p.setBrush(kIconColor); p.setPen(Qt::NoPen);
        for (int x : {12, 20, 28}) p.drawEllipse(QPointF(x, 28), 2.0, 2.0);
        p.setPen(QPen(kIconColor, 2.2)); p.setBrush(Qt::NoBrush);
        p.drawText(QRectF(6, 6, 28, 16), Qt::AlignCenter, "AA");
    }), "More styles · New style · Manage…");
    m_moreStylesBtn->setPopupMode(QToolButton::InstantPopup);

    rebuildStyleGallery();
    rebuildMoreStylesMenu();

    auto* stylesRow = new QWidget(tab);
    auto* srl = new QHBoxLayout(stylesRow);
    srl->setContentsMargins(0, 0, 0, 0); srl->setSpacing(4);
    srl->addWidget(m_styleGrid);
    srl->addWidget(m_moreStylesBtn);

    layout->addWidget(makeGroup("Styles", { stylesRow }));
    layout->addWidget(makeSeparator());

    // ══ GROUP 5 — Editing ══════════════════════════════════════════════════
    auto* btnFind = makeBigBtn(findIcon(), "Find &\nReplace", "Find and replace (Ctrl+F)");
    connect(btnFind, &QToolButton::clicked, this, &WriterRibbon::openFindReplace);

    auto* btnSelect = makeBigBtn(paintIcon([](QPainter& p) {
        QPen dash(kIconColor); dash.setStyle(Qt::DashLine); dash.setWidthF(2.0);
        p.setPen(dash); p.drawRoundedRect(QRectF(8, 10, 24, 20), 2, 2);
    }), "Select", "Selection options");
    btnSelect->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(btnSelect);
        m->addAction(selectAllIcon(), "Select All  (Ctrl+A)", this, [this] { if (m_editor) m_editor->selectAll(); });
        m->addAction(selectSimilarIcon(), "Select All with Similar Formatting", this, [this] { selectSimilarFormatting(); });
        btnSelect->setMenu(m);
    }

    // Focus Mode also lives on Home (not just View) so it's reachable without
    // hunting for it. Both buttons drive the same toggle and stay in sync.
    m_btnFocusHome = makeBigBtn(focusModeIcon(), "Focus\nMode", kFocusModeTip, true);
    connect(m_btnFocusHome, &QToolButton::toggled, this,
            [this](bool on) { emit focusModeRequested(on); });

    layout->addWidget(makeGroup("Editing", { btnFind, btnSelect, m_btnFocusHome }));
    layout->addStretch();

    scroll->setWidget(tab);
    return scroll;
}

// ─────────────────────────────────────────────────────────────────────────────
// Insert tab — Pages · Tables · Illustrations · Links · Header/Footer · Text · Symbols
// ─────────────────────────────────────────────────────────────────────────────
QWidget* WriterRibbon::buildInsertTab() {
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("ribbonScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto* tab = new QWidget(scroll);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2);
    layout->setSpacing(0);

    // ══ Pages ══════════════════════════════════════════════════════════════
    auto* btnTemplate  = makeBigBtn(templateIcon(),     "Templates",    "Start from a template");
    auto* btnCover     = makeBigBtn(coverPageIcon(),    "Cover\nPage",  "Insert a formatted cover page");
    auto* btnPageBreak = makeBigBtn(insPageBreakIcon(), "Page\nBreak",  "Insert a page break");
    auto* btnBlankPage = makeBigBtn(insBlankPageIcon(), "Blank\nPage",  "Insert a blank page");
    connect(btnTemplate,  &QToolButton::clicked, this, &WriterRibbon::showTemplateGallery);
    connect(btnCover,     &QToolButton::clicked, this, &WriterRibbon::insertCoverPage);
    connect(btnPageBreak, &QToolButton::clicked, this, &WriterRibbon::insertPageBreak);
    connect(btnBlankPage, &QToolButton::clicked, this, &WriterRibbon::insertBlankPage);
    layout->addWidget(makeGroup("Pages", { btnTemplate, btnCover, btnPageBreak, btnBlankPage }));
    layout->addWidget(makeSeparator());

    // ══ Tables ═════════════════════════════════════════════════════════════
    auto* btnTable = makeBigBtn(insTableIcon(), "Table", "Insert a table");
    btnTable->setPopupMode(QToolButton::InstantPopup);
    {
        auto* menu = new QMenu(btnTable);
        // A few common preset sizes, then a custom dialog.
        struct Preset { int rows, cols; };
        const Preset presets[] = { {2, 2}, {2, 3}, {3, 3}, {4, 4} };
        for (const auto& ps : presets) {
            const int r = ps.rows, c = ps.cols;
            menu->addAction(tablePresetIcon(r, c),
                            QString("%1 × %2 Table").arg(r).arg(c),
                            this, [this, r, c] { insertTableSized(r, c); });
        }
        menu->addSeparator();
        menu->addAction(insTableIcon(), "Insert Table…", this, [this] {
            QDialog dlg(window());
            dlg.setWindowTitle("Insert Table");
            dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
            auto* form = new QFormLayout(&dlg);
            auto* rs = new QSpinBox(&dlg); rs->setRange(1, 50); rs->setValue(3);
            auto* cs = new QSpinBox(&dlg); cs->setRange(1, 20); cs->setValue(3);
            form->addRow("Rows:", rs);
            form->addRow("Columns:", cs);
            auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
            form->addRow(bb);
            connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
            if (dlg.exec() == QDialog::Accepted) insertTableSized(rs->value(), cs->value());
        });
        btnTable->setMenu(menu);
    }
    layout->addWidget(makeGroup("Tables", { btnTable }));
    layout->addWidget(makeSeparator());

    // ══ Illustrations ══════════════════════════════════════════════════════
    auto* btnPicture = makeBigBtn(insPictureIcon(), "Pictures", "Insert an image from a file");
    connect(btnPicture, &QToolButton::clicked, this, &WriterRibbon::insertImageRequested);

    auto* btnShapes = makeBigBtn(insShapesIcon(), "Shapes", "Insert a shape");
    btnShapes->setPopupMode(QToolButton::InstantPopup);
    {
        auto* menu = new QMenu(btnShapes);
        auto* grid = new QWidget(menu);
        auto* gl = new QGridLayout(grid);
        gl->setContentsMargins(8, 8, 8, 8); gl->setSpacing(4);
        struct Sh { int kind; const char* tip; };
        const Sh shapes[] = {
            {0,"Rectangle"}, {1,"Rounded rectangle"}, {2,"Ellipse"}, {3,"Triangle"},
            {4,"Diamond"}, {5,"Right arrow"}, {6,"5-point star"}, {7,"Line"},
        };
        int i = 0;
        for (const auto& s : shapes) {
            auto* b = new QToolButton(grid);
            b->setObjectName("ribbonToolBtn");
            b->setFixedSize(34, 34);
            b->setIconSize(QSize(24, 24));
            b->setCursor(Qt::PointingHandCursor);
            b->setToolTip(s.tip);
            // Paint a small preview of the shape onto the button.
            QPixmap pm(28, 28); pm.fill(Qt::transparent);
            QPainter pp(&pm); pp.setRenderHint(QPainter::Antialiasing);
            pp.setPen(QPen(kIconColor, 2.0)); pp.setBrush(QColor(58, 63, 75, 40));
            const QRectF rr(5, 6, 18, 16);
            switch (s.kind) {
            case 0: pp.drawRect(rr); break;
            case 1: pp.drawRoundedRect(rr, 4, 4); break;
            case 2: pp.drawEllipse(rr); break;
            case 3: { QPolygonF t; t << QPointF(14,6) << QPointF(23,22) << QPointF(5,22); pp.drawPolygon(t); break; }
            case 4: { QPolygonF d; d << QPointF(14,5) << QPointF(24,14) << QPointF(14,23) << QPointF(4,14); pp.drawPolygon(d); break; }
            case 5: { QPolygonF a; a << QPointF(5,11)<<QPointF(16,11)<<QPointF(16,7)<<QPointF(24,14)<<QPointF(16,21)<<QPointF(16,17)<<QPointF(5,17); pp.drawPolygon(a); break; }
            case 6: { QPolygonF st; for(int k=0;k<10;++k){double ang=-90+k*36;double rad=(k%2==0)?10:4;st<<QPointF(14+rad*qCos(ang*M_PI/180),14+rad*qSin(ang*M_PI/180));} pp.drawPolygon(st); break; }
            case 7: pp.setPen(QPen(kIconColor,2.4,Qt::SolidLine,Qt::RoundCap)); pp.drawLine(QPointF(5,22),QPointF(23,6)); break;
            }
            pp.end();
            b->setIcon(QIcon(pm));
            const int kind = s.kind;
            connect(b, &QToolButton::clicked, this, [this, menu, kind] {
                insertShapeImage(kind); menu->hide();
            });
            gl->addWidget(b, i / 4, i % 4); ++i;
        }
        auto* wa = new QWidgetAction(menu); wa->setDefaultWidget(grid);
        menu->addAction(wa);
        btnShapes->setMenu(menu);
    }

    auto* btnChart = makeBigBtn(chartKindIcon(0), "Chart", "Insert a chart");
    btnChart->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(btnChart);
        m->addAction(chartKindIcon(0), "Bar Chart",  this, [this] { insertChart(0); });
        m->addAction(chartKindIcon(1), "Line Chart", this, [this] { insertChart(1); });
        m->addAction(chartKindIcon(2), "Pie Chart",  this, [this] { insertChart(2); });
        btnChart->setMenu(m);
    }

    auto* btnHr = makeBigBtn(insHrIcon(), "Horizontal\nLine", "Insert a horizontal line");
    connect(btnHr, &QToolButton::clicked, this, &WriterRibbon::insertHorizontalRule);

    layout->addWidget(makeGroup("Illustrations", { btnPicture, btnShapes, btnChart, btnHr }));
    layout->addWidget(makeSeparator());

    // ══ Links ══════════════════════════════════════════════════════════════
    auto* btnLink = makeBigBtn(insLinkIcon(), "Link", "Insert a hyperlink");
    auto* btnBookmark = makeBigBtn(insBookmarkIcon(), "Bookmark", "Insert a bookmark anchor");
    connect(btnLink, &QToolButton::clicked, this, &WriterRibbon::insertHyperlink);
    connect(btnBookmark, &QToolButton::clicked, this, &WriterRibbon::insertBookmark);
    layout->addWidget(makeGroup("Links", { btnLink, btnBookmark }));
    layout->addWidget(makeSeparator());

    // ══ Header & Footer ════════════════════════════════════════════════════
    auto* btnHeader  = makeBigBtn(headerIcon(false), "Header", "Insert a header at the top of the document");
    auto* btnFooter  = makeBigBtn(headerIcon(true),  "Footer", "Insert a footer at the end of the document");
    auto* btnPageNum = makeBigBtn(insPageNumIcon(), "Page\nNumber", "Insert the current page number");
    connect(btnHeader,  &QToolButton::clicked, this, [this] { insertHeaderFooter(true); });
    connect(btnFooter,  &QToolButton::clicked, this, [this] { insertHeaderFooter(false); });
    connect(btnPageNum, &QToolButton::clicked, this, &WriterRibbon::insertPageNumberField);
    layout->addWidget(makeGroup("Header & Footer", { btnHeader, btnFooter, btnPageNum }));
    layout->addWidget(makeSeparator());

    // ══ Text ═══════════════════════════════════════════════════════════════
    auto* btnTextBox = makeBigBtn(insTextBoxIcon(), "Text\nBox", "Insert a text box");
    auto* btnWordArt = makeBigBtn(insWordArtIcon(), "WordArt", "Insert decorative WordArt text");
    auto* btnDropCap = makeBigBtn(insDropCapIcon(), "Drop\nCap", "Make the first letter a drop cap");
    connect(btnTextBox, &QToolButton::clicked, this, &WriterRibbon::insertTextBox);
    connect(btnWordArt, &QToolButton::clicked, this, &WriterRibbon::insertWordArt);
    connect(btnDropCap, &QToolButton::clicked, this, &WriterRibbon::insertDropCap);

    auto* btnDate = makeBigBtn(insDateTimeIcon(), "Date &\nTime", "Insert the date and time");
    btnDate->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(btnDate);
        const QDateTime now = QDateTime::currentDateTime();
        const QStringList fmts = {
            "dddd, MMMM d, yyyy", "MMMM d, yyyy", "d MMM yyyy",
            "dd/MM/yyyy", "MM/dd/yyyy", "hh:mm AP", "dd/MM/yyyy hh:mm AP"
        };
        for (const QString& f : fmts) {
            const QString text = now.toString(f);
            m->addAction(insDateTimeIcon(), text, this, [this, text] { insertDateTimeText(text); });
        }
        btnDate->setMenu(m);
    }

    layout->addWidget(makeGroup("Text", { btnTextBox, btnWordArt, btnDropCap, btnDate }));
    layout->addWidget(makeSeparator());

    // ══ Symbols ════════════════════════════════════════════════════════════
    auto* btnSymbol = makeBigBtn(insSymbolIcon(), "Symbol", "Insert a symbol");
    btnSymbol->setPopupMode(QToolButton::InstantPopup);
    {
        auto* menu = new QMenu(btnSymbol);
        auto* grid = new QWidget(menu);
        auto* gl = new QGridLayout(grid);
        gl->setContentsMargins(8, 8, 8, 8); gl->setSpacing(2);
        const QStringList syms = {
            "©","®","™","°","±","×","÷","≈","≠","≤","≥","∞",
            "µ","Ω","π","Σ","√","∫","€","£","¥","¢","§","¶",
            "•","→","←","↑","↓","★","☆","♥","½","¼","¾","°C"
        };
        int i = 0;
        for (const QString& s : syms) {
            auto* b = new QToolButton(grid);
            b->setText(s);
            b->setObjectName("ribbonToolBtn");
            b->setFixedSize(30, 30);
            b->setCursor(Qt::PointingHandCursor);
            b->setFont(QFont("Segoe UI", 12));
            connect(b, &QToolButton::clicked, this, [this, menu, s] {
                insertSymbolText(s); menu->hide();
            });
            gl->addWidget(b, i / 6, i % 6); ++i;
        }
        auto* wa = new QWidgetAction(menu); wa->setDefaultWidget(grid);
        menu->addAction(wa);
        menu->addSeparator();
        // Full character map, grouped by category (Word's "More Symbols…").
        menu->addAction(insSymbolIcon(), "More Symbols…", this, [this] {
            QDialog dlg(window());
            dlg.setWindowTitle("Symbols");
            dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
            dlg.resize(520, 440);
            auto* root = new QVBoxLayout(&dlg);
            auto* combo = new QComboBox(&dlg);
            auto* scroll = new QScrollArea(&dlg);
            scroll->setWidgetResizable(true);
            root->addWidget(combo);
            root->addWidget(scroll, 1);

            struct Range { const char* name; uint from, to; };
            const Range ranges[] = {
                { "Latin & Punctuation",  0x00A1, 0x00FF },
                { "Greek",                0x0391, 0x03C9 },
                { "Currency",             0x20A0, 0x20BF },
                { "Arrows",               0x2190, 0x21FF },
                { "Mathematical",         0x2200, 0x22FF },
                { "Geometric Shapes",     0x25A0, 0x25FF },
                { "Miscellaneous",        0x2600, 0x26FF },
                { "Dingbats",             0x2700, 0x27BF },
                { "Box Drawing",          0x2500, 0x257F },
                { "Superscript/Subscript",0x2070, 0x209C },
            };
            for (const auto& r : ranges) combo->addItem(r.name);

            auto rebuild = [this, scroll, &ranges, &dlg](int idx) {
                auto* grid2 = new QWidget(scroll);
                auto* g2 = new QGridLayout(grid2);
                g2->setContentsMargins(6, 6, 6, 6); g2->setSpacing(2);
                int n = 0;
                for (uint cp = ranges[idx].from; cp <= ranges[idx].to; ++cp) {
                    const QString s = QString::fromUcs4(reinterpret_cast<const char32_t*>(&cp), 1);
                    auto* b = new QToolButton(grid2);
                    b->setText(s);
                    b->setFixedSize(34, 34);
                    b->setCursor(Qt::PointingHandCursor);
                    b->setFont(QFont("Segoe UI", 13));
                    b->setToolTip(QString("U+%1").arg(cp, 4, 16, QChar('0')).toUpper());
                    connect(b, &QToolButton::clicked, this, [this, s] { insertSymbolText(s); });
                    g2->addWidget(b, n / 10, n % 10); ++n;
                }
                scroll->setWidget(grid2);
                Q_UNUSED(dlg);
            };
            connect(combo, &QComboBox::currentIndexChanged, &dlg, rebuild);
            rebuild(0);
            dlg.exec();
        });
        btnSymbol->setMenu(menu);
    }

    auto* btnEquation = makeBigBtn(equationIcon(), "Equation", "Insert an equation");
    btnEquation->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(btnEquation);
        struct Eq { const char* label; const char* text; };
        const Eq eqs[] = {
            { "Quadratic formula", "x = (-b ± √(b² - 4ac)) / 2a" },
            { "Pythagorean",       "a² + b² = c²" },
            { "Area of circle",    "A = πr²" },
            { "Summation",         "∑ⁿᵢ₌₁ i = n(n+1)/2" },
            { "Mass-energy",       "E = mc²" },
            { "Fraction",          "a/b" },
            { "Square root",       "√x" },
            { "Integral",          "∫ f(x) dx" },
        };
        m->addAction(equationIcon(), "Equation Editor…", this, &WriterRibbon::showEquationEditor);
        m->addSeparator();
        for (const auto& e : eqs) {
            const QString text = QString::fromUtf8(e.text);
            m->addAction(equationIcon(), e.label, this, [this, text] { insertEquation(text); });
        }
        btnEquation->setMenu(m);
    }

    layout->addWidget(makeGroup("Symbols", { btnSymbol, btnEquation }));
    layout->addStretch();

    scroll->setWidget(tab);
    return scroll;
}

// ─────────────────────────────────────────────────────────────────────────────
// Insert-tab actions
// ─────────────────────────────────────────────────────────────────────────────
void WriterRibbon::insertImageData(const QImage& img) {
    if (!m_editor || img.isNull()) return;
    QByteArray data;
    { QBuffer buf(&data); buf.open(QIODevice::WriteOnly); img.save(&buf, "PNG"); }
    const QString html = QStringLiteral("<img src=\"data:image/png;base64,%1\"/>")
                             .arg(QString::fromLatin1(data.toBase64()));
    m_editor->textCursor().insertHtml(html);
    m_editor->setFocus();
}

void WriterRibbon::insertTableSized(int rows, int cols) {
    if (!m_editor) return;
    QTextTableFormat tf;
    tf.setCellPadding(4);
    tf.setCellSpacing(0);
    tf.setBorder(1);
    tf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    tf.setBorderBrush(QColor("#9CA3AF"));
    tf.setWidth(QTextLength(QTextLength::PercentageLength, 100));
    QTextCursor cur = m_editor->textCursor();
    cur.insertTable(rows, cols, tf);
    m_editor->setTextCursor(cur);     // place the editor cursor inside the new table
    m_editor->setFocus();

    // Reveal the contextual Table tab so the table tools are reachable, but stay
    // on the tab the user is actually on — yanking the ribbon away mid-task reads
    // as a glitch rather than a help.
    refreshTableTab();
}

// A page break marks where the following content starts a new page; it is not a
// way to add a page (that's Insert → Blank Page). So it only ever pushes content
// that already exists: with nothing after the cursor there is nothing to push,
// and inserting a break there would just strand an empty page at the end.
void WriterRibbon::insertPageBreak() {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();

    QTextCursor probe = cur;
    probe.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    if (probe.selectedText().trimmed().isEmpty()) {
        if (m_editor->window())
            QToolTip::showText(QCursor::pos(),
                tr("Nothing after the cursor to move onto a new page.\n"
                   "Use Insert → Blank Page to add a page."), m_editor);
        return;
    }

    cur.beginEditBlock();
    // At a block start the block itself can carry the break; mid-block, split it
    // first so the remainder (not a new empty paragraph) is what moves down.
    if (!cur.atBlockStart()) cur.insertBlock();
    QTextBlockFormat bf = cur.blockFormat();
    bf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    cur.setBlockFormat(bf);
    cur.endEditBlock();
    m_editor->setTextCursor(cur);
    m_editor->setFocus();
}

// Adding a page IS this button's job (unlike Page Break). Reuses the paper's own
// page insertion, so it is one undo step and already handles the first-block case
// that used to spawn a spurious second page.
void WriterRibbon::insertBlankPage() {
    auto* paper = qobject_cast<PagedTextEdit*>(m_editor);
    if (!paper) return;
    paper->addBlankPageBelow(paper->currentPageNumber() - 1);
    m_editor->setFocus();
}

void WriterRibbon::insertHorizontalRule() {
    if (!m_editor) return;
    m_editor->textCursor().insertHtml("<hr/>");
    m_editor->setFocus();
}

void WriterRibbon::insertHyperlink() {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    const QString preset = cur.hasSelection() ? cur.selectedText() : QString();

    QDialog dlg(window());
    dlg.setWindowTitle("Insert Hyperlink");
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    auto* form = new QFormLayout(&dlg);
    auto* textEdit = new QLineEdit(preset, &dlg);
    auto* urlEdit  = new QLineEdit("https://", &dlg);
    form->addRow("Text:", textEdit);
    form->addRow("Address:", urlEdit);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString url = urlEdit->text().trimmed();
    QString text = textEdit->text();
    if (url.isEmpty()) return;
    if (text.isEmpty()) text = url;
    const QString html = QStringLiteral(
        "<a href=\"%1\" style=\"color:#2563EB; text-decoration:underline;\">%2</a>&nbsp;")
        .arg(url.toHtmlEscaped(), text.toHtmlEscaped());
    cur.insertHtml(html);
    m_editor->setFocus();
}

void WriterRibbon::insertBookmark() {
    if (!m_editor) return;
    bool ok = false;
    const QString name = QInputDialog::getText(window(), "Insert Bookmark",
                            "Bookmark name:", QLineEdit::Normal, QString(), &ok);
    if (!ok || name.isEmpty()) return;
    const QString anchor = name.simplified().replace(' ', '_').toHtmlEscaped();
    m_editor->textCursor().insertHtml(
        QStringLiteral("<a name=\"%1\">⚑</a>").arg(anchor));
    m_editor->setFocus();
}

// Page numbers live in the header/footer zones, so this asks the two questions
// that actually matter (where, and what should it say) and writes the field for
// you. It used to silently re-apply the built-in default — which is already
// "Page {n} of {N}" in the footer centre — so clicking it looked like a no-op.
void WriterRibbon::insertPageNumberField() {
    auto* paper = qobject_cast<PagedTextEdit*>(m_editor);
    if (!paper) return;

    QDialog dlg(window());
    dlg.setWindowTitle("Page Numbers");
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    auto* form = new QFormLayout(&dlg);

    auto* posV = new QComboBox(&dlg);
    posV->addItem("Bottom of page (footer)", 1);
    posV->addItem("Top of page (header)",    0);
    auto* posH = new QComboBox(&dlg);
    posH->addItem("Centre", 1);
    posH->addItem("Left",   0);
    posH->addItem("Right",  2);
    auto* fmt = new QComboBox(&dlg);
    fmt->addItem("Page 1 of N", "Page {n} of {N}");
    fmt->addItem("1",           "{n}");
    fmt->addItem("Page 1",      "Page {n}");
    fmt->addItem("- 1 -",       "- {n} -");
    fmt->addItem("Remove page numbers", "");

    form->addRow("Position:", posV);
    form->addRow("Alignment:", posH);
    form->addRow("Format:", fmt);

    auto* preview = new QLabel(&dlg);
    preview->setStyleSheet("color:#3A3F4B;background:#FFFFFF;border:1px solid #D8DCE6;"
                           "border-radius:6px;padding:6px;");
    auto updatePreview = [&, paper] {
        QString s = fmt->currentData().toString();
        if (s.isEmpty()) { preview->setText("Page numbers will be removed."); return; }
        s.replace("{n}", "1").replace("{N}", QString::number(paper->pageCountValue()));
        preview->setText("On page 1 this reads:  " + s);
    };
    updatePreview();
    connect(fmt, &QComboBox::currentIndexChanged, &dlg, [&updatePreview] { updatePreview(); });
    form->addRow(preview);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) { m_editor->setFocus(); return; }

    QStringList h = paper->headerZones();
    QStringList f = paper->footerZones();
    const QString code = fmt->currentData().toString();
    const int zone = posH->currentData().toInt();
    const bool footer = posV->currentData().toInt() == 1;

    // Clear any existing page-number field wherever it currently sits, so the
    // number moves rather than appearing twice.
    for (QStringList* z : { &h, &f })
        for (QString& s : *z)
            if (s.contains("{n}") || s.contains("{N}")) s.clear();
    (footer ? f : h)[zone] = code;

    paper->setHeaderFooter(h, f, paper->differentFirstPage());
    m_editor->setFocus();
}

void WriterRibbon::insertTextBox() {
    if (!m_editor) return;
    QTextTableFormat tf;
    tf.setCellPadding(8);
    tf.setCellSpacing(0);
    tf.setBorder(1);
    tf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    tf.setBorderBrush(QColor("#6B7280"));
    tf.setWidth(QTextLength(QTextLength::PercentageLength, 60));
    QTextCursor cur = m_editor->textCursor();
    QTextTable* tbl = cur.insertTable(1, 1, tf);
    if (tbl) {
        QTextCursor inner = tbl->cellAt(0, 0).firstCursorPosition();
        inner.insertText("Text box");
        m_editor->setTextCursor(tbl->cellAt(0, 0).firstCursorPosition());
    }
    m_editor->setFocus();
}

// ── WordArt ─────────────────────────────────────────────────────────────────
namespace {

struct WordArtStyle {
    const char* name;
    const char* family;
    bool        italic;
    const char* top;        // gradient fill, top → bottom
    const char* bottom;
    const char* outline;
    double      outlineWidth;
    bool        shadow;
};

// Ten presets. Deliberately spread across colour families and treatments
// (gradient, outline-only, shadowed) so the gallery reads as ten real choices
// rather than one style in ten colours.
const WordArtStyle kWordArtStyles[] = {
    { "Crimson",  "Georgia",  true,  "#FF5247", "#B91C1C", "#2C3140", 2.0, false },
    { "Ocean",    "Segoe UI", false, "#38BDF8", "#1D4ED8", "#0B2545", 2.0, false },
    { "Gold",     "Georgia",  false, "#FDE68A", "#D97706", "#7C2D12", 2.0, true  },
    { "Orchid",   "Segoe UI", true,  "#F0ABFC", "#7E22CE", "#3B0764", 2.0, false },
    { "Meadow",   "Segoe UI", false, "#86EFAC", "#15803D", "#052E16", 2.0, false },
    { "Outline",  "Impact",   false, "#FFFFFF", "#F1F5F9", "#1F2937", 2.6, false },
    { "Midnight", "Georgia",  false, "#475569", "#0F172A", "#94A3B8", 1.4, true  },
    { "Sunset",   "Segoe UI", true,  "#FBBF24", "#DB2777", "#7F1D1D", 2.0, false },
    { "Steel",    "Segoe UI", false, "#F8FAFC", "#64748B", "#334155", 2.0, true  },
    { "Lagoon",   "Georgia",  true,  "#5EEAD4", "#0F766E", "#FFFFFF", 2.4, false },
};
constexpr int kWordArtCount = int(sizeof(kWordArtStyles) / sizeof(kWordArtStyles[0]));

QImage renderWordArt(const QString& text, int style, int pointSize) {
    const WordArtStyle& s = kWordArtStyles[qBound(0, style, kWordArtCount - 1)];
    QFont f(QString::fromLatin1(s.family), pointSize, QFont::Bold);
    f.setItalic(s.italic);
    const QFontMetrics fm(f);
    const int pad = qMax(10, pointSize / 2);
    const int w = fm.horizontalAdvance(text) + pad * 2 + 8;
    const int h = fm.height() + pad * 2;

    QImage img(qMax(w, 1), qMax(h, 1), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    QPainterPath path;
    path.addText(pad, pad + fm.ascent(), f, text);

    if (s.shadow) {
        p.save();
        p.translate(pointSize * 0.06 + 1.5, pointSize * 0.06 + 1.5);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 70));
        p.drawPath(path);
        p.restore();
    }
    QLinearGradient g(0, pad, 0, h - pad);
    g.setColorAt(0.0, QColor(s.top));
    g.setColorAt(1.0, QColor(s.bottom));
    p.setPen(QPen(QColor(s.outline), s.outlineWidth));
    p.setBrush(QBrush(g));
    p.drawPath(path);
    p.end();
    return img;
}

} // namespace

void WriterRibbon::insertWordArt() {
    if (!m_editor) return;

    QDialog dlg(window());
    dlg.setWindowTitle("Insert WordArt");
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    auto* root = new QVBoxLayout(&dlg);
    root->setSpacing(10);

    auto* textRow = new QHBoxLayout();
    textRow->addWidget(new QLabel("Text:", &dlg));
    auto* edit = new QLineEdit("WordArt", &dlg);
    edit->selectAll();
    textRow->addWidget(edit, 1);
    root->addLayout(textRow);

    root->addWidget(new QLabel("Pick a style — the previews show your own text:", &dlg));

    auto* grid = new QGridLayout();
    grid->setSpacing(6);
    auto* group = new QButtonGroup(&dlg);
    group->setExclusive(true);
    QList<QToolButton*> tiles;
    for (int i = 0; i < kWordArtCount; ++i) {
        auto* b = new QToolButton(&dlg);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setIconSize(QSize(148, 44));
        b->setFixedSize(160, 56);
        b->setToolTip(kWordArtStyles[i].name);
        b->setStyleSheet(
            "QToolButton{background:#FFFFFF;border:1px solid #D8DCE6;border-radius:6px;}"
            "QToolButton:hover{border:1px solid #9C8CF0;}"
            "QToolButton:checked{border:2px solid #6D5AE6;background:#F3F1FF;}");
        group->addButton(b, i);
        grid->addWidget(b, i / 5, i % 5);
        tiles << b;
    }
    tiles[0]->setChecked(true);
    root->addLayout(grid);

    // Live previews: cheap enough (ten small renders) to redo on every keystroke.
    auto refreshTiles = [&tiles, edit] {
        const QString t = edit->text().isEmpty() ? QStringLiteral("WordArt") : edit->text();
        for (int i = 0; i < tiles.size(); ++i) {
            const QImage im = renderWordArt(t, i, 20);
            tiles[i]->setIcon(QIcon(QPixmap::fromImage(
                im.scaled(148, 44, Qt::KeepAspectRatio, Qt::SmoothTransformation))));
        }
    };
    refreshTiles();
    connect(edit, &QLineEdit::textChanged, &dlg, [&refreshTiles] { refreshTiles(); });
    // Enter in the text field accepts, so a quick pick needn't reach for the mouse.
    connect(edit, &QLineEdit::returnPressed, &dlg, &QDialog::accept);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    root->addWidget(bb);

    if (dlg.exec() != QDialog::Accepted) return;
    const QString text = edit->text();
    if (text.isEmpty()) return;
    insertImageData(renderWordArt(text, qMax(0, group->checkedId()), 44));
}

void WriterRibbon::insertDropCap() {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    cur.movePosition(QTextCursor::StartOfBlock);
    cur.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    if (!cur.hasSelection()) return;
    QTextCharFormat fmt;
    fmt.setFontPointSize(36);
    fmt.setFontWeight(QFont::Bold);
    fmt.setForeground(QColor("#2C3140"));
    cur.mergeCharFormat(fmt);
    m_editor->setFocus();
}

void WriterRibbon::insertDateTimeText(const QString& formatted) {
    if (!m_editor) return;
    m_editor->textCursor().insertText(formatted);
    m_editor->setFocus();
}

void WriterRibbon::insertSymbolText(const QString& sym) {
    if (!m_editor) return;
    m_editor->textCursor().insertText(sym);
    m_editor->setFocus();
}

void WriterRibbon::insertShapeImage(int kind) {
    if (!m_editor) return;
    const int W = 180, H = 120;
    QImage img(W, H, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#2C3140"), 3.0));
    p.setBrush(QColor(232, 55, 42, 60));
    const QRectF r(14, 14, W - 28, H - 28);
    switch (kind) {
    case 0: p.drawRect(r); break;
    case 1: p.drawRoundedRect(r, 16, 16); break;
    case 2: p.drawEllipse(r); break;
    case 3: { QPolygonF t; t << QPointF(W/2.0, 14) << QPointF(W-14, H-14) << QPointF(14, H-14); p.drawPolygon(t); break; }
    case 4: { QPolygonF d; d << QPointF(W/2.0,14) << QPointF(W-14,H/2.0) << QPointF(W/2.0,H-14) << QPointF(14,H/2.0); p.drawPolygon(d); break; }
    case 5: { QPolygonF a; a << QPointF(14,44)<<QPointF(110,44)<<QPointF(110,24)<<QPointF(166,60)
                               <<QPointF(110,96)<<QPointF(110,76)<<QPointF(14,76); p.drawPolygon(a); break; }
    case 6: { QPolygonF st; for(int k=0;k<10;++k){double ang=-90+k*36;double rad=(k%2==0)?52:21;
              st<<QPointF(W/2.0+rad*qCos(ang*M_PI/180), H/2.0+rad*qSin(ang*M_PI/180));} p.drawPolygon(st); break; }
    case 7: p.setPen(QPen(QColor("#2C3140"), 3.0, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(14, H-14), QPointF(W-14, 14)); break;
    }
    p.end();
    insertImageData(img);
}

void WriterRibbon::insertCoverPage() {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    cur.beginEditBlock();
    cur.movePosition(QTextCursor::Start);

    auto writeLine = [&](const QString& text, WriterStyle style) {
        QTextCharFormat cf; QTextBlockFormat bf;
        // (styleFormats lives in the anon namespace; replicate the key bits inline)
        cf.setFontFamilies({"Segoe UI", "Inter", "Roboto", "sans-serif"});
        bf.setAlignment(Qt::AlignHCenter);
        if (style == WriterStyle::Title)    { cf.setFontPointSize(34); cf.setFontWeight(QFont::Light); bf.setTopMargin(120); }
        else                                { cf.setFontPointSize(16); cf.setFontItalic(true); cf.setForeground(QColor("#6B7280")); bf.setTopMargin(10); }
        cur.insertBlock(bf, cf);
        cur.insertText(text, cf);
    };

    writeLine("Document Title", WriterStyle::Title);
    writeLine("Subtitle or author name", WriterStyle::Subtitle);

    // Page break so the body starts on a fresh page.
    QTextBlockFormat brk;
    brk.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    cur.insertBlock(brk, QTextCharFormat());
    cur.endEditBlock();
    m_editor->setFocus();
}

void WriterRibbon::insertHeaderFooter(bool header) {
    openHeaderFooterDialog(header ? 0 : 1);
}

// A form laid out the way the page actually is: a Header row and a Footer row,
// each with the three zones side by side in the order they appear on paper, and a
// live preview underneath. `focusFooter` decides which row is focused, so the
// Header and Footer buttons no longer do the identical thing.
void WriterRibbon::openHeaderFooterDialog(int focusSection) {
    auto* paper = qobject_cast<PagedTextEdit*>(m_editor);
    if (!paper) return;

    QDialog dlg(window());
    dlg.setWindowTitle("Header and Footer");
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    dlg.setMinimumWidth(560);
    auto* root = new QVBoxLayout(&dlg);
    root->setSpacing(10);

    const QStringList hz = paper->headerZones();
    const QStringList fz = paper->footerZones();
    QLineEdit* ed[2][3];

    // Zones laid out left/centre/right, matching where they land on the page.
    auto buildSection = [&](const char* title, const char* hint,
                            const QStringList& vals, int row) {
        auto* box = new QVBoxLayout();
        auto* t = new QLabel(QString("<b>%1</b>  <span style='color:#8A90A0'>%2</span>")
                                 .arg(title, hint), &dlg);
        box->addWidget(t);
        auto* cols = new QHBoxLayout();
        const char* zone[3] = { "Left", "Centre", "Right" };
        for (int i = 0; i < 3; ++i) {
            auto* col = new QVBoxLayout();
            auto* lbl = new QLabel(zone[i], &dlg);
            lbl->setStyleSheet("color:#6B7280;font-size:11px;");
            ed[row][i] = new QLineEdit(vals.value(i), &dlg);
            ed[row][i]->setPlaceholderText("(empty)");
            col->addWidget(lbl);
            col->addWidget(ed[row][i]);
            cols->addLayout(col);
        }
        box->addLayout(cols);
        return box;
    };
    root->addLayout(buildSection("Header", "printed in the top margin of every page", hz, 0));
    root->addLayout(buildSection("Footer", "printed in the bottom margin of every page", fz, 1));

    // Remember the last field the user was in, so the field buttons always have a
    // target. Tracking focus explicitly matters: the old code read focusWidget()
    // at click time and silently did nothing if no field had been clicked yet.
    // exec() blocks below, so capturing this local by reference is safe.
    QLineEdit* target = ed[focusSection == 1 ? 1 : 0][1];
    connect(qApp, &QApplication::focusChanged, &dlg, [&target](QWidget*, QWidget* now) {
        if (auto* le = qobject_cast<QLineEdit*>(now)) target = le;
    });

    auto* fieldRow = new QHBoxLayout();
    auto* fl = new QLabel("Add:", &dlg);
    fieldRow->addWidget(fl);
    struct Fld { const char* label; const char* code; const char* tip; };
    for (const Fld& f : { Fld{"Page number", "{n}", "The current page's number"},
                          Fld{"Total pages", "{N}", "How many pages the document has"},
                          Fld{"Date",        "{date}", "Today's date"},
                          Fld{"File name",   "{file}", "The document's file name"} }) {
        auto* b = new QPushButton(f.label, &dlg);
        b->setFocusPolicy(Qt::NoFocus);     // keep focus in the field being edited
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(f.tip);
        const QString code = f.code;
        connect(b, &QPushButton::clicked, &dlg, [&target, code] {
            if (target) { target->insert(code); target->setFocus(); }
        });
        fieldRow->addWidget(b);
    }
    fieldRow->addStretch();
    root->addLayout(fieldRow);

    auto* diff = new QCheckBox("Leave page 1 blank (title pages usually have no header)", &dlg);
    diff->setChecked(paper->differentFirstPage());
    root->addWidget(diff);

    // Live preview — shows the fields expanded, so {n} stops being a mystery.
    auto* preview = new QLabel(&dlg);
    preview->setStyleSheet("background:#FFFFFF;border:1px solid #D8DCE6;border-radius:6px;"
                           "padding:8px;color:#3A3F4B;font-size:11px;");
    preview->setTextFormat(Qt::RichText);
    auto updatePreview = [&, paper] {
        const int total = paper->pageCountValue();
        auto rowHtml = [&](int r) {
            QStringList cells;
            for (int i = 0; i < 3; ++i) {
                QString s = ed[r][i]->text();
                s.replace("{n}", "1").replace("{N}", QString::number(total));
                s.replace("{date}", QDate::currentDate().toString("MMMM d, yyyy"));
                s.replace("{file}", "Untitled");
                cells << (s.isEmpty() ? QStringLiteral("&nbsp;") : s.toHtmlEscaped());
            }
            // NB: QString::arg has no %% escape — a literal % is just '%'.
            return QString("<tr><td width='33%' align='left'>%1</td>"
                           "<td width='34%' align='center'>%2</td>"
                           "<td width='33%' align='right'>%3</td></tr>")
                .arg(cells[0], cells[1], cells[2]);
        };
        preview->setText(QString(
            "<b style='color:#6B7280'>Preview — page 1 of %1</b>"
            "<table width='100%' cellpadding='2'>%2"
            "<tr><td colspan='3' style='color:#B4B9C6'>— page content —</td></tr>%3</table>")
            .arg(total).arg(rowHtml(0), rowHtml(1)));
    };
    updatePreview();
    for (int r = 0; r < 2; ++r)
        for (int i = 0; i < 3; ++i)
            connect(ed[r][i], &QLineEdit::textChanged, &dlg, [&updatePreview] { updatePreview(); });
    root->addWidget(preview);

    auto* bb = new QDialogButtonBox(&dlg);
    bb->addButton(QDialogButtonBox::Ok);
    bb->addButton(QDialogButtonBox::Cancel);
    auto* clear = bb->addButton("Clear All", QDialogButtonBox::ResetRole);
    connect(clear, &QPushButton::clicked, &dlg, [&] {
        for (int r = 0; r < 2; ++r) for (int i = 0; i < 3; ++i) ed[r][i]->clear();
    });
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    root->addWidget(bb);

    ed[focusSection == 1 ? 1 : 0][1]->setFocus();
    ed[focusSection == 1 ? 1 : 0][1]->selectAll();

    if (dlg.exec() == QDialog::Accepted) {
        paper->setHeaderFooter({ ed[0][0]->text(), ed[0][1]->text(), ed[0][2]->text() },
                               { ed[1][0]->text(), ed[1][1]->text(), ed[1][2]->text() },
                               diff->isChecked());
    }
    m_editor->setFocus();
}

void WriterRibbon::insertEquation(const QString& text) {
    if (!m_editor) return;
    QTextCharFormat fmt;
    fmt.setFontFamilies({"Cambria Math", "Segoe UI", "serif"});
    fmt.setFontItalic(true);
    QTextCursor cur = m_editor->textCursor();
    cur.insertText(" " + text + " ", fmt);
    m_editor->setFocus();
}

void WriterRibbon::insertChart(int kind) {
    if (!m_editor) return;
    const int W = 320, H = 200;
    QImage img(W, H, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);

    // frame + axes
    p.setPen(QPen(QColor("#9CA3AF"), 1));
    p.drawRect(0, 0, W - 1, H - 1);
    const QRectF plot(40, 20, W - 60, H - 60);
    p.setPen(QPen(QColor("#6B7280"), 1.5));
    p.drawLine(plot.bottomLeft(), plot.bottomRight());
    p.drawLine(plot.topLeft(), plot.bottomLeft());

    const QColor bars[] = { QColor("#E8372A"), QColor("#2563EB"), QColor("#16A34A"),
                            QColor("#EA580C"), QColor("#7C3AED") };
    const double vals[] = { 0.5, 0.8, 0.4, 1.0, 0.65 };
    const int n = 5;

    if (kind == 0) {                 // bar
        const double bw = plot.width() / (n * 1.6);
        for (int i = 0; i < n; ++i) {
            const double x = plot.left() + 12 + i * (plot.width() / n);
            const double h = vals[i] * plot.height();
            p.setBrush(bars[i]); p.setPen(Qt::NoPen);
            p.drawRect(QRectF(x, plot.bottom() - h, bw, h));
        }
    } else if (kind == 1) {          // line
        QPolygonF pts;
        for (int i = 0; i < n; ++i)
            pts << QPointF(plot.left() + i * (plot.width() / (n - 1)),
                           plot.bottom() - vals[i] * plot.height());
        p.setPen(QPen(QColor("#E8372A"), 2.5)); p.setBrush(Qt::NoBrush);
        p.drawPolyline(pts);
        p.setBrush(QColor("#E8372A")); p.setPen(Qt::NoPen);
        for (const QPointF& pt : pts) p.drawEllipse(pt, 3.2, 3.2);
    } else {                         // pie
        const QRectF pie(W / 2.0 - 70, H / 2.0 - 70, 140, 140);
        double total = 0; for (double v : vals) total += v;
        double start = 90 * 16;
        for (int i = 0; i < n; ++i) {
            const double span = -(vals[i] / total) * 360 * 16;
            p.setBrush(bars[i]); p.setPen(QPen(Qt::white, 1.5));
            p.drawPie(pie, static_cast<int>(start), static_cast<int>(span));
            start += span;
        }
    }
    p.end();
    insertImageData(img);
}

// ─────────────────────────────────────────────────────────────────────────────
// Page Layout tab — Page Setup · Background · Paragraph
// ─────────────────────────────────────────────────────────────────────────────
QWidget* WriterRibbon::buildPageLayoutTab() {
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("ribbonScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* tab = new QWidget(scroll);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2); layout->setSpacing(0);

    // ── Page Setup ──────────────────────────────────────────────────────────
    auto* btnMargins = makeBigBtn(marginsIcon(), "Margins", "Set page margins");
    btnMargins->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(btnMargins);
        struct M { const char* name; double px; };
        const M ms[] = { {"Normal  (2.5 cm)", 60}, {"Narrow  (1.3 cm)", 30},
                         {"Moderate  (1.9 cm)", 45}, {"Wide  (5 cm)", 96},
                         {"Office Classic  (3.2 cm)", 76} };
        for (const auto& mm : ms) {
            const double px = mm.px;
            m->addAction(marginsIcon(), mm.name, this, [this, px] { emit pageMarginsRequested(px); });
        }
        m->addSeparator();
        m->addAction(marginsIcon(), "Custom Margins…", this, [this] {
            bool ok = false;
            const double cm = QInputDialog::getDouble(window(), "Custom Margins",
                                  "Margin (cm):", 2.5, 0.0, 8.0, 1, &ok);
            if (ok) emit pageMarginsRequested(cm / 2.54 * 96.0);
        });
        btnMargins->setMenu(m);
    }

    auto* btnOrient = makeBigBtn(orientationIcon(false), "Orientation", "Page orientation");
    btnOrient->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(btnOrient);
        m->addAction(orientationIcon(false), "Portrait",  this, [this] { emit orientationRequested(false); });
        m->addAction(orientationIcon(true),  "Landscape", this, [this] { emit orientationRequested(true); });
        btnOrient->setMenu(m);
    }

    auto* btnSize = makeBigBtn(pageSizeIcon(), "Size", "Paper size");
    btnSize->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(btnSize);
        struct S { const char* name; double w, h; };
        const S ss[] = {
            { "A3  (29.7 × 42 cm)",    1123, 1587 },
            { "A4  (21 × 29.7 cm)",    794, 1123 },
            { "A5  (14.8 × 21 cm)",    559, 794 },
            { "A6  (10.5 × 14.8 cm)",  397, 559 },
            { "B5  (17.6 × 25 cm)",    665, 944 },
            { "Letter  (8.5 × 11 in)", 816, 1056 },
            { "Legal  (8.5 × 14 in)",  816, 1344 },
            { "Executive  (7.25 × 10.5 in)", 696, 1008 },
            { "Tabloid  (11 × 17 in)", 1056, 1632 },
        };
        for (const auto& s : ss) {
            const double w = s.w, h = s.h;
            m->addAction(pageSizeIcon(), s.name, this, [this, w, h] { emit pageSizeRequested(w, h); });
        }
        m->addSeparator();
        m->addAction(pageSizeIcon(), "Custom Size…", this, [this] {
            bool ok = false;
            const double wcm = QInputDialog::getDouble(window(), "Custom Page Size",
                                   "Width (cm):", 21.0, 5.0, 120.0, 1, &ok);
            if (!ok) return;
            const double hcm = QInputDialog::getDouble(window(), "Custom Page Size",
                                   "Height (cm):", 29.7, 5.0, 120.0, 1, &ok);
            if (!ok) return;
            emit pageSizeRequested(wcm / 2.54 * 96.0, hcm / 2.54 * 96.0);
        });
        btnSize->setMenu(m);
    }

    auto* btnTextDir = makeBigBtn(textDirIcon(), "Text\nDirection", "Paragraph text direction");
    btnTextDir->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(btnTextDir);
        m->addAction(textDirIcon(), "Left-to-Right", this, [this] { setTextDirection(false); });
        m->addAction(textDirIcon(), "Right-to-Left", this, [this] { setTextDirection(true); });
        btnTextDir->setMenu(m);
    }
    layout->addWidget(makeGroup("Page Setup", { btnMargins, btnOrient, btnSize, btnTextDir }));
    layout->addWidget(makeSeparator());

    // ── Columns & Sections (Tier 3 #11) ──────────────────────────────────────
    auto* btnColumns = makeBigBtn(columnsIcon(), "Columns", "Lay text out in columns");
    btnColumns->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(btnColumns);
        m->addAction(columnsIcon(), "One",   this, [this] { applyColumns(1); });
        m->addAction(columnsIcon(), "Two",   this, [this] { applyColumns(2); });
        m->addAction(columnsIcon(), "Three", this, [this] { applyColumns(3); });
        btnColumns->setMenu(m);
    }
    auto* btnSection = makeBigBtn(insPageBreakIcon(), "Section\nBreak", "Start a new section on the next page");
    connect(btnSection, &QToolButton::clicked, this, &WriterRibbon::insertPageBreak);
    layout->addWidget(makeGroup("Columns", { btnColumns, btnSection }));
    layout->addWidget(makeSeparator());

    // ── Page Background ─────────────────────────────────────────────────────
    auto* btnPageColor = makeBigBtn(pageColorIcon(), "Page\nColor", "Page background colour");
    btnPageColor->setPopupMode(QToolButton::InstantPopup);
    {
        auto* menu = new QMenu(btnPageColor);
        auto* grid = new QWidget(menu);
        auto* gl = new QGridLayout(grid);
        gl->setContentsMargins(8, 8, 8, 8); gl->setSpacing(4);
        const char* palette[] = {
            "#FFFFFF","#F8F5EC","#F3F4F6","#FFF7ED","#F0FDF4","#EFF6FF",
            "#FDF2F8","#FEFCE8","#1C1E26","#2C3140","#FEE2E2","#E0E7FF" };
        int i = 0;
        for (const char* hex : palette) {
            auto* sw = new QToolButton(grid);
            sw->setFixedSize(22, 22); sw->setCursor(Qt::PointingHandCursor); sw->setToolTip(hex);
            sw->setStyleSheet(QString("QToolButton{background:%1;border:1px solid #C6CAD3;border-radius:3px;}"
                                      "QToolButton:hover{border:2px solid #6D5BE8;}").arg(hex));
            const QColor c(hex);
            connect(sw, &QToolButton::clicked, this, [this, c, menu] { emit pageColorRequested(c); menu->hide(); });
            gl->addWidget(sw, i / 6, i % 6); ++i;
        }
        auto* wa = new QWidgetAction(menu); wa->setDefaultWidget(grid);
        menu->addAction(wa);
        menu->addAction(pageColorIcon(), "More Colours…", this, [this] {
            const QColor c = QColorDialog::getColor(Qt::white, window(), "Page Colour");
            if (c.isValid()) emit pageColorRequested(c);
        });
        btnPageColor->setMenu(menu);
    }

    auto* btnBorders = makeBigBtn(pageBordersIcon(), "Page\nBorders", "Border around the page");
    btnBorders->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(btnBorders);
        m->addAction(pageBordersIcon(), "Box Border",    this, [this] { applyPageBorders(1); });
        m->addAction(pageBordersIcon(), "Shadow Border", this, [this] { applyPageBorders(2); });
        m->addAction(pageBordersIcon(), "No Border",     this, [this] { applyPageBorders(0); });
        btnBorders->setMenu(m);
    }

    auto* btnWatermark = makeBigBtn(watermarkIcon(), "Watermark", "Insert a watermark line");
    btnWatermark->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(btnWatermark);
        for (const char* w : { "DRAFT", "CONFIDENTIAL", "SAMPLE", "DO NOT COPY" }) {
            const QString text = QString::fromUtf8(w);
            m->addAction(watermarkIcon(), text, this, [this, text] {
                if (!m_editor) return;
                QTextCursor cur = m_editor->textCursor();
                cur.movePosition(QTextCursor::Start);
                QTextCharFormat cf; cf.setForeground(QColor(160, 160, 160));
                cf.setFontPointSize(40); cf.setFontWeight(QFont::Bold);
                QTextBlockFormat bf; bf.setAlignment(Qt::AlignHCenter);
                cur.insertBlock(bf, cf); cur.insertText(text, cf);
                cur.insertBlock(QTextBlockFormat(), QTextCharFormat());
                m_editor->setFocus();
            });
        }
        btnWatermark->setMenu(m);
    }
    layout->addWidget(makeGroup("Page Background", { btnPageColor, btnBorders, btnWatermark }));
    layout->addWidget(makeSeparator());

    // ── Pages ───────────────────────────────────────────────────────────────
    auto* btnBreaks = makeBigBtn(breaksIcon(), "Breaks", "Insert a break");
    btnBreaks->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(btnBreaks);
        m->addAction(insPageBreakIcon(), "Page Break",      this, [this] { insertPageBreak(); });
        m->addAction(insHrIcon(),        "Horizontal Line", this, [this] { insertHorizontalRule(); });
        btnBreaks->setMenu(m);
    }
    auto* btnCover = makeBigBtn(coverPageIcon(), "Cover\nPage", "Insert a cover page");
    connect(btnCover, &QToolButton::clicked, this, &WriterRibbon::insertCoverPage);
    auto* btnBlank = makeBigBtn(insBlankPageIcon(), "Blank\nPage", "Insert a blank page");
    connect(btnBlank, &QToolButton::clicked, this, [this] { insertPageBreak(); insertPageBreak(); });
    layout->addWidget(makeGroup("Pages", { btnBreaks, btnCover, btnBlank }));
    layout->addWidget(makeSeparator());

    // ── Paragraph (indent + spacing) ────────────────────────────────────────
    auto* rIndLi = makeRowBtn(indentSideIcon(false), "Indent Left +",  "Increase left indent");
    auto* rIndLd = makeRowBtn(indentIcon(false),     "Indent Left −",  "Decrease left indent");
    auto* rIndRi = makeRowBtn(indentSideIcon(true),  "Indent Right +", "Increase right indent");
    connect(rIndLi, &QToolButton::clicked, this, [this] { changeParagraphIndent(0, +24); });
    connect(rIndLd, &QToolButton::clicked, this, [this] { changeParagraphIndent(0, -24); });
    connect(rIndRi, &QToolButton::clicked, this, [this] { changeParagraphIndent(1, +24); });
    auto* indCol = new QWidget(tab);
    auto* ic = new QVBoxLayout(indCol); ic->setContentsMargins(0,0,0,0); ic->setSpacing(2);
    ic->addWidget(rIndLi); ic->addWidget(rIndLd); ic->addWidget(rIndRi);

    auto* rSpB = makeRowBtn(spacingIcon(true),  "Space Before +", "Add space before paragraph");
    auto* rSpA = makeRowBtn(spacingIcon(false), "Space After +",  "Add space after paragraph");
    auto* rIndRd = makeRowBtn(indentIcon(true), "Indent Right −",  "Decrease right indent");
    connect(rSpB, &QToolButton::clicked, this, [this] { changeParagraphSpacing(true,  +6); });
    connect(rSpA, &QToolButton::clicked, this, [this] { changeParagraphSpacing(false, +6); });
    connect(rIndRd, &QToolButton::clicked, this, [this] { changeParagraphIndent(1, -24); });
    auto* spCol = new QWidget(tab);
    auto* spc = new QVBoxLayout(spCol); spc->setContentsMargins(0,0,0,0); spc->setSpacing(2);
    spc->addWidget(rSpB); spc->addWidget(rSpA); spc->addWidget(rIndRd);

    layout->addWidget(makeGroup("Paragraph", { indCol, spCol }));
    layout->addStretch();
    scroll->setWidget(tab);
    return scroll;
}

// ─────────────────────────────────────────────────────────────────────────────
// References tab
// ─────────────────────────────────────────────────────────────────────────────
QWidget* WriterRibbon::buildReferencesTab() {
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("ribbonScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* tab = new QWidget(scroll);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2); layout->setSpacing(0);

    // ── Table of Contents ───────────────────────────────────────────────────
    auto* btnToc = makeBigBtn(tocIcon(), "Table of\nContents", "Build a table of contents from headings");
    btnToc->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(btnToc);
        m->addAction(tocIcon(), "Contents from Headings", this, &WriterRibbon::insertTableOfContents);
        m->addAction(tocIcon(), "Update / Rebuild",        this, &WriterRibbon::insertTableOfContents);
        btnToc->setMenu(m);
    }
    layout->addWidget(makeGroup("Table of Contents", { btnToc }));
    layout->addWidget(makeSeparator());

    // ── Footnotes & Endnotes ────────────────────────────────────────────────
    auto* btnFoot = makeBigBtn(footnoteIcon(), "Insert\nFootnote", "Insert a numbered footnote");
    auto* btnEnd  = makeBigBtn(endnoteIcon(),  "Insert\nEndnote",  "Insert an endnote");
    connect(btnFoot, &QToolButton::clicked, this, &WriterRibbon::insertFootnote);
    connect(btnEnd,  &QToolButton::clicked, this, &WriterRibbon::insertEndnote);
    auto* rNextNote = makeRowBtn(navArrowIcon(true),  "Next Note",     "Jump to next note marker");
    auto* rPrevNote = makeRowBtn(navArrowIcon(false), "Previous Note", "Jump to previous note marker");
    connect(rNextNote, &QToolButton::clicked, this, [this] { gotoComment(true); });
    connect(rPrevNote, &QToolButton::clicked, this, [this] { gotoComment(false); });
    auto* noteNav = new QWidget(tab);
    auto* nnl = new QVBoxLayout(noteNav); nnl->setContentsMargins(0,0,0,0); nnl->setSpacing(2);
    nnl->addWidget(rNextNote); nnl->addWidget(rPrevNote);
    layout->addWidget(makeGroup("Footnotes", { btnFoot, btnEnd, noteNav }));
    layout->addWidget(makeSeparator());

    // ── Citations & Bibliography ────────────────────────────────────────────
    auto* btnCite = makeBigBtn(citationIcon(), "Citation", "Insert a citation");
    btnCite->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(btnCite);
        m->addAction(citationIcon(), "(Author, Year)",        this, [this] { insertCitation("(Author, 2026)"); });
        m->addAction(citationIcon(), "[1] numbered",          this, [this] { insertCitation("[1]"); });
        m->addAction(citationIcon(), "(Author, Year, p. #)",  this, [this] { insertCitation("(Author, 2026, p. 1)"); });
        btnCite->setMenu(m);
    }
    auto* btnBib = makeBigBtn(bibliographyIcon(), "Bibliography", "Insert a bibliography section");
    connect(btnBib, &QToolButton::clicked, this, &WriterRibbon::insertBibliography);
    layout->addWidget(makeGroup("Citations & Bibliography", { btnCite, btnBib }));
    layout->addWidget(makeSeparator());

    // ── Captions ────────────────────────────────────────────────────────────
    auto* btnCap = makeBigBtn(captionIcon(), "Insert\nCaption", "Insert a caption");
    btnCap->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(btnCap);
        m->addAction(captionIcon(), "Figure",   this, [this] { insertCaption("Figure"); });
        m->addAction(captionIcon(), "Table",     this, [this] { insertCaption("Table"); });
        m->addAction(captionIcon(), "Equation",  this, [this] { insertCaption("Equation"); });
        btnCap->setMenu(m);
    }
    auto* rTof   = makeRowBtn(tofIcon(),      "Table of Figures", "Insert a list of captions");
    auto* rXref  = makeRowBtn(crossRefIcon(), "Cross-reference",  "Insert a cross-reference");
    auto* rUpd   = makeRowBtn(crossRefIcon(), "Update Fields",    "Renumber captions and refresh fields");
    connect(rTof,  &QToolButton::clicked, this, &WriterRibbon::insertTableOfFigures);
    connect(rXref, &QToolButton::clicked, this, &WriterRibbon::insertCrossReference);
    connect(rUpd,  &QToolButton::clicked, this, &WriterRibbon::updateFields);
    auto* capCol = new QWidget(tab);
    auto* cpl = new QVBoxLayout(capCol); cpl->setContentsMargins(0,0,0,0); cpl->setSpacing(2);
    cpl->addWidget(rTof); cpl->addWidget(rXref); cpl->addWidget(rUpd);
    layout->addWidget(makeGroup("Captions", { btnCap, capCol }));
    layout->addWidget(makeSeparator());

    // ── Index ───────────────────────────────────────────────────────────────
    auto* btnMark = makeBigBtn(markEntryIcon(), "Mark\nEntry", "Mark the selected text for the index");
    auto* btnIndex = makeBigBtn(indexIcon(), "Insert\nIndex", "Build an index from marked entries");
    connect(btnMark,  &QToolButton::clicked, this, &WriterRibbon::markIndexEntry);
    connect(btnIndex, &QToolButton::clicked, this, &WriterRibbon::insertIndex);
    layout->addWidget(makeGroup("Index", { btnMark, btnIndex }));
    layout->addWidget(makeSeparator());

    // ── Mail Merge ──────────────────────────────────────────────────────────
    auto* btnMerge = makeBigBtn(mailMergeIcon(), "Mail\nMerge", "Merge a CSV data source into the document");
    connect(btnMerge, &QToolButton::clicked, this, &WriterRibbon::showMailMerge);
    layout->addWidget(makeGroup("Mailings", { btnMerge }));
    layout->addStretch();
    scroll->setWidget(tab);
    return scroll;
}

// ─────────────────────────────────────────────────────────────────────────────
// Review tab
// ─────────────────────────────────────────────────────────────────────────────
QWidget* WriterRibbon::buildReviewTab() {
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("ribbonScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* tab = new QWidget(scroll);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2); layout->setSpacing(0);

    // ── Proofing ────────────────────────────────────────────────────────────
    auto* btnSpell = makeBigBtn(spellingIcon(), "Spelling &\nGrammar", "Check spelling and grammar");
    auto* btnWc    = makeBigBtn(wordCountIcon(), "Word\nCount", "Show document word count");
    connect(btnSpell, &QToolButton::clicked, this, &WriterRibbon::showSpellingDialog);
    connect(btnWc,    &QToolButton::clicked, this, &WriterRibbon::showWordCountDialog);
    layout->addWidget(makeGroup("Proofing", { btnSpell, btnWc }));
    layout->addWidget(makeSeparator());

    // ── Comments ────────────────────────────────────────────────────────────
    auto* btnComment = makeBigBtn(commentInsIcon(), "New\nComment", "Insert a comment");
    connect(btnComment, &QToolButton::clicked, this, &WriterRibbon::insertComment);
    auto* rDel    = makeRowBtn(deleteCommentIcon(), "Delete",      "Delete the next comment");
    auto* rDelAll = makeRowBtn(deleteCommentIcon(), "Delete All",  "Delete all comments");
    connect(rDel,    &QToolButton::clicked, this, [this] { deleteComments(false); });
    connect(rDelAll, &QToolButton::clicked, this, [this] { deleteComments(true); });
    auto* rNext = makeRowBtn(navArrowIcon(true),  "Next",     "Go to next comment");
    auto* rPrev = makeRowBtn(navArrowIcon(false), "Previous", "Go to previous comment");
    connect(rNext, &QToolButton::clicked, this, [this] { gotoComment(true); });
    connect(rPrev, &QToolButton::clicked, this, [this] { gotoComment(false); });
    auto* cDelCol = new QWidget(tab);
    auto* cdl = new QVBoxLayout(cDelCol); cdl->setContentsMargins(0,0,0,0); cdl->setSpacing(2);
    cdl->addWidget(rDel); cdl->addWidget(rDelAll);
    auto* cNavCol = new QWidget(tab);
    auto* cnl = new QVBoxLayout(cNavCol); cnl->setContentsMargins(0,0,0,0); cnl->setSpacing(2);
    cnl->addWidget(rNext); cnl->addWidget(rPrev);
    auto* btnCommentsPane = makeBigBtn(navPaneIcon(), "Comments\nPane", "Show all comments in a side pane", true);
    connect(btnCommentsPane, &QToolButton::toggled, this, [this](bool on){ emit commentsPaneToggled(on); });
    layout->addWidget(makeGroup("Comments", { btnComment, cDelCol, cNavCol, btnCommentsPane }));
    layout->addWidget(makeSeparator());

    // ── Tracking (real revisions: insertions green-underline, deletions red-strike)
    auto* btnTrack = makeBigBtn(trackChangesIcon(), "Track\nChanges",
                                "Record insertions and deletions for review", true);
    connect(btnTrack, &QToolButton::toggled, this, [this](bool on) {
        if (auto* paper = qobject_cast<PagedTextEdit*>(m_editor)) paper->setTrackChanges(on);
        if (m_editor) m_editor->setFocus();
    });
    auto* rAccept = makeRowBtn(acceptIcon(), "Accept All", "Accept all tracked edits");
    auto* rReject = makeRowBtn(rejectIcon(), "Reject All", "Reject all tracked edits");
    connect(rAccept, &QToolButton::clicked, this, [this] {
        if (auto* paper = qobject_cast<PagedTextEdit*>(m_editor)) paper->acceptAllChanges();
        m_editor->setFocus();
    });
    connect(rReject, &QToolButton::clicked, this, [this] {
        if (auto* paper = qobject_cast<PagedTextEdit*>(m_editor)) paper->rejectAllChanges();
        m_editor->setFocus();
    });
    auto* trkCol = new QWidget(tab);
    auto* tkl = new QVBoxLayout(trkCol); tkl->setContentsMargins(0,0,0,0); tkl->setSpacing(2);
    tkl->addWidget(rAccept); tkl->addWidget(rReject);
    layout->addWidget(makeGroup("Tracking", { btnTrack, trkCol }));
    layout->addWidget(makeSeparator());

    // ── Compare ─────────────────────────────────────────────────────────────
    auto* btnCompare = makeBigBtn(compareIcon(), "Compare", "Compare with another document");
    connect(btnCompare, &QToolButton::clicked, this, &WriterRibbon::showCompareDialog);
    layout->addWidget(makeGroup("Compare", { btnCompare }));
    layout->addWidget(makeSeparator());

    // ── Collaborate (Tier 5, LAN live share) ──────────────────────────────────
    auto* btnHost = makeBigBtn(collabIcon(), "Host\nLive Share", "Host a live collaboration session");
    connect(btnHost, &QToolButton::clicked, this, &WriterRibbon::collabHost);
    auto* rJoin = makeRowBtn(collabIcon(), "Join…", "Join a live session by host:port");
    auto* rLeave = makeRowBtn(collabIcon(), "Leave", "Leave the live session");
    connect(rJoin,  &QToolButton::clicked, this, &WriterRibbon::collabJoin);
    connect(rLeave, &QToolButton::clicked, this, &WriterRibbon::collabStop);
    m_collabStatus = new QLabel("Not connected", tab);
    m_collabStatus->setStyleSheet("color:#6B7280;font-size:11px;");
    m_collabStatus->setWordWrap(true);
    m_collabStatus->setFixedWidth(120);
    auto* collabCol = new QWidget(tab);
    auto* ccl = new QVBoxLayout(collabCol); ccl->setContentsMargins(0,0,0,0); ccl->setSpacing(2);
    ccl->addWidget(rJoin); ccl->addWidget(rLeave); ccl->addWidget(m_collabStatus);
    layout->addWidget(makeGroup("Collaborate", { btnHost, collabCol }));
    layout->addWidget(makeSeparator());

    // ── Protect ─────────────────────────────────────────────────────────────
    auto* btnRestrict = makeBigBtn(restrictIcon(), "Restrict\nEditing", "Make the document read-only", true);
    connect(btnRestrict, &QToolButton::toggled, this, [this](bool on) { setReadOnly(on); });
    layout->addWidget(makeGroup("Protect", { btnRestrict }));
    layout->addStretch();
    scroll->setWidget(tab);
    return scroll;
}

// ─────────────────────────────────────────────────────────────────────────────
// View tab
// ─────────────────────────────────────────────────────────────────────────────
QWidget* WriterRibbon::buildViewTab() {
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("ribbonScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* tab = new QWidget(scroll);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2); layout->setSpacing(0);

    // ── Document Views ──────────────────────────────────────────────────────
    auto* btnFull  = makeBigBtn(fullScreenIcon(), "Full\nScreen", "Toggle full screen (Esc to exit)", true);
    auto* btnRead  = makeBigBtn(readModeIcon(), "Read\nMode", "Read-only reading mode", true);
    connect(btnFull, &QToolButton::toggled, this, [this](bool) { toggleFullScreen(); });
    connect(btnRead, &QToolButton::toggled, this, [this](bool on) { setReadOnly(on); });

    m_btnFocus = makeBigBtn(focusModeIcon(), "Focus\nMode", kFocusModeTip, true);
    connect(m_btnFocus, &QToolButton::toggled, this,
            [this](bool on) { emit focusModeRequested(on); });

    auto* btnPrintView = makeBigBtn(layoutViewIcon(false), "Print\nLayout", "Paged print layout", true);
    auto* btnWebView   = makeBigBtn(layoutViewIcon(true),  "Web\nLayout", "Full-width web layout", true);
    btnPrintView->setChecked(true);
    auto* viewGroup = new QButtonGroup(this);
    viewGroup->setExclusive(true);
    viewGroup->addButton(btnPrintView); viewGroup->addButton(btnWebView);
    connect(btnPrintView, &QToolButton::clicked, this, [this] { emit webLayoutRequested(false); });
    connect(btnWebView,   &QToolButton::clicked, this, [this] { emit webLayoutRequested(true); });
    layout->addWidget(makeGroup("Views", { btnFull, btnRead, m_btnFocus, btnPrintView, btnWebView }));
    layout->addWidget(makeSeparator());

    // ── Show ────────────────────────────────────────────────────────────────
    auto* btnMarksView = makeBigBtn(marksIcon(), "Formatting\nMarks", "Show or hide formatting marks", true);
    connect(btnMarksView, &QToolButton::toggled, this, [this](bool c) {
        toggleFormattingMarks(c);
        if (m_btnMarks) { QSignalBlocker b(m_btnMarks); m_btnMarks->setChecked(c); }
    });
    auto* btnEye = makeBigBtn(eyeProtectIcon(), "Eye\nProtection", "Soft page tints to reduce eye strain");
    btnEye->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(btnEye);
        m->addAction(eyeProtectIcon(), "Off (White)", this, [this] { emit pageColorRequested(QColor("#FFFFFF")); });
        m->addAction(eyeProtectIcon(), "Green",       this, [this] { emit pageColorRequested(QColor("#E5F1E4")); });
        m->addAction(eyeProtectIcon(), "Sepia",       this, [this] { emit pageColorRequested(QColor("#F4ECD8")); });
        m->addAction(eyeProtectIcon(), "Light Grey",  this, [this] { emit pageColorRequested(QColor("#ECECEC")); });
        btnEye->setMenu(m);
    }
    auto* btnRuler = makeBigBtn(rulerIcon(), "Ruler", "Show or hide the rulers", true);
    btnRuler->setChecked(true);
    connect(btnRuler, &QToolButton::toggled, this, [this](bool on) { emit rulerToggled(on); });
    auto* btnNav = makeBigBtn(navPaneIcon(), "Navigation\nPane", "Show the heading outline", true);
    connect(btnNav, &QToolButton::toggled, this, [this](bool on) { emit navPaneToggled(on); });
    layout->addWidget(makeGroup("Show", { btnMarksView, btnEye, btnRuler, btnNav }));
    layout->addWidget(makeSeparator());

    // ── Zoom ────────────────────────────────────────────────────────────────
    auto* btnZoomIn  = makeBigBtn(zoomIcon(0), "Zoom\nIn",  "Zoom in");
    auto* btnZoomOut = makeBigBtn(zoomIcon(1), "Zoom\nOut", "Zoom out");
    auto* btnZoom100 = makeBigBtn(zoomIcon(2), "100%", "Reset zoom to 100%");
    auto* btnPageWidth = makeBigBtn(pageWidthIcon(), "Page\nWidth", "Fit the page to the window width");
    connect(btnZoomIn,  &QToolButton::clicked, this, &WriterRibbon::zoomInRequested);
    connect(btnZoomOut, &QToolButton::clicked, this, &WriterRibbon::zoomOutRequested);
    connect(btnZoom100, &QToolButton::clicked, this, &WriterRibbon::zoomResetRequested);
    connect(btnPageWidth, &QToolButton::clicked, this, [this] { emit webLayoutRequested(true); });
    layout->addWidget(makeGroup("Zoom", { btnZoomIn, btnZoomOut, btnZoom100, btnPageWidth }));
    layout->addStretch();
    scroll->setWidget(tab);
    return scroll;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tools tab
// ─────────────────────────────────────────────────────────────────────────────
QWidget* WriterRibbon::buildToolsTab() {
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("ribbonScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* tab = new QWidget(scroll);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2); layout->setSpacing(0);

    // ── Word documents ──────────────────────────────────────────────────────
    auto* btnOpenW = makeBigBtn(wordIcon(), "Open\nWord", "Open a Microsoft Word .docx file");
    auto* btnSaveW = makeBigBtn(wordIcon(), "Save as\nWord", "Save this document as a Word .docx file");
    connect(btnOpenW, &QToolButton::clicked, this, &WriterRibbon::importFromWord);
    connect(btnSaveW, &QToolButton::clicked, this, &WriterRibbon::exportToWord);
    layout->addWidget(makeGroup("Word (.docx)", { btnOpenW, btnSaveW }));
    layout->addWidget(makeSeparator());

    // ── Print ─────────────────────────────────────────────────────────────────
    auto* btnPrint   = makeBigBtn(printIcon(), "Print", "Print the document (Ctrl+P)");
    auto* btnPreview = makeBigBtn(printIcon(), "Print\nPreview", "Preview before printing");
    connect(btnPrint,   &QToolButton::clicked, this, &WriterRibbon::printDocument);
    connect(btnPreview, &QToolButton::clicked, this, &WriterRibbon::printPreview);
    layout->addWidget(makeGroup("Print", { btnPrint, btnPreview }));
    layout->addWidget(makeSeparator());

    // ── Export ──────────────────────────────────────────────────────────────
    auto* btnPdf  = makeBigBtn(exportPdfIcon(), "Export to\nPDF", "Export the document as a PDF");
    auto* btnPic  = makeBigBtn(exportPicIcon(), "Export to\nPicture", "Export the document as a PNG image");
    auto* btnText = makeBigBtn(extractTextIcon(), "Extract\nText", "Save the plain text to a .txt file");
    connect(btnPdf,  &QToolButton::clicked, this, &WriterRibbon::exportToPdf);
    connect(btnPic,  &QToolButton::clicked, this, &WriterRibbon::exportToPicture);
    connect(btnText, &QToolButton::clicked, this, &WriterRibbon::exportToText);
    layout->addWidget(makeGroup("Export", { btnPdf, btnPic, btnText }));
    layout->addWidget(makeSeparator());

    // ── Proofing ────────────────────────────────────────────────────────────
    auto* btnStats = makeBigBtn(statsIcon(), "Word\nCount", "Document statistics");
    auto* btnSpell = makeBigBtn(spellingIcon(), "Spell\nCheck", "Check spelling and grammar");
    auto* btnAuto  = makeBigBtn(autoCorrectIcon(), "AutoCorrect\nOptions", "Configure as-you-type corrections");
    connect(btnStats, &QToolButton::clicked, this, &WriterRibbon::showWordCountDialog);
    connect(btnSpell, &QToolButton::clicked, this, &WriterRibbon::showSpellingDialog);
    connect(btnAuto,  &QToolButton::clicked, this, &WriterRibbon::showAutoCorrectOptions);
    layout->addWidget(makeGroup("Proofing", { btnStats, btnSpell, btnAuto }));
    layout->addWidget(makeSeparator());

    auto* btnFind  = makeBigBtn(findIcon(), "Find &\nReplace", "Find and replace (Ctrl+F)");
    connect(btnFind, &QToolButton::clicked, this, &WriterRibbon::openFindReplace);

    auto* btnCase = makeBigBtn(caseIcon(), "Change\nCase", "Change letter case");
    btnCase->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(btnCase);
        m->addAction(caseIcon(), "UPPERCASE",     this, [this] { applyChangeCase(0); });
        m->addAction(caseIcon(), "lowercase",     this, [this] { applyChangeCase(1); });
        m->addAction(caseIcon(), "Title Case",    this, [this] { applyChangeCase(2); });
        m->addAction(caseIcon(), "Sentence case", this, [this] { applyChangeCase(3); });
        m->addAction(caseIcon(), "tOGGLE cASE",   this, [this] { applyChangeCase(4); });
        btnCase->setMenu(m);
    }

    auto* btnClear = makeBigBtn(clearFmtIcon(), "Clear\nFormatting", "Remove all formatting");
    connect(btnClear, &QToolButton::clicked, this, &WriterRibbon::clearFormatting);
    layout->addWidget(makeGroup("Editing", { btnFind, btnCase, btnClear }));
    layout->addWidget(makeSeparator());

    auto* btnDate = makeBigBtn(insDateTimeIcon(), "Date &\nTime", "Insert the date and time");
    btnDate->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(btnDate);
        const QDateTime now = QDateTime::currentDateTime();
        for (const QString& f : { "dddd, MMMM d, yyyy", "MMMM d, yyyy", "dd/MM/yyyy",
                                   "hh:mm AP", "dd/MM/yyyy hh:mm AP" }) {
            const QString text = now.toString(f);
            m->addAction(insDateTimeIcon(), text, this, [this, text] { insertDateTimeText(text); });
        }
        btnDate->setMenu(m);
    }
    layout->addWidget(makeGroup("Insert", { btnDate }));
    layout->addStretch();
    scroll->setWidget(tab);
    return scroll;
}

// ─────────────────────────────────────────────────────────────────────────────
// Table tab — contextual; shown only when the cursor is inside a table.
// All actions route through TableOps (shared with the right-click menu).
// ─────────────────────────────────────────────────────────────────────────────
QWidget* WriterRibbon::buildTableTab() {
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("ribbonScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* tab = new QWidget(scroll);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2); layout->setSpacing(0);

    // ── Rows & Columns ──────────────────────────────────────────────────────
    auto* bRowA = makeBigBtn(tblInsRow(false), "Insert\nAbove", "Insert a row above");
    auto* bRowB = makeBigBtn(tblInsRow(true),  "Insert\nBelow", "Insert a row below");
    auto* bColL = makeBigBtn(tblInsCol(false), "Insert\nLeft",  "Insert a column to the left");
    auto* bColR = makeBigBtn(tblInsCol(true),  "Insert\nRight", "Insert a column to the right");
    connect(bRowA, &QToolButton::clicked, this, [this]{ TableOps::insertRow(m_editor, false);  m_editor->setFocus(); });
    connect(bRowB, &QToolButton::clicked, this, [this]{ TableOps::insertRow(m_editor, true);   m_editor->setFocus(); });
    connect(bColL, &QToolButton::clicked, this, [this]{ TableOps::insertColumn(m_editor, false); m_editor->setFocus(); });
    connect(bColR, &QToolButton::clicked, this, [this]{ TableOps::insertColumn(m_editor, true);  m_editor->setFocus(); });

    auto* bDelete = makeBigBtn(tblDelete(), "Delete", "Delete rows, columns or the table");
    bDelete->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(bDelete);
        m->addAction(tblDelete(), "Delete Row",    this, [this]{ TableOps::deleteRow(m_editor); m_editor->setFocus(); });
        m->addAction(tblDelete(), "Delete Column", this, [this]{ TableOps::deleteColumn(m_editor); m_editor->setFocus(); });
        m->addSeparator();
        m->addAction(tblDelete(), "Delete Table",  this, [this]{ TableOps::deleteTable(m_editor); refreshTableTab(); });
        bDelete->setMenu(m);
    }
    layout->addWidget(makeGroup("Rows & Columns", { bRowA, bRowB, bColL, bColR, bDelete }));
    layout->addWidget(makeSeparator());

    // ── Merge ───────────────────────────────────────────────────────────────
    auto* bMerge = makeBigBtn(tblMerge(), "Merge\nCells", "Merge the selected cells");
    auto* bSplit = makeBigBtn(tblSplit(), "Split\nCell",  "Split a merged cell");
    connect(bMerge, &QToolButton::clicked, this, [this]{ TableOps::mergeCells(m_editor); m_editor->setFocus(); });
    connect(bSplit, &QToolButton::clicked, this, [this]{ TableOps::splitCell(m_editor);  m_editor->setFocus(); });
    layout->addWidget(makeGroup("Merge", { bMerge, bSplit }));
    layout->addWidget(makeSeparator());

    // ── Cell ────────────────────────────────────────────────────────────────
    auto* bShade = makeBigBtn(tblShade(), "Shading", "Cell background colour");
    bShade->setPopupMode(QToolButton::InstantPopup);
    {
        auto* menu = new QMenu(bShade);
        auto* grid = new QWidget(menu);
        auto* gl = new QGridLayout(grid);
        gl->setContentsMargins(8, 8, 8, 8); gl->setSpacing(4);
        const char* pal[] = { "#FFFFFF","#F1F3F7","#E5E7EB","#FEF3C7","#DCFCE7","#DBEAFE",
                               "#FCE7F3","#2C3140","#E8372A","#16A34A","#2563EB","#EA580C" };
        int i = 0;
        for (const char* hex : pal) {
            auto* sw = new QToolButton(grid);
            sw->setFixedSize(22, 22); sw->setCursor(Qt::PointingHandCursor); sw->setToolTip(hex);
            sw->setStyleSheet(QString("QToolButton{background:%1;border:1px solid #C6CAD3;border-radius:3px;}"
                                      "QToolButton:hover{border:2px solid #6D5BE8;}").arg(hex));
            const QColor c(hex);
            connect(sw, &QToolButton::clicked, this, [this, c, menu]{ TableOps::setCellShading(m_editor, c); menu->hide(); m_editor->setFocus(); });
            gl->addWidget(sw, i / 6, i % 6); ++i;
        }
        auto* wa = new QWidgetAction(menu); wa->setDefaultWidget(grid);
        menu->addAction(wa);
        menu->addAction(tblShade(), "No Shading", this, [this]{ TableOps::setCellShading(m_editor, Qt::transparent); m_editor->setFocus(); });
        bShade->setMenu(menu);
    }

    auto* bBorders = makeBigBtn(tblBorders(), "Borders", "Cell borders");
    bBorders->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(bBorders);
        m->addAction(tblBorders(), "All Borders", this, [this]{ TableOps::setCellBorders(m_editor, 1); m_editor->setFocus(); });
        m->addAction(tblBorders(), "No Borders",  this, [this]{ TableOps::setCellBorders(m_editor, 0); m_editor->setFocus(); });
        bBorders->setMenu(m);
    }

    auto* bAlign = makeBigBtn(tblAlign(0), "Align", "Cell content alignment");
    bAlign->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(bAlign);
        m->addAction(tblAlign(0), "Left",   this, [this]{ TableOps::setCellAlignment(m_editor, Qt::AlignLeft);    m_editor->setFocus(); });
        m->addAction(tblAlign(1), "Center", this, [this]{ TableOps::setCellAlignment(m_editor, Qt::AlignHCenter); m_editor->setFocus(); });
        m->addAction(tblAlign(2), "Right",  this, [this]{ TableOps::setCellAlignment(m_editor, Qt::AlignRight);   m_editor->setFocus(); });
        bAlign->setMenu(m);
    }
    layout->addWidget(makeGroup("Cell", { bShade, bBorders, bAlign }));
    layout->addWidget(makeSeparator());

    // ── Table Styles ────────────────────────────────────────────────────────
    auto* bHeader = makeBigBtn(tblHeader(), "Header\nRow", "Toggle a styled header row", true);
    connect(bHeader, &QToolButton::toggled, this, [this](bool on){ TableOps::setHeaderRow(m_editor, on); m_editor->setFocus(); });

    auto* bStGrid   = makeBigBtn(tblStyle(0), "Grid",   "Plain grid");
    auto* bStHeader = makeBigBtn(tblStyle(1), "Header", "Shaded header row");
    auto* bStBand   = makeBigBtn(tblStyle(2), "Banded", "Banded rows");
    connect(bStGrid,   &QToolButton::clicked, this, [this]{ TableOps::applyTableStyle(m_editor, 0); m_editor->setFocus(); });
    connect(bStHeader, &QToolButton::clicked, this, [this]{ TableOps::applyTableStyle(m_editor, 1); m_editor->setFocus(); });
    connect(bStBand,   &QToolButton::clicked, this, [this]{ TableOps::applyTableStyle(m_editor, 3); m_editor->setFocus(); });
    layout->addWidget(makeGroup("Table Styles", { bHeader, bStGrid, bStHeader, bStBand }));
    layout->addStretch();
    scroll->setWidget(tab);
    return scroll;
}

void WriterRibbon::ensureTabBuilt(int id) {
    if (id < 0 || id >= m_tabBuilt.size() || m_tabBuilt[id]) return;
    QWidget* built = nullptr;
    switch (id) {
    case 1: built = buildInsertTab();     break;
    case 2: built = buildPageLayoutTab(); break;
    case 3: built = buildReferencesTab(); break;
    case 4: built = buildReviewTab();     break;
    case 5: built = buildViewTab();       break;
    case 6: built = buildToolsTab();      break;
    case 7: built = buildTableTab();      break;
    default: return;
    }
    QWidget* placeholder = m_stack->widget(id);
    m_stack->removeWidget(placeholder);
    placeholder->deleteLater();
    m_stack->insertWidget(id, built);
    m_tabBuilt[id] = true;
}

void WriterRibbon::refreshTableTab() {
    const bool inTbl = m_editor && m_editor->textCursor().currentTable() != nullptr;
    if (m_tableTabBtn) m_tableTabBtn->setVisible(inTbl);
    if (!inTbl && m_stack && m_stack->currentIndex() == 7) {
        if (auto* b = m_tabGroup->button(0)) b->setChecked(true);
        m_stack->setCurrentIndex(0);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// References / Review / Tools actions
// ─────────────────────────────────────────────────────────────────────────────
void WriterRibbon::changeParagraphIndent(int side, double deltaPx) {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    QTextBlockFormat bf = cur.blockFormat();
    if (side == 0) bf.setLeftMargin(qMax(0.0, bf.leftMargin() + deltaPx));
    else           bf.setRightMargin(qMax(0.0, bf.rightMargin() + deltaPx));
    cur.mergeBlockFormat(bf);
    m_editor->setFocus();
}

void WriterRibbon::changeParagraphSpacing(bool before, double deltaPx) {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    QTextBlockFormat bf = cur.blockFormat();
    if (before) bf.setTopMargin(qMax(0.0, bf.topMargin() + deltaPx));
    else        bf.setBottomMargin(qMax(0.0, bf.bottomMargin() + deltaPx));
    cur.mergeBlockFormat(bf);
    m_editor->setFocus();
}

void WriterRibbon::insertTableOfContents() {
    if (!m_editor) return;
    QTextDocument* doc = m_editor->document();

    // Collect headings (paragraphs tagged with a Heading style).
    struct Entry { QString text; int level; };
    QList<Entry> entries;
    for (QTextBlock b = doc->begin(); b != doc->end(); b = b.next()) {
        const QTextBlockFormat bf = b.blockFormat();
        // Prefer the heading level (round-trips natively via HTML <h1>..<h3>);
        // fall back to the style-name tag from the manager.
        int level = bf.headingLevel();
        if (level < 1 || level > 3) {
            const QString nm = bf.stringProperty(kStyleNameProp);
            if (const WriterStyleDef* d = m_styles ? m_styles->find(nm) : nullptr)
                level = (d->headingLevel >= 1 && d->headingLevel <= 3) ? d->headingLevel : 0;
            else
                level = 0;
        }
        if (level >= 1 && level <= 3 && !b.text().trimmed().isEmpty())
            entries.append({ b.text().trimmed(), level });
    }

    QTextCursor cur = m_editor->textCursor();
    cur.beginEditBlock();

    QTextCharFormat title;
    title.setFontPointSize(18); title.setFontWeight(QFont::Bold);
    QTextBlockFormat tb; tb.setBottomMargin(8);
    cur.insertBlock(tb, title);
    cur.insertText("Contents", title);

    if (entries.isEmpty()) {
        QTextCharFormat note; note.setForeground(QColor("#6B7280")); note.setFontItalic(true);
        QTextBlockFormat nb;
        cur.insertBlock(nb, note);
        cur.insertText("No headings found. Apply Heading 1–3 styles to build a contents list.", note);
    } else {
        for (const Entry& e : entries) {
            QTextBlockFormat eb;
            eb.setLeftMargin(20 * (e.level - 1));
            eb.setTopMargin(2);
            QTextCharFormat ef;
            ef.setForeground(QColor("#1C1E26"));
            ef.setFontWeight(e.level == 1 ? QFont::Bold : QFont::Normal);
            cur.insertBlock(eb, ef);
            cur.insertText(e.text, ef);
        }
    }
    QTextBlockFormat after;
    cur.insertBlock(after, QTextCharFormat());
    cur.endEditBlock();
    m_editor->setFocus();
}

void WriterRibbon::insertFootnote() {
    if (!m_editor) return;
    QTextDocument* doc = m_editor->document();

    // Count existing markers to pick the next number.
    static const QRegularExpression re("\\[\\^(\\d+)\\]");
    int next = 1;
    for (QTextBlock b = doc->begin(); b != doc->end(); b = b.next()) {
        auto it = re.globalMatch(b.text());
        while (it.hasNext()) { const int n = it.next().captured(1).toInt(); if (n >= next) next = n + 1; }
    }

    QTextCursor cur = m_editor->textCursor();
    cur.beginEditBlock();
    // superscript marker at the cursor
    QTextCharFormat sup;
    sup.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    sup.setForeground(QColor("#2563EB"));
    cur.insertText(QString("[^%1]").arg(next), sup);

    // footnote text at end of document
    QTextCursor end(doc); end.movePosition(QTextCursor::End);
    QTextCharFormat normal; normal.setVerticalAlignment(QTextCharFormat::AlignNormal);
    QTextCharFormat note; note.setFontPointSize(9); note.setForeground(QColor("#4B5563"));
    QTextBlockFormat nb; nb.setTopMargin(4);
    end.insertBlock(nb, note);
    end.insertText(QString("[^%1] Footnote text.").arg(next), note);
    cur.endEditBlock();

    m_editor->setCurrentCharFormat(normal);
    m_editor->setFocus();
}

void WriterRibbon::insertCitation(const QString& text) {
    if (!m_editor) return;
    m_editor->textCursor().insertText(" " + text + " ");
    m_editor->setFocus();
}

void WriterRibbon::insertBibliography() {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    cur.movePosition(QTextCursor::End);
    cur.beginEditBlock();
    QTextCharFormat title; title.setFontPointSize(18); title.setFontWeight(QFont::Bold);
    QTextBlockFormat tb; tb.setTopMargin(16); tb.setBottomMargin(8);
    cur.insertBlock(tb, title);
    cur.insertText("Bibliography", title);

    QTextCharFormat ef; ef.setForeground(QColor("#1C1E26"));
    QTextBlockFormat eb; eb.setLeftMargin(24); eb.setTextIndent(-24); eb.setTopMargin(4);
    const char* entries[] = {
        "Author, A. (2026). Title of the work. Publisher.",
        "Writer, B. (2025). Another reference. Journal, 12(3), 45–67.",
    };
    for (const char* e : entries) {
        cur.insertBlock(eb, ef);
        cur.insertText(QString::fromUtf8(e), ef);
    }
    cur.endEditBlock();
    m_editor->setFocus();
}

void WriterRibbon::insertCaption(const QString& kind) {
    if (!m_editor) return;
    bool ok = false;
    const QString text = QInputDialog::getText(window(), "Insert Caption",
                            kind + " caption:", QLineEdit::Normal, QString(), &ok);
    if (!ok) return;

    // Auto-number: count captions of this kind that precede the cursor.
    QTextCursor cur = m_editor->textCursor();
    int number = 1;
    for (QTextBlock b = m_editor->document()->begin(); b != m_editor->document()->end(); b = b.next()) {
        if (b.position() >= cur.position()) break;
        if (b.blockFormat().stringProperty(kCaptionKindProp) == kind) ++number;
    }

    cur.beginEditBlock();
    QTextCharFormat cf; cf.setFontPointSize(10); cf.setFontItalic(true);
    cf.setForeground(QColor("#4B5563"));
    QTextBlockFormat bf; bf.setAlignment(Qt::AlignHCenter); bf.setTopMargin(4); bf.setBottomMargin(8);
    bf.setProperty(kCaptionKindProp, kind);
    cur.insertBlock(bf, cf);
    QTextCharFormat lbl = cf; lbl.setFontWeight(QFont::Bold);
    cur.insertText(QString("%1 %2").arg(kind).arg(number), lbl);
    cur.insertText(text.isEmpty() ? "" : ": " + text, cf);
    cur.endEditBlock();
    m_editor->setFocus();
}

void WriterRibbon::updateFields() {
    if (!m_editor) return;
    QTextDocument* doc = m_editor->document();
    QHash<QString, int> counters;
    int renumbered = 0;

    QTextCursor edit(doc);
    edit.beginEditBlock();
    for (QTextBlock b = doc->begin(); b != doc->end(); b = b.next()) {
        const QString kind = b.blockFormat().stringProperty(kCaptionKindProp);
        if (kind.isEmpty()) continue;
        const int n = ++counters[kind];
        const QString prefix = kind + " ";
        const QString t = b.text();
        if (!t.startsWith(prefix)) continue;
        int ds = prefix.size(), de = ds;
        while (de < t.size() && t.at(de).isDigit()) ++de;
        if (de == ds) continue;
        const QString current = t.mid(ds, de - ds);
        if (current == QString::number(n)) continue;
        // Replace just the digits, keeping their (bold) character format.
        QTextCursor c(doc);
        c.setPosition(b.position() + ds);
        c.setPosition(b.position() + de, QTextCursor::KeepAnchor);
        const QTextCharFormat fmt = c.charFormat();
        c.insertText(QString::number(n), fmt);
        ++renumbered;
    }
    edit.endEditBlock();

    // Rebuild the navigation/outline by touching the document is automatic; tell
    // the user what happened.
    QMessageBox::information(window(), "Update Fields",
        renumbered > 0 ? QString("Updated %1 caption number(s).").arg(renumbered)
                       : "Fields are up to date.");
    m_editor->setFocus();
}

void WriterRibbon::insertComment() {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    if (!cur.hasSelection()) cur.select(QTextCursor::WordUnderCursor);
    if (!cur.hasSelection()) {
        QMessageBox::information(window(), "New Comment",
            "Select the text you want to comment on first.");
        return;
    }
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(window(), "New Comment",
                            "Comment:", QString(), &ok);
    if (!ok || text.trimmed().isEmpty()) return;

    // Anchor the comment to the selection: highlight it and stash the text on the
    // range's char format. The comments pane reads it back.
    QTextCharFormat cf;
    cf.setBackground(QColor("#FFF1A8"));
    cf.setProperty(kCommentProp, text);
    cur.mergeCharFormat(cf);
    emit commentsChanged();
    m_editor->setFocus();
}

void WriterRibbon::showWordCountDialog() {
    if (!m_editor) return;
    const QString plain = m_editor->document()->toPlainText();
    const int words = plain.trimmed().isEmpty()
                          ? 0 : static_cast<int>(plain.split(QRegularExpression("\\s+"),
                                                             Qt::SkipEmptyParts).size());
    const int chars = static_cast<int>(plain.length());
    const int charsNoSpace = static_cast<int>(QString(plain).remove(QRegularExpression("\\s")).length());
    const int paragraphs = m_editor->document()->blockCount();
    const int pages = qMax(1, static_cast<int>(std::ceil(m_editor->document()->size().height() / 1123.0)));

    QMessageBox box(window());
    box.setWindowTitle("Word Count");
    box.setIcon(QMessageBox::Information);
    box.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    box.setText(QString(
        "<table cellpadding='4'>"
        "<tr><td>Pages</td><td align='right'><b>%1</b></td></tr>"
        "<tr><td>Words</td><td align='right'><b>%2</b></td></tr>"
        "<tr><td>Characters (with spaces)</td><td align='right'><b>%3</b></td></tr>"
        "<tr><td>Characters (no spaces)</td><td align='right'><b>%4</b></td></tr>"
        "<tr><td>Paragraphs</td><td align='right'><b>%5</b></td></tr>"
        "</table>")
        .arg(pages).arg(words).arg(chars).arg(charsNoSpace).arg(paragraphs));
    box.exec();
}

void WriterRibbon::showSpellingDialog() {
    if (!m_editor) return;
    auto* sc = SpellChecker::instance();
    sc->ensureLoaded();
    auto* paper = qobject_cast<PagedTextEdit*>(m_editor);

    QDialog dlg(window());
    dlg.setWindowTitle("Spelling & Grammar");
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    dlg.setMinimumWidth(420);

    auto* root = new QVBoxLayout(&dlg);
    auto* lblWord = new QLabel(&dlg);
    lblWord->setTextFormat(Qt::RichText);
    auto* sugTitle = new QLabel("Suggestions:", &dlg);
    auto* sugList = new QListWidget(&dlg);
    sugList->setMinimumHeight(140);
    root->addWidget(lblWord);
    root->addWidget(sugTitle);
    root->addWidget(sugList);

    auto* btnRow = new QHBoxLayout;
    auto* btnIgnore    = new QPushButton("Ignore", &dlg);
    auto* btnIgnoreAll = new QPushButton("Ignore All", &dlg);
    auto* btnAdd       = new QPushButton("Add", &dlg);
    auto* btnChange    = new QPushButton("Change", &dlg);
    auto* btnChangeAll = new QPushButton("Change All", &dlg);
    auto* btnClose     = new QPushButton("Close", &dlg);
    for (auto* b : { btnIgnore, btnIgnoreAll, btnAdd, btnChange, btnChangeAll, btnClose })
        btnRow->addWidget(b);
    root->addLayout(btnRow);

    static const QRegularExpression wordRe("[A-Za-z][A-Za-z']*");
    int searchPos = 0;          // plain-text offset to resume scanning from
    QString curWord;
    int curStart = -1;

    auto selectRange = [this](int start, int len) {
        QTextCursor c(m_editor->document());
        c.setPosition(start);
        c.setPosition(start + len, QTextCursor::KeepAnchor);
        m_editor->setTextCursor(c);
    };

    auto finish = [&dlg, this] {
        QMessageBox done(window());
        done.setWindowTitle("Spelling & Grammar");
        done.setIcon(QMessageBox::Information);
        done.setStyleSheet(ThemeManager::inputDialogStyleSheet());
        done.setText("Spelling check complete.");
        done.exec();
        dlg.accept();
    };

    // Advance to the next misspelling from searchPos; false when none remain.
    auto findNext = [&]() -> bool {
        const QString text = m_editor->toPlainText();
        auto it = wordRe.globalMatch(text, searchPos);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            QString w = m.captured(0);
            while (w.endsWith('\'')) w.chop(1);       // trailing apostrophes
            if (w.size() < 2 || sc->isCorrect(w)) continue;
            curWord  = w;
            curStart = m.capturedStart();
            searchPos = m.capturedStart() + w.size();
            selectRange(curStart, w.size());
            lblWord->setText(QString("Not in dictionary:  <b style='color:#C0271C'>%1</b>")
                                 .arg(w.toHtmlEscaped()));
            sugList->clear();
            const QStringList sugg = sc->suggestions(w);
            if (sugg.isEmpty()) {
                auto* none = new QListWidgetItem("(no suggestions)", sugList);
                none->setFlags(none->flags() & ~Qt::ItemIsEnabled);
            } else {
                sugList->addItems(sugg);
                sugList->setCurrentRow(0);
            }
            return true;
        }
        return false;
    };

    auto next = [&]{ if (!findNext()) finish(); };

    connect(btnIgnore, &QPushButton::clicked, &dlg, [&]{ next(); });
    connect(btnIgnoreAll, &QPushButton::clicked, &dlg, [&]{
        sc->ignoreWord(curWord);
        if (paper) paper->rehighlightSpelling();
        next();
    });
    connect(btnAdd, &QPushButton::clicked, &dlg, [&]{
        sc->addToDictionary(curWord);
        if (paper) paper->rehighlightSpelling();
        next();
    });
    connect(btnChange, &QPushButton::clicked, &dlg, [&]{
        auto* item = sugList->currentItem();
        if (!item || !(item->flags() & Qt::ItemIsEnabled)) { next(); return; }
        const QString repl = item->text();
        selectRange(curStart, curWord.size());
        m_editor->textCursor().insertText(repl);
        searchPos = curStart + repl.size();
        next();
    });
    connect(btnChangeAll, &QPushButton::clicked, &dlg, [&]{
        auto* item = sugList->currentItem();
        if (!item || !(item->flags() & Qt::ItemIsEnabled)) { next(); return; }
        const QString repl = item->text();
        QTextDocument* doc = m_editor->document();
        QTextCursor edit(doc);
        edit.beginEditBlock();
        QTextCursor found = doc->find(curWord, curStart,
                                      QTextDocument::FindWholeWords
                                      | QTextDocument::FindCaseSensitively);
        int lastEnd = curStart;
        while (!found.isNull()) {
            lastEnd = found.selectionStart() + repl.size();
            found.insertText(repl);
            found = doc->find(curWord, found,
                              QTextDocument::FindWholeWords
                              | QTextDocument::FindCaseSensitively);
        }
        edit.endEditBlock();
        searchPos = lastEnd;
        next();
    });
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(sugList, &QListWidget::itemDoubleClicked, btnChange, &QPushButton::click);

    if (!findNext()) {
        QMessageBox box(window());
        box.setWindowTitle("Spelling & Grammar");
        box.setIcon(QMessageBox::Information);
        box.setStyleSheet(ThemeManager::inputDialogStyleSheet());
        box.setText("Spelling check complete.\nNo issues were found.");
        box.exec();
        return;
    }
    dlg.exec();
}

void WriterRibbon::showAutoCorrectOptions() {
    auto* paper = qobject_cast<PagedTextEdit*>(m_editor);
    if (!paper) return;
    AutoCorrectSettings s = paper->autoCorrectSettings();

    QDialog dlg(window());
    dlg.setWindowTitle("AutoCorrect Options");
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    auto* root = new QVBoxLayout(&dlg);

    auto* master = new QCheckBox("Enable AutoCorrect while typing", &dlg);
    master->setChecked(paper->autoCorrectEnabled());
    root->addWidget(master);

    auto* line = new QFrame(&dlg); line->setFrameShape(QFrame::HLine);
    root->addWidget(line);

    auto* cQuotes = new QCheckBox("Straight quotes with smart quotes (“ ”)", &dlg);
    auto* cCaps   = new QCheckBox("Capitalize first letter of sentences", &dlg);
    auto* cTwo    = new QCheckBox("Correct TWo INitial CApitals", &dlg);
    auto* cTypos  = new QCheckBox("Replace common typos (teh → the)", &dlg);
    auto* cDash   = new QCheckBox("Replace -- with em dash (—)", &dlg);
    cQuotes->setChecked(s.smartQuotes);
    cCaps->setChecked(s.capitalizeSentences);
    cTwo->setChecked(s.twoInitialCaps);
    cTypos->setChecked(s.replaceTypos);
    cDash->setChecked(s.smartDashes);
    for (QCheckBox* cb : { cQuotes, cCaps, cTwo, cTypos, cDash }) root->addWidget(cb);

    auto syncEnabled = [=]{
        for (QCheckBox* cb : { cQuotes, cCaps, cTwo, cTypos, cDash })
            cb->setEnabled(master->isChecked());
    };
    syncEnabled();
    connect(master, &QCheckBox::toggled, &dlg, syncEnabled);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    root->addWidget(bb);

    if (dlg.exec() != QDialog::Accepted) return;
    s.smartQuotes         = cQuotes->isChecked();
    s.capitalizeSentences = cCaps->isChecked();
    s.twoInitialCaps      = cTwo->isChecked();
    s.replaceTypos        = cTypos->isChecked();
    s.smartDashes         = cDash->isChecked();
    paper->setAutoCorrectSettings(s);
    paper->setAutoCorrectEnabled(master->isChecked());
    m_editor->setFocus();
}

void WriterRibbon::setTextDirection(bool rtl) {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    QTextBlockFormat bf = cur.blockFormat();
    bf.setLayoutDirection(rtl ? Qt::RightToLeft : Qt::LeftToRight);
    cur.mergeBlockFormat(bf);
    m_editor->setFocus();
}

void WriterRibbon::applyColumns(int n) {
    if (!m_editor || n < 1) return;

    if (n == 1) {
        // Return the column region to normal flow. This used to only show an
        // information box, so once you made two or three columns there was no
        // way back to one from the menu.
        QTextCursor cur = m_editor->textCursor();
        QTextTable* t = cur.currentTable();
        if (!t) {
            QMessageBox::information(window(), "Columns",
                "The cursor is not inside a column region. Put it in one, then "
                "choose One to merge the columns back into a single flow.");
            return;
        }

        // Read the columns out in reading order before dropping the table.
        QStringList parts;
        for (int r = 0; r < t->rows(); ++r) {
            for (int c = 0; c < t->columns(); ++c) {
                const QTextTableCell cell = t->cellAt(r, c);
                if (!cell.isValid()) continue;
                QTextCursor cc = cell.firstCursorPosition();
                cc.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
                const QString s = cc.selectedText();
                if (!s.trimmed().isEmpty()) parts << s;
            }
        }

        QTextCursor tc(m_editor->document());
        tc.beginEditBlock();
        // The table frame spans firstPosition()-1 .. lastPosition()+1; taking
        // that whole span out removes the table itself, not just its contents.
        tc.setPosition(t->firstPosition() - 1);
        tc.setPosition(t->lastPosition() + 1, QTextCursor::KeepAnchor);
        tc.removeSelectedText();

        bool first = true;
        for (const QString& part : parts) {
            // Cell text uses U+2029 between paragraphs.
            const QStringList paras = part.split(QChar(0x2029), Qt::SkipEmptyParts);
            for (const QString& p : paras) {
                if (!first) tc.insertBlock();
                tc.insertText(p);
                first = false;
            }
        }
        tc.endEditBlock();

        m_editor->setTextCursor(tc);
        m_editor->setFocus();
        return;
    }
    // Columns are implemented as a borderless full-width table — a real, usable
    // side-by-side layout (QTextEdit has no native newspaper-column flow).
    QTextCursor cur = m_editor->textCursor();
    QTextTableFormat tf;
    tf.setBorder(0);
    tf.setBorderStyle(QTextFrameFormat::BorderStyle_None);
    tf.setCellPadding(8);
    tf.setCellSpacing(0);
    tf.setWidth(QTextLength(QTextLength::PercentageLength, 100));
    QList<QTextLength> widths;
    for (int i = 0; i < n; ++i)
        widths << QTextLength(QTextLength::PercentageLength, 100.0 / n);
    tf.setColumnWidthConstraints(widths);
    QTextTable* t = cur.insertTable(1, n, tf);
    if (t) m_editor->setTextCursor(t->cellAt(0, 0).firstCursorPosition());
    m_editor->setFocus();
}

void WriterRibbon::applyPageBorders(int kind) {
    if (!m_editor) return;
    QTextFrameFormat ff = m_editor->document()->rootFrame()->frameFormat();
    if (kind == 0) {
        ff.setBorder(0);
    } else {
        ff.setBorder(kind == 2 ? 3 : 2);
        ff.setBorderBrush(QColor("#2C3140"));
        ff.setBorderStyle(kind == 2 ? QTextFrameFormat::BorderStyle_Outset
                                    : QTextFrameFormat::BorderStyle_Solid);
        ff.setPadding(6);
    }
    m_editor->document()->rootFrame()->setFrameFormat(ff);
    m_editor->setFocus();
}

void WriterRibbon::insertEndnote() {
    if (!m_editor) return;
    QTextDocument* doc = m_editor->document();
    static const QRegularExpression re("\\[end (\\d+)\\]");
    int next = 1;
    for (QTextBlock b = doc->begin(); b != doc->end(); b = b.next()) {
        auto it = re.globalMatch(b.text());
        while (it.hasNext()) { const int n = it.next().captured(1).toInt(); if (n >= next) next = n + 1; }
    }
    QTextCursor cur = m_editor->textCursor();
    cur.beginEditBlock();
    QTextCharFormat sup; sup.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    sup.setForeground(QColor("#7C3AED"));
    cur.insertText(QString("[end %1]").arg(next), sup);

    QTextCursor end(doc); end.movePosition(QTextCursor::End);
    QTextCharFormat note; note.setFontPointSize(9); note.setForeground(QColor("#4B5563"));
    QTextBlockFormat nb; nb.setTopMargin(4);
    end.insertBlock(nb, note);
    end.insertText(QString("Endnote %1: text.").arg(next), note);
    cur.endEditBlock();
    QTextCharFormat normal; normal.setVerticalAlignment(QTextCharFormat::AlignNormal);
    m_editor->setCurrentCharFormat(normal);
    m_editor->setFocus();
}

void WriterRibbon::insertTableOfFigures() {
    if (!m_editor) return;
    QTextDocument* doc = m_editor->document();
    static const QRegularExpression re("^(Figure|Table|Equation)\\s+\\d+");
    QStringList figs;
    for (QTextBlock b = doc->begin(); b != doc->end(); b = b.next()) {
        const QString t = b.text().trimmed();
        if (re.match(t).hasMatch()) figs << t;
    }
    QTextCursor cur = m_editor->textCursor();
    cur.beginEditBlock();
    QTextCharFormat title; title.setFontPointSize(16); title.setFontWeight(QFont::Bold);
    QTextBlockFormat tb; tb.setBottomMargin(6);
    cur.insertBlock(tb, title);
    cur.insertText("Table of Figures", title);
    QTextCharFormat ef; ef.setForeground(QColor("#1C1E26"));
    if (figs.isEmpty()) {
        QTextCharFormat note = ef; note.setFontItalic(true); note.setForeground(QColor("#6B7280"));
        QTextBlockFormat nb; cur.insertBlock(nb, note);
        cur.insertText("No captions found. Use References ▸ Caption first.", note);
    } else {
        for (const QString& f : figs) {
            QTextBlockFormat eb; eb.setTopMargin(2); cur.insertBlock(eb, ef);
            cur.insertText(f, ef);
        }
    }
    cur.insertBlock(QTextBlockFormat(), QTextCharFormat());
    cur.endEditBlock();
    m_editor->setFocus();
}

void WriterRibbon::insertCrossReference() {
    if (!m_editor) return;
    const QStringList marks = collectBookmarks(m_editor->document());
    if (marks.isEmpty()) {
        QMessageBox::information(window(), "Cross-reference",
            "No bookmarks found. Insert a bookmark first (Insert ▸ Bookmark), "
            "then reference it here.");
        return;
    }

    QDialog dlg(window());
    dlg.setWindowTitle("Cross-reference");
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    auto* form = new QFormLayout(&dlg);
    auto* combo = new QComboBox(&dlg); combo->addItems(marks);
    auto* textEdit = new QLineEdit(&dlg);
    textEdit->setPlaceholderText("Link text (defaults to the bookmark name)");
    form->addRow("Bookmark:", combo);
    form->addRow("Text:", textEdit);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString anchor = combo->currentText();
    QString label = textEdit->text().trimmed();
    if (label.isEmpty()) label = anchor;
    // A clickable internal link (Ctrl+click jumps to the bookmark).
    m_editor->textCursor().insertHtml(
        QStringLiteral("<a href=\"#%1\" style=\"color:#2563EB;text-decoration:underline;\">%2</a>&nbsp;")
            .arg(anchor.toHtmlEscaped(), label.toHtmlEscaped()));
    m_editor->setFocus();
}

void WriterRibbon::markIndexEntry() {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();

    // Tag the run itself rather than injecting marker text. The old version
    // inserted a literal "{index:<selected text>}" next to the selection, so
    // marking a paragraph duplicated that whole paragraph into the document,
    // and marking twice nested the markers.
    if (!cur.hasSelection()) cur.select(QTextCursor::WordUnderCursor);

    QString term = cur.selectedText().simplified();
    if (term.isEmpty()) {
        bool ok = false;
        term = QInputDialog::getText(window(), "Mark Index Entry", "Term:",
                                     QLineEdit::Normal, QString(), &ok).simplified();
        if (!ok || term.isEmpty()) return;
        // Nothing selected to tag, so anchor the entry at the caret.
        cur = m_editor->textCursor();
        cur.select(QTextCursor::WordUnderCursor);
        if (!cur.hasSelection()) {
            QMessageBox::information(window(), "Mark Entry",
                "Place the cursor in a word, or select some text, then mark it.");
            return;
        }
    }

    QTextCharFormat cf;
    cf.setProperty(kIndexTermProp, term);
    cf.setBackground(QColor("#E0E7FF"));      // visible cue that it is marked
    cur.mergeCharFormat(cf);

    m_editor->setFocus();
}

void WriterRibbon::insertIndex() {
    if (!m_editor) return;
    QTextDocument* doc = m_editor->document();
    QStringList terms;
    // Entries live on the character format of the marked run.
    for (QTextBlock b = doc->begin(); b != doc->end(); b = b.next()) {
        for (auto it = b.begin(); it != b.end(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.isValid()) continue;
            const QString t = f.charFormat().stringProperty(kIndexTermProp);
            if (!t.isEmpty()) terms << t;
        }
    }
    // Documents marked by an earlier build carry literal "{index:term}" text.
    // Keep reading those so an already-marked document still yields an index.
    static const QRegularExpression legacy("\\{index:([^}]+)\\}");
    for (QTextBlock b = doc->begin(); b != doc->end(); b = b.next()) {
        auto it = legacy.globalMatch(b.text());
        while (it.hasNext()) terms << it.next().captured(1);
    }
    terms.removeDuplicates();
    terms.sort(Qt::CaseInsensitive);
    QTextCursor cur = m_editor->textCursor();
    cur.movePosition(QTextCursor::End);
    cur.beginEditBlock();
    QTextCharFormat title; title.setFontPointSize(16); title.setFontWeight(QFont::Bold);
    QTextBlockFormat tb; tb.setTopMargin(14); tb.setBottomMargin(6);
    cur.insertBlock(tb, title);
    cur.insertText("Index", title);
    QTextCharFormat ef; ef.setForeground(QColor("#1C1E26"));
    if (terms.isEmpty()) {
        QTextCharFormat note = ef; note.setFontItalic(true); note.setForeground(QColor("#6B7280"));
        cur.insertBlock(QTextBlockFormat(), note);
        cur.insertText("No entries marked. Use References ▸ Mark Entry first.", note);
    } else {
        for (const QString& t : terms) {
            QTextBlockFormat eb; eb.setTopMargin(1); cur.insertBlock(eb, ef);
            cur.insertText(t, ef);
        }
    }
    cur.endEditBlock();
    m_editor->setFocus();
}

void WriterRibbon::deleteComments(bool all) {
    if (!m_editor) return;
    QTextDocument* doc = m_editor->document();
    // Clear the comment anchor (highlight + stored text) from commented runs.
    const int caret = m_editor->textCursor().position();
    QList<QPair<int,int>> ranges;
    for (QTextBlock b = doc->begin(); b != doc->end(); b = b.next()) {
        for (auto it = b.begin(); it != b.end(); ++it) {
            const QTextFragment f = it.fragment();
            if (f.isValid() && !f.charFormat().stringProperty(kCommentProp).isEmpty()) {
                const int s = f.position(), e = f.position() + f.length();
                if (all || (caret >= s && caret <= e)) ranges << qMakePair(s, e);
            }
        }
    }
    std::sort(ranges.begin(), ranges.end(),
              [](const QPair<int,int>& a, const QPair<int,int>& b){ return a.first > b.first; });
    QTextCursor cur(doc); cur.beginEditBlock();
    for (const auto& r : ranges) {
        cur.setPosition(r.first); cur.setPosition(r.second, QTextCursor::KeepAnchor);
        QTextCharFormat clr; clr.setBackground(Qt::transparent); clr.setProperty(kCommentProp, QString());
        cur.mergeCharFormat(clr);
        if (!all) break;
    }
    cur.endEditBlock();
    emit commentsChanged();
    m_editor->setFocus();
}

void WriterRibbon::gotoComment(bool next) {
    if (!m_editor) return;
    QTextDocument* doc = m_editor->document();
    const int caret = m_editor->textCursor().position();
    int bestPos = -1;
    for (QTextBlock b = doc->begin(); b != doc->end(); b = b.next()) {
        for (auto it = b.begin(); it != b.end(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.isValid() || f.charFormat().stringProperty(kCommentProp).isEmpty()) continue;
            const int pos = f.position();
            if (next) { if (pos > caret && (bestPos < 0 || pos < bestPos)) bestPos = pos; }
            else      { if (pos < caret && (bestPos < 0 || pos > bestPos)) bestPos = pos; }
        }
    }
    if (bestPos >= 0) {
        QTextCursor c(doc); c.setPosition(bestPos);
        m_editor->setTextCursor(c);
        m_editor->ensureCursorVisible();
    }
    m_editor->setFocus();
}

void WriterRibbon::acceptAllChanges() {
    if (auto* paper = qobject_cast<PagedTextEdit*>(m_editor)) paper->acceptAllChanges();
    if (m_editor) m_editor->setFocus();
}

void WriterRibbon::setReadOnly(bool on) {
    if (!m_editor) return;
    m_editor->setReadOnly(on);
    if (!on) m_editor->setFocus();
}

void WriterRibbon::setFocusModeChecked(bool on) {
    // Either copy may not exist yet — ribbon tabs are built lazily.
    for (QToolButton* b : { m_btnFocusHome, m_btnFocus }) {
        if (!b) continue;
        QSignalBlocker block(b);         // reflect state without re-emitting
        b->setChecked(on);
    }
}

void WriterRibbon::toggleFullScreen() {
    QWidget* w = window();
    if (!w) return;
    if (w->isFullScreen()) w->showNormal();
    else                   w->showFullScreen();
}

void WriterRibbon::exportToPdf() {
    if (!m_editor) return;
    const QString path = QFileDialog::getSaveFileName(window(), "Export to PDF",
                            QDir::homePath() + "/Document.pdf", "PDF Files (*.pdf)");
    if (path.isEmpty()) return;
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);
    writer.setResolution(NativeOffice::ExportPrefs::pdfExportDpi());
    m_editor->document()->print(&writer);
    NativeOffice::Watermark::stampIfRequired(path);
    QMessageBox::information(window(), "Export to PDF", "Exported to:\n" + path);
}

// ── Printing (Tier 4) ────────────────────────────────────────────────────────
void WriterRibbon::renderToPrinter(QPrinter* printer) {
    if (m_editor) m_editor->document()->print(printer);
}

void WriterRibbon::printDocument() {
    if (!m_editor) return;
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);
    QPrintDialog dlg(&printer, window());
    dlg.setWindowTitle("Print");
    if (dlg.exec() == QDialog::Accepted) renderToPrinter(&printer);
}

void WriterRibbon::printPreview() {
    if (!m_editor) return;
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);
    QPrintPreviewDialog preview(&printer, window());
    preview.setWindowTitle("Print Preview");
    preview.resize(900, 700);
    connect(&preview, &QPrintPreviewDialog::paintRequested, this, &WriterRibbon::renderToPrinter);
    preview.exec();
}

// ── Templates (Tier 4) ───────────────────────────────────────────────────────
void WriterRibbon::showTemplateGallery() {
    QDialog dlg(window());
    dlg.setWindowTitle("New from Template");
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    dlg.resize(360, 340);
    auto* root = new QVBoxLayout(&dlg);
    root->addWidget(new QLabel("Choose a template to start from:", &dlg));

    auto* listw = new QListWidget(&dlg);
    const char* names[] = { "Blank Document", "Letter", "Report", "Resume / CV", "Meeting Notes" };
    for (const char* n : names) listw->addItem(n);
    listw->setCurrentRow(0);
    root->addWidget(listw, 1);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    bb->button(QDialogButtonBox::Ok)->setText("Create");
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(listw, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);
    root->addWidget(bb);

    if (dlg.exec() == QDialog::Accepted) applyTemplate(listw->currentRow());
}

void WriterRibbon::applyTemplate(int id) {
    if (!m_editor) return;
    if (!m_editor->document()->toPlainText().trimmed().isEmpty()) {
        if (QMessageBox::question(window(), "New from Template",
                "This replaces the current document content. Continue?")
            != QMessageBox::Yes) return;
    }

    m_editor->clear();
    QTextCursor cur = m_editor->textCursor();
    cur.movePosition(QTextCursor::Start);
    cur.beginEditBlock();

    bool first = true;
    auto add = [&](const QString& text, const QString& style) {
        if (!first) cur.insertBlock();
        first = false;
        // A block inserted right after a bullet would inherit the list — drop it
        // so headings/body that follow a bullet group aren't bulleted too.
        if (QTextList* lst = cur.currentList()) lst->remove(cur.block());
        QTextBlockFormat plain = cur.blockFormat();
        plain.setObjectIndex(-1); plain.setIndent(0);
        cur.setBlockFormat(plain);
        cur.insertText(text);
        m_editor->setTextCursor(cur);
        applyStyleByName(style);
        cur = m_editor->textCursor();
    };
    auto bullet = [&](const QString& text) {
        add(text, "Normal");
        ListOps::applyList(m_editor, QTextListFormat::ListDisc);
        cur = m_editor->textCursor();
    };

    const QString today = QDateTime::currentDateTime().toString("MMMM d, yyyy");

    switch (id) {
    case 1: // Letter
        add("[Your Name]", "Normal");
        add("[Street Address]", "Normal");
        add("[City, State ZIP]", "Normal");
        add("", "Normal");
        add(today, "Normal");
        add("", "Normal");
        add("[Recipient Name]", "Normal");
        add("[Company / Address]", "Normal");
        add("", "Normal");
        add("Dear [Recipient],", "Normal");
        add("", "Normal");
        add("Write the body of your letter here. Keep it concise and to the point.", "Normal");
        add("", "Normal");
        add("Sincerely,", "Normal");
        add("[Your Name]", "Normal");
        break;
    case 2: // Report
        add("Report Title", "Title");
        add("Subtitle or Department", "Subtitle");
        add("Introduction", "Heading 1");
        add("Summarize the purpose and scope of this report.", "Normal");
        add("Background", "Heading 1");
        add("Provide relevant context and prior work.", "Normal");
        add("Findings", "Heading 1");
        add("Present your findings here.", "Normal");
        add("Conclusion", "Heading 1");
        add("State conclusions and recommendations.", "Normal");
        break;
    case 3: // Resume / CV
        add("Your Name", "Title");
        add("email@example.com  ·  (555) 123-4567  ·  City, State", "Subtitle");
        add("Experience", "Heading 1");
        add("Job Title — Company (20XX–Present)", "Heading 3");
        bullet("Key accomplishment or responsibility.");
        bullet("Another accomplishment with measurable impact.");
        add("Education", "Heading 1");
        add("Degree — University (20XX)", "Heading 3");
        add("Skills", "Heading 1");
        add("Skill A, Skill B, Skill C, Skill D", "Normal");
        break;
    case 4: // Meeting Notes
        add("Meeting Notes", "Title");
        add(today + "  ·  Attendees: ", "Subtitle");
        add("Agenda", "Heading 1");
        bullet("First agenda item");
        bullet("Second agenda item");
        add("Discussion", "Heading 1");
        add("Capture the discussion here.", "Normal");
        add("Action Items", "Heading 1");
        bullet("Owner — task — due date");
        break;
    default: // Blank
        add("", "Normal");
        break;
    }

    cur.endEditBlock();
    QTextCursor home = m_editor->textCursor();
    home.movePosition(QTextCursor::Start);
    m_editor->setTextCursor(home);
    m_editor->setFocus();
}

// ── AI assistant (Tier 5) ────────────────────────────────────────────────────
void WriterRibbon::runAi(const QString& system, const QString& user, bool replaceSelection) {
    if (!m_editor || !m_ai) return;
    if (user.trimmed().isEmpty()) {
        QMessageBox::information(window(), "AI Assistant", "There's no text to work with.");
        return;
    }
    // Remember the target range so the async reply lands in the right place.
    QTextCursor target = m_editor->textCursor();
    QApplication::setOverrideCursor(Qt::BusyCursor);

    m_ai->ask(system, user, [this, target, replaceSelection](bool ok, QString text) mutable {
        QApplication::restoreOverrideCursor();
        if (!ok) { QMessageBox::warning(window(), "AI Assistant", text); return; }

        if (replaceSelection && target.hasSelection()) {
            target.insertText(text);
            m_editor->setTextCursor(target);
        } else {
            // Offer to insert the result.
            QMessageBox box(window());
            box.setWindowTitle("AI Assistant");
            box.setText(text.left(4000));
            box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
            box.button(QMessageBox::Ok)->setText("Insert");
            box.button(QMessageBox::Cancel)->setText("Discard");
            if (box.exec() == QMessageBox::Ok) {
                QTextCursor c = m_editor->textCursor();
                c.insertText(text);
            }
        }
        m_editor->setFocus();
    });
}

void WriterRibbon::aiRewrite() {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    if (!cur.hasSelection()) {
        QMessageBox::information(window(), "AI Rewrite", "Select the text you want to rewrite first.");
        return;
    }
    runAi("You are a precise copy editor. Rewrite the user's text to be clearer, "
          "more concise, and well-structured, preserving meaning and tone. "
          "Return only the rewritten text with no preamble or quotation marks.",
          cur.selectedText().replace(QChar(0x2029), '\n'), /*replaceSelection*/ true);
}

void WriterRibbon::aiSummarize() {
    if (!m_editor) return;
    const QString doc = m_editor->document()->toPlainText();
    runAi("Summarize the following document in a few clear sentences. "
          "Return only the summary.",
          doc, /*replaceSelection*/ false);
}

void WriterRibbon::aiGenerate() {
    if (!m_editor) return;
    bool ok = false;
    const QString prompt = QInputDialog::getMultiLineText(
        window(), "AI — Generate", "Describe what you'd like written:", QString(), &ok);
    if (!ok || prompt.trimmed().isEmpty()) return;
    runAi("You are a helpful writing assistant. Write the requested content directly, "
          "ready to paste into a document. Return only the content.",
          prompt, /*replaceSelection*/ false);
}

void WriterRibbon::aiSettings() {
    QDialog dlg(window());
    dlg.setWindowTitle("AI Assistant — Settings");
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    auto* form = new QFormLayout(&dlg);

    auto* keyEdit = new QLineEdit(WriterAi::apiKey(), &dlg);
    keyEdit->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    keyEdit->setPlaceholderText("sk-ant-…");
    auto* modelEdit = new QLineEdit(WriterAi::model(), &dlg);
    form->addRow("Anthropic API key:", keyEdit);
    form->addRow("Model:", modelEdit);
    auto* note = new QLabel("Your key is stored locally. Document text you send is "
                            "transmitted to the Anthropic API to generate responses.", &dlg);
    note->setWordWrap(true);
    note->setStyleSheet("color:#6B7280;font-size:11px;");
    form->addRow(note);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);

    if (dlg.exec() != QDialog::Accepted) return;
    WriterAi::setApiKey(keyEdit->text().trimmed());
    WriterAi::setModel(modelEdit->text().trimmed());
}

// ── Collaboration (Tier 5) ───────────────────────────────────────────────────
void WriterRibbon::collabHost() {
    if (!m_collab) return;
    if (m_collab->active()) { collabStop(); return; }
    bool ok = false;
    const int port = QInputDialog::getInt(window(), "Start Live Share",
        "Host a collaboration session on port:", 8787, 1024, 65535, 1, &ok);
    if (!ok) return;
    QString err;
    if (!m_collab->startHost(quint16(port), err)) {
        QMessageBox::warning(window(), "Live Share", "Couldn't start hosting:\n" + err);
        return;
    }
    QMessageBox::information(window(), "Live Share",
        QString("Hosting on port %1.\n\nOthers on your network can join with "
                "your IP address and this port. Edits sync live.").arg(port));
}

void WriterRibbon::collabJoin() {
    if (!m_collab) return;
    if (m_collab->active()) { collabStop(); return; }
    QDialog dlg(window());
    dlg.setWindowTitle("Join Live Share");
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    auto* form = new QFormLayout(&dlg);
    auto* hostEdit = new QLineEdit("127.0.0.1", &dlg);
    auto* portEdit = new QLineEdit("8787", &dlg);
    form->addRow("Host address:", hostEdit);
    form->addRow("Port:", portEdit);
    auto* note = new QLabel("Joining replaces your document with the host's, then "
                            "keeps both in sync.", &dlg);
    note->setWordWrap(true); note->setStyleSheet("color:#6B7280;font-size:11px;");
    form->addRow(note);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    if (dlg.exec() != QDialog::Accepted) return;

    QString err;
    if (!m_collab->joinHost(hostEdit->text().trimmed(), quint16(portEdit->text().toInt()), err)) {
        QMessageBox::warning(window(), "Live Share", "Couldn't connect:\n" + err);
    }
}

void WriterRibbon::collabStop() {
    if (!m_collab) return;
    m_collab->stop();
    if (m_collabStatus) m_collabStatus->setText("Not connected");
}

// ── Equation editor (Tier 4) ─────────────────────────────────────────────────
void WriterRibbon::showEquationEditor() {
    if (!m_editor) return;
    QDialog dlg(window());
    dlg.setWindowTitle("Equation Editor");
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    dlg.resize(560, 380);
    auto* root = new QVBoxLayout(&dlg);

    auto* preview = new QLabel(&dlg);
    preview->setAlignment(Qt::AlignCenter);
    preview->setMinimumHeight(90);
    preview->setStyleSheet("background:#FFFFFF;border:1px solid #C6CAD3;border-radius:4px;");
    root->addWidget(preview);

    auto* input = new QLineEdit(&dlg);
    input->setText("x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}");
    root->addWidget(input);
    root->addWidget(new QLabel("Use ^ _ for scripts, \\frac{}{}, \\sqrt{}, and the buttons below.", &dlg));

    auto* grid = new QGridLayout();
    struct Sym { const char* l; const char* ins; };
    const Sym syms[] = {
        {"x²","^{2}"},{"xₙ","_{n}"},{"√","\\sqrt{}"},{"a/b","\\frac{}{}"},
        {"π","\\pi"},{"∑","\\sum"},{"∫","\\int"},{"∞","\\infty"},{"∂","\\partial"},
        {"α","\\alpha"},{"β","\\beta"},{"θ","\\theta"},{"λ","\\lambda"},{"Δ","\\Delta"},
        {"≤","\\leq"},{"≥","\\geq"},{"≠","\\neq"},{"±","\\pm"},{"×","\\times"},{"→","\\to"},
    };
    int col = 0, r = 0;
    for (const Sym& sm : syms) {
        auto* b = new QPushButton(QString::fromUtf8(sm.l), &dlg);
        b->setFixedSize(46, 28);
        const QString ins = QString::fromUtf8(sm.ins);
        connect(b, &QPushButton::clicked, &dlg, [input, ins]{ input->insert(ins); input->setFocus(); });
        grid->addWidget(b, r, col);
        if (++col == 10) { col = 0; ++r; }
    }
    root->addLayout(grid);

    auto updatePreview = [preview, input]{
        const QImage img = EquationRenderer::render(input->text(), 30, QColor("#1C1E26"));
        preview->setPixmap(QPixmap::fromImage(img));
    };
    connect(input, &QLineEdit::textChanged, &dlg, updatePreview);
    updatePreview();

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    bb->button(QDialogButtonBox::Ok)->setText("Insert");
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    root->addWidget(bb);

    if (dlg.exec() == QDialog::Accepted) insertEquationImage(input->text());
}

void WriterRibbon::insertEquationImage(const QString& expr) {
    if (!m_editor || expr.trimmed().isEmpty()) return;
    const QImage img = EquationRenderer::render(expr, 34, QColor("#1C1E26"));
    insertImageData(img);   // embeds as inline base64 PNG, so it persists
}

// ── Mail merge (Tier 4) ──────────────────────────────────────────────────────
namespace {
// Minimal CSV row splitter (handles "quoted, fields").
QStringList splitCsvLine(const QString& line) {
    QStringList out; QString cur; bool inQ = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (inQ) {
            if (c == '"') { if (i + 1 < line.size() && line.at(i + 1) == '"') { cur += '"'; ++i; } else inQ = false; }
            else cur += c;
        } else {
            if (c == '"') inQ = true;
            else if (c == ',') { out << cur; cur.clear(); }
            else cur += c;
        }
    }
    out << cur;
    return out;
}
} // namespace

void WriterRibbon::insertMergeField(const QString& field) {
    if (!m_editor || field.isEmpty()) return;
    m_editor->textCursor().insertText(QString::fromUtf8("«") + field + QString::fromUtf8("»"));
    m_editor->setFocus();
}

void WriterRibbon::mergeToDocument() {
    if (!m_editor) return;
    if (m_mergeRows.isEmpty()) {
        QMessageBox::information(window(), "Mail Merge", "Load a CSV data source first.");
        return;
    }
    if (QMessageBox::question(window(), "Mail Merge",
            QString("Generate %1 merged record(s)? This replaces the current document.")
                .arg(m_mergeRows.size())) != QMessageBox::Yes)
        return;

    std::unique_ptr<QTextDocument> tmpl(m_editor->document()->clone());
    m_editor->clear();
    QTextCursor out(m_editor->document());
    out.beginEditBlock();

    for (int rec = 0; rec < m_mergeRows.size(); ++rec) {
        std::unique_ptr<QTextDocument> doc(tmpl->clone());
        const QStringList& row = m_mergeRows[rec];
        for (int f = 0; f < m_mergeHeaders.size(); ++f) {
            const QString token = QString::fromUtf8("«") + m_mergeHeaders[f] + QString::fromUtf8("»");
            const QString value = (f < row.size()) ? row[f] : QString();
            QTextCursor fc(doc.get());
            for (;;) {
                fc = doc->find(token, fc);
                if (fc.isNull()) break;
                fc.insertText(value);
            }
        }
        if (rec > 0) {
            QTextBlockFormat pb; pb.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
            out.insertBlock(pb);
        }
        out.insertFragment(QTextDocumentFragment(doc.get()));
    }
    out.endEditBlock();
    m_editor->moveCursor(QTextCursor::Start);
    m_editor->setFocus();
}

void WriterRibbon::showMailMerge() {
    if (!m_editor) return;
    auto* dlg = new QDialog(window());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle("Mail Merge");
    dlg->setStyleSheet(ThemeManager::inputDialogStyleSheet());
    dlg->resize(320, 380);
    auto* root = new QVBoxLayout(dlg);

    auto* status = new QLabel(dlg);
    auto* fields = new QListWidget(dlg);
    auto refresh = [this, status, fields]{
        fields->clear();
        for (const QString& h : m_mergeHeaders) fields->addItem(h);
        status->setText(m_mergeRows.isEmpty()
            ? "No data source loaded."
            : QString("%1 record(s), %2 field(s).").arg(m_mergeRows.size()).arg(m_mergeHeaders.size()));
    };

    auto* btnLoad = new QPushButton("Load Data Source (CSV)…", dlg);
    connect(btnLoad, &QPushButton::clicked, dlg, [this, refresh]{
        const QString path = QFileDialog::getOpenFileName(window(), "Load CSV", QDir::homePath(),
                                                          "CSV Files (*.csv);;All Files (*)");
        if (path.isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        QTextStream in(&f); in.setEncoding(QStringConverter::Utf8);
        m_mergeHeaders.clear(); m_mergeRows.clear();
        bool first = true;
        while (!in.atEnd()) {
            const QString line = in.readLine();
            if (line.trimmed().isEmpty()) continue;
            const QStringList cells = splitCsvLine(line);
            if (first) { for (const QString& c : cells) m_mergeHeaders << c.trimmed(); first = false; }
            else m_mergeRows << cells;
        }
        f.close();
        refresh();
    });
    root->addWidget(btnLoad);
    root->addWidget(status);
    root->addWidget(new QLabel("Fields (click into the document, then Insert):", dlg));
    root->addWidget(fields, 1);

    auto* btnInsert = new QPushButton("Insert Field", dlg);
    connect(btnInsert, &QPushButton::clicked, dlg, [this, fields]{
        if (fields->currentItem()) insertMergeField(fields->currentItem()->text());
    });
    connect(fields, &QListWidget::itemDoubleClicked, dlg, [this](QListWidgetItem* it){
        if (it) insertMergeField(it->text());
    });
    root->addWidget(btnInsert);

    auto* btnMerge = new QPushButton("Merge to New Document", dlg);
    connect(btnMerge, &QPushButton::clicked, dlg, [this]{ mergeToDocument(); });
    root->addWidget(btnMerge);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::close);
    root->addWidget(bb);

    refresh();
    dlg->show();   // modeless so the user can position the cursor in the document
}

// ── Document compare (Tier 4) ────────────────────────────────────────────────
namespace {
QString plainTextFromFile(const QString& path) {
    QTextDocument doc;
    if (path.endsWith(".docx", Qt::CaseInsensitive)) {
        DocxIo::importDocx(path, &doc);
        return doc.toPlainText();
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    QTextStream in(&f); in.setEncoding(QStringConverter::Utf8);
    QString content = in.readAll();
    f.close();
    content.remove("<!-- NativeOffice Writer Document (.noff) -->\n");
    if (content.contains("<html", Qt::CaseInsensitive) || content.contains("<p", Qt::CaseInsensitive))
        doc.setHtml(content);
    else
        doc.setPlainText(content);
    return doc.toPlainText();
}

QStringList tokenize(const QString& s) {
    QStringList toks;
    static const QRegularExpression re("(\\s+|\\S+)");
    auto it = re.globalMatch(s);
    while (it.hasNext()) toks << it.next().captured(1);
    return toks;
}
} // namespace

void WriterRibbon::showCompareDialog() {
    if (!m_editor) return;
    const QString path = QFileDialog::getOpenFileName(window(), "Compare With…", QDir::homePath(),
        "Documents (*.noff *.txt *.html *.docx);;All Files (*)");
    if (path.isEmpty()) return;
    compareWithFile(path);
}

void WriterRibbon::compareWithFile(const QString& path) {
    if (!m_editor) return;
    const QStringList a = tokenize(m_editor->document()->toPlainText());   // original (mine)
    const QStringList b = tokenize(plainTextFromFile(path));               // revised (other)

    if (qint64(a.size()) * b.size() > 9'000'000) {
        QMessageBox::warning(window(), "Compare",
            "The documents are too large to compare word-by-word.");
        return;
    }

    // LCS over token sequences.
    const int n = a.size(), m = b.size();
    QVector<QVector<int>> dp(n + 1, QVector<int>(m + 1, 0));
    for (int i = n - 1; i >= 0; --i)
        for (int j = m - 1; j >= 0; --j)
            dp[i][j] = (a[i] == b[j]) ? dp[i + 1][j + 1] + 1
                                      : qMax(dp[i + 1][j], dp[i][j + 1]);

    if (QMessageBox::question(window(), "Compare Documents",
            "Show differences in this document?\nDeletions are red strikethrough, "
            "insertions are green underline.") != QMessageBox::Yes)
        return;

    QTextCharFormat eq;  eq.setForeground(QColor("#1C1E26"));
    QTextCharFormat del; del.setForeground(QColor("#C0271C")); del.setFontStrikeOut(true);
    QTextCharFormat ins; ins.setForeground(QColor("#16A34A")); ins.setFontUnderline(true);

    m_editor->clear();
    QTextCursor cur(m_editor->document());
    cur.beginEditBlock();
    auto put = [&cur](const QString& tok, const QTextCharFormat& fmt) {
        if (tok == "\n") { cur.insertBlock(); return; }
        if (tok.contains('\n')) {                 // whitespace run with newlines
            const QStringList parts = tok.split('\n');
            for (int k = 0; k < parts.size(); ++k) {
                if (k) cur.insertBlock();
                if (!parts[k].isEmpty()) cur.insertText(parts[k], fmt);
            }
            return;
        }
        cur.insertText(tok, fmt);
    };

    int i = 0, j = 0;
    while (i < n && j < m) {
        if (a[i] == b[j]) { put(a[i], eq); ++i; ++j; }
        else if (dp[i + 1][j] >= dp[i][j + 1]) { put(a[i], del); ++i; }
        else { put(b[j], ins); ++j; }
    }
    while (i < n) put(a[i++], del);
    while (j < m) put(b[j++], ins);
    cur.endEditBlock();
    m_editor->moveCursor(QTextCursor::Start);
    m_editor->setFocus();
}

void WriterRibbon::exportToPicture() {
    if (!m_editor) return;
    const QString path = QFileDialog::getSaveFileName(window(), "Export to Picture",
                            QDir::homePath() + "/Document.png", "PNG Image (*.png)");
    if (path.isEmpty()) return;
    QTextDocument* doc = m_editor->document();
    const QSizeF sz = doc->size();
    QImage img(qMax(1, int(sz.width())), qMax(1, int(sz.height())),
               QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    QPainter p(&img);
    doc->drawContents(&p);
    p.end();
    img.save(path);
    QMessageBox::information(window(), "Export to Picture", "Exported to:\n" + path);
}

void WriterRibbon::exportToText() {
    if (!m_editor) return;
    const QString path = QFileDialog::getSaveFileName(window(), "Extract Text",
                            QDir::homePath() + "/Document.txt", "Text Files (*.txt)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream(&f) << m_editor->document()->toPlainText();
        f.close();
        QMessageBox::information(window(), "Extract Text", "Saved to:\n" + path);
    }
}

void WriterRibbon::exportToWord() {
    if (!m_editor) return;
    const QString path = QFileDialog::getSaveFileName(window(), "Save as Word Document",
                            QDir::homePath() + "/Document.docx", "Word Document (*.docx)");
    if (path.isEmpty()) return;
    if (DocxIo::exportDocx(m_editor->document(), path))
        QMessageBox::information(window(), "Save as Word", "Saved to:\n" + path);
    else
        QMessageBox::warning(window(), "Save as Word", "Could not write the .docx file.");
}

void WriterRibbon::importFromWord() {
    if (!m_editor) return;
    const QString path = QFileDialog::getOpenFileName(window(), "Open Word Document",
                            QDir::homePath(), "Word Document (*.docx)");
    if (path.isEmpty()) return;
    if (!DocxIo::importDocx(path, m_editor->document()))
        QMessageBox::warning(window(), "Open Word", "Could not read the .docx file.");
    m_editor->setFocus();
}

// ─────────────────────────────────────────────────────────────────────────────
// Placeholder tabs
// ─────────────────────────────────────────────────────────────────────────────
QWidget* WriterRibbon::buildPlaceholderTab(const QString& tabName) {
    auto* tab = new QWidget(this);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(20, 0, 20, 0);
    auto* lbl = new QLabel(QString("%1  ·  Coming Soon").arg(tabName), tab);
    lbl->setObjectName("ribbonComingSoon");
    lbl->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    layout->addWidget(lbl);
    layout->addStretch();
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// Formatting actions
// ─────────────────────────────────────────────────────────────────────────────
void WriterRibbon::mergeFormatOnSelection(const QTextCharFormat& fmt) {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    if (cur.hasSelection())
        cur.mergeCharFormat(fmt);
    m_editor->mergeCurrentCharFormat(fmt);
    m_editor->setFocus();
}

void WriterRibbon::adjustFontSize(int delta) {
    if (!m_editor) return;
    const QList<int> ladder = {8,9,10,11,12,14,16,18,20,22,24,28,32,36,40,48,60,72,96};
    qreal cur = m_editor->currentCharFormat().fontPointSize();
    if (cur <= 0) cur = 12;
    int idx = 0;
    while (idx < ladder.size() && ladder[idx] < cur) ++idx;
    idx = qBound(0, idx + delta, ladder.size() - 1);
    QTextCharFormat fmt; fmt.setFontPointSize(ladder[idx]);
    mergeFormatOnSelection(fmt);
}

void WriterRibbon::applyChangeCase(int mode) {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    if (!cur.hasSelection()) {
        cur.select(QTextCursor::WordUnderCursor);
        if (!cur.hasSelection()) return;
    }
    const QString src = cur.selectedText();
    QString out;
    switch (mode) {
    case 0: out = src.toUpper(); break;
    case 1: out = src.toLower(); break;
    case 2: {                                   // Title Case
        out = src.toLower();
        bool start = true;
        for (QChar& ch : out) {
            if (ch.isLetter()) { if (start) ch = ch.toUpper(); start = false; }
            else start = true;
        }
        break;
    }
    case 3: {                                   // Sentence case
        out = src.toLower();
        bool start = true;
        for (QChar& ch : out) {
            if (start && ch.isLetter()) { ch = ch.toUpper(); start = false; }
            else if (ch == '.' || ch == '!' || ch == '?') start = true;
        }
        break;
    }
    case 4:                                      // tOGGLE cASE
        for (QChar ch : src) out += ch.isUpper() ? ch.toLower() : ch.toUpper();
        break;
    }
    cur.insertText(out);
    m_editor->setFocus();
}

void WriterRibbon::clearFormatting() {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    QTextCharFormat fmt;
    fmt.setFontFamilies({"Segoe UI", "Inter", "Roboto", "sans-serif"});
    fmt.setFontPointSize(12);
    fmt.setFontWeight(QFont::Normal);
    fmt.setFontItalic(false);
    fmt.setFontUnderline(false);
    fmt.setFontStrikeOut(false);
    fmt.setForeground(QColor("#1C1E26"));
    fmt.setBackground(Qt::transparent);
    fmt.setVerticalAlignment(QTextCharFormat::AlignNormal);
    if (cur.hasSelection())
        cur.setCharFormat(fmt);
    else {
        cur.select(QTextCursor::BlockUnderCursor);
        cur.setCharFormat(fmt);
    }
    m_editor->setCurrentCharFormat(fmt);
    m_editor->setFocus();
}

void WriterRibbon::applyUnderlineStyle(int style) {
    if (!m_editor) return;
    QTextCharFormat fmt;
    fmt.setFontUnderline(true);
    fmt.setUnderlineStyle(static_cast<QTextCharFormat::UnderlineStyle>(style));
    mergeFormatOnSelection(fmt);
}

void WriterRibbon::toggleVerticalAlign(int align) {
    if (!m_editor) return;
    QTextCharFormat fmt;
    switch (align) {
    case 1: fmt.setVerticalAlignment(QTextCharFormat::AlignSuperScript); break;
    case 2: fmt.setVerticalAlignment(QTextCharFormat::AlignSubScript); break;
    default: fmt.setVerticalAlignment(QTextCharFormat::AlignNormal); break;
    }
    mergeFormatOnSelection(fmt);
    // keep sub/super mutually exclusive
    if (!m_syncing) {
        m_syncing = true;
        if (align == 1 && m_btnSub)   m_btnSub->setChecked(false);
        if (align == 2 && m_btnSuper) m_btnSuper->setChecked(false);
        m_syncing = false;
    }
}

void WriterRibbon::applyBullets(int style) {
    ListOps::applyList(m_editor, static_cast<QTextListFormat::Style>(style));
}

void WriterRibbon::applyNumbering(int style) {
    ListOps::applyList(m_editor, static_cast<QTextListFormat::Style>(style));
}

void WriterRibbon::changeIndent(int delta) {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    // Inside a list, indent buttons promote/demote the item between levels.
    if (cur.currentList()) { ListOps::changeLevel(m_editor, delta); return; }
    QTextBlockFormat bf = cur.blockFormat();
    bf.setIndent(qMax(0, bf.indent() + delta));
    cur.mergeBlockFormat(bf);
    m_editor->setFocus();
}

// ── Multi-level list operations (Tier 2 #9) ──────────────────────────────────
void WriterRibbon::applyMultilevelList(bool numbered) {
    if (!m_editor) return;
    ListOps::applyList(m_editor, ListOps::markerForLevel(numbered, 1));
}

void WriterRibbon::changeListLevel(int delta) { ListOps::changeLevel(m_editor, delta); }
void WriterRibbon::restartNumbering()         { ListOps::restartNumbering(m_editor); }
void WriterRibbon::continueNumbering()        { ListOps::continueNumbering(m_editor); }

void WriterRibbon::customizeList() {
    if (!m_editor) return;
    QDialog dlg(this);
    dlg.setWindowTitle("Define List Format");
    auto* form = new QFormLayout(&dlg);

    auto* typeCombo = new QComboBox(&dlg);
    typeCombo->addItems({ "Bullet", "Numbered" });
    auto* styleCombo = new QComboBox(&dlg);
    form->addRow("Type:", typeCombo);
    form->addRow("Format:", styleCombo);

    // Each entry carries {style, prefix, suffix} encoded in user data.
    auto fillStyles = [&](int type) {
        styleCombo->clear();
        if (type == 0) {
            styleCombo->addItem("•  Disc",   QVariantList{ int(QTextListFormat::ListDisc), "", "" });
            styleCombo->addItem("◦  Circle", QVariantList{ int(QTextListFormat::ListCircle), "", "" });
            styleCombo->addItem("▪  Square", QVariantList{ int(QTextListFormat::ListSquare), "", "" });
        } else {
            styleCombo->addItem("1.  2.  3.",  QVariantList{ int(QTextListFormat::ListDecimal),    "", "." });
            styleCombo->addItem("1)  2)  3)",  QVariantList{ int(QTextListFormat::ListDecimal),    "", ")" });
            styleCombo->addItem("(1) (2) (3)", QVariantList{ int(QTextListFormat::ListDecimal),    "(", ")" });
            styleCombo->addItem("a.  b.  c.",  QVariantList{ int(QTextListFormat::ListLowerAlpha), "", "." });
            styleCombo->addItem("A.  B.  C.",  QVariantList{ int(QTextListFormat::ListUpperAlpha), "", "." });
            styleCombo->addItem("i.  ii. iii.",QVariantList{ int(QTextListFormat::ListLowerRoman), "", "." });
            styleCombo->addItem("I.  II. III.",QVariantList{ int(QTextListFormat::ListUpperRoman), "", "." });
        }
    };
    fillStyles(0);
    connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, fillStyles);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;
    const QVariantList v = styleCombo->currentData().toList();
    if (v.size() < 3) return;
    const auto style = static_cast<QTextListFormat::Style>(v[0].toInt());
    const QString prefix = v[1].toString(), suffix = v[2].toString();

    // Re-style the existing list if the cursor is in one; otherwise create it.
    QTextCursor cur = m_editor->textCursor();
    if (QTextList* list = cur.currentList()) {
        QTextListFormat lf = list->format();
        lf.setStyle(style);
        lf.setNumberPrefix(prefix);
        lf.setNumberSuffix(suffix);
        list->setFormat(lf);
    } else {
        ListOps::applyList(m_editor, style, prefix, suffix);
    }
    m_editor->setFocus();
}

void WriterRibbon::sortParagraphs(bool ascending) {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    if (!cur.hasSelection()) return;

    int start = cur.selectionStart();
    int end   = cur.selectionEnd();

    QTextCursor c(m_editor->document());
    c.setPosition(start);
    const int firstBlock = c.blockNumber();
    c.setPosition(end);
    const int lastBlock = c.blockNumber();
    if (lastBlock <= firstBlock) return;

    QStringList lines;
    for (int b = firstBlock; b <= lastBlock; ++b)
        lines << m_editor->document()->findBlockByNumber(b).text();

    lines.sort(Qt::CaseInsensitive);
    if (!ascending) std::reverse(lines.begin(), lines.end());

    // Replace the whole block range with the sorted text.
    QTextCursor sel(m_editor->document());
    sel.setPosition(m_editor->document()->findBlockByNumber(firstBlock).position());
    const QTextBlock lb = m_editor->document()->findBlockByNumber(lastBlock);
    sel.setPosition(lb.position() + lb.length() - 1, QTextCursor::KeepAnchor);
    sel.beginEditBlock();
    sel.insertText(lines.join("\n"));
    sel.endEditBlock();
    m_editor->setFocus();
}

void WriterRibbon::toggleFormattingMarks(bool show) {
    if (!m_editor) return;
    QTextOption opt = m_editor->document()->defaultTextOption();
    QTextOption::Flags flags = opt.flags();
    if (show) flags |=  (QTextOption::ShowTabsAndSpaces | QTextOption::ShowLineAndParagraphSeparators);
    else      flags &= ~(QTextOption::ShowTabsAndSpaces | QTextOption::ShowLineAndParagraphSeparators);
    opt.setFlags(flags);
    m_editor->document()->setDefaultTextOption(opt);
    m_editor->viewport()->update();
}

void WriterRibbon::setAlignment(Qt::Alignment a) {
    if (!m_editor) return;
    m_editor->setAlignment(a);
    m_editor->setFocus();
}

void WriterRibbon::applyLineSpacing(double mult) {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    QTextBlockFormat bf = cur.blockFormat();
    bf.setLineHeight(mult * 100.0, QTextBlockFormat::ProportionalHeight);
    cur.mergeBlockFormat(bf);
    m_editor->setFocus();
}

void WriterRibbon::applyShading(const QColor& c) {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    QTextBlockFormat bf = cur.blockFormat();
    if (c.alpha() == 0) bf.clearBackground();
    else                bf.setBackground(c);
    cur.mergeBlockFormat(bf);
    m_editor->setFocus();
}

void WriterRibbon::applyBorder(int kind) {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    // A horizontal rule is the most faithful effect QTextEdit supports natively.
    if (kind == 2) {                    // top border → rule before the block
        cur.movePosition(QTextCursor::StartOfBlock);
        cur.insertHtml("<hr>");
    } else {                            // bottom / horizontal line → rule after
        cur.movePosition(QTextCursor::EndOfBlock);
        cur.insertHtml("<hr>");
    }
    m_editor->setFocus();
}

void WriterRibbon::applyParagraphStyle(WriterStyle s) {
    // Built-in enum → manager name, so the legacy enum entry points still work.
    static const char* names[] = {
        "Normal", "No Spacing", "Heading 1", "Heading 2", "Heading 3",
        "Title", "Subtitle", "Quote" };
    applyStyleByName(QString::fromLatin1(names[static_cast<int>(s)]));
}

// ─────────────────────────────────────────────────────────────────────────────
// Named styles (Tier 2 #8)
// ─────────────────────────────────────────────────────────────────────────────
void WriterRibbon::applyStyleByName(const QString& name) {
    if (!m_editor || !m_styles) return;
    const WriterStyleDef* d = m_styles->find(name);
    if (!d) return;

    QTextCursor cur = m_editor->textCursor();
    cur.beginEditBlock();

    if (d->isCharacter) {
        // Character style: format the selection (or the word under the cursor).
        QTextCursor range = cur;
        if (!range.hasSelection()) range.select(QTextCursor::WordUnderCursor);
        range.mergeCharFormat(d->charFormat());
        m_editor->mergeCurrentCharFormat(d->charFormat());
    } else {
        QTextCharFormat  cf = d->charFormat();
        QTextBlockFormat bf = d->blockFormat();
        bf.setProperty(kStyleNameProp, name);
        // Explicit background so switching off a shaded style (e.g. Quote) clears it.
        if (!d->hasBackground) bf.setBackground(QBrush(Qt::transparent));

        QTextCursor range = cur;
        if (!range.hasSelection()) {
            range.movePosition(QTextCursor::StartOfBlock);
            range.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        }
        range.mergeBlockFormat(bf);
        // The BLOCK's char format, not just the text's.
        //
        // mergeCharFormat only touches characters that are actually selected,
        // so on an empty paragraph it does nothing at all, and the only thing
        // carrying the new style was setCurrentCharFormat below - which Qt
        // throws away the moment the cursor moves. That is the whole of the
        // "clear the text, retype it, and Heading 2 comes back as Heading 1"
        // report: the emptied block kept the previous style's char format and
        // handed it straight back to the newly typed text.
        //
        // The block char format is what an empty block renders with and what
        // text typed into it inherits, so it has to be set too. It matters for
        // every paragraph style, not only the headings.
        range.mergeBlockCharFormat(cf);
        range.mergeCharFormat(cf);
        m_editor->setCurrentCharFormat(cf);
    }

    cur.endEditBlock();
    m_editor->setFocus();
    syncToCurrentFormat();
}

void WriterRibbon::rebuildStyleGallery() {
    if (!m_styleGrid || !m_styles) return;
    auto* sgl = qobject_cast<QGridLayout*>(m_styleGrid->layout());
    if (!sgl) return;

    // Clear existing chips.
    for (QToolButton* b : m_styleButtons) { if (b) { m_styleGroup->removeButton(b); b->deleteLater(); } }
    m_styleButtons.clear();

    int si = 0;
    for (const WriterStyleDef& sd : m_styles->styles()) {
        auto* b = new QToolButton(m_styleGrid);
        b->setObjectName("ribbonStyleBtn");
        b->setText(sd.name);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(92, 28);
        b->setContextMenuPolicy(Qt::CustomContextMenu);
        b->setToolTip(QString("Apply '%1'  ·  right-click to edit").arg(sd.name));

        QFont f("Segoe UI", 9);
        if (sd.headingLevel == 1) { f.setPointSize(11); f.setBold(true); }
        else if (sd.headingLevel == 2) { f.setPointSize(10); f.setBold(true); }
        else if (sd.headingLevel == 3) f.setBold(true);
        else { f.setBold(sd.bold); f.setItalic(sd.italic); }
        b->setFont(f);

        const QString nm = sd.name;
        connect(b, &QToolButton::clicked, this, [this, nm] { applyStyleByName(nm); });
        connect(b, &QToolButton::customContextMenuRequested, this,
                [this, b, nm](const QPoint& p) { showStyleContextMenu(nm, b->mapToGlobal(p)); });

        m_styleGroup->addButton(b);
        sgl->addWidget(b, si / 4, si % 4);
        m_styleButtons.insert(nm, b);
        ++si;
    }
    if (auto* nb = m_styleButtons.value("Normal")) nb->setChecked(true);
    rebuildMoreStylesMenu();
}

void WriterRibbon::rebuildMoreStylesMenu() {
    if (!m_moreStylesBtn || !m_styles) return;
    auto* m = new QMenu(m_moreStylesBtn);
    for (const WriterStyleDef& sd : m_styles->styles()) {
        const QString nm = sd.name;
        const QString label = sd.isCharacter ? (nm + "  (character)") : nm;
        m->addAction(label, this, [this, nm] { applyStyleByName(nm); });
    }
    m->addSeparator();
    m->addAction("New Style…",     this, [this] { openStyleEditor(QString()); });
    m->addAction("Manage Styles…", this, [this] { manageStyles(); });
    if (QMenu* old = m_moreStylesBtn->menu()) old->deleteLater();
    m_moreStylesBtn->setMenu(m);
}

void WriterRibbon::showStyleContextMenu(const QString& name, const QPoint& globalPos) {
    if (!m_styles) return;
    const WriterStyleDef* d = m_styles->find(name);
    if (!d) return;
    QMenu menu(this);
    menu.addAction(QString("Apply '%1'").arg(name), this, [this, name] { applyStyleByName(name); });
    menu.addSeparator();
    menu.addAction("Modify…", this, [this, name] { openStyleEditor(name); });
    menu.addAction("Update to Match Selection", this, [this, name] { updateStyleFromSelection(name); });
    if (!d->builtin) {
        menu.addAction("Rename…", this, [this, name] { renameStyleInteractive(name); });
        menu.addAction("Delete",  this, [this, name] { deleteStyleInteractive(name); });
    }
    menu.exec(globalPos);
}

void WriterRibbon::openStyleEditor(const QString& editName) {
    if (!m_styles) return;
    const bool isNew = editName.isEmpty();
    WriterStyleDef def;
    if (!isNew) {
        if (const WriterStyleDef* d = m_styles->find(editName)) def = *d;
        else return;
    } else {
        def.fontSize = 12;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(isNew ? "New Style" : QString("Modify Style — %1").arg(editName));
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    auto* form = new QFormLayout(&dlg);

    auto* nameEdit = new QLineEdit(def.name, &dlg);
    nameEdit->setEnabled(isNew || !def.builtin);     // built-in names are fixed
    form->addRow("Name:", nameEdit);

    auto* typeCombo = new QComboBox(&dlg);
    typeCombo->addItems({ "Paragraph", "Character" });
    typeCombo->setCurrentIndex(def.isCharacter ? 1 : 0);
    typeCombo->setEnabled(isNew);
    form->addRow("Type:", typeCombo);

    // Font family — lazy delegate so we don't measure every installed font.
    auto* familyCombo = new QComboBox(&dlg);
    familyCombo->addItem("(default)");
    familyCombo->addItems(QFontDatabase::families());
    familyCombo->setItemDelegate(new FontPreviewDelegate(familyCombo));
    if (auto* lv = qobject_cast<QListView*>(familyCombo->view())) lv->setUniformItemSizes(true);
    familyCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    if (!def.fontFamily.isEmpty()) familyCombo->setCurrentText(def.fontFamily);
    form->addRow("Font:", familyCombo);

    auto* sizeSpin = new QSpinBox(&dlg);
    sizeSpin->setRange(6, 96);
    sizeSpin->setValue(def.fontSize > 0 ? int(def.fontSize) : 12);
    form->addRow("Size:", sizeSpin);

    auto* boldChk = new QCheckBox("Bold", &dlg);      boldChk->setChecked(def.bold);
    auto* italChk = new QCheckBox("Italic", &dlg);    italChk->setChecked(def.italic);
    auto* undChk  = new QCheckBox("Underline", &dlg); undChk->setChecked(def.underline);
    auto* fxRow = new QWidget(&dlg);
    auto* fxl = new QHBoxLayout(fxRow); fxl->setContentsMargins(0,0,0,0);
    fxl->addWidget(boldChk); fxl->addWidget(italChk); fxl->addWidget(undChk); fxl->addStretch();
    form->addRow("Style:", fxRow);

    QColor chosen = def.hasColor ? def.color : QColor("#1C1E26");
    auto* colorBtn = new QPushButton("Text Colour…", &dlg);
    auto paintColorBtn = [colorBtn](const QColor& c) {
        colorBtn->setStyleSheet(QString("text-align:left;padding:4px 8px;"
            "border:1px solid #C6CAD3;border-radius:4px;background:%1;color:%2;")
            .arg(c.name(), c.lightness() < 128 ? "#FFFFFF" : "#1C1E26"));
    };
    paintColorBtn(chosen);
    bool hasColor = def.hasColor;
    connect(colorBtn, &QPushButton::clicked, &dlg, [&]{
        const QColor c = QColorDialog::getColor(chosen, &dlg, "Text Colour");
        if (c.isValid()) { chosen = c; hasColor = true; paintColorBtn(c); }
    });
    form->addRow("Colour:", colorBtn);

    auto* alignCombo = new QComboBox(&dlg);
    alignCombo->addItems({ "Left", "Center", "Right", "Justify" });
    const Qt::Alignment al = static_cast<Qt::Alignment>(def.alignment);
    alignCombo->setCurrentIndex(al.testFlag(Qt::AlignHCenter) ? 1 :
                                al.testFlag(Qt::AlignRight)   ? 2 :
                                al.testFlag(Qt::AlignJustify) ? 3 : 0);
    form->addRow("Alignment:", alignCombo);

    auto* spaceBefore = new QSpinBox(&dlg); spaceBefore->setRange(0, 200); spaceBefore->setValue(int(def.spaceBefore));
    auto* spaceAfter  = new QSpinBox(&dlg); spaceAfter->setRange(0, 200);  spaceAfter->setValue(int(def.spaceAfter));
    auto* spRow = new QWidget(&dlg); auto* spl = new QHBoxLayout(spRow); spl->setContentsMargins(0,0,0,0);
    spl->addWidget(new QLabel("Before", spRow)); spl->addWidget(spaceBefore);
    spl->addWidget(new QLabel("After", spRow));  spl->addWidget(spaceAfter); spl->addStretch();
    form->addRow("Spacing:", spRow);

    auto* lineCombo = new QComboBox(&dlg);
    lineCombo->addItems({ "Single", "1.15", "1.5", "Double" });
    lineCombo->setCurrentIndex(def.lineHeight >= 200 ? 3 : def.lineHeight >= 150 ? 2 :
                               def.lineHeight >= 115 ? 1 : 0);
    form->addRow("Line spacing:", lineCombo);

    // Paragraph-only rows hide for character styles.
    auto refreshType = [&](int idx) {
        const bool para = (idx == 0);
        alignCombo->setEnabled(para); spaceBefore->setEnabled(para);
        spaceAfter->setEnabled(para); lineCombo->setEnabled(para);
    };
    refreshType(typeCombo->currentIndex());
    connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, refreshType);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    const QString newName = nameEdit->text().trimmed();
    if (newName.isEmpty()) { QMessageBox::warning(this, "Style", "A style needs a name."); return; }
    if (isNew && m_styles->contains(newName)) {
        QMessageBox::warning(this, "Style", "A style with that name already exists.");
        return;
    }

    def.name        = newName;
    def.isCharacter = (typeCombo->currentIndex() == 1);
    def.fontFamily  = (familyCombo->currentIndex() == 0) ? QString() : familyCombo->currentText();
    def.fontSize    = sizeSpin->value();
    def.bold        = boldChk->isChecked();
    def.italic      = italChk->isChecked();
    def.underline   = undChk->isChecked();
    def.hasColor    = hasColor;
    def.color       = chosen;
    if (!def.isCharacter) {
        static const Qt::Alignment al4[] = { Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight, Qt::AlignJustify };
        def.alignment   = static_cast<int>(al4[alignCombo->currentIndex()]);
        def.spaceBefore = spaceBefore->value();
        def.spaceAfter  = spaceAfter->value();
        static const double lh[] = { 0, 115, 150, 200 };
        def.lineHeight  = lh[lineCombo->currentIndex()];
    }

    m_styles->upsert(def);
    rebuildStyleGallery();
    applyStyleByName(def.name);     // apply the freshly created/edited style
}

void WriterRibbon::updateStyleFromSelection(const QString& name) {
    if (!m_editor || !m_styles) return;
    const WriterStyleDef* existing = m_styles->find(name);
    if (!existing) return;
    WriterStyleDef def = *existing;
    const QTextCursor cur = m_editor->textCursor();
    def.captureFrom(cur.charFormat(), cur.blockFormat());
    m_styles->upsert(def);
    rebuildStyleGallery();
    // Re-apply so the active paragraph reflects the captured look immediately.
    applyStyleByName(name);
}

void WriterRibbon::renameStyleInteractive(const QString& name) {
    if (!m_styles) return;
    bool ok = false;
    const QString nn = QInputDialog::getText(this, "Rename Style", "New name:",
                                             QLineEdit::Normal, name, &ok).trimmed();
    if (!ok || nn.isEmpty() || nn == name) return;
    if (m_styles->contains(nn)) { QMessageBox::warning(this, "Rename", "That name is taken."); return; }
    if (!m_styles->rename(name, nn)) return;

    // Re-tag any paragraphs that used the old name.
    if (m_editor) {
        QTextDocument* doc = m_editor->document();
        for (QTextBlock b = doc->begin(); b != doc->end(); b = b.next()) {
            if (b.blockFormat().stringProperty(kStyleNameProp) == name) {
                QTextCursor c(b);
                QTextBlockFormat bf; bf.setProperty(kStyleNameProp, nn);
                c.mergeBlockFormat(bf);
            }
        }
    }
    rebuildStyleGallery();
}

void WriterRibbon::deleteStyleInteractive(const QString& name) {
    if (!m_styles) return;
    if (QMessageBox::question(this, "Delete Style",
            QString("Delete the style '%1'? Paragraphs keep their look but revert to Normal.").arg(name))
        != QMessageBox::Yes) return;

    if (m_editor) {
        QTextDocument* doc = m_editor->document();
        for (QTextBlock b = doc->begin(); b != doc->end(); b = b.next()) {
            if (b.blockFormat().stringProperty(kStyleNameProp) == name) {
                QTextCursor c(b);
                QTextBlockFormat bf; bf.setProperty(kStyleNameProp, QString("Normal"));
                c.mergeBlockFormat(bf);
            }
        }
    }
    m_styles->remove(name);
    rebuildStyleGallery();
}

void WriterRibbon::manageStyles() {
    if (!m_styles) return;
    QDialog dlg(this);
    dlg.setWindowTitle("Manage Styles");
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    dlg.resize(360, 420);
    auto* root = new QVBoxLayout(&dlg);

    auto* listw = new QListWidget(&dlg);
    auto fill = [&]{
        listw->clear();
        for (const WriterStyleDef& sd : m_styles->styles())
            listw->addItem(sd.isCharacter ? (sd.name + "  (character)") : sd.name);
    };
    fill();
    root->addWidget(listw, 1);

    auto selectedName = [&]() -> QString {
        if (!listw->currentItem()) return QString();
        QString t = listw->currentItem()->text();
        const int sep = t.indexOf("  (character)");
        return sep >= 0 ? t.left(sep) : t;
    };

    auto* row = new QHBoxLayout();
    auto* btnNew    = new QPushButton("New…", &dlg);
    auto* btnMod    = new QPushButton("Modify…", &dlg);
    auto* btnUpd    = new QPushButton("Update", &dlg);
    auto* btnRen    = new QPushButton("Rename…", &dlg);
    auto* btnDel    = new QPushButton("Delete", &dlg);
    for (auto* b : { btnNew, btnMod, btnUpd, btnRen, btnDel }) row->addWidget(b);
    root->addLayout(row);

    connect(btnNew, &QPushButton::clicked, &dlg, [&]{ openStyleEditor(QString()); fill(); });
    connect(btnMod, &QPushButton::clicked, &dlg, [&]{ if (!selectedName().isEmpty()) { openStyleEditor(selectedName()); fill(); } });
    connect(btnUpd, &QPushButton::clicked, &dlg, [&]{ if (!selectedName().isEmpty()) { updateStyleFromSelection(selectedName()); } });
    connect(btnRen, &QPushButton::clicked, &dlg, [&]{ if (!selectedName().isEmpty()) { renameStyleInteractive(selectedName()); fill(); } });
    connect(btnDel, &QPushButton::clicked, &dlg, [&]{ if (!selectedName().isEmpty()) { deleteStyleInteractive(selectedName()); fill(); } });
    connect(listw, &QListWidget::itemDoubleClicked, &dlg, [&]{ if (!selectedName().isEmpty()) applyStyleByName(selectedName()); });

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    root->addWidget(bb);
    dlg.exec();
}

// ─────────────────────────────────────────────────────────────────────────────
// Style persistence (called by WriterModule on save / load)
// ─────────────────────────────────────────────────────────────────────────────
QString WriterRibbon::styleDefsJson() const {
    return m_styles ? m_styles->serializeCustom() : QString();
}

QString WriterRibbon::blockAssignmentsJson() const {
    if (!m_editor) return QString();
    QJsonArray arr;
    int i = 0;
    for (QTextBlock b = m_editor->document()->begin();
         b != m_editor->document()->end(); b = b.next(), ++i) {
        const QString nm = b.blockFormat().stringProperty(kStyleNameProp);
        if (!nm.isEmpty() && nm != "Normal") {
            QJsonObject o; o["i"] = i; o["s"] = nm;
            arr.append(o);
        }
    }
    if (arr.isEmpty()) return QString();
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void WriterRibbon::applyPersistedStyles(const QString& defsJson, const QString& blocksJson) {
    if (!m_styles) return;
    if (!defsJson.isEmpty()) m_styles->loadCustom(defsJson);
    rebuildStyleGallery();

    if (m_editor && !blocksJson.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(blocksJson.toUtf8());
        if (doc.isArray()) {
            QHash<int, QString> map;
            for (const QJsonValue& v : doc.array()) {
                const QJsonObject o = v.toObject();
                map.insert(o.value("i").toInt(), o.value("s").toString());
            }
            int i = 0;
            for (QTextBlock b = m_editor->document()->begin();
                 b != m_editor->document()->end(); b = b.next(), ++i) {
                auto it = map.constFind(i);
                if (it == map.constEnd()) continue;
                // Re-tag (visuals already round-tripped via HTML; heading levels too).
                QTextCursor c(b);
                QTextBlockFormat bf;
                bf.setProperty(kStyleNameProp, it.value());
                c.mergeBlockFormat(bf);
            }
        }
    }
    syncToCurrentFormat();
}

void WriterRibbon::pasteAsPlainText() {
    if (!m_editor) return;
    const QString text = QGuiApplication::clipboard()->text();
    if (!text.isEmpty())
        m_editor->textCursor().insertText(text);
    m_editor->setFocus();
}

void WriterRibbon::toggleFormatPainter(bool on) {
    if (!m_editor) {
        if (m_btnPainter && m_btnPainter->isChecked()) {
            QSignalBlocker block(m_btnPainter);
            m_btnPainter->setChecked(false);
        }
        return;
    }
    if (!on) { disarmFormatPainter(); return; }

    m_painterFmt = m_editor->currentCharFormat();

    // Copy only the paragraph properties a user expects the painter to carry.
    // Merging a whole QTextBlockFormat would also drag along page-break flags
    // and frame state, which produces surprises far from the click.
    const QTextBlockFormat src = m_editor->textCursor().blockFormat();
    QTextBlockFormat bf;
    bf.setAlignment(src.alignment());
    bf.setIndent(src.indent());
    bf.setTextIndent(src.textIndent());
    bf.setLeftMargin(src.leftMargin());
    bf.setRightMargin(src.rightMargin());
    bf.setTopMargin(src.topMargin());
    bf.setBottomMargin(src.bottomMargin());
    bf.setLineHeight(src.lineHeight(), src.lineHeightType());
    m_painterBlockFmt = bf;

    m_painterActive = true;
    m_editor->viewport()->setCursor(Qt::IBeamCursor);
}

// Paint the captured format onto whatever the user just clicked or selected.
// Re-entrancy is blocked by m_painterApplying: the document edits below emit
// selection and format signals that can route back here.
void WriterRibbon::applyPainterFormat() {
    if (!m_editor || !m_painterActive || m_painterApplying) return;

    QTextCursor cur = m_editor->textCursor();
    if (!cur.hasSelection()) {
        // A bare click paints the word under the caret, the way Word does.
        cur.select(QTextCursor::WordUnderCursor);
        if (!cur.hasSelection()) { disarmFormatPainter(); return; }
    }

    m_painterApplying = true;
    cur.beginEditBlock();
    cur.mergeCharFormat(m_painterFmt);
    cur.mergeBlockFormat(m_painterBlockFmt);
    cur.endEditBlock();
    m_painterApplying = false;

    disarmFormatPainter();
    syncToCurrentFormat();
}

void WriterRibbon::disarmFormatPainter() {
    m_painterActive = false;
    if (m_editor) m_editor->viewport()->unsetCursor();
    if (m_btnPainter && m_btnPainter->isChecked()) {
        QSignalBlocker block(m_btnPainter);   // must not re-enter toggleFormatPainter
        m_btnPainter->setChecked(false);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Find & Replace (modeless)
// ─────────────────────────────────────────────────────────────────────────────
void WriterRibbon::openFindReplace() {
    if (!m_editor) return;
    if (m_findDlg) { m_findDlg->show(); m_findDlg->raise(); m_findDlg->activateWindow(); return; }

    auto* dlg = new QDialog(window());
    dlg->setWindowTitle("Find and Replace");
    dlg->setModal(false);
    dlg->setStyleSheet(ThemeManager::inputDialogStyleSheet());
    auto* form = new QFormLayout(dlg);

    m_findEdit    = new QLineEdit(dlg);
    m_replaceEdit = new QLineEdit(dlg);
    m_matchCase   = new QCheckBox("Match case", dlg);
    m_wholeWord   = new QCheckBox("Whole word", dlg);
    m_useRegex    = new QCheckBox("Regular expression", dlg);
    m_findCount   = new QLabel(QString(), dlg);
    m_findCount->setObjectName("findCountLabel");
    form->addRow("Find:", m_findEdit);
    form->addRow("Replace with:", m_replaceEdit);
    auto* opts = new QHBoxLayout();
    opts->addWidget(m_matchCase); opts->addWidget(m_wholeWord); opts->addWidget(m_useRegex);
    opts->addStretch();
    form->addRow(opts);
    form->addRow(m_findCount);

    auto* row1 = new QHBoxLayout();
    auto* btnPrev    = new QPushButton("Find Previous", dlg);
    auto* btnNext    = new QPushButton("Find Next", dlg);
    auto* btnHi      = new QPushButton("Highlight All", dlg);
    row1->addWidget(btnPrev); row1->addWidget(btnNext); row1->addWidget(btnHi);
    form->addRow(row1);

    auto* row2 = new QHBoxLayout();
    auto* btnRep     = new QPushButton("Replace", dlg);
    auto* btnRepAll  = new QPushButton("Replace All", dlg);
    auto* btnClose   = new QPushButton("Close", dlg);
    row2->addWidget(btnRep); row2->addWidget(btnRepAll); row2->addWidget(btnClose);
    form->addRow(row2);

    auto flags = [this](bool backward) {
        QTextDocument::FindFlags f;
        if (m_matchCase->isChecked()) f |= QTextDocument::FindCaseSensitively;
        if (m_wholeWord->isChecked()) f |= QTextDocument::FindWholeWords;
        if (backward)                 f |= QTextDocument::FindBackward;
        return f;
    };
    auto regex = [this]() {
        return QRegularExpression(m_findEdit->text(),
            m_matchCase->isChecked() ? QRegularExpression::NoPatternOption
                                     : QRegularExpression::CaseInsensitiveOption);
    };
    auto findStep = [this, flags, regex](bool backward) -> bool {
        const QString needle = m_findEdit->text();
        if (needle.isEmpty()) return false;
        bool ok;
        if (m_useRegex->isChecked()) {
            const QRegularExpression re = regex();
            if (!re.isValid()) { m_findCount->setText("Invalid regular expression"); return false; }
            ok = m_editor->find(re, flags(backward));
            if (!ok) { QTextCursor c = m_editor->textCursor();
                       c.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
                       m_editor->setTextCursor(c); ok = m_editor->find(re, flags(backward)); }
        } else {
            ok = m_editor->find(needle, flags(backward));
            if (!ok) { QTextCursor c = m_editor->textCursor();
                       c.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
                       m_editor->setTextCursor(c); ok = m_editor->find(needle, flags(backward)); }
        }
        if (!ok) m_findCount->setText("No matches");
        return ok;
    };

    connect(btnNext, &QPushButton::clicked, dlg, [findStep]{ findStep(false); });
    connect(btnPrev, &QPushButton::clicked, dlg, [findStep]{ findStep(true); });
    connect(btnHi,   &QPushButton::clicked, this, &WriterRibbon::highlightAllMatches);
    connect(m_findEdit, &QLineEdit::textChanged, this, &WriterRibbon::highlightAllMatches);
    connect(m_matchCase, &QCheckBox::toggled, this, &WriterRibbon::highlightAllMatches);
    connect(m_wholeWord, &QCheckBox::toggled, this, &WriterRibbon::highlightAllMatches);
    connect(m_useRegex,  &QCheckBox::toggled, this, &WriterRibbon::highlightAllMatches);

    connect(btnRep, &QPushButton::clicked, dlg, [this, findStep]{
        QTextCursor c = m_editor->textCursor();
        if (c.hasSelection()) { c.insertText(m_replaceEdit->text()); }
        findStep(false);
        highlightAllMatches();
    });
    connect(btnRepAll, &QPushButton::clicked, dlg, [this, flags, regex]{
        const QString needle = m_findEdit->text();
        if (needle.isEmpty()) return;
        QTextCursor c = m_editor->textCursor();
        c.beginEditBlock();
        c.movePosition(QTextCursor::Start);
        m_editor->setTextCursor(c);
        int n = 0;
        const bool rx = m_useRegex->isChecked();
        const QRegularExpression re = regex();
        if (rx && !re.isValid()) { c.endEditBlock(); m_findCount->setText("Invalid regular expression"); return; }
        while (rx ? m_editor->find(re, flags(false)) : m_editor->find(needle, flags(false))) {
            m_editor->textCursor().insertText(m_replaceEdit->text());
            ++n;
        }
        c.endEditBlock();
        m_findCount->setText(QString("Replaced %1").arg(n));
        highlightAllMatches();
    });
    connect(btnClose, &QPushButton::clicked, dlg, [this]{
        m_editor->setExtraSelections({});   // clear highlights
        if (m_findDlg) m_findDlg->close();
    });
    connect(dlg, &QDialog::finished, this, [this]{ m_editor->setExtraSelections({}); });

    m_findDlg = dlg;
    dlg->show();
    m_findEdit->setFocus();
}

void WriterRibbon::highlightAllMatches() {
    if (!m_editor || !m_findEdit) return;
    QList<QTextEdit::ExtraSelection> sels;
    const QString needle = m_findEdit->text();
    if (!needle.isEmpty()) {
        QTextDocument* doc = m_editor->document();
        QTextDocument::FindFlags f;
        if (m_matchCase->isChecked()) f |= QTextDocument::FindCaseSensitively;
        if (m_wholeWord->isChecked()) f |= QTextDocument::FindWholeWords;
        QTextCharFormat hf; hf.setBackground(QColor("#FFE27A")); hf.setForeground(QColor("#1C1E26"));
        const bool rx = m_useRegex->isChecked();
        QRegularExpression re(needle, m_matchCase->isChecked()
            ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption);
        if (rx && !re.isValid()) { if (m_findCount) m_findCount->setText("Invalid regular expression"); return; }
        QTextCursor c(doc);
        for (;;) {
            QTextCursor found = rx ? doc->find(re, c, f) : doc->find(needle, c, f);
            if (found.isNull()) break;
            QTextEdit::ExtraSelection es; es.cursor = found; es.format = hf;
            sels.append(es);
            c = found;
            c.setPosition(found.selectionEnd());
            if (found.selectionStart() == found.selectionEnd()) {   // zero-width match guard
                if (c.atEnd()) break;
                c.movePosition(QTextCursor::NextCharacter);
            }
        }
    }
    m_editor->setExtraSelections(sels);
    if (m_findCount)
        m_findCount->setText(sels.isEmpty() ? (needle.isEmpty() ? QString() : "No matches")
                                            : QString("%1 match%2").arg(sels.size()).arg(sels.size() == 1 ? "" : "es"));
}

void WriterRibbon::selectSimilarFormatting() {
    if (!m_editor) return;
    QTextDocument* doc = m_editor->document();
    const QTextCharFormat ref = m_editor->currentCharFormat();

    auto matches = [&](int pos) {
        QTextCursor c(doc);
        c.setPosition(pos);
        c.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        if (!c.hasSelection()) return false;
        const QTextCharFormat f = c.charFormat();
        return f.fontWeight() == ref.fontWeight()
            && f.fontItalic() == ref.fontItalic()
            && f.fontUnderline() == ref.fontUnderline()
            && qFuzzyCompare(f.fontPointSize() + 1, ref.fontPointSize() + 1);
    };

    const int origin = m_editor->textCursor().position();
    int lo = origin, hi = origin;
    while (lo > 0 && matches(lo - 1)) --lo;
    const int last = doc->characterCount() - 1;
    while (hi < last && matches(hi)) ++hi;

    QTextCursor sel(doc);
    sel.setPosition(lo);
    sel.setPosition(hi, QTextCursor::KeepAnchor);
    m_editor->setTextCursor(sel);
    m_editor->setFocus();
}

// ─────────────────────────────────────────────────────────────────────────────
// Sync (cursor → ribbon state)
// ─────────────────────────────────────────────────────────────────────────────
void WriterRibbon::syncToCurrentFormat() {
    if (!m_editor || m_syncing) return;
    m_syncing = true;

    const QTextCharFormat fmt = m_editor->currentCharFormat();

    m_btnBold->setChecked(fmt.fontWeight() >= QFont::Bold);
    m_btnItalic->setChecked(fmt.fontItalic());
    m_btnUnderline->setChecked(fmt.fontUnderline());
    m_btnStrike->setChecked(fmt.fontStrikeOut());
    m_btnSuper->setChecked(fmt.verticalAlignment() == QTextCharFormat::AlignSuperScript);
    m_btnSub->setChecked(fmt.verticalAlignment() == QTextCharFormat::AlignSubScript);

    // Fall back to the document default. A run that never had an explicit
    // family or size reports an empty family and a point size of 0, and
    // skipping the update left the boxes showing whatever was set last rather
    // than what is actually under the cursor.
    const QFont base = m_editor->document()->defaultFont();

    QString family = fmt.fontFamilies().toStringList().value(0);
    if (family.isEmpty()) family = base.family();
    if (!family.isEmpty()) {
        const int idx = m_fontCombo->findText(family);
        if (idx >= 0) m_fontCombo->setCurrentIndex(idx);
        else m_fontCombo->setCurrentText(family);
    }

    qreal pt = fmt.fontPointSize();
    if (pt <= 0) pt = base.pointSizeF();
    if (pt > 0) m_sizeCombo->setCurrentText(QString::number(qRound(pt)));

    const Qt::Alignment a = m_editor->alignment();
    m_btnAlignLeft->setChecked(a.testFlag(Qt::AlignLeft));
    m_btnAlignCenter->setChecked(a.testFlag(Qt::AlignHCenter));
    m_btnAlignRight->setChecked(a.testFlag(Qt::AlignRight));
    m_btnAlignJustify->setChecked(a.testFlag(Qt::AlignJustify));

    // Active paragraph style — highlight the matching gallery chip.
    {
        const QTextBlockFormat bf = m_editor->textCursor().blockFormat();
        QString nm = bf.stringProperty(kStyleNameProp);
        if (nm.isEmpty()) nm = "Normal";
        if (auto* b = m_styleButtons.value(nm)) b->setChecked(true);
        else if (auto* nb = m_styleButtons.value("Normal")) nb->setChecked(true);
    }

    // Contextual Table tab follows the cursor in/out of tables.
    refreshTableTab();

    m_syncing = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Collapsible ribbon — double-click a tab to toggle
// ─────────────────────────────────────────────────────────────────────────────
bool WriterRibbon::eventFilter(QObject* obj, QEvent* ev) {
    if (ev->type() == QEvent::MouseButtonDblClick) {
        if (auto* btn = qobject_cast<QToolButton*>(obj)) {
            if (m_tabGroup->buttons().contains(btn)) {
                setRibbonCollapsed(!m_collapsed);
                return true;
            }
        }
    }

    // ── Format painter ──────────────────────────────────────────────────────
    // Release, not selectionChanged: by the time the button comes up the
    // selection is final, so we paint once instead of on every drag step.
    if (m_painterActive && m_editor) {
        if (obj == m_editor->viewport()
            && ev->type() == QEvent::MouseButtonRelease
            && static_cast<QMouseEvent*>(ev)->button() == Qt::LeftButton) {
            applyPainterFormat();
        } else if (obj == m_editor
                   && ev->type() == QEvent::KeyPress
                   && static_cast<QKeyEvent*>(ev)->key() == Qt::Key_Escape) {
            disarmFormatPainter();
            return true;
        }
    }

    return QWidget::eventFilter(obj, ev);
}

// ─────────────────────────────────────────────────────────────────────────────
// Small builders
// ─────────────────────────────────────────────────────────────────────────────
QToolButton* WriterRibbon::makeTabButton(const QString& label) {
    auto* btn = new QToolButton(this);
    btn->setText(label);
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setObjectName("ribbonTabBtn");
    return btn;
}

QToolButton* WriterRibbon::makeToolBtn(const QString& text, const QString& tip, bool checkable) {
    auto* btn = new QToolButton(this);
    btn->setText(text);
    btn->setToolTip(tip);
    btn->setCheckable(checkable);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFocusPolicy(Qt::NoFocus);          // never steal focus from the editor
    btn->setObjectName("ribbonToolBtn");
    btn->setFixedSize(30, 28);
    return btn;
}

QToolButton* WriterRibbon::makeIconBtn(const QIcon& icon, const QString& tip, bool checkable) {
    auto* btn = new QToolButton(this);
    btn->setIcon(icon);
    btn->setIconSize(QSize(18, 18));
    btn->setToolTip(tip);
    btn->setCheckable(checkable);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setObjectName("ribbonToolBtn");
    btn->setFixedSize(30, 28);
    return btn;
}

QToolButton* WriterRibbon::makeBigBtn(const QIcon& icon, const QString& text,
                                       const QString& tip, bool checkable) {
    auto* btn = new QToolButton(this);
    btn->setIcon(icon);
    btn->setIconSize(QSize(22, 22));
    btn->setText(text);
    btn->setToolTip(tip.isEmpty() ? text : tip);
    btn->setCheckable(checkable);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setObjectName("ribbonBigBtn");
    btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btn->setFixedHeight(60);
    btn->setMinimumWidth(56);
    return btn;
}

QToolButton* WriterRibbon::makeRowBtn(const QIcon& icon, const QString& text,
                                       const QString& tip, bool checkable) {
    auto* btn = new QToolButton(this);
    btn->setIcon(icon);
    btn->setIconSize(QSize(18, 18));
    btn->setText("  " + text);
    btn->setToolTip(tip.isEmpty() ? text : tip);
    btn->setCheckable(checkable);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setObjectName("ribbonRowBtn");
    btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    btn->setFixedHeight(26);
    btn->setMinimumWidth(112);
    return btn;
}

QWidget* WriterRibbon::makeSeparator() {
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedWidth(1);
    sep->setObjectName("ribbonSep");
    return sep;
}

void WriterRibbon::setRibbonCollapsed(bool on) {
    m_collapsed = on;
    if (m_stack) m_stack->setVisible(!on);
    if (m_collapseBtn) {
        m_collapseBtn->setText(on ? QStringLiteral("⌄") : QStringLiteral("⌃"));
        m_collapseBtn->setToolTip(on ? "Show the ribbon  (or double-click a tab)"
                                     : "Collapse the ribbon  (or double-click a tab)");
    }
}

QWidget* WriterRibbon::makeGroup(const QString& name, const QList<QWidget*>& widgets) {
    auto* group = new QWidget(this);
    auto* v = new QVBoxLayout(group);
    v->setContentsMargins(6, 2, 6, 0);
    v->setSpacing(2);

    auto* row = new QWidget(group);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(3);
    for (QWidget* w : widgets) { w->setParent(row); h->addWidget(w); }
    h->addStretch();

    auto* label = new QLabel(name, group);
    label->setObjectName("ribbonGroupLabel");
    label->setAlignment(Qt::AlignHCenter);
    // Reserve the caption's line box explicitly. Left to share leftover space
    // it was the first thing squeezed when a group grew tall, so it rendered
    // clipped rather than simply sitting lower.
    label->setFixedHeight(13);

    v->addWidget(row, 1);
    v->addWidget(label, 0);
    return group;
}

// ─────────────────────────────────────────────────────────────────────────────
// Styling
// ─────────────────────────────────────────────────────────────────────────────
void WriterRibbon::applyStyles() {
    const bool dark = ThemeManager::instance().isDark();
    const QString arrowPath = QDir(QDir::tempPath()).filePath(
        dark ? "nativeoffice_writer_arrow_dark.png" : "nativeoffice_writer_arrow.png");
    {
        QPixmap pm(20, 12);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(dark ? QColor(154, 164, 184) : QColor(90, 96, 110));
        p.setPen(Qt::NoPen);
        QPolygonF tri; tri << QPointF(5, 4) << QPointF(15, 4) << QPointF(10, 10);
        p.drawPolygon(tri);
        p.end();
        pm.save(arrowPath, "PNG");
    }

    if (dark) {
        setStyleSheet(QString(R"(
QWidget#writerRibbon {
    background-color: #12161F;
    border-bottom: 1px solid #2A3344;
}
QWidget#ribbonTabRow {
    background-color: #0D1117;
    border-bottom: 1px solid #2A3344;
}
QToolButton#ribbonTabBtn {
    color: #9AA4B8;
    background: transparent;
    border: none;
    padding: 5px 16px;
    font-size: 12px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-weight: 500;
}
QToolButton#ribbonTabBtn:hover {
    color: #E6E9F0;
    background: #1E2737;
}
QToolButton#ribbonTabBtn:checked {
    color: #E6E9F0;
    background-color: #12161F;
    border-bottom: 2px solid #E8372A;
    font-weight: 700;
}
QToolButton#ribbonTableTabBtn {
    color: #FF9A8C;
    background: #3A1F1F;
    border: none;
    border-top-left-radius: 4px;
    border-top-right-radius: 4px;
    padding: 5px 16px;
    margin: 0 1px;
    font-size: 12px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-weight: 600;
}
QToolButton#ribbonTableTabBtn:hover { background: #4A2620; color: #FF9A8C; }
QToolButton#ribbonTableTabBtn:checked {
    color: #FFFFFF;
    background-color: #E8372A;
    border-bottom: 2px solid #E8372A;
    font-weight: 700;
}
QStackedWidget#ribbonStack { background-color: #12161F; }
QScrollArea#ribbonScroll { background-color: #12161F; border: none; }
QLabel#ribbonGroupLabel {
    color: #9AA4B8;
    font-size: 10px;
    font-family: "Segoe UI", "Inter", sans-serif;
}
QLabel#ribbonComingSoon {
    color: #9AA4B8;
    font-size: 14px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-style: italic;
}
QToolButton#ribbonToolBtn {
    color: #E6E9F0;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    font-size: 13px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-weight: 500;
}
QToolButton#ribbonToolBtn:hover {
    background: #1E2737; color: #E6E9F0; border-color: #2A3344;
}
QToolButton#ribbonToolBtn:checked {
    background-color: #3A1F1F; color: #FF9A8C; border-color: #E8372A; font-weight: 700;
}
QToolButton#ribbonToolBtn:pressed { background-color: #4A2620; }
QToolButton#ribbonToolBtn:disabled { color: #565F70; }
QToolButton#ribbonToolBtn::menu-button { width: 12px; border: none; background: transparent; }
QToolButton#ribbonToolBtn::menu-arrow { image: url("%1"); width: 8px; height: 5px; }
QToolButton#ribbonBigBtn {
    color: #E6E9F0;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 3px 4px;
    font-size: 11px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-weight: 500;
}
QToolButton#ribbonBigBtn:hover {
    background: #1E2737; color: #E6E9F0; border-color: #2A3344;
}
QToolButton#ribbonBigBtn:checked {
    background-color: #3A1F1F; color: #FF9A8C; border-color: #E8372A; font-weight: 700;
}
QToolButton#ribbonBigBtn:pressed { background-color: #4A2620; }
QToolButton#ribbonBigBtn::menu-button { width: 14px; border: none; background: transparent; }
QToolButton#ribbonBigBtn::menu-arrow { image: url("%1"); width: 8px; height: 5px; }
QToolButton#ribbonStyleBtn {
    color: #E6E9F0;
    background: #12161F;
    border: 1px solid #2A3344;
    border-radius: 5px;
    padding: 0 6px;
    font-family: "Segoe UI", "Inter", sans-serif;
}
QToolButton#ribbonStyleBtn:hover { border-color: #3A4456; background: #17233B; }
QToolButton#ribbonStyleBtn:checked {
    background-color: #3A1F1F; color: #FF9A8C; border-color: #E8372A;
}
QComboBox#ribbonCombo {
    background-color: #12161F;
    color: #E6E9F0;
    border: 1px solid #2A3344;
    border-radius: 6px;
    padding: 3px 6px;
    min-height: 20px;
    font-size: 12px;
    font-family: "Segoe UI", "Inter", sans-serif;
}
QComboBox#ribbonCombo:hover { border-color: #3A4456; background-color: #17233B; }
QComboBox#ribbonCombo:focus { border-color: #6D5BE8; }
QComboBox#ribbonCombo::drop-down {
    subcontrol-origin: padding; subcontrol-position: center right; border: none; width: 16px;
}
QComboBox#ribbonCombo::down-arrow { image: url("%1"); width: 10px; height: 6px; }
QComboBox QAbstractItemView {
    background-color: #12161F;
    color: #E6E9F0;
    border: 1px solid #2A3344;
    border-radius: 6px;
    selection-background-color: #6D5BE8;
    selection-color: #FFFFFF;
    outline: none;
    padding: 4px;
}
QComboBox QAbstractItemView::item { min-height: 24px; padding-left: 6px; border-radius: 4px; }
/* Once ::item carries any rule, Qt paints the row itself and stops honouring
   selection-background-color, while selection-color still tints the text —
   which left the highlighted row white-on-white and looked like the entry had
   gone missing from the list. Both halves must be stated together. */
QComboBox QAbstractItemView::item:selected,
QComboBox QAbstractItemView::item:hover {
    background-color: #6D5BE8;
    color: #FFFFFF;
}
QToolButton#ribbonRowBtn {
    color: #E6E9F0;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 2px 8px 2px 4px;
    font-size: 12px;
    font-family: "Segoe UI", "Inter", sans-serif;
    text-align: left;
}
QToolButton#ribbonRowBtn:hover { background: #1E2737; color: #E6E9F0; border-color: #2A3344; }
QToolButton#ribbonRowBtn:checked { background-color: #3A1F1F; color: #FF9A8C; border-color: #E8372A; }
QToolButton#ribbonRowBtn:pressed { background-color: #4A2620; }
QToolButton#ribbonRowBtn::menu-indicator { image: url("%1"); subcontrol-position: right center; width: 8px; }
QFrame#ribbonSep { background-color: #2A3344; border: none; margin: 6px 2px 16px 2px; }
QMenu {
    background-color: #12161F; color: #E6E9F0;
    border: 1px solid #2A3344; border-radius: 8px; padding: 4px;
    font-family: "Segoe UI", "Inter", sans-serif; font-size: 12px;
}
QMenu::item { padding: 6px 18px; border-radius: 5px; }
QMenu::item:selected { background-color: #3A1F1F; color: #FF9A8C; }
QMenu::separator { height: 1px; background: #2A3344; margin: 4px 8px; }
)").arg(arrowPath));
    } else {
        setStyleSheet(QString(R"(
QWidget#writerRibbon {
    background-color: #FFFFFF;
    border-bottom: 1px solid #D7DAE0;
}
QWidget#ribbonTabRow {
    background-color: #F3F4F6;
    border-bottom: 1px solid #E2E4E9;
}
QToolButton#ribbonTabBtn {
    color: #5A6071;
    background: transparent;
    border: none;
    padding: 5px 16px;
    font-size: 12px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-weight: 500;
}
QToolButton#ribbonTabBtn:hover {
    color: #1C1E26;
    background: #E7E9EE;
}
QToolButton#ribbonTabBtn:checked {
    color: #1C1E26;
    background-color: #FFFFFF;
    border-bottom: 2px solid #6D5BE8;
    font-weight: 700;
}
QToolButton#ribbonTableTabBtn {
    color: #4C3BD6;
    background: #EDE9FC;
    border: none;
    border-top-left-radius: 4px;
    border-top-right-radius: 4px;
    padding: 5px 16px;
    margin: 0 1px;
    font-size: 12px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-weight: 600;
}
QToolButton#ribbonTableTabBtn:hover { background: #DFD8FA; color: #3D2EC0; }
QToolButton#ribbonTableTabBtn:checked {
    color: #FFFFFF;
    background-color: #6D5BE8;
    border-bottom: 2px solid #6D5BE8;
    font-weight: 700;
}
QStackedWidget#ribbonStack { background-color: #FFFFFF; }
QScrollArea#ribbonScroll { background-color: #FFFFFF; border: none; }
QLabel#ribbonGroupLabel {
    color: #9097A3;
    font-size: 10px;
    font-family: "Segoe UI", "Inter", sans-serif;
}
QLabel#ribbonComingSoon {
    color: #9097A3;
    font-size: 14px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-style: italic;
}
QToolButton#ribbonToolBtn {
    color: #3A3F4B;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    font-size: 13px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-weight: 500;
}
QToolButton#ribbonToolBtn:hover {
    background: #ECEEF2; color: #1C1E26; border-color: #DCDFE6;
}
QToolButton#ribbonToolBtn:checked {
    background-color: #EDE9FC; color: #4C3BD6; border-color: #6D5BE8; font-weight: 700;
}
QToolButton#ribbonToolBtn:pressed { background-color: #DFD8FA; }
QToolButton#ribbonToolBtn:disabled { color: #C2C6CE; }
QToolButton#ribbonToolBtn::menu-button { width: 12px; border: none; background: transparent; }
QToolButton#ribbonToolBtn::menu-arrow { image: url("%1"); width: 8px; height: 5px; }
QToolButton#ribbonBigBtn {
    color: #3A3F4B;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 3px 4px;
    font-size: 11px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-weight: 500;
}
QToolButton#ribbonBigBtn:hover {
    background: #ECEEF2; color: #1C1E26; border-color: #DCDFE6;
}
QToolButton#ribbonBigBtn:checked {
    background-color: #EDE9FC; color: #4C3BD6; border-color: #6D5BE8; font-weight: 700;
}
QToolButton#ribbonBigBtn:pressed { background-color: #DFD8FA; }
QToolButton#ribbonBigBtn::menu-button { width: 14px; border: none; background: transparent; }
QToolButton#ribbonBigBtn::menu-arrow { image: url("%1"); width: 8px; height: 5px; }
QToolButton#ribbonStyleBtn {
    color: #2F3440;
    background: #FFFFFF;
    border: 1px solid #E2E4E9;
    border-radius: 5px;
    padding: 0 6px;
    font-family: "Segoe UI", "Inter", sans-serif;
}
QToolButton#ribbonStyleBtn:hover { border-color: #B9BEC9; background: #F7F8FA; }
QToolButton#ribbonStyleBtn:checked {
    background-color: #EDE9FC; color: #4C3BD6; border-color: #6D5BE8;
}
QComboBox#ribbonCombo {
    background-color: #FFFFFF;
    color: #1C1E26;
    border: 1px solid #D5D8DF;
    border-radius: 6px;
    padding: 3px 6px;
    min-height: 20px;
    font-size: 12px;
    font-family: "Segoe UI", "Inter", sans-serif;
}
QComboBox#ribbonCombo:hover { border-color: #9CA3AF; background-color: #FAFBFC; }
QComboBox#ribbonCombo:focus { border-color: #6D5BE8; }
QComboBox#ribbonCombo::drop-down {
    subcontrol-origin: padding; subcontrol-position: center right; border: none; width: 16px;
}
QComboBox#ribbonCombo::down-arrow { image: url("%1"); width: 10px; height: 6px; }
QComboBox QAbstractItemView {
    background-color: #FFFFFF;
    color: #1C1E26;
    border: 1px solid #D5D8DF;
    border-radius: 6px;
    selection-background-color: #6D5BE8;
    selection-color: #FFFFFF;
    outline: none;
    padding: 4px;
}
QComboBox QAbstractItemView::item { min-height: 24px; padding-left: 6px; border-radius: 4px; }
/* Once ::item carries any rule, Qt paints the row itself and stops honouring
   selection-background-color, while selection-color still tints the text —
   which left the highlighted row white-on-white and looked like the entry had
   gone missing from the list. Both halves must be stated together. */
QComboBox QAbstractItemView::item:selected,
QComboBox QAbstractItemView::item:hover {
    background-color: #6D5BE8;
    color: #FFFFFF;
}
QToolButton#ribbonRowBtn {
    color: #3A3F4B;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 2px 8px 2px 4px;
    font-size: 12px;
    font-family: "Segoe UI", "Inter", sans-serif;
    text-align: left;
}
QToolButton#ribbonRowBtn:hover { background: #ECEEF2; color: #1C1E26; border-color: #DCDFE6; }
QToolButton#ribbonRowBtn:checked { background-color: #EDE9FC; color: #4C3BD6; border-color: #6D5BE8; }
QToolButton#ribbonRowBtn:pressed { background-color: #DFD8FA; }
QToolButton#ribbonRowBtn::menu-indicator { image: url("%1"); subcontrol-position: right center; width: 8px; }
QFrame#ribbonSep { background-color: #E2E4E9; border: none; margin: 6px 2px 16px 2px; }
QMenu {
    background-color: #FFFFFF; color: #1C1E26;
    border: 1px solid #D5D8DF; border-radius: 8px; padding: 4px;
    font-family: "Segoe UI", "Inter", sans-serif; font-size: 12px;
}
QMenu::item { padding: 6px 18px; border-radius: 5px; }
QMenu::item:selected { background-color: #EDE9FC; color: #4C3BD6; }
QMenu::separator { height: 1px; background: #E2E4E9; margin: 4px 8px; }
)").arg(arrowPath));
    }
}

} // namespace NativeOffice
