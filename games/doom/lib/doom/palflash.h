/*
 * DOOM on PixelRoot32 - the palette flashes.
 *
 * Derived from linuxdoom-1.10 st_stuff.c ST_doPaletteStuff() (Copyright (C)
 * 1993-1996 by id Software, Inc.), licensed under the GNU General Public
 * License v2.
 */
#ifndef DOOM_PALFLASH_H
#define DOOM_PALFLASH_H

#include <stdint.h>

#include "p_user.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * WHICH PLAYPAL PAGE THE SCREEN SHOWS
 *
 * PLAYPAL holds 14 pages of 256 RGB triples: page 0 is the normal picture,
 * pages 1-8 are the red damage tints, 9-12 the gold pickup tints and 13 the
 * green radiation-suit tint. ST_doPaletteStuff() picks one page per frame from
 * the counters the simulation already owns and hands it to I_SetPalette() only
 * when it differs from the page already on the screen.
 */
#define PALFLASH_STARTREDPALS 1
#define PALFLASH_NUMREDPALS 8
#define PALFLASH_STARTBONUSPALS 9
#define PALFLASH_NUMBONUSPALS 4
#define PALFLASH_RADIATIONPAL 13
#define PALFLASH_NUMPALS 14

/**
 * @brief ST_doPaletteStuff()'s choice of page, without the I_SetPalette call.
 *
 * @code
 *   cnt = damagecount;
 *   if (powers[pw_strength]) { bzc = 12 - (powers[pw_strength] >> 6);
 *                              if (bzc > cnt) cnt = bzc; }
 *   if (cnt)            page = STARTREDPALS + min((cnt + 7) >> 3, NUMREDPALS - 1);
 *   else if (bonuscount) page = STARTBONUSPALS + min((bonuscount + 7) >> 3,
 *                                                   NUMBONUSPALS - 1);
 *   else if (powers[pw_ironfeet] > 4 * 32 || (powers[pw_ironfeet] & 8))
 *                        page = RADIATIONPAL;
 *   else                 page = 0;
 * @endcode
 *
 * Berserk is the strength POWER (it counts up, so the red tint fades over about
 * 770 tics), not a test of the wielded weapon: upstream never looks at the
 * weapon. Reads the player, writes nothing. A NULL player is page 0.
 *
 * @return 0..PALFLASH_NUMPALS-1.
 */
int palflash_index(const p_player_t* player);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_PALFLASH_H */
