#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfEditSession.h — the PDF module's document model.
//
// Edits are whole-file revisions: every operation is a pure
// (inputPath → outputPath) transform (the same shape PdfOps already uses).
// apply() runs the transform from the current revision into a fresh temp
// file, pushes it onto the revision stack, and re-opens the renderer on the
// result. Undo/redo just move the revision cursor — no operation ever needs
// an inverse, and a failed transform leaves the document exactly as it was.
//
// Encrypted files: PDFium opens them with a password, and the session
// immediately materializes a decrypted revision (FPDF_SaveAsCopy) so the
// structural reader/writer — which reject encryption — can operate on it.
// Re-encrypting on save is the Protect tab's job (PdfCrypto).
// ─────────────────────────────────────────────────────────────────────────────

#include "PdfOps.h"
#include "PdfRenderer.h"

#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <functional>
#include <memory>
#include <vector>

namespace NativeOffice::Pdf {

class EditSession : public QObject {
    Q_OBJECT

public:
    explicit EditSession(QObject* parent = nullptr);
    ~EditSession() override;

    // Opens `path`. If the file is password-protected, `password` is required
    // (caller prompts and retries). Returns false with a user-facing reason.
    bool openFile(const QString& path, const QString& password, QString* error,
                  bool* needsPassword = nullptr);

    [[nodiscard]] bool     hasDocument()  const { return m_renderer != nullptr; }
    [[nodiscard]] QString  filePath()     const { return m_filePath; }
    [[nodiscard]] bool     isDirty()      const { return m_dirty; }
    [[nodiscard]] int      pageCount()    const { return m_renderer ? m_renderer->pageCount() : 0; }
    [[nodiscard]] Renderer* renderer()    const { return m_renderer.get(); }

    // Path of the current revision on disk — what ops read and Save copies.
    [[nodiscard]] QString currentRevisionPath() const;

    // Runs `op` current-revision → new-temp-file. On success the result
    // becomes the new head revision and documentChanged() fires.
    OpResult apply(const QString& opLabel,
                   const std::function<OpResult(const QString& in, const QString& out)>& op);

    [[nodiscard]] bool canUndo() const { return m_cursor > 0; }
    [[nodiscard]] bool canRedo() const { return m_cursor + 1 < int(m_revisions.size()); }
    bool undo();
    bool redo();

    bool save(QString* error);                          // to m_filePath
    bool saveAs(const QString& target, QString* error); // retargets m_filePath

signals:
    void documentChanged();          // new revision visible — re-render everything
    void dirtyChanged(bool dirty);
    void filePathChanged(const QString& path);

private:
    bool reopenRenderer(QString* error);
    QString newRevisionPath();
    void setDirty(bool dirty);

    QString m_filePath;              // user-visible document path
    QString m_password;              // kept for re-opens of encrypted originals

    QTemporaryDir m_tempDir;
    std::vector<QString> m_revisions;   // [0] = original (or decrypted copy)
    int m_cursor = -1;                  // index of current revision
    int m_revSeq = 0;
    bool m_dirty = false;

    std::unique_ptr<Renderer> m_renderer;
};

} // namespace NativeOffice::Pdf
