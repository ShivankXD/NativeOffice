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

namespace NativeOffice {

// Char-format properties tagging tracked-change runs.
static constexpr int kTrackInsert = QTextFormat::UserProperty + 30;
static constexpr int kTrackDelete = QTextFormat::UserProperty + 31;
static const QColor kInsColor("#1A7F37");   // green — inserted
static const QColor kDelColor("#C0271C");   // red   — deleted

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

    connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
            this, [this](const QSizeF&) { syncHeight(); });

    // Spell checking: a syntax highlighter draws red squiggles under misspellings.
    // Starts disabled; WriterModule turns it on to match the status-bar pill.
    m_spell = new SpellHighlighter(document());
}

void PagedTextEdit::setZoom(double factor) {
    if (m_spell) m_spell->setZoom(factor);
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
        document()->setPageSize(QSizeF(pageW, pageH));
        setFixedWidth(qRound(pageW));
    }
    syncHeight();
    viewport()->update();
}

void PagedTextEdit::setPaged(bool on) {
    if (m_paged == on) return;
    m_paged = on;
    if (on) {
        document()->setPageSize(QSizeF(m_pageW, m_pageH));
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

void PagedTextEdit::syncHeight() {
    double h = m_paged ? pageCountValue() * m_pageH
                       : document()->size().height();
    if (h < 1.0) h = m_pageH;
    setFixedHeight(static_cast<int>(std::ceil(h)) + 1);
}

void PagedTextEdit::resizeEvent(QResizeEvent* e) {
    QTextEdit::resizeEvent(e);
    // QTextEdit re-derives the document width on resize; re-assert pagination.
    if (m_paged) document()->setPageSize(QSizeF(m_pageW, m_pageH));
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
    const QRect ra = cursorRect(a);
    const QRect rb = cursorRect(b);
    int x = ra.left();
    int w = rb.left() - ra.left();
    int top = ra.top();
    int h = ra.height();
    const QTextImageFormat f = selectedImageFormat();
    if (w <= 2 && f.width() > 0)  w = int(f.width());
    if (h <= 2 && f.height() > 0) h = int(f.height());
    return QRect(x, top, qMax(w, 1), qMax(h, 1));
}

int PagedTextEdit::handleAt(const QPoint& p) const {
    if (m_imagePos < 0) return -1;
    const QRect r = selectedImageRect();
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
    // Fit within the content area between the margins.
    const int maxWidth = qMax(100, int(m_pageW - 2 * m_margin));
    if (img.width() > maxWidth)
        img = img.scaledToWidth(maxWidth, Qt::SmoothTransformation);

    QByteArray data;
    {
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        img.save(&buffer, "PNG");
    }
    textCursor().insertHtml(QStringLiteral("<img src=\"data:image/png;base64,%1\"/>")
                                .arg(QString::fromLatin1(data.toBase64())));
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
        const QString before = block.left(qMax(0, wordStart)).trimmed();
        sentenceStart = before.isEmpty() || AutoCorrect::isSentenceEnd(before.back());
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

    for (int i = 0; i < pages; ++i) {
        const double bottom = (i + 1) * m_pageH;        // page i bottom edge

        // Gap band between this page and the next (not after the last page).
        if (i < pages - 1) {
            p.fillRect(QRectF(0, bottom - half, w, gap), m_desk);
            // shadow under the upper page's edge
            QLinearGradient up(0, bottom - half - 7, 0, bottom - half);
            up.setColorAt(0.0, QColor(0, 0, 0, 0));
            up.setColorAt(1.0, QColor(0, 0, 0, 38));
            p.fillRect(QRectF(0, bottom - half - 7, w, 7), up);
            // faint top highlight on the lower page's edge
            p.fillRect(QRectF(0, bottom + half, w, 1), QColor(0, 0, 0, 18));
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
