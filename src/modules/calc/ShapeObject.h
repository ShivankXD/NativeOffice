#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ShapeObject.h
// Draws one DrawingML shape on the sheet: the banners, buttons, callouts and
// rules that a real workbook is full of and that used to be dropped on import.
//
// A shape is decoration, not a document object, so this widget is deliberately
// transparent to the mouse. Excel lets a shape swallow clicks, but a shape here
// is an approximation of the file's geometry, and an approximation that covers
// a block of cells and eats every click on them is worse than one that does
// not. Clicks go to the cells underneath; the shape just gets out of the way.
// ─────────────────────────────────────────────────────────────────────────────

#include <QPixmap>
#include <QWidget>

#include "ChartSpec.h"

class QTextDocument;

namespace NativeOffice {

class ShapeObject : public QWidget {
    Q_OBJECT
public:
    explicit ShapeObject(const SheetShape& shape, QWidget* parent = nullptr);
    ~ShapeObject() override;

    [[nodiscard]] const SheetShape& shape() const { return m_shape; }

    // Text in a shape scales with the sheet, the way it does in Excel: the box
    // is sized by its anchor, so a caption that kept its point size would spill
    // out of it at 70% and swim in it at 200%.
    void setZoom(double z);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    void rebuildText();
    [[nodiscard]] QPainterPath outline(const QRectF& r) const;

    SheetShape     m_shape;
    QPixmap        m_fillPixmap;   // decoded once; empty unless the fill is a picture
    QTextDocument* m_doc { nullptr };
    double         m_zoom { 1.0 };
};

} // namespace NativeOffice
