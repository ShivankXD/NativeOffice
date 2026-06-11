#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TemplateBarWidget.h
// Horizontal "quick-create" bar shown at the top of the Start Screen content
// area. Contains three large clickable tiles: Writer (blue), Calc (green),
// and Impress (orange/red).
// ─────────────────────────────────────────────────────────────────────────────

#include "core/application/AppController.h"

#include <QWidget>
#include <QHBoxLayout>
#include <QPushButton>

namespace NativeOffice {

class TemplateBarWidget : public QWidget {
    Q_OBJECT

public:
    explicit TemplateBarWidget(QWidget* parent = nullptr);

signals:
    void createDocument(DocumentType type);

private:
    void buildUi();
    QPushButton* makeTemplateButton(const QString& emoji,
                                    const QString& title,
                                    const QString& subtitle,
                                    const QString& bgColor,
                                    const QString& hoverColor,
                                    DocumentType   type);

    QHBoxLayout* m_layout { nullptr };
};

} // namespace NativeOffice
