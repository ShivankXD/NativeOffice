#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SpellChecker.h
// Dictionary backend for the Writer's real spell checking.
//
//   • Loads a bundled English word list (dict/en_US.txt, ~370k words) into a
//     hash set. The load runs on a worker thread so opening the document never
//     blocks on it; ready() fires when the dictionary is usable.
//   • isCorrect()  — case-insensitive lookup, with handling for contractions,
//     possessives ('s), hyphenated compounds, all-caps acronyms and numbers.
//   • suggestions() — Norvig-style edit-distance-1/2 correction, ranked.
//   • Custom dictionary (Add to Dictionary) + session ignore list, both honoured
//     by isCorrect(); the custom list persists across runs.
//
// Single shared instance via SpellChecker::instance().
// ─────────────────────────────────────────────────────────────────────────────

#include <QObject>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <atomic>

namespace NativeOffice {

class SpellChecker : public QObject {
    Q_OBJECT

public:
    static SpellChecker* instance();

    // True once the main word list has finished loading. Until then isCorrect()
    // returns true for everything (so nothing is wrongly flagged mid-load).
    [[nodiscard]] bool isReady() const noexcept { return m_ready.load(); }

    // Kick off the (idempotent) async load. Safe to call repeatedly.
    void ensureLoaded();

    // Case-insensitive correctness test. Tokens with digits, all-caps acronyms
    // and 1-letter words are treated as correct (not flagged).
    [[nodiscard]] bool isCorrect(const QString& word) const;

    // Up to `limit` ranked correction candidates for a misspelling.
    [[nodiscard]] QStringList suggestions(const QString& word, int limit = 7) const;

    // Add to the persistent custom dictionary (survives restarts).
    void addToDictionary(const QString& word);
    // Ignore for this session only.
    void ignoreWord(const QString& word);

signals:
    void ready();   // emitted (queued, on the GUI thread) when loading finishes

private:
    explicit SpellChecker(QObject* parent = nullptr);

    void   loadBlocking();                       // runs on the worker thread
    bool   knownLower(const QString& lower) const;
    static QString customDictPath();
    void   loadCustom();

    QSet<QString>  m_words;        // main dictionary (lowercase)
    QSet<QString>  m_custom;       // user-added (lowercase)
    QSet<QString>  m_ignored;      // session ignores (lowercase)
    QSet<QString>  m_contractions; // common contractions (lowercase, with ')

    std::atomic<bool> m_ready   { false };
    std::atomic<bool> m_loading { false };
};

} // namespace NativeOffice
