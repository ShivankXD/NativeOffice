#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// WriterToolbar.h  (Sprint 2)
// Formatting toolbar for the NativeOffice Writer (Word Processor).
//
// Provides real-time, selection-aware controls wired to a target QTextEdit:
//   • Bold / Italic / Underline toggle buttons
//   • Font family combo box
//   • Font size combo box (8–72 pt)
//   • Text colour picker (defaults to brand Charcoal #2C3140)
//   • Paragraph alignment buttons (Left / Center / Right / Justify)
//   • Undo / Redo buttons
//
// Design: Charcoal (#2C3140) toolbar background, Scarlet (#E8372A) for
//         checked/hovered/active states — consistent with ThemeManager palette.
// ─────────────────────────────────────────────────────────────────────────────

#include <QWidget>
#include <QToolButton>
#include <QComboBox>
#include <QPushButton>
#include <QColor>

class QTextEdit;
class QHBoxLayout;
class QFrame;

namespace NativeOffice {

class WriterToolbar : public QWidget {
    Q_OBJECT

public:
    explicit WriterToolbar(QWidget* parent = nullptr);

    // Attach this toolbar to a QTextEdit. Must be called before use.
    void attachEditor(QTextEdit* editor);

    // Call this when the cursor position changes in the editor so the
    // toolbar can sync its checked states to the current character format.
    void syncToCurrentFormat();

signals:
    // Emitted when the user picks a text colour via the colour dialog.
    void textColorChanged(const QColor& color);

private slots:
    void onBoldToggled(bool checked);
    void onItalicToggled(bool checked);
    void onUnderlineToggled(bool checked);
    void onFontFamilyChanged(const QString& family);
    void onFontSizeChanged(const QString& sizeStr);
    void onTextColorClicked();
    void onAlignLeft();
    void onAlignCenter();
    void onAlignRight();
    void onAlignJustify();
    void onUndo();
    void onRedo();

private:
    void buildUi();
    void applyStyles();
    QToolButton* makeToolBtn(const QString& text,
                             const QString& tooltip,
                             bool           checkable = false);
    QFrame* makeSeparator();

    // ── child widgets ──────────────────────────────────────────────────────
    QHBoxLayout* m_layout    { nullptr };
    QTextEdit*   m_editor    { nullptr };

    // Undo / Redo
    QToolButton* m_btnUndo   { nullptr };
    QToolButton* m_btnRedo   { nullptr };

    // Font family
    QComboBox*   m_fontCombo { nullptr };

    // Font size
    QComboBox*   m_sizeCombo { nullptr };

    // B / I / U
    QToolButton* m_btnBold      { nullptr };
    QToolButton* m_btnItalic    { nullptr };
    QToolButton* m_btnUnderline { nullptr };

    // Text colour
    QToolButton* m_btnColor  { nullptr };
    QColor       m_textColor { "#2C3140" };   // default: Brand Charcoal

    // Alignment
    QToolButton* m_btnAlignLeft    { nullptr };
    QToolButton* m_btnAlignCenter  { nullptr };
    QToolButton* m_btnAlignRight   { nullptr };
    QToolButton* m_btnAlignJustify { nullptr };

    bool m_syncing { false };   // guard against recursive signal loops
};

} // namespace NativeOffice
