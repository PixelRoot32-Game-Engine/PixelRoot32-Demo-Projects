/*
 * Ceilings: EV_DoCeiling, T_MoveCeiling, EV_CeilingCrushStop.
 *
 * The reference is linuxdoom-1.10 p_ceilng.c. A ceiling is a T_MovePlane move on
 * the ceiling plane; the interesting part is which types raise again after
 * reaching the bottom, and how a blocked move changes the speed.
 *
 * THE CRUSH-AND-RAISE FAMILY
 *
 *   fastCrushAndRaise     starts down at CEILSPEED*2, bottom = floor + 8; on
 *                         arrival goes back up without changing speed.
 *   crushAndRaise         starts down at CEILSPEED, bottom = floor + 8; on
 *                         arrival sets CEILSPEED and goes back up.
 *   silentCrushAndRaise   the same as crushAndRaise, plus the fall-through that
 *                         also sets CEILSPEED; the "silent" half is the missing
 *                         start-move sound, which this layer does not have.
 *   lowerAndCrush         bottom = floor + 8, no raise; removes itself.
 *   lowerToFloor          bottom = floor, no raise; removes itself.
 *   raiseToHighest        top = highest ceiling surrounding; removes itself.
 *
 * A blocked down move (`crushed`) slows crushAndRaise, silentCrushAndRaise and
 * lowerAndCrush to CEILSPEED/8; every other type keeps its speed. A ceiling
 * that reaches its destination during an *up* move is never blocked, because
 * p_move_plane's ceiling-up branch does not revert (p_floor.c).
 *
 * STASIS
 *
 * `EV_CeilingCrushStop` keeps the ceiling in the list with a NULL function and
 * `direction = 0`; `EV_DoCeiling` with a crush type first calls
 * `P_ActivateInStasisCeiling`, which restores `oldDirection`. Upstream finds the
 * ceilings through `activeceilings[]`; this port walks the thinker list, which
 * holds the same records (p_plats.h documents the same choice for plats).
 *
 * This is C99 only: no allocation, no globals, no stdio, no floating point.
 */
#ifndef DOOM_P_CEILNG_H
#define DOOM_P_CEILNG_H

#include <stdint.h>

#include "m_fixed.h"
#include "p_spec.h"
#include "p_tick.h"

#ifdef __cplusplus
extern "C" {
#endif

struct p_mobj_context;

/* ceiling_e (p_spec.h), in upstream's order. The values are the dump protocol. */
typedef enum p_ceiling_type {
    P_CEIL_LOWER_TO_FLOOR = 0,
    P_CEIL_RAISE_TO_HIGHEST = 1,
    P_CEIL_LOWER_AND_CRUSH = 2,
    P_CEIL_CRUSH_AND_RAISE = 3,
    P_CEIL_FAST_CRUSH_AND_RAISE = 4,
    P_CEIL_SILENT_CRUSH_AND_RAISE = 5
} p_ceiling_type_t;

/* ceiling_t (p_spec.h). `tag` is the line tag that started it, which is what
 * EV_CeilingCrushStop and the in-stasis resume match on. */
typedef struct p_ceiling_thinker {
    p_thinker_t thinker;
    uint16_t sector;
    int16_t type;
    int16_t direction;
    int16_t crush;
    int16_t tag;
    int16_t oldDirection;
    fixed_t bottomHeight;
    fixed_t topHeight;
    fixed_t speed;
} p_ceiling_thinker_t;

/**
 * @brief EV_DoCeiling(): starts a ceiling move on every sector with the line's tag.
 *
 * For the three crush types, first resumes every in-stasis ceiling with the same
 * tag (`P_ActivateInStasisCeiling`). A sector already moving is skipped.
 *
 * @return 1 when at least one thinker was created, 0 otherwise.
 */
int p_do_ceiling(struct p_mobj_context* world, uint16_t lineIndex,
                 p_ceiling_type_t type);

/**
 * @brief EV_CeilingCrushStop(): stops every moving ceiling with the line's tag.
 *
 * Sets `oldDirection`, clears the thinker's function and sets direction 0.
 * @return 1 when at least one ceiling was stopped, 0 otherwise.
 */
int p_ceiling_crush_stop(struct p_mobj_context* world, uint16_t lineIndex);

/** T_MoveCeiling()'s entry point. */
void p_ceiling_thinker(p_thinker_t* thinker, void* context);

/** P_ActivateInStasisCeiling(line's tag). */
int p_activate_in_stasis_ceilings(struct p_mobj_context* world, int16_t tag);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_P_CEILNG_H */
