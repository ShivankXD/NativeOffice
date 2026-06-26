#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// WriterRibbon.h  (Sprint 14)
// WPS/Word-style tabbed ribbon for the NativeOffice Writer, replacing the old
// single-row WriterToolbar.
//
// Tabs: Home | Insert | Page Layout | References | Review | View | Tools
//   • Home is fully implemented (Clipboard · Font · Paragraph · Styles · Editing)
//   • Other tabs are "Coming Soon" placeholders.
//
// The ribbon is self-contained: it holds a pointer to the target QTextEdit and
// performs all formatting directly (mirroring the original WriterToolbar design),
// so WriterModule only needs to attach the editor and listen for image inserts.
//
// Features: format painter, paste variants, searchable font picker, font sizing,
// change-case, clear formatting, B/I/U (+underline styles)/strike, sub/superscript,
// font & highlight colour, bullets/numbering, indent, sort, formatting marks,
// alignment, line spacing, shading, borders, a working paragraph-styles engine,
// find & replace, and select-similar. Collapsible by double-clicking a tab.
// ─────────────────────────────────────────────────────────────────────────────

#include <QWidget>
#include <QColor>
#include <QList>
#include <QPointer>

class QIcon;
class QTextEdit;
class QToolButton;
class QComboBox;
class QButtonGroup;
class QStackedWidget;
class QTextCharFormat;
class QDialog;
class QLineEdit;
class QCheckBox;

namespace NativeOffice {

// Built-in paragraph styles (SECTION 3).
enum class WriterStyle {
    Normal, NoSpacing, Heading1, Heading2, Heading3, Title, Subtitle, Quote
};

class WriterRibbon : public QWidget {
    Q_OBJECT

public:
    explicit WriterRibbon(QWidget* parent = nullptr);

    // Attach the target editor. Must be called before use.
    void attachEditor(QTextEdit* editor);

    // Sync all checked/active states to the cursor's current format.
    void syncToCurrentFormat();

signals:
    // Forwarded to WriterModule (which owns the embed pipeline).
    void insertImageRequested();

    // Page-level operations handled by WriterModule (owns page geometry/zoom).
    void zoomInRequested();
    void zoomOutRequested();
    void zoomResetRequested();
    void pageMarginsRequested(double px);
    void orientationRequested(bool landscape);
    void pageSizeRequested(double portraitW, double portraitH);
    void pageColorRequested(const QColor& color);
    void webLayoutRequested(bool web);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    // ── tab construction ────────────────────────────────────────────────────
    QWidget* buildHomeTab();
    QWidget* buildInsertTab();
    QWidget* buildPageLayoutTab();
    QWidget* buildReferencesTab();
    QWidget* buildReviewTab();
    QWidget* buildViewTab();
    QWidget* buildToolsTab();
    QWidget* buildPlaceholderTab(const QString& tabName);

    // ── small builders (ported from the Impress ribbon visual language) ─────
    QToolButton* makeTabButton(const QString& label);
    QToolButton* makeToolBtn(const QString& text, const QString& tip, bool checkable = false);
    QToolButton* makeIconBtn(const QIcon& icon, const QString& tip, bool checkable = false);
    QToolButton* makeBigBtn(const QIcon& icon, const QString& text,
                            const QString& tip, bool checkable = false);
    QWidget*     makeSeparator();
    QWidget*     makeGroup(const QString& name, const QList<QWidget*>& widgets);
    void         applyStyles();

    // ── formatting helpers ──────────────────────────────────────────────────
    void mergeFormatOnSelection(const QTextCharFormat& fmt);
    void adjustFontSize(int delta);
    void applyChangeCase(int mode);
    void clearFormatting();
    void applyUnderlineStyle(int style);
    void toggleVerticalAlign(int align);    // 0 normal, 1 super, 2 sub
    void applyBullets(int style);
    void applyNumbering(int style);
    void changeIndent(int delta);
    void sortParagraphs(bool ascending);
    void toggleFormattingMarks(bool show);
    void setAlignment(Qt::Alignment a);
    void applyLineSpacing(double mult);
    void applyShading(const QColor& c);
    void applyBorder(int kind);             // 0 none,1 bottom,2 top,3 hr
    void applyParagraphStyle(WriterStyle s);
    void pasteAsPlainText();

    // format painter
    void toggleFormatPainter(bool on);

    // ── Insert-tab actions (operate directly on the editor) ─────────────────
    void insertImageData(const QImage& img);   // embed a QImage as base64 PNG
    void insertTableSized(int rows, int cols);
    void insertPageBreak();
    void insertHorizontalRule();
    void insertHyperlink();
    void insertBookmark();
    void insertPageNumberField();
    void insertTextBox();
    void insertWordArt();
    void insertDropCap();
    void insertDateTimeText(const QString& formatted);
    void insertSymbolText(const QString& sym);
    void insertShapeImage(int kind);
    void insertCoverPage();
    void insertHeaderFooter(bool header);
    void insertEquation(const QString& text);
    void insertChart(int kind);

    // ── References / Review / Tools actions ─────────────────────────────────
    void changeParagraphIndent(int side, double deltaPx);  // side: 0 left, 1 right
    void changeParagraphSpacing(bool before, double deltaPx);
    void insertTableOfContents();
    void insertFootnote();
    void insertCitation(const QString& text);
    void insertBibliography();
    void insertCaption(const QString& kind);
    void insertComment();
    void showWordCountDialog();
    void showSpellingDialog();

    // Sprint 16 — denser tab feature set
    void setTextDirection(bool rtl);
    void applyPageBorders(int kind);          // 0 none, 1 box, 2 shadow
    void insertEndnote();
    void insertTableOfFigures();
    void insertCrossReference();
    void markIndexEntry();
    void insertIndex();
    void deleteComments(bool all);
    void gotoComment(bool next);
    void acceptAllChanges();
    void setReadOnly(bool on);
    void toggleFullScreen();
    void exportToPdf();
    void exportToPicture();
    void exportToText();

    QToolButton* makeRowBtn(const QIcon& icon, const QString& text, const QString& tip,
                            bool checkable = false);

    // find & replace
    void openFindReplace();

    // select with similar formatting (contiguous run around the cursor)
    void selectSimilarFormatting();

    // ── state ───────────────────────────────────────────────────────────────
    QTextEdit*      m_editor { nullptr };

    QStackedWidget* m_stack    { nullptr };
    QButtonGroup*   m_tabGroup { nullptr };
    bool            m_collapsed { false };

    // Font group
    QComboBox*   m_fontCombo { nullptr };
    QComboBox*   m_sizeCombo { nullptr };
    QToolButton* m_btnBold      { nullptr };
    QToolButton* m_btnItalic    { nullptr };
    QToolButton* m_btnUnderline { nullptr };
    QToolButton* m_btnStrike    { nullptr };
    QToolButton* m_btnSub       { nullptr };
    QToolButton* m_btnSuper     { nullptr };
    QToolButton* m_btnFontColor { nullptr };
    QToolButton* m_btnHighlight { nullptr };
    QColor       m_fontColor { "#1C1E26" };
    QColor       m_highlightColor { "#FFF27A" };

    // Paragraph group
    QToolButton* m_btnAlignLeft    { nullptr };
    QToolButton* m_btnAlignCenter  { nullptr };
    QToolButton* m_btnAlignRight   { nullptr };
    QToolButton* m_btnAlignJustify { nullptr };
    QToolButton* m_btnMarks        { nullptr };

    // Clipboard
    QToolButton* m_btnPainter { nullptr };

    // Styles gallery
    QButtonGroup* m_styleGroup { nullptr };

    // Format painter state
    bool             m_painterActive { false };
    QTextCharFormat* m_painterFmt    { nullptr };

    // Find & replace (modeless, lazily created)
    QPointer<QDialog> m_findDlg;
    QLineEdit*        m_findEdit    { nullptr };
    QLineEdit*        m_replaceEdit { nullptr };
    QCheckBox*        m_matchCase   { nullptr };

    bool m_syncing { false };
};

} // namespace NativeOffice
