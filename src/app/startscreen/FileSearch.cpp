// ─────────────────────────────────────────────────────────────────────────────
// FileSearch.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "FileSearch.h"
#include "HomeKit.h"
#include "LucideIcons.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QScrollArea>
#include <QSet>
#include <QStandardPaths>
#include <QStyle>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>

namespace NativeOffice {

namespace {

// Hard ceilings so an unusual disk layout cannot turn the first search into a
// multi-minute walk.
constexpr int  kMaxFiles      = 40000;
constexpr int  kMaxDirs       = 24000;
constexpr int  kMaxSeconds    = 20;
constexpr int  kMaxDepth      = 8;
constexpr int  kMaxRowsShown  = 60;

// Directory names never worth walking: package/build caches, VCS metadata and
// the Windows/OS trees. Compared case-insensitively against the folder name.
bool isSkippedDir(const QString& name) {
    static const QSet<QString> skip = {
        "node_modules", "appdata", "windows", "program files", "program files (x86)",
        "programdata", "$recycle.bin", "system volume information", ".git", ".svn",
        ".hg", "__pycache__", "venv", ".venv", "env", "build", "out", "dist",
        "target", ".cache", ".gradle", ".m2", ".nuget", ".vs", ".idea",
        "packages", "obj", "bin", "cmakefiles", "vcpkg", "anaconda3", "miniconda3",
        "site-packages", ".conda", "steamapps", ".android", ".vscode",
    };
    return skip.contains(name.toLower());
}

QStringList searchRoots() {
    QStringList roots;
    auto add = [&roots](const QString& p) {
        if (!p.isEmpty() && QDir(p).exists() && !roots.contains(p)) roots << p;
    };
    add(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    add(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    add(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));

    // OneDrive relocates Documents/Desktop without changing the plain home
    // paths, so both spellings are indexed when they exist.
    const QString home = QDir::homePath();
    for (const QString& od : { QStringLiteral("OneDrive"),
                               QStringLiteral("OneDrive - Personal") }) {
        const QString base = home + QLatin1Char('/') + od;
        if (!QDir(base).exists()) continue;
        for (const QString& sub : { QStringLiteral("Documents"),
                                    QStringLiteral("Desktop"),
                                    QStringLiteral("Pictures") })
            add(base + QLatin1Char('/') + sub);
    }
    return roots;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// FileIndexWorker
// ─────────────────────────────────────────────────────────────────────────────
void FileIndexWorker::scan() {
    QVector<IndexedFile> out;
    QSet<QString> seenPaths;
    QSet<QString> seenDirs;

    QStringList wanted = supportedFileSuffixes();
    QSet<QString> exts(wanted.begin(), wanted.end());

    QElapsedTimer clock;
    clock.start();
    int dirCount = 0;

    // Breadth-first with an explicit queue: it keeps shallow (and therefore
    // more likely relevant) files even when the budget runs out mid-walk,
    // which a recursive QDirIterator would not.
    struct Pending { QString path; int depth; };
    QList<Pending> queue;
    for (const QString& r : searchRoots()) queue.append({ r, 0 });

    while (!queue.isEmpty()) {
        if (out.size() >= kMaxFiles || dirCount >= kMaxDirs
            || clock.hasExpired(kMaxSeconds * 1000))
            break;

        const Pending cur = queue.takeFirst();
        const QString canonical = QDir(cur.path).canonicalPath();
        if (canonical.isEmpty() || seenDirs.contains(canonical)) continue;
        seenDirs.insert(canonical);
        ++dirCount;

        QDirIterator it(cur.path, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                        QDirIterator::NoIteratorFlags);
        while (it.hasNext()) {
            it.next();
            const QFileInfo fi = it.fileInfo();
            if (fi.isSymLink()) continue;
            if (fi.isDir()) {
                if (cur.depth + 1 > kMaxDepth) continue;
                const QString name = fi.fileName();
                if (name.startsWith(QLatin1Char('.')) || isSkippedDir(name)) continue;
                queue.append({ fi.absoluteFilePath(), cur.depth + 1 });
                continue;
            }
            const QString suffix = fi.suffix().toLower();
            if (!exts.contains(suffix)) continue;
            if (fi.fileName().startsWith(QLatin1Char('~'))) continue;   // Office lock files
            const QString path = fi.absoluteFilePath();
            if (seenPaths.contains(path)) continue;
            seenPaths.insert(path);
            out.append({ path, fi.fileName(), fi.dir().dirName(), suffix,
                         fi.lastModified().toSecsSinceEpoch() });
            if (out.size() >= kMaxFiles) break;
        }
    }

    emit scanned(out);
}

// ─────────────────────────────────────────────────────────────────────────────
// FileIndex
// ─────────────────────────────────────────────────────────────────────────────
FileIndex& FileIndex::instance() {
    static FileIndex inst;
    return inst;
}

FileIndex::FileIndex(QObject* parent) : QObject(parent) {
    m_thread = new QThread;
    m_worker = new FileIndexWorker;
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &FileIndexWorker::scanned, this,
            [this](const QVector<IndexedFile>& files) {
                m_files = files;
                m_ready = true;
                emit readyChanged();
            });
    m_thread->start(QThread::LowPriority);
}

FileIndex::~FileIndex() {
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(3000);
        delete m_thread;
    }
}

void FileIndex::ensureStarted() {
    if (m_started) return;
    m_started = true;
    QMetaObject::invokeMethod(m_worker, "scan", Qt::QueuedConnection);
}

void FileIndex::rescan() {
    m_ready = false;
    m_started = true;
    emit readyChanged();
    QMetaObject::invokeMethod(m_worker, "scan", Qt::QueuedConnection);
}

QVector<IndexedFile> FileIndex::search(const QString& queryIn, int limit) const {
    const QString query = queryIn.trimmed();
    if (query.isEmpty()) return {};

    const QStringList tokens = query.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    struct Scored { const IndexedFile* file; int score; };
    QVector<Scored> hits;
    hits.reserve(256);

    for (const IndexedFile& f : m_files) {
        int score = 0;
        bool ok = true;
        for (const QString& t : tokens) {
            const int inName = f.name.indexOf(t, 0, Qt::CaseInsensitive);
            if (inName == 0)                                score += 100;   // prefix
            else if (inName > 0)                            score += 55;    // substring
            else if (f.folder.contains(t, Qt::CaseInsensitive)) score += 18; // folder only
            else { ok = false; break; }
        }
        if (!ok) continue;
        // A shorter name matching the same query is the more likely target.
        score -= qMin(40, f.name.size() / 4);
        hits.append({ &f, score });
    }

    // Ties break on recency: the file touched most recently wins.
    std::sort(hits.begin(), hits.end(), [](const Scored& a, const Scored& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.file->modified > b.file->modified;
    });

    QVector<IndexedFile> out;
    out.reserve(qMin(limit, hits.size()));
    for (int i = 0; i < hits.size() && out.size() < limit; ++i)
        out.append(*hits[i].file);
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// SearchPopup
// ─────────────────────────────────────────────────────────────────────────────
SearchPopup::SearchPopup(QWidget* anchor, QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint), m_anchor(anchor) {
    setObjectName("searchPopup");
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::NoFocus);   // typing stays in the search box

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(8, 8, 8, 8);
    v->setSpacing(0);

    m_status = heading(QString(), 12, Home::kMuted, false, this);
    m_status->setContentsMargins(10, 6, 10, 8);
    v->addWidget(m_status);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFocusPolicy(Qt::NoFocus);
    auto* body = new QWidget(m_scroll);
    body->setObjectName("searchBody");
    m_rows = new QVBoxLayout(body);
    m_rows->setContentsMargins(0, 0, 0, 0);
    m_rows->setSpacing(2);
    m_rows->addStretch();
    m_scroll->setWidget(body);
    v->addWidget(m_scroll, 1);

    setStyleSheet(QString(R"(
        QWidget#searchPopup { background:%1; border:1px solid %2; border-radius:14px; }
        QWidget#searchBody  { background:transparent; }
        #searchRow      { background:transparent; border-radius:9px; }
        #searchRow:hover { background:%3; }
        #searchRowSel   { background:%3; border-radius:9px; }
        QScrollBar:vertical { background:transparent; width:9px; margin:2px; }
        QScrollBar::handle:vertical { background:#2A3244; border-radius:4px; min-height:28px; }
        QScrollBar::add-line, QScrollBar::sub-line { height:0; }
    )").arg(Home::kPanel, Home::kBorder, Home::kPanelHover));

    connect(&FileIndex::instance(), &FileIndex::readyChanged, this, [this] {
        if (isVisible()) rebuild();
    });
}

void SearchPopup::setQuery(const QString& query) {
    m_query = query;
    m_selected = -1;
    rebuild();
}

void SearchPopup::rebuild() {
    // Drop the previous rows (everything but the trailing stretch).
    for (QWidget* w : m_rowWidgets) w->deleteLater();
    m_rowWidgets.clear();

    auto& index = FileIndex::instance();
    m_hits = index.search(m_query, kMaxRowsShown);

    if (m_query.trimmed().isEmpty()) {
        m_status->setText(index.ready()
            ? tr("Type to search %1 files on this computer").arg(index.count())
            : tr("Indexing your files…"));
    } else if (m_hits.isEmpty()) {
        m_status->setText(index.ready()
            ? tr("No files match \"%1\"").arg(m_query.trimmed())
            : tr("Still indexing — no match yet for \"%1\"").arg(m_query.trimmed()));
    } else {
        m_status->setText(m_hits.size() == 1 ? tr("1 result")
                                             : tr("%1 results").arg(m_hits.size()));
    }

    for (int i = 0; i < m_hits.size(); ++i) {
        const IndexedFile& f = m_hits[i];
        const FileKind kind = fileKindForSuffix(f.suffix);

        auto* row = new ClickableFrame(m_scroll->widget());
        row->setObjectName("searchRow");
        row->setFixedHeight(46);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(10, 0, 12, 0);
        h->setSpacing(11);
        h->addWidget(badge(kind.letter, kind.color, 28, row));

        auto* col = new QVBoxLayout();
        col->setSpacing(1);
        auto* name = label600(f.name, 12, Home::kText, row);
        name->setAttribute(Qt::WA_TransparentForMouseEvents);
        col->addWidget(name);
        auto* sub = heading(kind.module + QStringLiteral("  ·  ") + f.folder,
                            11, Home::kFaint, false, row);
        sub->setAttribute(Qt::WA_TransparentForMouseEvents);
        col->addWidget(sub);
        h->addLayout(col, 1);

        auto* when = heading(QDateTime::fromSecsSinceEpoch(f.modified)
                                 .toString(QStringLiteral("MMM d")),
                             11, Home::kFaint, false, row);
        when->setAttribute(Qt::WA_TransparentForMouseEvents);
        h->addWidget(when);

        const int idx = i;
        row->onClick = [this, idx] { activate(idx); };
        m_rows->insertWidget(m_rows->count() - 1, row);
        m_rowWidgets.append(row);
    }

    // Height follows the result count so a single hit is not framed by an
    // empty 400 px box.
    const int rowsH = qMin(m_hits.size(), 8) * 48;
    setFixedHeight(qBound(78, 46 + rowsH + 16, 470));
}

void SearchPopup::showUnder() {
    if (!m_anchor) return;
    FileIndex::instance().ensureStarted();
    const QPoint below = m_anchor->mapToGlobal(QPoint(0, m_anchor->height() + 8));
    setFixedWidth(qMax(420, m_anchor->width()));
    move(below);
    show();
    raise();
}

void SearchPopup::moveSelection(int delta) {
    if (m_hits.isEmpty()) return;
    m_selected = qBound(0, m_selected < 0 ? (delta > 0 ? 0 : m_hits.size() - 1)
                                          : m_selected + delta,
                        m_hits.size() - 1);
    applySelectionStyle();
    if (m_selected >= 0 && m_selected < m_rowWidgets.size())
        m_scroll->ensureWidgetVisible(m_rowWidgets[m_selected], 0, 24);
}

void SearchPopup::applySelectionStyle() {
    for (int i = 0; i < m_rowWidgets.size(); ++i) {
        m_rowWidgets[i]->setObjectName(i == m_selected ? "searchRowSel" : "searchRow");
        // Re-polish, or the object-name swap does not repaint.
        m_rowWidgets[i]->style()->unpolish(m_rowWidgets[i]);
        m_rowWidgets[i]->style()->polish(m_rowWidgets[i]);
    }
}

void SearchPopup::activate(int row) {
    if (row < 0 || row >= m_hits.size()) return;
    const QString path = m_hits[row].path;
    hide();
    emit fileChosen(path);
}

bool SearchPopup::handleKey(int key) {
    switch (key) {
    case Qt::Key_Down:   moveSelection(+1); return true;
    case Qt::Key_Up:     moveSelection(-1); return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        activate(m_selected >= 0 ? m_selected : (m_hits.isEmpty() ? -1 : 0));
        return true;
    case Qt::Key_Escape: hide(); return true;
    default: break;
    }
    return false;
}

void SearchPopup::keyPressEvent(QKeyEvent* e) {
    if (!handleKey(e->key())) QWidget::keyPressEvent(e);
}

} // namespace NativeOffice
