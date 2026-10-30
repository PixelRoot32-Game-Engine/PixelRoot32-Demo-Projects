/*
 * Renderer-wide constants for the DOOM port.
 *
 * These are the numbers DOOM hard-codes across r_defs.h, r_main.c, r_bsp.c and
 * r_plane.c. They are shared by the fixed-point layer, the lookup tables, the
 * BSP walk and the visplane set, so they live in one header instead of being
 * repeated (and eventually disagreed with).
 *
 * WHY 320 x 200 IS NOT A KNOB
 *
 * Every one of these numbers is structural. `visplane_t` is two 320-byte
 * column arrays, `floorclip[]` and `ceilingclip[]` are one short per screen
 * column, and the projection tables are built for a 320-wide window. Rendering
 * narrower is a renderer change plus a HUD relayout, not a build flag
 * (docs/plan-doom-esp32.md 6.1). The panel is a presentation problem and never
 * reaches this layer.
 *
 * This is C99 only: <stdint.h>, no allocation, no globals, no floating point.
 */
#ifndef DOOM_R_DEFS_H
#define DOOM_R_DEFS_H

#include <stdint.h>

#include "m_fixed.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The render surface, in pixels. Not the panel: see the header comment. */
#define R_SCREEN_WIDTH 320
#define R_SCREEN_HEIGHT 200

/* DOOM's field of view, in fine angles. 2048 of 8192 is 90 degrees, which is
 * the horizontal arc the view-angle-to-column table flattens onto 320 px. */
#define R_FIELDOFVIEW 2048

/* One drawseg per visible wall fragment. DOOM's MAXDRAWSEGS. When the array is
 * full upstream stops storing fragments; this port does the same and records
 * that it happened instead of corrupting the list. */
#define R_MAX_DRAWSEGS 256

/* One solid clip post: a run of screen columns already covered by a wall.
 * DOOM's MAXSEGS. The first two entries are the permanent sentinels, so this
 * is the number of *interior* posts the walk can hold, and a storage block
 * smaller than this is rejected rather than risking an overflow. */
#define R_MAX_CLIP_SEGS 32

/*
 * Silhouette flags of a drawseg: which edge of the wall a sprite behind it may
 * be clipped against. DOOM's SIL_NONE / SIL_BOTTOM / SIL_TOP / SIL_BOTH.
 */
#define R_SIL_NONE 0
#define R_SIL_BOTTOM 1
#define R_SIL_TOP 2
#define R_SIL_BOTH (R_SIL_BOTTOM | R_SIL_TOP)

/* A BSP child index with this bit set addresses a subsector. DOOM's
 * NF_SUBSECTOR, and the reason a map with zero nodes needs no special case
 * beyond passing -1 as the root. */
#define R_NF_SUBSECTOR 0x8000u

/* A packed texture/flat name is the eight name bytes in file order, most
 * significant byte first. DOOM compares translated integer indices; this port
 * has no global texture array at this layer, so the name itself is the key and
 * comparing two packed values is one 64-bit compare. */
typedef uint64_t r_name_t;

/* The packed name of a texture or flat the map does not have. DOOM's R_*NumForName
 * answers 0 for "-", and "-" maps to the first directory entry for a real
 * texture; at this layer a name whose first byte is '-' or NUL is "absent". */
#define R_NAME_NONE ((r_name_t)0)

/**
 * @brief Packs the eight bytes of a WAD name field into one r_name_t.
 *
 * The bytes are copied verbatim (they are upper case in the IWAD) and not
 * trimmed, so the comparison is over the whole field, exactly as DOOM's name8
 * comparison is.
 */
static inline r_name_t r_pack_name(const uint8_t* name) {
    r_name_t packed = 0;
    int i;

    for (i = 0; i < 8; ++i) {
        packed = (packed << 8) | (r_name_t)name[i];
    }
    return packed;
}

/**
 * @brief Unpacks an r_name_t into nine bytes: eight name bytes plus NUL.
 */
static inline void r_unpack_name(r_name_t packed, char out[9]) {
    int i;

    for (i = 7; i >= 0; --i) {
        out[i] = (char)(packed & 0xffu);
        packed >>= 8;
    }
    out[8] = '\0';
}

/**
 * @brief True when a packed name names a texture that exists.
 *
 * DOOM's R_TextureNumForName() maps a name whose first character is '-' to
 * texture 0, which is the "no texture" sentinel every rendering test uses. An
 * all-zero field is the same answer for a padded map record.
 */
static inline int r_has_name(r_name_t packed) {
    const uint8_t first = (uint8_t)(packed >> 56);

    return first != 0u && first != (uint8_t)'-';
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_R_DEFS_H */
