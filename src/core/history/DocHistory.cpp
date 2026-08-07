// ─────────────────────────────────────────────────────────────────────────────
// DocHistory.cpp — see DocHistory.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "DocHistory.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace NativeOffice {

namespace {

constexpr const char* kHistoryRoot = ".nativeoffice-history";
constexpr const char* kIndexFile   = "index.json";

// Keys used in a delta payload. Short because they repeat once per snapshot.
constexpr QLatin1String kSet("s");      // added or modified: key -> value
constexpr QLatin1String kDel("d");      // removed keys
constexpr QLatin1String kFull("f");     // whole snapshot (keyframe)

// Hide the history directory on Windows so it does not clutter the folder the
// document lives in. Best effort: failing to hide it is cosmetic, and the
// history still works.
void hideDirectory(const QString& path) {
#ifdef Q_OS_WIN
    SetFileAttributesW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(path).utf16()),
                       FILE_ATTRIBUTE_HIDDEN);
#else
    Q_UNUSED(path);   // a leading dot is already hidden everywhere else
#endif
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// Serialisation
// ═════════════════════════════════════════════════════════════════════════════

QByteArray serializeSnapshot(const DocSnapshot& s) {
    QJsonObject full;
    for (auto it = s.begin(); it != s.end(); ++it) full.insert(it.key(), it.value());
    QJsonObject root;
    root.insert(kFull, full);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

DocSnapshot deserializeSnapshot(const QByteArray& json, bool* ok) {
    if (ok) *ok = false;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) return {};
    const QJsonObject full = doc.object().value(kFull).toObject();
    DocSnapshot out;
    for (auto it = full.begin(); it != full.end(); ++it) out.insert(it.key(), it.value().toString());
    if (ok) *ok = true;
    return out;
}

QByteArray serializeDelta(const DocSnapshot& from, const DocSnapshot& to) {
    QJsonObject set;
    QJsonArray  del;

    for (auto it = to.begin(); it != to.end(); ++it) {
        const auto prev = from.find(it.key());
        if (prev == from.end() || prev.value() != it.value())
            set.insert(it.key(), it.value());
    }
    for (auto it = from.begin(); it != from.end(); ++it)
        if (!to.contains(it.key())) del.append(it.key());

    QJsonObject root;
    root.insert(kSet, set);
    root.insert(kDel, del);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

DocSnapshot applyDelta(const DocSnapshot& base, const QByteArray& json, bool* ok) {
    if (ok) *ok = false;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) return base;
    const QJsonObject root = doc.object();

    // A payload carrying a full snapshot is a keyframe; base is irrelevant.
    if (root.contains(kFull)) return deserializeSnapshot(json, ok);

    DocSnapshot out = base;
    const QJsonObject set = root.value(kSet).toObject();
    for (auto it = set.begin(); it != set.end(); ++it) out.insert(it.key(), it.value().toString());
    for (const QJsonValue& v : root.value(kDel).toArray()) out.remove(v.toString());
    if (ok) *ok = true;
    return out;
}

QVector<DocChange> compareSnapshots(const DocSnapshot& a, const DocSnapshot& b) {
    QVector<DocChange> out;
    for (auto it = b.begin(); it != b.end(); ++it) {
        const auto prev = a.find(it.key());
        if (prev == a.end())
            out.append({ DocChange::Added, it.key(), QString(), it.value() });
        else if (prev.value() != it.value())
            out.append({ DocChange::Modified, it.key(), prev.value(), it.value() });
    }
    for (auto it = a.begin(); it != a.end(); ++it)
        if (!b.contains(it.key()))
            out.append({ DocChange::Removed, it.key(), it.value(), QString() });
    return out;
}

// ═════════════════════════════════════════════════════════════════════════════
// DocHistory
// ═════════════════════════════════════════════════════════════════════════════

DocHistory::DocHistory(const QString& documentPath) {
    if (documentPath.isEmpty()) {
        m_error = QStringLiteral("Save the document first: history is stored next to the file.");
        return;
    }
    const QFileInfo fi(documentPath);
    const QString root = fi.absolutePath() + QLatin1Char('/') + QLatin1String(kHistoryRoot);
    const QString dir  = root + QLatin1Char('/') + fi.fileName() + QStringLiteral(".hist");

    QDir d;
    if (!d.mkpath(dir)) {
        m_error = QStringLiteral("Could not create the history folder next to the document.");
        return;
    }
    hideDirectory(root);
    m_dir = dir;
}

QString DocHistory::snapPath(int id) const {
    return m_dir + QStringLiteral("/%1.snap").arg(id, 6, 10, QLatin1Char('0'));
}

QVector<SnapshotInfo> DocHistory::readIndex() const {
    QVector<SnapshotInfo> out;
    QFile f(m_dir + QLatin1Char('/') + QLatin1String(kIndexFile));
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    for (const QJsonValue& v : doc.array()) {
        const QJsonObject o = v.toObject();
        SnapshotInfo s;
        s.id       = o.value(QStringLiteral("id")).toInt();
        s.when     = QDateTime::fromSecsSinceEpoch(qint64(o.value(QStringLiteral("t")).toDouble()));
        s.message  = o.value(QStringLiteral("m")).toString();
        s.keyframe = o.value(QStringLiteral("k")).toBool();
        s.bytes    = qint64(o.value(QStringLiteral("b")).toDouble());
        if (s.id > 0) out.append(s);
    }
    return out;
}

bool DocHistory::writeIndex(const QVector<SnapshotInfo>& list) {
    QJsonArray arr;
    for (const SnapshotInfo& s : list) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), s.id);
        o.insert(QStringLiteral("t"),  double(s.when.toSecsSinceEpoch()));
        o.insert(QStringLiteral("m"),  s.message);
        o.insert(QStringLiteral("k"),  s.keyframe);
        o.insert(QStringLiteral("b"),  double(s.bytes));
        arr.append(o);
    }
    // QSaveFile: the index is the only thing that makes the payloads findable,
    // so a half-written one after a crash would orphan the whole history.
    QSaveFile f(m_dir + QLatin1Char('/') + QLatin1String(kIndexFile));
    if (!f.open(QIODevice::WriteOnly)) {
        m_error = QStringLiteral("Could not write the history index.");
        return false;
    }
    const QByteArray json = QJsonDocument(arr).toJson(QJsonDocument::Compact);
    if (f.write(json) != json.size() || !f.commit()) {
        m_error = QStringLiteral("Could not write the history index.");
        return false;
    }
    return true;
}

bool DocHistory::writePayload(int id, const QByteArray& json) {
    QSaveFile f(snapPath(id));
    if (!f.open(QIODevice::WriteOnly)) {
        m_error = QStringLiteral("Could not write snapshot %1.").arg(id);
        return false;
    }
    const QByteArray blob = qCompress(json, 9);
    if (f.write(blob) != blob.size() || !f.commit()) {
        m_error = QStringLiteral("Could not write snapshot %1.").arg(id);
        return false;
    }
    return true;
}

QByteArray DocHistory::readPayload(int id, bool* ok) const {
    if (ok) *ok = false;
    QFile f(snapPath(id));
    if (!f.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("Snapshot %1 is missing.").arg(id);
        return {};
    }
    const QByteArray blob = f.readAll();
    f.close();
    const QByteArray json = qUncompress(blob);
    if (json.isEmpty() && !blob.isEmpty()) {
        m_error = QStringLiteral("Snapshot %1 is corrupt.").arg(id);
        return {};
    }
    if (ok) *ok = true;
    return json;
}

QVector<SnapshotInfo> DocHistory::snapshots() const {
    if (!isUsable()) return {};
    return readIndex();
}

int DocHistory::commit(const DocSnapshot& snapshot, const QString& message) {
    if (!isUsable()) return -1;

    QVector<SnapshotInfo> list = readIndex();
    const int nextId = list.isEmpty() ? 1 : list.last().id + 1;

    // Compare against the current tip so an unchanged document does not create
    // a version. Reported as 0 rather than an error: nothing went wrong.
    DocSnapshot tip;
    if (!list.isEmpty()) {
        bool ok = false;
        tip = reconstruct(list.last().id, &ok);
        if (!ok) return -1;
        if (tip == snapshot) return 0;
    }

    // Keyframe on the first commit and every kKeyframeInterval after it, so a
    // rebuild never replays more than that many deltas.
    const bool keyframe = list.isEmpty() || (nextId % kKeyframeInterval) == 1;
    const QByteArray json = keyframe ? serializeSnapshot(snapshot)
                                     : serializeDelta(tip, snapshot);
    if (!writePayload(nextId, json)) return -1;

    SnapshotInfo info;
    info.id       = nextId;
    info.when     = QDateTime::currentDateTime();
    info.message  = message.trimmed().isEmpty() ? QStringLiteral("(no message)") : message.trimmed();
    info.keyframe = keyframe;
    info.bytes    = QFileInfo(snapPath(nextId)).size();
    list.append(info);

    if (!writeIndex(list)) {
        // Roll the payload back so the directory cannot hold a snapshot the
        // index does not know about.
        QFile::remove(snapPath(nextId));
        return -1;
    }
    return nextId;
}

DocSnapshot DocHistory::reconstruct(int id, bool* ok) const {
    if (ok) *ok = false;
    if (!isUsable()) return {};

    const QVector<SnapshotInfo> list = readIndex();
    int idx = -1;
    for (int i = 0; i < list.size(); ++i) if (list.at(i).id == id) { idx = i; break; }
    if (idx < 0) { m_error = QStringLiteral("No such version: %1.").arg(id); return {}; }

    // Walk back to the nearest keyframe, then replay forward from it.
    int start = idx;
    while (start > 0 && !list.at(start).keyframe) --start;

    DocSnapshot cur;
    for (int i = start; i <= idx; ++i) {
        bool got = false;
        const QByteArray json = readPayload(list.at(i).id, &got);
        if (!got) return {};
        bool applied = false;
        cur = applyDelta(cur, json, &applied);
        if (!applied) {
            m_error = QStringLiteral("Version %1 could not be read.").arg(list.at(i).id);
            return {};
        }
    }
    if (ok) *ok = true;
    return cur;
}

QVector<DocChange> DocHistory::diff(int fromId, int toId, bool* ok) const {
    if (ok) *ok = false;
    DocSnapshot a, b;
    if (fromId > 0) {
        bool got = false;
        a = reconstruct(fromId, &got);
        if (!got) return {};
    }
    bool got = false;
    b = reconstruct(toId, &got);
    if (!got) return {};
    if (ok) *ok = true;
    return compareSnapshots(a, b);
}

QVector<DocChange> DocHistory::diffAgainst(int fromId, const DocSnapshot& current, bool* ok) const {
    if (ok) *ok = false;
    DocSnapshot a;
    if (fromId > 0) {
        bool got = false;
        a = reconstruct(fromId, &got);
        if (!got) return {};
    }
    if (ok) *ok = true;
    return compareSnapshots(a, current);
}

bool DocHistory::clear() {
    if (!isUsable()) return false;
    QDir d(m_dir);
    if (!d.removeRecursively()) {
        m_error = QStringLiteral("Could not remove the history folder.");
        return false;
    }
    m_dir.clear();
    return true;
}

} // namespace NativeOffice
