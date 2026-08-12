#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// RecentFilesManager.h  (Sprint 3)
// QSettings-backed singleton that persists the recent-files list across
// application sessions.
//
// Storage location (QSettings default):
//   Windows : HKCU\Software\NativeOffice\NativeOffice\RecentFiles
//   macOS   : ~/Library/Preferences/nativeoffice.app.plist
//   Linux   : ~/.config/NativeOffice/NativeOffice.conf
//
// Each entry is stored as a JSON-like group under the "RecentFiles" key:
//   RecentFiles/1/path       = "/home/user/mydoc.noff"
//   RecentFiles/1/name       = "mydoc.noff"
//   RecentFiles/1/type       = "Writer"
//   RecentFiles/1/lastOpened = "2026-06-11T10:00:00Z"
//
// Maximum entries: MAX_RECENT (20). Oldest entries are pruned automatically.
// ─────────────────────────────────────────────────────────────────────────────

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <vector>

namespace NativeOffice {

// Forward-declared here so core doesn't depend on the app-layer header.
// RecentFile struct mirrors the one in RecentFilesWidget for convenience.
struct RecentFileEntry {
    QString   path;
    QString   name;
    QString   type;        // "Writer" | "Calc" | "Impress"
    QDateTime lastOpened;
};

class RecentFilesManager : public QObject {
    Q_OBJECT

public:
    static constexpr int MAX_RECENT = 20;

    static RecentFilesManager& instance();

    // Returns a copy of the current list (most-recent first)
    [[nodiscard]] std::vector<RecentFileEntry> recentFiles() const;

    // Prepend a new entry (or bump existing entry to front) and persist
    void addFile(const QString& path, const QString& type);

    // Remove a specific path from the list
    void removeFile(const QString& path);

    // Clear the entire list
    void clearAll();

    // ── Favourites ─────────────────────────────────────────────────────────
    // The Home sidebar had a "Favorites" entry with nothing behind it and no
    // way to star anything. Favourites are kept separately from the recent
    // list so a starred file survives being pruned out of the recent 20.
    // Stored under "Favorites/paths".
    [[nodiscard]] bool isFavorite(const QString& path) const;
    void setFavorite(const QString& path, bool on);
    [[nodiscard]] QStringList favorites() const;

signals:
    // Emitted whenever the list changes so any connected widget can refresh
    void listChanged();
    void favoritesChanged();

private:
    explicit RecentFilesManager(QObject* parent = nullptr);
    ~RecentFilesManager() override = default;

    RecentFilesManager(const RecentFilesManager&)            = delete;
    RecentFilesManager& operator=(const RecentFilesManager&) = delete;

    void load();
    void save() const;

    std::vector<RecentFileEntry> m_entries;
};

} // namespace NativeOffice
