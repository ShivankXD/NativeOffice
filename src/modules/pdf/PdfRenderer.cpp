// ─────────────────────────────────────────────────────────────────────────────
// PdfRenderer.cpp — see PdfRenderer.h. All PDFium usage in the codebase lives
// behind this file (plus the feature-specific users that need fpdf_annot/
// fpdf_formfill later); nothing else includes PDFium headers directly.
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfRenderer.h"

// fpdfview.h includes windows.h; keep its min/max macros from breaking std::.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <fpdfview.h>
#include <fpdf_doc.h>
#include <fpdf_edit.h>
#include <fpdf_formfill.h>
#include <fpdf_save.h>
#include <fpdf_text.h>

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cmath>

namespace NativeOffice::Pdf {

namespace {

// FPDF_InitLibrary must run once before any other call; DestroyLibrary at
// process exit. A function-local static gives exactly that lifetime.
struct PdfiumLibrary {
    PdfiumLibrary()  { FPDF_InitLibrary(); }
    ~PdfiumLibrary() { FPDF_DestroyLibrary(); }
};

void ensurePdfiumInit() {
    static PdfiumLibrary lib;
    (void)lib;
}

// PDFium returns UTF-16LE with a trailing NUL for all its string getters.
QString fromUtf16Buffer(const std::vector<unsigned short>& buf) {
    if (buf.empty()) return {};
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(buf.data()),
                              int(buf.size()) - 1);   // drop trailing NUL
}

// Groups character boxes into per-line rectangles: chars whose vertical
// centers overlap are one line; each line contributes the union of its boxes.
std::vector<QRectF> groupIntoLineRects(std::vector<QRectF> boxes) {
    std::vector<QRectF> lines;
    std::sort(boxes.begin(), boxes.end(), [](const QRectF& a, const QRectF& b) {
        return a.center().y() < b.center().y() || (a.center().y() == b.center().y() && a.x() < b.x());
    });
    for (const QRectF& b : boxes) {
        if (b.isEmpty()) continue;
        if (!lines.empty()) {
            QRectF& last = lines.back();
            const qreal cy = b.center().y();
            if (cy >= last.top() && cy <= last.bottom()) {
                last = last.united(b);
                continue;
            }
        }
        lines.push_back(b);
    }
    return lines;
}

} // namespace

std::unique_ptr<Renderer> Renderer::open(const QString& path,
                                         const QString& password,
                                         RenderOpenStatus& status) {
    ensurePdfiumInit();

    QFile f(path);
    if (!f.exists()) { status = RenderOpenStatus::FileNotFound; return nullptr; }
    if (!f.open(QIODevice::ReadOnly)) { status = RenderOpenStatus::Unknown; return nullptr; }

    std::unique_ptr<Renderer> r(new Renderer);
    r->m_data = f.readAll();
    f.close();

    const QByteArray pwd = password.toUtf8();
    r->m_doc = FPDF_LoadMemDocument(r->m_data.constData(), int(r->m_data.size()),
                                    pwd.isEmpty() ? nullptr : pwd.constData());
    if (!r->m_doc) {
        switch (FPDF_GetLastError()) {
            case FPDF_ERR_PASSWORD: status = RenderOpenStatus::PasswordRequired; break;
            case FPDF_ERR_FORMAT:   status = RenderOpenStatus::Corrupt;          break;
            case FPDF_ERR_FILE:     status = RenderOpenStatus::FileNotFound;     break;
            default:                status = RenderOpenStatus::Unknown;          break;
        }
        return nullptr;
    }

    const int n = FPDF_GetPageCount(r->m_doc);
    r->m_pageSizes.reserve(n);
    for (int i = 0; i < n; ++i) {
        FS_SIZEF sz{};
        if (FPDF_GetPageSizeByIndexF(r->m_doc, i, &sz))
            r->m_pageSizes.emplace_back(sz.width, sz.height);
        else
            r->m_pageSizes.emplace_back(612.0, 792.0);   // Letter fallback
    }
    r->m_pageCache.assign(n, nullptr);
    r->m_textCache.assign(n, nullptr);

    // Init a form-fill environment so Widget (AcroForm field) annotations
    // render. Rendering-only: all interaction callbacks are left null. The
    // FPDF_FORMFILLINFO must outlive the handle, so it's heap-allocated.
    auto* ffi = new FPDF_FORMFILLINFO{};
    ffi->version = 1;
    r->m_formFillInfo = ffi;
    r->m_form = FPDFDOC_InitFormFillEnvironment(r->m_doc, ffi);
    if (r->m_form)
        FPDF_SetFormFieldHighlightAlpha(r->m_form, 0);   // no field tint in render

    status = RenderOpenStatus::Ok;
    return r;
}

Renderer::~Renderer() {
    for (FPDF_TEXTPAGE tp : m_textCache)
        if (tp) FPDFText_ClosePage(tp);
    for (FPDF_PAGE p : m_pageCache)
        if (p) FPDF_ClosePage(p);
    if (m_form) FPDFDOC_ExitFormFillEnvironment(m_form);
    delete static_cast<FPDF_FORMFILLINFO*>(m_formFillInfo);
    if (m_doc) FPDF_CloseDocument(m_doc);
}

FPDF_PAGE Renderer::pageHandle(int pageIndex) const {
    if (pageIndex < 0 || pageIndex >= pageCount()) return nullptr;
    if (!m_pageCache[pageIndex])
        m_pageCache[pageIndex] = FPDF_LoadPage(m_doc, pageIndex);
    return m_pageCache[pageIndex];
}

FPDF_TEXTPAGE Renderer::textPageHandle(int pageIndex) const {
    if (pageIndex < 0 || pageIndex >= pageCount()) return nullptr;
    if (!m_textCache[pageIndex]) {
        if (FPDF_PAGE p = pageHandle(pageIndex))
            m_textCache[pageIndex] = FPDFText_LoadPage(p);
    }
    return m_textCache[pageIndex];
}

QSizeF Renderer::pageSizePt(int pageIndex) const {
    if (pageIndex < 0 || pageIndex >= pageCount()) return {};
    return m_pageSizes[pageIndex];
}

QImage Renderer::renderPage(int pageIndex, qreal scale) const {
    FPDF_PAGE page = pageHandle(pageIndex);
    if (!page || scale <= 0) return {};

    const int w = std::max(1, int(FPDF_GetPageWidthF(page)  * scale + 0.5));
    const int h = std::max(1, int(FPDF_GetPageHeightF(page) * scale + 0.5));

    // QImage ARGB32 on little-endian is BGRA in memory — exactly PDFium's
    // FPDFBitmap_BGRA layout, so PDFium renders straight into the QImage.
    QImage img(w, h, QImage::Format_ARGB32);
    if (img.isNull()) return {};
    img.fill(Qt::white);

    FPDF_BITMAP bmp = FPDFBitmap_CreateEx(w, h, FPDFBitmap_BGRA,
                                          img.bits(), int(img.bytesPerLine()));
    if (!bmp) return {};
    const int flags = FPDF_ANNOT | FPDF_LCD_TEXT;
    FPDF_RenderPageBitmap(bmp, page, 0, 0, w, h, 0, flags);
    // Overlay form-field widgets (skipped by the base page render).
    if (m_form)
        FPDF_FFLDraw(m_form, bmp, page, 0, 0, w, h, 0, flags);
    FPDFBitmap_Destroy(bmp);
    return img;
}

QString Renderer::pageText(int pageIndex) const {
    FPDF_TEXTPAGE tp = textPageHandle(pageIndex);
    if (!tp) return {};
    const int nChars = FPDFText_CountChars(tp);
    if (nChars <= 0) return {};
    std::vector<unsigned short> buf(size_t(nChars) + 1, 0);
    const int written = FPDFText_GetText(tp, 0, nChars, buf.data());
    buf.resize(size_t(std::max(written, 0)));
    return fromUtf16Buffer(buf);
}

std::vector<Renderer::CharBox> Renderer::pageChars(int pageIndex) const {
    std::vector<CharBox> out;
    FPDF_TEXTPAGE tp = textPageHandle(pageIndex);
    if (!tp) return out;
    const qreal pageH = pageSizePt(pageIndex).height();
    const int n = FPDFText_CountChars(tp);
    out.reserve(size_t(std::max(n, 0)));
    for (int i = 0; i < n; ++i) {
        double l = 0, r = 0, b = 0, t = 0;
        if (!FPDFText_GetCharBox(tp, i, &l, &r, &b, &t)) continue;
        CharBox cb;
        cb.ch  = QChar(ushort(FPDFText_GetUnicode(tp, i)));
        cb.box = QRectF(l, pageH - t, r - l, t - b);    // flip to top-left origin

        // GetFontSize returns the unscaled Tf operand; text that is shrunk via
        // the text matrix (common for tightly-set headings/labels) reports a
        // huge nominal size. Multiply by the matrix's vertical scale to get the
        // size actually seen on the page — without this a 13pt label set at
        // Tf 77pt with a 0.17 matrix scaled a résumé to 20 Word pages.
        cb.fontSize = FPDFText_GetFontSize(tp, i);      // points (nominal)
        FS_MATRIX cm;
        if (FPDFText_GetMatrix(tp, i, &cm)) {
            const double scaleY = std::sqrt(double(cm.b) * cm.b + double(cm.d) * cm.d);
            if (scaleY > 0) cb.fontSize *= scaleY;
        }

        unsigned int R = 0, G = 0, B = 0, A = 0;
        if (FPDFText_GetFillColor(tp, i, &R, &G, &B, &A) && A > 0)
            cb.color = QColor(int(R), int(G), int(B));

        // Weight/slant: the descriptor flags are authoritative when present
        // (ForceBold bit 19, Italic bit 7), but many bold/italic fonts don't
        // set them — fall back to the font's own name.
        int flags = 0;
        char nameBuf[128] = {0};
        const unsigned long nlen =
            FPDFText_GetFontInfo(tp, i, nameBuf, sizeof(nameBuf), &flags);
        const QString fontName =
            QString::fromUtf8(nameBuf, nlen > 0 ? int(nlen) - 1 : 0);
        cb.italic = (flags & (1 << 6)) != 0
                 || fontName.contains("Italic", Qt::CaseInsensitive)
                 || fontName.contains("Oblique", Qt::CaseInsensitive);
        cb.bold = (flags & (1 << 18)) != 0
               || fontName.contains("Bold", Qt::CaseInsensitive)
               || fontName.contains("Black", Qt::CaseInsensitive)
               || fontName.contains("Heavy", Qt::CaseInsensitive)
               || fontName.contains("Semibold", Qt::CaseInsensitive);

        out.push_back(cb);
    }
    return out;
}

// Shared conversion of a PDFium bitmap into a Qt image (deep copy: the caller
// destroys the FPDF_BITMAP right after).
static QImage bitmapToQImage(void* bmpv) {
    FPDF_BITMAP bmp = static_cast<FPDF_BITMAP>(bmpv);
    const int w = FPDFBitmap_GetWidth(bmp);
    const int h = FPDFBitmap_GetHeight(bmp);
    const int stride = FPDFBitmap_GetStride(bmp);
    const int format = FPDFBitmap_GetFormat(bmp);
    const auto* buf = static_cast<const uchar*>(FPDFBitmap_GetBuffer(bmp));
    if (w <= 0 || h <= 0 || !buf) return {};
    QImage::Format qf = (format == FPDFBitmap_BGRA) ? QImage::Format_ARGB32
                      : (format == FPDFBitmap_BGRx) ? QImage::Format_RGB32
                                                    : QImage::Format_RGB888;
    // BGR(x/a) memory maps to Qt's ARGB32/RGB32 on little-endian; BGR
    // (3-channel) needs an explicit swap.
    QImage img(buf, w, h, stride, qf);
    return format == FPDFBitmap_BGR ? img.rgbSwapped().copy() : img.copy();
}

std::vector<QImage> Renderer::pageImages(int pageIndex) const {
    std::vector<QImage> out;
    FPDF_PAGE page = pageHandle(pageIndex);
    if (!page) return out;
    const int nObj = FPDFPage_CountObjects(page);
    for (int i = 0; i < nObj; ++i) {
        FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
        if (!obj || FPDFPageObj_GetType(obj) != FPDF_PAGEOBJ_IMAGE) continue;
        FPDF_BITMAP bmp = FPDFImageObj_GetRenderedBitmap(m_doc, page, obj);
        if (!bmp) continue;
        QImage img = bitmapToQImage(bmp);
        if (!img.isNull()) out.push_back(std::move(img));
        FPDFBitmap_Destroy(bmp);
    }
    return out;
}

std::vector<Renderer::PlacedImage> Renderer::pagePlacedImages(int pageIndex) const {
    std::vector<PlacedImage> out;
    FPDF_PAGE page = pageHandle(pageIndex);
    if (!page) return out;
    const qreal pageH = pageSizePt(pageIndex).height();
    const int nObj = FPDFPage_CountObjects(page);
    for (int i = 0; i < nObj; ++i) {
        FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
        if (!obj || FPDFPageObj_GetType(obj) != FPDF_PAGEOBJ_IMAGE) continue;
        float l = 0, b = 0, r = 0, t = 0;
        if (!FPDFPageObj_GetBounds(obj, &l, &b, &r, &t)) continue;
        FPDF_BITMAP bmp = FPDFImageObj_GetRenderedBitmap(m_doc, page, obj);
        if (!bmp) continue;
        QImage img = bitmapToQImage(bmp);
        FPDFBitmap_Destroy(bmp);
        if (img.isNull()) continue;
        PlacedImage pi;
        pi.img = std::move(img);
        pi.box = QRectF(l, pageH - t, r - l, t - b);   // flip to top-left origin
        out.push_back(std::move(pi));
    }
    return out;
}

std::vector<QRectF> Renderer::snapTextRects(int pageIndex, const QRectF& area) const {
    std::vector<QRectF> boxes;
    for (const CharBox& cb : pageChars(pageIndex))
        if (!cb.box.isEmpty() && area.contains(cb.box.center()))
            boxes.push_back(cb.box);
    return groupIntoLineRects(std::move(boxes));
}

std::vector<QRectF> Renderer::searchPage(int pageIndex, const QString& needle) const {
    std::vector<QRectF> out;
    FPDF_TEXTPAGE tp = textPageHandle(pageIndex);
    if (!tp || needle.isEmpty()) return out;
    const qreal pageH = pageSizePt(pageIndex).height();

    std::vector<unsigned short> term(size_t(needle.size()) + 1, 0);
    for (int i = 0; i < needle.size(); ++i)
        term[size_t(i)] = needle.at(i).unicode();

    FPDF_SCHHANDLE sh = FPDFText_FindStart(tp, term.data(), 0 /*case-insensitive*/, 0);
    if (!sh) return out;
    while (FPDFText_FindNext(sh)) {
        const int start = FPDFText_GetSchResultIndex(sh);
        const int count = FPDFText_GetSchCount(sh);
        std::vector<QRectF> boxes;
        for (int i = start; i < start + count; ++i) {
            double l = 0, r = 0, b = 0, t = 0;
            if (FPDFText_GetCharBox(tp, i, &l, &r, &b, &t))
                boxes.emplace_back(l, pageH - t, r - l, t - b);
        }
        for (QRectF& rect : groupIntoLineRects(std::move(boxes)))
            out.push_back(rect);
    }
    FPDFText_FindClose(sh);
    return out;
}

bool Renderer::saveDecryptedCopy(const QString& outPath) const {
    if (!m_doc) return false;

    struct FileSink : public FPDF_FILEWRITE {
        QFile file;
    } sink;
    sink.version = 1;
    sink.file.setFileName(outPath);
    if (!sink.file.open(QIODevice::WriteOnly)) return false;
    sink.WriteBlock = [](FPDF_FILEWRITE* self, const void* data, unsigned long size) -> int {
        auto* s = static_cast<FileSink*>(self);
        return s->file.write(static_cast<const char*>(data), qint64(size)) == qint64(size) ? 1 : 0;
    };

    const bool ok = FPDF_SaveAsCopy(m_doc, &sink, FPDF_REMOVE_SECURITY) != 0;
    sink.file.close();
    if (!ok) sink.file.remove();
    return ok;
}

std::vector<OutlineNode> Renderer::outline() const {
    // Recursive walk of the bookmark tree. `visited` guards against
    // malformed PDFs with sibling cycles.
    std::vector<OutlineNode> roots;
    if (!m_doc) return roots;

    struct Walker {
        FPDF_DOCUMENT doc;
        int visited = 0;

        void walk(FPDF_BOOKMARK bm, std::vector<OutlineNode>& out) {
            while (bm && ++visited < 4096) {
                OutlineNode node;
                const unsigned long len = FPDFBookmark_GetTitle(bm, nullptr, 0);
                if (len >= 2) {
                    std::vector<unsigned short> buf(len / 2, 0);
                    FPDFBookmark_GetTitle(bm, buf.data(), len);
                    node.title = fromUtf16Buffer(buf);
                }
                if (FPDF_DEST dest = FPDFBookmark_GetDest(doc, bm))
                    node.pageIndex = FPDFDest_GetDestPageIndex(doc, dest);
                walk(FPDFBookmark_GetFirstChild(doc, bm), node.children);
                out.push_back(std::move(node));
                bm = FPDFBookmark_GetNextSibling(doc, bm);
            }
        }
    } walker{m_doc};

    walker.walk(FPDFBookmark_GetFirstChild(m_doc, nullptr), roots);
    return roots;
}

} // namespace NativeOffice::Pdf
