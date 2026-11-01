/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996 id
 * Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * R_DrawPlayerSprites() (r_things.c): the glue between the simulation's psprite
 * layers and the column drawer in r_things.c. See r_psprite.h.
 *
 *   - upstream reads `sprites[state->sprite].spriteframes[frame]` for the lump;
 *     a weapon frame has rotate false, so that is `lump[0]`, named
 *     `<sprite><letter>0`, and never mirrored;
 *   - FF_FULLBRIGHT lives in the state's frame word, as for any thing;
 *   - a layer whose patch cannot be had is counted and skipped, never half drawn.
 *
 * C99 only: no allocation, no globals, no stdio.
 */
#include "r_psprite.h"

#include <string.h>

#include "info_tables.h"

#define R_PSPRITE_FF_FRAMEMASK 0x7FFF
#define R_PSPRITE_FF_FULLBRIGHT 0x8000

int r_psprites_draw(r_view_state_t* state, r_draw_surface_t* surface,
                    const r_light_t* light, const p_player_t* player, int sectorLight,
                    int extralight, st_lump_fn lump, void* context, uint32_t* written) {
    r_psprite_layer_t layers[R_PSPRITE_LAYERS];
    uint16_t count = 0u;
    int missing = 0;
    int index;

    if (player == NULL || lump == NULL) {
        return 0;
    }
    for (index = 0; index < R_PSPRITE_LAYERS && index < P_NUMPSPRITES; ++index) {
        const p_pspdef_t* psp = &player->psprites[index];
        const doom_state_t* st;
        char name[9];
        size_t size = 0u;
        const uint8_t* data;
        int frame;

        if (psp->state <= 0 || psp->state >= NUM_STATES) {
            continue; /* S_NULL: the layer is inactive */
        }
        st = &states[psp->state];
        if (st->sprite < 0 || st->sprite >= NUM_SPRITES) {
            ++missing;
            continue;
        }
        frame = st->frame & R_PSPRITE_FF_FRAMEMASK;
        if (frame < 0 || frame > 25) {
            ++missing;
            continue;
        }
        memcpy(name, sprnames[st->sprite], 4);
        name[4] = (char)('A' + frame);
        name[5] = '0';
        name[6] = '\0';
        data = lump(context, name, &size);
        if (data == NULL) {
            ++missing;
            continue;
        }
        layers[count].patch.data = data;
        layers[count].patch.size = size;
        layers[count].sx = psp->sx;
        layers[count].sy = psp->sy;
        layers[count].fullbright = (st->frame & R_PSPRITE_FF_FULLBRIGHT) != 0 ? 1u : 0u;
        ++count;
    }
    if (count == 0u) {
        return missing;
    }
    if (r_psprites_emit(state, surface, light,
                        (sectorLight >> R_LIGHT_SEG_SHIFT) + extralight, layers, count,
                        written)
        != R_THINGS_OK) {
        missing += (int)count;
    }
    return missing;
}
