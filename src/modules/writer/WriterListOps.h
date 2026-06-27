#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// WriterListOps.h  — multi-level list operations (Tier 2, item 9)
// Header-only namespace shared by the ribbon, the list dialogs and the editor's
// Tab/Shift+Tab handling.
//
//   • demote / promote items between nesting levels, each level getting its own
//     marker from a coordinated scheme (•/◦/▪ for bullets, 1./a./i. for numbers);
//   • restart / continue numbering;
//   • apply a list (optionally with a custom number prefix/suffix);
// All operations work on the current block or the whole selection.
// ─────────────────────────────────────────────────────────────────────────────

#include <QTextEdit>
#include <QTextCursor>
#include <QTextList>
#include <QTextListFormat>
#include <QTextBlock>
#include <QList>

namespace NativeOffice {
namespace ListOps {

inline bool isNumbered(QTextListFormat::Style s) {
    return s <= QTextListFormat::ListDecimal;   // Decimal..UpperRoman are < Square
}

// The marker for a given (1-based) nesting level, cycling every three levels.
inline QTextListFormat::Style markerForLevel(bool numbered, int level) {
    const int i = (qMax(1, level) - 1) % 3;
    if (numbered) {
        static const QTextListFormat::Style n[] = {
            QTextListFormat::ListDecimal, QTextListFormat::ListLowerAlpha,
            QTextListFormat::ListLowerRoman };
        return n[i];
    }
    static const QTextListFormat::Style b[] = {
        QTextListFormat::ListDisc, QTextListFormat::ListCircle,
        QTextListFormat::ListSquare };
    return b[i];
}

inline QList<QTextBlock> blocksOf(QTextList* list) {
    QList<QTextBlock> r;
    if (!list) return r;
    for (int i = 0; i < list->count(); ++i) r << list->item(i);
    return r;
}

// Create / convert the current block or selection into a list of the given style
// at level 1. prefix/suffix customise numbered markers (e.g. "1)" or "(1)").
inline void applyList(QTextEdit* ed, QTextListFormat::Style style,
                      const QString& prefix = QString(), const QString& suffix = QString()) {
    if (!ed) return;
    QTextCursor cur = ed->textCursor();
    QTextListFormat lf;
    lf.setStyle(style);
    lf.setIndent(1);
    if (!prefix.isEmpty()) lf.setNumberPrefix(prefix);
    if (!suffix.isEmpty()) lf.setNumberSuffix(suffix);
    cur.createList(lf);
    ed->setFocus();
}

// Move every selected list item one level deeper (+1) or shallower (-1).
// Promoting out of level 1 removes the item from the list entirely.
inline void changeLevel(QTextEdit* ed, int delta) {
    if (!ed || delta == 0) return;
    QTextDocument* doc = ed->document();
    QTextCursor cur = ed->textCursor();
    const int a = cur.selectionStart(), b = cur.selectionEnd();
    QTextBlock first = doc->findBlock(a);
    QTextBlock last  = doc->findBlock(b);

    cur.beginEditBlock();
    for (QTextBlock blk = first; blk.isValid(); blk = blk.next()) {
        QTextCursor bc(blk);
        QTextList* list = bc.currentList();
        if (list) {
            const QTextListFormat lf = list->format();
            const int level = qMax(1, lf.indent());
            const bool numbered = isNumbered(lf.style());

            if (delta < 0 && level <= 1) {
                // Promote out of the list.
                list->remove(blk);
                QTextBlockFormat bf = bc.blockFormat();
                bf.setIndent(0);
                bf.setObjectIndex(-1);
                bc.setBlockFormat(bf);
            } else {
                const int newLevel = qBound(1, level + delta, 9);
                if (newLevel != level) {
                    const QTextListFormat::Style ns = markerForLevel(numbered, newLevel);
                    // Continue an adjacent same-level list to keep numbering contiguous.
                    QTextBlock prev = blk.previous();
                    QTextCursor pc(prev);
                    QTextList* pl = prev.isValid() ? pc.currentList() : nullptr;
                    if (pl && pl != list && pl->format().indent() == newLevel
                            && pl->format().style() == ns) {
                        pl->add(blk);
                    } else {
                        QTextListFormat nf = lf;
                        nf.setStyle(ns);
                        nf.setIndent(newLevel);
                        bc.createList(nf);
                    }
                }
            }
        }
        if (blk == last) break;
    }
    cur.endEditBlock();
    ed->setFocus();
}

// Restart numbering at 1 from the current item to the end of its list.
inline void restartNumbering(QTextEdit* ed) {
    if (!ed) return;
    QTextCursor cur = ed->textCursor();
    QTextList* list = cur.currentList();
    if (!list) return;
    const QList<QTextBlock> items = blocksOf(list);
    const QTextBlock cb = cur.block();
    const int idx = items.indexOf(cb);
    if (idx < 0) return;

    cur.beginEditBlock();
    QTextListFormat nf = list->format();
    nf.setStart(1);
    QTextCursor bc(cb);
    QTextList* nl = bc.createList(nf);
    for (int i = idx + 1; i < items.size(); ++i) nl->add(items[i]);
    cur.endEditBlock();
    ed->setFocus();
}

// Merge the current list into the preceding sibling list so numbering continues.
inline void continueNumbering(QTextEdit* ed) {
    if (!ed) return;
    QTextCursor cur = ed->textCursor();
    QTextList* list = cur.currentList();
    if (!list || list->count() == 0) return;
    const QTextBlock first = list->item(0);
    const QTextBlock prev = first.previous();
    if (!prev.isValid()) return;
    QTextCursor pc(prev);
    QTextList* pl = pc.currentList();
    if (!pl || pl == list) return;

    cur.beginEditBlock();
    for (const QTextBlock& blk : blocksOf(list)) pl->add(blk);
    cur.endEditBlock();
    ed->setFocus();
}

} // namespace ListOps
} // namespace NativeOffice
