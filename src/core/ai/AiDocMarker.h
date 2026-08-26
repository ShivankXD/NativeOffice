#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AiDocMarker.h
// Deciding whether a Stasis reply is meant for the open file or for the chat.
//
// The model marks a reply destined for the document with [[DOCUMENT]] on a line
// of its own. Everything after that line is content; everything before it is
// the model talking, and belongs in the transcript.
//
// This lived inside AiSidebar.cpp as a startsWith() on the whole reply, which
// is what broke deck generation: asked to build a presentation, the model first
// explained what it was about to do ("I don't have access to that page, so I'll
// build the deck from ...") and only then wrote the marker. startsWith() failed,
// the reply was classified as conversation, and several hundred lines of slide
// JSON were printed into the chat while the deck stayed empty.
//
// It is a header of its own so the rule can be tested without a running panel:
// getting it wrong is silent, and the failure lands in front of a user as a
// wall of JSON.
// ─────────────────────────────────────────────────────────────────────────────

#include <QLatin1String>
#include <QString>

namespace NativeOffice {

inline QLatin1String aiDocMarker() { return QLatin1String("[[DOCUMENT]]"); }

// Offset of the marker, or -1. It must begin a LINE: leading spaces or tabs are
// tolerated, anything else on the line before it is not. Matching mid-line
// would let a reply that merely mentions the marker ("put [[DOCUMENT]] on its
// own line") be mistaken for content.
inline int aiDocMarkerAt(const QString& reply) {
    const QLatin1String marker = aiDocMarker();
    int from = 0;
    for (;;) {
        const int at = reply.indexOf(marker, from);
        if (at < 0) return -1;
        int i = at - 1;
        while (i >= 0 && (reply.at(i) == QLatin1Char(' ') || reply.at(i) == QLatin1Char('\t')))
            --i;
        if (i < 0 || reply.at(i) == QLatin1Char('\n') || reply.at(i) == QLatin1Char('\r'))
            return at;
        from = at + 1;
    }
}

// Is this reply destined for the document? When it is, *content receives what
// follows the marker line and *preamble whatever the model said before it.
//
// A marker with no newline after it yields empty content: the marker line is
// not finished, so nothing after it can be trusted yet.
inline bool aiIsDocumentReply(const QString& reply,
                              QString* content,
                              QString* preamble = nullptr) {
    const int at = aiDocMarkerAt(reply);
    if (at < 0) return false;
    if (preamble) *preamble = reply.left(at).trimmed();
    if (content) {
        QString rest = reply.mid(at + aiDocMarker().size());
        const int nl = rest.indexOf(QLatin1Char('\n'));
        rest = nl >= 0 ? rest.mid(nl + 1) : QString();
        *content = rest.trimmed();
    }
    return true;
}

} // namespace NativeOffice
