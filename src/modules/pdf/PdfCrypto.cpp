// ─────────────────────────────────────────────────────────────────────────────
// PdfCrypto.cpp — see PdfCrypto.h. AES-256 (V5/R6) standard security handler
// built on Windows CNG (bcrypt). Reference: ISO 32000-2, §7.6.4.3
// (Algorithms 2.A / 2.B / 8 / 9 / 10).
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfCrypto.h"
#include "PdfRebuild.h"

#include <QFile>

#include <array>
#include <cstring>

#ifdef _WIN32
#  define WIN32_NO_STATUS
#  include <windows.h>
#  undef WIN32_NO_STATUS
#  include <bcrypt.h>
#  pragma comment(lib, "bcrypt.lib")
#endif

namespace NativeOffice::Pdf {

using namespace NativeOffice::Pdf::rebuild;

namespace {

#ifdef _WIN32

QByteArray cngRandom(int n) {
    QByteArray out(n, 0);
    BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(out.data()), ULONG(n),
                    BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return out;
}

// One-shot hash. bits ∈ {256, 384, 512}.
QByteArray cngSha(int bits, const QByteArray& data) {
    LPCWSTR alg = bits == 256 ? BCRYPT_SHA256_ALGORITHM
                : bits == 384 ? BCRYPT_SHA384_ALGORITHM
                              : BCRYPT_SHA512_ALGORITHM;
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (BCryptOpenAlgorithmProvider(&hAlg, alg, nullptr, 0) != 0) return {};
    QByteArray out(bits / 8, 0);
    const NTSTATUS st = BCryptHash(hAlg, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(data.constData())), ULONG(data.size()),
        reinterpret_cast<PUCHAR>(out.data()), ULONG(out.size()));
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return st == 0 ? out : QByteArray();
}

// AES-CBC (encrypt only). pkcs7=false requires data length a multiple of 16.
QByteArray cngAesCbc(const QByteArray& key, const QByteArray& iv,
                     const QByteArray& data, bool pkcs7) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) return {};
    BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
        sizeof(BCRYPT_CHAIN_MODE_CBC), 0);

    BCRYPT_KEY_HANDLE hKey = nullptr;
    if (BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0,
            reinterpret_cast<PUCHAR>(const_cast<char*>(key.constData())),
            ULONG(key.size()), 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }

    QByteArray ivCopy = iv;                          // BCryptEncrypt mutates the IV
    const ULONG flags = pkcs7 ? BCRYPT_BLOCK_PADDING : 0;
    ULONG cb = 0;
    BCryptEncrypt(hKey,
        reinterpret_cast<PUCHAR>(const_cast<char*>(data.constData())), ULONG(data.size()),
        nullptr, reinterpret_cast<PUCHAR>(ivCopy.data()), ULONG(ivCopy.size()),
        nullptr, 0, &cb, flags);
    QByteArray out(int(cb), 0);
    ULONG written = 0;
    const NTSTATUS st = BCryptEncrypt(hKey,
        reinterpret_cast<PUCHAR>(const_cast<char*>(data.constData())), ULONG(data.size()),
        nullptr, reinterpret_cast<PUCHAR>(ivCopy.data()), ULONG(ivCopy.size()),
        reinterpret_cast<PUCHAR>(out.data()), cb, &written, flags);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    if (st != 0) return {};
    out.resize(int(written));
    return out;
}

// AES-256-ECB, no padding, single block (used for /Perms).
QByteArray cngAesEcbNoPad(const QByteArray& key, const QByteArray& data) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) return {};
    BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_ECB)),
        sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
    BCRYPT_KEY_HANDLE hKey = nullptr;
    if (BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0,
            reinterpret_cast<PUCHAR>(const_cast<char*>(key.constData())),
            ULONG(key.size()), 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }
    ULONG cb = 0;
    BCryptEncrypt(hKey, reinterpret_cast<PUCHAR>(const_cast<char*>(data.constData())),
        ULONG(data.size()), nullptr, nullptr, 0, nullptr, 0, &cb, 0);
    QByteArray out(int(cb), 0);
    ULONG written = 0;
    const NTSTATUS st = BCryptEncrypt(hKey,
        reinterpret_cast<PUCHAR>(const_cast<char*>(data.constData())), ULONG(data.size()),
        nullptr, nullptr, 0, reinterpret_cast<PUCHAR>(out.data()), cb, &written, 0);
    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    if (st != 0) return {};
    out.resize(int(written));
    return out;
}

// ISO 32000-2 Algorithm 2.B — the R6 password hash.
QByteArray hashR6(const QByteArray& password, const QByteArray& salt, const QByteArray& udata) {
    QByteArray K = cngSha(256, password + salt + udata);
    for (int round = 0; ; ++round) {
        // K1 = 64 repetitions of (password || K || udata).
        QByteArray block = password + K + udata;
        QByteArray K1;
        K1.reserve(block.size() * 64);
        for (int i = 0; i < 64; ++i) K1 += block;

        // E = AES-128-CBC-NoPad, key = K[0:16], iv = K[16:32].
        const QByteArray E = cngAesCbc(K.left(16), K.mid(16, 16), K1, /*pkcs7=*/false);
        if (E.isEmpty()) return {};

        // Next hash chosen by (first 16 bytes as big-endian int) mod 3, which
        // equals (sum of those bytes) mod 3 since 256 ≡ 1 (mod 3).
        int sum = 0;
        for (int i = 0; i < 16; ++i) sum += static_cast<unsigned char>(E[i]);
        const int mod = sum % 3;
        K = cngSha(mod == 0 ? 256 : mod == 1 ? 384 : 512, E);

        // Terminate after ≥64 rounds once the last byte of E is small enough.
        if (round >= 63 && static_cast<unsigned char>(E.back()) <= round - 32)
            break;
    }
    return K.left(32);
}

// Everything the /Encrypt dict + the object encryptor need.
struct CryptState {
    QByteArray fileKey;        // 32 bytes
    QByteArray O, U;           // 48 bytes each
    QByteArray OE, UE;         // 32 bytes each
    QByteArray perms;          // 16 bytes
    qint32     P = 0;
    QByteArray id;             // 16 bytes
    bool ok = false;
};

CryptState buildCryptState(const EncryptOptions& opts) {
    CryptState s;
    s.fileKey = cngRandom(32);
    s.id      = cngRandom(16);
    if (s.fileKey.isEmpty()) return s;

    const QByteArray userPwd  = opts.userPassword.toUtf8().left(127);
    const QByteArray ownerPwd = (opts.ownerPassword.isEmpty() ? opts.userPassword
                                                              : opts.ownerPassword).toUtf8().left(127);

    // ── User password (Algorithm 8) ─────────────────────────────────────
    const QByteArray uValSalt = cngRandom(8);
    const QByteArray uKeySalt = cngRandom(8);
    const QByteArray uHash = hashR6(userPwd, uValSalt, {});
    if (uHash.isEmpty()) return s;
    s.U = uHash + uValSalt + uKeySalt;                         // 48 bytes
    const QByteArray uInter = hashR6(userPwd, uKeySalt, {});
    s.UE = cngAesCbc(uInter, QByteArray(16, '\0'), s.fileKey, /*pkcs7=*/false);

    // ── Owner password (Algorithm 9 — hashes include U) ─────────────────
    const QByteArray oValSalt = cngRandom(8);
    const QByteArray oKeySalt = cngRandom(8);
    const QByteArray oHash = hashR6(ownerPwd, oValSalt, s.U);
    if (oHash.isEmpty()) return s;
    s.O = oHash + oValSalt + oKeySalt;                         // 48 bytes
    const QByteArray oInter = hashR6(ownerPwd, oKeySalt, s.U);
    s.OE = cngAesCbc(oInter, QByteArray(16, '\0'), s.fileKey, /*pkcs7=*/false);

    // ── Permissions (/P) ────────────────────────────────────────────────
    constexpr quint32 PRINT = 1u << 2, MODIFY = 1u << 3, COPY = 1u << 4, ANNOT = 1u << 5,
                      FILLFORM = 1u << 8, EXTRACT_A11Y = 1u << 9, ASSEMBLE = 1u << 10,
                      PRINT_HIGH = 1u << 11;
    quint32 p = 0xFFFFFFFCu;               // reserved high bits set; bits 1,2 clear
    p |= MODIFY | EXTRACT_A11Y;            // keep these enabled
    if (!opts.allowPrinting)   p &= ~(PRINT | PRINT_HIGH);
    if (!opts.allowCopying)    p &= ~COPY;
    if (!opts.allowCommenting) p &= ~ANNOT;
    if (!opts.allowFormFill)   p &= ~FILLFORM;
    if (!opts.allowAssembly)   p &= ~ASSEMBLE;
    s.P = static_cast<qint32>(p);

    // ── /Perms (Algorithm 10): AES-256-ECB(fileKey, permsBlock) ─────────
    QByteArray block(16, '\0');
    block[0] = char(p & 0xFF);
    block[1] = char((p >> 8) & 0xFF);
    block[2] = char((p >> 16) & 0xFF);
    block[3] = char((p >> 24) & 0xFF);
    block[4] = char(0xFF); block[5] = char(0xFF); block[6] = char(0xFF); block[7] = char(0xFF);
    block[8] = 'T';                        // EncryptMetadata = true
    block[9] = 'a'; block[10] = 'd'; block[11] = 'b';
    const QByteArray rnd = cngRandom(4);
    for (int i = 0; i < 4; ++i) block[12 + i] = rnd[i];
    s.perms = cngAesEcbNoPad(s.fileKey, block);

    s.ok = !s.U.isEmpty() && !s.UE.isEmpty() && !s.O.isEmpty() && !s.OE.isEmpty()
           && s.perms.size() == 16;
    return s;
}

// Encrypts every string/stream reachable in an object with AES-256-CBC
// (16-byte random IV prepended, PKCS#7 padding) using the single file key.
struct StandardEncryptor : ObjectEncryptor {
    QByteArray fileKey;

    QByteArray enc(const QByteArray& data) {
        const QByteArray iv = cngRandom(16);
        return iv + cngAesCbc(fileKey, iv, data, /*pkcs7=*/true);
    }
    void walk(Object& o) {
        switch (o.type) {
        case Object::Type::String:
            o.strVal = enc(o.strVal);
            break;
        case Object::Type::Stream:
            o.streamData = enc(o.streamData);
            for (auto it = o.dict.begin(); it != o.dict.end(); ++it) walk(it.value());
            break;
        case Object::Type::Dict:
            for (auto it = o.dict.begin(); it != o.dict.end(); ++it) walk(it.value());
            break;
        case Object::Type::Array:
            for (Object& e : o.arr) walk(e);
            break;
        default: break;
        }
    }
    void encrypt(Object& obj, int /*objNum*/) override { walk(obj); }
};

Object nameObj(const char* n) { return Object::makeName(n); }
Object strObj(const QByteArray& raw) { Object o; o.type = Object::Type::String; o.strVal = raw; return o; }

#endif  // _WIN32

} // namespace

bool encryptionSupported() {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

OpResult encryptDocument(const QString& in, const QString& out, const EncryptOptions& opts) {
#ifndef _WIN32
    return { false, "Encryption is only available on Windows in this build." };
#else
    if (opts.userPassword.isEmpty() && opts.ownerPassword.isEmpty())
        return { false, "Set at least one password." };

    OpenStatus status;
    auto doc = Document::open(in, status);
    if (!doc)
        return { false, QString("This PDF isn't supported yet: %1").arg(openStatusReason(status)) };

    const CryptState cs = buildCryptState(opts);
    if (!cs.ok)
        return { false, "Could not initialize encryption (CNG failure)." };

    Writer writer;
    const int pagesNum   = writer.allocate();
    const int catalogNum = writer.allocate();
    const int encryptNum = writer.allocate();

    StandardEncryptor enc;
    enc.fileKey = cs.fileKey;
    Copier copier(*doc, writer, &enc);

    // Pre-map pages so annotation/field back-references resolve to the copies.
    const int n = doc->pageCount();
    std::vector<int> newPageNums(size_t(n), 0);
    for (int i = 0; i < n; ++i) {
        newPageNums[size_t(i)] = writer.allocate();
        copier.mapPage(doc->pages()[size_t(i)].ref, newPageNums[size_t(i)]);
    }
    for (int i = 0; i < n; ++i) {
        Object dict = doc->pages()[size_t(i)].dict;
        dict.dict.insert("Parent", Object::makeRef(Ref{ -pagesNum, 0 }));
        enc.encrypt(dict, newPageNums[size_t(i)]);          // encrypt any page-level strings
        writer.setObjectBody(newPageNums[size_t(i)],
            serializeObjectBody(dict, [&copier](Ref r) { return copier.copy(r); }));
    }

    // Pages node + catalog (no direct strings → not encrypted).
    auto sentinelOnly = [](Ref r) { return r.num < 0 ? -r.num : 0; };
    Object pages; pages.type = Object::Type::Dict;
    pages.dict.insert("Type", nameObj("Pages"));
    std::vector<Object> kids;
    for (int k : newPageNums) kids.push_back(Object::makeRef(Ref{ -k, 0 }));
    pages.dict.insert("Kids", Object::makeArray(std::move(kids)));
    pages.dict.insert("Count", Object::makeInt(qint64(newPageNums.size())));
    writer.setObjectBody(pagesNum, serializeObjectBody(pages, sentinelOnly));

    Object cat; cat.type = Object::Type::Dict;
    cat.dict.insert("Type", nameObj("Catalog"));
    cat.dict.insert("Pages", Object::makeRef(Ref{ -pagesNum, 0 }));
    static const char* kKeep[] = { "Outlines", "AcroForm", "Names", "PageLabels",
                                   "Lang", "ViewerPreferences", "PageMode", "PageLayout" };
    const Object& srcCat = doc->catalog();
    if (srcCat.isDict())
        for (const char* key : kKeep)
            if (const Object* v = srcCat.find(key); v && v->isRef())
                cat.dict.insert(key, Object::makeRef(Ref{ -copier.copy(v->ref), 0 }));
    writer.setObjectBody(catalogNum, serializeObjectBody(cat, sentinelOnly));

    // /Encrypt dict — stored with LITERAL (unencrypted) string values.
    Object edict; edict.type = Object::Type::Dict;
    edict.dict.insert("Filter", nameObj("Standard"));
    edict.dict.insert("V", Object::makeInt(5));
    edict.dict.insert("R", Object::makeInt(6));
    edict.dict.insert("Length", Object::makeInt(256));
    Object stdcf; stdcf.type = Object::Type::Dict;
    stdcf.dict.insert("CFM", nameObj("AESV3"));
    stdcf.dict.insert("AuthEvent", nameObj("DocOpen"));
    stdcf.dict.insert("Length", Object::makeInt(32));
    Object cf; cf.type = Object::Type::Dict;
    cf.dict.insert("StdCF", stdcf);
    edict.dict.insert("CF", cf);
    edict.dict.insert("StmF", nameObj("StdCF"));
    edict.dict.insert("StrF", nameObj("StdCF"));
    edict.dict.insert("O", strObj(cs.O));
    edict.dict.insert("U", strObj(cs.U));
    edict.dict.insert("OE", strObj(cs.OE));
    edict.dict.insert("UE", strObj(cs.UE));
    edict.dict.insert("Perms", strObj(cs.perms));
    edict.dict.insert("P", Object::makeInt(cs.P));
    { Object em; em.type = Object::Type::Bool; em.boolVal = true;
      edict.dict.insert("EncryptMetadata", em); }
    writer.setObjectBody(encryptNum, serializeObjectBody(edict, sentinelOnly));

    writer.setEncryptTrailer(encryptNum, cs.id);
    if (!writer.writeTo(out, catalogNum))
        return { false, "Could not write the encrypted file — check the destination is writable." };

    // Light self-check: the file must exist and start with the PDF header.
    QFile f(out);
    if (!f.open(QIODevice::ReadOnly) || !f.read(5).startsWith("%PDF")) {
        f.close(); QFile::remove(out);
        return { false, "The encrypted file didn't verify and was discarded." };
    }
    f.close();
    return { true, {} };
#endif
}

} // namespace NativeOffice::Pdf
