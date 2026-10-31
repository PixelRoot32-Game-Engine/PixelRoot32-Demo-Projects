/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * Interactions: damage, death and pickups.
 *
 * THE REFERENCE
 *
 * linuxdoom-1.10 p_inter.c: P_GiveAmmo, P_GiveWeapon, P_GiveBody, P_GiveArmor,
 * P_GiveCard, P_GivePower, P_TouchSpecialThing, P_KillMobj and P_DamageMobj.
 * Every rule below cites the upstream statement it reproduces. This is C99 only:
 * no allocation (the mobjs a kill or a blood spray creates come from the
 * context's zone), no globals, no stdio, no floating point.
 *
 * WHERE THE PLAYER RECORD COMES FROM
 *
 * Upstream reaches the player through `mobj->player`, a pointer. This port's
 * `r_mobj_t::player` is a byte saying "this is the avatar", and the record lives
 * on the level (p_level_t::playerState), because there is exactly one player
 * and D5 owns the rest. Every entry point that needs the record therefore takes
 * the world context and resolves it through p_inter.c's player lookup; the
 * header only promises that the lookup is NULL for a context without a level,
 * which is what makes the functions safe for a hand-built unit test.
 *
 * WHAT IS COUNTED, AND WHAT IS NOT
 *
 * The intermission totals (`killcount`, `itemcount`) are the player's, not the
 * level's: upstream increments `source->player->killcount` from P_KillMobj and
 * `player->itemcount` from P_TouchSpecialThing, and the level's TOTALS
 * (p_level_t::totalKills / totalItems) stay the denominators the load counted.
 * Both halves are needed for the intermission screen, and only this file may
 * write the numerator.
 *
 * FOUR PERTURBABLE CONSTANTS, ON PURPOSE
 *
 * The two armor divisors (below) and the crush damage are `#ifndef`s, like
 * P_ORIG_FRICTION and P_VIEWHEIGHT; the fourth is p_mobj.h's P_BLOOD_Z_SHIFT.
 * tools/damage_check.py compiles the C side with one of them changed, and the
 * comparison against tools/doom_ref.py must then fail. A gate that cannot fail
 * proves only that it runs.
 */
#ifndef DOOM_P_INTER_H
#define DOOM_P_INTER_H

#include <stdint.h>

#include "m_fixed.h"
#include "p_mobj.h"
#include "p_user.h"

#ifdef __cplusplus
extern "C" {
#endif

/* p_inter.c's BONUSADD: the gold-screen counter a pickup adds, six tics. */
#define P_BONUSADD 6

/* p_local.h's BASETHRESHOLD: "chase no matter what" is cleared to this many
 * tics of sound-less awareness when a monster is damaged. */
#define P_BASETHRESHOLD 100

/* P_DamageMobj's armor absorption: `if (armortype == 1) saved = damage/3; else
 * saved = damage/2;`. Overridable so the gate can prove its comparison fails. */
#ifndef P_ARMOR_GREEN_DIVISOR
#define P_ARMOR_GREEN_DIVISOR 3
#endif
#ifndef P_ARMOR_BLUE_DIVISOR
#define P_ARMOR_BLUE_DIVISOR 2
#endif

/* PIT_ChangeSector's `P_DamageMobj (thing,NULL,NULL,10)`: ten points every
 * fourth tic while a crushing plane keeps a thing from fitting. */
#ifndef P_CRUSH_DAMAGE
#define P_CRUSH_DAMAGE 10
#endif

/* One record per weapon, d_items.c's weaponinfo[], lives in p_pspr.c: the ammo
 * pool P_GiveWeapon fills plus the five psprite state numbers (p_weapon_info()).
 * Until D4.2d2 this header could only promise the ammo column, because the rest
 * belonged to the weapon state machine. */
struct p_level;

/* --- Giving things -------------------------------------------------------- */

/**
 * @brief P_GiveAmmo(): add @p num clip-loads of @p ammo, refusing when full.
 *
 * `num` counts *loads*, not rounds: `clipammo[] = {10,4,20,1}` (clip, shell,
 * cell, rocket), so 1 is a clip and 5 is a box. `num == 0` is upstream's "half a
 * load", which is what a dropped clip gives (10/2 = 5 rounds). Baby and
 * nightmare double the amount (`num <<= 1`).
 *
 * The refusal is `player->ammo[ammo] == player->maxammo[ammo]`, not a clamp
 * check: a player one round short still gets the whole load and is clamped at
 * the ceiling.
 *
 * The auto-select tail is here: when the player was *empty* of this pool
 * (`oldammo == 0`), the weapon in hand decides whether a new one is asked for --
 * a clip re-arms a bare-fisted player with the chaingun when it is owned and the
 * pistol otherwise; shells re-arm a fist or a pistol with the shotgun; cells and
 * rockets likewise. It compares `readyweapon`, which D4.2d2 added.
 *
 * @param world  The level context, for the skill's double-ammo rule.
 * @param player The player record; NULL is refused.
 * @param ammo   The pool.
 * @param num    Loads to add, or 0 for half a load.
 * @return 1 when the ammo was granted, 0 when refused.
 */
int p_give_ammo(const p_mobj_context_t* world, p_player_t* player, p_ammo_t ammo,
                int num);

/**
 * @brief P_GiveWeapon(): the weapon, plus one or two loads of its ammo.
 *
 * A found weapon gives two loads, a dropped one (`MF_DROPPED`) one, and a weapon
 * with no ammo (`am_noammo`, the fist and the chainsaw) gives none. The answer
 * is `gaveweapon || gaveammo`, which is what P_TouchSpecialThing tests before
 * removing the item: a second shotgun is picked up for its shells.
 *
 * NOT PORTED HERE: the netgame branch that leaves a placed weapon in the world
 * forever (`if (netgame && deathmatch != 2 && !dropped)`). Net play is D5 and
 * p_spawn_rules_t::netgame is 0 in every mode this port ships.
 *
 * @param world   The level context, for the ammo grant.
 * @param player  The player record.
 * @param weapon  The weapon slot.
 * @param dropped Nonzero when the item carries MF_DROPPED.
 * @return 1 when the weapon or its ammo was granted, 0 when neither was.
 */
int p_give_weapon(const p_mobj_context_t* world, p_player_t* player,
                  p_weapon_t weapon, int dropped);

/**
 * @brief P_GiveBody(): health, refusing at MAXHEALTH.
 *
 * `if (player->health >= MAXHEALTH) return false;` -- a stimpack on a full
 * player stays on the floor. The mobj's health is mirrored (`player->mo->health
 * = player->health`) because P_DamageMobj keeps the two equal.
 *
 * @return 1 when health was added, 0 when the player was already full.
 */
int p_give_body(p_player_t* player, int num);

/**
 * @brief P_GiveArmor(): replace the armor when the new kind is better.
 *
 * `hits = armortype*100`, and `if (player->armorpoints >= hits) return false`.
 * So green armor (100 points) is refused once the player has any armor at 100 or
 * more, and blue armor (200 points) is refused only at 200. Picking up armor
 * sets both fields; it does not add to them.
 *
 * @param armortype 1 for green, 2 for blue.
 * @return 1 when the armor was taken, 0 when it was refused.
 */
int p_give_armor(p_player_t* player, int armortype);

/**
 * @brief P_GiveCard(): record a key and flash the screen, once.
 *
 * `if (player->cards[card]) return; player->bonuscount = BONUSADD;
 * player->cards[card] = 1;` -- note `=`, not `+=`: a key sets the counter.
 */
void p_give_card(p_player_t* player, p_card_t card);

/**
 * @brief P_GivePower(): grant a power-up, storing its tic count.
 *
 * The four timed powers store their duration (P_INVULNTICS and friends) and
 * always answer 1. Invisibility also sets MF_SHADOW on the avatar; strength
 * heals 100 points through P_GiveBody and stores 1, which P_PlayerThink fades
 * (T14). The two permanent-ish powers (all-map, infrared) store their tic count
 * too, because upstream's counters are tic counts for all six.
 *
 * @param player The player record; the avatar it points at is what MF_SHADOW
 *               and the strength heal touch.
 * @param power  The power-up.
 * @return 1 when granted, 0 when the player already has the permanent one.
 */
int p_give_power(p_player_t* player, p_power_t power);

/* --- Touching, killing and damaging -------------------------------------- */

/**
 * @brief P_TouchSpecialThing(): one item, one toucher.
 *
 * Upstream's order, reproduced:
 *
 *   1. `delta = special->z - toucher->z`; out of reach means `delta >
 *      toucher->height` or `delta < -8*FRACUNIT`;
 *   2. a toucher with `health <= 0` gets nothing ("Dead thing touching ... can
 *      happen with a sliding player corpse");
 *   3. the switch on `special->sprite` -- the sprite, not the mobj type, because
 *      that is what upstream compares and a dropped copy keeps it;
 *   4. every arm that refuses (armor already better, a full player, a power
 *      already held) returns *before* the item is removed;
 *   5. `if (special->flags & MF_COUNTITEM) player->itemcount++;`,
 *      P_RemoveMobj(special), `player->bonuscount += BONUSADD`.
 *
 * The sound and the message text are the audio layer's and the HUD's (D5); the
 * branch that names which they would be is kept so the rule is readable.
 *
 * @param world   The level context: the zone, the RNG and the player record.
 * @param special The item mobj (PIT_CheckThing's `thing`).
 * @param toucher The mobj that touched it (`tmthing`).
 */
void p_touch_special_thing(p_mobj_context_t* world, r_mobj_t* special,
                           r_mobj_t* toucher);

/**
 * @brief P_KillMobj(): the death a damage total produces.
 *
 * Upstream's body:
 *
 *   - clear MF_SHOOTABLE, MF_FLOAT and MF_SKULLFLY; clear MF_NOGRAVITY for
 *     everything but a flying skull; set MF_CORPSE and MF_DROPOFF; quarter the
 *     height (`target->height >>= 2`), which is what makes a corpse a low
 *     obstacle and what the crush rule later measures;
 *   - intermission: a player's source raises that player's `killcount` for an
 *     MF_COUNTKILL target, and a single-player world credits player 0 for every
 *     monster death, including one a monster caused;
 *   - a player target loses MF_SOLID and becomes PST_DEAD;
 *   - the state is `xdeathstate` when `health < -spawnhealth` and the type has
 *     one, `deathstate` otherwise, and then `tics -= P_Random()&3` (at least 1);
 *   - the drop: MT_WOLFSS/MT_POSSESSED leave a clip, MT_SHOTGUY a shotgun,
 *     MT_CHAINGUY a chaingun, spawned at the corpse's feet with MF_DROPPED.
 *
 * NOT PORTED HERE, and named rather than dropped: the deathmatch `frags[]`
 * bookkeeping (D5) and the automap stop (D5). `P_DropWeapon` *is* here: a dying
 * player's weapon psprite lowers and stops at the bottom of the screen.
 *
 * @param world  The level context: zone, RNG, player record.
 * @param source Who caused it (may be NULL for the environment).
 * @param target The thing that died.
 */
void p_kill_mobj(p_mobj_context_t* world, r_mobj_t* source, r_mobj_t* target);

/**
 * @brief P_DamageMobj(): the one damage entry point.
 *
 * Upstream's order, which the port keeps because two of its steps draw from the
 * random sequence:
 *
 *   1. a thing that is not MF_SHOOTABLE, or whose health is already <= 0, takes
 *      nothing;
 *   2. a flying skull stops its momentum;
 *   3. baby skill halves the damage, then an inflictor thrusts the target away
 *      (`damage*(FRACUNIT>>3)*100/mass` along the inflictor-to-target angle),
 *      with the "fall forwards" variant -- ANG180, four times the thrust -- when
 *      `damage < 40 && damage > health && z - inflictor->z > 64` and
 *      `P_Random()&1` says so: that draw is part of the sequence;
 *   4. the player record: skill 11's "end of game hell" clamp, the god/invuln
 *      immunity for damage under 1000, armor absorption (`damage/3` for green,
 *      `damage/2` for blue, the whole armor when it is worth less than the
 *      share), the health mirror, `attacker = source` and `damagecount += damage`
 *      capped at 100;
 *   5. `target->health -= damage`; at zero or below, P_KillMobj and return;
 *   6. the pain: `P_Random() < painchance` sets MF_JUSTHIT and the pain state --
 *      unless the thing is still a flying skull;
 *   7. `reactiontime = 0`, and a thing with no threshold adopts `source` as its
 *      target (threshold to BASETHRESHOLD) and enters its see state when it was
 *      still standing in its spawn state.
 *
 * `attacker` is the *source*, not the inflictor, which is why a barrel's
 * explosion credits the barrel's killer and not the barrel.
 *
 * @param world     The level context: RNG and the player record.
 * @param target    The thing being damaged.
 * @param inflictor The thing that delivered it, for the thrust angle (may be
 *                  NULL for slime, a crush or a sector effect).
 * @param source    The thing to retaliate against (may be NULL).
 * @param damage    Points before armor and before the skill adjustment.
 */
void p_damage_mobj(p_mobj_context_t* world, r_mobj_t* target,
                   r_mobj_t* inflictor, r_mobj_t* source, int damage);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_P_INTER_H */
