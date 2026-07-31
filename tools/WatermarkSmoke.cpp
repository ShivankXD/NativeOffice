// ─────────────────────────────────────────────────────────────────────────────
// WatermarkSmoke.cpp — dev-only check that the export watermark lands correctly
// in a PDF: artwork drawn on every page, and one invisible link annotation per
// page whose rect matches the artwork's bounding box and whose action is the
// NativeOffice URL.
//
// The link is the part worth testing. It has to be invisible, present on every
// page, and confined to the mark itself; a loose or oversized hotspot would
// make clicks near the page corner follow the link by accident. This exercises
// the real Watermark::stampFile() path, then reopens the file with PDFium and
// asks it what a click at various points would hit.
//
// Not installed and not shipped. Build: --target WatermarkSmoke
// ─────────────────────────────────────────────────────────────────────────────
#include "watermark/Watermark.h"
#include "watermark/WatermarkPdf.h"

#include <fpdf_doc.h>
#include <fpdfview.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPageSize>
#include <QPainter>
#include <QImage>
#include <QPdfWriter>
#include <QString>

#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const QString& what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", qPrintable(what));
    if (!ok) ++failures;
}

QString uriOf(FPDF_DOCUMENT doc, FPDF_LINK link) {
    FPDF_ACTION act = FPDFLink_GetAction(link);
    if (!act) return {};
    const unsigned long n = FPDFAction_GetURIPath(doc, act, nullptr, 0);
    if (n == 0) return {};
    QByteArray buf(int(n), '\0');
    FPDFAction_GetURIPath(doc, act, buf.data(), n);
    return QString::fromLatin1(buf.constData());   // includes the NUL slot
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    const QString out = QDir::temp().filePath("nativeoffice-watermark-smoke.pdf");
    QFile::remove(out);

    // Dump the artwork on its own so the mark can be eyeballed against the
    // reference: grey "Made with", the brand icon, "Native" in ink and
    // "Office" in the gradient.
    {
        const QImage art = NativeOffice::Watermark::renderImage(8.0);
        const QString png = QDir::temp().filePath("nativeoffice-watermark-art.png");
        check(!art.isNull() && art.save(png), "artwork PNG rendered");
        check(art.width() > 40 && art.height() > 6, "artwork has sensible pixel size");
        std::printf("      artwork: %s (%dx%d)\n", qPrintable(png), art.width(), art.height());
    }

    // ── Build a three-page A4 PDF the way the editors do ─────────────────────
    {
        QPdfWriter w(out);
        w.setPageSize(QPageSize(QPageSize::A4));
        w.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);
        w.setResolution(300);
        QPainter p(&w);
        for (int i = 0; i < 3; ++i) {
            if (i > 0) w.newPage();
            p.drawText(QRectF(0, 0, w.width(), 400), Qt::AlignCenter,
                       QStringLiteral("Page %1").arg(i + 1));
        }
    }
    check(QFileInfo::exists(out), "source PDF written");

    // ── Stamp ────────────────────────────────────────────────────────────────
    const bool stamped = NativeOffice::Watermark::stampFile(out);
    check(stamped, "stampFile() reported success");

    // ── Inspect ──────────────────────────────────────────────────────────────
    FPDF_InitLibrary();
    FPDF_DOCUMENT doc = FPDF_LoadDocument(out.toUtf8().constData(), nullptr);
    check(doc != nullptr, "stamped PDF reopens");
    if (!doc) { FPDF_DestroyLibrary(); return 1; }

    check(FPDF_GetPageCount(doc) == 3, "still three pages after stamping");

    const QSizeF sz  = NativeOffice::Watermark::sizePoints();
    const qreal  mar = NativeOffice::Watermark::marginPoints();

    for (int i = 0; i < FPDF_GetPageCount(doc); ++i) {
        FPDF_PAGE page = FPDF_LoadPage(doc, i);
        if (!page) { check(false, QStringLiteral("page %1 loads").arg(i)); continue; }

        const double pw = FPDF_GetPageWidth(page);
        const double ph = FPDF_GetPageHeight(page);

        // Expected artwork box, bottom-right inset by the margin.
        const double x0 = pw - mar - sz.width();
        const double y0 = mar;
        const double cx = x0 + sz.width() / 2.0;
        const double cy = y0 + sz.height() / 2.0;

        // A click in the middle of the mark must hit the link.
        FPDF_LINK hit = FPDFLink_GetLinkAtPoint(page, cx, cy);
        check(hit != nullptr,
              QStringLiteral("page %1: click on the mark hits a link").arg(i + 1));

        if (hit) {
            const QString uri = uriOf(doc, hit);
            check(uri.startsWith(NativeOffice::Watermark::targetUrl()),
                  QStringLiteral("page %1: link points at %2 (got \"%3\")")
                      .arg(i + 1).arg(NativeOffice::Watermark::targetUrl(), uri));

            FS_RECTF r;
            if (FPDFLink_GetAnnotRect(hit, &r)) {
                const double w = r.right - r.left;
                const double h = r.top - r.bottom;
                // Tight: the hotspot must match the artwork, not a padded box.
                check(qAbs(w - sz.width()) < 1.0 && qAbs(h - sz.height()) < 1.0,
                      QStringLiteral("page %1: hotspot is %2x%3 pt, artwork is %4x%5 pt")
                          .arg(i + 1).arg(w, 0, 'f', 1).arg(h, 0, 'f', 1)
                          .arg(sz.width(), 0, 'f', 1).arg(sz.height(), 0, 'f', 1));
            } else {
                check(false, QStringLiteral("page %1: hotspot rect readable").arg(i + 1));
            }
        }

        // ── Nuisance-click checks: just outside the mark must hit nothing ────
        struct Probe { const char* name; double x, y; };
        const Probe probes[] = {
            { "8pt left of the mark",   x0 - 8,             cy },
            { "8pt above the mark",     cx,                 y0 + sz.height() + 8 },
            { "8pt below the mark",     cx,                 y0 - 8 },
            { "page bottom-right nook", pw - 4,             4 },
            { "page centre",            pw / 2,             ph / 2 },
            { "bottom-left corner",     4,                  4 },
        };
        for (const Probe& p : probes) {
            const bool clear = FPDFLink_GetLinkAtPoint(page, p.x, p.y) == nullptr;
            check(clear, QStringLiteral("page %1: %2 does not trigger the link")
                             .arg(i + 1).arg(QString::fromLatin1(p.name)));
        }

        FPDF_ClosePage(page);
    }

    FPDF_CloseDocument(doc);
    FPDF_DestroyLibrary();

    std::printf("\n%s (%d failure(s))\nfile: %s\n",
                failures ? "FAILED" : "PASSED", failures, qPrintable(out));
    return failures ? 1 : 0;
}
