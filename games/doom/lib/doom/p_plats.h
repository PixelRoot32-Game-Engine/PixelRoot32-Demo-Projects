/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * Lifts (plats): EV_DoPlat, T_PlatRaise, EV_StopPlat and the in-stasis resume.
 *
 * The reference is linuxdoom-1.10 p_plats.c. A plat is a floor move between
 * `low` and `high` with a wait; it is the only mover with a `waiting` state that
 * also decides its next direction from where the floor is.
 *
 * THE UPSTREAM QUIRKS THIS FILE KEEPS
 *
 *   - `T_PlatRaise`'s `waiting` case falls through to `in_stasis` (there is no
 *     `break`), which is harmless but is the shape of the state machine.
 *   - `downWaitUpStay`, `blazeDWUS`, `raiseAndChange` and
 *     `raiseToNearestAndChange` remove themselves when they reach the top;
 *     `perpetualRaise` never does, and `EV_StopPlat` is how a perpetual plat is
 *     stopped.
 *   - `perpetualRaise` starts in `up` or `down` from `P_Random() & 1`, which
 *     consumes the sequence at trigger time.
 *   - `low` is clamped down to the sector's own floor and `high` up to it, so a
 *     plat never starts by moving away from itself.
 *
 * THE IN-STASIS RULE, WITHOUT THE activeplats[] ARRAY
 *
 * Upstream keeps `activeplats[MAXPLATS]` to find a stopped plat again. Every
 * plat is already in the thinker list, so the port walks that list instead:
 * `P_ActivateInStasis` is `p_activate_in_stasis_plats`, and `EV_StopPlat`
 * clears the thinker's function pointer exactly as upstream does. The 30-plat
 * cap disappears with the array; no level the port loads approaches it.
 *
 * This is C99 only: no allocation, no globals, no stdio, no floating point.
 */
#ifndef DOOM_P_PLATS_H
#define DOOM_P_PLATS_H

#include <stdint.h>

#include "m_fixed.h"
#include "p_spec.h"
#include "p_tick.h"

#ifdef __cplusplus
extern "C" {
#endif

struct p_mobj_context;

/* plat_e (p_spec.h), in upstream's order. The values are the dump protocol. */
typedef enum p_plat_status {
    P_PLAT_UP = 0,
    P_PLAT_DOWN = 1,
    P_PLAT_WAITING = 2,
    P_PLAT_IN_STASIS = 3
} p_plat_status_t;

/* plattype_e (p_spec.h), in upstream's order. */
typedef enum p_plat_type {
    P_PLAT_PERPETUAL_RAISE = 0,
    P_PLAT_DOWN_WAIT_UP_STAY = 1,
    P_PLAT_RAISE_AND_CHANGE = 2,
    P_PLAT_RAISE_TO_NEAREST_AND_CHANGE = 3,
    P_PLAT_BLAZE_DWUS = 4
} p_plat_type_t;

/* plat_t (p_spec.h). `status`/`oldStatus` are p_plat_status_t; `wait` and
 * `count` are tics. */
typedef struct p_plat_thinker {
    p_thinker_t thinker;
    uint16_t sector;
    int16_t type;
    int16_t status;
    int16_t oldStatus;
    int16_t crush;
    int16_t tag;
    fixed_t speed;
    fixed_t low;
    fixed_t high;
    int32_t wait;
    int32_t count;
} p_plat_thinker_t;

/**
 * @brief EV_DoPlat(): starts a plat on every sector with the line's tag.
 *
 * `amount` is used only by `raiseAndChange` (14/15/66/67 in P_UseSpecialLine),
 * exactly as upstream. `perpetualRaise` first resumes every in-stasis plat with
 * the same tag, which is why a second switch press restarts a stopped lift.
 *
 * @return 1 when at least one thinker was created, 0 otherwise.
 */
int p_do_plat(struct p_mobj_context* world, uint16_t lineIndex,
              p_plat_type_t type, int amount);

/**
 * @brief EV_StopPlat(): puts every non-stasis plat with the line's tag in stasis.
 *
 * The thinker is left in the list with a NULL function, so it neither moves nor
 * is removed; `p_do_plat(P_PLAT_PERPETUAL_RAISE)` resumes it at its old status.
 *
 * @return 1 when at least one plat was stopped, 0 otherwise.
 */
int p_stop_plat(struct p_mobj_context* world, uint16_t lineIndex);

/** T_PlatRaise()'s entry point. */
void p_plat_thinker(p_thinker_t* thinker, void* context);

/** P_ActivateInStasis(tag), exposed for the tests and the oracle's parity. */
int p_activate_in_stasis_plats(struct p_mobj_context* world, int16_t tag);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_P_PLATS_H */
