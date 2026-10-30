/*
 * The COLORMAP lump, and the one lookup a wall column needs from it.
 *
 * DOOM does not light a wall pixel by pixel. It keeps COLORMAP -- 34 tables of
 * 256 bytes each -- where table n maps palette index i to the colour i becomes
 * after n steps of darkening, and a drawing routine picks *one* table per wall
 * column with a single pointer assignment. The pixel loop then costs one extra
 * table read, and every lighting question is answered by "which table".
 *
 * THE TWO-DIMENSIONAL SELECTION, AND WHY THIS FILE HAS TWO FUNCTIONS
 *
 * A wall's table is chosen twice, from two different numbers:
 *
 *   the row    by light level. R_StoreWallRange() (r_segs.c) computes
 *              `lightnum = (frontsector->lightlevel >> LIGHTSEGSHIFT) + extralight`,
 *              then `-1` for a horizontal seg and `+1` for a vertical one, and
 *              clamps it:
 *                  lightnum < 0            -> scalelight[0]
 *                  lightnum >= LIGHTLEVELS -> scalelight[LIGHTLEVELS-1]
 *   the column by texture scale, once per column. R_RenderSegLoop() computes
 *              `index = rw_scale >> LIGHTSCALESHIFT`, clamps it to
 *              MAXLIGHTSCALE-1, and reads `walllights[index]`.
 *
 * `scalelight[i][j]` itself is R_ExecuteSetViewSize()'s table:
 *
 *     startmap = ((LIGHTLEVELS-1-i)*2) * NUMCOLORMAPS / LIGHTLEVELS
 *     level    = startmap - j*SCREENWIDTH/(viewwidth<<detailshift)/DISTMAP
 *     level    = clamp(level, 0, NUMCOLORMAPS-1)
 *
 * which this port emits as a constant (tools/gen_r_tables.py, declared in
 * r_tables.h as r_light_level) because both SCREENWIDTH and the view width are
 * fixed here. So the whole selection is an array read: no per-frame table is
 * built and no state is carried between frames.
 *
 * The two-dimensional shape is why there are two functions rather than one.
 * r_light_wall() is the exact lookup -- row from the light number, column from
 * the scale -- and is what r_segs.c calls once per column. r_light_wall_level()
 * is the row's first map, which is *not* the same thing as "the map for this
 * light level" and is provided only for a caller that has no scale to give
 * (a 1993-era "pick the level's map" test). A single-argument lookup cannot be
 * exact: the row alone does not name a table.
 *
 * WHAT IS VALIDATED, AND WHY
 *
 * The lump is 34 tables of 256 bytes: 32 the light tables reach, plus the two
 * full-bright/inverse tables DOOM keeps after them. Only the first
 * NUMCOLORMAPS are reachable from r_light_level, and the initialiser insists on
 * the whole documented layout anyway, because a lump of the wrong length is a
 * damaged WAD and reporting it here is cheaper than reading past a table later.
 *
 * This is C99 only: <stddef.h> and <stdint.h>, no allocation, no globals (the
 * one table it reads is const), no stdio, no floating point.
 */
#ifndef DOOM_R_LIGHT_H
#define DOOM_R_LIGHT_H

#include <stddef.h>
#include <stdint.h>

#include "m_fixed.h"
#include "r_tables.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Constants, from upstream's r_data.h --------------------------------- */

/* LIGHTSEGSHIFT: sector light levels 0..255 become 16 bands. This shift is
 * applied by the *walk* (r_wall_fragment_open), which stores the result in the
 * drawseg; the emitter only clamps it. */
#define R_LIGHT_SEG_SHIFT 4

/* LIGHTSCALESHIFT: the texture scale's high bits index the row. */
#define R_LIGHT_SCALE_SHIFT 12

/* One COLORMAP table, in bytes. */
#define R_LIGHT_MAP_BYTES 256

/* How many tables the lump carries: NUMCOLORMAPS + the two extra maps DOOM
 * keeps after the light levels. */
#define R_LIGHT_LUMP_MAPS 34

/* --- Error codes --------------------------------------------------------- */

/*
 *   R_LIGHT_OK              the lump was accepted
 *   R_LIGHT_ERR_NULL_STATE  no state struct
 *   R_LIGHT_ERR_NULL_DATA   no lump
 *   R_LIGHT_ERR_LUMP_SIZE   size is 0 or not a whole number of tables
 *   R_LIGHT_ERR_MAP_COUNT   the lump is not R_LIGHT_LUMP_MAPS tables -- a
 *                           damaged COLORMAP, not a short one
 */
typedef enum r_light_error {
    R_LIGHT_OK = 0,
    R_LIGHT_ERR_NULL_STATE = 1,
    R_LIGHT_ERR_NULL_DATA = 2,
    R_LIGHT_ERR_LUMP_SIZE = 3,
    R_LIGHT_ERR_MAP_COUNT = 4
} r_light_error_t;

/* The validated lump. `colormaps` points at the caller's bytes (a mapped flash
 * region, a decoded lump, a host array) and is NULL until a successful init. */
typedef struct r_light {
    const uint8_t* colormaps;
} r_light_t;

/* --- Public API ---------------------------------------------------------- */

/**
 * @brief Validates a COLORMAP lump and binds it.
 *
 * Validation order:
 *
 *   1. light == NULL                        R_LIGHT_ERR_NULL_STATE
 *   2. data == NULL                         R_LIGHT_ERR_NULL_DATA
 *   3. size == 0 or size % 256 != 0         R_LIGHT_ERR_LUMP_SIZE
 *   4. size / 256 != R_LIGHT_LUMP_MAPS      R_LIGHT_ERR_MAP_COUNT
 *
 * A rejected call leaves `light->colormaps` NULL, so a caller that ignores the
 * result still cannot read through it: every lookup below answers NULL.
 *
 * @param light Caller-owned state; rewritten on every call.
 * @param data  COLORMAP bytes. Must outlive every use of @p light.
 * @param size  Bytes available at @p data.
 * @return R_LIGHT_OK or one of the codes above.
 */
r_light_error_t r_light_init(r_light_t* light, const uint8_t* data, size_t size);

/**
 * @brief The scalelight row upstream's wall path selects, clamped.
 *
 * `lightnum < 0` answers 0 and `lightnum >= R_LIGHT_LEVELS` answers
 * R_LIGHT_LEVELS-1, exactly as R_StoreWallRange()'s three-way branch does.
 */
static inline int r_light_row(int lightnum) {
    if (lightnum < 0) {
        return 0;
    }
    if (lightnum >= R_LIGHT_LEVELS) {
        return R_LIGHT_LEVELS - 1;
    }
    return lightnum;
}

/**
 * @brief The scalelight column for one column's texture scale, clamped.
 *
 * R_RenderSegLoop()'s `index = rw_scale>>LIGHTSCALESHIFT; if (index >=
 * MAXLIGHTSCALE) index = MAXLIGHTSCALE-1;`. The cast to unsigned is upstream's
 * too: `index` is declared `unsigned` there, so a scale that is somehow negative
 * clamps to the last column instead of reading before the row.
 */
static inline int r_light_column(fixed_t scale) {
    const unsigned int index = ((unsigned int)scale) >> R_LIGHT_SCALE_SHIFT;

    return index >= (unsigned int)R_LIGHT_SCALE_LEVELS ? (R_LIGHT_SCALE_LEVELS - 1)
                                                       : (int)index;
}

/**
 * @brief One 256-byte map: the table a wall column with this light and scale
 *        draws through.
 *
 * @param light    A successfully initialised state.
 * @param lightnum The drawseg's lightNum (already includes the gun flash and the
 *                 seg-axis correction), not clamped by the caller.
 * @param scale    The column's texture scale (rw_scale).
 * @return A pointer into the COLORMAP lump, or NULL when @p light holds no
 *         validated lump.
 */
static inline const uint8_t* r_light_wall(const r_light_t* light, int lightnum,
                                          fixed_t scale) {
    if (light == NULL || light->colormaps == NULL) {
        return NULL;
    }
    return light->colormaps
           + (size_t)r_light_level[r_light_row(lightnum)][r_light_column(scale)]
                 * (size_t)R_LIGHT_MAP_BYTES;
}

/**
 * @brief The row's first map: the table for a column whose scale index is 0.
 *
 * Kept as the "level only" form of the lookup. It is the brightest map of the
 * row, which is what a caller with no scale means, but it is *not* what a wall
 * column draws through unless that column's scale is below 4096.
 */
static inline const uint8_t* r_light_wall_level(const r_light_t* light,
                                                int lightnum) {
    return r_light_wall(light, lightnum, 0);
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_R_LIGHT_H */
