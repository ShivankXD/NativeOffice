#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfRibbon.h — WPS-style tabbed ribbon for the PDF editor.
//
// Tabs: Home | Edit | Page | Comment | Tool | Fill & Sign | Protect | Convert
// (WPS's cloud/AI features — WPS AI, Scan to Mobile, File Collect, Request
// E-signatures, Snip & Pin — are deliberately not reproduced.)
//
// The ribbon is presentation-only: every button resolves to a PdfAction value
// emitted through action(); PdfModule owns all behavior. Tabs are lazy-built
// on first click (same perf pattern as ImpressRibbon/WriterRibbon).
// ─────────────────────────────────────────────────────────────────────────────

#include <QWidget>
#include <QList>

class QButtonGroup;
class QComboBox;
class QStackedWidget;
class QToolButton;

namespace NativeOffice {

enum class PdfAction {
    // tools
    PanTool, SelectTool,
    // content edit
    EditContent, AddText, AddPicture, WipeOff,
    // convert
    ToWord, ToExcel, ToPpt, ToPicture, ToText, PictureToPdf, ToImageOnlyPdf,
    ExtractText, ExtractPageBtn, ExtractPicture,
    // page structure
    MergePdf, SplitPdf, InsertBlankPage, InsertFromFile, DeletePages,
    ReplacePages, RotateLeft, RotateRight, RotateAllPages, CropPages, PageSizeDlg,
    // annotations
    HighlightText, HighlightArea, Underline, Strikethrough, TextComment,
    TextBox, Callout, NoteAnnot, InkDraw, ShapeRect, ShapeEllipse, ShapeLine,
    ShapeArrow, ReplaceTextAnnot, InsertTextAnnot, AttachFile,
    HideComments, ManageComments, ExportComments, ImportComments,
    // edit-tab inserts
    AddLink, AddBookmark, Watermark, Background, PageNumber, HeaderFooter, StampDefault,
    // tool tab
    Compress, Print,
    // fill & sign
    FillForm, HighlightFields, AddSignature, AddInitials,
    // protect
    Encrypt, CertSign, Timestamp, ManageCerts, ValidateSigs,
    // misc
    Find, OcrPdf, Translate, ReadMode, ZoomIn, ZoomOut,
    // file menu (leftmost in the tab row, like WPS's ☰ File)
    OpenFile, SaveFile, SaveFileAs, UndoEdit, RedoEdit,
};

class PdfRibbon : public QWidget {
    Q_OBJECT

public:
    explicit PdfRibbon(QWidget* parent = nullptr);

    // Reflect the viewer's tool mode in the Pan/Select toggles of every tab.
    void syncToolMode(bool panActive);

    // Reflect the viewer's zoom in the Home-tab zoom combo.
    void setZoomPercent(int percent);

signals:
    void action(NativeOffice::PdfAction a);
    void stampSelected(const QString& text);     // Stamp ▾ menu entries
    void zoomPercentRequested(int percent);      // Home-tab zoom combo
    void tabChanged(int index);                  // 2 == Page tab (organizer view)

private:
    QWidget* buildHomeTab();
    QWidget* buildEditTab();
    QWidget* buildPageTab();
    QWidget* buildCommentTab();
    QWidget* buildToolTab();
    QWidget* buildFillSignTab();
    QWidget* buildProtectTab();
    QWidget* buildConvertTab();
    void ensureTabBuilt(int id);

    // building blocks
    QToolButton* makeTabButton(const QString& text);
    QToolButton* makeBigBtn(const QString& text, const QIcon& icon,
                            PdfAction a, const QString& tip = {}, bool enabled = true);
    QToolButton* makeBigMenuBtn(const QString& text, const QIcon& icon);
    QWidget*     makeToolPair(QToolButton*& panBtn, QToolButton*& selBtn,
                              const QString& panLabel);
    QWidget*     makeSeparator();
    void applyStyles();

    QButtonGroup*   m_tabGroup { nullptr };
    QStackedWidget* m_stack    { nullptr };
    QList<bool>     m_tabBuilt;
    QComboBox*      m_zoomCombo{ nullptr };
    QList<QToolButton*> m_panBtns, m_selBtns;    // one pair per tab that has them
};

} // namespace NativeOffice
