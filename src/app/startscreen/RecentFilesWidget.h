#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// RecentFilesWidget.h  (Sprint 3)
// Central list displaying recently opened documents on the Start Screen.
//
// Sprint 3 changes:
//   • Loads the initial list from RecentFilesManager (QSettings-backed)
//   • Auto-refreshes when RecentFilesManager::listChanged() fires
//   • File rows are properly clickable (mouse-press event on the row widget)
//   • "Remove" context-menu action on each row
// ─────────────────────────────────────────────────────────────────────────────

#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QString>
#include <QDateTime>
#include <vector>

namespace NativeOffice {

// Mirror of RecentFileEntry so the widget stays independent of core headers.
struct RecentFile {
    QString   path;
    QString   name;
    QString   type;         // "Writer" | "Calc" | "Impress"
    QDateTime lastOpened;
};

class RecentFilesWidget : public QWidget {
    Q_OBJECT

public:
    explicit RecentFilesWidget(QWidget* parent = nullptr);

    // Externally set the list (used when RecentFilesManager fires listChanged)
    void setRecentFiles(const std::vector<RecentFile>& files);

    // Reload from RecentFilesManager directly
    void refreshFromManager();

    // Show only starred files. Backs the Home sidebar's "Favorites" entry,
    // which previously did nothing at all.
    void setFavoritesOnly(bool on);
    [[nodiscard]] bool favoritesOnly() const { return m_favoritesOnly; }

signals:
    void fileSelected(const QString& path);

private:
    void buildUi();
    void populateList();

    // Creates a clickable file row widget
    QWidget* makeFileRow(const RecentFile& file);

    QVBoxLayout*            m_layout     { nullptr };
    QScrollArea*            m_scrollArea { nullptr };
    QWidget*                m_listWidget { nullptr };
    QVBoxLayout*            m_listLayout { nullptr };
    std::vector<RecentFile> m_files;
    QLabel*                 m_heading      { nullptr };
    bool                    m_favoritesOnly { false };
};

} // namespace NativeOffice
