#include "AiSidebar.h"

#include "AiSourcesStrip.h"
#include "AiToast.h"
#include "ai/AiChatStore.h"
#include "ai/AiDocMarker.h"
#include "ai/AiStreamTarget.h"
#include "ai/AiDocumentAgent.h"
#include "ai/AiQuota.h"
#include "ai/StasisClient.h"

#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

namespace NativeOffice {

namespace {

constexpr int kHeaderHeight = 40;
// How far down from the top the pointer still counts as "at the top of the
// sidebar" for the purpose of revealing the close button.
constexpr int kCloseRevealBand = 56;

const char* kPanelBg   = "#0F131B";
const char* kPanelEdge = "rgba(255,255,255,0.07)";

// The card a user turn sits on. Assistant turns get none: the reply is the
// substance of the panel and putting it in a grey slab of its own makes a
// column of them read as a form rather than as an answer. Only the shorter of
// the two speakers is boxed, which is what separates them.
class BubbleCard : public QWidget {
public:
    BubbleCard(bool user, QWidget* parent) : QWidget(parent), m_user(user) {}

protected:
    void paintEvent(QPaintEvent*) override {
        if (!m_user) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        // Square off the bottom right corner. It points the bubble at its
        // author, and it is the whole reason a chat reads as a conversation
        // rather than as a stack of identical panels.
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath path;
        path.moveTo(r.left() + 14, r.top());
        path.lineTo(r.right() - 14, r.top());
        path.quadTo(r.right(), r.top(), r.right(), r.top() + 14);
        path.lineTo(r.right(), r.bottom() - 4);
        path.quadTo(r.right(), r.bottom(), r.right() - 4, r.bottom());
        path.lineTo(r.left() + 14, r.bottom());
        path.quadTo(r.left(), r.bottom(), r.left(), r.bottom() - 14);
        path.lineTo(r.left(), r.top() + 14);
        path.quadTo(r.left(), r.top(), r.left() + 14, r.top());
        p.fillPath(path, QColor(0x6D, 0x4E, 0xF2, 56));
        p.setPen(QPen(QColor(0x8B, 0x74, 0xFF, 80), 1));
        p.drawPath(path);
    }

private:
    bool m_user { false };
};

// One turn in the transcript. A user turn is a card pushed to the right and
// held to a fraction of the panel, so a short question does not stretch into a
// full-width banner; an assistant turn runs the full width as plain text.
class Bubble : public QWidget {
public:
    Bubble(const QString& text, bool fromUser, QWidget* parent)
        : QWidget(parent), m_user(fromUser)
    {
        auto* h = new QHBoxLayout(this);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(0);

        m_card = new BubbleCard(fromUser, this);
        auto* v = new QVBoxLayout(m_card);
        v->setContentsMargins(fromUser ? 14 : 2, fromUser ? 10 : 1,
                              fromUser ? 14 : 2, fromUser ? 10 : 1);
        v->setSpacing(0);

        m_label = new QLabel(text, m_card);
        m_label->setWordWrap(true);
        m_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_label->setStyleSheet(QStringLiteral(
            "color:%1; font:%2 13.5px 'Segoe UI'; background:transparent;")
            .arg(fromUser ? QStringLiteral("#F2F0FF") : QStringLiteral("#D9DEE9"),
                 fromUser ? QStringLiteral("500") : QStringLiteral("400")));
        v->addWidget(m_label);

        if (fromUser) { h->addStretch(1); h->addWidget(m_card, 0); }
        else          { h->addWidget(m_card, 1); }
    }

    QLabel* label() const { return m_label; }

protected:
    void resizeEvent(QResizeEvent* e) override {
        QWidget::resizeEvent(e);
        // A question of three words should be three words wide. Without a
        // ceiling the card grows to whatever the text wants, and a long one
        // fills the panel edge to edge and stops looking addressed to anyone.
        if (m_user) m_card->setMaximumWidth(qMax(120, int(width() * 0.84)));
    }

private:
    QLabel*      m_label { nullptr };
    BubbleCard*  m_card  { nullptr };
    bool         m_user  { false };
};

// The document marker rule lives in AiDocMarker.h so it can be tested without a
// running panel. See that file for why it is a line match rather than a
// startsWith on the whole reply.
const QLatin1String kDocMarker = aiDocMarker();

using NativeOffice::aiDocMarkerAt;
using NativeOffice::aiIsDocumentReply;

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

AiSidebar::AiSidebar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("aiSidebar"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("#aiSidebar { background:%1; border-left:1px solid %2; }")
                      .arg(QLatin1String(kPanelBg), QLatin1String(kPanelEdge)));
    setMinimumWidth(300);

    m_client = new StasisClient(this);
    m_chats  = new AiChatStore(this);
    m_agent  = new AiDocumentAgent(this);

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    v->addWidget(buildHeader(), 0);
    v->addWidget(buildChatArea(), 1);

    m_toast = new AiToast(this);
    auto* toastWrap = new QWidget(this);
    auto* tw = new QVBoxLayout(toastWrap);
    tw->setContentsMargins(12, 0, 12, 8);
    tw->setSpacing(0);
    tw->addWidget(m_toast);
    v->addWidget(toastWrap, 0);

    v->addWidget(buildComposer(), 0);

    // The pointer has to be watched at the window level: the close button only
    // appears when the cursor is near the top of the panel, and children eat
    // the move events that would otherwise tell us where it is.
    setMouseTracking(true);
    installEventFilter(this);

    connect(m_client, &StasisClient::delta, this, [this](const QString& chunk) {
        m_streamAccum += chunk;

        // Once the reply is already going into the page, every further chunk
        // goes straight there. Nothing is staged in the chat on the way.
        if (m_streamToDoc) {
            if (m_streamToDeck) m_deckTarget->aiFeed(chunk);
            else                m_agent->feed(chunk);
            return;
        }
        if (m_streamDecided) {
            if (m_streamTarget) { m_streamTarget->setText(m_streamAccum); scrollToBottom(); }
            return;
        }

        // Undecided. The marker can be preceded by the model explaining what it
        // is about to build, so this cannot commit to "conversation" the moment
        // the first character is not a bracket. Instead the preamble is shown
        // as it streams, and the switch happens if and when a marker line
        // arrives; a reply that ends without one was simply conversation.
        const int at = aiDocMarkerAt(m_streamAccum);
        if (at < 0) {
            // No marker yet. Show what has arrived, minus a partial marker that
            // may still be coming, so the reader never sees "[[DOCU" flash.
            QString shown = m_streamAccum;
            const int tailStart = qMax(0, shown.size() - kDocMarker.size());
            const int brk = shown.indexOf(kDocMarker.left(1), tailStart);
            if (brk >= 0 && kDocMarker.left(shown.size() - brk) == shown.mid(brk))
                shown = shown.left(brk);
            if (m_streamTarget) { m_streamTarget->setText(shown); scrollToBottom(); }
            return;
        }

        const int nl = m_streamAccum.indexOf(QLatin1Char('\n'), at);
        if (nl < 0) return;                           // marker line not finished

        m_streamDecided = true;
        const QString preamble = m_streamAccum.left(at).trimmed();
        const QString body     = m_streamAccum.mid(nl + 1);

        if (capabilityFor(m_mode) == AiCapability::Edit
            && (m_docTarget || m_deckTarget)) {
            m_streamToDoc = true;
            m_streamToDeck = m_deckTarget && !m_docTarget;
            if (m_streamToDeck) { m_deckTarget->aiBegin(); m_deckTarget->aiFeed(body); }
            else                { m_agent->beginLive(m_docTarget); m_agent->feed(body); }
            if (m_streamTarget) {
                // Keep what the model said before the marker: it is usually the
                // reason it built what it built, and it is the only part of the
                // reply the reader can still see once the rest is in the file.
                const QString note = QStringLiteral("Writing into your %1 document...")
                                         .arg(modeName(m_mode));
                m_streamTarget->setText(preamble.isEmpty() ? note
                                                           : preamble + "\n\n" + note);
            }
        } else if (m_streamTarget) {
            // Marked as content somewhere it cannot be written; show it.
            m_streamTarget->setText(preamble.isEmpty() ? body
                                                       : preamble + "\n\n" + body);
        }
        scrollToBottom();
    });
    connect(m_client, &StasisClient::sources, this,
            [this](const QVector<AiSource>& list) {
        if (m_strip) m_strip->setSources(list);
    });
    connect(m_client, &StasisClient::finished, this,
            [this](bool ok, const QString& full, const QString& error) {
        setWorking(false);
        if (!ok) {
            if (m_streamTarget && m_streamTarget->text().isEmpty())
                m_streamTarget->setText(QStringLiteral("Stasis could not answer that."));
            m_toast->post(error.isEmpty()
                              ? QStringLiteral("Stasis could not be reached.")
                              : error,
                          AiToast::Tone::Warning);
        } else {
            AiMessage a;
            a.role = AiMessage::Role::Assistant;
            a.text = full;
            a.at   = QDateTime::currentDateTime();
            m_history.append(a);

            // The stream already decided where this reply was going and put it
            // there as it arrived. All that is left is to close it off.
            if (m_streamToDeck && m_deckTarget) {
                m_deckTarget->aiEnd();
                const int written = m_deckTarget->aiCharactersWritten();
                if (written > 0) {
                    AiQuota::recordGeneration(written);
                    showRollback(false);
                    if (m_streamTarget)
                        m_streamTarget->setText(
                            QStringLiteral("Built into your presentation."));
                } else if (m_streamTarget) {
                    // Nothing reached the deck. Saying so is the whole point:
                    // the alternative is an empty bubble sitting under the
                    // request, which reads as the assistant ignoring it, and
                    // the count of remaining generations going down anyway.
                    m_streamTarget->setText(
                        QStringLiteral("Nothing came back that time. Send it again."));
                }
                refreshQuotaLabel();
            } else if (m_streamToDoc) {
                m_agent->endLive();
                if (m_streamTarget)
                    m_streamTarget->setText(QStringLiteral("Written into your %1 document.")
                                                .arg(modeName(m_mode)));
            } else if (m_streamTarget) {
                QString docContent, preamble;
                // A reply so short it finished before the marker line could be
                // judged still needs the marker taken off, and whatever the
                // model said ahead of it kept.
                QString shown = full;
                if (aiIsDocumentReply(full, &docContent, &preamble))
                    shown = preamble.isEmpty() ? docContent
                                               : preamble + "\n\n" + docContent;
                // An empty reply gets said out loud rather than left as a blank
                // bubble under the question.
                m_streamTarget->setText(shown.trimmed().isEmpty()
                    ? QStringLiteral("Nothing came back that time. Send it again.")
                    : shown);
            }
        }
        m_streamTarget = nullptr;
        refreshQuotaLabel();
        scrollToBottom();
    });

    connect(m_agent, &AiDocumentAgent::finished, this, [this](int chars) {
        // The server counts characters from the stream it produced and is the
        // tally that decides; this keeps the local mirror in step so the
        // sidebar's "generations left" is right without waiting for a refresh.
        //
        // A turn that wrote nothing spends nothing. The server already works
        // this way, and counting it here anyway is what told the user two of
        // their generations had gone on two replies that never arrived.
        if (chars > 0) {
            AiQuota::recordGeneration(chars);
            showRollback(false);
        }
        refreshQuotaLabel();
    });

    refreshQuotaLabel();
    refreshHeroVisibility();
}

// ── header ───────────────────────────────────────────────────────────────────

QWidget* AiSidebar::buildHeader() {
    m_header = new QWidget(this);
    m_header->setFixedHeight(kHeaderHeight);
    m_header->setAttribute(Qt::WA_StyledBackground, true);
    m_header->setObjectName(QStringLiteral("aiHeader"));
    // A hairline under the header. Without it the panel is one flat field from
    // the title to the composer and nothing looks deliberate.
    m_header->setStyleSheet(QStringLiteral(
        "#aiHeader { background:transparent; border-bottom:1px solid %1; }")
        .arg(QLatin1String(kPanelEdge)));

    auto* h = new QHBoxLayout(m_header);
    h->setContentsMargins(14, 0, 0, 0);
    h->setSpacing(8);

    auto* mark = new QLabel(m_header);
    QPixmap px(QStringLiteral(":/assets/stasis-mark-32.png"));
    if (!px.isNull())
        mark->setPixmap(px.scaledToHeight(16, Qt::SmoothTransformation));
    mark->setStyleSheet(QStringLiteral("background:transparent;"));

    auto* name = new QLabel(QStringLiteral("Stasis"), m_header);
    name->setStyleSheet(QStringLiteral(
        "color:#C9CFDB; font:600 12.5px 'Segoe UI'; background:transparent;"));

    // "In Writer". Sits above the chat and changes as the user moves between
    // tabs, so the panel never silently acts on a surface you are not looking at.
    m_modeChip = new QLabel(m_header);
    // Fixed height, or the row stretches it to the full header and a small
    // label becomes a button-sized slab next to the product name.
    m_modeChip->setFixedHeight(20);
    m_modeChip->setAlignment(Qt::AlignCenter);
    m_modeChip->setStyleSheet(QStringLiteral(
        "color:#AFA3FF; font:600 10.5px 'Segoe UI';"
        "background:rgba(124,92,255,0.14); border:none;"
        "border-radius:7px; padding:0 9px;"));

    // Matches the shell's own close button: transparent until hovered, then the
    // Windows close red.
    m_close = new QPushButton(QStringLiteral("✕"), m_header);
    m_close->setCursor(Qt::ArrowCursor);
    m_close->setFixedSize(38, kHeaderHeight);
    m_close->setFocusPolicy(Qt::NoFocus);
    m_close->setToolTip(QStringLiteral("Close Stasis"));
    m_close->setStyleSheet(QStringLiteral(
        "QPushButton { border:none; background:transparent; color:#C9CFDB; font:11px 'Segoe UI'; }"
        "QPushButton:hover { background:#E81123; color:#FFFFFF; }"
        "QPushButton:pressed { background:#C50F1F; color:#FFFFFF; }"));
    connect(m_close, &QPushButton::clicked, this, &AiSidebar::closeRequested);

    m_closeFade = new QGraphicsOpacityEffect(m_close);
    m_closeFade->setOpacity(0.0);
    m_close->setGraphicsEffect(m_closeFade);
    m_closeAnim = new QPropertyAnimation(m_closeFade, "opacity", this);
    m_closeAnim->setDuration(130);

    h->addWidget(mark, 0);
    h->addWidget(name, 0);
    h->addSpacing(4);
    h->addWidget(m_modeChip, 0);
    h->addStretch(1);
    h->addWidget(m_close, 0);
    return m_header;
}

// ── chat ─────────────────────────────────────────────────────────────────────

QWidget* AiSidebar::buildHero() {
    // Shown while the transcript is empty: the mark, the attribution, and one
    // line saying what the panel will do where it currently is.
    m_hero = new QWidget(this);
    m_hero->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* v = new QVBoxLayout(m_hero);
    v->setContentsMargins(24, 24, 24, 24);
    v->setSpacing(0);
    v->addStretch(1);

    auto* mark = new QLabel(m_hero);
    QPixmap px(QStringLiteral(":/assets/stasis-mark-256.png"));
    if (!px.isNull())
        mark->setPixmap(px.scaledToHeight(96, Qt::SmoothTransformation));
    mark->setAlignment(Qt::AlignCenter);
    mark->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* markFade = new QGraphicsOpacityEffect(mark);
    markFade->setOpacity(0.92);
    mark->setGraphicsEffect(markFade);
    v->addWidget(mark, 0, Qt::AlignCenter);

    v->addSpacing(18);
    auto* powered = new QLabel(QStringLiteral("Powered by Stasis"), m_hero);
    powered->setAlignment(Qt::AlignCenter);
    powered->setStyleSheet(QStringLiteral(
        "color:#E6E9F0; font:600 15px 'Segoe UI'; letter-spacing:0.4px;"
        "background:transparent;"));
    v->addWidget(powered);

    v->addSpacing(6);
    auto* hint = new QLabel(m_hero);
    hint->setObjectName(QStringLiteral("heroHint"));
    hint->setAlignment(Qt::AlignCenter);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral(
        "color:#79839A; font:12px 'Segoe UI'; background:transparent;"));
    v->addWidget(hint);

    v->addStretch(1);
    return m_hero;
}

QWidget* AiSidebar::buildChatArea() {
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral(
        "QScrollArea { background:transparent; border:none; }"
        "QScrollBar:vertical { width:8px; background:transparent; margin:0; }"
        "QScrollBar::handle:vertical { background:#333C4D; border-radius:4px; min-height:30px; }"
        "QScrollBar::handle:vertical:hover { background:#48536A; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }"));
    m_scroll->viewport()->setStyleSheet(QStringLiteral("background:transparent;"));

    m_chatBody = new QWidget(m_scroll);
    m_chatBody->setStyleSheet(QStringLiteral("background:transparent;"));
    m_chatLay = new QVBoxLayout(m_chatBody);
    m_chatLay->setContentsMargins(14, 12, 14, 8);
    m_chatLay->setSpacing(15);

    m_chatLay->addWidget(buildHero(), 1);

    // "Stasis is working" lives at the end of the transcript, so it appears
    // exactly where the answer is about to.
    m_workRow = new QWidget(m_chatBody);
    m_workRow->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* wh = new QHBoxLayout(m_workRow);
    wh->setContentsMargins(4, 0, 0, 0);
    wh->setSpacing(8);
    auto* wmark = new QLabel(m_workRow);
    QPixmap wpx(QStringLiteral(":/assets/stasis-mark-32.png"));
    if (!wpx.isNull()) wmark->setPixmap(wpx.scaledToHeight(14, Qt::SmoothTransformation));
    wmark->setStyleSheet(QStringLiteral("background:transparent;"));
    m_workText = new QLabel(QStringLiteral("Stasis is working."), m_workRow);
    m_workText->setStyleSheet(QStringLiteral(
        "color:#9B8CFF; font:12.5px 'Segoe UI'; background:transparent;"));
    wh->addWidget(wmark, 0);
    wh->addWidget(m_workText, 0);
    wh->addStretch(1);
    m_workRow->hide();
    m_chatLay->addWidget(m_workRow, 0);

    m_chatLay->addStretch(1);

    m_workTick = new QTimer(this);
    m_workTick->setInterval(380);
    connect(m_workTick, &QTimer::timeout, this, [this] {
        // '.' then '..' then '...' then '....' and back round.
        m_workDots = (m_workDots % 4) + 1;
        m_workText->setText(QStringLiteral("Stasis is working")
                            + QString(m_workDots, QLatin1Char('.')));
    });

    m_scroll->setWidget(m_chatBody);
    return m_scroll;
}

// ── composer ─────────────────────────────────────────────────────────────────

QWidget* AiSidebar::buildComposer() {
    auto* wrap = new QWidget(this);
    wrap->setAttribute(Qt::WA_StyledBackground, true);
    wrap->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* v = new QVBoxLayout(wrap);
    v->setContentsMargins(12, 0, 12, 12);
    v->setSpacing(8);

    m_attachStrip = new QWidget(wrap);
    m_attachStrip->setStyleSheet(QStringLiteral("background:transparent;"));
    new QHBoxLayout(m_attachStrip);
    m_attachStrip->layout()->setContentsMargins(0, 0, 0, 0);
    m_attachStrip->layout()->setSpacing(6);
    m_attachStrip->hide();
    v->addWidget(m_attachStrip, 0);

    // Sources and the rollback control share the row directly above the input.
    m_strip = new AiSourcesStrip(wrap);
    connect(m_strip, &AiSourcesStrip::rollbackClicked, this, [this] {
        if (m_deckTarget && (m_deckTarget->aiCanRollback() || m_deckTarget->aiCanRollforward())) {
            if (m_deckTarget->aiCanRollback()) { m_deckTarget->aiRollback();    showRollback(true);  }
            else                               { m_deckTarget->aiRollforward(); showRollback(false); }
            return;
        }
        if (m_agent->canRollback())         { m_agent->rollback();    showRollback(true);  }
        else if (m_agent->canRollforward()) { m_agent->rollforward(); showRollback(false); }
    });
    v->addWidget(m_strip, 0);

    m_composerBox = new QWidget(wrap);
    QWidget* box = m_composerBox;
    box->setObjectName(QStringLiteral("composerBox"));
    box->setAttribute(Qt::WA_StyledBackground, true);
    setComposerFocused(false);
    auto* bv = new QVBoxLayout(box);
    bv->setContentsMargins(10, 8, 8, 8);
    bv->setSpacing(6);

    m_input = new QTextEdit(box);
    m_input->setPlaceholderText(QStringLiteral("Ask Stasis, or describe what to build"));
    m_input->setFrameShape(QFrame::NoFrame);
    m_input->setFixedHeight(56);
    m_input->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_input->setStyleSheet(QStringLiteral(
        "QTextEdit { background:transparent; border:none; color:#E7EAF1;"
        "  font:13px 'Segoe UI'; }"));
    m_input->installEventFilter(this);
    bv->addWidget(m_input);

    auto* row = new QWidget(box);
    row->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* rh = new QHBoxLayout(row);
    rh->setContentsMargins(0, 0, 0, 0);
    rh->setSpacing(8);

    m_attach = new QPushButton(QStringLiteral("Attach"), row);
    m_attach->setCursor(Qt::PointingHandCursor);
    m_attach->setFixedHeight(28);
    m_attach->setFocusPolicy(Qt::NoFocus);
    m_attach->setToolTip(QStringLiteral("Up to 5 images or 2 files"));
    m_attach->setStyleSheet(QStringLiteral(
        "QPushButton { color:#9AA4B8; font:12px 'Segoe UI'; background:transparent;"
        "  border:1px solid rgba(255,255,255,0.12); border-radius:8px; padding:0 11px; }"
        "QPushButton:hover { color:#E7EAF1; border-color:rgba(255,255,255,0.26); }"));
    connect(m_attach, &QPushButton::clicked, this, &AiSidebar::pickAttachments);

    m_quota = new QLabel(row);
    m_quota->setStyleSheet(QStringLiteral(
        "color:#6C7689; font:11px 'Segoe UI'; background:transparent;"));

    m_send = new QPushButton(QStringLiteral("Send"), row);
    m_send->setCursor(Qt::PointingHandCursor);
    m_send->setFixedHeight(28);
    m_send->setStyleSheet(QStringLiteral(
        "QPushButton { color:#FFFFFF; font:600 12px 'Segoe UI';"
        "  background:#6D4EF2; border:none; border-radius:8px; padding:0 16px; }"
        "QPushButton:hover { background:#7C5CFF; }"
        "QPushButton:pressed { background:#5B3FD6; }"
        "QPushButton:disabled { background:#2A2F3C; color:#5A6274; }"));
    connect(m_send, &QPushButton::clicked, this, &AiSidebar::submit);

    rh->addWidget(m_attach, 0);
    rh->addStretch(1);
    rh->addWidget(m_quota, 0);
    rh->addWidget(m_send, 0);
    bv->addWidget(row);

    v->addWidget(box);
    return wrap;
}

// ── behaviour ────────────────────────────────────────────────────────────────

void AiSidebar::setMode(AiMode mode) {
    m_mode = mode;
    m_modeChip->setText(QStringLiteral("In %1").arg(modeName(mode)));

    const bool edits = capabilityFor(mode) == AiCapability::Edit;
    m_input->setPlaceholderText(edits
        ? QStringLiteral("Ask Stasis, or describe what to build")
        : QStringLiteral("Ask Stasis about this file"));

    if (m_hero) {
        if (auto* hint = m_hero->findChild<QLabel*>(QStringLiteral("heroHint"))) {
            hint->setText(edits
                ? QStringLiteral("Describe what you want and Stasis will build it "
                                 "into your %1 document, live.").arg(modeName(mode))
                : QStringLiteral("Here Stasis answers questions about what is open. "
                                 "It does not change the file."));
        }
    }
}

void AiSidebar::startNewSession() {
    m_history.clear();
    m_pending.clear();
    m_chatId.clear();          // the next prompt opens a new named conversation
    rebuildAttachmentStrip();
    // Drop every bubble, keeping the hero, the working row and the trailing
    // stretch that the layout is built around.
    for (int i = m_chatLay->count() - 1; i >= 0; --i) {
        QLayoutItem* it = m_chatLay->itemAt(i);
        QWidget* w = it ? it->widget() : nullptr;
        if (!w || w == m_hero || w == m_workRow) continue;
        m_chatLay->takeAt(i);
        w->deleteLater();
    }
    refreshHeroVisibility();
    refreshQuotaLabel();
}

void AiSidebar::focusComposer() {
    m_input->setFocus(Qt::OtherFocusReason);
}

void AiSidebar::setDeckTarget(AiStreamTarget* target) {
    if (m_deckTarget == target) return;
    m_deckTarget = target;
    hideRollback();
}

void AiSidebar::setDocumentTarget(QTextEdit* target) {
    if (m_docTarget == target) return;
    m_docTarget = target;
    // The rollback on offer belonged to the document that was open. Moving to
    // another tab retires it rather than letting one press edit the wrong file.
    hideRollback();
}

void AiSidebar::showRollback(bool rolledBack) {
    m_rolledBack = rolledBack;
    if (m_strip) m_strip->setRollbackVisible(true, rolledBack);
}

void AiSidebar::hideRollback() {
    if (m_strip) m_strip->setRollbackVisible(false, false);
}

void AiSidebar::setComposerFocused(bool on) {
    if (!m_composerBox) return;
    m_composerBox->setStyleSheet(QStringLiteral(
        "#composerBox { background:%1; border:1px solid %2; border-radius:13px; }")
        .arg(on ? QStringLiteral("#171D29") : QStringLiteral("#141924"),
             on ? QStringLiteral("rgba(124,92,255,0.55)")
                : QStringLiteral("rgba(255,255,255,0.09)")));
}

void AiSidebar::refreshHeroVisibility() {
    if (m_hero) m_hero->setVisible(m_history.isEmpty());
}

void AiSidebar::appendMessage(const AiMessage& m) {
    auto* b = new Bubble(m.text, m.role == AiMessage::Role::User, m_chatBody);
    // Inserted before the working row and the trailing stretch so the "working"
    // line always sits at the very bottom of the transcript.
    const int idx = m_chatLay->indexOf(m_workRow);
    m_chatLay->insertWidget(idx < 0 ? m_chatLay->count() - 1 : idx, b);
    if (m.role == AiMessage::Role::Assistant) m_streamTarget = b->label();
    refreshHeroVisibility();
    scrollToBottom();
}

void AiSidebar::scrollToBottom() {
    // After the layout has settled, otherwise the maximum is still the old one.
    QTimer::singleShot(0, this, [this] {
        m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
    });
}

void AiSidebar::setWorking(bool on) {
    m_workRow->setVisible(on);
    m_send->setEnabled(!on);
    if (on) {
        m_workDots = 0;
        m_workText->setText(QStringLiteral("Stasis is working."));
        m_workTick->start();
    } else {
        m_workTick->stop();
    }
    scrollToBottom();
}

void AiSidebar::refreshQuotaLabel() {
    m_quota->setText(AiQuota::summaryText());
    m_quota->setToolTip(
        QStringLiteral("%1 of %2 characters used this month")
            .arg(QLocale().toString(AiQuota::charactersUsed()),
                 QLocale().toString(AiQuota::characterLimit())));
}

void AiSidebar::submit() {
    const QString text = m_input->toPlainText().trimmed();
    if (text.isEmpty()) return;

    if (!StasisClient::online()) {
        m_toast->post(QStringLiteral("Please turn on the internet. Stasis needs a "
                                     "connection; the rest of NativeOffice does not."),
                      AiToast::Tone::Warning);
        return;
    }

    // A turn that will write into the document spends a generation, so the
    // allowance is checked before the prompt is sent rather than after the
    // model has already been paid for.
    const bool willEdit = capabilityFor(m_mode) == AiCapability::Edit
                          && (m_docTarget || m_deckTarget);
    if (willEdit && !AiQuota::canGenerate()) {
        m_toast->post(AiQuota::blockedReason(), AiToast::Tone::Warning, 9000);
        return;
    }

    AiMessage u;
    u.role = AiMessage::Role::User;
    u.text = text;
    u.at   = QDateTime::currentDateTime();
    u.attachments = m_pending;

    // The opening prompt names the conversation. Done here rather than when the
    // answer arrives so a chat that is abandoned mid-reply is still in the list.
    const bool firstTurn = m_history.isEmpty();
    m_history.append(u);
    appendMessage(u);
    if (firstTurn) {
        if (m_chatId.isEmpty()) m_chatId = AiChatStore::mintId();
        m_chats->save(m_chatId, AiChatStore::titleFrom(text), modeName(m_mode));
    }

    m_input->clear();
    m_pending.clear();
    rebuildAttachmentStrip();

    // The empty assistant bubble is created up front so the stream has
    // somewhere to land as it arrives.
    AiMessage placeholder;
    placeholder.role = AiMessage::Role::Assistant;
    placeholder.text = QString();
    m_streamAccum.clear();
    m_streamDecided = false;
    m_streamToDoc   = false;
    m_streamToDeck  = false;
    if (m_strip) m_strip->clearSources();   // these belong to the new answer
    appendMessage(placeholder);

    // What the user has open travels with the prompt, so "add a conclusion" or
    // "make it shorter" can act on the real text instead of being answered with
    // a question about a document sitting on the other half of the screen.
    QString excerpt;
    if (m_docTarget) {
        excerpt = m_docTarget->toPlainText();
        constexpr int kMaxContext = 12000;
        if (excerpt.size() > kMaxContext) {
            // Keep both ends: the opening establishes what the document is, and
            // the tail is what a "continue from here" has to follow on from.
            excerpt = excerpt.left(kMaxContext / 3)
                    + QStringLiteral("\n\n[...]\n\n")
                    + excerpt.right(kMaxContext * 2 / 3);
        }
    }

    setWorking(true);
    m_client->send(QString(), m_history,
                   willEdit ? StasisClient::Intent::Generate
                            : StasisClient::Intent::Answer,
                   modeName(m_mode), excerpt);
}

void AiSidebar::pickAttachments() {
    const QStringList picked = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Attach to Stasis"), QString(),
        QStringLiteral("Images and documents (*.png *.jpg *.jpeg *.webp *.bmp "
                       "*.docx *.xlsx *.pptx *.pdf *.txt *.md *.csv)"));
    if (picked.isEmpty()) return;

    int images = 0, docs = 0;
    for (const AiAttachment& a : m_pending)
        (a.kind == AiAttachment::Kind::Image ? images : docs)++;

    for (const QString& path : picked) {
        const QString suffix = QFileInfo(path).suffix().toLower();
        const bool isImage = QStringList{QStringLiteral("png"), QStringLiteral("jpg"),
                                         QStringLiteral("jpeg"), QStringLiteral("webp"),
                                         QStringLiteral("bmp")}.contains(suffix);
        if (isImage && images >= kMaxImageAttachments) {
            m_toast->post(QStringLiteral("Up to %1 images can be attached at once.")
                              .arg(kMaxImageAttachments));
            continue;
        }
        if (!isImage && docs >= kMaxDocumentAttachments) {
            m_toast->post(QStringLiteral("Up to %1 files can be attached at once.")
                              .arg(kMaxDocumentAttachments));
            continue;
        }
        AiAttachment a;
        a.kind = isImage ? AiAttachment::Kind::Image : AiAttachment::Kind::Document;
        a.path = path;
        a.displayName = QFileInfo(path).fileName();
        a.bytes = QFileInfo(path).size();
        m_pending.append(a);
        (isImage ? images : docs)++;
    }
    rebuildAttachmentStrip();
}

void AiSidebar::rebuildAttachmentStrip() {
    auto* lay = qobject_cast<QHBoxLayout*>(m_attachStrip->layout());
    while (QLayoutItem* it = lay->takeAt(0)) {
        if (QWidget* w = it->widget()) w->deleteLater();
        delete it;
    }
    if (m_pending.isEmpty()) { m_attachStrip->hide(); return; }

    for (int i = 0; i < m_pending.size(); ++i) {
        const AiAttachment& a = m_pending.at(i);
        auto* chip = new QPushButton(a.displayName + QStringLiteral("  ✕"), m_attachStrip);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setFixedHeight(24);
        chip->setFocusPolicy(Qt::NoFocus);
        chip->setToolTip(QStringLiteral("Remove"));
        chip->setStyleSheet(QStringLiteral(
            "QPushButton { color:#AAB3C4; font:11px 'Segoe UI';"
            "  background:rgba(255,255,255,0.06);"
            "  border:1px solid rgba(255,255,255,0.12);"
            "  border-radius:7px; padding:0 8px; }"
            "QPushButton:hover { color:#FFFFFF; border-color:rgba(255,255,255,0.28); }"));
        connect(chip, &QPushButton::clicked, this, [this, i] {
            if (i < m_pending.size()) m_pending.remove(i);
            rebuildAttachmentStrip();
        });
        lay->addWidget(chip, 0);
    }
    lay->addStretch(1);
    m_attachStrip->show();
}

// ── events ───────────────────────────────────────────────────────────────────

void AiSidebar::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
}

bool AiSidebar::eventFilter(QObject* o, QEvent* e) {
    if (o == m_input && e->type() == QEvent::FocusIn)  setComposerFocused(true);
    if (o == m_input && e->type() == QEvent::FocusOut) setComposerFocused(false);
    if (o == m_input && e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        // Enter sends, Shift+Enter makes a new line: the convention every chat
        // box uses, so it needs no explaining.
        if ((k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter)
            && !(k->modifiers() & Qt::ShiftModifier)) {
            submit();
            return true;
        }
    }
    if (o == this) {
        if (e->type() == QEvent::MouseMove || e->type() == QEvent::Enter) {
            const QPoint p = mapFromGlobal(QCursor::pos());
            const bool nearTop = p.y() >= 0 && p.y() < kCloseRevealBand;
            if (m_closeAnim && m_closeFade) {
                const qreal want = nearTop ? 1.0 : 0.0;
                if (!qFuzzyCompare(m_closeFade->opacity(), want)) {
                    m_closeAnim->stop();
                    m_closeAnim->setStartValue(m_closeFade->opacity());
                    m_closeAnim->setEndValue(want);
                    m_closeAnim->start();
                }
            }
        } else if (e->type() == QEvent::Leave) {
            if (m_closeAnim && m_closeFade) {
                m_closeAnim->stop();
                m_closeAnim->setStartValue(m_closeFade->opacity());
                m_closeAnim->setEndValue(0.0);
                m_closeAnim->start();
            }
        }
    }
    return QWidget::eventFilter(o, e);
}

} // namespace NativeOffice
