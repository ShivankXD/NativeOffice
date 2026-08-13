#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AiConsent.h — the one-time notice standing between a user and the Stasis
// sidebar.
//
// The sidebar is the only part of NativeOffice that sends anything anywhere:
// every other feature works offline once signed in. That difference is worth
// stating plainly and getting an answer to before a single prompt leaves the
// machine, so the notice is a gate rather than a banner.
//
// Three states, and only one of them opens the sidebar:
//   Unanswered  never shown, or shown and dismissed without choosing
//   Declined    the user said no
//   Accepted    the user said yes
//
// Unanswered and Declined behave identically at the gate: Use AI re-shows the
// notice. Declining is therefore not a permanent lockout the user cannot undo,
// it just means nothing happens until they accept, which is what was asked for
// ("as long as accept is not clicked the sidebar can never be used").
//
// Recorded per account rather than per machine, because the promise is made to
// a person. Signing in as someone else asks that person for themselves.
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>

namespace NativeOffice {

class AiConsent {
public:
    enum class State { Unanswered, Declined, Accepted };

    // State for the currently signed-in account.
    static State state();
    static bool  accepted() { return state() == State::Accepted; }

    static void recordAccepted();
    static void recordDeclined();

    // Test and support hook: forgets the answer so the notice appears again.
    static void reset();

private:
    // QSettings key for the active account. Scoped by a digest of the email so
    // the stored key cannot leak the address into a plain-text settings file.
    static QString settingsKey();
};

} // namespace NativeOffice
