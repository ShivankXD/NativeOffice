#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfSign.h — the Protect tab's certificate-based digital signatures, built
// on Windows crypt32 (already linked for DPAPI) — no OpenSSL, no added size.
//
//   listCertificates()   — enumerate the current user's "My" certificate
//                           store (Manage Certificates picker).
//   signPdf()             — embed a PKCS#7 detached signature (adbe.pkcs7.
//                           detached) over the whole document via a proper
//                           /ByteRange + /Contents two-pass, using the chosen
//                           certificate's private key (CryptSignMessage).
//   timestampPdf()        — add an RFC-3161 document timestamp (ETSI.RFC3161)
//                           from a Time Stamping Authority URL.
//   validateSignatures()  — verify every signature's digest over its byte
//                           range and its certificate chain (CertGetCertificate
//                           Chain).
// ─────────────────────────────────────────────────────────────────────────────

#include "PdfOps.h"

#include <QDateTime>
#include <QRectF>
#include <QString>
#include <vector>

namespace NativeOffice::Pdf {

struct CertInfo {
    QString subject;
    QString issuer;
    QDateTime notBefore;
    QDateTime notAfter;
    QString   thumbprintHex;      // SHA-1 thumbprint; identifies the cert to sign with
    bool      hasPrivateKey = false;
};

// Certificates in the CurrentUser\My store. Only those with a usable private
// key can sign, but all are listed (with hasPrivateKey set) so the manager
// can show them.
std::vector<CertInfo> listCertificates();

struct SignOptions {
    QString thumbprintHex;        // which certificate (from listCertificates)
    QString reason;
    QString location;
    QString contactInfo;
    QString signerName;           // /Name; defaults to the cert subject CN
    bool    visible = false;      // draw a visible signature box on page 1
    int     page = 0;
    QRectF  rect;                 // visible box, top-left origin (if visible)
};

OpResult signPdf(const QString& in, const QString& out, const SignOptions& opts);

// Adds a document-level RFC-3161 timestamp from `tsaUrl` (e.g.
// http://timestamp.digicert.com). Network round-trip via crypt32's
// CryptRetrieveTimeStamp. Fails cleanly if the TSA is unreachable.
OpResult timestampPdf(const QString& in, const QString& out, const QString& tsaUrl);

struct SignatureStatus {
    QString  signerName;
    QString  reason;
    QString  location;
    QDateTime signedAt;
    bool digestValid = false;     // the byte range hashes to the signed digest
    bool certTrusted = false;     // chain builds to a trusted root
    bool isTimestamp = false;     // a DocTimeStamp rather than an approval sig
    QString summary;              // human-readable one-liner
};

std::vector<SignatureStatus> validateSignatures(const QString& path);

bool signingSupported();          // false on non-Windows builds

} // namespace NativeOffice::Pdf
