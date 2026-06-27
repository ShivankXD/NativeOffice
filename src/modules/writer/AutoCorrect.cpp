// ─────────────────────────────────────────────────────────────────────────────
// AutoCorrect.cpp  (Tier 4 — AutoCorrect / smart quotes)
// ─────────────────────────────────────────────────────────────────────────────
#include "AutoCorrect.h"

namespace NativeOffice {

const QHash<QString, QString>& AutoCorrect::typoMap() {
    static const QHash<QString, QString> m = {
        {"teh","the"}, {"adn","and"}, {"taht","that"}, {"thier","their"},
        {"recieve","receive"}, {"recieved","received"}, {"seperate","separate"},
        {"definately","definitely"}, {"occured","occurred"}, {"occuring","occurring"},
        {"untill","until"}, {"wich","which"}, {"becuase","because"}, {"becasue","because"},
        {"alot","a lot"}, {"acheive","achieve"}, {"beleive","believe"},
        {"calender","calendar"}, {"cant","can't"}, {"dont","don't"}, {"doesnt","doesn't"},
        {"didnt","didn't"}, {"wont","won't"}, {"isnt","isn't"}, {"wasnt","wasn't"},
        {"couldnt","couldn't"}, {"shouldnt","shouldn't"}, {"wouldnt","wouldn't"},
        {"im","I'm"}, {"ive","I've"}, {"id","I'd"}, {"ill","I'll"},
        {"youre","you're"}, {"theyre","they're"}, {"wasnt","wasn't"},
        {"enviroment","environment"}, {"goverment","government"}, {"neccessary","necessary"},
        {"occassion","occasion"}, {"persue","pursue"}, {"priviledge","privilege"},
        {"reccomend","recommend"}, {"refered","referred"}, {"tommorow","tomorrow"},
        {"tommorrow","tomorrow"}, {"wierd","weird"}, {"writting","writing"},
        {"accross","across"}, {"agaisnt","against"}, {"arguement","argument"},
        {"begining","beginning"}, {"beleive","believe"}, {"comming","coming"},
        {"completly","completely"}, {"concious","conscious"}, {"embarass","embarrass"},
        {"existance","existence"}, {"familar","familiar"}, {"finaly","finally"},
        {"foriegn","foreign"}, {"freind","friend"}, {"gaurd","guard"},
        {"happend","happened"}, {"intresting","interesting"}, {"knowlege","knowledge"},
        {"libary","library"}, {"maintainance","maintenance"}, {"managment","management"},
        {"propoganda","propaganda"}, {"realy","really"}, {"succesful","successful"},
        {"suprise","surprise"}, {"truely","truly"}, {"unfortunatly","unfortunately"},
        {"usefull","useful"}, {"wether","whether"}, {"wich","which"},
        {"hte","the"}, {"nad","and"}, {"tihs","this"}, {"thsi","this"},
    };
    return m;
}

QString AutoCorrect::correctWord(const QString& word, const AutoCorrectSettings& s) {
    if (word.isEmpty()) return word;
    QString w = word;

    // Common typos (case-insensitive; keep the leading capital if the user used one).
    if (s.replaceTypos) {
        auto it = typoMap().constFind(w.toLower());
        if (it != typoMap().constEnd()) {
            QString repl = it.value();
            if (!w.isEmpty() && w.at(0).isUpper() && !repl.isEmpty())
                repl[0] = repl[0].toUpper();
            w = repl;
        }
    }

    // Standalone "i" → "I".
    if (w == QLatin1String("i")) w = QStringLiteral("I");

    // TWo INitial CApitals → Two Initial Capitals (lowercase the 2nd letter).
    if (s.twoInitialCaps && w.size() >= 3
        && w.at(0).isUpper() && w.at(1).isUpper() && w.at(2).isLower()) {
        bool restLower = true;
        for (int i = 2; i < w.size(); ++i)
            if (w.at(i).isLetter() && w.at(i).isUpper()) { restLower = false; break; }
        if (restLower) w[1] = w.at(1).toLower();
    }

    return w;
}

QChar AutoCorrect::smartQuote(QChar quote, QChar prevChar) {
    const bool opening = prevChar.isNull() || prevChar.isSpace()
                       || prevChar == QLatin1Char('(') || prevChar == QLatin1Char('[')
                       || prevChar == QLatin1Char('{')
                       || prevChar == QChar(0x2014) /* em dash */ || prevChar == QChar(0x2013);
    if (quote == '"')  return opening ? QChar(0x201C) : QChar(0x201D);   // “ ”
    if (quote == '\'') return opening ? QChar(0x2018) : QChar(0x2019);   // ‘ ’
    return quote;
}

} // namespace NativeOffice
