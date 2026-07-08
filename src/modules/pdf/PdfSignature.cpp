// ─────────────────────────────────────────────────────────────────────────────
// PdfSignature.cpp — see PdfSignature.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfSignature.h"
#include "core/theme/ThemeManager.h"

#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>

namespace NativeOffice::Pdf {

// ─────────────────────────────────────────────────────────────────────────────
// SignaturePad
// ─────────────────────────────────────────────────────────────────────────────

SignaturePad::SignaturePad(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(420, 150);
    setAttribute(Qt::WA_StaticContents);
    setCursor(Qt::CrossCursor);
}

void SignaturePad::ensureCanvas() {
    const QSize need = size() * devicePixelRatioF();
    if (m_canvas.size() != need) {
        QImage fresh(need, QImage::Format_ARGB32_Premultiplied);
        fresh.setDevicePixelRatio(devicePixelRatioF());
        fresh.fill(Qt::transparent);
        if (!m_canvas.isNull()) {
            QPainter p(&fresh);
            p.drawImage(0, 0, m_canvas);
        }
        m_canvas = fresh;
    }
}

void SignaturePad::clear() {
    ensureCanvas();
    m_canvas.fill(Qt::transparent);
    m_empty = true;
    update();
    emit changed();
}

void SignaturePad::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const auto& tm = ThemeManager::instance();
    p.fillRect(rect(), QColor(tm.isDark() ? "#0D1117" : "#FFFFFF"));
    // guide line
    p.setPen(QPen(QColor(tm.chromeBorder()), 1, Qt::DashLine));
    p.drawLine(16, height() - 34, width() - 16, height() - 34);
    p.setPen(QColor(tm.chromeTextMuted()));
    p.drawText(20, height() - 20, tr("Sign above"));
    if (!m_canvas.isNull()) p.drawImage(0, 0, m_canvas);
}

void SignaturePad::mousePressEvent(QMouseEvent* ev) {
    if (ev->button() != Qt::LeftButton) return;
    ensureCanvas();
    m_drawing = true;
    m_last = ev->position();
}

void SignaturePad::mouseMoveEvent(QMouseEvent* ev) {
    if (!m_drawing) return;
    ensureCanvas();
    QPainter p(&m_canvas);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(m_penColor, 2.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.drawLine(m_last, ev->position());
    m_last = ev->position();
    m_empty = false;
    update();
    emit changed();
}

void SignaturePad::mouseReleaseEvent(QMouseEvent*) {
    m_drawing = false;
}

QImage SignaturePad::signatureImage() const {
    if (m_empty || m_canvas.isNull()) return {};
    // Crop to the inked bounding box (+ small margin).
    const QImage img = m_canvas;
    int minX = img.width(), minY = img.height(), maxX = 0, maxY = 0;
    bool any = false;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(row[x]) > 8) {
                any = true;
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
            }
        }
    }
    if (!any) return {};
    const int m = 8;
    QRect box(QPoint(std::max(0, minX - m), std::max(0, minY - m)),
              QPoint(std::min(img.width() - 1, maxX + m), std::min(img.height() - 1, maxY + m)));
    return img.copy(box);
}

// ─────────────────────────────────────────────────────────────────────────────
// SignatureLibrary
// ─────────────────────────────────────────────────────────────────────────────

QString SignatureLibrary::dir() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir d(base + "/signatures");
    if (!d.exists()) d.mkpath(".");
    return d.absolutePath();
}

QStringList SignatureLibrary::savedFiles() {
    QDir d(dir());
    QStringList files;
    for (const QFileInfo& fi : d.entryInfoList({ "*.png" }, QDir::Files, QDir::Time))
        files << fi.absoluteFilePath();
    return files;
}

QString SignatureLibrary::save(const QImage& img) {
    const QString path = dir() + "/sig_"
        + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".png";
    img.save(path, "PNG");
    return path;
}

void SignatureLibrary::remove(const QString& path) {
    QFile::remove(path);
}

// ─────────────────────────────────────────────────────────────────────────────
// typed signature
// ─────────────────────────────────────────────────────────────────────────────

QImage typedSignatureImage(const QString& text, const QColor& color) {
    if (text.trimmed().isEmpty()) return {};
    // Prefer a script-like font if available, else italic serif.
    QFont font;
    const QStringList families = QFontDatabase::families();
    for (const QString& candidate : { "Segoe Script", "Brush Script MT",
                                      "Lucida Handwriting", "Freestyle Script" }) {
        if (families.contains(candidate)) { font.setFamily(candidate); break; }
    }
    if (font.family().isEmpty() || !families.contains(font.family())) {
        font.setFamily("Georgia");
        font.setItalic(true);
    }
    font.setPixelSize(56);

    QFontMetrics fm(font);
    const QRect bounds = fm.boundingRect(text);
    const int w = bounds.width() + 40, h = fm.height() + 24;
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(font);
    p.setPen(color);
    p.drawText(QRect(20, 12, w - 40, h - 24), Qt::AlignVCenter | Qt::AlignLeft, text);
    p.end();
    return img;
}

// ─────────────────────────────────────────────────────────────────────────────
// chooser dialog
// ─────────────────────────────────────────────────────────────────────────────

QImage runSignatureDialog(QWidget* parent, bool initials) {
    QDialog dlg(parent);
    dlg.setWindowTitle(initials ? QObject::tr("Add Initials") : QObject::tr("Add Signature"));
    dlg.setStyleSheet(ThemeManager::inputDialogStyleSheet());
    dlg.resize(520, 380);
    auto* v = new QVBoxLayout(&dlg);

    auto* tabs = new QTabWidget(&dlg);

    // ── Draw tab ────────────────────────────────────────────────────────
    auto* drawTab = new QWidget;
    auto* drawLayout = new QVBoxLayout(drawTab);
    auto* pad = new SignaturePad(drawTab);
    auto* clearBtn = new QPushButton(QObject::tr("Clear"), drawTab);
    QObject::connect(clearBtn, &QPushButton::clicked, pad, &SignaturePad::clear);
    drawLayout->addWidget(pad, 1);
    auto* drawTools = new QHBoxLayout;
    drawTools->addWidget(clearBtn);
    drawTools->addStretch();
    drawLayout->addLayout(drawTools);
    tabs->addTab(drawTab, QObject::tr("Draw"));

    // ── Type tab ────────────────────────────────────────────────────────
    auto* typeTab = new QWidget;
    auto* typeLayout = new QVBoxLayout(typeTab);
    auto* typeEdit = new QLineEdit(typeTab);
    typeEdit->setPlaceholderText(initials ? QObject::tr("Your initials")
                                          : QObject::tr("Your name"));
    auto* preview = new QLabel(typeTab);
    preview->setMinimumHeight(90);
    preview->setAlignment(Qt::AlignCenter);
    preview->setStyleSheet("background:#FFFFFF; border-radius:6px;");
    auto refreshPreview = [typeEdit, preview] {
        const QImage img = typedSignatureImage(typeEdit->text());
        preview->setPixmap(img.isNull() ? QPixmap()
                                        : QPixmap::fromImage(img).scaledToHeight(72, Qt::SmoothTransformation));
    };
    QObject::connect(typeEdit, &QLineEdit::textChanged, preview, [refreshPreview] { refreshPreview(); });
    typeLayout->addWidget(typeEdit);
    typeLayout->addWidget(preview, 1);
    tabs->addTab(typeTab, QObject::tr("Type"));

    // ── Saved tab ───────────────────────────────────────────────────────
    auto* savedTab = new QWidget;
    auto* savedLayout = new QVBoxLayout(savedTab);
    auto* savedList = new QListWidget(savedTab);
    savedList->setViewMode(QListView::IconMode);
    savedList->setIconSize(QSize(160, 60));
    savedList->setResizeMode(QListView::Adjust);
    auto reloadSaved = [savedList] {
        savedList->clear();
        for (const QString& path : SignatureLibrary::savedFiles()) {
            auto* it = new QListWidgetItem(QIcon(path), {}, savedList);
            it->setData(Qt::UserRole, path);
        }
    };
    reloadSaved();
    auto* delBtn = new QPushButton(QObject::tr("Delete Selected"), savedTab);
    QObject::connect(delBtn, &QPushButton::clicked, savedTab, [savedList, reloadSaved] {
        if (auto* it = savedList->currentItem()) {
            SignatureLibrary::remove(it->data(Qt::UserRole).toString());
            reloadSaved();
        }
    });
    savedLayout->addWidget(savedList, 1);
    savedLayout->addWidget(delBtn);
    tabs->addTab(savedTab, QObject::tr("Saved"));

    v->addWidget(tabs, 1);

    // ── import + save option + buttons ──────────────────────────────────
    auto* saveCheck = new QPushButton(QObject::tr("Import Image…"), &dlg);
    QImage importedImage;
    QObject::connect(saveCheck, &QPushButton::clicked, &dlg, [&] {
        const QString p = QFileDialog::getOpenFileName(&dlg, QObject::tr("Import Signature Image"),
            QString(), QObject::tr("Images (*.png *.jpg *.jpeg *.bmp)"));
        if (p.isEmpty()) return;
        importedImage = QImage(p);
        if (!importedImage.isNull()) dlg.accept();
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Place"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto* bottomRow = new QHBoxLayout;
    bottomRow->addWidget(saveCheck);
    bottomRow->addStretch();
    bottomRow->addWidget(buttons);
    v->addLayout(bottomRow);

    if (dlg.exec() != QDialog::Accepted) return {};

    if (!importedImage.isNull())
        return importedImage;

    QImage result;
    switch (tabs->currentIndex()) {
    case 0: result = pad->signatureImage(); break;
    case 1: result = typedSignatureImage(typeEdit->text()); break;
    case 2:
        if (auto* it = savedList->currentItem())
            result = QImage(it->data(Qt::UserRole).toString());
        return result;   // saved images aren't re-saved
    }
    // Persist freshly created signatures for reuse.
    if (!result.isNull()) SignatureLibrary::save(result);
    return result;
}

} // namespace NativeOffice::Pdf
