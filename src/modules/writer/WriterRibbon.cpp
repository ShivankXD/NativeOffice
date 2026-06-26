// ─────────────────────────────────────────────────────────────────────────────
// WriterRibbon.cpp  (Sprint 14)
// WPS/Word-style tabbed ribbon implementation for NativeOffice Writer.
// ─────────────────────────────────────────────────────────────────────────────
#include "WriterRibbon.h"
#include "core/theme/ThemeManager.h"

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
#include <QTextBlock>
#include <QTextOption>
#include <QTextDocument>
#include <QDateTime>
#include <QImage>
#include <QBuffer>
#include <QByteArray>
#include <QLinearGradient>
#include <QSpinBox>
#include <QPainter>
#include <QPixmap>
#include <QIcon>
#include <QFontMetrics>
#include <QtMath>
#include <QStyledItemDelegate>
#include <QListView>
#include <QPolygonF>
#include <QPainterPath>
#include <QEvent>
#include <QMouseEvent>
#include <QDir>
#include <QStringList>
#include <algorithm>
#include <functional>

namespace NativeOffice {

// Custom block-format property: which named style a paragraph carries.
static constexpr int kStyleProp = QTextFormat::UserProperty + 11;

namespace {

const QColor kIconColor("#3A3F4B");

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

// Resolve the visual attributes of a built-in paragraph style.
void styleFormats(WriterStyle s, QTextCharFormat& cf, QTextBlockFormat& bf) {
    cf.setFontFamilies({"Segoe UI", "Inter", "Roboto", "sans-serif"});
    cf.setFontItalic(false);
    cf.setFontWeight(QFont::Normal);
    cf.setForeground(QColor("#1C1E26"));
    cf.setFontPointSize(12);

    bf.setTopMargin(0);
    bf.setBottomMargin(6);
    bf.setLeftMargin(0);
    bf.setIndent(0);
    bf.setAlignment(Qt::AlignLeft | Qt::AlignAbsolute);

    switch (s) {
    case WriterStyle::Normal:    break;
    case WriterStyle::NoSpacing: bf.setBottomMargin(0); break;
    case WriterStyle::Heading1:
        cf.setFontPointSize(24); cf.setFontWeight(QFont::Bold);
        bf.setTopMargin(14); bf.setBottomMargin(6); break;
    case WriterStyle::Heading2:
        cf.setFontPointSize(18); cf.setFontWeight(QFont::Bold);
        cf.setForeground(QColor("#2C3140"));
        bf.setTopMargin(12); bf.setBottomMargin(4); break;
    case WriterStyle::Heading3:
        cf.setFontPointSize(14); cf.setFontWeight(QFont::Bold);
        cf.setForeground(QColor("#3A4054"));
        bf.setTopMargin(10); bf.setBottomMargin(3); break;
    case WriterStyle::Title:
        cf.setFontPointSize(32); cf.setFontWeight(QFont::Light);
        bf.setAlignment(Qt::AlignHCenter);
        bf.setTopMargin(6); bf.setBottomMargin(4); break;
    case WriterStyle::Subtitle:
        cf.setFontPointSize(16); cf.setFontItalic(true);
        cf.setForeground(QColor("#6B7280"));
        bf.setAlignment(Qt::AlignHCenter);
        bf.setBottomMargin(10); break;
    case WriterStyle::Quote:
        cf.setFontPointSize(13); cf.setFontItalic(true);
        cf.setForeground(QColor("#4B5563"));
        bf.setLeftMargin(34); bf.setIndent(0);
        bf.setBackground(QColor("#F3F4F6"));
        bf.setTopMargin(6); bf.setBottomMargin(6); break;
    }
}

struct StyleDef { WriterStyle id; const char* name; };
const StyleDef kStyles[] = {
    { WriterStyle::Normal,    "Normal" },
    { WriterStyle::NoSpacing, "No Spacing" },
    { WriterStyle::Heading1,  "Heading 1" },
    { WriterStyle::Heading2,  "Heading 2" },
    { WriterStyle::Heading3,  "Heading 3" },
    { WriterStyle::Title,     "Title" },
    { WriterStyle::Subtitle,  "Subtitle" },
    { WriterStyle::Quote,     "Quote" },
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────
WriterRibbon::WriterRibbon(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("writerRibbon");

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
        "Home", "Insert", "Page Layout", "References", "Review", "View", "Tools"
    };
    for (int i = 0; i < tabNames.size(); ++i) {
        auto* btn = makeTabButton(tabNames[i]);
        btn->installEventFilter(this);          // double-click → collapse
        m_tabGroup->addButton(btn, i);
        tabLayout->addWidget(btn);
    }
    tabLayout->addStretch();
    m_tabGroup->button(0)->setChecked(true);

    // ── Stacked content ─────────────────────────────────────────────────────
    m_stack = new QStackedWidget(this);
    m_stack->setObjectName("ribbonStack");
    m_stack->setFixedHeight(100);
    m_stack->addWidget(buildHomeTab());      // 0 — Home
    m_stack->addWidget(buildInsertTab());    // 1 — Insert
    for (int i = 2; i < tabNames.size(); ++i)
        m_stack->addWidget(buildPlaceholderTab(tabNames[i]));

    connect(m_tabGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_stack->setCurrentIndex(id);
        if (m_collapsed) {                       // a click re-opens a collapsed ribbon
            m_collapsed = false;
            m_stack->setVisible(true);
        }
    });

    root->addWidget(tabRow);
    root->addWidget(m_stack);

    applyStyles();
}

// ─────────────────────────────────────────────────────────────────────────────
// Editor wiring
// ─────────────────────────────────────────────────────────────────────────────
void WriterRibbon::attachEditor(QTextEdit* editor) {
    if (m_editor == editor) return;
    m_editor = editor;

    connect(m_editor, &QTextEdit::cursorPositionChanged,
            this, &WriterRibbon::syncToCurrentFormat);
    connect(m_editor, &QTextEdit::currentCharFormatChanged,
            this, [this](const QTextCharFormat&) { syncToCurrentFormat(); });

    // Format painter: apply once the user makes a selection while it's armed.
    connect(m_editor, &QTextEdit::selectionChanged, this, [this] {
        if (!m_painterActive || !m_painterFmt) return;
        QTextCursor cur = m_editor->textCursor();
        if (!cur.hasSelection()) return;
        cur.mergeCharFormat(*m_painterFmt);
        m_painterActive = false;
        if (m_btnPainter) m_btnPainter->setChecked(false);
        m_editor->viewport()->unsetCursor();
    });

    // Ctrl+F → Find & Replace (QTextEdit has no default binding for it).
    auto* find = new QShortcut(QKeySequence::Find, m_editor);
    connect(find, &QShortcut::activated, this, &WriterRibbon::openFindReplace);

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
    connect(m_fontCombo, &QComboBox::currentTextChanged, this, [this](const QString& fam) {
        if (m_syncing || fam.isEmpty()) return;
        QTextCharFormat fmt; fmt.setFontFamilies({fam});
        mergeFormatOnSelection(fmt);
    });

    m_sizeCombo = new QComboBox(tab);
    m_sizeCombo->setObjectName("ribbonCombo");
    m_sizeCombo->setEditable(true);
    m_sizeCombo->setFixedWidth(54);
    m_sizeCombo->setToolTip("Font size");
    for (int s : {8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 28, 32, 36, 48, 60, 72})
        m_sizeCombo->addItem(QString::number(s));
    m_sizeCombo->setCurrentText("12");
    connect(m_sizeCombo, &QComboBox::currentTextChanged, this, [this](const QString& s) {
        if (m_syncing) return;
        bool ok = false; const int pt = s.toInt(&ok);
        if (!ok || pt <= 0) return;
        QTextCharFormat fmt; fmt.setFontPointSize(pt);
        mergeFormatOnSelection(fmt);
    });

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
                                      "QToolButton:hover{border:2px solid #E8372A;}").arg(hex));
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
                                      "QToolButton:hover{border:2px solid #E8372A;}").arg(hex));
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
                                      "QToolButton:hover{border:2px solid #E8372A;}").arg(hex));
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
    m_styleGroup = new QButtonGroup(this);
    m_styleGroup->setExclusive(true);
    auto* styleGrid = new QWidget(tab);
    auto* sgl = new QGridLayout(styleGrid);
    sgl->setContentsMargins(0, 0, 0, 0); sgl->setSpacing(3);
    int si = 0;
    for (const auto& sd : kStyles) {
        auto* b = new QToolButton(styleGrid);
        b->setObjectName("ribbonStyleBtn");
        b->setText(sd.name);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(92, 28);
        b->setToolTip(QString("Apply the %1 style").arg(sd.name));
        // Give each chip a hint of its own look.
        QFont f("Segoe UI", 9);
        switch (sd.id) {
        case WriterStyle::Heading1: f.setPointSize(11); f.setBold(true); break;
        case WriterStyle::Heading2: f.setPointSize(10); f.setBold(true); break;
        case WriterStyle::Heading3: f.setBold(true); break;
        case WriterStyle::Title:    f.setPointSize(11); break;
        case WriterStyle::Subtitle: f.setItalic(true); break;
        case WriterStyle::Quote:    f.setItalic(true); break;
        default: break;
        }
        b->setFont(f);
        const WriterStyle id = sd.id;
        m_styleGroup->addButton(b, static_cast<int>(id));
        connect(b, &QToolButton::clicked, this, [this, id] { applyParagraphStyle(id); });
        sgl->addWidget(b, si / 4, si % 4);
        ++si;
    }
    m_styleGroup->button(static_cast<int>(WriterStyle::Normal))->setChecked(true);

    auto* btnMoreStyles = makeIconBtn(paintIcon([](QPainter& p) {
        p.setBrush(kIconColor); p.setPen(Qt::NoPen);
        for (int x : {12, 20, 28}) p.drawEllipse(QPointF(x, 28), 2.0, 2.0);
        p.setPen(QPen(kIconColor, 2.2)); p.setBrush(Qt::NoBrush);
        p.drawText(QRectF(6, 6, 28, 16), Qt::AlignCenter, "AA");
    }), "More styles");
    btnMoreStyles->setPopupMode(QToolButton::InstantPopup);
    {
        auto* m = new QMenu(btnMoreStyles);
        for (const auto& sd : kStyles) {
            const WriterStyle id = sd.id;
            m->addAction(sd.name, this, [this, id] { applyParagraphStyle(id); });
        }
        btnMoreStyles->setMenu(m);
    }

    auto* stylesRow = new QWidget(tab);
    auto* srl = new QHBoxLayout(stylesRow);
    srl->setContentsMargins(0, 0, 0, 0); srl->setSpacing(4);
    srl->addWidget(styleGrid);
    srl->addWidget(btnMoreStyles);

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

    layout->addWidget(makeGroup("Editing", { btnFind, btnSelect }));
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
    auto* btnCover     = makeBigBtn(coverPageIcon(),    "Cover\nPage",  "Insert a formatted cover page");
    auto* btnPageBreak = makeBigBtn(insPageBreakIcon(), "Page\nBreak",  "Insert a page break");
    auto* btnBlankPage = makeBigBtn(insBlankPageIcon(), "Blank\nPage",  "Insert a blank page");
    connect(btnCover,     &QToolButton::clicked, this, &WriterRibbon::insertCoverPage);
    connect(btnPageBreak, &QToolButton::clicked, this, &WriterRibbon::insertPageBreak);
    connect(btnBlankPage, &QToolButton::clicked, this, [this] {
        insertPageBreak(); insertPageBreak();   // a blank page = two breaks around an empty one
    });
    layout->addWidget(makeGroup("Pages", { btnCover, btnPageBreak, btnBlankPage }));
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
    m_editor->setFocus();
}

void WriterRibbon::insertPageBreak() {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    cur.beginEditBlock();
    cur.insertBlock();
    QTextBlockFormat bf = cur.blockFormat();
    bf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    cur.setBlockFormat(bf);
    cur.endEditBlock();
    // Visual cue in the continuous editor (PDF/print honour the policy above).
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

void WriterRibbon::insertPageNumberField() {
    if (!m_editor) return;
    // Estimate the current page from the cursor's vertical position.
    const QTextCursor cur = m_editor->textCursor();
    const QRect r = m_editor->cursorRect(cur);
    const double pageH = 1123.0;   // A4 px height @100%
    const int page = qMax(1, static_cast<int>(r.top() / pageH) + 1);
    m_editor->textCursor().insertText(QString::number(page));
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

void WriterRibbon::insertWordArt() {
    if (!m_editor) return;
    bool ok = false;
    const QString text = QInputDialog::getText(window(), "Insert WordArt",
                            "Text:", QLineEdit::Normal, "WordArt", &ok);
    if (!ok || text.isEmpty()) return;

    // Render the text with a gradient fill + dark outline, then embed as an image.
    QFont f("Georgia", 44, QFont::Bold);
    f.setItalic(true);
    const QFontMetrics fm(f);
    const int w = fm.horizontalAdvance(text) + 40;
    const int h = fm.height() + 28;
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(f);
    QPainterPath path;
    path.addText(20, 20 + fm.ascent(), f, text);
    QLinearGradient g(0, 0, 0, h);
    g.setColorAt(0.0, QColor("#FF5247"));
    g.setColorAt(1.0, QColor("#B91C1C"));
    p.setPen(QPen(QColor("#2C3140"), 2.0));
    p.setBrush(QBrush(g));
    p.drawPath(path);
    p.end();

    insertImageData(img);
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
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    cur.beginEditBlock();
    cur.movePosition(header ? QTextCursor::Start : QTextCursor::End);

    QTextCharFormat cf;
    cf.setForeground(QColor("#6B7280"));
    cf.setFontPointSize(10);
    QTextBlockFormat bf;
    bf.setAlignment(Qt::AlignHCenter);
    if (header) bf.setBottomMargin(12); else bf.setTopMargin(12);

    cur.insertBlock(bf, cf);
    cur.insertText(header ? "Header" : "Footer", cf);
    // separating rule
    cur.insertHtml("<hr/>");
    cur.endEditBlock();
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
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    QTextListFormat lf;
    lf.setStyle(static_cast<QTextListFormat::Style>(style));
    cur.createList(lf);
    m_editor->setFocus();
}

void WriterRibbon::applyNumbering(int style) {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    QTextListFormat lf;
    lf.setStyle(static_cast<QTextListFormat::Style>(style));
    cur.createList(lf);
    m_editor->setFocus();
}

void WriterRibbon::changeIndent(int delta) {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    QTextBlockFormat bf = cur.blockFormat();
    bf.setIndent(qMax(0, bf.indent() + delta));
    cur.mergeBlockFormat(bf);
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
    if (!m_editor) return;
    QTextCharFormat cf;
    QTextBlockFormat bf;
    styleFormats(s, cf, bf);
    bf.setProperty(kStyleProp, static_cast<int>(s));

    QTextCursor cur = m_editor->textCursor();
    cur.beginEditBlock();
    QTextCursor range = cur;
    if (!range.hasSelection()) {
        range.movePosition(QTextCursor::StartOfBlock);
        range.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    }
    // Clear any quote/shading background unless this style sets one.
    if (s != WriterStyle::Quote) bf.clearBackground();
    range.mergeBlockFormat(bf);
    range.mergeCharFormat(cf);
    cur.endEditBlock();

    m_editor->setCurrentCharFormat(cf);
    m_editor->setFocus();
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
    if (!m_editor) return;
    if (on) {
        if (!m_painterFmt) m_painterFmt = new QTextCharFormat();
        *m_painterFmt = m_editor->currentCharFormat();
        m_painterActive = true;
        m_editor->viewport()->setCursor(Qt::IBeamCursor);
    } else {
        m_painterActive = false;
        m_editor->viewport()->unsetCursor();
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
    form->addRow("Find:", m_findEdit);
    form->addRow("Replace with:", m_replaceEdit);
    form->addRow(m_matchCase);

    auto* row = new QHBoxLayout();
    auto* btnFind    = new QPushButton("Find Next", dlg);
    auto* btnRep     = new QPushButton("Replace", dlg);
    auto* btnRepAll  = new QPushButton("Replace All", dlg);
    auto* btnClose   = new QPushButton("Close", dlg);
    row->addWidget(btnFind); row->addWidget(btnRep);
    row->addWidget(btnRepAll); row->addWidget(btnClose);
    form->addRow(row);

    auto findNext = [this]() -> bool {
        const QString needle = m_findEdit->text();
        if (needle.isEmpty()) return false;
        QTextDocument::FindFlags flags;
        if (m_matchCase->isChecked()) flags |= QTextDocument::FindCaseSensitively;
        if (!m_editor->find(needle, flags)) {
            QTextCursor c = m_editor->textCursor();
            c.movePosition(QTextCursor::Start);
            m_editor->setTextCursor(c);
            return m_editor->find(needle, flags);
        }
        return true;
    };

    connect(btnFind, &QPushButton::clicked, dlg, [findNext] { findNext(); });
    connect(btnRep, &QPushButton::clicked, dlg, [this, findNext] {
        QTextCursor c = m_editor->textCursor();
        const bool cs = m_matchCase->isChecked();
        if (c.hasSelection() &&
            QString::compare(c.selectedText(), m_findEdit->text(),
                             cs ? Qt::CaseSensitive : Qt::CaseInsensitive) == 0) {
            c.insertText(m_replaceEdit->text());
        }
        findNext();
    });
    connect(btnRepAll, &QPushButton::clicked, dlg, [this] {
        const QString needle = m_findEdit->text();
        if (needle.isEmpty()) return;
        QTextDocument::FindFlags flags;
        if (m_matchCase->isChecked()) flags |= QTextDocument::FindCaseSensitively;
        QTextCursor c = m_editor->textCursor();
        c.movePosition(QTextCursor::Start);
        m_editor->setTextCursor(c);
        int n = 0;
        while (m_editor->find(needle, flags)) {
            m_editor->textCursor().insertText(m_replaceEdit->text());
            ++n;
        }
        m_editor->setWindowTitle(m_editor->windowTitle());  // no-op; keep focus path
    });
    connect(btnClose, &QPushButton::clicked, dlg, &QDialog::close);

    m_findDlg = dlg;
    dlg->show();
    m_findEdit->setFocus();
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

    const QString family = fmt.fontFamilies().toStringList().value(0);
    if (!family.isEmpty()) {
        const int idx = m_fontCombo->findText(family);
        if (idx >= 0) m_fontCombo->setCurrentIndex(idx);
        else m_fontCombo->setCurrentText(family);
    }

    const qreal pt = fmt.fontPointSize();
    if (pt > 0) m_sizeCombo->setCurrentText(QString::number(static_cast<int>(pt)));

    const Qt::Alignment a = m_editor->alignment();
    m_btnAlignLeft->setChecked(a.testFlag(Qt::AlignLeft));
    m_btnAlignCenter->setChecked(a.testFlag(Qt::AlignHCenter));
    m_btnAlignRight->setChecked(a.testFlag(Qt::AlignRight));
    m_btnAlignJustify->setChecked(a.testFlag(Qt::AlignJustify));

    // Active paragraph style
    if (m_styleGroup) {
        const QTextBlockFormat bf = m_editor->textCursor().blockFormat();
        const int sid = bf.hasProperty(kStyleProp)
                            ? bf.intProperty(kStyleProp)
                            : static_cast<int>(WriterStyle::Normal);
        if (auto* b = m_styleGroup->button(sid)) b->setChecked(true);
    }

    m_syncing = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Collapsible ribbon — double-click a tab to toggle
// ─────────────────────────────────────────────────────────────────────────────
bool WriterRibbon::eventFilter(QObject* obj, QEvent* ev) {
    if (ev->type() == QEvent::MouseButtonDblClick) {
        if (auto* btn = qobject_cast<QToolButton*>(obj)) {
            if (m_tabGroup->buttons().contains(btn)) {
                m_collapsed = !m_collapsed;
                m_stack->setVisible(!m_collapsed);
                return true;
            }
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

QWidget* WriterRibbon::makeSeparator() {
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedWidth(1);
    sep->setObjectName("ribbonSep");
    return sep;
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

    v->addWidget(row, 1);
    v->addWidget(label);
    return group;
}

// ─────────────────────────────────────────────────────────────────────────────
// Styling
// ─────────────────────────────────────────────────────────────────────────────
void WriterRibbon::applyStyles() {
    const QString arrowPath = QDir(QDir::tempPath()).filePath("nativeoffice_writer_arrow.png");
    {
        QPixmap pm(20, 12);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(90, 96, 110));
        p.setPen(Qt::NoPen);
        QPolygonF tri; tri << QPointF(5, 4) << QPointF(15, 4) << QPointF(10, 10);
        p.drawPolygon(tri);
        p.end();
        pm.save(arrowPath, "PNG");
    }

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
    border-bottom: 2px solid #E8372A;
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
    background-color: #FCE4E2; color: #C0271C; border-color: #E8372A; font-weight: 700;
}
QToolButton#ribbonToolBtn:pressed { background-color: #F6D2CE; }
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
    background-color: #FCE4E2; color: #C0271C; border-color: #E8372A; font-weight: 700;
}
QToolButton#ribbonBigBtn:pressed { background-color: #F6D2CE; }
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
    background-color: #FCE4E2; color: #C0271C; border-color: #E8372A;
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
QComboBox#ribbonCombo:focus { border-color: #E8372A; }
QComboBox#ribbonCombo::drop-down {
    subcontrol-origin: padding; subcontrol-position: center right; border: none; width: 16px;
}
QComboBox#ribbonCombo::down-arrow { image: url("%1"); width: 10px; height: 6px; }
QComboBox QAbstractItemView {
    background-color: #FFFFFF;
    color: #1C1E26;
    border: 1px solid #D5D8DF;
    border-radius: 6px;
    selection-background-color: #E8372A;
    selection-color: #FFFFFF;
    outline: none;
    padding: 4px;
}
QComboBox QAbstractItemView::item { min-height: 24px; padding-left: 6px; border-radius: 4px; }
QFrame#ribbonSep { background-color: #E2E4E9; border: none; margin: 6px 2px 16px 2px; }
QMenu {
    background-color: #FFFFFF; color: #1C1E26;
    border: 1px solid #D5D8DF; border-radius: 8px; padding: 4px;
    font-family: "Segoe UI", "Inter", sans-serif; font-size: 12px;
}
QMenu::item { padding: 6px 18px; border-radius: 5px; }
QMenu::item:selected { background-color: #FCE4E2; color: #C0271C; }
QMenu::separator { height: 1px; background: #E2E4E9; margin: 4px 8px; }
)").arg(arrowPath));
}

} // namespace NativeOffice
