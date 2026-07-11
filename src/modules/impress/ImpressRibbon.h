#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ImpressRibbon.h  (Sprint 12)
// Tabbed ribbon for the Impress module, replacing the old single-row
// ImpressToolbar. Tabs: Home, Insert, Design, Transitions, Animations,
// Slide Show, Review, View. Each tab is a real (if compact) toolset rather
// than a placeholder row.
// ─────────────────────────────────────────────────────────────────────────────

#include "SlideScene.h"     // InsertMode
#include "SlideData.h"      // SlideLayout, SlideTransition

#include <QWidget>
#include <QList>
#include <QIcon>

class QToolButton;
class QButtonGroup;
class QComboBox;
class QStackedWidget;

namespace NativeOffice {

enum class ImpressViewMode;

class ImpressRibbon : public QWidget {
    Q_OBJECT

public:
    explicit ImpressRibbon(QWidget* parent = nullptr);

    // Call when the scene resets insert mode (so the button un-checks)
    void resetInsertMode();
    void setUndoRedoEnabled(bool undoEnabled, bool redoEnabled);
    // Reflect the deck's slide-number state on the Insert-tab toggle button.
    void setSlideNumberActive(bool on);

    // Sync formatting buttons to the current text selection's format
    void syncCharFormat(bool bold, bool italic, bool underline, bool strike,
                        const QString& family, int pointSize, const QColor& color);
    void syncAlignment(Qt::Alignment align);

signals:
    void undoRequested();
    void redoRequested();

    void boldToggled(bool on);
    void italicToggled(bool on);
    void underlineToggled(bool on);
    void strikeToggled(bool on);
    void fontFamilyChanged(const QString& family);
    void fontSizeChanged(int pt);
    void textColorChanged(const QColor& color);

    void alignChanged(Qt::Alignment align);
    void bulletsRequested();
    void numberingRequested();
    void indentRequested(int direction);          // +1 = increase, -1 = decrease
    void lineSpacingChanged(double multiplier);

    void newSlideRequested();
    void duplicateSlideRequested();
    void deleteSlideRequested();
    void layoutSelected(NativeOffice::SlideLayout layout);

    void insertModeChanged(NativeOffice::InsertMode mode);
    void shapeInsertRequested(NativeOffice::ShapeKind kind);
    void smartArtRequested(NativeOffice::SmartArtKind kind);
    void shapeFillRequested(const QColor& color);
    void shapeOutlineRequested(const QColor& color);
    void shadowToggleRequested();
    void insertTableRequested(int rows, int cols);
    void chartRequested(NativeOffice::ChartKind kind);
    void insertImageRequested();
    void wordArtRequested();
    void symbolRequested(const QString& symbol);
    void slideNumberToggled(bool on);   // Insert → Slide Number (checkable)
    void dateTimeRequested();

    void designColorSelected(const QColor& color);
    void designThemeSelected(const QColor& top, const QColor& bottom);
    void templatesRequested();
    void designColorAllRequested(const QColor& color);
    void designThemeAllRequested(const QColor& top, const QColor& bottom);

    void transitionSelected(NativeOffice::SlideTransition transition);
    void transitionApplyAllRequested(NativeOffice::SlideTransition transition);
    void animationSelected(NativeOffice::ItemAnimation animation);
    void slideAnimationSelected(NativeOffice::SlideAnimation animation);

    void slideShowFromBeginningRequested();
    void slideShowFromCurrentRequested();
    void presenterViewRequested();

    void notesToggleRequested();
    void commentsToggleRequested();

    void viewModeChanged(NativeOffice::ImpressViewMode mode);

private:
    void     ensureTabBuilt(int id);   // lazy tab construction (perf)
    QVector<bool> m_tabBuilt;
    QWidget* buildHomeTab();
    QWidget* buildInsertTab();
    QWidget* buildDesignTab();
    QWidget* buildTransitionsTab();
    QWidget* buildAnimationsTab();
    QWidget* buildSlideShowTab();
    QWidget* buildReviewTab();
    QWidget* buildViewTab();

    QToolButton* makeTabButton(const QString& label);
    QToolButton* makeToolBtn(const QString& text, const QString& tooltip, bool checkable = false);
    QToolButton* makeIconBtn(const QIcon& icon, const QString& tooltip, bool checkable = false);
    // Flexible-width text command button (fixed height only) — e.g. "New Slide".
    QToolButton* makeCmdBtn(const QString& text, const QString& tooltip,
                            int minWidth, bool checkable = false);
    // WPS-style large button: icon on top, caption beneath (icon-over-text).
    QToolButton* makeBigBtn(const QIcon& icon, const QString& text,
                            const QString& tooltip, bool checkable = false);
    QWidget*     makeSeparator();
    // Wrap a row of controls into a labelled WPS-style ribbon group.
    QWidget*     makeGroup(const QString& name, const QList<QWidget*>& widgets);

    void applyStyles();

    QStackedWidget* m_stack { nullptr };
    QButtonGroup*   m_tabGroup { nullptr };

    // Home tab widgets needed for sync
    QToolButton* m_btnUndo { nullptr };
    QToolButton* m_btnRedo { nullptr };
    QComboBox*   m_fontCombo { nullptr };
    QComboBox*   m_sizeCombo { nullptr };
    QToolButton* m_btnBold { nullptr };
    QToolButton* m_btnItalic { nullptr };
    QToolButton* m_btnUnderline { nullptr };
    QToolButton* m_btnStrike { nullptr };
    QToolButton* m_btnColor { nullptr };
    QToolButton* m_btnAlignLeft { nullptr };
    QToolButton* m_btnAlignCenter { nullptr };
    QToolButton* m_btnAlignRight { nullptr };
    QToolButton* m_btnAlignJustify { nullptr };

    QToolButton*  m_slideNumberBtn { nullptr };  // Insert → Slide Number toggle
    QButtonGroup* m_shapeGroup { nullptr };
    QButtonGroup* m_transitionGroup { nullptr };
    QToolButton*  m_designAllBtn { nullptr };   // "Apply to all slides" toggle
    bool          m_syncing { false };
    QColor        m_textColor { "#1C1E26" };
};

} // namespace NativeOffice
