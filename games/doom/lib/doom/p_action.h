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
 * WHAT D4.1a IMPLEMENTS
 *
 * Only A_Look, and only as a deliberate stub: it is the action a monster's
 * standing state carries (`S_POSS_STND` -> A_Look), so a spawned monster
 * reaches it on its first state cycle. The stub is a *distinct outcome*
 * (`P_ACTION_STUB`), not "done": the counter separates "nothing to do yet"
 * from "implemented and ran", so D4.2 can watch the stub count fall to zero
 * action by action.
 *
 * `ACT_NULL` is not an action. Upstream writes `{NULL}` in the table and skips
 * the call entirely (`if (st->action.acp1)`), which is what decorations and
 * every inert frame do; dispatching it returns P_ACTION_NONE and is not counted
 * at all, or the "not implemented" counter would be dominated by the 519 of
 * 967 states that never had an action.
 *
 * This is C99 only: <stdint.h>, no allocation, no globals, no stdio, no
 * floating point. The counters are caller-owned.
 */
#ifndef DOOM_P_ACTION_H
#define DOOM_P_ACTION_H

#include <stdint.h>

#include "p_mobj.h"

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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_P_ACTION_H */
