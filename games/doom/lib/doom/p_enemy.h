/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * The enemy actions this port has written: A_Look and the player scan behind it,
 * then A_Chase and the attack actions -- P_Move/P_TryWalk/
 * P_NewChaseDir, P_CheckMeleeRange/P_CheckMissileRange, A_FaceTarget,
 * A_PosAttack, A_TroopAttack and A_Explode. What upstream's p_enemy.c has that
 * this port does not is boss behaviour, the Arch-vile, the lost soul's flight and
 * the other monsters' attack shapes; p_action.c's default branch still counts
 * them.
 *
 * WHAT A_Look DOES, IN UPSTREAM'S ORDER
 *
 *   1. `actor->threshold = 0;` -- "any shot will wake up".
 *   2. read the sector's `soundtarget`; if it is an MF_SHOOTABLE mobj it becomes
 *      the actor's target. An MF_AMBUSH actor must still see it (P_CheckSight)
 *      before waking; a non-ambush actor wakes on the sound alone.
 *   3. otherwise P_LookForPlayers(actor, false): the four-slot scan upstream
 *      writes, reduced to the one slot this port simulates.
 *   4. on waking: the see sound is chosen by upstream's switch (one draw for the
 *      two zombie families, none otherwise) and started; the actor enters
 *      `mobjinfo[type].seestate`.
 *
 * WHAT A_Chase DOES, IN UPSTREAM'S ORDER
 *
 *   1. `reactiontime--`; the target threshold decays (or clears when the target
 *      is gone or dead).
 *   2. the 45-degree turn: `angle &= 7<<29`, then one 22.5-degree step toward
 *      `movedir`, which is why a chasing monster's facing is an octant and not
 *      the exact bearing.
 *   3. a lost or dead target: re-scan the players, else back to the spawn state.
 *   4. MF_JUSTATTACKED: clear it, pick a direction, return -- no two attacks in
 *      a row.
 *   5. melee if the type has a meleestate and P_CheckMeleeRange; otherwise a
 *      missile when `movecount` reached zero (or on nightmare) and
 *      P_CheckMissileRange passes; on a missile, set MF_JUSTATTACKED.
 *   6. walk: `--movecount` or P_NewChaseDir when the step is blocked.
 *   7. the active sound: one P_Random() draw, and the sound only when it is
 *      below 3, exactly upstream. Both E1M1 enemies name an active sound, so
 *      every A_Chase draws one.
 *
 * THE WALK SPEED IS AN INTEGER
 *
 * A walker's mobjinfo `speed` is 8 map units, not 8*FRACUNIT, and P_Move
 * multiplies it by the 16.16 xspeed[]/yspeed[] table with a plain integer
 * product. A missile's speed is 16.16 and P_SpawnMissile uses FixedMul. Both
 * shapes are upstream's.
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
 * On a wake the actor's `target` is set and it enters `mobjinfo[type].seestate`,
 * whose own action is A_Chase, so the monster starts moving on the same tic it
 * wakes.
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

/**
 * @brief P_CheckMeleeRange(): inside 64 units less 20, widened by the target's
 *        radius, and in sight.
 *
 * `dist >= MELEERANGE - 20*FRACUNIT + pl->info->radius` refuses; then
 * P_CheckSight decides. A NULL target is refused.
 *
 * @return 1 when the target is in reach.
 */
int p_check_melee_range(p_mobj_context_t* world, r_mobj_t* actor);

/**
 * @brief P_CheckMissileRange(): sight, a fresh hit, reaction time, then a draw.
 *
 * SIGHT first; an MF_JUSTHIT actor clears the flag and attacks at once; a
 * nonzero `reactiontime` refuses. Otherwise the distance minus 64 units (minus
 * another 128 for a type with no melee state) is shifted to map units, capped at
 * 200, and compared with one P_Random() draw: below it, no shot.
 *
 * @return 1 when the actor may fire on this call.
 */
int p_check_missile_range(p_mobj_context_t* world, r_mobj_t* actor);

/**
 * @brief P_NewChaseDir(): choose `movedir` and take the first step.
 *
 * The direct diagonal, then the two axis directions (randomly swapped when the
 * draw is above 200 or the Y delta is larger), then the old direction, then a
 * hunt around the eight directions in a random rotation, then the turnaround.
 * Every candidate is tried with P_TryWalk, so a call that succeeds has already
 * moved the actor and re-armed `movecount`.
 */
void p_new_chase_dir(p_mobj_context_t* world, r_mobj_t* actor);

/**
 * @brief A_Chase(): the run-state action. See the header comment for the order.
 *
 * A blocked step does not open a door in this port; see p_enemy.c.
 */
void p_enemy_chase(p_mobj_context_t* world, r_mobj_t* actor);

/**
 * @brief A_FaceTarget(): clear MF_AMBUSH and point at the target exactly.
 *
 * An MF_SHADOW target adds `(P_Random()-P_Random())<<21` so a fuzzy player is
 * not tracked perfectly. No target is a no-op.
 */
void p_enemy_face_target(p_mobj_context_t* world, r_mobj_t* actor);

/**
 * @brief A_PosAttack(): the zombieman's hitscan pistol.
 *
 * A_FaceTarget, aim, one random pair of drift, `((P_Random()%5)+1)*3` damage,
 * through p_line_attack(). The pistol sound is the sound layer's (s_sound.c).
 */
void p_enemy_pos_attack(p_mobj_context_t* world, r_mobj_t* actor);

/**
 * @brief A_SPosAttack(): the shotgun guy's three-shot spread.
 *
 * Upstream's order, which is three interleaved draws per pass:
 *
 *   A_FaceTarget;
 *   base = actor->angle; slope = P_AimLineAttack(actor, base, MISSILERANGE);
 *   for (i = 0; i < 3; ++i) {
 *       angle = base + ((P_Random()-P_Random())<<20);
 *       damage = ((P_Random()%5)+1)*3;
 *       P_LineAttack(actor, angle, MISSILERANGE, slope, damage);
 *   }
 *
 * The aim is taken once, on the facing, and every pass re-spreads from that
 * base: `angle` is not accumulated across passes. The nine draws are part of a
 * demo's sequence even though the shotgun sound is the sound layer's.
 *
 * @param world The level's context (random sequence, attack trace).
 * @param actor The shotgun guy; no target is a no-op.
 */
void p_enemy_spos_attack(p_mobj_context_t* world, r_mobj_t* actor);

/**
 * @brief A_SargAttack(): the demon's and spectre's melee bite.
 *
 * `A_FaceTarget (actor); if (P_CheckMeleeRange (actor)) { damage =
 * ((P_Random()%10)+1)*4; P_DamageMobj (actor->target, actor, actor, damage); }`
 *
 * The draw happens only after P_CheckMeleeRange succeeds, so a bite that misses
 * consumes nothing. S_StartSound belongs to the sound layer and draws nothing.
 *
 * @param world The level's context.
 * @param actor The demon or spectre; no target is a no-op.
 */
void p_enemy_sarg_attack(p_mobj_context_t* world, r_mobj_t* actor);

/**
 * @brief A_TroopAttack(): the imp's claw inside melee range, else a fireball.
 *
 * The claw rolls `((P_Random()%8)+1)*3` and damages directly; the fireball is
 * p_spawn_missile(MT_TROOPSHOT).
 */
void p_enemy_troop_attack(p_mobj_context_t* world, r_mobj_t* actor);

/**
 * @brief A_Explode(): `p_radius_attack (actor, actor->target, 128)`.
 *
 * The imp's fireball burst rows are inert (its damage is the direct missile
 * hit); the barrel's S_BEXP chain and a rocket's S_EXPLODE1 carry A_Explode.
 */
void p_enemy_explode(p_mobj_context_t* world, r_mobj_t* actor);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_P_ENEMY_H */
