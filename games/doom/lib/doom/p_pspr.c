/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * The weapon sprite state machine and the weapon actions.
 *
 * The reference is linuxdoom-1.10 p_pspr.c and d_items.c's `weaponinfo[]`, with
 * every rule reproduced in upstream's statement order. Three structural
 * differences, all forced by the port's rules, and each stated at its site:
 *
 *   1. there are no file-scope globals, so upstream's `swingx`, `swingy`,
 *      `bulletslope` and `linetarget` are an out-parameter, a return value and an
 *      out-parameter respectively;
 *   2. `leveltime` is `world->level->leveltime`, which is what the two bob
 *      angles and the weapon's sway read;
 *   3. every `S_StartSound` is counted in p_player_t::silentSounds instead of
 *      played: there is no audio layer in this port (S2), and a counter is how
 *      the gap stays visible rather than becoming a missing statement.
 *
 * This is C99 only: no allocation, no globals, no stdio, no floating point.
 */
#include "p_pspr.h"

#include "m_random.h"
#include "p_action.h"
#include "p_map.h"
#include "p_mobj.h"
#include "p_setup.h"
#include "p_sight.h"
#include "p_user.h"
#include "r_tables.h"

/* --- The weapon table ---------------------------------------------------- */

/*
 * d_items.c's weaponinfo[NUMWEAPONS], one row per p_weapon_t, in upstream's
 * order. P_NUMWEAPONS rows and no more: a caller that indexes past the end is
 * refused by p_weapon_info() rather than reading the next table.
 *
 * The state numbers are p_pspr.h's statenum_t values; `P_NULL` is upstream's
 * S_NULL, which P_SetPsprite reads as "deactivate this layer". The state rows
 * themselves live in info_tables.h, which is generated from info.c.
 */
static const p_weaponinfo_t p_weapon_table[P_NUMWEAPONS] = {
    /* ammo, up, down, ready, attack, flash */
    {(int8_t)P_AM_NOAMMO, S_PUNCHUP, S_PUNCHDOWN, S_PUNCH, S_PUNCH1, S_NULL},
    {(int8_t)P_AM_CLIP, S_PISTOLUP, S_PISTOLDOWN, S_PISTOL, S_PISTOL1,
     S_PISTOLFLASH},
    {(int8_t)P_AM_SHELL, S_SGUNUP, S_SGUNDOWN, S_SGUN, S_SGUN1, S_SGUNFLASH1},
    {(int8_t)P_AM_CLIP, S_CHAINUP, S_CHAINDOWN, S_CHAIN, S_CHAIN1,
     S_CHAINFLASH1},
    {(int8_t)P_AM_MISL, S_MISSILEUP, S_MISSILEDOWN, S_MISSILE, S_MISSILE1,
     S_MISSILEFLASH1},
    {(int8_t)P_AM_CELL, S_PLASMAUP, S_PLASMADOWN, S_PLASMA, S_PLASMA1,
     S_PLASMAFLASH1},
    {(int8_t)P_AM_CELL, S_BFGUP, S_BFGDOWN, S_BFG, S_BFG1, S_BFGFLASH1},
    {(int8_t)P_AM_NOAMMO, S_SAWUP, S_SAWDOWN, S_SAW, S_SAW1, S_NULL},
    {(int8_t)P_AM_SHELL, S_DSGUNUP, S_DSGUNDOWN, S_DSGUN, S_DSGUN1,
     S_DSGUNFLASH1}
};

const p_weaponinfo_t* p_weapon_info(int32_t weapon) {
    if (weapon < 0 || weapon >= (int32_t)P_NUMWEAPONS) {
        return NULL;
    }
    return &p_weapon_table[weapon];
}

/* A table row for a slot this port's caller should never name. Upstream would
 * read past the array (its `weaponinfo[player->readyweapon]` has no test); the
 * port answers the fist's row, which is inert, so a corrupt slot cannot make a
 * fire action spend another weapon's ammo. */
static const p_weaponinfo_t* weapon_row(int32_t weapon) {
    const p_weaponinfo_t* row = p_weapon_info(weapon);

    return row != NULL ? row : &p_weapon_table[P_WP_FIST];
}

/* Upstream's `leveltime` is a global; here it is p_level_t::leveltime. A
 * hand-built context (a unit test) has no level and answers 0, which is the tic
 * the first P_PlayerThink of a level runs on. */
static int32_t level_time(const p_mobj_context_t* world) {
    return world != NULL && world->level != NULL ? world->level->leveltime : 0;
}

/* The two fine-angle lookups r_fine_sine offers: it is 5/4 of a turn long so the
 * cosine is a pointer offset, exactly as tables.h's pointer is. */
static fixed_t fine_cos(uint32_t index) {
    return r_fine_sine[index + (FINEANGLES / 4)];
}

static fixed_t fine_sin(uint32_t index) {
    return r_fine_sine[index];
}

/* Every `S_StartSound (...)` in p_pspr.c and in the attack path reaches this. It
 * counts and plays nothing: S2 puts audio outside this task and outside this
 * layer, and a named counter is how a reader sees that the branch exists. */
static void weapon_sound(p_player_t* player) {
    if (player != NULL) {
        player->silentSounds += 1u;
    }
}

/* --- The twelve weapon actions ------------------------------------------- */

/*
 * A_WeaponReady: the idle state. The player can fire, or change weapon, or --
 * once dead -- put the gun away.
 *
 * Upstream's order matters: leaving the attack state first, then the pending
 * change (which returns), then the fire test, and only then the bob. The bob is
 * the reason `leveltime` is read twice with two different masks: `(128*leveltime)
 * &FINEMASK` for the cosine and the same value masked to a quarter turn for the
 * sine, which makes the sway a closed curve rather than a line.
 */
static void a_weapon_ready(p_mobj_context_t* world, p_player_t* player,
                           p_pspdef_t* psp) {
    r_mobj_t* mo = player->mo;
    uint32_t angle;

    /* "get out of attack state" */
    if (mo != NULL && (mo->state == (int16_t)S_PLAY_ATK1 ||
                       mo->state == (int16_t)S_PLAY_ATK2)) {
        (void)p_set_mobj_state(world, mo, S_PLAY, NULL);
    }

    if (player->readyweapon == (int32_t)P_WP_CHAINSAW &&
        psp->state == (int16_t)S_SAW) {
        weapon_sound(player); /* sfx_sawidl */
    }

    /* "check for change: if player is dead, put the weapon away" */
    if (player->pendingweapon != (int32_t)P_WP_NOCHANGE || player->health == 0) {
        p_set_psprite(world, player, PS_WEAPON,
                      (statenum_t)weapon_row(player->readyweapon)->downState);
        return;
    }

    /* "check for fire; the missile launcher and bfg do not auto fire" */
    if (((uint32_t)player->cmd.buttons & (uint32_t)BT_ATTACK) != 0u) {
        if (player->attackdown == 0 ||
            (player->readyweapon != (int32_t)P_WP_MISSILE &&
             player->readyweapon != (int32_t)P_WP_BFG)) {
            player->attackdown = 1;
            p_fire_weapon(world, player);
            return;
        }
    } else {
        player->attackdown = 0;
    }

    /* "bob the weapon based on movement speed" */
    angle = ((uint32_t)128 * (uint32_t)level_time(world)) & FINEMASK;
    psp->sx = FRACUNIT + fixed_mul(player->bob, fine_cos(angle));
    angle &= (uint32_t)(FINEANGLES / 2 - 1);
    psp->sy = P_WEAPONTOP + fixed_mul(player->bob, fine_sin(angle));
}

/*
 * A_ReFire: fire again without lowering the weapon, or stop and re-check the
 * ammo. The three conditions are upstream's: the button is still down, no weapon
 * change is pending (so a change goes through instead of another shot), and the
 * player is alive.
 *
 * This is where `refire` grows: it is what makes a held trigger's bullets drift
 * (P_GunShot's `accurate` is `!player->refire`), so a chaingun burst is wider
 * than its first round.
 */
static void a_refire(p_mobj_context_t* world, p_player_t* player,
                     p_pspdef_t* psp) {
    (void)psp;

    if (((uint32_t)player->cmd.buttons & (uint32_t)BT_ATTACK) != 0u &&
        player->pendingweapon == (int32_t)P_WP_NOCHANGE && player->health != 0) {
        player->refire += 1;
        p_fire_weapon(world, player);
    } else {
        player->refire = 0;
        (void)p_check_ammo(world, player);
    }
}

/* A_CheckReload: only the super shotgun's S_DSGUN4 carries it, and the super
 * shotgun is a DOOM II weapon this port cannot hand out. It is implemented anyway
 * because it is two upstream statements and an unimplemented one would be a
 * counted gap for a state the table still walks. */
static void a_check_reload(p_mobj_context_t* world, p_player_t* player,
                           p_pspdef_t* psp) {
    (void)psp;
    (void)p_check_ammo(world, player);
}

/*
 * A_Lower: walk the weapon off the bottom of the screen, then swap.
 *
 * The three early exits are upstream's and their order matters: "already down"
 * (still above WEAPONBOTTOM), "player is dead" (stop at the bottom and do not
 * bring anything back up), and "no health" (the layer is dropped entirely). Only
 * a live player reaches `readyweapon = pendingweapon` and the raise.
 */
static void a_lower(p_mobj_context_t* world, p_player_t* player,
                    p_pspdef_t* psp) {
    psp->sy += P_LOWERSPEED;

    if (psp->sy < P_WEAPONBOTTOM) {
        return;
    }
    if (player->playerstate == P_PST_DEAD) {
        psp->sy = P_WEAPONBOTTOM;
        return;
    }
    if (player->health == 0) {
        p_set_psprite(world, player, PS_WEAPON, S_NULL);
        return;
    }

    player->readyweapon = player->pendingweapon;
    p_bring_up_weapon(world, player);
}

/* A_Raise: walk the weapon up, then enter its ready state. */
static void a_raise(p_mobj_context_t* world, p_player_t* player,
                    p_pspdef_t* psp) {
    psp->sy -= P_RAISESPEED;

    if (psp->sy > P_WEAPONTOP) {
        return;
    }
    psp->sy = P_WEAPONTOP;

    p_set_psprite(world, player, PS_WEAPON,
                  (statenum_t)weapon_row(player->readyweapon)->readyState);
}

/* A_GunFlash: the shot that shows a flash on a *separate* state row (the missile
 * launcher and the BFG). */
static void a_gun_flash(p_mobj_context_t* world, p_player_t* player,
                        p_pspdef_t* psp) {
    (void)psp;

    if (player->mo != NULL) {
        (void)p_set_mobj_state(world, player->mo, S_PLAY_ATK2, NULL);
    }
    p_set_psprite(world, player, PS_FLASH,
                  (statenum_t)weapon_row(player->readyweapon)->flashState);
}

/*
 * The five hitscan and missile fire actions, in upstream's statement order.
 *
 * Every one of them sets S_PLAY_ATK2 on the avatar before spending the round,
 * because the avatar's sprite is what the renderer shows while the gun fires.
 * The flash psprite is set *before* the trace, so a shot's flash is on screen on
 * the tic the shot resolves.
 */
static void fire_pistol(p_mobj_context_t* world, p_player_t* player) {
    const p_weaponinfo_t* row = weapon_row(player->readyweapon);
    fixed_t slope;

    weapon_sound(player); /* sfx_pistol */

    if (player->mo != NULL) {
        (void)p_set_mobj_state(world, player->mo, S_PLAY_ATK2, NULL);
        player->ammo[row->ammo] -= 1;
        p_set_psprite(world, player, PS_FLASH, (statenum_t)row->flashState);
        slope = p_bullet_slope(world, player->mo, NULL);
        p_gun_shot(world, player->mo, slope, player->refire == 0);
    } else {
        player->ammo[row->ammo] -= 1;
        p_set_psprite(world, player, PS_FLASH, (statenum_t)row->flashState);
    }
}

static void fire_shotgun(p_mobj_context_t* world, p_player_t* player) {
    const p_weaponinfo_t* row = weapon_row(player->readyweapon);
    fixed_t slope;
    int i;

    weapon_sound(player); /* sfx_shotgn */

    if (player->mo != NULL) {
        (void)p_set_mobj_state(world, player->mo, S_PLAY_ATK2, NULL);
    }
    player->ammo[row->ammo] -= 1;
    p_set_psprite(world, player, PS_FLASH, (statenum_t)row->flashState);
    if (player->mo == NULL) {
        return;
    }

    slope = p_bullet_slope(world, player->mo, NULL);

    /* "seven pellets, every one of them inaccurate" */
    for (i = 0; i < 7; ++i) {
        p_gun_shot(world, player->mo, slope, 0);
    }
}

static void fire_chaingun(p_mobj_context_t* world, p_player_t* player,
                          const p_pspdef_t* psp) {
    const p_weaponinfo_t* row = weapon_row(player->readyweapon);
    fixed_t slope;

    weapon_sound(player); /* sfx_pistol */

    /* `if (!player->ammo[weaponinfo[readyweapon].ammo]) return;` -- the empty
     * gun clicks instead of firing, and the flash sprite's offset arithmetic
     * below would read a state the gun is not in. */
    if (player->ammo[row->ammo] == 0) {
        return;
    }

    if (player->mo != NULL) {
        (void)p_set_mobj_state(world, player->mo, S_PLAY_ATK2, NULL);
    }
    player->ammo[row->ammo] -= 1;

    /* `weaponinfo[readyweapon].flashstate + psp->state - &states[S_CHAIN1]`: the
     * flash tracks *which* of the two chaingun barrels is firing, which is a
     * state-index difference. */
    p_set_psprite(world, player, PS_FLASH,
                  (statenum_t)(row->flashState +
                               (int32_t)psp->state - (int32_t)S_CHAIN1));

    if (player->mo == NULL) {
        return;
    }
    slope = p_bullet_slope(world, player->mo, NULL);
    p_gun_shot(world, player->mo, slope, player->refire == 0);
}

static void fire_missile(p_mobj_context_t* world, p_player_t* player) {
    const p_weaponinfo_t* row = weapon_row(player->readyweapon);

    player->ammo[row->ammo] -= 1;
    if (player->mo != NULL) {
        p_spawn_player_missile(world, player->mo, MT_ROCKET);
    }
}

static void fire_plasma(p_mobj_context_t* world, p_player_t* player) {
    const p_weaponinfo_t* row = weapon_row(player->readyweapon);

    player->ammo[row->ammo] -= 1;

    /* `weaponinfo[readyweapon].flashstate + (P_Random ()&1)`: the plasma rifle
     * alternates two flash frames, and the choice is a draw from the sequence, so
     * it is taken exactly here -- before the missile's own draws. */
    p_set_psprite(world, player, PS_FLASH,
                  (statenum_t)(row->flashState +
                               (int32_t)(p_random(world->random) & 1)));
    if (player->mo != NULL) {
        p_spawn_player_missile(world, player->mo, MT_PLASMA);
    }
}

static void fire_bfg(p_mobj_context_t* world, p_player_t* player) {
    const p_weaponinfo_t* row = weapon_row(player->readyweapon);

    player->ammo[row->ammo] -= P_BFGCELLS;
    if (player->mo != NULL) {
        p_spawn_player_missile(world, player->mo, MT_BFG);
    }
}

/* --- The psprite action dispatch ----------------------------------------- */

/*
 * The one place a psprite state's action runs. Upstream calls
 * `state->action.acp2 (player, psp)`; the port switches on the enum for the same
 * reason p_action.c does (p_action.h explains): the table stays data, and an
 * action nobody wrote is a counted number instead of a null pointer.
 *
 * The twelve weapon actions are here; A_Light0/1/2 belong to p_action.c because
 * they write the renderer's light level and need nothing from this file, and
 * everything else -- A_Saw, A_FireShotgun2, A_BFGSpray, A_BFGsound -- goes to
 * p_action_run_psprite() to be counted as not implemented.
 */
static void psprite_action(p_mobj_context_t* world, p_player_t* player,
                           p_pspdef_t* psp, action_t action) {
    switch (action) {
        case A_WeaponReady:
            a_weapon_ready(world, player, psp);
            return;
        case A_ReFire:
            a_refire(world, player, psp);
            return;
        case A_CheckReload:
            a_check_reload(world, player, psp);
            return;
        case A_Lower:
            a_lower(world, player, psp);
            return;
        case A_Raise:
            a_raise(world, player, psp);
            return;
        case A_GunFlash:
            a_gun_flash(world, player, psp);
            return;
        case A_FirePistol:
            fire_pistol(world, player);
            return;
        case A_FireShotgun:
            fire_shotgun(world, player);
            return;
        case A_FireCGun:
            fire_chaingun(world, player, psp);
            return;
        case A_FireMissile:
            fire_missile(world, player);
            return;
        case A_FirePlasma:
            fire_plasma(world, player);
            return;
        case A_FireBFG:
            fire_bfg(world, player);
            return;
        case A_Punch:
            /* The fist's swing is not a state transition on the weapon layer; it
             * only spends the punch and turns the avatar. `psp` is unused. */
            (void)psp;
            a_punch(world, player);
            return;
        default:
            (void)p_action_run_psprite(world, player, psp, action);
            return;
    }
}

/* --- P_SetPsprite / P_SetupPsprites / P_MovePsprites --------------------- */

/* Upstream has no bound on the zero-tic chain; this one gives up after a length
 * no real chain reaches, exactly as p_mobj.c's state chain does. */
#define P_PSPRITE_CHAIN_LIMIT 256

void p_set_psprite(p_mobj_context_t* world, p_player_t* player, int position,
                   statenum_t stnum) {
    p_pspdef_t* psp;
    int32_t guard = 0;

    if (world == NULL || player == NULL) {
        return;
    }
    if (position < 0 || position >= (int)P_NUMPSPRITES) {
        return;
    }
    psp = &player->psprites[position];

    for (;;) {
        const doom_state_t* st;

        if (stnum == S_NULL) {
            /* "object removed itself" */
            psp->state = (int16_t)S_NULL;
            return;
        }

        st = &states[stnum];
        psp->state = (int16_t)stnum;
        psp->tics = st->tics; /* could be 0 */

        /* `if (state->misc1) { psp->sx = state->misc1 << FRACBITS; ... }` -- no
         * shipped state row carries a non-zero misc1 (p_pspr.h says why), so the
         * "coordinate set" branch has no reachable form here. */

        /* "Call action routine. Modified handling." -- `if (!psp->state) break;`
         * is an action that deactivated the layer, and it wins over the chain. */
        if (st->action != (uint8_t)ACT_NULL) {
            psprite_action(world, player, psp, (action_t)st->action);
            if (psp->state == (int16_t)S_NULL) {
                return;
            }
        }

        /* `stnum = psp->state->nextstate;` -- the state the layer is in *after*
         * the action, which a fire action has just changed, and not the one this
         * iteration entered. */
        stnum = (statenum_t)states[psp->state].nextstate;
        if (psp->tics != 0) {
            return;
        }
        if (++guard > P_PSPRITE_CHAIN_LIMIT) {
            return;
        }
    }
}

void p_setup_psprites(p_mobj_context_t* world, p_player_t* player) {
    int i;

    if (player == NULL) {
        return;
    }
    /* "remove all psprites" */
    for (i = 0; i < (int)P_NUMPSPRITES; ++i) {
        player->psprites[i].state = (int16_t)S_NULL;
        player->psprites[i].tics = 0;
        player->psprites[i].sx = 0;
        player->psprites[i].sy = 0;
    }

    /* "spawn the gun" */
    player->pendingweapon = player->readyweapon;
    p_bring_up_weapon(world, player);
    player->pspritesStarted = 1u;
}

void p_move_psprites(p_mobj_context_t* world, p_player_t* player) {
    int i;

    if (player == NULL) {
        return;
    }

    for (i = 0; i < (int)P_NUMPSPRITES; ++i) {
        p_pspdef_t* psp = &player->psprites[i];

        /* "a null state means not active" */
        if (psp->state == (int16_t)S_NULL) {
            continue;
        }
        /* "a -1 tic count never changes" */
        if (psp->tics != -1) {
            psp->tics -= 1;
            if (psp->tics == 0) {
                p_set_psprite(world, player, i,
                              (statenum_t)states[psp->state].nextstate);
            }
        }
    }

    /* The flash follows the gun: `psprites[ps_flash].sx = psprites[ps_weapon].sx`
     * and the same for sy, every tic, in both directions. */
    player->psprites[PS_FLASH].sx = player->psprites[PS_WEAPON].sx;
    player->psprites[PS_FLASH].sy = player->psprites[PS_WEAPON].sy;
}

void p_calc_swing(const struct p_level* level, const struct p_player* player,
                  fixed_t* swingx, fixed_t* swingy) {
    const int32_t now = level != NULL ? level->leveltime : 0;
    const fixed_t swing = player != NULL ? player->bob : 0;
    const uint32_t base = ((uint32_t)FINEANGLES / 70u) * (uint32_t)now;
    fixed_t x;
    uint32_t angle;

    /* "OPTIMIZE: tablify this. A LUT would allow for different modes, and add
     * flexibility." -- upstream's comment, kept. */
    angle = base & FINEMASK;
    x = fixed_mul(swing, fine_sin(angle));

    angle = (base + (uint32_t)(FINEANGLES / 2)) & FINEMASK;

    if (swingx != NULL) {
        *swingx = x;
    }
    if (swingy != NULL) {
        *swingy = -fixed_mul(x, fine_sin(angle));
    }
}

/* --- P_BringUpWeapon / P_CheckAmmo / P_FireWeapon / P_DropWeapon -------- */

void p_bring_up_weapon(p_mobj_context_t* world, p_player_t* player) {
    statenum_t newstate;

    if (player == NULL) {
        return;
    }
    if (player->pendingweapon == (int32_t)P_WP_NOCHANGE) {
        player->pendingweapon = player->readyweapon;
    }

    if (player->pendingweapon == (int32_t)P_WP_CHAINSAW) {
        weapon_sound(player); /* sfx_sawup */
    }

    newstate = (statenum_t)weapon_row(player->pendingweapon)->upState;

    player->pendingweapon = (int32_t)P_WP_NOCHANGE;
    player->psprites[PS_WEAPON].sy = P_WEAPONBOTTOM;

    p_set_psprite(world, player, PS_WEAPON, newstate);
}

int p_check_ammo(p_mobj_context_t* world, p_player_t* player) {
    const p_weaponinfo_t* row;
    int32_t count;
    p_ammo_t ammo;

    if (player == NULL) {
        return 0;
    }
    row = weapon_row(player->readyweapon);
    ammo = (p_ammo_t)row->ammo;

    /* "Minimal amount for one shot varies." */
    if (player->readyweapon == (int32_t)P_WP_BFG) {
        count = P_BFGCELLS;
    } else if (player->readyweapon == (int32_t)P_WP_SUPERSHOTGUN) {
        count = 2; /* double barrel */
    } else {
        count = 1;
    }

    /* "Some do not need ammunition anyway." */
    if (ammo == P_AM_NOAMMO || player->ammo[ammo] >= count) {
        return 1;
    }

    /*
     * "Out of ammo, pick a weapon to change to. Preferences are set here."
     *
     * Upstream's two `gamemode` arms -- the plasma rifle (`gamemode != shareware`)
     * and the super shotgun (`gamemode == commercial`) -- are false in DOOM 1
     * shareware, which is the only game this port loads (L2 Q3), and are named
     * here rather than written. The BFG's arm has the same `gamemode` test.
     *
     * The do/while is upstream's `while (pendingweapon == wp_nochange)`; every arm
     * assigns, so it runs once and the port writes it as the if/else chain the
     * two forms are equal to.
     */
    if (player->weaponowned[P_WP_CHAINGUN] && player->ammo[P_AM_CLIP] != 0) {
        player->pendingweapon = (int32_t)P_WP_CHAINGUN;
    } else if (player->weaponowned[P_WP_SHOTGUN] &&
               player->ammo[P_AM_SHELL] != 0) {
        player->pendingweapon = (int32_t)P_WP_SHOTGUN;
    } else if (player->ammo[P_AM_CLIP] != 0) {
        /* Note the missing `weaponowned` test: upstream falls back to the pistol
         * on clips alone, because G_PlayerReborn always gives it. */
        player->pendingweapon = (int32_t)P_WP_PISTOL;
    } else if (player->weaponowned[P_WP_CHAINSAW]) {
        player->pendingweapon = (int32_t)P_WP_CHAINSAW;
    } else if (player->weaponowned[P_WP_MISSILE] &&
               player->ammo[P_AM_MISL] != 0) {
        player->pendingweapon = (int32_t)P_WP_MISSILE;
    } else {
        /* "If everything fails." */
        player->pendingweapon = (int32_t)P_WP_FIST;
    }

    /* "Now set appropriate weapon overlay." */
    p_set_psprite(world, player, PS_WEAPON,
                  (statenum_t)weapon_row(player->readyweapon)->downState);
    return 0;
}

void p_fire_weapon(p_mobj_context_t* world, p_player_t* player) {
    if (player == NULL) {
        return;
    }
    if (!p_check_ammo(world, player)) {
        return;
    }

    if (player->mo != NULL) {
        (void)p_set_mobj_state(world, player->mo, S_PLAY_ATK1, NULL);
    }
    p_set_psprite(world, player, PS_WEAPON,
                  (statenum_t)weapon_row(player->readyweapon)->attackState);

    /* `P_NoiseAlert (player->mo, player->mo);` -- every shot wakes the monsters
     * that can hear this sector (T15a). The counter still records the call, so
     * "the shot happened" and "the shot was silent" are separately visible. */
    p_noise_alert(world, player->mo, player->mo);
    player->noiseAlerts += 1u;
}

void p_drop_weapon(p_mobj_context_t* world, p_player_t* player) {
    if (player == NULL) {
        return;
    }
    p_set_psprite(world, player, PS_WEAPON,
                  (statenum_t)weapon_row(player->readyweapon)->downState);
}

/* --- The attack assets --------------------------------------------------- */

fixed_t p_bullet_slope(p_mobj_context_t* world, r_mobj_t* shooter,
                       r_mobj_t** target) {
    angle_t an;
    fixed_t slope;
    r_mobj_t* found = NULL;

    if (target != NULL) {
        *target = NULL;
    }
    if (world == NULL || shooter == NULL) {
        return 0;
    }

    /* "see which target is to be aimed at" -- upstream keeps `linetarget` in a
     * global, so every probe writes it and the test below reads the last one. The
     * port keeps the same variable whatever the caller asked for: a caller that
     * passes NULL for @p target must not change how many probes run. */
    an = shooter->angle;
    slope = p_aim_line_attack(world, shooter, an, 16 * 64 * FRACUNIT, &found);

    if (found == NULL) {
        an += 1u << 26;
        slope = p_aim_line_attack(world, shooter, an, 16 * 64 * FRACUNIT, &found);
        if (found == NULL) {
            an -= 2u << 26;
            slope =
                p_aim_line_attack(world, shooter, an, 16 * 64 * FRACUNIT, &found);
        }
    }

    if (target != NULL) {
        *target = found;
    }
    return slope;
}

void p_gun_shot(p_mobj_context_t* world, r_mobj_t* shooter, fixed_t slope,
                int accurate) {
    angle_t angle;
    int damage;

    if (world == NULL || shooter == NULL) {
        return;
    }

    /* `damage = 5*(P_Random ()%3+1);` -- the draw is the first of a bullet's
     * two, and it happens whether or not the shot is accurate. */
    damage = P_GUNSHOT_DAMAGE * (p_random(world->random) % 3 + 1);
    angle = shooter->angle;

    if (!accurate) {
        /* `angle += (P_Random()-P_Random())<<18;` -- two draws, and only on an
         * inaccurate shot, so their presence is observable in the sequence. */
        angle += (angle_t)((uint32_t)(int32_t)(p_random(world->random) -
                                               p_random(world->random))
                           << P_SHOT_SPREAD_SHIFT);
    }

    p_line_attack(world, shooter, angle, P_MISSILERANGE, slope, damage);
}

void a_punch(p_mobj_context_t* world, struct p_player* player) {
    r_mobj_t* target = NULL;
    angle_t angle;
    fixed_t damage;
    fixed_t slope;

    if (world == NULL || player == NULL || player->mo == NULL) {
        return;
    }

    /* `damage = (P_Random ()%10+1)<<1;` then `if (powers[pw_strength])
     * damage *= 10;` */
    damage = (fixed_t)((p_random(world->random) % 10 + 1) << 1);
    if (player->powers[P_PW_STRENGTH] != 0) {
        damage *= 10;
    }

    angle = player->mo->angle;
    angle += (angle_t)((uint32_t)(int32_t)(p_random(world->random) -
                                           p_random(world->random))
                       << 18);

    slope = p_aim_line_attack(world, player->mo, angle, P_MELEERANGE, &target);
    p_line_attack(world, player->mo, angle, P_MELEERANGE, slope, (int)damage);

    /* "turn to face target" -- and the punch's sound, which is the audio layer's
     * (S2) and is counted instead of played. */
    if (target != NULL) {
        weapon_sound(player);
        player->mo->angle =
            p_point_to_angle(target->x - player->mo->x,
                             target->y - player->mo->y);
    }
}
