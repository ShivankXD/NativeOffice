#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SpellHighlighter.h
// QSyntaxHighlighter that paints a red spell-check squiggle under every word the
// SpellChecker reports as misspelled. Because it is a syntax highlighter, only
// edited blocks are re-checked on each keystroke — heavy work stays off the
// per-keystroke path. It rehighlights once when the async dictionary load
// finishes (SpellChecker::ready).
// ─────────────────────────────────────────────────────────────────────────────

#include <QSyntaxHighlighter>
#include <QRegularExpression>

class QTextDocument;
class QTimer;

namespace NativeOffice {

// Despite the name, this single highlighter (Qt allows only one per document)
// does two presentation-only jobs: red spell squiggles, and zoom scaling of
// explicitly-sized text runs. Zoom scaling is an overlay — the document keeps
// logical 100% font sizes, so the size combo, save and .docx export are
// unaffected; only the on-screen size changes so headings / imported text
// scale together with the (already-scaled) page and default font.
class SpellHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit SpellHighlighter(QTextDocument* doc);

    void setEnabled(bool on);
    [[nodiscard]] bool isEnabled() const noexcept { return m_enabled; }

    // 1.0 == 100%. Scales explicitly-sized runs; debounced so a fast zoom
    // gesture over a long document doesn't rehighlight on every wheel tick.
    void setZoom(double factor);

protected:
    void highlightBlock(const QString& text) override;

private:
    bool               m_enabled { false };
    double             m_zoom    { 1.0 };
    QTimer*            m_rehlDebounce { nullptr };
    QRegularExpression m_wordRe;
};

} // namespace NativeOffice
