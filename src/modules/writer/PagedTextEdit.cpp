// ─────────────────────────────────────────────────────────────────────────────
// PagedTextEdit.cpp  (Sprint 17 — real pagination)
// ─────────────────────────────────────────────────────────────────────────────
#include "PagedTextEdit.h"
#include "WriterTableOps.h"
#include "WriterListOps.h"
#include "SpellChecker.h"
#include "SpellHighlighter.h"

#include <QPainter>
#include <QTextDocument>
#include <QTextBlock>
#include <QAbstractTextDocumentLayout>
#include <QLinearGradient>
#include <QResizeEvent>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QColorDialog>
#include <QTextImageFormat>
#include <QTextBlockFormat>
#include <QAction>
#include <QFont>
#include <QDesktopServices>
#include <QUrl>
#include <QDate>
#include <QMimeData>
#include <QBuffer>
#include <QFileInfo>
#include <QImage>
#include <QtMath>
#include <cmath>
#include <QToolButton>
#include <QVBoxLayout>
#include <QToolTip>
#include <QTimer>
#include <QIcon>
#include <QPixmap>
#include <functional>

namespace NativeOffice {

// Char-format properties tagging tracked-change runs.
static constexpr int kTrackInsert = QTextFormat::UserProperty + 30;
static constexpr int kTrackDelete = QTextFormat::UserProperty + 31;
static const QColor kInsColor("#1A7F37");   // green — inserted
static const QColor kDelColor("#C0271C");   // red   — deleted

// ── Page-control icons (drawn so no asset/cross-module dependency is needed) ──
namespace {
QPixmap plusPixmap(const QColor& c, int px) {
    QPixmap pm(px, px); pm.fill(Qt::transparent);
    QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
    QPen pen(c, px * 0.11); pen.setCapStyle(Qt::RoundCap); p.setPen(pen);
    const double m = px * 0.28, c2 = px / 2.0;
    p.drawLine(QPointF(c2, m), QPointF(c2, px - m));
    p.drawLine(QPointF(m, c2), QPointF(px - m, c2));
    return pm;
}
QPixmap trashPixmap(const QColor& c, int px) {
    QPixmap pm(px, px); pm.fill(Qt::transparent);
    QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
    QPen pen(c, px * 0.085); pen.setCapStyle(Qt::RoundCap); pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    const double w = px, h = px;
    // lid + handle
    p.drawLine(QPointF(w*0.22, h*0.28), QPointF(w*0.78, h*0.28));
    p.drawLine(QPointF(w*0.40, h*0.28), QPointF(w*0.42, h*0.20));
    p.drawLine(QPointF(w*0.42, h*0.20), QPointF(w*0.58, h*0.20));
    p.drawLine(QPointF(w*0.58, h*0.20), QPointF(w*0.60, h*0.28));
    // can body
    p.drawLine(QPointF(w*0.30, h*0.30), QPointF(w*0.34, h*0.80));
    p.drawLine(QPointF(w*0.70, h*0.30), QPointF(w*0.66, h*0.80));
    p.drawLine(QPointF(w*0.34, h*0.80), QPointF(w*0.66, h*0.80));
    // vertical ribs
    p.drawLine(QPointF(w*0.50, h*0.38), QPointF(w*0.50, h*0.72));
    return pm;
}
} // anonymous namespace

// A small floating column of three "cute" light buttons (＋ / 🗑 / ＋) that the
// PagedTextEdit parks at the top-right of the hovered page. No bounding box —
// the buttons rest on their own. One instance is reused for every page, so it
// never lags no matter how many pages the document has.
class PageActionBar : public QWidget {
public:
    explicit PageActionBar(QWidget* parent) : QWidget(parent) {
        setObjectName("pageActionBar");
        setAttribute(Qt::WA_NoSystemBackground);
        setCursor(Qt::ArrowCursor);
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(5);
        m_addAbove = makeButton(plusPixmap(QColor("#5B6472"), 16),  tr("Add a blank page above"));
        m_delete   = makeButton(trashPixmap(QColor("#C0392B"), 16), tr("Click to delete this page"));
        m_addBelow = makeButton(plusPixmap(QColor("#5B6472"), 16),  tr("Add a blank page below"));
        lay->addWidget(m_addAbove);
        lay->addWidget(m_delete);
        lay->addWidget(m_addBelow);

        m_hideTimer = new QTimer(this);
        m_hideTimer->setSingleShot(true);
        m_hideTimer->setInterval(220);
        connect(m_hideTimer, &QTimer::timeout, this, [this]{ hide(); });

        connect(m_addAbove, &QToolButton::clicked, this, [this]{ if (onAddAbove) onAddAbove(); });
        connect(m_addBelow, &QToolButton::clicked, this, [this]{ if (onAddBelow) onAddBelow(); });
        connect(m_delete,   &QToolButton::clicked, this, [this]{
            if (!m_canDelete) {
                QToolTip::showText(m_delete->mapToGlobal(QPoint(m_delete->width(), 0)),
                                   tr("A document must keep at least one page."), m_delete);
                return;
            }
            if (onDelete) onDelete();
        });
        hide();
    }

    void showForPage(const QPoint& topLeft, bool canDelete) {
        m_canDelete = canDelete;
        // Dim (but keep clickable) the trash when the page can't be deleted, so a
        // click can still surface the little "can't delete" notice.
        m_delete->setStyleSheet(buttonStyle(canDelete ? "#C0392B" : "#C7A6A2",
                                            canDelete ? "#FBE9E7" : "#FFFFFF"));
        adjustSize();
        move(topLeft);
        show();
        raise();
        m_hideTimer->stop();
    }
    void requestHide() { if (isVisible()) m_hideTimer->start(); }

    std::function<void()> onAddAbove, onDelete, onAddBelow;

protected:
    void enterEvent(QEnterEvent*) override { m_hideTimer->stop(); }
    void leaveEvent(QEvent*)      override { m_hideTimer->start(); }

private:
    static QString buttonStyle(const QString& iconTint, const QString& hoverBg) {
        Q_UNUSED(iconTint);
        return QString(
            "QToolButton { background:rgba(255,255,255,0.92); border:1px solid #E1E4EA;"
            "  border-radius:13px; padding:0; }"
            "QToolButton:hover { background:%1; border:1px solid #C9CED8; }").arg(hoverBg);
    }
    QToolButton* makeButton(const QPixmap& icon, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setIcon(QIcon(icon));
        b->setIconSize(QSize(16, 16));
        b->setFixedSize(26, 26);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setStyleSheet(buttonStyle("#5B6472", "#EEF1F6"));
        return b;
    }
    QToolButton *m_addAbove{}, *m_delete{}, *m_addBelow{};
    QTimer* m_hideTimer{};
    bool m_canDelete { true };
};

PagedTextEdit::PagedTextEdit(QWidget* parent)
    : QTextEdit(parent)
{
    setFrameShape(QFrame::NoFrame);
    // The widget grows to its full multi-page height; the surrounding
    // QScrollArea does the scrolling, so we never scroll internally.
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    document()->setPageSize(QSizeF(m_pageW, m_pageH));
    document()->setDocumentMargin(m_margin);

    // Every path that clobbers the page size resizes the document, so this is the
    // one place that catches all of them — restore pagination, then measure.
    connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
            this, [this](const QSizeF&) {
        if (m_inBaseResize) return;      // resizeEvent restores and syncs itself
        restorePaginationIfDropped();    // may re-enter; syncHeight's memo absorbs it
        syncHeight();
    });

    // Spell checking: a syntax highlighter draws red squiggles under misspellings.
    // Starts disabled; WriterModule turns it on to match the status-bar pill.
    m_spell = new SpellHighlighter(document());

    // Mouse tracking so the page hover controls follow the cursor without a
    // button held. The controls are a single reused overlay (no per-page widget).
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    // Parent the controls to the surrounding canvas (not the page viewport) so
    // they can float in the gray desk area just *outside* the page's right edge.
    m_pageBar = new PageActionBar(parentWidget() ? parentWidget() : viewport());
    m_pageBar->onAddAbove = [this]{ if (m_barPage >= 0) { addBlankPageAbove(m_barPage); m_pageBar->requestHide(); } };
    m_pageBar->onAddBelow = [this]{ if (m_barPage >= 0) { addBlankPageBelow(m_barPage); m_pageBar->requestHide(); } };
    m_pageBar->onDelete   = [this]{ if (m_barPage >= 0) { deletePage(m_barPage);        m_pageBar->requestHide(); } };
}

void PagedTextEdit::setZoom(double factor) {
    if (m_spell) m_spell->setZoom(factor);
}

// ── Page operations (hover controls) ────────────────────────────────────────
int PagedTextEdit::pageAtViewportY(int y) const {
    if (!m_paged || m_pageH <= 0) return -1;
    const int idx = int(y / m_pageH);
    return (idx >= 0 && idx < pageCountValue()) ? idx : -1;
}

void PagedTextEdit::updatePageBar(const QPoint& pos) {
    if (!m_pageBar) return;
    const int page = pageAtViewportY(pos.y());
    if (page < 0) { m_pageBar->requestHide(); return; }

    // Trigger zone: the top-right region of the hovered page (matches where the
    // controls sit). Generous enough to be easy to reach, but not the whole page.
    const double vw = viewport()->width();
    const double pageTop = page * m_pageH;
    const double zoneW = qMin(190.0, m_pageW * 0.42);
    const double zoneH = qMin(190.0, m_pageH * 0.34);
    const bool inZone = pos.x() >= vw - zoneW &&
                        pos.y() >= pageTop && pos.y() <= pageTop + zoneH;
    if (!inZone) { m_pageBar->requestHide(); return; }

    m_barPage = page;
    // Place the controls in the gray desk area just outside the page's right
    // edge, aligned near the top of the hovered page. The bar lives in the
    // canvas coordinate space, so map the page's top-right corner into it.
    QWidget* host = m_pageBar->parentWidget();
    const QPoint tr = host ? mapTo(host, QPoint(width(), int(pageTop)))
                           : QPoint(width(), int(pageTop));
    m_pageBar->showForPage(QPoint(tr.x() + 12, tr.y() + 14),
                           /*canDelete=*/pageCountValue() > 1);
}

QTextBlock PagedTextEdit::firstBlockOnPage(int pageIndex) const {
    const double pageTop = pageIndex * m_pageH - 0.5;
    auto* lay = document()->documentLayout();
    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next())
        if (lay->blockBoundingRect(b).top() >= pageTop)
            return b;
    return QTextBlock();
}

void PagedTextEdit::insertBlankPageBefore(const QTextBlock& anchor) {
    QTextCursor cur(document());
    cur.beginEditBlock();

    auto setBreak = [&](int pos, bool on) {
        QTextCursor c(document());
        c.setPosition(pos);
        QTextBlockFormat f = c.blockFormat();
        f.setPageBreakPolicy(on ? QTextFormat::PageBreak_AlwaysBefore
                                : QTextFormat::PageBreak_Auto);
        c.setBlockFormat(f);
    };

    if (anchor.isValid()) {
        // Insert one empty paragraph in front of the anchor content. After the
        // split the anchor content is the cursor's block and the new empty
        // paragraph is the block immediately before it.
        cur.setPosition(anchor.position());
        cur.insertBlock();
        const QTextBlock contentBlk = cur.block();
        const QTextBlock blankBlk   = contentBlk.previous();
        // The content always moves to a fresh page (one page down).
        setBreak(contentBlk.position(), true);
        // The blank paragraph gets its own page too — UNLESS it is now the very
        // first block of the document, where a "break before" would spawn a
        // spurious *extra* empty page in front of it (the add-above double-page
        // bug). As the first block it is already page 1, so no break is needed.
        if (blankBlk.isValid())
            setBreak(blankBlk.position(), blankBlk != document()->begin());
    } else {
        // No anchor → append a blank page at the very end of the document.
        cur.movePosition(QTextCursor::End);
        QTextBlockFormat brk;
        brk.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
        cur.insertBlock(brk);
    }

    cur.endEditBlock();
    syncHeight();
    viewport()->update();
}

void PagedTextEdit::addBlankPageAbove(int pageIndex) {
    if (!m_paged) return;
    insertBlankPageBefore(firstBlockOnPage(pageIndex));
}

void PagedTextEdit::addBlankPageBelow(int pageIndex) {
    if (!m_paged) return;
    // A blank page after page N is a blank page before whatever starts page N+1
    // (or appended at the end when N is the last page).
    insertBlankPageBefore(firstBlockOnPage(pageIndex + 1));
}

bool PagedTextEdit::deletePage(int pageIndex) {
    if (!m_paged || pageCountValue() <= 1) return false;   // never delete the last page

    const QTextBlock startB = firstBlockOnPage(pageIndex);
    if (!startB.isValid()) return false;
    const QTextBlock endB = firstBlockOnPage(pageIndex + 1);

    QTextCursor cur(document());
    cur.beginEditBlock();
    cur.setPosition(startB.position());
    if (endB.isValid())
        cur.setPosition(endB.position(), QTextCursor::KeepAnchor);
    else
        cur.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    cur.removeSelectedText();
    // The content that flows up into the deleted page's slot must not keep a
    // forced page break, or a blank gap would remain where the page used to be.
    QTextBlockFormat bf = cur.blockFormat();
    if (bf.pageBreakPolicy() != QTextFormat::PageBreak_Auto) {
        bf.setPageBreakPolicy(QTextFormat::PageBreak_Auto);
        cur.setBlockFormat(bf);
    }
    cur.endEditBlock();
    syncHeight();
    viewport()->update();
    return true;
}

void PagedTextEdit::setSpellCheckEnabled(bool on) {
    if (m_spell) m_spell->setEnabled(on);
}

bool PagedTextEdit::spellCheckEnabled() const {
    return m_spell && m_spell->isEnabled();
}

void PagedTextEdit::rehighlightSpelling() {
    if (m_spell) m_spell->rehighlight();
}

void PagedTextEdit::setPageMetrics(double pageW, double pageH, double margin) {
    m_pageW = pageW; m_pageH = pageH; m_margin = margin;
    document()->setDocumentMargin(margin);
    if (m_paged) {
        applyPageSize();
        setFixedWidth(qRound(pageW));
    }
    syncHeight();
    viewport()->update();
}

void PagedTextEdit::setPaged(bool on) {
    if (m_paged == on) return;
    m_paged = on;
    if (on) {
        applyPageSize();
        setFixedWidth(qRound(m_pageW));
    } else {
        // Continuous flow — let the document grow to a single tall page.
        document()->setPageSize(QSizeF());
    }
    syncHeight();
    viewport()->update();
}

void PagedTextEdit::setPaperColor(const QColor& c) {
    m_paper = c.isValid() ? c : QColor("#FFFFFF");
    viewport()->update();
}

int PagedTextEdit::pageCountValue() const {
    return m_paged ? qMax(1, document()->pageCount()) : 1;
}

int PagedTextEdit::currentPageNumber() const {
    if (!m_paged || m_pageH <= 0) return 1;
    // Widget coords == document coords (no internal scrolling), so the cursor's
    // y position divided by the page height is the page index.
    const QRect cr = cursorRect();
    return qBound(1, int(cr.top() / m_pageH) + 1, pageCountValue());
}

void PagedTextEdit::setHeaderFooter(const QStringList& header, const QStringList& footer,
                                    bool differentFirstPage) {
    for (int i = 0; i < 3; ++i) {
        m_header[i] = header.value(i);
        m_footer[i] = footer.value(i);
    }
    m_diffFirst = differentFirstPage;
    viewport()->update();
}

void PagedTextEdit::setDocName(const QString& n) {
    m_docName = n;
    viewport()->update();
}

QString PagedTextEdit::expandFields(const QString& s, int pageNum, int total) const {
    if (s.isEmpty()) return s;
    QString r = s;
    r.replace("{n}", QString::number(pageNum));
    r.replace("{N}", QString::number(total));
    r.replace("{date}", QDate::currentDate().toString("MMMM d, yyyy"));
    r.replace("{file}", m_docName.isEmpty() ? "Untitled" : m_docName);
    return r;
}

// Qt's setPageSize() relayouts the whole document unconditionally — it never
// checks whether the value actually changed. Route every assignment through
// here so re-asserting the current geometry costs nothing.
void PagedTextEdit::applyPageSize() {
    if (!m_paged) return;            // web layout: QTextEdit owns the text width
    const QSizeF want(m_pageW, m_pageH);
    if (document()->pageSize() == want) return;
    document()->setPageSize(want);
}

// Qt drops pagination behind our back. Several internal paths call
// document()->setTextWidth(), which IS setPageSize(width, -1) — a height of -1
// means "unpaginated", collapsing the document to one tall page. resizeEvent is
// only one of those paths: changing the fixed width (a zoom) relayouts the
// viewport and clobbers the page size without ever sending us a resize event.
// Rather than try to guard every caller, notice the symptom — a paged document
// whose page height has gone negative — and put the pagination back.
bool PagedTextEdit::restorePaginationIfDropped() {
    if (!m_paged || document()->pageSize().height() >= 0.0) return false;
    applyPageSize();
    return true;
}

void PagedTextEdit::syncHeight() {
    // Mid-resize the document is transiently unpaginated (see resizeEvent), so
    // pageCount() lies. Ignore it; resizeEvent syncs once it has restored state.
    if (m_inBaseResize) return;

    double h = m_paged ? pageCountValue() * m_pageH
                       : document()->size().height();
    if (h < 1.0) h = m_pageH;
    const int want = static_cast<int>(std::ceil(h)) + 1;
    // Only resize when the height really changes. This is what breaks the
    // resize → relayout → documentSizeChanged → resize feedback loop.
    if (want == m_fixedH) return;
    m_fixedH = want;
    setFixedHeight(want);
}

void PagedTextEdit::resizeEvent(QResizeEvent* e) {
    // QTextEdit::resizeEvent calls document()->setTextWidth(viewport width)
    // internally, and setTextWidth IS setPageSize(width, -1) — it silently drops
    // pagination and collapses the document to one tall page. Left alone, the
    // documentSizeChanged it emits drives syncHeight() off a bogus pageCount of
    // 1, which resizes us, which relayouts again: the document re-breaks across
    // pages on a loop and the text visibly shuffles after opening. So: ignore
    // the layout signals it emits mid-flight, restore pagination before anything
    // can observe the collapsed state, then sync the height once, from the truth.
    m_inBaseResize = true;
    QTextEdit::resizeEvent(e);
    applyPageSize();
    m_inBaseResize = false;
    syncHeight();
}

// ── Image selection & resize ─────────────────────────────────────────────────
QTextImageFormat PagedTextEdit::selectedImageFormat() const {
    if (m_imagePos < 0) return {};
    QTextCursor c(document());
    c.setPosition(m_imagePos);
    c.setPosition(m_imagePos + 1, QTextCursor::KeepAnchor);
    const QTextCharFormat f = c.charFormat();
    return f.isImageFormat() ? f.toImageFormat() : QTextImageFormat();
}

QRect PagedTextEdit::selectedImageRect() const {
    if (m_imagePos < 0) return {};
    QTextCursor a(document()); a.setPosition(m_imagePos);
    QTextCursor b(document()); b.setPosition(m_imagePos + 1);
    const QTextImageFormat f = selectedImageFormat();
    // A stale m_imagePos left pointing at something that is no longer an image
    // would otherwise hand back a rectangle built out of two unrelated caret
    // positions, and handleAt() would scatter phantom resize grips over it.
    if (!f.isImageFormat()) return {};

    const QRect ra = cursorRect(a);
    const QRect rb = cursorRect(b);

    // Measure the image from its own format, and anchor it on the caret that
    // sits AFTER it.
    //
    // Subtracting the two caret positions, which is what this used to do, is
    // only right while both carets are on the same line. They are not whenever
    // the image wrapped onto a line of its own (the caret before it stays at
    // the end of the previous line) and they can be on different PAGES when the
    // image lands near a break. The subtraction then produced a wide, flat
    // rectangle lying across the page gap, nowhere near the picture: that is
    // the box in the "cannot type after pasting an image" report.
    //
    // It did not just look wrong. handleAt() treats anything within a few
    // pixels of that rectangle's eight corners as a resize grip, so a rectangle
    // stretched across the document put grips in the middle of the text, and
    // mousePressEvent swallows a press on a grip without passing it to
    // QTextEdit. Clicks landed on nothing, the caret never moved, and the
    // document appeared to refuse input.
    const bool sameLine = qAbs(ra.top() - rb.top()) <= 2;

    int w = f.width()  > 0 ? int(f.width())
                           : (sameLine ? rb.left() - ra.left() : ra.height());
    int h = f.height() > 0 ? int(f.height())
                           : (sameLine ? ra.height() : rb.height());
    w = qMax(w, 1);
    h = qMax(h, 1);

    // The caret after the image is always on the image's own line, so the image
    // ends at its left edge and sits on its baseline.
    const int x   = rb.left() - w;
    const int top = rb.bottom() - h;
    return QRect(x, top, w, h);
}

int PagedTextEdit::handleAt(const QPoint& p) const {
    if (m_imagePos < 0) return -1;
    const QRect r = selectedImageRect();
    // A grip only exists next to the picture. Without this, any rectangle that
    // came out wrong reached across the page and quietly turned parts of the
    // text into resize grips, so clicks there never got to the caret.
    if (!r.adjusted(-10, -10, 10, 10).contains(p)) return -1;
    const QPoint pts[8] = {
        r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft(),
        QPoint(r.center().x(), r.top()), QPoint(r.right(), r.center().y()),
        QPoint(r.center().x(), r.bottom()), QPoint(r.left(), r.center().y())
    };
    for (int i = 0; i < 8; ++i)
        if ((pts[i] - p).manhattanLength() <= 9) return i;
    return -1;
}

void PagedTextEdit::selectImageAt(const QPoint& vpPos) {
    auto isImgAt = [this](int p) -> bool {
        if (p < 0) return false;
        QTextCursor t(document());
        t.setPosition(p);
        if (!t.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor)) return false;
        return t.charFormat().isImageFormat();
    };
    const int cp = cursorForPosition(vpPos).position();
    const int found = isImgAt(cp) ? cp : (isImgAt(cp - 1) ? cp - 1 : -1);
    if (found != m_imagePos) {
        m_imagePos = found;
        viewport()->update();
        emit imageSelectionChanged(found >= 0);
    }
}

void PagedTextEdit::applyImageSize(int w, int h) {
    if (m_imagePos < 0) return;
    QTextCursor c(document());
    c.setPosition(m_imagePos);
    c.setPosition(m_imagePos + 1, QTextCursor::KeepAnchor);
    QTextImageFormat f = c.charFormat().toImageFormat();
    if (!f.isImageFormat()) return;
    f.setWidth(w);
    f.setHeight(h);
    c.mergeCharFormat(f);
    viewport()->update();
    syncHeight();
}

void PagedTextEdit::setImageWidth(int px) {
    const QRect r = selectedImageRect();
    const double aspect = r.height() > 0 ? double(r.width()) / r.height() : 1.0;
    applyImageSize(px, int(px / aspect));
}

void PagedTextEdit::scaleImage(double factor) {
    const QRect r = selectedImageRect();
    applyImageSize(int(r.width() * factor), int(r.height() * factor));
}

void PagedTextEdit::alignImage(Qt::Alignment a) {
    if (m_imagePos < 0) return;
    QTextCursor c(document());
    c.setPosition(m_imagePos);
    QTextBlockFormat bf;
    bf.setAlignment(a);
    c.mergeBlockFormat(bf);
    viewport()->update();
    setFocus();
}

void PagedTextEdit::deleteSelectedImage() {
    if (m_imagePos < 0) return;
    QTextCursor c(document());
    c.setPosition(m_imagePos);
    c.setPosition(m_imagePos + 1, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    m_imagePos = -1;
    emit imageSelectionChanged(false);
    viewport()->update();
    syncHeight();
    setFocus();
}

void PagedTextEdit::mousePressEvent(QMouseEvent* e) {
    // Ctrl+click follows a hyperlink (external URL → browser, #anchor → scroll).
    if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier)) {
        const QString href = anchorAt(e->pos());
        if (!href.isEmpty()) {
            if (href.startsWith('#')) scrollToAnchor(href.mid(1));
            else QDesktopServices::openUrl(QUrl(href));
            e->accept();
            return;
        }
    }
    if (e->button() == Qt::LeftButton && m_imagePos >= 0) {
        const int h = handleAt(e->pos());
        if (h >= 0) {
            m_resizing = true;
            m_resizeHandle = h;
            m_resizeStart = selectedImageRect();
            m_dragOrigin = e->pos();
            m_imgAspect = m_resizeStart.height() > 0
                ? double(m_resizeStart.width()) / m_resizeStart.height() : 1.0;
            e->accept();
            return;
        }
    }
    if (e->button() == Qt::LeftButton) selectImageAt(e->pos());
    QTextEdit::mousePressEvent(e);
}

void PagedTextEdit::mouseMoveEvent(QMouseEvent* e) {
    if (m_resizing) {
        const QPoint d = e->pos() - m_dragOrigin;
        int newW = m_resizeStart.width();
        switch (m_resizeHandle) {
        case 0: case 3: case 7: newW = m_resizeStart.width() - d.x(); break;  // left side
        case 1: case 2: case 5: newW = m_resizeStart.width() + d.x(); break;  // right side
        case 4:                 newW = int((m_resizeStart.height() - d.y()) * m_imgAspect); break;  // top
        case 6:                 newW = int((m_resizeStart.height() + d.y()) * m_imgAspect); break;  // bottom
        }
        newW = qBound(24, newW, 2000);
        applyImageSize(newW, int(newW / m_imgAspect));
        e->accept();
        return;
    }
    if (m_imagePos >= 0 && handleAt(e->pos()) >= 0) {
        const int h = handleAt(e->pos());
        setCursor((h == 0 || h == 2) ? Qt::SizeFDiagCursor
                : (h == 1 || h == 3) ? Qt::SizeBDiagCursor
                : (h == 4 || h == 6) ? Qt::SizeVerCursor : Qt::SizeHorCursor);
    } else if (cursor().shape() != Qt::IBeamCursor && cursor().shape() != Qt::ArrowCursor) {
        unsetCursor();
    }
    updatePageBar(e->pos());
    QTextEdit::mouseMoveEvent(e);
}

void PagedTextEdit::mouseReleaseEvent(QMouseEvent* e) {
    if (m_resizing) {
        m_resizing = false;
        m_resizeHandle = -1;
        e->accept();
        return;
    }
    QTextEdit::mouseReleaseEvent(e);
}

void PagedTextEdit::leaveEvent(QEvent* e) {
    // Leaving the page starts the dismiss timer; if the cursor is on its way to
    // the controls (which sit just outside the page), the bar's own enterEvent
    // cancels it, so it stays up while hovered.
    if (m_pageBar) m_pageBar->requestHide();
    QTextEdit::leaveEvent(e);
}

// ── Image paste & drag-drop ──────────────────────────────────────────────────
static bool isLocalImageFile(const QUrl& url) {
    if (!url.isLocalFile()) return false;
    static const QStringList exts = { "png", "jpg", "jpeg", "bmp", "gif", "webp", "tiff" };
    const QString suffix = QFileInfo(url.toLocalFile()).suffix().toLower();
    return exts.contains(suffix);
}

bool PagedTextEdit::canInsertFromMimeData(const QMimeData* source) const {
    if (source->hasImage()) return true;
    if (source->hasUrls()) {
        for (const QUrl& u : source->urls())
            if (isLocalImageFile(u)) return true;
    }
    return QTextEdit::canInsertFromMimeData(source);
}

void PagedTextEdit::insertFromMimeData(const QMimeData* source) {
    if (insertMimeImage(source)) return;
    QTextEdit::insertFromMimeData(source);
}

bool PagedTextEdit::insertMimeImage(const QMimeData* source) {
    if (source->hasImage()) {
        const QImage img = qvariant_cast<QImage>(source->imageData());
        if (!img.isNull()) { embedImage(img); return true; }
    }
    if (source->hasUrls()) {
        bool any = false;
        for (const QUrl& u : source->urls()) {
            if (!isLocalImageFile(u)) continue;
            QImage img(u.toLocalFile());
            if (!img.isNull()) { embedImage(img); any = true; }
        }
        if (any) return true;
    }
    return false;
}

void PagedTextEdit::embedImage(QImage img) {
    if (img.isNull()) return;

    // Fit within the content area between the margins - in BOTH directions.
    //
    // Only the width used to be capped, so a tall screenshot came in taller
    // than a whole page. Qt cannot break a line, so the image straddled the
    // page boundary, the gray gap band was painted across the middle of it,
    // and the paragraph after it landed at the top of the next page. That is
    // what the "text at the top of a new page is cut off" report was looking
    // at. Word scales a pasted picture to fit the page; so does this now.
    const int maxWidth  = qMax(100, int(m_pageW - 2 * m_margin));
    const int maxHeight = qMax(100, int(m_pageH - 2 * m_margin));
    int drawW = qMin(img.width(), maxWidth);
    int drawH = qMax(1, qRound(double(img.height()) * drawW / double(img.width())));
    if (drawH > maxHeight) {
        drawH = maxHeight;
        drawW = qMax(1, qRound(double(img.width()) * drawH / double(img.height())));
    }

    // Store at up to twice the drawn width and constrain the *displayed* size
    // separately. Resampling down to the drawn width, as this used to do, threw
    // the extra pixels away for good, so a pasted screenshot looked sharp at
    // 100% and pixelated the moment it was zoomed, printed or exported. The 2x
    // cap keeps that detail without letting a large screenshot bloat the file.
    const int storeW = qMin(img.width(), drawW * 2);
    if (img.width() > storeW)
        img = img.scaledToWidth(storeW, Qt::SmoothTransformation);

    QByteArray data;
    {
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        img.save(&buffer, "PNG");
    }
    textCursor().insertHtml(
        QStringLiteral("<img src=\"data:image/png;base64,%1\" width=\"%2\" height=\"%3\"/>")
            .arg(QString::fromLatin1(data.toBase64()))
            .arg(drawW)
            .arg(drawH));
}

void PagedTextEdit::keyPressEvent(QKeyEvent* e) {
    // Tab / Shift+Tab demote / promote list items (Word-style multi-level lists),
    // but only outside tables, where Tab still moves between cells.
    if ((e->key() == Qt::Key_Tab || e->key() == Qt::Key_Backtab)
            && textCursor().currentList() && !TableOps::inTable(this)) {
        ListOps::changeLevel(this, e->key() == Qt::Key_Backtab ? -1 : +1);
        e->accept();
        return;
    }

    const bool isReturn = (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
                          && !(e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier));

    // Enter on an empty list item ends the list (Word/WPS behaviour) instead of
    // producing another empty bullet.
    if (isReturn && !isReadOnly()) {
        QTextCursor c = textCursor();
        QTextList* list = c.currentList();
        if (list && c.block().text().trimmed().isEmpty() && !c.hasSelection()) {
            c.beginEditBlock();
            list->remove(c.block());
            QTextBlockFormat bf = c.blockFormat();
            bf.setIndent(0);
            bf.setObjectIndex(-1);
            c.setBlockFormat(bf);
            c.endEditBlock();
            e->accept();
            return;
        }
    }

    // Enter at the end of a heading starts a Normal paragraph (Word's
    // "style for following paragraph") instead of continuing the heading.
    if (isReturn && !isReadOnly() && !textCursor().hasSelection()
            && textCursor().atBlockEnd()
            && textCursor().blockFormat().headingLevel() > 0) {
        QTextEdit::keyPressEvent(e);
        QTextCursor c = textCursor();
        QTextBlockFormat bf = c.blockFormat();
        bf.setHeadingLevel(0);
        c.setBlockFormat(bf);
        QTextCharFormat cf;
        cf.setFontFamilies({"Segoe UI", "Inter", "Roboto", "sans-serif"});
        cf.setFontPointSize(12);
        cf.setFontWeight(QFont::Normal);
        cf.setForeground(QColor("#1C1E26"));
        c.setCharFormat(cf);
        setTextCursor(c);
        return;
    }

    // Track changes intercepts typing/deletion to mark rather than mutate.
    if (m_trackChanges && !isReadOnly() && handleTrackedKey(e)) { e->accept(); return; }

    // AutoCorrect (only on real typing, never read-only or with a selection edit).
    if (m_autoCorrectEnabled && !isReadOnly() && !textCursor().hasSelection()) {
        const QString t = e->text();

        // Smart curly quotes / apostrophes.
        if (m_autoCorrect.smartQuotes && (t == "\"" || t == "'")) {
            QTextCursor c = textCursor();
            QChar prev;
            if (c.positionInBlock() > 0) prev = c.block().text().at(c.positionInBlock() - 1);
            c.insertText(AutoCorrect::smartQuote(t.at(0), prev));
            e->accept();
            return;
        }

        // "--" → em dash.
        if (m_autoCorrect.smartDashes && t == "-") {
            QTextCursor c = textCursor();
            if (c.positionInBlock() > 0 && c.block().text().at(c.positionInBlock() - 1) == '-') {
                c.deletePreviousChar();
                c.insertText(QString(QChar(0x2014)));
                e->accept();
                return;
            }
        }

        // Word committed → correct the word just typed, then insert the trigger.
        const bool commit = e->key() == Qt::Key_Space || e->key() == Qt::Key_Return
                          || e->key() == Qt::Key_Enter
                          || (t.size() == 1 && QStringLiteral(".,;:!?)]}").contains(t));
        if (commit) applyAutoCorrectAtCursor();
    }

    QTextEdit::keyPressEvent(e);
}

bool PagedTextEdit::handleTrackedKey(QKeyEvent* e) {
    const int k = e->key();
    const QString t = e->text();

    // Deletion → strike through instead of removing.
    if (k == Qt::Key_Backspace || k == Qt::Key_Delete) {
        QTextCursor c = textCursor();
        if (!c.hasSelection()) {
            c.movePosition(k == Qt::Key_Backspace ? QTextCursor::PreviousCharacter
                                                  : QTextCursor::NextCharacter,
                           QTextCursor::KeepAnchor);
        }
        if (c.hasSelection()) {
            QTextCharFormat del;
            del.setForeground(kDelColor);
            del.setFontStrikeOut(true);
            del.setProperty(kTrackDelete, true);
            c.mergeCharFormat(del);
            QTextCursor nc = textCursor();
            nc.setPosition(k == Qt::Key_Backspace ? c.selectionStart() : c.selectionEnd());
            setTextCursor(nc);
        }
        return true;
    }

    // Printable text → insert marked as an insertion (mark any replaced selection
    // as deleted first).
    if (!t.isEmpty() && t.at(0).isPrint()) {
        QTextCursor c = textCursor();
        if (c.hasSelection()) {
            QTextCharFormat del;
            del.setForeground(kDelColor); del.setFontStrikeOut(true);
            del.setProperty(kTrackDelete, true);
            c.mergeCharFormat(del);
            QTextCursor nc = textCursor(); nc.setPosition(c.selectionEnd());
            setTextCursor(nc);
        }
        QTextCharFormat ins;
        ins.setForeground(kInsColor);
        ins.setFontUnderline(true);
        ins.setProperty(kTrackInsert, true);
        textCursor().insertText(t, ins);
        return true;
    }
    return false;   // navigation keys etc. fall through to normal handling
}

void PagedTextEdit::acceptAllChanges() {
    QTextDocument* d = document();
    struct Range { int start, end; bool del; };
    QList<Range> ranges;
    for (QTextBlock b = d->begin(); b != d->end(); b = b.next()) {
        for (auto it = b.begin(); it != b.end(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.isValid()) continue;
            const QTextCharFormat cf = f.charFormat();
            if (cf.boolProperty(kTrackDelete))
                ranges.append({ f.position(), f.position() + f.length(), true });
            else if (cf.boolProperty(kTrackInsert))
                ranges.append({ f.position(), f.position() + f.length(), false });
        }
    }
    std::sort(ranges.begin(), ranges.end(),
              [](const Range& a, const Range& b){ return a.start > b.start; });
    QTextCursor c(d);
    c.beginEditBlock();
    for (const Range& r : ranges) {
        c.setPosition(r.start);
        c.setPosition(r.end, QTextCursor::KeepAnchor);
        if (r.del) {
            c.removeSelectedText();                 // deletion accepted → gone
        } else {                                    // insertion accepted → keep as normal
            QTextCharFormat n;
            n.setForeground(QColor("#1C1E26"));
            n.setFontUnderline(false);
            n.setProperty(kTrackInsert, false);
            c.mergeCharFormat(n);
        }
    }
    c.endEditBlock();
}

void PagedTextEdit::rejectAllChanges() {
    QTextDocument* d = document();
    struct Range { int start, end; bool ins; };
    QList<Range> ranges;
    for (QTextBlock b = d->begin(); b != d->end(); b = b.next()) {
        for (auto it = b.begin(); it != b.end(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.isValid()) continue;
            const QTextCharFormat cf = f.charFormat();
            if (cf.boolProperty(kTrackInsert))
                ranges.append({ f.position(), f.position() + f.length(), true });
            else if (cf.boolProperty(kTrackDelete))
                ranges.append({ f.position(), f.position() + f.length(), false });
        }
    }
    std::sort(ranges.begin(), ranges.end(),
              [](const Range& a, const Range& b){ return a.start > b.start; });
    QTextCursor c(d);
    c.beginEditBlock();
    for (const Range& r : ranges) {
        c.setPosition(r.start);
        c.setPosition(r.end, QTextCursor::KeepAnchor);
        if (r.ins) {
            c.removeSelectedText();                 // insertion rejected → gone
        } else {                                    // deletion rejected → restore text
            QTextCharFormat n;
            n.setForeground(QColor("#1C1E26"));
            n.setFontStrikeOut(false);
            n.setProperty(kTrackDelete, false);
            c.mergeCharFormat(n);
        }
    }
    c.endEditBlock();
}

PagedTextEdit::TrackedRun PagedTextEdit::trackedRunAt(int pos) const {
    const QTextBlock b = document()->findBlock(pos);
    if (!b.isValid()) return {};
    // Locate the fragment containing pos, then extend across adjacent fragments
    // of the same tracked kind so one click covers the whole edit.
    bool del = false;
    int start = -1, end = -1;
    for (auto it = b.begin(); it != b.end(); ++it) {
        const QTextFragment f = it.fragment();
        if (!f.isValid()) continue;
        if (pos < f.position() || pos > f.position() + f.length()) continue;
        const QTextCharFormat cf = f.charFormat();
        if (cf.boolProperty(kTrackDelete)) del = true;
        else if (!cf.boolProperty(kTrackInsert)) return {};
        start = f.position();
        end   = f.position() + f.length();
        break;
    }
    if (start < 0) return {};
    for (auto it = b.begin(); it != b.end(); ++it) {         // extend both ways
        const QTextFragment f = it.fragment();
        if (!f.isValid()) continue;
        const QTextCharFormat cf = f.charFormat();
        const bool same = del ? cf.boolProperty(kTrackDelete)
                              : cf.boolProperty(kTrackInsert);
        if (!same) continue;
        if (f.position() + f.length() == start) start = f.position();
        if (f.position() == end)                end   = f.position() + f.length();
    }
    return { start, end, del };
}

void PagedTextEdit::resolveTrackedRun(const TrackedRun& run, bool accept) {
    if (run.start < 0 || run.end <= run.start) return;
    QTextCursor c(document());
    c.setPosition(run.start);
    c.setPosition(qMin(run.end, document()->characterCount() - 1),
                  QTextCursor::KeepAnchor);
    c.beginEditBlock();
    // Accepting a deletion or rejecting an insertion removes the text;
    // otherwise the text stays and the tracking markup is stripped.
    if (run.deletion == accept) {
        c.removeSelectedText();
    } else {
        QTextCharFormat n;
        n.setForeground(QColor("#1C1E26"));
        n.setFontUnderline(false);
        n.setFontStrikeOut(false);
        n.setProperty(kTrackInsert, false);
        n.setProperty(kTrackDelete, false);
        c.mergeCharFormat(n);
    }
    c.endEditBlock();
}

void PagedTextEdit::applyAutoCorrectAtCursor() {
    QTextCursor c = textCursor();
    if (c.hasSelection() || c.positionInBlock() == 0) return;

    QTextCursor w = c;
    w.select(QTextCursor::WordUnderCursor);
    const QString word = w.selectedText();
    if (word.isEmpty() || !word.at(0).isLetter()) return;

    // Sentence-start detection (block-scoped): is the text before this word empty
    // or ending in . ! ? ?
    bool sentenceStart = false;
    if (m_autoCorrect.capitalizeSentences) {
        const QString block = c.block().text();
        const int wordStart = w.selectionStart() - c.block().position();
        const QString before = block.left(qMax(0, wordStart));
        sentenceStart = AutoCorrect::endsSentence(before);
    }

    QString fixed = AutoCorrect::correctWord(word, m_autoCorrect);
    if (sentenceStart && !fixed.isEmpty() && fixed.at(0).isLetter() && fixed.at(0).isLower())
        fixed[0] = fixed.at(0).toUpper();

    if (fixed != word) {
        w.beginEditBlock();
        w.insertText(fixed);
        w.endEditBlock();
    }
}

void PagedTextEdit::contextMenuEvent(QContextMenuEvent* e) {
    // If the click is on an image, show the image menu.
    selectImageAt(e->pos());
    if (m_imagePos >= 0 && selectedImageFormat().isImageFormat()) {
        QMenu menu(this);
        menu.addAction("Align Left",   this, [this]{ alignImage(Qt::AlignLeft); });
        menu.addAction("Align Center", this, [this]{ alignImage(Qt::AlignHCenter); });
        menu.addAction("Align Right",  this, [this]{ alignImage(Qt::AlignRight); });
        menu.addSeparator();
        menu.addAction("Size: Small",   this, [this]{ setImageWidth(200); });
        menu.addAction("Size: Medium",  this, [this]{ setImageWidth(360); });
        menu.addAction("Size: Large",   this, [this]{ setImageWidth(560); });
        menu.addAction("Original Size", this, [this]{ applyImageSize(0, 0); });
        menu.addSeparator();
        menu.addAction("Delete Image",  this, [this]{ deleteSelectedImage(); });
        menu.exec(e->globalPos());
        return;
    }

    // Right-clicking a tracked change offers per-change accept/reject.
    {
        const TrackedRun run = trackedRunAt(cursorForPosition(e->pos()).position());
        if (run.start >= 0) {
            QMenu* menu = createStandardContextMenu(e->pos());
            QAction* before = menu->actions().isEmpty() ? nullptr : menu->actions().first();
            auto* acc = new QAction(run.deletion ? tr("Accept Deletion")
                                                 : tr("Accept Insertion"), menu);
            connect(acc, &QAction::triggered, this, [this, run]{ resolveTrackedRun(run, true); });
            auto* rej = new QAction(run.deletion ? tr("Reject Deletion (Restore Text)")
                                                 : tr("Reject Insertion"), menu);
            connect(rej, &QAction::triggered, this, [this, run]{ resolveTrackedRun(run, false); });
            menu->insertAction(before, acc);
            menu->insertAction(before, rej);
            menu->insertSeparator(before);
            menu->exec(e->globalPos());
            delete menu;
            return;
        }
    }

    // Spell suggestions take priority when right-clicking a misspelled word.
    if (m_spell && m_spell->isEnabled()) {
        QTextCursor wc = cursorForPosition(e->pos());
        wc.select(QTextCursor::WordUnderCursor);
        const QString word = wc.selectedText();
        auto* sc = SpellChecker::instance();
        if (!word.isEmpty() && !sc->isCorrect(word)) {
            setTextCursor(wc);                       // highlight the offending word
            QMenu* menu = createStandardContextMenu(e->pos());
            QAction* before = menu->actions().isEmpty() ? nullptr : menu->actions().first();

            const QStringList sugg = sc->suggestions(word);
            if (sugg.isEmpty()) {
                auto* none = new QAction(tr("(no suggestions)"), menu);
                none->setEnabled(false);
                menu->insertAction(before, none);
            } else {
                for (const QString& s : sugg) {
                    auto* act = new QAction(s, menu);
                    QFont f = act->font(); f.setBold(true); act->setFont(f);
                    connect(act, &QAction::triggered, this, [this, wc, s]() mutable {
                        QTextCursor c = wc; c.insertText(s);
                    });
                    menu->insertAction(before, act);
                }
            }
            menu->insertSeparator(before);
            auto* addAct = new QAction(tr("Add to Dictionary"), menu);
            connect(addAct, &QAction::triggered, this, [this, sc, word] {
                sc->addToDictionary(word);
                if (m_spell) m_spell->rehighlight();
            });
            menu->insertAction(before, addAct);
            auto* ignoreAct = new QAction(tr("Ignore All"), menu);
            connect(ignoreAct, &QAction::triggered, this, [this, sc, word] {
                sc->ignoreWord(word);
                if (m_spell) m_spell->rehighlight();
            });
            menu->insertAction(before, ignoreAct);
            menu->insertSeparator(before);

            menu->exec(e->globalPos());
            delete menu;
            return;
        }
    }

    // Position the cursor under the click so table ops act on the right cell.
    QTextCursor c = cursorForPosition(e->pos());
    if (!textCursor().hasSelection()) setTextCursor(c);

    if (!TableOps::inTable(this)) { QTextEdit::contextMenuEvent(e); return; }

    QMenu menu(this);
    menu.addAction("Insert Row Above",   this, [this]{ TableOps::insertRow(this, false); });
    menu.addAction("Insert Row Below",   this, [this]{ TableOps::insertRow(this, true); });
    menu.addAction("Insert Column Left", this, [this]{ TableOps::insertColumn(this, false); });
    menu.addAction("Insert Column Right",this, [this]{ TableOps::insertColumn(this, true); });
    menu.addSeparator();
    menu.addAction("Delete Row",    this, [this]{ TableOps::deleteRow(this); });
    menu.addAction("Delete Column", this, [this]{ TableOps::deleteColumn(this); });
    menu.addAction("Delete Table",  this, [this]{ TableOps::deleteTable(this); });
    menu.addSeparator();
    menu.addAction("Merge Cells", this, [this]{ TableOps::mergeCells(this); });
    menu.addAction("Split Cell",  this, [this]{ TableOps::splitCell(this); });
    menu.addSeparator();
    auto* shade = menu.addAction("Cell Shading…");
    connect(shade, &QAction::triggered, this, [this]{
        const QColor col = QColorDialog::getColor(QColor("#F1F3F7"), this, "Cell Shading");
        if (col.isValid()) TableOps::setCellShading(this, col);
    });
    menu.addAction("Clear Shading", this, [this]{ TableOps::setCellShading(this, Qt::transparent); });
    menu.exec(e->globalPos());
}

void PagedTextEdit::paintEvent(QPaintEvent* e) {
    // 1) Let QTextEdit paint the paper (its background) + text + selection/cursor.
    QTextEdit::paintEvent(e);

    // 1b) Word/WPS-style table-cell selection: when a selection runs through a
    //     table, fill the covered cells with a translucent highlight instead of
    //     the thin per-cell caret marks. Driven by cursor positions, so it works
    //     for both rectangular (drag) and linear selections.
    if (textCursor().hasSelection()) {
        const QTextCursor cur = textCursor();
        const int s = cur.selectionStart(), e2 = cur.selectionEnd();
        QTextCursor cs(document()); cs.setPosition(s);
        QTextCursor ce(document()); ce.setPosition(e2);
        QTextTable* tt = cs.currentTable() ? cs.currentTable() : ce.currentTable();
        if (tt) {
            auto* lay = document()->documentLayout();
            QPainter sp(viewport());
            sp.setRenderHint(QPainter::Antialiasing);
            const QColor hl(53, 124, 210, 90);
            for (int i = 0; i < tt->rows(); ++i) {
                for (int j = 0; j < tt->columns(); ++j) {
                    const QTextTableCell cell = tt->cellAt(i, j);
                    if (!cell.isValid()) continue;
                    const int cellStart = cell.firstCursorPosition().position();
                    const int cellEnd   = cell.lastCursorPosition().position();
                    if (cellEnd < s || cellStart > e2) continue;   // no overlap
                    QRectF rc;
                    QTextBlock b = cell.firstCursorPosition().block();
                    const QTextBlock last = cell.lastCursorPosition().block();
                    for (;;) {
                        if (b.isValid()) rc = rc.united(lay->blockBoundingRect(b));
                        if (b == last || !b.isValid()) break;
                        b = b.next();
                    }
                    if (rc.isValid()) sp.fillRect(rc.adjusted(-6, -3, 6, 3), hl);
                }
            }
        }
    }

    // 1c) Selected-image border + resize handles.
    if (m_imagePos >= 0) {
        if (!selectedImageFormat().isImageFormat()) {
            m_imagePos = -1;   // image was removed
        } else {
            const QRect r = selectedImageRect();
            QPainter ip(viewport());
            ip.setRenderHint(QPainter::Antialiasing);
            ip.setPen(QPen(QColor("#2563EB"), 1.5));
            ip.setBrush(Qt::NoBrush);
            ip.drawRect(r);
            const QPoint pts[8] = {
                r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft(),
                QPoint(r.center().x(), r.top()), QPoint(r.right(), r.center().y()),
                QPoint(r.center().x(), r.bottom()), QPoint(r.left(), r.center().y())
            };
            ip.setBrush(QColor("#FFFFFF"));
            ip.setPen(QPen(QColor("#2563EB"), 1.4));
            for (const QPoint& pt : pts)
                ip.drawRect(QRect(pt.x() - 4, pt.y() - 4, 8, 8));
        }
    }

    if (!m_paged) return;

    // 2) Overlay page chrome: gray gaps carved from the inter-page margins,
    //    soft shadows, and footer page numbers — drawn in widget coordinates,
    //    which equal document coordinates because we never scroll internally.
    const int pages = pageCountValue();
    const int w = viewport()->width();

    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing);

    const double gap  = qBound(18.0, m_margin * 0.66, 46.0);   // gray gap height
    const double half = gap / 2.0;
    const double scale = m_pageH / 1123.0;
    QFont ff("Segoe UI", qMax(7, qRound(8.5 * scale)));
    p.setFont(ff);

    // Content that sits across a page boundary, which the gap band must not be
    // painted over.
    //
    // The band is page CHROME and it is drawn after the text, so anything the
    // layout put in the inter-page margins gets destroyed by it - the reader
    // sees a line of text with its top half missing. Normally nothing is there,
    // because Qt breaks pages inside the margins. But Qt cannot break a single
    // line, so a line taller than the content area (a large picture, a tall
    // table row) legitimately spans the boundary and has to survive.
    //
    // Blocks are found by hit-testing the band rather than by walking the
    // document, so this stays proportional to the number of pages on screen
    // instead of the length of the document.
    QRegion protectedContent;
    if (pages > 1) {
        auto* lay = document()->documentLayout();
        for (int i = 0; i < pages - 1; ++i) {
            const double bottom = (i + 1) * m_pageH;
            const QRectF band(0, bottom - half - 7, w, gap + 8);
            const double probes[3] = { bottom - half, bottom, bottom + half };
            for (double y : probes) {
                const int pos = lay->hitTest(QPointF(m_pageW / 2.0, y), Qt::FuzzyHit);
                if (pos < 0) continue;
                const QTextBlock hit = document()->findBlock(pos);
                const QTextBlock around[3] = { hit.previous(), hit, hit.next() };
                for (const QTextBlock& b : around) {
                    if (!b.isValid()) continue;
                    QTextLayout* tl = b.layout();
                    if (!tl) continue;
                    const QPointF org = lay->blockBoundingRect(b).topLeft();
                    for (int k = 0; k < tl->lineCount(); ++k) {
                        const QRectF lr = tl->lineAt(k).rect().translated(org);
                        if (lr.intersects(band))
                            protectedContent += lr.toAlignedRect().adjusted(-2, -1, 2, 1);
                    }
                }
            }
        }
    }

    for (int i = 0; i < pages; ++i) {
        const double bottom = (i + 1) * m_pageH;        // page i bottom edge

        // Gap band between this page and the next (not after the last page).
        if (i < pages - 1) {
            p.save();
            if (!protectedContent.isEmpty())
                p.setClipRegion(QRegion(viewport()->rect()) - protectedContent);
            p.fillRect(QRectF(0, bottom - half, w, gap), m_desk);
            // shadow under the upper page's edge
            QLinearGradient up(0, bottom - half - 7, 0, bottom - half);
            up.setColorAt(0.0, QColor(0, 0, 0, 0));
            up.setColorAt(1.0, QColor(0, 0, 0, 38));
            p.fillRect(QRectF(0, bottom - half - 7, w, 7), up);
            // faint top highlight on the lower page's edge
            p.fillRect(QRectF(0, bottom + half, w, 1), QColor(0, 0, 0, 18));
            p.restore();
        }

        // Header & footer zones (left / center / right), drawn in the margins.
        const int pageNum = i + 1;
        const bool skip = m_diffFirst && i == 0;
        if (!skip) {
            p.setPen(QColor(120, 124, 132));
            const double contentX = m_margin;
            const double contentW = w - 2 * m_margin;
            const int aligns[3] = { Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight };

            // Header — in the top margin of this page.
            const double headTop = i * m_pageH + m_margin * 0.34;
            const double headH   = m_margin * 0.42;
            for (int z = 0; z < 3; ++z) {
                const QString txt = expandFields(m_header[z], pageNum, pages);
                if (!txt.isEmpty())
                    p.drawText(QRectF(contentX, headTop, contentW, headH),
                               aligns[z] | Qt::AlignVCenter, txt);
            }
            // Footer — in the white part of the bottom margin (above any gap band).
            const double footTop = bottom - m_margin + m_margin * 0.18;
            const double footBot = (i < pages - 1) ? bottom - half - 5 : bottom - m_margin * 0.22;
            if (footBot - footTop > 6.0) {
                for (int z = 0; z < 3; ++z) {
                    const QString txt = expandFields(m_footer[z], pageNum, pages);
                    if (!txt.isEmpty())
                        p.drawText(QRectF(contentX, footTop, contentW, footBot - footTop),
                                   aligns[z] | Qt::AlignVCenter, txt);
                }
            }
        }
    }
    p.end();
}

} // namespace NativeOffice
