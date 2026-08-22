// ─────────────────────────────────────────────────────────────────────────────
// CrashRecovery.cpp  (see CrashRecovery.h)
// ─────────────────────────────────────────────────────────────────────────────
#include "CrashRecovery.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QRandomGenerator>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QWidget>

namespace NativeOffice {
namespace {

// Untitled snapshots claimed during THIS session. Two blank documents opened
// after a crash must be offered two different leftovers, not the same one
// twice, and the file is still on disk at the moment the second one looks.
QSet<QString>& claimed() {
    static QSet<QString> s;
    return s;
}

QString humanKind(const QString& kind) {
    if (kind == QLatin1String("calc"))    return QStringLiteral("spreadsheet");
    if (kind == QLatin1String("impress")) return QStringLiteral("presentation");
    return QStringLiteral("document");
}

} // namespace

CrashRecovery::CrashRecovery(QString kind, QWidget* dialogParent, QObject* parent)
    : QObject(parent ? parent : static_cast<QObject*>(dialogParent))
    , m_kind(std::move(kind))
    , m_parent(dialogParent)
{
    // Unique per open document, so untitled documents do not share a file.
    m_untitledId = QString::number(QDateTime::currentMSecsSinceEpoch(), 16)
                 + QLatin1Char('-')
                 + QString::number(QRandomGenerator::global()->generate(), 16);

    // A clean exit means there is nothing to recover.
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { discard(); });
}

CrashRecovery::~CrashRecovery() = default;

void CrashRecovery::setSource(std::function<quint64()> revision,
                              std::function<QByteArray()> serialize) {
    m_revision  = std::move(revision);
    m_serialize = std::move(serialize);
    if (m_revision) m_lastRevision = m_revision();
}

QString CrashRecovery::directory() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) dir = QDir::tempPath();
    dir += QStringLiteral("/recovery");
    QDir().mkpath(dir);
    return dir;
}

QString CrashRecovery::filePath() const {
    // A named document is keyed by its path so reopening the same file finds
    // its own snapshot; an untitled one is keyed by this instance.
    const QString key = m_docPath.isEmpty()
        ? QStringLiteral("untitled-") + m_untitledId
        : QString::number(qHash(m_docPath), 16);
    return directory() + QLatin1Char('/') + m_kind + QLatin1Char('-') + key
         + QStringLiteral(".noff");
}

void CrashRecovery::setDocumentPath(const QString& path) {
    if (path == m_docPath) return;
    // The snapshot under the old identity would otherwise be offered again on
    // a later launch, as a document the user has since saved.
    discard();
    m_docPath = path;
    // Whatever is in memory now belongs to the new identity.
    if (m_revision) m_lastRevision = m_revision();
}

void CrashRecovery::start(int seconds) {
    if (!m_timer) {
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &CrashRecovery::snapshot);
    }
    m_timer->start(qBound(5, seconds, 300) * 1000);
}

void CrashRecovery::snapshot() {
    if (!m_revision || !m_serialize) return;

    const quint64 rev = m_revision();
    if (rev == m_lastRevision) return;     // nothing new since the last one

    const QByteArray bytes = m_serialize();
    if (bytes.isEmpty()) { m_lastRevision = rev; return; }

    // Written to a temporary first and then moved into place: a crash halfway
    // through the write would otherwise replace a good snapshot with a torn
    // one, which is the only thing worse than having none.
    const QString target = filePath();
    const QString tmp    = target + QStringLiteral(".part");
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    const bool ok = f.write(bytes) == bytes.size();
    f.close();
    if (!ok) { QFile::remove(tmp); return; }

    QFile::remove(target);
    if (!QFile::rename(tmp, target)) { QFile::remove(tmp); return; }

    m_lastRevision = rev;
    m_wrote = true;
}

void CrashRecovery::discard() {
    if (!m_wrote) return;
    QFile::remove(filePath());
    m_wrote = false;
}

QByteArray CrashRecovery::takeLeftover() {
    QDir dir(directory());
    QString found;

    if (!m_docPath.isEmpty()) {
        const QString own = filePath();
        if (QFileInfo::exists(own)) found = own;
    } else {
        // The oldest unclaimed untitled snapshot for this editor.
        const QString pattern = m_kind + QStringLiteral("-untitled-*.noff");
        const QFileInfoList files =
            dir.entryInfoList({ pattern }, QDir::Files, QDir::Time | QDir::Reversed);
        for (const QFileInfo& fi : files) {
            const QString p = fi.absoluteFilePath();
            if (claimed().contains(p)) continue;
            found = p;
            break;
        }
    }
    if (found.isEmpty()) return {};

    claimed().insert(found);

    QFile f(found);
    if (!f.open(QIODevice::ReadOnly)) { QFile::remove(found); return {}; }
    const QByteArray bytes = f.readAll();
    f.close();

    // Removed whatever happens next. An answer of no means the user does not
    // want it, and leaving it would ask again on every launch.
    QFile::remove(found);
    return bytes;
}

bool CrashRecovery::offerLeftover(QByteArray& out) {
    const QByteArray bytes = takeLeftover();
    if (bytes.isEmpty()) return false;

    const QString name = m_docPath.isEmpty()
        ? tr("an unsaved %1").arg(humanKind(m_kind))
        : QFileInfo(m_docPath).fileName();
    const auto choice = QMessageBox::question(
        m_parent, tr("Recover Unsaved Changes"),
        tr("NativeOffice found unsaved changes to %1 from a session that did not "
           "close normally.\n\nRecover them?").arg(name),
        QMessageBox::Yes | QMessageBox::No);
    if (choice != QMessageBox::Yes) return false;

    out = bytes;
    return true;
}

} // namespace NativeOffice
