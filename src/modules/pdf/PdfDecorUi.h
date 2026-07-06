#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfDecorUi.h — the dialogs for the decoration features:
//   • runWatermarkUi     — WPS-style panel: Custom "Add" (text/image editor),
//                          a Preset grid (Confidential / Top Secret / Do Not
//                          Copy / Internal Information, plain + tiled — all
//                          fully available, no paywall), Update and Delete.
//   • runBackgroundUi    — page background color (set / remove).
//   • runPageNumberUi    — position/format/start/size.
//   • runHeaderFooterUi  — 6 fields with &[Page]/&[Pages]/&[Date] macros.
// Each function drives the edit through the session and reports via `toast`.
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>
#include <functional>

class QWidget;

namespace NativeOffice::Pdf {

class EditSession;

void runWatermarkUi(QWidget* parent, EditSession* session,
                    const std::function<void(const QString&)>& toast);
void runBackgroundUi(QWidget* parent, EditSession* session,
                     const std::function<void(const QString&)>& toast);
void runPageNumberUi(QWidget* parent, EditSession* session,
                     const std::function<void(const QString&)>& toast);
void runHeaderFooterUi(QWidget* parent, EditSession* session,
                       const std::function<void(const QString&)>& toast);

} // namespace NativeOffice::Pdf
