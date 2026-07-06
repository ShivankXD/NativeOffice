// Temporary smoke test: writes a PDF with QPdfWriter, re-renders it through
// the vendored PDFium via Pdf::Renderer, and verifies real content came back.
// Also exercises the decoration engine ("--decor") end to end.
#include "PdfAnnots.h"
#include "PdfDecor.h"
#include "PdfRenderer.h"

#include <QGuiApplication>
#include <QPdfWriter>
#include <QPainter>
#include <QImage>
#include <QDir>

#include <cstdio>

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    // "--make N out.pdf": emit an N-page sample document (for UI testing).
    if (argc == 4 && QByteArray(argv[1]) == "--make") {
        const int n = QByteArray(argv[2]).toInt();
        QPdfWriter w(QString::fromLocal8Bit(argv[3]));
        w.setPageSize(QPageSize(QPageSize::A4));
        QPainter p(&w);
        for (int i = 0; i < n; ++i) {
            if (i > 0) w.newPage();
            p.setPen(Qt::black);
            p.setFont(QFont("Arial", 28));
            p.drawText(400, 700, QString("Sample document — page %1 of %2").arg(i + 1).arg(n));
            p.setFont(QFont("Arial", 12));
            for (int line = 0; line < 18; ++line)
                p.drawText(400, 1100 + line * 260,
                           QString("Line %1: the quick brown fox jumps over the lazy dog.").arg(line + 1));
            p.setPen(QPen(QColor::fromHsv((i * 60) % 360, 200, 200), 12));
            p.drawRect(400, 6200, 2200, 1400);
        }
        p.end();
        std::printf("wrote %d pages\n", n);
        return 0;
    }

    // "--decor in.pdf": watermark + page numbers round-trip test.
    if (argc == 3 && QByteArray(argv[1]) == "--decor") {
        using namespace NativeOffice::Pdf;
        const QString in = QString::fromLocal8Bit(argv[2]);
        const QString withWm = QDir::tempPath() + "/decor_wm.pdf";
        const QString withPn = QDir::tempPath() + "/decor_pn.pdf";
        const QString removed = QDir::tempPath() + "/decor_rm.pdf";

        TextWatermarkSpec wm;
        OpResult r = addTextWatermark(in, withWm, wm);
        if (!r.ok) { std::printf("FAIL watermark: %s\n", qPrintable(r.message)); return 1; }

        PageNumberSpec pn;
        pn.format = PageNumberSpec::Format::PageOfTotal;
        r = addPageNumbers(withWm, withPn, pn);
        if (!r.ok) { std::printf("FAIL pagenum: %s\n", qPrintable(r.message)); return 1; }

        RenderOpenStatus st{};
        auto rend = Renderer::open(withPn, {}, st);
        if (!rend) { std::printf("FAIL reopen: %d\n", int(st)); return 1; }
        const QString text = rend->pageText(0);
        const bool hasWm = text.contains("CONFIDENTIAL");
        const bool hasPn = text.contains("Page 1 of");
        const QImage img = rend->renderPage(0, 1.0);
        img.save(QDir::tempPath() + "/decor_page1.png");
        std::printf("decorated text: wm=%d pn=%d hasDecorWm=%d hasDecorPn=%d\n",
                    int(hasWm), int(hasPn),
                    int(hasDecor(withPn, DecorKind::Watermark)),
                    int(hasDecor(withPn, DecorKind::PageNumber)));

        r = removeDecor(withPn, removed, DecorKind::Watermark);
        if (!r.ok) { std::printf("FAIL remove: %s\n", qPrintable(r.message)); return 1; }
        auto rend2 = Renderer::open(removed, {}, st);
        const bool wmGone = rend2 && !rend2->pageText(0).contains("CONFIDENTIAL");
        const bool pnKept = rend2 && rend2->pageText(0).contains("Page 1 of");
        std::printf("after remove: wmGone=%d pnKept=%d\n", int(wmGone), int(pnKept));
        std::printf(hasWm && hasPn && wmGone && pnKept ? "PASS\n" : "FAIL\n");
        return hasWm && hasPn && wmGone && pnKept ? 0 : 1;
    }

    // "--annot in.pdf": annotation + XFDF round-trip test.
    if (argc == 3 && QByteArray(argv[1]) == "--annot") {
        using namespace NativeOffice::Pdf;
        const QString in = QString::fromLocal8Bit(argv[2]);
        const QString out = QDir::tempPath() + "/annot_out.pdf";
        const QString xfdf = QDir::tempPath() + "/annot_out.xfdf";
        const QString reimport = QDir::tempPath() + "/annot_reimport.pdf";

        std::vector<AnnotSpec> specs;
        { AnnotSpec s; s.kind = AnnotSpec::Kind::Highlight; s.pageIndex = 0;
          s.rect = QRectF(60, 120, 300, 20); s.quads = { s.rect };
          s.color = QColor("#FFD400"); specs.push_back(s); }
        { AnnotSpec s; s.kind = AnnotSpec::Kind::Square; s.pageIndex = 0;
          s.rect = QRectF(60, 200, 200, 120); s.color = QColor("#E8372A"); specs.push_back(s); }
        { AnnotSpec s; s.kind = AnnotSpec::Kind::FreeText; s.pageIndex = 0;
          s.rect = QRectF(300, 200, 200, 80); s.contents = "Review comment here";
          s.color = QColor("#1C1E26"); specs.push_back(s); }
        { AnnotSpec s; s.kind = AnnotSpec::Kind::Note; s.pageIndex = 0;
          s.rect = QRectF(520, 120, 20, 20); s.contents = "Sticky note"; specs.push_back(s); }

        OpResult r = addAnnotations(in, out, specs);
        if (!r.ok) { std::printf("FAIL add: %s\n", qPrintable(r.message)); return 1; }

        auto annots = listAnnotations(out);
        std::printf("listed %zu annotations\n", annots.size());

        RenderOpenStatus st{};
        auto rend = Renderer::open(out, {}, st);
        if (rend) rend->renderPage(0, 1.5).save(QDir::tempPath() + "/annot_page1.png");

        r = exportXfdf(out, xfdf);
        if (!r.ok) { std::printf("FAIL export: %s\n", qPrintable(r.message)); return 1; }

        r = importXfdf(in, xfdf, reimport);
        if (!r.ok) { std::printf("FAIL import: %s\n", qPrintable(r.message)); return 1; }
        auto reimported = listAnnotations(reimport);
        std::printf("re-imported %zu annotations from XFDF\n", reimported.size());

        // remove first annotation
        r = removeAnnotation(out, QDir::tempPath() + "/annot_less.pdf", 0, 0);
        auto after = r.ok ? listAnnotations(QDir::tempPath() + "/annot_less.pdf") : std::vector<AnnotInfo>{};
        std::printf("after remove one: %zu (was %zu)\n", after.size(), annots.size());

        const bool pass = annots.size() == 4 && reimported.size() >= 3 && after.size() == 3;
        std::printf(pass ? "PASS\n" : "FAIL\n");
        return pass ? 0 : 1;
    }

    const QString dir = QDir::tempPath();
    const QString pdfPath = dir + "/pdfium_smoke.pdf";
    const QString pngPath = dir + "/pdfium_smoke.png";

    {
        QPdfWriter w(pdfPath);
        w.setPageSize(QPageSize(QPageSize::A4));
        QPainter p(&w);
        p.setPen(Qt::red);
        p.setFont(QFont("Arial", 32));
        p.drawText(400, 800, "PDFium smoke test");
        p.fillRect(400, 1200, 2000, 600, Qt::blue);
        p.end();
    }

    using namespace NativeOffice::Pdf;
    RenderOpenStatus st{};
    auto r = Renderer::open(pdfPath, {}, st);
    if (!r) { std::printf("FAIL: open status=%d\n", int(st)); return 1; }

    std::printf("pages=%d size=%.1fx%.1fpt\n", r->pageCount(),
                r->pageSizePt(0).width(), r->pageSizePt(0).height());

    const QImage img = r->renderPage(0, 2.0);
    if (img.isNull()) { std::printf("FAIL: null image\n"); return 1; }
    img.save(pngPath);

    int nonWhite = 0;
    for (int y = 0; y < img.height(); y += 4)
        for (int x = 0; x < img.width(); x += 4)
            if (img.pixel(x, y) != 0xFFFFFFFF) ++nonWhite;

    const QString text = r->pageText(0);
    std::printf("rendered %dx%d nonWhiteSamples=%d text=\"%s\"\n",
                img.width(), img.height(), nonWhite, qPrintable(text.left(40)));
    std::printf(nonWhite > 100 && text.contains("smoke") ? "PASS\n" : "FAIL\n");
    return nonWhite > 100 && text.contains("smoke") ? 0 : 1;
}
