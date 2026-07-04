// ─────────────────────────────────────────────────────────────────────────────
// PdfWriter.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "PdfWriter.h"

#include <QFile>
#include <cctype>
#include <cstdio>

namespace NativeOffice::Pdf {

QByteArray escapePdfName(const QByteArray& raw) {
    QByteArray out;
    out.reserve(raw.size() + 2);
    for (unsigned char c : raw) {
        const bool safe = c > 0x20 && c < 0x7F
            && c != '/' && c != '(' && c != ')' && c != '<' && c != '>'
            && c != '[' && c != ']' && c != '{' && c != '}' && c != '%' && c != '#';
        if (safe) {
            out.append(char(c));
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "#%02X", c);
            out.append(buf);
        }
    }
    return out;
}

namespace {

QByteArray numberToPdf(double v) {
    if (v == static_cast<long long>(v) && std::abs(v) < 1e15)
        return QByteArray::number(static_cast<long long>(v));
    QByteArray s = QByteArray::number(v, 'f', 6);
    while (s.endsWith('0')) s.chop(1);
    if (s.endsWith('.')) s.chop(1);
    if (s.isEmpty() || s == "-") s = "0";
    return s;
}

QByteArray escapeHexString(const QByteArray& raw) {
    return "<" + raw.toHex() + ">";
}

} // namespace

QByteArray serializeObjectBody(const Object& obj, const std::function<int(Ref)>& remap) {
    switch (obj.type) {
    case Object::Type::Null:  return "null";
    case Object::Type::Bool:  return obj.boolVal ? "true" : "false";
    case Object::Type::Int:   return QByteArray::number(obj.intVal);
    case Object::Type::Real:  return numberToPdf(obj.realVal);
    case Object::Type::Name:  return "/" + escapePdfName(obj.strVal);
    case Object::Type::String: return escapeHexString(obj.strVal);
    case Object::Type::Ref: {
        const int n = remap ? remap(obj.ref) : 0;
        return QByteArray::number(n) + " 0 R";
    }
    case Object::Type::Array: {
        QByteArray out = "[";
        for (size_t i = 0; i < obj.arr.size(); ++i) {
            if (i) out += " ";
            out += serializeObjectBody(obj.arr[i], remap);
        }
        out += "]";
        return out;
    }
    case Object::Type::Dict:
    case Object::Type::Stream: {
        QByteArray out = "<<";
        for (auto it = obj.dict.begin(); it != obj.dict.end(); ++it) {
            // /Length is recomputed for the (possibly re-encoded) stream data.
            if (obj.type == Object::Type::Stream && it.key() == "Length") continue;
            out += " /" + escapePdfName(it.key()) + " " + serializeObjectBody(it.value(), remap);
        }
        if (obj.type == Object::Type::Stream)
            out += " /Length " + QByteArray::number(obj.streamData.size());
        out += " >>";
        if (obj.type == Object::Type::Stream) {
            out += "\nstream\n";
            out += obj.streamData;
            out += "\nendstream";
        }
        return out;
    }
    }
    return "null";
}

int Writer::allocate() { return m_next++; }

void Writer::setObjectBody(int objNum, const QByteArray& body) {
    m_bodies[objNum] = body;
}

bool Writer::writeTo(const QString& path, int rootObjNum) const {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;

    QByteArray out;
    out += "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";   // binary-marker comment, standard practice

    std::map<int, qint64> offsets;
    // Object numbers are contiguous from 1 (Writer::allocate()); m_next - 1
    // is the highest allocated. Any allocated-but-unset number (shouldn't
    // normally happen) is written as a harmless empty dict so the xref table
    // stays contiguous and valid.
    const int highest = m_next - 1;
    for (int n = 1; n <= highest; ++n) {
        offsets[n] = out.size();
        out += QByteArray::number(n) + " 0 obj\n";
        auto it = m_bodies.find(n);
        out += (it != m_bodies.end()) ? it->second : QByteArray("<< >>");
        out += "\nendobj\n";
    }

    const qint64 xrefOffset = out.size();
    out += "xref\n";
    out += "0 " + QByteArray::number(highest + 1) + "\n";
    out += "0000000000 65535 f \n";
    for (int n = 1; n <= highest; ++n) {
        char line[32];
        std::snprintf(line, sizeof(line), "%010lld 00000 n \n", static_cast<long long>(offsets[n]));
        out += line;
    }

    out += "trailer\n";
    out += "<< /Size " + QByteArray::number(highest + 1)
         + " /Root " + QByteArray::number(rootObjNum) + " 0 R >>\n";
    out += "startxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n";

    const qint64 written = f.write(out);
    f.close();
    return written == out.size();
}

} // namespace NativeOffice::Pdf
