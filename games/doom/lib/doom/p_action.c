/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * The state-action dispatch: one case per action this port has written.
 *
 * The shape is a switch with an explicit `default`, on purpose. Adding a case is
 * how an action becomes implemented; the default is what counts the rest, so an
 * action can never be forgotten silently -- it is reported as not implemented
 * and the harness and the unit tests can see the number.
 *
 * WHAT IS HERE, AND WHY SOME OF IT IS A STUB
 *
 *   A_Look       real: upstream's A_Look (p_enemy.c) reads the sector's
 *                sound target, falls back to a visible-player scan, discards the
 *                see sound's P_Random draw and enters the run state.
 *   A_Chase, A_FaceTarget, A_PosAttack, A_TroopAttack, A_Explode
 *                real: the monster's chase, its octant turn, the
 *                zombieman's hitscan, the imp's claw-or-fireball and the
 *                explosion's radius damage. See p_enemy.h.
 *   A_Fall       real, and the smallest real action in the game:
 *                `actor->flags &= ~MF_SOLID;` -- a corpse stops blocking. This
 *                is the action the player's death chain carries (S_PLAY_DIE3) and
 *                every monster's death chain too.
 *   A_Scream, A_XScream, A_PlayerScream, A_Pain
 *                sound actions: the draw(s) upstream's selection makes, then the
 *                cue itself through DOOM_SOUND_START. They are still counted as
 *                stubs because the counter is what says "the action ran" in a
 *                build without DOOM_AUDIO, and their ids are upstream's.
 *
 * This is C99 only: no allocation, no globals, no stdio, no floating point.
 */
#include "p_action.h"

#include "p_enemy.h"
#include "p_user.h"
#include "s_sound.h"

/*
 * Records a missing action in the caller's trace, if there is one. The first
 * dispatch of an action wins: later ones add nothing, so the table stays the
 * distinct set rather than a log. A full table is reported through `overflow`
 * instead of being silently dropped.
 */
static void trace_missing(p_mobj_context_t* world, uint32_t action,
                          uint32_t type) {
    p_action_trace_t* trace;
    uint32_t index;

    if (world == NULL || world->actionTrace == NULL) {
        return;
    }
    trace = world->actionTrace;
    for (index = 0u; index < trace->count; ++index) {
        if (trace->entries[index].action == action) {
            return;
        }
    }
    if (trace->count >= (uint32_t)P_ACTION_MISSING_MAX) {
        trace->overflow += 1u;
        return;
    }
    trace->entries[trace->count].action = action;
    trace->entries[trace->count].firstTic = trace->tic;
    trace->entries[trace->count].type = type;
    trace->count += 1u;
}

void p_action_init(p_action_stats_t* stats) {
    if (stats == NULL) {
        return;
    }
    stats->calls = 0u;
    stats->done = 0u;
    stats->stubs = 0u;
    stats->notImplemented = 0u;
    stats->unknown = 0u;
    stats->nullMobj = 0u;
    stats->lastNotImplemented = 0u;
}

p_action_outcome_t p_action_run(p_mobj_context_t* world, r_mobj_t* mobj,
                                action_t action) {
    p_action_stats_t* stats = world != NULL ? world->actions : NULL;

    /* ACT_NULL is upstream's `{NULL}`: no action exists, nothing is called and
     * nothing is counted. Dispatching it would otherwise make the 519 inert
     * states the bulk of the "not implemented" counter. */
    if (action == ACT_NULL) {
        return P_ACTION_NONE;
    }
    if (mobj == NULL) {
        if (stats != NULL) {
            stats->calls += 1u;
            stats->nullMobj += 1u;
        }
        return P_ACTION_NULL_MOBJ;
    }
    if ((uint32_t)action >= NUM_ACTIONS) {
        if (stats != NULL) {
            stats->calls += 1u;
            stats->unknown += 1u;
        }
        return P_ACTION_UNKNOWN;
    }

    switch (action) {
        case A_Look:
            /* A_Look (p_enemy.c): read the sector's sound target, scan for
             * a visible player, discard the see sound's draw and enter the see
             * state. */
            p_enemy_look(world, mobj);
            if (stats != NULL) {
                stats->calls += 1u;
                stats->done += 1u;
            }
            return P_ACTION_DONE;

        case A_Chase:
            /* A_Chase (p_enemy.c): reaction time, the octant turn, melee
             * or missile, one step, and the active sound's discarded draw. */
            p_enemy_chase(world, mobj);
            if (stats != NULL) {
                stats->calls += 1u;
                stats->done += 1u;
            }
            return P_ACTION_DONE;

        case A_FaceTarget:
            p_enemy_face_target(world, mobj);
            if (stats != NULL) {
                stats->calls += 1u;
                stats->done += 1u;
            }
            return P_ACTION_DONE;

        case A_PosAttack:
            p_enemy_pos_attack(world, mobj);
            if (stats != NULL) {
                stats->calls += 1u;
                stats->done += 1u;
            }
            return P_ACTION_DONE;

        case A_SPosAttack:
            /* A_SPosAttack (p_enemy.c): three spread passes, each with its
             * own drift/drift/damage draw, aimed once on the actor's facing. */
            p_enemy_spos_attack(world, mobj);
            if (stats != NULL) {
                stats->calls += 1u;
                stats->done += 1u;
            }
            return P_ACTION_DONE;

        case A_SargAttack:
            /* A_SargAttack (p_enemy.c): face, range-test, then one draw
             * for 4..40 points of bite. Demon and spectre share the state. */
            p_enemy_sarg_attack(world, mobj);
            if (stats != NULL) {
                stats->calls += 1u;
                stats->done += 1u;
            }
            return P_ACTION_DONE;

        case A_TroopAttack:
            p_enemy_troop_attack(world, mobj);
            if (stats != NULL) {
                stats->calls += 1u;
                stats->done += 1u;
            }
            return P_ACTION_DONE;

        case A_Explode:
            p_enemy_explode(world, mobj);
            if (stats != NULL) {
                stats->calls += 1u;
                stats->done += 1u;
            }
            return P_ACTION_DONE;

        case A_Fall:
            /* `actor->flags &= ~MF_SOLID;` (p_enemy.c) -- the corpse lies down
             * and stops being an obstacle. */
            mobj->flags &= ~MF_SOLID;
            if (stats != NULL) {
                stats->calls += 1u;
                stats->done += 1u;
            }
            return P_ACTION_DONE;

        case A_Scream: {
            /* A_Scream (p_enemy.c) chooses deathsound with one P_Random draw for
             * the zombie families: `sfx_podth1 + P_Random()%3` for podth1..3
             * (59..61) and `sfx_bgdth1 + P_Random()%2` for bgdth1..2 (62..63);
             * every other deathsound takes the default arm and draws nothing.
             * The draw is part of a demo's random sequence, and the chosen id is
             * the one the sound layer plays -- the same switch, one value. */
            int32_t sound = 0;
            int32_t deathsound = 0;

            if (world != NULL && (uint32_t)mobj->type < NUM_MOBJ_TYPES) {
                deathsound = mobjinfo[mobj->type].deathsound;
            }
            sound = deathsound;
            if (world != NULL && world->random != NULL && deathsound != 0) {
                if (deathsound >= (int32_t)sfx_podth1
                    && deathsound <= (int32_t)sfx_podth3) {
                    sound = (int32_t)sfx_podth1
                        + (p_random(world->random) % 3);
                } else if (deathsound >= (int32_t)sfx_bgdth1
                           && deathsound <= (int32_t)sfx_bgdth2) {
                    sound = (int32_t)sfx_bgdth1
                        + (p_random(world->random) % 2);
                }
            }
            if (sound > 0 && sound < (int32_t)NUMSFX) {
                // "Check for bosses": upstream plays those at full volume, with
                // no origin, so distance cannot attenuate them.
                if (mobj->type == (int16_t)MT_SPIDER
                    || mobj->type == (int16_t)MT_CYBORG) {
                    DOOM_SOUND_START(NULL, sound);
                } else {
                    DOOM_SOUND_START(mobj, sound);
                }
            }
            if (stats != NULL) {
                stats->calls += 1u;
                stats->stubs += 1u;
            }
            return P_ACTION_STUB;
        }

        case A_XScream:
            /* `S_StartSound (actor, sfx_slop);` -- the gibbed thing. */
            DOOM_SOUND_START(mobj, sfx_slop);
            if (stats != NULL) {
                stats->calls += 1u;
                stats->stubs += 1u;
            }
            return P_ACTION_STUB;

        case A_PlayerScream:
            /* Upstream starts with sfx_pldeth and switches to sfx_pdiehi only
             * in a commercial game with `health < -50`; this port is shareware,
             * so the default is the whole rule. */
            DOOM_SOUND_START(mobj, sfx_pldeth);
            if (stats != NULL) {
                stats->calls += 1u;
                stats->stubs += 1u;
            }
            return P_ACTION_STUB;

        case A_Pain:
            /* `if (actor->info->painsound) S_StartSound (actor, ...);` -- the
             * player's pain (sfx_plpain) reaches here through S_PLAY_PAIN. */
            if ((uint32_t)mobj->type < NUM_MOBJ_TYPES
                && mobjinfo[mobj->type].painsound > 0) {
                DOOM_SOUND_START(mobj, mobjinfo[mobj->type].painsound);
            }
            if (stats != NULL) {
                stats->calls += 1u;
                stats->stubs += 1u;
            }
            return P_ACTION_STUB;

        default:
            if (stats != NULL) {
                stats->calls += 1u;
                stats->notImplemented += 1u;
                stats->lastNotImplemented = (uint32_t)action;
            }
            trace_missing(world, (uint32_t)action, (uint32_t)mobj->type);
            return P_ACTION_NOT_IMPLEMENTED;
    }
}

/*
 * The weapon-sprite half. Upstream's `actionf_t` union has two signatures and this
 * is `acp2 (player_t*, pspdef_t*)`.
 *
 * The three light-level actions are here rather than in p_pspr.c because they are
 * the only weapon actions that need nothing from that file: each is one
 * assignment to `player->extralight`, which is upstream's muzzle-flash light
 * level. The world view is rendered with an extralight of 0, so the write does
 * not brighten the walls; the weapon sprite is its only reader. The counter keeps
 * the action visible, and a test can read `extralight` directly.
 *
 * Everything else falls through to the same "not implemented" counters the mobj
 * half keeps. A_Saw (the chainsaw), A_FireShotgun2 (the super shotgun),
 * A_BFGSpray and A_BFGsound are the four that can reach it, and none of them can
 * be fired in DOOM 1 shareware; the counters are how that stays a stated gap
 * instead of a missing case.
 *
 * A NULL player is P_ACTION_NULL_MOBJ: it is the same defect (an action with no
 * subject) and it must not dereference.
 */
p_action_outcome_t p_action_run_psprite(p_mobj_context_t* world,
                                        struct p_player* player,
                                        p_pspdef_t* psp, action_t action) {
    p_action_stats_t* stats = world != NULL ? world->actions : NULL;

    (void)psp;

    if (action == ACT_NULL) {
        return P_ACTION_NONE;
    }
    if (player == NULL) {
        if (stats != NULL) {
            stats->calls += 1u;
            stats->nullMobj += 1u;
        }
        return P_ACTION_NULL_MOBJ;
    }
    if ((uint32_t)action >= NUM_ACTIONS) {
        if (stats != NULL) {
            stats->calls += 1u;
            stats->unknown += 1u;
        }
        return P_ACTION_UNKNOWN;
    }

    switch (action) {
        case A_Light0:
            player->extralight = 0;
            break;
        case A_Light1:
            player->extralight = 1;
            break;
        case A_Light2:
            player->extralight = 2;
            break;

        default:
            if (stats != NULL) {
                stats->calls += 1u;
                stats->notImplemented += 1u;
                stats->lastNotImplemented = (uint32_t)action;
            }
            trace_missing(world, (uint32_t)action, P_ACTION_TYPE_NONE);
            return P_ACTION_NOT_IMPLEMENTED;
    }

    if (stats != NULL) {
        stats->calls += 1u;
        stats->done += 1u;
    }
    return P_ACTION_DONE;
}
