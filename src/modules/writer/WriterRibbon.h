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
#include <QHash>
#include <QString>
#include <QPointer>

class QIcon;
class QPrinter;
class QTextEdit;
class QToolButton;
class QComboBox;
class QButtonGroup;
class QStackedWidget;
class QTextCharFormat;
class QDialog;
class QLineEdit;
class QCheckBox;
class QLabel;

class QGridLayout;

namespace NativeOffice {

class WriterStyleManager;

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

    // ── Style persistence (called by WriterModule on save/load) ──────────────
    // JSON describing the custom + modified-built-in styles (empty if none).
    [[nodiscard]] QString styleDefsJson() const;
    // JSON mapping non-default paragraphs (by block index) to their style name.
    [[nodiscard]] QString blockAssignmentsJson() const;
    // Restore styles + per-paragraph tags after a document is loaded.
    void applyPersistedStyles(const QString& defsJson, const QString& blocksJson);

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
    void rulerToggled(bool show);
    void navPaneToggled(bool show);
    void commentsPaneToggled(bool show);
    void commentsChanged();            // a comment was added/removed → refresh pane
    void focusModeRequested(bool on);  // View → Focus Mode

public:
    // Reflects Focus Mode state back onto the View tab's button when the mode is
    // toggled by its shortcut rather than by a click (no-op if the lazily-built
    // View tab hasn't been opened yet).
    void setFocusModeChecked(bool on);

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
    QWidget* buildTableTab();
    void     ensureTabBuilt(int id);     // lazy tab construction (perf)
    void     refreshTableTab();          // show/hide the contextual Table tab
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

    // ── Named styles (Tier 2 #8) ────────────────────────────────────────────
    void applyStyleByName(const QString& name);
    void rebuildStyleGallery();
    void rebuildMoreStylesMenu();
    void openStyleEditor(const QString& editName);   // empty → create new
    void updateStyleFromSelection(const QString& name);
    void renameStyleInteractive(const QString& name);
    void deleteStyleInteractive(const QString& name);
    void showStyleContextMenu(const QString& name, const QPoint& globalPos);
    void manageStyles();

    // ── Multi-level lists (Tier 2 #9) ───────────────────────────────────────
    void applyMultilevelList(bool numbered);
    void changeListLevel(int delta);
    void restartNumbering();
    void continueNumbering();
    void customizeList();

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
    // focusSection: 0 = focus the Header row, 1 = focus the Footer row.
    void openHeaderFooterDialog(int focusSection = 0);
    void insertBlankPage();
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
    void showAutoCorrectOptions();

    // Sprint 16 — denser tab feature set
    void setTextDirection(bool rtl);
    void applyColumns(int n);                 // multi-column layout (Tier 3 #11)
    void applyPageBorders(int kind);          // 0 none, 1 box, 2 shadow
    void insertEndnote();
    void insertTableOfFigures();
    void insertCrossReference();
    void updateFields();             // renumber captions / refresh fields
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
    void exportToWord();
    void importFromWord();

    // Tier 4 — printing & templates
    void printDocument();
    void printPreview();
    void renderToPrinter(QPrinter* printer);
    void showTemplateGallery();
    void applyTemplate(int id);

    // Tier 5 — AI assistant
    void aiRewrite();
    void aiSummarize();
    void aiGenerate();
    void aiSettings();

    // Tier 5 — real-time collaboration (LAN)
    void collabHost();
    void collabJoin();
    void collabStop();

    // Tier 4 — equation editor, mail merge, document compare
    void showEquationEditor();
    void insertEquationImage(const QString& expr);
    void showMailMerge();
    void insertMergeField(const QString& field);
    void mergeToDocument();
    void showCompareDialog();
    void compareWithFile(const QString& path);

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
    QToolButton* m_btnFocus        { nullptr };   // View → Focus Mode
    QToolButton* m_btnFocusHome    { nullptr };   // Home → Focus Mode (same toggle)

    // Clipboard
    QToolButton* m_btnPainter { nullptr };

    // Styles gallery
    QButtonGroup* m_styleGroup { nullptr };
    WriterStyleManager* m_styles { nullptr };
    QWidget*      m_styleGrid    { nullptr };   // container repopulated on change
    QToolButton*  m_moreStylesBtn { nullptr };
    QHash<QString, QToolButton*> m_styleButtons;   // style name → gallery chip

    // Contextual "Table" tab (shown only when the cursor is in a table)
    QToolButton* m_tableTabBtn { nullptr };

    // Format painter state
    bool             m_painterActive { false };
    QTextCharFormat* m_painterFmt    { nullptr };

    // Find & replace (modeless, lazily created)
    QPointer<QDialog> m_findDlg;
    QLineEdit*        m_findEdit    { nullptr };
    QLineEdit*        m_replaceEdit { nullptr };
    QCheckBox*        m_matchCase   { nullptr };
    QCheckBox*        m_wholeWord   { nullptr };
    QCheckBox*        m_useRegex    { nullptr };
    QLabel*           m_findCount   { nullptr };
    void              highlightAllMatches();   // live "highlight all"

    bool m_syncing { false };
    int  m_caseCycle { 0 };   // Shift+F3 cycle position (UPPER → lower → Title)
    QVector<bool> m_tabBuilt; // lazy ribbon tabs: which stack pages exist yet

    // Mail-merge data source (headers + records).
    QStringList         m_mergeHeaders;
    QList<QStringList>  m_mergeRows;

    // Tier 5 — AI assistant
    class WriterAi* m_ai { nullptr };
    void runAi(const QString& system, const QString& user, bool replaceSelection);

    // Tier 5 — collaboration
    class WriterCollab* m_collab { nullptr };
    QLabel*             m_collabStatus { nullptr };
};

} // namespace NativeOffice
