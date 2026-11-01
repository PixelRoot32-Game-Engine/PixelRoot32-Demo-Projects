/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * Movement and collision: the thing-versus-world tests, the move that follows
 * them, the slide along a wall, the height clip, the use trace, and the shot.
 *
 * THE REFERENCE
 *
 * linuxdoom-1.10 p_map.c (P_CheckPosition, P_TryMove, P_SlideMove,
 * P_HitSlideLine, P_ThingHeightClip, P_UseLines, P_ChangeSector, P_AimLineAttack,
 * P_LineAttack, P_ShootSpecialLine and the two traversers) and p_maputl.c
 * (P_PointOnLineSide, P_BoxOnLineSide, P_LineOpening, P_InterceptVector,
 * P_PathTraverse, PIT_AddLineIntercepts, PIT_AddThingIntercepts,
 * P_TraverseIntercepts). Every non-obvious rule cites the upstream statement it
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
 * depends on order: heights are max/min and blocking is a boolean. P_UseLines and
 * the attack path are the two places order could matter, and each is documented at
 * its definition.
 *
 * WHAT LIVES ON THE STACK
 *
 * Upstream keeps tmfloorz, tmceilingz, tmdropoffz and spechit[] as globals. The
 * port has no globals, so one p_position_result_t (about 32 bytes) carries them
 * and p_try_move puts the special-line list in it. P_ChangeSector's sector box
 * is four fixed_t on the stack. No file-scope array is added.
 *
 * The attack path adds the one larger frame in this file: P_AimLineAttack and
 * P_LineAttack each hold one attack_trace_t, which is upstream's intercepts[]
 * array (P_MAXINTERCEPTS entries of 12 bytes) plus the eight numbers upstream
 * keeps in globals -- 768 B and change on the 32-bit target. A shot is never
 * re-entered, so one frame is all there is; p_map.c states the arithmetic and
 * why an array beats the repeated-minimum walk P_UseLines uses.
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

/* p_local.h's USERANGE and MELEERANGE: the reach of a use press and of a melee
 * swing. Upstream defines both as 64 map units -- two names for one number,
 * because a punch and a use press happen to reach the same -- and this port keeps
 * both names so a call site still says which rule it is following. */
#define P_USERANGE (64 * FRACUNIT)
#define P_MELEERANGE (64 * FRACUNIT)

/* p_local.h's MISSILERANGE: `32*64*FRACUNIT`, so a bullet's or a rocket's trace
 * is 2,048 map units. Overridable so tools/attack_check.py can perturb it and
 * prove its comparison fails; no shipped build defines it. */
#ifndef P_MISSILERANGE
#define P_MISSILERANGE (32 * 64 * FRACUNIT)
#endif

/* p_map.c's `topslope = 100*FRACUNIT/160` and its negative: "can't shoot outside
 * view angles". The two bounds a trace may not aim past, in 16.16. */
#define P_AIM_TOP_SLOPE (100 * FRACUNIT / 160)

/* Upstream's MAXINTERCEPTS. The port's trace keeps its intercepts in one fixed
 * array on the stack (see p_map.c); 64 entries are 768 B on the 32-bit target and
 * a 2,048-unit trace across E1M1 crosses far fewer than that. An overflow stops
 * collection and raises a counter, where upstream writes past its array. */
#define P_MAXINTERCEPTS 64


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

/*
 * The same idea for the attack path: what a shot's trace did, for a gate or a
 * log. Optional, and a NULL pointer costs nothing -- the firmware passes NULL,
 * tools/attack_check.c sets one.
 *
 *   traces            p_aim_line_attack() and p_line_attack() calls
 *   intercepts        line and thing intercepts collected, over all traces
 *   interceptOverflow traces that stopped collecting because P_MAXINTERCEPTS was
 *                     reached (upstream writes past its array; this counts)
 *   skyHits           shot ends the sky hack refused to put a puff on
 *   specialsShot      special lines a shot triggered
 *   zeroDistance      a plane or thing slope skipped because the intercept's
 *                     distance was 0 (upstream divides by zero there)
 */
typedef struct p_attack_stats {
    uint32_t traces;
    uint32_t intercepts;
    uint32_t interceptOverflow;
    uint32_t skyHits;
    uint32_t specialsShot;
    uint32_t zeroDistance;
} p_attack_stats_t;

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
 * @brief R_PointToAngle2() from (0,0) to (@p dx, @p dy): the binary angle.
 *
 * The octant-normalised arc tangent through `r_tan_to_angle[]`, with upstream's
 * exact quadrant arithmetic (`ANG90 - 1 - tac` and friends). P_HitSlideLine and
 * P_DamageMobj's thrust both need it, so it is public: the orientation of a
 * hit's push is part of the damage rule, not of the renderer.
 *
 * R_PointToAngle2(x1, y1, x2, y2) is this function applied to (x2-x1, y2-y1).
 */
angle_t p_point_to_angle(fixed_t dx, fixed_t dy);

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
 * @brief P_TeleportMove(): the teleport write, without the line checks.
 *
 * Upstream's P_TeleportMove is deliberately *not* P_TryMove: it takes the base
 * floor and ceiling from the destination's subsector, runs only
 * PIT_StompThing over the things already there, then links the thing at the new
 * position. No line is examined, so a teleport destination inside a wall is not
 * refused -- the map is trusted, which is what upstream does.
 *
 * PIT_StompThing, in upstream's order:
 *
 *   1. only MF_SHOOTABLE things are candidates;
 *   2. `blockdist = thing->radius + tmthing->radius`, and a candidate farther
 *      than that on either axis is not hit;
 *   3. the teleporting thing itself is never a candidate;
 *   4. `if (!tmthing->player && gamemap != 30) return false;` -- a monster does
 *      not stomp, it refuses the teleport. This port loads DOOM1's episode 1,
 *      whose maps are gamemap 1..9, so the `gamemap != 30` half is always true
 *      and the refusal is named here rather than faked;
 *   5. otherwise P_DamageMobj(10000) with the teleporter as inflictor and
 *      source, which is what makes a teleport onto a player a kill.
 *
 * @param world The level context; its map supplies the BSP and its RNG is not
 *              drawn (P_SpawnMobj is the caller's, not this function's).
 * @param mobj  The thing being moved; its floorz/ceilingz/subsector/sector are
 *              rewritten from the destination.
 * @param x,y   Destination, 16.16.
 * @return 1 when the thing moved, 0 when a thing refused it.
 */
int p_teleport_move(p_mobj_context_t* world, r_mobj_t* mobj, fixed_t x,
                    fixed_t y);

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
 *   - anything else does not fit: the sector reports it, and -- when the move is
 *     allowed to crush -- it takes P_CRUSH_DAMAGE every fourth level tic and
 *     sprays a blood mobj in a random direction.
 *
 * The damage is `if (crushchange && !(leveltime&3)) { P_DamageMobj (thing, NULL,
 * NULL, 10); ... MT_BLOOD ... }`: no inflictor, so no thrust, and no source, so
 * a monster's death by crushing is credited to player 0 by P_KillMobj's
 * environment branch. `crushChange` is T_MovePlane's `crush` argument, which is
 * why every mover forwards its own flag: a door closing on a player reports
 * "does not fit" and is undone, while a crushing floor keeps the height and
 * applies the damage.
 *
 * A thing already marked for removal is skipped: upstream's P_RemoveMobj unlinks
 * it from the blockmap, and this walk visits the thinker list, where the node
 * survives until the next p_run_thinkers(). Without the test a picked-up item
 * would be touched twice in one tic.
 *
 * @param world       The level's context: zone, RNG, player record, sectors.
 * @param sector      The sector whose plane just moved.
 * @param crushChange Nonzero when the moving plane may crush instead of
 *                    reverting.
 * @return 1 when at least one thing does not fit, 0 when the sector is clear.
 */
int p_change_sector(p_mobj_context_t* world, uint16_t sector, int crushChange);

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

/* --- Shooting and aiming ------------------------------------------------- */

/**
 * @brief P_AimLineAttack(): the slope a shot at @p angle would hit at.
 *
 * Upstream's body:
 *
 *   `shootz = t1->z + (t1->height>>1) + 8*FRACUNIT;` -- the eye is the thing's
 *   mid-height plus eight units, *not* the player's viewz, so a monster aims the
 *   same way a player does;
 *   `topslope = 100*FRACUNIT/160; bottomslope = -100*FRACUNIT/160;`
 *   `attackrange = distance; linetarget = NULL;`
 *   `P_PathTraverse (t1->x, t1->y, x2, y2, PT_ADDLINES|PT_ADDTHINGS,
 *    PTR_AimTraverse);`
 *   `if (linetarget) return aimslope; return 0;`
 *
 * The port has no pitch: when nothing is found the slope is 0, which is vanilla's
 * own answer and the only one this port can produce (there is no `lookdir`).
 *
 * `x2 = t1->x + (distance>>FRACBITS)*finecosine[angle]` is upstream's integer
 * product, not a FixedMul, and it is kept: at 2,048 map units the product is
 * 134,217,728, which still fits a 32-bit int.
 *
 * @param world    The level's context.
 * @param shooter  The mobj aiming.
 * @param angle    The direction of the trace.
 * @param distance How far the trace reaches.
 * @param target   Optional out: what the trace found, or NULL.
 * @return The slope, or 0 when nothing was hit.
 */
fixed_t p_aim_line_attack(p_mobj_context_t* world, r_mobj_t* shooter,
                          angle_t angle, fixed_t distance, r_mobj_t** target);

/**
 * @brief P_LineAttack(): trace a shot and resolve what it hits.
 *
 * `shootthing = t1; la_damage = damage; ...; shootz = t1->z + (t1->height>>1) +
 * 8*FRACUNIT; attackrange = distance; aimslope = slope;` then the traverser.
 *
 * The traverser's three rules, in upstream's order:
 *
 *   - a line with a special runs P_ShootSpecialLine first, and a shot triggers
 *     only for a player (p_spec.c owns the trigger rules);
 *   - a one-sided line is hit; a two-sided line is hit when its opening puts a
 *     floor or a ceiling in the shot's way (`slope > aimslope` for the floor,
 *     `slope < aimslope` for the ceiling), otherwise the shot continues;
 *   - a hit line puts a puff four units short of it, unless the sky hack refuses
 *     (`frontsector->ceilingpic == skyflatnum` above the ceiling, or a sky on both
 *     sides) -- "don't shoot the sky!";
 *   - a thing that is MF_SHOOTABLE and inside the aim band takes a puff when it
 *     is MF_NOBLOOD and a spray of blood otherwise, ten units short of it, and
 *     then `P_DamageMobj (th, shootthing, shootthing, la_damage)` when the damage
 *     is not zero ("If damage == 0, it is just a test trace").
 *
 * The puff and the blood are p_mobj.c's; the damage is p_inter.c's. Both are
 * handed the *shot's* range so P_SpawnPuff can tell a melee spark from a bullet
 * one (upstream reads the `attackrange` global; p_mobj.h says why that is a
 * parameter here).
 *
 * @param world    The level's context: zone, RNG, sectors, player record.
 * @param shooter  The mobj firing.
 * @param angle    The direction of the shot.
 * @param distance How far the shot reaches.
 * @param slope    The aim slope, from p_aim_line_attack() or 0.
 * @param damage   Points on a hit; 0 makes the call a pure trace.
 */
void p_line_attack(p_mobj_context_t* world, r_mobj_t* shooter, angle_t angle,
                   fixed_t distance, fixed_t slope, int damage);

/**
 * @brief P_ShootSpecialLine(): a hitscan hit a special line.
 *
 * Upstream `if (!thing->player) return;` then a switch on the line's special that
 * runs the EV function and `P_ChangeSwitchTexture` for the arms that flip one.
 * This port's dispatch is p_spec.c's `p_trigger_special_line (..., P_TRIGGER_SHOOT,
 * ...)`, which owns both the trigger rules and the switch-texture bookkeeping it
 * counts as `deferredLineSpecial`; the player test is passed through as `byPlayer`.
 *
 * @param world     The level's context.
 * @param shooter   The mobj whose shot hit the line.
 * @param lineIndex The linedef index.
 */
void p_shoot_special_line(p_mobj_context_t* world, r_mobj_t* shooter,
                          uint16_t lineIndex);

/**
 * @brief P_RadiusAttack(): damage everything shootable within @p damage units.
 *
 * Upstream's PIT_RadiusAttack, per thing: MF_SHOOTABLE only, no damage for the
 * boss spider or cyborg, `dist = max(|dx|, |dy|) - thing->radius` shifted to map
 * units and clamped at zero, refuse when `dist >= damage`, then require
 * P_CheckSight(thing, spot) before `P_DamageMobj(thing, spot, source,
 * damage - dist)`.
 *
 * The blockmap walk is a thinker-list walk here; the distance test decides, not
 * the box, so the set of victims is the same. A NULL spot or world does nothing.
 *
 * @param world  The level's context.
 * @param spot   The explosion's centre (the exploding mobj).
 * @param source Whoever the explosion is credited to, or NULL.
 * @param damage The blast's strength in points of damage at zero distance.
 */
void p_radius_attack(p_mobj_context_t* world, r_mobj_t* spot,
                     r_mobj_t* source, int damage);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_P_MAP_H */
