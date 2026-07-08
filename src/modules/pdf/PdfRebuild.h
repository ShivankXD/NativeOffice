#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfRebuild.h — INTERNAL shared machinery for features that rebuild the
// document through PdfDocument→PdfWriter (annotations, crypto, …): the
// memoizing object copier, sentinel-ref helpers, raw-stream emission, and
// the common pages-node/catalog/self-check tail. Header-only; not part of
// the module's public surface.
// ─────────────────────────────────────────────────────────────────────────────

#include "PdfDocument.h"
#include "PdfOps.h"
#include "PdfWriter.h"

#include <QFile>
#include <map>

namespace NativeOffice::Pdf::rebuild {

// Optional hook applied to each object right before serialization — used by
// the encryption pass to encrypt every string/stream in place, keyed by the
// object's new number.
struct ObjectEncryptor {
    virtual ~ObjectEncryptor() = default;
    virtual void encrypt(Object& obj, int objNum) = 0;
};

class Copier {
public:
    explicit Copier(const Document& src, Writer& dst, ObjectEncryptor* enc = nullptr)
        : m_src(src), m_dst(dst), m_enc(enc) {}

    int copy(Ref oldRef) {
        if (oldRef.num < 0) return -oldRef.num;   // sentinel passthrough
        auto it = m_map.find(oldRef);
        if (it != m_map.end()) return it->second;
        const int newNum = m_dst.allocate();
        m_map[oldRef] = newNum;                    // register first: breaks cycles
        if (m_enc) {
            Object working = m_src.resolve(oldRef);   // mutable copy to encrypt
            m_enc->encrypt(working, newNum);
            m_dst.setObjectBody(newNum, serializeObjectBody(working, [this](Ref r) { return copy(r); }));
        } else {
            const Object& working = m_src.resolve(oldRef);
            m_dst.setObjectBody(newNum, serializeObjectBody(working, [this](Ref r) { return copy(r); }));
        }
        return newNum;
    }

    // Pre-maps a page's source ref so outline dests/fields land on the copy.
    void mapPage(Ref oldRef, int newNum) { if (oldRef.num > 0) m_map[oldRef] = newNum; }

    // Pre-maps ANY source object to a caller-built replacement number, so
    // every reference to it (from pages, AcroForm, etc.) resolves to the
    // replacement instead of a verbatim copy. Used to substitute modified
    // form-field / AcroForm objects during a rebuild.
    void preMap(Ref oldRef, int newNum) { if (oldRef.num > 0) m_map[oldRef] = newNum; }
    [[nodiscard]] bool isMapped(Ref oldRef) const { return m_map.count(oldRef) > 0; }

private:
    const Document& m_src;
    Writer& m_dst;
    ObjectEncryptor* m_enc = nullptr;
    std::map<Ref, int> m_map;
};

inline Object refTo(int writerNum) { return Object::makeRef(Ref{ -writerNum, 0 }); }

inline Object realObj(double v) {
    Object o; o.type = Object::Type::Real; o.realVal = v; return o;
}

inline int addRawStream(Writer& writer, QMap<QByteArray, Object> dict, const QByteArray& data) {
    Object stream;
    stream.type = Object::Type::Stream;
    stream.dict = std::move(dict);
    stream.dict.insert("Length", Object::makeInt(data.size()));
    stream.streamData = data;
    const int n = writer.allocate();
    writer.setObjectBody(n, serializeObjectBody(stream, [](Ref r) { return r.num < 0 ? -r.num : 0; }));
    return n;
}

// MediaBox of a resolved page dict (inheritance already baked in).
inline void mediaBoxOf(const Document& doc, const Object& pageDict,
                       double& x0, double& y0, double& w, double& h) {
    x0 = 0; y0 = 0; w = 612; h = 792;
    if (const Object* mb = pageDict.find("MediaBox")) {
        const Object& box = doc.resolve(*mb);
        if (box.isArray() && box.arr.size() == 4) {
            auto v = [&](int i) {
                const Object& o = doc.resolve(box.arr[size_t(i)]);
                return o.type == Object::Type::Real ? o.realVal : double(o.asInt());
            };
            x0 = v(0); y0 = v(1); w = v(2) - x0; h = v(3) - y0;
        }
    }
}

// Writes the Pages node + Catalog (carrying over catalog extras through
// `copier`), writes the file, and self-checks it. `skipCatalogKeys` names
// extras the caller replaces itself (e.g. "Outlines" when rebuilding the
// outline, "AcroForm" when the caller rewrote fields); `extraCatalog`
// entries are inserted verbatim afterwards.
inline OpResult finishTree(Writer& writer, int pagesNum, int catalogNum,
                           const std::vector<int>& newPageNums, const QString& outputPath,
                           const Document& doc, Copier& copier,
                           const QList<QByteArray>& skipCatalogKeys = {},
                           const QMap<QByteArray, Object>& extraCatalog = {}) {
    auto sentinelOnly = [](Ref r) { return r.num < 0 ? -r.num : 0; };

    Object pagesObj; pagesObj.type = Object::Type::Dict;
    pagesObj.dict.insert("Type", Object::makeName("Pages"));
    std::vector<Object> kids;
    kids.reserve(newPageNums.size());
    for (int n : newPageNums) kids.push_back(refTo(n));
    pagesObj.dict.insert("Kids", Object::makeArray(std::move(kids)));
    pagesObj.dict.insert("Count", Object::makeInt(qint64(newPageNums.size())));
    writer.setObjectBody(pagesNum, serializeObjectBody(pagesObj, sentinelOnly));

    Object catalogObj; catalogObj.type = Object::Type::Dict;
    catalogObj.dict.insert("Type", Object::makeName("Catalog"));
    catalogObj.dict.insert("Pages", refTo(pagesNum));

    static const char* kKeep[] = { "Outlines", "AcroForm", "Names", "PageLabels",
                                   "Lang", "ViewerPreferences", "PageMode", "PageLayout" };
    const Object& srcCatalog = doc.catalog();
    if (srcCatalog.isDict()) {
        for (const char* key : kKeep) {
            if (skipCatalogKeys.contains(QByteArray(key))) continue;
            if (const Object* v = srcCatalog.find(key); v && v->isRef())
                catalogObj.dict.insert(key, refTo(copier.copy(v->ref)));
        }
    }
    for (auto it = extraCatalog.begin(); it != extraCatalog.end(); ++it)
        catalogObj.dict.insert(it.key(), it.value());

    writer.setObjectBody(catalogNum, serializeObjectBody(catalogObj, sentinelOnly));

    if (!writer.writeTo(outputPath, catalogNum))
        return { false, "Could not write the output file — check the destination is writable." };

    OpenStatus st;
    auto check = Document::open(outputPath, st);
    if (!check || check->pageCount() != int(newPageNums.size())) {
        QFile::remove(outputPath);
        return { false, "The file was written but didn't verify correctly afterward, so it was discarded." };
    }
    return { true, {} };
}

} // namespace NativeOffice::Pdf::rebuild
