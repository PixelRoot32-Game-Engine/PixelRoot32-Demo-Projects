/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * The state-action dispatch.
 *
 * Upstream stores a function pointer in every one of the 967 `states[]` records
 * (`actionf_t action`) and P_SetMobjState() calls it when the state is set. A
 * function pointer in the table is a relocation in .rodata and, more
 * importantly, it leaves no single place that can say what is implemented: a
 * missing action is simply a null pointer and nothing happens.
 *
 * This port generates `states[]` with an `action_t` enum instead (see
 * info_tables.h) and maps values to implementations here. That buys two things
 * the port needs:
 *
 *   1. the state table is pure data -- five numbers per record, no addresses --
 *      so it is compiled once into flash and compared field for field against
 *      the oracle;
 *   2. an action that is not implemented yet is *counted* rather than silently
 *      skipped. D4.2 implements most of them; until then a monster that should
 *      chase stands still and the counter says so, which is a visible defect
 *      instead of a silent one.
 *
 * WHAT IS IMPLEMENTED
 *
 * A_Look (T15a, p_enemy.c) is the action a monster's standing state carries
 * (`S_POSS_STND` -> A_Look). It reads the sector's sound target, scans for a
 * visible player and enters the see state. Every other action is counted, so a
 * monster that should chase stands still and the counter says so, which is a
 * visible defect instead of a silent one.
 *
 * `ACT_NULL` is not an action. Upstream writes `{NULL}` in the table and skips
 * the call entirely (`if (st->action.acp1)`), which is what decorations and
 * every inert frame do; dispatching it returns P_ACTION_NONE and is not counted
 * at all, or the "not implemented" counter would be dominated by the 519 of
 * 967 states that never had an action.
 *
 * THE TWO SIGNATURES
 *
 * Upstream's `actionf_t` is a union of `void (*acp1)(mobj_t*)` and
 * `void (*acp2)(player_t*, pspdef_t*)`. p_mobj.c calls acp1 for a thing's state
 * and p_pspr.c calls acp2 for a weapon sprite's state. This file dispatches
 * both: p_action_run() is the mobj half, p_action_run_psprite() the weapon half.
 * The split is not cosmetic -- A_Light0/1/2 are the weapon actions that touch
 * nothing but the player record, and they live here so that p_pspr.c does not
 * have to know about light levels.
 *
 * This is C99 only: <stdint.h>, no allocation, no globals, no stdio, no
 * floating point. The counters are caller-owned.
 */
#ifndef DOOM_P_ACTION_H
#define DOOM_P_ACTION_H

#include <stdint.h>

#include "p_mobj.h"
#include "p_pspr.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What one dispatch did.
 *
 *   P_ACTION_NONE             ACT_NULL: nothing to run, not counted
 *   P_ACTION_DONE             an implemented action ran
 *   P_ACTION_STUB             an action implemented as a deliberate no-op
 *   P_ACTION_NOT_IMPLEMENTED  a real action D4.2 has not written yet
 *   P_ACTION_UNKNOWN          a value outside action_t (a corrupt state table)
 *   P_ACTION_NULL_MOBJ        no mobj was passed
 */
typedef enum p_action_outcome {
    P_ACTION_NONE = 0,
    P_ACTION_DONE = 1,
    P_ACTION_STUB = 2,
    P_ACTION_NOT_IMPLEMENTED = 3,
    P_ACTION_UNKNOWN = 4,
    P_ACTION_NULL_MOBJ = 5
} p_action_outcome_t;

/*
 * The counters, owned by whoever owns the world (p_level_t holds one). Every
 * field is a plain count since the last p_action_init(); `calls` counts
 * dispatches of a non-null action, so it equals done + stubs + notImplemented +
 * unknown + nullMobj.
 *
 * `lastNotImplemented` is the action_t of the most recent missing action, which
 * is what makes the counter actionable: "14 not implemented" is a number, "the
 * last one was A_Chase" is a bug report.
 */
typedef struct p_action_stats {
    uint32_t calls;
    uint32_t done;
    uint32_t stubs;
    uint32_t notImplemented;
    uint32_t unknown;
    uint32_t nullMobj;
    uint32_t lastNotImplemented;
} p_action_stats_t;

/*
 * The distinct missing-action record (D4.4).
 *
 * The counters above answer "how many" and "which one last", but a run that
 * reaches two different missing actions in one tic collapses both into one
 * counter increment and one `lastNotImplemented` value. Before anyone can
 * decide how much action work a demo still needs, the gate has to name the
 * *set*. This is a small, caller-owned log of that set: each distinct action
 * once, with the tic it was first reached and the actor that reached it.
 *
 * It costs the firmware no storage of its own: p_mobj_context_t carries one
 * pointer (NULL in every shipped build) and the table belongs to the caller.
 * `tic` is the caller's current tic -- the playback harness sets it before each
 * world tic; firstTic is whatever it held when the action was first dispatched.
 *
 * `type` is the r_mobj_t::type of the actor that ran the action, or
 * P_ACTION_TYPE_NONE for a weapon-sprite dispatch, which has no mobj.
 */
#define P_ACTION_MISSING_MAX 32
#define P_ACTION_TYPE_NONE 0xFFFFFFFFu

typedef struct p_action_missing {
    uint32_t action;
    uint32_t firstTic;
    uint32_t type;
} p_action_missing_t;

typedef struct p_action_trace {
    uint32_t tic;      /* the caller's current tic, set before each world tic */
    uint32_t count;    /* distinct actions recorded */
    uint32_t overflow; /* distinct actions not recorded: the table was full */
    p_action_missing_t entries[P_ACTION_MISSING_MAX];
} p_action_trace_t;

/**
 * @brief Zeroes @p stats. A NULL pointer is a no-op.
 */
void p_action_init(p_action_stats_t* stats);

/**
 * @brief Runs @p action on @p mobj, counting the outcome.
 *
 * The counters live in `world->actions`; a NULL world means "dispatch without
 * counting", which is only useful to a caller that has no world yet. A NULL
 * mobj is refused (`P_ACTION_NULL_MOBJ`) before any implementation runs, so an
 * action never dereferences a null record.
 *
 * @param world  The mobj context, for the counter sink.
 * @param mobj   The thing the action applies to.
 * @param action The state's action value.
 * @return The outcome, one of p_action_outcome_t.
 */
p_action_outcome_t p_action_run(p_mobj_context_t* world, r_mobj_t* mobj,
                                action_t action);

/**
 * @brief Runs the weapon-sprite half of the dispatch: `acp2 (player, psp)`.
 *
 * Only the three light-level actions are implemented here, because they are the
 * only weapon actions that need nothing from p_pspr.c:
 *
 *   A_Light0, A_Light1, A_Light2   `player->extralight = 0 | 1 | 2;`
 *
 * Upstream's muzzle flash is a brighter *view* for as long as the flash psprite
 * shows one of these states. The renderer has no light state yet (the weapon
 * widget and the status bar are D5.1), so the write is real state that nothing
 * draws: counted as P_ACTION_DONE, because the statement runs, and comparable in
 * a test through `extralight` so "the flash happened" and "the flash was a no-op"
 * cannot look the same.
 *
 * Everything else -- A_Saw, A_FireShotgun2, A_BFGSpray, A_BFGsound, and anything
 * a corrupt table produces -- is counted as P_ACTION_NOT_IMPLEMENTED through the
 * same counters p_action_run() keeps, so a weapon state the psprite machine
 * reaches without an implementation is a number and not a silence.
 *
 * @param world  The mobj context, for the counter sink.
 * @param player The player record the light levels are written to.
 * @param psp    The layer the state belongs to (unused by the three lights today;
 *               carried so the signature is upstream's).
 * @param action The state's action value.
 * @return P_ACTION_DONE, P_ACTION_NOT_IMPLEMENTED, P_ACTION_UNKNOWN,
 *         P_ACTION_NULL_MOBJ or P_ACTION_NONE.
 */
p_action_outcome_t p_action_run_psprite(p_mobj_context_t* world,
                                        struct p_player* player,
                                        p_pspdef_t* psp, action_t action);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_P_ACTION_H */
