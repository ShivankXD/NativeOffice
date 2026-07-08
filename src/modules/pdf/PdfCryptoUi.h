#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfCryptoUi.h — the "Encrypt with Password" dialog, reproducing the WPS
// reference structure: two independently-checkable password sections (open /
// edit-and-extract), each with password + confirm (6–128 chars), an "Also
// restrict" permission checklist with a Select-all toggle, and Confirm/Cancel.
//
// Returns the chosen EncryptOptions (with a flag saying whether the user
// confirmed) so the module can run encryptDocument() and write a protected
// copy.
// ─────────────────────────────────────────────────────────────────────────────

#include "PdfCrypto.h"

#include <QString>

class QWidget;

namespace NativeOffice::Pdf {

struct EncryptDialogResult {
    bool           confirmed = false;
    EncryptOptions options;
};

EncryptDialogResult runEncryptDialog(QWidget* parent);

} // namespace NativeOffice::Pdf
