// ─────────────────────────────────────────────────────────────────────────────
// AppController.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "AppController.h"

#include <QDebug>

namespace NativeOffice {

struct AppController::Impl {
    // Future: hold weak refs to open editor windows
};

AppController::AppController(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Impl>())
{}

AppController::~AppController() = default;

void AppController::showStartScreen() {
    emit startScreenRequested();
}

void AppController::newDocument(DocumentType type) {
    qDebug() << "[AppController] New document requested:"
             << static_cast<int>(type);
    emit newDocumentRequested(type);
}

void AppController::openFile(const QString& filePath) {
    qDebug() << "[AppController] Open file:" << filePath;
    emit fileOpenRequested(filePath);
}

void AppController::openSettings() {
    qDebug() << "[AppController] Settings requested";
    emit settingsRequested();
}

} // namespace NativeOffice
