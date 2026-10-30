/*
 * The seam between the DOOM core (C) and the port (C++).
 *
 * The DOOM core is C and will live in this directory, compiled by the C
 * compiler under the firmware's -Os / -fno-lto / -fgc-sections flags while the
 * engine, the scene and everything under src/doomc are C++. That pairing only
 * works if both sides agree on the symbols and on the calling convention, so
 * this header is the single list of them:
 *
 *   - the three `doom_bridge_*` entry points are defined in C++
 *     (src/doomc/DoomLinkBridge.cpp) and called from C;
 *   - the two `doom_link_probe_*` functions are defined in C
 *     (doom_link_probe.c) and called from C++.
 *
 * Everything here is C99 plus <stdint.h>: no C++ construct, no compound
 * literal, no variable-length array, no allocation. A C++ consumer sees the
 * same declarations because of the `extern "C"` guard below, which is the whole
 * point -- what crosses the boundary is a plain C symbol, never a mangled one.
 *
 * The probe is a link test, not DOOM code: it proves that a C translation unit
 * built by this project can call into the C++ engine and back, on both shipped
 * targets, with a value the C side verifies. It carries no licence header
 * because it contains no DOOM source.
 */
#ifndef DOOM_LINK_PROBE_H
#define DOOM_LINK_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- The C++ side of the boundary ---------------------------------------- */

/**
 * @brief Applies the shared salt to a value crossing from C to C++ and back.
 *
 * The contract is exactly `value ^ DOOM_LINK_SALT`, nothing else: any bit the
 * boundary lost on the way in, or on the way out, shows up as a mismatch in the
 * caller's comparison. The salt lives in this header, so the C side's
 * expectation and the C++ side's transform cannot drift apart.
 */
uint32_t doom_bridge_mix(uint32_t value);

/**
 * @brief Returns one byte of the port's palette table, through a real port TU.
 *
 * Exists so the crossing reaches a symbol this port owns and not only the
 * engine: index 0 must answer DOOM_LINK_PALETTE_BLACK_BYTE and index 15
 * DOOM_LINK_PALETTE_WHITE_BYTE, both pinned at build time in
 * src/palette/DoomPalette.cpp. An index outside 0..255 answers 0 and no
 * palette entry; the probe never asks for one.
 */
uint32_t doom_bridge_palette_byte(uint32_t palette_index);

/**
 * @brief Writes the probe's single `LINK,...` line through the engine console.
 *
 * The line is exactly `LINK,status=ok,checksum=0x%08X\n` when @p status is
 * DOOM_LINK_STATUS_OK and `LINK,status=fail,checksum=0x%08X\n` otherwise, with
 * @p checksum formatted as an 8-digit uppercase hex value. Called once per
 * probe run, so one boot prints one line.
 */
void doom_bridge_emit_line(uint32_t status, uint32_t checksum);

/* --- The C side of the boundary ------------------------------------------ */

/**
 * @brief FNV-1a over this probe's fixed table, seeded and repeated.
 *
 * The table is the 32 bytes of the ASCII string `pixelroot32/doom-link-probe/v1`
 * followed by two zero bytes. One iteration hashes all 32 bytes; @p iterations
 * passes are applied in order, and `iterations == 0` returns the offset basis
 * XOR @p seed, which is the documented boundary of the loop rather than a
 * special case. Arithmetic is 32-bit unsigned and wraps, so the answer is the
 * same on every target and never depends on state outside the arguments.
 */
uint32_t doom_link_probe_checksum(uint32_t seed, uint32_t iterations);

/**
 * @brief Runs the probe once: C, through the C++ bridge, and back again.
 *
 * Computes the checksum, asks the C++ side for the salted value and for two
 * palette bytes, verifies all three against what this side computed, then asks
 * the C++ side for the one `LINK,...` line. Uses
 * DOOM_LINK_PROBE_SEED / DOOM_LINK_PROBE_ITERATIONS.
 *
 * @return DOOM_LINK_STATUS_OK when every check matched, DOOM_LINK_STATUS_FAIL
 *         otherwise. The printed line carries the same verdict.
 */
int doom_link_probe_run(void);

/* --- Shared constants ---------------------------------------------------- */

/* Applied by doom_bridge_mix() and expected by the C side: 0x9E3779B9, the
 * 32-bit golden-ratio constant. An odd, high-entropy salt, so a boundary that
 * loses or duplicates a bit cannot land on the expected value by accident. */
#define DOOM_LINK_SALT 0x9E3779B9u

/* The probe's verdict, as a number. The printed status word is derived from
 * this: only DOOM_LINK_STATUS_OK prints `ok`. */
#define DOOM_LINK_STATUS_FAIL 0
#define DOOM_LINK_STATUS_OK 1

/* The parameters the shipped probe runs with: seed "PR32" and eight passes over
 * the table, so 256 bytes of table are folded into one 32-bit word. */
#define DOOM_LINK_PROBE_SEED 0x50523332u
#define DOOM_LINK_PROBE_ITERATIONS 8u

/* The two palette entries the probe asks for and the wire bytes the port's
 * palette contract pins them to (see src/palette/DoomPalette.cpp). */
#define DOOM_LINK_PALETTE_BLACK_INDEX 0u
#define DOOM_LINK_PALETTE_WHITE_INDEX 15u
#define DOOM_LINK_PALETTE_BLACK_BYTE 0x00u
#define DOOM_LINK_PALETTE_WHITE_BYTE 0xFFu

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_LINK_PROBE_H */
