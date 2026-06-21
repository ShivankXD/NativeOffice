// ─────────────────────────────────────────────────────────────────────────────
// ImpressRibbon.cpp  (Sprint 12 → Sprint 13: WPS-style grouped ribbon)
// ─────────────────────────────────────────────────────────────────────────────
#include "ImpressRibbon.h"
#include "ImpressModule.h"   // ImpressViewMode

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QToolButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QStackedWidget>
#include <QMenu>
#include <QWidgetAction>
#include <QPainterPath>
#include <QDialog>
#include <QSpinBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QFrame>
#include <QFont>
#include <QFontDatabase>
#include <QColorDialog>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QLinearGradient>
#include <QIcon>
#include <QDir>
#include <QPolygonF>
#include <functional>

namespace NativeOffice {

namespace {

// Monochrome icon tint for the light ribbon (dark slate, à la PowerPoint/WPS).
const QColor kIconColor("#3A3F4B");

// Paint a crisp monochrome icon so we never depend on a glyph being
// present in the system font. Drawn on a 40×40 canvas, displayed at 20×20.
QIcon paintIcon(const std::function<void(QPainter&)>& draw) {
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(kIconColor);
    pen.setWidthF(2.4);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    draw(p);
    p.end();
    return QIcon(pm);
}

QIcon newSlideIcon() {
    return paintIcon([](QPainter& p) {
        p.drawRoundedRect(QRectF(7, 9, 20, 15), 2, 2);     // slide
        p.drawLine(QPointF(30, 22), QPointF(30, 34));       // plus (vertical)
        p.drawLine(QPointF(24, 28), QPointF(36, 28));       // plus (horizontal)
    });
}

QIcon duplicateIcon() {
    return paintIcon([](QPainter& p) {
        p.drawRoundedRect(QRectF(8, 8, 18, 18), 2, 2);
        p.drawRoundedRect(QRectF(16, 16, 18, 18), 2, 2);
    });
}

QIcon deleteIcon() {
    return paintIcon([](QPainter& p) {
        p.drawLine(QPointF(9, 13), QPointF(31, 13));        // lid
        p.drawLine(QPointF(16, 13), QPointF(16, 10));       // handle
        p.drawLine(QPointF(24, 13), QPointF(24, 10));
        p.drawLine(QPointF(16, 10), QPointF(24, 10));
        p.drawLine(QPointF(12, 13), QPointF(14, 33));       // body
        p.drawLine(QPointF(28, 13), QPointF(26, 33));
        p.drawLine(QPointF(14, 33), QPointF(26, 33));
        p.drawLine(QPointF(20, 16), QPointF(20, 30));       // center stripe
    });
}

QIcon bulletIcon(bool numbered) {
    return paintIcon([numbered](QPainter& p) {
        QPen pen(kIconColor);
        pen.setWidthF(2.2);
        pen.setCapStyle(Qt::RoundCap);
        const int ys[3] = { 12, 21, 30 };
        for (int i = 0; i < 3; ++i) {
            // text lines
            p.setPen(pen);
            p.drawLine(QPointF(17, ys[i]), QPointF(33, ys[i]));
            // marker
            if (numbered) {
                QFont f("Segoe UI", 7, QFont::Bold);
                p.setFont(f);
                p.drawText(QRectF(5, ys[i] - 7, 11, 14),
                           Qt::AlignRight | Qt::AlignVCenter,
                           QString::number(i + 1));
            } else {
                p.setBrush(kIconColor);
                p.setPen(Qt::NoPen);
                p.drawEllipse(QPointF(9, ys[i]), 2.2, 2.2);
            }
        }
    });
}

QIcon indentIcon(bool increase) {
    return paintIcon([increase](QPainter& p) {
        QPen pen(kIconColor);
        pen.setWidthF(2.2);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        const int ys[4] = { 11, 18, 25, 32 };
        for (int i = 0; i < 4; ++i) {
            const int x0 = (i == 1 || i == 2) ? 18 : 8;
            p.drawLine(QPointF(x0, ys[i]), QPointF(33, ys[i]));
        }
        // arrow between the indented lines
        p.setBrush(kIconColor);
        p.setPen(Qt::NoPen);
        QPolygonF tri;
        if (increase)
            tri << QPointF(8, 17) << QPointF(8, 26) << QPointF(14, 21.5);
        else
            tri << QPointF(14, 17) << QPointF(14, 26) << QPointF(8, 21.5);
        p.drawPolygon(tri);
    });
}

// Paint a unicode/emoji glyph as an icon. Geometric glyphs pick up the
// monochrome ribbon tint; colour-emoji glyphs render in their own colours.
QIcon glyphIcon(const QString& glyph, int px = 20) {
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont f("Segoe UI Emoji");
    f.setPixelSize(px);
    p.setFont(f);
    p.setPen(kIconColor);
    p.drawText(QRectF(0, 0, 40, 40), Qt::AlignCenter, glyph);
    p.end();
    return QIcon(pm);
}

QIcon tableIcon() {
    return paintIcon([](QPainter& p) {
        p.drawRect(QRectF(7, 9, 26, 22));
        p.drawLine(QPointF(7, 17),  QPointF(33, 17));
        p.drawLine(QPointF(7, 25),  QPointF(33, 25));
        p.drawLine(QPointF(16, 9),  QPointF(16, 31));
        p.drawLine(QPointF(25, 9),  QPointF(25, 31));
    });
}

// Paint a gallery shape into an icon, reusing the scene's own path geometry so
// the ribbon preview matches exactly what gets placed on the slide.
QIcon shapeKindIcon(ShapeKind k) {
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(kIconColor);
    pen.setWidthF(2.2);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(k == ShapeKind::Line ? QBrush(Qt::NoBrush) : QBrush(QColor(58, 63, 75, 45)));
    p.drawPath(SlideScene::shapePath(k, QRectF(8, 9, 24, 22)));
    p.end();
    return QIcon(pm);
}

// A small thumbnail for Design "theme" tiles: a vertical top→bottom gradient
// so the tile previews exactly what the two-colour theme applies to the slide.
QIcon swatchThumb(const QColor& top, const QColor& bottom) {
    QPixmap pm(80, 56);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(0, 0, 0, 56);
    g.setColorAt(0.0, top);
    g.setColorAt(1.0, bottom);
    p.fillRect(pm.rect(), g);
    p.setPen(QPen(QColor(0, 0, 0, 40)));
    p.drawRect(0, 0, 79, 55);
    p.end();
    return QIcon(pm);
}

// mode: 0 = left, 1 = center, 2 = right, 3 = justify
QIcon alignIcon(int mode) {
    return paintIcon([mode](QPainter& p) {
        QPen pen(kIconColor);
        pen.setWidthF(2.2);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        const int ys[4]     = { 11, 18, 25, 32 };
        const int widths[4] = { 26, 16, 22, 14 };
        for (int i = 0; i < 4; ++i) {
            const int w = (mode == 3) ? 26 : widths[i];
            int x0, x1;
            if      (mode == 0) { x0 = 7;       x1 = 7 + w; }
            else if (mode == 2) { x1 = 33;      x0 = 33 - w; }
            else if (mode == 1) { x0 = 20 - w/2; x1 = 20 + w/2; }
            else                { x0 = 7;       x1 = 33; }
            p.drawLine(QPointF(x0, ys[i]), QPointF(x1, ys[i]));
        }
    });
}

} // namespace

ImpressRibbon::ImpressRibbon(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("impressRibbon");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Tab strip ───────────────────────────────────────────────────────
    auto* tabRow = new QWidget(this);
    tabRow->setObjectName("ribbonTabRow");
    tabRow->setFixedHeight(32);
    auto* tabLayout = new QHBoxLayout(tabRow);
    tabLayout->setContentsMargins(10, 0, 10, 0);
    tabLayout->setSpacing(2);

    m_tabGroup = new QButtonGroup(this);
    m_tabGroup->setExclusive(true);

    const QStringList tabNames = {
        "Home", "Insert", "Design", "Transitions",
        "Animations", "Slide Show", "Review", "View"
    };
    for (int i = 0; i < tabNames.size(); ++i) {
        auto* btn = makeTabButton(tabNames[i]);
        m_tabGroup->addButton(btn, i);
        tabLayout->addWidget(btn);
    }
    tabLayout->addStretch();
    m_tabGroup->button(0)->setChecked(true);

    // ── Stacked content ─────────────────────────────────────────────────
    m_stack = new QStackedWidget(this);
    m_stack->setObjectName("ribbonStack");
    m_stack->setFixedHeight(92);
    m_stack->addWidget(buildHomeTab());
    m_stack->addWidget(buildInsertTab());
    m_stack->addWidget(buildDesignTab());
    m_stack->addWidget(buildTransitionsTab());
    m_stack->addWidget(buildAnimationsTab());
    m_stack->addWidget(buildSlideShowTab());
    m_stack->addWidget(buildReviewTab());
    m_stack->addWidget(buildViewTab());

    connect(m_tabGroup, &QButtonGroup::idClicked, m_stack, &QStackedWidget::setCurrentIndex);

    root->addWidget(tabRow);
    root->addWidget(m_stack);

    applyStyles();
}

// ─────────────────────────────────────────────────────────────────────────────
// Home tab — Slides · Font · Paragraph groups
// ─────────────────────────────────────────────────────────────────────────────
QWidget* ImpressRibbon::buildHomeTab() {
    auto* tab = new QWidget(this);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2);
    layout->setSpacing(0);

    // ── Quick access: undo / redo ───────────────────────────────────────
    m_btnUndo = makeToolBtn("↶", "Undo  (Ctrl+Z)");
    m_btnRedo = makeToolBtn("↷", "Redo  (Ctrl+Y)");
    m_btnUndo->setEnabled(false);
    m_btnRedo->setEnabled(false);
    connect(m_btnUndo, &QToolButton::clicked, this, &ImpressRibbon::undoRequested);
    connect(m_btnRedo, &QToolButton::clicked, this, &ImpressRibbon::redoRequested);
    layout->addWidget(makeGroup("Undo", { m_btnUndo, m_btnRedo }));
    layout->addWidget(makeSeparator());

    // ── Slides group ────────────────────────────────────────────────────
    auto* btnNew = makeCmdBtn("New Slide", "Add a new slide", 104);
    btnNew->setIcon(newSlideIcon());
    btnNew->setIconSize(QSize(20, 20));
    btnNew->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(btnNew, &QToolButton::clicked, this, &ImpressRibbon::newSlideRequested);

    auto* layoutCombo = new QComboBox(tab);
    layoutCombo->setObjectName("ribbonCombo");
    layoutCombo->addItem("Title Slide", static_cast<int>(SlideLayout::Title));
    layoutCombo->addItem("Title + Content", static_cast<int>(SlideLayout::TitleContent));
    layoutCombo->addItem("Blank", static_cast<int>(SlideLayout::Blank));
    layoutCombo->setFixedWidth(126);
    layoutCombo->setToolTip("Slide layout");
    connect(layoutCombo, &QComboBox::activated, this, [this, layoutCombo](int idx) {
        emit layoutSelected(static_cast<SlideLayout>(layoutCombo->itemData(idx).toInt()));
    });

    auto* btnDup = makeIconBtn(duplicateIcon(), "Duplicate this slide");
    auto* btnDel = makeIconBtn(deleteIcon(),    "Delete this slide");
    connect(btnDup, &QToolButton::clicked, this, &ImpressRibbon::duplicateSlideRequested);
    connect(btnDel, &QToolButton::clicked, this, &ImpressRibbon::deleteSlideRequested);
    layout->addWidget(makeGroup("Slides", { btnNew, layoutCombo, btnDup, btnDel }));
    layout->addWidget(makeSeparator());

    // ── Font group ──────────────────────────────────────────────────────
    m_fontCombo = new QComboBox(tab);
    m_fontCombo->setObjectName("ribbonCombo");
    m_fontCombo->setFixedWidth(140);
    m_fontCombo->setToolTip("Font family");
    const QFontDatabase fdb;
    for (const QString& f : fdb.families()) m_fontCombo->addItem(f);
    const int segoeIdx = m_fontCombo->findText("Segoe UI");
    m_fontCombo->setCurrentIndex(segoeIdx >= 0 ? segoeIdx : 0);
    connect(m_fontCombo, &QComboBox::currentTextChanged, this, [this](const QString& f) {
        if (!m_syncing) emit fontFamilyChanged(f);
    });

    m_sizeCombo = new QComboBox(tab);
    m_sizeCombo->setObjectName("ribbonCombo");
    m_sizeCombo->setEditable(true);
    m_sizeCombo->setFixedWidth(54);
    m_sizeCombo->setToolTip("Font size");
    for (int s : {8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 36, 40, 48, 60, 72})
        m_sizeCombo->addItem(QString::number(s));
    m_sizeCombo->setCurrentText("18");
    connect(m_sizeCombo, &QComboBox::currentTextChanged, this, [this](const QString& s) {
        if (m_syncing) return;
        bool ok = false; const int pt = s.toInt(&ok);
        if (ok && pt > 0) emit fontSizeChanged(pt);
    });

    m_btnBold      = makeToolBtn("B", "Bold  (Ctrl+B)", true);
    m_btnItalic    = makeToolBtn("I", "Italic  (Ctrl+I)", true);
    m_btnUnderline = makeToolBtn("U", "Underline  (Ctrl+U)", true);
    m_btnStrike    = makeToolBtn("S", "Strikethrough", true);
    m_btnBold->setFont(QFont("Segoe UI", 11, QFont::Bold));
    QFont it("Segoe UI", 11); it.setItalic(true); m_btnItalic->setFont(it);
    QFont ul("Segoe UI", 11); ul.setUnderline(true); m_btnUnderline->setFont(ul);
    QFont sk("Segoe UI", 11); sk.setStrikeOut(true); m_btnStrike->setFont(sk);
    connect(m_btnBold,      &QToolButton::toggled, this, [this](bool c){ if (!m_syncing) emit boldToggled(c); });
    connect(m_btnItalic,    &QToolButton::toggled, this, [this](bool c){ if (!m_syncing) emit italicToggled(c); });
    connect(m_btnUnderline, &QToolButton::toggled, this, [this](bool c){ if (!m_syncing) emit underlineToggled(c); });
    connect(m_btnStrike,    &QToolButton::toggled, this, [this](bool c){ if (!m_syncing) emit strikeToggled(c); });

    m_btnColor = makeToolBtn("A", "Font colour");
    connect(m_btnColor, &QToolButton::clicked, this, [this]() {
        const QColor c = QColorDialog::getColor(m_textColor, this, "Font Colour");
        if (c.isValid()) { m_textColor = c; emit textColorChanged(c); }
    });

    layout->addWidget(makeGroup("Font", {
        m_fontCombo, m_sizeCombo, m_btnBold, m_btnItalic,
        m_btnUnderline, m_btnStrike, m_btnColor }));
    layout->addWidget(makeSeparator());

    // ── Paragraph group ─────────────────────────────────────────────────
    m_btnAlignLeft    = makeIconBtn(alignIcon(0), "Align left", true);
    m_btnAlignCenter  = makeIconBtn(alignIcon(1), "Align center", true);
    m_btnAlignRight   = makeIconBtn(alignIcon(2), "Align right", true);
    m_btnAlignJustify = makeIconBtn(alignIcon(3), "Justify", true);
    connect(m_btnAlignLeft,    &QToolButton::clicked, this, [this]{ emit alignChanged(Qt::AlignLeft); });
    connect(m_btnAlignCenter,  &QToolButton::clicked, this, [this]{ emit alignChanged(Qt::AlignHCenter); });
    connect(m_btnAlignRight,   &QToolButton::clicked, this, [this]{ emit alignChanged(Qt::AlignRight); });
    connect(m_btnAlignJustify, &QToolButton::clicked, this, [this]{ emit alignChanged(Qt::AlignJustify); });

    auto* btnBullets = makeIconBtn(bulletIcon(false), "Bullet list");
    auto* btnNumbers = makeIconBtn(bulletIcon(true),  "Numbered list");
    connect(btnBullets, &QToolButton::clicked, this, &ImpressRibbon::bulletsRequested);
    connect(btnNumbers, &QToolButton::clicked, this, &ImpressRibbon::numberingRequested);

    auto* btnIndentDec = makeIconBtn(indentIcon(false), "Decrease indent");
    auto* btnIndentInc = makeIconBtn(indentIcon(true),  "Increase indent");
    connect(btnIndentInc, &QToolButton::clicked, this, [this]{ emit indentRequested(+1); });
    connect(btnIndentDec, &QToolButton::clicked, this, [this]{ emit indentRequested(-1); });

    auto* spacingCombo = new QComboBox(tab);
    spacingCombo->setObjectName("ribbonCombo");
    spacingCombo->setFixedWidth(64);
    spacingCombo->setToolTip("Line spacing");
    for (double v : {1.0, 1.15, 1.5, 2.0}) spacingCombo->addItem(QString::number(v) + "×", v);
    connect(spacingCombo, &QComboBox::activated, this, [this, spacingCombo](int idx) {
        emit lineSpacingChanged(spacingCombo->itemData(idx).toDouble());
    });

    layout->addWidget(makeGroup("Paragraph", {
        m_btnAlignLeft, m_btnAlignCenter, m_btnAlignRight, m_btnAlignJustify,
        btnBullets, btnNumbers, btnIndentDec, btnIndentInc, spacingCombo }));

    layout->addStretch();
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// Insert tab
// ─────────────────────────────────────────────────────────────────────────────
QWidget* ImpressRibbon::buildInsertTab() {
    auto* tab = new QWidget(this);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2);
    layout->setSpacing(0);

    // ── Slides ──────────────────────────────────────────────────────────
    auto* btnNew = makeBigBtn(newSlideIcon(), "New\nSlide", "Add a new slide");
    auto* btnDup = makeBigBtn(duplicateIcon(), "Duplicate", "Duplicate this slide");
    connect(btnNew, &QToolButton::clicked, this, &ImpressRibbon::newSlideRequested);
    connect(btnDup, &QToolButton::clicked, this, &ImpressRibbon::duplicateSlideRequested);
    layout->addWidget(makeGroup("Slides", { btnNew, btnDup }));
    layout->addWidget(makeSeparator());

    // ── Images ──────────────────────────────────────────────────────────
    auto* btnImage = makeBigBtn(glyphIcon(QStringLiteral("\U0001F5BC")), "Pictures",
                                "Insert an image from a file");
    connect(btnImage, &QToolButton::clicked, this, &ImpressRibbon::insertImageRequested);
    layout->addWidget(makeGroup("Images", { btnImage }));
    layout->addWidget(makeSeparator());

    // ── Shapes (interactive insert modes) ───────────────────────────────
    m_shapeGroup = new QButtonGroup(this);
    m_shapeGroup->setExclusive(true);
    auto* btnText    = makeBigBtn(glyphIcon("T", 22),       "Text\nBox",  "Insert a text box", true);
    auto* btnRect    = makeBigBtn(glyphIcon(QString::fromUtf8("▭"), 24), "Rectangle", "Insert a rectangle", true);
    auto* btnEllipse = makeBigBtn(glyphIcon(QString::fromUtf8("◯"), 22), "Ellipse",   "Insert an ellipse",   true);
    m_shapeGroup->addButton(btnText,    static_cast<int>(InsertMode::TextBox));
    m_shapeGroup->addButton(btnRect,    static_cast<int>(InsertMode::Rectangle));
    m_shapeGroup->addButton(btnEllipse, static_cast<int>(InsertMode::Ellipse));
    connect(m_shapeGroup, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
        if (checked) emit insertModeChanged(static_cast<InsertMode>(id));
    });

    // ── Shapes gallery (drop-down grid of preset shapes) ────────────────
    auto* btnGallery = makeBigBtn(shapeKindIcon(ShapeKind::Star5), "More\nShapes",
                                  "Choose from the shape gallery");
    btnGallery->setPopupMode(QToolButton::InstantPopup);
    auto* shapeMenu = new QMenu(btnGallery);
    auto* galleryGrid = new QWidget(shapeMenu);
    auto* gl = new QGridLayout(galleryGrid);
    gl->setContentsMargins(8, 8, 8, 8);
    gl->setSpacing(4);
    struct GShape { ShapeKind kind; const char* tip; };
    const GShape gallery[] = {
        { ShapeKind::Rectangle,     "Rectangle" },
        { ShapeKind::RoundedRect,   "Rounded rectangle" },
        { ShapeKind::Ellipse,       "Ellipse" },
        { ShapeKind::Triangle,      "Triangle" },
        { ShapeKind::RightTriangle, "Right triangle" },
        { ShapeKind::Diamond,       "Diamond" },
        { ShapeKind::Pentagon,      "Pentagon" },
        { ShapeKind::Hexagon,       "Hexagon" },
        { ShapeKind::Star5,         "5-point star" },
        { ShapeKind::Arrow,         "Block arrow" },
        { ShapeKind::Chevron,       "Chevron" },
        { ShapeKind::Line,          "Line" },
        { ShapeKind::Cloud,         "Cloud" },
        { ShapeKind::Heart,         "Heart" },
    };
    int gi = 0;
    for (const auto& gs : gallery) {
        auto* tile = new QToolButton(galleryGrid);
        tile->setObjectName("ribbonToolBtn");
        tile->setIcon(shapeKindIcon(gs.kind));
        tile->setIconSize(QSize(26, 26));
        tile->setFixedSize(40, 40);
        tile->setToolTip(gs.tip);
        tile->setCursor(Qt::PointingHandCursor);
        const ShapeKind kind = gs.kind;
        connect(tile, &QToolButton::clicked, this, [this, shapeMenu, kind] {
            emit shapeInsertRequested(kind);
            shapeMenu->hide();
        });
        gl->addWidget(tile, gi / 5, gi % 5);
        ++gi;
    }
    auto* wa = new QWidgetAction(shapeMenu);
    wa->setDefaultWidget(galleryGrid);
    shapeMenu->addAction(wa);
    btnGallery->setMenu(shapeMenu);

    layout->addWidget(makeGroup("Shapes", { btnText, btnRect, btnEllipse, btnGallery }));
    layout->addWidget(makeSeparator());

    // ── Shape Style (applies to the selected shape) ─────────────────────
    auto* btnFill = makeBigBtn(glyphIcon(QString::fromUtf8("\xF0\x9F\x96\x8C"), 22), "Fill",
                               "Fill colour of the selected shape");
    auto* btnOutline = makeBigBtn(glyphIcon(QString::fromUtf8("\xE2\x97\xAF"), 22), "Outline",
                                  "Outline colour of the selected shape");
    auto* btnShadow = makeBigBtn(glyphIcon(QString::fromUtf8("\xE2\x97\x90"), 22), "Shadow",
                                 "Toggle a drop shadow on the selected object");
    connect(btnFill, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(QColor("#E8372A"), this, "Shape Fill",
                                                QColorDialog::ShowAlphaChannel);
        if (c.isValid()) emit shapeFillRequested(c);
    });
    connect(btnOutline, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(QColor("#2C3140"), this, "Shape Outline");
        if (c.isValid()) emit shapeOutlineRequested(c);
    });
    connect(btnShadow, &QToolButton::clicked, this, &ImpressRibbon::shadowToggleRequested);
    layout->addWidget(makeGroup("Shape Style", { btnFill, btnOutline, btnShadow }));
    layout->addWidget(makeSeparator());

    // ── SmartArt (diagram templates) ────────────────────────────────────
    auto* btnSmart = makeBigBtn(glyphIcon(QString::fromUtf8("\xE2\x9A\x9E"), 22), "Smart\nArt",
                                "Insert a diagram");
    btnSmart->setPopupMode(QToolButton::InstantPopup);
    auto* smartMenu = new QMenu(btnSmart);
    struct SA { const char* label; SmartArtKind kind; };
    const SA sas[] = {
        { "Process Flow", SmartArtKind::ProcessFlow },
        { "Cycle",        SmartArtKind::Cycle },
        { "Hierarchy",    SmartArtKind::Hierarchy },
        { "Pyramid",      SmartArtKind::Pyramid },
        { "Bullet List",  SmartArtKind::BulletList },
        { "Venn",         SmartArtKind::Venn },
    };
    for (const auto& sa : sas) {
        const SmartArtKind kind = sa.kind;
        smartMenu->addAction(sa.label, this, [this, kind] { emit smartArtRequested(kind); });
    }
    btnSmart->setMenu(smartMenu);
    layout->addWidget(makeGroup("SmartArt", { btnSmart }));
    layout->addWidget(makeSeparator());

    // ── Tables ──────────────────────────────────────────────────────────
    auto* btnTable = makeBigBtn(tableIcon(), "Table", "Insert a table");
    connect(btnTable, &QToolButton::clicked, this, [this] {
        QDialog dlg(this);
        dlg.setWindowTitle("Insert Table");
        auto* form = new QFormLayout(&dlg);
        auto* rowsSpin = new QSpinBox(&dlg);
        rowsSpin->setRange(1, 20); rowsSpin->setValue(3);
        auto* colsSpin = new QSpinBox(&dlg);
        colsSpin->setRange(1, 12); colsSpin->setValue(3);
        form->addRow("Rows:", rowsSpin);
        form->addRow("Columns:", colsSpin);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() == QDialog::Accepted)
            emit insertTableRequested(rowsSpin->value(), colsSpin->value());
    });
    layout->addWidget(makeGroup("Tables", { btnTable }));
    layout->addWidget(makeSeparator());

    // ── Text ────────────────────────────────────────────────────────────
    auto* btnWordArt = makeBigBtn(glyphIcon("A", 24),       "WordArt",   "Insert decorative WordArt text");
    auto* btnSymbol  = makeBigBtn(glyphIcon(QString::fromUtf8("Ω"), 22), "Symbol",  "Insert a symbol");
    auto* btnNumber  = makeBigBtn(glyphIcon("#", 22),       "Slide\nNumber", "Insert the slide number");
    auto* btnDate    = makeBigBtn(glyphIcon(QStringLiteral("\U0001F4C5")), "Date &\nTime", "Insert today's date");
    connect(btnWordArt, &QToolButton::clicked, this, &ImpressRibbon::wordArtRequested);
    connect(btnSymbol,  &QToolButton::clicked, this, [this]{ emit symbolRequested(QString::fromUtf8("★")); });
    connect(btnNumber,  &QToolButton::clicked, this, &ImpressRibbon::slideNumberRequested);
    connect(btnDate,    &QToolButton::clicked, this, &ImpressRibbon::dateTimeRequested);
    layout->addWidget(makeGroup("Text", { btnWordArt, btnSymbol, btnNumber, btnDate }));
    layout->addWidget(makeSeparator());

    // ── Comments ────────────────────────────────────────────────────────
    auto* btnComment = makeBigBtn(glyphIcon(QStringLiteral("\U0001F4AC")), "Notes",
                                  "Show or hide the speaker notes panel");
    connect(btnComment, &QToolButton::clicked, this, &ImpressRibbon::notesToggleRequested);
    layout->addWidget(makeGroup("Comments", { btnComment }));

    layout->addStretch();
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// Design tab — quick background colour swatches
// ─────────────────────────────────────────────────────────────────────────────
QWidget* ImpressRibbon::buildDesignTab() {
    auto* tab = new QWidget(this);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2);
    layout->setSpacing(0);

    // ── Themes (apply a two-colour vertical gradient background) ────────
    struct Theme { QColor top; QColor bottom; const char* name; };
    const Theme themes[] = {
        { "#FFFFFF", "#DBEAFE", "Office" },   // white → light blue
        { "#FFFFFF", "#DCFCE7", "Mint"   },   // white → light green
        { "#E8372A", "#1A1F2E", "Ember"  },   // scarlet → near-black
        { "#3B4252", "#1A1F2E", "Slate"  },   // slate → near-black
        { "#FFF7ED", "#FDBA74", "Amber"  },   // cream → amber
        { "#EFF6FF", "#2563EB", "Sky"    },   // pale → blue
    };
    auto* themeRow = new QWidget(tab);
    auto* trl = new QHBoxLayout(themeRow);
    trl->setContentsMargins(0, 0, 0, 0);
    trl->setSpacing(6);
    for (const auto& th : themes) {
        auto* btn = new QToolButton(themeRow);
        btn->setObjectName("ribbonThemeBtn");
        btn->setIcon(swatchThumb(th.top, th.bottom));
        btn->setIconSize(QSize(56, 38));
        btn->setFixedSize(64, 50);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(QString("%1 theme").arg(th.name));
        const QColor top = th.top, bottom = th.bottom;
        connect(btn, &QToolButton::clicked, this, [this, top, bottom]{
            emit designThemeSelected(top, bottom);
        });
        trl->addWidget(btn);
    }
    layout->addWidget(makeGroup("Themes", { themeRow }));
    layout->addWidget(makeSeparator());

    // ── Background swatches + custom ────────────────────────────────────
    auto* swatchRow = new QWidget(tab);
    auto* swl = new QHBoxLayout(swatchRow);
    swl->setContentsMargins(0, 0, 0, 0);
    swl->setSpacing(5);
    const QList<QColor> swatches = {
        "#FFFFFF", "#F5F6FA", "#2C3140", "#1A1F2E",
        "#E8372A", "#EA580C", "#16A34A", "#2563EB"
    };
    for (const QColor& c : swatches) {
        auto* btn = new QToolButton(swatchRow);
        btn->setFixedSize(26, 26);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(c.name());
        btn->setStyleSheet(QString("QToolButton { background-color: %1; border: 1px solid #C6CAD3; border-radius: 4px; } QToolButton:hover { border: 2px solid #E8372A; }")
                            .arg(c.name()));
        connect(btn, &QToolButton::clicked, this, [this, c]{ emit designColorSelected(c); });
        swl->addWidget(btn);
    }

    auto* btnCustom = makeCmdBtn(QString::fromUtf8("⊕  Custom…"), "Pick a custom background colour", 90);
    connect(btnCustom, &QToolButton::clicked, this, [this]() {
        const QColor c = QColorDialog::getColor(Qt::white, this, "Slide Background");
        if (c.isValid()) emit designColorSelected(c);
    });

    layout->addWidget(makeGroup("Background", { swatchRow, btnCustom }));
    layout->addWidget(makeSeparator());

    // ── Layout ──────────────────────────────────────────────────────────
    auto* layoutCombo = new QComboBox(tab);
    layoutCombo->setObjectName("ribbonCombo");
    layoutCombo->addItem("Title Slide",      static_cast<int>(SlideLayout::Title));
    layoutCombo->addItem("Title + Content",  static_cast<int>(SlideLayout::TitleContent));
    layoutCombo->addItem("Blank",            static_cast<int>(SlideLayout::Blank));
    layoutCombo->setFixedWidth(140);
    layoutCombo->setToolTip("Apply a slide layout");
    connect(layoutCombo, &QComboBox::activated, this, [this, layoutCombo](int idx) {
        emit layoutSelected(static_cast<SlideLayout>(layoutCombo->itemData(idx).toInt()));
    });
    layout->addWidget(makeGroup("Layout", { layoutCombo }));

    layout->addStretch();
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// Transitions tab
// ─────────────────────────────────────────────────────────────────────────────
QWidget* ImpressRibbon::buildTransitionsTab() {
    auto* tab = new QWidget(this);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2);
    layout->setSpacing(0);

    m_transitionGroup = new QButtonGroup(this);
    m_transitionGroup->setExclusive(true);
    struct TItem { const char* label; const char* glyph; SlideTransition t; };
    const TItem items[] = {
        { "None",     "—",            SlideTransition::None     },
        { "Fade",     "◐",            SlideTransition::Fade     },
        { "Push",     "⬅",            SlideTransition::Push     },
        { "Wipe",     "▧",            SlideTransition::Wipe     },
        { "Zoom",     "⌕",            SlideTransition::Zoom     },
        { "Cut",      "✂",            SlideTransition::Cut      },
        { "Cover",    "⧉",            SlideTransition::Cover    },
        { "Uncover",  "▤",            SlideTransition::Uncover  },
        { "Dissolve", "▒",            SlideTransition::Dissolve },
        { "Blinds",   "☰",            SlideTransition::Blinds   },
    };
    QList<QWidget*> tBtns;
    for (const auto& it : items) {
        auto* b = makeBigBtn(glyphIcon(QString::fromUtf8(it.glyph), 22), it.label,
                             QString("%1 transition").arg(it.label), true);
        b->setMinimumWidth(56);
        m_transitionGroup->addButton(b, static_cast<int>(it.t));
        tBtns.append(b);
    }
    m_transitionGroup->button(static_cast<int>(SlideTransition::None))->setChecked(true);
    connect(m_transitionGroup, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
        if (checked) emit transitionSelected(static_cast<SlideTransition>(id));
    });
    layout->addWidget(makeGroup("Transition to This Slide", tBtns));
    layout->addWidget(makeSeparator());

    auto* btnAll = makeCmdBtn(QString::fromUtf8("⧉  Apply to All"), "Apply this transition to every slide", 120);
    connect(btnAll, &QToolButton::clicked, this, [this]() {
        const int id = m_transitionGroup->checkedId();
        if (id >= 0) emit transitionApplyAllRequested(static_cast<SlideTransition>(id));
    });
    layout->addWidget(makeGroup("Timing", { btnAll }));
    layout->addStretch();
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// Animations tab — per-object entrance effects
// ─────────────────────────────────────────────────────────────────────────────
QWidget* ImpressRibbon::buildAnimationsTab() {
    auto* tab = new QWidget(this);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2);
    layout->setSpacing(0);

    struct AItem { const char* label; const char* glyph; const char* tip; ItemAnimation a; };
    const AItem items[] = {
        { "None",        "—",  "No entrance animation",             ItemAnimation::None },
        { "Fade\nIn",    "◐",  "Object fades in",                   ItemAnimation::FadeIn },
        { "Fly\nLeft",   "➡",  "Flies in from the left",            ItemAnimation::FlyInLeft },
        { "Fly\nRight",  "⬅",  "Flies in from the right",           ItemAnimation::FlyInRight },
        { "Fly\nTop",    "⬇",  "Flies in from the top",             ItemAnimation::FlyInTop },
        { "Fly\nBottom", "⬆",  "Flies in from the bottom",          ItemAnimation::FlyInBottom },
        { "Zoom\nIn",    "⌕",  "Zooms in from its centre",          ItemAnimation::ZoomIn },
        { "Spin\nIn",    "↻",  "Spins and scales in",               ItemAnimation::SpinIn },
    };
    QList<QWidget*> aBtns;
    for (const auto& it : items) {
        auto* b = makeBigBtn(glyphIcon(QString::fromUtf8(it.glyph), 22), it.label, it.tip);
        b->setMinimumWidth(56);
        const ItemAnimation a = it.a;
        connect(b, &QToolButton::clicked, this, [this, a]{ emit animationSelected(a); });
        aBtns.append(b);
    }
    layout->addWidget(makeGroup("Entrance (applies to selected object)", aBtns));
    layout->addWidget(makeSeparator());

    // ── Emphasis effects ────────────────────────────────────────────────
    const AItem emph[] = {
        { "Pulse", "\xE2\x97\x89", "Pulses larger then back",  ItemAnimation::EmphasisPulse },
        { "Spin",  "\xE2\x86\xBB", "Spins a full turn",        ItemAnimation::EmphasisSpin  },
        { "Blink", "\xE2\x97\x8B", "Blinks to draw attention", ItemAnimation::EmphasisBlink },
    };
    QList<QWidget*> eBtns;
    for (const auto& it : emph) {
        auto* b = makeBigBtn(glyphIcon(QString::fromUtf8(it.glyph), 22), it.label, it.tip);
        b->setMinimumWidth(56);
        const ItemAnimation a = it.a;
        connect(b, &QToolButton::clicked, this, [this, a]{ emit animationSelected(a); });
        eBtns.append(b);
    }
    layout->addWidget(makeGroup("Emphasis", eBtns));
    layout->addStretch();
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// Slide Show tab
// ─────────────────────────────────────────────────────────────────────────────
QWidget* ImpressRibbon::buildSlideShowTab() {
    auto* tab = new QWidget(this);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2);
    layout->setSpacing(0);

    auto* btnFromStart   = makeCmdBtn("▶  From Beginning", "Start the slide show from slide 1", 140);
    auto* btnFromCurrent = makeCmdBtn("▶  From Current Slide", "Start the slide show from the active slide", 162);

    connect(btnFromStart,   &QToolButton::clicked, this, &ImpressRibbon::slideShowFromBeginningRequested);
    connect(btnFromCurrent, &QToolButton::clicked, this, &ImpressRibbon::slideShowFromCurrentRequested);

    layout->addWidget(makeGroup("Start Slide Show", { btnFromStart, btnFromCurrent }));
    layout->addStretch();
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// Review tab
// ─────────────────────────────────────────────────────────────────────────────
QWidget* ImpressRibbon::buildReviewTab() {
    auto* tab = new QWidget(this);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2);
    layout->setSpacing(0);

    auto* btnNotes = makeCmdBtn("🗒  Notes", "Show or hide the speaker notes panel", 88);
    connect(btnNotes, &QToolButton::clicked, this, &ImpressRibbon::notesToggleRequested);

    layout->addWidget(makeGroup("Notes", { btnNotes }));
    layout->addStretch();
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// View tab
// ─────────────────────────────────────────────────────────────────────────────
QWidget* ImpressRibbon::buildViewTab() {
    auto* tab = new QWidget(this);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(8, 4, 8, 2);
    layout->setSpacing(0);

    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    auto* btnNormal  = makeCmdBtn("Normal",  "Normal editing view", 78, true);
    auto* btnOutline = makeCmdBtn("Outline", "Outline view", 78, true);
    auto* btnSorter  = makeCmdBtn("Sorter",  "Slide Sorter view", 78, true);
    btnNormal->setChecked(true);
    group->addButton(btnNormal,  static_cast<int>(ImpressViewMode::Normal));
    group->addButton(btnOutline, static_cast<int>(ImpressViewMode::Outline));
    group->addButton(btnSorter,  static_cast<int>(ImpressViewMode::SlideSorter));
    connect(group, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
        if (checked) emit viewModeChanged(static_cast<ImpressViewMode>(id));
    });

    layout->addWidget(makeGroup("Presentation Views", { btnNormal, btnOutline, btnSorter }));
    layout->addStretch();
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
QToolButton* ImpressRibbon::makeTabButton(const QString& label) {
    auto* btn = new QToolButton(this);
    btn->setText(label);
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setObjectName("ribbonTabBtn");
    return btn;
}

QToolButton* ImpressRibbon::makeToolBtn(const QString& text, const QString& tooltip, bool checkable) {
    auto* btn = new QToolButton(this);
    btn->setText(text);
    btn->setToolTip(tooltip);
    btn->setCheckable(checkable);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setObjectName("ribbonToolBtn");
    btn->setFixedSize(32, 32);
    return btn;
}

QToolButton* ImpressRibbon::makeIconBtn(const QIcon& icon, const QString& tooltip, bool checkable) {
    auto* btn = new QToolButton(this);
    btn->setIcon(icon);
    btn->setIconSize(QSize(20, 20));
    btn->setToolTip(tooltip);
    btn->setCheckable(checkable);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setObjectName("ribbonToolBtn");
    btn->setFixedSize(32, 32);
    return btn;
}

QToolButton* ImpressRibbon::makeCmdBtn(const QString& text, const QString& tooltip,
                                        int minWidth, bool checkable) {
    auto* btn = new QToolButton(this);
    btn->setText(text);
    btn->setToolTip(tooltip);
    btn->setCheckable(checkable);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setObjectName("ribbonCmdBtn");
    btn->setFixedHeight(32);
    btn->setMinimumWidth(minWidth);
    return btn;
}

QToolButton* ImpressRibbon::makeBigBtn(const QIcon& icon, const QString& text,
                                        const QString& tooltip, bool checkable) {
    auto* btn = new QToolButton(this);
    btn->setIcon(icon);
    btn->setIconSize(QSize(24, 24));
    btn->setText(text);
    btn->setToolTip(tooltip.isEmpty() ? text : tooltip);
    btn->setCheckable(checkable);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setObjectName("ribbonBigBtn");
    btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btn->setFixedHeight(62);
    btn->setMinimumWidth(54);
    return btn;
}

QWidget* ImpressRibbon::makeGroup(const QString& name, const QList<QWidget*>& widgets) {
    auto* group = new QWidget(this);
    auto* v = new QVBoxLayout(group);
    v->setContentsMargins(6, 0, 6, 0);
    v->setSpacing(2);

    auto* row = new QWidget(group);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(3);
    for (QWidget* w : widgets) {
        w->setParent(row);
        h->addWidget(w);
    }
    h->addStretch();

    auto* label = new QLabel(name, group);
    label->setObjectName("ribbonGroupLabel");
    label->setAlignment(Qt::AlignHCenter);

    v->addWidget(row);
    v->addWidget(label);
    return group;
}

QWidget* ImpressRibbon::makeSeparator() {
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedWidth(1);
    sep->setObjectName("ribbonSep");
    return sep;
}

void ImpressRibbon::resetInsertMode() {
    if (m_shapeGroup) {
        if (auto* checked = m_shapeGroup->checkedButton()) {
            m_shapeGroup->setExclusive(false);
            checked->setChecked(false);
            m_shapeGroup->setExclusive(true);
        }
    }
}

void ImpressRibbon::setUndoRedoEnabled(bool undoEnabled, bool redoEnabled) {
    m_btnUndo->setEnabled(undoEnabled);
    m_btnRedo->setEnabled(redoEnabled);
}

void ImpressRibbon::syncCharFormat(bool bold, bool italic, bool underline, bool strike,
                                   const QString& family, int pointSize, const QColor& color) {
    m_syncing = true;
    m_btnBold->setChecked(bold);
    m_btnItalic->setChecked(italic);
    m_btnUnderline->setChecked(underline);
    m_btnStrike->setChecked(strike);
    if (!family.isEmpty()) {
        const int idx = m_fontCombo->findText(family);
        if (idx >= 0) m_fontCombo->setCurrentIndex(idx);
    }
    if (pointSize > 0) m_sizeCombo->setCurrentText(QString::number(pointSize));
    if (color.isValid()) m_textColor = color;
    m_syncing = false;
}

void ImpressRibbon::syncAlignment(Qt::Alignment align) {
    m_btnAlignLeft->setChecked(align.testFlag(Qt::AlignLeft));
    m_btnAlignCenter->setChecked(align.testFlag(Qt::AlignHCenter));
    m_btnAlignRight->setChecked(align.testFlag(Qt::AlignRight));
    m_btnAlignJustify->setChecked(align.testFlag(Qt::AlignJustify));
}

// ─────────────────────────────────────────────────────────────────────────────
// Styling
// ─────────────────────────────────────────────────────────────────────────────
void ImpressRibbon::applyStyles() {
    // The pure-CSS border-triangle for ::down-arrow renders inconsistently
    // across Qt styles (it showed as a solid square), so generate a real
    // arrow PNG once and reference it from the stylesheet.
    const QString arrowPath = QDir(QDir::tempPath()).filePath("nativeoffice_combo_arrow.png");
    {
        QPixmap pm(20, 12);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(90, 96, 110));
        p.setPen(Qt::NoPen);
        QPolygonF tri;
        tri << QPointF(5, 4) << QPointF(15, 4) << QPointF(10, 10);
        p.drawPolygon(tri);
        p.end();
        pm.save(arrowPath, "PNG");
    }

    setStyleSheet(QString(R"(
QWidget#impressRibbon {
    background-color: #FFFFFF;
    border-bottom: 1px solid #D7DAE0;
}
QWidget#ribbonTabRow {
    background-color: #F3F4F6;
    border-bottom: 1px solid #E2E4E9;
}
QToolButton#ribbonTabBtn {
    color: #5A6071;
    background: transparent;
    border: none;
    border-radius: 0;
    padding: 6px 16px;
    font-size: 12px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-weight: 500;
}
QToolButton#ribbonTabBtn:hover {
    color: #1C1E26;
    background: #E7E9EE;
}
QToolButton#ribbonTabBtn:checked {
    color: #1C1E26;
    background-color: #FFFFFF;
    border-bottom: 2px solid #E8372A;
    font-weight: 700;
}
QStackedWidget#ribbonStack {
    background-color: #FFFFFF;
}
QLabel#ribbonGroupLabel {
    color: #9097A3;
    font-size: 10px;
    font-family: "Segoe UI", "Inter", sans-serif;
}
QToolButton#ribbonToolBtn {
    color: #3A3F4B;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    font-size: 14px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-weight: 500;
}
QToolButton#ribbonToolBtn:hover {
    background: #ECEEF2;
    color: #1C1E26;
    border-color: #DCDFE6;
}
QToolButton#ribbonToolBtn:checked {
    background-color: #FCE4E2;
    color: #C0271C;
    border-color: #E8372A;
    font-weight: 700;
}
QToolButton#ribbonToolBtn:pressed {
    background-color: #F6D2CE;
}
QToolButton#ribbonToolBtn:disabled {
    color: #C2C6CE;
}
QToolButton#ribbonBigBtn {
    color: #3A3F4B;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 4px 4px;
    font-size: 11px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-weight: 500;
}
QToolButton#ribbonBigBtn:hover {
    background: #ECEEF2;
    color: #1C1E26;
    border-color: #DCDFE6;
}
QToolButton#ribbonBigBtn:checked {
    background-color: #FCE4E2;
    color: #C0271C;
    border-color: #E8372A;
    font-weight: 700;
}
QToolButton#ribbonBigBtn:pressed {
    background-color: #F6D2CE;
}
QToolButton#ribbonThemeBtn {
    background: #FFFFFF;
    border: 1px solid #D5D8DF;
    border-radius: 6px;
    padding: 2px;
}
QToolButton#ribbonThemeBtn:hover {
    border: 2px solid #E8372A;
}
QToolButton#ribbonCmdBtn {
    color: #2F3440;
    background: #F5F6FA;
    border: 1px solid #D5D8DF;
    border-radius: 6px;
    padding: 5px 12px;
    font-size: 13px;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-weight: 500;
}
QToolButton#ribbonCmdBtn:hover {
    background: #ECEEF3;
    color: #1C1E26;
    border-color: #B9BEC9;
}
QToolButton#ribbonCmdBtn:checked {
    background-color: #FCE4E2;
    color: #C0271C;
    border-color: #E8372A;
    font-weight: 600;
}
QToolButton#ribbonCmdBtn:pressed {
    background-color: #F6D2CE;
}
QComboBox#ribbonCombo {
    background-color: #FFFFFF;
    color: #1C1E26;
    border: 1px solid #D5D8DF;
    border-radius: 6px;
    padding: 4px 6px;
    min-height: 22px;
    font-size: 12px;
    font-family: "Segoe UI", "Inter", sans-serif;
}
QComboBox#ribbonCombo:hover {
    border-color: #9CA3AF;
    background-color: #FAFBFC;
}
QComboBox#ribbonCombo:focus {
    border-color: #E8372A;
}
QComboBox#ribbonCombo::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: center right;
    border: none;
    width: 16px;
}
QComboBox#ribbonCombo::down-arrow {
    image: url("%1");
    width: 10px;
    height: 6px;
}
QComboBox QAbstractItemView {
    background-color: #FFFFFF;
    color: #1C1E26;
    border: 1px solid #D5D8DF;
    border-radius: 6px;
    selection-background-color: #E8372A;
    selection-color: #FFFFFF;
    outline: none;
    padding: 4px;
}
QComboBox QAbstractItemView::item {
    height: 26px;
    padding-left: 6px;
    border-radius: 4px;
}
QFrame#ribbonSep {
    background-color: #E2E4E9;
    border: none;
    margin: 6px 2px 18px 2px;
}
)").arg(arrowPath));
}

} // namespace NativeOffice
