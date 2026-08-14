#include "AiDeckTheme.h"

#include <QRegularExpression>
#include <QSet>

namespace NativeOffice {

QColor mixed(const QColor& a, const QColor& b, qreal t) {
    t = qBound<qreal>(0, t, 1);
    return QColor::fromRgbF(a.redF()   + (b.redF()   - a.redF())   * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF()  + (b.blueF()  - a.blueF())  * t);
}

QColor alpha(const QColor& c, int a) {
    QColor out = c;
    out.setAlpha(qBound(0, a, 255));
    return out;
}

namespace {

DeckTheme make(const char* name,
               const char* paper, const char* paperTint,
               const char* deep, const char* deep2,
               const char* ink, const char* body, const char* muted, const char* onDeep,
               const char* accent, const char* accent2,
               const char* headFont, const char* bodyFont,
               qreal titlePt, qreal headPt, qreal radius,
               DeckMotif motif, const QStringList& vocabulary) {
    DeckTheme t;
    t.name      = QString::fromLatin1(name);
    t.paper     = QColor(QLatin1String(paper));
    t.paperTint = QColor(QLatin1String(paperTint));
    t.deep      = QColor(QLatin1String(deep));
    t.deep2     = QColor(QLatin1String(deep2));
    t.ink       = QColor(QLatin1String(ink));
    t.body      = QColor(QLatin1String(body));
    t.muted     = QColor(QLatin1String(muted));
    t.onDeep    = QColor(QLatin1String(onDeep));
    t.accent    = QColor(QLatin1String(accent));
    t.accent2   = QColor(QLatin1String(accent2));
    t.headFont  = QString::fromLatin1(headFont);
    t.bodyFont  = QString::fromLatin1(bodyFont);
    t.titlePt   = titlePt;
    t.headPt    = headPt;
    t.radius    = radius;
    t.motif     = motif;
    for (const QString& w : vocabulary) t.vocabulary.push_back(w);
    return t;
}

// Twelve of them, and they are meant to be visibly different from one another
// rather than twelve shades of the same idea. If two themes would produce decks
// a viewer could confuse, one of them is not earning its place.
const QVector<DeckTheme>& library() {
    static const QVector<DeckTheme> all = {
        // Machined, high contrast, a warning-light orange. Anything with an
        // engine, a factory floor or a chassis in it.
        make("carbon", "#F4F5F7", "#E9EBEF", "#0A0B0D", "#1A1D22",
             "#0D1013", "#3A4049", "#8A919B", "#DDE2E8",
             "#FF4D14", "#FFB020", "Segoe UI", "Segoe UI",
             56, 30, 4, DeckMotif::DiagonalBand,
             {"car", "cars", "engine", "engines", "vehicle", "vehicles", "automotive",
              "supercar", "motor", "chassis", "turbo", "horsepower", "racing", "race",
              "machine", "machinery", "manufacturing", "factory", "industrial",
              "mechanical", "hardware", "aerospace", "aviation", "drivetrain",
              "torque", "brake", "brakes", "suspension", "transmission", "piston"}),

        // Screen light on deep navy. Software, data, models, networks.
        make("midnight", "#F7F9FC", "#EEF3FA", "#070C1B", "#152A52",
             "#0B1220", "#39435A", "#8892A6", "#DCE6F7",
             "#22D3EE", "#7C5CFF", "Segoe UI", "Segoe UI",
             54, 29, 10, DeckMotif::DotGrid,
             {"software", "technology", "tech", "data", "ai", "artificial",
              "intelligence", "machine learning", "algorithm", "cloud", "platform",
              "api", "network", "networks", "cyber", "computing", "digital", "code",
              "developer", "engineering", "database", "analytics", "saas", "startup",
              "internet", "automation", "robotics", "encryption", "server",
              "open source", "neural", "dataset", "model training"}),

        // Cream stock, brick red, a serif. History, literature, culture, policy
        // essays: anything that would be at home as a printed page.
        make("editorial", "#FBF7EF", "#F2EADC", "#1A1512", "#33241C",
             "#1B1610", "#4A423A", "#8F857A", "#EFE6D8",
             "#A8321E", "#C08A2E", "Georgia", "Georgia",
             52, 30, 0, DeckMotif::SideRule,
             {"history", "historical", "literature", "literary", "culture", "cultural",
              "art", "arts", "philosophy", "civilisation", "civilization", "ancient",
              "medieval", "renaissance", "empire", "century", "heritage", "museum",
              "archaeology", "poetry", "novel", "author", "writing", "language",
              "tradition", "war", "revolution", "dynasty", "archive"}),

        // Field green. Climate, agriculture, conservation, sustainability.
        make("forest", "#F4F8F3", "#E7F1E6", "#0A1C13", "#16341F",
             "#101A13", "#3B4A40", "#83958A", "#DDEDE0",
             "#3FBF6F", "#A3C93A", "Segoe UI", "Segoe UI",
             54, 29, 16, DeckMotif::CornerArc,
             {"environment", "environmental", "climate", "sustainability",
              "sustainable", "nature", "ecology", "ecosystem", "forest", "farming",
              "agriculture", "crop", "crops", "soil", "biodiversity", "conservation",
              "renewable", "carbon", "emissions", "green", "wildlife", "plant",
              "plants", "botany", "organic", "recycling", "water", "biology"}),

        // Clean white and teal, hairline rules. Medicine, research, method.
        make("clinical", "#FFFFFF", "#F0F7F8", "#06202B", "#0C3F4E",
             "#0A1A1F", "#37525C", "#7E939B", "#D9EEF2",
             "#12B5A8", "#0EA5E9", "Segoe UI", "Segoe UI",
             52, 28, 6, DeckMotif::Frame,
             {"health", "healthcare", "medical", "medicine", "clinical", "patient",
              "disease", "treatment", "therapy", "hospital", "doctor", "nursing",
              "research", "study", "trial", "science", "scientific", "biology",
              "chemistry", "physics", "laboratory", "diagnosis", "vaccine", "genome",
              "anatomy", "surgery", "pharma", "nutrition", "epidemiology",
              "immune", "immunity", "sleep", "infection", "antibody", "cell",
              "cells", "symptom", "dose", "recovery", "wound", "cancer",
              "cardiac", "neuro", "patient care", "public health"}),

        // Warm sand and amber. Energy, growth, food, anything with heat in it.
        make("solar", "#FFF9F0", "#FCEFDC", "#2A1704", "#4E2C09",
             "#241705", "#4E4034", "#9A8B77", "#F7E7D2",
             "#F59E0B", "#E2542B", "Segoe UI", "Segoe UI",
             54, 29, 14, DeckMotif::GradientWash,
             {"energy", "solar", "power", "fuel", "oil", "gas", "electricity",
              "battery", "grid", "food", "cooking", "cuisine", "restaurant", "coffee",
              "growth", "marketing", "brand", "sales", "campaign", "advertising",
              "retail", "commerce", "customer", "hospitality", "tourism", "desert",
              "heat", "summer", "harvest"}),

        // Ink black, warm metal. Architecture, design, luxury, craft.
        make("mono", "#FAFAF9", "#EFEFED", "#0E0E0E", "#1F1F1E",
             "#0C0C0C", "#3E3E3C", "#8C8C88", "#E8E8E4",
             "#B99A5B", "#6B6B66", "Segoe UI", "Segoe UI",
             56, 30, 0, DeckMotif::None,
             {"design", "architecture", "architect", "interior", "luxury", "fashion",
              "craft", "furniture", "minimal", "typography", "photography", "studio",
              "gallery", "aesthetic", "watch", "jewellery", "jewelry", "couture",
              "portfolio", "creative direction", "brand identity", "product design"}),

        // Deep water and sky. Travel, logistics, geography, the sea.
        make("ocean", "#F2F8FC", "#E4F0F8", "#042538", "#0A4C71",
             "#08202E", "#37505F", "#7F95A4", "#D8ECF7",
             "#38BDF8", "#2DD4BF", "Segoe UI", "Segoe UI",
             54, 29, 14, DeckMotif::CornerArc,
             {"travel", "tourism", "ocean", "sea", "marine", "shipping", "port",
              "logistics", "supply chain", "freight", "transport", "geography",
              "island", "coast", "river", "weather", "navigation", "fishing",
              "voyage", "aviation route", "trade", "export", "import", "harbour",
              "harbor", "cruise", "expedition"}),

        // Alarm red on near-black. Sport, competition, security, risk, urgency.
        make("crimson", "#FDF6F6", "#F8E7E7", "#160406", "#3E0A11",
             "#170709", "#4B3A3C", "#9C8486", "#F6DFE1",
             "#EF3B47", "#F97316", "Segoe UI", "Segoe UI",
             56, 30, 6, DeckMotif::DiagonalBand,
             {"sport", "sports", "football", "cricket", "basketball", "olympic",
              "championship", "tournament", "athlete", "training", "fitness",
              "competition", "security", "threat", "risk", "fraud", "attack",
              "crisis", "emergency", "defence", "defense", "military", "crime",
              "safety", "incident", "breach", "fire"}),

        // Corporate blue on slate. Finance, strategy, consulting, law, policy.
        make("slate", "#F8FAFC", "#EDF2F8", "#0D1626", "#1E2E47",
             "#0F172A", "#3F4C61", "#8794A6", "#DEE7F3",
             "#3B82F6", "#8B5CF6", "Segoe UI", "Segoe UI",
             52, 28, 8, DeckMotif::SideRule,
             {"business", "finance", "financial", "revenue", "profit", "investment",
              "investor", "market", "strategy", "consulting", "corporate", "budget",
              "quarterly", "forecast", "banking", "insurance", "economy", "economic",
              "legal", "law", "policy", "governance", "compliance", "audit",
              "operations", "management", "roadmap", "stakeholder", "valuation"}),

        // Warm purple and pink. Teaching, wellbeing, community, the arts.
        make("bloom", "#FFF7FB", "#FBEAF3", "#280F3A", "#5A1E70",
             "#22102E", "#4C3B55", "#9585A0", "#F5E3F0",
             "#EC4899", "#FBBF24", "Segoe UI", "Segoe UI",
             54, 29, 20, DeckMotif::DotGrid,
             {"education", "school", "student", "students", "teaching", "teacher",
              "learning", "classroom", "curriculum", "university", "course",
              "children", "kids", "family", "wellbeing", "wellness", "mental health",
              "community", "charity", "volunteer", "music", "dance", "theatre",
              "theater", "festival", "celebration", "creativity", "storytelling"}),

        // The original look, kept as the general-purpose default. Product,
        // launches, anything that does not lean anywhere in particular.
        make("violet", "#F8F7FF", "#EEEBFC", "#120F2B", "#302266",
             "#11141C", "#3C4457", "#8A93A6", "#E9E6FA",
             "#7C5CFF", "#22D3EE", "Segoe UI", "Segoe UI",
             54, 28, 12, DeckMotif::GradientWash,
             {"product", "launch", "feature", "roadmap", "overview", "introduction",
              "team", "process", "workflow", "plan", "review", "update", "proposal"}),
    };
    return all;
}

// Splits a line into lowercase words, so "Supercars: The Anatomy of Speed" is
// scored on its words rather than as one string. A trailing "s" is dropped so
// "trials" matches "trial" and "patients" matches "patient": without it the
// scoring turned on whether the deck happened to use a plural, which is not
// something anyone would call a design decision.
QSet<QString> stemsOf(const QString& s) {
    static const QRegularExpression sep(QStringLiteral("[^a-z0-9]+"));
    QSet<QString> out;
    for (const QString& w : s.toLower().split(sep, Qt::SkipEmptyParts)) {
        out.insert(w);
        if (w.size() > 3 && w.endsWith(QLatin1Char('s'))) out.insert(w.chopped(1));
        if (w.size() > 4 && w.endsWith(QLatin1String("es"))) out.insert(w.chopped(2));
    }
    return out;
}

// One theme's score against one body of text, at the given weight per hit.
int scoreAgainst(const DeckTheme& t, const QSet<QString>& stems, const QString& flat,
                 int weight) {
    if (flat.trimmed().isEmpty()) return 0;
    int score = 0;
    for (const QString& term : t.vocabulary) {
        if (term.contains(QLatin1Char(' '))) {
            // Multi-word entries ("supply chain", "machine learning") are
            // matched against the whole line rather than the word list.
            if (flat.contains(term)) score += weight + 1;
        } else if (stems.contains(term)) {
            // An exact word is a much stronger signal than a substring: "car"
            // inside "carbon" or "scarce" is noise, and matching it sent every
            // deck to the automotive theme.
            score += weight;
        }
    }
    return score;
}

} // namespace

const QVector<DeckTheme>& deckThemes() { return library(); }

DeckTheme themeFor(const QString& named, const QString& headline,
                   const QString& context) {
    const QVector<DeckTheme>& all = library();

    // A theme the model named outright wins. It has read the request; the
    // keyword scoring below is only there for when it has not chosen.
    if (!named.isEmpty()) {
        const QString want = named.trimmed().toLower();
        for (const DeckTheme& t : all)
            if (t.name == want) return t;
    }

    const QSet<QString> headStems = stemsOf(headline);
    const QSet<QString> ctxStems  = stemsOf(context);
    const QString headFlat = QStringLiteral(" ") + headline.toLower() + QStringLiteral(" ");
    const QString ctxFlat  = QStringLiteral(" ") + context.toLower()  + QStringLiteral(" ");

    int bestScore = 0;
    int bestIndex = all.size() - 1;          // violet, the neutral default
    for (int i = 0; i < all.size(); ++i) {
        const int score = scoreAgainst(all.at(i), headStems, headFlat, 4)
                        + scoreAgainst(all.at(i), ctxStems,  ctxFlat,  1);
        if (score > bestScore) { bestScore = score; bestIndex = i; }
    }
    return all.at(bestIndex);
}

} // namespace NativeOffice
