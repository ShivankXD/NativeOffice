#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfSignUi.h — dialogs for the Protect tab's certificate features:
//   runCertificateManager — lists the CurrentUser\My certificates.
//   runSignDialog         — pick a cert + reason/location, returns SignOptions.
//   runTimestampDialog    — prompt for a TSA URL.
//   showValidationReport  — human-readable per-signature validation results.
// ─────────────────────────────────────────────────────────────────────────────

#include "PdfSign.h"

#include <QString>

class QWidget;

namespace NativeOffice::Pdf {

// Manage Certificates: read-only viewer of installed certificates.
void runCertificateManager(QWidget* parent);

struct SignDialogResult {
    bool confirmed = false;
    SignOptions options;
};
SignDialogResult runSignDialog(QWidget* parent);

// Returns an empty string if cancelled.
QString runTimestampDialog(QWidget* parent);

void showValidationReport(QWidget* parent, const std::vector<SignatureStatus>& results);

} // namespace NativeOffice::Pdf
