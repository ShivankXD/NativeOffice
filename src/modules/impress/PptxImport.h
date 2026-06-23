#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PptxImport.h
// Best-effort PowerPoint (.pptx) reader: opens a real .pptx package (a ZIP of
// OOXML parts, usually DEFLATE-compressed) and converts each slide into a
// NativeOffice SlideData. Self-contained — bundles its own raw-DEFLATE inflater
// and ZIP reader so no external dependency is required.
//
// Imported per slide: solid background, text boxes (position, text, size, bold,
// colour, alignment), preset shapes (geometry + fill) and embedded images.
// Not imported: charts, tables, SmartArt, animations, transitions, masters.
// ─────────────────────────────────────────────────────────────────────────────

#include "SlideData.h"

#include <QString>
#include <vector>

namespace NativeOffice {

// Parse the .pptx at `path` into `outSlides`. Returns true if the package was a
// readable ZIP containing at least one slide. On failure `outSlides` is cleared.
bool importPptx(const QString& path, std::vector<SlideData>& outSlides);

} // namespace NativeOffice
