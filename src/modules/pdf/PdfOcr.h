#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfOcr.h — "OCR PDF": makes a scanned/image PDF searchable by recognizing
// text on each page and overlaying an invisible text layer at the recognized
// positions (see PdfDecor::addInvisibleTextLayer).
//
// The recognition engine is Windows.Media.Ocr (WinRT) — built into Windows
// 10/11, so it adds ZERO binary size and uses the user's own installed
// language packs. Pages are rasterized via PDFium, recognized, and the word
// boxes mapped back to page points.
// ─────────────────────────────────────────────────────────────────────────────

#include "PdfOps.h"

#include <QString>
#include <QStringList>
#include <functional>

namespace NativeOffice::Pdf {

// True when a Windows OCR engine is available for at least one language.
bool ocrAvailable();

// BCP-47 tags of installed OCR languages (e.g. "en-US"), for the UI picker.
QStringList ocrLanguages();

struct OcrProgress {
    int page = 0;
    int total = 0;
};

// Recognizes every page of `in` and writes `out` with an added invisible text
// layer. `languageTag` empty = the system default OCR language. `renderDpi`
// controls recognition quality (200–300 is a good range). `onProgress` is
// optional. Fails cleanly if no OCR engine/language is available.
OpResult ocrPdf(const QString& in, const QString& out, const QString& languageTag,
                int renderDpi, const std::function<void(OcrProgress)>& onProgress = {});

} // namespace NativeOffice::Pdf
