#include "AiQuota.h"

#include "auth/AuthManager.h"

#include <QCryptographicHash>
#include <QDate>
#include <QLocale>
#include <QSettings>

namespace NativeOffice {

namespace {
constexpr int    kFreeGenerations    = 2;
constexpr int    kPremiumGenerations = 20;
constexpr qint64 kFreeCharacters     = 500000;      // 5 lakh
constexpr qint64 kPremiumCharacters  = 5000000;     // 50 lakh

// Groups every counter for one account under one settings prefix, digested for
// the same reason as AiConsent: the settings file should not carry the address.
QString accountSlug() {
    const QString email = AuthManager::instance().userEmail();
    if (email.isEmpty()) return QStringLiteral("anonymous");
    return QString::fromLatin1(
        QCryptographicHash::hash(email.toUtf8().toLower(), QCryptographicHash::Sha256)
            .toHex()
            .left(16));
}
} // namespace

QString AiQuota::currentPeriod() {
    return QDate::currentDate().toString(QStringLiteral("yyyy-MM"));
}

QString AiQuota::keyFor(const QString& field) {
    return QStringLiteral("ai/usage/") + accountSlug() + QLatin1Char('/') + field;
}

void AiQuota::rollPeriodIfNeeded() {
    QSettings s;
    const QString stored = s.value(keyFor(QStringLiteral("period"))).toString();
    const QString now = currentPeriod();
    if (stored == now) return;
    // A new month, or the very first use. Both start from zero.
    s.setValue(keyFor(QStringLiteral("period")), now);
    s.setValue(keyFor(QStringLiteral("generations")), 0);
    s.setValue(keyFor(QStringLiteral("characters")), 0);
    s.sync();
}

int AiQuota::generationLimit() {
    return AuthManager::instance().premiumActive() ? kPremiumGenerations : kFreeGenerations;
}

qint64 AiQuota::characterLimit() {
    return AuthManager::instance().premiumActive() ? kPremiumCharacters : kFreeCharacters;
}

int AiQuota::generationsUsed() {
    rollPeriodIfNeeded();
    return QSettings().value(keyFor(QStringLiteral("generations")), 0).toInt();
}

qint64 AiQuota::charactersUsed() {
    rollPeriodIfNeeded();
    return QSettings().value(keyFor(QStringLiteral("characters")), 0).toLongLong();
}

int AiQuota::generationsLeft() {
    return qMax(0, generationLimit() - generationsUsed());
}

qint64 AiQuota::charactersLeft() {
    return qMax<qint64>(0, characterLimit() - charactersUsed());
}

bool AiQuota::canGenerate() {
    return generationsLeft() > 0 && charactersLeft() > 0;
}

QString AiQuota::blockedReason() {
    if (canGenerate()) return {};
    const bool premium = AuthManager::instance().premiumActive();
    const QString upgrade = premium
        ? QStringLiteral("Your allowance resets at the start of next month.")
        : QStringLiteral("Premium raises this to 20 files and 50 lakh characters a month.");

    if (generationsLeft() <= 0) {
        return QStringLiteral("You have used all %1 file generations for this month. %2")
            .arg(generationLimit())
            .arg(upgrade);
    }
    return QStringLiteral("You have used this month's writing allowance of %1 characters. %2")
        .arg(QLocale().toString(characterLimit()))
        .arg(upgrade);
}

void AiQuota::recordGeneration(qint64 characters) {
    rollPeriodIfNeeded();
    QSettings s;
    s.setValue(keyFor(QStringLiteral("generations")),
               s.value(keyFor(QStringLiteral("generations")), 0).toInt() + 1);
    s.setValue(keyFor(QStringLiteral("characters")),
               s.value(keyFor(QStringLiteral("characters")), 0).toLongLong()
                   + qMax<qint64>(0, characters));
    s.sync();
}

void AiQuota::recordCharacters(qint64 characters) {
    rollPeriodIfNeeded();
    QSettings s;
    s.setValue(keyFor(QStringLiteral("characters")),
               s.value(keyFor(QStringLiteral("characters")), 0).toLongLong()
                   + qMax<qint64>(0, characters));
    s.sync();
}

QString AiQuota::summaryText() {
    return QStringLiteral("%1 of %2 generations left")
        .arg(generationsLeft())
        .arg(generationLimit());
}

void AiQuota::resetMonth() {
    QSettings s;
    s.setValue(keyFor(QStringLiteral("period")), currentPeriod());
    s.setValue(keyFor(QStringLiteral("generations")), 0);
    s.setValue(keyFor(QStringLiteral("characters")), 0);
    s.sync();
}

} // namespace NativeOffice
