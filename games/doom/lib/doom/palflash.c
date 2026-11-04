/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */
#include "palflash.h"

int palflash_index(const p_player_t* player) {
    int32_t cnt;
    int32_t strength;
    int32_t palette;

    if (player == NULL) {
        return 0;
    }

    cnt = player->damagecount;
    strength = player->powers[P_PW_STRENGTH];
    if (strength != 0) {
        /* "slowly fade the berserk out" */
        const int32_t bzc = 12 - (strength >> 6);

        if (bzc > cnt) {
            cnt = bzc;
        }
    }

    if (cnt != 0) {
        palette = (cnt + 7) >> 3;
        if (palette >= PALFLASH_NUMREDPALS) {
            palette = PALFLASH_NUMREDPALS - 1;
        }
        return (int)(palette + PALFLASH_STARTREDPALS);
    }
    if (player->bonuscount != 0) {
        palette = (player->bonuscount + 7) >> 3;
        if (palette >= PALFLASH_NUMBONUSPALS) {
            palette = PALFLASH_NUMBONUSPALS - 1;
        }
        return (int)(palette + PALFLASH_STARTBONUSPALS);
    }
    if (player->powers[P_PW_IRONFEET] > 4 * 32 || (player->powers[P_PW_IRONFEET] & 8) != 0) {
        return PALFLASH_RADIATIONPAL;
    }
    return 0;
}
