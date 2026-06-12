# NativeOffice — Project Summary

> **Auto-maintained file.** Updated automatically at every development milestone.  
> Last updated: **2026-06-12** · Status: **🟢 Sprint 10 Complete — Writer Image Insertion**

---

## 1. Project Overview

**NativeOffice** is a high-performance, cross-platform native desktop office suite built in modern C++ with Qt 6. It is designed to rival WPS Office and Microsoft Office in user experience and performance.

---

## 2. Technology Stack

| Layer            | Technology                               |
|------------------|------------------------------------------|
| Language         | C++20                                    |
| Build System     | CMake 3.21+                              |
| UI Framework     | Qt 6 (QtWidgets + QtSvg)                 |
| Persistence      | QSettings (registry on Windows, plist on macOS, .conf on Linux) |
| File Format      | `.noff` (UTF-8 HTML with custom header comment) |
| Target Platforms | Windows, macOS, Linux                    |

---

## 3. UI Theme Colors (Derived from Logo)

| Role             | Hex       | Description                              |
|------------------|-----------|------------------------------------------|
| **Primary**      | `#2C3140` | Deep Charcoal — sidebar bg, Writer toolbar |
| **Secondary**    | `#E8372A` | Vivid Scarlet — active states, selection highlight |
| **Accent**       | `#1A1F2E` | Near-black — logo area bg, dropdown backgrounds |
| **Background**   | `#F5F6FA` | App-level off-white                      |
| **Surface**      | `#FFFFFF` | Pure white — cards, panels, the Writer paper |
| **Canvas**       | `#E8E9ED` | Off-white behind the Writer paper        |
| **Text Primary** | `#1C1E26` | Default document text                    |
| **Text Muted**   | `#9CA3AF` | Metadata, hints, placeholders            |
| **Writer Blue**  | `#2563EB` | Writer / Document module tile            |
| **Calc Green**   | `#16A34A` | Calc / Spreadsheet module tile           |
| **Impress Org.** | `#EA580C` | Impress / Presentation module tile       |

---

## 4. Folder Architecture

```
NativeOffice/
├── CMakeLists.txt
├── logo.jpg
├── summary.md                           ← This file
├── resources/resources.qrc
└── src/
    ├── core/
    │   ├── CMakeLists.txt
    │   ├── theme/ThemeManager.h/cpp     ← Color palette + Qt stylesheet
    │   └── application/
    │       ├── AppController.h/cpp      ← Signal-based navigation router
    │       ├── RecentFilesManager.h/cpp ← ✅ QSettings-backed file list
    │       └── FileRouter.h/cpp        ← ✅ NEW: Content-based file type detection
    ├── modules/
    │   ├── writer/                      ← ✅ Word Processor (SPRINT 10 COMPLETE)
    │   │   ├── CMakeLists.txt
    │   │   ├── WriterModule.h/cpp       ← File I/O + Image Insertion (Sprint 10)
    │   │   └── WriterToolbar.h/cpp      ← Formatting toolbar + Insert Image button
    │   ├── calc/                        ← ✅ Spreadsheet Engine (SPRINT 8 COMPLETE)
    │   │   ├── CMakeLists.txt
    │   │   ├── CalcModule.h/cpp         ← Root widget + JSON file I/O (Sprint 8)
    │   │   ├── SpreadsheetModel.h/cpp   ← QAbstractTableModel (100x26 grid) + rawData()
    │   │   ├── FormulaEngine.h/cpp      ← Recursive-descent parser + SUM/AVERAGE/range math (Sprint 9)
    │   │   └── CalcHeaderView.h/cpp     ← Themed Charcoal/Scarlet headers
    │   └── impress/                     ← ✅ Presentation Tool (SPRINT 8 COMPLETE)
    │       ├── ImpressModule.h/cpp      ← Three-pane + JSON file I/O (Sprint 8)
    │       ├── SlideData.h              ← Pure data: SlideItem + SlideData
    │       ├── SlideScene.h/cpp         ← QGraphicsScene: render + saveToData/loadFromData
    │       ├── SlidePanelWidget.h/cpp   ← Thumbnail panel + clear()
    │       ├── SlideThumbnailWidget.h/cpp
    │       └── ImpressToolbar.h/cpp
    └── app/
        ├── CMakeLists.txt
        ├── main.cpp                     ← ✅ Sprint 8: WriterWindow + CalcWindow + ImpressWindow
        └── startscreen/
            ├── StartScreen.h/cpp
            ├── SidebarWidget.h/cpp
            ├── TemplateBarWidget.h/cpp
            └── RecentFilesWidget.h/cpp  ← ✅ Clickable, QSettings-backed
```

---

## 5. The .noff File Format

```
<!-- NativeOffice Writer Document (.noff) -->
<!DOCTYPE HTML PUBLIC ...>
<html><head>...</head><body>
  ... Qt-generated rich-text HTML ...
</body></html>
```

- **Extension**: `.noff`
- **Encoding**: UTF-8
- **Content**: Standard Qt `QTextEdit::toHtml()` output (rich text, inline CSS)
- **Header comment**: Self-describing for external tools
- **Compatibility**: Readable by any browser; future sprints may add a ZIP container for embedded images

---

## 6. Current Development Status

### ✅ Sprint 1 — Project Bootstrap & Start Screen
- CMake build system, ThemeManager, AppController, full Start Screen UI

### ✅ Sprint 2 — WriterModule / Word Processor
- Full formatting toolbar (B/I/U, font, size, colour, alignment)
- A4-proportioned QTextEdit paper with shadow
- `documentModified` signal, dirty indicator in title bar

### ✅ Sprint 3 — Writer File Persistence (Complete)
- **`RecentFilesManager`** — QSettings-backed singleton
  - Persists up to 20 recent files across sessions
  - `addFile(path, type)` / `removeFile(path)` / `clearAll()`
  - Emits `listChanged()` signal for reactive UI updates
- **`WriterModule`** file I/O upgrades
  - `saveToPath(path)` — writes `.noff` (UTF-8 HTML), clears dirty flag, emits `filePathChanged`
  - `loadFromPath(path)` — reads `.noff`, sets HTML, clears dirty flag
  - `isDirty()` / `markClean()` / `titleString()` — proper document state
  - `m_ignoreChange` guard prevents spurious dirty-flag on programmatic loads
- **`WriterWindow`** (in main.cpp) — dedicated QMainWindow subclass
  - `save()` — silent overwrite if path known, else triggers `saveAs()`
  - `saveAs()` — native file dialog filtered to `.noff`, updates title on success
  - `open` — native file dialog, creates new `WriterWindow`
  - `closeEvent()` — prompts Save / Discard / Cancel for unsaved changes
  - Title format: `"* Filename.noff — NativeOffice Writer"` (star = dirty)
- **`RecentFilesWidget`** Sprint 3 overhaul
  - Loads from `RecentFilesManager` on construction
  - Auto-refreshes via `RecentFilesManager::listChanged()` signal
  - **Rows are properly clickable** (`ClickableRow` inner class with `mousePressEvent`)
  - Right-click context menu: "Remove from list"
  - Open arrow `›` indicator on each row
  - "Clear all" button wired to `RecentFilesManager::clearAll()`

### ✅ Sprint 4 — CalcModule / Spreadsheet Engine (Complete)
- **`FormulaEngine`** — stateless recursive-descent arithmetic parser
  - Grammar: `expr = term { +/- term }`, `term = factor { */div factor }`, `factor = number | cellRef | (expr)`
  - Cell references (A1, BC23) resolved via CellLookup callback — model-agnostic
  - Error tokens: `#ERR` (parse fail / divide-by-zero), `#CIRC` (circular reference guard)
  - Recursive formula chains resolve correctly (=A1+B1 where A1 is itself =C1*2)
- **`SpreadsheetModel`** — `QAbstractTableModel` (100 rows x 26 columns, A-Z)
  - `Qt::DisplayRole` evaluated result, `Qt::EditRole` raw input
  - Numbers right-aligned (#1C1E26), formula results blue (#2563EB), errors scarlet (#E8372A)
  - Whole-sheet dataChanged broadcast keeps dependent formula cells fresh
- **`CalcHeaderView`** — custom `QHeaderView` with brand styling
  - Column headers: Charcoal #2C3140; selected column: Scarlet #E8372A
  - Row headers: #343848 (lighter charcoal); selected row: Scarlet #E8372A
- **`CalcModule`** — full spreadsheet widget
  - Formula Bar (36 px, Charcoal): Name Box + fx label + QLineEdit
  - Formula bar shows raw content; commits on Enter; advances to next row
  - QTableView with Tab navigation, AnyKeyPressed editing trigger

### ✅ Sprint 5 — ImpressModule / Presentation Tool (Complete)
- **`SlideData`** — plain C++ struct for slide data (SlideItem + SlideData); serialisable
- **`SlideScene`** — `QGraphicsScene` (960×540 px, 16:9)
  - White slide surface at z=-1 (non-selectable background rect)
  - `InsertMode` enum: None / TextBox / Rectangle / Ellipse
  - `TextBox`: click-to-place editable `QGraphicsTextItem` at cursor position
  - `Rectangle`: drag-to-draw `QGraphicsRectItem` with translucent charcoal fill
  - `Ellipse`: drag-to-draw `QGraphicsEllipseItem` with translucent scarlet fill
  - Dashed scarlet preview item shown during drag; replaced with real item on release
  - Minimum 20 px drag distance — single click still creates a sensible 160×90 shape
  - `addDefaultPlaceholders()` inserts "Click to add Title" + "Click to add Subtitle"
  - `loadFromData()` / `saveToData()` round-trip for slide switching
  - Emits `sceneModified()` → thumbnail auto-refreshes
  - Emits `insertModeLeft()` → toolbar button un-checks automatically
- **`SlideThumbnailWidget`** — 160×90 px clickable mini-slide preview
  - Renders `QGraphicsScene::render()` to a `QPixmap`
  - Slide number label above thumbnail
  - Scarlet border when active, grey hover border, drop shadow
- **`SlidePanelWidget`** — 200 px Charcoal left sidebar
  - Scarlet "＋ New Slide" button at top
  - `QScrollArea` of thumbnails below
  - `setActiveSlide(idx)` highlights the current slide
- **`ImpressToolbar`** — 44 px Charcoal toolbar
  - T / ☐ / ◯ shape buttons (exclusive `QButtonGroup`; active = Scarlet bg)
  - ⎘ Duplicate / ✕ Delete slide buttons
- **`ImpressModule`** — three-pane root widget
  - `QGraphicsView` with `fitInView(16:9)` — re-fits on every `resizeEvent`
  - `addNewSlide()` creates scene + placeholders + thumbnail, switches to it
  - `duplicateCurrentSlide()` deep-copies `SlideData` and creates new scene
  - `deleteCurrentSlide()` guards against last-slide deletion with `QMessageBox`
  - Slide switching saves current scene to `SlideData` before loading next

### ✅ Sprint 6 — PDF Export / Cross-Compatibility (Complete)
- **Writer PDF Export** (`File → ★ Export to PDF…`, `Ctrl+Shift+E`)
  - `QPdfWriter` at **300 dpi**, A4 portrait, 20 mm margins on all sides
  - `QTextDocument::print(&pdfWriter)` — handles pagination, rich text, fonts, inline CSS
  - Suggested filename auto-derived from the open `.noff` path (same folder, `.pdf` extension)
  - Success dialog confirms output path
- **Impress PDF Export** (`File → ★ Export to PDF…`, `Ctrl+Shift+E`)
  - `QPdfWriter` at **150 dpi**, custom widescreen 16:9 page (338.67 × 190.5 mm, no margins)
  - Loops every `SlideScene` in the deck: each slide → one PDF page via `QPainter` + `QGraphicsScene::render()`
  - Aspect-ratio guard: letterboxes the slide inside the page if rounding produces non-exact 16:9
  - `saveToData()` called for the current slide before iteration so unsaved in-flight edits are captured
  - Success dialog reports number of slides exported and output path
- **`createImpressWindow()` helper** added (mirrors `createWriterWindow()`)
  - All three Impress window creation sites (new, open-by-path, start screen) now use it
  - Full menu bar: `File → New Presentation / Export to PDF… / Close` + View + Help
- **CMake**: `Qt6::PrintSupport` added to `WriterModule`, `ImpressModule`, and `NativeOffice` executable

### ✅ Sprint 7 — Smart Content-Based File Routing (Complete)
- **`FileRouter`** — stateless content-detection utility (`core/application/`)
  - `detectFileType(path)` returns `DetectedFileType::{WriterDocument, SpreadsheetData, PresentationData}`
  - **Extension fast-path**: binary formats (`.xlsx`, `.ods`, `.pptx`, `.odp`) routed by extension alone
  - **Content sampling**: reads first ~8 KB of text-based files for heuristic analysis
  - **Presentation markers**: NativeOffice Impress header, JSON `"slides"` key, ODP/PPTX XML namespaces
  - **Spreadsheet/CSV markers**: ≥60% of non-empty lines with consistent column count (≥2 columns, comma or tab delimited)
  - **Default fallback**: HTML tags, `.noff` header, or plain text → Writer
- **`openDocumentByPath()`** in `main.cpp` overhauled (Sprint 7)
  - Uses `FileRouter::detectFileType()` instead of raw extension matching
  - Registers the **detected** type in `RecentFilesManager` so the badge is accurate
  - A `.txt` file with CSV data now correctly opens in Calc, not Writer
- **Sidebar "Open" button** — now functional
  - `SidebarItem::Open` wired to `QFileDialog` → `fileOpenRequested` signal
  - Supports all file types: `.noff .html .txt .csv .tsv .pptx .odp .xlsx .ods`
  - Routes through `AppController` → `openDocumentByPath()` → `FileRouter`

### ✅ Sprint 8 — Full File Persistence for Calc & Impress (Complete)
- **CalcModule File I/O** — JSON-based `.noff` format
  - `saveToPath(path)` serializes non-empty cells from `SpreadsheetModel::rawData()` into `{"type":"calc","cells":[{col,row,value},…]}`
  - `loadFromPath(path)` parses JSON; falls back to CSV import (comma-separated) for plain `.csv` files
  - Header comment: `<!-- NativeOffice Calc Spreadsheet (.noff) -->`
  - Dirty flag tracked via `SpreadsheetModel::dataChanged` signal with `m_ignoreChange` guard
  - `titleString()` / `isDirty()` / `markClean()` API mirrors WriterModule
- **ImpressModule File I/O** — JSON-based `.noff` format
  - `saveToPath(path)` serializes full slide deck: `{"type":"impress","slides":[{title,background,items:[{type,x,y,w,h,text,fontSize,fillColor,penColor,penWidth},…]},…]}`
  - `loadFromPath(path)` parses JSON, clears existing deck via `clearDeck()`, recreates scenes with `createSlide()`
  - Header comment: `<!-- NativeOffice Impress Presentation (.noff) -->`
  - Dirty flag tracked via `SlideScene::sceneModified` signal
  - All shape types preserved: TextBox (text + fontSize + placeholder flag), Rectangle, Ellipse
  - Colors stored in `#aarrggbb` hex format for full alpha support
- **CalcWindow** subclass in `main.cpp` (mirrors WriterWindow)
  - Save / Save As / Open menus with `Ctrl+S`, `Ctrl+Shift+S`, `Ctrl+O`
  - Close-confirmation dialog on dirty state (Save / Discard / Cancel)
  - `RecentFilesManager::addFile(path, "Calc")` on save
- **ImpressWindow** subclass in `main.cpp` (mirrors WriterWindow)
  - Save / Save As / Open / Export to PDF menus
  - Close-confirmation dialog on dirty state
  - `RecentFilesManager::addFile(path, "Impress")` on save
- **`createCalcWindow(filePath)`** — new free function with full File menu
- **`createImpressWindow(filePath)`** — upgraded from Sprint 6; now has Save/SaveAs/Open/Close
- **`openDocumentByPath()`** simplified — now delegates to `createCalcWindow(path)` / `createImpressWindow(path)`
- **FileRouter** updated — added `NativeOffice Calc` header detection (Sprint 8)
- **SlidePanelWidget** — added `clear()` method for deck reset during `loadFromPath()`
- **SpreadsheetModel** — added `rawData()` const accessor for serialization

### ✅ Sprint 9 — FormulaEngine Range Math & Office Functions (Complete)
- **Range Parsing Layer (Colon Syntax)**
  - `expandRange(rangeStr)` — static helper that expands `"A1:A5"` into `{"A1","A2","A3","A4","A5"}`
  - Handles vertical ranges (same column), horizontal ranges (same row), and rectangular ranges (multi-col × multi-row)
  - Normalises reversed ranges (e.g. `A5:A1` treated as `A1:A5`)
- **`parseFactor()` upgraded** with function-call lookahead
  - When a letter-sequence is followed by `(`, routes to `parseFuncCall()`
  - Otherwise falls through to existing `parseCellRef()` logic — zero impact on existing formulas
- **SUM() function**
  - `=SUM(A1:A5)` sums all numeric cells in the range; text/empty cells silently skipped (Excel behavior)
  - Works in expression context: `=SUM(A1:A3)+10` evaluates correctly
  - Handles formulas within the range (recursive evaluation via existing `CellLookup` chain)
- **AVERAGE() function**
  - `=AVERAGE(A1:A3)` computes sum / count-of-numeric-cells
  - Returns `#ERR` when zero numeric cells are found (empty range or all text)
- **Case-insensitive** — `=sum(a1:a3)`, `=SUM(A1:A3)`, `=Sum(A1:A3)` all work identically
- **Error handling**
  - Errors within a range (`#ERR`, `#CIRC`) propagate up to the calling formula
  - Circular references detected naturally: range resolution goes through the existing `CellLookup` → `evaluate()` path, so `SpreadsheetModel::m_evaluating` guard fires as expected
  - Invalid range syntax, unknown function names → `#ERR`
- **No model or CMake changes required** — all new code is inside `FormulaEngine.h/cpp`; the existing `CellLookup` callback pattern meant no `SpreadsheetModel` changes were needed

### ✅ Sprint 10 — Writer Image Insertion (Complete)
- **Toolbar Integration**
  - New “Insert Image” button (🖼 icon) added to `WriterToolbar`, positioned after alignment buttons with a separator
  - Styled consistently with ThemeManager Charcoal/Scarlet palette via existing `#toolbarBtn` stylesheet
  - Emits `insertImageRequested()` signal — toolbar stays policy-free, `WriterModule` handles the logic
- **Image Handling Pipeline**
  - `QFileDialog::getOpenFileName()` filtered for `*.png *.jpg *.jpeg *.bmp`
  - `QImage` loads the selected file; silently aborts if the image is null
  - Auto-scaling: images wider than 674 px (= 794 A4 width − 60×2 document margins) are scaled down with `Qt::SmoothTransformation`
- **Rich Text Embedding**
  - Scaled image encoded to PNG Base64 via `QBuffer` + `QImage::save()`
  - Inserted at cursor position as `<img src="data:image/png;base64,..."/>` using `QTextCursor::insertHtml()`
  - Image flows inline with text — no absolute positioning
- **File Persistence**
  - Base64 images embed inside QTextEdit’s HTML output — no file-system changes needed
  - `.noff` save/load already persists full HTML, so images round-trip automatically
  - `QTextDocument::contentsChanged` fires on insert, correctly setting the dirty flag (`*` title prefix)
- **No CMake changes** — `QImage`, `QBuffer`, `QFileDialog` are all in `Qt6::Gui`/`Qt6::Widgets`, already linked

### 🔲 Ongoing Backlog
- Custom app icon
- Unit tests (Catch2 / Google Test)
- Installer packaging (NSIS for Windows, `.dmg` for macOS)

---

## 7. Build Instructions

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug ^
      -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/msvc2022_64"
cmake --build build --config Debug --parallel
.\build\src\app\Debug\NativeOffice.exe
```

---

## 8. Key Design Decisions

| Decision | Rationale |
|---|---|
| `.noff` = UTF-8 HTML | QTextEdit's native format; no custom parser needed; human-readable; browser-viewable |
| `RecentFilesManager` in `core` | Available to all layers without circular deps; `RecentFilesWidget` consumes it |
| `ClickableRow` inner class | Widget-level mouse event captures the full 60 px hit zone — no tiny button targets |
| `m_ignoreChange` guard | Prevents `saveToPath`/`loadFromPath` from setting dirty flag during programmatic edits |
| `WriterWindow` subclass in main.cpp | Keeps all file I/O policy in one place; modules stay policy-free |
| QSettings array storage | Survives app restart; no custom file needed for recent-files persistence |
| `CellLookup` callback in FormulaEngine | Engine is stateless and model-agnostic; any backing store can be plugged in |
| Whole-sheet `dataChanged` on edit | Simpler than a dependency graph; fast enough for 100x26; avoids stale formula cells |
| `#CIRC` circular-reference guard | Prevents infinite recursion without a full DAG; sufficient for Sprint 4 |
| `Qt::EditRole` raw / `Qt::DisplayRole` evaluated | Standard Qt MVC split; delegates always see raw text for editing |
| `QAbstractTableModel` not `QStandardItemModel` | Fine-grained control over roles, alignment, and color without QStandardItem overhead |
| `SlideData` decoupled from `SlideScene` | Data is serialisable without any Qt graphics dependency; future file format work is clean |
| `QGraphicsScene::render()` for thumbnails | Native Qt; no extra libraries; always pixel-accurate with the live canvas |
| `fitInView(16:9)` on `resizeEvent` | Slide always fills available space without scrollbars; maintains exact aspect ratio |
| Minimum-drag-size guard (20 px) | Single-click still produces a visible shape — same UX as PowerPoint click-to-place |
| `insertModeLeft` signal | Toolbar button auto-unchecks without the scene needing a back-reference to the toolbar |
| `QTextDocument::print()` for Writer PDF | Qt handles pagination + rich text rendering natively; zero custom layout code needed |
| `QGraphicsScene::render()` for Impress PDF | Same rendering path used by thumbnails; pixel-accurate at any DPI |
| 16:9 custom page size for Impress PDF | Prevents letterboxing in PDF viewers; slide fills the whole page |
| 300 dpi Writer / 150 dpi Impress | Writer needs print quality; Impress at 150 dpi is smaller file, still sharp on screen |
| `createImpressWindow()` free function | Mirrors `createWriterWindow()`; all window creation goes through one consistent path |
| `Ctrl+Shift+E` shortcut for Export to PDF | Consistent across Writer and Impress; doesn't conflict with standard Ctrl+P (print) |
| Content-based routing via `FileRouter` | Extension-only routing is fragile; reading file content makes `.txt` with CSV data open in Calc correctly |
| 8 KB sample size | Enough to detect headers, CSV patterns, and XML namespaces without reading the entire file |
| ≥60% consistent-column threshold | Avoids false positives on prose text that happens to contain a comma; requires real tabular structure |
| Binary format extension fast-path | `.xlsx`/`.pptx` are ZIP-based binary formats that can't be usefully scanned as text; skip content read |
| Sidebar "Open" → `fileOpenRequested` signal | Reuses the existing signal chain through `AppController` → no new wiring in `main.cpp` needed |
| JSON `.noff` for Calc/Impress | Human-readable, easy to debug; `QJsonDocument` is built into Qt Core — no external dependencies |
| Sparse cell storage in Calc JSON | Only non-empty cells are saved; a 100×26 grid with 10 cells produces a tiny file |
| CSV fallback in `CalcModule::loadFromPath()` | FileRouter routes `.csv` to Calc; the loader must handle it without requiring JSON |
| `CalcWindow` / `ImpressWindow` subclasses | Keeps file I/O policy (save/dirty/close-confirm) in `main.cpp`; modules stay policy-free |
| `clearDeck()` in ImpressModule | Centralized scene cleanup for `loadFromPath()`; detaches view, deletes scenes, clears panel |
| `rawData()` const accessor on SpreadsheetModel | Read-only; doesn't break encapsulation but allows CalcModule to iterate for serialization |
| `#aarrggbb` color format in Impress JSON | Preserves alpha channel for translucent shape fills; `QColor::name(HexArgb)` is native Qt |
| Header comments in `.noff` files | `FileRouter` uses these for instant detection without parsing JSON; different header per module |
| Function-call lookahead in `parseFactor()` | Minimal grammar change: letter+`(` routes to function call, otherwise existing cellRef path; zero impact on Sprint 4 arithmetic |
| `expandRange()` as a static public helper | Reusable outside the parser — future functions (COUNT, MIN, MAX) and unit tests can call it directly |
| Range values skip non-numeric cells | Matches Excel/Sheets behavior: `SUM(A1:A3)` ignores text; `AVERAGE` divides by numeric count only |
| `AVERAGE` returns `#ERR` on empty range | Prevents divide-by-zero; mirrors Excel's `#DIV/0!` semantics |
| Case-insensitive function names via `toUpper()` | Standard Excel/Sheets behavior — users shouldn't need to remember capitalisation |
| No `SpreadsheetModel` changes for Sprint 9 | Range resolution reuses the existing `CellLookup` → `evaluate()` chain; `#CIRC` detection works automatically |
| `insertImageRequested()` signal in WriterToolbar | Toolbar stays policy-free; WriterModule owns the file-dialog and embedding logic |
| Base64 PNG data URIs for image embedding | Images live inside the HTML string — no external files to manage; `.noff` save/load works unchanged |
| 674 px max image width | A4 paper (794 px) minus 60 px margins on each side; images never overflow the printable area |
| `Qt::SmoothTransformation` for scaling | Anti-aliased downscale looks sharp on screen and in PDF export |
| PNG encoding for all inserted images | Lossless; preserves transparency; JPEG originals are re-encoded to PNG for consistency |
