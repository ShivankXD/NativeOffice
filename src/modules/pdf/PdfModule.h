#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfModule.h — the PDF editor: a WPS-style ribbon-tabbed PDF application
// (viewer + page organizer + structural editing), replacing the old
// tool-hub-with-cards layout.
//
// Composition:
//   PdfRibbon (8 tabs) ─ emits PdfAction values
//   Sidebar (bookmarks / thumbnails / comments) │ center stack:
//       • Pdf::Viewer     — continuous page canvas (all tabs except Page)
//       • PageOrganizer   — thumbnail grid with drag-reorder (Page tab)
//   StatusBar — page navigation + zoom
//
// Document state lives in Pdf::EditSession (whole-file revisions with
// undo/redo). Exposes the same file-state interface the other modules give
// their EditorWindow wrapper (currentFilePath/isDirty/titleString/saveToPath).
// ─────────────────────────────────────────────────────────────────────────────

#include "PdfRibbon.h"

#include <QColor>
#include <QPolygonF>
#include <QRectF>
#include <QWidget>

class QStackedWidget;

namespace NativeOffice {

namespace Pdf {
class EditSession;
class Viewer;
class Sidebar;
class StatusBar;
}
class PageOrganizer;
class PdfFindBar;

class PdfModule : public QWidget {
    Q_OBJECT

public:
    explicit PdfModule(QWidget* parent = nullptr);

    [[nodiscard]] QString currentFilePath() const noexcept;
    [[nodiscard]] bool    isDirty()         const noexcept;
    [[nodiscard]] QString titleString()     const;

    // Opens a .pdf (from Home, CLI, or the File menu). Prompts for a
    // password if the file is encrypted.
    void setInitialFile(const QString& path);

    bool saveToPath(const QString& path);   // EditorWindow save pipeline

signals:
    void documentModified();
    void filePathChanged(const QString& newPath);

private:
    void dispatch(PdfAction a);
    void openInteractive();                 // file picker + password loop
    bool openPath(const QString& path);

    // action helpers (implemented across the feature files)
    void doSave(bool saveAs);
    void doPrint();
    void doMerge();
    void doSplit();
    void doCompress();
    void doRotate(int degreesCW, bool allPages);
    void doDeletePages();
    void doExtractPages();
    void doInsertBlank();
    void doInsertFromFile();
    void doReplacePages();
    void doPageSize();
    void doCropPages();
    void doFind();
    void doExtractText();
    void toast(const QString& message);     // transient status message
    void comingSoon(const QString& feature);

    // ── comment / annotation tools ──────────────────────────────────────
    // m_pendingAnnot >= 0 holds an AnnotSpec::Kind cast to int while a tool
    // is armed; -1 means no comment tool is active.
    void startCommentTool(int kind, const QColor& color);
    void onRectPlaced(const QString& tag, int page, const QRectF& rectPt);
    void onClickPlaced(const QString& tag, int page, const QPointF& posPt);
    void onInkDrawn(const QString& tag, int page, const QPolygonF& strokePt);
    void refreshComments();                 // repopulate the comments pane
    void doExportComments();
    void doImportComments();
    void doAddBookmark();
    void doAddLink(int page, const QRectF& rectPt);

    // ── fill & sign ─────────────────────────────────────────────────────
    void doFillForm();
    void doHighlightFields();
    void doAddSignature(bool initials);

    // ── protect ─────────────────────────────────────────────────────────
    void doEncrypt();
    void doSign();
    void doTimestamp();
    void doValidateSignatures();

    // ── convert ─────────────────────────────────────────────────────────
    void doConvert(PdfAction which);
    void doPictureToPdf();
    void doOcr();

    [[nodiscard]] std::vector<int> selectedOrCurrentPages() const;

    Pdf::EditSession* m_session   { nullptr };
    PdfRibbon*        m_ribbon    { nullptr };
    Pdf::Viewer*      m_viewer    { nullptr };
    Pdf::Sidebar*     m_sidebar   { nullptr };
    Pdf::StatusBar*   m_status    { nullptr };
    PageOrganizer*    m_organizer { nullptr };
    QStackedWidget*   m_center    { nullptr };
    PdfFindBar*       m_findBar   { nullptr };
    bool m_readMode = false;

    // find state (search continues from the last hit)
    QString m_lastFindNeedle;
    int     m_lastFindPage = -1;

    // comment-tool state
    int     m_pendingAnnot = -1;            // AnnotSpec::Kind as int, or -1
    QColor  m_pendingAnnotColor;
    QString m_pendingAnnotImage;            // image/attachment path for the pending tool
    bool    m_commentsHidden = false;
    bool    m_fieldsHighlighted = false;
};

} // namespace NativeOffice
