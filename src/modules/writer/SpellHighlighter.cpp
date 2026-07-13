// ─────────────────────────────────────────────────────────────────────────────
// SpellHighlighter.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "SpellHighlighter.h"
#include "SpellChecker.h"

#include <QTextCharFormat>
#include <QTextImageFormat>
#include <QTextDocument>
#include <QTextBlock>
#include <QTimer>
#include <QtMath>

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

    // Coalesce rapid zoom changes into a single full rehighlight.
    m_rehlDebounce = new QTimer(this);
    m_rehlDebounce->setSingleShot(true);
    m_rehlDebounce->setInterval(45);
    connect(m_rehlDebounce, &QTimer::timeout, this, [this]{ rehighlight(); });
}

void SpellHighlighter::setEnabled(bool on) {
    if (m_enabled == on) return;
    m_enabled = on;
    if (on) SpellChecker::instance()->ensureLoaded();
    rehighlight();   // adds squiggles when on, clears them when off
}

void SpellHighlighter::setZoom(double factor) {
    if (factor <= 0.0) factor = 1.0;
    if (qFuzzyCompare(m_zoom, factor)) return;
    m_zoom = factor;
    // Rehighlight to re-apply (or, at 100%, drop) the scaled-size overlay.
    m_rehlDebounce->start();
}

void SpellHighlighter::highlightBlock(const QString& text) {
    // ── Zoom scaling (presentation only) ────────────────────────────────────
    // Scale every run that carries an *explicit* font size by the zoom factor.
    // Runs with no explicit size inherit the document's default font, which
    // WriterModule already scales, so we deliberately skip those. The document's
    // stored formats are never touched — this is a display overlay only.
    if (!qFuzzyCompare(m_zoom, 1.0)) {
        const QTextBlock blk = currentBlock();
        for (QTextBlock::iterator it = blk.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid()) continue;
            const QTextCharFormat cf = frag.charFormat();
            const int start = frag.position() - blk.position();
            if (cf.isImageFormat()) {
                // Scale inline images too, or a fixed-px image (e.g. an imported
                // cover) overflows the shrunken page at low zoom and shoves the
                // (centred) text off the right edge.
                QTextImageFormat imf = cf.toImageFormat();
                const double w = imf.width(), h = imf.height();
                if (w > 0.0 && h > 0.0) {
                    imf.setWidth(w * m_zoom);
                    imf.setHeight(h * m_zoom);
                    setFormat(start, frag.length(), imf);
                }
            } else if (cf.hasProperty(QTextFormat::FontPointSize)) {
                const double logical = cf.doubleProperty(QTextFormat::FontPointSize);
                if (logical > 0.0) {
                    QTextCharFormat f;
                    f.setProperty(QTextFormat::FontPointSize, logical * m_zoom);
                    setFormat(start, frag.length(), f);
                }
            }
        }
    }

    // ── Spell squiggles ─────────────────────────────────────────────────────
    if (!m_enabled) return;

    auto* sc = SpellChecker::instance();
    auto it = m_wordRe.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (sc->isCorrect(m.captured())) continue;
        // Merge over whatever the zoom pass already set for this range, so the
        // squiggle doesn't wipe the run's scaled size (setFormat replaces).
        QTextCharFormat fmt = format(m.capturedStart());
        fmt.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
        fmt.setUnderlineColor(QColor(0xE0, 0x32, 0x2A));   // red squiggle
        setFormat(m.capturedStart(), m.capturedLength(), fmt);
    }
}

} // namespace NativeOffice
