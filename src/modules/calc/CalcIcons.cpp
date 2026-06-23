// ─────────────────────────────────────────────────────────────────────────────
// CalcIcons.cpp  (Sprint 25)
// SVG line-icon library rendered via QSvgRenderer into cached QIcons.
// ─────────────────────────────────────────────────────────────────────────────
#include "CalcIcons.h"

#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QByteArray>

namespace NativeOffice {

// ── Icon path bodies (inner SVG for a 0 0 24 24 viewBox) ───────────────────────
// "@C" is replaced by the requested colour (used for filled accents).
static const QHash<QString, QString>& iconBodies() {
    static const QHash<QString, QString> m = {
    // ── Clipboard ─────────────────────────────────────────────────────────────
    {"paste",          "<rect x='6' y='4' width='12' height='17' rx='1.5'/><rect x='9' y='2.5' width='6' height='3' rx='1'/><line x1='9' y1='10' x2='15' y2='10'/><line x1='9' y1='13.5' x2='15' y2='13.5'/><line x1='9' y1='17' x2='13' y2='17'/>"},
    {"cut",            "<circle cx='7' cy='17' r='2.3'/><circle cx='7' cy='7' r='2.3'/><line x1='8.9' y1='8.4' x2='20' y2='17'/><line x1='8.9' y1='15.6' x2='20' y2='7'/>"},
    {"copy",           "<rect x='9' y='9' width='10' height='11' rx='1.5'/><path d='M14 9 V5.5 A1.5 1.5 0 0 0 12.5 4 H6 A1.5 1.5 0 0 0 4.5 5.5 V15 A1.5 1.5 0 0 0 6 16.5 H9'/>"},
    {"format-painter", "<rect x='4' y='4' width='12' height='5' rx='1'/><path d='M16 6.5 H19 V10.5 H10 V13.5'/><rect x='8.5' y='13.5' width='4' height='6.5' rx='1'/>"},

    // ── Font ──────────────────────────────────────────────────────────────────
    {"font-grow",      "<path d='M3 19 L8 6 L13 19 M5 14.5 H11'/><path d='M18 19 V9 M15 12 L18 9 L21 12'/>"},
    {"font-shrink",    "<path d='M3 19 L8 6 L13 19 M5 14.5 H11'/><path d='M18 9 V19 M15 16 L18 19 L21 16'/>"},
    {"font-color",     "<path d='M4 18 L9.5 5 L15 18 M6.2 13.5 H12.8'/>"},
    {"fill-color",     "<path d='M4 11 L11 4 L18 11 L11.5 17.5 A1.5 1.5 0 0 1 9.5 17.5 L4 12 Z'/><path d='M19 14 c1.6 1.8 1.6 3.6 0 3.6 s-1.6 -1.8 0 -3.6 z' fill='@C' stroke='none'/>"},
    {"borders",        "<rect x='4' y='4' width='16' height='16' rx='1'/><line x1='12' y1='4' x2='12' y2='20'/><line x1='4' y1='12' x2='20' y2='12'/>"},
    {"clear-format",   "<path d='M3 18 L7.5 7 L12 18 M4.7 14 H10.3'/><line x1='14.5' y1='8' x2='20.5' y2='14'/><line x1='20.5' y1='8' x2='14.5' y2='14'/>"},

    // ── Alignment ───────────────────────────────────────────────────────────────
    {"valign-top",     "<line x1='5' y1='4' x2='19' y2='4' stroke-width='2.2'/><line x1='7' y1='8' x2='17' y2='8'/><line x1='7' y1='11.5' x2='13' y2='11.5'/>"},
    {"valign-middle",  "<line x1='5' y1='12' x2='19' y2='12' stroke-width='2.2'/><line x1='7' y1='7.5' x2='17' y2='7.5'/><line x1='7' y1='16.5' x2='13' y2='16.5'/>"},
    {"valign-bottom",  "<line x1='5' y1='20' x2='19' y2='20' stroke-width='2.2'/><line x1='7' y1='12.5' x2='17' y2='12.5'/><line x1='7' y1='16' x2='13' y2='16'/>"},
    {"halign-left",    "<line x1='4' y1='6' x2='20' y2='6'/><line x1='4' y1='10' x2='14' y2='10'/><line x1='4' y1='14' x2='18' y2='14'/><line x1='4' y1='18' x2='12' y2='18'/>"},
    {"halign-center",  "<line x1='4' y1='6' x2='20' y2='6'/><line x1='7' y1='10' x2='17' y2='10'/><line x1='5' y1='14' x2='19' y2='14'/><line x1='8' y1='18' x2='16' y2='18'/>"},
    {"halign-right",   "<line x1='4' y1='6' x2='20' y2='6'/><line x1='10' y1='10' x2='20' y2='10'/><line x1='6' y1='14' x2='20' y2='14'/><line x1='12' y1='18' x2='20' y2='18'/>"},
    {"indent-inc",     "<line x1='4' y1='6' x2='20' y2='6'/><line x1='11' y1='10' x2='20' y2='10'/><line x1='11' y1='14' x2='20' y2='14'/><line x1='4' y1='18' x2='20' y2='18'/><path d='M4 9 L8 12 L4 15 Z' fill='@C' stroke='none'/>"},
    {"indent-dec",     "<line x1='4' y1='6' x2='20' y2='6'/><line x1='11' y1='10' x2='20' y2='10'/><line x1='11' y1='14' x2='20' y2='14'/><line x1='4' y1='18' x2='20' y2='18'/><path d='M8 9 L4 12 L8 15 Z' fill='@C' stroke='none'/>"},
    {"orientation",    "<path d='M4 20 L9 8 L14 20 M6 15.5 H12'/><path d='M15 5 A6 6 0 0 1 20 9'/><path d='M20 6 L20.5 9.5 L17 9'/>"},
    {"wrap",           "<line x1='4' y1='6' x2='20' y2='6'/><path d='M4 12 H16 A3 3 0 0 1 16 18 H12'/><path d='M14.5 15.5 L12 18 L14.5 20.5'/><line x1='4' y1='18' x2='8' y2='18'/>"},
    {"merge",          "<rect x='4' y='6' width='16' height='12' rx='1'/><line x1='12' y1='6' x2='12' y2='9'/><line x1='12' y1='15' x2='12' y2='18'/><path d='M9 10 L7 12 L9 14'/><path d='M15 10 L17 12 L15 14'/><line x1='7' y1='12' x2='17' y2='12'/>"},

    // ── Number ──────────────────────────────────────────────────────────────────
    {"currency",       "<circle cx='12' cy='12' r='8'/><path d='M14.7 9 A3 3 0 0 0 12 7.6 C9.7 7.6 9.7 10.4 12 10.8 C14.3 11.2 14.3 14 12 14 A3 3 0 0 1 9.3 12.6'/><line x1='12' y1='5.5' x2='12' y2='18.5'/>"},
    {"percent",        "<circle cx='7.5' cy='7.5' r='2.6'/><circle cx='16.5' cy='16.5' r='2.6'/><line x1='6' y1='18' x2='18' y2='6'/>"},
    {"comma",          "<line x1='13' y1='8' x2='20' y2='8'/><line x1='13' y1='12' x2='20' y2='12'/><line x1='13' y1='16' x2='18' y2='16'/><path d='M5 15 H8 V18 A3 3 0 0 1 5 21' fill='@C' stroke='none'/>"},
    {"dec-inc",        "<line x1='4' y1='8' x2='10' y2='8'/><line x1='4' y1='12' x2='10' y2='12'/><circle cx='6.5' cy='18' r='1.2' fill='@C' stroke='none'/><line x1='12' y1='17.5' x2='20' y2='17.5'/><path d='M16.5 14 L20 17.5 L16.5 21'/>"},
    {"dec-dec",        "<line x1='4' y1='8' x2='10' y2='8'/><line x1='4' y1='12' x2='10' y2='12'/><circle cx='6.5' cy='18' r='1.2' fill='@C' stroke='none'/><line x1='20' y1='17.5' x2='12' y2='17.5'/><path d='M15.5 14 L12 17.5 L15.5 21'/>"},

    // ── Styles ────────────────────────────────────────────────────────────────
    {"cond-format",    "<rect x='4' y='4' width='16' height='16' rx='1.5'/><line x1='4' y1='12' x2='20' y2='12'/><line x1='12' y1='4' x2='12' y2='20'/><circle cx='8' cy='8' r='1.6' fill='@C' stroke='none'/><circle cx='16' cy='16' r='1.6' fill='@C' stroke='none'/>"},
    {"format-table",   "<rect x='3' y='5' width='18' height='14' rx='1.5'/><path d='M3.5 6 A1 1 0 0 1 4.5 5 H19.5 A1 1 0 0 1 20.5 6 V9 H3.5 Z' fill='@C' stroke='none' opacity='0.22'/><line x1='3' y1='9' x2='21' y2='9'/><line x1='9' y1='9' x2='9' y2='19'/><line x1='15' y1='9' x2='15' y2='19'/><line x1='3' y1='14' x2='21' y2='14'/>"},
    {"cell-styles",    "<rect x='4' y='4' width='16' height='16' rx='1.5'/><path d='M9 15 L12 7.5 L15 15 M10 12.5 H14'/>"},

    // ── Cells ─────────────────────────────────────────────────────────────────
    {"insert-cells",   "<rect x='3.5' y='3.5' width='11' height='11' rx='1'/><line x1='18' y1='13' x2='18' y2='22'/><line x1='13.5' y1='17.5' x2='22.5' y2='17.5'/>"},
    {"delete-cells",   "<rect x='3.5' y='3.5' width='11' height='11' rx='1'/><line x1='15' y1='15' x2='21.5' y2='21.5'/><line x1='21.5' y1='15' x2='15' y2='21.5'/>"},
    {"format-cells",   "<rect x='3' y='8' width='18' height='8' rx='1'/><line x1='7' y1='8' x2='7' y2='12'/><line x1='11' y1='8' x2='11' y2='13'/><line x1='15' y1='8' x2='15' y2='12'/>"},

    // ── Editing / Functions ─────────────────────────────────────────────────────
    {"sigma",          "<path d='M17 5 H7 L13 12 L7 19 H17'/>"},
    {"fill",           "<rect x='5' y='4' width='14' height='5' rx='1'/><path d='M12 11 V19 M9 16 L12 19 L15 16'/>"},
    {"clear",          "<path d='M6 15 L13 8 A2 2 0 0 1 16 8 L19 11 A2 2 0 0 1 19 14 L14 19 H9 Z'/><line x1='8' y1='19' x2='19' y2='19'/>"},
    {"sort-filter",    "<path d='M4 5 H20 L14 12.5 V19 L10 17 V12.5 Z'/>"},
    {"sort-az",        "<path d='M6 5 V19 M3.5 16 L6 19 L8.5 16'/><path d='M12 6 H17 L12 11 H17'/><path d='M12.5 19 L14.5 13.5 L16.5 19 M13.2 17 H15.8'/>"},
    {"sort-za",        "<path d='M6 5 V19 M3.5 16 L6 19 L8.5 16'/><path d='M12.5 11 L14.5 5.5 L16.5 11 M13.2 9 H15.8'/><path d='M12 14 H17 L12 19 H17'/>"},
    {"find",           "<circle cx='10' cy='10' r='6'/><line x1='14.5' y1='14.5' x2='20.5' y2='20.5'/>"},

    // ── Insert ──────────────────────────────────────────────────────────────────
    {"pivot-table",    "<rect x='3' y='4' width='18' height='16' rx='1.5'/><line x1='3' y1='9' x2='21' y2='9'/><line x1='9' y1='9' x2='9' y2='20'/><path d='M13 12 L16 15 L13 18 M16 15 H11.5'/>"},
    {"pivot-chart",    "<rect x='3' y='4' width='10' height='16' rx='1.5'/><line x1='3' y1='9' x2='13' y2='9'/><line x1='8' y1='9' x2='8' y2='20'/><path d='M16 19 V13 M19 19 V9 M22 19 V15' transform='translate(-1.5 0)'/>"},
    {"table",          "<rect x='3' y='5' width='18' height='14' rx='1.5'/><line x1='3' y1='9' x2='21' y2='9'/><line x1='9' y1='5' x2='9' y2='19'/><line x1='15' y1='5' x2='15' y2='19'/><line x1='3' y1='14' x2='21' y2='14'/>"},
    {"picture",        "<rect x='3' y='5' width='18' height='14' rx='1.5'/><circle cx='8.5' cy='10' r='1.8'/><path d='M5 18 L10 13 L13 16 L16.5 12 L20 16'/>"},
    {"camera",         "<rect x='3' y='7' width='18' height='12' rx='1.5'/><circle cx='12' cy='13' r='3.2'/><path d='M8 7 L9.5 4.5 H14.5 L16 7'/>"},
    {"shapes",         "<rect x='3' y='12' width='8' height='8' rx='1'/><circle cx='16.5' cy='7.5' r='3.8'/><path d='M13 20 L17 13 L21 20 Z'/>"},
    {"icons-lib",      "<path d='M12 4 L14.3 8.7 L19.5 9.4 L15.7 13.1 L16.6 18.3 L12 15.9 L7.4 18.3 L8.3 13.1 L4.5 9.4 L9.7 8.7 Z'/>"},
    {"wordart",        "<path d='M4 19 L10 5 L16 19 M6.2 14 H13.8'/><path d='M18 7 Q21 9 18 11'/>"},
    {"textbox",        "<rect x='3' y='6' width='18' height='12' rx='1.5'/><line x1='9' y1='9.5' x2='15' y2='9.5'/><line x1='12' y1='9.5' x2='12' y2='14.5'/>"},
    {"file-object",    "<path d='M6 3 H14 L19 8 V20 A1 1 0 0 1 18 21 H6 A1 1 0 0 1 5 20 V4 A1 1 0 0 1 6 3 Z'/><path d='M14 3 V8 H19'/>"},
    {"chart",          "<path d='M4 4 V20 H20'/><rect x='7' y='12' width='3' height='6' fill='@C' stroke='none' opacity='0.25'/><rect x='7' y='12' width='3' height='6'/><rect x='12' y='8' width='3' height='10'/><rect x='17' y='5' width='3' height='13'/>"},
    {"chart-line",     "<path d='M4 4 V20 H20'/><path d='M6 16 L10 11 L13 14 L19 6'/><circle cx='19' cy='6' r='1.1' fill='@C' stroke='none'/>"},
    {"chart-pie",      "<circle cx='12' cy='12' r='8'/><path d='M12 12 V4 A8 8 0 0 1 20 12 Z' fill='@C' stroke='none' opacity='0.22'/><path d='M12 12 V4 M12 12 L20 12'/>"},
    {"chart-scatter",  "<path d='M4 4 V20 H20'/><circle cx='9' cy='15' r='1.2' fill='@C' stroke='none'/><circle cx='13' cy='9' r='1.2' fill='@C' stroke='none'/><circle cx='17' cy='12' r='1.2' fill='@C' stroke='none'/><circle cx='8' cy='10' r='1.2' fill='@C' stroke='none'/><circle cx='15' cy='16' r='1.2' fill='@C' stroke='none'/>"},
    {"sparkline",      "<path d='M3 16 L7 10 L11 13 L15 6 L21 12'/>"},
    {"link",           "<path d='M9 15 L15 9'/><path d='M11 7 L13 5 A3.5 3.5 0 0 1 18 10 L16 12'/><path d='M13 17 L11 19 A3.5 3.5 0 0 1 6 14 L8 12'/>"},
    {"equation",       "<path d='M4 13 L7 19 L12 5 H20'/>"},
    {"symbol",         "<path d='M5 19 H9 C5 16 5 8 12 8 C19 8 19 16 15 19 H19'/>"},
    {"latex",          "<path d='M4 13 L7 19 L11 5 H17'/><path d='M14 14 L18 19 M18 14 L14 19'/>"},
    {"forms",          "<rect x='4' y='3' width='16' height='18' rx='1.5'/><line x1='8' y1='8' x2='16' y2='8'/><line x1='8' y1='12' x2='16' y2='12'/><line x1='8' y1='16' x2='13' y2='16'/>"},

    // ── Page Layout ───────────────────────────────────────────────────────────
    {"print-preview",  "<path d='M7 9 V4 H17 V9'/><rect x='4' y='9' width='16' height='6' rx='1'/><rect x='7' y='14' width='10' height='6'/><circle cx='17' cy='12' r='1' fill='@C' stroke='none'/>"},
    {"print-area",     "<rect x='5' y='5' width='14' height='14' stroke-dasharray='2.5 2.5'/><path d='M9 9 H15 V15 H9 Z'/>"},
    {"margins",        "<rect x='4' y='4' width='16' height='16' rx='1'/><rect x='8' y='8' width='8' height='8' stroke-dasharray='2 2'/>"},
    {"page-orient",    "<rect x='6' y='4' width='12' height='16' rx='1.5'/><path d='M16 7 A5 5 0 0 1 20 11 M20 8 L20 11 L17 11'/>"},
    {"page-size",      "<rect x='5' y='3' width='14' height='18' rx='1.5'/><line x1='5' y1='8' x2='19' y2='8'/><line x1='9' y1='3' x2='9' y2='8'/>"},
    {"print-titles",   "<rect x='3' y='5' width='18' height='14' rx='1.5'/><path d='M3.5 6 A1 1 0 0 1 4.5 5 H19.5 A1 1 0 0 1 20.5 6 V9 H3.5 Z' fill='@C' stroke='none' opacity='0.22'/><line x1='3' y1='9' x2='21' y2='9'/><line x1='9' y1='9' x2='9' y2='19'/>"},
    {"header-footer",  "<rect x='4' y='4' width='16' height='16' rx='1.5'/><line x1='4' y1='8' x2='20' y2='8'/><line x1='4' y1='16' x2='20' y2='16'/>"},
    {"page-break",     "<rect x='5' y='3' width='14' height='18' rx='1.5'/><path d='M2 12 H22' stroke-dasharray='3 2'/>"},
    {"insert-break",   "<rect x='5' y='3' width='14' height='18' rx='1.5'/><line x1='5' y1='12' x2='19' y2='12'/><path d='M9 9.5 L12 12 L9 14.5'/>"},
    {"themes",         "<path d='M12 4 A8 8 0 1 0 12 20 A2 2 0 0 0 12 16 H13 A3 3 0 0 0 13 10 Z'/><circle cx='8' cy='10' r='1' fill='@C' stroke='none'/><circle cx='12' cy='8' r='1' fill='@C' stroke='none'/><circle cx='16' cy='11' r='1' fill='@C' stroke='none'/>"},
    {"settings",       "<circle cx='12' cy='12' r='3'/><path d='M12 3.5 V6 M12 18 V20.5 M3.5 12 H6 M18 12 H20.5 M5.7 5.7 L7.5 7.5 M16.5 16.5 L18.3 18.3 M18.3 5.7 L16.5 7.5 M7.5 16.5 L5.7 18.3'/>"},

    // ── Formulas ──────────────────────────────────────────────────────────────
    {"fx",             "<path d='M7 19 C9.5 19 9 11 10.5 7.5 C11.5 5 13 5 14.5 6 M7.5 12 H13'/><path d='M15 12 L19 18 M19 12 L15 18'/>"},
    {"recently-used",  "<circle cx='12' cy='12' r='8'/><path d='M12 7.5 V12 L15 14'/>"},
    {"fn-financial",   "<circle cx='12' cy='12' r='8'/><path d='M14.5 9.5 A3 3 0 0 0 12 8.2 C9.9 8.2 9.9 10.6 12 11 C14.1 11.4 14.1 13.8 12 13.8 A3 3 0 0 1 9.5 12.5'/><line x1='12' y1='6.3' x2='12' y2='17.7'/>"},
    {"fn-logical",     "<path d='M12 4 L20 12 L12 20 L4 12 Z'/><path d='M10.5 10.5 A1.8 1.8 0 1 1 12.5 12.4 V13.3'/><circle cx='12' cy='16' r='0.7' fill='@C' stroke='none'/>"},
    {"fn-text",        "<path d='M5 7 V5 H19 V7 M12 5 V19 M9.5 19 H14.5'/>"},
    {"fn-datetime",    "<rect x='4' y='5' width='16' height='15' rx='1.5'/><line x1='4' y1='9' x2='20' y2='9'/><line x1='9' y1='3' x2='9' y2='7'/><line x1='15' y1='3' x2='15' y2='7'/><path d='M12 12 V15 L14 16'/>"},
    {"fn-lookup",      "<rect x='3' y='4' width='13' height='11' rx='1.5'/><line x1='3' y1='8' x2='16' y2='8'/><line x1='9' y1='8' x2='9' y2='15'/><circle cx='16' cy='16' r='3.5'/><line x1='18.5' y1='18.5' x2='21' y2='21'/>"},
    {"fn-math",        "<path d='M4 6 H10 M7 3 V9'/><path d='M14 6 H20'/><line x1='14' y1='17' x2='20' y2='17'/><path d='M5 15 L9 19 M9 15 L5 19'/><circle cx='17' cy='13.5' r='0.8' fill='@C' stroke='none'/><circle cx='17' cy='20.5' r='0.8' fill='@C' stroke='none'/>"},
    {"fn-more",        "<circle cx='6' cy='12' r='1.5' fill='@C' stroke='none'/><circle cx='12' cy='12' r='1.5' fill='@C' stroke='none'/><circle cx='18' cy='12' r='1.5' fill='@C' stroke='none'/>"},
    {"name-manager",   "<path d='M4 6 A2 2 0 0 1 6 4 H12.5 L20 11.5 A2 2 0 0 1 20 14.5 L14.5 20 A2 2 0 0 1 11.5 20 L4 12.5 Z'/><circle cx='9' cy='9' r='1.4'/>"},
    {"define-name",    "<path d='M4 6 A2 2 0 0 1 6 4 H11 L17 10 V12 M4 6 V12 L10 18'/><line x1='17' y1='15' x2='17' y2='21'/><line x1='14' y1='18' x2='20' y2='18'/><circle cx='8' cy='8' r='1.2'/>"},
    {"trace-prec",     "<rect x='13' y='8' width='7.5' height='8' rx='1'/><circle cx='5.5' cy='12' r='2'/><path d='M7.5 12 H13 M11 9.5 L13 12 L11 14.5'/>"},
    {"trace-dep",      "<rect x='3.5' y='8' width='7.5' height='8' rx='1'/><circle cx='18.5' cy='12' r='2'/><path d='M11 12 H16.5 M14.5 9.5 L16.5 12 L14.5 14.5'/>"},
    {"remove-arrows",  "<path d='M3 12 H13 M10.5 9.5 L13 12 L10.5 14.5'/><line x1='15.5' y1='8' x2='21' y2='13.5'/><line x1='21' y1='8' x2='15.5' y2='13.5'/>"},
    {"show-formulas",  "<rect x='3' y='5' width='18' height='14' rx='1.5'/><path d='M8 9 C9.3 9 9 13 9.7 14.5 M6.5 11.5 H10.5'/><path d='M13 11 L16 15 M16 11 L13 15'/>"},
    {"error-check",    "<path d='M12 4 L21 20 H3 Z'/><line x1='12' y1='10' x2='12' y2='14'/><circle cx='12' cy='17' r='0.8' fill='@C' stroke='none'/>"},
    {"evaluate",       "<rect x='3' y='5' width='18' height='14' rx='1.5'/><path d='M8 9.5 L12 12 L8 14.5'/><line x1='13' y1='14.5' x2='17' y2='14.5'/>"},
    {"calculator",     "<rect x='4' y='3' width='16' height='18' rx='1.5'/><rect x='7' y='6' width='10' height='3.5' rx='0.5'/><circle cx='8' cy='13' r='0.9' fill='@C' stroke='none'/><circle cx='12' cy='13' r='0.9' fill='@C' stroke='none'/><circle cx='16' cy='13' r='0.9' fill='@C' stroke='none'/><circle cx='8' cy='17' r='0.9' fill='@C' stroke='none'/><circle cx='12' cy='17' r='0.9' fill='@C' stroke='none'/><circle cx='16' cy='17' r='0.9' fill='@C' stroke='none'/>"},
    {"calc-sheet",     "<rect x='4' y='3' width='16' height='18' rx='1.5'/><line x1='4' y1='8' x2='20' y2='8'/><path d='M7 12 H11 M7 15 H11 M7 18 H10'/><path d='M16 11 L13.5 17 M14 14 H18'/>"},

    // ── Data ────────────────────────────────────────────────────────────────────
    {"show-all",       "<path d='M4 5 H20 L14 12.5 V19 L10 17 V12.5 Z'/><line x1='15' y1='15' x2='20' y2='20'/>"},
    {"reapply",        "<path d='M4 5 H20 L14 12.5 V16 Z'/><path d='M18 16 A4 4 0 1 1 17 13 M18 12 V15 H15'/>"},
    {"highlight-dup",  "<rect x='4' y='4' width='10' height='10' rx='1'/><rect x='10' y='10' width='10' height='10' rx='1' fill='@C' stroke='none' opacity='0.18'/><rect x='10' y='10' width='10' height='10' rx='1'/>"},
    {"manage-dup",     "<rect x='4' y='4' width='9' height='9' rx='1'/><rect x='10' y='10' width='9' height='9' rx='1' fill='@C' stroke='none' opacity='0.18'/><rect x='10' y='10' width='9' height='9' rx='1'/><path d='M17 4 V8 M15 6 H19'/>"},
    {"text-columns",   "<rect x='3' y='5' width='18' height='14' rx='1.5'/><line x1='12' y1='5' x2='12' y2='19'/><path d='M6 10 L8 12 L6 14 M18 10 L16 12 L18 14'/>"},
    {"validation",     "<rect x='4' y='4' width='16' height='16' rx='2'/><path d='M8 12 L11 15 L16 9'/>"},
    {"consolidate",    "<rect x='9' y='9' width='6' height='6' rx='1'/><path d='M3 3 L7 7 M21 3 L17 7 M3 21 L7 17 M21 21 L17 17'/><path d='M5 3 H3 V5 M19 3 H21 V5 M5 21 H3 V19 M19 21 H21 V19'/>"},
    {"dropdown",       "<rect x='3' y='6' width='18' height='6' rx='1'/><path d='M14 8 L16 10 L18 8'/><line x1='3' y1='16' x2='13' y2='16'/><line x1='3' y1='19.5' x2='9' y2='19.5'/>"},
    {"subtotal",       "<rect x='3' y='5' width='18' height='14' rx='1.5'/><line x1='3' y1='14' x2='21' y2='14'/><line x1='6' y1='9' x2='13' y2='9'/><path d='M18 16 L15.5 16 L17 18 L15.5 18'/>"},
    {"group",          "<rect x='6' y='6' width='12' height='12' rx='1'/><path d='M3 6 V3 H6 M18 3 H21 V6 M3 18 V21 H6 M18 21 H21 V18'/>"},
    {"ungroup",        "<rect x='8' y='8' width='10' height='10' rx='1' stroke-dasharray='2.5 2'/><path d='M3 6 V3 H6 M18 3 H21 V6 M3 18 V21 H6'/>"},
    {"show-detail",    "<rect x='5' y='5' width='14' height='14' rx='1.5'/><line x1='9' y1='12' x2='15' y2='12'/><line x1='12' y1='9' x2='12' y2='15'/>"},
    {"hide-detail",    "<rect x='5' y='5' width='14' height='14' rx='1.5'/><line x1='9' y1='12' x2='15' y2='12'/>"},
    {"get-data",       "<path d='M4 7 C4 5 20 5 20 7 V17 C20 19 4 19 4 17 Z'/><path d='M4 7 C4 9 20 9 20 7'/><path d='M4 12 C4 14 20 14 20 12'/>"},
    {"edit-links",     "<path d='M9 14 L14 9'/><path d='M11 7 L13 5 A3 3 0 0 1 17 9 L15 11'/><path d='M5 19 L5 16 L13 8 L16 11 L8 19 Z'/>"},
    {"refresh",        "<path d='M19 8 A8 8 0 1 0 20 13'/><path d='M20 3.5 V8.5 H15'/>"},
    {"what-if",        "<circle cx='12' cy='12' r='8'/><path d='M10 10 A2 2 0 1 1 12.4 12 V13'/><circle cx='12' cy='16' r='0.7' fill='@C' stroke='none'/>"},

    // ── Review ──────────────────────────────────────────────────────────────────
    {"spelling",       "<path d='M3 18 L7 7 L11 18 M4.5 14 H9.5'/><path d='M14 11 H18 A2 2 0 0 1 18 15 H14 Z M14 15 H18.5 A2 2 0 0 1 18.5 19 H14 Z' opacity='0'/><path d='M13.5 16 L16 18.5 L21 12.5'/>"},
    {"thesaurus",      "<path d='M4 5 A2 2 0 0 1 6 3 H11 V18 H6 A2 2 0 0 0 4 20 Z'/><path d='M20 5 A2 2 0 0 0 18 3 H13 V18 H18 A2 2 0 0 1 20 20 Z'/>"},
    {"new-comment",    "<path d='M4 5 H20 A1 1 0 0 1 21 6 V15 A1 1 0 0 1 20 16 H10 L6 20 V16 H4 A1 1 0 0 1 3 15 V6 A1 1 0 0 1 4 5 Z'/><line x1='12' y1='8' x2='12' y2='13'/><line x1='9.5' y1='10.5' x2='14.5' y2='10.5'/>"},
    {"delete-comment", "<path d='M4 5 H20 A1 1 0 0 1 21 6 V15 A1 1 0 0 1 20 16 H10 L6 20 V16 H4 A1 1 0 0 1 3 15 V6 A1 1 0 0 1 4 5 Z'/><line x1='9.5' y1='8.5' x2='14.5' y2='12.5'/><line x1='14.5' y1='8.5' x2='9.5' y2='12.5'/>"},
    {"prev",           "<path d='M14.5 5 L8 12 L14.5 19'/>"},
    {"next",           "<path d='M9.5 5 L16 12 L9.5 19'/>"},
    {"show-comments",  "<path d='M4 5 H20 A1 1 0 0 1 21 6 V15 A1 1 0 0 1 20 16 H10 L6 20 V16 H4 A1 1 0 0 1 3 15 V6 A1 1 0 0 1 4 5 Z'/><line x1='7' y1='9' x2='17' y2='9'/><line x1='7' y1='12.5' x2='13' y2='12.5'/>"},
    {"reset",          "<path d='M5 8 A8 8 0 1 1 4.2 13'/><path d='M4 3.5 V8.5 H9'/>"},
    {"lock-cell",      "<rect x='5' y='11' width='14' height='9' rx='1.5'/><path d='M8 11 V8 A4 4 0 0 1 16 8 V11'/><circle cx='12' cy='15.5' r='1.3' fill='@C' stroke='none'/>"},
    {"allow-edit",     "<rect x='5' y='11' width='14' height='9' rx='1.5'/><path d='M8 11 V8 A4 4 0 0 1 15.5 6'/><circle cx='12' cy='15.5' r='1.3' fill='@C' stroke='none'/>"},
    {"protect-sheet",  "<path d='M12 3 L20 6 V11 C20 16 16.5 19.5 12 21 C7.5 19.5 4 16 4 11 V6 Z'/><path d='M9 12 L11 14 L15 9.5'/>"},
    {"protect-book",   "<path d='M12 3 L20 6 V11 C20 16 16.5 19.5 12 21 C7.5 19.5 4 16 4 11 V6 Z'/><line x1='9' y1='10' x2='15' y2='10'/><line x1='9' y1='13' x2='15' y2='13'/>"},
    {"share",          "<circle cx='6' cy='12' r='2.6'/><circle cx='17' cy='6.5' r='2.6'/><circle cx='17' cy='17.5' r='2.6'/><line x1='8.2' y1='10.8' x2='14.8' y2='7.5'/><line x1='8.2' y1='13.2' x2='14.8' y2='16.5'/>"},

    // ── View ──────────────────────────────────────────────────────────────────
    {"view-normal",    "<rect x='3' y='5' width='18' height='14' rx='1.5'/><line x1='3' y1='9' x2='21' y2='9'/><line x1='9' y1='9' x2='9' y2='19'/><line x1='15' y1='9' x2='15' y2='19'/>"},
    {"view-pagelayout","<rect x='4' y='3' width='16' height='18' rx='1.5'/><line x1='4' y1='7' x2='20' y2='7'/><line x1='4' y1='17' x2='20' y2='17'/>"},
    {"eye",            "<path d='M3 12 C6 6.5 18 6.5 21 12 C18 17.5 6 17.5 3 12 Z'/><circle cx='12' cy='12' r='2.6'/>"},
    {"highlight-rc",   "<rect x='3' y='3' width='18' height='18' rx='1.5'/><rect x='3' y='9' width='18' height='5' fill='@C' stroke='none' opacity='0.18'/><rect x='9' y='3' width='5' height='18' fill='@C' stroke='none' opacity='0.18'/>"},
    {"full-screen",    "<path d='M4 9 V4 H9 M15 4 H20 V9 M20 15 V20 H15 M9 20 H4 V15'/>"},
    {"freeze",         "<rect x='3' y='3' width='18' height='18' rx='1.5'/><line x1='3' y1='9' x2='21' y2='9' stroke-width='2.2'/><line x1='9' y1='3' x2='9' y2='21' stroke-width='2.2'/>"},
    {"arrange-all",    "<rect x='3' y='3' width='8' height='8' rx='1'/><rect x='13' y='3' width='8' height='8' rx='1'/><rect x='3' y='13' width='8' height='8' rx='1'/><rect x='13' y='13' width='8' height='8' rx='1'/>"},
    {"new-window",     "<rect x='3' y='6' width='13' height='13' rx='1.5'/><path d='M9 6 V4 A1 1 0 0 1 10 3 H20 A1 1 0 0 1 21 4 V14 A1 1 0 0 1 20 15 H18'/>"},
    {"split-window",   "<rect x='3' y='3' width='18' height='18' rx='1.5'/><line x1='3' y1='12' x2='21' y2='12'/><line x1='12' y1='3' x2='12' y2='21'/>"},
    {"side-by-side",   "<rect x='3' y='5' width='8' height='14' rx='1.5'/><rect x='13' y='5' width='8' height='14' rx='1.5'/>"},
    {"sync-scroll",    "<path d='M7 5 V19 M4.5 16 L7 19 L9.5 16'/><path d='M17 19 V5 M14.5 8 L17 5 L19.5 8'/>"},
    {"zoom-in",        "<circle cx='10' cy='10' r='6'/><line x1='14.5' y1='14.5' x2='20.5' y2='20.5'/><line x1='10' y1='7.5' x2='10' y2='12.5'/><line x1='7.5' y1='10' x2='12.5' y2='10'/>"},
    {"zoom-100",       "<circle cx='10' cy='10' r='6'/><line x1='14.5' y1='14.5' x2='20.5' y2='20.5'/><line x1='7.5' y1='10' x2='12.5' y2='10'/>"},
    {"custom-views",   "<rect x='3' y='5' width='18' height='14' rx='1.5'/><circle cx='12' cy='12' r='3'/><path d='M12 6 V8 M12 16 V18'/>"},

    // ── Tools ────────────────────────────────────────────────────────────────
    {"export-pdf",     "<path d='M6 3 H13 L18 8 V20 A1 1 0 0 1 17 21 H7 A1 1 0 0 1 6 20' opacity='0'/><path d='M6 3 H13 L18 8 V15 H6 Z'/><path d='M13 3 V8 H18'/><path d='M12 14 V21 M9 18 L12 21 L15 18'/>"},
    {"export-pic",     "<rect x='3' y='4' width='18' height='12' rx='1.5'/><circle cx='8' cy='8.5' r='1.6'/><path d='M4.5 15 L9 10.5 L12 13 L15 9.5 L19.5 14'/><path d='M12 17 V22 M9 19 L12 22 L15 19'/>"},
    {"extract-text",   "<path d='M6 3 H14 L19 8 V20 A1 1 0 0 1 18 21 H6 A1 1 0 0 1 5 20 V4 A1 1 0 0 1 6 3 Z'/><path d='M14 3 V8 H19'/><line x1='8' y1='12' x2='16' y2='12'/><line x1='8' y1='15' x2='16' y2='15'/><line x1='8' y1='18' x2='13' y2='18'/>"},
    {"split-merge",    "<rect x='3' y='4' width='8' height='16' rx='1.5'/><rect x='13' y='4' width='8' height='16' rx='1.5'/><path d='M11 12 H13'/>"},
    {"auto-backup",    "<path d='M7 18 A4.5 4.5 0 0 1 6.5 9 A5.5 5.5 0 0 1 17 9.5 A4 4 0 0 1 16.5 18 Z'/><path d='M12 11 V16 M9.5 13.5 L12 16 L14.5 13.5'/>"},
    {"save-cloud",     "<path d='M7 18 A4.5 4.5 0 0 1 6.5 9 A5.5 5.5 0 0 1 17 9.5 A4 4 0 0 1 16.5 18 Z'/><path d='M12 16 V10 M9.5 12.5 L12 10 L14.5 12.5'/>"},
    {"file-collect",   "<path d='M3 7 A1 1 0 0 1 4 6 H9 L11 8 H20 A1 1 0 0 1 21 9 V18 A1 1 0 0 1 20 19 H4 A1 1 0 0 1 3 18 Z'/>"},
    {"design-lib",     "<rect x='3' y='3' width='8' height='8' rx='1'/><circle cx='17' cy='7' r='4'/><path d='M7 13 L11 20 H3 Z'/><rect x='13' y='13' width='8' height='8' rx='1'/>"},
    {"invoice",        "<path d='M6 3 H18 V21 L15.5 19 L13 21 L10.5 19 L8 21 L6 19 Z'/><line x1='9' y1='8' x2='15' y2='8'/><line x1='9' y1='12' x2='15' y2='12'/>"},
    {"batch-rename",   "<path d='M6 3 H14 L19 8 V20 A1 1 0 0 1 18 21 H6 A1 1 0 0 1 5 20 V4 A1 1 0 0 1 6 3 Z'/><path d='M14 3 V8 H19'/><path d='M9 17 L9 14 L14 9 L17 12 L12 17 Z'/>"},

    // ── Smart Toolbox ───────────────────────────────────────────────────────────
    {"smart-insert",   "<rect x='3' y='5' width='14' height='14' rx='1.5'/><line x1='3' y1='10' x2='17' y2='10'/><line x1='8' y1='10' x2='8' y2='19'/><path d='M20 4 V10 M17 7 H23'/>"},
    {"smart-text",     "<rect x='3' y='5' width='18' height='14' rx='1.5'/><line x1='12' y1='5' x2='12' y2='19'/><path d='M6 10 H9 M15 10 H18'/>"},

    // ── Generic ───────────────────────────────────────────────────────────────
    {"generic",        "<rect x='4' y='4' width='16' height='16' rx='2.5'/>"},
    };
    return m;
}

// Map aliases (same artwork, different logical names used in the ribbon).
static QString resolveName(const QString& name) {
    static const QHash<QString, QString> alias = {
        {"autosum", "sigma"}, {"insert-function", "fx"}, {"recently", "recently-used"},
        {"calc-now", "calculator"}, {"calc-options", "settings"}, {"pivot", "pivot-table"},
        {"filter", "sort-filter"}, {"sort", "sort-az"}, {"chart-column", "chart"},
        {"chart-bar", "chart"}, {"chart-area", "chart-line"}, {"chart-lib", "chart"},
        {"background", "picture"}, {"screenshot", "camera"}, {"page-break-preview", "page-break"},
        {"view-pagebreak", "page-break"}, {"reset-position", "reset"}, {"use-in-formula", "paste"},
        {"picture-pdf", "export-pdf"}, {"pdf-excel", "export-pdf"}, {"scan-mobile", "save-cloud"},
        {"split-sheet", "split-merge"}, {"merge-sheet", "split-merge"},
        {"smart-fill", "fill"}, {"smart-delete", "delete-cells"}, {"smart-format", "format-cells"},
        {"smart-calc", "calculator"}, {"smart-catalog", "dropdown"},
    };
    return alias.value(name, name);
}

static QPixmap renderSvg(const QString& svg, int px) {
    QSvgRenderer r(svg.toUtf8());
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    r.render(&p);
    p.end();
    return pm;
}

QIcon calcIcon(const QString& name, const QColor& color) {
    static QHash<QString, QIcon> cache;
    const QString key = name + '|' + color.name();
    const auto it = cache.constFind(key);
    if (it != cache.constEnd()) return it.value();

    const auto& bodies = iconBodies();
    QString body = bodies.value(resolveName(name));
    if (body.isEmpty()) body = bodies.value("generic");
    body.replace("@C", color.name());

    const QString svg = QStringLiteral(
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' "
        "stroke='%1' stroke-width='1.7' stroke-linecap='round' stroke-linejoin='round'>%2</svg>")
        .arg(color.name(), body);

    // Render once at a high resolution; QIcon downscales smoothly for any button.
    QIcon icon(renderSvg(svg, 96));
    cache.insert(key, icon);
    return icon;
}

} // namespace NativeOffice
