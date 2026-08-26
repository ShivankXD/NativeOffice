// ─────────────────────────────────────────────────────────────────────────────
// AiMarkerSmoke.cpp
// Dev-only check on the rule that decides whether a Stasis reply goes into the
// open file or into the chat.
//
// This exists because the failure it guards against reached a user. Asked to
// build a presentation, the model wrote a sentence explaining what it was about
// to do and only then the [[DOCUMENT]] marker. The old rule required the marker
// to be the first characters of the whole reply, so the match failed, the reply
// was treated as conversation, and several hundred lines of slide JSON were
// printed into the chat panel while the deck stayed empty.
//
// Nothing about that failure is loud: no error, no crash, and the reply "looks
// like" the model answered. Only a person reading the panel notices.
// ─────────────────────────────────────────────────────────────────────────────
#include <QCoreApplication>
#include <QTextStream>

#include "ai/AiDocMarker.h"

using namespace NativeOffice;

static QTextStream out(stdout);
static int failures = 0;

static void check(bool ok, const QString& what) {
    out << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    out.flush();
    if (!ok) ++failures;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // ── The reply that was reported ──────────────────────────────────────────
    {
        const QString reply =
            "I don't have access to the specific content from nativeoffice.online or\n"
            "the Microsoft Store listing, so I'll build this deck from NativeOffice's\n"
            "general positioning as an offline-first productivity suite.\n"
            "\n"
            "[[DOCUMENT]]\n"
            "{\"op\":\"theme\",\"name\":\"midnight\"}\n"
            "{\"op\":\"slide\",\"layout\":\"title\",\"title\":\"NativeOffice\"}";

        QString content, preamble;
        const bool isDoc = aiIsDocumentReply(reply, &content, &preamble);
        check(isDoc, "a marker after a preamble is still a document reply");
        check(content.startsWith("{\"op\":\"theme\""),
              "the slide JSON is handed to the deck, not to the chat");
        check(!content.contains("[[DOCUMENT]]"), "the marker itself is stripped");
        check(preamble.startsWith("I don't have access"),
              "and what the model said first is kept for the transcript");
        check(!preamble.contains("{\"op\""), "with none of the JSON in it");
    }

    // ── The plain case, which has to keep working ───────────────────────────
    {
        const QString reply = "[[DOCUMENT]]\nHello world.";
        QString content, preamble;
        check(aiIsDocumentReply(reply, &content, &preamble),
              "a marker on the first line is a document reply");
        check(content == "Hello world.", "its content is everything after the line");
        check(preamble.isEmpty(), "and there is no preamble");
    }

    // ── Ordinary conversation must NOT be written into the file ─────────────
    {
        QString content;
        check(!aiIsDocumentReply("Sure, here is what I would put on those slides.", &content),
              "a reply with no marker stays conversation");
        check(aiDocMarkerAt("Ask me and I will answer.") < 0, "no marker, no offset");
    }

    // ── A marker mid-line is talking ABOUT the marker, not using it ─────────
    {
        QString content;
        check(!aiIsDocumentReply("Put [[DOCUMENT]] on a line of its own.", &content),
              "a marker mid-sentence does not trigger a document write");
    }

    // ── Leading whitespace on the marker line is tolerated ──────────────────
    {
        QString content, preamble;
        check(aiIsDocumentReply("Building it now.\n   [[DOCUMENT]]\nbody", &content, &preamble),
              "an indented marker line still counts");
        check(content == "body", "and its content is read correctly");
    }

    // ── Carriage returns, because replies arrive over the wire ──────────────
    {
        QString content;
        check(aiIsDocumentReply("Here you go.\r\n[[DOCUMENT]]\r\n{\"op\":\"theme\"}",
                                &content),
              "a CRLF reply is handled");
        check(content.startsWith("{\"op\""), "and its content survives the line ending");
    }

    // ── An unfinished marker line yields nothing yet ────────────────────────
    {
        QString content;
        check(aiIsDocumentReply("[[DOCUMENT]]", &content),
              "a marker with nothing after it is still recognised");
        check(content.isEmpty(),
              "but yields no content until the line is finished");
    }

    out << "\n" << (failures ? QString("%1 FAILURE(S)").arg(failures)
                             : QStringLiteral("all checks passed")) << "\n";
    out.flush();
    return failures ? 1 : 0;
}
