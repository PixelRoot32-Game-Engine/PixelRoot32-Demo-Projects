/*
 * Floors: T_MovePlane (the one stepper every mover uses) and EV_DoFloor.
 *
 * The reference is linuxdoom-1.10 p_floor.c: T_MovePlane, T_MoveFloor and
 * EV_DoFloor. `T_MovePlane` is the only function in the port that writes a
 * sector height; every door, lift and ceiling calls it, so its clamping,
 * overshoot and crush rules exist once.
 *
 * THE RULES, AND WHERE EACH COMES FROM (p_floor.c T_MovePlane)
 *
 *   - A step that would pass the destination is clamped to it and answers
 *     `pastdest`, whether it overshoots by one unit or by a thousand. The
 *     comparison is `height - speed < dest` going down and `height + speed >
 *     dest` going up, so landing exactly on the destination takes the stepping
 *     branch and answers `ok`; the next tic then clamps by zero.
 *   - The write happens *before* P_ChangeSector(), because that function reads
 *     the new height. If it reports "does not fit", the old height is written
 *     back and the hook is called again, and the answer is `crushed`.
 *   - A floor moving *up* with `crush == true` returns `crushed` and leaves the
 *     new height in place (the things being crushed stay crushed). A floor
 *     moving up without crush reverts. A ceiling moving down has the same pair.
 *   - A ceiling moving *up* never reverts: upstream's branch is `#if 0`.
 *   - The overshoot branch never answers `crushed`, even when the hook reports
 *     a problem, because the revert has already happened. That is upstream's
 *     comment `//return crushed;`.
 *
 * P_ChangeSector() ITSELF IS D4.2's. Here it is p_spec_p_change_sector(), a
 * counted "the sector is clear"; see p_spec.h. The call sites are upstream's,
 * so the counter is the proof the hook is on every write path.
 *
 * THE CACHED HEIGHT RULE
 *
 * A mobj copies floorz/ceilingz at spawn (p_spawn_mobj). After a height write
 * settles, p_move_plane() calls p_spec_refresh_things() for that sector, which
 * rewrites those two fields on every mobj standing in it. That is how decision
 * (1) keeps the sector array and the spawn-time cache from disagreeing.
 *
 * This is C99 only: no allocation, no globals, no stdio, no floating point.
 */
#ifndef DOOM_P_FLOOR_H
#define DOOM_P_FLOOR_H

#include <stdint.h>

#include "m_fixed.h"
#include "p_spec.h"
#include "p_tick.h"

#ifdef __cplusplus
extern "C" {
#endif

struct p_mobj_context;

/*
 * result_e (p_spec.h), in upstream's order and with its meanings:
 *
 *   P_MOVE_OK        the plane moved and has not arrived
 *   P_MOVE_CRUSHED   something is in the way and the move was undone (or, for a
 *                    crush move, left in place)
 *   P_MOVE_PASTDEST  the plane arrived at its destination this tic
 */
typedef enum p_move_result {
    P_MOVE_OK = 0,
    P_MOVE_CRUSHED = 1,
    P_MOVE_PASTDEST = 2
} p_move_result_t;

/* floor_e (p_spec.h), in upstream's order. The numbers are the dump protocol
 * and the oracle's table, so they are not free to renumber. */
typedef enum p_floor_type {
    P_FLOOR_LOWER = 0,
    P_FLOOR_LOWER_TO_LOWEST = 1,
    P_FLOOR_TURBO_LOWER = 2,
    P_FLOOR_RAISE = 3,
    P_FLOOR_RAISE_TO_NEAREST = 4,
    P_FLOOR_RAISE_TO_TEXTURE = 5,
    P_FLOOR_LOWER_AND_CHANGE = 6,
    P_FLOOR_RAISE_24 = 7,
    P_FLOOR_RAISE_24_AND_CHANGE = 8,
    P_FLOOR_RAISE_CRUSH = 9,
    P_FLOOR_RAISE_TURBO = 10,
    P_FLOOR_DONUT_RAISE = 11,
    P_FLOOR_RAISE_512 = 12
} p_floor_type_t;

/* floormove_t (p_spec.h), with the sector as an index into p_sector_t[].
 *
 * `texture` and `newspecial` are the two fields lowerAndChange and donutRaise
 * write when they arrive; `destHeight` is `floordestheight`; `crush` is the
 * boolean that changes T_MovePlane's answer on a blocked raise. */
typedef struct p_floor_thinker {
    p_thinker_t thinker;
    uint16_t sector;
    int16_t type;
    int16_t direction;
    int16_t crush;
    int16_t newspecial;
    r_name_t texture;
    fixed_t destHeight;
    fixed_t speed;
} p_floor_thinker_t;

/**
 * @brief T_MovePlane(): moves one floor or ceiling toward @p dest.
 *
 * Reproduces p_floor.c T_MovePlane exactly, including the write-then-check
 * order, the revert, and the ceiling-up branch that never reverts. `speed` is a
 * magnitude; @p direction is +1 or -1.
 *
 * @param world         The level context; its sector array is written.
 * @param sector        Sector index.
 * @param speed         Units per tic, positive.
 * @param dest          Destination height, 16.16.
 * @param crush         Nonzero for a move that may crush instead of reverting.
 * @param floorOrCeiling 0 for floor, 1 for ceiling (upstream's parameter).
 * @param direction     +1 up, -1 down.
 * @return P_MOVE_OK, P_MOVE_CRUSHED or P_MOVE_PASTDEST. A NULL context or an
 *         out-of-range sector answers P_MOVE_OK and changes nothing.
 */
p_move_result_t p_move_plane(struct p_mobj_context* world, uint16_t sector,
                             fixed_t speed, fixed_t dest, int crush,
                             int floorOrCeiling, int direction);

/**
 * @brief EV_DoFloor(): starts a floor move on every sector with the line's tag.
 *
 * `if (sec->specialdata) continue;` is upstream's "already moving, keep going"
 * rule: a second trigger on the same sector creates no second thinker and does
 * not touch the first. That is the merge rule for floors.
 *
 * `P_FLOOR_RAISE_TO_TEXTURE` needs `textureheight[]` (p_floor.c), which this
 * layer does not hold: the call is counted in `deferredLineSpecial` and no
 * thinker is created. Every other type is implemented.
 *
 * @return 1 when at least one thinker was created, 0 otherwise.
 */
int p_do_floor(struct p_mobj_context* world, uint16_t lineIndex,
               p_floor_type_t type);

/**
 * @brief EV_DoDonut() (p_spec.c): the donut raise/lower pair.
 *
 * Kept with the floors because it creates two p_floor_thinker_t records. The
 * upstream loop's `s2->lines[0]` assumption is replaced by the same
 * neighbour scan the rest of this layer uses; the pair of heights it computes is
 * unchanged.
 *
 * @return 1 when a pair was created, 0 otherwise.
 */
int p_do_donut(struct p_mobj_context* world, uint16_t lineIndex);

/** T_MoveFloor()'s entry point. */
void p_floor_thinker(p_thinker_t* thinker, void* context);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_P_FLOOR_H */
