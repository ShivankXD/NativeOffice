#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfSignature.h — the Fill & Sign signature/initials workflow.
//
//   SignaturePad     — a small draw surface capturing a freehand signature.
//   SignatureLibrary — persists created signatures as transparent PNGs under
//                      the app data dir, so they're reusable across sessions.
//   runSignatureDialog — the "Add Signature/Initials" chooser: create a new
//                      one (Draw / Type / Import Image) or pick a saved one.
//                      Returns the chosen signature as a QImage (with alpha).
//
// The module places the returned image as a Picture (Stamp) annotation via
// the existing annotation engine, so signatures render everywhere and are
// removable like any other annotation.
// ─────────────────────────────────────────────────────────────────────────────

#include <QImage>
#include <QString>
#include <QStringList>
#include <QWidget>

class QPaintEvent;
class QMouseEvent;

namespace NativeOffice::Pdf {

// A transparent-background draw surface for a signature stroke.
class SignaturePad : public QWidget {
    Q_OBJECT
public:
    explicit SignaturePad(QWidget* parent = nullptr);

    void clear();
    [[nodiscard]] bool isEmpty() const { return m_empty; }
    // Tightly-cropped signature on a transparent background (null if empty).
    [[nodiscard]] QImage signatureImage() const;

    void setPenColor(const QColor& c) { m_penColor = c; update(); }

signals:
    void changed();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    QImage m_canvas;
    QColor m_penColor { "#0B3D91" };   // ink blue
    bool   m_drawing = false;
    bool   m_empty = true;
    QPointF m_last;
    void ensureCanvas();
};

// Persistent library of saved signatures (PNG files in AppDataLocation).
class SignatureLibrary {
public:
    static QString dir();                       // creates it on first use
    static QStringList savedFiles();            // absolute paths, newest first
    static QString save(const QImage& img);     // returns the stored path
    static void remove(const QString& path);
};

// Renders `text` as a signature-style image (cursive-ish font, ink color).
QImage typedSignatureImage(const QString& text, const QColor& color = QColor("#0B3D91"));

// The chooser dialog. Returns a null image if cancelled. `initials` tweaks
// the labels/placeholders for the Add-Initials variant.
QImage runSignatureDialog(QWidget* parent, bool initials);

} // namespace NativeOffice::Pdf
