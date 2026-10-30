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

/*
 * THE VIEW WINDOW INSIDE THE SURFACE, AND THE ONE PLACE A HUD WOULD MOVE IT
 *
 * DOOM's R_ExecuteSetViewSize() derives two independent numbers from the
 * window it renders into: `viewheight` (how many rows the renderer may write,
 * which is what floorclip[] is initialised to) and `centery` (the horizon, the
 * row R_RenderSegLoop's 12.4 fractional screen positions are measured from).
 * Upstream they are `viewheight` and `viewheight/2`; the status bar steals rows
 * by shrinking the window, so both change together.
 *
 * This port draws a 200-row view with no status bar yet, so R_VIEW_HEIGHT is
 * the whole surface. A Phase-5 HUD that steals rows changes R_CENTER_Y (and
 * R_VIEW_HEIGHT, which is what a clip array starts at); nothing else in the
 * renderer hard-codes either number. r_bsp.c and r_segs.c read these macros and
 * never a literal 100 or 200.
 */
#define R_VIEW_HEIGHT R_SCREEN_HEIGHT
#define R_CENTER_Y (R_VIEW_HEIGHT / 2)
#define R_CENTER_Y_FRAC (((fixed_t)R_CENTER_Y) << FRACBITS)

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

/*
 * The drawing classes the wall pass counts in r_draw_surface_t::tierPixels, in
 * the order R_RenderSegLoop tries them: the single-sided mid texture, the upper
 * tier of a two-sided line, then the lower one.
 */
#define R_DRAW_TIER_MID 0
#define R_DRAW_TIER_TOP 1
#define R_DRAW_TIER_BOTTOM 2
#define R_DRAW_TIER_COUNT 3

/* The texture-column wrap of a wall texture, as R_InitTextures() computes it:
 * `j = 1; while (j*2 <= width) j <<= 1; texturewidthmask = j - 1;` -- the
 * largest power of two that does not exceed the width, minus one. It is not
 * `width - 1` for a width that is not a power of two (a 72-wide texture wraps at
 * 63), which is why it is a function and not a caller's assumption. */
static inline int16_t r_texture_width_mask(int width) {
    int16_t mask = 1;

    if (width < 1) {
        return 0;
    }
    while ((int)mask * 2 <= width) {
        mask = (int16_t)(mask * 2);
    }
    return (int16_t)(mask - 1);
}


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

/*
 * One palette-index surface: R_SCREEN_WIDTH x R_VIEW_HEIGHT bytes, row major,
 * pixel (x, y) at pixels[x + y * width]. `width` and `height` are carried
 * rather than assumed so the column emitter can prove its writes are in bounds
 * with one comparison instead of trusting the caller.
 *
 * `covered` is the coverage record the frame hash needs: one byte per column,
 * set to 1 when the pass wrote at least one pixel there. It is optional (NULL
 * means "do not record"), and it exists because an uncovered column is filled
 * with 0xFF -- a real palette index -- so the pixel buffer alone cannot say
 * whether a column was written. A pass that writes nothing leaves it alone.
 *
 * `tierPixels` is the same record per drawing class (the wall pass writes three:
 * mid, upper and lower). It is what makes "the same pixels" and "the same tiers
 * in the same order" two separately checkable claims in the frame protocol; the
 * sum of the three is the number of pixels the pass wrote.
 *
 * THE PLANE PASS'S OWN RECORD
 *
 * The plane pass (D3.3) writes to the same surface after the wall pass, so the
 * coverage mask alone can no longer say which pass produced a pixel. Three
 * optional fields make that answerable without a second surface:
 *
 *   planeMask    one byte per pixel, row major, 1 where the plane pass wrote
 *                and 0 elsewhere. NULL means "do not record". It is the only
 *                per-pixel attribution the frame protocol needs: a pixel with
 *                planeMask == 1 was written by the plane pass (ON TOP of
 *                whatever the wall pass left there), and a pixel with
 *                planeMask == 0 and covered[x] == 1 was written by the wall
 *                pass. The plane pass clears it before writing, so it is a
 *                complete record of one call.
 *   planePixels  the number of pixels the plane pass wrote (the number of 1s in
 *                planeMask when it is recorded).
 *   planeSpans   the number of flat spans the plane pass emitted, i.e. the
 *                number of R_MapPlane() calls. A sky column is not a span: it
 *                is one R_DrawColumn() call per column in upstream's sky
 *                branch, and it is counted only in planePixels.
 *
 * The wall pass ignores all three; r_wall_emit() neither clears nor reads them.
 */
typedef struct r_draw_surface {
    uint8_t* pixels;
    int32_t width;
    int32_t height;
    uint8_t* covered;
    uint32_t tierPixels[R_DRAW_TIER_COUNT];
    uint8_t* planeMask;
    uint32_t planePixels;
    uint32_t planeSpans;
} r_draw_surface_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_R_DEFS_H */
