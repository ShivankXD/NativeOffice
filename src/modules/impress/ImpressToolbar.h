#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ImpressToolbar.h  (Sprint 5)
// Charcoal-themed toolbar at the top of the Impress module.
//
// Buttons:
//   "T"   — Add Text Box  (InsertMode::TextBox)
//   "☐"   — Add Rectangle (InsertMode::Rectangle)
//   "◯"   — Add Ellipse   (InsertMode::Ellipse)
//   (separator)
//   "Dup" — Duplicate current slide
//   "Del" — Delete current slide
//
// Each shape button is checkable; only one is active at a time.
// Signals insertModeChanged(mode) when the user picks a shape tool.
// Signals duplicateSlideRequested() / deleteSlideRequested().
// ─────────────────────────────────────────────────────────────────────────────

#include "SlideScene.h"   // for InsertMode

#include <QWidget>

class QToolButton;
class QButtonGroup;

namespace NativeOffice {

class ImpressToolbar : public QWidget {
    Q_OBJECT

public:
    explicit ImpressToolbar(QWidget* parent = nullptr);

    // Call when the scene resets insert mode (so the button un-checks)
    void resetInsertMode();

signals:
    void insertModeChanged(NativeOffice::InsertMode mode);
    void duplicateSlideRequested();
    void deleteSlideRequested();

private:
    QToolButton* makeButton(const QString& label,
                            const QString& tooltip,
                            bool checkable = false);

    QToolButton* m_btnText   { nullptr };
    QToolButton* m_btnRect   { nullptr };
    QToolButton* m_btnEllipse{ nullptr };
    QButtonGroup* m_shapeGroup{ nullptr };
};

} // namespace NativeOffice
