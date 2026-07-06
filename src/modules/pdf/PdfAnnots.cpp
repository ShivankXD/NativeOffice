// ─────────────────────────────────────────────────────────────────────────────
// PdfAnnots.cpp — see PdfAnnots.h. Every annotation is emitted with an
// explicit appearance stream (/AP /N) so third-party viewers render it
// identically to ours; the semantic dict (Subtype/QuadPoints/etc.) is filled
// in too so Acrobat's comment tools still recognize them.
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfAnnots.h"
#include "PdfDecor.h"        // helveticaTextWidthPt (shared AFM metric)
#include "PdfRebuild.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>
#include <cmath>

namespace NativeOffice::Pdf {

using namespace NativeOffice::Pdf::rebuild;

namespace {

constexpr char kApFont[] = "NOannF1";

QByteArray num(double v) { return QByteArray::number(v, 'f', 3); }
QByteArray rg(const QColor& c) {
    return num(c.redF()) + " " + num(c.greenF()) + " " + num(c.blueF()) + " rg\n";
}
QByteArray RG(const QColor& c) {
    return num(c.redF()) + " " + num(c.greenF()) + " " + num(c.blueF()) + " RG\n";
}

QByteArray escapePdfText(const QString& text) {
    QByteArray out;
    const QByteArray latin = text.toLatin1();
    for (char c : latin) {
        if (c == '\\' || c == '(' || c == ')') out += '\\';
        out += c;
    }
    return out;
}

// PDF date: D:YYYYMMDDHHmmSS+00'00'
QByteArray pdfDate(const QDateTime& dt) {
    return "D:" + dt.toString("yyyyMMddHHmmss").toLatin1() + "+00'00'";
}

// A shared Type1 Helvetica font object for FreeText appearances, allocated
// once per rebuild and referenced from each appearance stream's /Resources.
int ensureApFont(Writer& writer, int& cached) {
    if (cached) return cached;
    Object font; font.type = Object::Type::Dict;
    font.dict.insert("Type", Object::makeName("Font"));
    font.dict.insert("Subtype", Object::makeName("Type1"));
    font.dict.insert("BaseFont", Object::makeName("Helvetica"));
    font.dict.insert("Encoding", Object::makeName("WinAnsiEncoding"));
    cached = writer.allocate();
    writer.setObjectBody(cached, serializeObjectBody(font, [](Ref) { return 0; }));
    return cached;
}

// Builds a Form XObject appearance stream. `bbox` is in the appearance's own
// coordinate space (we use page space translated so the annot rect's
// bottom-left is the origin's reference — simplest is to use absolute page
// coords for BBox and an identity matrix). Returns the object number.
int addAppearance(Writer& writer, const QRectF& bboxPdf, const QByteArray& content,
                  int fontObj = 0) {
    QMap<QByteArray, Object> d;
    d.insert("Type", Object::makeName("XObject"));
    d.insert("Subtype", Object::makeName("Form"));
    d.insert("FormType", Object::makeInt(1));
    std::vector<Object> bbox;
    bbox.push_back(realObj(bboxPdf.left()));
    bbox.push_back(realObj(bboxPdf.top()));      // caller passes PDF-space rect
    bbox.push_back(realObj(bboxPdf.right()));
    bbox.push_back(realObj(bboxPdf.bottom()));
    d.insert("BBox", Object::makeArray(std::move(bbox)));
    if (fontObj > 0) {
        Object res; res.type = Object::Type::Dict;
        Object fonts; fonts.type = Object::Type::Dict;
        fonts.dict.insert(kApFont, refTo(fontObj));
        res.dict.insert("Font", fonts);
        d.insert("Resources", res);
    }
    return addRawStream(writer, std::move(d), content);
}

// Helper describing PDF-space geometry of an annotation, computed from the
// top-left-origin spec against a page of height `pageH`.
struct Geom {
    QRectF rect;                     // PDF-space rect (bottom-left origin)
    std::vector<QRectF> quads;       // PDF-space per-line rects
    QPolygonF ink;                   // PDF-space stroke
};

Geom toPdfGeom(const AnnotSpec& s, double pageH) {
    Geom g;
    auto flipRect = [pageH](const QRectF& r) {
        return QRectF(r.left(), pageH - r.bottom(), r.width(), r.height());
    };
    g.rect = flipRect(s.rect);
    for (const QRectF& q : s.quads) g.quads.push_back(flipRect(q));
    for (const QPointF& p : s.ink) g.ink << QPointF(p.x(), pageH - p.y());
    return g;
}

// /QuadPoints array for text-markup annotations (8 numbers per quad, in the
// x1 y1 x2 y2 x3 y3 x4 y4 order Adobe expects: TL TR BL BR).
Object quadPointsArray(const std::vector<QRectF>& quads) {
    std::vector<Object> arr;
    for (const QRectF& q : quads) {
        const double x1 = q.left(),  x2 = q.right();
        const double yTop = q.bottom(), yBot = q.top();   // PDF-space: bottom<top
        for (double v : { x1, yTop, x2, yTop, x1, yBot, x2, yBot })
            arr.push_back(realObj(v));
    }
    return Object::makeArray(std::move(arr));
}

Object rectArray(const QRectF& r) {
    std::vector<Object> arr;
    for (double v : { r.left(), r.top(), r.right(), r.bottom() })
        arr.push_back(realObj(v));
    return Object::makeArray(std::move(arr));
}

// Renders the appearance content + fills the semantic keys for one spec.
// Returns the annotation dict (without /P, added by the caller), having
// allocated any appearance/font objects it needs in `writer`.
Object buildAnnot(const AnnotSpec& s, double pageH, Writer& writer, int& apFontCache) {
    const Geom g = toPdfGeom(s, pageH);
    Object a; a.type = Object::Type::Dict;
    a.dict.insert("Type", Object::makeName("Annot"));
    if (!s.contents.isEmpty())
        a.dict.insert("Contents", [](const QString& t) {
            Object o; o.type = Object::Type::String; o.strVal = t.toUtf8(); return o;
        }(s.contents));
    if (!s.author.isEmpty()) {
        Object o; o.type = Object::Type::String; o.strVal = s.author.toUtf8();
        a.dict.insert("T", o);
    }
    { Object o; o.type = Object::Type::String; o.strVal = pdfDate(QDateTime::currentDateTime());
      a.dict.insert("M", o); }
    // Print + normal flags.
    a.dict.insert("F", Object::makeInt(4));

    auto setColor = [&](const QColor& c) {
        std::vector<Object> col;
        col.push_back(realObj(c.redF()));
        col.push_back(realObj(c.greenF()));
        col.push_back(realObj(c.blueF()));
        a.dict.insert("C", Object::makeArray(std::move(col)));
    };

    QByteArray ap;
    QRectF apRect = g.rect;

    using K = AnnotSpec::Kind;
    switch (s.kind) {
    case K::Highlight:
    case K::Underline:
    case K::StrikeOut: {
        const char* sub = s.kind == K::Highlight ? "Highlight"
                        : s.kind == K::Underline ? "Underline" : "StrikeOut";
        a.dict.insert("Subtype", Object::makeName(sub));
        const auto& quads = g.quads.empty() ? std::vector<QRectF>{ g.rect } : g.quads;
        a.dict.insert("QuadPoints", quadPointsArray(quads));
        setColor(s.color);

        // union bbox
        QRectF u = quads.front();
        for (const QRectF& q : quads) u = u.united(q);
        apRect = u;

        if (s.kind == K::Highlight) {
            // Multiply blend so underlying text stays legible.
            ap += "q\n" + rg(s.color);
            for (const QRectF& q : quads)
                ap += num(q.left()) + " " + num(q.top()) + " "
                    + num(q.width()) + " " + num(q.height()) + " re f\n";
            ap += "Q\n";
        } else {
            ap += "q\n" + RG(s.color) + num(std::max(0.8, s.borderWidth)) + " w\n";
            for (const QRectF& q : quads) {
                const double y = (s.kind == K::Underline) ? q.top() + 1.0
                                                          : q.top() + q.height() * 0.5;
                ap += num(q.left()) + " " + num(y) + " m "
                    + num(q.right()) + " " + num(y) + " l S\n";
            }
            ap += "Q\n";
        }
        break;
    }
    case K::Square:
    case K::Circle: {
        a.dict.insert("Subtype", Object::makeName(s.kind == K::Square ? "Square" : "Circle"));
        setColor(s.color);
        Object bs; bs.type = Object::Type::Dict;
        bs.dict.insert("W", realObj(s.borderWidth));
        a.dict.insert("BS", bs);
        const double bw = s.borderWidth;
        const QRectF r = g.rect.adjusted(bw / 2, bw / 2, -bw / 2, -bw / 2);
        ap += "q\n" + RG(s.color) + num(bw) + " w\n";
        if (s.kind == K::Square) {
            ap += num(r.left()) + " " + num(r.top()) + " "
                + num(r.width()) + " " + num(r.height()) + " re S\n";
        } else {
            // Bézier ellipse.
            const double kx = r.width() / 2 * 0.5523, ky = r.height() / 2 * 0.5523;
            const double cx = r.center().x(), cy = r.center().y();
            const double l = r.left(), rr = r.right(), b = r.top(), t = r.bottom();
            ap += num(rr) + " " + num(cy) + " m\n";
            ap += num(rr) + " " + num(cy + ky) + " " + num(cx + kx) + " " + num(t) + " " + num(cx) + " " + num(t) + " c\n";
            ap += num(cx - kx) + " " + num(t) + " " + num(l) + " " + num(cy + ky) + " " + num(l) + " " + num(cy) + " c\n";
            ap += num(l) + " " + num(cy - ky) + " " + num(cx - kx) + " " + num(b) + " " + num(cx) + " " + num(b) + " c\n";
            ap += num(cx + kx) + " " + num(b) + " " + num(rr) + " " + num(cy - ky) + " " + num(rr) + " " + num(cy) + " c\n";
            ap += "S\n";
        }
        ap += "Q\n";
        break;
    }
    case K::Line:
    case K::Arrow: {
        a.dict.insert("Subtype", Object::makeName("Line"));
        setColor(s.color);
        // Line goes from rect top-left to bottom-right in PDF space.
        const QPointF p1(g.rect.left(), g.rect.bottom());
        const QPointF p2(g.rect.right(), g.rect.top());
        std::vector<Object> l;
        for (double v : { p1.x(), p1.y(), p2.x(), p2.y() }) l.push_back(realObj(v));
        a.dict.insert("L", Object::makeArray(std::move(l)));
        if (s.kind == K::Arrow) {
            std::vector<Object> le;
            le.push_back(Object::makeName("None"));
            le.push_back(Object::makeName("ClosedArrow"));
            a.dict.insert("LE", Object::makeArray(std::move(le)));
        }
        ap += "q\n" + RG(s.color) + num(s.borderWidth) + " w\n";
        ap += num(p1.x()) + " " + num(p1.y()) + " m " + num(p2.x()) + " " + num(p2.y()) + " l S\n";
        if (s.kind == K::Arrow) {
            const double ang = std::atan2(p2.y() - p1.y(), p2.x() - p1.x());
            const double ah = 10 + s.borderWidth * 2;
            const double a1 = ang + M_PI - 0.5, a2 = ang + M_PI + 0.5;
            ap += rg(s.color);
            ap += num(p2.x()) + " " + num(p2.y()) + " m ";
            ap += num(p2.x() + ah * std::cos(a1)) + " " + num(p2.y() + ah * std::sin(a1)) + " l ";
            ap += num(p2.x() + ah * std::cos(a2)) + " " + num(p2.y() + ah * std::sin(a2)) + " l f\n";
        }
        ap += "Q\n";
        apRect = QRectF(std::min(p1.x(), p2.x()) - 12, std::min(p1.y(), p2.y()) - 12,
                        std::abs(p2.x() - p1.x()) + 24, std::abs(p2.y() - p1.y()) + 24);
        break;
    }
    case K::Ink: {
        a.dict.insert("Subtype", Object::makeName("Ink"));
        setColor(s.color);
        std::vector<Object> path;
        for (const QPointF& p : g.ink) { path.push_back(realObj(p.x())); path.push_back(realObj(p.y())); }
        std::vector<Object> inkList;
        inkList.push_back(Object::makeArray(std::move(path)));
        a.dict.insert("InkList", Object::makeArray(std::move(inkList)));
        ap += "q\n" + RG(s.color) + num(s.borderWidth) + " w 1 J 1 j\n";
        bool first = true;
        for (const QPointF& p : g.ink) {
            ap += num(p.x()) + " " + num(p.y()) + (first ? " m\n" : " l\n");
            first = false;
        }
        ap += "S\nQ\n";
        break;
    }
    case K::Note: {
        a.dict.insert("Subtype", Object::makeName("Text"));
        a.dict.insert("Name", Object::makeName("Comment"));
        a.dict.insert("Open", [](bool){ Object o; o.type = Object::Type::Bool; o.boolVal = false; return o; }(false));
        setColor(s.color);
        // 20x20 note icon at the spec point.
        const QRectF r(g.rect.left(), g.rect.top(), 20, 20);
        apRect = r;
        ap += "q\n" + rg(s.color) + RG(QColor(90, 70, 0));
        ap += num(r.left()) + " " + num(r.top()) + " 18 18 re b\n";
        ap += "1 1 1 rg 1 w\n";
        for (int i = 0; i < 3; ++i) {
            const double y = r.top() + 5 + i * 4;
            ap += num(r.left() + 4) + " " + num(y) + " m " + num(r.left() + 14) + " " + num(y) + " l S\n";
        }
        ap += "Q\n";
        break;
    }
    case K::FreeText:
    case K::Callout:
    case K::PlainText: {
        a.dict.insert("Subtype", Object::makeName("FreeText"));
        setColor(s.color);
        ensureApFont(writer, apFontCache);
        // Default appearance string.
        { Object da; da.type = Object::Type::String;
          da.strVal = "/" + QByteArray(kApFont) + " " + num(s.fontSizePt) + " Tf "
                      + num(s.color.redF()) + " " + num(s.color.greenF()) + " "
                      + num(s.color.blueF()) + " rg";
          a.dict.insert("DA", da); }

        ap += "q\n";
        if (s.kind != K::PlainText) {
            // Text box / callout: white fill + border.
            ap += "1 1 1 rg\n" + RG(s.color) + num(s.borderWidth) + " w\n";
            ap += num(g.rect.left()) + " " + num(g.rect.top()) + " "
                + num(g.rect.width()) + " " + num(g.rect.height()) + " re B\n";
        }
        if (s.kind == K::Callout) {
            // Leader line from the rect's left edge down-left.
            std::vector<Object> cl;
            const double sx = g.rect.left() - 40, sy = g.rect.top() - 20;
            const double kx = g.rect.left() - 12, ky = g.rect.top();
            for (double v : { sx, sy, kx, ky, g.rect.left(), ky }) cl.push_back(realObj(v));
            a.dict.insert("CL", Object::makeArray(std::move(cl)));
            a.dict.insert("IT", Object::makeName("FreeTextCallout"));
            ap += RG(s.color) + num(s.borderWidth) + " w\n";
            ap += num(sx) + " " + num(sy) + " m " + num(kx) + " " + num(ky) + " l "
                + num(g.rect.left()) + " " + num(ky) + " l S\n";
        }
        // Text (wrapped naively at the box width).
        ap += "BT\n/" + QByteArray(kApFont) + " " + num(s.fontSizePt) + " Tf\n" + rg(s.color);
        const double lead = s.fontSizePt * 1.2;
        // Text flows from the visual top of the box downward. After the
        // y-flip, the visual top is the numerically-larger PDF y (bottom()).
        double ty = g.rect.bottom() - s.fontSizePt;
        const double maxW = g.rect.width() - 6;
        const QStringList words = s.contents.split(' ', Qt::SkipEmptyParts);
        QString line;
        auto flush = [&] {
            if (line.isEmpty()) return;
            ap += "1 0 0 1 " + num(g.rect.left() + 3) + " " + num(ty) + " Tm ("
                + escapePdfText(line) + ") Tj\n";
            ty -= lead;
            line.clear();
        };
        for (const QString& w : words) {
            const QString cand = line.isEmpty() ? w : line + " " + w;
            if (helveticaTextWidthPt(cand, s.fontSizePt) > maxW && !line.isEmpty()) flush();
            line = line.isEmpty() ? w : line + " " + w;
        }
        flush();
        ap += "ET\nQ\n";
        if (s.kind == K::Callout)
            apRect = g.rect.united(QRectF(g.rect.left() - 44, g.rect.top() - 24, 44, 24));
        break;
    }
    case K::Stamp: {
        a.dict.insert("Subtype", Object::makeName("Stamp"));
        setColor(s.color);
        ensureApFont(writer, apFontCache);
        const double fs = std::max(12.0, g.rect.height() * 0.5);
        ap += "q\n" + RG(s.color) + rg(s.color) + "2 w\n";
        ap += num(g.rect.left()) + " " + num(g.rect.top()) + " "
            + num(g.rect.width()) + " " + num(g.rect.height()) + " re S\n";
        ap += "BT\n/" + QByteArray(kApFont) + " " + num(fs) + " Tf\n" + rg(s.color);
        const double tw = helveticaTextWidthPt(s.contents, fs);
        ap += "1 0 0 1 " + num(g.rect.center().x() - tw / 2) + " "
            + num(g.rect.center().y() - fs * 0.35) + " Tm (" + escapePdfText(s.contents) + ") Tj\nET\nQ\n";
        break;
    }
    case K::Link: {
        a.dict.insert("Subtype", Object::makeName("Link"));
        Object action; action.type = Object::Type::Dict;
        action.dict.insert("Type", Object::makeName("Action"));
        action.dict.insert("S", Object::makeName("URI"));
        { Object uri; uri.type = Object::Type::String; uri.strVal = s.url.toLatin1();
          action.dict.insert("URI", uri); }
        a.dict.insert("A", action);
        std::vector<Object> border; border.push_back(Object::makeInt(0));
        border.push_back(Object::makeInt(0)); border.push_back(Object::makeInt(0));
        a.dict.insert("Border", Object::makeArray(std::move(border)));
        // Links need no appearance stream.
        break;
    }
    case K::WipeOff: {
        a.dict.insert("Subtype", Object::makeName("Square"));
        a.dict.insert("IT", Object::makeName("NativeOfficeWipe"));
        std::vector<Object> white;
        for (int i = 0; i < 3; ++i) white.push_back(realObj(1.0));
        a.dict.insert("IC", Object::makeArray(std::move(white)));
        std::vector<Object> noBorder; noBorder.push_back(realObj(1)); noBorder.push_back(realObj(1));
        Object bs; bs.type = Object::Type::Dict; bs.dict.insert("W", realObj(0));
        a.dict.insert("BS", bs);
        ap += "q\n1 1 1 rg\n" + num(g.rect.left()) + " " + num(g.rect.top()) + " "
            + num(g.rect.width()) + " " + num(g.rect.height()) + " re f\nQ\n";
        break;
    }
    case K::Caret: {
        a.dict.insert("Subtype", Object::makeName("Caret"));
        setColor(s.color);
        const QRectF r = g.rect;
        apRect = r;
        ap += "q\n" + rg(s.color);
        ap += num(r.left()) + " " + num(r.top()) + " m "
            + num(r.right()) + " " + num(r.top()) + " l "
            + num(r.center().x()) + " " + num(r.bottom()) + " l f\nQ\n";
        break;
    }
    case K::FileAttachment: {
        a.dict.insert("Subtype", Object::makeName("FileAttachment"));
        a.dict.insert("Name", Object::makeName("PushPin"));
        setColor(s.color);
        QFile f(s.filePath);
        QByteArray data;
        if (f.open(QIODevice::ReadOnly)) data = f.readAll();
        // Embedded file stream.
        QMap<QByteArray, Object> efDict;
        efDict.insert("Type", Object::makeName("EmbeddedFile"));
        Object params; params.type = Object::Type::Dict;
        params.dict.insert("Size", Object::makeInt(data.size()));
        efDict.insert("Params", params);
        const int efNum = addRawStream(writer, std::move(efDict), data);
        Object fs; fs.type = Object::Type::Dict;
        fs.dict.insert("Type", Object::makeName("Filespec"));
        { Object nm; nm.type = Object::Type::String; nm.strVal = QFileInfo(s.filePath).fileName().toUtf8();
          fs.dict.insert("F", nm); fs.dict.insert("UF", nm); }
        Object ef; ef.type = Object::Type::Dict; ef.dict.insert("F", refTo(efNum));
        fs.dict.insert("EF", ef);
        const int fsNum = writer.allocate();
        writer.setObjectBody(fsNum, serializeObjectBody(fs, [](Ref r) { return r.num < 0 ? -r.num : 0; }));
        a.dict.insert("FS", refTo(fsNum));
        const QRectF r(g.rect.left(), g.rect.top(), 20, 24);
        apRect = r;
        ap += "q\n" + rg(s.color) + num(r.left() + 8) + " " + num(r.top()) + " m "
            + num(r.left() + 8) + " " + num(r.top() + 20) + " l 3 w S\nQ\n";
        break;
    }
    case K::Picture: {
        a.dict.insert("Subtype", Object::makeName("Stamp"));
        QImage img(s.filePath);
        if (!img.isNull()) {
            img = img.convertToFormat(QImage::Format_RGB888);
            QByteArray raw;
            for (int y = 0; y < img.height(); ++y)
                raw.append(reinterpret_cast<const char*>(img.constScanLine(y)), img.width() * 3);
            QByteArray defl = qCompress(raw, 9).mid(4);
            QMap<QByteArray, Object> id;
            id.insert("Type", Object::makeName("XObject"));
            id.insert("Subtype", Object::makeName("Image"));
            id.insert("Width", Object::makeInt(img.width()));
            id.insert("Height", Object::makeInt(img.height()));
            id.insert("ColorSpace", Object::makeName("DeviceRGB"));
            id.insert("BitsPerComponent", Object::makeInt(8));
            id.insert("Filter", Object::makeName("FlateDecode"));
            const int imgNum = addRawStream(writer, std::move(id), defl);
            // Appearance uses an XObject resource.
            QMap<QByteArray, Object> ad;
            ad.insert("Type", Object::makeName("XObject"));
            ad.insert("Subtype", Object::makeName("Form"));
            ad.insert("BBox", rectArray(g.rect));
            Object res; res.type = Object::Type::Dict;
            Object xo; xo.type = Object::Type::Dict; xo.dict.insert("Im0", refTo(imgNum));
            res.dict.insert("XObject", xo);
            ad.insert("Resources", res);
            QByteArray c = "q\n" + num(g.rect.width()) + " 0 0 " + num(g.rect.height()) + " "
                + num(g.rect.left()) + " " + num(g.rect.top()) + " cm /Im0 Do\nQ\n";
            const int apNum = addRawStream(writer, std::move(ad), c);
            Object apDict; apDict.type = Object::Type::Dict;
            apDict.dict.insert("N", refTo(apNum));
            a.dict.insert("AP", apDict);
            a.dict.insert("Rect", rectArray(g.rect));
            return a;   // appearance already attached
        }
        break;
    }
    }

    a.dict.insert("Rect", rectArray(apRect));
    if (!ap.isEmpty()) {
        const int fontObj = (s.kind == K::FreeText || s.kind == K::Callout ||
                             s.kind == K::PlainText || s.kind == K::Stamp) ? apFontCache : 0;
        const int apNum = addAppearance(writer, apRect, ap, fontObj);
        Object apDict; apDict.type = Object::Type::Dict;
        apDict.dict.insert("N", refTo(apNum));
        a.dict.insert("AP", apDict);
    }
    return a;
}

bool isListableAnnot(const Document& doc, const Object& annot) {
    if (!annot.isDict()) return false;
    const Object* st = annot.find("Subtype");
    if (!st) return false;
    const QByteArray sub = st->asName();
    // Skip form widgets and popups from the comments list.
    return sub != "Widget" && sub != "Popup" && sub != "Link";
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// addAnnotations
// ─────────────────────────────────────────────────────────────────────────────

OpResult addAnnotations(const QString& in, const QString& out,
                        const std::vector<AnnotSpec>& specs) {
    OpenStatus status;
    auto doc = Document::open(in, status);
    if (!doc)
        return { false, QString("This PDF isn't supported yet: %1").arg(openStatusReason(status)) };

    Writer writer;
    const int pagesNum = writer.allocate();
    const int catalogNum = writer.allocate();
    Copier copier(*doc, writer);
    int apFontCache = 0;

    // Group specs by page.
    std::map<int, std::vector<const AnnotSpec*>> byPage;
    for (const AnnotSpec& s : specs)
        if (s.pageIndex >= 0 && s.pageIndex < doc->pageCount())
            byPage[s.pageIndex].push_back(&s);

    // Pre-allocate every page's output object number and register it with
    // the copier, so an annotation's /P back-reference is known before the
    // annotation is serialized (page↔annot is a mutual reference).
    const int n = doc->pageCount();
    std::vector<int> newPageNums(size_t(n), 0);
    for (int i = 0; i < n; ++i) {
        newPageNums[size_t(i)] = writer.allocate();
        copier.mapPage(doc->pages()[size_t(i)].ref, newPageNums[size_t(i)]);
    }

    for (int i = 0; i < n; ++i) {
        const PageInfo& page = doc->pages()[size_t(i)];
        Object dict = page.dict;
        double bx, by, bw, bh;
        mediaBoxOf(*doc, dict, bx, by, bw, bh);

        // Existing /Annots refs are copied through; new ones appended.
        std::vector<Object> annots;
        if (const Object* ex = dict.find("Annots")) {
            const Object& arr = doc->resolve(*ex);
            if (arr.isArray())
                for (const Object& e : arr.arr) annots.push_back(e);
        }

        auto it = byPage.find(i);
        if (it != byPage.end()) {
            for (const AnnotSpec* s : it->second) {
                Object annot = buildAnnot(*s, bh, writer, apFontCache);
                annot.dict.insert("P", refTo(newPageNums[size_t(i)]));
                const int annotNum = writer.allocate();
                writer.setObjectBody(annotNum,
                    serializeObjectBody(annot, [&copier](Ref r) { return copier.copy(r); }));
                annots.push_back(refTo(annotNum));
            }
        }

        dict.dict.insert("Parent", refTo(pagesNum));
        if (!annots.empty())
            dict.dict.insert("Annots", Object::makeArray(std::move(annots)));
        writer.setObjectBody(newPageNums[size_t(i)],
            serializeObjectBody(dict, [&copier](Ref r) { return copier.copy(r); }));
    }

    return finishTree(writer, pagesNum, catalogNum, newPageNums, out, *doc, copier);
}

// ─────────────────────────────────────────────────────────────────────────────
// listing / removal
// ─────────────────────────────────────────────────────────────────────────────

std::vector<AnnotInfo> listAnnotations(const QString& path) {
    std::vector<AnnotInfo> out;
    OpenStatus status;
    auto doc = Document::open(path, status);
    if (!doc) return out;

    for (int i = 0; i < doc->pageCount(); ++i) {
        const Object& pageDict = doc->pages()[size_t(i)].dict;
        const Object* an = pageDict.find("Annots");
        if (!an) continue;
        const Object& arr = doc->resolve(*an);
        if (!arr.isArray()) continue;

        double bx, by, bw, bh;
        mediaBoxOf(*doc, pageDict, bx, by, bw, bh);

        int idx = 0;
        for (const Object& e : arr.arr) {
            const Object& annot = doc->resolve(e);
            if (!isListableAnnot(*doc, annot)) continue;

            AnnotInfo info;
            info.pageIndex = i;
            info.indexOnPage = idx++;
            info.subtype = QString::fromLatin1(annot.find("Subtype")->asName());
            if (const Object* c = annot.find("Contents"))
                info.contents = QString::fromUtf8(doc->resolve(*c).strVal);
            if (const Object* t = annot.find("T"))
                info.author = QString::fromUtf8(doc->resolve(*t).strVal);
            if (const Object* m = annot.find("M"))
                info.modified = QString::fromLatin1(doc->resolve(*m).strVal);
            if (const Object* r = annot.find("Rect")) {
                const Object& ra = doc->resolve(*r);
                if (ra.isArray() && ra.arr.size() == 4) {
                    auto v = [&](int k) {
                        const Object& o = doc->resolve(ra.arr[size_t(k)]);
                        return o.type == Object::Type::Real ? o.realVal : double(o.asInt());
                    };
                    // back to top-left origin for the UI
                    const double lly = v(1), ury = v(3);
                    info.rect = QRectF(v(0), bh - ury, v(2) - v(0), ury - lly);
                }
            }
            out.push_back(info);
        }
    }
    return out;
}

namespace {

// Rebuilds `in`, filtering each page's /Annots with `keep(pageIndex,
// listableIndexOnPage, annotObject)`. listableIndex is -1 for non-listable
// entries (widgets/popups/links), which are always kept.
OpResult rebuildFilteringAnnots(const QString& in, const QString& out,
        const std::function<bool(int, int, const Object&)>& keep) {
    OpenStatus status;
    auto doc = Document::open(in, status);
    if (!doc)
        return { false, QString("This PDF isn't supported yet: %1").arg(openStatusReason(status)) };

    Writer writer;
    const int pagesNum = writer.allocate();
    const int catalogNum = writer.allocate();
    Copier copier(*doc, writer);

    const int n = doc->pageCount();
    std::vector<int> newPageNums(size_t(n), 0);
    for (int i = 0; i < n; ++i) {
        newPageNums[size_t(i)] = writer.allocate();
        copier.mapPage(doc->pages()[size_t(i)].ref, newPageNums[size_t(i)]);
    }

    for (int i = 0; i < n; ++i) {
        Object dict = doc->pages()[size_t(i)].dict;
        if (const Object* an = dict.find("Annots")) {
            const Object& arr = doc->resolve(*an);
            if (arr.isArray()) {
                std::vector<Object> kept;
                int listable = 0;
                for (const Object& e : arr.arr) {
                    const Object& annot = doc->resolve(e);
                    const bool isList = isListableAnnot(*doc, annot);
                    const int li = isList ? listable : -1;
                    if (isList) ++listable;
                    if (keep(i, li, annot)) kept.push_back(e);
                }
                if (kept.empty()) dict.dict.remove("Annots");
                else              dict.dict.insert("Annots", Object::makeArray(std::move(kept)));
            }
        }
        dict.dict.insert("Parent", refTo(pagesNum));
        writer.setObjectBody(newPageNums[size_t(i)],
            serializeObjectBody(dict, [&copier](Ref r) { return copier.copy(r); }));
    }

    return finishTree(writer, pagesNum, catalogNum, newPageNums, out, *doc, copier);
}

} // namespace

OpResult removeAnnotation(const QString& in, const QString& out,
                          int pageIndex, int indexOnPage) {
    return rebuildFilteringAnnots(in, out,
        [pageIndex, indexOnPage](int page, int li, const Object&) {
            return !(page == pageIndex && li == indexOnPage);
        });
}

OpResult removeAllAnnotations(const QString& in, const QString& out) {
    return rebuildFilteringAnnots(in, out,
        [](int, int li, const Object&) { return li < 0; });   // keep only widgets/popups/links
}

// ─────────────────────────────────────────────────────────────────────────────
// XFDF import/export
// ─────────────────────────────────────────────────────────────────────────────

OpResult exportXfdf(const QString& pdfPath, const QString& xfdfOut) {
    const std::vector<AnnotInfo> annots = listAnnotations(pdfPath);

    QFile f(xfdfOut);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return { false, "Could not write the XFDF file." };

    QXmlStreamWriter w(&f);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeStartElement("xfdf");
    w.writeAttribute("xmlns", "http://ns.adobe.com/xfdf/");
    w.writeStartElement("annots");
    for (const AnnotInfo& a : annots) {
        w.writeStartElement(a.subtype.toLower());
        w.writeAttribute("page", QString::number(a.pageIndex));
        w.writeAttribute("rect", QStringLiteral("%1,%2,%3,%4")
            .arg(a.rect.left()).arg(a.rect.top()).arg(a.rect.right()).arg(a.rect.bottom()));
        if (!a.author.isEmpty())   w.writeAttribute("title", a.author);
        if (!a.modified.isEmpty()) w.writeAttribute("date", a.modified);
        if (!a.contents.isEmpty()) {
            w.writeStartElement("contents");
            w.writeCharacters(a.contents);
            w.writeEndElement();
        }
        w.writeEndElement();
    }
    w.writeEndElement();   // annots
    w.writeEndElement();   // xfdf
    w.writeEndDocument();
    f.close();
    return { true, {} };
}

OpResult importXfdf(const QString& in, const QString& xfdfPath, const QString& out) {
    QFile f(xfdfPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return { false, "Could not read the XFDF file." };

    QString err;
    const int pageCount = pdfPageCountOrError(in, err);
    if (pageCount < 0) return { false, err };

    std::vector<AnnotSpec> specs;
    QXmlStreamReader r(&f);
    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement()) continue;
        const QString name = r.name().toString().toLower();

        AnnotSpec::Kind kind;
        if      (name == "highlight")   kind = AnnotSpec::Kind::Highlight;
        else if (name == "underline")   kind = AnnotSpec::Kind::Underline;
        else if (name == "strikeout")   kind = AnnotSpec::Kind::StrikeOut;
        else if (name == "square")      kind = AnnotSpec::Kind::Square;
        else if (name == "circle")      kind = AnnotSpec::Kind::Circle;
        else if (name == "line")        kind = AnnotSpec::Kind::Line;
        else if (name == "text")        kind = AnnotSpec::Kind::Note;
        else if (name == "freetext")    kind = AnnotSpec::Kind::FreeText;
        else continue;

        AnnotSpec s;
        s.kind = kind;
        const auto attrs = r.attributes();
        s.pageIndex = attrs.value("page").toInt();
        if (s.pageIndex < 0 || s.pageIndex >= pageCount) continue;
        const QStringList rc = attrs.value("rect").toString().split(',');
        if (rc.size() == 4)
            s.rect = QRectF(QPointF(rc[0].toDouble(), rc[1].toDouble()),
                            QPointF(rc[2].toDouble(), rc[3].toDouble())).normalized();
        s.author = attrs.value("title").toString();
        if (kind == AnnotSpec::Kind::Highlight || kind == AnnotSpec::Kind::Underline
            || kind == AnnotSpec::Kind::StrikeOut)
            s.quads = { s.rect };
        // Read child <contents>.
        while (!(r.isEndElement() && r.name().toString().toLower() == name) && !r.atEnd()) {
            r.readNext();
            if (r.isStartElement() && r.name().toString().toLower() == "contents")
                s.contents = r.readElementText();
        }
        specs.push_back(s);
    }
    f.close();

    if (specs.empty())
        return { false, "No importable comments were found in the XFDF file." };
    return addAnnotations(in, out, specs);
}

// ─────────────────────────────────────────────────────────────────────────────
// outline bookmark
// ─────────────────────────────────────────────────────────────────────────────

OpResult addOutlineBookmark(const QString& in, const QString& out,
                            const QString& title, int pageIndex) {
    OpenStatus status;
    auto doc = Document::open(in, status);
    if (!doc)
        return { false, QString("This PDF isn't supported yet: %1").arg(openStatusReason(status)) };
    if (pageIndex < 0 || pageIndex >= doc->pageCount())
        return { false, "The bookmark's target page is out of range." };

    Writer writer;
    const int pagesNum = writer.allocate();
    const int catalogNum = writer.allocate();
    Copier copier(*doc, writer);

    const int n = doc->pageCount();
    std::vector<int> newPageNums(size_t(n), 0);
    for (int i = 0; i < n; ++i) {
        newPageNums[size_t(i)] = writer.allocate();
        copier.mapPage(doc->pages()[size_t(i)].ref, newPageNums[size_t(i)]);
    }
    for (int i = 0; i < n; ++i) {
        Object dict = doc->pages()[size_t(i)].dict;
        dict.dict.insert("Parent", refTo(pagesNum));
        writer.setObjectBody(newPageNums[size_t(i)],
            serializeObjectBody(dict, [&copier](Ref r) { return copier.copy(r); }));
    }

    // Fresh, flat outline: copy existing top-level bookmarks + append ours.
    // (A full nested-tree rewrite isn't needed for "Add Bookmark".)
    const int outlinesNum = writer.allocate();
    std::vector<int> itemNums;

    struct ExistingBm { QString title; int page; };
    std::vector<ExistingBm> existing;
    const Object& cat = doc->catalog();
    if (const Object* ol = cat.find("Outlines")) {
        const Object& outlines = doc->resolve(*ol);
        Object cur = outlines.find("First") ? doc->resolve(*outlines.find("First")) : Object::makeNull();
        int guard = 0;
        while (cur.isDict() && guard++ < 2048) {
            ExistingBm bm;
            if (const Object* t = cur.find("Title")) bm.title = QString::fromUtf8(doc->resolve(*t).strVal);
            bm.page = 0;
            if (const Object* d = cur.find("Dest")) {
                const Object& dest = doc->resolve(*d);
                if (dest.isArray() && !dest.arr.empty()) {
                    // Best-effort: match the dest page ref to an index.
                    const Object& pref = dest.arr[0];
                    if (pref.isRef())
                        for (int i = 0; i < n; ++i)
                            if (doc->pages()[size_t(i)].ref == pref.ref) { bm.page = i; break; }
                }
            }
            existing.push_back(bm);
            if (const Object* nx = cur.find("Next")) cur = doc->resolve(*nx);
            else break;
        }
    }

    // Allocate an object number per outline item up front (so Prev/Next can
    // cross-reference), then write each item's body with the links.
    std::vector<std::pair<QString, int>> all;
    for (const ExistingBm& bm : existing) all.push_back({ bm.title, std::clamp(bm.page, 0, n - 1) });
    all.push_back({ title, pageIndex });
    for (const auto& entry : all) itemNums.push_back(writer.allocate());

    for (size_t k = 0; k < itemNums.size(); ++k) {
        Object item; item.type = Object::Type::Dict;
        { Object o; o.type = Object::Type::String; o.strVal = all[k].first.toUtf8(); item.dict.insert("Title", o); }
        item.dict.insert("Parent", refTo(outlinesNum));
        std::vector<Object> dest;
        dest.push_back(refTo(newPageNums[size_t(all[k].second)]));
        dest.push_back(Object::makeName("Fit"));
        item.dict.insert("Dest", Object::makeArray(std::move(dest)));
        if (k > 0)                    item.dict.insert("Prev", refTo(itemNums[k - 1]));
        if (k + 1 < itemNums.size())  item.dict.insert("Next", refTo(itemNums[k + 1]));
        writer.setObjectBody(itemNums[k],
            serializeObjectBody(item, [](Ref r) { return r.num < 0 ? -r.num : 0; }));
    }

    Object outlines; outlines.type = Object::Type::Dict;
    outlines.dict.insert("Type", Object::makeName("Outlines"));
    if (!itemNums.empty()) {
        outlines.dict.insert("First", refTo(itemNums.front()));
        outlines.dict.insert("Last", refTo(itemNums.back()));
        outlines.dict.insert("Count", Object::makeInt(qint64(itemNums.size())));
    }
    writer.setObjectBody(outlinesNum,
        serializeObjectBody(outlines, [](Ref r) { return r.num < 0 ? -r.num : 0; }));

    QMap<QByteArray, Object> extra;
    extra.insert("Outlines", refTo(outlinesNum));
    return finishTree(writer, pagesNum, catalogNum, newPageNums, out, *doc, copier,
                      { QByteArray("Outlines") }, extra);
}

} // namespace NativeOffice::Pdf
