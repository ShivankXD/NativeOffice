// ─────────────────────────────────────────────────────────────────────────────
// PdfOps.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfOps.h"
#include "PdfDocument.h"
#include "PdfWriter.h"

#include <QFile>
#include <QFileInfo>
#include <functional>
#include <map>

namespace NativeOffice::Pdf {

namespace {

// Same Qt-qCompress-based approach as PdfDocument.cpp — see the comment
// there. qUncompress needs a synthetic 4-byte size header; qCompress
// produces one that must be stripped back off before use as a PDF stream.
bool zlibInflateAll(const QByteArray& in, QByteArray& out) {
    if (in.isEmpty()) return false;
    QByteArray withHeader;
    withHeader.reserve(in.size() + 4);
    const quint32 guess = static_cast<quint32>(in.size()) * 4 + 64;
    withHeader.append(char((guess >> 24) & 0xFF));
    withHeader.append(char((guess >> 16) & 0xFF));
    withHeader.append(char((guess >> 8) & 0xFF));
    withHeader.append(char(guess & 0xFF));
    withHeader.append(in);
    out = qUncompress(withHeader);
    return !out.isEmpty();
}

bool zlibDeflateAll(const QByteArray& in, QByteArray& out, int level) {
    const QByteArray withHeader = qCompress(in, level);
    if (withHeader.size() <= 4) return false;
    out = withHeader.mid(4);
    return true;
}

// Deep-copies objects from one source Document into a shared Writer,
// renumbering as it goes and memoizing so shared/cyclic references (e.g. a
// font used by many pages) are copied once. A NEGATIVE Ref::num is a
// sentinel meaning "this is already a destination object number" (used to
// splice in objects — like a shared Pages node — that the caller built
// directly rather than copying from the source).
class Copier {
public:
    Copier(const Document& src, Writer& dst, bool recompress)
        : m_src(src), m_dst(dst), m_recompress(recompress) {}

    int copy(Ref oldRef) {
        if (oldRef.num < 0) return -oldRef.num;   // sentinel passthrough
        auto it = m_map.find(oldRef);
        if (it != m_map.end()) return it->second;

        const int newNum = m_dst.allocate();
        m_map[oldRef] = newNum;   // register before recursing: breaks cycles

        Object working = m_src.resolve(oldRef);
        maybeRecompress(working);
        const QByteArray body = serializeObjectBody(working, [this](Ref r) { return copy(r); });
        m_dst.setObjectBody(newNum, body);
        return newNum;
    }

    // Copies an in-hand page dict (already resolved, with inheritance baked
    // in by PdfDocument) after retargeting /Parent at a new, shared Pages
    // node built by the caller.
    int copyPageWithParent(Object pageDict, int newParentNum) {
        pageDict.dict.insert("Parent", Object::makeRef(Ref{ -newParentNum, 0 }));
        const int newNum = m_dst.allocate();
        const QByteArray body = serializeObjectBody(pageDict, [this](Ref r) { return copy(r); });
        m_dst.setObjectBody(newNum, body);
        return newNum;
    }

private:
    void maybeRecompress(Object& obj) const {
        if (!m_recompress || !obj.isStream()) return;
        const Object* filter = obj.find("Filter");
        if (!filter || !filter->isName() || filter->asName() != "FlateDecode") return;
        QByteArray inflated;
        if (!zlibInflateAll(obj.streamData, inflated)) return;
        QByteArray recompressed;
        if (zlibDeflateAll(inflated, recompressed, 9) && recompressed.size() < obj.streamData.size())
            obj.streamData = recompressed;
    }

    const Document& m_src;
    Writer& m_dst;
    bool m_recompress;
    std::map<Ref, int> m_map;
};

// Builds the shared Pages node + Catalog for a set of already-copied page
// object numbers, and writes the file. Common tail for merge/split/compress.
bool writeOutputTree(Writer& writer, int pagesNum, int catalogNum,
                      const std::vector<int>& newPageNums, const QString& outputPath) {
    auto sentinelOnly = [](Ref r) { return r.num < 0 ? -r.num : 0; };

    Object pagesObj; pagesObj.type = Object::Type::Dict;
    pagesObj.dict.insert("Type", Object::makeName("Pages"));
    std::vector<Object> kids;
    kids.reserve(newPageNums.size());
    for (int n : newPageNums) kids.push_back(Object::makeRef(Ref{ -n, 0 }));
    pagesObj.dict.insert("Kids", Object::makeArray(std::move(kids)));
    pagesObj.dict.insert("Count", Object::makeInt(static_cast<long long>(newPageNums.size())));
    writer.setObjectBody(pagesNum, serializeObjectBody(pagesObj, sentinelOnly));

    Object catalogObj; catalogObj.type = Object::Type::Dict;
    catalogObj.dict.insert("Type", Object::makeName("Catalog"));
    catalogObj.dict.insert("Pages", Object::makeRef(Ref{ -pagesNum, 0 }));
    writer.setObjectBody(catalogNum, serializeObjectBody(catalogObj, sentinelOnly));

    return writer.writeTo(outputPath, catalogNum);
}

// Re-opens `path` and confirms it parses cleanly with the expected page
// count. On failure, deletes the just-written file — never leaves a
// corrupt/partial result behind.
OpResult selfCheck(const QString& path, int expectedPageCount) {
    OpenStatus status;
    auto doc = Document::open(path, status);
    if (!doc || doc->pageCount() != expectedPageCount) {
        QFile::remove(path);
        return { false, "The file was written but didn't verify correctly afterward, "
                         "so it was discarded. Please try again." };
    }
    return { true, {} };
}

QString reasonFor(const QString& path, OpenStatus status) {
    return QString("\"%1\" isn't supported yet: %2")
        .arg(QFileInfo(path).fileName(), openStatusReason(status));
}

} // namespace

int pdfPageCountOrError(const QString& path, QString& error) {
    OpenStatus status;
    auto doc = Document::open(path, status);
    if (!doc) { error = reasonFor(path, status); return -1; }
    return doc->pageCount();
}

OpResult mergePdfs(const QStringList& inputPaths, const QString& outputPath) {
    if (inputPaths.size() < 2)
        return { false, "Pick at least two PDF files to merge." };

    std::vector<std::unique_ptr<Document>> docs;
    for (const QString& path : inputPaths) {
        OpenStatus status;
        auto doc = Document::open(path, status);
        if (!doc) return { false, reasonFor(path, status) };
        docs.push_back(std::move(doc));
    }

    Writer writer;
    const int pagesNum = writer.allocate();
    const int catalogNum = writer.allocate();

    std::vector<int> newPageNums;
    int totalPages = 0;
    for (const auto& doc : docs) {
        Copier copier(*doc, writer, /*recompress=*/false);
        for (const PageInfo& page : doc->pages()) {
            newPageNums.push_back(copier.copyPageWithParent(page.dict, pagesNum));
            ++totalPages;
        }
    }

    if (!writeOutputTree(writer, pagesNum, catalogNum, newPageNums, outputPath))
        return { false, "Could not write the merged file — check the destination is writable." };

    return selfCheck(outputPath, totalPages);
}

OpResult splitPdf(const QString& inputPath, int startPage, int endPage, const QString& outputPath) {
    OpenStatus status;
    auto doc = Document::open(inputPath, status);
    if (!doc) return { false, reasonFor(inputPath, status) };

    if (startPage < 1 || endPage > doc->pageCount() || startPage > endPage)
        return { false, QString("Page range must be between 1 and %1.").arg(doc->pageCount()) };

    Writer writer;
    const int pagesNum = writer.allocate();
    const int catalogNum = writer.allocate();

    Copier copier(*doc, writer, /*recompress=*/false);
    std::vector<int> newPageNums;
    for (int i = startPage - 1; i < endPage; ++i)
        newPageNums.push_back(copier.copyPageWithParent(doc->pages()[size_t(i)].dict, pagesNum));

    if (!writeOutputTree(writer, pagesNum, catalogNum, newPageNums, outputPath))
        return { false, "Could not write the split file — check the destination is writable." };

    return selfCheck(outputPath, endPage - startPage + 1);
}

OpResult compressPdf(const QString& inputPath, const QString& outputPath) {
    OpenStatus status;
    auto doc = Document::open(inputPath, status);
    if (!doc) return { false, reasonFor(inputPath, status) };

    Writer writer;
    const int pagesNum = writer.allocate();
    const int catalogNum = writer.allocate();

    Copier copier(*doc, writer, /*recompress=*/true);
    std::vector<int> newPageNums;
    for (const PageInfo& page : doc->pages())
        newPageNums.push_back(copier.copyPageWithParent(page.dict, pagesNum));

    if (!writeOutputTree(writer, pagesNum, catalogNum, newPageNums, outputPath))
        return { false, "Could not write the compressed file — check the destination is writable." };

    return selfCheck(outputPath, doc->pageCount());
}

} // namespace NativeOffice::Pdf
