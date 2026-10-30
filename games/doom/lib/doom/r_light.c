/*
 * The COLORMAP lump of the DOOM port (r_light.h), from upstream's
 * R_InitColormaps() in r_data.c.
 *
 * R_InitColormaps() reads the lump and 256-aligns the allocation so a table
 * pointer can be truncated into the renderer's short-lived `lighttable_t*`
 * cache. This port keeps the lump as one const byte buffer the caller owns --
 * on the board a mapped flash region, in a test an array -- so there is nothing
 * to align and nothing to free, and the "one table per column" lookup is an
 * offset into those bytes.
 *
 * The light *level* table (upstream's scalelight[,], built by
 * R_ExecuteSetViewSize()) is not here: it depends only on SCREENWIDTH and the
 * view size, both fixed in this port, so it is generated into r_tables.c with
 * the projection tables (tools/gen_r_tables.py) and read through
 * r_light_level. This file therefore holds validation and no state beyond the
 * pointer the caller gave it.
 *
 * This is C99 only: no allocation, no globals, no stdio, no floating point.
 */
#include "r_light.h"

r_light_error_t r_light_init(r_light_t* light, const uint8_t* data, size_t size) {
    if (light == NULL) {
        return R_LIGHT_ERR_NULL_STATE;
    }
    light->colormaps = NULL;

    if (data == NULL) {
        return R_LIGHT_ERR_NULL_DATA;
    }
    if (size == 0u || (size % (size_t)R_LIGHT_MAP_BYTES) != 0u) {
        return R_LIGHT_ERR_LUMP_SIZE;
    }
    if (size / (size_t)R_LIGHT_MAP_BYTES != (size_t)R_LIGHT_LUMP_MAPS) {
        return R_LIGHT_ERR_MAP_COUNT;
    }

    light->colormaps = data;
    return R_LIGHT_OK;
}
