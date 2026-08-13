#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AiQuota.h — how much of Stasis a plan is entitled to in a calendar month.
//
// Two ceilings run at once and either one can stop a request:
//
//                    file generations / month     characters / month
//   Free                     2                          500,000
//   Premium                 20                        5,000,000
//
// They are independent, so both readings of the rule hold: burning the whole
// character budget on the first document blocks the second even though a
// generation is left, and finishing both documents well under the character
// budget still ends the month, because the generation count is its own limit.
//
// A "generation" is one produced file (a document, a sheet, a deck). Answering
// a question costs neither counter: the limits exist to bound how much the
// assistant writes into your files, not how often you talk to it.
//
// Counted per account per calendar month, and read through the live entitlement
// so an upgrade applies immediately: usage already spent carries over and the
// ceiling rises, rather than the month restarting.
//
// This is the client's copy of the tally, which keeps the UI honest and the
// spend bounded. It is deliberately not the last word: the server sees every
// request and is the place a hard limit can actually be enforced, so treat this
// as the fast local mirror rather than the authority.
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>

namespace NativeOffice {

class AiQuota {
public:
    // Ceilings for the current plan.
    static int    generationLimit();
    static qint64 characterLimit();

    // Spent so far this calendar month.
    static int    generationsUsed();
    static qint64 charactersUsed();

    static int    generationsLeft();     // never negative
    static qint64 charactersLeft();      // never negative

    // True when another file may be produced: both counters still have room.
    static bool canGenerate();

    // Why canGenerate() is false, ready to show in the sidebar. Empty when a
    // generation is allowed.
    static QString blockedReason();

    // Book one produced file and the characters it wrote.
    static void recordGeneration(qint64 characters);

    // Book characters written without producing a new file (an edit to a
    // document that already exists). Spends the character budget only.
    static void recordCharacters(qint64 characters);

    // Human-readable summary for the sidebar footer, e.g. "2 of 20 left".
    static QString summaryText();

    // Test and support hook: clears this month's tally.
    static void resetMonth();

    // Period the counters belong to, as "YYYY-MM". Exposed for the sidebar's
    // "resets on <date>" hint and so tests can pin a month.
    static QString currentPeriod();

private:
    static QString keyFor(const QString& field);
    // Zeroes the counters when the stored period is not the current month.
    static void rollPeriodIfNeeded();
};

} // namespace NativeOffice
