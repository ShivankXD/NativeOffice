#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AppController.h
// Top-level application controller: owns the main window, manages module
// lifecycle, and routes navigation events from the Start Screen.
// ─────────────────────────────────────────────────────────────────────────────

#include <QObject>
#include <QString>
#include <memory>

namespace NativeOffice {

enum class DocumentType {
    Writer,    // Word processor
    Calc,      // Spreadsheet
    Impress,   // Presentation
    Pdf        // PDF tool hub
};

class AppController : public QObject {
    Q_OBJECT

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    // Show the Start Screen (called on first launch)
    void showStartScreen();

    // Open a new document of the given type
    void newDocument(DocumentType type);

    // Open an existing file from disk
    void openFile(const QString& filePath);

    // Open application settings
    void openSettings();

signals:
    void startScreenRequested();
    void newDocumentRequested(DocumentType type);
    void fileOpenRequested(const QString& filePath);
    void settingsRequested();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace NativeOffice
