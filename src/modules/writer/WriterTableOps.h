#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// WriterTableOps.h  (Sprint 18 — full table editing)
// Header-only helpers that operate on the QTextTable under a QTextEdit's cursor.
// Shared by PagedTextEdit (right-click context menu) and WriterRibbon (the
// contextual "Table" tab) so the logic lives in exactly one place.
// ─────────────────────────────────────────────────────────────────────────────

#include <QTextEdit>
#include <QTextCursor>
#include <QTextTable>
#include <QTextTableCell>
#include <QTextTableFormat>
#include <QTextTableCellFormat>
#include <QTextCharFormat>
#include <QTextBlockFormat>
#include <QTextLength>
#include <QColor>
#include <QBrush>
#include <QList>

namespace NativeOffice {
namespace TableOps {

inline QTextTable* tableAt(QTextEdit* ed) {
    return ed ? ed->textCursor().currentTable() : nullptr;
}

inline bool inTable(QTextEdit* ed) { return tableAt(ed) != nullptr; }

// The cells covered by the current selection (or the single cell at the cursor).
inline QList<QTextTableCell> selectedCells(QTextEdit* ed) {
    QList<QTextTableCell> out;
    QTextTable* t = tableAt(ed);
    if (!t) return out;
    QTextCursor c = ed->textCursor();
    if (c.hasComplexSelection()) {
        int r, col, nr, nc;
        c.selectedTableCells(&r, &col, &nr, &nc);
        for (int i = r; i < r + nr; ++i)
            for (int j = col; j < col + nc; ++j)
                out << t->cellAt(i, j);
    } else {
        out << t->cellAt(c);
    }
    return out;
}

inline void mergeCellText(QTextTableCell& cell, const QTextCharFormat& fmt) {
    QTextCursor c = cell.firstCursorPosition();
    c.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
    c.mergeCharFormat(fmt);
}

inline void mergeCellBlock(QTextTableCell& cell, const QTextBlockFormat& bf) {
    QTextCursor c = cell.firstCursorPosition();
    c.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
    c.mergeBlockFormat(bf);
}

// ── Rows & columns ───────────────────────────────────────────────────────────
inline void insertRow(QTextEdit* ed, bool below) {
    QTextTable* t = tableAt(ed); if (!t) return;
    const QTextTableCell c = t->cellAt(ed->textCursor());
    const int at = below ? c.row() + c.rowSpan() : c.row();
    t->insertRows(at, 1);
}

inline void insertColumn(QTextEdit* ed, bool right) {
    QTextTable* t = tableAt(ed); if (!t) return;
    const QTextTableCell c = t->cellAt(ed->textCursor());
    const int at = right ? c.column() + c.columnSpan() : c.column();
    t->insertColumns(at, 1);
}

inline void deleteRow(QTextEdit* ed) {
    QTextTable* t = tableAt(ed); if (!t) return;
    const QTextTableCell c = t->cellAt(ed->textCursor());
    if (t->rows() <= 1) { /* deleting the last row removes the table below */ }
    t->removeRows(c.row(), 1);
}

inline void deleteColumn(QTextEdit* ed) {
    QTextTable* t = tableAt(ed); if (!t) return;
    const QTextTableCell c = t->cellAt(ed->textCursor());
    t->removeColumns(c.column(), 1);
}

inline void deleteTable(QTextEdit* ed) {
    QTextTable* t = tableAt(ed); if (!t) return;
    // Select the whole table frame (incl. its boundary chars) and delete it.
    QTextCursor sel(ed->document());
    sel.setPosition(t->firstPosition() - 1);
    sel.setPosition(t->lastPosition() + 1, QTextCursor::KeepAnchor);
    sel.removeSelectedText();
    ed->setFocus();
}

// ── Merge / split ────────────────────────────────────────────────────────────
inline void mergeCells(QTextEdit* ed) {
    QTextTable* t = tableAt(ed); if (!t) return;
    QTextCursor c = ed->textCursor();
    if (!c.hasComplexSelection()) return;
    int r, col, nr, nc;
    c.selectedTableCells(&r, &col, &nr, &nc);
    if (nr >= 1 && nc >= 1 && (nr > 1 || nc > 1))
        t->mergeCells(r, col, nr, nc);
}

inline void splitCell(QTextEdit* ed) {
    QTextTable* t = tableAt(ed); if (!t) return;
    const QTextTableCell cell = t->cellAt(ed->textCursor());
    if (cell.rowSpan() > 1 || cell.columnSpan() > 1)
        t->splitCell(cell.row(), cell.column(), 1, 1);   // un-merge
}

// ── Borders (per selected cell) ──────────────────────────────────────────────
// kind: 0 none, 1 all, 2 outline only (best-effort: all sides on each cell)
inline void setCellBorders(QTextEdit* ed, int kind, const QColor& color = QColor("#2C3140")) {
    const auto cells = selectedCells(ed);
    const qreal w = (kind == 0) ? 0.0 : 1.0;
    const auto style = (kind == 0) ? QTextFrameFormat::BorderStyle_None
                                   : QTextFrameFormat::BorderStyle_Solid;
    const QBrush brush(color);
    for (QTextTableCell cell : cells) {
        QTextTableCellFormat f = cell.format().toTableCellFormat();
        f.setLeftBorder(w);   f.setLeftBorderBrush(brush);   f.setLeftBorderStyle(style);
        f.setRightBorder(w);  f.setRightBorderBrush(brush);  f.setRightBorderStyle(style);
        f.setTopBorder(w);    f.setTopBorderBrush(brush);    f.setTopBorderStyle(style);
        f.setBottomBorder(w); f.setBottomBorderBrush(brush); f.setBottomBorderStyle(style);
        cell.setFormat(f);
    }
}

// ── Shading (per selected cell) ──────────────────────────────────────────────
inline void setCellShading(QTextEdit* ed, const QColor& c) {
    const auto cells = selectedCells(ed);
    for (QTextTableCell cell : cells) {
        QTextTableCellFormat f = cell.format().toTableCellFormat();
        if (c.alpha() == 0) f.clearBackground();
        else                f.setBackground(QBrush(c));
        cell.setFormat(f);
    }
}

// ── Cell content alignment ───────────────────────────────────────────────────
inline void setCellAlignment(QTextEdit* ed, Qt::Alignment a) {
    const auto cells = selectedCells(ed);
    QTextBlockFormat bf; bf.setAlignment(a);
    for (QTextTableCell cell : cells) mergeCellBlock(cell, bf);
}

// ── Header row ───────────────────────────────────────────────────────────────
inline void setHeaderRow(QTextEdit* ed, bool on) {
    QTextTable* t = tableAt(ed); if (!t) return;
    QTextTableFormat tf = t->format();
    tf.setHeaderRowCount(on ? 1 : 0);
    t->setFormat(tf);
    QTextCharFormat hf;
    hf.setForeground(on ? QColor("#FFFFFF") : QColor("#1C1E26"));
    hf.setFontWeight(on ? QFont::Bold : QFont::Normal);
    for (int c = 0; c < t->columns(); ++c) {
        QTextTableCell cell = t->cellAt(0, c);
        QTextTableCellFormat cf = cell.format().toTableCellFormat();
        if (on) cf.setBackground(QBrush(QColor("#2C3140")));
        else    cf.clearBackground();
        cell.setFormat(cf);
        mergeCellText(cell, hf);
    }
}

// ── Whole-table styles ───────────────────────────────────────────────────────
// 0 Grid, 1 Header, 2 Banded, 3 Header+Banded, 4 Plain (borderless)
inline void applyTableStyle(QTextEdit* ed, int style) {
    QTextTable* t = tableAt(ed); if (!t) return;
    const bool borderless = (style == 4);

    QTextTableFormat tf = t->format();
    tf.setBorder(borderless ? 0 : 1);
    tf.setBorderStyle(borderless ? QTextFrameFormat::BorderStyle_None
                                 : QTextFrameFormat::BorderStyle_Solid);
    tf.setBorderBrush(QColor("#9CA3AF"));
    tf.setCellPadding(4);
    tf.setHeaderRowCount((style == 1 || style == 3) ? 1 : 0);
    t->setFormat(tf);

    const int rows = t->rows(), cols = t->columns();
    for (int r = 0; r < rows; ++r) {
        const bool header = (r == 0) && (style == 1 || style == 3);
        const bool band   = (!header) && (style == 2 || style == 3) && (r % 2 == 1);
        QColor bg = Qt::transparent;
        if (header) bg = QColor("#2C3140");
        else if (band) bg = QColor("#F1F3F7");
        for (int c = 0; c < cols; ++c) {
            QTextTableCell cell = t->cellAt(r, c);
            QTextTableCellFormat cf = cell.format().toTableCellFormat();
            if (bg.alpha() == 0) cf.clearBackground(); else cf.setBackground(QBrush(bg));
            cell.setFormat(cf);
            QTextCharFormat hf;
            hf.setForeground(header ? QColor("#FFFFFF") : QColor("#1C1E26"));
            hf.setFontWeight(header ? QFont::Bold : QFont::Normal);
            mergeCellText(cell, hf);
        }
    }
}

} // namespace TableOps
} // namespace NativeOffice
