// ─────────────────────────────────────────────────────────────────────────────
// RecoverySmoke.cpp
// Dev-only check for the crash-recovery snapshots (core/common/CrashRecovery).
//
// The thing being guarded is unusual: the feature only ever matters when the
// process died, so nobody exercises it by hand and a break in it is invisible
// until someone loses work. The rules that are easy to get wrong are all here:
//
//   * two untitled documents must not share a snapshot file (Writer's old bug)
//   * a snapshot is skipped when nothing changed, so a large workbook is not
//     re-serialized every tick for nothing
//   * saving retires the snapshot, and rebinds to the saved path, so a crash
//     afterwards does not offer work the user already has on disk
//   * a clean exit leaves nothing behind
//   * one leftover is offered once, to one document
//
// Prints a line per check and exits non-zero on any failure.
// ─────────────────────────────────────────────────────────────────────────────
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include "core/common/CrashRecovery.h"

using NativeOffice::CrashRecovery;

static QTextStream out(stdout);
static int failures = 0;

static void check(bool ok, const QString& what) {
    out << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    out.flush();
    if (!ok) ++failures;
}

static QString recoveryDir() {
    QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (d.isEmpty()) d = QDir::tempPath();
    return d + "/recovery";
}

// Snapshots for one editor kind, so a run cannot see another test's leftovers.
static QStringList snapshots(const QString& kind) {
    return QDir(recoveryDir()).entryList({ kind + "-*.noff" }, QDir::Files, QDir::Name);
}

static void clear(const QString& kind) {
    QDir d(recoveryDir());
    for (const QString& f : snapshots(kind)) d.remove(f);
}

// A stand-in document: a counter that moves on "edit" and some bytes.
struct FakeDoc {
    quint64 rev { 0 };
    QByteArray body { "<!-- NativeOffice Test (.noff) -->\n{}" };
    void edit(const QByteArray& b) { body = b; ++rev; }
};

static void bind(CrashRecovery& r, FakeDoc& d) {
    r.setSource([&d] { return d.rev; }, [&d] { return d.body; });
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("NativeOffice");
    QCoreApplication::setApplicationName("NativeOffice");

    const QString kind = "smoketest";
    clear(kind);

    // ── Two untitled documents must not share a file ────────────────────────
    out << "\n[untitled] two blank documents keep separate snapshots\n";
    {
        FakeDoc a, b;
        CrashRecovery ra(kind, nullptr), rb(kind, nullptr);
        bind(ra, a); bind(rb, b);

        a.edit("<!-- t -->\nFIRST");
        b.edit("<!-- t -->\nSECOND");
        ra.snapshot();
        rb.snapshot();

        const QStringList files = snapshots(kind);
        check(files.size() == 2,
              QString("two snapshots on disk (got %1)").arg(files.size()));

        // Whichever order they are found in, both documents survived intact.
        QSet<QByteArray> bodies;
        for (const QString& f : files) {
            QFile fh(recoveryDir() + "/" + f);
            if (fh.open(QIODevice::ReadOnly)) bodies.insert(fh.readAll());
        }
        check(bodies.contains("<!-- t -->\nFIRST")
              && bodies.contains("<!-- t -->\nSECOND"),
              "neither document overwrote the other");
    }
    clear(kind);

    // ── Nothing changed means nothing written ───────────────────────────────
    out << "\n[skip] an unchanged document is not re-serialized\n";
    {
        FakeDoc d;
        int serialized = 0;
        CrashRecovery r(kind, nullptr);
        r.setSource([&d] { return d.rev; },
                    [&d, &serialized]() -> QByteArray { ++serialized; return d.body; });

        r.snapshot();                       // no edit yet
        check(serialized == 0, "no edit, so the document was never serialized");
        check(snapshots(kind).isEmpty(), "and nothing was written");

        d.edit("<!-- t -->\nONE");
        r.snapshot();
        check(serialized == 1, "one edit, serialized once");
        r.snapshot(); r.snapshot();
        check(serialized == 1, "two more ticks with no edit, still serialized once");

        d.edit("<!-- t -->\nTWO");
        r.snapshot();
        check(serialized == 2, "another edit, serialized again");
    }
    clear(kind);

    // ── Saving retires the snapshot and rebinds ─────────────────────────────
    out << "\n[save] saving an untitled document retires its snapshot\n";
    {
        FakeDoc d;
        CrashRecovery r(kind, nullptr);
        bind(r, d);
        d.edit("<!-- t -->\nWORK");
        r.snapshot();
        check(snapshots(kind).size() == 1, "untitled snapshot written");

        // What the module does on a successful save.
        const QString saved = QDir::tempPath() + "/recovery-smoke-doc.noff";
        r.setDocumentPath(saved);
        r.discard();
        check(snapshots(kind).isEmpty(),
              "after saving, no snapshot is left to offer");

        // Later edits belong to the saved file, not to "untitled".
        d.edit("<!-- t -->\nMORE");
        r.snapshot();
        const QStringList files = snapshots(kind);
        check(files.size() == 1 && !files.first().contains("untitled"),
              QString("later snapshots follow the saved file (got %1)")
                  .arg(files.join(", ")));

        r.discard();
    }
    clear(kind);

    // ── A leftover is offered once, to one document ─────────────────────────
    out << "\n[leftover] a crash leaves one snapshot, claimed once\n";
    {
        {   // The session that "crashed": snapshot, then never discard.
            FakeDoc d;
            CrashRecovery r(kind, nullptr);
            bind(r, d);
            d.edit("<!-- t -->\nRECOVER ME");
            r.snapshot();
        }
        check(snapshots(kind).size() == 1, "the snapshot outlived its session");

        CrashRecovery fresh(kind, nullptr);
        const QByteArray got = fresh.takeLeftover();
        check(got == "<!-- t -->\nRECOVER ME", "the next launch reads it back");
        check(snapshots(kind).isEmpty(), "and the file is consumed, not re-offered");

        CrashRecovery second(kind, nullptr);
        check(second.takeLeftover().isEmpty(),
              "a second document is not offered the same work twice");
    }
    clear(kind);

    // ── A named document only ever sees its own ─────────────────────────────
    out << "\n[named] a document is never offered another document's work\n";
    {
        const QString mine  = QDir::tempPath() + "/recovery-smoke-mine.noff";
        const QString other = QDir::tempPath() + "/recovery-smoke-other.noff";
        {
            FakeDoc d;
            CrashRecovery r(kind, nullptr);
            r.setDocumentPath(other);
            bind(r, d);
            d.edit("<!-- t -->\nOTHER DOC");
            r.snapshot();
        }
        CrashRecovery r(kind, nullptr);
        r.setDocumentPath(mine);
        check(r.takeLeftover().isEmpty(), "a different file's snapshot is not offered");

        CrashRecovery r2(kind, nullptr);
        r2.setDocumentPath(other);
        check(r2.takeLeftover() == "<!-- t -->\nOTHER DOC",
              "the file it belongs to does get it");
    }
    clear(kind);

    // ── A clean exit leaves nothing ─────────────────────────────────────────
    out << "\n[clean exit] quitting normally leaves nothing to recover\n";
    {
        FakeDoc d;
        auto* r = new CrashRecovery(kind, nullptr, &app);
        bind(*r, d);
        d.edit("<!-- t -->\nTYPED");
        r->snapshot();
        check(snapshots(kind).size() == 1, "snapshot exists while running");
        // aboutToQuit is a private signal, so this exercises the real thing:
        // run the event loop and leave it the way a normal shutdown does.
        QTimer::singleShot(0, &app, [] { QCoreApplication::quit(); });
        app.exec();
        check(snapshots(kind).isEmpty(), "and is gone after a clean exit");
        delete r;
    }
    clear(kind);

    out << "\n" << (failures ? QString("%1 FAILURE(S)").arg(failures)
                             : QStringLiteral("all checks passed")) << "\n";
    out.flush();
    return failures ? 1 : 0;
}
