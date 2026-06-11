#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SidebarWidget.h
// Left navigation sidebar for the NativeOffice Start Screen.
// Styled with the brand Primary color (#2C3140) and Scarlet accent (#E8372A).
// ─────────────────────────────────────────────────────────────────────────────

#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QButtonGroup>
#include <vector>

namespace NativeOffice {

enum class SidebarItem {
    Home,
    New,
    Open,
    Settings
};

class SidebarWidget : public QWidget {
    Q_OBJECT

public:
    explicit SidebarWidget(QWidget* parent = nullptr);

    void setActiveItem(SidebarItem item);

signals:
    void itemSelected(SidebarItem item);

private:
    void buildUi();
    QPushButton* makeNavButton(const QString& iconText,
                               const QString& label,
                               SidebarItem   item);
    void applyStyles();

    QVBoxLayout*              m_layout      { nullptr };
    QWidget*                  m_logoArea    { nullptr };
    QWidget*                  m_navArea     { nullptr };
    QButtonGroup*             m_btnGroup    { nullptr };

    QPushButton* m_btnHome     { nullptr };
    QPushButton* m_btnNew      { nullptr };
    QPushButton* m_btnOpen     { nullptr };
    QPushButton* m_btnSettings { nullptr };

    SidebarItem m_activeItem { SidebarItem::Home };
};

} // namespace NativeOffice
