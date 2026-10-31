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
 *   A_Look       D4.1a's deliberate stub: upstream's A_Look (p_enemy.c) scans
 *                the sector for a visible player, plays the see sound and enters
 *                the run state, all of which need P_CheckSight and the sound
 *                layer (T15). The state machine's shape is left alone.
 *   A_Fall       real, and the smallest real action in the game:
 *                `actor->flags &= ~MF_SOLID;` -- a corpse stops blocking. This
 *                is the action the player's death chain carries (S_PLAY_DIE3) and
 *                every monster's death chain too.
 *   A_Scream, A_XScream, A_PlayerScream, A_Pain
 *                sound only, so they are stubs until the APU layer exists (S2).
 *                They are counted rather than skipped for the same reason A_Look
 *                is: "the death played its sound" and "the death was silent" must
 *                not look the same from a counter.
 *
 * This is C99 only: no allocation, no globals, no stdio, no floating point.
 */
#include "p_action.h"

#include "p_user.h"

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
            /* D4.1a stub: a spawned monster stands and looks at nothing. */
            if (stats != NULL) {
                stats->calls += 1u;
                stats->stubs += 1u;
            }
            return P_ACTION_STUB;

        case A_Fall:
            /* `actor->flags &= ~MF_SOLID;` (p_enemy.c) -- the corpse lies down
             * and stops being an obstacle. */
            mobj->flags &= ~MF_SOLID;
            if (stats != NULL) {
                stats->calls += 1u;
                stats->done += 1u;
            }
            return P_ACTION_DONE;

        case A_Scream:
        case A_XScream:
        case A_PlayerScream:
        case A_Pain:
            /* Pure sound in upstream: A_Scream picks deathsound (gibdth for the
             * three zombie variants), A_XScream plays sfx_slop for a gibbed
             * thing, A_PlayerScream picks pldeth or pdiehi, A_Pain plays
             * painsound. No APU layer exists yet (S2), so nothing happens; the
             * counter is what says so. */
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
 * level. The renderer has no light state yet -- the weapon widget, the status bar
 * and the lit view are D5.1 -- so the write is state nothing draws; the counter
 * is what keeps that from being invisible, and a test can read `extralight`
 * directly.
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
            return P_ACTION_NOT_IMPLEMENTED;
    }

    if (stats != NULL) {
        stats->calls += 1u;
        stats->done += 1u;
    }
    return P_ACTION_DONE;
}
