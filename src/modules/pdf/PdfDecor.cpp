// ─────────────────────────────────────────────────────────────────────────────
// PdfDecor.cpp — see PdfDecor.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfDecor.h"
#include "PdfDocument.h"
#include "PdfWriter.h"

#include <QDate>
#include <QFile>
#include <QImage>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace NativeOffice::Pdf {

namespace {

// Resource names our decoration streams reference. Deliberately unlikely to
// collide with anything a real-world producer emits.
constexpr char kFontName[]  = "NOdecorF1";
constexpr char kGStateName[] = "NOdecorG1";
constexpr char kImageName[] = "NOdecorI1";
constexpr char kDecorKey[]  = "NativeOfficeDecor";

const char* kindTag(DecorKind kind) {
    switch (kind) {
        case DecorKind::Watermark:    return "watermark";
        case DecorKind::Background:   return "background";
        case DecorKind::PageNumber:   return "pagenumber";
        case DecorKind::HeaderFooter: return "headerfooter";
    }
    return "decor";
}

// Helvetica AFM widths for WinAnsi 32..126 (1000-unit em).
const int kHelvWidths[95] = {
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333,
    278, 278, 556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278,
    584, 584, 584, 556, 1015, 667, 667, 722, 722, 667, 611, 778, 722, 278,
    500, 667, 556, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944,
    667, 667, 611, 278, 278, 278, 469, 556, 333, 556, 556, 500, 556, 556,
    278, 556, 556, 222, 222, 500, 222, 833, 556, 556, 556, 556, 333, 500,
    278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584,
};

// PDF literal-string escaping, Latin-1 bytes (font is WinAnsi-encoded).
QByteArray escapePdfText(const QString& text) {
    QByteArray out;
    const QByteArray latin = text.toLatin1();   // '?' for the unmappable
    for (char c : latin) {
        if (c == '\\' || c == '(' || c == ')') out += '\\';
        out += c;
    }
    return out;
}

QByteArray num(double v) { return QByteArray::number(v, 'f', 3); }

QByteArray rgbOp(const QColor& c) {
    return num(c.redF()) + " " + num(c.greenF()) + " " + num(c.blueF()) + " rg\n";
}

// Same qCompress-based deflate as PdfOps.cpp (strip the 4-byte Qt header).
bool deflateAll(const QByteArray& in, QByteArray& out) {
    const QByteArray withHeader = qCompress(in, 9);
    if (withHeader.size() <= 4) return false;
    out = withHeader.mid(4);
    return true;
}

// One "BT … Tj ET" block placing `text` rotated by `deg` (CCW) with the
// text's center at (cx, cy). PDF y-axis is bottom-up here.
QByteArray placedText(const QString& text, double sizePt, double deg,
                      double cx, double cy) {
    const double w   = helveticaTextWidthPt(text, sizePt);
    const double rad = qDegreesToRadians(deg);
    const double c = std::cos(rad), s = std::sin(rad);
    // Baseline start = center - R·(w/2, 0.35·size)
    const double tx = cx - (c * (w / 2) - s * (0.35 * sizePt));
    const double ty = cy - (s * (w / 2) + c * (0.35 * sizePt));
    QByteArray out;
    out += "BT\n/" + QByteArray(kFontName) + " " + num(sizePt) + " Tf\n";
    out += num(c) + " " + num(s) + " " + num(-s) + " " + num(c) + " "
         + num(tx) + " " + num(ty) + " Tm\n";
    out += "(" + escapePdfText(text) + ") Tj\nET\n";
    return out;
}

// ── the shared rebuild machinery ────────────────────────────────────────────
// (Local copies of the Copier/tree-writing pattern from PdfOps.cpp — kept
// separate because decoration needs to allocate its own shared objects and
// rewrite per-page Contents/Resources, which the PdfOps entry points don't
// expose.)

class DecorCopier {
public:
    DecorCopier(const Document& src, Writer& dst) : m_src(src), m_dst(dst) {}

    int copy(Ref oldRef) {
        if (oldRef.num < 0) return -oldRef.num;
        auto it = m_map.find(oldRef);
        if (it != m_map.end()) return it->second;
        const int newNum = m_dst.allocate();
        m_map[oldRef] = newNum;
        const Object& working = m_src.resolve(oldRef);
        m_dst.setObjectBody(newNum, serializeObjectBody(working, [this](Ref r) { return copy(r); }));
        return newNum;
    }

    void mapPage(Ref oldRef, int newNum) { if (oldRef.num > 0) m_map[oldRef] = newNum; }

private:
    const Document& m_src;
    Writer& m_dst;
    std::map<Ref, int> m_map;
};

Object refTo(int writerNum) { return Object::makeRef(Ref{ -writerNum, 0 }); }

int addRawStream(Writer& writer, QMap<QByteArray, Object> dict, const QByteArray& data) {
    Object stream;
    stream.type = Object::Type::Stream;
    stream.dict = std::move(dict);
    stream.dict.insert("Length", Object::makeInt(data.size()));
    stream.streamData = data;
    const int n = writer.allocate();
    writer.setObjectBody(n, serializeObjectBody(stream, [](Ref r) { return r.num < 0 ? -r.num : 0; }));
    return n;
}

int addTaggedContentStream(Writer& writer, const char* tag, const QByteArray& content) {
    QMap<QByteArray, Object> d;
    d.insert(kDecorKey, Object::makeName(tag));
    return addRawStream(writer, std::move(d), content);
}

// Media box of a resolved page dict (PdfDocument bakes inheritance in).
void mediaBoxOf(const Document& doc, const Object& pageDict,
                double& x0, double& y0, double& w, double& h) {
    x0 = 0; y0 = 0; w = 612; h = 792;
    if (const Object* mb = pageDict.find("MediaBox")) {
        const Object& box = doc.resolve(*mb);
        if (box.isArray() && box.arr.size() == 4) {
            auto v = [&](int i) {
                const Object& o = doc.resolve(box.arr[size_t(i)]);
                return o.type == Object::Type::Real ? o.realVal : double(o.asInt());
            };
            x0 = v(0); y0 = v(1); w = v(2) - x0; h = v(3) - y0;
        }
    }
}

struct DecorPlan {
    const char* tag = "decor";
    bool underlay = false;                       // background goes UNDER content
    bool needFont = false;
    double gsAlpha = -1;                         // >= 0: add an ExtGState with this alpha
    QString imagePath;                           // non-empty: embed as XObject
    // Per-page content (already self-contained q…Q). Empty = skip this page.
    std::function<QByteArray(int pageIdx, double w, double h)> content;
    std::vector<int> pages;                      // empty = all
};

OpResult applyDecor(const QString& in, const QString& out, const DecorPlan& plan,
                    double* imageAspectOut = nullptr) {
    OpenStatus status;
    auto doc = Document::open(in, status);
    if (!doc)
        return { false, QString("This PDF isn't supported yet: %1").arg(openStatusReason(status)) };

    std::vector<bool> apply(size_t(doc->pageCount()), plan.pages.empty());
    for (int p : plan.pages)
        if (p >= 0 && p < doc->pageCount()) apply[size_t(p)] = true;

    Writer writer;
    const int pagesNum = writer.allocate();
    const int catalogNum = writer.allocate();
    DecorCopier copier(*doc, writer);

    // ── shared decoration objects ───────────────────────────────────────
    int fontNum = 0, gsNum = 0, imageNum = 0;
    double imageAspect = 1.0;
    if (plan.needFont) {
        Object font; font.type = Object::Type::Dict;
        font.dict.insert("Type", Object::makeName("Font"));
        font.dict.insert("Subtype", Object::makeName("Type1"));
        font.dict.insert("BaseFont", Object::makeName("Helvetica"));
        font.dict.insert("Encoding", Object::makeName("WinAnsiEncoding"));
        fontNum = writer.allocate();
        writer.setObjectBody(fontNum, serializeObjectBody(font, [](Ref) { return 0; }));
    }
    if (plan.gsAlpha >= 0) {
        Object gs; gs.type = Object::Type::Dict;
        gs.dict.insert("Type", Object::makeName("ExtGState"));
        Object a; a.type = Object::Type::Real; a.realVal = plan.gsAlpha;
        gs.dict.insert("ca", a);
        gs.dict.insert("CA", a);
        gsNum = writer.allocate();
        writer.setObjectBody(gsNum, serializeObjectBody(gs, [](Ref) { return 0; }));
    }
    if (!plan.imagePath.isEmpty()) {
        QImage img(plan.imagePath);
        if (img.isNull())
            return { false, "The watermark image could not be loaded." };
        if (img.width() > 1600)
            img = img.scaledToWidth(1600, Qt::SmoothTransformation);
        img = img.convertToFormat(QImage::Format_RGB888);
        imageAspect = img.width() > 0 ? double(img.height()) / img.width() : 1.0;

        QByteArray raw;
        raw.reserve(img.width() * img.height() * 3);
        for (int y = 0; y < img.height(); ++y)
            raw.append(reinterpret_cast<const char*>(img.constScanLine(y)), img.width() * 3);
        QByteArray deflated;
        if (!deflateAll(raw, deflated))
            return { false, "The watermark image could not be encoded." };

        QMap<QByteArray, Object> d;
        d.insert("Type", Object::makeName("XObject"));
        d.insert("Subtype", Object::makeName("Image"));
        d.insert("Width", Object::makeInt(img.width()));
        d.insert("Height", Object::makeInt(img.height()));
        d.insert("ColorSpace", Object::makeName("DeviceRGB"));
        d.insert("BitsPerComponent", Object::makeInt(8));
        d.insert("Filter", Object::makeName("FlateDecode"));
        imageNum = addRawStream(writer, std::move(d), deflated);
    }
    if (imageAspectOut) *imageAspectOut = imageAspect;

    // ── pages ───────────────────────────────────────────────────────────
    std::vector<int> newPageNums;
    for (int i = 0; i < doc->pageCount(); ++i) {
        const PageInfo& page = doc->pages()[size_t(i)];
        Object dict = page.dict;

        double bx, by, bw, bh;
        mediaBoxOf(*doc, dict, bx, by, bw, bh);

        QByteArray content;
        if (apply[size_t(i)] && plan.content)
            content = plan.content(i, bw, bh);

        if (!content.isEmpty()) {
            const int decorNum = addTaggedContentStream(writer, plan.tag, content);

            std::vector<Object> contents;
            auto pushExisting = [&] {
                if (const Object* c = dict.find("Contents")) {
                    if (c->isArray())
                        for (const Object& e : c->arr) contents.push_back(e);
                    else
                        contents.push_back(*c);
                }
            };
            if (plan.underlay) {
                contents.push_back(refTo(decorNum));
                pushExisting();
            } else {
                // Sandbox the original content's graphics state so an
                // unbalanced q/Q in it can't distort our overlay.
                const int preNum  = addTaggedContentStream(writer, "wrap-pre",  "q\n");
                const int postNum = addTaggedContentStream(writer, "wrap-post", "\nQ\n");
                contents.push_back(refTo(preNum));
                pushExisting();
                contents.push_back(refTo(postNum));
                contents.push_back(refTo(decorNum));
            }
            dict.dict.insert("Contents", Object::makeArray(std::move(contents)));

            // Per-page copy of Resources with our entries merged in (the
            // original Resources object may be shared across pages — never
            // mutate it in place).
            Object res;
            res.type = Object::Type::Dict;
            if (const Object* r = dict.find("Resources")) {
                const Object& resolved = doc->resolve(*r);
                if (resolved.isDict()) res.dict = resolved.dict;
            }
            auto mergeInto = [&](const char* dictName, const char* entryName, int objNum) {
                Object sub;
                sub.type = Object::Type::Dict;
                if (const Object* existing = res.find(dictName)) {
                    const Object& resolved = doc->resolve(*existing);
                    if (resolved.isDict()) sub.dict = resolved.dict;
                }
                sub.dict.insert(entryName, refTo(objNum));
                res.dict.insert(dictName, sub);
            };
            if (plan.needFont)        mergeInto("Font", kFontName, fontNum);
            if (plan.gsAlpha >= 0)    mergeInto("ExtGState", kGStateName, gsNum);
            if (imageNum > 0)         mergeInto("XObject", kImageName, imageNum);
            dict.dict.insert("Resources", res);
        }

        const int newNum = writer.allocate();
        copier.mapPage(page.ref, newNum);
        dict.dict.insert("Parent", refTo(pagesNum));
        writer.setObjectBody(newNum, serializeObjectBody(dict, [&copier](Ref r) { return copier.copy(r); }));
        newPageNums.push_back(newNum);
    }

    // ── pages node + catalog (with extras) ──────────────────────────────
    auto sentinelOnly = [](Ref r) { return r.num < 0 ? -r.num : 0; };
    Object pagesObj; pagesObj.type = Object::Type::Dict;
    pagesObj.dict.insert("Type", Object::makeName("Pages"));
    std::vector<Object> kids;
    for (int n : newPageNums) kids.push_back(refTo(n));
    pagesObj.dict.insert("Kids", Object::makeArray(std::move(kids)));
    pagesObj.dict.insert("Count", Object::makeInt(qint64(newPageNums.size())));
    writer.setObjectBody(pagesNum, serializeObjectBody(pagesObj, sentinelOnly));

    Object catalogObj; catalogObj.type = Object::Type::Dict;
    catalogObj.dict.insert("Type", Object::makeName("Catalog"));
    catalogObj.dict.insert("Pages", refTo(pagesNum));
    static const char* kKeep[] = { "Outlines", "AcroForm", "Names", "PageLabels",
                                   "Lang", "ViewerPreferences", "PageMode", "PageLayout" };
    const Object& srcCatalog = doc->catalog();
    if (srcCatalog.isDict()) {
        for (const char* key : kKeep) {
            if (const Object* v = srcCatalog.find(key)) {
                if (v->isRef())
                    catalogObj.dict.insert(key, refTo(copier.copy(v->ref)));
            }
        }
    }
    writer.setObjectBody(catalogNum, serializeObjectBody(catalogObj, sentinelOnly));

    if (!writer.writeTo(out, catalogNum))
        return { false, "Could not write the output file — check the destination is writable." };

    // self-check
    OpenStatus st2;
    auto check = Document::open(out, st2);
    if (!check || check->pageCount() != doc->pageCount()) {
        QFile::remove(out);
        return { false, "The file was written but didn't verify correctly afterward, so it was discarded." };
    }
    return { true, {} };
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// public API
// ─────────────────────────────────────────────────────────────────────────────

double helveticaTextWidthPt(const QString& text, double fontSizePt) {
    qint64 units = 0;
    const QByteArray latin = text.toLatin1();
    for (unsigned char c : latin)
        units += (c >= 32 && c <= 126) ? kHelvWidths[c - 32] : 556;
    return units * fontSizePt / 1000.0;
}

OpResult addTextWatermark(const QString& in, const QString& out,
                          const TextWatermarkSpec& spec, const std::vector<int>& pages) {
    if (spec.text.trimmed().isEmpty())
        return { false, "Enter the watermark text." };

    DecorPlan plan;
    plan.tag = kindTag(DecorKind::Watermark);
    plan.needFont = true;
    plan.gsAlpha = std::clamp(spec.opacity, 0.02, 1.0);
    plan.pages = pages;
    plan.content = [spec](int, double w, double h) {
        QByteArray body;
        body += "q\n/" + QByteArray(kGStateName) + " gs\n";
        body += rgbOp(spec.color);
        if (!spec.tiled) {
            body += placedText(spec.text, spec.fontSizePt, spec.rotationDeg, w / 2, h / 2);
        } else {
            const double tw = helveticaTextWidthPt(spec.text, spec.fontSizePt);
            const double stepX = tw + 80;
            const double stepY = spec.fontSizePt * 3 + 60;
            // Cover generously beyond the page so rotated tiles reach corners.
            for (double y = -h * 0.4; y < h * 1.4; y += stepY)
                for (double x = -w * 0.4; x < w * 1.4; x += stepX)
                    body += placedText(spec.text, spec.fontSizePt, spec.rotationDeg, x, y);
        }
        body += "Q\n";
        return body;
    };
    return applyDecor(in, out, plan);
}

OpResult addImageWatermark(const QString& in, const QString& out,
                           const ImageWatermarkSpec& spec, const std::vector<int>& pages) {
    DecorPlan plan;
    plan.tag = kindTag(DecorKind::Watermark);
    plan.gsAlpha = std::clamp(spec.opacity, 0.02, 1.0);
    plan.imagePath = spec.imagePath;
    plan.pages = pages;

    // The aspect ratio comes from applyDecor's image load — build the content
    // in two phases: first a probe load for the aspect, then the plan.
    QImage probe(spec.imagePath);
    if (probe.isNull())
        return { false, "The watermark image could not be loaded." };
    const double aspect = probe.width() > 0 ? double(probe.height()) / probe.width() : 1.0;

    plan.content = [spec, aspect](int, double w, double h) {
        const double iw = w * std::clamp(spec.scalePct, 5.0, 100.0) / 100.0;
        const double ih = iw * aspect;
        const double rad = qDegreesToRadians(spec.rotationDeg);
        const double c = std::cos(rad), s = std::sin(rad);
        QByteArray body;
        body += "q\n/" + QByteArray(kGStateName) + " gs\n";
        auto drawAt = [&](double cx, double cy) {
            // center the iw×ih image at (cx, cy) with rotation
            const double tx = cx - (c * iw / 2 - s * ih / 2);
            const double ty = cy - (s * iw / 2 + c * ih / 2);
            QByteArray one;
            one += "q\n";
            one += num(c * iw) + " " + num(s * iw) + " " + num(-s * ih) + " "
                 + num(c * ih) + " " + num(tx) + " " + num(ty) + " cm\n";
            one += "/" + QByteArray(kImageName) + " Do\nQ\n";
            return one;
        };
        if (!spec.tiled) {
            body += drawAt(w / 2, h / 2);
        } else {
            const double stepX = iw + 60, stepY = ih + 60;
            for (double y = -h * 0.3; y < h * 1.3; y += stepY)
                for (double x = -w * 0.3; x < w * 1.3; x += stepX)
                    body += drawAt(x, y);
        }
        body += "Q\n";
        return body;
    };
    return applyDecor(in, out, plan);
}

OpResult setBackground(const QString& in, const QString& out,
                       const QColor& color, const std::vector<int>& pages) {
    DecorPlan plan;
    plan.tag = kindTag(DecorKind::Background);
    plan.underlay = true;
    plan.pages = pages;
    plan.content = [color](int, double w, double h) {
        QByteArray body;
        body += "q\n" + rgbOp(color);
        body += "0 0 " + num(w) + " " + num(h) + " re f\nQ\n";
        return body;
    };
    return applyDecor(in, out, plan);
}

OpResult addPageNumbers(const QString& in, const QString& out, const PageNumberSpec& spec) {
    QString err;
    const int total = pdfPageCountOrError(in, err);
    if (total < 0) return { false, err };

    DecorPlan plan;
    plan.tag = kindTag(DecorKind::PageNumber);
    plan.needFont = true;
    plan.content = [spec, total](int idx, double w, double h) {
        const int number = spec.startAt + idx;
        QString text;
        switch (spec.format) {
            case PageNumberSpec::Format::Plain:       text = QString::number(number); break;
            case PageNumberSpec::Format::PageOfTotal: text = QStringLiteral("Page %1 of %2").arg(number).arg(total); break;
            case PageNumberSpec::Format::DashNDash:   text = QStringLiteral("- %1 -").arg(number); break;
        }
        const double tw = helveticaTextWidthPt(text, spec.fontSizePt);
        const double margin = 24;
        double x = 0, y = 0;
        using P = PageNumberSpec::Position;
        switch (spec.position) {
            case P::BottomCenter: x = (w - tw) / 2;  y = margin; break;
            case P::BottomLeft:   x = margin;        y = margin; break;
            case P::BottomRight:  x = w - tw - margin; y = margin; break;
            case P::TopCenter:    x = (w - tw) / 2;  y = h - margin - spec.fontSizePt; break;
            case P::TopLeft:      x = margin;        y = h - margin - spec.fontSizePt; break;
            case P::TopRight:     x = w - tw - margin; y = h - margin - spec.fontSizePt; break;
        }
        QByteArray body;
        body += "q\n" + rgbOp(spec.color);
        body += "BT\n/" + QByteArray(kFontName) + " " + num(spec.fontSizePt) + " Tf\n";
        body += "1 0 0 1 " + num(x) + " " + num(y) + " Tm\n";
        body += "(" + escapePdfText(text) + ") Tj\nET\nQ\n";
        return body;
    };
    return applyDecor(in, out, plan);
}

OpResult addHeaderFooter(const QString& in, const QString& out, const HeaderFooterSpec& spec) {
    QString err;
    const int total = pdfPageCountOrError(in, err);
    if (total < 0) return { false, err };

    DecorPlan plan;
    plan.tag = kindTag(DecorKind::HeaderFooter);
    plan.needFont = true;
    plan.content = [spec, total](int idx, double w, double h) {
        auto expand = [&](QString t) {
            t.replace(QStringLiteral("&[Page]"), QString::number(idx + 1));
            t.replace(QStringLiteral("&[Pages]"), QString::number(total));
            t.replace(QStringLiteral("&[Date]"), QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));
            return t;
        };
        QByteArray body;
        body += "q\n" + rgbOp(spec.color);
        auto put = [&](const QString& raw, bool top, int align) {   // 0 L, 1 C, 2 R
            const QString text = expand(raw);
            if (text.isEmpty()) return;
            const double tw = helveticaTextWidthPt(text, spec.fontSizePt);
            const double y = top ? h - spec.marginPt : spec.marginPt - spec.fontSizePt * 0.8;
            double x = spec.marginPt;
            if (align == 1) x = (w - tw) / 2;
            if (align == 2) x = w - tw - spec.marginPt;
            body += "BT\n/" + QByteArray(kFontName) + " " + num(spec.fontSizePt) + " Tf\n";
            body += "1 0 0 1 " + num(x) + " " + num(y) + " Tm\n";
            body += "(" + escapePdfText(text) + ") Tj\nET\n";
        };
        put(spec.headerLeft,   true,  0);
        put(spec.headerCenter, true,  1);
        put(spec.headerRight,  true,  2);
        put(spec.footerLeft,   false, 0);
        put(spec.footerCenter, false, 1);
        put(spec.footerRight,  false, 2);
        body += "Q\n";
        return body;
    };
    return applyDecor(in, out, plan);
}

namespace {
OpResult removeDecorByTag(const QString& in, const QString& out, const QByteArray& tag);
bool     hasDecorTag(const QString& path, const QByteArray& tag);
} // namespace

OpResult addInvisibleTextLayer(const QString& in, const QString& out,
                               const std::map<int, std::vector<OcrWord>>& wordsByPage) {
    // Re-running OCR replaces the previous layer rather than stacking.
    QString source = in;
    QString scratch;
    if (hasDecorTag(in, "ocrtext")) {
        scratch = out + QStringLiteral(".noocr");
        const OpResult rm = removeDecorByTag(in, scratch, "ocrtext");
        if (!rm.ok) return rm;
        source = scratch;
    }

    DecorPlan plan;
    plan.tag = "ocrtext";
    plan.needFont = true;
    plan.content = [&wordsByPage](int pageIdx, double /*w*/, double h) -> QByteArray {
        auto it = wordsByPage.find(pageIdx);
        if (it == wordsByPage.end() || it->second.empty()) return {};
        QByteArray body = "q\nBT\n3 Tr\n";   // render mode 3: invisible
        for (const OcrWord& word : it->second) {
            if (word.text.trimmed().isEmpty() || word.box.height() <= 1) continue;
            const double size = word.box.height() * 0.9;
            // Horizontal scaling so the invisible glyphs span the same width
            // the printed word occupies (keeps selection rectangles aligned).
            const double natural = helveticaTextWidthPt(word.text, size);
            const double tz = natural > 0.5 ? word.box.width() / natural * 100.0 : 100.0;
            const double x = word.box.left();
            const double y = h - word.box.bottom() + word.box.height() * 0.18;   // baseline
            body += "/" + QByteArray(kFontName) + " " + num(size) + " Tf\n";
            body += num(std::clamp(tz, 10.0, 500.0)) + " Tz\n";
            body += "1 0 0 1 " + num(x) + " " + num(y) + " Tm\n";
            body += "(" + escapePdfText(word.text) + ") Tj\n";
        }
        body += "100 Tz\nET\nQ\n";
        return body;
    };

    const OpResult r = applyDecor(source, out, plan);
    if (!scratch.isEmpty()) QFile::remove(scratch);
    return r;
}

bool hasOcrLayer(const QString& path) {
    return hasDecorTag(path, "ocrtext");
}

OpResult removeDecor(const QString& in, const QString& out, DecorKind kind) {
    return removeDecorByTag(in, out, kindTag(kind));
}

namespace {

OpResult removeDecorByTag(const QString& in, const QString& out, const QByteArray& tag) {
    OpenStatus status;
    auto doc = Document::open(in, status);
    if (!doc)
        return { false, QString("This PDF isn't supported yet: %1").arg(openStatusReason(status)) };

    Writer writer;
    const int pagesNum = writer.allocate();
    const int catalogNum = writer.allocate();
    DecorCopier copier(*doc, writer);

    bool removedAny = false;
    std::vector<int> newPageNums;
    for (const PageInfo& page : doc->pages()) {
        Object dict = page.dict;

        if (const Object* c = dict.find("Contents")) {
            if (c->isArray()) {
                std::vector<Object> kept;
                for (const Object& e : c->arr) {
                    bool drop = false;
                    if (e.isRef()) {
                        const Object& target = doc->resolve(e);
                        if (target.isStream()) {
                            if (const Object* k = target.find(kDecorKey))
                                drop = (k->asName() == tag);
                        }
                    }
                    if (drop) removedAny = true;
                    else      kept.push_back(e);
                }
                dict.dict.insert("Contents", Object::makeArray(std::move(kept)));
            }
        }

        const int newNum = writer.allocate();
        copier.mapPage(page.ref, newNum);
        dict.dict.insert("Parent", refTo(pagesNum));
        writer.setObjectBody(newNum, serializeObjectBody(dict, [&copier](Ref r) { return copier.copy(r); }));
        newPageNums.push_back(newNum);
    }

    if (!removedAny)
        return { false, "This document has no such decoration to remove." };

    auto sentinelOnly = [](Ref r) { return r.num < 0 ? -r.num : 0; };
    Object pagesObj; pagesObj.type = Object::Type::Dict;
    pagesObj.dict.insert("Type", Object::makeName("Pages"));
    std::vector<Object> kids;
    for (int n : newPageNums) kids.push_back(refTo(n));
    pagesObj.dict.insert("Kids", Object::makeArray(std::move(kids)));
    pagesObj.dict.insert("Count", Object::makeInt(qint64(newPageNums.size())));
    writer.setObjectBody(pagesNum, serializeObjectBody(pagesObj, sentinelOnly));

    Object catalogObj; catalogObj.type = Object::Type::Dict;
    catalogObj.dict.insert("Type", Object::makeName("Catalog"));
    catalogObj.dict.insert("Pages", refTo(pagesNum));
    const Object& srcCatalog = doc->catalog();
    if (srcCatalog.isDict()) {
        static const char* kKeep[] = { "Outlines", "AcroForm", "Names", "PageLabels",
                                       "Lang", "ViewerPreferences", "PageMode", "PageLayout" };
        for (const char* key : kKeep)
            if (const Object* v = srcCatalog.find(key); v && v->isRef())
                catalogObj.dict.insert(key, refTo(copier.copy(v->ref)));
    }
    writer.setObjectBody(catalogNum, serializeObjectBody(catalogObj, sentinelOnly));

    if (!writer.writeTo(out, catalogNum))
        return { false, "Could not write the output file — check the destination is writable." };

    OpenStatus st2;
    auto check = Document::open(out, st2);
    if (!check || check->pageCount() != doc->pageCount()) {
        QFile::remove(out);
        return { false, "The file was written but didn't verify correctly afterward, so it was discarded." };
    }
    return { true, {} };
}

bool hasDecorTag(const QString& path, const QByteArray& tag) {
    OpenStatus status;
    auto doc = Document::open(path, status);
    if (!doc) return false;
    for (const PageInfo& page : doc->pages()) {
        const Object* c = page.dict.find("Contents");
        if (!c || !c->isArray()) continue;
        for (const Object& e : c->arr) {
            if (!e.isRef()) continue;
            const Object& target = doc->resolve(e);
            if (!target.isStream()) continue;
            if (const Object* k = target.find(kDecorKey))
                if (k->asName() == tag) return true;
        }
    }
    return false;
}

} // namespace

bool hasDecor(const QString& path, DecorKind kind) {
    return hasDecorTag(path, kindTag(kind));
}

} // namespace NativeOffice::Pdf
