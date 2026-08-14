#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AiDeckTheme.h — the look a generated deck commits to, chosen from its subject.
//
// The first version of slide generation had one palette. Every deck came out
// the same violet, whether it was about supercars, chemotherapy or medieval
// trade routes, and that single fact is most of why the output read as
// generated rather than designed. A deck about engines should be black and
// orange; a deck about clinical trials should be white and teal; a deck about
// the Renaissance should be cream and brick red with a serif.
//
// So a theme is picked once per deck and everything downstream inherits it:
// surfaces, ink, accent, the font pairing, how round the corners are, and which
// decorative motif the light slides carry. The model may name a theme outright;
// when it does not, the subject line is scored against each theme's vocabulary
// and the best match wins. Either way the choice is made once, on the opening
// slide, because a deck whose colours drift from slide to slide looks worse
// than one that picked wrongly and held its nerve.
// ─────────────────────────────────────────────────────────────────────────────

#include <QColor>
#include <QString>
#include <QVector>

namespace NativeOffice {

// Decoration carried by light slides. Enough to give a deck a recognisable
// signature without competing with the content; every one of these is drawn
// behind the text and at low contrast.
enum class DeckMotif {
    None,
    CornerArc,      // a large tinted disc bleeding off the top-right corner
    DiagonalBand,   // a raked accent bar entering from the right edge
    DotGrid,        // a small field of dots in the lower-right
    SideRule,       // a tinted band and a hard accent rule down the left edge
    GradientWash,   // no items: the surface itself becomes a faint tint
    Frame,          // hairline rules top and bottom, clinical and precise
};

struct DeckTheme {
    QString name;

    // ── surfaces ────────────────────────────────────────────────────────────
    QColor paper;      // light slide background
    QColor paperTint;  // second stop for GradientWash, and card fills
    QColor deep;       // dark slide background, top of the gradient
    QColor deep2;      // dark slide background, bottom of the gradient

    // ── ink ─────────────────────────────────────────────────────────────────
    QColor ink;        // headings on paper
    QColor body;       // body copy on paper
    QColor muted;      // labels, captions, eyebrows
    QColor onDeep;     // body copy on a dark surface

    // ── accent ──────────────────────────────────────────────────────────────
    QColor accent;
    QColor accent2;    // a second note, for the far end of a chart or a duotone

    // ── type ────────────────────────────────────────────────────────────────
    QString headFont;
    QString bodyFont;
    qreal   titlePt   { 54 };   // opening slide
    qreal   headPt    { 28 };   // ordinary slide heading
    bool    eyebrowUpper { true };  // small labels set in capitals

    // ── shape personality ───────────────────────────────────────────────────
    // Corner radius in canvas pixels. Zero is a hard modernist edge; 18 is the
    // soft card look. Converted to the cornerAdj fraction at placement, since
    // that is what a rounded rectangle actually takes.
    qreal   radius { 12 };
    DeckMotif motif { DeckMotif::GradientWash };

    // Words that pull a subject toward this theme. Not shown anywhere.
    QVector<QString> vocabulary;
};

// Every theme, in the order they are offered to the model.
const QVector<DeckTheme>& deckThemes();

// The theme to use for a deck. `named` is what the model asked for and wins
// outright when it matches; otherwise the deck's own words are scored against
// each theme's vocabulary. `headline` is the title, subtitle and kicker of the
// opening slide and counts for far more than `context`, which is the body and
// the speaker's notes: a deck called "Sleep and the Immune System" is about
// medicine even if the notes happen to mention a system or a platform. Always
// returns a usable theme.
DeckTheme themeFor(const QString& named, const QString& headline,
                   const QString& context = QString());

// A colour blended toward another by `t` in 0..1. Used constantly: a tint of
// the accent over paper, a card lifted off a dark surface, a rule that reads as
// present without shouting.
QColor mixed(const QColor& a, const QColor& b, qreal t);

// The same colour at a given alpha, for washes and scrims.
QColor alpha(const QColor& c, int a);

} // namespace NativeOffice
