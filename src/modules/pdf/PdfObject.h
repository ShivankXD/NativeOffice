#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfObject.h — minimal PDF object value model (dict/array/string/name/number/
// ref/stream) shared by the hand-rolled reader (PdfDocument) and writer
// (PdfWriter). Deliberately small: just enough structure to walk a page tree
// and copy objects byte-for-byte, not a full PDF content interpreter.
// ─────────────────────────────────────────────────────────────────────────────

#include <QByteArray>
#include <QMap>
#include <vector>

namespace NativeOffice::Pdf {

// An indirect reference "num gen R".
struct Ref {
    int num = 0;
    int gen = 0;
    bool operator<(const Ref& o) const { return num < o.num || (num == o.num && gen < o.gen); }
    bool operator==(const Ref& o) const { return num == o.num && gen == o.gen; }
    bool isNull() const { return num == 0; }
};

// A parsed PDF object. Streams carry their dict in `dict` and their still-
// encoded (not decompressed) bytes in `streamData` — merge/split copy stream
// bytes through verbatim rather than decoding image/content payloads.
class Object {
public:
    enum class Type { Null, Bool, Int, Real, String, Name, Array, Dict, Stream, Ref };

    Type type = Type::Null;
    bool boolVal = false;
    long long intVal = 0;
    double realVal = 0;
    QByteArray strVal;                  // literal/hex string bytes, or Name text (no leading '/')
    std::vector<Object> arr;
    QMap<QByteArray, Object> dict;       // present for Dict and Stream
    QByteArray streamData;               // present only when type == Stream (raw, still-filtered)
    Ref ref;

    Object() = default;
    static Object makeNull()               { Object o; o.type = Type::Null; return o; }
    static Object makeInt(long long v)     { Object o; o.type = Type::Int; o.intVal = v; return o; }
    static Object makeName(QByteArray n)   { Object o; o.type = Type::Name; o.strVal = std::move(n); return o; }
    static Object makeRef(Ref r)           { Object o; o.type = Type::Ref; o.ref = r; return o; }
    static Object makeArray(std::vector<Object> v) { Object o; o.type = Type::Array; o.arr = std::move(v); return o; }

    [[nodiscard]] bool isNull()   const { return type == Type::Null; }
    [[nodiscard]] bool isDict()   const { return type == Type::Dict || type == Type::Stream; }
    [[nodiscard]] bool isStream() const { return type == Type::Stream; }
    [[nodiscard]] bool isArray()  const { return type == Type::Array; }
    [[nodiscard]] bool isRef()    const { return type == Type::Ref; }
    [[nodiscard]] bool isName()   const { return type == Type::Name; }
    [[nodiscard]] bool isNumber() const { return type == Type::Int || type == Type::Real; }

    [[nodiscard]] long long asInt() const {
        if (type == Type::Int)  return intVal;
        if (type == Type::Real) return static_cast<long long>(realVal);
        return 0;
    }
    [[nodiscard]] QByteArray asName() const { return type == Type::Name ? strVal : QByteArray(); }

    [[nodiscard]] const Object* find(const QByteArray& key) const {
        auto it = dict.find(key);
        return it == dict.end() ? nullptr : &it.value();
    }
};

} // namespace NativeOffice::Pdf
