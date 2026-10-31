/*
 * Movement and collision: the thing-versus-world tests, the move that follows
 * them, the slide along a wall, the height clip and the use trace.
 *
 * THE REFERENCE
 *
 * linuxdoom-1.10 p_map.c (P_CheckPosition, P_TryMove, P_SlideMove,
 * P_HitSlideLine, P_ThingHeightClip, P_UseLines, P_ChangeSector) and
 * p_maputl.c (P_PointOnLineSide, P_BoxOnLineSide, P_LineOpening,
 * P_InterceptVector). Every non-obvious rule cites the upstream statement it
 * reproduces. This is C99 only: no allocation, no globals, no stdio, no
 * floating point.
 *
 * THERE IS NO BLOCKMAP
 *
 * doom_map.h documents that DOOM1's BLOCKMAP lump is never read: decoding it
 * would cost heap this board does not have. Upstream reaches lines and things
 * through P_BlockLinesIterator/P_BlockThingsIterator. This port replaces both
 * with a brute-force walk over every LINEDEF (E1M1 has 475) and every mobj in
 * thinker order (E1M1 spawns about 92), then applies upstream's own tests:
 * PIT_CheckLine's bounding-box rejection and P_BoxOnLineSide, and PIT_CheckThing's
 * `radius + radius` box test.
 *
 * The candidate *set* is a superset of the blockmap's -- the map's own
 * coordinates can put a line in no block, but never in the wrong one -- and the
 * *result* is identical because the tests that decide it are unchanged. Only
 * the iteration order differs, and neither PIT_CheckLine nor PIT_CheckThing
 * depends on order: heights are max/min and blocking is a boolean. P_UseLines is
 * the one place order could matter, and it is documented at its definition.
 *
 * WHAT LIVES ON THE STACK
 *
 * Upstream keeps tmfloorz, tmceilingz, tmdropoffz and spechit[] as globals. The
 * port has no globals, so one p_position_result_t (about 32 bytes) carries them
 * and p_try_move puts the special-line list in it. P_ChangeSector's sector box
 * is four fixed_t on the stack. No file-scope array is added.
 *
 * ONE NAME THAT CANNOT BE REUSED
 *
 * The brief called this result p_move_result_t, but p_floor.h already gives that
 * name to T_MovePlane's result_e (P_MOVE_OK/P_MOVE_CRUSHED/P_MOVE_PASTDEST). A
 * second type with the same name cannot compile, so the position check's result
 * is p_position_result_t and the two meanings stay distinguishable.
 */
#ifndef DOOM_P_MAP_H
#define DOOM_P_MAP_H

#include <stdint.h>

#include "m_fixed.h"
#include "p_mobj.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Upstream constants --------------------------------------------------- */

/* p_local.h's MAXMOVE: one tic's momentum is clamped to 30 map units, which is
 * why a thing cannot tunnel through a wall one linedef thick. */
#define P_MAXMOVE (30 * FRACUNIT)

/* p_local.h's USERANGE and MELEERANGE: the reach of a use press. */
#define P_USERANGE (64 * FRACUNIT)

/* p_mobj.c's local STOPSPEED and FRICTION (p_local.h's ORIG_FRICTION). Below
 * STOPSPEED in both axes a walking thing is stopped outright; above it the
 * momentum is scaled by ORIG_FRICTION each tic.
 *
 * linuxdoom-1.10 has exactly one friction constant. FRICTION_FLY belongs to
 * later ports and to things that fly; this P_XYMovement returns before the
 * friction block whenever `z > floorz`, so no second constant is used. */
#define P_STOPSPEED 0x1000
#ifndef P_ORIG_FRICTION
#define P_ORIG_FRICTION 0xe800
#endif

/* p_local.h's GRAVITY and FLOATSPEED, and p_map.c's 24-unit step. */
#define P_GRAVITY FRACUNIT
#define P_FLOATSPEED (4 * FRACUNIT)
#define P_STEPUP (24 * FRACUNIT)

/* p_map.c's MAXSPECIALCROSS. Upstream writes past the array when more lines are
 * hit in one move; the port stops collecting and raises `lineOverflow`, which
 * no real move reaches (a 32-unit box cannot touch nine linedefs). */
#define P_MAXSPECIALCROSS 8

/* p_local.h's MAXRADIUS: the blockmap iterator widens a box by it because a
 * thing's origin can sit one block away from its body. The brute-force walk
 * does not need it for the thing test, but P_ChangeSector uses it to widen the
 * moving sector's block box, which is what upstream's sector->blockbox already
 * carries. */
#define P_MAXRADIUS (128 * FRACUNIT)

/* m_bbox.h's box order, used by the line tests. */
#define P_BOXTOP 0
#define P_BOXBOTTOM 1
#define P_BOXLEFT 2
#define P_BOXRIGHT 3

/* ML_BLOCKMONSTERS. doom_map.h lists only the flags the renderer tests, and it
 * is not this task's surface, so the one simulation flag the move tests need is
 * named here. Its value is the 1993 linedef flag word's bit 4. */
#define P_ML_BLOCKMONSTERS 16

/* --- What a position check found ----------------------------------------- */

/*
 * The four numbers and the list upstream keeps in globals:
 *
 *   floorz      tmfloorz: the highest floor any contacted line exposes.
 *   ceilingz    tmceilingz: the lowest ceiling any contacted line exposes.
 *   dropoffz    tmdropoffz: the lowest floor contacted, which is the drop the
 *               thing would stand over.
 *   subsector   newsubsec: the subsector the checked point fell in, resolved
 *               once so P_TryMove does not walk the BSP a second time.
 *   sector      that subsector's sector.
 *   lines       spechit[]: the indices of the special lines contacted, in the
 *               order they were found (LINEDEF order here).
 *   lineCount   numspechit, capped at P_MAXSPECIALCROSS.
 *   lineOverflow 1 when more special lines were contacted than the list holds.
 *   ceilingLine the line that lowered tmceilingz, or P_NO_LINE.
 *   hasCeilingLine 1 when ceilingLine is a real index. P_XYMovement's missile
 *               branch asks it whether the ceiling is the sky.
 */
#define P_NO_LINE 0xFFFFu

typedef struct p_position_result {
    fixed_t floorz;
    fixed_t ceilingz;
    fixed_t dropoffz;
    uint16_t subsector;
    uint16_t sector;
    uint16_t lines[P_MAXSPECIALCROSS];
    uint16_t ceilingLine;
    uint8_t lineCount;
    uint8_t lineOverflow;
    uint8_t hasCeilingLine;
} p_position_result_t;

/* One linedef's opening, as p_line_opening() resolves it. */
typedef struct p_line_opening {
    fixed_t openTop;
    fixed_t openBottom;
    fixed_t openRange;
    fixed_t lowFloor;
} p_line_opening_t;

/*
 * The counters a caller can read without a debugger. They are optional: the
 * world context carries a pointer, NULL by default, so the firmware pays one
 * pointer and the harness gets the numbers the MOVEMENT protocol prints.
 *
 *   checks   p_check_position() calls (one per P_TryMove and per height clip)
 *   blocked  P_TryMove refusals, including the ones a slide then recovers from
 *   crossed  P_CrossSpecialLine invocations from a move (a side changed)
 *   slides   P_SlideMove calls
 */
typedef struct p_move_stats {
    uint32_t checks;
    uint32_t blocked;
    uint32_t crossed;
    uint32_t slides;
} p_move_stats_t;

/* --- Public API ---------------------------------------------------------- */

/**
 * @brief P_AproxDistance(): DOOM's cheap distance, not the Euclidean one.
 *
 * p_maputl.c: `dx+dy-(dy>>1)` with the larger axis treated as the hypotenuse's
 * proxy. P_ZMovement's float rule and P_HitSlideLine's length both use it, so it
 * is shared rather than repeated.
 */
fixed_t p_aprox_distance(fixed_t dx, fixed_t dy);

/**
 * @brief P_LineOpening(): the window a two-sided line leaves between its sides.
 *
 * `openTop` is the lower of the two ceilings, `openBottom` the higher of the
 * two floors, `lowFloor` the lower floor, and `openRange` their difference.
 * A single-sided line answers `openRange == 0` and leaves the other three at
 * zero, which is what every caller tests (`openrange <= 0` is "a wall").
 *
 * Upstream leaves the three fields stale for a single-sided line because its
 * callers check PTR_UseTraverse's `openrange` or have already rejected a
 * one-sided line. This port writes all four so an out-parameter is never
 * half-initialised; the only caller that reads them, PTR_SlideTraverse's
 * successor, has already rejected a one-sided line.
 *
 * @param world The level's context (its mutable sectors are the truth).
 * @param lineIndex The linedef index.
 * @param out Filled; zeroed when the index is not a line.
 */
void p_line_opening(const p_mobj_context_t* world, uint16_t lineIndex,
                    p_line_opening_t* out);

/**
 * @brief P_CheckPosition(): can a thing stand at (x, y), and over what?
 *
 * Upstream's order, which matters because a blocking thing stops the check
 * before any line is examined:
 *
 *   1. the base floor/ceiling is the floor/ceiling of the subsector the point
 *      falls in (R_PointInSubsector), not of the thing's current sector;
 *   2. every thing in the world, with PIT_CheckThing's box test;
 *   3. every LINEDEF, with PIT_CheckLine's box rejection, P_BoxOnLineSide, the
 *      one-sided rejection, ML_BLOCKING and ML_BLOCKMONSTERS, and the opening.
 *
 * Nothing is modified except the counters. A refused check still fills
 * `subsector`/`sector` and the heights found so far, because P_ThingHeightClip
 * uses them after a "blocked" answer.
 *
 * @return 1 when the position is free, 0 when a thing or a line refuses it.
 */
int p_check_position(p_mobj_context_t* world, r_mobj_t* mobj, fixed_t x,
                     fixed_t y, p_position_result_t* out);

/**
 * @brief P_TryMove(): the position check, the fit tests, the relink and the
 *        line crossings.
 *
 * After p_check_position() succeeds, upstream refuses a move that:
 *
 *   - leaves less room than the thing's height between floor and ceiling;
 *   - would put the thing's head through a ceiling (`tmceilingz - z < height`,
 *     unless MF_TELEPORT);
 *   - steps up more than 24 map units (unless MF_TELEPORT);
 *   - stands over a drop of more than 24 units (unless MF_DROPOFF or
 *     MF_FLOAT).
 *
 * MF_NOCLIP skips all four. The move then writes x, y, floorz and ceilingz,
 * re-resolves the subsector/sector the check already found, and runs every
 * collected special line whose side the thing changed, nearest-last as
 * upstream's `while (numspechit--)` does.
 *
 * @return 1 when the thing moved, 0 when the move was refused.
 */
int p_try_move(p_mobj_context_t* world, r_mobj_t* mobj, fixed_t x, fixed_t y);

/**
 * @brief P_SlideMove(): move up to the wall, then along it.
 *
 * Upstream traces three rays from the thing's leading and trailing corners,
 * takes the nearest blocking line on any of them, moves to just short of it,
 * projects the remaining momentum onto the line's direction (P_HitSlideLine)
 * and retries. After three attempts it falls back to the axis-aligned
 * "stairstep". The rays are walked over every LINEDEF here instead of through
 * the blockmap; a line crossed by a ray is the same line either way.
 *
 * Ties at exactly equal intercept fractions are the only possible divergence
 * from upstream's blockmap order: upstream keeps the first line the block walk
 * offered, this port keeps the first in LINEDEF order. Two distinct lines at
 * one 16.16 fraction is a measure-zero coincidence, and sorting the candidates
 * would be a second deviation rather than a fix.
 */
void p_slide_move(p_mobj_context_t* world, r_mobj_t* mobj);

/**
 * @brief P_ThingHeightClip(): follow the floor/ceiling under a thing.
 *
 * Re-runs P_CheckPosition at the thing's own position, copies the two heights,
 * then puts a thing that was standing on the floor back on it, or -- for one
 * that was floating -- pushes it down only if its head no longer fits.
 *
 * @return 1 when the thing still fits after the clip, 0 when it does not.
 */
int p_thing_height_clip(p_mobj_context_t* world, r_mobj_t* mobj);

/**
 * @brief P_ChangeSector(): adjust every thing the moving sector can touch.
 *
 * Upstream walks the sector's block box. The port has no blockmap and no
 * per-sector box stored, so it computes the box from the linedefs that touch
 * the sector (the same box P_GroupLines stores), widens it by MAXRADIUS, and
 * walks every mobj whose origin falls inside. That is a superset of the
 * blockmap's set and a strict subset of "every thing", so a mobj in a distant
 * sector is not scanned on every door step.
 *
 * PIT_ChangeSector's rules, in order:
 *
 *   - a thing that still fits after P_ThingHeightClip is left alone;
 *   - a dead thing becomes S_GIBS, loses MF_SOLID and its box;
 *   - an MF_DROPPED item is removed;
 *   - a non-shootable thing (gibs) is ignored;
 *   - anything else does not fit: the sector reports it and the caller undoes
 *     or keeps the move.
 *
 * The crushing *damage* is P_DamageMobj's, which p_inter.c owns and D4.2d has
 * not written yet. Instead of silently dropping it, each thing that upstream
 * would damage raises `specStats->unsupportedCrush`, so the gap is a number.
 *
 * @return 1 when at least one thing does not fit, 0 when the sector is clear.
 */
int p_change_sector(p_mobj_context_t* world, uint16_t sector);

/**
 * @brief P_UseLines(): use the nearest special line in front of a thing.
 *
 * The trace is USERANGE (64 map units) along the thing's angle. Upstream walks
 * the blockmap's intercepts in ascending distance and stops at the first
 * special line; a non-special line whose opening is closed stops the trace
 * ("can't use through a wall"). This port selects the intercepts from every
 * LINEDEF by repeated minimum, which visits them in the same ascending order
 * without an intercept array.
 *
 * The side upstream passes to P_UseSpecialLine (P_PointOnLineSide of the user)
 * is computed here for the record; this port's dispatch has no side argument,
 * so the line is triggered for every crossing the trace reaches.
 */
void p_use_lines(p_mobj_context_t* world, r_mobj_t* mobj);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_P_MAP_H */
