#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// QrCodeGenerator.h — Home → Tools → QR Code Generator.
//
//  ┌──────────────────────────┬─────────────────────────────────────────┐
//  │ Content                  │                                         │
//  │  [Text/URL][Wi-Fi][Mail] │            ▄▄▄▄  ▄ ▄▄▄▄                 │
//  │  ...fields...            │            ▄  ▄  ▄▄  ▄                  │
//  │ Error correction L M Q H │            live preview                 │
//  │ Colours  ■ dark  ■ light │                                         │
//  │ Module size / quiet zone │      version 3 · 29x29 modules          │
//  │                          │  [ Save PNG ] [ Save SVG ] [ Copy ]     │
//  └──────────────────────────┴─────────────────────────────────────────┘
//
// The symbol is produced by QrEncoder (no third-party dependency) and redrawn
// on every keystroke. PNG export renders at the chosen module size; SVG export
// writes one rectangle per dark module, so the result stays sharp at any size.
// ─────────────────────────────────────────────────────────────────────────────

#include "QrEncoder.h"

#include <QColor>
#include <QImage>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QStackedWidget;
class QToolButton;
class QComboBox;
class QCheckBox;
class QPushButton;

namespace NativeOffice {

class QrCodeGeneratorWidget : public QWidget {
    Q_OBJECT

public:
    explicit QrCodeGeneratorWidget(QWidget* parent = nullptr);

private:
    enum Kind { TextUrl = 0, WiFi, Email, Phone, Sms };

    QWidget* buildSidePanel();
    QWidget* buildPreview();
    QWidget* buildKindPage(Kind kind);

    // The payload string the current tab describes, e.g.
    // "WIFI:T:WPA;S:<ssid>;P:<pass>;;".
    [[nodiscard]] QString payload() const;
    [[nodiscard]] QImage  render(int modulePx) const;

    void regenerate();
    void pickColor(bool foreground);
    void savePng();
    void saveSvg();
    void copyImage();

    Kind m_kind { TextUrl };

    QStackedWidget* m_pages   { nullptr };
    QPlainTextEdit* m_text    { nullptr };
    QLineEdit*      m_ssid    { nullptr };
    QLineEdit*      m_wifiPw  { nullptr };
    QComboBox*      m_wifiSec { nullptr };
    QCheckBox*      m_wifiHidden { nullptr };
    QLineEdit*      m_mailTo  { nullptr };
    QLineEdit*      m_mailSub { nullptr };
    QLineEdit*      m_mailBody{ nullptr };
    QLineEdit*      m_phone   { nullptr };
    QLineEdit*      m_smsTo   { nullptr };
    QLineEdit*      m_smsBody { nullptr };

    QToolButton*    m_fgSwatch { nullptr };
    QToolButton*    m_bgSwatch { nullptr };
    QSpinBox*       m_modulePx { nullptr };
    QCheckBox*      m_quietZone{ nullptr };

    QLabel*         m_preview { nullptr };
    QLabel*         m_caption { nullptr };
    QPushButton*    m_savePng { nullptr };
    QPushButton*    m_saveSvg { nullptr };
    QPushButton*    m_copyBtn { nullptr };

    QColor m_fg { QStringLiteral("#0B0E14") };
    QColor m_bg { QStringLiteral("#FFFFFF") };
    Qr::Ecc m_ecc { Qr::Ecc::Medium };
    Qr::Code m_code;
};

} // namespace NativeOffice
