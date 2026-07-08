// Temporary smoke test: writes a PDF with QPdfWriter, re-renders it through
// the vendored PDFium via Pdf::Renderer, and verifies real content came back.
// Also exercises the decoration engine ("--decor") end to end.
#include "PdfAnnots.h"
#include "PdfConvert.h"
#include "PdfCrypto.h"
#include "PdfDecor.h"
#include "PdfForms.h"
#include "PdfOcr.h"
#include "PdfRenderer.h"
#include "PdfSign.h"

#include <QtCore/private/qzipreader_p.h>

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

    // "--makeform out.pdf": emit a one-page PDF with two AcroForm text fields.
    if (argc == 3 && QByteArray(argv[1]) == "--makeform") {
        // Hand-write a minimal form PDF (QPdfWriter can't emit AcroForms).
        const QString path = QString::fromLocal8Bit(argv[2]);
        QByteArray b;
        auto add = [&](const QByteArray& s) { b += s; };
        std::vector<int> off;
        auto obj = [&](int n, const QByteArray& body) {
            while (int(off.size()) < n + 1) off.push_back(0);
            off[n] = b.size();
            add(QByteArray::number(n) + " 0 obj\n" + body + "\nendobj\n");
        };
        add("%PDF-1.7\n");
        obj(1, "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [4 0 R 5 0 R] /NeedAppearances true >> >>");
        obj(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
        obj(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Annots [4 0 R 5 0 R] "
               "/Resources << /Font << /Helv 6 0 R >> >> >>");
        obj(4, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /Rect [100 700 400 720] "
               "/DA (/Helv 12 Tf 0 g) /P 3 0 R >>");
        obj(5, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Email) /Rect [100 660 400 680] "
               "/DA (/Helv 12 Tf 0 g) /P 3 0 R >>");
        obj(6, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
        const int xrefPos = b.size();
        add("xref\n0 7\n0000000000 65535 f \n");
        for (int i = 1; i <= 6; ++i) {
            QByteArray line = QByteArray::number(off[i]).rightJustified(10, '0');
            add(line + " 00000 n \n");
        }
        add("trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n"
            + QByteArray::number(xrefPos) + "\n%%EOF");
        QFile f(path); f.open(QIODevice::WriteOnly); f.write(b); f.close();
        std::printf("wrote form pdf\n");
        return 0;
    }

    // "--fillform in.pdf": detect + fill the form fields.
    if (argc == 3 && QByteArray(argv[1]) == "--fillform") {
        using namespace NativeOffice::Pdf;
        const QString in = QString::fromLocal8Bit(argv[2]);
        auto fields = detectFormFields(in);
        std::printf("detected %zu fields\n", fields.size());
        for (const auto& f : fields)
            std::printf("  field '%s' type=%d page=%d\n",
                        qPrintable(f.fullName), int(f.type), f.pageIndex);

        std::map<QString, QString> vals;
        vals["Name"]  = "Ada Lovelace";
        vals["Email"] = "ada@example.com";
        const QString out = QDir::tempPath() + "/form_filled.pdf";
        OpResult r = fillTextFields(in, out, vals);
        if (!r.ok) { std::printf("FAIL fill: %s\n", qPrintable(r.message)); return 1; }

        RenderOpenStatus st{};
        auto rend = Renderer::open(out, {}, st);
        // Widget appearance text isn't part of page text extraction, so check
        // that the field region actually drew ink (non-white pixels) instead.
        int fieldInk = 0;
        if (rend) {
            const QImage img = rend->renderPage(0, 1.5);
            img.save(QDir::tempPath() + "/form_filled.png");
            // "Name" field is at PDF [100 700 400 720] on a 612x792 page.
            const double sc = 1.5;
            const QRect region(int(100 * sc), int((792 - 720) * sc),
                               int(300 * sc), int(20 * sc));
            for (int y = region.top(); y < region.bottom() && y < img.height(); ++y)
                for (int x = region.left(); x < region.right() && x < img.width(); ++x)
                    if (img.pixel(x, y) != 0xFFFFFFFF) ++fieldInk;
        }
        auto after = detectFormFields(out);
        const bool valOk = !after.empty() && after[0].value == "Ada Lovelace";
        std::printf("field-ink=%d value-ok=%d\n", fieldInk, int(valOk));
        const bool pass = fields.size() == 2 && fieldInk > 30 && valOk;
        std::printf(pass ? "PASS\n" : "FAIL\n");
        return pass ? 0 : 1;
    }

    // "--encrypt in.pdf": AES-256 encrypt then verify open with the password.
    if (argc == 3 && QByteArray(argv[1]) == "--encrypt") {
        using namespace NativeOffice::Pdf;
        const QString in = QString::fromLocal8Bit(argv[2]);
        const QString out = QDir::tempPath() + "/enc_out.pdf";

        EncryptOptions opts;
        opts.userPassword = "open-sesame";
        opts.ownerPassword = "owner-key-42";
        opts.allowPrinting = false;    // restrict printing
        opts.allowCopying = false;     // restrict copying

        OpResult r = encryptDocument(in, out, opts);
        if (!r.ok) { std::printf("FAIL encrypt: %s\n", qPrintable(r.message)); return 1; }

        // Wrong password must fail.
        RenderOpenStatus st{};
        auto wrong = Renderer::open(out, "nope", st);
        const bool wrongRejected = !wrong && st == RenderOpenStatus::PasswordRequired;

        // No password must be reported as password-required.
        RenderOpenStatus st2{};
        auto noPwd = Renderer::open(out, "", st2);
        const bool promptsForPwd = !noPwd && st2 == RenderOpenStatus::PasswordRequired;

        // Correct user password must open + render + extract text.
        RenderOpenStatus st3{};
        auto okDoc = Renderer::open(out, "open-sesame", st3);
        bool opensOk = false, textOk = false;
        if (okDoc) {
            opensOk = okDoc->pageCount() > 0;
            textOk = okDoc->pageText(0).contains("Sample");
            okDoc->renderPage(0, 1.0).save(QDir::tempPath() + "/enc_decrypted.png");
        }
        // Owner password must also open.
        RenderOpenStatus st4{};
        auto ownerDoc = Renderer::open(out, "owner-key-42", st4);
        const bool ownerOpens = ownerDoc && ownerDoc->pageCount() > 0;

        std::printf("wrongRejected=%d promptsForPwd=%d opensOk=%d textOk=%d ownerOpens=%d\n",
                    int(wrongRejected), int(promptsForPwd), int(opensOk), int(textOk), int(ownerOpens));
        const bool pass = wrongRejected && promptsForPwd && opensOk && textOk && ownerOpens;
        std::printf(pass ? "PASS\n" : "FAIL\n");
        return pass ? 0 : 1;
    }

    // "--certs": list certificates.
    if (argc == 2 && QByteArray(argv[1]) == "--certs") {
        using namespace NativeOffice::Pdf;
        for (const auto& c : listCertificates())
            std::printf("  %s | key=%d | %s\n", qPrintable(c.subject),
                        int(c.hasPrivateKey), qPrintable(c.thumbprintHex));
        return 0;
    }

    // "--sign in.pdf THUMBPRINT": sign then validate.
    if (argc == 4 && QByteArray(argv[1]) == "--sign") {
        using namespace NativeOffice::Pdf;
        const QString in = QString::fromLocal8Bit(argv[2]);
        const QString out = QDir::tempPath() + "/signed_out.pdf";
        SignOptions opts;
        opts.thumbprintHex = QString::fromLatin1(argv[3]);
        opts.reason = "I approve this document";
        opts.location = "Test Lab";
        OpResult r = signPdf(in, out, opts);
        if (!r.ok) { std::printf("FAIL sign: %s\n", qPrintable(r.message)); return 1; }

        // The signed file must still open and render.
        RenderOpenStatus st{};
        auto rend = Renderer::open(out, {}, st);
        const bool opensOk = rend && rend->pageCount() > 0;

        const auto results = validateSignatures(out);
        bool digestOk = false;
        for (const auto& s : results) {
            std::printf("  sig: digestValid=%d certTrusted=%d signer='%s' — %s\n",
                        int(s.digestValid), int(s.certTrusted),
                        qPrintable(s.signerName), qPrintable(s.summary));
            if (s.digestValid) digestOk = true;
        }
        // Tamper test: flip a byte and re-validate — digest must fail.
        QFile tf(out);
        tf.open(QIODevice::ReadOnly); QByteArray bytes = tf.readAll(); tf.close();
        bytes[bytes.size() / 2] = bytes[bytes.size() / 2] ^ 0xFF;
        const QString tampered = QDir::tempPath() + "/signed_tampered.pdf";
        QFile wf(tampered); wf.open(QIODevice::WriteOnly); wf.write(bytes); wf.close();
        bool tamperCaught = true;
        for (const auto& s : validateSignatures(tampered))
            if (s.digestValid) tamperCaught = false;

        std::printf("opensOk=%d digestOk=%d tamperCaught=%d numSigs=%zu\n",
                    int(opensOk), int(digestOk), int(tamperCaught), results.size());
        const bool pass = opensOk && digestOk && tamperCaught && results.size() == 1;
        std::printf(pass ? "PASS\n" : "FAIL\n");
        return pass ? 0 : 1;
    }

    // "--convert in.pdf": run all converters and validate outputs.
    if (argc == 3 && QByteArray(argv[1]) == "--convert") {
        using namespace NativeOffice::Pdf;
        const QString in = QString::fromLocal8Bit(argv[2]);
        const QString t = QDir::tempPath();
        auto zipHas = [](const QString& path, const QStringList& parts) {
            QZipReader z(path);
            if (z.status() != QZipReader::NoError) return false;
            QStringList names;
            for (const auto& e : z.fileInfoList()) names << e.filePath;
            for (const QString& p : parts) if (!names.contains(p)) return false;
            return true;
        };

        OpResult r = toTxt(in, t + "/c.txt");
        const bool txtOk = r.ok && QFile(t + "/c.txt").size() > 20;

        r = toDocx(in, t + "/c.docx");
        const bool docxOk = r.ok && zipHas(t + "/c.docx",
            { "[Content_Types].xml", "_rels/.rels", "word/document.xml" });

        r = toXlsx(in, t + "/c.xlsx");
        const bool xlsxOk = r.ok && zipHas(t + "/c.xlsx",
            { "[Content_Types].xml", "xl/workbook.xml", "xl/worksheets/sheet1.xml" });

        r = toPptx(in, t + "/c.pptx", 120);
        const bool pptxOk = r.ok && zipHas(t + "/c.pptx",
            { "[Content_Types].xml", "ppt/presentation.xml", "ppt/slides/slide1.xml", "ppt/media/image1.png" });

        QStringList imgs;
        r = toImages(in, t + "/imgs", "page", RasterFormat::Png, 100, &imgs);
        const bool imgOk = r.ok && !imgs.isEmpty() && QFile(imgs.first()).size() > 100;

        r = toImageOnlyPdf(in, t + "/c_imgonly.pdf", 100);
        RenderOpenStatus st{};
        auto ro = r.ok ? Renderer::open(t + "/c_imgonly.pdf", {}, st) : nullptr;
        const bool imgPdfOk = ro && ro->pageCount() > 0 && ro->pageText(0).isEmpty();

        r = imagesToPdf(imgs, t + "/c_frompics.pdf");
        auto rp = r.ok ? Renderer::open(t + "/c_frompics.pdf", {}, st) : nullptr;
        const bool picPdfOk = rp && rp->pageCount() == imgs.size();

        std::printf("txt=%d docx=%d xlsx=%d pptx=%d imgs=%d imgOnlyPdf=%d picToPdf=%d\n",
                    int(txtOk), int(docxOk), int(xlsxOk), int(pptxOk),
                    int(imgOk), int(imgPdfOk), int(picPdfOk));
        const bool pass = txtOk && docxOk && xlsxOk && pptxOk && imgOk && imgPdfOk && picPdfOk;
        std::printf(pass ? "PASS\n" : "FAIL\n");
        return pass ? 0 : 1;
    }

    // "--ocr in.pdf": rasterize to an image-only PDF, OCR it, check the
    // result has a searchable text layer.
    if (argc == 3 && QByteArray(argv[1]) == "--ocr") {
        using namespace NativeOffice::Pdf;
        const QString in = QString::fromLocal8Bit(argv[2]);
        const QString t = QDir::tempPath();
        std::printf("ocrAvailable=%d languages=%s\n", int(ocrAvailable()),
                    qPrintable(ocrLanguages().join(",")));
        if (!ocrAvailable()) { std::printf("SKIP (no OCR language installed)\n"); return 0; }

        // Make a scanned-style (image-only, no text) PDF first.
        const QString scanned = t + "/ocr_scanned.pdf";
        OpResult r = toImageOnlyPdf(in, scanned, 200);
        if (!r.ok) { std::printf("FAIL rasterize: %s\n", qPrintable(r.message)); return 1; }

        RenderOpenStatus st{};
        auto before = Renderer::open(scanned, {}, st);
        const bool noTextBefore = before && before->pageText(0).trimmed().isEmpty();

        const QString out = t + "/ocr_out.pdf";
        r = ocrPdf(scanned, out, {}, 300);
        if (!r.ok) { std::printf("FAIL ocr: %s\n", qPrintable(r.message)); return 1; }

        auto after = Renderer::open(out, {}, st);
        const QString text = after ? after->pageText(0) : QString();
        const bool hasText = text.contains("Sample", Qt::CaseInsensitive)
                          || text.contains("quick", Qt::CaseInsensitive);
        std::printf("noTextBefore=%d layerAdded=%d recognized='%s'\n",
                    int(noTextBefore), int(hasOcrLayer(out)),
                    qPrintable(text.simplified().left(60)));
        const bool pass = noTextBefore && hasOcrLayer(out) && hasText;
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
