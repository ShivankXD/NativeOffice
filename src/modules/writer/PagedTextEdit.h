#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PagedTextEdit.h  (Sprint 17 — real pagination)
// A QTextEdit that renders its document as discrete A4-style pages: the
// document's page size is set so content reflows into real pages (respecting
// manual page breaks), the widget sizes itself to the full multi-page height so
// the surrounding scroll area scrolls through every page, and paintEvent draws
// the gray gaps between pages, soft per-page shadows and footer page numbers.
//
// Crucially the text is NOT offset, so QTextEdit's own cursor, selection,
// hit-testing, IME and editing all keep working unchanged.
// ─────────────────────────────────────────────────────────────────────────────

#include <QTextEdit>
#include <QColor>
#include <QRect>
#include "AutoCorrect.h"

class QTextImageFormat;

namespace NativeOffice {

// Char-format property holding a comment's text on the commented range (shared by
// the ribbon that creates comments and the module that lists them in the pane).
inline constexpr int kCommentProp = QTextFormat::UserProperty + 32;

class SpellHighlighter;

class PagedTextEdit : public QTextEdit {
    Q_OBJECT

public:
    explicit PagedTextEdit(QWidget* parent = nullptr);

    // Page geometry in *device* px (already scaled for the current zoom).
    void setPageMetrics(double pageW, double pageH, double margin);

    // false → continuous "web layout" (no pages, no gaps).
    void setPaged(bool on);
    [[nodiscard]] bool isPaged() const noexcept { return m_paged; }

    // Colour of the paper (gaps always use the desk colour).
    void setPaperColor(const QColor& c);

    [[nodiscard]] int  pageCountValue() const;


    // 1-based page number the text cursor is currently on (1 in web layout).
    [[nodiscard]] int  currentPageNumber() const;

    // ── Headers & footers (drawn per page in the margins) ───────────────────
    // Each is 3 zones: left, center, right. Fields {n} {N} {date} {file} expand.
    void setHeaderFooter(const QStringList& header, const QStringList& footer,
                         bool differentFirstPage);
    [[nodiscard]] QStringList headerZones() const { return { m_header[0], m_header[1], m_header[2] }; }
    [[nodiscard]] QStringList footerZones() const { return { m_footer[0], m_footer[1], m_footer[2] }; }
    [[nodiscard]] bool differentFirstPage() const { return m_diffFirst; }
    void setDocName(const QString& n);

    // ── Zoom ──────────────────────────────────────────────────────────────
    // Presentation-only scale for explicitly-sized text runs (headings,
    // imported/ribbon-sized text). 1.0 == 100%. The default font + page metrics
    // are scaled separately by WriterModule; this makes the *rest* keep up.
    void setZoom(double factor);

    // ── Page operations (hover controls) ────────────────────────────────────
    // Each is a single undo step (Ctrl+Z restores). deletePage removes the page
    // AND the content that sits on it (like removing a slide); it refuses to
    // delete the only page and returns false so the caller can show a notice.
    void addBlankPageAbove(int pageIndex);
    void addBlankPageBelow(int pageIndex);
    bool deletePage(int pageIndex);
    // 0-based index of the page containing viewport y (or -1). Used by the
    // hover overlay to know which page the cursor is on.
    [[nodiscard]] int pageAtViewportY(int y) const;
    [[nodiscard]] double pageHeightPx() const noexcept { return m_pageH; }

    // ── Spell check ─────────────────────────────────────────────────────────
    void setSpellCheckEnabled(bool on);
    [[nodiscard]] bool spellCheckEnabled() const;
    void rehighlightSpelling();   // refresh squiggles after dictionary changes

    // ── AutoCorrect ─────────────────────────────────────────────────────────
    void setAutoCorrectEnabled(bool on) { m_autoCorrectEnabled = on; }
    [[nodiscard]] bool autoCorrectEnabled() const { return m_autoCorrectEnabled; }
    void setAutoCorrectSettings(const AutoCorrectSettings& s) { m_autoCorrect = s; }
    [[nodiscard]] AutoCorrectSettings autoCorrectSettings() const { return m_autoCorrect; }

    // ── Track changes ─────────────────────────────────────────────────────────
    void setTrackChanges(bool on) {
        m_trackChanges = on;
        if (!on) {   // stop tinting subsequently-typed text
            QTextCharFormat f;
            f.setForeground(QColor("#1C1E26"));
            f.setFontUnderline(false);
            f.setFontStrikeOut(false);
            mergeCurrentCharFormat(f);
        }
    }
    [[nodiscard]] bool trackChanges() const { return m_trackChanges; }
    void acceptAllChanges();    // keep insertions, drop deletions
    void rejectAllChanges();    // drop insertions, restore deletions

    // Per-change review (Word parity): the contiguous tracked run at `pos`
    // (start/end/isDeletion), or start=-1 when the position isn't tracked.
    struct TrackedRun { int start = -1; int end = -1; bool deletion = false; };
    [[nodiscard]] TrackedRun trackedRunAt(int pos) const;
    void resolveTrackedRun(const TrackedRun& run, bool accept);

signals:
    void imageSelectionChanged(bool hasImage);

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;   // start dismissing the page controls

    // Clipboard-paste and drag-drop of images (Word/WPS parity): raw image data
    // and local image files are embedded as base64 PNG scaled to the text width.
    bool canInsertFromMimeData(const QMimeData* source) const override;
    void insertFromMimeData(const QMimeData* source) override;

public:
    // Image actions (used by the context menu and could be wired to a ribbon).
    bool hasImageSelected() const { return m_imagePos >= 0; }
    void setImageWidth(int px);                 // aspect-locked
    void scaleImage(double factor);             // relative
    void alignImage(Qt::Alignment a);           // paragraph alignment
    void deleteSelectedImage();

private:
    void syncHeight();
    // Assigns the paginated page size only when it actually differs (Qt relayouts
    // the whole document on every setPageSize call, changed or not).
    void applyPageSize();
    // Puts pagination back when Qt has silently dropped it (setTextWidth ==
    // setPageSize(w, -1)); true if a restore was needed. See the .cpp.
    bool restorePaginationIfDropped();
    // True while QTextEdit::resizeEvent runs — the document is transiently
    // unpaginated in there, so its layout signals must not be acted on.
    bool m_inBaseResize { false };
    int  m_fixedH       { -1 };   // last height we forced; -1 = none yet
    // First text block that starts on the given 0-based page (invalid if none).
    [[nodiscard]] QTextBlock firstBlockOnPage(int pageIndex) const;
    // Insert an empty, page-breaking paragraph before `anchor` (or at the end of
    // the document if `anchor` is invalid), pushing `anchor` onto the next page.
    void insertBlankPageBefore(const QTextBlock& anchor);
    bool insertMimeImage(const QMimeData* source);   // true if an image was embedded
    void embedImage(QImage img);
    QTextImageFormat selectedImageFormat() const;
    QRect  selectedImageRect() const;           // viewport coords
    int    handleAt(const QPoint& p) const;      // -1 none, 0..7 handles
    void   selectImageAt(const QPoint& vpPos);   // pick image under a click
    void   applyImageSize(int w, int h);

    double m_pageW   { 794.0 };
    double m_pageH   { 1123.0 };
    double m_margin  { 60.0 };
    bool   m_paged   { true };
    QColor m_paper   { "#FFFFFF" };
    QColor m_desk    { "#E8E9ED" };

    // Header / footer (3 zones each: left, center, right)
    QString m_header[3] { QString(), QString(), QString() };
    QString m_footer[3] { QString(), QStringLiteral("Page {n} of {N}"), QString() };
    bool    m_diffFirst { false };
    QString m_docName;
    QString expandFields(const QString& s, int pageNum, int total) const;

    // ── Page hover controls (add/delete page) ──────────────────────────────
    class PageActionBar* m_pageBar { nullptr };
    int  m_barPage { -1 };            // page the bar currently acts on
    void updatePageBar(const QPoint& viewportPos);   // called from mouseMove

    // ── Spell check ───────────────────────────────────────────────────────────
    SpellHighlighter* m_spell { nullptr };

    // ── AutoCorrect ───────────────────────────────────────────────────────────
    bool               m_autoCorrectEnabled { true };
    AutoCorrectSettings m_autoCorrect;
    void applyAutoCorrectAtCursor();   // corrects the word just before the cursor

    // ── Track changes ───────────────────────────────────────────────────────────
    bool m_trackChanges { false };
    bool handleTrackedKey(QKeyEvent* e);   // returns true if it consumed the key

    // ── Image selection / resize ────────────────────────────────────────────
    int    m_imagePos    { -1 };     // position of the selected image char, or -1
    bool   m_resizing    { false };
    int    m_resizeHandle{ -1 };     // 0..7 while resizing
    QRect  m_resizeStart;            // image rect at drag start
    QPoint m_dragOrigin;             // mouse pos at drag start
    double m_imgAspect   { 1.0 };    // w/h at drag start
};

} // namespace NativeOffice
