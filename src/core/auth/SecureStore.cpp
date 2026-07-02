// ─────────────────────────────────────────────────────────────────────────────
// SecureStore.cpp — DPAPI-backed secret storage (see header).
// ─────────────────────────────────────────────────────────────────────────────
#include "SecureStore.h"

#include <QSettings>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <dpapi.h>
#endif

namespace NativeOffice {

static QString settingsKey(const QString& key) {
    return QStringLiteral("secure/") + key;
}

#ifdef Q_OS_WIN
// Extra entropy mixed into the DPAPI key so other same-user apps can't
// trivially decrypt our blobs with a bare CryptUnprotectData call.
static const char kEntropy[] = "NativeOffice.SecureStore.v1";

bool SecureStore::save(const QString& key, const QByteArray& secret) {
    DATA_BLOB in {}, out {}, entropy {};
    in.pbData      = reinterpret_cast<BYTE*>(const_cast<char*>(secret.constData()));
    in.cbData      = static_cast<DWORD>(secret.size());
    entropy.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy));
    entropy.cbData = sizeof(kEntropy);

    if (!CryptProtectData(&in, L"NativeOffice", &entropy, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out))
        return false;

    const QByteArray blob(reinterpret_cast<char*>(out.pbData),
                          static_cast<int>(out.cbData));
    LocalFree(out.pbData);

    QSettings().setValue(settingsKey(key), blob.toBase64());
    return true;
}

QByteArray SecureStore::load(const QString& key) {
    const QByteArray blob =
        QByteArray::fromBase64(QSettings().value(settingsKey(key)).toByteArray());
    if (blob.isEmpty()) return {};

    DATA_BLOB in {}, out {}, entropy {};
    in.pbData      = reinterpret_cast<BYTE*>(const_cast<char*>(blob.constData()));
    in.cbData      = static_cast<DWORD>(blob.size());
    entropy.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy));
    entropy.cbData = sizeof(kEntropy);

    if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};

    QByteArray secret(reinterpret_cast<char*>(out.pbData),
                      static_cast<int>(out.cbData));
    // Zero + free the OS buffer promptly.
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return secret;
}

#else // non-Windows fallback (base64 only — replace with Keychain/libsecret)

bool SecureStore::save(const QString& key, const QByteArray& secret) {
    QSettings().setValue(settingsKey(key), secret.toBase64());
    return true;
}

QByteArray SecureStore::load(const QString& key) {
    return QByteArray::fromBase64(QSettings().value(settingsKey(key)).toByteArray());
}

#endif

void SecureStore::remove(const QString& key) {
    QSettings().remove(settingsKey(key));
}

} // namespace NativeOffice
