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
#include <QIcon>
#include <QPainterPath>
#include <QResizeEvent>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextEdit>
#include <QToolButton>
#include <QTimer>
#include <QDesktopServices>
#include <QDialog>
#include <QGridLayout>
#include <QListWidget>
#include <QListView>
#include <QStackedWidget>
#include <QUrl>
#include <QDateTime>
#include <QJsonObject>
#include <QVBoxLayout>

namespace NativeOffice {

namespace {

// Two rows of text now, so the header needs the height for both.
constexpr int kHeaderHeight = 54;
// How far down from the top the pointer still counts as "at the top of the
// sidebar" for the purpose of revealing the close button.
constexpr int kCloseRevealBand = 56;

// Near-black with a violet cast, not the blue-grey the panel used to be. The
// assistant is the one surface in the suite that is deliberately its own place
// rather than a shade of the editor beside it.
const char* kPanelBg    = "#07070C";
const char* kRailBg     = "#050509";
const char* kPanelEdge  = "rgba(255,255,255,0.06)";
const char* kCardBg     = "#0D0D15";
const char* kCardEdge   = "rgba(255,255,255,0.07)";
const char* kAccent     = "#8B5CF6";   // violet, the one accent
const char* kAccentSoft = "#B9A5FF";
const char* kTextHigh   = "#F4F4F8";
const char* kTextMid    = "#9299AD";
const char* kTextLow    = "#6A7185";

// ── icons ───────────────────────────────────────────────────────────────────
// Drawn rather than shipped: they are a dozen simple glyphs, they have to take
// the colour of whatever they sit on, and a resource per icon per state is a
// lot of files to keep in step with a palette.
QPixmap glyph(const QString& kind, const QColor& c, int px) {
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(c, px * 0.085);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const double w = px, h = px;

    if (kind == QLatin1String("sparkle")) {
        // A four-point star, the shape everything AI has settled on.
        QPainterPath s1;
        s1.moveTo(w * 0.50, h * 0.14);
        s1.quadTo(w * 0.56, h * 0.44, w * 0.86, h * 0.50);
        s1.quadTo(w * 0.56, h * 0.56, w * 0.50, h * 0.86);
        s1.quadTo(w * 0.44, h * 0.56, w * 0.14, h * 0.50);
        s1.quadTo(w * 0.44, h * 0.44, w * 0.50, h * 0.14);
        p.fillPath(s1, c);
    } else if (kind == QLatin1String("history")) {
        p.drawArc(QRectF(w * 0.18, h * 0.18, w * 0.64, h * 0.64), 60 * 16, 280 * 16);
        p.drawLine(QPointF(w * 0.50, h * 0.32), QPointF(w * 0.50, h * 0.52));
        p.drawLine(QPointF(w * 0.50, h * 0.52), QPointF(w * 0.66, h * 0.60));
        p.drawLine(QPointF(w * 0.30, h * 0.14), QPointF(w * 0.30, h * 0.30));
        p.drawLine(QPointF(w * 0.30, h * 0.30), QPointF(w * 0.46, h * 0.30));
    } else if (kind == QLatin1String("chat")) {
        QPainterPath b;
        b.addRoundedRect(QRectF(w * 0.14, h * 0.20, w * 0.72, h * 0.50), w * 0.14, w * 0.14);
        b.moveTo(w * 0.32, h * 0.68);
        b.lineTo(w * 0.30, h * 0.86);
        b.lineTo(w * 0.48, h * 0.70);
        p.drawPath(b);
    } else if (kind == QLatin1String("doc")) {
        p.drawRoundedRect(QRectF(w * 0.24, h * 0.14, w * 0.52, h * 0.72), w * 0.08, w * 0.08);
        for (int i = 0; i < 3; ++i)
            p.drawLine(QPointF(w * 0.36, h * (0.34 + i * 0.16)),
                       QPointF(w * 0.64, h * (0.34 + i * 0.16)));
    } else if (kind == QLatin1String("pencil")) {
        p.drawLine(QPointF(w * 0.22, h * 0.78), QPointF(w * 0.30, h * 0.56));
        p.drawLine(QPointF(w * 0.30, h * 0.56), QPointF(w * 0.64, h * 0.22));
        p.drawLine(QPointF(w * 0.64, h * 0.22), QPointF(w * 0.78, h * 0.36));
        p.drawLine(QPointF(w * 0.78, h * 0.36), QPointF(w * 0.44, h * 0.70));
        p.drawLine(QPointF(w * 0.44, h * 0.70), QPointF(w * 0.22, h * 0.78));
    } else if (kind == QLatin1String("bulb")) {
        p.drawArc(QRectF(w * 0.26, h * 0.14, w * 0.48, h * 0.48), 0, 360 * 16);
        p.drawLine(QPointF(w * 0.42, h * 0.62), QPointF(w * 0.42, h * 0.74));
        p.drawLine(QPointF(w * 0.58, h * 0.62), QPointF(w * 0.58, h * 0.74));
        p.drawLine(QPointF(w * 0.40, h * 0.80), QPointF(w * 0.60, h * 0.80));
    } else if (kind == QLatin1String("clip")) {
        // One open hook, drawn as a path. Two arcs meeting at their ends closed
        // into a ring and the button read as a nought.
        QPainterPath c1;
        c1.moveTo(w * 0.66, h * 0.34);
        c1.lineTo(w * 0.38, h * 0.62);
        c1.quadTo(w * 0.26, h * 0.74, w * 0.36, h * 0.84);
        c1.quadTo(w * 0.46, h * 0.94, w * 0.58, h * 0.82);
        c1.lineTo(w * 0.82, h * 0.58);
        c1.quadTo(w * 0.96, h * 0.44, w * 0.80, h * 0.26);
        c1.quadTo(w * 0.62, h * 0.10, w * 0.48, h * 0.24);
        c1.lineTo(w * 0.22, h * 0.50);
        p.drawPath(c1);
    } else if (kind == QLatin1String("send")) {
        QPainterPath t;
        t.moveTo(w * 0.14, h * 0.50);
        t.lineTo(w * 0.86, h * 0.18);
        t.lineTo(w * 0.58, h * 0.86);
        t.lineTo(w * 0.48, h * 0.58);
        t.closeSubpath();
        p.fillPath(t, c);
    } else if (kind == QLatin1String("shield")) {
        QPainterPath sh;
        sh.moveTo(w * 0.50, h * 0.12);
        sh.lineTo(w * 0.82, h * 0.26);
        sh.lineTo(w * 0.82, h * 0.52);
        sh.quadTo(w * 0.82, h * 0.78, w * 0.50, h * 0.90);
        sh.quadTo(w * 0.18, h * 0.78, w * 0.18, h * 0.52);
        sh.lineTo(w * 0.18, h * 0.26);
        sh.closeSubpath();
        p.drawPath(sh);
    } else if (kind == QLatin1String("chevron")) {
        p.drawLine(QPointF(w * 0.28, h * 0.40), QPointF(w * 0.50, h * 0.62));
        p.drawLine(QPointF(w * 0.50, h * 0.62), QPointF(w * 0.72, h * 0.40));
    } else if (kind == QLatin1String("arrow")) {
        p.drawLine(QPointF(w * 0.20, h * 0.50), QPointF(w * 0.76, h * 0.50));
        p.drawLine(QPointF(w * 0.56, h * 0.30), QPointF(w * 0.78, h * 0.50));
        p.drawLine(QPointF(w * 0.78, h * 0.50), QPointF(w * 0.56, h * 0.70));
    }
    return pm;
}

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
    // The rail and the column share this; children paint over it.
    // The rail costs 56px that used to belong to the conversation, and the
    // openers sit two to a row, so the old 300 minimum left them about 90px
    // each and clipped their own titles.
    setMinimumWidth(360);

    m_client = new StasisClient(this);
    m_chats  = new AiChatStore(this);
    m_agent  = new AiDocumentAgent(this);

    // Rail down the left, everything else in a column beside it. The rail is
    // what makes History reachable: it used to be a mode with no way in.
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildRail(), 0);

    auto* column = new QWidget(this);
    column->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* v = new QVBoxLayout(column);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    root->addWidget(column, 1);

    v->addWidget(buildHeader(), 0);

    // Two pages behind one header: the conversation, and the history list.
    m_pages = new QStackedWidget(column);
    m_pages->setStyleSheet(QStringLiteral("background:transparent;"));
    m_pages->addWidget(buildChatArea());
    m_pages->addWidget(buildHistoryPage());
    v->addWidget(m_pages, 1);

    m_toast = new AiToast(this);
    auto* toastWrap = new QWidget(this);
    auto* tw = new QVBoxLayout(toastWrap);
    tw->setContentsMargins(12, 0, 12, 8);
    tw->setSpacing(0);
    tw->addWidget(m_toast);
    v->addWidget(toastWrap, 0);

    m_composerHost = buildComposer();
    v->addWidget(m_composerHost, 0);
    v->addWidget(buildFooter(), 0);

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
                    m_turnAction = buildTurnAction(QStringLiteral("deck"), written, full);
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
                m_turnAction = buildTurnAction(QStringLiteral("document"), 0, full);
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
        // The bubble now says whatever it is going to say, so the record of what
        // this turn did can be pinned under it and stored beside it. Chat turns
        // get one too: "Answered in the chat" is a real answer to "what did it
        // do", and without it the bars would appear and disappear down the
        // transcript for no reason the reader can see.
        if (ok) {
            if (m_turnAction.isEmpty())
                m_turnAction = buildTurnAction(QStringLiteral("chat"), 0, full);
            if (m_streamBubble) attachActionBar(m_streamBubble, m_turnAction);
            const QString shown = m_streamTarget ? m_streamTarget->text() : full;
            m_chats->appendTurn(m_chatId, /*fromUser=*/false, shown, m_turnAction);
        }

        m_streamTarget = nullptr;
        m_streamBubble = nullptr;
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
        "color:%1; font:700 17px 'Segoe UI'; background:transparent;")
        .arg(QLatin1String(kTextHigh)));

    // "In Writer". Sits above the chat and changes as the user moves between
    // tabs, so the panel never silently acts on a surface you are not looking at.
    m_modeChip = new QLabel(m_header);
    // Fixed height, or the row stretches it to the full header and a small
    // label becomes a button-sized slab next to the product name.
    m_modeChip->setFixedHeight(20);
    m_modeChip->setAlignment(Qt::AlignCenter);
    m_modeChip->setStyleSheet(QStringLiteral(
        "color:%1; font:11.5px 'Segoe UI'; background:transparent;"
        "border:none; padding:0;").arg(QLatin1String(kTextMid)));

    // Start a fresh conversation.
    //
    // startNewSession() has always existed but nothing in the panel reached it:
    // the only caller runs once per process, so closing and reopening Stasis
    // kept the whole transcript. Testers were told by the assistant itself to
    // "look for a New Chat button, or close and reopen the panel", and neither
    // worked. This is that button.
    m_newChat = new QPushButton(QStringLiteral("+"), m_header);
    m_newChat->setCursor(Qt::PointingHandCursor);
    m_newChat->setFixedSize(30, 24);
    m_newChat->setFocusPolicy(Qt::NoFocus);
    m_newChat->setToolTip(QStringLiteral("New chat"));
    m_newChat->setStyleSheet(QStringLiteral(
        "QPushButton { border:none; background:transparent; color:#C9CFDB;"
        "  font:600 15px 'Segoe UI'; border-radius:6px; padding-bottom:2px; }"
        "QPushButton:hover { background:rgba(255,255,255,0.10); color:#FFFFFF; }"
        "QPushButton:pressed { background:rgba(255,255,255,0.05); }"));
    connect(m_newChat, &QPushButton::clicked, this, [this] {
        startNewSession();
        focusComposer();
    });

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

    // Two rows. The product and what it is sit together on the first, and where
    // it currently is on the second, which is the line that changes as the user
    // moves between tabs and the one worth its own space.
    auto* kind = new QLabel(QStringLiteral("AI Assistant"), m_header);
    kind->setFixedHeight(21);
    kind->setAlignment(Qt::AlignCenter);
    kind->setStyleSheet(QStringLiteral(
        "color:%1; font:600 10.5px 'Segoe UI';"
        "background:rgba(139,92,246,0.20);"
        "border:1px solid rgba(139,92,246,0.34);"
        "border-radius:10px; padding:0 10px;").arg(QLatin1String(kAccentSoft)));

    auto* rows = new QVBoxLayout();
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(1);

    auto* top = new QHBoxLayout();
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(7);
    top->addWidget(mark, 0);
    top->addWidget(name, 0);
    top->addWidget(kind, 0);
    top->addStretch(1);
    rows->addLayout(top);

    auto* bottom = new QHBoxLayout();
    bottom->setContentsMargins(0, 0, 0, 0);
    bottom->setSpacing(0);
    bottom->addWidget(m_modeChip, 0);
    bottom->addStretch(1);
    rows->addLayout(bottom);

    h->addLayout(rows, 1);
    h->addWidget(m_newChat, 0);
    h->addSpacing(2);
    h->addWidget(m_close, 0);
    return m_header;
}

// ── rail, history, footer ───────────────────────────────────────────────────

QWidget* AiSidebar::buildRail() {
    auto* rail = new QWidget(this);
    rail->setObjectName(QStringLiteral("aiRail"));
    rail->setAttribute(Qt::WA_StyledBackground, true);
    rail->setFixedWidth(62);
    rail->setStyleSheet(QStringLiteral(
        "#aiRail { background:%1; border-right:1px solid %2; }")
        .arg(QLatin1String(kRailBg), QLatin1String(kPanelEdge)));

    auto* v = new QVBoxLayout(rail);
    v->setContentsMargins(0, 14, 0, 16);
    v->setSpacing(6);

    auto* mark = new QLabel(rail);
    QPixmap px(QStringLiteral(":/assets/stasis-mark-256.png"));
    if (!px.isNull())
        mark->setPixmap(px.scaledToHeight(30, Qt::SmoothTransformation));
    mark->setAlignment(Qt::AlignCenter);
    mark->setStyleSheet(QStringLiteral("background:transparent;"));
    v->addWidget(mark, 0, Qt::AlignHCenter);
    v->addSpacing(18);

    // Two destinations and no more. Settings and Help sat on this rail with
    // nothing behind them, which is worse than not offering them at all.
    m_railAssist  = railButton(rail, QStringLiteral("Assist"),  true);
    m_railHistory = railButton(rail, QStringLiteral("History"), false);
    m_railAssist->setIcon(QIcon(glyph(QStringLiteral("sparkle"),
                                      QColor(kAccentSoft), 20)));
    m_railHistory->setIcon(QIcon(glyph(QStringLiteral("history"),
                                       QColor(kTextMid), 20)));

    v->addWidget(m_railAssist,  0, Qt::AlignHCenter);
    v->addWidget(m_railHistory, 0, Qt::AlignHCenter);
    v->addStretch(1);

    connect(m_railAssist,  &QToolButton::clicked, this, [this] { showAssist(); });
    connect(m_railHistory, &QToolButton::clicked, this, [this] { showHistory(); });
    return rail;
}

QToolButton* AiSidebar::railButton(QWidget* parent, const QString& text, bool on) {
    auto* b = new QToolButton(parent);
    b->setText(text);
    b->setCheckable(true);
    b->setChecked(on);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    b->setFixedSize(54, 54);
    // Text under icon. A QPushButton puts them side by side and no amount of
    // padding moves them, which ran the label off the edge of the rail.
    b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    b->setIconSize(QSize(19, 19));
    b->setStyleSheet(QStringLiteral(
        "QToolButton { border:none; background:transparent; color:%1;"
        "  font:600 9px 'Segoe UI'; border-radius:12px; padding:6px 0 4px 0; }"
        "QToolButton:hover { background:rgba(255,255,255,0.05); color:#C9CFDB; }"
        "QToolButton:checked { background:rgba(139,92,246,0.18); color:%2; }")
        .arg(QLatin1String(kTextLow), QLatin1String(kAccentSoft)));
    return b;
}

void AiSidebar::showAssist() {
    m_railAssist->setChecked(true);
    m_railHistory->setChecked(false);
    m_pages->setCurrentIndex(0);
    // The composer belongs to the conversation. Left under the history list it
    // invited typing into a page that cannot answer.
    if (m_composerHost) m_composerHost->setVisible(true);
}

void AiSidebar::showHistory() {
    m_railAssist->setChecked(false);
    m_railHistory->setChecked(true);
    m_pages->setCurrentIndex(1);
    if (m_composerHost) m_composerHost->setVisible(false);
    reloadHistory();
}

QWidget* AiSidebar::buildHistoryPage() {
    auto* page = new QWidget(this);
    page->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(14, 12, 14, 10);
    v->setSpacing(9);

    auto* title = new QLabel(QStringLiteral("Your conversations"), page);
    title->setStyleSheet(QStringLiteral(
        "color:#E6E9F0; font:600 14px 'Segoe UI'; background:transparent;"));
    v->addWidget(title);

    m_historyNote = new QLabel(page);
    m_historyNote->setWordWrap(true);
    m_historyNote->setStyleSheet(QStringLiteral(
        "color:#79839A; font:12px 'Segoe UI'; background:transparent;"));
    v->addWidget(m_historyNote);

    m_historyList = new QListWidget(page);
    m_historyList->setFrameShape(QFrame::NoFrame);
    m_historyList->setSpacing(3);
    // Titles are whole sentences. Wrapped, never scrolled sideways: a list you
    // have to drag horizontally to read is not a list.
    m_historyList->setWordWrap(true);
    m_historyList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_historyList->setTextElideMode(Qt::ElideRight);
    m_historyList->setResizeMode(QListView::Adjust);
    m_historyList->setUniformItemSizes(false);
    m_historyList->setStyleSheet(QStringLiteral(
        "QListWidget { background:transparent; border:none; outline:none; }"
        "QListWidget::item { color:#C9CFDB; background:#0D0D15;"
        "  border:1px solid rgba(255,255,255,0.07); border-radius:11px;"
        "  padding:10px 11px; }"
        "QListWidget::item:hover { background:#12121C;"
        "  border:1px solid rgba(139,92,246,0.35); }"
        "QListWidget::item:selected { background:rgba(139,92,246,0.20);"
        "  border:1px solid rgba(139,92,246,0.50); color:#F4F4F8; }"
        "QScrollBar:vertical { width:8px; background:transparent; margin:0; }"
        "QScrollBar::handle:vertical { background:#333C4D; border-radius:4px;"
        "  min-height:30px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"));
    v->addWidget(m_historyList, 1);

    connect(m_historyList, &QListWidget::itemClicked, this, &AiSidebar::openHistoryItem);

    connect(m_chats, &AiChatStore::chatsLoaded, this,
            [this](const QVector<AiChatSummary>& chats) {
        m_historyList->clear();
        if (chats.isEmpty()) {
            m_historyNote->setText(
                QStringLiteral("Nothing here yet. Conversations you have with Stasis "
                               "are listed here so you can pick one up again."));
            return;
        }
        m_historyNote->setText(QStringLiteral("Open one to read it again."));
        for (const AiChatSummary& c : chats) {
            auto* it = new QListWidgetItem(m_historyList);
            const QString when = c.updatedAt.isValid()
                ? c.updatedAt.toLocalTime().toString(QStringLiteral("d MMM, HH:mm"))
                : QString();
            it->setText(c.mode.isEmpty()
                            ? QStringLiteral("%1\n%2").arg(c.title, when)
                            : QStringLiteral("%1\n%2  .  %3").arg(c.title, c.mode, when));
            it->setData(Qt::UserRole, c.id);
        }
    });
    connect(m_chats, &AiChatStore::loadFailed, this, [this](const QString& why) {
        if (m_historyNote) m_historyNote->setText(why);
    });
    connect(m_chats, &AiChatStore::turnsLoaded, this,
            [this](const QString& chatId, const QVector<AiStoredTurn>& turns) {
        replayConversation(chatId, turns);
    });
    return page;
}

void AiSidebar::reloadHistory() {
    if (!m_historyNote) return;
    m_historyNote->setText(QStringLiteral("Loading..."));
    m_chats->loadChats();
}

void AiSidebar::openHistoryItem(QListWidgetItem* item) {
    if (!item) return;
    const QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty()) return;
    m_historyNote->setText(QStringLiteral("Opening..."));
    m_chats->loadTurns(id);
}

void AiSidebar::replayConversation(const QString& chatId,
                                   const QVector<AiStoredTurn>& turns) {
    // A reopened conversation CONTINUES rather than being copied: the id is
    // adopted, so anything said next is appended to the same chat and History
    // does not fill up with fragments of one exchange.
    startNewSession();
    m_chatId = chatId;

    if (turns.isEmpty()) {
        // Conversations from before the transcript was stored still have their
        // title and nothing else. An empty panel here reads as a bug; saying
        // what happened reads as what it is.
        AiMessage note;
        note.role = AiMessage::Role::Assistant;
        note.text = QStringLiteral(
            "This conversation was had before Stasis started keeping transcripts, "
            "so only its name was saved. Anything from here on is kept.");
        note.at = QDateTime::currentDateTime();
        appendMessage(note);
        m_streamTarget = nullptr;
        m_streamBubble = nullptr;
        refreshHeroVisibility();
        showAssist();
        return;
    }

    for (const AiStoredTurn& t : turns) {
        AiMessage m;
        m.role = t.fromUser ? AiMessage::Role::User : AiMessage::Role::Assistant;
        m.text = t.text;
        m.at   = t.at;
        m_history.append(m);
        QWidget* b = appendMessage(m);
        if (!t.fromUser && !t.action.isEmpty()) attachActionBar(b, t.action);
    }
    // appendMessage aims the stream at the last assistant bubble it made. These
    // are replayed history, not a live answer, so that aim has to be cleared or
    // the next reply would stream into a bubble from months ago.
    m_streamTarget = nullptr;
    m_streamBubble = nullptr;
    refreshHeroVisibility();
    showAssist();
    scrollToBottom();
}

QWidget* AiSidebar::buildFooter() {
    auto* w = new QWidget(this);
    w->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(16, 0, 16, 10);
    h->setSpacing(6);

    auto* shield = new QLabel(w);
    shield->setPixmap(glyph(QStringLiteral("shield"), QColor(kTextLow), 13));
    shield->setStyleSheet(QStringLiteral("background:transparent;"));
    h->addWidget(shield, 0);

    auto* note = new QLabel(QStringLiteral("Your data stays private and secure."), w);
    note->setStyleSheet(QStringLiteral(
        "color:%1; font:10.5px 'Segoe UI'; background:transparent;")
        .arg(QLatin1String(kTextLow)));
    h->addWidget(note, 0);
    h->addStretch(1);

    auto* learn = new QPushButton(QStringLiteral("Learn more"), w);
    learn->setCursor(Qt::PointingHandCursor);
    learn->setFocusPolicy(Qt::NoFocus);
    learn->setStyleSheet(QStringLiteral(
        "QPushButton { border:none; background:transparent; color:%1;"
        "  font:10.5px 'Segoe UI'; }"
        "QPushButton:hover { color:#FFFFFF; }").arg(QLatin1String(kAccentSoft)));
    connect(learn, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("https://nativeoffice.online/privacy")));
    });
    h->addWidget(learn, 0);
    return w;
}

// ── chat ─────────────────────────────────────────────────────────────────────

QWidget* AiSidebar::buildHero() {
    // Shown while the transcript is empty: the mark, the attribution, and one
    // line saying what the panel will do where it currently is.
    m_hero = new QWidget(this);
    m_hero->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* v = new QVBoxLayout(m_hero);
    v->setContentsMargins(12, 20, 12, 20);
    v->setSpacing(0);
    v->addStretch(1);

    auto* mark = new QLabel(m_hero);
    QPixmap px(QStringLiteral(":/assets/stasis-mark-256.png"));
    if (!px.isNull())
        mark->setPixmap(px.scaledToHeight(76, Qt::SmoothTransformation));
    mark->setAlignment(Qt::AlignCenter);
    mark->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* markFade = new QGraphicsOpacityEffect(mark);
    markFade->setOpacity(0.92);
    mark->setGraphicsEffect(markFade);
    v->addWidget(mark, 0, Qt::AlignCenter);

    v->addSpacing(18);
    // Rich text so the name carries the accent and the rest does not.
    auto* powered = new QLabel(m_hero);
    powered->setTextFormat(Qt::RichText);
    powered->setText(QStringLiteral(
        "<span style='color:%1;'>Powered by </span>"
        "<span style='color:%2;'>Stasis</span>")
        .arg(QLatin1String(kTextHigh), QLatin1String(kAccentSoft)));
    powered->setAlignment(Qt::AlignCenter);
    powered->setStyleSheet(QStringLiteral(
        "font:600 19px 'Segoe UI'; background:transparent;"));
    v->addWidget(powered);

    v->addSpacing(7);
    auto* hint = new QLabel(m_hero);
    hint->setObjectName(QStringLiteral("heroHint"));
    hint->setAlignment(Qt::AlignCenter);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral(
        "color:%1; font:12.5px 'Segoe UI'; background:transparent;")
        .arg(QLatin1String(kTextMid)));
    v->addWidget(hint);

    v->addSpacing(22);

    // Four openers. They are not decoration: an empty panel with a blinking
    // caret is the hardest possible thing to start using, and naming what it
    // can do is the difference between a first prompt and a closed panel.
    // Each fills the composer rather than sending, because the useful version
    // of every one of these has the user's own subject in it.
    auto* grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);
    // Both columns share the width. Without this the cards sit at their hint
    // width in the middle of the panel and clip their own titles.
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);

    struct Opener { const char* title; const char* blurb; const char* seed;
                    const char* icon; const char* tint; };
    static const Opener kOpeners[4] = {
        { "Ask Anything", "Instant answers, in the chat.",
          "", "chat", "#8B5CF6" },
        { "Summarize",    "Long documents, made short.",
          "Summarize this in a few sentences: ", "doc", "#3B82F6" },
        { "Write Better", "Improve or rewrite what is there.",
          "Rewrite this so it reads better: ", "pencil", "#22C7A9" },
        { "Brainstorm",   "Ideas before you commit.",
          "Give me a few angles on ", "bulb", "#F59E0B" },
    };

    for (int i = 0; i < 4; ++i) {
        const Opener& o = kOpeners[i];
        const QColor tint(o.tint);
        auto* card = new QPushButton(m_hero);
        card->setCursor(Qt::PointingHandCursor);
        card->setFocusPolicy(Qt::NoFocus);
        // A word-wrapped QLabel reports a height hint that does not know the
        // width it will end up at, so the card's own hint comes out short and
        // the blurb is cut off at the bottom. The floor is set from what two
        // wrapped lines actually need.
        card->setMinimumHeight(126);
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        card->setStyleSheet(QStringLiteral(
            "QPushButton { text-align:left; padding:0; border-radius:14px;"
            "  background:%1; border:1px solid %2; }"
            "QPushButton:hover { background:#12121C; border:1px solid %3; }")
            .arg(QLatin1String(kCardBg), QLatin1String(kCardEdge),
                 tint.name(QColor::HexRgb)));

        auto* cv = new QVBoxLayout(card);
        cv->setContentsMargins(11, 10, 11, 8);
        cv->setSpacing(6);

        // The icon sits in a tinted rounded tile, which is what gives the four
        // cards their own identity without four different card colours.
        auto* tile = new QLabel(card);
        tile->setFixedSize(30, 30);
        tile->setAlignment(Qt::AlignCenter);
        tile->setPixmap(glyph(QLatin1String(o.icon), tint, 17));
        tile->setAttribute(Qt::WA_TransparentForMouseEvents);
        QColor tileBg = tint;
        tileBg.setAlphaF(0.16);
        tile->setStyleSheet(QStringLiteral(
            "background:rgba(%1,%2,%3,0.16); border-radius:10px;"
            "border:1px solid rgba(%1,%2,%3,0.30);")
            .arg(tint.red()).arg(tint.green()).arg(tint.blue()));
        cv->addWidget(tile, 0, Qt::AlignLeft);

        auto* t = new QLabel(QLatin1String(o.title), card);
        t->setStyleSheet(QStringLiteral(
            "color:%1; font:600 13px 'Segoe UI'; background:transparent;")
            .arg(QLatin1String(kTextHigh)));
        t->setAttribute(Qt::WA_TransparentForMouseEvents);
        // Elided, not clipped. A panel dragged narrow should show "Ask Anyth..."
        // rather than "Ask Anythin", which just looks broken.
        t->setMinimumWidth(1);
        t->setProperty("fullText", QLatin1String(o.title));
        t->installEventFilter(this);

        auto* bl = new QLabel(QLatin1String(o.blurb), card);
        bl->setWordWrap(true);
        bl->setMinimumHeight(30);          // two lines at 11px, whatever the width
        bl->setStyleSheet(QStringLiteral(
            "color:%1; font:11px 'Segoe UI'; background:transparent;")
            .arg(QLatin1String(kTextMid)));
        bl->setAttribute(Qt::WA_TransparentForMouseEvents);
        cv->addWidget(t);
        cv->addWidget(bl);
        cv->addStretch(1);

        auto* arrow = new QLabel(card);
        arrow->setPixmap(glyph(QStringLiteral("arrow"), tint, 15));
        arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
        arrow->setStyleSheet(QStringLiteral("background:transparent;"));
        cv->addWidget(arrow, 0, Qt::AlignRight);

        const QString seed = QLatin1String(o.seed);
        connect(card, &QPushButton::clicked, this, [this, seed] {
            if (!seed.isEmpty()) m_input->setPlainText(seed);
            focusComposer();
            QTextCursor c = m_input->textCursor();
            c.movePosition(QTextCursor::End);
            m_input->setTextCursor(c);
        });
        grid->addWidget(card, i / 2, i % 2);
    }
    v->addLayout(grid);

    if (qEnvironmentVariableIsSet("NATIVEOFFICE_AI_LAYOUT")) {
        QTimer::singleShot(3200, this, [this] {
            qInfo("[ai] panel %d rail-excluded scroll %d body %d hero %d",
                  width(), m_scroll ? m_scroll->viewport()->width() : -1,
                  m_chatBody ? m_chatBody->width() : -1,
                  m_hero ? m_hero->width() : -1);
            const auto cards = m_hero->findChildren<QPushButton*>();
            for (QPushButton* c : cards)
                qInfo("[ai]   card x=%d w=%d hint=%d minhint=%d",
                      c->x(), c->width(), c->sizeHint().width(),
                      c->minimumSizeHint().width());
        });
    }

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
    bv->setContentsMargins(12, 10, 12, 10);
    bv->setSpacing(9);

    // A line naming what the box is for, above the box itself. It carries the
    // mode, so the composer says where the answer will land without the reader
    // having to look back up at the header.
    auto* capRow = new QWidget(box);
    capRow->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* capH = new QHBoxLayout(capRow);
    capH->setContentsMargins(0, 0, 0, 0);
    capH->setSpacing(6);
    auto* capIcon = new QLabel(capRow);
    capIcon->setPixmap(glyph(QStringLiteral("sparkle"), QColor(kAccentSoft), 13));
    capIcon->setStyleSheet(QStringLiteral("background:transparent;"));
    m_composerCaption = new QLabel(QStringLiteral("Ask Stasis"), capRow);
    m_composerCaption->setStyleSheet(QStringLiteral(
        "color:%1; font:600 11.5px 'Segoe UI'; background:transparent;")
        .arg(QLatin1String(kAccentSoft)));
    capH->addWidget(capIcon, 0);
    capH->addWidget(m_composerCaption, 0);
    capH->addStretch(1);
    bv->addWidget(capRow, 0);

    m_input = new QTextEdit(box);
    m_input->setPlaceholderText(QStringLiteral("Ask Stasis, or describe what to build"));
    m_input->setFrameShape(QFrame::NoFrame);
    m_input->setFixedHeight(62);
    m_input->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // Its own inset field inside the box, which is what separates the thing you
    // type in from the controls around it.
    m_input->setStyleSheet(QStringLiteral(
        "QTextEdit { background:#0A0A12; border:1px solid rgba(255,255,255,0.06);"
        "  border-radius:11px; padding:8px 10px; color:%1; font:13px 'Segoe UI'; }"
        "QScrollBar:vertical { width:6px; background:transparent; }"
        "QScrollBar::handle:vertical { background:#2A2A3A; border-radius:3px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }")
        .arg(QLatin1String(kTextHigh)));
    m_input->installEventFilter(this);
    bv->addWidget(m_input);

    auto* row = new QWidget(box);
    row->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* rh = new QHBoxLayout(row);
    rh->setContentsMargins(0, 0, 0, 0);
    rh->setSpacing(8);

    m_attach = new QPushButton(row);
    m_attach->setCursor(Qt::PointingHandCursor);
    m_attach->setFixedSize(30, 30);
    m_attach->setFocusPolicy(Qt::NoFocus);
    m_attach->setIcon(QIcon(glyph(QStringLiteral("clip"), QColor(kTextMid), 16)));
    m_attach->setIconSize(QSize(16, 16));
    m_attach->setToolTip(QStringLiteral("Attach up to 5 images or 2 files"));
    m_attach->setStyleSheet(QStringLiteral(
        "QPushButton { background:transparent;"
        "  border:1px solid rgba(255,255,255,0.09); border-radius:9px; }"
        "QPushButton:hover { background:rgba(255,255,255,0.06);"
        "  border-color:rgba(255,255,255,0.20); }"));
    connect(m_attach, &QPushButton::clicked, this, &AiSidebar::pickAttachments);

    m_quota = new QLabel(row);
    m_quota->setStyleSheet(QStringLiteral(
        "color:%1; font:10.5px 'Segoe UI'; background:transparent;")
        .arg(QLatin1String(kTextLow)));

    m_send = new QPushButton(QStringLiteral(" Send"), row);
    m_send->setCursor(Qt::PointingHandCursor);
    m_send->setFixedHeight(32);
    m_send->setIcon(QIcon(glyph(QStringLiteral("send"), QColor("#FFFFFF"), 14)));
    m_send->setIconSize(QSize(14, 14));
    m_send->setStyleSheet(QStringLiteral(
        "QPushButton { color:#FFFFFF; font:600 12px 'Segoe UI';"
        "  background:#7C3AED; border:none; border-radius:10px; padding:0 16px; }"
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

// What this turn did, in the shape the action bar and its window read.
//
// `full` is the raw reply, marker and all. The part before the marker is the
// model saying what it was about to do, which is the closest thing to its
// reasoning that ever reaches the client, and it is otherwise thrown away once
// the content is routed into the file. The part after it is what went in, and a
// few lines of that is what makes the record worth opening.
QJsonObject AiSidebar::buildTurnAction(const QString& kind, int written,
                                       const QString& full) {
    QString preamble, body;
    if (!aiIsDocumentReply(full, &body, &preamble)) preamble = full.trimmed();

    QJsonObject a{
        {QStringLiteral("kind"),   kind},
        {QStringLiteral("mode"),   modeName(m_mode)},
        {QStringLiteral("prompt"), m_turnPrompt},
        {QStringLiteral("at"),     QDateTime::currentDateTime()
                                       .toString(QStringLiteral("d MMM yyyy, HH:mm"))},
    };
    if (written > 0) a.insert(QStringLiteral("characters"), written);

    if (kind == QLatin1String("deck")) {
        // Every slide is one op line, so counting them is counting the deck.
        int slides = 0;
        const QStringList lines = body.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString& l : lines)
            if (l.contains(QLatin1String("\"op\":\"slide\""))) ++slides;
        if (slides > 0) a.insert(QStringLiteral("slides"), slides);
    }

    if (!preamble.isEmpty())
        a.insert(QStringLiteral("reasoning"), preamble.left(1200));
    if (!body.isEmpty())
        a.insert(QStringLiteral("preview"), body.left(1600));
    return a;
}

// ── behaviour ────────────────────────────────────────────────────────────────

void AiSidebar::setMode(AiMode mode) {
    m_mode = mode;
    m_modeChip->setText(QStringLiteral("In %1").arg(modeName(mode)));

    const bool edits = capabilityFor(mode) == AiCapability::Edit;
    m_input->setPlaceholderText(edits
        ? QStringLiteral("Ask Stasis, or describe what to build")
        : QStringLiteral("Ask Stasis anything"));

    if (m_hero) {
        if (auto* hint = m_hero->findChild<QLabel*>(QStringLiteral("heroHint"))) {
            // Say what is true HERE. One line covered every non-editing surface
            // and was wrong on most of them: on Home it said Stasis answers
            // questions "about what is open" when nothing is open, and it led
            // with what Stasis will not do, which reads as a refusal to someone
            // who came to the panel for help.
            QString line;
            switch (mode) {
            case AiMode::Home:
                line = QStringLiteral("Ask a question, think something through, or "
                                      "start a draft here. Open a file and Stasis "
                                      "can work in it with you.");
                break;
            case AiMode::Pdf:
                line = QStringLiteral("Ask about this PDF and Stasis will read it "
                                      "with you. Editing a PDF's text is not "
                                      "something it can do.");
                break;
            default:
                line = edits
                    ? QStringLiteral("Ask anything, or say what to write and Stasis "
                                     "builds it into your %1 file as you watch.")
                          .arg(modeName(mode))
                    : QStringLiteral("Ask anything about what you have open here.");
                break;
            }
            hint->setText(line);
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

void AiSidebar::devShowHistory() { showHistory(); }

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
    // The box carries a violet edge at rest and a brighter one with the caret in
    // it, which is what makes it read as the thing you act on.
    m_composerBox->setStyleSheet(QStringLiteral(
        "#composerBox { background:%1; border:1px solid %2; border-radius:16px; }")
        .arg(QStringLiteral("#0D0D15"),
             on ? QStringLiteral("rgba(139,92,246,0.75)")
                : QStringLiteral("rgba(139,92,246,0.32)")));
}

void AiSidebar::refreshHeroVisibility() {
    if (m_hero) m_hero->setVisible(m_history.isEmpty());
}

QWidget* AiSidebar::appendMessage(const AiMessage& m) {
    auto* b = new Bubble(m.text, m.role == AiMessage::Role::User, m_chatBody);
    // Inserted before the working row and the trailing stretch so the "working"
    // line always sits at the very bottom of the transcript.
    const int idx = m_chatLay->indexOf(m_workRow);
    m_chatLay->insertWidget(idx < 0 ? m_chatLay->count() - 1 : idx, b);
    if (m.role == AiMessage::Role::Assistant) {
        m_streamTarget = b->label();
        m_streamBubble = b;
    }
    refreshHeroVisibility();
    scrollToBottom();
    return b;
}

// ── "What Stasis did" ───────────────────────────────────────────────────────
//
// A reply that went into the file says almost nothing in the transcript: the
// bubble reads "Built into your presentation." and everything of substance is
// in the document. Reopened months later that is a question with no answer
// attached to it. The bar records what the turn actually did, and it is stored
// with the turn so it is still there when the chat is opened again.

QString AiSidebar::actionSummary(const QJsonObject& a) {
    const QString kind = a.value(QStringLiteral("kind")).toString();
    const QString where = a.value(QStringLiteral("mode")).toString();
    if (kind == QLatin1String("deck")) {
        const int slides = a.value(QStringLiteral("slides")).toInt();
        return slides > 0 ? QStringLiteral("Built %1 slide%2 into your presentation")
                                .arg(slides).arg(slides == 1 ? "" : "s")
                          : QStringLiteral("Built into your presentation");
    }
    if (kind == QLatin1String("document")) {
        const int chars = a.value(QStringLiteral("characters")).toInt();
        return chars > 0 ? QStringLiteral("Wrote %1 characters into your %2 document")
                               .arg(chars).arg(where.isEmpty() ? QStringLiteral("open")
                                                               : where)
                         : QStringLiteral("Wrote into your document");
    }
    return QStringLiteral("Answered in the chat");
}

void AiSidebar::attachActionBar(QWidget* bubble, const QJsonObject& action) {
    if (!bubble || action.isEmpty()) return;

    auto* bar = new QPushButton(actionSummary(action), m_chatBody);
    bar->setCursor(Qt::PointingHandCursor);
    bar->setFocusPolicy(Qt::NoFocus);
    bar->setStyleSheet(QStringLiteral(
        "QPushButton { text-align:left; padding:6px 11px; border-radius:8px;"
        "  color:#9FA9C0; font:11px 'Segoe UI';"
        "  background:rgba(255,255,255,0.045);"
        "  border:1px solid rgba(255,255,255,0.07); }"
        "QPushButton:hover { background:rgba(124,92,255,0.16);"
        "  border:1px solid rgba(124,92,255,0.40); color:#D9D3FF; }"));

    const QJsonObject copy = action;
    connect(bar, &QPushButton::clicked, this, [this, copy] { showActionDetail(copy); });

    const int at = m_chatLay->indexOf(bubble);
    m_chatLay->insertWidget(at < 0 ? m_chatLay->count() - 1 : at + 1, bar);
    scrollToBottom();
}

void AiSidebar::showActionDetail(const QJsonObject& action) {
    QDialog dlg(window());
    dlg.setWindowTitle(QStringLiteral("What Stasis did"));
    dlg.setModal(true);
    dlg.setMinimumWidth(520);
    dlg.setStyleSheet(QStringLiteral(
        "QDialog { background:#0F131B; }"
        "QLabel { color:#C9CFDB; font:12.5px 'Segoe UI'; background:transparent; }"));

    auto* v = new QVBoxLayout(&dlg);
    v->setContentsMargins(24, 22, 24, 20);
    v->setSpacing(0);

    auto* head = new QLabel(actionSummary(action), &dlg);
    head->setWordWrap(true);
    head->setStyleSheet(QStringLiteral(
        "color:#F2F0FF; font:600 16px 'Segoe UI'; background:transparent;"));
    v->addWidget(head);

    const QString when = action.value(QStringLiteral("at")).toString();
    if (!when.isEmpty()) {
        auto* sub = new QLabel(when, &dlg);
        sub->setStyleSheet(QStringLiteral(
            "color:#6E7890; font:11px 'Segoe UI'; background:transparent;"));
        v->addSpacing(3);
        v->addWidget(sub);
    }
    v->addSpacing(18);

    auto section = [&](const QString& title, const QString& body) {
        if (body.trimmed().isEmpty()) return;
        auto* t = new QLabel(title, &dlg);
        t->setStyleSheet(QStringLiteral(
            "color:#AFA3FF; font:600 11px 'Segoe UI'; background:transparent;"));
        v->addWidget(t);
        v->addSpacing(5);

        auto* box = new QTextEdit(&dlg);
        box->setReadOnly(true);
        box->setPlainText(body);
        box->setFrameShape(QFrame::NoFrame);
        box->setMaximumHeight(170);
        box->setStyleSheet(QStringLiteral(
            "QTextEdit { color:#C9CFDB; font:12px 'Segoe UI'; border-radius:9px;"
            "  background:rgba(255,255,255,0.035); padding:9px 11px;"
            "  border:1px solid rgba(255,255,255,0.06); }"
            "QScrollBar:vertical { width:8px; background:transparent; }"
            "QScrollBar::handle:vertical { background:#333C4D; border-radius:4px; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"));
        v->addWidget(box);
        v->addSpacing(14);
    };

    section(QStringLiteral("YOU ASKED"), action.value(QStringLiteral("prompt")).toString());
    section(QStringLiteral("HOW STASIS READ IT"),
            action.value(QStringLiteral("reasoning")).toString());
    section(QStringLiteral("WHAT IT PUT IN THE FILE"),
            action.value(QStringLiteral("preview")).toString());

    v->addStretch(1);
    auto* close = new QPushButton(QStringLiteral("Close"), &dlg);
    close->setCursor(Qt::PointingHandCursor);
    close->setFixedHeight(32);
    close->setStyleSheet(QStringLiteral(
        "QPushButton { border:none; border-radius:8px; padding:0 20px;"
        "  background:rgba(124,92,255,0.22); color:#D9D3FF;"
        "  font:600 12px 'Segoe UI'; }"
        "QPushButton:hover { background:rgba(124,92,255,0.34); color:#FFFFFF; }"));
    connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);

    auto* row = new QHBoxLayout();
    row->addStretch(1);
    row->addWidget(close);
    v->addLayout(row);

    dlg.exec();
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
    if (m_chatId.isEmpty()) m_chatId = AiChatStore::mintId();
    if (firstTurn)
        m_chats->save(m_chatId, AiChatStore::titleFrom(text), modeName(m_mode));
    m_chats->appendTurn(m_chatId, /*fromUser=*/true, text);

    // Held for the turn so the "what Stasis did" record can quote the request
    // it was answering. The transcript alone cannot supply it later: by the
    // time the answer lands the prompt is several widgets up the panel.
    m_turnPrompt = text;
    m_turnAction = QJsonObject();

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
    // Opener titles elide to whatever room the panel currently gives them. The
    // text is re-elided from the original on every resize, never from what is
    // already shown, or a couple of narrowings would eat it a word at a time.
    if (e->type() == QEvent::Resize) {
        if (auto* lab = qobject_cast<QLabel*>(o)) {
            const QString full = lab->property("fullText").toString();
            if (!full.isEmpty())
                lab->setText(lab->fontMetrics().elidedText(
                    full, Qt::ElideRight, lab->width()));
        }
    }
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
