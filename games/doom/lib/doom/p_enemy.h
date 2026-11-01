/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * The enemy actions this port has written: A_Look() and the player scan behind
 * it. A_Chase() and everything that needs it are T15b's and are still counted by
 * p_action.c's "not implemented" branch.
 *
 * WHAT A_Look DOES, IN UPSTREAM'S ORDER
 *
 *   1. `actor->threshold = 0;` -- "any shot will wake up".
 *   2. read the sector's `soundtarget`; if it is an MF_SHOOTABLE mobj it becomes
 *      the actor's target. An MF_AMBUSH actor must still see it (P_CheckSight)
 *      before waking; a non-ambush actor wakes on the sound alone.
 *   3. otherwise P_LookForPlayers(actor, false): the four-slot scan upstream
 *      writes, reduced to the one slot this port simulates.
 *   4. on waking: the see sound is *discarded* (S2 has no audio layer) and the
 *      actor enters `mobjinfo[type].seestate`.
 *
 * THE SEE-SOUND DRAW IS KEPT
 *
 * Upstream chooses the see sound with one P_Random() draw for the zombie and
 * sergeant families (`sfx_posit1 + P_Random()%3`, `sfx_bgsit1 + P_Random()%2`)
 * and no draw for everything else. The sound cannot be played here, but the draw
 * is part of the random sequence a demo replays, so it is performed and thrown
 * away. "The wake path must never depend on a sound" is therefore true: the
 * wake happens first, and the draw is a consequence of the wake, not a
 * condition of it.
 *
 * WHY THE PLAYER SCAN IS ONE SLOT
 *
 * linuxdoom-1.10 walks `playeringame[0..3]` and checks `players[slot].health` and
 * `players[slot].mo`. This port spawns and simulates exactly one console player
 * (`p_level_t::player` / `playerState`, slot 0), while `p_spawn_rules_t` still
 * carries the four `playerInGame` flags the level loaded with. The loop keeps
 * upstream's `lastlook` rotation, its `stop` early exit and its two-player cap
 * (`c++ == 2`); slots above 0 are skipped because they have no mobj, which is the
 * same answer upstream gives a slot that is not in the game.
 *
 * This is C99 only: <stdint.h>, no allocation, no globals, no stdio, no floating
 * point.
 */
#ifndef DOOM_P_ENEMY_H
#define DOOM_P_ENEMY_H

#include "p_mobj.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A_Look(): stay in the spawn state until a player is heard or seen.
 *
 * The actor's sector sound target wins over the sight scan, exactly as upstream:
 * a shot in a neighbouring room wakes a non-ambush monster even when the player
 * is behind a wall. An MF_AMBUSH monster must still pass P_CheckSight.
 *
 * On a wake the actor's `target` is set and it enters `mobjinfo[type].seestate`;
 * A_Chase (the see state's action) is T15b's, so the actor animates its run
 * frames in place rather than moving.
 *
 * @param world The level's context (random sequence, sound array, level).
 * @param actor The monster; a NULL actor or world is a no-op.
 */
void p_enemy_look(p_mobj_context_t* world, r_mobj_t* actor);

/**
 * @brief P_LookForPlayers(): scan the player slots for a visible player.
 *
 * Upstream's loop verbatim, over this port's one player: the `lastlook` rotation
 * (`(lastlook+1)&3`), the `stop = (lastlook-1)&3` early exit, the two-slot cap,
 * the health and P_CheckSight tests, and the 180-degree cone test when
 * @p allaround is false (`if (an > ANG90 && an < ANG270)` and farther than
 * MELEERANGE is "behind back").
 *
 * @param world     The level's context; its `level` supplies the player.
 * @param actor     The looker; its `angle` and `lastlook` are read and updated.
 * @param allaround Nonzero to skip the facing test (A_Chase uses that).
 * @return 1 and `actor->target` set when a player was found; 0 otherwise.
 */
int p_look_for_players(p_mobj_context_t* world, r_mobj_t* actor, int allaround);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_P_ENEMY_H */
