#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ImageResizer.h — Home-screen tool: batch image resize / convert / compress.
//
//  ┌───────────────────┬────────────────────────────────────────────┐
//  │ [+] [sort] [🗑]    │                                            │
//  │ Resize Settings   │        White dotted canvas                 │
//  │  By Size | As %   │   ┌──────────────────┐                     │
//  │  W / H / lock     │   │  Upload Images   │  (or thumbnails)    │
//  │  Background Fill  │   └──────────────────┘                     │
//  │ Export Settings   │   "drag or click + to add images"          │
//  │  target size / fmt│                                            │
//  │ [    Export →   ] │                                            │
//  └───────────────────┴────────────────────────────────────────────┘
//
// By Size: exact px targets; with Lock Aspect Ratio off, an optional
// Background Fill (colour / transparent) pads instead of stretching.
// As Percentage: uniform scale. Export writes <name>_resized.<ext> to a
// chosen folder; JPG can be squeezed under a target KB/MB budget.
// ─────────────────────────────────────────────────────────────────────────────

#include <QWidget>
#include <QColor>
#include <QStringList>
#include <utility>
#include <vector>

class QLineEdit;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QToolButton;
class QRadioButton;
class QFrame;
class QStackedWidget;

namespace NativeOffice {

class ImageResizerWidget : public QWidget {
    Q_OBJECT

public:
    explicit ImageResizerWidget(QWidget* parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent* ev) override;
    void dropEvent(QDropEvent* ev) override;

private:
    QWidget* buildSettingsPanel();
    QWidget* buildCanvas();
    void applyStyles();

    void addImagesDialog();
    void addImages(const QStringList& paths);
    void clearAll();
    void sortImages();
    void refreshList();
    void updateUiState();
    void updateTargetBadges();
    void setMode(bool bySize);
    void pickFillColor();
    void exportImages();

    // Resize one source image according to the current settings.
    [[nodiscard]] QImage transform(const QImage& src) const;
    // The output size the current settings produce for a given source size
    // (drives the per-card "orig → target" badges without reloading pixels).
    [[nodiscard]] QSize computeTargetSize(const QSize& src) const;

    // ── data ───────────────────────────────────────────────────────────
    QStringList m_paths;
    bool   m_sortAscending { true };
    QColor m_fillColor     { Qt::black };

    // ── settings panel ─────────────────────────────────────────────────
    QToolButton*  m_btnSort     { nullptr };
    QToolButton*  m_btnClear    { nullptr };
    QLabel*       m_countLabel  { nullptr };
    QToolButton*  m_tabSize     { nullptr };
    QToolButton*  m_tabPercent  { nullptr };
    QWidget*      m_pageSize    { nullptr };
    QWidget*      m_pagePercent { nullptr };
    QLineEdit*    m_widthEdit   { nullptr };
    QLineEdit*    m_heightEdit  { nullptr };
    QCheckBox*    m_lockRatio   { nullptr };
    QFrame*       m_fillGroup   { nullptr };
    QCheckBox*    m_fillEnabled { nullptr };
    QRadioButton* m_fillPick    { nullptr };
    QRadioButton* m_fillTransparent { nullptr };
    QToolButton*  m_fillSwatch  { nullptr };
    QLabel*       m_fillHex     { nullptr };
    QSpinBox*     m_percentSpin { nullptr };
    QLineEdit*    m_targetSize  { nullptr };
    QComboBox*    m_targetUnit  { nullptr };
    QComboBox*    m_formatCombo { nullptr };
    QPushButton*  m_exportBtn   { nullptr };

    // ── canvas ─────────────────────────────────────────────────────────
    QStackedWidget* m_canvasStack { nullptr };
    QListWidget*    m_thumbList   { nullptr };
    // Per-card target-size badge + the source image size it reports on.
    std::vector<std::pair<QLabel*, QSize>> m_targetBadges;
};

} // namespace NativeOffice
