#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// HeroBanner.h — the wide greeting band at the top of the home dashboard.
//
//  ┌──────────────────────────────────────────────────────────────────────┐
//  │  Good evening, Shivank 👋                     ░░▒▒▓▓ photo ▓▓▒▒░░    │
//  │  Where ideas become documents.                                       │
//  │  [ + Create New  ▾ ]  [ ⤒ Open File ]                                │
//  └──────────────────────────────────────────────────────────────────────┘
//
// The background is one of four photographs chosen by the clock (morning /
// afternoon / evening / night), cover-cropped to the band and darkened on the
// left by a horizontal scrim so the greeting keeps its contrast at any window
// width. A one-minute timer re-checks the slot, so the picture and the
// greeting change with the time of day without restarting the app.
//
// "Create New" opens a menu (Document / Spreadsheet / Presentation /
// Markdown) from the whole button as well as from its chevron. "Open File"
// replaced the old "Import File" that used to sit off to the right, which is
// what frees the space this band occupies.
// ─────────────────────────────────────────────────────────────────────────────

#include "core/application/AppController.h"

#include <QFrame>
#include <QPixmap>

class QLabel;
class QPushButton;

namespace NativeOffice {

class HeroBanner : public QFrame {
    Q_OBJECT

public:
    // Markdown is not an AppController DocumentType (it is a home tool, not a
    // module), so the menu reports its choice through this enum instead.
    enum class Create { Document, Spreadsheet, Presentation, Markdown };

    explicit HeroBanner(QWidget* parent = nullptr);

    // Re-read the signed-in name and the clock. Called on profile changes and
    // on the internal minute tick.
    void refresh();

    // Drop the Create New menu, as if the button had been pressed.
    void openCreateMenu() { showCreateMenu(); }

signals:
    void createRequested(HeroBanner::Create what);
    void openFileRequested();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    enum class Slot { Morning, Afternoon, Evening, Night };

    static Slot  slotForNow();
    static QString imageForSlot(Slot s);
    static QString greetingForSlot(Slot s);

    void showCreateMenu();

    QLabel*      m_greeting  { nullptr };
    QLabel*      m_subtitle  { nullptr };
    QPushButton* m_createBtn { nullptr };
    QPixmap m_art;                     // full-size source for the current slot
    QPixmap m_scaled;                  // cover-cropped cache for the current size
    Slot    m_slot { Slot::Morning };
    bool    m_slotValid { false };
};

} // namespace NativeOffice
