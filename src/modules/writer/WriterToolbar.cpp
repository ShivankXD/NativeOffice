// ─────────────────────────────────────────────────────────────────────────────
// WriterToolbar.cpp  (Sprint 2 → Sprint 10)
// Full formatting toolbar implementation for NativeOffice Writer.
// ─────────────────────────────────────────────────────────────────────────────
#include "WriterToolbar.h"
#include "core/theme/ThemeManager.h"

#include <QTextEdit>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextBlockFormat>
#include <QHBoxLayout>
#include <QFrame>
#include <QFontDatabase>
#include <QColorDialog>
#include <QPainter>
#include <QPixmap>
#include <QIcon>
#include <QFont>
#include <QLabel>
#include <QSizePolicy>

namespace NativeOffice {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────
WriterToolbar::WriterToolbar(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    applyStyles();
    setObjectName("writerToolbar");
    setFixedHeight(48);
}

void WriterToolbar::attachEditor(QTextEdit* editor) {
    if (m_editor == editor) return;
    m_editor = editor;

    // Sync toolbar state whenever the cursor moves
    connect(m_editor, &QTextEdit::cursorPositionChanged,
            this,     &WriterToolbar::syncToCurrentFormat);
    connect(m_editor, &QTextEdit::currentCharFormatChanged,
            this,     [this](const QTextCharFormat&) { syncToCurrentFormat(); });

    // Wire undo / redo availability
    connect(m_editor->document(), &QTextDocument::undoAvailable,
            m_btnUndo, &QToolButton::setEnabled);
    connect(m_editor->document(), &QTextDocument::redoAvailable,
            m_btnRedo, &QToolButton::setEnabled);

    m_btnUndo->setEnabled(false);
    m_btnRedo->setEnabled(false);
}

// ─────────────────────────────────────────────────────────────────────────────
// UI Construction
// ─────────────────────────────────────────────────────────────────────────────
void WriterToolbar::buildUi() {
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(12, 0, 12, 0);
    m_layout->setSpacing(2);

    // ── Undo / Redo ───────────────────────────────────────────────────────
    m_btnUndo = makeToolBtn("↩", "Undo  (Ctrl+Z)");
    m_btnRedo = makeToolBtn("↪", "Redo  (Ctrl+Y)");
    connect(m_btnUndo, &QToolButton::clicked, this, &WriterToolbar::onUndo);
    connect(m_btnRedo, &QToolButton::clicked, this, &WriterToolbar::onRedo);

    // ── Font family ───────────────────────────────────────────────────────
    m_fontCombo = new QComboBox(this);
    m_fontCombo->setObjectName("toolbarCombo");
    m_fontCombo->setToolTip("Font family");
    m_fontCombo->setFixedWidth(168);
    m_fontCombo->setFocusPolicy(Qt::StrongFocus);

    // Populate with system fonts
    const QFontDatabase fdb;
    const auto families = fdb.families();
    for (const QString& f : families) {
        m_fontCombo->addItem(f);
    }
    // Default to "Segoe UI" if present, otherwise first item
    int segoeIdx = m_fontCombo->findText("Segoe UI");
    m_fontCombo->setCurrentIndex(segoeIdx >= 0 ? segoeIdx : 0);

    connect(m_fontCombo, &QComboBox::currentTextChanged,
            this, &WriterToolbar::onFontFamilyChanged);

    // ── Font size ─────────────────────────────────────────────────────────
    m_sizeCombo = new QComboBox(this);
    m_sizeCombo->setObjectName("toolbarCombo");
    m_sizeCombo->setToolTip("Font size");
    m_sizeCombo->setFixedWidth(64);
    m_sizeCombo->setEditable(true);
    m_sizeCombo->setFocusPolicy(Qt::StrongFocus);

    const QList<int> sizes = {8, 9, 10, 11, 12, 14, 16, 18, 20,
                               22, 24, 28, 32, 36, 48, 60, 72};
    for (int s : sizes) m_sizeCombo->addItem(QString::number(s));
    m_sizeCombo->setCurrentText("12");

    connect(m_sizeCombo, &QComboBox::currentTextChanged,
            this, &WriterToolbar::onFontSizeChanged);

    // ── Bold / Italic / Underline ─────────────────────────────────────────
    m_btnBold      = makeToolBtn("B",  "Bold  (Ctrl+B)",      true);
    m_btnItalic    = makeToolBtn("I",  "Italic  (Ctrl+I)",    true);
    m_btnUnderline = makeToolBtn("U",  "Underline  (Ctrl+U)", true);

    // Style B/I/U labels distinctly
    m_btnBold->setFont(QFont("Segoe UI", 11, QFont::Bold));
    QFont italFont("Segoe UI", 11);
    italFont.setItalic(true);
    m_btnItalic->setFont(italFont);
    QFont ulFont("Segoe UI", 11);
    ulFont.setUnderline(true);
    m_btnUnderline->setFont(ulFont);

    connect(m_btnBold,      &QToolButton::toggled, this, &WriterToolbar::onBoldToggled);
    connect(m_btnItalic,    &QToolButton::toggled, this, &WriterToolbar::onItalicToggled);
    connect(m_btnUnderline, &QToolButton::toggled, this, &WriterToolbar::onUnderlineToggled);

    // ── Text colour ───────────────────────────────────────────────────────
    m_btnColor = makeToolBtn("A", "Text colour");
    m_btnColor->setToolTip("Text colour");
    connect(m_btnColor, &QToolButton::clicked, this, &WriterToolbar::onTextColorClicked);

    // ── Alignment ─────────────────────────────────────────────────────────
    m_btnAlignLeft    = makeToolBtn("⬅", "Align left",    true);
    m_btnAlignCenter  = makeToolBtn("☰", "Align center",  true);
    m_btnAlignRight   = makeToolBtn("➡", "Align right",   true);
    m_btnAlignJustify = makeToolBtn("≡", "Justify",       true);

    connect(m_btnAlignLeft,    &QToolButton::clicked, this, &WriterToolbar::onAlignLeft);
    connect(m_btnAlignCenter,  &QToolButton::clicked, this, &WriterToolbar::onAlignCenter);
    connect(m_btnAlignRight,   &QToolButton::clicked, this, &WriterToolbar::onAlignRight);
    connect(m_btnAlignJustify, &QToolButton::clicked, this, &WriterToolbar::onAlignJustify);

    // ── Insert Image (Sprint 10) ────────────────────────────────────────────
    m_btnInsertImage = makeToolBtn(QStringLiteral("\U0001F5BC"), "Insert Image");
    connect(m_btnInsertImage, &QToolButton::clicked,
            this, &WriterToolbar::insertImageRequested);

    // ── Assemble ──────────────────────────────────────────────────────────
    m_layout->addWidget(m_btnUndo);
    m_layout->addWidget(m_btnRedo);
    m_layout->addWidget(makeSeparator());
    m_layout->addWidget(m_fontCombo);
    m_layout->addSpacing(4);
    m_layout->addWidget(m_sizeCombo);
    m_layout->addWidget(makeSeparator());
    m_layout->addWidget(m_btnBold);
    m_layout->addWidget(m_btnItalic);
    m_layout->addWidget(m_btnUnderline);
    m_layout->addSpacing(4);
    m_layout->addWidget(m_btnColor);
    m_layout->addWidget(makeSeparator());
    m_layout->addWidget(m_btnAlignLeft);
    m_layout->addWidget(m_btnAlignCenter);
    m_layout->addWidget(m_btnAlignRight);
    m_layout->addWidget(m_btnAlignJustify);
    m_layout->addWidget(makeSeparator());
    m_layout->addWidget(m_btnInsertImage);
    m_layout->addStretch();
}


QToolButton* WriterToolbar::makeToolBtn(const QString& text,
                                         const QString& tooltip,
                                         bool           checkable) {
    auto* btn = new QToolButton(this);
    btn->setText(text);
    btn->setToolTip(tooltip);
    btn->setCheckable(checkable);
    btn->setFixedSize(34, 34);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setObjectName("toolbarBtn");
    return btn;
}

QFrame* WriterToolbar::makeSeparator() {
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedWidth(1);
    sep->setFixedHeight(28);
    sep->setObjectName("toolbarSep");
    return sep;
}

// ─────────────────────────────────────────────────────────────────────────────
// Styling
// ─────────────────────────────────────────────────────────────────────────────
void WriterToolbar::applyStyles() {
    const auto& t = ThemeManager::instance().theme();

    setStyleSheet(QString(R"(
/* ── Toolbar container ───────────────────────────────────────────── */
QWidget#writerToolbar {
    background-color: %1;
    border-bottom: 1px solid rgba(255,255,255,0.08);
}

/* ── Generic tool buttons ────────────────────────────────────────── */
QToolButton#toolbarBtn {
    background-color: transparent;
    color: rgba(255,255,255,0.80);
    border: none;
    border-radius: 6px;
    font-size: 13px;
    font-weight: 500;
    font-family: "Segoe UI", "Inter", sans-serif;
}
QToolButton#toolbarBtn:hover {
    background-color: rgba(255,255,255,0.10);
    color: #FFFFFF;
}
QToolButton#toolbarBtn:checked {
    background-color: %2;
    color: #FFFFFF;
}
QToolButton#toolbarBtn:pressed {
    background-color: rgba(232,55,42,0.80);
    color: #FFFFFF;
}
QToolButton#toolbarBtn:disabled {
    color: rgba(255,255,255,0.25);
}

/* ── Combo boxes ─────────────────────────────────────────────────── */
QComboBox#toolbarCombo {
    background-color: rgba(255,255,255,0.08);
    color: rgba(255,255,255,0.85);
    border: 1px solid rgba(255,255,255,0.15);
    border-radius: 6px;
    padding: 4px 8px;
    font-size: 12px;
    font-family: "Segoe UI", "Inter", sans-serif;
    selection-background-color: %2;
}
QComboBox#toolbarCombo:hover {
    border-color: rgba(255,255,255,0.35);
    background-color: rgba(255,255,255,0.12);
}
QComboBox#toolbarCombo:focus {
    border-color: %2;
}
QComboBox#toolbarCombo::drop-down {
    border: none;
    width: 18px;
}
QComboBox#toolbarCombo::down-arrow {
    image: none;
    border-left:  4px solid transparent;
    border-right: 4px solid transparent;
    border-top:   5px solid rgba(255,255,255,0.55);
    width: 0;
    height: 0;
}
QComboBox QAbstractItemView {
    background-color: %3;
    color: #FFFFFF;
    border: 1px solid rgba(255,255,255,0.12);
    border-radius: 6px;
    selection-background-color: %2;
    outline: none;
    padding: 4px;
}
QComboBox QAbstractItemView::item {
    height: 26px;
    padding-left: 8px;
    border-radius: 4px;
}

/* ── Separators ──────────────────────────────────────────────────── */
QFrame#toolbarSep {
    background-color: rgba(255,255,255,0.12);
    border: none;
    margin: 0 4px;
}
)")
    .arg(ThemeManager::cssColor(t.primary))    // %1 toolbar bg
    .arg(ThemeManager::cssColor(t.secondary))  // %2 scarlet active
    .arg(ThemeManager::cssColor(t.accent))     // %3 dropdown bg
    );
}

// ─────────────────────────────────────────────────────────────────────────────
// Format synchronisation  (cursor → toolbar state)
// ─────────────────────────────────────────────────────────────────────────────
void WriterToolbar::syncToCurrentFormat() {
    if (!m_editor || m_syncing) return;
    m_syncing = true;

    const QTextCharFormat fmt = m_editor->currentCharFormat();

    m_btnBold->setChecked(fmt.fontWeight() >= QFont::Bold);
    m_btnItalic->setChecked(fmt.fontItalic());
    m_btnUnderline->setChecked(fmt.fontUnderline());

    // Font family
    const QString family = fmt.fontFamilies().toStringList().value(0);
    if (!family.isEmpty()) {
        int idx = m_fontCombo->findText(family);
        if (idx >= 0) m_fontCombo->setCurrentIndex(idx);
    }

    // Font size
    const qreal ptSize = fmt.fontPointSize();
    if (ptSize > 0)
        m_sizeCombo->setCurrentText(QString::number(static_cast<int>(ptSize)));

    // Text colour swatch on the "A" button
    m_textColor = fmt.foreground().color().isValid()
                      ? fmt.foreground().color()
                      : QColor("#2C3140");

    // Alignment
    const Qt::Alignment align = m_editor->alignment();
    m_btnAlignLeft   ->setChecked(align & Qt::AlignLeft);
    m_btnAlignCenter ->setChecked(align & Qt::AlignHCenter);
    m_btnAlignRight  ->setChecked(align & Qt::AlignRight);
    m_btnAlignJustify->setChecked(align & Qt::AlignJustify);

    m_syncing = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Slots — formatting actions
// ─────────────────────────────────────────────────────────────────────────────
void WriterToolbar::onBoldToggled(bool checked) {
    if (!m_editor || m_syncing) return;
    QTextCharFormat fmt;
    fmt.setFontWeight(checked ? QFont::Bold : QFont::Normal);
    m_editor->mergeCurrentCharFormat(fmt);
    m_editor->setFocus();
}

void WriterToolbar::onItalicToggled(bool checked) {
    if (!m_editor || m_syncing) return;
    QTextCharFormat fmt;
    fmt.setFontItalic(checked);
    m_editor->mergeCurrentCharFormat(fmt);
    m_editor->setFocus();
}

void WriterToolbar::onUnderlineToggled(bool checked) {
    if (!m_editor || m_syncing) return;
    QTextCharFormat fmt;
    fmt.setFontUnderline(checked);
    m_editor->mergeCurrentCharFormat(fmt);
    m_editor->setFocus();
}

void WriterToolbar::onFontFamilyChanged(const QString& family) {
    if (!m_editor || m_syncing) return;
    QTextCharFormat fmt;
    fmt.setFontFamilies({family});
    m_editor->mergeCurrentCharFormat(fmt);
    m_editor->setFocus();
}

void WriterToolbar::onFontSizeChanged(const QString& sizeStr) {
    if (!m_editor || m_syncing) return;
    bool ok = false;
    const int pt = sizeStr.toInt(&ok);
    if (!ok || pt <= 0) return;
    QTextCharFormat fmt;
    fmt.setFontPointSize(pt);
    m_editor->mergeCurrentCharFormat(fmt);
    m_editor->setFocus();
}

void WriterToolbar::onTextColorClicked() {
    if (!m_editor) return;

    const QColor chosen = QColorDialog::getColor(
        m_textColor, this, "Choose Text Colour",
        QColorDialog::ShowAlphaChannel);

    if (!chosen.isValid()) return;

    m_textColor = chosen;
    emit textColorChanged(chosen);

    QTextCharFormat fmt;
    fmt.setForeground(chosen);
    m_editor->mergeCurrentCharFormat(fmt);
    m_editor->setFocus();
}

void WriterToolbar::onAlignLeft() {
    if (!m_editor) return;
    m_editor->setAlignment(Qt::AlignLeft | Qt::AlignAbsolute);
}

void WriterToolbar::onAlignCenter() {
    if (!m_editor) return;
    m_editor->setAlignment(Qt::AlignHCenter);
}

void WriterToolbar::onAlignRight() {
    if (!m_editor) return;
    m_editor->setAlignment(Qt::AlignRight | Qt::AlignAbsolute);
}

void WriterToolbar::onAlignJustify() {
    if (!m_editor) return;
    m_editor->setAlignment(Qt::AlignJustify);
}

void WriterToolbar::onUndo() {
    if (m_editor) m_editor->undo();
}

void WriterToolbar::onRedo() {
    if (m_editor) m_editor->redo();
}

} // namespace NativeOffice
