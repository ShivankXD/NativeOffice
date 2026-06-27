#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// DocxIo.h  (Sprint 19 — Microsoft Word .docx import / export)
// Reads and writes real OOXML .docx packages (a ZIP of XML parts) against a
// QTextDocument. Covers the common content: paragraphs (alignment, spacing,
// indent), runs (bold/italic/underline/strike/colour/size/font/super-sub/
// highlight), tables (borders, shading, merged cells), inline images and
// bullet/numbered lists. Uses Qt's private QZipReader/QZipWriter for the ZIP.
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>

class QTextDocument;

namespace NativeOffice {

class DocxIo {
public:
    // Write the document to a .docx file. Returns true on success.
    static bool exportDocx(QTextDocument* doc, const QString& path);

    // Read a .docx file into the document (replacing its contents).
    static bool importDocx(const QString& path, QTextDocument* doc);
};

} // namespace NativeOffice
