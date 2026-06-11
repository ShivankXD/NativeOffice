# NativeOffice — Project Summary

> **Auto-maintained file.** Updated automatically at every development milestone.  
> Last updated: **2026-06-11** · Status: **🟢 Sprint 6 Complete — PDF Export live for Writer & Impress**

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
    │       ├── RecentFilesManager.h/cpp ← ✅ NEW: QSettings-backed file list
    ├── modules/
    │   ├── writer/                      ← ✅ Word Processor (SPRINT 3 COMPLETE)
    │   │   ├── CMakeLists.txt
    │   │   ├── WriterModule.h/cpp       ← File I/O: saveToPath / loadFromPath
    │   │   └── WriterToolbar.h/cpp      ← Formatting toolbar
    │   ├── calc/                        ← ✅ Spreadsheet Engine (SPRINT 4 COMPLETE)
    │   │   ├── CMakeLists.txt
    │   │   ├── CalcModule.h/cpp         ← Root widget: formula bar + grid
    │   │   ├── SpreadsheetModel.h/cpp   ← QAbstractTableModel (100x26 grid)
    │   │   ├── FormulaEngine.h/cpp      ← Recursive-descent formula parser
    │   │   └── CalcHeaderView.h/cpp     ← Themed Charcoal/Scarlet headers
    │   └── impress/ (stub – Sprint 5)
    └── app/
        ├── CMakeLists.txt
        ├── main.cpp                     ← ✅ Sprint 3: WriterWindow + full file ops
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

### 🔲 Ongoing Backlog
- Custom app icon
- Unit tests (Catch2 / Google Test)
- Installer packaging (NSIS for Windows, `.dmg` for macOS)
- CalcModule file persistence (`.nocalc` or `.csv` format)
- ImpressModule file persistence (`.noimpress` JSON format)

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
| `Ctrl+Shift+E` shortcut for Export to PDF | Consistent across Writer and Impress; doesn’t conflict with standard Ctrl+P (print) |
