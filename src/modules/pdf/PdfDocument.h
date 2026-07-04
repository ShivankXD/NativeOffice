#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfDocument.h — hand-rolled PDF reader (no external PDF/rendering library).
//
// Supports the two cross-reference formats real-world PDFs actually use:
//   • classic plain-text xref tables ("xref" / "trailer")
//   • cross-reference STREAMS (PDF 1.5+, /Type /XRef) and OBJECT STREAMS
//     (/Type /ObjStm) — what recent Chrome "Print to PDF", modern LibreOffice,
//     and most scanners emit.
// /Prev chains (incrementally-updated files) are followed and merged.
//
// Deliberately does NOT decode page content/image streams — those are only
// ever copied through byte-for-byte by PdfWriter. The only streams actually
// decompressed here are xref streams and object streams, since their content
// must be read to locate other objects.
//
// Fails closed: encrypted, unrecognized, or structurally broken PDFs are
// rejected with a specific OpenStatus rather than partially/incorrectly
// parsed. There is no byte-scanning "rebuild from a damaged file" fallback —
// that's a deliberate non-goal for this pass (see PdfDocument.cpp).
// ─────────────────────────────────────────────────────────────────────────────

#include "PdfObject.h"

#include <QByteArray>
#include <QString>
#include <map>
#include <memory>
#include <vector>

namespace NativeOffice::Pdf {

enum class OpenStatus {
    Ok,
    FileNotFound,
    NotAPdf,
    Encrypted,
    MalformedXref,
    MalformedTrailer,
    MalformedPageTree,
    IoError,
};

// Human-readable reason shown in the UI, e.g. prefixed with
// "This PDF isn't supported yet: ".
QString openStatusReason(OpenStatus status);

// A page, with inherited /Resources and /MediaBox already resolved and baked
// into `dict` (both are inheritable attributes in the PDF page tree — a leaf
// page without its own copy uses its nearest ancestor's).
struct PageInfo {
    Ref ref;          // this page object's reference in the SOURCE document
    Object dict;       // fully-resolved page dictionary (Type/Page)
};

class Document {
public:
    // Parses and validates `path`. Returns nullptr on failure with `status`
    // set to the specific reason (see openStatusReason()).
    static std::unique_ptr<Document> open(const QString& path, OpenStatus& status);

    [[nodiscard]] int pageCount() const { return int(m_pages.size()); }
    [[nodiscard]] const std::vector<PageInfo>& pages() const { return m_pages; }

    // Resolves a reference to its object; returns a null Object if not found.
    // Direct (non-Stream) objects are cached after first parse.
    [[nodiscard]] const Object& resolve(Ref r) const;
    // If `v` is a Ref, resolves it; otherwise returns `v` unchanged.
    [[nodiscard]] const Object& resolve(const Object& v) const;

private:
    Document() = default;

    bool load(const QString& path, OpenStatus& status);
    bool parseXrefChain(qint64 startOffset, OpenStatus& status);
    bool parseClassicXrefSection(qint64 offset, OpenStatus& status, qint64& prevOffset, bool& hasPrev);
    bool parseXrefStreamSection(qint64 offset, OpenStatus& status, qint64& prevOffset, bool& hasPrev);
    bool buildPageList(OpenStatus& status);
    void walkPageTree(Ref node, Object inheritedRes, Object inheritedBox,
                       std::vector<Ref>& visiting, OpenStatus& status, bool& failed);

    const Object& fetchObject(Ref r) const;
    Object parseObjectAtOffset(qint64 offset) const;
    Object parseIndirectFromObjStream(int streamObjNum, int indexInStream) const;

    QByteArray m_data;                       // whole file, loaded once

    // objNum -> either a byte offset (direct) or (streamObjNum, index) for
    // compressed objects. type 0 = free, 1 = direct offset, 2 = in-stream.
    struct XEntry { int type = 0; long long a = 0; long long b = 0; };
    std::map<int, XEntry> m_xref;

    Object m_trailer;
    std::vector<PageInfo> m_pages;

    mutable std::map<int, Object> m_objCache;
    mutable std::map<int, Object> m_objStmCache;   // cache of decoded object-stream member objects
};

} // namespace NativeOffice::Pdf
