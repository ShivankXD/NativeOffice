#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SecureStore.h — OS-protected storage for small secrets (auth tokens).
//
// Windows: the secret is encrypted with DPAPI (CryptProtectData), bound to the
// current Windows user account, and the opaque blob is kept in QSettings.
// Nothing readable ever touches disk. Other platforms currently fall back to
// base64-in-QSettings (TODO: Keychain / libsecret when those ports land).
// ─────────────────────────────────────────────────────────────────────────────

#include <QByteArray>
#include <QString>

namespace NativeOffice {

class SecureStore {
public:
    static bool       save(const QString& key, const QByteArray& secret);
    static QByteArray load(const QString& key);
    static void       remove(const QString& key);
};

} // namespace NativeOffice
