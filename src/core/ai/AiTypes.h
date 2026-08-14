#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AiTypes.h — the vocabulary shared by the Stasis sidebar, its client and the
// document agent. Kept header-only and dependency-light so the UI, the network
// layer and the per-module editing code can all speak it without any of them
// having to include each other.
// ─────────────────────────────────────────────────────────────────────────────

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace NativeOffice {

// Which surface of the app the sidebar is looking at. The sidebar itself is
// global (one instance, alive across every tab), so this is what tells it what
// it is allowed to do and what the "In <mode>" chip should read.
enum class AiMode {
    Home,
    Writer,
    Calc,
    Impress,
    Pdf,
    ImageResizer,
    MarkdownEditor,
    Unknown
};

// What the assistant may do in a given mode.
//
//   Answer  the model may read context and reply in the chat, nothing else.
//   Edit    the model may additionally build and change the open document.
//
// PDF and Image Resizer are deliberately Answer for now: both hold formats the
// agent has no safe write path into yet, and answering a question about a file
// cannot corrupt it. Home is Answer because there is no document behind it.
enum class AiCapability { Answer, Edit };

inline AiCapability capabilityFor(AiMode m) {
    switch (m) {
        case AiMode::Writer:
        case AiMode::Calc:
        case AiMode::Impress:
        case AiMode::MarkdownEditor:
            return AiCapability::Edit;
        case AiMode::Pdf:
        case AiMode::ImageResizer:
        case AiMode::Home:
        case AiMode::Unknown:
        default:
            return AiCapability::Answer;
    }
}

// Display name for the "In <mode>" chip above the chat.
inline QString modeName(AiMode m) {
    switch (m) {
        case AiMode::Home:           return QStringLiteral("Home");
        case AiMode::Writer:         return QStringLiteral("Writer");
        case AiMode::Calc:           return QStringLiteral("Sheets");
        case AiMode::Impress:        return QStringLiteral("Slides");
        case AiMode::Pdf:            return QStringLiteral("PDF");
        case AiMode::ImageResizer:   return QStringLiteral("Image Resizer");
        case AiMode::MarkdownEditor: return QStringLiteral("Markdown Editor");
        case AiMode::Unknown:
        default:                     return QStringLiteral("NativeOffice");
    }
}

// One attachment the user has pinned to the conversation. Images and documents
// are counted separately because the limits differ (5 images, 2 files), and the
// distinction is made once here rather than re-derived from the extension at
// every call site.
struct AiAttachment {
    enum class Kind { Image, Document };
    Kind    kind { Kind::Document };
    QString path;
    QString displayName;
    qint64  bytes { 0 };
};

// Ceilings on what can be pinned at once. Small on purpose: every attachment is
// uploaded with the prompt, and a request that carries tens of megabytes fails
// slowly and expensively rather than quickly and cheaply.
inline constexpr int kMaxImageAttachments    = 5;
inline constexpr int kMaxDocumentAttachments = 2;

// One place the assistant took information from while answering. Shown above
// the composer so the claim and its provenance are in the same glance, rather
// than the user having to take a generated document on trust.
struct AiSource {
    QString title;
    QString url;
    QString domain;      // shown when a title is missing or unhelpfully long
};

// A single turn in the conversation.
struct AiMessage {
    enum class Role { User, Assistant, System };
    Role      role { Role::User };
    QString   text;
    QDateTime at;
    QVector<AiAttachment> attachments;

    // Set on an assistant turn that changed the document, which is what puts
    // the Rollback control under it. Empty on a turn that only answered.
    QString   editLabel;
    bool      didEdit { false };
};

} // namespace NativeOffice
