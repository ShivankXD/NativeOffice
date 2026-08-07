#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// DocHistory.h — local, offline snapshot history for a document.
//
// "Commit" a snapshot with a message, list what has been committed, roll back
// to any of them, and diff any two. Everything is on disk beside the document;
// nothing leaves the machine and there is no network path in this file at all.
//
// ── The generic document model ──────────────────────────────────────────────
// A snapshot is a flat, ordered map of key -> value. That is the whole contract,
// and it is what lets one store serve both editors:
//
//   Calc    "SheetName!C,R"  -> raw cell content     (see CalcHistory)
//   Writer  "block:000123"   -> that paragraph's text
//
// Keeping it keyed rather than storing an opaque blob is the point: a delta
// between two maps is exactly the set of keys that were added, changed or
// removed, which is both small to store and directly reportable as "these cells
// changed". A blob diff would give neither.
//
// ── Storage ─────────────────────────────────────────────────────────────────
//   <doc dir>/.nativeoffice-history/<doc name>.hist/
//       index.json      the snapshot list
//       000001.snap     qCompress'd JSON payload
//
// Snapshots after the first are stored as DELTAS against the previous one, so a
// commit that touches three cells costs three entries rather than a copy of the
// sheet. Every kKeyframeInterval commits a full snapshot is written instead, so
// reconstructing a version never has to replay an unbounded chain and one
// corrupt delta cannot cost more than the commits back to the last keyframe.
//
// Compression is qCompress/qUncompress (zlib, already linked through QtCore).
// No new dependency.
// ─────────────────────────────────────────────────────────────────────────────

#include <QDateTime>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace NativeOffice {

// Ordered so serialisation is deterministic: the same document always produces
// byte-identical payloads, which keeps deltas honest.
using DocSnapshot = QMap<QString, QString>;

struct SnapshotInfo {
    int       id { 0 };            // 1-based, monotonic
    QDateTime when;
    QString   message;
    bool      keyframe { false };  // stored whole rather than as a delta
    qint64    bytes { 0 };         // compressed size on disk
};

// One changed key between two snapshots.
struct DocChange {
    enum Kind { Added, Removed, Modified };
    Kind    kind;
    QString key;
    QString before;
    QString after;
};

class DocHistory {
public:
    // `documentPath` is the file being tracked. History lives beside it, so an
    // unsaved document cannot have any: isUsable() is false and every call is a
    // no-op that reports why.
    explicit DocHistory(const QString& documentPath);

    [[nodiscard]] bool    isUsable() const { return !m_dir.isEmpty(); }
    [[nodiscard]] QString lastError() const { return m_error; }
    [[nodiscard]] QString storageDir() const { return m_dir; }

    // Newest last.
    [[nodiscard]] QVector<SnapshotInfo> snapshots() const;

    // Writes a new snapshot. Returns its id, or -1 on failure (see lastError).
    // Returns 0 and commits nothing when the content is identical to the tip,
    // so pressing Commit twice does not fill the log with empty versions.
    int commit(const DocSnapshot& snapshot, const QString& message);

    // Full content of a snapshot, rebuilt from its keyframe forward.
    [[nodiscard]] DocSnapshot reconstruct(int id, bool* ok = nullptr) const;

    // What changed between two snapshots. Pass 0 for `fromId` to diff against
    // an empty document.
    [[nodiscard]] QVector<DocChange> diff(int fromId, int toId, bool* ok = nullptr) const;

    // Convenience: changes between a snapshot and the live document.
    [[nodiscard]] QVector<DocChange> diffAgainst(int fromId, const DocSnapshot& current,
                                                 bool* ok = nullptr) const;

    // Delete every snapshot and the directory itself.
    bool clear();

private:
    [[nodiscard]] QString snapPath(int id) const;
    [[nodiscard]] bool    writeIndex(const QVector<SnapshotInfo>& list);
    [[nodiscard]] QVector<SnapshotInfo> readIndex() const;
    [[nodiscard]] bool    writePayload(int id, const QByteArray& json);
    [[nodiscard]] QByteArray readPayload(int id, bool* ok) const;

    QString         m_dir;      // empty when unusable
    mutable QString m_error;
};

// How often a full snapshot is written instead of a delta.
inline constexpr int kKeyframeInterval = 10;

// Serialisation helpers, exposed for testing.
[[nodiscard]] QByteArray  serializeSnapshot(const DocSnapshot& s);
[[nodiscard]] DocSnapshot deserializeSnapshot(const QByteArray& json, bool* ok = nullptr);
[[nodiscard]] QByteArray  serializeDelta(const DocSnapshot& from, const DocSnapshot& to);
[[nodiscard]] DocSnapshot applyDelta(const DocSnapshot& base, const QByteArray& json, bool* ok = nullptr);
[[nodiscard]] QVector<DocChange> compareSnapshots(const DocSnapshot& a, const DocSnapshot& b);

} // namespace NativeOffice
