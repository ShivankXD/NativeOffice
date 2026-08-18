// ─────────────────────────────────────────────────────────────────────────────
// QrEncoder.cpp — see QrEncoder.h.
//
// The structure follows the reference algorithm in the specification: build
// the bit stream, split it into blocks, append Reed-Solomon parity, interleave,
// lay the function patterns, snake the codewords in, then try all eight masks
// and keep the one the penalty rules like best.
// ─────────────────────────────────────────────────────────────────────────────
#include "QrEncoder.h"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace NativeOffice::Qr {

namespace {

// ── Per-version tables (index 0 is padding) ─────────────────────────────────
const int8_t kEccCodewordsPerBlock[4][41] = {
    // 0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15  16  17  18  19  20  21  22  23  24  25  26  27  28  29  30  31  32  33  34  35  36  37  38  39  40
    { -1,  7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30 },  // Low
    { -1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28 },  // Medium
    { -1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30 },  // Quartile
    { -1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30 },  // High
};

const int8_t kNumEccBlocks[4][41] = {
    // 0  1  2  3  4  5  6  7  8  9 10  11  12  13  14  15  16  17  18  19  20  21  22  23  24  25  26  27  28  29  30  31  32  33  34  35  36  37  38  39  40
    { -1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4,  4,  4,  4,  4,  6,  6,  6,  6,  7,  8,  8,  9,  9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25 },  // Low
    { -1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5,  5,  8,  9,  9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49 },  // Medium
    { -1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8,  8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68 },  // Quartile
    { -1, 1, 1, 2, 4, 4, 4, 5, 5, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81 },  // High
};

constexpr int kPenaltyN1 = 3;
constexpr int kPenaltyN2 = 3;
constexpr int kPenaltyN3 = 40;
constexpr int kPenaltyN4 = 10;

int eccIndex(Ecc e) { return int(e); }

// The five-bit pattern the format information uses for each level, which is
// NOT the same ordering as the enum.
int eccFormatBits(Ecc e) {
    switch (e) {
    case Ecc::Low:      return 1;
    case Ecc::Medium:   return 0;
    case Ecc::Quartile: return 3;
    case Ecc::High:     break;
    }
    return 2;
}

// Total data + ECC modules available in a version, ignoring function patterns.
int numRawDataModules(int ver) {
    int result = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
        const int numAlign = ver / 7 + 2;
        result -= (25 * numAlign - 10) * numAlign - 55;
        if (ver >= 7) result -= 36;
    }
    return result;
}

int numDataCodewords(int ver, Ecc ecl) {
    return numRawDataModules(ver) / 8
           - kEccCodewordsPerBlock[eccIndex(ecl)][ver]
                 * kNumEccBlocks[eccIndex(ecl)][ver];
}

int byteModeCountBits(int ver) { return ver <= 9 ? 8 : 16; }

// ── GF(2^8) arithmetic ──────────────────────────────────────────────────────
uint8_t rsMultiply(uint8_t x, uint8_t y) {
    int z = 0;
    for (int i = 7; i >= 0; --i) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return uint8_t(z);
}

std::vector<uint8_t> rsDivisor(int degree) {
    std::vector<uint8_t> result(std::size_t(degree), 0);
    result[std::size_t(degree) - 1] = 1;
    uint8_t root = 1;
    for (int i = 0; i < degree; ++i) {
        for (std::size_t j = 0; j < result.size(); ++j) {
            result[j] = rsMultiply(result[j], root);
            if (j + 1 < result.size()) result[j] ^= result[j + 1];
        }
        root = rsMultiply(root, 0x02);
    }
    return result;
}

std::vector<uint8_t> rsRemainder(const std::vector<uint8_t>& data,
                                 const std::vector<uint8_t>& divisor) {
    std::vector<uint8_t> result(divisor.size(), 0);
    for (uint8_t b : data) {
        const uint8_t factor = b ^ result[0];
        result.erase(result.begin());
        result.push_back(0);
        for (std::size_t i = 0; i < result.size(); ++i)
            result[i] ^= rsMultiply(divisor[i], factor);
    }
    return result;
}

// ── The symbol under construction ───────────────────────────────────────────
struct Canvas {
    int size { 0 };
    int version { 0 };
    Ecc ecl { Ecc::Medium };
    std::vector<bool> modules;     // dark?
    std::vector<bool> function;    // part of a function pattern?

    bool get(int x, int y) const { return modules[std::size_t(y) * size + x]; }
    void set(int x, int y, bool dark) { modules[std::size_t(y) * size + x] = dark; }
    bool isFn(int x, int y) const { return function[std::size_t(y) * size + x]; }

    void setFunction(int x, int y, bool dark) {
        if (x < 0 || y < 0 || x >= size || y >= size) return;
        set(x, y, dark);
        function[std::size_t(y) * size + x] = true;
    }

    std::vector<int> alignmentPositions() const {
        if (version == 1) return {};
        const int numAlign = version / 7 + 2;
        const int step = (version == 32) ? 26
                       : (version * 4 + numAlign * 2 + 1) / (numAlign * 2 - 2) * 2;
        std::vector<int> result;
        for (int pos = size - 7; int(result.size()) < numAlign - 1; pos -= step)
            result.insert(result.begin(), pos);
        result.insert(result.begin(), 6);
        return result;
    }

    void drawFinder(int cx, int cy) {
        for (int dy = -4; dy <= 4; ++dy)
            for (int dx = -4; dx <= 4; ++dx) {
                const int dist = std::max(std::abs(dx), std::abs(dy));   // Chebyshev
                setFunction(cx + dx, cy + dy, dist != 2 && dist != 4);
            }
    }

    void drawAlignment(int cx, int cy) {
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx)
                setFunction(cx + dx, cy + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
    }

    void drawFormatBits(int mask) {
        const int data = eccFormatBits(ecl) << 3 | mask;
        int rem = data;
        for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
        const int bits = (data << 10 | rem) ^ 0x5412;

        auto bit = [bits](int i) { return ((bits >> i) & 1) != 0; };

        for (int i = 0; i <= 5; ++i) setFunction(8, i, bit(i));
        setFunction(8, 7, bit(6));
        setFunction(8, 8, bit(7));
        setFunction(7, 8, bit(8));
        for (int i = 9; i < 15; ++i) setFunction(14 - i, 8, bit(i));

        for (int i = 0; i < 8; ++i) setFunction(size - 1 - i, 8, bit(i));
        for (int i = 8; i < 15; ++i) setFunction(8, size - 15 + i, bit(i));
        setFunction(8, size - 8, true);   // always-dark module
    }

    void drawVersionBits() {
        if (version < 7) return;
        int rem = version;
        for (int i = 0; i < 12; ++i) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
        const long bits = long(version) << 12 | rem;
        for (int i = 0; i < 18; ++i) {
            const bool b = ((bits >> i) & 1) != 0;
            const int a = size - 11 + i % 3;
            const int c = i / 3;
            setFunction(a, c, b);
            setFunction(c, a, b);
        }
    }

    void drawFunctionPatterns() {
        for (int i = 0; i < size; ++i) {
            setFunction(6, i, i % 2 == 0);
            setFunction(i, 6, i % 2 == 0);
        }
        drawFinder(3, 3);
        drawFinder(size - 4, 3);
        drawFinder(3, size - 4);

        const std::vector<int> align = alignmentPositions();
        const std::size_t n = align.size();
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = 0; j < n; ++j) {
                const bool corner = (i == 0 && j == 0) || (i == 0 && j == n - 1)
                                 || (i == n - 1 && j == 0);
                if (!corner) drawAlignment(align[i], align[j]);
            }

        drawFormatBits(0);   // placeholder; reserves the area
        drawVersionBits();
    }

    void drawCodewords(const std::vector<uint8_t>& data) {
        std::size_t i = 0;
        for (int right = size - 1; right >= 1; right -= 2) {
            if (right == 6) right = 5;                 // skip the vertical timing line
            for (int vert = 0; vert < size; ++vert) {
                for (int j = 0; j < 2; ++j) {
                    const int x = right - j;
                    const bool upward = ((right + 1) & 2) == 0;
                    const int y = upward ? size - 1 - vert : vert;
                    if (!isFn(x, y) && i < data.size() * 8) {
                        set(x, y, ((data[i >> 3] >> (7 - int(i & 7))) & 1) != 0);
                        ++i;
                    }
                    // Any remaining modules stay light, as the spec requires.
                }
            }
        }
    }

    void applyMask(int mask) {
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                if (isFn(x, y)) continue;
                bool invert = false;
                switch (mask) {
                case 0: invert = (x + y) % 2 == 0; break;
                case 1: invert = y % 2 == 0; break;
                case 2: invert = x % 3 == 0; break;
                case 3: invert = (x + y) % 3 == 0; break;
                case 4: invert = (x / 3 + y / 2) % 2 == 0; break;
                case 5: invert = x * y % 2 + x * y % 3 == 0; break;
                case 6: invert = (x * y % 2 + x * y % 3) % 2 == 0; break;
                case 7: invert = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
                default: break;
                }
                if (invert) set(x, y, !get(x, y));
            }
    }

    // ── Penalty scoring ─────────────────────────────────────────────────────
    void finderPenaltyAddHistory(int runLength, std::array<int, 7>& history) const {
        if (history[0] == 0) runLength += size;   // the light border counts
        std::copy_backward(history.cbegin(), history.cend() - 1, history.end());
        history[0] = runLength;
    }

    static int finderPenaltyCountPatterns(const std::array<int, 7>& h) {
        const int n = h[1];
        const bool core = n > 0 && h[2] == n && h[3] == n * 3 && h[4] == n && h[5] == n;
        return (core && h[0] >= n * 4 && h[6] >= n ? 1 : 0)
             + (core && h[6] >= n * 4 && h[0] >= n ? 1 : 0);
    }

    int finderPenaltyTerminate(bool currentColor, int runLength,
                               std::array<int, 7>& history) const {
        if (currentColor) { finderPenaltyAddHistory(runLength, history); runLength = 0; }
        runLength += size;
        finderPenaltyAddHistory(runLength, history);
        return finderPenaltyCountPatterns(history);
    }

    long penaltyScore() const {
        long result = 0;

        for (int y = 0; y < size; ++y) {
            bool color = false; int run = 0;
            std::array<int, 7> history {};
            for (int x = 0; x < size; ++x) {
                if (get(x, y) == color) {
                    ++run;
                    if (run == 5) result += kPenaltyN1;
                    else if (run > 5) ++result;
                } else {
                    finderPenaltyAddHistory(run, history);
                    if (!color) result += finderPenaltyCountPatterns(history) * kPenaltyN3;
                    color = get(x, y);
                    run = 1;
                }
            }
            result += finderPenaltyTerminate(color, run, history) * kPenaltyN3;
        }

        for (int x = 0; x < size; ++x) {
            bool color = false; int run = 0;
            std::array<int, 7> history {};
            for (int y = 0; y < size; ++y) {
                if (get(x, y) == color) {
                    ++run;
                    if (run == 5) result += kPenaltyN1;
                    else if (run > 5) ++result;
                } else {
                    finderPenaltyAddHistory(run, history);
                    if (!color) result += finderPenaltyCountPatterns(history) * kPenaltyN3;
                    color = get(x, y);
                    run = 1;
                }
            }
            result += finderPenaltyTerminate(color, run, history) * kPenaltyN3;
        }

        for (int y = 0; y < size - 1; ++y)
            for (int x = 0; x < size - 1; ++x) {
                const bool c = get(x, y);
                if (c == get(x + 1, y) && c == get(x, y + 1) && c == get(x + 1, y + 1))
                    result += kPenaltyN2;
            }

        int dark = 0;
        for (bool b : modules) if (b) ++dark;
        const int total = size * size;
        const int k = int((std::abs(long(dark) * 20L - long(total) * 10L) + total - 1) / total) - 1;
        result += long(k) * kPenaltyN4;
        return result;
    }
};

// Splits `data` into blocks, appends parity, and interleaves as the spec lays
// the codewords out.
std::vector<uint8_t> addEccAndInterleave(const std::vector<uint8_t>& data,
                                         int version, Ecc ecl) {
    const int numBlocks   = kNumEccBlocks[eccIndex(ecl)][version];
    const int blockEccLen = kEccCodewordsPerBlock[eccIndex(ecl)][version];
    const int rawCodewords = numRawDataModules(version) / 8;
    const int numShortBlocks = numBlocks - rawCodewords % numBlocks;
    const int shortBlockLen  = rawCodewords / numBlocks;

    std::vector<std::vector<uint8_t>> blocks;
    const std::vector<uint8_t> divisor = rsDivisor(blockEccLen);
    for (int i = 0, k = 0; i < numBlocks; ++i) {
        const int len = shortBlockLen - blockEccLen + (i < numShortBlocks ? 0 : 1);
        std::vector<uint8_t> dat(data.cbegin() + k, data.cbegin() + k + len);
        k += len;
        const std::vector<uint8_t> ecc = rsRemainder(dat, divisor);
        if (i < numShortBlocks) dat.push_back(0);      // pad slot, skipped below
        dat.insert(dat.end(), ecc.cbegin(), ecc.cend());
        blocks.push_back(std::move(dat));
    }

    std::vector<uint8_t> result;
    result.reserve(std::size_t(rawCodewords));
    for (std::size_t i = 0; i < blocks[0].size(); ++i)
        for (std::size_t j = 0; j < blocks.size(); ++j)
            if (int(i) != shortBlockLen - blockEccLen || int(j) >= numShortBlocks)
                result.push_back(blocks[j][i]);
    return result;
}

} // namespace

int capacityBytes(Ecc ecl) {
    // Version 40: 16 count bits and a 4-bit mode indicator come off the top.
    return numDataCodewords(40, ecl) - 3;
}

Code encode(const QByteArray& data, Ecc ecl) {
    const int len = int(data.size());

    int version = 0;
    for (int v = 1; v <= 40; ++v) {
        const int capacityBits = numDataCodewords(v, ecl) * 8;
        const int needed = 4 + byteModeCountBits(v) + 8 * len;
        if (needed <= capacityBits) { version = v; break; }
    }
    if (version == 0) return {};

    // ── Bit stream ──────────────────────────────────────────────────────────
    std::vector<bool> bits;
    auto append = [&bits](uint32_t value, int n) {
        for (int i = n - 1; i >= 0; --i) bits.push_back(((value >> i) & 1) != 0);
    };
    append(0b0100, 4);                              // byte mode
    append(uint32_t(len), byteModeCountBits(version));
    for (char c : data) append(uint8_t(c), 8);

    const int dataCapacityBits = numDataCodewords(version, ecl) * 8;
    append(0, std::min(4, dataCapacityBits - int(bits.size())));   // terminator
    while (bits.size() % 8 != 0) bits.push_back(false);
    for (uint8_t pad = 0xEC; int(bits.size()) < dataCapacityBits; pad ^= 0xEC ^ 0x11)
        append(pad, 8);

    std::vector<uint8_t> dataCodewords(bits.size() / 8, 0);
    for (std::size_t i = 0; i < bits.size(); ++i)
        if (bits[i]) dataCodewords[i >> 3] |= uint8_t(1 << (7 - (i & 7)));

    // ── Draw ────────────────────────────────────────────────────────────────
    Canvas c;
    c.version = version;
    c.ecl = ecl;
    c.size = version * 4 + 17;
    c.modules.assign(std::size_t(c.size) * c.size, false);
    c.function.assign(std::size_t(c.size) * c.size, false);
    c.drawFunctionPatterns();
    c.drawCodewords(addEccAndInterleave(dataCodewords, version, ecl));

    int bestMask = 0;
    long bestScore = -1;
    for (int mask = 0; mask < 8; ++mask) {
        c.applyMask(mask);
        c.drawFormatBits(mask);
        const long score = c.penaltyScore();
        if (bestScore < 0 || score < bestScore) { bestScore = score; bestMask = mask; }
        c.applyMask(mask);   // XOR is its own inverse
    }
    c.applyMask(bestMask);
    c.drawFormatBits(bestMask);

    Code out;
    out.version = version;
    out.size    = c.size;
    out.modules = c.modules;
    return out;
}

} // namespace NativeOffice::Qr
