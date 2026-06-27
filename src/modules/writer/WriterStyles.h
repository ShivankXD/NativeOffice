#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// WriterStyles.h
// Named paragraph & character styles for the Writer (Tier 2, item 8).
//
//   • WriterStyleDef — a single named style described by discrete attributes
//     (font, size, bold/italic/underline, colour, alignment, spacing, indent,
//     background, heading level). It can build the QTextCharFormat /
//     QTextBlockFormat it represents, and serialise to / from JSON.
//   • WriterStyleManager — an ordered registry of styles. Seeds the eight
//     built-ins (Normal, Headings, Title, Subtitle, Quote, …), supports
//     create / modify / delete / rename, and (de)serialises the *custom* and
//     *modified-built-in* styles so they persist in the document.
//
// Paragraphs are tagged with their style name via a custom block property; the
// manager + a per-block assignment list (saved in the .noff sidecar) restore the
// tags on load, which is what fixes the old style round-trip bug.
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>
#include <QList>
#include <QHash>
#include <QColor>
#include <QTextCharFormat>
#include <QTextBlockFormat>

class QJsonObject;

namespace NativeOffice {

// Block property carrying the paragraph's style name (survives via the sidecar).
inline constexpr int kStyleNameProp = QTextFormat::UserProperty + 12;

struct WriterStyleDef {
    QString name;
    bool    isCharacter  = false;   // true → character style (char format only)
    bool    builtin      = false;
    bool    modified     = false;   // a built-in the user changed (still persisted)
    int     headingLevel = 0;       // 1..3 for headings, else 0

    // Character attributes
    QString fontFamily;             // empty → inherit
    double  fontSize     = 0;       // 0 → inherit
    bool    bold         = false;
    bool    italic       = false;
    bool    underline    = false;
    bool    hasColor     = false;
    QColor  color;

    // Paragraph attributes (ignored for character styles)
    int     alignment    = static_cast<int>(Qt::AlignLeft);
    double  spaceBefore  = 0;
    double  spaceAfter   = 6;
    double  lineHeight   = 0;        // proportional %, 0 → default (single)
    double  leftIndent   = 0;        // px
    bool    hasBackground = false;
    QColor  background;

    [[nodiscard]] QTextCharFormat  charFormat()  const;
    [[nodiscard]] QTextBlockFormat blockFormat() const;

    [[nodiscard]] QJsonObject toJson() const;
    static WriterStyleDef fromJson(const QJsonObject& o);

    // Fill the character/paragraph attributes from live formats (for
    // "update style to match selection").
    void captureFrom(const QTextCharFormat& cf, const QTextBlockFormat& bf);
};

class WriterStyleManager {
public:
    WriterStyleManager() { seedBuiltins(); }

    [[nodiscard]] const QList<WriterStyleDef>& styles() const { return m_order; }
    [[nodiscard]] bool contains(const QString& name) const { return m_index.contains(name); }
    [[nodiscard]] const WriterStyleDef* find(const QString& name) const;

    void upsert(const WriterStyleDef& def);     // add or replace by name
    void remove(const QString& name);           // built-ins cannot be removed
    bool rename(const QString& oldName, const QString& newName);

    // JSON of every custom or modified-built-in style (the things worth saving).
    [[nodiscard]] QString serializeCustom() const;
    // Merge persisted styles back in (overriding matching built-ins).
    void loadCustom(const QString& json);

private:
    void seedBuiltins();
    void reindex();

    QList<WriterStyleDef> m_order;
    QHash<QString, int>   m_index;
};

} // namespace NativeOffice
