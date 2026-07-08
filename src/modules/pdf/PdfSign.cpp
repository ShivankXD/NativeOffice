// ─────────────────────────────────────────────────────────────────────────────
// PdfSign.cpp — see PdfSign.h. Certificate signatures via Windows crypt32.
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfSign.h"
#include "PdfRebuild.h"

#include <QFile>

#include <algorithm>
#include <string>

#ifdef _WIN32
#  include <windows.h>
#  include <wincrypt.h>
#  pragma comment(lib, "crypt32.lib")
#  pragma comment(lib, "bcrypt.lib")
#  include <bcrypt.h>
#endif

namespace NativeOffice::Pdf {

using namespace NativeOffice::Pdf::rebuild;

namespace {

// Reserved space for the DER PKCS#7 (hex chars). ~8 KB covers a signer cert
// plus a small chain comfortably.
constexpr int kContentsHexLen = 16384;

#ifdef _WIN32

QDateTime fromFileTime(const FILETIME& ft) {
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    // FILETIME is 100-ns since 1601-01-01; convert to ms since 1970.
    const qint64 ms = qint64((u.QuadPart - 116444736000000000ULL) / 10000ULL);
    return QDateTime::fromMSecsSinceEpoch(ms, Qt::UTC);
}

QString certName(PCCERT_CONTEXT ctx, DWORD type, DWORD flags) {
    const DWORD n = CertGetNameStringW(ctx, type, flags, nullptr, nullptr, 0);
    if (n <= 1) return {};
    std::wstring buf(n, L'\0');
    CertGetNameStringW(ctx, type, flags, nullptr, buf.data(), n);
    return QString::fromWCharArray(buf.c_str());
}

QString thumbprintOf(PCCERT_CONTEXT ctx) {
    BYTE hash[20]; DWORD cb = sizeof(hash);
    if (!CertGetCertificateContextProperty(ctx, CERT_HASH_PROP_ID, hash, &cb)) return {};
    return QByteArray(reinterpret_cast<char*>(hash), int(cb)).toHex().toUpper();
}

// Finds a cert in CurrentUser\My by SHA-1 thumbprint. Caller frees with
// CertFreeCertificateContext.
PCCERT_CONTEXT findCertByThumbprint(const QString& thumbHex) {
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
        CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_READONLY_FLAG, L"MY");
    if (!store) return nullptr;
    const QByteArray want = thumbHex.toUpper().toLatin1();
    PCCERT_CONTEXT ctx = nullptr, found = nullptr;
    while ((ctx = CertEnumCertificatesInStore(store, ctx)) != nullptr) {
        if (thumbprintOf(ctx).toLatin1() == want) {
            found = CertDuplicateCertificateContext(ctx);
            break;
        }
    }
    if (ctx) CertFreeCertificateContext(ctx);
    CertCloseStore(store, 0);
    return found;
}

QByteArray sha256(const QByteArray& data) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
    QByteArray out(32, 0);
    BCryptHash(hAlg, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(data.constData())), ULONG(data.size()),
        reinterpret_cast<PUCHAR>(out.data()), 32);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return out;
}

// Detached PKCS#7 SignedData over `content` with the given cert (SHA-256).
QByteArray pkcs7Sign(PCCERT_CONTEXT cert, const QByteArray& content, bool* ok) {
    *ok = false;
    CRYPT_SIGN_MESSAGE_PARA para{};
    para.cbSize = sizeof(para);
    para.dwMsgEncodingType = PKCS_7_ASN_ENCODING | X509_ASN_ENCODING;
    para.pSigningCert = cert;
    para.HashAlgorithm.pszObjId = const_cast<LPSTR>(szOID_NIST_sha256);
    para.cMsgCert = 1;
    para.rgpMsgCert = &cert;

    const BYTE* msgs[1] = { reinterpret_cast<const BYTE*>(content.constData()) };
    DWORD sizes[1] = { DWORD(content.size()) };

    DWORD cb = 0;
    if (!CryptSignMessage(&para, TRUE, 1, msgs, sizes, nullptr, &cb)) return {};
    QByteArray sig(int(cb), 0);
    if (!CryptSignMessage(&para, TRUE, 1, msgs, sizes,
                          reinterpret_cast<BYTE*>(sig.data()), &cb)) return {};
    sig.resize(int(cb));
    *ok = true;
    return sig;
}

// PDF literal string escaping for /Reason etc.
QByteArray litStr(const QString& s) {
    QByteArray out = "(";
    for (char c : s.toUtf8()) {
        if (c == '\\' || c == '(' || c == ')') out += '\\';
        out += c;
    }
    out += ")";
    return out;
}

Object refToN(int n) { return Object::makeRef(Ref{ -n, 0 }); }

// Shared rebuild that inserts a signature field (widget) on `opts.page` and a
// fresh AcroForm, leaving a manual sig-value dict body with placeholders at
// object number `sigValueNum` for the caller to patch. Returns the built
// buffer, or empty on failure.
QByteArray buildSignedBuffer(const Document& doc, const SignOptions& opts,
                             const QByteArray& subFilter, int& outByteRangePos,
                             int& outContentsPos) {
    Writer writer;
    const int pagesNum   = writer.allocate();
    const int catalogNum = writer.allocate();
    const int sigValueNum = writer.allocate();
    const int sigFieldNum = writer.allocate();
    const int acroFormNum = writer.allocate();
    Copier copier(doc, writer);

    const int n = doc.pageCount();
    const int page = std::clamp(opts.page, 0, n - 1);
    std::vector<int> newPageNums(size_t(n), 0);
    for (int i = 0; i < n; ++i) {
        newPageNums[size_t(i)] = writer.allocate();
        copier.mapPage(doc.pages()[size_t(i)].ref, newPageNums[size_t(i)]);
    }
    for (int i = 0; i < n; ++i) {
        Object dict = doc.pages()[size_t(i)].dict;
        if (i == page) {
            std::vector<Object> annots;
            if (const Object* ex = dict.find("Annots")) {
                const Object& arr = doc.resolve(*ex);
                if (arr.isArray()) for (const Object& e : arr.arr) annots.push_back(e);
            }
            annots.push_back(refToN(sigFieldNum));
            dict.dict.insert("Annots", Object::makeArray(std::move(annots)));
        }
        dict.dict.insert("Parent", refToN(pagesNum));
        writer.setObjectBody(newPageNums[size_t(i)],
            serializeObjectBody(dict, [&copier](Ref r) { return copier.copy(r); }));
    }

    // Pages node + catalog (+ AcroForm).
    auto sentinel = [](Ref r) { return r.num < 0 ? -r.num : 0; };
    Object pages; pages.type = Object::Type::Dict;
    pages.dict.insert("Type", Object::makeName("Pages"));
    std::vector<Object> kids;
    for (int k : newPageNums) kids.push_back(refToN(k));
    pages.dict.insert("Kids", Object::makeArray(std::move(kids)));
    pages.dict.insert("Count", Object::makeInt(qint64(newPageNums.size())));
    writer.setObjectBody(pagesNum, serializeObjectBody(pages, sentinel));

    Object cat; cat.type = Object::Type::Dict;
    cat.dict.insert("Type", Object::makeName("Catalog"));
    cat.dict.insert("Pages", refToN(pagesNum));
    cat.dict.insert("AcroForm", refToN(acroFormNum));
    static const char* kKeep[] = { "Outlines", "Names", "PageLabels", "Lang",
                                   "ViewerPreferences", "PageMode", "PageLayout" };
    const Object& srcCat = doc.catalog();
    if (srcCat.isDict())
        for (const char* key : kKeep)
            if (const Object* v = srcCat.find(key); v && v->isRef())
                cat.dict.insert(key, refToN(copier.copy(v->ref)));
    writer.setObjectBody(catalogNum, serializeObjectBody(cat, sentinel));

    // AcroForm with SigFlags = 3 (signatures exist + append-only).
    Object acro; acro.type = Object::Type::Dict;
    std::vector<Object> fields; fields.push_back(refToN(sigFieldNum));
    acro.dict.insert("Fields", Object::makeArray(std::move(fields)));
    acro.dict.insert("SigFlags", Object::makeInt(3));
    writer.setObjectBody(acroFormNum, serializeObjectBody(acro, sentinel));

    // Signature widget/field.
    double bx, by, bw, bh; mediaBoxOf(doc, doc.pages()[size_t(page)].dict, bx, by, bw, bh);
    QByteArray rectStr = "[0 0 0 0]";
    if (opts.visible && !opts.rect.isNull()) {
        const double x0 = opts.rect.left(), x1 = opts.rect.right();
        const double y1 = bh - opts.rect.top(), y0 = bh - opts.rect.bottom();
        rectStr = "[" + QByteArray::number(x0, 'f', 2) + " " + QByteArray::number(y0, 'f', 2)
                + " " + QByteArray::number(x1, 'f', 2) + " " + QByteArray::number(y1, 'f', 2) + "]";
    }
    QByteArray fieldBody = "<< /Type /Annot /Subtype /Widget /FT /Sig /T (Signature1) "
                           "/F 132 /Rect " + rectStr + " /P " + QByteArray::number(newPageNums[size_t(page)])
                         + " 0 R /V " + QByteArray::number(sigValueNum) + " 0 R >>";
    writer.setObjectBody(sigFieldNum, fieldBody);

    // Signature value dict — MANUAL body with fixed-width placeholders so the
    // caller can patch /ByteRange and /Contents after the buffer is laid out.
    // The signer name is already resolved into `opts` by the caller.
    const QByteArray zeros(kContentsHexLen, '0');
    QByteArray sigBody = "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /" + subFilter
        + " /ByteRange [0 0000000000 0000000000 0000000000]"
        + " /Contents <" + zeros + ">"
        + " /M " + litStr("D:" + QDateTime::currentDateTimeUtc().toString("yyyyMMddHHmmss") + "Z")
        ;
    if (!opts.reason.isEmpty())    sigBody += " /Reason " + litStr(opts.reason);
    if (!opts.location.isEmpty())  sigBody += " /Location " + litStr(opts.location);
    if (!opts.contactInfo.isEmpty()) sigBody += " /ContactInfo " + litStr(opts.contactInfo);
    if (!opts.signerName.isEmpty()) sigBody += " /Name " + litStr(opts.signerName);
    sigBody += " >>";
    writer.setObjectBody(sigValueNum, sigBody);

    QByteArray buffer = writer.buildBuffer(catalogNum);

    // Locate the placeholders in the final layout.
    outByteRangePos = buffer.indexOf("/ByteRange [0 0000000000");
    const int contentsKey = buffer.indexOf("/Contents <");
    outContentsPos = contentsKey < 0 ? -1 : buffer.indexOf('<', contentsKey);
    return buffer;
}

#endif  // _WIN32

} // namespace

bool signingSupported() {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

std::vector<CertInfo> listCertificates() {
    std::vector<CertInfo> out;
#ifdef _WIN32
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
        CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_READONLY_FLAG, L"MY");
    if (!store) return out;
    PCCERT_CONTEXT ctx = nullptr;
    while ((ctx = CertEnumCertificatesInStore(store, ctx)) != nullptr) {
        CertInfo info;
        info.subject = certName(ctx, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0);
        info.issuer  = certName(ctx, CERT_NAME_SIMPLE_DISPLAY_TYPE, CERT_NAME_ISSUER_FLAG);
        info.notBefore = fromFileTime(ctx->pCertInfo->NotBefore);
        info.notAfter  = fromFileTime(ctx->pCertInfo->NotAfter);
        info.thumbprintHex = thumbprintOf(ctx);
        DWORD spec = 0; BOOL freeIt = FALSE; HCRYPTPROV_OR_NCRYPT_KEY_HANDLE key = 0;
        if (CryptAcquireCertificatePrivateKey(ctx,
                CRYPT_ACQUIRE_SILENT_FLAG | CRYPT_ACQUIRE_ALLOW_NCRYPT_KEY_FLAG,
                nullptr, &key, &spec, &freeIt)) {
            info.hasPrivateKey = true;
            if (freeIt) {
                if (spec == CERT_NCRYPT_KEY_SPEC) NCryptFreeObject(key);
                else CryptReleaseContext(key, 0);
            }
        }
        out.push_back(info);
    }
    CertCloseStore(store, 0);
#endif
    return out;
}

OpResult signPdf(const QString& in, const QString& out, const SignOptions& opts) {
#ifndef _WIN32
    return { false, "Signing is only available on Windows in this build." };
#else
    PCCERT_CONTEXT cert = findCertByThumbprint(opts.thumbprintHex);
    if (!cert)
        return { false, "The selected certificate could not be found." };

    // Confirm we can access its private key before doing all the work.
    DWORD spec = 0; BOOL freeIt = FALSE; HCRYPTPROV_OR_NCRYPT_KEY_HANDLE key = 0;
    if (!CryptAcquireCertificatePrivateKey(cert,
            CRYPT_ACQUIRE_SILENT_FLAG | CRYPT_ACQUIRE_ALLOW_NCRYPT_KEY_FLAG,
            nullptr, &key, &spec, &freeIt)) {
        CertFreeCertificateContext(cert);
        return { false, "The certificate has no accessible private key for signing." };
    }
    if (freeIt) {
        if (spec == CERT_NCRYPT_KEY_SPEC) NCryptFreeObject(key);
        else CryptReleaseContext(key, 0);
    }

    OpenStatus status;
    auto doc = Document::open(in, status);
    if (!doc) {
        CertFreeCertificateContext(cert);
        return { false, QString("This PDF isn't supported yet: %1").arg(openStatusReason(status)) };
    }

    // Default the signer name to the certificate's subject CN.
    SignOptions effective = opts;
    if (effective.signerName.isEmpty())
        effective.signerName = certName(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0);

    int brPos = -1, cPos = -1;
    QByteArray buffer = buildSignedBuffer(*doc, effective, "adbe.pkcs7.detached", brPos, cPos);
    if (buffer.isEmpty() || brPos < 0 || cPos < 0) {
        CertFreeCertificateContext(cert);
        return { false, "Could not lay out the signed document." };
    }

    // /Contents spans '<' … '>' at [cPos, cPos + kContentsHexLen + 1].
    const int gapStart = cPos;
    const int gapLen   = kContentsHexLen + 2;               // include both brackets
    const int total    = buffer.size();
    const int a0 = 0, l0 = gapStart;
    const int a1 = gapStart + gapLen, l1 = total - a1;

    // Patch /ByteRange (fixed 10-digit fields; length preserved).
    auto pad10 = [](int v) { return QByteArray::number(v).rightJustified(10, '0'); };
    const QByteArray br = "/ByteRange [0 " + pad10(l0) + " " + pad10(a1) + " " + pad10(l1);
    // Original placeholder is "/ByteRange [0 0000000000 0000000000 0000000000"
    const QByteArray brPlaceholder = "/ByteRange [0 0000000000 0000000000 0000000000";
    if (buffer.mid(brPos, brPlaceholder.size()) != brPlaceholder) {
        CertFreeCertificateContext(cert);
        return { false, "Internal error: signature layout mismatch (ByteRange)." };
    }
    buffer.replace(brPos, brPlaceholder.size(), br);

    // Sign the two covered segments.
    QByteArray signedBytes = buffer.left(l0) + buffer.mid(a1, l1);
    bool sigOk = false;
    const QByteArray der = pkcs7Sign(cert, signedBytes, &sigOk);
    CertFreeCertificateContext(cert);
    if (!sigOk || der.isEmpty())
        return { false, "The certificate could not produce a signature." };
    if (der.size() * 2 > kContentsHexLen)
        return { false, "The signature is larger than the reserved space." };

    // Patch /Contents hex (zero-padded to the reserved width).
    QByteArray hex = der.toHex().toUpper();
    hex = hex.leftJustified(kContentsHexLen, '0');
    buffer.replace(cPos + 1, kContentsHexLen, hex);

    QFile f(out);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return { false, "Could not write the signed file." };
    f.write(buffer);
    f.close();
    return { true, {} };
#endif
}

OpResult timestampPdf(const QString& in, const QString& out, const QString& tsaUrl) {
#ifndef _WIN32
    return { false, "Timestamping is only available on Windows in this build." };
#else
    if (tsaUrl.isEmpty())
        return { false, "Enter a Time Stamping Authority (TSA) URL." };

    OpenStatus status;
    auto doc = Document::open(in, status);
    if (!doc)
        return { false, QString("This PDF isn't supported yet: %1").arg(openStatusReason(status)) };

    SignOptions opts;                          // an invisible doc-timestamp field
    int brPos = -1, cPos = -1;
    QByteArray buffer = buildSignedBuffer(*doc, opts, "ETSI.RFC3161", brPos, cPos);
    if (buffer.isEmpty() || brPos < 0 || cPos < 0)
        return { false, "Could not lay out the timestamped document." };

    const int gapStart = cPos, gapLen = kContentsHexLen + 2, total = buffer.size();
    const int l0 = gapStart, a1 = gapStart + gapLen, l1 = total - a1;
    auto pad10 = [](int v) { return QByteArray::number(v).rightJustified(10, '0'); };
    const QByteArray brPlaceholder = "/ByteRange [0 0000000000 0000000000 0000000000";
    buffer.replace(brPos, brPlaceholder.size(),
                   "/ByteRange [0 " + pad10(l0) + " " + pad10(a1) + " " + pad10(l1));

    const QByteArray signedBytes = buffer.left(l0) + buffer.mid(a1, l1);
    const QByteArray digest = sha256(signedBytes);

    // Request an RFC-3161 token over the digest.
    const std::wstring wurl = tsaUrl.toStdWString();
    PCRYPT_TIMESTAMP_CONTEXT tsCtx = nullptr;
    CRYPT_TIMESTAMP_PARA tsPara{};
    tsPara.fRequestCerts = TRUE;
    const BOOL ok = CryptRetrieveTimeStamp(wurl.c_str(), TIMESTAMP_NO_AUTH_RETRIEVAL,
        15000, szOID_NIST_sha256, &tsPara,
        reinterpret_cast<const BYTE*>(digest.constData()), DWORD(digest.size()),
        &tsCtx, nullptr, nullptr);
    if (!ok || !tsCtx)
        return { false, "The timestamp authority did not respond. Check the URL and your connection." };

    QByteArray token(reinterpret_cast<const char*>(tsCtx->pbEncoded), int(tsCtx->cbEncoded));
    CryptMemFree(tsCtx);

    if (token.size() * 2 > kContentsHexLen)
        return { false, "The timestamp token is larger than the reserved space." };
    QByteArray hex = token.toHex().toUpper().leftJustified(kContentsHexLen, '0');
    buffer.replace(cPos + 1, kContentsHexLen, hex);

    QFile f(out);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return { false, "Could not write the timestamped file." };
    f.write(buffer);
    f.close();
    return { true, {} };
#endif
}

std::vector<SignatureStatus> validateSignatures(const QString& path) {
    std::vector<SignatureStatus> out;
#ifdef _WIN32
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QByteArray fileBytes = f.readAll();
    f.close();

    OpenStatus status;
    auto doc = Document::open(path, status);
    if (!doc) return out;

    const Object& cat = doc->catalog();
    const Object* afRef = cat.find("AcroForm");
    if (!afRef) return out;
    const Object& acro = doc->resolve(*afRef);
    const Object* fields = acro.find("Fields");
    if (!fields) return out;
    const Object& fieldsArr = doc->resolve(*fields);
    if (!fieldsArr.isArray()) return out;

    for (const Object& fref : fieldsArr.arr) {
        const Object& field = doc->resolve(fref);
        const Object* ft = field.find("FT");
        if (!ft || ft->asName() != "Sig") continue;
        const Object* vref = field.find("V");
        if (!vref) continue;
        const Object& sig = doc->resolve(*vref);
        if (!sig.isDict()) continue;

        SignatureStatus st;
        if (const Object* sub = sig.find("SubFilter"))
            st.isTimestamp = sub->asName() == "ETSI.RFC3161";
        if (const Object* nm = sig.find("Name")) st.signerName = QString::fromUtf8(doc->resolve(*nm).strVal);
        if (const Object* rs = sig.find("Reason")) st.reason = QString::fromUtf8(doc->resolve(*rs).strVal);
        if (const Object* lc = sig.find("Location")) st.location = QString::fromUtf8(doc->resolve(*lc).strVal);

        const Object* br = sig.find("ByteRange");
        const Object* co = sig.find("Contents");
        if (!br || !co) { st.summary = "Malformed signature."; out.push_back(st); continue; }
        const Object& bra = doc->resolve(*br);
        if (!bra.isArray() || bra.arr.size() != 4) { st.summary = "Malformed byte range."; out.push_back(st); continue; }
        auto rv = [&](int i) { return int(doc->resolve(bra.arr[size_t(i)]).asInt()); };
        const int a0 = rv(0), l0 = rv(1), a1 = rv(2), l1 = rv(3);

        QByteArray signedBytes;
        if (a0 >= 0 && l0 >= 0 && a1 >= 0 && l1 >= 0 && a0 + l0 <= fileBytes.size()
            && a1 + l1 <= fileBytes.size())
            signedBytes = fileBytes.mid(a0, l0) + fileBytes.mid(a1, l1);

        const QByteArray der = doc->resolve(*co).strVal;   // raw PKCS#7/token bytes

        if (st.isTimestamp) {
            // Verify the RFC-3161 token's messageImprint over the digest.
            const QByteArray digest = sha256(signedBytes);
            PCRYPT_TIMESTAMP_CONTEXT ctx = nullptr;
            const BOOL ok = CryptVerifyTimeStampSignature(
                reinterpret_cast<const BYTE*>(der.constData()), DWORD(der.size()),
                reinterpret_cast<const BYTE*>(digest.constData()), DWORD(digest.size()),
                nullptr, &ctx, nullptr, nullptr);
            st.digestValid = ok && ctx;
            st.certTrusted = ok;   // TSA token carries its own trust
            if (ctx) {
                st.signedAt = fromFileTime(ctx->pTimeStamp->ftTime);
                CryptMemFree(ctx);
            }
            st.summary = st.digestValid ? "Valid timestamp." : "Timestamp could not be verified.";
            out.push_back(st);
            continue;
        }

        // Approval signature: verify the detached PKCS#7 over the byte range.
        CRYPT_VERIFY_MESSAGE_PARA vpara{};
        vpara.cbSize = sizeof(vpara);
        vpara.dwMsgAndCertEncodingType = PKCS_7_ASN_ENCODING | X509_ASN_ENCODING;
        const BYTE* content[1] = { reinterpret_cast<const BYTE*>(signedBytes.constData()) };
        DWORD contentSize[1] = { DWORD(signedBytes.size()) };
        PCCERT_CONTEXT signer = nullptr;
        const BOOL vok = CryptVerifyDetachedMessageSignature(&vpara, 0,
            reinterpret_cast<const BYTE*>(der.constData()), DWORD(der.size()),
            1, content, contentSize, &signer);
        st.digestValid = vok;

        if (signer) {
            if (st.signerName.isEmpty())
                st.signerName = certName(signer, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0);
            CERT_CHAIN_PARA chainPara{};
            chainPara.cbSize = sizeof(chainPara);
            PCCERT_CHAIN_CONTEXT chain = nullptr;
            if (CertGetCertificateChain(nullptr, signer, nullptr, nullptr, &chainPara,
                    CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT, nullptr, &chain)) {
                st.certTrusted = chain->TrustStatus.dwErrorStatus == CERT_TRUST_NO_ERROR;
                CertFreeCertificateChain(chain);
            }
            CertFreeCertificateContext(signer);
        }

        st.summary = !st.digestValid ? "Invalid — the document was altered after signing."
                   : st.certTrusted  ? "Valid — signature and certificate are trusted."
                                     : "Signed, but the certificate chain is not trusted.";
        out.push_back(st);
    }
#endif
    return out;
}

} // namespace NativeOffice::Pdf
