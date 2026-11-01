/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * The level's sector and line specials: what P_SpawnSpecials() creates, the
 * sector state the movers write, and the queries every special needs.
 *
 * WHAT THIS LAYER IS
 *
 * linuxdoom-1.10 keeps a mutable `sector_t sectors[]` in the zone and lets the
 * movers write it; P_SpawnSpecials() scans that array once after the THINGS
 * run and turns the sector special numbers into thinkers. This layer is that
 * scan plus the state it reads.
 *
 * THE ONE DELIBERATE DIFFERENCE FROM p_mobj.c's SPAWN PATH
 *
 * `doom_map` is a zero-copy view over const bytes (on the board, a mapped flash
 * page). It cannot hold the level's mutable heights, and doom_map.* is not this
 * task's surface. So p_level_init() copies the seven sector fields the
 * simulation reads into `p_sector_t sectors[]` in the level's zone -- exactly
 * P_LoadSectors()' step, and the only mutable copy. `doom_map` stays the
 * immutable source; no second runtime copy exists, and every mover, light and
 * query below reads and writes `p_sector_t`. The same applies to the one
 * mutable line field: `p_level_t::lineSpecial[]` is P_LoadLineDefs' `special`
 * copy, because a one-shot trigger writes `line->special = 0`.
 *
 * WHAT IS IMPLEMENTED, AND WHAT IS DEFERRED
 *
 * Upstream enumeration is the checklist. Sector specials (p_spec.c
 * P_SpawnSpecials):
 *
 *   1   flickering light        P_SpawnLightFlash          implemented
 *   2   fast strobe             P_SpawnStrobeFlash(FAST)   implemented
 *   3   slow strobe             P_SpawnStrobeFlash(SLOW)   implemented
 *   4   fast strobe + damage    strobe, special kept       implemented
 *   5   hellslime damage        P_PlayerInSpecialSector    implemented
 *   7   nukage damage           P_PlayerInSpecialSector    implemented
 *   8   glowing light           P_SpawnGlowingLight        implemented
 *   9   secret sector           totalsecret++ at spawn,    implemented
 *                               secretcount++ and the
 *                               sector cleared by the player
 *   10  door closes in 30s      P_SpawnDoorCloseIn30       implemented
 *   11  exit super damage       P_PlayerInSpecialSector    implemented
 *   12  slow strobe, synced     P_SpawnStrobeFlash(SLOW,1) implemented
 *   13  fast strobe, synced     P_SpawnStrobeFlash(FAST,1) implemented
 *   14  door raises in 5 min    P_SpawnDoorRaiseIn5Mins    implemented
 *   16  super hellslime damage  P_PlayerInSpecialSector    implemented
 *   17  fire flicker            P_SpawnFireFlicker         implemented
 *   6,15,18..  not named by either upstream function and not carried by any
 *              episode-1 map: counted as `unsupportedSectorSpecial`
 *
 * WHY ONLY 4, 5, 7, 9, 11 AND 16 REACH THE PLAYER
 *
 * P_PlayerThink asks the avatar's sector once per tic and P_PlayerInSpecialSector
 * switches on 4, 5, 7, 9, 11 and 16. Every other number in the list above is a
 * *spawn* effect whose thinker writes `sector->special = 0`, so by the time a
 * player can stand there the field is zero: 1, 2, 3, 8 and 12/13 in p_lights.c,
 * 10 and 14 in p_doors.c, 17 again in p_lights.c. That is why the player effect
 * and the spawn scan have disjoint number sets, and why any other number is
 * either an authoring mistake or a mover's `newspecial` write.
 *
 * Line special 48 is not a trigger at all: it is the scroll effect
 * P_SpawnSpecials collects and P_UpdateSpecials advances (see
 * p_update_specials() below), so neither trigger function names it and it is
 * not counted as unsupported.
 *
 * Line specials (P_CrossSpecialLine, P_UseSpecialLine, P_ShootSpecialLine):
 * every number whose EV function exists below is dispatched; texture scrolling
 * (48) and the remaining switch numbers are counted as
 * `unsupportedLineSpecial` / `deferredLineSpecial`. A number that upstream's
 * trigger function does not name at all is a no-op upstream and is not counted
 * here: see `upstream_handles()` in p_spec.c. The header of each EV file lists
 * its own type enumeration against upstream.
 *
 *   P_CrossSpecialLine, one-shot (the number is cleared)
 *     2,3,4 doors; 5,19,36,37,38,40,56,58,59,119,130 floors;
 *     6,25,40,44,141 ceilings; 10,22,53,54,121 plats;
 *     12,13,17,35,104 lights          implemented
 *     30,96 raiseToTexture            deferred (needs textureheight[], r_data)
 *     8,100 stairs                    implemented (EV_BuildStairs)
 *     39,125 teleport                 implemented (EV_Teleport, one-shot)
 *   P_CrossSpecialLine, retrigger (the number stays)
 *     72,73,77 ceilings; 75,76,86,90 doors;
 *     82,83,84,91..94,98,128,129 floors;
 *     87,88,89 plats; 79,80,81 lights implemented
 *     97,126 teleport                 implemented (EV_Teleport, the line stays)
 *     48 scroll                       implemented (T35): P_SpawnSpecials records
 *                                     the line, P_UpdateSpecials adds
 *                                     FRACUNIT to its front side's texture
 *                                     offset every tic (see below)
 *     52,124 exit                     implemented (D4.5): the level's exit
 *                                     request is set, the line is not cleared
 *                                     (upstream's G_ExitLevel/G_SecretExitLevel
 *                                     do not write `line->special = 0`)
 *   P_UseSpecialLine
 *     1,26,27,28,31,32,33,34,117,118  manual doors, implemented (keys: D5)
 *     7,127 stairs                    implemented (EV_BuildStairs; the switch
 *                                     half is P_ChangeSwitchTexture, deferred)
 *     9,14,15,18,20,21,23,29,41,42,43,45,49,50,55,60..71,99,101,
 *     102,103,111..116,122,123,131..140
 *                                     EV implemented; the switch *texture*
 *                                     change is deferred (see p_switch note)
 *     11,51 exit                      implemented (D4.5): the level's exit
 *                                     request is set and the line is cleared,
 *                                     which is P_ChangeSwitchTexture(line, 0)'s
 *                                     one-shot write
 *   P_ShootSpecialLine
 *     24,46,47                        EV implemented; switch texture deferred
 *
 * THE EXIT REQUEST, AND WHY IT IS A LEVEL FIELD
 *
 * Upstream's G_ExitLevel()/G_SecretExitLevel() write two globals: `gameaction =
 * ga_completed` and `secretexit`. This port has no game globals, and p_spec.c
 * must keep linking without g_game.c (the line-special gates compile it with
 * their own source list), so the trigger records the request in
 * `p_level_t::exitRequest` instead. g_game.c's g_exit_from_level() turns that
 * request into the game-state write, and g_completed_next() computes the next
 * map exactly as G_DoCompleted does. A caller that ticks a level without a game
 * loop sees the request and does nothing, which is the faithful state for a
 * world that has no intermission.
 *
 * P_ChangeSwitchTexture IS STILL NOT part of this layer, and the exit specials
 * do not fake it. The measurement (D4.5): the nine episode-1 maps name 14
 * distinct `SW1*` textures; none of their 14 `SW2*` partners is in the baked
 * container, because the bake lists only the names the levels' own SIDEDEFS
 * carry (SW2BROWN is baked only because two maps already use it as a wall). A
 * swap would therefore ask the renderer for a texture the container does not
 * hold. Baking the partners costs 131,072 B of payload (131,744 B with the
 * container's per-lump overhead) against 46,116 B of free container space
 * (3,342,336 B partition - 3,296,220 B used), so it is a capacity decision and
 * not this task's. The one-shot line clear is kept because it is the part of
 * P_ChangeSwitchTexture(line, 0) the simulation can represent without a texture.
 *
 * NOT IMPLEMENTED AT ALL, DELIBERATELY
 *
 *   - P_ChangeSwitchTexture (the SW2 capacity decision above), the level timer
 *     (-avg/-timer), texture animation and P_UpdateSpecials' scrolling.
 *   - EV_SlidingDoor (compiled out upstream too).
 *   - P_PlayerInSpecialSector's `player->cheats &= ~CF_GODMODE` in case 11: this
 *     port has no cheats (D5), so the clear is a no-op and is named, not faked.
 *     The damage and the exit that the same case performs are real.
 *
 * P_ChangeSector's crush *damage* is no longer on this list: p_map.c's
 * p_change_sector() calls P_DamageMobj (p_inter.c) with T_MovePlane's own
 * `crush` flag, so a crushing floor or ceiling damages what it traps exactly as
 * upstream does, and nothing on this path is counted as unsupported.
 *
 * WHAT ONE SECTOR COSTS
 *
 * sizeof(p_sector_t) on the 32-bit target is the sector's mutable state plus
 * one pointer; E1M1's 85 sectors are about 3.4 KB. E1M1's 475 linedef specials
 * are 950 B. The sound flood adds one p_sector_sound_t per sector (12 B here:
 * pointer, epoch, traversed int16 and padding), so E1M1's array is 1,020 B plus
 * one zone header. The level's arena pays for all of it, and p_level_init()
 * reports an allocation failure rather than loading a world without them.
 *
 * This is C99 only: <stddef.h> and <stdint.h>, no allocation, no globals, no
 * stdio, no floating point. The caller owns the level, its zone and every
 * thinker.
 */
#ifndef DOOM_P_SPEC_H
#define DOOM_P_SPEC_H

#include <stddef.h>
#include <stdint.h>

#include "doom_map.h"
#include "m_fixed.h"
#include "m_random.h"
#include "p_tick.h"
#include "z_zone.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The level struct and the world context are defined one layer up; this header
 * only needs pointers to them. */
struct p_level;
struct p_mobj_context;
struct p_action_stats;
struct p_player;

/* --- Upstream constants --------------------------------------------------- */

/* p_local.h / p_spec.h. Kept here because every mover and every light needs
 * them and a second definition would eventually disagree. */
#define P_FLOORSPEED FRACUNIT
#define P_CEILSPEED FRACUNIT
#define P_PLATSPEED FRACUNIT
#define P_PLATWAIT 3
#define P_VDOORSPEED (FRACUNIT * 2)
#define P_VDOORWAIT 150
#define P_GLOWSPEED 8
#define P_STROBEBRIGHT 5
#define P_FASTDARK 15
#define P_SLOWDARK 35

/* DOOM's tic rate. The waits in this file are written in tics upstream
 * (`35*30`, `35*PLATWAIT`); naming the rate keeps the arithmetic readable. */
#define P_TICRATE 35

/* P_FindNextHighestFloor's "20 adjoining sectors max!" (p_spec.c). The port
 * cannot print a warning, so it counts the overflow and stops collecting. */
#define P_MAX_ADJOINING_SECTORS 20

/* --- The mutable sector --------------------------------------------------- */

/* Forward: the sound state points at a mobj, which p_mobj.h defines. */
struct r_mobj;

/*
 * One sector's sound-flood state (upstream's sector_t fields soundtarget,
 * soundtraversed and validcount). p_level_init() allocates and zeroes one per
 * sector from the level arena; `p_mobj_context_t` points at the array and
 * caches the level's flood epoch in `soundValidCount`.
 *
 *   soundTarget    who this sector last heard, or NULL.
 *   validCount     the flood epoch that wrote the record, compared against
 *                  p_mobj_context_t::soundValidCount.
 *   soundTraversed how many sound-blocking lines the flood crossed to reach it
 *                  (0 = direct), which is what bounds the recursion.
 */
typedef struct p_sector_sound {
    struct r_mobj* soundTarget;
    uint32_t validCount;
    int16_t soundTraversed;
} p_sector_sound_t;

/*
 * One sector as the simulation sees it. This is upstream's sector_t reduced to
 * the fields a mover, a light or a surrounding-sector query reads:
 *
 *   floorHeight, ceilingHeight  the truth T_MovePlane() writes.
 *   floorPic, ceilingPic        texture state; lowerAndChange/donutRaise move
 *                               floorPic, and the renderer reads both.
 *   lightLevel                  the truth the light thinkers write.
 *   special                     the mutable special number: some spawns clear
 *                               it, sector 4 restores it, lowerAndChange and
 *                               raiseToNearestAndChange rewrite it.
 *   tag                         the line-tag key, immutable in practice.
 *   specialdata                 upstream's `void* specialdata`: the thinker that
 *                               owns this sector's movement, or NULL. It is a
 *                               thinker node, not a record pointer, so the kind
 *                               says which record to cast to.
 *
 * `doom_map`'s sector record remains the immutable source; p_level_init() is
 * the only writer of the initial values.
 */
/*
 * `p_sector_t`'s two frame fields: no frame index, i.e. upstream's identity
 * translation. It is also the value a sector carries before the level's first
 * P_UpdateSpecials().
 */
#define P_ANIM_IDENTITY 0xFF

typedef struct p_sector {
    fixed_t floorHeight;
    fixed_t ceilingHeight;
    r_name_t floorPic;
    r_name_t ceilingPic;
    int16_t lightLevel;
    int16_t special;
    int16_t tag;
    /* The animation frame P_UpdateSpecials() selected for the flat above: an
     * index into `p_level_t::animFrames`, or P_ANIM_IDENTITY when the name is
     * outside every family. It lives here rather than in a second array because
     * these two bytes are the struct's existing tail padding, so the record is
     * the same size -- the demo gates' arena pins prove that. `floorPic` stays
     * the WAD's own name: the simulation reads it (a stair chain compares two
     * sectors' floor pictures), exactly as upstream keeps `flattranslation[]`
     * beside `sector_t` instead of overwriting it. */
    uint8_t floorFrame;
    uint8_t ceilingFrame;
    p_thinker_t* specialdata;
} p_sector_t;

/* --- What the specials did ------------------------------------------------ */

/*
 * The counters a report and the oracle read. Every "not implemented yet" is a
 * number here, not a silent branch:
 *
 *   spawned                 thinkers p_spawn_specials() created
 *   lightFlashes            sector special 1
 *   fastStrobes             sector specials 2, 4 and 13
 *   slowStrobes             sector specials 3 and 12
 *   glows                   sector special 8
 *   fireFlickers            sector special 17
 *   doorCloseIn30           sector special 10
 *   doorRaiseIn5Mins        sector special 14
 *   secrets                 sector special 9, also added to totalSecrets
 *   unsupportedSectorSpecial a nonzero sector special with no spawn
 *   unsupportedLineSpecial  a trigger whose number upstream's function acts on
 *                           but this port has no EV for
 *   deferredLineSpecial     a trigger whose EV exists but does something later
 *   lineScrolls             line special 48, P_UpdateSpecials' job
 *   deathmatchSpecials      P_SpawnDeathmatchSpecials' counter (see p_spec.c)
 *   changeSectorCalls       every P_ChangeSector() call, from every mover
 *   changeSectorClear       the calls that answered "clear"
 *   crushedThings           things that did not fit and were therefore damaged
 *                           by the crushing move (P_CRUSH_DAMAGE, every fourth
 *                           tic); a move without `crush` reverts and damages
 *                           nothing, so this counts only real crushes
 *   thingRefreshes          cached mobj floorz/ceilingz updates
 *   allocationFailures      the zone refused a thinker record
 *   adjacentOverflow        a sector with more than 20 neighbours
 *   triggers                p_trigger_special_line() calls that ran an EV
 *   triggersRefused         a trigger refused by a mode/player check
 *   useLineHits             P_UseLines() dispatches that reached a special line
 *                           (P_UseLines' PTR_UseTraverse answer, independent of
 *                           whether the EV then did anything)
 */
typedef struct p_spec_stats {
    uint32_t spawned;
    uint32_t lightFlashes;
    uint32_t fastStrobes;
    uint32_t slowStrobes;
    uint32_t glows;
    uint32_t fireFlickers;
    uint32_t doorCloseIn30;
    uint32_t doorRaiseIn5Mins;
    uint32_t secrets;
    uint32_t unsupportedSectorSpecial;
    uint32_t unsupportedLineSpecial;
    uint32_t deferredLineSpecial;
    uint32_t lineScrolls;
    uint32_t deathmatchSpecials;
    uint32_t changeSectorCalls;
    uint32_t changeSectorClear;
    uint32_t crushedThings;
    uint32_t thingRefreshes;
    uint32_t allocationFailures;
    uint32_t adjacentOverflow;
    uint32_t triggers;
    uint32_t triggersRefused;
    uint32_t useLineHits;
} p_spec_stats_t;

/*
 * The distinct line-special record (D4.4).
 *
 * `unsupportedLineSpecial` answers "how many triggers had no EV"; it cannot say
 * which special numbers, and a run that reaches a line with no handler is not
 * upstream's world after the first one, however coherent its counters look. This
 * is a small, caller-owned log of the set, one entry per distinct
 * (special, trigger kind), with the first tic it was reached and the line that
 * carried it.
 *
 * `unsupported` separates the two cases that a single counter conflates:
 *
 *   1  upstream's trigger function acts on this number and this port has no EV
 *      for it: a real fidelity gap.
 *   0  upstream's trigger function does not name this number either, so doing
 *      nothing is the faithful answer. Recorded as evidence, not counted as a
 *      gap.
 *
 * Like p_action_trace_t it costs the firmware no storage of its own:
 * p_mobj_context_t carries one pointer (NULL in every shipped build) and the
 * table belongs to the caller. `tic` is the caller's current tic, set before each
 * world tic.
 */
#define P_SPEC_UNSUPPORTED_MAX 32

typedef struct p_spec_trace_entry {
    int16_t special;
    int16_t kind;      /* the p_trigger_kind_t that reached it */
    uint16_t line;
    uint8_t unsupported; /* see above */
    uint32_t firstTic;
    uint32_t hits;     /* events that reached this (special, kind) */
} p_spec_trace_entry_t;

typedef struct p_spec_trace {
    uint32_t tic;      /* the caller's current tic, set before each world tic */
    uint32_t count;    /* distinct (special, kind) pairs recorded */
    uint32_t overflow; /* distinct pairs not recorded: the table was full */
    p_spec_trace_entry_t entries[P_SPEC_UNSUPPORTED_MAX];
} p_spec_trace_t;

/* --- Error codes ---------------------------------------------------------- */

/*
 *   P_SPEC_OK              the scan ran
 *   P_SPEC_ERR_NULL_LEVEL  no level was passed
 *   P_SPEC_ERR_NOT_OPEN    the level is closed
 *   P_SPEC_ERR_NO_SECTORS  the level has no sector state (p_level_init refused)
 *   P_SPEC_ERR_NO_WORLD    the level's zone, thinker list, map or RNG is missing
 *
 * A refused scan changes no sector and creates no thinker.
 */
typedef enum p_spec_error {
    P_SPEC_OK = 0,
    P_SPEC_ERR_NULL_LEVEL = 1,
    P_SPEC_ERR_NOT_OPEN = 2,
    P_SPEC_ERR_NO_SECTORS = 3,
    P_SPEC_ERR_NO_WORLD = 4
} p_spec_error_t;

/* --- Public API ---------------------------------------------------------- */

/**
 * @brief P_SpawnSpecials(): scans the level's sectors and creates the thinkers.
 *
 * Upstream's order, which is the order the thinkers appear in the list:
 *
 *   1. the deathmatch-guarded level-start hook (see p_spec.c);
 *   2. for sector 0..numsectors-1, in index order, one switch on the sector's
 *      special: 1 flash, 2 fast strobe, 3 slow strobe, 4 fast strobe (special
 *      kept as 4), 8 glow, 9 secret, 10 close-in-30, 12 slow sync strobe,
 *      13 fast sync strobe, 14 raise-in-5-min, 17 fire flicker;
 *   3. for line 0..numlines-1: special 48 is a scrolling effect, recorded in
 *      `level->scrolls` (one entry per line, in line order) for
 *      p_update_specials() to advance.
 *
 * The random sequence position matters: a non-synced strobe draws
 * `P_Random()&7` and a light flash draws `P_Random()&maxtime`, in sector order.
 *
 * Must be called after p_level_init() and before the first p_level_tick(), which
 * is P_SetupLevel's order (P_LoadThings, then P_SpawnSpecials).
 *
 * @return P_SPEC_OK or one of p_spec_error_t.
 */
p_spec_error_t p_spawn_specials(struct p_level* level);

/**
 * @brief Allocates, zeroes, initialises and links one sector thinker.
 *
 * This is the shared prologue of every spawn in this layer: Z_Malloc(PU_LEVEL),
 * `P_AddThinker`, and the accounting a failure needs. The record's first member
 * is the p_thinker_t, so the release callback p_level_release_thinker() returns
 * it to the zone unchanged.
 *
 * @param world The level's context (zone, thinkers, map, RNG, sectors).
 * @param size  sizeof the concrete thinker record.
 * @param fn    Its entry point.
 * @param kind  P_THINKER_CEILING / FLOOR / PLAT / DOOR / LIGHTFLASH.
 * @return The zeroed record, or NULL when the zone or the context refuses it.
 *         `specStats->allocationFailures` counts the refusal.
 */
void* p_spec_alloc_thinker(struct p_mobj_context* world, size_t size,
                           p_thinker_fn fn, p_thinker_kind_t kind);

/**
 * @brief P_ChangeSector(): the entry point every T_MovePlane() write calls.
 *
 * The rule is p_map.c's p_change_sector(): it walks the things the changed
 * sector can touch, runs P_ThingHeightClip on each, steps a standing thing onto
 * the new floor, and answers whether any living thing no longer fits. The call
 * is counted in `specStats->changeSectorCalls`, and a clear answer additionally
 * in `specStats->changeSectorClear`.
 *
 * PIT_ChangeSector's crush damage is delivered here: a thing that does not fit
 * and whose move may crush takes P_CRUSH_DAMAGE every fourth level tic, sprays a
 * blood mobj, and is counted in `specStats->crushedThings`.
 *
 * @param crush Nonzero when the moving plane may crush instead of reverting;
 *              this is T_MovePlane's own flag, forwarded unchanged.
 * @return 1 when something does not fit, 0 when the sector is clear.
 */
int p_spec_p_change_sector(struct p_mobj_context* world, uint16_t sector,
                           int crush);

/**
 * @brief False: P_ChangeSector() is the rule, not a stub.
 *
 * Kept so the regression test that used to prove the stub can now prove its
 * replacement, and so no caller mistakes the hook for a no-op.
 */
int p_spec_p_change_sector_is_stubbed(void);

/**
 * @brief Refreshes the heights every mobj in @p sector cached at spawn.
 *
 * Decision (1) of D4.1b: the sector array is the truth and anything that cached
 * a height at spawn follows it. The spawn path copies floorz/ceilingz from the
 * sector (p_spawn_mobj); this is the other half, called by p_move_plane() after
 * every height write (including a revert). It does not move a mobj's `z`: that
 * is P_ZMovement, D4.2.
 */
void p_spec_refresh_things(struct p_mobj_context* world, uint16_t sector);

/* --- The player in a special sector --------------------------------------- */

/*
 * Upstream's four damage numbers, one constant each so the oracle and the gate
 * can name them. `!(leveltime & 0x1f)` is the cadence every one of them shares:
 * the effect fires on tics 0, 32, 64, ... of the level's clock.
 *
 *   5   HELLSLIME DAMAGE        10 points, unless `powers[pw_ironfeet]`
 *   7   NUKAGE DAMAGE            5 points, unless `powers[pw_ironfeet]`
 *   4   STROBE HURT             20 points; the suit only survives a
 *   16  SUPER HELLSLIME DAMAGE  20 points   `P_Random()<5` roll
 *   11  EXIT SUPER DAMAGE       20 points, never shielded; the level exits when
 *                               `player->health <= 10`
 */
#define P_SECTOR_DAMAGE_HELLSLIME 10
#define P_SECTOR_DAMAGE_NUKAGE 5
#define P_SECTOR_DAMAGE_STRONG 20
#define P_SECTOR_EXIT_THRESHOLD 10
#define P_SECTOR_DAMAGE_SUIT_ROLL 5
#define P_SECTOR_DAMAGE_MASK 0x1f

/**
 * @brief P_PlayerInSpecialSector(): the player's tic in a special sector.
 *
 * Upstream's body, with its order kept because two of its steps are observable:
 *
 *   1. `if (player->mo->z != sector->floorheight) return;` -- a thing still
 *      falling through the sector takes nothing;
 *   2. `if (player->powers[pw_ironfeet])` is a complete immunity for 5 and 7.
 *      For 4 and 16 upstream writes
 *      `!player->powers[pw_ironfeet] || (P_Random()<5)`, so the suit still
 *      *draws* every tic and only reduces the odds -- the draw is part of the
 *      random sequence and is reproduced;
 *   3. the damage itself is P_DamageMobj (p_inter.c) with a NULL inflictor and
 *      a NULL source, so it is never thrust and never credits an attacker.
 *      P_DamageMobj's own "end of game hell" clamp is what keeps sector 11 from
 *      killing: `sector->special == 11 && damage >= health` becomes
 *      `damage = health - 1`;
 *   4. 9 is the secret: `player->secretcount++` and the sector's special is
 *      cleared, once, because that write is the one-shot;
 *   5. 11 clears god mode (no cheats exist here), damages, and then
 *      `if (player->health <= 10) G_ExitLevel();` -- the check is *after* the
 *      damage, and it is the only way out of E1M8.
 *
 * G_ExitLevel() is not p_spec.c's to call (that file must link without
 * g_game.c), so the exit is the same request p_exit_request_t records for the
 * line specials; g_game.c's g_exit_from_level() turns it into the game state.
 *
 * A number outside 4/5/7/9/11/16 is upstream's `I_Error`, which a port cannot
 * raise. It is left alone, and the level's counters are the only record: after
 * P_SpawnSpecials the field can only hold one of these six (see the header's
 * checklist), so a caller that finds another number has an authoring problem
 * upstream would have aborted on.
 *
 * @param level  The level; it owns the sector array, the clock and the request.
 * @param player The player record whose `mo` is standing in the sector. A
 *               record with no avatar, or a sector index outside the level,
 *               changes nothing.
 */
void p_player_in_special_sector(struct p_level* level, struct p_player* player);

/* --- Surrounding-sector queries (upstream p_spec.c) ----------------------- */

/* All of these reproduce P_Find*Surrounding(). A sector's neighbours are
 * derived by scanning LINEDEFS for two-sided lines that touch it, which is the
 * same set P_GroupLines stores per sector; the map is zero-copy, so this is a
 * scan rather than a second adjacency table. */

fixed_t p_find_lowest_floor_surrounding(const struct p_mobj_context* world,
                                        uint16_t sector);
fixed_t p_find_highest_floor_surrounding(const struct p_mobj_context* world,
                                         uint16_t sector);
fixed_t p_find_next_highest_floor(const struct p_mobj_context* world,
                                  uint16_t sector, fixed_t currentHeight);
fixed_t p_find_lowest_ceiling_surrounding(const struct p_mobj_context* world,
                                          uint16_t sector);
fixed_t p_find_highest_ceiling_surrounding(const struct p_mobj_context* world,
                                           uint16_t sector);
int p_find_min_surrounding_light(const struct p_mobj_context* world,
                                 uint16_t sector, int max);

/**
 * @brief P_FindSectorFromLineTag(): the next sector at or after @p start + 1
 *        whose tag is @p tag, or -1.
 */
int p_find_sector_from_line_tag(const struct p_mobj_context* world, int16_t tag,
                                int start);

/**
 * @brief getNextSector(): the sector on the other side of a two-sided line.
 *
 * @return 1 and writes @p out when the line is two-sided; 0 otherwise. As
 *         upstream, a line whose front sector is not @p sector answers its
 *         front sector rather than refusing, because callers only pass lines
 *         that belong to the sector.
 */
int p_line_other_sector(const doom_map_t* map, const doom_line_t* line,
                        uint16_t sector, uint16_t* out);

/* Front/back sector of a line, resolved through its sidedefs. `1` and an out
 * value when the side exists; `0` when it does not. */
int p_line_front_sector(const doom_map_t* map, const doom_line_t* line,
                        uint16_t* out);
int p_line_back_sector(const doom_map_t* map, const doom_line_t* line,
                       uint16_t* out);

/* The decoded record behind the two accessors below: `0` when the map has no
 * LINEDEFS or the index is outside the lump, which is what keeps their "reads
 * 0" contract without a second bounds check in each. */
static inline int p_line_decode(const doom_map_t* map, uint16_t index,
                                doom_line_t* out) {
    if (map == NULL || map->lumps[DOOM_MAP_LINEDEFS].data == NULL ||
        (uint32_t)index >= map->lumps[DOOM_MAP_LINEDEFS].count) {
        return 0;
    }
    doom_map_get_line(map, index, out);
    return 1;
}

/**
 * @brief The `special` (or `tag`) field of a linedef, as doom_map_get_line()
 *        decodes it.
 *
 * `special` is the line's action number (0 = none) and `tag` the sector tag it
 * addresses; doom_map_get_line() reads both verbatim from the record and
 * validates neither, because DOOM does not either -- an unknown special is a
 * no-op at run time.
 *
 * Each call now costs one record decode: an unaligned 14-byte read from mapped
 * flash, plus the index and flag reconstruction the decoder does for fields the
 * caller does not use. Keeping a copy of the pair was rejected: p_setup.c
 * already copies the mutable `special` into `lineSpecial[]` (a one-shot trigger
 * writes 0 over it), and `tag` is immutable, so a second copy would duplicate
 * what the level already holds for no benefit.
 *
 * They stay `static inline` so p_setup.c can build `lineSpecial[]` without
 * linking this layer: the THINGS harness (tools/things_check.py) compiles
 * p_setup.c and doom_map.c but not p_spec.c.
 *
 * A map with no LINEDEFS, or a line index outside the lump, reads 0.
 */
static inline int16_t p_line_special(const doom_map_t* map, uint16_t index) {
    doom_line_t line;

    if (!p_line_decode(map, index, &line)) {
        return 0;
    }
    return line.special;
}

static inline int16_t p_line_tag(const doom_map_t* map, uint16_t index) {
    doom_line_t line;

    if (!p_line_decode(map, index, &line)) {
        return 0;
    }
    return line.tag;
}

/* --- Line triggering ------------------------------------------------------ */

/*
 * What G_ExitLevel() / G_SecretExitLevel() set, on the level instead of in a
 * game global (see the file comment). `p_level_t::exitRequest` holds one of
 * these; g_game.c reads it when the game loop runs G_DoCompleted's step.
 *
 *   P_EXIT_NONE    no exit requested: the value every level load starts at
 *   P_EXIT_NORMAL  G_ExitLevel(): `secretexit = false; gameaction = ga_completed`
 *   P_EXIT_SECRET  G_SecretExitLevel(): `secretexit = true` (non-commercial)
 */
typedef enum p_exit_request {
    P_EXIT_NONE = 0,
    P_EXIT_NORMAL = 1,
    P_EXIT_SECRET = 2
} p_exit_request_t;

/*
 * Which upstream trigger function a caller is standing in for. The dispatch in
 * p_spec.c covers the implemented EV functions; everything else is counted, so
 * an unhandled line is visible instead of silent.
 *
 *   P_TRIGGER_CROSS  P_CrossSpecialLine  (a thing walks over the line)
 *   P_TRIGGER_USE    P_UseSpecialLine    (a player presses use)
 *   P_TRIGGER_SHOOT  P_ShootSpecialLine  (a hitscan hits the line)
 */
typedef enum p_trigger_kind {
    P_TRIGGER_CROSS = 0,
    P_TRIGGER_USE = 1,
    P_TRIGGER_SHOOT = 2
} p_trigger_kind_t;

/**
 * @brief EV_Teleport(): P_TeleportMove plus the two fogs.
 *
 * Upstream's EV_Teleport (p_telept.c), in its order:
 *
 *   1. `if (thing->flags & MF_MISSILE) return 0;` and `if (side == 1) return 0;`
 *      -- a missile never teleports, and a line crossed from its back side
 *      ("so you can get out of teleporter") does not either;
 *   2. for each sector whose `tag` is the line's, look for an MT_TELEPORTMAN
 *      *whose own subsector's sector is that sector*. A tagged sector with no
 *      teleportman standing in it is a dead teleporter, and the loop moves on to
 *      the next tagged sector;
 *   3. remember the thing's x/y/z, P_TeleportMove() it to the teleportman's
 *      x/y, then `thing->z = thing->floorz` (the destination's floor);
 *   4. a player's `viewz` is rewritten to `z + viewheight`;
 *   5. two MT_TFOG mobjs: one at the source (`oldx, oldy, oldz`) and one at
 *      `m->x + 20*finecosine[an], m->y + 20*finesine[an], thing->z` with
 *      `an = m->angle >> ANGLETOFINESHIFT`;
 *   6. a player's `reactiontime = 18`, `thing->angle = m->angle`, and all three
 *      momentum components are zeroed.
 *
 * `S_StartSound(fog, sfx_telept)` is not in this port: there is no sound layer
 * yet, so the call is named here and nothing is faked.
 *
 * @param level     The level; its mobjs are searched and its player record is
 *                  written.
 * @param lineIndex The linedef whose `tag` names the destination sector.
 * @param thing     The thing that crossed (NULL is refused: there is no actor).
 * @param side      P_PointOnLineSide() of the position the thing came from.
 * @return 1 when the thing moved and the fogs were spawned, 0 otherwise.
 */
int p_do_teleport(struct p_level* level, uint16_t lineIndex,
                  struct r_mobj* thing, int side);

/* The teleport's two constants, named so the gate and the oracle share them. */
#define P_TELEPORT_REACTIONTIME 18
#define P_TELEPORT_FOG_DISTANCE 20

/**
 * @brief Runs one line's special, with upstream's trigger rules.
 *
 * Reads the level's mutable `lineSpecial[]`, dispatches to the EV function the
 * number names, and clears the number for a one-shot trigger exactly as
 * upstream's `line->special = 0` does (retriggers are left alone).
 *
 * `thing` is upstream's actor argument: P_CrossSpecialLine's `thing`,
 * P_UseSpecialLine's `thing` and P_ShootSpecialLine's `thing`. `thing->player`
 * is the mode check both functions make (a non-player thing may only cross the
 * short list in P_CrossSpecialLine, may only use 1/32/33/34, and may only shoot
 * 46), and it is now also the thing EV_Teleport moves. A NULL thing is "no
 * actor": it is treated as a non-player and a teleport it reaches is refused.
 * `tools/level_check.c` uses NULL for the "a monster must not end the level"
 * fixtures, which need no actor.
 *
 * `side` is P_PointOnLineSide() of the *old* position for P_TRIGGER_CROSS (the
 * side the thing came from), and the user's side for P_TRIGGER_USE. It is
 * ignored by P_TRIGGER_SHOOT.
 *
 * @return 1 when an EV function ran (or a deferred effect was recognised),
 *         0 when the line has no special or the trigger was refused.
 */
int p_trigger_special_line(struct p_level* level, uint16_t lineIndex,
                           p_trigger_kind_t kind, struct r_mobj* thing,
                           int side);

/* --- P_UpdateSpecials: scrolling walls and animated pictures -------------- */

/*
 * linuxdoom-1.10's P_UpdateSpecials(), and what each half costs.
 *
 * THE SCROLL (line special 48)
 *
 * There is no table in this upstream. P_UpdateSpecials' "ANIMATE LINE SPECIALS"
 * loop walks `linespeciallist[]` (filled by P_SpawnSpecials with every line
 * whose special is 48) and, for each, runs
 *
 *     case 48:
 *       // EFFECT FIRSTCOL SCROLL +
 *       sides[line->sidenum[0]].textureoffset += FRACUNIT;
 *
 * so the step is one map unit horizontally on the *front* side, there is no
 * vertical component, and the line's number is re-tested every tic (a number
 * cleared by a trigger stops scrolling). Measured from DOOM1.WAD, the shareware
 * episode carries the number on twelve lines of two maps: E1M1 lines 352..359
 * (eight two-sided lines whose top and bottom tiers are both TEKWALL1, side 0
 * already at offset 8) and E1M7 lines 925..928 (four, side 0 at offset 0).
 * E1M2..E1M6, E1M8 and E1M9 carry none. `scrollers[]` with a per-entry
 * `dx`/`dy` is a later source's (BOOM's) table and is not what this port's
 * reference has; the measured step is P_SCROLL_STEP on x and 0 on y.
 *
 * The array is sized to the map rather than to upstream's MAXLINEANIMS (64).
 * That slice is an array bound, not a rule: upstream writes past it when a map
 * carries more than 64, and no shareware map carries more than eight. Sizing to
 * the count reproduces the rule exactly and cannot overflow.
 *
 * THE ANIMATION (animdefs[])
 *
 * P_InitPicAnims() turns the animdefs[] table into `anims[]`, skipping an entry
 * whose start name the WAD does not have, with `numpics = end - start + 1` over
 * the *lump indices* and `speed` from the table. P_UpdateSpecials then writes,
 * for every family and every index `i` in it,
 *
 *     pic = basepic + ((leveltime / speed + i) % numpics)
 *
 * into texturetranslation[] or flattranslation[]. Note `i` is the absolute
 * index, so the family's own base index is part of the phase.
 *
 * MEASURED FROM DOOM1.WAD: of the 22 animdefs entries, exactly two exist in the
 * shareware WAD; the other 20 are skipped by that presence test.
 *
 *   flat    NUKAGE1..NUKAGE3   base 51, 3 pics, speed 8   (51 % 3 == 0)
 *   texture SLADRIP1..SLADRIP3 base 61, 3 pics, speed 8   (61 % 3 == 1)
 *
 * The tables are keyed by name here because this port has no picture numbers.
 * P_ANIM_FAMILIES carries those two, with the base indices the WAD gives them,
 * so the phase is upstream's and not "frame 0 first".
 *
 * TIMING, AND THE ONE-TIC TRAP
 *
 * P_Ticker runs P_UpdateSpecials *before* `leveltime++`, and the frame is
 * rendered after it. So the picture a frame uses was selected with the clock at
 * `leveltime - 1`. `p_level_t::animTime` is that clock: the leveltime of the
 * last p_update_specials() call, -1 before the first one (upstream's
 * translation arrays are the identity until then). Reading `leveltime` at draw
 * time instead would show every animation one tic early.
 */

/* `textureoffset += FRACUNIT`: one map unit, horizontally. */
#define P_SCROLL_STEP FRACUNIT

/* `animTime` before any P_UpdateSpecials(): the identity translation. */
#define P_ANIM_BEFORE_FIRST (-1)

/* One scrolling line: the line, its front side, and what has accumulated. */
typedef struct p_scroll {
    uint16_t line;
    uint16_t side; /* line->sidenum[0]: the only side upstream scrolls */
    fixed_t dx;
    fixed_t dy;    /* always 0 for special 48 in this upstream */
} p_scroll_t;

/**
 * @brief Adds @p scrolls' accumulation to one parsed sidedef record.
 *
 * This is the arithmetic the renderer's side seam runs, kept here (and only
 * here) so the firmware, the host harnesses and the pixel gate all add the same
 * numbers. Header-only on purpose: a gate that proves the renderer reads the
 * offset does not need the whole world linked to get this function.
 *
 * @param scrolls The level's scrolling lines, or NULL.
 * @param count   How many entries @p scrolls holds.
 * @param index   The sidedef index the renderer just asked for.
 * @param side    The parsed record; its offsets are raised in place.
 */
static inline void p_scroll_adjust_side(const p_scroll_t* scrolls,
                                        uint16_t count, uint16_t index,
                                        doom_side_t* side) {
    uint16_t i;

    if (scrolls == NULL || side == NULL) {
        return;
    }
    for (i = 0; i < count; ++i) {
        if (scrolls[i].side == index) {
            side->textureOffset += scrolls[i].dx;
            side->rowOffset += scrolls[i].dy;
        }
    }
}

/**
 * @brief P_UpdateSpecials(): advances the scrolls and the picture clock.
 *
 * Called once per world tic, after the thinkers and before `leveltime++`
 * (p_level_tick() is that call site, so a game tic reaches it through
 * p_level_tic() and a thinker-only tic still runs it, which is what upstream's
 * P_Ticker does).
 *
 * `levelTimer` (-timer/-avg, a deathmatch option this port has no parser for)
 * is not implemented: G_ExitLevel() is not p_spec.c's to call and no shipped
 * build sets it.
 *
 * @param level The level; NULL or a closed level changes nothing.
 */
void p_update_specials(struct p_level* level);

/**
 * @brief The picture the animation tables select for @p name at @p animTime.
 *
 * Upstream's `texturetranslation[i]`/`flattranslation[i]` lookup, with the
 * measured families above. A name outside every family, or @p animTime of
 * P_ANIM_BEFORE_FIRST, is returned unchanged (the identity table).
 *
 * @param animTime The leveltime of the last p_update_specials(), or -1.
 * @param name     The packed picture name a sidedef or a sector carries.
 * @return The frame's packed name.
 */
r_name_t p_anim_frame(int32_t animTime, r_name_t name);

/**
 * @brief True when the animation tables name @p name as one of their frames.
 *
 * The predicate the bake and the gate use to ask "does this picture animate at
 * all", which `p_anim_frame()` cannot answer at a phase that happens to be the
 * identity.
 *
 * @param name A packed picture name.
 * @return 1 when some family holds it, 0 otherwise.
 */
int p_anim_is_animated(r_name_t name);

/* The two families the shareware WAD has, and the widest family's picture
 * count. The translation table P_UpdateSpecials() writes holds every name of
 * every family, family by family. */
#define P_ANIM_FAMILIES 2
#define P_ANIM_MAXPICS 3
#define P_ANIM_PICTURES (P_ANIM_FAMILIES * P_ANIM_MAXPICS)

/**
 * @brief Copies the animation families' source names into @p out, family by
 *        family, for the per-sector refresh P_UpdateSpecials() runs.
 *
 * The families themselves are `static const` and live in flash on the board;
 * that loop runs once per sector per tic, immediately after the thinker walk has
 * thrashed the flash cache, so it compares against this RAM copy instead.
 *
 * @param out P_ANIM_PICTURES packed names.
 */
void p_anim_names(r_name_t out[P_ANIM_PICTURES]);

/**
 * @brief Binds a level's live state to a map for rendering.
 *
 * Installs the seams doom_map.h documents:
 *
 *   readSector     the level's mutable `sectors[]` (heights, flats, light and
 *                  the animation frame of each flat), which is this port's
 *                  replacement for upstream's live `sector_t sectors[]` pointer;
 *   adjustSide     the scroll offsets (`p_scroll_adjust_side`);
 *   translateName  the animation tables (`p_anim_frame` at `animTime`) -- only
 *                  when the map's SIDEDEFS name a texture some family holds. A
 *                  map that names none of them cannot have a translated side, so
 *                  the seam stays uninstalled and the per-segment side read does
 *                  not pay for a translation that can only be the identity.
 *
 * The caller owns both objects and must keep them alive while the map is used;
 * the map is the renderer's, and this is the only place a level writes into it.
 * tools/ and the firmware call this, and a probe that renders the static map
 * simply never does.
 *
 * @param level The open level whose state the renderer must see.
 * @param map   The map the walk will be given; must outlive the binding.
 */
void p_level_bind_render(struct p_level* level, doom_map_t* map);

/**
 * @brief Clears the three seams p_level_bind_render() installed.
 *
 * Leaves the map valid for the immutable view again; it does not touch the
 * level. Safe on a NULL map or one that was never bound.
 */
void p_level_unbind_render(doom_map_t* map);

/**
 * @brief doom_sector_read_fn over a level: the live sector record.
 *
 * This is what p_level_bind_render() installs, exported because the gate
 * harnesses bind the same seam explicitly, and a second implementation of "copy
 * the seven map-facing fields" is exactly the drift this port has paid for
 * before.
 */
void p_level_read_sector(void* context, uint16_t index, doom_sector_t* out);

/**
 * @brief doom_side_adjust_fn over a level: the accumulated scroll.
 */
void p_level_adjust_side(void* context, uint16_t index, doom_side_t* side);

/**
 * @brief doom_name_translate_fn over a level: the animation tables.
 */
r_name_t p_level_translate_name(void* context, r_name_t name);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_P_SPEC_H */
