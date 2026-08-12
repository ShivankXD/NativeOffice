// ─────────────────────────────────────────────────────────────────────────────
// RecentFilesManager.cpp  (Sprint 3)
// ─────────────────────────────────────────────────────────────────────────────
#include "RecentFilesManager.h"

#include <QSettings>
#include <QFileInfo>
#include <algorithm>

namespace NativeOffice {

// ── Singleton ────────────────────────────────────────────────────────────────
RecentFilesManager& RecentFilesManager::instance() {
    static RecentFilesManager inst;
    return inst;
}

RecentFilesManager::RecentFilesManager(QObject* parent)
    : QObject(parent)
{
    load();
}

// ── Public API ───────────────────────────────────────────────────────────────
std::vector<RecentFileEntry> RecentFilesManager::recentFiles() const {
    return m_entries;
}

// ── Favourites ───────────────────────────────────────────────────────────────
namespace {
constexpr auto kFavoritesKey = "Favorites/paths";
}

QStringList RecentFilesManager::favorites() const {
    return QSettings().value(kFavoritesKey).toStringList();
}

bool RecentFilesManager::isFavorite(const QString& path) const {
    return favorites().contains(path, Qt::CaseInsensitive);
}

void RecentFilesManager::setFavorite(const QString& path, bool on) {
    if (path.isEmpty()) return;
    QStringList list = favorites();
    const bool had = list.removeAll(path) > 0;
    if (on) list.prepend(path);
    if (had == on) return;                     // already in the requested state
    QSettings().setValue(kFavoritesKey, list);
    emit favoritesChanged();
    emit listChanged();                        // rows redraw their star
}

void RecentFilesManager::addFile(const QString& path, const QString& type) {
    // Remove any existing entry for the same path (we'll re-insert at front)
    m_entries.erase(
        std::remove_if(m_entries.begin(), m_entries.end(),
                       [&path](const RecentFileEntry& e) { return e.path == path; }),
        m_entries.end());

    RecentFileEntry entry;
    entry.path       = path;
    entry.name       = QFileInfo(path).fileName();
    entry.type       = type;
    entry.lastOpened = QDateTime::currentDateTime();

    // Insert at the front (most recent first)
    m_entries.insert(m_entries.begin(), entry);

    // Prune to MAX_RECENT
    if (static_cast<int>(m_entries.size()) > MAX_RECENT)
        m_entries.resize(MAX_RECENT);

    save();
    emit listChanged();
}

void RecentFilesManager::removeFile(const QString& path) {
    m_entries.erase(
        std::remove_if(m_entries.begin(), m_entries.end(),
                       [&path](const RecentFileEntry& e) { return e.path == path; }),
        m_entries.end());
    save();
    emit listChanged();
}

void RecentFilesManager::clearAll() {
    m_entries.clear();
    QSettings s;
    s.remove("RecentFiles");
    emit listChanged();
}

// ── Persistence ──────────────────────────────────────────────────────────────
void RecentFilesManager::load() {
    QSettings s;
    const int count = s.beginReadArray("RecentFiles");
    m_entries.reserve(count);
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        RecentFileEntry e;
        e.path       = s.value("path").toString();
        e.name       = s.value("name").toString();
        e.type       = s.value("type", "Writer").toString();
        e.lastOpened = s.value("lastOpened").toDateTime();
        if (!e.path.isEmpty())
            m_entries.push_back(std::move(e));
    }
    s.endArray();
}

void RecentFilesManager::save() const {
    QSettings s;
    s.beginWriteArray("RecentFiles", static_cast<int>(m_entries.size()));
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        s.setArrayIndex(i);
        const auto& e = m_entries[i];
        s.setValue("path",       e.path);
        s.setValue("name",       e.name);
        s.setValue("type",       e.type);
        s.setValue("lastOpened", e.lastOpened);
    }
    s.endArray();
    s.sync();
}

} // namespace NativeOffice
