// ─────────────────────────────────────────────────────────────────────────────
// WatermarkPdf.cpp — PDFium-backed watermark stamping and link attachment.
// ─────────────────────────────────────────────────────────────────────────────
#include "WatermarkPdf.h"
#include "Watermark.h"

#include <fpdf_annot.h>
#include <fpdf_edit.h>
#include <fpdf_save.h>
#include <fpdfview.h>

#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSaveFile>
#include <QSizeF>

#include <algorithm>

namespace NativeOffice {
namespace Watermark {

namespace {

// FPDF_InitLibrary must run once per process before any other call. The PDF
// module has its own guard for the same reason; both resolve to this one
// static because the library is reference-counted by neither, so keeping the
// init alive for the process lifetime is the safe arrangement.
struct PdfiumLibrary {
    PdfiumLibrary()  { FPDF_InitLibrary(); }
    ~PdfiumLibrary() { FPDF_DestroyLibrary(); }
};
void ensurePdfium() { static PdfiumLibrary lib; (void)lib; }

// Writes `doc` to a sibling temp file and returns its path, or an empty string
// on failure.
//
// It has to be a separate file, not the original. PDFium keeps the source file
// open for the lifetime of the document and streams from it while saving, so
// writing over the original here fails on Windows with a sharing violation and
// would corrupt the export on any platform that allowed it. The caller closes
// the document first, then swaps the temp into place.
QString saveToTemp(FPDF_DOCUMENT doc, const QString& path) {
    const QString tmp = path + QStringLiteral(".nowm.tmp");
    QFile out(tmp);
    if (!out.open(QIODevice::WriteOnly)) return {};

    struct Sink : FPDF_FILEWRITE { QFile* f; bool ok; } sink;
    sink.version = 1;
    sink.f  = &out;
    sink.ok = true;
    sink.WriteBlock = [](FPDF_FILEWRITE* self, const void* data,
                         unsigned long size) -> int {
        auto* s = static_cast<Sink*>(self);
        const qint64 n = s->f->write(static_cast<const char*>(data), qint64(size));
        if (n != qint64(size)) s->ok = false;
        return s->ok ? 1 : 0;
    };

    const bool saved = FPDF_SaveAsCopy(doc, &sink, 0) != 0;
    out.close();
    if (!saved || !sink.ok) { QFile::remove(tmp); return {}; }
    return tmp;
}

// Swaps the stamped temp file over the original. Only called once the document
// is closed and the source file handle released.
bool commitTemp(const QString& tmp, const QString& path) {
    if (tmp.isEmpty()) return false;
    if (!QFile::remove(path)) { QFile::remove(tmp); return false; }
    if (!QFile::rename(tmp, path)) { QFile::remove(tmp); return false; }
    return true;
}

// Attaches one invisible link over `rect` (PDF points, origin bottom-left).
//
// Invisible and tightly scoped is the whole point here:
//   • The rect is the artwork's measured bounding box, nothing padded.
//   • Border width 0 removes the ruled box some viewers draw around links.
//   • No appearance stream is attached, so the annotation contributes nothing
//     to the rendered page; the visible mark is the page content underneath.
bool addLink(FPDF_PAGE page, const QRectF& rect) {
    FPDF_ANNOTATION annot = FPDFPage_CreateAnnot(page, FPDF_ANNOT_LINK);
    if (!annot) return false;

    FS_RECTF r;
    r.left   = float(rect.left());
    r.right  = float(rect.right());
    r.bottom = float(rect.top());        // QRectF top/bottom are y-down; the
    r.top    = float(rect.bottom());     // caller hands us y-up PDF points.
    if (r.bottom > r.top) std::swap(r.bottom, r.top);

    bool ok = FPDFAnnot_SetRect(annot, &r) != 0;
    ok = FPDFAnnot_SetBorder(annot, 0.f, 0.f, 0.f) != 0 && ok;
    ok = FPDFAnnot_SetURI(annot, targetUrl().toLatin1().constData()) != 0 && ok;

    FPDFPage_CloseAnnot(annot);
    return ok;
}

} // namespace

bool stampIfRequired(const QString& pdfPath) {
    if (!enabledForExport()) return true;
    return stampFile(pdfPath);
}

bool stampFile(const QString& pdfPath) {
    if (!QFileInfo::exists(pdfPath)) return false;
    ensurePdfium();

    FPDF_DOCUMENT doc = FPDF_LoadDocument(pdfPath.toUtf8().constData(), nullptr);
    if (!doc) return false;

    // One shared bitmap for every page: the artwork is identical throughout and
    // PDFium copies the pixels into the document on SetBitmap.
    const QImage art = renderImage(4.0).convertToFormat(QImage::Format_RGBA8888);
    if (art.isNull()) { FPDF_CloseDocument(doc); return false; }

    FPDF_BITMAP bmp = FPDFBitmap_CreateEx(art.width(), art.height(),
                                          FPDFBitmap_BGRA, nullptr, 0);
    if (!bmp) { FPDF_CloseDocument(doc); return false; }
    {
        // PDFium wants BGRA; QImage gave us RGBA, so swap per pixel.
        auto* dst = static_cast<unsigned char*>(FPDFBitmap_GetBuffer(bmp));
        const int stride = FPDFBitmap_GetStride(bmp);
        for (int y = 0; y < art.height(); ++y) {
            const uchar* src = art.constScanLine(y);
            unsigned char* row = dst + qsizetype(y) * stride;
            for (int x = 0; x < art.width(); ++x) {
                row[x * 4 + 0] = src[x * 4 + 2];   // B
                row[x * 4 + 1] = src[x * 4 + 1];   // G
                row[x * 4 + 2] = src[x * 4 + 0];   // R
                row[x * 4 + 3] = src[x * 4 + 3];   // A
            }
        }
    }

    const QSizeF sizePt = sizePoints();
    const qreal  margin = marginPoints();
    const int    pageCount = FPDF_GetPageCount(doc);
    bool ok = true;
    bool anyStamped = false;

    for (int i = 0; i < pageCount; ++i) {
        FPDF_PAGE page = FPDF_LoadPage(doc, i);
        if (!page) { ok = false; continue; }

        const double pw = FPDF_GetPageWidth(page);
        const double ph = FPDF_GetPageHeight(page);

        // A page too small to hold the mark plus its margin is left alone
        // rather than having the artwork spill off the edge.
        if (pw < sizePt.width() + 2 * margin || ph < sizePt.height() + 2 * margin) {
            FPDF_ClosePage(page);
            continue;
        }

        const double x = pw - margin - sizePt.width();
        const double y = margin;                       // PDF origin is bottom-left

        FPDF_PAGEOBJECT img = FPDFPageObj_NewImageObj(doc);
        if (!img) { ok = false; FPDF_ClosePage(page); continue; }

        // The matrix maps the unit square onto the target rect, which is how
        // PDFium places image objects.
        FS_MATRIX m;
        m.a = float(sizePt.width());  m.b = 0.f;
        m.c = 0.f;                    m.d = float(sizePt.height());
        m.e = float(x);               m.f = float(y);

        if (!FPDFImageObj_SetBitmap(&page, 0, img, bmp) ||
            !FPDFPageObj_SetMatrix(img, &m)) {
            FPDFPageObj_Destroy(img);
            ok = false;
            FPDF_ClosePage(page);
            continue;
        }

        FPDFPage_InsertObject(page, img);
        if (!FPDFPage_GenerateContent(page)) ok = false;

        // Hotspot exactly over the artwork just placed.
        if (!addLink(page, QRectF(x, y, sizePt.width(), sizePt.height())))
            ok = false;

        anyStamped = true;
        FPDF_ClosePage(page);
    }

    FPDFBitmap_Destroy(bmp);

    // Save, then close, then swap: the document must let go of the source file
    // before the temp can replace it.
    QString tmp;
    if (ok && anyStamped) tmp = saveToTemp(doc, pdfPath);
    FPDF_CloseDocument(doc);

    if (!anyStamped) return true;           // nothing to stamp is not a failure
    if (!ok || tmp.isEmpty()) { QFile::remove(tmp); return false; }
    return commitTemp(tmp, pdfPath);
}

} // namespace Watermark
} // namespace NativeOffice
