#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfCrypto.h — the Protect tab's "Encrypt with Password".
//
// Implements the PDF 2.0 standard security handler at V5/R6 (AES-256), using
// only Windows CNG (bcrypt.dll — an OS component, zero added binary size) for
// AES / SHA-256/384/512 / secure random. No OpenSSL, no X.509/PKI toolkit.
//
// DECRYPTION of existing files isn't here: PDFium already opens password-
// protected PDFs (PdfEditSession materializes a decrypted working copy on
// open), so only the encryption direction needs a hand-rolled implementation.
//
// encryptDocument() takes an UNENCRYPTED input (the current working revision)
// and writes an AES-256 encrypted copy that opens in Acrobat/Chrome/our own
// viewer with the given password(s) and enforces the permission flags.
// ─────────────────────────────────────────────────────────────────────────────

#include "PdfOps.h"

#include <QString>

namespace NativeOffice::Pdf {

struct EncryptOptions {
    // Either or both may be set. An empty user (open) password means the file
    // opens without prompting but still enforces permissions via the owner
    // password. An empty owner password defaults to the user password.
    QString userPassword;      // "Password for opening"
    QString ownerPassword;     // "Password for editing and extracting"

    // Permissions — true = allowed. These map to the dialog's "Also restrict"
    // checkboxes (a checked restriction clears the matching allow-flag).
    bool allowPrinting   = true;   // + high-res printing
    bool allowCopying    = true;   // text/graphics extraction
    bool allowCommenting = true;   // add/edit annotations
    bool allowAssembly   = true;   // insert / delete / rotate pages
    bool allowFormFill   = true;   // fill form fields
};

// Encrypts `in` → `out`. `in` must be an unencrypted, structurally valid PDF.
OpResult encryptDocument(const QString& in, const QString& out, const EncryptOptions& opts);

// True on Windows builds where CNG is available (always true here; the guard
// exists so a future cross-platform port can report the feature as absent).
bool encryptionSupported();

} // namespace NativeOffice::Pdf
