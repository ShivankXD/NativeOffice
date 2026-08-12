#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AutoCorrect.h  (Tier 4 — AutoCorrect / smart quotes)
// Pure, testable helpers driving the Writer's as-you-type corrections:
//   • smart (curly) quotes & apostrophes, chosen from the preceding character;
//   • a common-typo replacement table (teh→the, …);
//   • "TWo INitial CApitals" repair and a standalone "i" → "I";
//   • (the sentence-capitalisation step needs document context, so PagedTextEdit
//      applies it on top of correctWord()).
// The editor calls these on word-commit (space / Enter / punctuation).
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>
#include <QHash>
#include <QChar>

namespace NativeOffice {

struct AutoCorrectSettings {
    bool smartQuotes         = true;
    bool capitalizeSentences = true;
    bool twoInitialCaps      = true;
    bool replaceTypos        = true;
    bool smartDashes         = true;
};

class AutoCorrect {
public:
    static const QHash<QString, QString>& typoMap();

    // Correct one word (typos, TWo-caps, i→I). Returns the word unchanged if
    // nothing applies. Case of the typo replacement follows the input.
    static QString correctWord(const QString& word, const AutoCorrectSettings& s);

    // The curly character to insert for a typed straight quote, given the
    // character immediately before the cursor (null/space ⇒ opening).
    static QChar smartQuote(QChar quote, QChar prevChar);

    static bool isSentenceEnd(QChar c) { return c == '.' || c == '!' || c == '?'; }

    // Whether the text preceding a word really ends a sentence, so the next
    // word should be capitalised. Testing the last character alone is not
    // enough: it capitalises the g in "e.g" and the s in "Mr. smith".
    // Single-letter initials and a list of common abbreviations are excluded,
    // the way Word's exception list works. An empty (block-start) string
    // counts as a sentence start.
    static bool endsSentence(const QString& textBefore);
};

} // namespace NativeOffice
