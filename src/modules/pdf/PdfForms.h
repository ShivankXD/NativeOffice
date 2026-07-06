#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfForms.h — AcroForm text-field detection and filling.
//
// Detection walks the catalog's /AcroForm /Fields tree and collects terminal
// text fields (/FT /Tx), resolving each widget's page and rect. Filling
// rebuilds the document, substituting the modified field objects (new /V +
// regenerated /AP appearance in base-14 Helvetica) via the rebuild copier's
// pre-map mechanism, and flips /NeedAppearances on so viewers that prefer to
// regenerate appearances themselves also show the values.
//
// Checkbox/radio/choice/signature fields are detected (so the UI can report
// them) but only text fields are fillable in this pass.
// ─────────────────────────────────────────────────────────────────────────────

#include "PdfOps.h"

#include <QRectF>
#include <QString>
#include <vector>

namespace NativeOffice::Pdf {

struct FormField {
    enum class Type { Text, Checkbox, Radio, Choice, Signature, Button, Unknown };
    QString fullName;                // fully-qualified field name (/T chain)
    Type    type = Type::Unknown;
    QString value;                   // current /V (text fields)
    int     pageIndex = -1;
    QRectF  rect;                    // top-left origin, page points
    bool    multiline = false;
};

// All form fields in reading order. Empty if the PDF has no AcroForm.
std::vector<FormField> detectFormFields(const QString& path);

// Sets the given text fields' values (keyed by fullName) and writes `out`.
// Names not present in the document are ignored.
OpResult fillTextFields(const QString& in, const QString& out,
                        const std::map<QString, QString>& values);

// Flattens all form fields into page content (bakes appearances in, drops
// the interactive AcroForm) — "print-ready" forms. Not yet wired to UI but
// available for the Convert/print paths.
OpResult flattenForms(const QString& in, const QString& out);

} // namespace NativeOffice::Pdf
