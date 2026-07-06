// ─────────────────────────────────────────────────────────────────────────────
// PdfEditSession.cpp — see PdfEditSession.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfEditSession.h"

#include <QFile>
#include <QFileInfo>

namespace NativeOffice::Pdf {

EditSession::EditSession(QObject* parent)
    : QObject(parent)
{
}

EditSession::~EditSession() = default;

QString EditSession::currentRevisionPath() const {
    return (m_cursor >= 0 && m_cursor < int(m_revisions.size()))
               ? m_revisions[size_t(m_cursor)] : QString();
}

QString EditSession::newRevisionPath() {
    return m_tempDir.filePath(QStringLiteral("rev%1.pdf").arg(m_revSeq++));
}

bool EditSession::openFile(const QString& path, const QString& password,
                           QString* error, bool* needsPassword) {
    if (needsPassword) *needsPassword = false;

    RenderOpenStatus st{};
    auto renderer = Renderer::open(path, password, st);
    if (!renderer) {
        switch (st) {
            case RenderOpenStatus::FileNotFound:
                if (error) *error = tr("File not found.");
                break;
            case RenderOpenStatus::PasswordRequired:
                if (needsPassword) *needsPassword = true;
                if (error) *error = password.isEmpty()
                        ? tr("This PDF is password-protected.")
                        : tr("Wrong password.");
                break;
            case RenderOpenStatus::Corrupt:
                if (error) *error = tr("The file is damaged or not a valid PDF.");
                break;
            default:
                if (error) *error = tr("The file could not be opened.");
                break;
        }
        return false;
    }

    if (!m_tempDir.isValid()) {
        if (error) *error = tr("Could not create a working folder for edits.");
        return false;
    }

    // Revision 0: encrypted originals become a decrypted working copy so the
    // structural reader/writer can process later edits; plain files start
    // from the original path directly (no copy until the first edit).
    QString rev0 = path;
    if (!password.isEmpty()) {
        rev0 = newRevisionPath();
        if (!renderer->saveDecryptedCopy(rev0)) {
            if (error) *error = tr("Could not prepare an editable copy of the encrypted file.");
            return false;
        }
        // Re-open the renderer on the decrypted copy so renderer state and
        // revision state always describe the same bytes.
        RenderOpenStatus st2{};
        auto reopened = Renderer::open(rev0, {}, st2);
        if (!reopened) {
            if (error) *error = tr("Could not re-open the decrypted working copy.");
            return false;
        }
        renderer = std::move(reopened);
    }

    m_filePath = path;
    m_password = password;
    m_renderer = std::move(renderer);
    m_revisions.clear();
    m_revisions.push_back(rev0);
    m_cursor = 0;
    setDirty(false);

    emit filePathChanged(m_filePath);
    emit documentChanged();
    return true;
}

bool EditSession::reopenRenderer(QString* error) {
    RenderOpenStatus st{};
    auto r = Renderer::open(currentRevisionPath(), {}, st);
    if (!r) {
        if (error) *error = tr("Internal error: the edited file could not be re-opened.");
        return false;
    }
    m_renderer = std::move(r);
    return true;
}

OpResult EditSession::apply(const QString& opLabel,
                            const std::function<OpResult(const QString&, const QString&)>& op) {
    OpResult res;
    if (!hasDocument()) {
        res.message = tr("No document is open.");
        return res;
    }

    const QString out = newRevisionPath();
    res = op(currentRevisionPath(), out);
    if (!res.ok) {
        QFile::remove(out);
        return res;
    }

    // Drop any redo tail, then push the new head revision.
    m_revisions.resize(size_t(m_cursor) + 1);
    m_revisions.push_back(out);
    ++m_cursor;

    QString err;
    if (!reopenRenderer(&err)) {
        // Roll back to the revision that was fine.
        m_revisions.pop_back();
        --m_cursor;
        reopenRenderer(nullptr);
        res.ok = false;
        res.message = err.isEmpty() ? tr("%1 failed.").arg(opLabel) : err;
        return res;
    }

    setDirty(true);
    emit documentChanged();
    return res;
}

bool EditSession::undo() {
    if (!canUndo()) return false;
    --m_cursor;
    if (!reopenRenderer(nullptr)) { ++m_cursor; reopenRenderer(nullptr); return false; }
    setDirty(m_cursor != 0);
    emit documentChanged();
    return true;
}

bool EditSession::redo() {
    if (!canRedo()) return false;
    ++m_cursor;
    if (!reopenRenderer(nullptr)) { --m_cursor; reopenRenderer(nullptr); return false; }
    setDirty(true);
    emit documentChanged();
    return true;
}

bool EditSession::save(QString* error) {
    return saveAs(m_filePath, error);
}

bool EditSession::saveAs(const QString& target, QString* error) {
    if (!hasDocument()) {
        if (error) *error = tr("No document is open.");
        return false;
    }
    const QString src = currentRevisionPath();
    if (QFileInfo(src) != QFileInfo(target)) {
        // Write via a temp sibling + rename so a failed copy never truncates
        // the destination.
        const QString tmp = target + QStringLiteral(".nosave");
        QFile::remove(tmp);
        if (!QFile::copy(src, tmp)) {
            if (error) *error = tr("Could not write to \"%1\".").arg(target);
            return false;
        }
        QFile::remove(target);
        if (!QFile::rename(tmp, target)) {
            QFile::remove(tmp);
            if (error) *error = tr("Could not replace \"%1\".").arg(target);
            return false;
        }
    }
    if (m_filePath != target) {
        m_filePath = target;
        emit filePathChanged(m_filePath);
    }
    setDirty(false);
    return true;
}

void EditSession::setDirty(bool dirty) {
    if (m_dirty == dirty) return;
    m_dirty = dirty;
    emit dirtyChanged(dirty);
}

} // namespace NativeOffice::Pdf
