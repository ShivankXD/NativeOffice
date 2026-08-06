#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ExportPrefs.h — the premium export preferences, read from one place.
//
// The Premium Settings page writes these; the export paths read them through
// here so the key names and defaults are stated once. Every accessor falls back
// to the value the app used before the setting existed, so a free account (or a
// premium account that never opened Settings) behaves exactly as it did.
//
// Entitlement is not re-checked here. SettingsTray only writes these keys for
// a premium account, so a free account has nothing stored and always gets the
// defaults. Keeping the check in one place means these can stay plain lookups.
// ─────────────────────────────────────────────────────────────────────────────

#include <QSettings>
#include <QString>

namespace NativeOffice {
namespace ExportPrefs {

// Resolution for "Export to PDF" across Writer, Calc and Impress.
// 300 dpi was the previous hardcoded value in Writer, so that is the default.
inline int pdfExportDpi() {
    const int dpi = QSettings().value(QStringLiteral("premium/pdfExportDpi"), 300).toInt();
    return (dpi == 150 || dpi == 300 || dpi == 600) ? dpi : 300;
}

// Compression aggressiveness for the PDF toolkit's Compress action.
enum class Compression { Light, Balanced, Maximum };

inline Compression pdfCompression() {
    const QString v = QSettings()
        .value(QStringLiteral("premium/pdfCompressLevel"), QStringLiteral("balanced"))
        .toString();
    if (v == QLatin1String("light")) return Compression::Light;
    if (v == QLatin1String("max"))   return Compression::Maximum;
    return Compression::Balanced;
}

// Default extension offered by each editor's Save As dialog. The previous
// behaviour was .docx / .xlsx / .pptx, so those remain the defaults.
inline QString defaultDocFormat() {
    const QString v = QSettings()
        .value(QStringLiteral("premium/defaultDocFormat"), QStringLiteral("docx")).toString();
    return v == QLatin1String("noff") ? v : QStringLiteral("docx");
}

inline QString defaultSheetFormat() {
    const QString v = QSettings()
        .value(QStringLiteral("premium/defaultSheetFormat"), QStringLiteral("xlsx")).toString();
    return v == QLatin1String("noff") ? v : QStringLiteral("xlsx");
}

inline QString defaultDeckFormat() {
    const QString v = QSettings()
        .value(QStringLiteral("premium/defaultDeckFormat"), QStringLiteral("pptx")).toString();
    return v == QLatin1String("noff") ? v : QStringLiteral("pptx");
}

} // namespace ExportPrefs
} // namespace NativeOffice
