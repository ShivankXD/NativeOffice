#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// QrEncoder.h: a self-contained QR Code encoder (ISO/IEC 18004).
//
// Byte mode, versions 1 to 40, all four error-correction levels, all eight
// data masks scored by the standard penalty rules. No third-party dependency
// and no Qt: it takes bytes and hands back a square bit matrix, which the tool
// page paints (and exports) however it likes.
//
// Byte mode alone is deliberate: it encodes any UTF-8 payload, URLs, Wi-Fi
// strings, vCards, plain text, at the cost of a slightly larger symbol for
// pure-digit input than numeric mode would give.
// ─────────────────────────────────────────────────────────────────────────────

#include <QByteArray>

#include <cstdint>
#include <vector>

namespace NativeOffice::Qr {

enum class Ecc {
    Low,        // tolerates ~7% damage
    Medium,     // ~15%
    Quartile,   // ~25%
    High        // ~30%
};

struct Code {
    int  version { 0 };                 // 1..40, or 0 when encoding failed
    int  size    { 0 };                 // modules per side (21 + 4*(version-1))
    std::vector<bool> modules;          // row-major, true = dark

    [[nodiscard]] bool isValid() const { return version > 0; }
    [[nodiscard]] bool at(int x, int y) const {
        if (x < 0 || y < 0 || x >= size || y >= size) return false;
        return modules[std::size_t(y) * std::size_t(size) + std::size_t(x)];
    }
};

// Encodes `data` in byte mode at the given correction level, picking the
// smallest version that fits. Returns an invalid Code when the payload is too
// large for version 40 at that level.
Code encode(const QByteArray& data, Ecc ecl);

// Largest payload, in bytes, that fits at the given level (version 40).
int capacityBytes(Ecc ecl);

} // namespace NativeOffice::Qr
