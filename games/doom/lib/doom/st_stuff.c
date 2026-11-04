/*
 * DOOM on PixelRoot32 - the status bar.
 *
 * Derived from linuxdoom-1.10 st_stuff.c, st_lib.c and v_video.c (V_DrawPatch)
 * (Copyright (C) 1993-1996 by id Software, Inc.), licensed under the GNU General
 * Public License v2. See NOTICE.
 *
 * What changed from upstream, and why:
 *
 *   - No off-screen background and no incremental update. Upstream repaints a
 *     widget only when its value changed, copying the cleared area back from
 *     `screens[4]`. With one 8bpp surface and nothing to save, st_draw() paints
 *     STBAR and every widget each frame; the pixels are what upstream's
 *     `refresh == true` path leaves.
 *   - Patches come by name through a callback instead of W_CacheLumpName().
 *   - A patch that is missing or malformed is skipped and counted, never half
 *     drawn: the surface bound is checked per pixel, the lump bound per post.
 *   - M_Random() is m_random.c's p_random() on a private index, so the face
 *     never moves the simulation's sequence.
 */
#include "st_stuff.h"

#include <stdio.h>
#include <string.h>

#include "m_fixed.h"
#include "p_map.h"
#include "p_pspr.h"

/* st_stuff.c's layout. */
#define ST_AMMOX 44
#define ST_AMMOY 171
#define ST_HEALTHX 90
#define ST_HEALTHY 171
#define ST_ARMSX 111
#define ST_ARMSY 172
#define ST_ARMSBGX 104
#define ST_ARMSBGY 168
#define ST_ARMSXSPACE 12
#define ST_ARMSYSPACE 10
#define ST_ARMORX 221
#define ST_ARMORY 171
#define ST_FACESX 143
#define ST_FACESY 168
#define ST_KEYX 239
#define ST_AMMOCOLX 288
#define ST_MAXAMMOCOLX 314
#define ST_NUMBERWIDTH 3

/* ST_updateFaceWidget's timing and offsets. */
#define ST_TURNOFFSET 3
#define ST_OUCHOFFSET 5
#define ST_EVILGRINOFFSET 6
#define ST_RAMPAGEOFFSET 7
#define ST_STRAIGHTFACECOUNT 17
#define ST_TURNCOUNT 35
#define ST_RAMPAGEDELAY 70
#define ST_EVILGRINCOUNT 70
#define ST_MUCHPAIN 20

/* "No number": upstream's `largeammo`, what a weapon without ammo points at. */
#define ST_NONUMBER 1994

/* d_items.c's weaponinfo[].ammo is p_weapon_info(); the small-ammo rows run
 * clip, shell, cell, rocket top to bottom as st_stuff.c's ST_AMMO0Y..ST_AMMO3Y. */
static const int16_t kAmmoRowY[P_NUMAMMO] = {173, 179, 191, 185};
static const int16_t kKeyRowY[3] = {171, 181, 191};

/* --- v_video.c: V_DrawPatch ---------------------------------------------- */

static int16_t le16(const uint8_t* p) { return (int16_t)(p[0] | (p[1] << 8)); }

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
           | ((uint32_t)p[3] << 24);
}

/* Fetches @p name and checks its header and column table. 0 on failure. */
static int fetch(st_lump_fn lump, void* context, const char* name, const uint8_t** data,
                 size_t* size, int* width) {
    *data = lump(context, name, size);
    if (*data == NULL || *size < 8u) {
        return 0;
    }
    *width = le16(*data);
    if (*width < 1 || 8u + 4u * (size_t)*width > *size) {
        return 0;
    }
    return 1;
}

/* Draws one patch with its own offsets applied. Returns 0 when it is unusable. */
static int drawPatch(st_lump_fn lump, void* context, const char* name, int x, int y,
                     uint8_t* surface, int stride) {
    const uint8_t* data;
    size_t size;
    int width;
    int column;

    if (!fetch(lump, context, name, &data, &size, &width)) {
        return 0;
    }
    x -= le16(data + 4);
    y -= le16(data + 6);

    for (column = 0; column < width; ++column) {
        size_t offset = le32(data + 8 + 4 * column);
        const int screenX = x + column;

        while (offset + 1u < size && data[offset] != 0xFF) {
            const int topdelta = data[offset];
            const int length = data[offset + 1];
            int row;

            if (offset + 3u + (size_t)length > size) {
                return 0;
            }
            for (row = 0; row < length; ++row) {
                const int screenY = y + topdelta + row;
                if (screenX >= 0 && screenX < R_SCREEN_WIDTH && screenY >= 0
                    && screenY < R_SCREEN_HEIGHT) {
                    surface[screenY * stride + screenX] = data[offset + 3 + row];
                }
            }
            offset += (size_t)length + 4u;
        }
    }
    return 1;
}

/* --- st_lib.c: numbers, percents -------------------------------------------- */

/* STlib_drawNum(): @p digits is "STTNUM" or "STYSNUM"; @p x is the RIGHT edge. */
static int drawNumber(st_lump_fn lump, void* context, uint8_t* surface, int stride, int x,
                      int y, int width, int value, const char* digits) {
    char name[16];
    const uint8_t* data;
    size_t size;
    int glyph;
    int negative;
    int failed = 0;

    if (value == ST_NONUMBER) {
        return 0;
    }
    snprintf(name, sizeof name, "%s0", digits);
    if (!fetch(lump, context, name, &data, &size, &glyph)) {
        return 1;
    }

    negative = value < 0;
    if (negative) {
        if (width == 2 && value < -9) {
            value = -9;
        } else if (width == 3 && value < -99) {
            value = -99;
        }
        value = -value;
    }

    if (value == 0) {
        failed += !drawPatch(lump, context, name, x - glyph, y, surface, stride);
    }
    while (value != 0 && width-- > 0) {
        x -= glyph;
        snprintf(name, sizeof name, "%s%d", digits, value % 10);
        failed += !drawPatch(lump, context, name, x, y, surface, stride);
        value /= 10;
    }
    if (negative) {
        failed += !drawPatch(lump, context, "STTMINUS", x - 8, y, surface, stride);
    }
    return failed;
}

/* STlib_updatePercent(): the percent sign at @p x, then the number ending there. */
static int drawPercent(st_lump_fn lump, void* context, uint8_t* surface, int stride, int x,
                       int y, int value) {
    int failed = !drawPatch(lump, context, "STTPRCNT", x, y, surface, stride);
    failed += drawNumber(lump, context, surface, stride, x, y, ST_NUMBERWIDTH, value, "STTNUM");
    return failed;
}

/* --- st_stuff.c: the face ---------------------------------------------------- */

static int32_t painOffset(st_state_t* state, const p_player_t* player) {
    int32_t health = player->health > 100 ? 100 : player->health;

    if (health != state->painOldHealth) {
        state->painLastCalc = ST_FACESTRIDE * (((100 - health) * ST_NUMPAINFACES) / 101);
        state->painOldHealth = health;
    }
    return state->painLastCalc;
}

void st_start(st_state_t* state, const p_player_t* player) {
    int index;

    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof *state);
    m_random_init(&state->random);
    state->oldHealth = -1;
    state->lastAttackDown = -1;
    state->painOldHealth = -1;
    if (player != NULL) {
        for (index = 0; index < P_NUMWEAPONS; ++index) {
            state->oldWeaponsOwned[index] = player->weaponowned[index];
        }
    }
}

static void updateFaceWidget(st_state_t* state, const p_player_t* player) {
    int index;

    if (state->priority < 10 && player->health == 0) {
        state->priority = 9;
        state->faceIndex = ST_DEADFACE;
        state->faceCount = 1;
    }

    if (state->priority < 9 && player->bonuscount != 0) {
        int grin = 0;

        for (index = 0; index < P_NUMWEAPONS; ++index) {
            if (state->oldWeaponsOwned[index] != player->weaponowned[index]) {
                grin = 1;
                state->oldWeaponsOwned[index] = player->weaponowned[index];
            }
        }
        if (grin) {
            state->priority = 8;
            state->faceCount = ST_EVILGRINCOUNT;
            state->faceIndex = painOffset(state, player) + ST_EVILGRINOFFSET;
        }
    }

    if (state->priority < 8 && player->damagecount != 0 && player->attacker != NULL
        && player->attacker != player->mo) {
        state->priority = 7;

        if (player->health - state->oldHealth > ST_MUCHPAIN) {
            state->faceCount = ST_TURNCOUNT;
            state->faceIndex = painOffset(state, player) + ST_OUCHOFFSET;
        } else {
            const angle_t badguy = p_point_to_angle(
                (fixed_t)(player->attacker->x - player->mo->x),
                (fixed_t)(player->attacker->y - player->mo->y));
            angle_t diff;
            int right;

            if (badguy > player->mo->angle) {
                diff = badguy - player->mo->angle;
                right = diff > ANG180;
            } else {
                diff = player->mo->angle - badguy;
                right = diff <= ANG180;
            }

            state->faceCount = ST_TURNCOUNT;
            state->faceIndex = painOffset(state, player);
            if (diff < ANG45) {
                state->faceIndex += ST_RAMPAGEOFFSET;
            } else if (right) {
                state->faceIndex += ST_TURNOFFSET;
            } else {
                state->faceIndex += ST_TURNOFFSET + 1;
            }
        }
    }

    if (state->priority < 7 && player->damagecount != 0) {
        if (player->health - state->oldHealth > ST_MUCHPAIN) {
            state->priority = 7;
            state->faceCount = ST_TURNCOUNT;
            state->faceIndex = painOffset(state, player) + ST_OUCHOFFSET;
        } else {
            state->priority = 6;
            state->faceCount = ST_TURNCOUNT;
            state->faceIndex = painOffset(state, player) + ST_RAMPAGEOFFSET;
        }
    }

    if (state->priority < 6) {
        if (player->attackdown != 0) {
            if (state->lastAttackDown == -1) {
                state->lastAttackDown = ST_RAMPAGEDELAY;
            } else if (--state->lastAttackDown == 0) {
                state->priority = 5;
                state->faceIndex = painOffset(state, player) + ST_RAMPAGEOFFSET;
                state->faceCount = 1;
                state->lastAttackDown = 1;
            }
        } else {
            state->lastAttackDown = -1;
        }
    }

    if (state->priority < 5 && player->powers[P_PW_INVULNERABILITY] != 0) {
        state->priority = 4;
        state->faceIndex = ST_GODFACE;
        state->faceCount = 1;
    }

    if (state->faceCount == 0) {
        state->faceIndex = painOffset(state, player) + (state->randomNumber % 3);
        state->faceCount = ST_STRAIGHTFACECOUNT;
        state->priority = 0;
    }
    state->faceCount--;
}

void st_ticker(st_state_t* state, const p_player_t* player) {
    if (state == NULL || player == NULL || player->mo == NULL) {
        return;
    }
    state->randomNumber = p_random(&state->random);
    updateFaceWidget(state, player);
    state->oldHealth = player->health;
}

/* ST_loadGraphics()'s face list: 8 per pain level, then GOD and DEAD. */
static void faceName(int32_t index, char out[12]) {
    static const char* const kSpecial[5] = {"STFTR%d0", "STFTL%d0", "STFOUCH%d", "STFEVL%d",
                                            "STFKILL%d"};
    int32_t pain;
    int32_t within;

    if (index == ST_GODFACE) {
        strcpy(out, "STFGOD0");
        return;
    }
    if (index == ST_DEADFACE) {
        strcpy(out, "STFDEAD0");
        return;
    }
    pain = index / ST_FACESTRIDE;
    within = index % ST_FACESTRIDE;
    if (within < 3) {
        snprintf(out, 12, "STFST%d%d", (int)pain, (int)within);
    } else {
        snprintf(out, 12, kSpecial[within - 3], (int)pain);
    }
}

/* --- ST_Drawer ------------------------------------------------------------------ */

int st_draw(const st_state_t* state, const p_player_t* player, st_lump_fn lump,
            void* context, uint8_t* surface, int stride, int withFaces) {
    char name[16];
    int failed = 0;
    int index;

    if (state == NULL || player == NULL || lump == NULL || surface == NULL) {
        return -1;
    }

    /* The background, then the widgets in ST_drawWidgets' order. */
    failed += !drawPatch(lump, context, "STBAR", 0, ST_Y, surface, stride);

    {
        const p_weaponinfo_t* info = p_weapon_info(player->readyweapon);
        int ready = ST_NONUMBER;
        if (info != NULL && info->ammo >= 0 && info->ammo < P_NUMAMMO) {
            ready = player->ammo[info->ammo];
        }
        failed += drawNumber(lump, context, surface, stride, ST_AMMOX, ST_AMMOY,
                             ST_NUMBERWIDTH, ready, "STTNUM");
    }
    for (index = 0; index < P_NUMAMMO; ++index) {
        failed += drawNumber(lump, context, surface, stride, ST_AMMOCOLX, kAmmoRowY[index],
                             ST_NUMBERWIDTH, player->ammo[index], "STYSNUM");
        failed += drawNumber(lump, context, surface, stride, ST_MAXAMMOCOLX, kAmmoRowY[index],
                             ST_NUMBERWIDTH, player->maxammo[index], "STYSNUM");
    }
    failed += drawPercent(lump, context, surface, stride, ST_HEALTHX, ST_HEALTHY,
                          player->health);
    failed += drawPercent(lump, context, surface, stride, ST_ARMORX, ST_ARMORY,
                          player->armorpoints);

    failed += !drawPatch(lump, context, "STARMS", ST_ARMSBGX, ST_ARMSBGY, surface, stride);
    for (index = 0; index < 6; ++index) {
        snprintf(name, sizeof name, "%s%d",
                 player->weaponowned[index + 1] != 0 ? "STYSNUM" : "STGNUM", index + 2);
        failed += !drawPatch(lump, context, name, ST_ARMSX + (index % 3) * ST_ARMSXSPACE,
                             ST_ARMSY + (index / 3) * ST_ARMSYSPACE, surface, stride);
    }

    if (withFaces && state->faceIndex >= 0 && state->faceIndex <= ST_DEADFACE) {
        faceName(state->faceIndex, name);
        failed += !drawPatch(lump, context, name, ST_FACESX, ST_FACESY, surface, stride);
    }

    for (index = 0; index < 3; ++index) {
        int key = player->cards[index] != 0 ? index : -1;
        if (player->cards[index + 3] != 0) {
            key = index + 3;
        }
        if (key != -1) {
            snprintf(name, sizeof name, "STKEYS%d", key);
            failed += !drawPatch(lump, context, name, ST_KEYX, kKeyRowY[index], surface, stride);
        }
    }
    return failed;
}
