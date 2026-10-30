/*
 * The C side of the DOOM port's C <-> C++ seam.
 *
 * This file is compiled by the C compiler (`-x c`, `.c` -> `.c.o`) with the
 * firmware's own flags, which is the property the DOOM core depends on: the
 * 1993 sources are C, and they must link against an engine that is C++. It is
 * written to stay portable C99 -- no C++ construct, no compound-literal
 * initialiser, no variable-length array, no allocation, no float -- and it calls
 * the C++ side only through the declarations in doom_link_probe.h.
 *
 * The probe, in order (the whole call chain of D1.5):
 *
 *   1. this file hashes a 32-byte table with FNV-1a and keeps the answer;
 *   2. doom_bridge_mix() applies the shared salt in C++ and returns it to C;
 *   3. doom_bridge_palette_byte() reaches doom::palette::indexTable(), a symbol
 *      this port owns, and returns two palette bytes;
 *   4. this file compares all three results with what it computed itself and
 *      asks doom_bridge_emit_line() for the one LINK line, which the C++ side
 *      writes through the engine's platformPrint().
 *
 * A single deterministic integer therefore crosses the boundary in both
 * directions, and the value that comes back is verified against an expectation
 * this translation unit derived on its own.
 *
 * No licence header: this is a link probe written for this port, not DOOM
 * source.
 */
#include "doom_link_probe.h"

/*
 * The table, as 32 character constants rather than a string literal: the
 * contract in doom_link_probe.h calls for exactly 32 bytes, the printed text
 * `pixelroot32/doom-link-probe/v1` is 30 of them, and the last two are zero. A
 * string literal would hide the padding length, and the host test states the
 * same table as text, so the two spellings cross-check each other.
 */
static const unsigned char kLinkProbeTable[32] = {
    'p', 'i', 'x', 'e', 'l', 'r', 'o', 'o',
    't', '3', '2', '/', 'd', 'o', 'o', 'm',
    '-', 'l', 'i', 'n', 'k', '-', 'p', 'r',
    'o', 'b', 'e', '/', 'v', '1', 0, 0
};

/* One FNV-1a pass over the table, in 32-bit unsigned arithmetic that wraps. */
static uint32_t hashTableOnce(uint32_t hash) {
    unsigned int index;

    for (index = 0; index < 32u; ++index) {
        hash = hash ^ (uint32_t)kLinkProbeTable[index];
        hash = hash * 16777619u; /* FNV-1a prime; unsigned, so it wraps */
    }

    return hash;
}

uint32_t doom_link_probe_checksum(uint32_t seed, uint32_t iterations) {
    /* 2166136261 is the FNV-1a 32-bit offset basis; the seed is folded in
     * before the first pass so two seeds never start from the same state. */
    uint32_t hash = 2166136261u ^ seed;
    uint32_t pass;

    for (pass = 0; pass < iterations; ++pass) {
        hash = hashTableOnce(hash);
    }

    return hash;
}

int doom_link_probe_run(void) {
    const uint32_t computed =
        doom_link_probe_checksum(DOOM_LINK_PROBE_SEED, DOOM_LINK_PROBE_ITERATIONS);

    /* C -> C++ and back. The salt is invertible, so any bit the boundary lost
     * in either direction shows up in this comparison. */
    const uint32_t returned = doom_bridge_mix(computed);

    /* C -> C++ -> a port translation unit this file knows nothing about. The
     * two wire bytes are the palette contract pinned in DoomPalette.cpp. */
    const uint32_t black = doom_bridge_palette_byte(DOOM_LINK_PALETTE_BLACK_INDEX);
    const uint32_t white = doom_bridge_palette_byte(DOOM_LINK_PALETTE_WHITE_INDEX);

    int status = DOOM_LINK_STATUS_OK;

    if (returned != (computed ^ DOOM_LINK_SALT)) {
        status = DOOM_LINK_STATUS_FAIL;
    }
    if (black != DOOM_LINK_PALETTE_BLACK_BYTE) {
        status = DOOM_LINK_STATUS_FAIL;
    }
    if (white != DOOM_LINK_PALETTE_WHITE_BYTE) {
        status = DOOM_LINK_STATUS_FAIL;
    }

    /* One line per run, from the C++ side, through the engine console. */
    doom_bridge_emit_line((uint32_t)status, computed);

    return status;
}
