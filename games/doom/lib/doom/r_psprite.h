/*
 * DOOM on PixelRoot32 - the player's weapon (D5.1).
 *
 * Derived from linuxdoom-1.10 r_things.c (R_DrawPSprite, R_DrawPlayerSprites)
 * (Copyright (C) 1993-1996 by id Software, Inc.), licensed under the GNU General
 * Public License v2.
 */
#ifndef DOOM_R_PSPRITE_H
#define DOOM_R_PSPRITE_H

#include <stdint.h>

#include "p_user.h"
#include "r_light.h"
#include "r_things.h"
#include "st_stuff.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * THE WEAPON IN VIEW
 *
 * The simulation already owns the psprites (p_pspr.c: the weapon and flash
 * layers, their state, sx and sy). This is the part that was missing: turning
 * each active layer into the patch it shows and drawing it, which is
 * R_DrawPlayerSprites(). The patch is named the way R_InitSpriteDefs does,
 * `sprnames[state->sprite]` + `'A' + (frame & FF_FRAMEMASK)` + `'0'`, and is
 * looked up BY NAME through the same callback type the status bar uses: the
 * container keeps the weapon art raw (tools/weapon_lumps.txt), readable in
 * place, not in the bake manifest.
 *
 * NOT IMPLEMENTED (and why): the spectre fuzz of an invisible player
 * (`powers[pw_invisibility]`) and `fixedcolormap` (the invulnerability
 * inverse-colour map) -- neither exists anywhere else in this renderer, and no
 * pickup in episode 1's reachable set is required to play it.
 */

/**
 * @brief R_DrawPlayerSprites(): draws the player's active psprite layers.
 *
 * @param state        The view state r_masked_emit() used (only its optional
 *                     probe is read).
 * @param surface      The render surface; rows below R_VIEW_HEIGHT are never
 *                     touched.
 * @param light        The COLORMAP.
 * @param player       The player whose `psprites[]` to draw.
 * @param sectorLight  The light level of the sector the player's mobj is in.
 * @param extralight   The view's extra light (added to the sector row).
 * @param lump         How a patch name becomes bytes.
 * @param context      Handed to @p lump.
 * @param written      Optional: receives (adds) the pixels drawn.
 * @return the number of active layers that could not be drawn (patch missing
 *         or malformed); 0 when the weapon is complete or no layer is active.
 */
int r_psprites_draw(r_view_state_t* state, r_draw_surface_t* surface,
                    const r_light_t* light, const p_player_t* player, int sectorLight,
                    int extralight, st_lump_fn lump, void* context, uint32_t* written);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_R_PSPRITE_H */
