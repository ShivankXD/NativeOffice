// ─────────────────────────────────────────────────────────────────────────────
// PdfOcr.cpp — see PdfOcr.h. Recognition via Windows.Media.Ocr (WinRT).
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfOcr.h"
#include "PdfDecor.h"
#include "PdfRenderer.h"

#include <QImage>

#include <algorithm>
#include <map>
#include <thread>
#include <vector>

// The WinRT OCR path is only compiled when the Windows SDK's C++/WinRT
// projection headers are available. Otherwise the feature reports itself as
// unavailable and fails cleanly (keeping non-Windows / minimal-SDK builds
// green).
#if defined(_WIN32) && __has_include(<winrt/Windows.Media.Ocr.h>)
#  define NATIVEOFFICE_HAVE_WINRT_OCR 1
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winrt/base.h>
#  include <winrt/Windows.Foundation.h>
#  include <winrt/Windows.Foundation.Collections.h>
#  include <winrt/Windows.Globalization.h>
#  include <winrt/Windows.Graphics.Imaging.h>
#  include <winrt/Windows.Media.Ocr.h>
#  include <winrt/Windows.Security.Cryptography.h>
#  include <winrt/Windows.Storage.Streams.h>
#else
#  define NATIVEOFFICE_HAVE_WINRT_OCR 0
#endif

namespace NativeOffice::Pdf {

#if NATIVEOFFICE_HAVE_WINRT_OCR

namespace {
using namespace winrt;
using namespace winrt::Windows::Media::Ocr;
using namespace winrt::Windows::Globalization;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Security::Cryptography;
using namespace winrt::Windows::Storage::Streams;

// A tightly-packed BGRA QImage → SoftwareBitmap (Bgra8) for the OCR engine.
SoftwareBitmap toSoftwareBitmap(const QImage& srcIn) {
    QImage src = srcIn.convertToFormat(QImage::Format_ARGB32);   // BGRA in memory (LE)
    const int w = src.width(), h = src.height();
    QByteArray packed;
    packed.reserve(w * h * 4);
    for (int y = 0; y < h; ++y)
        packed.append(reinterpret_cast<const char*>(src.constScanLine(y)), w * 4);

    array_view<uint8_t const> view(reinterpret_cast<const uint8_t*>(packed.constData()),
                                   reinterpret_cast<const uint8_t*>(packed.constData()) + packed.size());
    IBuffer buffer = CryptographicBuffer::CreateFromByteArray(view);
    return SoftwareBitmap::CreateCopyFromBuffer(buffer, BitmapPixelFormat::Bgra8, w, h,
                                                BitmapAlphaMode::Premultiplied);
}

OcrEngine createEngine(const QString& languageTag) {
    if (!languageTag.isEmpty()) {
        Language lang(winrt::hstring(languageTag.toStdWString()));
        if (OcrEngine::IsLanguageSupported(lang))
            if (OcrEngine engine = OcrEngine::TryCreateFromLanguage(lang))
                return engine;
    }
    return OcrEngine::TryCreateFromUserProfileLanguages();
}

} // namespace

bool ocrAvailable() {
    try {
        init_apartment(apartment_type::multi_threaded);
    } catch (...) { /* already initialized on this thread — fine */ }
    try {
        return OcrEngine::AvailableRecognizerLanguages().Size() > 0;
    } catch (...) {
        return false;
    }
}

QStringList ocrLanguages() {
    QStringList out;
    try {
        for (const auto& lang : OcrEngine::AvailableRecognizerLanguages())
            out << QString::fromWCharArray(lang.LanguageTag().c_str());
    } catch (...) {}
    return out;
}

OpResult ocrPdf(const QString& in, const QString& out, const QString& languageTag,
                int renderDpi, const std::function<void(OcrProgress)>& onProgress) {
    std::map<int, std::vector<OcrWord>> wordsByPage;
    std::string error;
    const double scale = std::clamp(renderDpi, 72, 600) / 72.0;

    // WinRT wants its own apartment; run the recognition (and the PDFium
    // rasterization it drives) on a dedicated MTA thread so it never fights
    // Qt's GUI STA.
    std::thread worker([&] {
        try {
            init_apartment(apartment_type::multi_threaded);
        } catch (...) {}
        try {
            OcrEngine engine = createEngine(languageTag);
            if (!engine) { error = "No OCR language is installed on this system."; return; }

            RenderOpenStatus st{};
            auto renderer = Renderer::open(in, {}, st);
            if (!renderer) { error = "The PDF could not be read for OCR."; return; }

            const int n = renderer->pageCount();
            for (int p = 0; p < n; ++p) {
                const QImage img = renderer->renderPage(p, scale);
                if (img.isNull()) continue;
                SoftwareBitmap bmp = toSoftwareBitmap(img);
                OcrResult result = engine.RecognizeAsync(bmp).get();

                std::vector<OcrWord> words;
                for (const auto& line : result.Lines()) {
                    for (const auto& word : line.Words()) {
                        const auto rect = word.BoundingRect();     // bitmap pixels
                        OcrWord ow;
                        ow.text = QString::fromWCharArray(word.Text().c_str());
                        ow.box = QRectF(rect.X / scale, rect.Y / scale,
                                        rect.Width / scale, rect.Height / scale);
                        words.push_back(std::move(ow));
                    }
                }
                if (!words.empty()) wordsByPage[p] = std::move(words);
                if (onProgress) onProgress({ p + 1, n });
            }
        } catch (const hresult_error& e) {
            error = winrt::to_string(e.message());
        } catch (...) {
            error = "OCR failed unexpectedly.";
        }
    });
    worker.join();

    if (!error.empty()) return { false, QString::fromStdString(error) };
    if (wordsByPage.empty())
        return { false, "No text could be recognized. Try a higher resolution or a different language." };

    return addInvisibleTextLayer(in, out, wordsByPage);
}

#else  // no WinRT OCR

bool ocrAvailable() { return false; }
QStringList ocrLanguages() { return {}; }
OpResult ocrPdf(const QString&, const QString&, const QString&, int,
                const std::function<void(OcrProgress)>&) {
    return { false, "OCR requires Windows 10/11 with an installed OCR language pack." };
}

#endif

} // namespace NativeOffice::Pdf
