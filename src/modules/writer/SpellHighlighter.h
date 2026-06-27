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

namespace NativeOffice {

class SpellHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit SpellHighlighter(QTextDocument* doc);

    void setEnabled(bool on);
    [[nodiscard]] bool isEnabled() const noexcept { return m_enabled; }

protected:
    void highlightBlock(const QString& text) override;

private:
    bool               m_enabled { false };
    QRegularExpression m_wordRe;
};

} // namespace NativeOffice
