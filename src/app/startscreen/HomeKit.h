#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// HomeKit.h — the small shared vocabulary of the home dashboard: its colour
// palette, the click-anywhere frame, text/badge/SVG helpers, and the
// extension → module mapping that every file row (search results, recent
// files, quick access) paints itself from.
//
// It used to live as an anonymous namespace inside StartScreen.cpp, which
// meant the hero banner, the search popup, the activity card and the template
// gallery could not share any of it. Everything here is header-only so those
// files can just include it.
// ─────────────────────────────────────────────────────────────────────────────

#include <QByteArray>
#include <QEnterEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSvgRenderer>
#include <QString>

#include <functional>

namespace NativeOffice {

// ── Palette ──────────────────────────────────────────────────────────────────
// One place for the dashboard's colours so a panel added later cannot drift a
// shade off from the ones beside it.
namespace Home {

inline constexpr const char* kBg          = "#090C13";  // page background
inline constexpr const char* kSidebar     = "#070910";  // left rail
inline constexpr const char* kPanel       = "#12151F";  // card background
inline constexpr const char* kPanelSoft   = "#171B27";  // nested tile inside a card
inline constexpr const char* kPanelHover  = "#1C2130";
inline constexpr const char* kBorder      = "#1F2434";
inline constexpr const char* kBorderSoft  = "#191D2A";

inline constexpr const char* kText        = "#EDEFF6";  // headings
inline constexpr const char* kTextBody    = "#C6CCDA";  // body copy
inline constexpr const char* kMuted       = "#7E8799";  // secondary / captions
inline constexpr const char* kFaint       = "#5C6478";

inline constexpr const char* kAccent      = "#7C6CF6";  // brand violet
inline constexpr const char* kAccentSoft  = "#9C8FFF";
inline constexpr const char* kBlue        = "#4B7BF5";
inline constexpr const char* kGreen       = "#22C55E";
inline constexpr const char* kAmber       = "#F5A524";

// Module accents, shared by badges, cards and template art.
inline constexpr const char* kWriter      = "#2F6FE4";
inline constexpr const char* kCalc        = "#1DA75B";
inline constexpr const char* kImpress     = "#EE6C1F";
inline constexpr const char* kPdf         = "#E0453F";
inline constexpr const char* kMarkdown    = "#7C5CFC";
inline constexpr const char* kImageKind   = "#0EA5E9";

} // namespace Home

// ── A frame the whole of which responds to a left click ──────────────────────
class ClickableFrame : public QFrame {
public:
    using QFrame::QFrame;
    std::function<void()> onClick;
    // Right-click hook, given the click point in global coordinates.
    std::function<void(const QPoint&)> onContextMenu;
    // Hover hooks, so a card can light up without every caller writing its own
    // event filter.
    std::function<void(bool)> onHover;

protected:
    void enterEvent(QEnterEvent*) override {
        if (onClick) setCursor(Qt::PointingHandCursor);
        if (onHover) onHover(true);
    }
    void leaveEvent(QEvent*) override { if (onHover) onHover(false); }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::RightButton && onContextMenu) {
            onContextMenu(e->globalPosition().toPoint());
            e->accept();
            return;
        }
        // Consume the click when handled, so it doesn't bubble to a clickable
        // ancestor (e.g. a template card inside a clickable panel).
        if (e->button() == Qt::LeftButton && onClick) { onClick(); e->accept(); return; }
        QFrame::mousePressEvent(e);
    }
};

// ── Text ─────────────────────────────────────────────────────────────────────
inline QLabel* heading(const QString& text, int px, const QString& color,
                       bool bold, QWidget* p = nullptr) {
    auto* l = new QLabel(text, p);
    l->setStyleSheet(QString("color:%1;font:%2 %3px 'Segoe UI';background:transparent;")
                         .arg(color).arg(bold ? "700" : "400").arg(px));
    return l;
}

// Semibold variant — the dashboard leans on 600 far more than on 700.
inline QLabel* label600(const QString& text, int px, const QString& color, QWidget* p = nullptr) {
    auto* l = new QLabel(text, p);
    l->setStyleSheet(QString("color:%1;font:600 %2px 'Segoe UI';background:transparent;")
                         .arg(color).arg(px));
    return l;
}

// ── Badges ───────────────────────────────────────────────────────────────────
// A rounded "app tile": a soft vertical gradient of the module colour with a
// white letter. Painted rather than stylesheet-styled so the gradient and the
// glyph stay crisp on hi-dpi and the label can carry a transparent corner.
inline QPixmap badgePixmap(const QString& letter, const QString& colorHex,
                           int size, qreal dpr, int radius = -1) {
    const int px = int(size * dpr);
    QPixmap pm(px, px);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    const QColor base(colorHex);
    QLinearGradient g(0, 0, 0, size);
    g.setColorAt(0.0, base.lighter(118));
    g.setColorAt(1.0, base.darker(112));

    const qreal r = radius < 0 ? size * 0.28 : radius;
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, size, size), r, r);
    p.fillPath(path, g);

    p.setPen(QColor(255, 255, 255, 235));
    QFont f("Segoe UI", 1, QFont::Bold);
    f.setPixelSize(qMax(9, int(size * 0.48)));
    f.setWeight(QFont::Bold);
    p.setFont(f);
    p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, letter);
    p.end();
    return pm;
}

inline QLabel* badge(const QString& letter, const QString& colorHex, int size,
                     QWidget* parent) {
    auto* l = new QLabel(parent);
    l->setPixmap(badgePixmap(letter, colorHex, size,
                             parent ? parent->devicePixelRatio() : 1.0));
    l->setFixedSize(size, size);
    l->setStyleSheet("background:transparent;");
    l->setAttribute(Qt::WA_TransparentForMouseEvents);
    return l;
}

// ── Inline SVG art ───────────────────────────────────────────────────────────
inline QPixmap svgPixmap(const char* svg, int h, qreal dpr) {
    const QByteArray data(svg);
    QSvgRenderer r(data);
    const QSize def = r.defaultSize();
    const int w = (def.height() > 0) ? def.width() * h / def.height() : h;
    QPixmap pm(int(w * dpr), int(h * dpr));
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    r.render(&p);
    p.end();
    pm.setDevicePixelRatio(dpr);
    return pm;
}

// Render an inline SVG illustration into a transparent QLabel at the given
// height (clicks pass through to the parent card).
inline QLabel* svgArt(const char* svg, int h, QWidget* parent) {
    auto* l = new QLabel(parent);
    const qreal dpr = parent ? parent->devicePixelRatio() : 1.0;
    const QPixmap pm = svgPixmap(svg, h, dpr);
    l->setPixmap(pm);
    l->setFixedSize(pm.deviceIndependentSize().toSize());
    l->setStyleSheet("background:transparent;");
    l->setAttribute(Qt::WA_TransparentForMouseEvents);
    return l;
}

// ── File kinds ───────────────────────────────────────────────────────────────
// Everything that lists files on the home screen (search results, recent
// files, quick access) paints the same badge for the same extension, and the
// same "which module opens this" wording.
struct FileKind {
    QString letter;    // badge glyph
    QString color;     // badge colour
    QString module;    // "Writer" | "Sheets" | "Slides" | "PDF" | "Markdown"
};

inline FileKind fileKindForSuffix(const QString& suffixIn) {
    const QString s = suffixIn.toLower();
    if (s == "xlsx" || s == "xls" || s == "xlsm" || s == "ods"
        || s == "csv" || s == "tsv")
        return { QStringLiteral("S"), Home::kCalc,     QStringLiteral("Sheets") };
    if (s == "pptx" || s == "ppt" || s == "odp")
        return { QStringLiteral("P"), Home::kImpress,  QStringLiteral("Slides") };
    if (s == "pdf")
        return { QStringLiteral("A"), Home::kPdf,      QStringLiteral("PDF") };
    if (s == "md" || s == "markdown" || s == "mdown")
        return { QStringLiteral("M"), Home::kMarkdown, QStringLiteral("Markdown") };
    if (s == "png" || s == "jpg" || s == "jpeg" || s == "webp" || s == "bmp"
        || s == "gif")
        return { QStringLiteral("I"), Home::kImageKind, QStringLiteral("Image") };
    return { QStringLiteral("W"), Home::kWriter, QStringLiteral("Writer") };
}

// The same mapping keyed by the module name RecentFilesManager persists.
inline FileKind fileKindForModule(const QString& module) {
    if (module == QLatin1String("Calc"))
        return { QStringLiteral("S"), Home::kCalc,    QStringLiteral("Sheets") };
    if (module == QLatin1String("Impress"))
        return { QStringLiteral("P"), Home::kImpress, QStringLiteral("Slides") };
    // The recent-files store has written both spellings over time.
    if (module.compare(QLatin1String("pdf"), Qt::CaseInsensitive) == 0)
        return { QStringLiteral("A"), Home::kPdf,     QStringLiteral("PDF") };
    if (module == QLatin1String("Markdown"))
        return { QStringLiteral("M"), Home::kMarkdown, QStringLiteral("Markdown") };
    return { QStringLiteral("W"), Home::kWriter, QStringLiteral("Writer") };
}

// Every extension the Open File dialog and the global search accept.
inline QStringList supportedFileSuffixes() {
    return { "docx", "doc", "odt", "rtf", "txt", "html", "htm", "noff",
             "xlsx", "xls", "xlsm", "ods", "csv", "tsv",
             "pptx", "ppt", "odp",
             "pdf", "md", "markdown" };
}

// The filter string shared by every "open a document" file dialog.
inline QString supportedFilesFilter() {
    return QStringLiteral(
        "All Supported Files (*.docx *.doc *.odt *.rtf *.txt *.html *.noff "
        "*.xlsx *.xls *.ods *.csv *.tsv *.pptx *.ppt *.odp *.pdf *.md);;"
        "Documents (*.docx *.doc *.odt *.rtf *.txt *.html *.noff);;"
        "Spreadsheets (*.xlsx *.xls *.ods *.csv *.tsv);;"
        "Presentations (*.pptx *.ppt *.odp);;"
        "PDF (*.pdf);;Markdown (*.md *.markdown);;All Files (*)");
}

} // namespace NativeOffice
