// ─────────────────────────────────────────────────────────────────────────────
// PdfDocument.cpp — see PdfDocument.h for scope/limitations.
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfDocument.h"

#include <QFile>
#include <QFileInfo>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <functional>

namespace NativeOffice::Pdf {

// ─────────────────────────────────────────────────────────────────────────────
// zlib helpers — implemented via Qt's own qCompress()/qUncompress() rather
// than linking zlib directly (not guaranteed to be present as a standalone
// system package everywhere this project builds). Qt's qUncompress expects a
// 4-byte big-endian "expected size" header before the zlib-format payload;
// PDF FlateDecode streams are pure zlib-format payloads with no such header,
// so a synthetic one is prepended/stripped here. Everything after those 4
// bytes is passed straight to zlib's own uncompress()/compress2() under the
// hood, so this is functionally identical to calling zlib directly.
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// Portable byte-string search (MSVC's CRT has no memmem()).
const char* findBytes(const char* hay, size_t hayLen, const char* needle, size_t needleLen) {
    if (needleLen == 0 || needleLen > hayLen) return nullptr;
    const char* last = hay + hayLen - needleLen;
    for (const char* p = hay; p <= last; ++p)
        if (memcmp(p, needle, needleLen) == 0) return p;
    return nullptr;
}

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

// Reverses the PNG predictor filters (types 0–4: None/Sub/Up/Average/Paeth)
// PDF xref streams commonly apply on top of FlateDecode. `bpp` is bytes per
// pixel-equivalent unit (Colors * BitsPerComponent / 8, minimum 1); `rowBytes`
// is the encoded row width (Columns * Colors * BitsPerComponent / 8).
QByteArray undoPngPredictor(const QByteArray& in, int rowBytes, int bpp) {
    if (rowBytes <= 0) return in;
    QByteArray out;
    out.reserve(in.size());
    std::vector<unsigned char> prevRow(rowBytes, 0);
    std::vector<unsigned char> curRow(rowBytes, 0);
    const unsigned char* p = reinterpret_cast<const unsigned char*>(in.constData());
    const unsigned char* end = p + in.size();
    while (p + 1 + rowBytes <= end) {
        const int filterType = *p++;
        for (int i = 0; i < rowBytes; ++i) curRow[i] = p[i];
        p += rowBytes;
        for (int i = 0; i < rowBytes; ++i) {
            const int a = i >= bpp ? curRow[i - bpp] : 0;                // left
            const int b = prevRow[i];                                    // up
            const int c = i >= bpp ? prevRow[i - bpp] : 0;                // upper-left
            int val = curRow[i];
            switch (filterType) {
            case 0: break;                              // None
            case 1: val += a; break;                     // Sub
            case 2: val += b; break;                     // Up
            case 3: val += (a + b) / 2; break;            // Average
            case 4: {                                     // Paeth
                const int pp = a + b - c;
                const int pa = std::abs(pp - a), pb = std::abs(pp - b), pc = std::abs(pp - c);
                val += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                break;
            }
            default: break;
            }
            curRow[i] = static_cast<unsigned char>(val & 0xFF);
        }
        out.append(reinterpret_cast<const char*>(curRow.data()), rowBytes);
        prevRow = curRow;
    }
    return out;
}

bool isWs(char c) { return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\f'||c=='\0'; }
bool isDelim(char c) { return c=='('||c==')'||c=='<'||c=='>'||c=='['||c==']'||c=='{'||c=='}'||c=='/'||c=='%'; }

// ─────────────────────────────────────────────────────────────────────────────
// Tokenizer / recursive-descent object parser over an in-memory buffer.
// ─────────────────────────────────────────────────────────────────────────────
struct Cursor {
    const char* p;
    const char* end;
    // Resolves an indirect /Length (etc.) to an integer; returns -1 if it
    // cannot be resolved yet (e.g. during initial xref bootstrap), in which
    // case the caller falls back to scanning for "endstream".
    std::function<long long(Ref)> resolveRef;

    void skipWs() {
        for (;;) {
            while (p < end && isWs(*p)) ++p;
            if (p < end && *p == '%') { while (p < end && *p != '\n' && *p != '\r') ++p; continue; }
            break;
        }
    }
    bool startsWith(const char* lit) const {
        const size_t n = strlen(lit);
        return size_t(end - p) >= n && strncmp(p, lit, n) == 0;
    }
    bool consume(const char* lit) {
        if (!startsWith(lit)) return false;
        p += strlen(lit);
        return true;
    }

    Object parseName() {
        // p points at '/'
        ++p;
        QByteArray out;
        while (p < end && !isWs(*p) && !isDelim(*p)) {
            if (*p == '#' && p + 2 < end && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
                char hex[3] = { p[1], p[2], 0 };
                out.append(char(strtol(hex, nullptr, 16)));
                p += 3;
            } else {
                out.append(*p++);
            }
        }
        return Object::makeName(out);
    }

    Object parseLiteralString() {
        // p points at '('
        ++p;
        QByteArray out;
        int depth = 1;
        while (p < end && depth > 0) {
            char c = *p++;
            if (c == '\\' && p < end) {
                char e = *p++;
                switch (e) {
                case 'n': out.append('\n'); break;
                case 'r': out.append('\r'); break;
                case 't': out.append('\t'); break;
                case 'b': out.append('\b'); break;
                case 'f': out.append('\f'); break;
                case '(': out.append('('); break;
                case ')': out.append(')'); break;
                case '\\': out.append('\\'); break;
                case '\r': if (p < end && *p == '\n') ++p; break;   // line continuation
                case '\n': break;
                default:
                    if (e >= '0' && e <= '7') {
                        int val = e - '0';
                        for (int i = 0; i < 2 && p < end && *p >= '0' && *p <= '7'; ++i)
                            val = val * 8 + (*p++ - '0');
                        out.append(char(val & 0xFF));
                    } else out.append(e);
                }
                continue;
            }
            if (c == '(') { ++depth; out.append(c); continue; }
            if (c == ')') { --depth; if (depth > 0) out.append(c); continue; }
            out.append(c);
        }
        Object o; o.type = Object::Type::String; o.strVal = out; return o;
    }

    Object parseHexString() {
        // p points at '<' (already checked not "<<")
        ++p;
        QByteArray hex;
        while (p < end && *p != '>') { if (!isWs(*p)) hex.append(*p); ++p; }
        if (p < end) ++p;   // consume '>'
        if (hex.size() % 2) hex.append('0');
        QByteArray out = QByteArray::fromHex(hex);
        Object o; o.type = Object::Type::String; o.strVal = out; return o;
    }

    // Parses a number, then looks ahead for "gen R" to detect a reference.
    Object parseNumberOrRef() {
        const char* start = p;
        bool isReal = false;
        if (*p == '+' || *p == '-') ++p;
        while (p < end && (isdigit((unsigned char)*p) || *p == '.')) { if (*p == '.') isReal = true; ++p; }
        const QByteArray numTok(start, int(p - start));

        if (!isReal) {
            const char* save = p;
            skipWs();
            const char* genStart = p;
            bool genOk = p < end && isdigit((unsigned char)*p);
            while (p < end && isdigit((unsigned char)*p)) ++p;
            const QByteArray genTok(genStart, int(p - genStart));
            if (genOk) {
                const char* afterGen = p;
                skipWs();
                if (p < end && *p == 'R' && (p + 1 == end || isWs(p[1]) || isDelim(p[1]))) {
                    ++p;
                    Ref r; r.num = numTok.toInt(); r.gen = genTok.toInt();
                    return Object::makeRef(r);
                }
                p = afterGen;
            }
            p = save;   // not a ref: rewind past the lookahead
        }
        Object o;
        if (isReal) { o.type = Object::Type::Real; o.realVal = numTok.toDouble(); }
        else        { o.type = Object::Type::Int;  o.intVal  = numTok.toLongLong(); }
        return o;
    }

    Object parseArray() {
        ++p;   // '['
        std::vector<Object> items;
        for (;;) {
            skipWs();
            if (p >= end) break;
            if (*p == ']') { ++p; break; }
            items.push_back(parseValue());
        }
        return Object::makeArray(std::move(items));
    }

    Object parseDictOrStream() {
        p += 2;   // '<<'
        Object o; o.type = Object::Type::Dict;
        for (;;) {
            skipWs();
            if (p >= end) break;
            if (startsWith(">>")) { p += 2; break; }
            if (*p != '/') { ++p; continue; }   // tolerate stray tokens
            Object key = parseName();
            skipWs();
            Object val = parseValue();
            o.dict.insert(key.strVal, val);
        }
        // A dict immediately followed by "stream" is actually a stream object.
        const char* save = p;
        skipWs();
        if (consume("stream")) {
            if (p < end && *p == '\r') ++p;
            if (p < end && *p == '\n') ++p;
            const char* dataStart = p;
            long long len = -1;
            if (const Object* lenObj = o.find("Length")) {
                if (lenObj->isNumber()) len = lenObj->asInt();
                else if (lenObj->isRef() && resolveRef) len = resolveRef(lenObj->ref);
            }
            const char* dataEnd;
            if (len >= 0 && dataStart + len <= end) {
                dataEnd = dataStart + len;
                // Spec-compliant files have "endstream" right after; tolerate
                // a slightly-off /Length by re-syncing on the keyword if it's
                // not immediately here (some producers pad/miscount).
                const char* probe = dataEnd;
                while (probe < end && isWs(*probe)) ++probe;
                if (!(size_t(end - probe) >= 9 && strncmp(probe, "endstream", 9) == 0)) {
                    const char* found = findBytes(dataStart, size_t(end - dataStart), "endstream", 9);
                    if (found) dataEnd = found;
                }
            } else {
                const char* found = findBytes(dataStart, size_t(end - dataStart), "endstream", 9);
                dataEnd = found ? found : end;
            }
            o.type = Object::Type::Stream;
            o.streamData = QByteArray(dataStart, int(dataEnd - dataStart));
            p = dataEnd;
            skipWs();
            consume("endstream");
        } else {
            p = save;
        }
        return o;
    }

    Object parseValue() {
        skipWs();
        if (p >= end) return Object::makeNull();
        if (startsWith("<<")) return parseDictOrStream();
        if (*p == '<') return parseHexString();
        if (*p == '[') return parseArray();
        if (*p == '/') return parseName();
        if (*p == '(') return parseLiteralString();
        if (consume("true"))  { Object o; o.type = Object::Type::Bool; o.boolVal = true;  return o; }
        if (consume("false")) { Object o; o.type = Object::Type::Bool; o.boolVal = false; return o; }
        if (consume("null"))  return Object::makeNull();
        if (*p == '+' || *p == '-' || *p == '.' || isdigit((unsigned char)*p)) return parseNumberOrRef();
        ++p;   // unknown token; skip one char to make progress
        return Object::makeNull();
    }
};

} // namespace

QString openStatusReason(OpenStatus status) {
    switch (status) {
    case OpenStatus::Ok:                return QString();
    case OpenStatus::FileNotFound:      return "file not found.";
    case OpenStatus::NotAPdf:           return "not a recognizable PDF file.";
    case OpenStatus::Encrypted:         return "encrypted PDFs aren't supported yet.";
    case OpenStatus::MalformedXref:     return "its cross-reference table is missing or unreadable.";
    case OpenStatus::MalformedTrailer:  return "its trailer/catalog is missing or unreadable.";
    case OpenStatus::MalformedPageTree: return "its page structure is broken or uses an unsupported layout.";
    case OpenStatus::IoError:           return "the file could not be read.";
    }
    return "unsupported.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Document::open — top-level orchestration
// ─────────────────────────────────────────────────────────────────────────────
std::unique_ptr<Document> Document::open(const QString& path, OpenStatus& status) {
    auto doc = std::unique_ptr<Document>(new Document());
    if (!doc->load(path, status)) return nullptr;
    return doc;
}

bool Document::load(const QString& path, OpenStatus& status) {
    if (!QFileInfo::exists(path)) { status = OpenStatus::FileNotFound; return false; }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { status = OpenStatus::IoError; return false; }
    m_data = f.readAll();
    f.close();

    if (!m_data.startsWith("%PDF-")) { status = OpenStatus::NotAPdf; return false; }

    // Find the LAST "startxref" (files can contain stale ones inside older
    // incremental-update sections; the final one is authoritative).
    const int sxPos = m_data.lastIndexOf("startxref");
    if (sxPos < 0) { status = OpenStatus::MalformedXref; return false; }
    const char* p = m_data.constData() + sxPos + 9;
    const char* end = m_data.constData() + m_data.size();
    while (p < end && isWs(*p)) ++p;
    const char* numStart = p;
    while (p < end && isdigit((unsigned char)*p)) ++p;
    if (p == numStart) { status = OpenStatus::MalformedXref; return false; }
    const qint64 startOffset = QByteArray(numStart, int(p - numStart)).toLongLong();

    if (!parseXrefChain(startOffset, status)) return false;

    if (m_trailer.find("Encrypt")) { status = OpenStatus::Encrypted; return false; }
    if (!m_trailer.find("Root"))   { status = OpenStatus::MalformedTrailer; return false; }

    if (!buildPageList(status)) return false;
    if (m_pages.empty()) { status = OpenStatus::MalformedPageTree; return false; }

    status = OpenStatus::Ok;
    return true;
}

// Follows the /Prev chain (both classic and stream sections), merging
// entries so the newest definition of each object number wins, and merging
// trailer keys so an older section can supply /Root etc. if a later,
// incremental-update trailer omitted it.
bool Document::parseXrefChain(qint64 startOffset, OpenStatus& status) {
    qint64 offset = startOffset;
    int guard = 0;
    while (offset >= 0 && guard++ < 64) {   // guard against /Prev cycles
        if (offset < 0 || offset >= m_data.size()) { status = OpenStatus::MalformedXref; return false; }
        const char* p = m_data.constData() + offset;
        const char* end = m_data.constData() + m_data.size();
        while (p < end && isWs(*p)) ++p;

        qint64 prevOffset = -1;
        bool hasPrev = false;
        bool ok;
        if (size_t(end - p) >= 4 && strncmp(p, "xref", 4) == 0) {
            ok = parseClassicXrefSection(offset, status, prevOffset, hasPrev);
        } else {
            ok = parseXrefStreamSection(offset, status, prevOffset, hasPrev);
        }
        if (!ok) return false;
        offset = hasPrev ? prevOffset : -1;
    }
    return true;
}

bool Document::parseClassicXrefSection(qint64 offset, OpenStatus& status, qint64& prevOffset, bool& hasPrev) {
    Cursor c{ m_data.constData() + offset, m_data.constData() + m_data.size(), {} };
    if (!c.consume("xref")) { status = OpenStatus::MalformedXref; return false; }
    for (;;) {
        c.skipWs();
        if (c.startsWith("trailer")) break;
        if (c.p >= c.end || !isdigit((unsigned char)*c.p)) break;
        const char* s1 = c.p; while (c.p < c.end && isdigit((unsigned char)*c.p)) ++c.p;
        const int startNum = QByteArray(s1, int(c.p - s1)).toInt();
        c.skipWs();
        const char* s2 = c.p; while (c.p < c.end && isdigit((unsigned char)*c.p)) ++c.p;
        const int count = QByteArray(s2, int(c.p - s2)).toInt();
        c.skipWs();
        for (int i = 0; i < count; ++i) {
            // Fixed-width 20-byte entries: "nnnnnnnnnn ggggg n \r\n" (lenient
            // about exact separators/EOL, since not every producer is exact).
            c.skipWs();
            const char* off1 = c.p; while (c.p < c.end && isdigit((unsigned char)*c.p)) ++c.p;
            const qint64 objOffset = QByteArray(off1, int(c.p - off1)).toLongLong();
            c.skipWs();
            const char* gen1 = c.p; while (c.p < c.end && isdigit((unsigned char)*c.p)) ++c.p;
            Q_UNUSED(gen1);
            c.skipWs();
            const char type = c.p < c.end ? *c.p : 'f';
            if (c.p < c.end) ++c.p;
            const int objNum = startNum + i;
            if (type == 'n' && m_xref.find(objNum) == m_xref.end()) {
                m_xref[objNum] = XEntry{ 1, objOffset, 0 };
            }
        }
    }
    if (!c.consume("trailer")) { status = OpenStatus::MalformedTrailer; return false; }
    c.resolveRef = [](Ref) -> long long { return -1; };   // trailer is a plain dict, never a stream
    Object trailer = c.parseValue();
    if (!trailer.isDict()) { status = OpenStatus::MalformedTrailer; return false; }
    for (auto it = trailer.dict.begin(); it != trailer.dict.end(); ++it)
        if (!m_trailer.find(it.key())) m_trailer.dict.insert(it.key(), it.value());
    m_trailer.type = Object::Type::Dict;

    if (const Object* prev = trailer.find("Prev")) { prevOffset = prev->asInt(); hasPrev = true; }
    return true;
}

bool Document::parseXrefStreamSection(qint64 offset, OpenStatus& status, qint64& prevOffset, bool& hasPrev) {
    // "N G obj << ...xref stream dict... >> stream ... endstream"
    Cursor c{ m_data.constData() + offset, m_data.constData() + m_data.size(), {} };
    c.skipWs();
    while (c.p < c.end && isdigit((unsigned char)*c.p)) ++c.p;   // object number
    c.skipWs();
    while (c.p < c.end && isdigit((unsigned char)*c.p)) ++c.p;   // generation
    c.skipWs();
    if (!c.consume("obj")) { status = OpenStatus::MalformedXref; return false; }
    c.resolveRef = [](Ref) -> long long { return -1; };   // bootstrap: no xref yet
    Object xrefObj = c.parseValue();
    if (!xrefObj.isStream()) { status = OpenStatus::MalformedXref; return false; }

    const Object* typeObj = xrefObj.find("Type");
    if (!typeObj || typeObj->asName() != "XRef") { status = OpenStatus::MalformedXref; return false; }

    QByteArray decoded;
    const Object* filter = xrefObj.find("Filter");
    const bool flate = filter && ((filter->isName() && filter->asName() == "FlateDecode")
                                    || (filter->isArray() && !filter->arr.empty()
                                        && filter->arr.front().asName() == "FlateDecode"));
    if (flate) {
        if (!zlibInflateAll(xrefObj.streamData, decoded)) { status = OpenStatus::MalformedXref; return false; }
    } else {
        decoded = xrefObj.streamData;   // uncompressed xref stream (rare but legal)
    }

    int predictor = 1, columns = 1, colors = 1, bpc = 8;
    if (const Object* dp = xrefObj.find("DecodeParms")) {
        const Object* d = dp->isArray() && !dp->arr.empty() ? &dp->arr.front() : dp;
        if (d->isDict()) {
            if (auto* v = d->find("Predictor"))        predictor = int(v->asInt());
            if (auto* v = d->find("Columns"))           columns   = int(v->asInt());
            if (auto* v = d->find("Colors"))            colors    = int(v->asInt());
            if (auto* v = d->find("BitsPerComponent"))  bpc       = int(v->asInt());
        }
    }
    if (predictor >= 10) {
        const int bpp = std::max(1, colors * bpc / 8);
        const int rowBytes = columns * colors * bpc / 8;
        decoded = undoPngPredictor(decoded, rowBytes, bpp);
    }

    int w0 = 1, w1 = 1, w2 = 1;
    if (const Object* w = xrefObj.find("W")) {
        if (w->isArray() && w->arr.size() >= 3) {
            w0 = int(w->arr[0].asInt()); w1 = int(w->arr[1].asInt()); w2 = int(w->arr[2].asInt());
        }
    }
    const int rowLen = w0 + w1 + w2;
    if (rowLen <= 0) { status = OpenStatus::MalformedXref; return false; }

    std::vector<std::pair<int,int>> subsections;   // (start, count)
    if (const Object* idx = xrefObj.find("Index"); idx && idx->isArray()) {
        for (size_t i = 0; i + 1 < idx->arr.size(); i += 2)
            subsections.emplace_back(int(idx->arr[i].asInt()), int(idx->arr[i + 1].asInt()));
    } else {
        const Object* size = xrefObj.find("Size");
        subsections.emplace_back(0, size ? int(size->asInt()) : 0);
    }

    auto readField = [](const unsigned char*& q, int width) -> long long {
        long long v = 0;
        for (int i = 0; i < width; ++i) v = (v << 8) | *q++;
        return v;
    };

    const unsigned char* q = reinterpret_cast<const unsigned char*>(decoded.constData());
    const unsigned char* qend = q + decoded.size();
    for (const auto& [start, count] : subsections) {
        for (int i = 0; i < count && q + rowLen <= qend; ++i) {
            const long long f0 = w0 ? readField(q, w0) : 1;   // default type 1 if width 0
            const long long f1 = readField(q, w1);
            const long long f2 = w2 ? readField(q, w2) : 0;
            const int objNum = start + i;
            if (m_xref.find(objNum) != m_xref.end()) continue;   // newer section already won
            if (f0 == 1) m_xref[objNum] = XEntry{ 1, f1, 0 };
            else if (f0 == 2) m_xref[objNum] = XEntry{ 2, f1, f2 };
            // f0 == 0: free object, ignore
        }
    }

    for (auto it = xrefObj.dict.begin(); it != xrefObj.dict.end(); ++it)
        if (!m_trailer.find(it.key())) m_trailer.dict.insert(it.key(), it.value());
    m_trailer.type = Object::Type::Dict;

    if (const Object* prev = xrefObj.find("Prev")) { prevOffset = prev->asInt(); hasPrev = true; }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Object access
// ─────────────────────────────────────────────────────────────────────────────
const Object& Document::resolve(Ref r) const { return fetchObject(r); }

const Object& Document::resolve(const Object& v) const {
    if (v.isRef()) return fetchObject(v.ref);
    return v;
}

const Object& Document::fetchObject(Ref r) const {
    static const Object kNull = Object::makeNull();
    auto cacheIt = m_objCache.find(r.num);
    if (cacheIt != m_objCache.end()) return cacheIt->second;

    auto xIt = m_xref.find(r.num);
    if (xIt == m_xref.end()) return kNull;

    Object obj;
    if (xIt->second.type == 1) {
        obj = parseObjectAtOffset(xIt->second.a);
    } else if (xIt->second.type == 2) {
        obj = parseIndirectFromObjStream(int(xIt->second.a), int(xIt->second.b));
    } else {
        return kNull;
    }
    auto [insIt, _] = m_objCache.emplace(r.num, std::move(obj));
    return insIt->second;
}

Object Document::parseObjectAtOffset(qint64 offset) const {
    if (offset < 0 || offset >= m_data.size()) return Object::makeNull();
    Cursor c{ m_data.constData() + offset, m_data.constData() + m_data.size(), {} };
    c.skipWs();
    while (c.p < c.end && isdigit((unsigned char)*c.p)) ++c.p;   // obj number
    c.skipWs();
    while (c.p < c.end && isdigit((unsigned char)*c.p)) ++c.p;   // generation
    c.skipWs();
    if (!c.consume("obj")) return Object::makeNull();
    c.resolveRef = [this](Ref rr) -> long long { return resolve(rr).asInt(); };
    return c.parseValue();
}

Object Document::parseIndirectFromObjStream(int streamObjNum, int indexInStream) const {
    auto cacheIt = m_objStmCache.find(streamObjNum);
    QByteArray decoded;
    int n = 0, first = 0;
    const Object* stm = nullptr;
    Object stmHolder;
    if (cacheIt == m_objStmCache.end()) {
        Ref sr{ streamObjNum, 0 };
        stmHolder = resolve(sr);
        stm = &stmHolder;
        if (!stm->isStream()) return Object::makeNull();
        const Object* filter = stm->find("Filter");
        const bool flate = filter && filter->isName() && filter->asName() == "FlateDecode";
        if (flate) { if (!zlibInflateAll(stm->streamData, decoded)) return Object::makeNull(); }
        else         decoded = stm->streamData;
    } else {
        // Re-decode is cheap enough for our use (tool-hub scale files); we
        // only cache the parsed member objects themselves below.
        Ref sr{ streamObjNum, 0 };
        stmHolder = resolve(sr);
        stm = &stmHolder;
        const Object* filter = stm->find("Filter");
        const bool flate = filter && filter->isName() && filter->asName() == "FlateDecode";
        if (flate) { if (!zlibInflateAll(stm->streamData, decoded)) return Object::makeNull(); }
        else         decoded = stm->streamData;
    }
    const Object* nObj = stm->find("N");
    const Object* firstObj = stm->find("First");
    if (!nObj || !firstObj) return Object::makeNull();
    n = int(nObj->asInt());
    first = int(firstObj->asInt());
    if (indexInStream < 0 || indexInStream >= n) return Object::makeNull();

    Cursor header{ decoded.constData(), decoded.constData() + decoded.size(), {} };
    long long targetOffset = -1;
    for (int i = 0; i < n; ++i) {
        header.skipWs();
        const char* s1 = header.p; while (header.p < header.end && isdigit((unsigned char)*header.p)) ++header.p;
        Q_UNUSED(s1);
        header.skipWs();
        const char* s2 = header.p; while (header.p < header.end && isdigit((unsigned char)*header.p)) ++header.p;
        const long long relOffset = QByteArray(s2, int(header.p - s2)).toLongLong();
        if (i == indexInStream) { targetOffset = relOffset; }
    }
    if (targetOffset < 0) return Object::makeNull();
    Cursor body{ decoded.constData() + first + targetOffset, decoded.constData() + decoded.size(), {} };
    body.resolveRef = [this](Ref rr) -> long long { return resolve(rr).asInt(); };
    return body.parseValue();
}

// ─────────────────────────────────────────────────────────────────────────────
// Page tree
// ─────────────────────────────────────────────────────────────────────────────
const Object& Document::catalog() const {
    static const Object kNull;
    const Object* rootRefObj = m_trailer.find("Root");
    if (!rootRefObj || !rootRefObj->isRef()) return kNull;
    return resolve(*rootRefObj);
}

bool Document::buildPageList(OpenStatus& status) {
    const Object* rootRefObj = m_trailer.find("Root");
    if (!rootRefObj || !rootRefObj->isRef()) { status = OpenStatus::MalformedTrailer; return false; }
    const Object& catalog = resolve(*rootRefObj);
    if (!catalog.isDict()) { status = OpenStatus::MalformedTrailer; return false; }
    const Object* pagesRefObj = catalog.find("Pages");
    if (!pagesRefObj || !pagesRefObj->isRef()) { status = OpenStatus::MalformedPageTree; return false; }

    std::vector<Ref> visiting;
    bool failed = false;
    walkPageTree(pagesRefObj->ref, Object::makeNull(), Object::makeNull(), visiting, status, failed);
    if (failed) return false;
    return true;
}

void Document::walkPageTree(Ref node, Object inheritedRes, Object inheritedBox,
                             std::vector<Ref>& visiting, OpenStatus& status, bool& failed) {
    if (failed) return;
    for (const Ref& v : visiting) {
        if (v == node) { status = OpenStatus::MalformedPageTree; failed = true; return; }
    }
    if (visiting.size() > 256) { status = OpenStatus::MalformedPageTree; failed = true; return; }
    visiting.push_back(node);

    const Object& obj = resolve(node);
    if (!obj.isDict()) { visiting.pop_back(); return; }   // tolerate a dangling kid ref

    Object res = obj.find("Resources") ? *obj.find("Resources") : inheritedRes;
    Object box = obj.find("MediaBox")  ? *obj.find("MediaBox")  : inheritedBox;

    const Object* kids = obj.find("Kids");
    if (kids && kids->isArray()) {
        for (const Object& kid : kids->arr) {
            if (!kid.isRef()) continue;
            walkPageTree(kid.ref, res, box, visiting, status, failed);
            if (failed) { visiting.pop_back(); return; }
        }
    } else {
        // Leaf page. Bake inherited attributes in so PdfWriter doesn't need
        // to understand inheritance when copying just this page.
        Object leaf = obj;
        if (!leaf.find("Resources") && !res.isNull()) leaf.dict.insert("Resources", res);
        if (!leaf.find("MediaBox")  && !box.isNull()) leaf.dict.insert("MediaBox", box);
        m_pages.push_back(PageInfo{ node, leaf });
    }
    visiting.pop_back();
}

} // namespace NativeOffice::Pdf
