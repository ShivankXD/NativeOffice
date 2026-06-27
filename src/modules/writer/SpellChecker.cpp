// ─────────────────────────────────────────────────────────────────────────────
// SpellChecker.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "SpellChecker.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <thread>

namespace NativeOffice {

namespace {
// Common English contractions/possessive forms missing from an alpha-only list.
const char* kContractions[] = {
    "i'm","i've","i'll","i'd","you're","you've","you'll","you'd","we're","we've",
    "we'll","we'd","they're","they've","they'll","they'd","he's","he'll","he'd",
    "she's","she'll","she'd","it's","it'll","that's","that'll","there's","there'll",
    "here's","what's","what're","who's","who'll","where's","when's","why's","how's",
    "let's","don't","doesn't","didn't","isn't","aren't","wasn't","weren't","haven't",
    "hasn't","hadn't","won't","wouldn't","can't","couldn't","shouldn't","mustn't",
    "mightn't","needn't","shan't","ain't","y'all","ma'am","o'clock"
};
} // namespace

SpellChecker* SpellChecker::instance() {
    static SpellChecker s_instance;
    return &s_instance;
}

SpellChecker::SpellChecker(QObject* parent) : QObject(parent) {
    for (const char* c : kContractions) m_contractions.insert(QString::fromLatin1(c));
    loadCustom();
}

QString SpellChecker::customDictPath() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) dir = QDir::homePath();
    QDir().mkpath(dir);
    return dir + "/writer_custom_dict.txt";
}

void SpellChecker::loadCustom() {
    QFile f(customDictPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream ts(&f);
    while (!ts.atEnd()) {
        const QString w = ts.readLine().trimmed().toLower();
        if (!w.isEmpty()) m_custom.insert(w);
    }
}

void SpellChecker::ensureLoaded() {
    if (m_ready.load() || m_loading.exchange(true)) return;
    // Load on a detached worker thread so the GUI never stalls on the ~370k-word
    // file. ready() is emitted across threads → queued to the GUI thread, where
    // this object lives, so the connected rehighlight runs safely there.
    std::thread([this] {
        loadBlocking();
        m_ready.store(true);
        emit ready();
    }).detach();
}

void SpellChecker::loadBlocking() {
    // Search a few likely locations for the bundled dictionary.
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + "/dictionaries/en_US.txt",
        appDir + "/en_US.txt",
        appDir + "/../share/nativeoffice/en_US.txt",
        QStringLiteral(SPELL_DICT_SOURCE_PATH),   // configured source path (dev fallback)
    };

    QString path;
    for (const QString& c : candidates) {
        if (QFileInfo::exists(c)) { path = c; break; }
    }
    if (path.isEmpty()) return;   // stays not-ready → nothing gets flagged

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QSet<QString> words;
    words.reserve(400000);
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    QString line;
    while (ts.readLineInto(&line)) {
        if (!line.isEmpty()) words.insert(line.trimmed().toLower());
    }
    words.remove(QString());
    m_words = std::move(words);
}

bool SpellChecker::knownLower(const QString& lower) const {
    return m_words.contains(lower) || m_custom.contains(lower)
        || m_ignored.contains(lower) || m_contractions.contains(lower);
}

bool SpellChecker::isCorrect(const QString& word) const {
    if (!m_ready.load()) return true;          // don't flag while loading
    if (word.isEmpty()) return true;

    // Skip tokens containing digits (e.g. "v2", "3rd", "h2o").
    for (const QChar& ch : word)
        if (ch.isDigit()) return true;

    // Skip 1-letter words and all-caps acronyms (USA, HTML, NASA…).
    if (word.size() == 1) return true;
    bool allUpper = true;
    for (const QChar& ch : word)
        if (ch.isLetter() && !ch.isUpper()) { allUpper = false; break; }
    if (allUpper && word.size() >= 2) return true;

    // Normalise curly apostrophes to straight ones.
    QString w = word;
    w.replace(QChar(0x2019), QLatin1Char('\''));

    const QString lower = w.toLower();
    if (knownLower(lower)) return true;

    // Possessive: "John's" → check "john"; also strip a stray trailing 's.
    const int ap = lower.indexOf(QLatin1Char('\''));
    if (ap > 0) {
        if (knownLower(lower.left(ap))) return true;            // base word
        if (knownLower(lower.mid(ap + 1))) return true;         // suffix (rare)
    }

    // Hyphenated compound: correct if every part is correct.
    if (lower.contains(QLatin1Char('-'))) {
        const QStringList parts = lower.split(QLatin1Char('-'), Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            bool all = true;
            for (const QString& p : parts) if (!knownLower(p)) { all = false; break; }
            if (all) return true;
        }
    }

    return false;
}

QStringList SpellChecker::suggestions(const QString& word, int limit) const {
    QStringList out;
    if (!m_ready.load() || word.isEmpty()) return out;

    QString w = word;
    w.replace(QChar(0x2019), QLatin1Char('\''));
    const QString lower = w.toLower();
    const bool initialCap = !word.isEmpty() && word.at(0).isUpper();

    const QString letters = QStringLiteral("abcdefghijklmnopqrstuvwxyz");

    auto edits1 = [&](const QString& s) {
        QSet<QString> res;
        const int n = s.size();
        for (int i = 0; i <= n; ++i) {
            if (i < n) res.insert(s.left(i) + s.mid(i + 1));                 // delete
            if (i < n - 1)                                                  // transpose
                res.insert(s.left(i) + s.at(i + 1) + s.at(i) + s.mid(i + 2));
            for (const QChar& c : letters) {
                if (i < n) res.insert(s.left(i) + c + s.mid(i + 1));        // replace
                res.insert(s.left(i) + c + s.mid(i));                       // insert
            }
        }
        res.remove(s);
        return res;
    };

    // Multiset character overlap — favours transpositions/reorderings (e.g. the
    // canonical "teh"→"the") and near-anagrams over candidates that merely share
    // a prefix ("teh"→"tea"). This is our stand-in for word-frequency ranking.
    auto shared = [](const QString& a, const QString& b) {
        QHash<QChar, int> cnt;
        for (const QChar& c : a) cnt[c]++;
        int s = 0;
        for (const QChar& c : b) {
            auto it = cnt.find(c);
            if (it != cnt.end() && it.value() > 0) { ++s; --it.value(); }
        }
        return s;
    };
    auto better = [&](const QString& a, const QString& b) {
        const int sa = shared(a, lower), sb = shared(b, lower);
        if (sa != sb) return sa > sb;                       // most shared letters first
        const bool fa = !a.isEmpty() && !lower.isEmpty() && a.at(0) == lower.at(0);
        const bool fb = !b.isEmpty() && !lower.isEmpty() && b.at(0) == lower.at(0);
        if (fa != fb) return fa;                            // same first letter
        const int da = qAbs(a.size() - lower.size());
        const int db = qAbs(b.size() - lower.size());
        if (da != db) return da < db;                       // closest length
        return a < b;                                       // stable alphabetical
    };

    QSet<QString> seen;
    auto take = [&](const QString& cand) {
        if (out.size() >= limit) return;
        if (!knownLower(cand) || seen.contains(cand)) return;
        seen.insert(cand);
        QString disp = cand;
        if (initialCap && !disp.isEmpty()) disp[0] = disp[0].toUpper();
        out << disp;
    };

    // Edit-distance 1 candidates, ordered to prefer same-first-letter / closer length.
    QStringList e1 = QStringList(edits1(lower).values());
    std::sort(e1.begin(), e1.end(), better);
    for (const QString& c : e1) take(c);

    // Edit-distance 2 only if we still need more (kept bounded for performance).
    if (out.size() < limit && lower.size() <= 14) {
        QSet<QString> e2known;
        for (const QString& m : edits1(lower)) {
            for (const QString& c : edits1(m))
                if (knownLower(c)) e2known.insert(c);
            if (e2known.size() > 200) break;
        }
        QStringList e2 = QStringList(e2known.values());
        std::sort(e2.begin(), e2.end(), better);
        for (const QString& c : e2) take(c);
    }

    return out;
}

void SpellChecker::addToDictionary(const QString& word) {
    const QString lower = word.trimmed().toLower();
    if (lower.isEmpty()) return;
    m_custom.insert(lower);
    QFile f(customDictPath());
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << lower << '\n';
    }
}

void SpellChecker::ignoreWord(const QString& word) {
    const QString lower = word.trimmed().toLower();
    if (!lower.isEmpty()) m_ignored.insert(lower);
}

} // namespace NativeOffice
