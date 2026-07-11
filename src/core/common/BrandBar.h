#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// BrandBar.h — the unified brand tray shown above the ribbon in every editor
// (Writer, Calc, Impress, PDF). One implementation so all modules stay
// pixel-identical: the NativeOffice brand mark + two-tone wordmark on the
// left, soft decorative art on the right, and a Free/Premium plan pill pinned
// to the far right edge. Follows the chrome light/dark mode and updates live
// when the user's entitlement changes.
// ─────────────────────────────────────────────────────────────────────────────

#include <QPixmap>
#include <QWidget>

class QLabel;
class QLineEdit;

namespace NativeOffice {

class BrandBar : public QWidget {
    Q_OBJECT
public:
    explicit BrandBar(QWidget* parent = nullptr);

    // Show an editable document name (e.g. "untitled") just after the wordmark,
    // with the immutable format suffix (".docx", ".md", …) pinned outside the
    // field. An empty ext hides the whole rename control (used by tools).
    void setDocName(const QString& base, const QString& ext);

signals:
    // Emitted as the user edits the name field (live).
    void docNameEdited(const QString& base);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    void styleNameField();        // theme the rename field for light/dark chrome
    void layoutNameField();       // position the field + suffix after the wordmark

    QPixmap    m_mark;            // brand mark cropped out of the full logo art
    bool       m_premium { false };
    QLineEdit* m_nameEdit { nullptr };
    QLabel*    m_extLabel { nullptr };
};

} // namespace NativeOffice
