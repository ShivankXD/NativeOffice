// ─────────────────────────────────────────────────────────────────────────────
// PdfForms.cpp — see PdfForms.h.
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfForms.h"
#include "PdfDecor.h"        // helveticaTextWidthPt
#include "PdfRebuild.h"

#include <QFile>

#include <functional>
#include <map>

namespace NativeOffice::Pdf {

using namespace NativeOffice::Pdf::rebuild;

namespace {

constexpr char kFormFont[] = "NOformF1";

QByteArray num(double v) { return QByteArray::number(v, 'f', 3); }

QByteArray escapePdfText(const QString& text) {
    QByteArray out;
    const QByteArray latin = text.toLatin1();
    for (char c : latin) {
        if (c == '\\' || c == '(' || c == ')') out += '\\';
        out += c;
    }
    return out;
}

Object strObj(const QString& s) {
    Object o; o.type = Object::Type::String; o.strVal = s.toUtf8(); return o;
}

// Map a widget's page: prefer /P; else scan page /Annots for the field ref.
int findFieldPage(const Document& doc, Ref fieldRef, const Object& fieldDict) {
    if (const Object* p = fieldDict.find("P")) {
        if (p->isRef())
            for (int i = 0; i < doc.pageCount(); ++i)
                if (doc.pages()[size_t(i)].ref == p->ref) return i;
    }
    for (int i = 0; i < doc.pageCount(); ++i) {
        const Object* an = doc.pages()[size_t(i)].dict.find("Annots");
        if (!an) continue;
        const Object& arr = doc.resolve(*an);
        if (!arr.isArray()) continue;
        for (const Object& e : arr.arr)
            if (e.isRef() && e.ref == fieldRef) return i;
    }
    return -1;
}

FormField::Type fieldType(const QByteArray& ft, long long flags) {
    if (ft == "Tx") return FormField::Type::Text;
    if (ft == "Btn") {
        if (flags & (1 << 16)) return FormField::Type::Button;   // pushbutton
        if (flags & (1 << 15)) return FormField::Type::Radio;    // radio
        return FormField::Type::Checkbox;
    }
    if (ft == "Ch")  return FormField::Type::Choice;
    if (ft == "Sig") return FormField::Type::Signature;
    return FormField::Type::Unknown;
}

// Recursively walk the field tree collecting terminal fields.
void walkFields(const Document& doc, const Object& fieldsArray, const QString& prefix,
                std::function<void(Ref, const Object&, const QString&)> onTerminal,
                int depth = 0) {
    if (depth > 32 || !fieldsArray.isArray()) return;
    for (const Object& e : fieldsArray.arr) {
        if (!e.isRef()) continue;
        const Object& f = doc.resolve(e);
        if (!f.isDict()) continue;

        QString partial;
        if (const Object* t = f.find("T")) partial = QString::fromUtf8(doc.resolve(*t).strVal);
        const QString full = prefix.isEmpty() ? partial
                            : (partial.isEmpty() ? prefix : prefix + "." + partial);

        const Object* kids = f.find("Kids");
        // A node with Kids that are themselves fields (have /T) is a
        // non-terminal; Kids that are pure widgets belong to this field.
        bool hasFieldKids = false;
        if (kids) {
            const Object& ka = doc.resolve(*kids);
            if (ka.isArray())
                for (const Object& k : ka.arr)
                    if (k.isRef() && doc.resolve(k).find("T")) { hasFieldKids = true; break; }
            if (hasFieldKids) {
                walkFields(doc, ka, full, onTerminal, depth + 1);
                continue;
            }
        }
        onTerminal(e.ref, f, full);
    }
}

} // namespace

std::vector<FormField> detectFormFields(const QString& path) {
    std::vector<FormField> out;
    OpenStatus status;
    auto doc = Document::open(path, status);
    if (!doc) return out;

    const Object& cat = doc->catalog();
    const Object* afRef = cat.find("AcroForm");
    if (!afRef) return out;
    const Object& acro = doc->resolve(*afRef);
    const Object* fields = acro.find("Fields");
    if (!fields) return out;
    const Object& fieldsArr = doc->resolve(*fields);

    walkFields(*doc, fieldsArr, {}, [&](Ref ref, const Object& f, const QString& full) {
        QByteArray ft;
        if (const Object* t = f.find("FT")) ft = doc->resolve(*t).asName();
        long long flags = 0;
        if (const Object* ff = f.find("Ff")) flags = doc->resolve(*ff).asInt();

        FormField field;
        field.fullName = full;
        field.type = fieldType(ft, flags);
        field.multiline = (flags & (1 << 12)) != 0;
        if (const Object* v = f.find("V")) {
            const Object& vo = doc->resolve(*v);
            if (vo.type == Object::Type::String) field.value = QString::fromUtf8(vo.strVal);
            else if (vo.isName()) field.value = QString::fromLatin1(vo.asName());
        }
        field.pageIndex = findFieldPage(*doc, ref, f);
        if (const Object* r = f.find("Rect")) {
            const Object& ra = doc->resolve(*r);
            if (ra.isArray() && ra.arr.size() == 4) {
                auto vv = [&](int k) {
                    const Object& o = doc->resolve(ra.arr[size_t(k)]);
                    return o.type == Object::Type::Real ? o.realVal : double(o.asInt());
                };
                double bx, by, bw, bh;
                if (field.pageIndex >= 0)
                    mediaBoxOf(*doc, doc->pages()[size_t(field.pageIndex)].dict, bx, by, bw, bh);
                else bh = 792;
                const double lly = vv(1), ury = vv(3);
                field.rect = QRectF(vv(0), bh - ury, vv(2) - vv(0), ury - lly);
            }
        }
        out.push_back(field);
    });
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// fill
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// Builds a text-field widget appearance form XObject for `value` in a box of
// wxh points. Returns its object number.
int buildFieldAppearance(Writer& writer, double w, double h, const QString& value,
                         double fontSize, int fontObj, bool multiline) {
    QByteArray content = "/Tx BMC\nq\n";
    // Clip to the box.
    content += "0 0 " + num(w) + " " + num(h) + " re W n\n";
    content += "BT\n/" + QByteArray(kFormFont) + " " + num(fontSize) + " Tf 0 g\n";
    const double pad = 2.0;
    if (!multiline) {
        const double ty = (h - fontSize) / 2 + fontSize * 0.2;
        content += "1 0 0 1 " + num(pad) + " " + num(std::max(pad, ty)) + " Tm ("
                 + escapePdfText(value) + ") Tj\n";
    } else {
        double ty = h - fontSize;
        const double lead = fontSize * 1.15;
        for (const QString& line : value.split('\n')) {
            content += "1 0 0 1 " + num(pad) + " " + num(ty) + " Tm ("
                     + escapePdfText(line) + ") Tj\n";
            ty -= lead;
        }
    }
    content += "ET\nQ\nEMC\n";

    QMap<QByteArray, Object> d;
    d.insert("Type", Object::makeName("XObject"));
    d.insert("Subtype", Object::makeName("Form"));
    d.insert("FormType", Object::makeInt(1));
    std::vector<Object> bbox;
    for (double v : { 0.0, 0.0, w, h }) bbox.push_back(realObj(v));
    d.insert("BBox", Object::makeArray(std::move(bbox)));
    Object res; res.type = Object::Type::Dict;
    Object fonts; fonts.type = Object::Type::Dict;
    fonts.dict.insert(kFormFont, refTo(fontObj));
    res.dict.insert("Font", fonts);
    d.insert("Resources", res);
    return addRawStream(writer, std::move(d), content);
}

} // namespace

OpResult fillTextFields(const QString& in, const QString& out,
                        const std::map<QString, QString>& values) {
    OpenStatus status;
    auto doc = Document::open(in, status);
    if (!doc)
        return { false, QString("This PDF isn't supported yet: %1").arg(openStatusReason(status)) };

    const Object& cat = doc->catalog();
    const Object* afRef = cat.find("AcroForm");   // may be an inline dict or a ref
    if (!afRef)
        return { false, "This PDF has no fillable form." };

    Writer writer;
    const int pagesNum = writer.allocate();
    const int catalogNum = writer.allocate();
    Copier copier(*doc, writer);

    // Pre-map pages.
    const int n = doc->pageCount();
    std::vector<int> newPageNums(size_t(n), 0);
    for (int i = 0; i < n; ++i) {
        newPageNums[size_t(i)] = writer.allocate();
        copier.mapPage(doc->pages()[size_t(i)].ref, newPageNums[size_t(i)]);
    }

    // A shared Helvetica for regenerated appearances.
    int formFont = writer.allocate();
    {
        Object font; font.type = Object::Type::Dict;
        font.dict.insert("Type", Object::makeName("Font"));
        font.dict.insert("Subtype", Object::makeName("Type1"));
        font.dict.insert("BaseFont", Object::makeName("Helvetica"));
        font.dict.insert("Encoding", Object::makeName("WinAnsiEncoding"));
        writer.setObjectBody(formFont, serializeObjectBody(font, [](Ref) { return 0; }));
    }

    // Substitute each named text field with a modified copy (new /V + /AP).
    int filled = 0;
    const Object& acro = doc->resolve(*afRef);
    if (const Object* fields = acro.find("Fields")) {
        walkFields(*doc, doc->resolve(*fields), {},
            [&](Ref ref, const Object& f, const QString& full) {
                auto it = values.find(full);
                if (it == values.end()) return;
                QByteArray ft;
                if (const Object* t = f.find("FT")) ft = doc->resolve(*t).asName();
                if (ft != "Tx") return;

                double bx, by, bw, bh, rw = 100, rh = 14;
                if (const Object* r = f.find("Rect")) {
                    const Object& ra = doc->resolve(*r);
                    if (ra.isArray() && ra.arr.size() == 4) {
                        auto vv = [&](int k) {
                            const Object& o = doc->resolve(ra.arr[size_t(k)]);
                            return o.type == Object::Type::Real ? o.realVal : double(o.asInt());
                        };
                        rw = std::abs(vv(2) - vv(0));
                        rh = std::abs(vv(3) - vv(1));
                    }
                }
                Q_UNUSED(bx); Q_UNUSED(by); Q_UNUSED(bw); Q_UNUSED(bh);

                long long flags = 0;
                if (const Object* ff = f.find("Ff")) flags = doc->resolve(*ff).asInt();
                const bool multiline = (flags & (1 << 12)) != 0;
                double fontSize = multiline ? 10 : std::max(6.0, std::min(rh - 4, 12.0));

                const int apNum = buildFieldAppearance(writer, rw, rh, it->second,
                                                       fontSize, formFont, multiline);

                Object nf = f;   // copy the field dict
                nf.dict.insert("V", strObj(it->second));
                Object apDict; apDict.type = Object::Type::Dict;
                apDict.dict.insert("N", refTo(apNum));
                nf.dict.insert("AP", apDict);

                const int newFieldNum = writer.allocate();
                copier.preMap(ref, newFieldNum);
                writer.setObjectBody(newFieldNum,
                    serializeObjectBody(nf, [&copier](Ref r) { return copier.copy(r); }));
                ++filled;
            });
    }

    if (filled == 0)
        return { false, "None of the given fields exist in this form." };

    // Copy the pages (their /Annots reference the — now substituted — fields).
    for (int i = 0; i < n; ++i) {
        Object dict = doc->pages()[size_t(i)].dict;
        dict.dict.insert("Parent", refTo(pagesNum));
        writer.setObjectBody(newPageNums[size_t(i)],
            serializeObjectBody(dict, [&copier](Ref r) { return copier.copy(r); }));
    }

    // Rebuild AcroForm as its own indirect object (works whether the source
    // AcroForm was inline or an indirect ref). Its /Fields refs resolve to
    // the substituted field objects via the copier.
    //
    // NeedAppearances is set FALSE: we generate correct /AP streams for every
    // filled field, and a `true` value tells viewers to discard those and
    // regenerate — which static (non-interactive) renderers like PDFium's
    // can't do, leaving the fields blank.
    const int newAcroNum = writer.allocate();
    if (afRef->isRef()) copier.preMap(afRef->ref, newAcroNum);
    {
        Object na = acro;
        Object needAp; needAp.type = Object::Type::Bool; needAp.boolVal = false;
        na.dict.insert("NeedAppearances", needAp);
        writer.setObjectBody(newAcroNum,
            serializeObjectBody(na, [&copier](Ref r) { return copier.copy(r); }));
    }

    QMap<QByteArray, Object> extra;
    extra.insert("AcroForm", refTo(newAcroNum));
    return finishTree(writer, pagesNum, catalogNum, newPageNums, out, *doc, copier,
                      { QByteArray("AcroForm") }, extra);
}

OpResult flattenForms(const QString& in, const QString& out) {
    // Minimal flatten: fill nothing new, but drop the AcroForm so the baked
    // widget appearances become static content-equivalent overlays. (A full
    // flatten that merges each widget /AP into page content is a later
    // refinement; most viewers already print widget appearances.)
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
        dict.dict.insert("Parent", refTo(pagesNum));
        writer.setObjectBody(newPageNums[size_t(i)],
            serializeObjectBody(dict, [&copier](Ref r) { return copier.copy(r); }));
    }
    // finishTree copies catalog extras; skip AcroForm to drop interactivity.
    return finishTree(writer, pagesNum, catalogNum, newPageNums, out, *doc, copier,
                      { QByteArray("AcroForm") });
}

} // namespace NativeOffice::Pdf
