/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * Line of sight and sound propagation: P_CheckSight(), its BSP walk, the
 * REJECT pre-test, and P_NoiseAlert()'s sector flood.
 *
 * WHAT THIS LAYER IS
 *
 * Upstream keeps six file-scope variables for a sight check (`strace`, `t2x`,
 * `t2y`, `sightzstart`, `topslope`, `bottomslope`) and one `validcount` shared
 * with the sound flood. This port has no globals, so the same values live in the
 * check's own locals and the sound flood's counter lives in the level's context
 * (`p_mobj_context_t::soundValidCount`).
 *
 * THE REJECT PRE-TEST IS NOT AN OPTIMISATION TO SKIP
 *
 * linuxdoom-1.10's P_CheckSight() first asks the map's REJECT table whether the
 * two sectors can possibly see each other, and only then walks the BSP. The
 * table is a bit per ordered sector pair. The port reproduces the rejection and
 * counts it (`p_sight_stats_t::rejectHits`), because a map whose REJECT is wrong
 * must fail the same way it fails upstream instead of being papered over by a
 * longer walk. A missing or short REJECT lump is not a rejection: it is counted
 * as `rejectMissing` and the BSP walk answers alone, which is what a zero-length
 * table means.
 *
 * THE BSP WALK, AND ONE DELIBERATE OMISSION
 *
 * P_CrossSubsector() marks each linedef it tests with the global `validcount` so
 * a line shared by two subsectors is tested once. This port does not keep a
 * second array for that mark: the line test has no side effects beyond the two
 * slope bounds, so testing a shared line twice computes the same two bounds and
 * cannot change the verdict. The omission is stated here rather than left as a
 * silent difference; the alternative is 475 * 4 B of arena for a count nobody
 * reads.
 *
 * P_DivlineSide(): FAITHFUL TO A 1993 TYPO
 *
 * The vertical-partition branch reads
 *
 *     if (x==node->y) return 2;
 *
 * where every other branch compares the coordinate it was given (`y`). It is
 * harmless for the partitions DOOM's node builder produces (a vertical partition
 * has a constant `x`, and the typo compares `x` with `y`) but it is part of the
 * rule, so both this file and tools/doom_ref.py keep it. Changing it would make
 * the port disagree with the game it is copying on exactly the geometry that
 * expression was written for.
 *
 * SOUND: P_RecursiveSound AND THE PER-SECTOR STATE
 *
 * `p_sector_sound_t` (p_spec.h) is one sector's sound state: the mobj the
 * sector last heard, how many sound-blocking lines the flood crossed to reach
 * it, and the flood's epoch. p_level_init() allocates and zeroes the array from
 * the level arena; `p_mobj_context_t` only points at it. p_noise_alert() starts
 * the flood at the emitter's sector and p_recursive_sound() crosses every
 * two-sided line whose opening is positive, bounded by `soundTraversed` so a
 * cycle cannot loop and by `ML_SOUNDBLOCK` so a sound-blocking line is crossed
 * at most once.
 *
 * A sector's neighbours are found by scanning LINEDEFS for two-sided lines that
 * touch it -- the pattern p_spec.h documents for every surrounding-sector query.
 * There is no per-sector line list and no second adjacency table.
 *
 * This is C99 only: <stdint.h>, no allocation inside a call, no globals, no
 * stdio, no floating point.
 */
#ifndef DOOM_P_SIGHT_H
#define DOOM_P_SIGHT_H

#include <stdint.h>

#include "p_mobj.h"
#include "p_spec.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The eye height: `sightzstart = t1->z + t1->height - (t1->height>>2)`, i.e. the
 * looker's eyes sit three quarters of the way up its body. Overridable so
 * tools/enemy_check.py --perturb can prove the comparison fails; no shipped
 * build defines it.
 */
#ifndef P_SIGHT_EYE_SHIFT
#define P_SIGHT_EYE_SHIFT 2
#endif

/*
 * What p_check_sight() did. All counts are per level; the harness prints them so
 * "the REJECT table was consulted" and "the BSP walk answered" are measurable
 * facts and not claims about the source.
 *
 *   checks        calls that had a world, two mobjs and a map
 *   rejectHits    REJECT named the sector pair blocked (the BSP was not walked)
 *   rejectMissing the map's REJECT lump is absent or shorter than the pair table
 *   bspWalks      checks that reached the BSP walk
 *   bspBlocked    BSP walks that found an obstruction
 *   accepted      BSP walks that found a clear line
 */
typedef struct p_sight_stats {
    uint32_t checks;
    uint32_t rejectHits;
    uint32_t rejectMissing;
    uint32_t bspWalks;
    uint32_t bspBlocked;
    uint32_t accepted;
} p_sight_stats_t;

/**
 * @brief P_CheckSight(): is a straight line from @p t1 to @p t2 unobstructed?
 *
 * The REJECT pair is tested first, then upstream's P_CrossBSPNode() walk from
 * the map's head node. The looker's eyes are `t1->z + t1->height -
 * (t1->height>>P_SIGHT_EYE_SHIFT)`; the target is judged over its whole height
 * (`topslope`/`bottomslope` from its z to z+height). A two-sided line whose
 * opening leaves no gap, or whose slope window closes, blocks the line.
 *
 * The map's mutable sector heights are read through p_line_opening() (p_map.c),
 * so a door that opened for the simulation also lets sight through it.
 *
 * @param world The level's context: map, mutable sectors, optional counters.
 * @param t1    The looker (its `sector`, `z` and `height` are read).
 * @param t2    The target.
 * @return 1 when the line is clear, 0 when anything blocks it or an argument is
 *         unusable. A NULL world/mobj or a closed map is 0 and is not counted.
 */
int p_check_sight(p_mobj_context_t* world, const r_mobj_t* t1,
                  const r_mobj_t* t2);

/**
 * @brief P_NoiseAlert(): flood the sound from @p emitter's sector.
 *
 * `soundtarget = target; validcount++; P_RecursiveSound(emmiter's sector, 0);`
 * in upstream's order. The flood writes `soundTarget`, `soundTraversed` and
 * `validCount` in the level's p_sector_sound_t array; a sector reached through
 * `ML_SOUNDBLOCK` lines is marked so it cannot be crossed again.
 *
 * A context with no sound array (a hand-built unit-test context) makes this a
 * no-op: there is nowhere to record the alert.
 *
 * @param world   The level's context.
 * @param target  Who was heard (the player, for a weapon).
 * @param emitter Whose sector starts the flood (the player, for a weapon).
 */
void p_noise_alert(p_mobj_context_t* world, r_mobj_t* target,
                   r_mobj_t* emitter);

/**
 * @brief The mobj a sector last heard, or NULL.
 *
 * This is upstream's `sector->soundtarget` read. A_Look() is the only caller in
 * the shipped game; the harness reads it to report the flood's own answer.
 *
 * @param world  The level's context.
 * @param sector The sector index.
 * @return The sound target, or NULL when the sector was never flooded or the
 *         context has no sound array.
 */
r_mobj_t* p_sector_sound_target(const p_mobj_context_t* world, uint16_t sector);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_P_SIGHT_H */
