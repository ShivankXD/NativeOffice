#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// CondFormat.h  (Conditional Formatting)
// Lightweight, widget-free description of a conditional-formatting rule so the
// SpreadsheetModel can store rules in its data model without depending on Qt
// Widgets. Mirrors the ChartSpec.h pattern.
//
// A rule applies a styling override (background colour, text colour, bold) to
// every cell in `range` for which `formula` evaluates to TRUE. The formula uses
// the same FormulaEngine as ordinary cells; its cell references are *relative*
// to the top-left of `range` (Excel semantics) and shift per cell. Prefix a
// column or row with '$' to lock it (e.g. =$C1="Done" highlights whole rows by
// a status column).
// ─────────────────────────────────────────────────────────────────────────────
#include <QRect>
#include <QString>
#include <QColor>

namespace NativeOffice {

struct CondFormatRule {
    QRect   range;             // cells covered (left=col1,top=row1 … right,bottom)
    QString formula;           // e.g. "=A1<0", "=B1=\"Done\"", "=C1>100"
    QColor  bgColor;           // invalid → no background override
    QColor  textColor;         // invalid → no text-colour override
    bool    bold { false };    // apply bold when the rule matches

    // The effective styling a cell should receive when one or more rules match.
    struct Result {
        bool   matched { false };
        QColor bgColor;
        QColor textColor;
        bool   bold { false };
    };
};

} // namespace NativeOffice
