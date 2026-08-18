#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfToolPage.h — Home → Tools → Compress PDF / OCR & Scan / PDF to Word.
//
// The engines behind all three already shipped inside the PDF editor
// (Pdf::compressPdf, Pdf::ocrPdf, Pdf::toDocx), but they were only reachable
// after opening a document into the full ribbon. These give each one a direct
// page: drop a file in, set the one option that matters, run, see the result.
//
//  ┌──────────────────────────────────────────────────────────────────┐
//  │   ▣  Compress PDF                                                │
//  │   Squeeze a PDF without touching image quality.                  │
//  │                                                                  │
//  │   ┌────────────────────────────────────────────────────────┐     │
//  │   │        Drop a PDF here, or click to choose             │     │
//  │   └────────────────────────────────────────────────────────┘     │
//  │   report.pdf   4.8 MB   ·   12 pages                             │
//  │   Effort  ○ Fast  ● Balanced  ○ Smallest                         │
//  │                                          [   Compress   ]        │
//  │   ✓ Saved report-compressed.pdf — 4.8 MB → 3.1 MB (36% smaller)  │
//  └──────────────────────────────────────────────────────────────────┘
//
// The work runs on a worker thread so a 200-page OCR pass does not freeze the
// window; progress is reported back on the UI thread.
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>
#include <QWidget>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QFrame;

namespace NativeOffice {

class PdfToolPage : public QWidget {
    Q_OBJECT

public:
    enum class Job {
        Compress,   // Pdf::compressPdf
        Ocr,        // Pdf::ocrPdf   (Windows OCR engine)
        ToWord      // Pdf::toDocx
    };

    explicit PdfToolPage(Job job, QWidget* parent = nullptr);

    // Title/subtitle for the tool, also used by the window wrapper.
    static QString jobTitle(Job job);
    static QString jobSubtitle(Job job);

protected:
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;
    // The drop zone is a plain QFrame, so its click comes through here.
    bool eventFilter(QObject*, QEvent*) override;

private:
    void chooseFile();
    void setSource(const QString& path);
    void run();                       // ask where, then runTo()
    void runTo(const QString& out);   // do the work on a worker thread
    void report(bool ok, const QString& message);
    void setBusy(bool busy);

    [[nodiscard]] QString defaultOutputPath() const;

    Job     m_job;
    QString m_source;
    qint64  m_sourceBytes { 0 };

    QFrame*       m_drop     { nullptr };
    QLabel*       m_dropText { nullptr };
    QLabel*       m_fileInfo { nullptr };
    QComboBox*    m_option   { nullptr };
    QLabel*       m_optionCaption { nullptr };
    QPushButton*  m_runBtn   { nullptr };
    QProgressBar* m_progress { nullptr };
    QLabel*       m_result   { nullptr };
};

} // namespace NativeOffice
