#include "AiConsent.h"

#include "auth/AuthManager.h"

#include <QCryptographicHash>
#include <QSettings>

namespace NativeOffice {

QString AiConsent::settingsKey() {
    const QString email = AuthManager::instance().userEmail();
    // No account yet (the login gate has not completed) still gets a stable
    // slot, so a decision made in that window is not silently forgotten.
    const QByteArray id = email.isEmpty()
                            ? QByteArrayLiteral("anonymous")
                            : QCryptographicHash::hash(email.toUtf8().toLower(),
                                                       QCryptographicHash::Sha256)
                                  .toHex()
                                  .left(16);
    return QStringLiteral("ai/consent/") + QString::fromLatin1(id);
}

AiConsent::State AiConsent::state() {
    QSettings s;
    const QString v = s.value(settingsKey()).toString();
    if (v == QLatin1String("accepted")) return State::Accepted;
    if (v == QLatin1String("declined")) return State::Declined;
    return State::Unanswered;
}

void AiConsent::recordAccepted() {
    QSettings s;
    s.setValue(settingsKey(), QStringLiteral("accepted"));
    s.sync();
}

void AiConsent::recordDeclined() {
    QSettings s;
    s.setValue(settingsKey(), QStringLiteral("declined"));
    s.sync();
}

void AiConsent::reset() {
    QSettings s;
    s.remove(settingsKey());
    s.sync();
}

} // namespace NativeOffice
