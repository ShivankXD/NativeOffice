// ─────────────────────────────────────────────────────────────────────────────
// WriterStyles.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "WriterStyles.h"

#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace NativeOffice {

// ── WriterStyleDef ───────────────────────────────────────────────────────────

QTextCharFormat WriterStyleDef::charFormat() const {
    QTextCharFormat cf;
    if (!fontFamily.isEmpty())
        cf.setFontFamilies({ fontFamily, "Segoe UI", "Inter", "sans-serif" });
    else
        cf.setFontFamilies({ "Segoe UI", "Inter", "Roboto", "sans-serif" });
    if (fontSize > 0) cf.setFontPointSize(fontSize);
    cf.setFontWeight(bold ? QFont::Bold : QFont::Normal);
    cf.setFontItalic(italic);
    cf.setFontUnderline(underline);
    cf.setForeground(hasColor ? color : QColor("#1C1E26"));
    return cf;
}

QTextBlockFormat WriterStyleDef::blockFormat() const {
    QTextBlockFormat bf;
    bf.setTopMargin(spaceBefore);
    bf.setBottomMargin(spaceAfter);
    bf.setLeftMargin(leftIndent);
    bf.setIndent(0);
    bf.setAlignment(static_cast<Qt::Alignment>(alignment) | Qt::AlignAbsolute);
    if (lineHeight > 0)
        bf.setLineHeight(lineHeight, QTextBlockFormat::ProportionalHeight);
    if (headingLevel > 0) bf.setHeadingLevel(headingLevel);
    if (hasBackground) bf.setBackground(background);
    return bf;
}

void WriterStyleDef::captureFrom(const QTextCharFormat& cf, const QTextBlockFormat& bf) {
    const QStringList fams = cf.property(QTextFormat::FontFamilies).toStringList();
    if (!fams.isEmpty()) fontFamily = fams.first();
    if (cf.hasProperty(QTextFormat::FontPointSize)) fontSize = cf.fontPointSize();
    bold      = cf.fontWeight() >= QFont::DemiBold;
    italic    = cf.fontItalic();
    underline = cf.fontUnderline();
    if (cf.foreground().style() != Qt::NoBrush) {
        hasColor = true;
        color    = cf.foreground().color();
    }
    if (!isCharacter) {
        alignment   = static_cast<int>(bf.alignment() & ~Qt::AlignAbsolute);
        if (alignment == 0) alignment = static_cast<int>(Qt::AlignLeft);
        spaceBefore = bf.topMargin();
        spaceAfter  = bf.bottomMargin();
        leftIndent  = bf.leftMargin();
        if (bf.lineHeightType() == QTextBlockFormat::ProportionalHeight)
            lineHeight = bf.lineHeight();
        if (bf.background().style() != Qt::NoBrush) {
            hasBackground = true;
            background    = bf.background().color();
        } else {
            hasBackground = false;
        }
    }
}

QJsonObject WriterStyleDef::toJson() const {
    QJsonObject o;
    o["name"]      = name;
    o["char"]      = isCharacter;
    o["builtin"]   = builtin;
    o["heading"]   = headingLevel;
    o["family"]    = fontFamily;
    o["size"]      = fontSize;
    o["bold"]      = bold;
    o["italic"]    = italic;
    o["underline"] = underline;
    if (hasColor) o["color"] = color.name(QColor::HexArgb);
    o["align"]     = alignment;
    o["before"]    = spaceBefore;
    o["after"]     = spaceAfter;
    o["line"]      = lineHeight;
    o["lindent"]   = leftIndent;
    if (hasBackground) o["bg"] = background.name(QColor::HexArgb);
    return o;
}

WriterStyleDef WriterStyleDef::fromJson(const QJsonObject& o) {
    WriterStyleDef d;
    d.name         = o.value("name").toString();
    d.isCharacter  = o.value("char").toBool();
    d.builtin      = o.value("builtin").toBool();
    d.headingLevel = o.value("heading").toInt();
    d.fontFamily   = o.value("family").toString();
    d.fontSize     = o.value("size").toDouble();
    d.bold         = o.value("bold").toBool();
    d.italic       = o.value("italic").toBool();
    d.underline    = o.value("underline").toBool();
    if (o.contains("color")) { d.hasColor = true; d.color = QColor(o.value("color").toString()); }
    d.alignment    = o.value("align").toInt(static_cast<int>(Qt::AlignLeft));
    d.spaceBefore  = o.value("before").toDouble();
    d.spaceAfter   = o.value("after").toDouble(6);
    d.lineHeight   = o.value("line").toDouble();
    d.leftIndent   = o.value("lindent").toDouble();
    if (o.contains("bg")) { d.hasBackground = true; d.background = QColor(o.value("bg").toString()); }
    return d;
}

// ── WriterStyleManager ───────────────────────────────────────────────────────

void WriterStyleManager::seedBuiltins() {
    auto add = [&](WriterStyleDef d) { d.builtin = true; m_order.append(d); };

    WriterStyleDef normal;   normal.name   = "Normal";     normal.fontSize = 12;
    WriterStyleDef nospace;  nospace.name  = "No Spacing"; nospace.fontSize = 12; nospace.spaceAfter = 0;

    WriterStyleDef h1; h1.name = "Heading 1"; h1.fontSize = 24; h1.bold = true;
    h1.headingLevel = 1; h1.spaceBefore = 14; h1.spaceAfter = 6;

    WriterStyleDef h2; h2.name = "Heading 2"; h2.fontSize = 18; h2.bold = true;
    h2.headingLevel = 2; h2.hasColor = true; h2.color = QColor("#2C3140");
    h2.spaceBefore = 12; h2.spaceAfter = 4;

    WriterStyleDef h3; h3.name = "Heading 3"; h3.fontSize = 14; h3.bold = true;
    h3.headingLevel = 3; h3.hasColor = true; h3.color = QColor("#3A4054");
    h3.spaceBefore = 10; h3.spaceAfter = 3;

    WriterStyleDef title; title.name = "Title"; title.fontSize = 32;
    title.alignment = static_cast<int>(Qt::AlignHCenter);
    title.spaceBefore = 6; title.spaceAfter = 4;

    WriterStyleDef sub; sub.name = "Subtitle"; sub.fontSize = 16; sub.italic = true;
    sub.hasColor = true; sub.color = QColor("#6B7280");
    sub.alignment = static_cast<int>(Qt::AlignHCenter); sub.spaceAfter = 10;

    WriterStyleDef quote; quote.name = "Quote"; quote.fontSize = 13; quote.italic = true;
    quote.hasColor = true; quote.color = QColor("#4B5563");
    quote.leftIndent = 34; quote.spaceBefore = 6; quote.spaceAfter = 6;
    quote.hasBackground = true; quote.background = QColor("#F3F4F6");

    WriterStyleDef emph; emph.name = "Emphasis"; emph.isCharacter = true; emph.italic = true;
    WriterStyleDef strong; strong.name = "Strong"; strong.isCharacter = true; strong.bold = true;

    add(normal); add(nospace); add(h1); add(h2); add(h3);
    add(title); add(sub); add(quote); add(emph); add(strong);
    reindex();
}

void WriterStyleManager::reindex() {
    m_index.clear();
    for (int i = 0; i < m_order.size(); ++i) m_index.insert(m_order[i].name, i);
}

const WriterStyleDef* WriterStyleManager::find(const QString& name) const {
    auto it = m_index.constFind(name);
    return it == m_index.constEnd() ? nullptr : &m_order[it.value()];
}

void WriterStyleManager::upsert(const WriterStyleDef& def) {
    auto it = m_index.constFind(def.name);
    if (it != m_index.constEnd()) {
        WriterStyleDef d = def;
        const WriterStyleDef& old = m_order[it.value()];
        d.builtin = old.builtin;
        if (d.builtin) d.modified = true;       // changed built-in still persists
        m_order[it.value()] = d;
    } else {
        m_order.append(def);
        reindex();
    }
}

void WriterStyleManager::remove(const QString& name) {
    auto it = m_index.constFind(name);
    if (it == m_index.constEnd()) return;
    if (m_order[it.value()].builtin) return;    // never delete built-ins
    m_order.removeAt(it.value());
    reindex();
}

bool WriterStyleManager::rename(const QString& oldName, const QString& newName) {
    if (newName.isEmpty() || contains(newName)) return false;
    auto it = m_index.constFind(oldName);
    if (it == m_index.constEnd()) return false;
    if (m_order[it.value()].builtin) return false;
    m_order[it.value()].name = newName;
    reindex();
    return true;
}

QString WriterStyleManager::serializeCustom() const {
    QJsonArray arr;
    for (const WriterStyleDef& d : m_order)
        if (!d.builtin || d.modified) arr.append(d.toJson());
    if (arr.isEmpty()) return QString();
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void WriterStyleManager::loadCustom(const QString& json) {
    if (json.trimmed().isEmpty()) return;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isArray()) return;
    for (const QJsonValue& v : doc.array()) {
        if (!v.isObject()) continue;
        WriterStyleDef d = WriterStyleDef::fromJson(v.toObject());
        if (d.name.isEmpty()) continue;
        auto it = m_index.constFind(d.name);
        if (it != m_index.constEnd()) {
            const bool wasBuiltin = m_order[it.value()].builtin;
            d.builtin  = wasBuiltin;
            d.modified = wasBuiltin;
            m_order[it.value()] = d;            // override built-in / existing
        } else {
            d.builtin = false;
            m_order.append(d);
        }
    }
    reindex();
}

} // namespace NativeOffice
