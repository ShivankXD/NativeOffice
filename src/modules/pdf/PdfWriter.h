#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfWriter.h — writes a fresh, classic-xref PDF from a set of pre-rendered
// object bodies. Used by PdfOps (merge/split/compress) as the single output
// path — regardless of whether the SOURCE files used classic xref or
// xref-streams/object-streams, the output is always a plain classic xref
// table, which is simpler to emit correctly and universally readable.
// ─────────────────────────────────────────────────────────────────────────────

#include "PdfObject.h"

#include <QString>
#include <functional>
#include <map>

namespace NativeOffice::Pdf {

// Renders a single object's PDF syntax (dict/array/name/string/number/ref/
// stream). Any Ref encountered is translated through `remap` (old→new object
// number) — callers build that map before serializing.
QByteArray serializeObjectBody(const Object& obj, const std::function<int(Ref)>& remap);

// Escapes a name's bytes back into valid PDF /Name syntax (#XX for anything
// that isn't a safe printable, non-delimiter character).
QByteArray escapePdfName(const QByteArray& raw);

class Writer {
public:
    // Reserves and returns the next free object number (starts at 1).
    int allocate();

    // Associates a fully-rendered object body (e.g. "<< /Type /Page ... >>"
    // or a stream's dict+data) with a previously allocated object number.
    void setObjectBody(int objNum, const QByteArray& body);

    // Serializes the whole file into a byte buffer (header, all registered
    // objects, classic xref, trailer). Exposed so the signing path can build
    // the exact bytes, patch the /ByteRange + /Contents placeholders, and
    // then write — signatures must be computed over the final layout.
    [[nodiscard]] QByteArray buildBuffer(int rootObjNum) const;

    // Serializes the whole file: header, all registered objects, a classic
    // xref table, and a trailer pointing at `rootObjNum`. Returns false on
    // I/O failure (caller should treat that as "could not write output").
    bool writeTo(const QString& path, int rootObjNum) const;

    // Encryption trailer: when `encryptObjNum` > 0, the trailer references it
    // as /Encrypt and includes /ID [<idBytes><idBytes>]. The referenced
    // object (the standard security handler dict) must be registered by the
    // caller. `idBytes` is the raw file identifier (typically 16 bytes).
    void setEncryptTrailer(int encryptObjNum, const QByteArray& idBytes) {
        m_encryptObjNum = encryptObjNum;
        m_idBytes = idBytes;
    }

private:
    int m_next = 1;
    std::map<int, QByteArray> m_bodies;
    int m_encryptObjNum = 0;
    QByteArray m_idBytes;
};

} // namespace NativeOffice::Pdf
