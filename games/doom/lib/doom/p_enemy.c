/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * A_Look() and P_LookForPlayers(): the half of monster AI that decides the
 * monster has seen (or heard) the player. A_Chase() is T15b's and stays a counted
 * gap in p_action.c, which is why a woken monster animates its run frames in
 * place instead of moving.
 *
 * See p_enemy.h for the upstream order, the one-slot reduction of the player
 * scan, and the see-sound draw that is performed for the random sequence and
 * discarded for the missing audio layer.
 *
 * This is C99 only: <stdint.h>, no allocation, no globals, no stdio, no floating
 * point.
 */
#include "p_enemy.h"

#include "m_random.h"
#include "p_map.h"
#include "p_setup.h"
#include "p_sight.h"

/* --- P_LookForPlayers ------------------------------------------------------ */

int p_look_for_players(p_mobj_context_t* world, r_mobj_t* actor, int allaround) {
    const p_level_t* level;
    r_mobj_t* playerMobj;
    int c;
    int stop;

    if (world == NULL || actor == NULL) {
        return 0;
    }
    level = world->level;
    if (level == NULL || level->player == NULL) {
        return 0;
    }
    playerMobj = level->player;

    c = 0;
    stop = (actor->lastlook - 1) & 3;

    for (;; actor->lastlook = (int16_t)((actor->lastlook + 1) & 3)) {
        const uint8_t slot = (uint8_t)actor->lastlook;

        /* if (!playeringame[actor->lastlook]) continue; */
        if (slot >= MAXPLAYERS || world->rules.playerInGame[slot] == 0u) {
            continue;
        }

        /* if (c++ == 2 || actor->lastlook == stop) return false; */
        if (c == 2 || actor->lastlook == stop) {
            return 0;
        }
        c += 1;

        /* This port simulates slot 0 only; a higher slot that is "in game" has no
         * mobj, which is the same answer upstream gives a slot whose player is
         * not playing. */
        if (slot != 0u) {
            continue;
        }

        /* if (player->health <= 0) continue; -- dead */
        if (level->playerState.health <= 0) {
            continue;
        }

        /* if (!P_CheckSight (actor, player->mo)) continue; */
        if (!p_check_sight(world, actor, playerMobj)) {
            continue;
        }

        if (!allaround) {
            const angle_t an =
                p_point_to_angle(playerMobj->x - actor->x,
                                 playerMobj->y - actor->y) -
                actor->angle;

            if (an > ANG90 && an < ANG270) {
                const fixed_t dist =
                    p_aprox_distance(playerMobj->x - actor->x,
                                     playerMobj->y - actor->y);

                /* if real close, react anyway */
                if (dist > P_MELEERANGE) {
                    continue; /* behind back */
                }
            }
        }

        actor->target = playerMobj;
        return 1;
    }
}

/* --- A_Look ---------------------------------------------------------------- */

/*
 * The see-sound draw. Upstream's switch chooses the sound with one P_Random()
 * draw for sfx_posit1..3 (36..38) and sfx_bgsit1..2 (39..40); every other see
 * sound takes the default arm and draws nothing. No audio layer exists (S2), so
 * the draw is kept for the random sequence and the value thrown away.
 */
static void see_sound_draw(p_mobj_context_t* world, int32_t seesound) {
    if (world == NULL || world->random == NULL) {
        return;
    }
    if (seesound >= 36 && seesound <= 40) {
        (void)p_random(world->random);
    }
}

void p_enemy_look(p_mobj_context_t* world, r_mobj_t* actor) {
    const doom_mobjinfo_t* info;
    r_mobj_t* targ;

    if (world == NULL || actor == NULL) {
        return;
    }
    if ((uint32_t)actor->type >= NUM_MOBJ_TYPES) {
        return;
    }
    info = &mobjinfo[actor->type];

    actor->threshold = 0; /* any shot will wake up */
    targ = p_sector_sound_target(world, actor->sector);
    if (targ != NULL && (targ->flags & MF_SHOOTABLE) != 0u) {
        actor->target = targ;

        if ((actor->flags & MF_AMBUSH) != 0u) {
            if (p_check_sight(world, actor, actor->target)) {
                goto seeyou;
            }
        } else {
            goto seeyou;
        }
    }

    if (!p_look_for_players(world, actor, 0)) {
        return;
    }

seeyou:
    see_sound_draw(world, info->seesound);
    (void)p_set_mobj_state(world, actor, (statenum_t)info->seestate, NULL);
}
