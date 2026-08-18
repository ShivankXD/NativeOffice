#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// FileSearch.h — the home search box actually searching the machine.
//
// The box on the top bar used to be decorative: it accepted text and did
// nothing with it. This gives it a real index and a real result list.
//
//   FileIndex   a singleton that walks the user's document folders once on a
//               background thread (Desktop, Documents, Downloads, OneDrive
//               mirrors of those, and the home folder shallowly), keeping every
//               file NativeOffice can open. System, hidden, package and
//               build directories are skipped, and the walk is bounded in both
//               entries and wall-clock time so a huge disk cannot hang it.
//
//   SearchPopup a frameless drop-down under the box: matching files ranked
//               name-prefix first, each row carrying the module badge for its
//               type, scrollable when there are many, Enter/arrow-key
//               navigable, and opening the file on click.
//
// Matching is case-insensitive and space-separated: every token must appear in
// the file's name (or in its folder name, ranked lower).
// ─────────────────────────────────────────────────────────────────────────────

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>
#include <QWidget>

class QLineEdit;
class QThread;
class QVBoxLayout;
class QScrollArea;
class QLabel;

namespace NativeOffice {

struct IndexedFile {
    QString path;
    QString name;      // file name with extension
    QString folder;    // containing folder name
    QString suffix;    // lower-cased, no dot
    qint64  modified { 0 };   // secs since epoch, for recency ranking
};

// ── Background index ─────────────────────────────────────────────────────────
class FileIndexWorker : public QObject {
    Q_OBJECT
public slots:
    void scan();
signals:
    void scanned(const QVector<NativeOffice::IndexedFile>& files);
};

class FileIndex : public QObject {
    Q_OBJECT
public:
    static FileIndex& instance();

    // Start the first scan if it has not run yet. Cheap to call repeatedly.
    void ensureStarted();
    // Throw the index away and walk again (the Refresh row in the popup).
    void rescan();

    [[nodiscard]] bool  ready() const { return m_ready; }
    [[nodiscard]] int   count() const { return int(m_files.size()); }

    // Ranked matches for `query`, at most `limit` of them.
    [[nodiscard]] QVector<IndexedFile> search(const QString& query, int limit) const;

signals:
    void readyChanged();

private:
    explicit FileIndex(QObject* parent = nullptr);
    ~FileIndex() override;

    QThread*             m_thread { nullptr };
    FileIndexWorker*     m_worker { nullptr };
    QVector<IndexedFile> m_files;
    bool m_ready   { false };
    bool m_started { false };
};

// ── Drop-down ────────────────────────────────────────────────────────────────
class SearchPopup : public QWidget {
    Q_OBJECT
public:
    // `anchor` is the search box: the popup tracks its width and sits under it.
    explicit SearchPopup(QWidget* anchor, QWidget* parent = nullptr);

    void setQuery(const QString& query);
    void showUnder();
    // Arrow keys / Enter forwarded from the search box.
    bool handleKey(int key);

signals:
    void fileChosen(const QString& path);

protected:
    void keyPressEvent(QKeyEvent*) override;

private:
    void rebuild();
    void moveSelection(int delta);
    void applySelectionStyle();
    void activate(int row);

    QWidget*             m_anchor  { nullptr };
    QVBoxLayout*         m_rows    { nullptr };
    QScrollArea*         m_scroll  { nullptr };
    QLabel*              m_status  { nullptr };
    QString              m_query;
    QVector<IndexedFile> m_hits;
    QVector<QWidget*>    m_rowWidgets;
    int                  m_selected { -1 };
};

} // namespace NativeOffice
