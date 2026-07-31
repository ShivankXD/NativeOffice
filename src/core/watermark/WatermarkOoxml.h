#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// WatermarkOoxml.h — the watermark as OOXML parts, shared by the .docx, .xlsx
// and .pptx writers.
//
// All three embed the same PNG rather than rebuilding the mark out of text
// runs. That is deliberate: the wordmark's "Office" is a gradient, and OOXML
// gradient text fill (w14:textFill and friends) is inconsistently supported,
// so a run-based mark would render differently in Word, Excel, PowerPoint,
// LibreOffice and WPS. One picture renders identically everywhere and matches
// the PDF output.
//
// It also makes the hyperlink exact. The link goes on the picture, so its hit
// area is the picture's extent, which is the artwork's bounding box and nothing
// more. A run-based mark would have leaked the link into the surrounding
// paragraph's trailing whitespace.
//
// Hyperlinks here carry no styling of their own: a picture hyperlink has no
// text to colour or underline, so the mark looks the same whether or not the
// link is present.
// ─────────────────────────────────────────────────────────────────────────────

#include <QByteArray>
#include <QSize>
#include <QString>

namespace NativeOffice {
namespace Watermark {
namespace Ooxml {

// PNG of the mark, rendered once and cached for the process.
const QByteArray& pngBytes();
QSize             pngSizePx();

// Mark size in EMU (English Metric Units, 914400 per inch), the unit every
// OOXML drawing uses for extents.
qint64 widthEmu();
qint64 heightEmu();

// A relationship element pointing at the target URL. TargetMode="External" is
// what makes it a hyperlink rather than a part reference.
QString hyperlinkRel(const QString& relId);

// Relationship element for the embedded PNG.
QString imageRel(const QString& relId, const QString& target);

// ── Word ────────────────────────────────────────────────────────────────────
// A complete footer part. The mark is right-aligned in a borderless paragraph,
// so it lands in the bottom-right of every page the footer is applied to.
QByteArray docxFooterXml(const QString& imageRelId, const QString& linkRelId);

// ── PowerPoint ──────────────────────────────────────────────────────────────
// A <p:pic> for the slide tree, positioned bottom-right of a slide of the
// given size. shapeId must be unique within the slide.
QString pptxPicXml(int shapeId, const QString& imageRelId, const QString& linkRelId,
                   qint64 slideWEmu, qint64 slideHEmu);

// ── Excel ───────────────────────────────────────────────────────────────────
// A complete drawing part holding the linked mark, anchored so its bottom-right
// sits just past the given last used cell. This is the clickable copy; the
// repeated-on-every-printed-page copy is a footer graphic, which Excel's format
// gives no way to hyperlink.
QByteArray xlsxDrawingXml(const QString& imageRelId, const QString& linkRelId,
                          int anchorCol, int anchorRow);

// ── Excel: the repeated footer graphic ──────────────────────────────────────
// Excel repeats a header/footer picture on every printed page, which the
// anchored drawing above cannot do. The two have to be separate elements:
// footer graphics are referenced by a VML legacyDrawingHF part and the format
// provides no way to attach a hyperlink to one. So this copy is the one that
// repeats, and the drawing above is the one that is clickable.

// <headerFooter> element naming the graphic. "&R&G" places it right-aligned.
QString xlsxHeaderFooterXml();

// The VML part the footer graphic is drawn from. o:relid points at the image
// relationship in this part's own rels.
QByteArray xlsxFooterVml(const QString& imageRelId);

} // namespace Ooxml
} // namespace Watermark
} // namespace NativeOffice
