// ─────────────────────────────────────────────────────────────────────────────
// PdfOps.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfOps.h"
#include "PdfDocument.h"
#include "PdfWriter.h"

#include <QFile>
#include <QFileInfo>
#include <algorithm>
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
    Copier(const Document& src, Writer& dst, bool recompress, int level = 9)
        : m_src(src), m_dst(dst), m_recompress(recompress), m_level(level) {}

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
    // node built by the caller. When `srcRef` is passed, the page's source
    // ref is memoized to the new number FIRST, so anything else that points
    // at this page (outline destinations, form fields, links) resolves to
    // the copied page instead of deep-copying a duplicate.
    int copyPageWithParent(Object pageDict, int newParentNum, Ref srcRef = {}) {
        const int newNum = m_dst.allocate();
        if (srcRef.num > 0) m_map[srcRef] = newNum;
        pageDict.dict.insert("Parent", Object::makeRef(Ref{ -newParentNum, 0 }));
        const QByteArray body = serializeObjectBody(pageDict, [this](Ref r) { return copy(r); });
        m_dst.setObjectBody(newNum, body);
        return newNum;
    }

    Writer& writer() { return m_dst; }

private:
    void maybeRecompress(Object& obj) const {
        if (!m_recompress || !obj.isStream()) return;
        const Object* filter = obj.find("Filter");
        if (!filter || !filter->isName() || filter->asName() != "FlateDecode") return;
        QByteArray inflated;
        if (!zlibInflateAll(obj.streamData, inflated)) return;
        QByteArray recompressed;
        if (zlibDeflateAll(inflated, recompressed, m_level) && recompressed.size() < obj.streamData.size())
            obj.streamData = recompressed;
    }

    const Document& m_src;
    Writer& m_dst;
    bool m_recompress;
    int  m_level = 9;      // zlib level; lossless, so this trades size against time
    std::map<Ref, int> m_map;
};

// Builds the shared Pages node + Catalog for a set of already-copied page
// object numbers, and writes the file. Common tail for merge/split/compress.
//
// When `src`/`copier` are given, catalog-level extras (outlines, forms,
// names, page labels…) are carried over from the source catalog. Pass
// `keepOutlines = false` for operations that REMOVE pages: outline
// destinations pointing at removed pages would otherwise pull orphaned
// copies of them into the output.
bool writeOutputTree(Writer& writer, int pagesNum, int catalogNum,
                      const std::vector<int>& newPageNums, const QString& outputPath,
                      const Document* src = nullptr, Copier* copier = nullptr,
                      bool keepOutlines = true) {
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

    if (src && copier) {
        // Carry over catalog entries that survive a page-tree rebuild. The
        // Copier's page memoization (copyPageWithParent) makes their page
        // references land on the copied pages.
        static const char* kKeep[] = {
            "Outlines", "AcroForm", "Names", "PageLabels", "Lang",
            "ViewerPreferences", "PageMode", "PageLayout", "Metadata",
        };
        const Object& srcCatalog = src->catalog();
        if (srcCatalog.isDict()) {
            for (const char* key : kKeep) {
                if (!keepOutlines && qstrcmp(key, "Outlines") == 0) continue;
                const Object* v = srcCatalog.find(key);
                if (!v) continue;
                if (v->isRef()) {
                    catalogObj.dict.insert(key, Object::makeRef(Ref{ -copier->copy(v->ref), 0 }));
                } else {
                    // Direct value: hoist it into its own indirect object.
                    const QByteArray body = serializeObjectBody(*v,
                        [copier](Ref r) { return copier->copy(r); });
                    const int holder = copier->writer().allocate();
                    copier->writer().setObjectBody(holder, body);
                    catalogObj.dict.insert(key, Object::makeRef(Ref{ -holder, 0 }));
                }
            }
        }
    }

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

namespace {

// Common front half of the single-input page ops: open the doc, validate the
// page list (empty = all pages), and normalize it.
struct OpenedForEdit {
    std::unique_ptr<Document> doc;
    std::vector<int> pages;         // validated, 0-based
    OpResult error;                 // .ok == false when open/validation failed
};

OpenedForEdit openForEdit(const QString& inputPath, const std::vector<int>& pages) {
    OpenedForEdit r;
    OpenStatus status;
    r.doc = Document::open(inputPath, status);
    if (!r.doc) { r.error = { false, reasonFor(inputPath, status) }; return r; }

    if (pages.empty()) {
        for (int i = 0; i < r.doc->pageCount(); ++i) r.pages.push_back(i);
    } else {
        for (int p : pages) {
            if (p < 0 || p >= r.doc->pageCount()) {
                r.error = { false, QString("Page %1 is out of range (1–%2).")
                                       .arg(p + 1).arg(r.doc->pageCount()) };
                r.doc.reset();
                return r;
            }
            r.pages.push_back(p);
        }
    }
    r.error.ok = true;
    return r;
}

// Rebuild `doc` with per-page tweaks. `mutatePage(i, dict)` may modify each
// copied page dict; `keepPage(i)` filters; order defaults to source order.
OpResult rebuildWithPages(const Document& doc, const QString& outputPath,
                          const std::vector<int>& order,
                          const std::function<void(int, Object&)>& mutatePage,
                          bool keepOutlines = true) {
    Writer writer;
    const int pagesNum = writer.allocate();
    const int catalogNum = writer.allocate();

    Copier copier(doc, writer, /*recompress=*/false);
    std::vector<int> newPageNums;
    newPageNums.reserve(order.size());
    for (int srcIdx : order) {
        const PageInfo& page = doc.pages()[size_t(srcIdx)];
        Object dict = page.dict;
        if (mutatePage) mutatePage(srcIdx, dict);
        newPageNums.push_back(copier.copyPageWithParent(std::move(dict), pagesNum, page.ref));
    }

    if (!writeOutputTree(writer, pagesNum, catalogNum, newPageNums, outputPath,
                         &doc, &copier, keepOutlines))
        return { false, "Could not write the output file — check the destination is writable." };
    return selfCheck(outputPath, int(order.size()));
}

} // namespace

OpResult rotatePages(const QString& inputPath, const std::vector<int>& pages,
                     int degreesCW, const QString& outputPath) {
    auto in = openForEdit(inputPath, pages);
    if (!in.error.ok) return in.error;
    if (degreesCW % 90 != 0)
        return { false, "Rotation must be a multiple of 90 degrees." };

    std::vector<bool> rotate(size_t(in.doc->pageCount()), false);
    for (int p : in.pages) rotate[size_t(p)] = true;

    std::vector<int> order;
    for (int i = 0; i < in.doc->pageCount(); ++i) order.push_back(i);

    const Document& doc = *in.doc;
    return rebuildWithPages(doc, outputPath, order, [&](int i, Object& dict) {
        if (!rotate[size_t(i)]) return;
        long long cur = 0;
        if (const Object* r = dict.find("Rotate")) cur = doc.resolve(*r).asInt();
        long long next = (cur + degreesCW) % 360;
        if (next < 0) next += 360;
        dict.dict.insert("Rotate", Object::makeInt(next));
    });
}

OpResult deletePages(const QString& inputPath, const std::vector<int>& pages,
                     const QString& outputPath) {
    auto in = openForEdit(inputPath, pages);
    if (!in.error.ok) return in.error;

    std::vector<bool> drop(size_t(in.doc->pageCount()), false);
    for (int p : in.pages) drop[size_t(p)] = true;

    std::vector<int> order;
    for (int i = 0; i < in.doc->pageCount(); ++i)
        if (!drop[size_t(i)]) order.push_back(i);
    if (order.empty())
        return { false, "A PDF must keep at least one page." };
    if (order.size() == size_t(in.doc->pageCount()))
        return { false, "No pages were selected to delete." };

    return rebuildWithPages(*in.doc, outputPath, order, nullptr, /*keepOutlines=*/false);
}

OpResult reorderPages(const QString& inputPath, const std::vector<int>& newOrder,
                      const QString& outputPath) {
    OpenStatus status;
    auto doc = Document::open(inputPath, status);
    if (!doc) return { false, reasonFor(inputPath, status) };

    if (int(newOrder.size()) != doc->pageCount())
        return { false, "Internal error: reorder list doesn't match the page count." };
    std::vector<bool> seen(newOrder.size(), false);
    for (int p : newOrder) {
        if (p < 0 || p >= int(newOrder.size()) || seen[size_t(p)])
            return { false, "Internal error: reorder list isn't a permutation." };
        seen[size_t(p)] = true;
    }

    return rebuildWithPages(*doc, outputPath, newOrder, nullptr);
}

OpResult extractPages(const QString& inputPath, const std::vector<int>& pages,
                      const QString& outputPath) {
    auto in = openForEdit(inputPath, pages);
    if (!in.error.ok) return in.error;
    if (in.pages.empty()) return { false, "Pick at least one page to extract." };

    // Extraction produces a NEW document: don't carry over document-wide
    // extras (outlines etc. would mostly dangle).
    Writer writer;
    const int pagesNum = writer.allocate();
    const int catalogNum = writer.allocate();
    Copier copier(*in.doc, writer, false);
    std::vector<int> newPageNums;
    for (int idx : in.pages) {
        const PageInfo& page = in.doc->pages()[size_t(idx)];
        newPageNums.push_back(copier.copyPageWithParent(page.dict, pagesNum, page.ref));
    }
    if (!writeOutputTree(writer, pagesNum, catalogNum, newPageNums, outputPath))
        return { false, "Could not write the output file — check the destination is writable." };
    return selfCheck(outputPath, int(in.pages.size()));
}

OpResult insertBlankPage(const QString& inputPath, int atIndex,
                         double widthPt, double heightPt, const QString& outputPath) {
    OpenStatus status;
    auto doc = Document::open(inputPath, status);
    if (!doc) return { false, reasonFor(inputPath, status) };
    atIndex = std::max(0, std::min(atIndex, doc->pageCount()));

    Writer writer;
    const int pagesNum = writer.allocate();
    const int catalogNum = writer.allocate();
    Copier copier(*doc, writer, false);

    std::vector<int> newPageNums;
    auto pushBlank = [&] {
        Object blank; blank.type = Object::Type::Dict;
        blank.dict.insert("Type", Object::makeName("Page"));
        std::vector<Object> box;
        box.push_back(Object::makeInt(0));
        box.push_back(Object::makeInt(0));
        Object w; w.type = Object::Type::Real; w.realVal = widthPt;
        Object h; h.type = Object::Type::Real; h.realVal = heightPt;
        box.push_back(w);
        box.push_back(h);
        blank.dict.insert("MediaBox", Object::makeArray(std::move(box)));
        Object res; res.type = Object::Type::Dict;
        blank.dict.insert("Resources", res);
        blank.dict.insert("Parent", Object::makeRef(Ref{ -pagesNum, 0 }));
        const int num = writer.allocate();
        writer.setObjectBody(num, serializeObjectBody(blank, [](Ref r) { return r.num < 0 ? -r.num : 0; }));
        newPageNums.push_back(num);
    };

    for (int i = 0; i < doc->pageCount(); ++i) {
        if (i == atIndex) pushBlank();
        const PageInfo& page = doc->pages()[size_t(i)];
        newPageNums.push_back(copier.copyPageWithParent(page.dict, pagesNum, page.ref));
    }
    if (atIndex == doc->pageCount()) pushBlank();

    if (!writeOutputTree(writer, pagesNum, catalogNum, newPageNums, outputPath, doc.get(), &copier))
        return { false, "Could not write the output file — check the destination is writable." };
    return selfCheck(outputPath, doc->pageCount() + 1);
}

OpResult insertPdfAt(const QString& inputPath, const QString& insertPdfPath,
                     int atIndex, const QString& outputPath) {
    OpenStatus status;
    auto doc = Document::open(inputPath, status);
    if (!doc) return { false, reasonFor(inputPath, status) };
    auto ins = Document::open(insertPdfPath, status);
    if (!ins) return { false, reasonFor(insertPdfPath, status) };
    atIndex = std::max(0, std::min(atIndex, doc->pageCount()));

    Writer writer;
    const int pagesNum = writer.allocate();
    const int catalogNum = writer.allocate();
    Copier mainCopier(*doc, writer, false);
    Copier insCopier(*ins, writer, false);

    std::vector<int> newPageNums;
    auto pushInserted = [&] {
        for (const PageInfo& page : ins->pages())
            newPageNums.push_back(insCopier.copyPageWithParent(page.dict, pagesNum, page.ref));
    };
    for (int i = 0; i < doc->pageCount(); ++i) {
        if (i == atIndex) pushInserted();
        const PageInfo& page = doc->pages()[size_t(i)];
        newPageNums.push_back(mainCopier.copyPageWithParent(page.dict, pagesNum, page.ref));
    }
    if (atIndex == doc->pageCount()) pushInserted();

    if (!writeOutputTree(writer, pagesNum, catalogNum, newPageNums, outputPath, doc.get(), &mainCopier))
        return { false, "Could not write the output file — check the destination is writable." };
    return selfCheck(outputPath, doc->pageCount() + ins->pageCount());
}

OpResult replacePages(const QString& inputPath, int fromPage, int toPage,
                      const QString& srcPdfPath, const QString& outputPath) {
    OpenStatus status;
    auto doc = Document::open(inputPath, status);
    if (!doc) return { false, reasonFor(inputPath, status) };
    auto src = Document::open(srcPdfPath, status);
    if (!src) return { false, reasonFor(srcPdfPath, status) };
    if (fromPage < 0 || toPage >= doc->pageCount() || fromPage > toPage)
        return { false, QString("Page range must be between 1 and %1.").arg(doc->pageCount()) };

    Writer writer;
    const int pagesNum = writer.allocate();
    const int catalogNum = writer.allocate();
    Copier mainCopier(*doc, writer, false);
    Copier srcCopier(*src, writer, false);

    std::vector<int> newPageNums;
    for (int i = 0; i < doc->pageCount(); ++i) {
        if (i == fromPage)
            for (const PageInfo& page : src->pages())
                newPageNums.push_back(srcCopier.copyPageWithParent(page.dict, pagesNum, page.ref));
        if (i >= fromPage && i <= toPage) continue;
        const PageInfo& page = doc->pages()[size_t(i)];
        newPageNums.push_back(mainCopier.copyPageWithParent(page.dict, pagesNum, page.ref));
    }

    if (!writeOutputTree(writer, pagesNum, catalogNum, newPageNums, outputPath,
                         doc.get(), &mainCopier, /*keepOutlines=*/false))
        return { false, "Could not write the output file — check the destination is writable." };
    return selfCheck(outputPath, doc->pageCount() - (toPage - fromPage + 1) + src->pageCount());
}

OpResult setCropBox(const QString& inputPath, const std::vector<int>& pages,
                    double x0, double y0, double x1, double y1,
                    const QString& outputPath) {
    auto in = openForEdit(inputPath, pages);
    if (!in.error.ok) return in.error;
    if (x1 - x0 < 1 || y1 - y0 < 1)
        return { false, "The crop area is too small." };

    std::vector<bool> apply(size_t(in.doc->pageCount()), false);
    for (int p : in.pages) apply[size_t(p)] = true;
    std::vector<int> order;
    for (int i = 0; i < in.doc->pageCount(); ++i) order.push_back(i);

    return rebuildWithPages(*in.doc, outputPath, order, [&](int i, Object& dict) {
        if (!apply[size_t(i)]) return;
        std::vector<Object> box;
        for (double v : { x0, y0, x1, y1 }) {
            Object o; o.type = Object::Type::Real; o.realVal = v;
            box.push_back(o);
        }
        dict.dict.insert("CropBox", Object::makeArray(std::move(box)));
    });
}

OpResult resizePages(const QString& inputPath, const std::vector<int>& pages,
                     double widthPt, double heightPt, const QString& outputPath) {
    auto in = openForEdit(inputPath, pages);
    if (!in.error.ok) return in.error;
    if (widthPt < 18 || heightPt < 18)
        return { false, "The page size is too small." };

    std::vector<bool> apply(size_t(in.doc->pageCount()), false);
    for (int p : in.pages) apply[size_t(p)] = true;
    std::vector<int> order;
    for (int i = 0; i < in.doc->pageCount(); ++i) order.push_back(i);

    const Document& doc = *in.doc;

    Writer writer;
    const int pagesNum = writer.allocate();
    const int catalogNum = writer.allocate();
    Copier copier(doc, writer, false);

    std::vector<int> newPageNums;
    for (int i : order) {
        const PageInfo& page = doc.pages()[size_t(i)];
        Object dict = page.dict;

        if (apply[size_t(i)]) {
            // Old size from the (inheritance-baked) MediaBox.
            double ow = 612, oh = 792, ox = 0, oy = 0;
            if (const Object* mb = dict.find("MediaBox")) {
                const Object& box = doc.resolve(*mb);
                if (box.isArray() && box.arr.size() == 4) {
                    auto num = [&](int k) {
                        const Object& v = doc.resolve(box.arr[size_t(k)]);
                        return v.type == Object::Type::Real ? v.realVal : double(v.asInt());
                    };
                    ox = num(0); oy = num(1);
                    ow = num(2) - ox; oh = num(3) - oy;
                }
            }
            const double sx = widthPt / ow, sy = heightPt / oh;

            // Wrap existing content in q…Q and prepend the scale transform.
            const QByteArray pre = QByteArray("q\n")
                + QByteArray::number(sx, 'f', 6) + " 0 0 "
                + QByteArray::number(sy, 'f', 6) + " "
                + QByteArray::number(-ox * sx, 'f', 2) + " "
                + QByteArray::number(-oy * sy, 'f', 2) + " cm\n";
            Object preStream; preStream.type = Object::Type::Stream;
            preStream.dict.insert("Length", Object::makeInt(pre.size()));
            preStream.streamData = pre;
            const int preNum = writer.allocate();
            writer.setObjectBody(preNum, serializeObjectBody(preStream, [](Ref) { return 0; }));

            Object postStream; postStream.type = Object::Type::Stream;
            postStream.dict.insert("Length", Object::makeInt(2));
            postStream.streamData = "\nQ";
            const int postNum = writer.allocate();
            writer.setObjectBody(postNum, serializeObjectBody(postStream, [](Ref) { return 0; }));

            std::vector<Object> contents;
            contents.push_back(Object::makeRef(Ref{ -preNum, 0 }));
            if (const Object* c = dict.find("Contents")) {
                if (c->isArray())
                    for (const Object& e : c->arr) contents.push_back(e);
                else
                    contents.push_back(*c);
            }
            contents.push_back(Object::makeRef(Ref{ -postNum, 0 }));
            dict.dict.insert("Contents", Object::makeArray(std::move(contents)));

            std::vector<Object> box;
            for (double v : { 0.0, 0.0, widthPt, heightPt }) {
                Object o; o.type = Object::Type::Real; o.realVal = v;
                box.push_back(o);
            }
            dict.dict.insert("MediaBox", Object::makeArray(std::move(box)));
            dict.dict.remove("CropBox");
        }

        newPageNums.push_back(copier.copyPageWithParent(std::move(dict), pagesNum, page.ref));
    }

    if (!writeOutputTree(writer, pagesNum, catalogNum, newPageNums, outputPath, &doc, &copier))
        return { false, "Could not write the output file — check the destination is writable." };
    return selfCheck(outputPath, doc.pageCount());
}

OpResult compressPdf(const QString& inputPath, const QString& outputPath, int deflateLevel) {
    OpenStatus status;
    auto doc = Document::open(inputPath, status);
    if (!doc) return { false, reasonFor(inputPath, status) };

    Writer writer;
    const int pagesNum = writer.allocate();
    const int catalogNum = writer.allocate();

    Copier copier(*doc, writer, /*recompress=*/true, qBound(1, deflateLevel, 9));
    std::vector<int> newPageNums;
    for (const PageInfo& page : doc->pages())
        newPageNums.push_back(copier.copyPageWithParent(page.dict, pagesNum));

    if (!writeOutputTree(writer, pagesNum, catalogNum, newPageNums, outputPath))
        return { false, "Could not write the compressed file — check the destination is writable." };

    return selfCheck(outputPath, doc->pageCount());
}

} // namespace NativeOffice::Pdf
