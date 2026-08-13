#include "AiConsentDialog.h"

#include "ai/AiConsent.h"

#include <QGraphicsDropShadowEffect>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace NativeOffice {

namespace {

// One bullet: a small violet rule, a bold lead, then the explanation. Reads as
// a list without a bullet glyph, which at this size looks cleaner than a dot.
QWidget* point(const QString& lead, const QString& body, QWidget* parent) {
    auto* row = new QWidget(parent);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(12);

    auto* rule = new QWidget(row);
    rule->setFixedWidth(2);
    rule->setStyleSheet(QStringLiteral("background:#7C5CFF; border-radius:1px;"));

    auto* text = new QLabel(
        QStringLiteral("<span style='color:#E8EAF0; font-weight:600;'>%1</span>"
                       "<span style='color:#98A2B3;'> %2</span>")
            .arg(lead.toHtmlEscaped(), body.toHtmlEscaped()),
        row);
    text->setWordWrap(true);
    text->setTextFormat(Qt::RichText);
    text->setStyleSheet(QStringLiteral("font:13px 'Segoe UI'; background:transparent;"));

    h->addWidget(rule, 0);
    h->addWidget(text, 1);
    return row;
}

} // namespace

AiConsentDialog::AiConsentDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    setFixedWidth(560);

    auto* outer = new QVBoxLayout(this);
    // Room for the drop shadow to fall into; the card itself is painted inside.
    outer->setContentsMargins(26, 26, 26, 26);
    outer->addWidget(buildBody());

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(48);
    shadow->setColor(QColor(0, 0, 0, 190));
    shadow->setOffset(0, 14);
    setGraphicsEffect(shadow);
}

QWidget* AiConsentDialog::buildBody() {
    auto* body = new QWidget(this);
    body->setAttribute(Qt::WA_TranslucentBackground);
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(34, 30, 34, 26);
    v->setSpacing(0);

    // ── Mark ────────────────────────────────────────────────────────────────
    auto* mark = new QLabel(body);
    QPixmap px(QStringLiteral(":/assets/stasis-mark-64.png"));
    if (!px.isNull()) {
        px.setDevicePixelRatio(devicePixelRatioF());
        mark->setPixmap(px.scaledToHeight(int(42 * devicePixelRatioF()),
                                          Qt::SmoothTransformation));
    }
    mark->setAlignment(Qt::AlignCenter);
    mark->setStyleSheet(QStringLiteral("background:transparent;"));
    v->addWidget(mark, 0, Qt::AlignCenter);
    v->addSpacing(16);

    auto* title = new QLabel(QStringLiteral("Stasis needs the internet"), body);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral(
        "color:#F4F6FA; font:600 21px 'Segoe UI'; background:transparent;"));
    v->addWidget(title);
    v->addSpacing(8);

    auto* sub = new QLabel(
        QStringLiteral("Everything else in NativeOffice stays on your computer. "
                       "The assistant is the one part that does not, so here is "
                       "exactly what that means before you use it."),
        body);
    sub->setWordWrap(true);
    sub->setAlignment(Qt::AlignCenter);
    sub->setStyleSheet(QStringLiteral(
        "color:#9AA4B8; font:13px 'Segoe UI'; background:transparent;"));
    v->addWidget(sub);
    v->addSpacing(22);

    v->addWidget(point(QStringLiteral("Your prompts are sent to our server."),
                       QStringLiteral("What you type in the sidebar, and any files or images "
                                      "you attach to it, are transmitted so the model can "
                                      "answer. The model can read them."),
                       body));
    v->addSpacing(14);
    v->addWidget(point(QStringLiteral("Only what you send to the assistant leaves."),
                       QStringLiteral("Nothing is sent while you simply write, calculate or "
                                      "present. Your documents are not uploaded, scanned or "
                                      "synced in the background."),
                       body));
    v->addSpacing(14);
    v->addWidget(point(QStringLiteral("The rest of NativeOffice works fully offline."),
                       QStringLiteral("Once you are signed in, every other feature keeps "
                                      "working with no connection at all, exactly as "
                                      "promised."),
                       body));
    v->addSpacing(14);
    v->addWidget(point(QStringLiteral("Declining costs you only the assistant."),
                       QStringLiteral("The sidebar will not open and cannot be used, and the "
                                      "rest of the app is untouched. You can accept later by "
                                      "pressing Use AI again."),
                       body));

    v->addSpacing(26);

    // ── Buttons ─────────────────────────────────────────────────────────────
    auto* rowW = new QWidget(body);
    rowW->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* row = new QHBoxLayout(rowW);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(12);

    m_decline = new QPushButton(QStringLiteral("Decline"), rowW);
    m_decline->setCursor(Qt::PointingHandCursor);
    m_decline->setFixedHeight(42);
    m_decline->setStyleSheet(QStringLiteral(
        "QPushButton { color:#FF8A80; font:600 14px 'Segoe UI';"
        "  background:rgba(229,72,60,0.12); border:1px solid rgba(229,72,60,0.42);"
        "  border-radius:9px; padding:0 18px; }"
        "QPushButton:hover { background:rgba(229,72,60,0.22);"
        "  border-color:rgba(229,72,60,0.70); color:#FFB4AC; }"
        "QPushButton:pressed { background:rgba(229,72,60,0.30); }"));

    m_accept = new QPushButton(QStringLiteral("Accept and use Stasis"), rowW);
    m_accept->setCursor(Qt::PointingHandCursor);
    m_accept->setFixedHeight(42);
    m_accept->setDefault(true);
    m_accept->setStyleSheet(QStringLiteral(
        "QPushButton { color:#7BE6A8; font:600 14px 'Segoe UI';"
        "  background:rgba(34,197,94,0.14); border:1px solid rgba(34,197,94,0.45);"
        "  border-radius:9px; padding:0 22px; }"
        "QPushButton:hover { background:rgba(34,197,94,0.24);"
        "  border-color:rgba(34,197,94,0.72); color:#A7F3C6; }"
        "QPushButton:pressed { background:rgba(34,197,94,0.32); }"));

    row->addWidget(m_decline, 1);
    row->addWidget(m_accept, 2);
    v->addWidget(rowW);

    connect(m_accept, &QPushButton::clicked, this, [this] {
        AiConsent::recordAccepted();
        accept();
    });
    connect(m_decline, &QPushButton::clicked, this, [this] {
        AiConsent::recordDeclined();
        reject();
    });
    return body;
}

void AiConsentDialog::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF card = QRectF(rect()).adjusted(26, 26, -26, -26);
    QPainterPath path;
    path.addRoundedRect(card, 16, 16);

    // A very slight vertical lift so the card does not read as a flat slab.
    QLinearGradient g(card.topLeft(), card.bottomLeft());
    g.setColorAt(0.0, QColor(0x1A, 0x1F, 0x2B));
    g.setColorAt(1.0, QColor(0x12, 0x16, 0x1F));
    p.fillPath(path, g);

    p.setPen(QPen(QColor(255, 255, 255, 26), 1));
    p.drawPath(path);
}

void AiConsentDialog::keyPressEvent(QKeyEvent* e) {
    // Escape must not read as consent. Leaving it Unanswered means the notice
    // simply appears again next time, which is the intended behaviour.
    if (e->key() == Qt::Key_Escape) { reject(); return; }
    QDialog::keyPressEvent(e);
}

bool AiConsentDialog::runFor(QWidget* parent) {
    AiConsentDialog dlg(parent);
    return dlg.exec() == QDialog::Accepted && AiConsent::accepted();
}

} // namespace NativeOffice
