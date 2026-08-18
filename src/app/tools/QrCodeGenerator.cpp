// ─────────────────────────────────────────────────────────────────────────────
// QrCodeGenerator.cpp: see QrCodeGenerator.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "QrCodeGenerator.h"
#include "startscreen/HomeKit.h"
#include "startscreen/LucideIcons.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTextStream>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace NativeOffice {

namespace {

// Escapes the characters that terminate a field in the Wi-Fi / MECARD style
// payload grammar. Without this a password containing ';' silently truncates.
QString escapeMecard(const QString& in) {
    QString out;
    out.reserve(in.size() + 8);
    for (QChar c : in) {
        if (c == QLatin1Char('\\') || c == QLatin1Char(';')
            || c == QLatin1Char(',') || c == QLatin1Char(':') || c == QLatin1Char('"'))
            out += QLatin1Char('\\');
        out += c;
    }
    return out;
}

QString sectionTitle() { return QStringLiteral("color:%1;font:700 11px 'Segoe UI';"
                                               "letter-spacing:1px;background:transparent;"); }

} // namespace

QrCodeGeneratorWidget::QrCodeGeneratorWidget(QWidget* parent) : QWidget(parent) {
    setObjectName("qrTool");
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildSidePanel());
    root->addWidget(buildPreview(), 1);

    setStyleSheet(QString(R"(
        QWidget#qrTool { background:%1; }
        QWidget#qrSide { background:%2; border-right:1px solid %3; }
        QWidget#qrStage { background:%1; }
        QLineEdit, QPlainTextEdit, QComboBox, QSpinBox {
            background:%4; border:1px solid %3; border-radius:8px;
            color:%5; padding:7px 10px; font:13px 'Segoe UI'; selection-background-color:%6; }
        QLineEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QSpinBox:focus {
            border:1px solid %6; }
        QComboBox::drop-down { border:none; width:22px; }
        QComboBox::down-arrow { image:url(:/assets/chevron-down-light.png);
            width:10px; height:7px; }
        QToolButton#stepper { background:%4; border:1px solid %3; border-radius:7px;
            color:%5; font:600 15px 'Segoe UI'; }
        QToolButton#stepper:hover { background:%7; color:#FFFFFF; }
        QComboBox QAbstractItemView { background:%2; border:1px solid %3;
            color:%5; selection-background-color:%7; outline:none; }
        QCheckBox { color:%5; font:12px 'Segoe UI'; spacing:8px; }
        QCheckBox::indicator { width:16px; height:16px; border-radius:4px;
            border:1px solid %3; background:%4; }
        QCheckBox::indicator:checked { background:%6; border:1px solid %6;
            image:url(:/assets/check-white.png); }
        QToolButton#kindTab { background:%4; border:1px solid %3; border-radius:8px;
            color:%8; font:12px 'Segoe UI'; padding:7px 13px; }
        QToolButton#kindTab:hover { background:%7; }
        QToolButton#kindTab:checked { background:#232040; border:1px solid %6; color:#FFFFFF; }
        QToolButton#eccTab { background:%4; border:1px solid %3; border-radius:8px;
            color:%8; font:600 12px 'Segoe UI'; padding:6px 0; }
        QToolButton#eccTab:hover { background:%7; }
        QToolButton#eccTab:checked { background:#232040; border:1px solid %6; color:#FFFFFF; }
        QToolButton#swatch { border:1px solid %3; border-radius:8px; }
        QPushButton#primary { background:%6; border:none; border-radius:9px;
            color:#FFFFFF; font:600 13px 'Segoe UI'; padding:10px 18px; }
        QPushButton#primary:hover { background:%9; }
        QPushButton#ghost { background:%4; border:1px solid %3; border-radius:9px;
            color:%5; font:600 13px 'Segoe UI'; padding:10px 18px; }
        QPushButton#ghost:hover { background:%7; }
        QPushButton:disabled { color:%10; }
    )").arg(Home::kBg, Home::kPanel, Home::kBorder, Home::kPanelSoft, Home::kTextBody,
            Home::kAccent, Home::kPanelHover, Home::kMuted, Home::kAccentSoft, Home::kFaint));

    regenerate();
}

QWidget* QrCodeGeneratorWidget::buildSidePanel() {
    auto* side = new QWidget(this);
    side->setObjectName("qrSide");
    side->setFixedWidth(330);
    auto* v = new QVBoxLayout(side);
    v->setContentsMargins(20, 22, 20, 20);
    v->setSpacing(14);

    auto sectionLabel = [&](const QString& t) {
        auto* l = new QLabel(t.toUpper(), side);
        l->setStyleSheet(sectionTitle().arg(Home::kFaint));
        return l;
    };

    v->addWidget(sectionLabel(tr("Content")));

    // ── Kind tabs ───────────────────────────────────────────────────────────
    auto* tabsRow = new QWidget(side);
    auto* tl = new QHBoxLayout(tabsRow);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setSpacing(6);
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    const QStringList names = { tr("Text / URL"), tr("Wi-Fi"), tr("Email"),
                                tr("Phone"), tr("SMS") };
    for (int i = 0; i < names.size(); ++i) {
        auto* b = new QToolButton(tabsRow);
        b->setObjectName("kindTab");
        b->setText(names[i]);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        group->addButton(b, i);
        tl->addWidget(b);
        if (i == 2) { v->addWidget(tabsRow); tabsRow = new QWidget(side);
                      tl = new QHBoxLayout(tabsRow);
                      tl->setContentsMargins(0, 0, 0, 0); tl->setSpacing(6); }
    }
    tl->addStretch();
    v->addWidget(tabsRow);

    m_pages = new QStackedWidget(side);
    for (int k = TextUrl; k <= Sms; ++k) m_pages->addWidget(buildKindPage(Kind(k)));
    v->addWidget(m_pages);

    // A stack sizes itself to its tallest page, which left a tall empty gap
    // under the short ones. Follow the page that is actually showing.
    auto fitStack = [this] {
        if (QWidget* page = m_pages->currentWidget())
            m_pages->setFixedHeight(page->sizeHint().height());
    };
    connect(group, &QButtonGroup::idClicked, this, [this, fitStack](int id) {
        m_kind = Kind(id);
        m_pages->setCurrentIndex(id);
        fitStack();
        regenerate();
    });
    group->button(0)->setChecked(true);
    fitStack();

    // ── Error correction ────────────────────────────────────────────────────
    v->addSpacing(2);
    v->addWidget(sectionLabel(tr("Error correction")));
    auto* eccRow = new QWidget(side);
    auto* el = new QHBoxLayout(eccRow);
    el->setContentsMargins(0, 0, 0, 0);
    el->setSpacing(6);
    auto* eccGroup = new QButtonGroup(this);
    eccGroup->setExclusive(true);
    const QStringList eccNames = { QStringLiteral("L  7%"), QStringLiteral("M  15%"),
                                   QStringLiteral("Q  25%"), QStringLiteral("H  30%") };
    for (int i = 0; i < eccNames.size(); ++i) {
        auto* b = new QToolButton(eccRow);
        b->setObjectName("eccTab");
        b->setText(eccNames[i]);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tr("Higher levels survive more damage but make a denser code."));
        eccGroup->addButton(b, i);
        el->addWidget(b, 1);
    }
    v->addWidget(eccRow);
    connect(eccGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_ecc = Qr::Ecc(id);
        regenerate();
    });
    eccGroup->button(1)->setChecked(true);

    // ── Appearance ──────────────────────────────────────────────────────────
    v->addSpacing(2);
    v->addWidget(sectionLabel(tr("Appearance")));

    auto swatchRow = [&](const QString& caption, bool foreground, QToolButton** out) {
        auto* row = new QWidget(side);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(10);
        h->addWidget(heading(caption, 12, Home::kTextBody, false, row));
        h->addStretch();
        auto* sw = new QToolButton(row);
        sw->setObjectName("swatch");
        sw->setCursor(Qt::PointingHandCursor);
        sw->setFixedSize(58, 26);
        *out = sw;
        connect(sw, &QToolButton::clicked, this, [this, foreground] { pickColor(foreground); });
        h->addWidget(sw);
        v->addWidget(row);
    };
    swatchRow(tr("Code colour"), true, &m_fgSwatch);
    swatchRow(tr("Background"), false, &m_bgSwatch);

    auto* sizeRow = new QWidget(side);
    auto* sl = new QHBoxLayout(sizeRow);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->setSpacing(10);
    sl->addWidget(heading(tr("Module size"), 12, Home::kTextBody, false, sizeRow));
    sl->addStretch();
    // Explicit steppers rather than the native spin buttons: the shipped
    // spin arrows are dark artwork for the light theme and vanish on this panel.
    auto stepper = [&](const QString& glyph, int delta) {
        auto* b = new QToolButton(sizeRow);
        b->setObjectName("stepper");
        b->setText(glyph);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(28, 28);
        connect(b, &QToolButton::clicked, this, [this, delta] {
            m_modulePx->setValue(m_modulePx->value() + delta);
        });
        return b;
    };
    sl->addWidget(stepper(QStringLiteral("−"), -1));

    m_modulePx = new QSpinBox(sizeRow);
    m_modulePx->setRange(2, 40);
    m_modulePx->setValue(10);
    m_modulePx->setSuffix(QStringLiteral(" px"));
    m_modulePx->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_modulePx->setAlignment(Qt::AlignCenter);
    m_modulePx->setFixedWidth(72);
    connect(m_modulePx, &QSpinBox::valueChanged, this, [this] { regenerate(); });
    sl->addWidget(m_modulePx);
    sl->addWidget(stepper(QStringLiteral("+"), +1));
    v->addWidget(sizeRow);

    m_quietZone = new QCheckBox(tr("Include quiet zone border"), side);
    m_quietZone->setChecked(true);
    m_quietZone->setToolTip(tr("Scanners need clear space around a code. Leave this on "
                               "unless you are placing it on an already-blank area."));
    connect(m_quietZone, &QCheckBox::toggled, this, [this] { regenerate(); });
    v->addWidget(m_quietZone);

    v->addStretch();

    // Paint the initial swatches.
    auto paintSwatch = [](QToolButton* b, const QColor& c) {
        b->setStyleSheet(QString("QToolButton#swatch { background:%1; border:1px solid %2;"
                                 "border-radius:8px; }").arg(c.name(), Home::kBorder));
    };
    paintSwatch(m_fgSwatch, m_fg);
    paintSwatch(m_bgSwatch, m_bg);

    return side;
}

QWidget* QrCodeGeneratorWidget::buildKindPage(Kind kind) {
    auto* page = new QWidget(this);
    auto* form = new QVBoxLayout(page);
    form->setContentsMargins(0, 4, 0, 0);
    form->setSpacing(9);

    auto field = [&](const QString& caption, QLineEdit** out,
                     const QString& placeholder = QString()) {
        form->addWidget(heading(caption, 11, Home::kMuted, false, page));
        auto* e = new QLineEdit(page);
        e->setPlaceholderText(placeholder);
        connect(e, &QLineEdit::textChanged, this, [this] { regenerate(); });
        *out = e;
        form->addWidget(e);
    };

    switch (kind) {
    case TextUrl:
        form->addWidget(heading(tr("Text or link"), 11, Home::kMuted, false, page));
        m_text = new QPlainTextEdit(page);
        m_text->setPlainText(QStringLiteral("https://nativeoffice.online"));
        m_text->setFixedHeight(96);
        connect(m_text, &QPlainTextEdit::textChanged, this, [this] { regenerate(); });
        form->addWidget(m_text);
        break;
    case WiFi: {
        field(tr("Network name (SSID)"), &m_ssid, QStringLiteral("MyNetwork"));
        field(tr("Password"), &m_wifiPw);
        form->addWidget(heading(tr("Security"), 11, Home::kMuted, false, page));
        m_wifiSec = new QComboBox(page);
        m_wifiSec->addItems({ QStringLiteral("WPA / WPA2"), QStringLiteral("WEP"),
                              tr("None (open)") });
        connect(m_wifiSec, &QComboBox::currentIndexChanged, this, [this] { regenerate(); });
        form->addWidget(m_wifiSec);
        m_wifiHidden = new QCheckBox(tr("Hidden network"), page);
        connect(m_wifiHidden, &QCheckBox::toggled, this, [this] { regenerate(); });
        form->addWidget(m_wifiHidden);
        break;
    }
    case Email:
        field(tr("To"), &m_mailTo, QStringLiteral("someone@example.com"));
        field(tr("Subject"), &m_mailSub);
        field(tr("Message"), &m_mailBody);
        break;
    case Phone:
        field(tr("Phone number"), &m_phone, QStringLiteral("+1 555 0100"));
        break;
    case Sms:
        field(tr("Phone number"), &m_smsTo, QStringLiteral("+1 555 0100"));
        field(tr("Message"), &m_smsBody);
        break;
    }
    form->addStretch();
    return page;
}

QWidget* QrCodeGeneratorWidget::buildPreview() {
    auto* stage = new QWidget(this);
    stage->setObjectName("qrStage");
    auto* v = new QVBoxLayout(stage);
    v->setContentsMargins(30, 26, 30, 24);
    v->setSpacing(16);

    auto* head = new QVBoxLayout();
    head->setSpacing(3);
    head->addWidget(heading(tr("QR Code Generator"), 20, Home::kText, true, stage));
    head->addWidget(heading(tr("Everything is generated on this computer. Nothing is uploaded."),
                            12, Home::kMuted, false, stage));
    v->addLayout(head);

    v->addStretch();
    m_preview = new QLabel(stage);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumSize(300, 300);
    v->addWidget(m_preview, 0, Qt::AlignCenter);

    m_caption = heading(QString(), 12, Home::kMuted, false, stage);
    m_caption->setAlignment(Qt::AlignCenter);
    v->addWidget(m_caption, 0, Qt::AlignCenter);
    v->addStretch();

    auto* actions = new QHBoxLayout();
    actions->setSpacing(10);
    actions->addStretch();
    m_savePng = new QPushButton(tr("Save PNG"), stage);
    m_savePng->setObjectName("primary");
    m_savePng->setCursor(Qt::PointingHandCursor);
    connect(m_savePng, &QPushButton::clicked, this, &QrCodeGeneratorWidget::savePng);
    actions->addWidget(m_savePng);

    m_saveSvg = new QPushButton(tr("Save SVG"), stage);
    m_saveSvg->setObjectName("ghost");
    m_saveSvg->setCursor(Qt::PointingHandCursor);
    connect(m_saveSvg, &QPushButton::clicked, this, &QrCodeGeneratorWidget::saveSvg);
    actions->addWidget(m_saveSvg);

    m_copyBtn = new QPushButton(tr("Copy image"), stage);
    m_copyBtn->setObjectName("ghost");
    m_copyBtn->setCursor(Qt::PointingHandCursor);
    connect(m_copyBtn, &QPushButton::clicked, this, &QrCodeGeneratorWidget::copyImage);
    actions->addWidget(m_copyBtn);
    actions->addStretch();
    v->addLayout(actions);

    return stage;
}

QString QrCodeGeneratorWidget::payload() const {
    switch (m_kind) {
    case TextUrl:
        return m_text ? m_text->toPlainText() : QString();
    case WiFi: {
        if (!m_ssid || m_ssid->text().isEmpty()) return {};
        const QString type = m_wifiSec->currentIndex() == 0 ? QStringLiteral("WPA")
                           : m_wifiSec->currentIndex() == 1 ? QStringLiteral("WEP")
                                                            : QStringLiteral("nopass");
        QString out = QStringLiteral("WIFI:T:%1;S:%2;")
                          .arg(type, escapeMecard(m_ssid->text()));
        if (m_wifiSec->currentIndex() != 2 && !m_wifiPw->text().isEmpty())
            out += QStringLiteral("P:%1;").arg(escapeMecard(m_wifiPw->text()));
        if (m_wifiHidden->isChecked()) out += QStringLiteral("H:true;");
        return out + QLatin1Char(';');
    }
    case Email: {
        if (!m_mailTo || m_mailTo->text().isEmpty()) return {};
        QString out = QStringLiteral("mailto:") + m_mailTo->text();
        QStringList params;
        if (!m_mailSub->text().isEmpty())
            params << QStringLiteral("subject=") + QString::fromUtf8(
                          QUrl::toPercentEncoding(m_mailSub->text()));
        if (!m_mailBody->text().isEmpty())
            params << QStringLiteral("body=") + QString::fromUtf8(
                          QUrl::toPercentEncoding(m_mailBody->text()));
        if (!params.isEmpty()) out += QLatin1Char('?') + params.join(QLatin1Char('&'));
        return out;
    }
    case Phone:
        return (m_phone && !m_phone->text().isEmpty())
                   ? QStringLiteral("tel:") + m_phone->text().simplified().remove(QLatin1Char(' '))
                   : QString();
    case Sms:
        if (!m_smsTo || m_smsTo->text().isEmpty()) return {};
        return QStringLiteral("SMSTO:%1:%2")
            .arg(m_smsTo->text().simplified().remove(QLatin1Char(' ')), m_smsBody->text());
    }
    return {};
}

QImage QrCodeGeneratorWidget::render(int modulePx) const {
    if (!m_code.isValid()) return {};
    const int quiet = m_quietZone->isChecked() ? 4 : 0;
    const int side = (m_code.size + quiet * 2) * modulePx;
    QImage img(side, side, QImage::Format_ARGB32);
    img.fill(m_bg);

    QPainter p(&img);
    p.setPen(Qt::NoPen);
    p.setBrush(m_fg);
    for (int y = 0; y < m_code.size; ++y)
        for (int x = 0; x < m_code.size; ++x)
            if (m_code.at(x, y))
                p.drawRect((x + quiet) * modulePx, (y + quiet) * modulePx,
                           modulePx, modulePx);
    p.end();
    return img;
}

void QrCodeGeneratorWidget::regenerate() {
    const QString text = payload();
    m_code = text.isEmpty() ? Qr::Code()
                            : Qr::encode(text.toUtf8(), m_ecc);

    const bool ok = m_code.isValid();
    m_savePng->setEnabled(ok);
    m_saveSvg->setEnabled(ok);
    m_copyBtn->setEnabled(ok);

    if (!ok) {
        m_preview->setPixmap({});
        m_preview->setText(text.isEmpty()
            ? tr("Fill in the fields on the left\nand your code appears here.")
            : tr("That is too much data for one QR code.\n"
                 "Shorten it, or drop the error-correction level."));
        m_preview->setStyleSheet(QString("color:%1;font:13px 'Segoe UI';"
                                         "background:%2;border:1px dashed %3;"
                                         "border-radius:14px;padding:40px;")
                                     .arg(Home::kMuted, Home::kPanel, Home::kBorder));
        m_caption->setText(text.isEmpty()
            ? QString()
            : tr("Limit at this level: %1 characters")
                  .arg(Qr::capacityBytes(m_ecc)));
        return;
    }

    // Preview at a size that fits the stage regardless of the export setting.
    const int quiet = m_quietZone->isChecked() ? 4 : 0;
    const int total = m_code.size + quiet * 2;
    const int previewModule = qMax(1, 360 / total);
    const QImage img = render(previewModule);
    m_preview->setStyleSheet(QString("background:%1;border:1px solid %2;border-radius:14px;"
                                     "padding:14px;").arg(Home::kPanel, Home::kBorder));
    m_preview->setText(QString());
    m_preview->setPixmap(QPixmap::fromImage(img));

    const int exportSide = total * m_modulePx->value();
    m_caption->setText(tr("Version %1  ·  %2 x %2 modules  ·  exports at %3 x %3 px")
                           .arg(m_code.version).arg(m_code.size).arg(exportSide));
}

void QrCodeGeneratorWidget::pickColor(bool foreground) {
    const QColor start = foreground ? m_fg : m_bg;
    const QColor c = QColorDialog::getColor(start, this,
                                            foreground ? tr("Code colour") : tr("Background"),
                                            QColorDialog::ShowAlphaChannel);
    if (!c.isValid()) return;
    (foreground ? m_fg : m_bg) = c;
    QToolButton* b = foreground ? m_fgSwatch : m_bgSwatch;
    b->setStyleSheet(QString("QToolButton#swatch { background:%1; border:1px solid %2;"
                             "border-radius:8px; }").arg(c.name(), Home::kBorder));
    regenerate();
}

void QrCodeGeneratorWidget::savePng() {
    const QImage img = render(m_modulePx->value());
    if (img.isNull()) return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save QR Code"), dir + QStringLiteral("/qr-code.png"),
        tr("PNG image (*.png)"));
    if (path.isEmpty()) return;
    if (!img.save(path, "PNG"))
        QMessageBox::warning(this, tr("Save failed"),
                             tr("Could not write %1.").arg(path));
}

void QrCodeGeneratorWidget::saveSvg() {
    if (!m_code.isValid()) return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save QR Code"), dir + QStringLiteral("/qr-code.svg"),
        tr("SVG image (*.svg)"));
    if (path.isEmpty()) return;

    const int quiet = m_quietZone->isChecked() ? 4 : 0;
    const int total = m_code.size + quiet * 2;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Save failed"),
                             tr("Could not write %1.").arg(path));
        return;
    }
    QTextStream out(&f);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << total
        << ' ' << total << "\" shape-rendering=\"crispEdges\">\n"
        << "  <rect width=\"" << total << "\" height=\"" << total
        << "\" fill=\"" << m_bg.name() << "\"/>\n"
        << "  <path fill=\"" << m_fg.name() << "\" d=\"";
    for (int y = 0; y < m_code.size; ++y)
        for (int x = 0; x < m_code.size; ++x)
            if (m_code.at(x, y))
                out << 'M' << (x + quiet) << ',' << (y + quiet) << "h1v1h-1z";
    out << "\"/>\n</svg>\n";
    f.close();
}

void QrCodeGeneratorWidget::copyImage() {
    const QImage img = render(m_modulePx->value());
    if (img.isNull()) return;
    QApplication::clipboard()->setImage(img);
}

} // namespace NativeOffice
