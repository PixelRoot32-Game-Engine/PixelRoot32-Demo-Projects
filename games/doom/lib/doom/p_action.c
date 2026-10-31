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
 * A_Look is the only entry D4.1a ships, and it is a stub: upstream's A_Look
 * (p_enemy.c) scans the sector for a visible player, plays the see sound and
 * enters the run state. That needs P_CheckSight, the blockmap and the sound
 * layer, all of which arrive later; the stub leaves the state machine's shape
 * alone (tics and nextstate are untouched) so the world still ticks.
 *
 * This is C99 only: no allocation, no globals, no stdio, no floating point.
 */
#include "p_action.h"

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

        default:
            if (stats != NULL) {
                stats->calls += 1u;
                stats->notImplemented += 1u;
                stats->lastNotImplemented = (uint32_t)action;
            }
            return P_ACTION_NOT_IMPLEMENTED;
    }
}
