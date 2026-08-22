#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// CrashRecovery.h
// Periodic snapshots of unsaved work, so a crash does not lose it.
//
// The case this exists for is the untitled document. A named document at least
// has a file on disk holding the last explicit save; an untitled one has
// nothing at all, so until it is saved by hand every word typed into it lives
// only in memory. Calc and Impress had no periodic snapshot of any kind, and
// Writer had one that all untitled documents shared, so with two of them open
// each overwrote the other's.
//
// A snapshot NEVER lands on the document's own file. It goes to a private
// directory under AppData, always in NativeOffice's own .noff format whatever
// the document was opened from. That is deliberate and not an implementation
// detail: an automatic write aimed at a .xlsx destroyed real customer files
// once already (see the 1.7.4 notes), and an automatic write that can only
// ever produce a .noff inside a directory this application owns cannot.
//
// Lifecycle:
//   * every `seconds`, if the document changed since the last snapshot, write
//   * a clean save, or a clean application exit, drops the snapshot
//   * on open, a snapshot left behind by a session that did not exit cleanly
//     is offered to the user
// ─────────────────────────────────────────────────────────────────────────────

#include <QByteArray>
#include <QObject>
#include <QString>
#include <functional>

class QTimer;
class QWidget;

namespace NativeOffice {

class CrashRecovery : public QObject {
    Q_OBJECT
public:
    // `kind` is "writer" / "calc" / "impress". It namespaces the snapshots so
    // one editor never offers another's, and names the document in the prompt.
    CrashRecovery(QString kind, QWidget* dialogParent, QObject* parent = nullptr);
    ~CrashRecovery() override;

    // How to read the document.
    //
    // `revision` is any counter that moves when the document changes. It is
    // checked on every tick and serialization is skipped when it has not moved,
    // because serializing a large workbook every twenty seconds to write bytes
    // identical to the last ones is a stall the user would feel.
    //
    // `serialize` returns the document as .noff bytes, and is only ever called
    // when there is something new to write.
    void setSource(std::function<quint64()> revision,
                   std::function<QByteArray()> serialize);

    // The file this document is bound to; empty means untitled. Changing it
    // (a Save As, or the first save of an untitled document) drops the snapshot
    // written under the old identity, so it cannot be offered again later.
    void setDocumentPath(const QString& path);

    void start(int seconds);
    void snapshot();      // write now, if anything changed
    void discard();       // this document's snapshot is superseded

    // Offer a snapshot left behind by a session that did not exit cleanly.
    //
    // For a named document that is the snapshot for that same file. For an
    // untitled one it is the oldest untitled snapshot no one has claimed yet,
    // so opening two blank documents after a crash offers two different ones
    // rather than the same one twice.
    //
    // Returns the recovered bytes when the user accepts. Either way the file is
    // claimed and removed, so it is offered exactly once.
    bool offerLeftover(QByteArray& out);

    // The same search, claim and read with no prompt attached.
    //
    // offerLeftover() is this plus a question. They are separate so the rules
    // that are easy to get wrong (which snapshot belongs to which document,
    // claiming one so it is not offered twice) can be tested without a modal
    // dialog in the way. Returns empty when there is nothing to recover.
    QByteArray takeLeftover();

private:
    QString directory() const;
    QString filePath() const;      // where THIS document writes

    QString  m_kind;
    QWidget* m_parent { nullptr };
    QString  m_docPath;            // empty => untitled
    QString  m_untitledId;         // unique per instance, for untitled docs
    QTimer*  m_timer { nullptr };

    std::function<quint64()>    m_revision;
    std::function<QByteArray()> m_serialize;
    quint64 m_lastRevision { 0 };
    bool    m_wrote { false };     // a snapshot for this identity is on disk
};

} // namespace NativeOffice
