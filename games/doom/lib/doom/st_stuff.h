/*
 * DOOM on PixelRoot32 - the status bar.
 *
 * Derived from linuxdoom-1.10 st_stuff.c / st_lib.c (Copyright (C) 1993-1996 by
 * id Software, Inc.), licensed under the GNU General Public License v2.
 */
#ifndef DOOM_ST_STUFF_H
#define DOOM_ST_STUFF_H

#include <stddef.h>
#include <stdint.h>

#include "m_random.h"
#include "p_user.h"
#include "r_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * THE STATUS BAR, AS ST_Drawer() LEAVES THE SCREEN
 *
 * DOOM keeps the bar's background in an off-screen buffer and repaints only the
 * widgets that changed. This port has no off-screen buffer and does not need
 * the saving: st_draw() repaints the whole bar into the bottom ST_HEIGHT rows of
 * the 8bpp surface every frame, in the order ST_doRefresh() and ST_drawWidgets()
 * paint it with `refresh` true (the state upstream is in right after
 * ST_Start()):
 *
 *   1. STBAR at (0, ST_Y)                      ST_refreshBackground
 *   2. ready ammo (STTNUM, 3 digits)           STlib_updateNum(&w_ready)
 *   3. four ammo and four max-ammo (STYSNUM)   w_ammo[], w_maxammo[]
 *   4. health, armor (STTNUM + STTPRCNT)       STlib_updatePercent
 *   5. STARMS at (104, 168)                    w_armsbg
 *   6. six arms digits: STGNUM grey, STYSNUM yellow when owned   w_arms[]
 *   7. the face (STF*)                         w_faces
 *   8. three key boxes (STKEYS)                w_keyboxes[]
 *
 * Patches are looked up BY NAME through `st_lump_fn`, not through the sprite
 * resolver: the container keeps them raw and readable in place.
 *
 * THE FACE
 *
 * st_start() and st_ticker() are ST_Start()/ST_Ticker(): the face state machine
 * (ST_updateFaceWidget) advances once per world tic, from the player record the
 * simulation already owns. It draws its own random number with M_Random(), which
 * is a different index of the same table from P_Random(), so ticking the bar
 * never moves the simulation's sequence.
 *
 * NOT IMPLEMENTED (and why): no cheats exist (`CF_GODMODE`), so the god face
 * follows the invulnerability power alone; deathmatch frags and netgame face
 * backgrounds do not exist (one player); the damage and bonus PALETTE flashes
 * (ST_doPaletteStuff) are a whole-screen palette swap, not a bar widget.
 */

#define ST_HEIGHT 32
#define ST_WIDTH R_SCREEN_WIDTH
#define ST_Y (R_SCREEN_HEIGHT - ST_HEIGHT)

/* The face table: five pain levels of eight faces, then GOD and DEAD. */
#define ST_NUMPAINFACES 5
#define ST_FACESTRIDE 8
#define ST_GODFACE (ST_NUMPAINFACES * ST_FACESTRIDE)
#define ST_DEADFACE (ST_GODFACE + 1)

/**
 * @brief Looks up one patch lump by name.
 *
 * @param context  The caller's pointer, handed back unchanged.
 * @param name     Upper-case lump name, NUL terminated, at most 8 characters.
 * @param size     Receives the lump's byte length.
 * @return The lump bytes, valid for the duration of the draw call, or NULL.
 */
typedef const uint8_t* (*st_lump_fn)(void* context, const char* name, size_t* size);

/**
 * The bar's own state: ST_updateFaceWidget's statics, ST_calcPainOffset's two
 * and M_Random's index. About 60 bytes; nothing else in the bar persists.
 */
typedef struct st_state {
    m_random_t random;
    int32_t randomNumber;      /* st_randomnumber */
    int32_t faceIndex;         /* st_faceindex */
    int32_t faceCount;         /* st_facecount */
    int32_t oldHealth;         /* st_oldhealth */
    int32_t priority;          /* ST_updateFaceWidget's static `priority` */
    int32_t lastAttackDown;    /* ... and its static `lastattackdown` */
    int32_t painOldHealth;     /* ST_calcPainOffset's static `oldhealth` */
    int32_t painLastCalc;      /* ... and `lastcalc` */
    uint8_t oldWeaponsOwned[P_NUMWEAPONS];
} st_state_t;

/**
 * @brief ST_Start()/ST_initData(): the state a fresh level begins with.
 *
 * Faceindex 0, facecount 0, oldhealth -1, priority 0, `lastattackdown` -1 and
 * the weapons the player owns right now. A NULL pointer is a no-op.
 */
void st_start(st_state_t* state, const p_player_t* player);

/**
 * @brief ST_Ticker(): one world tic of the bar.
 *
 * M_Random(), then ST_updateWidgets() (the face), then `st_oldhealth = health`.
 * Call it once per `p_level_tic()`, after it, as G_Ticker calls ST_Ticker after
 * P_Ticker. A NULL pointer is a no-op.
 */
void st_ticker(st_state_t* state, const p_player_t* player);

/**
 * @brief Repaints the status bar into the bottom ST_HEIGHT rows of @p surface.
 *
 * @param state      The bar's state, as st_ticker() left it.
 * @param player     The player whose numbers, weapons, keys and face to draw.
 * @param lump       How a patch name becomes bytes.
 * @param context    Handed to @p lump.
 * @param surface    8bpp palette indices, at least R_SCREEN_WIDTH wide and
 *                   R_SCREEN_HEIGHT rows.
 * @param stride     Row stride of @p surface, in bytes.
 * @param withFaces  Draw the face widget. Zero leaves STBAR's own empty face
 *                   box, which is the DOOM_HUD_NOFACES fall-back profile
 *                   (Option B).
 * @return the number of patches that could not be drawn (missing or malformed);
 *         0 on a complete bar. A bad patch is skipped, never half-drawn.
 */
int st_draw(const st_state_t* state, const p_player_t* player, st_lump_fn lump,
            void* context, uint8_t* surface, int stride, int withFaces);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_ST_STUFF_H */
