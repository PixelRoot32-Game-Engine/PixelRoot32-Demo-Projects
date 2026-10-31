/*
 * The player: P_PlayerThink, P_MovePlayer, P_CalcHeight, P_Thrust, and the
 * game's tic P_Ticker (p_level_tic).
 *
 * The reference is linuxdoom-1.10 p_user.c and p_tick.c's P_Ticker. The body is
 * upstream's, in upstream's order, with every deferred branch named at the site
 * so the gap is visible in the source and in p_player_t's counters. The one
 * structural difference is forced by the port's rule of no file-scope mutable
 * globals: upstream's `onground` and `leveltime` are fields (`player->onground`,
 * `level->leveltime`).
 *
 * p_level_tic() lives here, not in p_setup.c, so that p_setup.c keeps linking
 * without this file: the two pre-existing gates build it alone. See p_setup.h.
 *
 * This is C99 only: no allocation, no stdio, no floating point, no globals.
 */
#include "p_user.h"

#include <string.h>

#include "p_map.h"
#include "p_setup.h"
#include "r_tables.h"

/* --- G_PlayerReborn ------------------------------------------------------ */

void p_player_init(p_level_t* level, r_mobj_t* mobj) {
    if (level == NULL) {
        return;
    }
    /* G_PlayerReborn's live fields; the rule and its reason live in p_user.h. */
    p_player_wire(&level->playerState, mobj);
}

/* --- P_MovePlayer -------------------------------------------------------- */

/*
 * `player->mo->angle += (cmd->angleturn<<16);`
 *
 * Upstream shifts a signed short left by 16 in an int. A negative left shift is
 * undefined in C99, so the port casts to uint32_t first: the two's-complement
 * value is the same, and `angle` is unsigned, so the addition wraps identically.
 *
 * `onground = (player->mo->z <= player->mo->floorz);`
 *
 * Both thrusts are gated on it ("Do not let the player control movement if not
 * onground"). The side thrust is `player->mo->angle-ANG90`, not `+ANG90`:
 * positive sidemove is strafe *right*, and in DOOM's counter-clockwise angle
 * space right is a quarter turn clockwise.
 */
static void p_move_player(p_level_t* level, p_player_t* player) {
    const p_ticcmd_t* cmd = &player->cmd;
    r_mobj_t* mo = player->mo;

    mo->angle += (angle_t)((uint32_t)(int32_t)cmd->angleturn << 16);

    player->onground = (uint8_t)(mo->z <= mo->floorz);

    if (cmd->forwardmove != 0 && player->onground != 0u) {
        /* `P_Thrust (player, player->mo->angle, cmd->forwardmove*2048);` */
        p_thrust(&level->context, mo, mo->angle,
                 (fixed_t)cmd->forwardmove * 2048);
    }

    if (cmd->sidemove != 0 && player->onground != 0u) {
        /* `P_Thrust (player, player->mo->angle-ANG90, cmd->sidemove*2048);` */
        p_thrust(&level->context, mo, mo->angle - ANG90,
                 (fixed_t)cmd->sidemove * 2048);
    }

    /* `if ((cmd->forwardmove || cmd->sidemove) && mo->state == &states[S_PLAY])
     *      P_SetMobjState (player->mo, S_PLAY_RUN1);`
     * The port stores a state index, so the test is on the index. */
    if ((cmd->forwardmove != 0 || cmd->sidemove != 0) &&
        mo->state == (int16_t)S_PLAY) {
        (void)p_set_mobj_state(&level->context, mo, S_PLAY_RUN1, NULL);
    }
}

/* --- P_CalcHeight -------------------------------------------------------- */

/*
 * `player->bob = FixedMul(momx,momx) + FixedMul(momy,momy); player->bob >>= 2;
 *  if (player->bob > MAXBOB) player->bob = MAXBOB;`
 *
 * Then upstream's two branches:
 *
 *   not onground  viewz = mo->z + VIEWHEIGHT, clamped to `mo->ceilingz -
 *                 4*FRACUNIT`, and then immediately overwritten by
 *                 `mo->z + player->viewheight`. The overwrite is upstream's and
 *                 is kept verbatim: the oracle compares the value it leaves.
 *   onground      the bob angle is `(FINEANGLES/20*leveltime)&FINEMASK` and the
 *                 offset is `FixedMul(player->bob/2, finesine[angle])`.
 *
 * The viewheight walk is upstream's half step: add `deltaviewheight`, clamp to
 * [VIEWHEIGHT/2, VIEWHEIGHT], and if a delta is active grow it by FRACUNIT/4 so
 * the eye eases into place over a few tics. `deltaviewheight` of 0 stays 0 (a
 * settled view does not start bobbing again).
 *
 * The final clamp against `mo->ceilingz - 4*FRACUNIT` is upstream's; there is no
 * floor clamp in P_CalcHeight.
 */
static void p_calc_height(p_level_t* level, p_player_t* player) {
    r_mobj_t* mo = player->mo;
    fixed_t bob;
    uint32_t angle;

    player->bob = fixed_mul(mo->momx, mo->momx) +
                  fixed_mul(mo->momy, mo->momy);
    player->bob >>= 2;
    if (player->bob > P_MAXBOB) {
        player->bob = P_MAXBOB;
    }

    /* `(player->cheats & CF_NOMOMENTUM)` is absent: this port has no cheats
     * (D5), so only the airborne branch remains. */
    if (player->onground == 0u) {
        player->viewz = mo->z + P_VIEWHEIGHT;
        if (player->viewz > mo->ceilingz - 4 * FRACUNIT) {
            player->viewz = mo->ceilingz - 4 * FRACUNIT;
        }
        /* Upstream's next statement discards the clamp above. Kept because the
         * value it leaves is the one every caller and the oracle observe. */
        player->viewz = mo->z + player->viewheight;
        return;
    }

    /* `angle = (FINEANGLES/20*leveltime)&FINEMASK;` -- leveltime is an int
     * upstream; the unsigned product is the same value modulo the mask. */
    angle = ((uint32_t)P_BOB_FINE_STEP * (uint32_t)level->leveltime) & FINEMASK;
    bob = fixed_mul(player->bob / 2, r_fine_sine[angle]);

    /* `if (player->playerstate == PST_LIVE)` -- the dead caller is
     * P_DeathThink's, which D4.2d owns; this function only runs live today. */
    if (player->playerstate == P_PST_LIVE) {
        player->viewheight += player->deltaviewheight;

        if (player->viewheight > P_VIEWHEIGHT) {
            player->viewheight = P_VIEWHEIGHT;
            player->deltaviewheight = 0;
        }

        if (player->viewheight < P_VIEWHEIGHT / 2) {
            player->viewheight = P_VIEWHEIGHT / 2;
            if (player->deltaviewheight <= 0) {
                player->deltaviewheight = 1;
            }
        }

        if (player->deltaviewheight != 0) {
            player->deltaviewheight += FRACUNIT / 4;
            if (player->deltaviewheight == 0) {
                player->deltaviewheight = 1;
            }
        }
    }

    player->viewz = mo->z + player->viewheight + bob;

    if (player->viewz > mo->ceilingz - 4 * FRACUNIT) {
        player->viewz = mo->ceilingz - 4 * FRACUNIT;
    }
}

/* --- P_PlayerThink ------------------------------------------------------- */

/*
 * P_UseLines() reports nothing (it is upstream's void), so the only evidence
 * that the press did something is the special dispatcher's own counter. The
 * delta around the call is this tic's answer; a line crossed later in the same
 * tic by P_XYMovement cannot be mistaken for the press.
 */
static void p_player_use_lines(p_level_t* level, p_player_t* player) {
    const uint32_t before = level->specStats.triggers;

    p_use_lines(&level->context, player->mo);

    if (level->specStats.triggers != before) {
        player->useTriggered = 1u;
        player->useTriggers += 1u;
    }
}

void p_player_think(p_level_t* level, const p_ticcmd_t* cmd) {
    p_player_t* player;
    p_ticcmd_t* command;
    p_ticcmd_t empty;

    if (level == NULL) {
        return;
    }
    player = &level->playerState;
    if (player->mo == NULL) {
        return;
    }

    if (cmd == NULL) {
        memset(&empty, 0, sizeof empty);
        cmd = &empty;
    }

    /* G_Ticker copies the network command into the player record before
     * P_Ticker runs; the player think then works from `player->cmd`. */
    player->cmd = *cmd;
    command = &player->cmd;
    player->useTriggered = 0u;

    /* `if (player->playerstate == PST_DEAD) { P_DeathThink (player); return; }`
     * P_DeathThink is D4.2d's. A record that is not live is counted rather
     * than pretended to live: the tic changes nothing except this counter. */
    if (player->playerstate != P_PST_LIVE) {
        player->unsupportedDeath += 1u;
        return;
    }

    /* `if (player->mo->reactiontime) player->mo->reactiontime--;
     *  else P_MovePlayer (player);` */
    if (player->mo->reactiontime != 0) {
        player->mo->reactiontime--;
    } else {
        p_move_player(level, player);
    }

    /* P_PlayerThink calls P_CalcHeight after P_MovePlayer, so the bob uses the
     * momentum this tic's thrust just added. */
    p_calc_height(level, player);

    /* `if (player->mo->subsector->sector->special) P_PlayerInSpecialSector`
     * is sector damage and belongs to D4.2d. */

    /* `if (cmd->buttons & BT_SPECIAL) cmd->buttons = 0;` -- "A special event
     * has no other buttons." The pause/save side of the word is G_Ticker's,
     * which is the game loop's and not this layer's. */
    if ((command->buttons & BT_SPECIAL) != 0u) {
        command->buttons = 0u;
    }

    /* Weapon change: `newweapon = (cmd->buttons&BT_WEAPONMASK)>>BT_WEAPONSHIFT`
     * and the weaponowned[] tests are D4.2d's. Counted so a pressed key is not
     * silent. */
    if ((command->buttons & BT_CHANGE) != 0u) {
        player->unsupportedWeaponChange += 1u;
    }

    /* `if (cmd->buttons & BT_USE) { if (!player->usedown) { P_UseLines (player);
     *      player->usedown = true; } } else player->usedown = false;` */
    if ((command->buttons & BT_USE) != 0u) {
        if (!player->usedown) {
            p_player_use_lines(level, player);
            player->usedown = 1;
        }
    } else {
        player->usedown = 0;
    }

    /* The fire button is consumed by A_WeaponReady inside P_MovePsprites, which
     * needs the weapon sprite state machine (D4.2d). Count it here so a press
     * is a number instead of a no-op. */
    if ((command->buttons & BT_ATTACK) != 0u) {
        player->unsupportedAttack += 1u;
    }

    /* `P_MovePsprites (player);` -- D4.2d. */

    /* `if (player->damagecount) player->damagecount--;
     *  if (player->bonuscount) player->bonuscount--;` */
    if (player->damagecount != 0) {
        player->damagecount--;
    }
    if (player->bonuscount != 0) {
        player->bonuscount--;
    }

    /* The power-up counters and the fixedcolormap they select are D4.2d's;
     * fixedcolormap stays 0 because no power-up can be active. */
}

/* --- P_Ticker ------------------------------------------------------------ */

p_tick_error_t p_level_tic(p_level_t* level, const p_ticcmd_t* cmd,
                           p_thinker_run_t* stats) {
    p_tick_error_t result;

    if (level == NULL) {
        return P_TICK_ERR_NULL_LIST;
    }
    if (!level->open) {
        return P_TICK_ERR_NOT_OPEN;
    }

    /* p_tick.c P_Ticker, in order: every player's think, P_RunThinkers, then
     * `leveltime++`. The player think runs first so P_MovePlayer's thrust is
     * the momentum P_XYMovement applies in the same tic. */
    if (level->playerState.mo != NULL) {
        p_player_think(level, cmd);
    }

    result = p_level_tick(level, stats);

    /* `leveltime++;` -- for par times, and the bob angle P_CalcHeight reads. */
    level->leveltime += 1;
    return result;
}
