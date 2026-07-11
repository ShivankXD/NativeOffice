// ─────────────────────────────────────────────────────────────────────────────
// ImageResizer.cpp — see ImageResizer.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "ImageResizer.h"
#include "startscreen/LucideIcons.h"
#include "core/common/BrandBar.h"

#include <QAction>
#include <QBuffer>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QImageReader>
#include <QImageWriter>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace NativeOffice {

namespace {

// The same Figma-style surface as the PDF canvas: soft white with a subtle
// dot grid. Children (upload button / thumbnails) sit transparently on top.
class DottedCanvas : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const QColor bg("#FDFDFE");
        p.fillRect(rect(), bg);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#D8DCE4"));
        constexpr int step = 24;
        for (int y = step / 2; y < height(); y += step)
            for (int x = step / 2; x < width(); x += step)
                p.drawEllipse(QPointF(x, y), 1.2, 1.2);
    }
};

// A card that reports clicks — used to select which image to resize.
class ClickCard : public QFrame {
public:
    using QFrame::QFrame;
    std::function<void()> onClick;

protected:
    void mousePressEvent(QMouseEvent* ev) override {
        if (ev->button() == Qt::LeftButton && onClick) onClick();
        QFrame::mousePressEvent(ev);
    }
};

QString joinedImageFilter() {
    QStringList globs;
    for (const QByteArray& f : QImageReader::supportedImageFormats())
        globs << "*." + QString::fromLatin1(f);
    return QStringLiteral("Images (%1)").arg(globs.join(' '));
}

bool isReadableImage(const QString& path) {
    const QByteArray suffix = QFileInfo(path).suffix().toLower().toLatin1();
    return QImageReader::supportedImageFormats().contains(suffix);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// construction
// ─────────────────────────────────────────────────────────────────────────────

ImageResizerWidget::ImageResizerWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("imageResizer");
    setAttribute(Qt::WA_StyledBackground, true);
    setAcceptDrops(true);

    // Large photos (e.g. 8000×6000) exceed Qt's default decode budget and
    // would silently fail to load; this tool legitimately handles them.
    QImageReader::setAllocationLimit(0);

    // The shared NativeOffice brand strip (mark + wordmark + Free/Premium pill)
    // sits above the tool, matching the editors. As a tool it carries no
    // document name, so the rename field stays hidden (never given a name).
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(new BrandBar(this));

    auto* body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addWidget(buildSettingsPanel());
    body->addWidget(buildCanvas(), 1);
    root->addLayout(body, 1);

    applyStyles();
    setMode(true);
    updateUiState();

    // Dev-only capture hook (same family as NATIVEOFFICE_PDF_GRAB): grab()
    // uses Qt's render path, which captures rounded/antialiased fills that
    // PrintWindow-based external captures drop.
    if (qEnvironmentVariableIsSet("NATIVEOFFICE_RESIZER_GRAB")) {
        const QString grabPath = qEnvironmentVariable("NATIVEOFFICE_RESIZER_GRAB");
        QTimer::singleShot(2500, this, [this, grabPath] {
            // Optional: pre-load images (";"-separated paths) so the grab
            // shows the populated card view. Earlier seeds are marked
            // exported so the one-image-at-a-time gate admits the next.
            const QStringList seeds =
                qEnvironmentVariable("NATIVEOFFICE_RESIZER_SEED")
                    .split(QLatin1Char(';'), Qt::SkipEmptyParts);
            for (int i = 0; i < seeds.size(); ++i) {
                addImages({ seeds[i] });
                if (i + 1 < seeds.size())
                    for (Entry& e : m_images) e.exported = true;
            }
            grab().save(grabPath, "PNG");
        });
    }
}

QWidget* ImageResizerWidget::buildSettingsPanel() {
    auto* panel = new QFrame(this);
    panel->setObjectName("rszPanel");
    panel->setFixedWidth(320);

    auto* v = new QVBoxLayout(panel);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    // ── toolbar: add / sort / clear ─────────────────────────────────────
    auto* bar = new QWidget(panel);
    auto* barL = new QVBoxLayout(bar);
    barL->setContentsMargins(16, 14, 16, 12);
    barL->setSpacing(6);

    auto mkTool = [&](const char* svg, const QString& tip) {
        auto* b = new QToolButton(bar);
        b->setObjectName("rszTool");
        b->setIcon(Lucide::icon(svg, "#3B4152", 18, devicePixelRatio()));
        b->setIconSize(QSize(18, 18));
        b->setFixedSize(40, 40);
        b->setToolTip(tip);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };
    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    m_btnAdd   = mkTool(Lucide::kPlus, tr("Add an image"));
    m_btnSort  = mkTool(Lucide::kSortDesc, tr("Sort by name"));
    m_btnClear = mkTool(Lucide::kTrash, tr("Remove all images"));
    row->addWidget(m_btnAdd);
    row->addStretch();
    row->addWidget(m_btnSort);
    row->addWidget(m_btnClear);
    barL->addLayout(row);

    m_countLabel = new QLabel(tr("No images added yet"), bar);
    m_countLabel->setObjectName("rszMuted");
    barL->addWidget(m_countLabel);
    v->addWidget(bar);

    auto* sep = new QFrame(panel);
    sep->setFrameShape(QFrame::HLine);
    sep->setObjectName("rszSep");
    sep->setFixedHeight(1);
    v->addWidget(sep);

    // ── scrollable settings ─────────────────────────────────────────────
    auto* scroll = new QScrollArea(panel);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* page = new QWidget(scroll);
    auto* pv = new QVBoxLayout(page);
    pv->setContentsMargins(16, 14, 16, 14);
    pv->setSpacing(12);

    auto heading = [&](const QString& text) {
        auto* l = new QLabel(text, page);
        l->setObjectName("rszHeading");
        return l;
    };
    auto caption = [&](const QString& text) {
        auto* l = new QLabel(text, page);
        l->setObjectName("rszMuted");
        l->setWordWrap(true);
        return l;
    };

    pv->addWidget(heading(tr("Resize Settings")));

    // Mode tabs: By Size | As Percentage
    auto* tabs = new QFrame(page);
    tabs->setObjectName("rszTabs");
    auto* tl = new QHBoxLayout(tabs);
    tl->setContentsMargins(4, 4, 4, 4);
    tl->setSpacing(2);
    auto mkTab = [&](const QString& text) {
        auto* b = new QToolButton(tabs);
        b->setObjectName("rszTab");
        b->setText(text);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return b;
    };
    m_tabSize    = mkTab(tr("By Size"));
    m_tabPercent = mkTab(tr("As Percentage"));
    tl->addWidget(m_tabSize);
    tl->addWidget(m_tabPercent);
    pv->addWidget(tabs);
    connect(m_tabSize,    &QToolButton::clicked, this, [this] { setMode(true);  });
    connect(m_tabPercent, &QToolButton::clicked, this, [this] { setMode(false); });

    // ── page: By Size ───────────────────────────────────────────────────
    m_pageSize = new QWidget(page);
    auto* sv = new QVBoxLayout(m_pageSize);
    sv->setContentsMargins(0, 0, 0, 0);
    sv->setSpacing(10);

    auto* dims = new QHBoxLayout;
    dims->setSpacing(10);
    auto mkDim = [&](const QString& label, QLineEdit*& edit) {
        auto* box = new QVBoxLayout;
        box->setSpacing(4);
        auto* l = new QLabel(label, m_pageSize);
        l->setObjectName("rszFieldLabel");
        edit = new QLineEdit(m_pageSize);
        edit->setObjectName("rszField");
        edit->setPlaceholderText(tr("Auto"));
        edit->setValidator(new QIntValidator(1, 30000, edit));
        box->addWidget(l);
        box->addWidget(edit);
        return box;
    };
    dims->addLayout(mkDim(tr("Width"), m_widthEdit), 1);
    dims->addLayout(mkDim(tr("Height"), m_heightEdit), 1);
    auto* unitBox = new QVBoxLayout;
    unitBox->setSpacing(4);
    auto* unitSpacer = new QLabel(QString(), m_pageSize);
    unitSpacer->setObjectName("rszFieldLabel");
    auto* unit = new QLabel(tr("px"), m_pageSize);
    unit->setObjectName("rszMuted");
    unitBox->addWidget(unitSpacer);
    unitBox->addWidget(unit);
    dims->addLayout(unitBox, 0);
    sv->addLayout(dims);

    m_lockRatio = new QCheckBox(tr("Lock Aspect Ratio"), m_pageSize);
    m_lockRatio->setChecked(true);
    m_lockRatio->setCursor(Qt::PointingHandCursor);
    sv->addWidget(m_lockRatio);

    // Background Fill — only offered for exact (unlocked) sizes, where the
    // source ratio may not match the target and padding beats stretching.
    m_fillGroup = new QFrame(m_pageSize);
    m_fillGroup->setObjectName("rszFillGroup");
    auto* fg = new QVBoxLayout(m_fillGroup);
    fg->setContentsMargins(12, 10, 12, 10);
    fg->setSpacing(8);
    m_fillEnabled = new QCheckBox(tr("Background Fill"), m_fillGroup);
    m_fillEnabled->setChecked(true);
    m_fillEnabled->setToolTip(tr("Pad to the exact size instead of stretching"));
    m_fillEnabled->setCursor(Qt::PointingHandCursor);
    fg->addWidget(m_fillEnabled);

    auto* pickRow = new QHBoxLayout;
    pickRow->setSpacing(8);
    m_fillPick = new QRadioButton(tr("Pick a color"), m_fillGroup);
    m_fillPick->setChecked(true);
    m_fillPick->setCursor(Qt::PointingHandCursor);
    m_fillHex = new QLabel(m_fillColor.name().toUpper(), m_fillGroup);
    m_fillHex->setObjectName("rszMuted");
    m_fillSwatch = new QToolButton(m_fillGroup);
    m_fillSwatch->setObjectName("rszSwatch");
    m_fillSwatch->setFixedSize(24, 24);
    m_fillSwatch->setCursor(Qt::PointingHandCursor);
    pickRow->addWidget(m_fillPick);
    pickRow->addStretch();
    pickRow->addWidget(m_fillHex);
    pickRow->addWidget(m_fillSwatch);
    fg->addLayout(pickRow);

    m_fillTransparent = new QRadioButton(tr("Transparent"), m_fillGroup);
    m_fillTransparent->setToolTip(tr("PNG output only — JPG has no transparency"));
    m_fillTransparent->setCursor(Qt::PointingHandCursor);
    fg->addWidget(m_fillTransparent);
    sv->addWidget(m_fillGroup);
    pv->addWidget(m_pageSize);

    connect(m_lockRatio, &QCheckBox::toggled, this, [this] { updateUiState(); });
    connect(m_fillSwatch, &QToolButton::clicked, this, &ImageResizerWidget::pickFillColor);
    connect(m_fillPick, &QRadioButton::clicked, this, [this] { updateUiState(); });
    connect(m_fillTransparent, &QRadioButton::clicked, this, [this] { updateUiState(); });
    connect(m_fillEnabled, &QCheckBox::toggled, this, [this] { updateUiState(); });

    // ── page: As Percentage ─────────────────────────────────────────────
    m_pagePercent = new QWidget(page);
    auto* pctL = new QVBoxLayout(m_pagePercent);
    pctL->setContentsMargins(0, 0, 0, 0);
    pctL->setSpacing(4);
    auto* pctLabel = new QLabel(tr("Scale"), m_pagePercent);
    pctLabel->setObjectName("rszFieldLabel");
    m_percentSpin = new QSpinBox(m_pagePercent);
    m_percentSpin->setObjectName("rszField");
    m_percentSpin->setRange(1, 500);
    m_percentSpin->setValue(50);
    m_percentSpin->setSuffix(QStringLiteral(" %"));
    pctL->addWidget(pctLabel);
    pctL->addWidget(m_percentSpin);
    pctL->addWidget(caption(tr("Each image is scaled to this percentage of its original size.")));
    pv->addWidget(m_pagePercent);

    // ── export settings ─────────────────────────────────────────────────
    pv->addSpacing(4);
    pv->addWidget(heading(tr("Export Settings")));

    auto* tsLabel = new QLabel(tr("Target File Size (optional)"), page);
    tsLabel->setObjectName("rszFieldLabel");
    pv->addWidget(tsLabel);
    auto* tsRow = new QHBoxLayout;
    tsRow->setSpacing(8);
    m_targetSize = new QLineEdit(page);
    m_targetSize->setObjectName("rszField");
    m_targetSize->setValidator(new QIntValidator(1, 500000, m_targetSize));
    m_targetUnit = new QComboBox(page);
    m_targetUnit->setObjectName("rszCombo");
    m_targetUnit->addItems({ QStringLiteral("KB"), QStringLiteral("MB") });
    m_targetUnit->setFixedWidth(76);
    tsRow->addWidget(m_targetSize, 1);
    tsRow->addWidget(m_targetUnit);
    pv->addLayout(tsRow);
    pv->addWidget(caption(tr("Set a max output file size. Only works for JPG files.")));

    auto* fmtLabel = new QLabel(tr("Save Image As"), page);
    fmtLabel->setObjectName("rszFieldLabel");
    pv->addWidget(fmtLabel);
    m_formatCombo = new QComboBox(page);
    m_formatCombo->setObjectName("rszCombo");
    m_formatCombo->addItem(QStringLiteral("JPG"));
    m_formatCombo->addItem(QStringLiteral("PNG"));
    if (QImageWriter::supportedImageFormats().contains("webp"))
        m_formatCombo->addItem(QStringLiteral("WEBP"));
    m_formatCombo->addItem(tr("Original"));
    m_formatCombo->setCurrentText(tr("Original"));
    pv->addWidget(m_formatCombo);

    pv->addStretch();
    scroll->setWidget(page);
    v->addWidget(scroll, 1);

    // ── export button ───────────────────────────────────────────────────
    m_exportBtn = new QPushButton(tr("Export  →"), panel);
    m_exportBtn->setObjectName("rszExport");
    m_exportBtn->setFixedHeight(52);
    m_exportBtn->setCursor(Qt::PointingHandCursor);
    v->addWidget(m_exportBtn);

    connect(m_btnAdd,    &QToolButton::clicked, this, &ImageResizerWidget::addImagesDialog);
    connect(m_btnSort,   &QToolButton::clicked, this, &ImageResizerWidget::sortImages);
    connect(m_btnClear,  &QToolButton::clicked, this, &ImageResizerWidget::clearAll);
    connect(m_exportBtn, &QPushButton::clicked, this, &ImageResizerWidget::exportImages);

    // Keep the per-card "→ target" badges in sync with the settings.
    connect(m_widthEdit,  &QLineEdit::textChanged, this,
            [this] { updateTargetBadges(); });
    connect(m_heightEdit, &QLineEdit::textChanged, this,
            [this] { updateTargetBadges(); });
    connect(m_lockRatio,  &QCheckBox::toggled, this,
            [this] { updateTargetBadges(); });
    connect(m_percentSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            [this] { updateTargetBadges(); });

    return panel;
}

QWidget* ImageResizerWidget::buildCanvas() {
    auto* canvas = new DottedCanvas(this);
    auto* cl = new QVBoxLayout(canvas);
    cl->setContentsMargins(24, 24, 24, 24);

    // NOTE: no stylesheet on the stack — a selector-less "background:
    // transparent" here would cascade to every descendant and beat the root
    // sheet's button styling (nearer ancestor wins). The app-wide stylesheet
    // already leaves plain QWidgets transparent, so the dots show through.
    m_canvasStack = new QStackedWidget(canvas);

    // Page 0 — empty state: centered upload button + hint
    auto* empty = new QWidget(m_canvasStack);
    auto* ev = new QVBoxLayout(empty);
    ev->addStretch();
    auto* upload = new QPushButton(tr("Upload Images"), empty);
    upload->setObjectName("rszUpload");
    upload->setFixedSize(220, 56);
    upload->setCursor(Qt::PointingHandCursor);
    ev->addWidget(upload, 0, Qt::AlignHCenter);
    ev->addSpacing(18);
    auto* hint = new QLabel(tr("Please drag or click the + icon to add some images"), empty);
    hint->setObjectName("rszHint");
    hint->setAlignment(Qt::AlignCenter);
    ev->addWidget(hint);
    auto* sub = new QLabel(tr("JPG, PNG, BMP and more — resized copies are saved next to a folder you pick"), empty);
    sub->setObjectName("rszHintSub");
    sub->setAlignment(Qt::AlignCenter);
    ev->addWidget(sub);
    ev->addStretch();
    m_canvasStack->addWidget(empty);
    connect(upload, &QPushButton::clicked, this, &ImageResizerWidget::addImagesDialog);

    // Page 1 — big preview cards for the added images (name + orig → target)
    m_thumbList = new QListWidget(m_canvasStack);
    m_thumbList->setObjectName("rszThumbs");
    m_thumbList->setViewMode(QListView::IconMode);
    m_thumbList->setResizeMode(QListView::Adjust);
    m_thumbList->setMovement(QListView::Static);
    m_thumbList->setSelectionMode(QAbstractItemView::NoSelection);
    m_thumbList->setSpacing(14);
    m_thumbList->setFrameShape(QFrame::NoFrame);
    m_thumbList->viewport()->setAutoFillBackground(false);
    m_canvasStack->addWidget(m_thumbList);

    cl->addWidget(m_canvasStack);
    return canvas;
}

// ─────────────────────────────────────────────────────────────────────────────
// styling — permanent white chrome, violet accents
// ─────────────────────────────────────────────────────────────────────────────
void ImageResizerWidget::applyStyles() {
    setStyleSheet(R"(
QWidget#imageResizer { background: #FFFFFF; }
QFrame#rszPanel { background: #FFFFFF; border-right: 1px solid #E4E7ED; }
QFrame#rszSep { background: #E4E7ED; border: none; }
QLabel#rszHeading { font-size: 16px; font-weight: 600; color: #1C1E26; }
QLabel#rszFieldLabel { font-size: 12px; font-weight: 600; color: #3B4152; }
QLabel#rszMuted { font-size: 11px; color: #8A90A0; }
QToolButton#rszTool { background: #F3F4F6; border: 1px solid #E4E7ED; border-radius: 8px; }
QToolButton#rszTool:hover { background: #E7E9EE; }
QFrame#rszTabs { background: #F3F4F6; border-radius: 8px; }
QToolButton#rszTab { background: transparent; border: none; border-radius: 6px;
    padding: 7px 10px; font-size: 12px; font-weight: 600; color: #5A6071; }
QToolButton#rszTab:hover { color: #1C1E26; }
QToolButton#rszTab:checked { background: #FFFFFF; color: #1C1E26; border: 1px solid #E4E7ED; }
QLineEdit#rszField, QSpinBox#rszField {
    background: #FFFFFF; border: 1px solid #D7DAE0; border-radius: 8px;
    padding: 8px 10px; font-size: 13px; color: #1C1E26; }
QLineEdit#rszField:focus, QSpinBox#rszField:focus { border-color: #6D5BE8; }
QSpinBox#rszField::up-button, QSpinBox#rszField::down-button { width: 18px; }
QCheckBox, QRadioButton { font-size: 12px; color: #3B4152; spacing: 8px; }
QFrame#rszFillGroup { background: #FAFBFC; border: 1px solid #E4E7ED; border-radius: 10px; }
QToolButton#rszSwatch { border: 1px solid #D7DAE0; border-radius: 5px; }
QComboBox#rszCombo { background: #FFFFFF; border: 1px solid #D7DAE0; border-radius: 8px;
    padding: 8px 10px; font-size: 13px; color: #1C1E26; }
QComboBox#rszCombo::drop-down { border: none; width: 26px; }
QComboBox#rszCombo QAbstractItemView { background: #FFFFFF; color: #1C1E26;
    border: 1px solid #E4E7ED; selection-background-color: #F3F1FD;
    selection-color: #4C3BD6; }
QPushButton#rszExport { background: #6D5BE8; color: #FFFFFF; border: none;
    font-size: 16px; font-weight: 600; }
QPushButton#rszExport:hover { background: #8674F0; }
QPushButton#rszExport:disabled { background: #C9CCD6; color: #FFFFFF; }
QPushButton#rszUpload { background: #6D5BE8; color: #FFFFFF; border: none;
    border-radius: 10px; font-size: 15px; font-weight: 600; }
QPushButton#rszUpload:hover { background: #8674F0; }
QLabel#rszHint { font-size: 15px; color: #5A6071; background: transparent; }
QLabel#rszHintSub { font-size: 11px; color: #9AA0AE; background: transparent; }
QListWidget#rszThumbs { background: transparent; }
QListWidget#rszThumbs::item { background: transparent; border: none; }
QFrame#rszCard { background: #FFFFFF; border: 1px solid #E4E7ED; border-radius: 12px; }
QFrame#rszCard[sel="true"] { border: 2px solid #6D5BE8; }
QLabel#rszCardPreview { background: #F7F8FA; border: 1px solid #EFF1F5; border-radius: 8px;
    color: #8A90A0; font-size: 12px; }
QLabel#rszCardName { font-size: 12px; font-weight: 600; color: #1C1E26; background: transparent; }
QLabel#rszCardDone { color: #16A34A; font-size: 11px; font-weight: 600; background: transparent; }
QLabel#rszCardPending { color: #B45309; font-size: 11px; font-weight: 600; background: transparent; }
QLabel#rszBadge { background: #F3F4F6; color: #3B4152; border-radius: 6px;
    padding: 4px 10px; font-size: 11px; font-weight: 600; }
QLabel#rszBadgeTarget { background: #EDE9FC; color: #4C3BD6; border-radius: 6px;
    padding: 4px 10px; font-size: 11px; font-weight: 600; }
QToolButton#rszCardClose { background: #F3F4F6; border: none; border-radius: 11px;
    color: #5A6071; font-size: 11px; }
QToolButton#rszCardClose:hover { background: #FDECEA; color: #C62828; }
QScrollBar:vertical { background: #F6F7F9; width: 10px; border: none; }
QScrollBar::handle:vertical { background: #CDD2DC; border-radius: 4px; min-height: 30px; margin: 2px; }
QScrollBar::handle:vertical:hover { background: #B7BDC9; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
)");
}

// ─────────────────────────────────────────────────────────────────────────────
// image management
// ─────────────────────────────────────────────────────────────────────────────

bool ImageResizerWidget::hasPending() const {
    return std::any_of(m_images.begin(), m_images.end(),
                       [](const Entry& e) { return !e.exported; });
}

void ImageResizerWidget::addImagesDialog() {
    if (hasPending()) {
        QMessageBox::information(this, tr("Image Resizer"),
            tr("Export the current image first — then you can add another."));
        return;
    }
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Add Image"), QDir::homePath(), joinedImageFilter());
    if (!file.isEmpty()) addImages({ file });
}

void ImageResizerWidget::addImages(const QStringList& paths) {
    // One image at a time: importing is locked while an image awaits export.
    if (hasPending()) return;
    for (const QString& p : paths) {
        if (!isReadableImage(p)) continue;
        const bool dup = std::any_of(m_images.begin(), m_images.end(),
                                     [&p](const Entry& e) { return e.path == p; });
        if (dup) continue;
        m_images.push_back({ p, false });
        m_selected = p;               // the new image is what gets resized next
        refreshList();
        return;                       // only the first valid image is taken
    }
}

void ImageResizerWidget::selectImage(const QString& path) {
    if (m_selected == path) return;
    m_selected = path;
    refreshList();
}

void ImageResizerWidget::clearAll() {
    if (m_images.empty()) return;
    m_images.clear();
    m_selected.clear();
    refreshList();
}

void ImageResizerWidget::sortImages() {
    std::sort(m_images.begin(), m_images.end(),
              [asc = m_sortAscending](const Entry& a, const Entry& b) {
        const int c = QString::compare(QFileInfo(a.path).fileName(),
                                       QFileInfo(b.path).fileName(),
                                       Qt::CaseInsensitive);
        return asc ? c < 0 : c > 0;
    });
    m_sortAscending = !m_sortAscending;
    refreshList();
}

void ImageResizerWidget::refreshList() {
    m_thumbList->clear();
    m_targetBadges.clear();

    for (const Entry& entry : m_images) {
        const QString p = entry.path;
        const bool selected = (p == m_selected);

        // Decode the preview at thumbnail scale — fast, low-memory, and it
        // still succeeds where a full-size decode of a huge photo would not.
        QImageReader reader(p);
        reader.setAutoTransform(true);
        QSize origSize = reader.size();
        QImage thumb;
        if (origSize.isValid()) {
            reader.setScaledSize(origSize.scaled(240, 216, Qt::KeepAspectRatio));
            thumb = reader.read();
        } else {
            thumb = reader.read();          // format can't report size upfront
            origSize = thumb.size();
            if (!thumb.isNull())
                thumb = thumb.scaled(240, 216, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
        }

        auto* card = new ClickCard;
        card->setObjectName("rszCard");
        card->setProperty("sel", selected);
        card->setFixedWidth(272);
        card->setCursor(Qt::PointingHandCursor);
        card->onClick = [this, p] { selectImage(p); };
        auto* cv = new QVBoxLayout(card);
        cv->setContentsMargins(12, 8, 12, 12);
        cv->setSpacing(8);

        // top row: export state + remove-this-image button
        auto* top = new QHBoxLayout;
        auto* state = new QLabel(card);
        state->setObjectName(entry.exported ? "rszCardDone" : "rszCardPending");
        state->setText(entry.exported ? tr("✓ Exported") : tr("Ready to resize"));
        auto* close = new QToolButton(card);
        close->setObjectName("rszCardClose");
        close->setText(QString::fromUtf8("\xE2\x9C\x95"));   // ✕
        close->setToolTip(tr("Remove this image"));
        close->setFixedSize(22, 22);
        close->setCursor(Qt::PointingHandCursor);
        top->addWidget(state);
        top->addStretch();
        top->addWidget(close);
        cv->addLayout(top);
        connect(close, &QToolButton::clicked, this, [this, p] {
            m_images.erase(std::remove_if(m_images.begin(), m_images.end(),
                               [&p](const Entry& e) { return e.path == p; }),
                           m_images.end());
            if (m_selected == p)
                m_selected = m_images.empty() ? QString() : m_images.back().path;
            refreshList();
        });

        // large, clearly visible preview
        auto* preview = new QLabel(card);
        preview->setObjectName("rszCardPreview");
        preview->setFixedSize(248, 224);
        preview->setAlignment(Qt::AlignCenter);
        if (!thumb.isNull())
            preview->setPixmap(QPixmap::fromImage(thumb));
        else
            preview->setText(tr("Preview unavailable"));
        cv->addWidget(preview, 0, Qt::AlignHCenter);

        auto* name = new QLabel(card);
        name->setObjectName("rszCardName");
        name->setText(name->fontMetrics().elidedText(
            QFileInfo(p).fileName(), Qt::ElideMiddle, 240));
        name->setToolTip(p);
        cv->addWidget(name);

        // size badges: original → target (target follows the settings live,
        // and only the selected card shows one — that's what Export resizes)
        auto* sizes = new QHBoxLayout;
        sizes->setSpacing(8);
        auto* orig = new QLabel(origSize.isValid()
                                    ? QStringLiteral("%1 × %2")
                                          .arg(origSize.width()).arg(origSize.height())
                                    : QStringLiteral("—"), card);
        orig->setObjectName("rszBadge");
        sizes->addWidget(orig);
        if (selected && origSize.isValid()) {
            auto* arrow = new QLabel(QStringLiteral("→"), card);
            arrow->setObjectName("rszMuted");
            auto* target = new QLabel(card);
            target->setObjectName("rszBadgeTarget");
            sizes->addWidget(arrow);
            sizes->addWidget(target);
            m_targetBadges.emplace_back(target, origSize);
        }
        sizes->addStretch();
        cv->addLayout(sizes);

        auto* item = new QListWidgetItem(m_thumbList);
        item->setData(Qt::UserRole, p);
        item->setSizeHint(card->sizeHint());
        m_thumbList->setItemWidget(item, card);
    }

    updateTargetBadges();
    updateUiState();
}

void ImageResizerWidget::updateTargetBadges() {
    for (const auto& [label, origSize] : m_targetBadges) {
        const QSize t = computeTargetSize(origSize);
        label->setText(QStringLiteral("%1 × %2").arg(t.width()).arg(t.height()));
    }
}

QSize ImageResizerWidget::computeTargetSize(const QSize& src) const {
    if (!src.isValid()) return src;
    if (m_tabPercent->isChecked()) {
        const double f = m_percentSpin->value() / 100.0;
        return { std::max(1, int(src.width() * f + 0.5)),
                 std::max(1, int(src.height() * f + 0.5)) };
    }
    const int w = m_widthEdit->text().toInt();    // 0 == Auto
    const int h = m_heightEdit->text().toInt();
    if (w <= 0 && h <= 0) return src;
    if (m_lockRatio->isChecked() || w <= 0 || h <= 0) {
        if (w > 0 && h > 0) return src.scaled(w, h, Qt::KeepAspectRatio);
        if (w > 0) return { w, std::max(1, int(std::llround(
                                double(src.height()) * w / src.width()))) };
        return { std::max(1, int(std::llround(
                     double(src.width()) * h / src.height()))), h };
    }
    return { w, h };
}

void ImageResizerWidget::updateUiState() {
    const int n = int(m_images.size());
    const bool pending = hasPending();
    if (n == 0)
        m_countLabel->setText(tr("No images added yet"));
    else if (pending)
        m_countLabel->setText(tr("Export the selected image to add more"));
    else
        m_countLabel->setText(tr("%n image(s) added", nullptr, n));
    m_canvasStack->setCurrentIndex(n == 0 ? 0 : 1);
    m_exportBtn->setEnabled(!m_selected.isEmpty());
    m_btnAdd->setEnabled(!pending);
    m_btnAdd->setToolTip(pending
        ? tr("Export the selected image first, then add another")
        : tr("Add an image"));
    m_btnSort->setEnabled(n > 1);
    m_btnClear->setEnabled(n > 0);

    // Background Fill is only meaningful for exact (unlocked) sizes.
    m_fillGroup->setVisible(!m_lockRatio->isChecked());
    const bool fillOn = m_fillEnabled->isChecked();
    m_fillPick->setEnabled(fillOn);
    m_fillTransparent->setEnabled(fillOn);
    m_fillSwatch->setEnabled(fillOn && m_fillPick->isChecked());
    m_fillHex->setText(m_fillColor.name().toUpper());
    m_fillSwatch->setStyleSheet(
        QStringLiteral("QToolButton#rszSwatch { background: %1; border: 1px solid #D7DAE0; "
                       "border-radius: 5px; }").arg(m_fillColor.name()));
}

void ImageResizerWidget::setMode(bool bySize) {
    m_tabSize->setChecked(bySize);
    m_tabPercent->setChecked(!bySize);
    m_pageSize->setVisible(bySize);
    m_pagePercent->setVisible(!bySize);
    updateTargetBadges();
}

void ImageResizerWidget::pickFillColor() {
    const QColor c = QColorDialog::getColor(m_fillColor, this, tr("Background Fill Color"));
    if (!c.isValid()) return;
    m_fillColor = c;
    updateUiState();
}

// ─────────────────────────────────────────────────────────────────────────────
// drag & drop
// ─────────────────────────────────────────────────────────────────────────────

void ImageResizerWidget::dragEnterEvent(QDragEnterEvent* ev) {
    if (hasPending() || !ev->mimeData()->hasUrls()) return;
    for (const QUrl& u : ev->mimeData()->urls())
        if (u.isLocalFile() && isReadableImage(u.toLocalFile())) {
            ev->acceptProposedAction();
            return;
        }
}

void ImageResizerWidget::dropEvent(QDropEvent* ev) {
    if (hasPending()) {
        QMessageBox::information(this, tr("Image Resizer"),
            tr("Export the current image first — then you can add another."));
        return;
    }
    QStringList paths;
    for (const QUrl& u : ev->mimeData()->urls())
        if (u.isLocalFile()) paths << u.toLocalFile();
    addImages(paths);   // takes the first valid image only
    ev->acceptProposedAction();
}

// ─────────────────────────────────────────────────────────────────────────────
// resize + export
// ─────────────────────────────────────────────────────────────────────────────

QImage ImageResizerWidget::transform(const QImage& src) const {
    if (m_tabPercent->isChecked()) {
        const double f = m_percentSpin->value() / 100.0;
        const QSize target(std::max(1, int(src.width() * f + 0.5)),
                           std::max(1, int(src.height() * f + 0.5)));
        return src.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    const int w = m_widthEdit->text().toInt();    // 0 == Auto
    const int h = m_heightEdit->text().toInt();
    if (w <= 0 && h <= 0) return src;

    if (m_lockRatio->isChecked() || w <= 0 || h <= 0) {
        if (w > 0 && h > 0)   // fit within the box, preserving ratio
            return src.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return (w > 0) ? src.scaledToWidth(w, Qt::SmoothTransformation)
                       : src.scaledToHeight(h, Qt::SmoothTransformation);
    }

    // Unlocked + both dimensions: exact target size.
    if (!m_fillEnabled->isChecked())
        return src.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    // Pad to exact size: image centered on a colour/transparent canvas.
    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    out.fill(m_fillTransparent->isChecked() ? Qt::transparent : m_fillColor);
    const QImage scaled =
        src.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter p(&out);
    p.drawImage(QPoint((w - scaled.width()) / 2, (h - scaled.height()) / 2), scaled);
    p.end();
    return out;
}

void ImageResizerWidget::exportImages() {
    // Export resizes the SELECTED image only.
    if (m_selected.isEmpty()) return;
    const QString srcPath = m_selected;

    if (m_tabSize->isChecked() && m_widthEdit->text().isEmpty()
        && m_heightEdit->text().isEmpty()) {
        QMessageBox::information(this, tr("Image Resizer"),
                                 tr("Enter a target width and/or height first."));
        return;
    }

    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Choose Output Folder"), QFileInfo(srcPath).absolutePath());
    if (dir.isEmpty()) return;

    // Target byte budget (JPG only).
    qint64 targetBytes = 0;
    if (!m_targetSize->text().isEmpty()) {
        targetBytes = m_targetSize->text().toLongLong() * 1024;
        if (m_targetUnit->currentText() == QLatin1String("MB")) targetBytes *= 1024;
    }

    QImageReader reader(srcPath);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) {
        QMessageBox::warning(this, tr("Image Resizer"),
                             tr("Could not read:\n%1").arg(srcPath));
        return;
    }

    img = transform(img);

    const QString fmtChoice = m_formatCombo->currentText();
    QString ext = fmtChoice == tr("Original")
                      ? QFileInfo(srcPath).suffix().toLower()
                      : fmtChoice.toLower();
    if (ext == QLatin1String("jpeg")) ext = QStringLiteral("jpg");
    const bool isJpg = ext == QLatin1String("jpg");
    if (isJpg && img.hasAlphaChannel()) {
        // JPEG has no alpha — composite on the fill colour (or white).
        QImage flat(img.size(), QImage::Format_RGB32);
        flat.fill(m_fillTransparent && m_fillTransparent->isChecked()
                      ? QColor(Qt::white) : m_fillColor);
        QPainter p(&flat);
        p.drawImage(0, 0, img);
        p.end();
        img = flat;
    }

    // Unique output path: <name>_resized.<ext>, then (2), (3), …
    const QString base = QFileInfo(srcPath).completeBaseName() + "_resized";
    QString outPath = dir + "/" + base + "." + ext;
    for (int i = 2; QFileInfo::exists(outPath); ++i)
        outPath = dir + "/" + base + QStringLiteral(" (%1).").arg(i) + ext;

    bool ok = false;
    if (isJpg && targetBytes > 0) {
        // Highest quality that fits the byte budget (85 → 10).
        QByteArray best;
        for (int q = 85; q >= 10; q -= 15) {
            QByteArray buf;
            QBuffer io(&buf);
            io.open(QIODevice::WriteOnly);
            img.save(&io, "jpg", q);
            best = buf;
            if (buf.size() <= targetBytes) break;
        }
        QFile f(outPath);
        ok = f.open(QIODevice::WriteOnly) && f.write(best) == best.size();
    } else {
        ok = img.save(outPath, ext.toLatin1().constData(), isJpg ? 90 : -1);
    }

    if (!ok) {
        QMessageBox::warning(this, tr("Image Resizer"),
                             tr("Could not write:\n%1").arg(outPath));
        return;
    }

    // Unlock importing: this image is done.
    for (Entry& e : m_images)
        if (e.path == srcPath) e.exported = true;
    refreshList();

    QMessageBox box(QMessageBox::Information, tr("Export Complete"),
                    tr("Saved %1 (%2 × %3) to:\n%4")
                        .arg(QFileInfo(outPath).fileName())
                        .arg(img.width()).arg(img.height()).arg(dir),
                    QMessageBox::Ok, this);
    auto* open = box.addButton(tr("Open Folder"), QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == open)
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

} // namespace NativeOffice
