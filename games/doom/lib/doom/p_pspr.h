/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * The weapon sprite state machine: the two overlay slots, the per-weapon state
 * numbers, and the actions upstream keeps in p_pspr.c.
 *
 * THE REFERENCE
 *
 * linuxdoom-1.10 p_pspr.h (`psprnum_t`, `pspdef_t`), p_pspr.c (P_SetPsprite,
 * P_SetupPsprites, P_MovePsprites, P_CalcSwing, P_BringUpWeapon, P_CheckAmmo,
 * P_FireWeapon, P_DropWeapon, P_BulletSlope, P_GunShot and every A_ function in
 * that file) and d_items.c's `weaponinfo[]`. Every non-obvious rule below cites
 * the upstream statement it reproduces. This is C99 only: no allocation, no
 * globals, no stdio, no floating point.
 *
 * WHY THIS HEADER SITS BELOW p_user.h
 *
 * Upstream's player_t holds `pspdef_t psprites[NUMPSPRITES]`, and pspdef_t is
 * declared here. p_user.h therefore includes this header before it defines
 * p_player_t, and nothing here may include p_user.h back. The player record is
 * therefore a *tag* declared below and taken by pointer -- the same trick p_mobj.h
 * already uses for `struct p_level` -- and so is the level. p_mobj.h *is* included,
 * because the attack assets name `r_mobj_t` and `p_mobj_context_t` and p_mobj.h
 * does not include this header. The weapon slot is an `int32_t` holding a
 * p_weapon_t, for the same reason p_user.h stores `pendingweapon` as an int32_t:
 * this header must not need that enumerator.
 *
 * WHAT THIS LAYER OWNS AND WHAT IT DOES NOT
 *
 * Owning: the state machine, the ammo a shot spends, the muzzle flash's light
 * level, and the drawing-free half of every weapon action. The coordinates
 * `sx`/`sy` are computed exactly as upstream computes them because they are
 * part of the state the weapon renderer (r_psprite.c) reads, and because A_Lower's and A_Raise's
 * transitions are decided by them.
 *
 * NOT owning, and named rather than dropped:
 *
 *   - the weapon sprite itself. Nothing here touches a pixel; r_psprite.c draws
 *     the weapon from `sx`/`sy`.
 *   - audio. Every `S_StartSound` the weapon actions make is counted in
 *     p_player_t::silentSounds and, under DOOM_AUDIO, played at the same site
 *     through DOOM_SOUND_START. The counter is what the attack gates assert;
 *     the call is what the APU hears.
 *   - P_NoiseAlert. p_sight.c owns the flood; P_FireWeapon calls it and still counts
 *     the call in p_player_t::noiseAlerts, so "a shot woke the neighbours" and
 *     "the call was reached" are separately visible.
 *   - A_Saw, A_FireShotgun2, A_BFGSpray and A_BFGsound. They are the chainsaw and
 *     the super shotgun, which no shareware level can hand the player, and the
 *     BFG's spray, which needs P_CheckSight. p_pspr.c routes them to
 *     p_action.c's psprite dispatcher, which counts them as not implemented, so
 *     the gap is a number and not a silence. Their states are still in the table
 *     above, and the psprite machine walks them.
 *
 * THE STATE NUMBER IS THE STATE
 *
 * Upstream keeps `state_t* state` and tests it against `&states[S_SAW]`; this
 * port keeps the statenum_t index because the table is data (p_action.h says
 * why). `S_NULL` (0) is upstream's NULL: "a NULL state means not active".
 */
#ifndef DOOM_P_PSPR_H
#define DOOM_P_PSPR_H

#include <stdint.h>

#include "info_tables.h"
#include "m_fixed.h"
#include "p_mobj.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The one tag this header only takes by pointer; see "WHY THIS HEADER SITS BELOW
 * p_user.h". It is declared in full by p_user.h, which includes this header. */
struct p_player;

/* --- Upstream constants --------------------------------------------------- */

/* p_pspr.c's LOWERSPEED, RAISESPEED and the two screen positions. The speeds are
 * overridable so tools/attack_check.py can perturb one and prove the comparison
 * fails; no shipped build defines them. */
#ifndef P_LOWERSPEED
#define P_LOWERSPEED (FRACUNIT * 6)
#endif
#ifndef P_RAISESPEED
#define P_RAISESPEED (FRACUNIT * 6)
#endif

/* `#define WEAPONBOTTOM 128*FRACUNIT` and `#define WEAPONTOP 32*FRACUNIT`. These
 * sit inside P_WEAPONTOP's guard because the raise target is what the reduce and
 * the flash share; no shipped build defines either. */
#define P_WEAPONBOTTOM (128 * FRACUNIT)
#ifndef P_WEAPONTOP
#define P_WEAPONTOP (32 * FRACUNIT)
#endif

/* `#define BFGCELLS 40`: a BFG shot costs 40 cells, which is why P_CheckAmmo
 * asks for more than the one round every other weapon needs. */
#define P_BFGCELLS 40

/* d_items.h's psprnum_t, with upstream's order. */
typedef enum p_psprnum {
    PS_WEAPON = 0,
    PS_FLASH = 1,
    P_NUMPSPRITES = 2
} p_psprnum_t;

/*
 * pspdef_t: one overlay layer.
 *
 *   state  the statenum_t the layer is showing; S_NULL means "not active",
 *          which is upstream's NULL pointer.
 *   tics   the state's remaining tics. -1 never advances (upstream:
 *          `if (psp->tics != -1)`).
 *   sx, sy screen coordinates, 16.16. Upstream's comment: "coordinates are given
 *          for a 320*200 view screen". r_psprite.c draws from them, and
 *          A_WeaponReady, A_Lower, A_Raise and P_BringUpWeapon all write them,
 *          so they are state the machine decides and a test can see.
 *
 * Upstream's state_t also carries `misc1`/`misc2`, which P_SetPsprite uses to set
 * sx/sy from the state row. Every one of the 967 shipped rows has both zero
 * (info.c's only explicit pair, on S_TECH2LAMP4, is `0,0`), so the branch is
 * unreachable and the two columns are not carried.
 */
typedef struct p_pspdef {
    int16_t state;
    int32_t tics;
    fixed_t sx;
    fixed_t sy;
} p_pspdef_t;

/*
 * One record of d_items.c's weaponinfo[], in upstream's field order:
 *
 *   ammo         the pool a shot spends; p_ammo_t::P_AM_NOAMMO for the fist and
 *                the chainsaw, and the one column p_inter.c's P_GiveWeapon reads.
 *   upState      the raise chain's first state (P_BringUpWeapon).
 *   downState    the lower chain's first state (A_WeaponReady's change branch,
 *                P_CheckAmmo's "out of ammo" and P_DropWeapon).
 *   readyState   the bobbing idle state; A_Raise ends there.
 *   attackState  what P_FireWeapon sets the weapon psprite to.
 *   flashState   what the A_Fire* actions set the flash psprite to, and what
 *                A_GunFlash sets it to.
 *
 * The chain is a `0` for a weapon with no flash (the fist and the chainsaw), and
 * P_SetPsprite treats that as "deactivate", which is upstream's S_NULL.
 */
typedef struct p_weaponinfo {
    int8_t ammo;
    int16_t upState;
    int16_t downState;
    int16_t readyState;
    int16_t attackState;
    int16_t flashState;
} p_weaponinfo_t;

/* --- Public API ---------------------------------------------------------- */

/**
 * @brief d_items.c's weaponinfo[] record for @p weapon, or NULL.
 *
 * `@p weapon` is a p_weapon_t as an int32_t (see the header comment); a value
 * outside 0..P_NUMWEAPONS-1 answers NULL rather than reading past the table.
 */
const p_weaponinfo_t* p_weapon_info(int32_t weapon);

/* --- P_SetPsprite and the setup ------------------------------------------ */

/**
 * @brief P_SetPsprite(): enter a state on one overlay layer, running its action.
 *
 * Upstream's loop, reproduced statement for statement:
 *
 *   do {
 *       if (!stnum) { psp->state = NULL; break; }
 *       state = &states[stnum];
 *       psp->state = state;
 *       psp->tics = state->tics;          // could be 0
 *       if (state->misc1) { psp->sx = ...; psp->sy = ...; }   // unreachable here
 *       if (state->action.acp2) { action(player, psp); if (!psp->state) break; }
 *       stnum = psp->state->nextstate;
 *   } while (!psp->tics);
 *
 * So an action runs on *entry* to its state, a zero-tic state is never visible
 * and its action still runs, and an action that deactivates the layer ends the
 * chain. The port adds one bound: a table whose zero-tic states formed a cycle
 * would spin forever, so after 256 iterations the call stops. No real chain is
 * longer than two states.
 *
 * The action dispatch is p_pspr.c's for the twelve weapon actions it implements
 * and p_action.c's `p_action_run_psprite()` for the rest, so an unimplemented one
 * (A_Saw, A_FireShotgun2, A_BFGSpray, A_BFGsound) is counted exactly as an
 * unimplemented mobj action is.
 *
 * @param world    The level's context: the RNG, the zone and the mobjs a shot
 *                 touches. NULL is refused.
 * @param player   The player record. NULL is refused.
 * @param position PS_WEAPON or PS_FLASH; anything else is refused.
 * @param stnum    The state to enter; S_NULL deactivates the layer.
 */
void p_set_psprite(p_mobj_context_t* world, struct p_player* player,
                   int position, statenum_t stnum);

/**
 * @brief P_SetupPsprites(): clear both layers, then raise the ready weapon.
 *
 * `for (i=0 ; i<NUMPSPRITES ; i++) player->psprites[i].state = NULL;` then
 * `player->pendingweapon = player->readyweapon; P_BringUpWeapon (player);`.
 *
 * Upstream calls this from G_PlayerReborn, which this port models as the
 * header-only p_player_wire() so that p_setup.c keeps linking without this file.
 * The call itself therefore lands on the first live P_PlayerThink (see
 * p_player_think) and on any caller that wants the weapon up at load time, as
 * tools/attack_check.c and test_world do. Both paths run this one function, so
 * the psprite timeline is upstream's either way: the setup runs A_Raise once
 * before the first P_MovePsprites.
 *
 * @param world  The level's context, for the raise chain's actions.
 * @param player The player record. NULL is refused.
 */
void p_setup_psprites(p_mobj_context_t* world, struct p_player* player);

/**
 * @brief P_MovePsprites(): advance both layers one tic and sync the flash.
 *
 * `for each layer: if (state) { if (tics != -1) { tics--; if (!tics)
 * P_SetPsprite (player, i, state->nextstate); } }` and then
 *
 *   `player->psprites[ps_flash].sx = player->psprites[ps_weapon].sx;`
 *   `player->psprites[ps_flash].sy = player->psprites[ps_weapon].sy;`
 *
 * The flash inherits the weapon's coordinates every tic, which is why a muzzle
 * flash follows the gun without a second animation.
 */
void p_move_psprites(p_mobj_context_t* world, struct p_player* player);

/**
 * @brief P_CalcSwing(): the weapon's horizontal swing offsets.
 *
 * `swing = player->bob; angle = (FINEANGLES/70*leveltime)&FINEMASK;
 *  swingx = FixedMul (swing, finesine[angle]);
 *  angle = (FINEANGLES/70*leveltime+FINEANGLES/2)&FINEMASK;
 *  swingy = -FixedMul (swingx, finesine[angle]);`
 *
 * Upstream keeps `swingx`/`swingy` as file-scope globals because the renderer
 * reads them. This port has no globals, so the two numbers are an
 * out-parameter: the rule is here so the value exists, and the only caller is a
 * test.
 *
 * @param level  The level, for `leveltime`; NULL means tic 0.
 * @param player The player record, for `bob`.
 * @param swingx Filled with upstream's swingx.
 * @param swingy Filled with upstream's swingy.
 */
void p_calc_swing(const struct p_level* level, const struct p_player* player,
                  fixed_t* swingx, fixed_t* swingy);

/* --- P_BringUpWeapon, P_CheckAmmo, P_FireWeapon, P_DropWeapon ------------ */

/**
 * @brief P_BringUpWeapon(): start raising the pending weapon from the bottom.
 *
 * `if (pendingweapon == wp_nochange) pendingweapon = readyweapon;` then
 * `newstate = weaponinfo[pendingweapon].upstate; pendingweapon = wp_nochange;
 * psprites[ps_weapon].sy = WEAPONBOTTOM; P_SetPsprite (player, ps_weapon,
 * newstate);`
 *
 * The chainsaw's `S_StartSound (sfx_sawup)` is counted in `silentSounds` and
 * played through the sound layer.
 */
void p_bring_up_weapon(p_mobj_context_t* world, struct p_player* player);

/**
 * @brief P_CheckAmmo(): is there enough ammo to shoot; if not, pick another.
 *
 * `count` is P_BFGCELLS for the BFG, 2 for the super shotgun and 1 otherwise, and
 * a weapon whose pool is P_AM_NOAMMO always answers yes. Out of ammo, the
 * preference order is upstream's: chaingun with clips, shotgun with shells, the
 * pistol if there are clips at all, the chainsaw, the missile launcher with
 * rockets, then the fist. The plasma, super-shotgun and BFG arms are upstream's
 * `gamemode` tests, and this port loads DOOM 1 shareware, where
 * all three are false; they are named in p_pspr.c rather than written.
 *
 * @return 1 when the ready weapon can fire, 0 when it cannot -- in which case
 *         the weapon psprite was sent down its lower chain and `pendingweapon`
 *         names the replacement.
 */
int p_check_ammo(p_mobj_context_t* world, struct p_player* player);

/**
 * @brief P_FireWeapon(): the player's one fire entry point.
 *
 * `if (!P_CheckAmmo (player)) return; P_SetMobjState (player->mo, S_PLAY_ATK1);
 * P_SetPsprite (player, ps_weapon, weaponinfo[readyweapon].atkstate);
 * P_NoiseAlert (player->mo, player->mo);`
 *
 * P_NoiseAlert is p_sight.c's flood: it starts at the player's sector,
 * crosses every open two-sided line and stops at a closed door, so a monster
 * behind a wall but in an open room hears the shot while one behind a closed
 * door does not. The call is also counted in `noiseAlerts`.
 */
void p_fire_weapon(p_mobj_context_t* world, struct p_player* player);

/**
 * @brief P_DropWeapon(): the player died, so put the weapon away.
 *
 * `P_SetPsprite (player, ps_weapon, weaponinfo[readyweapon].downstate);` -- the
 * lower chain, which stops at WEAPONBOTTOM because A_Lower sees PST_DEAD. It
 * drops no item: upstream's comment is "Player died, so put the weapon away", and
 * the mobj P_KillMobj leaves behind is a *monster's* clip, not the player's gun.
 */
void p_drop_weapon(p_mobj_context_t* world, struct p_player* player);

/* --- The attack assets --------------------------------------------------- */

/**
 * @brief P_BulletSlope(): the slope a bullet is aimed along.
 *
 * `an = mo->angle; bulletslope = P_AimLineAttack (mo, an, 16*64*FRACUNIT);` and
 * if nothing was found, the same widened by +-ANG90/4 (`an += 1<<26` then
 * `an -= 2<<26`), which is how a near miss lands at about the height of the
 * thing being aimed at.
 *
 * The port has no `bulletslope` global and no `linetarget` global: @p target
 * receives what the last trace found, which is what A_Punch needs and what the
 * port's P_GunShot does not.
 *
 * @param world    The level's context.
 * @param shooter  The mobj firing.
 * @param target   Optional out: the thing the last aim trace found, or NULL.
 * @return The slope, 0 when nothing was aimed at (vanilla's "no pitch" answer).
 */
fixed_t p_bullet_slope(p_mobj_context_t* world, r_mobj_t* shooter,
                       r_mobj_t** target);

/**
 * @brief P_GunShot(): one bullet, aimed along @p slope.
 *
 * `damage = 5*(P_Random()%3+1); angle = mo->angle; if (!accurate)
 *  angle += (P_Random()-P_Random())<<18; P_LineAttack (mo, angle,
 *  MISSILERANGE, bulletslope, damage);`
 *
 * The two draws for the drift happen only when the shot is inaccurate, so their
 * presence is part of the sequence. P_GUNSHOT_DAMAGE and P_SHOT_SPREAD_SHIFT are
 * overridable so tools/attack_check.py can perturb one and prove the comparison
 * fails; no shipped build defines them.
 *
 * @param world    The level's context.
 * @param shooter  The mobj firing.
 * @param slope    P_BulletSlope's answer.
 * @param accurate Nonzero for a first shot (`!player->refire`), zero for a refire.
 */
#ifndef P_GUNSHOT_DAMAGE
#define P_GUNSHOT_DAMAGE 5
#endif
#ifndef P_SHOT_SPREAD_SHIFT
#define P_SHOT_SPREAD_SHIFT 18
#endif
void p_gun_shot(p_mobj_context_t* world, r_mobj_t* shooter, fixed_t slope,
                int accurate);

/**
 * @brief A_Punch(): the fist's melee swing.
 *
 * `damage = (P_Random()%10+1)<<1;` then `damage *= 10` when the strength power is
 * running; `angle = mo->angle; angle += (P_Random()-P_Random())<<18;
 * slope = P_AimLineAttack (mo, angle, MELEERANGE);
 * P_LineAttack (mo, angle, MELEERANGE, slope, damage);` and then, when the trace
 * found a target, `mo->angle = R_PointToAngle2 (mo->x, mo->y, linetarget->x,
 * linetarget->y)` -- the punch turns the player to face what it hit.
 *
 * The `P_SpawnPuff` that lands on a wall from a melee trace starts at S_PUFF3,
 * which is the one branch of P_SpawnPuff this task owes p_mobj.c.
 */
void a_punch(p_mobj_context_t* world, struct p_player* player);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_P_PSPR_H */
