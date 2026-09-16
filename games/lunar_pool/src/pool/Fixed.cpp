/*
 * Fixed.cpp - see Fixed.h.
 */
#include "pool/Fixed.h"

namespace pool {

int64_t divRound(int64_t num, int64_t den) {
    // den is required to be strictly positive (see Fixed.h); every call site
    // in this codebase passes a compile-time-known positive divisor.
    const uint64_t absDen = static_cast<uint64_t>(den);
    if (num >= 0) {
        const uint64_t absNum = static_cast<uint64_t>(num);
        return static_cast<int64_t>((absNum + absDen / 2) / absDen);
    }
    // Negate through unsigned wraparound (well-defined by the standard)
    // instead of `-num`, which is undefined behavior when num == INT64_MIN
    // (its magnitude does not fit in a positive int64_t).
    const uint64_t absNum = static_cast<uint64_t>(0) - static_cast<uint64_t>(num);
    const uint64_t quotient = (absNum + absDen / 2) / absDen;
    return -static_cast<int64_t>(quotient);
}

uint32_t isqrt64(uint64_t value) {
    uint64_t remainder = 0;
    uint64_t root = 0;
    // Binary digit-by-digit method: consumes the input two bits at a time,
    // most-significant pair first, so each of the 32 iterations resolves one
    // more bit of the 32-bit result.
    for (int shift = 31; shift >= 0; --shift) {
        root <<= 1;
        remainder = (remainder << 2) | ((value >> (2 * shift)) & 0x3u);
        const uint64_t candidate = (root << 1) | 1u;
        if (remainder >= candidate) {
            remainder -= candidate;
            root |= 1u;
        }
    }
    return static_cast<uint32_t>(root);
}

int32_t toPixel(int32_t raw) {
    return static_cast<int32_t>(divRound(raw, kPxScale));
}

}  // namespace pool
