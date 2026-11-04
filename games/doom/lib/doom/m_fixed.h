/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * DOOM's fixed-point arithmetic (16.16) for the renderer.
 *
 * The renderer works in 16.16 fixed point exactly as the 1993 code does: one
 * unit of map geometry is 1 << 16, and an angle is a 32-bit binary angle
 * measure where a full turn wraps through 2^32. This header is the whole
 * contract -- there is no translation unit, because every function is two
 * instructions and a shift, and inlining them is what keeps the BSP walk and
 * the column emitter free of call overhead on the ESP32.
 *
 * DIVISION DELIBERATELY DIFFERS FROM UPSTREAM
 *
 * DOOM's m_fixed.c implements FixedDiv2() with double precision:
 *
 *     c = ((double)a) / ((double)b) * FRACUNIT;
 *     return (fixed_t) c;
 *
 * That is a floating-point operation on the renderer's hot path, and it is the
 * one thing this port's build rules forbid. Here the same quotient is computed
 * with a 64-bit integer division, which truncates toward zero exactly as the
 * double-to-int conversion does. The two agree for every value the renderer
 * produces; they can differ by one unit only when the exact quotient is within
 * about 2^-22 of an integer, which no renderer input reaches. The independent
 * host oracle (tools/doom_ref.py) uses the same integer rule, so the two
 * implementations are comparable bit for bit.
 *
 * FixedDiv() keeps DOOM's overflow gate: `abs(a) >> 14 >= abs(b)` answers
 * INT32_MAX or INT32_MIN instead of dividing, which is how upstream avoids the
 * 64-bit overflow in (a << 16) / b. The gate is part of the renderer's
 * behaviour, not a defensive extra: R_ScaleFromGlobalAngle() relies on it.
 *
 * This is C99 only: <stdint.h>, no allocation, no globals, no floating point.
 */
#ifndef DOOM_M_FIXED_H
#define DOOM_M_FIXED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One unit of geometry: 16.16, so FRACUNIT is 65536 and one map unit is
 * (1 << FRACBITS). */
#define FRACBITS 16
#define FRACUNIT ((int32_t)1 << FRACBITS)

/* Signed 16.16 fixed point, and the unsigned 32-bit binary angle measure. */
typedef int32_t fixed_t;
typedef uint32_t angle_t;

/* A full turn is 2^32; the named quadrants are DOOM's tables.h values. */
#define ANG45 0x20000000u
#define ANG90 0x40000000u
#define ANG180 0x80000000u
#define ANG270 0xc0000000u

/* The 32-bit angle is truncated to a table index by this shift: 8192 fine
 * angles per turn, so one fine angle is 2^19 binary-angle units. */
#define ANGLETOFINESHIFT 19
#define FINEANGLES 8192
#define FINEMASK (FINEANGLES - 1)

/* The arc-tangent table is indexed by SlopeDiv(), which is clamped here. */
#define SLOPERANGE 2048

/**
 * @brief 16.16 multiply: (a * b) >> 16, truncated by the shift.
 *
 * The 64-bit product is required: two 16.16 values whose magnitudes are near
 * INT32_MAX overflow a 32-bit product. The arithmetic right shift implements
 * floor() on negative products, which is what upstream's `>> FRACBITS` does
 * on every compiler this port supports.
 */
static inline fixed_t fixed_mul(fixed_t a, fixed_t b) {
    return (fixed_t)(((int64_t)a * (int64_t)b) >> FRACBITS);
}

/**
 * @brief a / b in 16.16, truncated toward zero, with no overflow gate.
 *
 * Only call this when |a| >> 14 < |b|; otherwise the (a << 16) product cannot
 * be represented and the result is undefined, exactly as upstream's
 * FixedDiv2() would be.
 */
static inline fixed_t fixed_div2(fixed_t a, fixed_t b) {
    return (fixed_t)(((int64_t)a << FRACBITS) / (int64_t)b);
}

/**
 * @brief a / b in 16.16 with DOOM's overflow gate.
 *
 * When the quotient cannot fit, the answer saturates by the sign of a^b, which
 * is upstream's MININT/MAXINT branch. The gate is evaluated in 64 bits so
 * INT32_MIN does not overflow.
 */
static inline fixed_t fixed_div(fixed_t a, fixed_t b) {
    const int64_t magnitudeA = a < 0 ? -(int64_t)a : (int64_t)a;
    const int64_t magnitudeB = b < 0 ? -(int64_t)b : (int64_t)b;

    if ((magnitudeA >> 14) >= magnitudeB) {
        return ((a ^ b) < 0) ? INT32_MIN : INT32_MAX;
    }
    return fixed_div2(a, b);
}

/**
 * @brief DOOM's SlopeDiv(): a slope ratio as an arc-tangent table index.
 *
 * `den < 512` answers the clamp without dividing, which is the behaviour the
 * renderer relies on for near-vertical rays. The ratio is computed by shifting
 * both operands (num << 3) / (den >> 8), exactly as tables.c does, so the
 * index matches the table upstream indexes.
 */
static inline int slope_div(uint32_t num, uint32_t den) {
    uint32_t answer;

    if (den < 512u) {
        return SLOPERANGE;
    }
    answer = (num << 3) / (den >> 8);
    return answer <= SLOPERANGE ? (int)answer : SLOPERANGE;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_M_FIXED_H */
