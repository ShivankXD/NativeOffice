#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Cell.h  (Sprint 10 — Modernization Foundation)
// Rich cell model for NativeOffice Calc.
//
// A Cell holds:
//   • content  — the raw text the user typed (plain value or "=formula")
//   • format   — visual + number formatting (font, colours, alignment, …)
//
// CellFormat is designed so that a *default-constructed* format means "inherit
// the grid defaults" — only cells that deviate carry meaningful state.  This
// keeps the sparse store small and makes equality / "is this cell empty?"
// checks cheap.  Formatting UI (Priority 2) mutates these fields; copy/paste
// and XLSX I/O serialise them.
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>
#include <QColor>
#include <Qt>

namespace NativeOffice {

struct CellFormat {
    // ── Font ────────────────────────────────────────────────────────────────
    QString fontFamily;            // empty → grid default ("Segoe UI")
    int     fontSize  { 0 };       // 0     → grid default (12 pt)
    bool    bold      { false };
    bool    italic    { false };
    bool    underline { false };
    bool    strike    { false };   // strikethrough

    // ── Colours (invalid QColor → grid default) ──────────────────────────────
    QColor  textColor;             // foreground
    QColor  bgColor;               // fill / background

    // ── Alignment (0 → auto: numbers right, text left) ────────────────────────
    int     hAlign { 0 };          // Qt::AlignLeft/Center/Right/Justify, or 0
    int     vAlign { 0 };          // Qt::AlignTop/VCenter/Bottom, or 0
    bool    wrap   { false };
    int     indent { 0 };          // left/right indent steps (each ≈ 12 px)

    // ── Number format ─────────────────────────────────────────────────────────
    // Empty → "General". Otherwise a code understood by NumberFormat,
    // e.g. "0.00", "$#,##0.00", "0%", "#,##0". (Date/time codes are reserved.)
    QString numberFormat;

    // ── Borders ─────────────────────────────────────────────────────────────
    // Bitmask of edges that carry a border line. 0 → none.
    enum BorderEdge { BTop = 1, BRight = 2, BBottom = 4, BLeft = 8,
                      BAll = BTop | BRight | BBottom | BLeft };
    int    borderEdges { 0 };
    QColor borderColor;            // invalid → default black when edges set

    [[nodiscard]] bool isDefault() const {
        return fontFamily.isEmpty() && fontSize == 0
            && !bold && !italic && !underline && !strike
            && !textColor.isValid() && !bgColor.isValid()
            && hAlign == 0 && vAlign == 0 && !wrap && indent == 0
            && numberFormat.isEmpty()
            && borderEdges == 0 && !borderColor.isValid();
    }

    bool operator==(const CellFormat& o) const {
        return fontFamily   == o.fontFamily
            && fontSize     == o.fontSize
            && bold         == o.bold
            && italic       == o.italic
            && underline    == o.underline
            && strike       == o.strike
            && textColor    == o.textColor
            && bgColor      == o.bgColor
            && hAlign       == o.hAlign
            && vAlign       == o.vAlign
            && wrap         == o.wrap
            && indent       == o.indent
            && numberFormat == o.numberFormat
            && borderEdges  == o.borderEdges
            && borderColor  == o.borderColor;
    }
    bool operator!=(const CellFormat& o) const { return !(*this == o); }
};

struct Cell {
    QString    content;
    CellFormat format;

    // ── Provenance, for a lossless .xlsx round trip ─────────────────────────
    // The style index this cell carried in the file it was imported from, and
    // the format that index resolved to. On save, a cell whose format still
    // matches `xfFormat` is written with `xfIndex` again, so the workbook's own
    // styles.xml never has to be regenerated - and if styles.xml survives, so
    // can every other part of the package, charts and pictures included.
    // -1 means "not imported", i.e. created or restyled in the app.
    int        xfIndex { -1 };
    CellFormat xfFormat;

    [[nodiscard]] bool isEmpty() const {
        return content.isEmpty() && format.isDefault();
    }

    // Whether this cell can still be written with its original style index.
    [[nodiscard]] bool keepsOriginalStyle() const {
        return xfIndex >= 0 && format == xfFormat;
    }

    bool operator==(const Cell& o) const {
        return content == o.content && format == o.format;
    }
    bool operator!=(const Cell& o) const { return !(*this == o); }
};

} // namespace NativeOffice
