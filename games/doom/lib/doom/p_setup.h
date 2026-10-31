/*
 * Level initialisation: the THINGS run, and the world a level owns.
 *
 * P_SetupLevel() (p_setup.c) builds a level in a fixed order: the map lumps,
 * then P_LoadThings(), then the specials, then P_GroupLines(). This layer is the
 * THINGS step and the state that step produces -- the zone the level's objects
 * live in, the thinker list they tick on, the random sequence they consume, and
 * the counters the intermission reads. It does not read the map: the caller
 * opens it with doom_map_open() and hands the validated view over.
 *
 * WHY THE BOOKKEEPING LIVES HERE
 *
 * Vanilla keeps the totals Phase 4 needs as four globals: `totalkills`,
 * `totalitems`, `totalsecret` and the player/deathmatch start arrays. They are
 * the denominators of the intermission screen ("Kills 34/63"), so a loader that
 * drops one thing silently changes a number the player sees. They are therefore
 * counted here, in one place, from the outcome P_SpawnMapThing() reports -- and
 * `p_level_stats_t` records *which* filter dropped the rest, so a level that
 * spawns nothing can be explained instead of guessed at.
 *
 * WHAT ONE LEVEL COSTS
 *
 * The arena is the hard budget: every mobj is sizeof(r_mobj_t) (120 B) plus a
 * ZONE_HEADER_SIZE (16 B) block header, so E1M1's spawned things cost
 * 136 B each. p_level_stats_t::spawned is the count; the caller sizes the arena
 * from it once, and z_stats() reports the rest every frame.
 *
 * This is C99 only: <stddef.h> and <stdint.h>, no allocation, no globals, no
 * stdio, no floating point. The caller owns the state struct and the arena.
 */
#ifndef DOOM_P_SETUP_H
#define DOOM_P_SETUP_H

#include <stddef.h>
#include <stdint.h>

#include "doom_map.h"
#include "m_random.h"
#include "p_action.h"
#include "p_mobj.h"
#include "p_spec.h"
#include "p_tick.h"
#include "z_zone.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Upstream's deathmatchstarts[10] (p_setup.c). A map may carry more type-11
 * records; the first ten are kept, exactly as `if (deathmatch_p <
 * &deathmatchstarts[10])` does. */
#define P_MAX_DEATHMATCH_STARTS 10

/*
 * What one level load did. Every field counts THINGS records in the branch
 * named; `things` is the number of records read, and the accounting identity is
 *
 *   things == spawned + deathmatchStarts + skippedSkill + skippedSingle
 *           + skippedNoMonsters + skippedDeathmatch + unknownType
 *           + invalidThing + spawnFailed + playerStartsWithoutMobj
 *
 * where `spawned` counts every mobj the load created (player avatars included)
 * and a player start whose slot is not playing contributes to `things` and to
 * no other field. A player start that does spawn is counted once, in `spawned`,
 * and once in `playerStarts`; the two counters answer different questions ("how
 * many records" and "how many mobjs") and are meant to differ.
 */
typedef struct p_level_stats {
    uint32_t things;             /* THINGS records read */
    uint32_t spawned;            /* mobjs created, player included */
    uint32_t skippedSkill;       /* options bit clear for this skill */
    uint32_t skippedSingle;      /* options bit 16 in single player */
    uint32_t skippedNoMonsters;  /* -nomonsters dropped a monster */
    uint32_t skippedDeathmatch;  /* deathmatch dropped an MF_NOTDMATCH thing */
    uint32_t unknownType;        /* no mobjinfo record has this doomednum */
    uint32_t invalidThing;       /* type <= 0: not a thing at all */
    uint32_t spawnFailed;        /* the zone or the map refused a spawn */
    uint32_t playerStarts;       /* type 1..4 records seen */
    uint32_t deathmatchStarts;   /* type 11 records seen */
} p_level_stats_t;

/*
 *   P_SETUP_OK            the level loaded
 *   P_SETUP_ERR_NULL_LEVEL no level state was passed
 *   P_SETUP_ERR_NULL_MAP  no map was passed
 *   P_SETUP_ERR_MAP_NOT_OPEN the map was never accepted by doom_map_open()
 *   P_SETUP_ERR_NULL_ARENA no arena buffer was passed
 *   P_SETUP_ERR_ARENA     z_init() refused the arena
 *   P_SETUP_ERR_ALLOC     the arena refused the sector or line-special array
 *
 * A refused init leaves the level zeroed (`open` 0), so a stale level can never
 * answer a later tick.
 */
typedef enum p_setup_error {
    P_SETUP_OK = 0,
    P_SETUP_ERR_NULL_LEVEL = 1,
    P_SETUP_ERR_NULL_MAP = 2,
    P_SETUP_ERR_MAP_NOT_OPEN = 3,
    P_SETUP_ERR_NULL_ARENA = 4,
    P_SETUP_ERR_ARENA = 5,
    P_SETUP_ERR_ALLOC = 6
} p_setup_error_t;

/*
 * One level's world, owned by the caller. The pointers inside `context` point at
 * this struct's own fields, so the struct must not be copied or moved after
 * p_level_init() -- a pointer to it may be, and should be, handed to code that
 * ticks the level.
 *
 * Fields:
 *
 *   map             the validated map view; outlives the level.
 *   zone            the level's arena, initialised over the caller's buffer.
 *   thinkers        every simulated object, in insertion order.
 *   actions         the state-action counters (see p_action.h).
 *   random          the P_Random() position this level has reached.
 *   rules           the filters and the spawn inputs, as loaded.
 *   context         the same four things plus the rules, in the shape the spawn
 *                   functions take.
 *   sectors         the level's mutable sector state (p_spec.h), copied from
 *                   SECTORS by p_level_init(). Every mover, light and
 *                   surrounding-sector query reads and writes this array; the
 *                   map's sector records stay the immutable source.
 *   sectorCount     how many entries @c sectors holds.
 *   lineSpecial     the mutable `line->special` copy, one per linedef, so a
 *                   one-shot trigger can clear it without writing to the map.
 *   lineCount       how many entries @c lineSpecial holds.
 *   specStats       what p_spawn_specials() and the movers did (p_spec.h).
 *   player          the mobj of the console player's start (type 1), or NULL.
 *   playerStarts    the four start records, verbatim (upstream playerstarts[]).
 *   playerStartSeen one flag per slot: the record was present in THINGS.
 *   deathmatchStarts the first ten type-11 records (upstream deathmatchstarts[]).
 *   deathmatchStartCount how many of those slots are filled.
 *   totalKills      MF_COUNTKILL things spawned (upstream totalkills).
 *   totalItems      MF_COUNTITEM things spawned (upstream totalitems).
 *   totalSecrets    the secret count; p_spawn_specials() raises it for every
 *                   sector special 9, which is where upstream's totalsecret++
 *                   lives. Zero until that call.
 *   stats           the load's branch counts.
 *   error           the result of the last p_level_init().
 *   open            1 after a successful load, 0 after shutdown or a refusal.
 */
typedef struct p_level {
    const doom_map_t* map;
    zone_state_t zone;
    p_thinker_list_t thinkers;
    p_action_stats_t actions;
    m_random_t random;
    p_spawn_rules_t rules;
    p_mobj_context_t context;

    p_sector_t* sectors;
    uint16_t sectorCount;
    int16_t* lineSpecial;
    uint16_t lineCount;
    p_spec_stats_t specStats;

    r_mobj_t* player;
    p_mapthing_t playerStarts[MAXPLAYERS];
    uint8_t playerStartSeen[MAXPLAYERS];
    p_mapthing_t deathmatchStarts[P_MAX_DEATHMATCH_STARTS];
    uint32_t deathmatchStartCount;

    int32_t totalKills;
    int32_t totalItems;
    int32_t totalSecrets;

    p_level_stats_t stats;
    p_setup_error_t error;
    int open;
} p_level_t;

/* --- Public API ---------------------------------------------------------- */

/**
 * @brief Loads a level: initialises the arena, then runs THINGS through
 *        P_SpawnMapThing().
 *
 * This is P_SetupLevel()'s THINGS step and the state around it:
 *
 *   1. zero the level state;
 *   2. z_init() the arena (a level change discards whatever the arena held);
 *   3. reset the thinker list, the action counters and the random sequence;
 *   4. copy SECTORS into the mutable `sectors[]` array (P_LoadSectors) and
 *      LINEDEFS' `special` field into `lineSpecial[]` (P_LoadLineDefs);
 *   5. for every THINGS record, in file order, p_spawn_map_thing();
 *   6. record the player and deathmatch starts, count MF_COUNTKILL and
 *      MF_COUNTITEM spawns, and count which branch dropped the rest.
 *
 * It deliberately does *not* call p_spawn_specials(): that is P_SetupLevel's
 * next step, it consumes the random sequence (light flashes and strobes draw),
 * and keeping it separate is what keeps the THINGS-only dump comparable. The
 * game -- and tools/specials_check.c -- calls p_level_init() then
 * p_spawn_specials().
 *
 * Order matters and is upstream's: P_LoadThings() walks the records in file
 * order, which is why the P_Random() sequence a load consumes is fixed and
 * comparable between the C loader and the oracle.
 *
 * A record that fails to spawn does not fail the load: the counter is the
 * refusal, and a level whose arena is too small reports `spawnFailed` for every
 * thing instead of crashing. Sound and HUD work that P_SetupLevel also does is
 * not here.
 *
 * @param level  Caller-owned state; zeroed on every call.
 * @param map    An open map view; must outlive the level.
 * @param arena  The level's arena; ZONE_ALIGNMENT-aligned.
 * @param size   Bytes available at @p arena.
 * @param rules  The filters, or NULL for p_spawn_rules_default().
 * @return P_SETUP_OK or one of the p_setup_error_t codes.
 */
p_setup_error_t p_level_init(p_level_t* level, const doom_map_t* map, void* arena,
                             size_t size, const p_spawn_rules_t* rules);

/**
 * @brief Frees every PU_LEVEL block and zeroes the level.
 *
 * The arena buffer stays owned by the caller; only the zone's view of it is
 * reset. Calling it on a closed or zeroed level is a no-op.
 */
void p_level_shutdown(p_level_t* level);

/**
 * @brief Runs one tic: p_run_thinkers() over this level's list.
 *
 * Removed mobjs are unlinked and returned to the level's zone during the walk,
 * which is the deferred half of P_RemoveMobj(). `stats` is optional.
 *
 * @return P_TICK_OK, P_TICK_ERR_NULL_LIST (no level), P_TICK_ERR_NOT_OPEN, or
 *         P_TICK_ERR_NULL_RELEASE (cannot happen with a valid level).
 */
p_tick_error_t p_level_tick(p_level_t* level, p_thinker_run_t* stats);

/**
 * @brief The release callback for level-owned thinkers, for a direct
 *        p_run_thinkers() call.
 *
 * @p context must be the level's p_mobj_context_t (i.e. &level->context); the
 * node is returned to the zone that context names. Exposed rather than static
 * because a caller that drives the thinker list itself -- a test, the harness --
 * must use the same release the level does.
 */
void p_level_release_thinker(void* context, p_thinker_t* thinker);

/**
 * @brief How many live mobjs the level holds, by walking the thinker list.
 *
 * O(n) and intended for reporting, not for a per-frame decision: it visits every
 * node and counts the ones tagged P_THINKER_MOBJ.
 */
uint32_t p_level_mobj_count(const p_level_t* level);

/**
 * @brief The first mobj in thinker (insertion) order, or NULL.
 *
 * p_level_next_mobj() continues the walk; both skip non-mobj thinkers, so the
 * pair keeps working when D4.1b adds sector thinkers to the same list.
 */
const r_mobj_t* p_level_first_mobj(const p_level_t* level);
const r_mobj_t* p_level_next_mobj(const r_mobj_t* mobj);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_P_SETUP_H */
