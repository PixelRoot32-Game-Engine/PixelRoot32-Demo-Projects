/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * Doors: EV_DoDoor, EV_VerticalDoor and T_VerticalDoor.
 *
 * The reference is linuxdoom-1.10 p_doors.c. A door is a ceiling-height move
 * (T_MovePlane with `floorOrCeiling == 1`) driven by a small state machine on
 * the p_door_thinker_t record.
 *
 * THE THREE DIRECTION VALUES, AND THE FOURTH
 *
 *   direction  1  moving up
 *   direction  0  waiting at the top; `topCountdown` is what ends the wait
 *   direction -1  moving down
 *   direction  2  the initial five-minute wait of P_SpawnDoorRaiseIn5Mins
 *
 * THE MERGE RULES (p_doors.c)
 *
 *   EV_DoDoor (a tag-triggered door) skips a sector whose `specialdata` is set:
 *   `if (sec->specialdata) continue;`. A second open on the same sector creates
 *   no second thinker and does not touch the first.
 *
 *   EV_VerticalDoor (a manual door) instead reuses the moving door for line
 *   specials 1, 26, 27, 28 and 117: if it is going down it goes back up, else a
 *   player turns it around immediately. A non-player may never turn a door
 *   down ("JDC: bad guys never close doors"). For the other manual numbers
 *   upstream allocates a new door and overwrites `specialdata`, orphaning the
 *   old thinker; this port refuses instead and counts
 *   `triggersRefused`, because orphaning a thinker is a leak, not a rule.
 *
 * LOCKS
 *
 * Upstream checks the player's key cards for 26/27/28 and 31/32/33/34. This port
 * has no inventory yet (D5), so a locked manual door opens like an unlocked one
 * and the gate never depends on it. The line-trigger dispatch is where the key
 * check belongs once cards exist.
 *
 * This is C99 only: no allocation, no globals, no stdio, no floating point.
 */
#ifndef DOOM_P_DOORS_H
#define DOOM_P_DOORS_H

#include <stdint.h>

#include "m_fixed.h"
#include "p_spec.h"
#include "p_tick.h"

#ifdef __cplusplus
extern "C" {
#endif

struct p_mobj_context;

/* vldoor_e (p_spec.h), in upstream's order. The values are the dump protocol
 * and the oracle's table. */
typedef enum p_door_type {
    P_DOOR_NORMAL = 0,
    P_DOOR_CLOSE_30_THEN_OPEN = 1,
    P_DOOR_CLOSE = 2,
    P_DOOR_OPEN = 3,
    P_DOOR_RAISE_IN_5_MINS = 4,
    P_DOOR_BLAZE_RAISE = 5,
    P_DOOR_BLAZE_OPEN = 6,
    P_DOOR_BLAZE_CLOSE = 7
} p_door_type_t;

/* vldoor_t (p_spec.h). `topHeight` is the height the up move stops at;
 * `topWait` is the tics spent waiting at the top; `topCountdown` is the live
 * counter (the wait, or the initial five minutes). */
typedef struct p_door_thinker {
    p_thinker_t thinker;
    uint16_t sector;
    int16_t type;
    int16_t direction;
    fixed_t topHeight;
    fixed_t speed;
    int32_t topWait;
    int32_t topCountdown;
} p_door_thinker_t;

/**
 * @brief EV_DoDoor(): starts a door move on every sector with the line's tag.
 *
 * The type selects the initial direction, speed and top height exactly as
 * p_doors.c does. `close`/`blazeClose` and `close30ThenOpen` start downward;
 * the other types open. A sector already moving is skipped (the merge rule).
 *
 * @return 1 when at least one thinker was created, 0 otherwise.
 */
int p_do_door(struct p_mobj_context* world, uint16_t lineIndex,
              p_door_type_t type);

/**
 * @brief EV_VerticalDoor(): the manual door of the line's back sector.
 *
 * The sector is `sides[line->sidenum[1]].sector`, which is what upstream's
 * `side = 0; ... sidenum[side^1]` resolves to. `byPlayer` is the only thing the
 * merge and lock rules need from the mobj that used the line.
 *
 * @return 1 when a door moved or was created, 0 when refused.
 */
int p_vertical_door(struct p_mobj_context* world, uint16_t lineIndex,
                    int byPlayer);

/**
 * @brief P_SpawnDoorCloseIn30(): sector special 10.
 *
 * Creates a `normal` door in the waiting state with a 35*30 tic countdown. When
 * the countdown ends it starts down (T_VerticalDoor, direction 0).
 */
int p_spawn_door_close_in_30(struct p_mobj_context* world, uint16_t sector);

/**
 * @brief P_SpawnDoorRaiseIn5Mins(): sector special 14.
 *
 * Creates a `raiseIn5Mins` door in direction 2; after 5*60*35 tics it becomes a
 * `normal` door moving up. `secnum` is not needed (upstream takes it and does
 * not use it), so this signature leaves it out.
 */
int p_spawn_door_raise_in_5_mins(struct p_mobj_context* world, uint16_t sector);

/** T_VerticalDoor()'s entry point. */
void p_door_thinker(p_thinker_t* thinker, void* context);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_P_DOORS_H */
