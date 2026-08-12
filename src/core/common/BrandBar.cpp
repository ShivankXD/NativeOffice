// ─────────────────────────────────────────────────────────────────────────────
// BrandBar.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "common/BrandBar.h"

#include "auth/AuthManager.h"
#include "theme/ThemeManager.h"

#include <QFont>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QPainter>

namespace NativeOffice {

BrandBar::BrandBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("brandBar");
    // 34 rather than the original 44. Every editor stacks this bar above its
    // ribbon, so ten pixels here is ten pixels of document area recovered in
    // every mode. Everything painted below is measured from height(), so the
    // bar stays composed at either size.
    setFixedHeight(34);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // The logo artwork is a full lockup (mark + wordmark + strapline); the
    // text is unreadable at tray size, so crop just the "N" mark out of it.
    // Fractions measured against the shipped 1254×1254 asset.
    const QPixmap full(QStringLiteral(":/assets/nativeoffice-logo.png"));
    if (!full.isNull()) {
        const QRect markRect(int(full.width()  * 0.205),
                             int(full.height() * 0.115),
                             int(full.width()  * 0.600),
                             int(full.height() * 0.555));
        m_mark = full.copy(markRect);
    }

    auto& auth = AuthManager::instance();
    m_premium  = auth.premiumActive();
    connect(&auth, &AuthManager::entitlementChanged,
            this, [this](bool on) { m_premium = on; update(); });
    connect(&ThemeManager::instance(), &ThemeManager::modeChanged,
            this, [this](ThemeMode) { update(); styleNameField(); });

    // ── On-screen rename field (hidden until a document sets its name) ────────
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setObjectName("brandNameEdit");
    m_nameEdit->setPlaceholderText(QStringLiteral("untitled"));
    m_nameEdit->setMaxLength(80);
    m_nameEdit->hide();
    m_extLabel = new QLabel(this);
    m_extLabel->setObjectName("brandExtLabel");
    m_extLabel->hide();
    connect(m_nameEdit, &QLineEdit::textEdited, this,
            [this](const QString& t) { emit docNameEdited(t); });
    styleNameField();
}

// The name lives in a rounded field; the extension sits just outside it, so
// the whole thing reads as "untitled.docx" with only "untitled" editable.
void BrandBar::setDocName(const QString& base, const QString& ext) {
    if (ext.isEmpty()) {                 // a tool (no document name)
        m_nameEdit->hide();
        m_extLabel->hide();
        return;
    }
    // Don't clobber the caret while the user is actively typing.
    if (!m_nameEdit->hasFocus() && m_nameEdit->text() != base)
        m_nameEdit->setText(base);
    m_extLabel->setText(ext);
    m_nameEdit->show();
    m_extLabel->show();
    layoutNameField();
}

void BrandBar::setAutoSaveStatus(const QString& text) {
    if (text == m_autoSave) return;      // repaint only when the wording changes
    m_autoSave = text;
    update();
}

void BrandBar::styleNameField() {
    const bool dark = ThemeManager::instance().isDark();
    m_nameEdit->setStyleSheet(QString(
        "QLineEdit#brandNameEdit { background:%1; border:1px solid %2; border-radius:7px;"
        " padding:2px 9px; color:%3; font:600 12px 'Segoe UI'; }"
        "QLineEdit#brandNameEdit:focus { border:1px solid #6D5BE8; }")
        .arg(dark ? "#151A24" : "#F4F5F8",
             dark ? "#2A3140" : "#DADDE4",
             dark ? "#E6E9F0" : "#1C2333"));
    m_extLabel->setStyleSheet(QString("QLabel#brandExtLabel { color:%1; font:600 12px 'Segoe UI'; }")
        .arg(dark ? "#7B8494" : "#9098A8"));
}

void BrandBar::layoutNameField() {
    if (m_nameEdit->isHidden()) return;
    const int h = 22;
    const int y = (height() - h) / 2;
    const int x = 152;               // just past the "NativeOffice" wordmark
    const int fw = 190;
    m_nameEdit->setGeometry(x, y, fw, h);
    const int ew = m_extLabel->sizeHint().width();
    m_extLabel->setGeometry(x + fw + 6, y, ew + 4, h);
}

void BrandBar::resizeEvent(QResizeEvent*) { layoutNameField(); }

void BrandBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const bool dark = ThemeManager::instance().isDark();
    const int  w = width(), h = height();

    p.fillRect(rect(), QColor(dark ? "#0D1117" : "#FFFFFF"));

    // ── Plan pill (far right) — measure first so decorations can avoid it ──
    QFont pillFont("Segoe UI");
    pillFont.setBold(true);
    pillFont.setPixelSize(11);
    pillFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
    const QString planText = m_premium ? QStringLiteral("PREMIUM")
                                       : QStringLiteral("FREE");
    const QFontMetrics pfm(pillFont);
    const qreal pillW = pfm.horizontalAdvance(planText) + 24;
    const qreal pillH = 19;
    const QRectF pill(w - 12 - pillW, (h - pillH) / 2.0, pillW, pillH);

    // ── Autosave status: measured here, painted after the art ───────────────
    // Only the geometry is worked out at this point. The clouds below are wide
    // ellipses drawn from `dx`, so they reach well past it and would cover the
    // text if it went down first; it is drawn last instead.
    QFont statusFont("Segoe UI");
    statusFont.setPixelSize(11);
    qreal statusLeft = pill.left();
    qreal statusW = 0;
    if (!m_autoSave.isEmpty()) {
        statusW = QFontMetrics(statusFont).horizontalAdvance(m_autoSave);
        statusLeft = pill.left() - 12 - statusW;
    }

    // ── Right-side decorations (soft clouds, dot grid, accents) ────────────
    p.setPen(Qt::NoPen);
    const qreal dx = statusLeft - 12;   // decorations end here
    p.setBrush(QColor(dark ? "#141C2E" : "#E2EAF8"));
    p.drawEllipse(QPointF(dx - 44, h + 8), 40, 26);
    p.drawEllipse(QPointF(dx, h + 10), 50, 32);
    p.setBrush(QColor(dark ? "#1A2540" : "#CFDCF4"));
    p.drawEllipse(QPointF(dx + 22, h + 6), 44, 28);

    p.setBrush(QColor(dark ? "#33405C" : "#C3C8D4"));
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            p.drawEllipse(QPointF(dx - 162 + c * 6.5, h / 2.0 - 6.5 + r * 6.5), 1.5, 1.5);

    p.setBrush(QColor("#3B82F6"));
    p.drawEllipse(QPointF(dx - 124, h / 2.0 - 7), 3.4, 3.4);
    p.setBrush(Qt::NoBrush);
    QPen ring(QColor("#34B6A7"));
    ring.setWidthF(1.7);
    p.setPen(ring);
    p.drawEllipse(QPointF(dx - 102, h / 2.0 + 4), 5.5, 5.5);

    // ── Plan pill on top of the art ─────────────────────────────────────────
    p.setFont(pillFont);
    if (m_premium) {
        QLinearGradient grad(pill.topLeft(), pill.bottomRight());
        grad.setColorAt(0.0, QColor("#F6C34C"));
        grad.setColorAt(1.0, QColor("#E8930C"));
        p.setPen(Qt::NoPen);
        p.setBrush(grad);
        p.drawRoundedRect(pill, pillH / 2, pillH / 2);
        p.setPen(QColor("#FFFFFF"));
    } else {
        p.setPen(QPen(QColor(dark ? "#2A3550" : "#D8DCE5"), 1));
        p.setBrush(QColor(dark ? "#12161F" : "#F7F8FA"));
        p.drawRoundedRect(pill, pillH / 2, pillH / 2);
        p.setPen(QColor(dark ? "#9AA4B8" : "#6B7280"));
    }
    p.drawText(pill, Qt::AlignCenter, planText);

    // Autosave status last, so it sits above the decorative art.
    if (!m_autoSave.isEmpty()) {
        p.setFont(statusFont);
        p.setPen(QColor(dark ? "#8A93A6" : "#7C8496"));
        p.drawText(QRectF(statusLeft, 0, statusW + 2, h),
                   Qt::AlignVCenter | Qt::AlignLeft, m_autoSave);
    }

    // ── Left: brand mark in a rounded white card ────────────────────────────
    const QRectF card(10, (h - 26) / 2.0, 26, 26);
    p.setPen(QPen(QColor(dark ? "#2A3140" : "#DADDE4"), 1));
    p.setBrush(Qt::white);
    p.drawRoundedRect(card, 7, 7);
    if (!m_mark.isNull()) {
        QRectF img = card.adjusted(2.5, 2.5, -2.5, -2.5);
        const qreal aspect = qreal(m_mark.width()) / qreal(m_mark.height());
        if (aspect > 1.0) {          // wider than tall: pin width, centre height
            const qreal ih = img.width() / aspect;
            img = QRectF(img.left(), img.center().y() - ih / 2, img.width(), ih);
        } else {
            const qreal iw = img.height() * aspect;
            img = QRectF(img.center().x() - iw / 2, img.top(), iw, img.height());
        }
        p.drawPixmap(img, m_mark, m_mark.rect());
    }

    // Divider between the mark and the wordmark.
    p.setPen(QPen(QColor(dark ? "#1B212C" : "#E6E8ED"), 1));
    p.drawLine(QPointF(46, 8), QPointF(46, h - 8));

    // Two-tone wordmark, echoing the logo art: "Native" ink + "Office" violet.
    QFont wf("Segoe UI");
    wf.setBold(true);
    wf.setPixelSize(14);
    p.setFont(wf);
    const QFontMetrics wfm(wf);
    const int tx = 56;
    p.setPen(QColor(dark ? "#E6E9F0" : "#1C2333"));
    p.drawText(QRectF(tx, 0, wfm.horizontalAdvance("Native") + 2, h),
               Qt::AlignVCenter | Qt::AlignLeft, "Native");
    p.setPen(QColor(dark ? "#9D8CFF" : "#6D5BE8"));
    p.drawText(QRectF(tx + wfm.horizontalAdvance("Native") + 1, 0,
                      wfm.horizontalAdvance("Office") + 4, h),
               Qt::AlignVCenter | Qt::AlignLeft, "Office");

    // Bottom hairline.
    p.setPen(QPen(QColor(dark ? "#1B212C" : "#E4E6EB"), 1));
    p.drawLine(0, h - 1, w, h - 1);
}

} // namespace NativeOffice
