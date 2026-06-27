// ─────────────────────────────────────────────────────────────────────────────
// SpellHighlighter.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "SpellHighlighter.h"
#include "SpellChecker.h"

#include <QTextCharFormat>
#include <QTextDocument>

namespace NativeOffice {

SpellHighlighter::SpellHighlighter(QTextDocument* doc)
    : QSyntaxHighlighter(doc)
{
    // A word is a run of letters, allowing internal apostrophes (don't, John's).
    // Hyphens act as separators so only the offending half is underlined.
    m_wordRe = QRegularExpression(
        QStringLiteral("[\\p{L}]+(?:['\\x{2019}][\\p{L}]+)*"),
        QRegularExpression::UseUnicodePropertiesOption);

    // Repaint squiggles once the dictionary becomes available.
    connect(SpellChecker::instance(), &SpellChecker::ready,
            this, [this]{ if (m_enabled) rehighlight(); });
}

void SpellHighlighter::setEnabled(bool on) {
    if (m_enabled == on) return;
    m_enabled = on;
    if (on) SpellChecker::instance()->ensureLoaded();
    rehighlight();   // adds squiggles when on, clears them when off
}

void SpellHighlighter::highlightBlock(const QString& text) {
    if (!m_enabled) return;

    QTextCharFormat fmt;
    fmt.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
    fmt.setUnderlineColor(QColor(0xE0, 0x32, 0x2A));   // red squiggle

    auto* sc = SpellChecker::instance();
    auto it = m_wordRe.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString word = m.captured();
        if (!sc->isCorrect(word))
            setFormat(m.capturedStart(), m.capturedLength(), fmt);
    }
}

} // namespace NativeOffice
