#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfRenderer.h — RAII wrapper around the vendored PDFium engine
// (third_party/pdfium). This is the module's only rasterization path: the
// hand-rolled PdfDocument/PdfWriter stay in charge of *structural* edits
// (merge/split/pages/annots/watermarks), while Renderer answers "what does
// page N look like" and "where is the text".
//
// PDFium is NOT thread-safe (global library state): every call, including
// open() and the destructor, must happen on the GUI thread. The viewer keeps
// interaction smooth with a raster cache, not with worker threads.
// ─────────────────────────────────────────────────────────────────────────────

#include <QImage>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <memory>
#include <vector>

// PDFium C handles, forward-declared exactly as fpdfview.h defines them
// (avoids leaking the PDFium headers into every consumer of this class).
typedef struct fpdf_document_t__* FPDF_DOCUMENT;
typedef struct fpdf_page_t__*     FPDF_PAGE;
typedef struct fpdf_textpage_t__* FPDF_TEXTPAGE;

namespace NativeOffice::Pdf {

enum class RenderOpenStatus {
    Ok,
    FileNotFound,
    PasswordRequired,   // encrypted and no/wrong password supplied
    Corrupt,
    Unknown,
};

// One entry of the document outline ("bookmarks" sidebar). Children are
// nested; pageIndex is -1 when the bookmark has no resolvable destination.
struct OutlineNode {
    QString title;
    int pageIndex = -1;
    std::vector<OutlineNode> children;
};

class Renderer {
public:
    // Loads the whole file into memory and opens it via PDFium. On failure
    // returns nullptr with `status` set. `password` may be empty; if the file
    // is encrypted, PasswordRequired is reported (also for a wrong password —
    // PDFium doesn't distinguish).
    static std::unique_ptr<Renderer> open(const QString& path,
                                          const QString& password,
                                          RenderOpenStatus& status);
    ~Renderer();

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    [[nodiscard]] int pageCount() const { return int(m_pageSizes.size()); }

    // Display size of a page in PDF points (1/72"), /Rotate already applied.
    [[nodiscard]] QSizeF pageSizePt(int pageIndex) const;

    // Rasterizes a page at `scale` (1.0 = 72 dpi). Annotations are rendered.
    // Returns a null image for an invalid index or an unrenderable page.
    [[nodiscard]] QImage renderPage(int pageIndex, qreal scale) const;

    // Plain text of one page (PDFium's extraction order).
    [[nodiscard]] QString pageText(int pageIndex) const;

    // Per-character geometry, in page points with a TOP-LEFT origin (PDF's
    // native origin is bottom-left; the flip happens here so all UI code
    // works in the same space it draws in).
    struct CharBox {
        QChar  ch;
        QRectF box;
    };
    [[nodiscard]] std::vector<CharBox> pageChars(int pageIndex) const;

    // Line-grouped rectangles for the characters inside `area` (page points,
    // top-left origin) — what Highlight/Underline/StrikeOut snap to.
    [[nodiscard]] std::vector<QRectF> snapTextRects(int pageIndex, const QRectF& area) const;

    // All match rectangles of `needle` on a page (case-insensitive),
    // line-grouped, top-left origin. Used by Find and the search sidebar.
    [[nodiscard]] std::vector<QRectF> searchPage(int pageIndex, const QString& needle) const;

    // Document outline for the bookmarks panel (empty if the PDF has none).
    [[nodiscard]] std::vector<OutlineNode> outline() const;

    // Writes the document to `outPath` with its encryption removed. Used when
    // opening password-protected files: the structural reader/writer only
    // work on unencrypted data, so the edit session materializes a decrypted
    // working copy first. Requires the document to have been opened with the
    // correct password.
    bool saveDecryptedCopy(const QString& outPath) const;

private:
    Renderer() = default;

    FPDF_PAGE     pageHandle(int pageIndex) const;      // lazily opened, cached
    FPDF_TEXTPAGE textPageHandle(int pageIndex) const;  // lazily opened, cached

    QByteArray    m_data;      // must outlive m_doc (FPDF_LoadMemDocument)
    FPDF_DOCUMENT m_doc = nullptr;

    std::vector<QSizeF> m_pageSizes;                    // filled at open()
    mutable std::vector<FPDF_PAGE>     m_pageCache;     // index-aligned, null until used
    mutable std::vector<FPDF_TEXTPAGE> m_textCache;
};

} // namespace NativeOffice::Pdf
