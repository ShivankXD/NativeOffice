// ─────────────────────────────────────────────────────────────────────────────
// PdfDecorUi.cpp — see PdfDecorUi.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfDecorUi.h"
#include "PdfDecor.h"
#include "PdfEditSession.h"
#include "core/theme/ThemeManager.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace NativeOffice::Pdf {

namespace {

// Paints a small watermark-preview tile (white page, diagonal gray text).
QIcon watermarkTile(const QString& text, bool tiled) {
    QPixmap pm(104, 128);
    pm.fill(Qt::white);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#C9CDD6"), 1));
    p.drawRect(0, 0, pm.width() - 1, pm.height() - 1);
    p.setPen(QColor(150, 150, 150));
    auto drawOne = [&](const QPointF& center, double px) {
        p.save();
        p.translate(center);
        p.rotate(-40);
        QFont f("Segoe UI", 1);
        f.setPixelSize(int(px));
        f.setBold(true);
        p.setFont(f);
        QFontMetricsF fm(f);
        p.drawText(QPointF(-fm.horizontalAdvance(text) / 2, fm.ascent() / 2 - 1), text);
        p.restore();
    };
    if (tiled) {
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 2; ++c)
                drawOne(QPointF(26 + c * 52 + (r % 2) * 20, 20 + r * 32), 9);
    } else {
        drawOne(QPointF(52, 64), 13);
    }
    p.end();
    return QIcon(pm);
}

void applyWatermarkSpec(QWidget* parent, EditSession* session,
                        const std::function<void(const QString&)>& toast,
                        const TextWatermarkSpec& spec) {
    // "Update" semantics: an existing watermark is replaced, not stacked.
    if (hasDecor(session->currentRevisionPath(), DecorKind::Watermark)) {
        const OpResult rm = session->apply(QObject::tr("Remove watermark"),
            [](const QString& in, const QString& out) {
                return removeDecor(in, out, DecorKind::Watermark);
            });
        if (!rm.ok) { toast(rm.message); return; }
    }
    const OpResult r = session->apply(QObject::tr("Watermark"),
        [&spec](const QString& in, const QString& out) {
            return addTextWatermark(in, out, spec);
        });
    toast(r.ok ? QObject::tr("Watermark \"%1\" applied.").arg(spec.text) : r.message);
    Q_UNUSED(parent);
}

// The custom text/image watermark editor. Returns true when applied.
bool runCustomWatermarkEditor(QWidget* parent, EditSession* session,
                              const std::function<void(const QString&)>& toast) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Custom Watermark"));
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    auto* v = new QVBoxLayout(&dlg);

    auto* textRadio  = new QRadioButton(QObject::tr("Text watermark"), &dlg);
    auto* imageRadio = new QRadioButton(QObject::tr("Image watermark"), &dlg);
    textRadio->setChecked(true);
    auto* radioRow = new QHBoxLayout;
    radioRow->addWidget(textRadio);
    radioRow->addWidget(imageRadio);
    radioRow->addStretch();
    v->addLayout(radioRow);

    auto* form = new QFormLayout;

    auto* textEdit = new QLineEdit(QStringLiteral("CONFIDENTIAL"), &dlg);
    auto* sizeSpin = new QSpinBox(&dlg);
    sizeSpin->setRange(8, 200);
    sizeSpin->setValue(52);

    auto* colorBtn = new QPushButton(QObject::tr("Choose…"), &dlg);
    QColor color(128, 128, 128);
    auto refreshColorBtn = [colorBtn, &color] {
        colorBtn->setText(color.name());
        colorBtn->setStyleSheet(QStringLiteral("color: %1;").arg(color.name()));
    };
    refreshColorBtn();
    QObject::connect(colorBtn, &QPushButton::clicked, &dlg, [&] {
        const QColor c = QColorDialog::getColor(color, &dlg, QObject::tr("Watermark Color"));
        if (c.isValid()) { color = c; refreshColorBtn(); }
    });

    auto* imageRow = new QWidget(&dlg);
    auto* imageLayout = new QHBoxLayout(imageRow);
    imageLayout->setContentsMargins(0, 0, 0, 0);
    auto* imageEdit = new QLineEdit(imageRow);
    imageEdit->setReadOnly(true);
    auto* browseBtn = new QPushButton(QObject::tr("Browse…"), imageRow);
    imageLayout->addWidget(imageEdit, 1);
    imageLayout->addWidget(browseBtn);
    QObject::connect(browseBtn, &QPushButton::clicked, &dlg, [&] {
        const QString p = QFileDialog::getOpenFileName(&dlg, QObject::tr("Watermark Image"),
            QString(), QObject::tr("Images (*.png *.jpg *.jpeg *.bmp)"));
        if (!p.isEmpty()) imageEdit->setText(p);
    });

    auto* scaleSpin = new QSpinBox(&dlg);
    scaleSpin->setRange(5, 100);
    scaleSpin->setValue(50);
    scaleSpin->setSuffix(QStringLiteral(" %"));

    auto* opacitySlider = new QSlider(Qt::Horizontal, &dlg);
    opacitySlider->setRange(5, 100);
    opacitySlider->setValue(35);
    auto* rotSpin = new QSpinBox(&dlg);
    rotSpin->setRange(-180, 180);
    rotSpin->setValue(45);
    rotSpin->setSuffix(QStringLiteral("°"));
    auto* tiledCheck = new QCheckBox(QObject::tr("Tile across the page"), &dlg);

    form->addRow(QObject::tr("Text:"), textEdit);
    form->addRow(QObject::tr("Font size:"), sizeSpin);
    form->addRow(QObject::tr("Color:"), colorBtn);
    form->addRow(QObject::tr("Image:"), imageRow);
    form->addRow(QObject::tr("Image width:"), scaleSpin);
    form->addRow(QObject::tr("Opacity:"), opacitySlider);
    form->addRow(QObject::tr("Rotation:"), rotSpin);
    form->addRow(QString(), tiledCheck);
    v->addLayout(form);

    auto syncMode = [&] {
        const bool img = imageRadio->isChecked();
        textEdit->setEnabled(!img);
        sizeSpin->setEnabled(!img);
        colorBtn->setEnabled(!img);
        imageEdit->setEnabled(img);
        browseBtn->setEnabled(img);
        scaleSpin->setEnabled(img);
    };
    QObject::connect(textRadio,  &QRadioButton::toggled, &dlg, syncMode);
    QObject::connect(imageRadio, &QRadioButton::toggled, &dlg, syncMode);
    syncMode();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Apply"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) return false;

    if (imageRadio->isChecked()) {
        if (imageEdit->text().isEmpty()) { toast(QObject::tr("Pick an image first.")); return false; }
        if (hasDecor(session->currentRevisionPath(), DecorKind::Watermark)) {
            const OpResult rm = session->apply(QObject::tr("Remove watermark"),
                [](const QString& in, const QString& out) {
                    return removeDecor(in, out, DecorKind::Watermark);
                });
            if (!rm.ok) { toast(rm.message); return false; }
        }
        ImageWatermarkSpec spec;
        spec.imagePath   = imageEdit->text();
        spec.opacity     = opacitySlider->value() / 100.0;
        spec.rotationDeg = rotSpin->value();
        spec.scalePct    = scaleSpin->value();
        spec.tiled       = tiledCheck->isChecked();
        const OpResult r = session->apply(QObject::tr("Watermark"),
            [&spec](const QString& in, const QString& out) {
                return addImageWatermark(in, out, spec);
            });
        toast(r.ok ? QObject::tr("Image watermark applied.") : r.message);
        return r.ok;
    }

    TextWatermarkSpec spec;
    spec.text        = textEdit->text();
    spec.fontSizePt  = sizeSpin->value();
    spec.color       = color;
    spec.opacity     = opacitySlider->value() / 100.0;
    spec.rotationDeg = rotSpin->value();
    spec.tiled       = tiledCheck->isChecked();
    applyWatermarkSpec(parent, session, toast, spec);
    return true;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Watermark panel
// ─────────────────────────────────────────────────────────────────────────────

void runWatermarkUi(QWidget* parent, EditSession* session,
                    const std::function<void(const QString&)>& toast) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Watermark"));
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    auto* v = new QVBoxLayout(&dlg);

    // ── Custom section ──────────────────────────────────────────────────
    auto* customBox = new QGroupBox(QObject::tr("Custom Watermark"), &dlg);
    auto* customLayout = new QHBoxLayout(customBox);
    auto* addBtn = new QToolButton(customBox);
    addBtn->setText(QObject::tr("＋\nAdd"));
    addBtn->setFixedSize(104, 128);
    addBtn->setToolTip(QObject::tr("Build your own text or image watermark"));
    customLayout->addWidget(addBtn);
    customLayout->addStretch();
    v->addWidget(customBox);

    // ── Preset grid ─────────────────────────────────────────────────────
    auto* presetBox = new QGroupBox(QObject::tr("Preset"), &dlg);
    auto* grid = new QGridLayout(presetBox);
    const QString presetTexts[4] = {
        QStringLiteral("CONFIDENTIAL"), QStringLiteral("TOP SECRET"),
        QStringLiteral("DO NOT COPY"),  QStringLiteral("INTERNAL INFORMATION"),
    };
    int col = 0;
    for (int tiledPass = 0; tiledPass < 2; ++tiledPass) {
        for (const QString& text : presetTexts) {
            auto* b = new QToolButton(presetBox);
            b->setIcon(watermarkTile(text, tiledPass == 1));
            b->setIconSize(QSize(104, 128));
            b->setToolTip(tiledPass == 1 ? QObject::tr("%1 (tiled)").arg(text) : text);
            grid->addWidget(b, tiledPass, col % 4);
            QObject::connect(b, &QToolButton::clicked, &dlg, [&, text, tiledPass] {
                TextWatermarkSpec spec;
                spec.text  = text;
                spec.tiled = (tiledPass == 1);
                if (spec.tiled) spec.fontSizePt = 26;
                if (text.size() > 14) spec.fontSizePt = spec.tiled ? 20 : 38;
                applyWatermarkSpec(parent, session, toast, spec);
                dlg.accept();
            });
            ++col;
        }
    }
    v->addWidget(presetBox);

    // ── Update / Delete ─────────────────────────────────────────────────
    const bool has = hasDecor(session->currentRevisionPath(), DecorKind::Watermark);
    auto* actionsRow = new QHBoxLayout;
    auto* updateBtn = new QPushButton(QObject::tr("Update Watermark…"), &dlg);
    auto* deleteBtn = new QPushButton(QObject::tr("Delete Watermark"), &dlg);
    auto* closeBtn  = new QPushButton(QObject::tr("Close"), &dlg);
    updateBtn->setEnabled(has);
    deleteBtn->setEnabled(has);
    actionsRow->addWidget(updateBtn);
    actionsRow->addWidget(deleteBtn);
    actionsRow->addStretch();
    actionsRow->addWidget(closeBtn);
    v->addLayout(actionsRow);

    QObject::connect(addBtn, &QToolButton::clicked, &dlg, [&] {
        if (runCustomWatermarkEditor(&dlg, session, toast)) dlg.accept();
    });
    QObject::connect(updateBtn, &QPushButton::clicked, &dlg, [&] {
        if (runCustomWatermarkEditor(&dlg, session, toast)) dlg.accept();
    });
    QObject::connect(deleteBtn, &QPushButton::clicked, &dlg, [&] {
        const OpResult r = session->apply(QObject::tr("Delete watermark"),
            [](const QString& in, const QString& out) {
                return removeDecor(in, out, DecorKind::Watermark);
            });
        toast(r.ok ? QObject::tr("Watermark removed.") : r.message);
        dlg.accept();
    });
    QObject::connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    dlg.exec();
}

// ─────────────────────────────────────────────────────────────────────────────
// Background
// ─────────────────────────────────────────────────────────────────────────────

void runBackgroundUi(QWidget* parent, EditSession* session,
                     const std::function<void(const QString&)>& toast) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Page Background"));
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    auto* v = new QVBoxLayout(&dlg);
    v->addWidget(new QLabel(QObject::tr("Give every page a solid background color.\n"
                                        "(Content that paints its own white background will cover it.)"), &dlg));

    auto* row = new QHBoxLayout;
    auto* pickBtn = new QPushButton(QObject::tr("Choose Color…"), &dlg);
    auto* removeBtn = new QPushButton(QObject::tr("Remove Background"), &dlg);
    auto* cancelBtn = new QPushButton(QObject::tr("Cancel"), &dlg);
    removeBtn->setEnabled(hasDecor(session->currentRevisionPath(), DecorKind::Background));
    row->addWidget(pickBtn);
    row->addWidget(removeBtn);
    row->addStretch();
    row->addWidget(cancelBtn);
    v->addLayout(row);

    QObject::connect(pickBtn, &QPushButton::clicked, &dlg, [&] {
        const QColor c = QColorDialog::getColor(QColor("#FFF8E1"), &dlg, QObject::tr("Background Color"));
        if (!c.isValid()) return;
        if (hasDecor(session->currentRevisionPath(), DecorKind::Background)) {
            const OpResult rm = session->apply(QObject::tr("Remove background"),
                [](const QString& in, const QString& out) {
                    return removeDecor(in, out, DecorKind::Background);
                });
            if (!rm.ok) { toast(rm.message); return; }
        }
        const OpResult r = session->apply(QObject::tr("Background"),
            [c](const QString& in, const QString& out) {
                return setBackground(in, out, c);
            });
        toast(r.ok ? QObject::tr("Background applied.") : r.message);
        dlg.accept();
    });
    QObject::connect(removeBtn, &QPushButton::clicked, &dlg, [&] {
        const OpResult r = session->apply(QObject::tr("Remove background"),
            [](const QString& in, const QString& out) {
                return removeDecor(in, out, DecorKind::Background);
            });
        toast(r.ok ? QObject::tr("Background removed.") : r.message);
        dlg.accept();
    });
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    dlg.exec();
}

// ─────────────────────────────────────────────────────────────────────────────
// Page numbers
// ─────────────────────────────────────────────────────────────────────────────

void runPageNumberUi(QWidget* parent, EditSession* session,
                     const std::function<void(const QString&)>& toast) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Page Number"));
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    auto* form = new QFormLayout(&dlg);

    auto* posCombo = new QComboBox(&dlg);
    posCombo->addItems({ QObject::tr("Bottom center"), QObject::tr("Bottom left"),
                         QObject::tr("Bottom right"),  QObject::tr("Top center"),
                         QObject::tr("Top left"),      QObject::tr("Top right") });
    auto* fmtCombo = new QComboBox(&dlg);
    fmtCombo->addItems({ QObject::tr("1, 2, 3…"), QObject::tr("Page 1 of N"), QObject::tr("- 1 -") });
    auto* startSpin = new QSpinBox(&dlg);
    startSpin->setRange(1, 9999);
    startSpin->setValue(1);
    auto* sizeSpin = new QSpinBox(&dlg);
    sizeSpin->setRange(6, 36);
    sizeSpin->setValue(10);

    form->addRow(QObject::tr("Position:"), posCombo);
    form->addRow(QObject::tr("Format:"), fmtCombo);
    form->addRow(QObject::tr("Start at:"), startSpin);
    form->addRow(QObject::tr("Font size:"), sizeSpin);

    auto* buttons = new QDialogButtonBox(&dlg);
    auto* applyBtn = buttons->addButton(QObject::tr("Apply"), QDialogButtonBox::AcceptRole);
    auto* removeBtn = buttons->addButton(QObject::tr("Remove"), QDialogButtonBox::DestructiveRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    removeBtn->setEnabled(hasDecor(session->currentRevisionPath(), DecorKind::PageNumber));
    form->addRow(buttons);

    QObject::connect(applyBtn, &QPushButton::clicked, &dlg, [&] {
        if (hasDecor(session->currentRevisionPath(), DecorKind::PageNumber)) {
            const OpResult rm = session->apply(QObject::tr("Remove page numbers"),
                [](const QString& in, const QString& out) {
                    return removeDecor(in, out, DecorKind::PageNumber);
                });
            if (!rm.ok) { toast(rm.message); return; }
        }
        PageNumberSpec spec;
        spec.position   = PageNumberSpec::Position(posCombo->currentIndex());
        spec.format     = PageNumberSpec::Format(fmtCombo->currentIndex());
        spec.startAt    = startSpin->value();
        spec.fontSizePt = sizeSpin->value();
        const OpResult r = session->apply(QObject::tr("Page numbers"),
            [&spec](const QString& in, const QString& out) {
                return addPageNumbers(in, out, spec);
            });
        toast(r.ok ? QObject::tr("Page numbers added.") : r.message);
        dlg.accept();
    });
    QObject::connect(removeBtn, &QPushButton::clicked, &dlg, [&] {
        const OpResult r = session->apply(QObject::tr("Remove page numbers"),
            [](const QString& in, const QString& out) {
                return removeDecor(in, out, DecorKind::PageNumber);
            });
        toast(r.ok ? QObject::tr("Page numbers removed.") : r.message);
        dlg.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.exec();
}

// ─────────────────────────────────────────────────────────────────────────────
// Header & footer
// ─────────────────────────────────────────────────────────────────────────────

void runHeaderFooterUi(QWidget* parent, EditSession* session,
                       const std::function<void(const QString&)>& toast) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Header and Footer"));
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    auto* v = new QVBoxLayout(&dlg);

    v->addWidget(new QLabel(QObject::tr("Macros: &[Page] = page number, &[Pages] = page count, &[Date] = today"), &dlg));

    auto* grid = new QGridLayout;
    grid->addWidget(new QLabel(QObject::tr("Header"), &dlg), 0, 0);
    auto* hL = new QLineEdit(&dlg); auto* hC = new QLineEdit(&dlg); auto* hR = new QLineEdit(&dlg);
    hL->setPlaceholderText(QObject::tr("Left"));
    hC->setPlaceholderText(QObject::tr("Center"));
    hR->setPlaceholderText(QObject::tr("Right"));
    grid->addWidget(hL, 0, 1); grid->addWidget(hC, 0, 2); grid->addWidget(hR, 0, 3);
    grid->addWidget(new QLabel(QObject::tr("Footer"), &dlg), 1, 0);
    auto* fL = new QLineEdit(&dlg); auto* fC = new QLineEdit(&dlg); auto* fR = new QLineEdit(&dlg);
    fL->setPlaceholderText(QObject::tr("Left"));
    fC->setPlaceholderText(QObject::tr("Center"));
    fC->setText(QStringLiteral("&[Page] / &[Pages]"));
    fR->setPlaceholderText(QObject::tr("Right"));
    grid->addWidget(fL, 1, 1); grid->addWidget(fC, 1, 2); grid->addWidget(fR, 1, 3);
    v->addLayout(grid);

    auto* form = new QFormLayout;
    auto* sizeSpin = new QSpinBox(&dlg);
    sizeSpin->setRange(6, 24);
    sizeSpin->setValue(9);
    form->addRow(QObject::tr("Font size:"), sizeSpin);
    v->addLayout(form);

    auto* buttons = new QDialogButtonBox(&dlg);
    auto* applyBtn = buttons->addButton(QObject::tr("Apply"), QDialogButtonBox::AcceptRole);
    auto* removeBtn = buttons->addButton(QObject::tr("Remove"), QDialogButtonBox::DestructiveRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    removeBtn->setEnabled(hasDecor(session->currentRevisionPath(), DecorKind::HeaderFooter));
    v->addWidget(buttons);

    QObject::connect(applyBtn, &QPushButton::clicked, &dlg, [&] {
        if (hasDecor(session->currentRevisionPath(), DecorKind::HeaderFooter)) {
            const OpResult rm = session->apply(QObject::tr("Remove header/footer"),
                [](const QString& in, const QString& out) {
                    return removeDecor(in, out, DecorKind::HeaderFooter);
                });
            if (!rm.ok) { toast(rm.message); return; }
        }
        HeaderFooterSpec spec;
        spec.headerLeft   = hL->text();
        spec.headerCenter = hC->text();
        spec.headerRight  = hR->text();
        spec.footerLeft   = fL->text();
        spec.footerCenter = fC->text();
        spec.footerRight  = fR->text();
        spec.fontSizePt   = sizeSpin->value();
        const OpResult r = session->apply(QObject::tr("Header and footer"),
            [&spec](const QString& in, const QString& out) {
                return addHeaderFooter(in, out, spec);
            });
        toast(r.ok ? QObject::tr("Header and footer applied.") : r.message);
        dlg.accept();
    });
    QObject::connect(removeBtn, &QPushButton::clicked, &dlg, [&] {
        const OpResult r = session->apply(QObject::tr("Remove header/footer"),
            [](const QString& in, const QString& out) {
                return removeDecor(in, out, DecorKind::HeaderFooter);
            });
        toast(r.ok ? QObject::tr("Header and footer removed.") : r.message);
        dlg.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.exec();
}

} // namespace NativeOffice::Pdf
