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
 *   4   fast strobe + damage    strobe, special kept       implemented (the
 *                                                          damage half is D4.2)
 *   8   glowing light           P_SpawnGlowingLight        implemented
 *   9   secret sector           totalsecret++              implemented
 *   10  door closes in 30s      P_SpawnDoorCloseIn30       implemented
 *   12  slow strobe, synced     P_SpawnStrobeFlash(SLOW,1) implemented
 *   13  fast strobe, synced     P_SpawnStrobeFlash(FAST,1) implemented
 *   14  door raises in 5 min    P_SpawnDoorRaiseIn5Mins    implemented
 *   17  fire flicker            P_SpawnFireFlicker         implemented
 *   5,7,11,16  damage/exit floors, and every value not listed above
 *                              the player-in-sector effects, owned by D4.2;
 *                              counted as `unsupportedSectorSpecial`
 *
 * Line specials (P_CrossSpecialLine, P_UseSpecialLine, P_ShootSpecialLine):
 * every number whose EV function exists below is dispatched; stairs, teleports,
 * exits, texture scrolling (48) and the remaining switch numbers are counted as
 * `unsupportedLineSpecial` / `deferredLineSpecial`. The header of each EV file
 * lists its own type enumeration against upstream.
 *
 *   P_CrossSpecialLine, one-shot (the number is cleared)
 *     2,3,4 doors; 5,19,36,37,38,40,56,58,59,119,130 floors;
 *     6,25,40,44,141 ceilings; 10,22,53,54,121 plats;
 *     12,13,17,35,104 lights          implemented
 *     30,96 raiseToTexture            deferred (needs textureheight[], r_data)
 *   P_CrossSpecialLine, retrigger (the number stays)
 *     72,73,77 ceilings; 75,76,86,90 doors;
 *     82,83,84,91..94,98,128,129 floors;
 *     87,88,89 plats; 79,80,81 lights implemented
 *     8,100 stairs; 39,97,125,126 teleport; 48 scroll; 52,124 exit
 *                                     unsupported by this layer
 *   P_UseSpecialLine
 *     1,26,27,28,31,32,33,34,117,118  manual doors, implemented (keys: D5)
 *     9,14,15,18,20,21,23,29,41,42,43,45,49,50,55,60..71,99,101,
 *     102,103,111..116,122,123,131..140
 *                                     EV implemented; the switch *texture*
 *                                     change is deferred (see p_switch note)
 *     7,127 stairs; 11,51 exits       deferred / unsupported
 *   P_ShootSpecialLine
 *     24,46,47                        EV implemented; switch texture deferred
 *
 * p_switch.* is deliberately not part of this task: the gate triggers line
 * specials directly, and P_ChangeSwitchTexture writes sidedef texture state,
 * which doom_map exposes as const zero-copy bytes. D4.2 owns mutable side state
 * and the player's use path.
 *
 * NOT IMPLEMENTED AT ALL, DELIBERATELY
 *
 *   - P_ChangeSector's crush *damage*. The height clip, the step and the
 *     "does not fit" answer are p_map.c's p_change_sector(); P_DamageMobj is
 *     p_inter.c's, which D4.2d owns. Every thing upstream would have damaged
 *     raises `p_spec_stats_t::unsupportedCrush` instead of being dropped
 *     silently.
 *   - EV_BuildStairs, EV_Teleport, G_ExitLevel/G_SecretExitLevel, the level
 *     timer (-avg/-timer), texture animation and P_UpdateSpecials' scrolling.
 *   - EV_SlidingDoor (compiled out upstream too).
 *
 * WHAT ONE SECTOR COSTS
 *
 * sizeof(p_sector_t) on the 32-bit target is the sector's mutable state plus
 * one pointer; E1M1's 85 sectors are about 3.4 KB. E1M1's 475 linedef specials
 * are 950 B. The level's arena pays for both, and p_level_init() reports an
 * allocation failure rather than loading a world without them.
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
typedef struct p_sector {
    fixed_t floorHeight;
    fixed_t ceilingHeight;
    r_name_t floorPic;
    r_name_t ceilingPic;
    int16_t lightLevel;
    int16_t special;
    int16_t tag;
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
 *   unsupportedLineSpecial  a trigger whose EV function does not exist here
 *   deferredLineSpecial     a trigger whose EV exists but does something later
 *   lineScrolls             line special 48, P_UpdateSpecials' job
 *   deathmatchSpecials      P_SpawnDeathmatchSpecials' counter (see p_spec.c)
 *   changeSectorCalls       every P_ChangeSector() call, from every mover
 *   changeSectorClear       the calls that answered "clear"
 *   unsupportedCrush        a thing that did not fit and that upstream's
 *                           PIT_ChangeSector would have damaged; P_DamageMobj
 *                           is p_inter.c's and D4.2d has not written it
 *   thingRefreshes          cached mobj floorz/ceilingz updates
 *   allocationFailures      the zone refused a thinker record
 *   adjacentOverflow        a sector with more than 20 neighbours
 *   triggers                p_trigger_special_line() calls that ran an EV
 *   triggersRefused         a trigger refused by a mode/player check
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
    uint32_t unsupportedCrush;
    uint32_t thingRefreshes;
    uint32_t allocationFailures;
    uint32_t adjacentOverflow;
    uint32_t triggers;
    uint32_t triggersRefused;
} p_spec_stats_t;

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
 *   3. for line 0..numlines-1: special 48 is a scrolling effect, counted here
 *      and left to P_UpdateSpecials (D4.x).
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
 * PIT_ChangeSector's crush damage is P_DamageMobj's (D4.2d); each thing that
 * would have been damaged raises `specStats->unsupportedCrush`.
 *
 * @return 1 when something does not fit, 0 when the sector is clear.
 */
int p_spec_p_change_sector(struct p_mobj_context* world, uint16_t sector);

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
 * @brief Runs one line's special, with upstream's trigger rules.
 *
 * Reads the level's mutable `lineSpecial[]`, dispatches to the EV function the
 * number names, and clears the number for a one-shot trigger exactly as
 * upstream's `line->special = 0` does (retriggers are left alone). `byPlayer`
 * feeds the two mode checks upstream makes: a non-player thing may only cross
 * the short list in P_CrossSpecialLine, and may only use 1/32/33/34.
 *
 * The gate triggers lines directly because D4.2 owns P_UseLines/P_TryMove; this
 * is the same entry point those will call.
 *
 * @return 1 when an EV function ran (or a deferred effect was recognised),
 *         0 when the line has no special or the trigger was refused.
 */
int p_trigger_special_line(struct p_level* level, uint16_t lineIndex,
                           p_trigger_kind_t kind, int byPlayer);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_P_SPEC_H */
