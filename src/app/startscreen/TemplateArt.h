#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TemplateArt.h: painted preview thumbnails for the template gallery.
//
// The old cards showed the same three flat SVG blobs behind every name, so a
// resume and a press release looked identical. These paint a miniature of the
// document itself: a page with a header band, a name block, ruled body lines,
// a table or a chart, the shapes that tell you at a glance what you are about
// to open.
//
// The layout comes from the template's own kind (resume, letter, report,
// budget, invoice, dashboard, deck…), inferred from its name, so adding a
// template to the list is enough to give it a fitting thumbnail. A hash of the
// name nudges the accent and the line pattern, so two reports never look like
// carbon copies.
// ─────────────────────────────────────────────────────────────────────────────

#include "core/application/AppController.h"

#include <QPixmap>
#include <QSize>
#include <QString>

namespace NativeOffice::TemplateArt {

// A preview of `name` at `size` logical pixels, ready for a QLabel.
QPixmap preview(const QString& name, DocumentType type, QSize size, qreal dpr);

} // namespace NativeOffice::TemplateArt
