/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

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
#include "p_user.h"
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
 *   P_SETUP_ERR_ALLOC     the arena refused the sector, line-special or
 *                         sector-sound array
 *   P_SETUP_ERR_NOT_OPEN  p_level_reload() was given a level that is not loaded
 *
 * A refused init leaves the level zeroed (`open` 0), so a stale level can never
 * answer a later tick. A refused reload leaves the same way: p_level_init()
 * zeroes the state before it validates anything, and the caller's map and arena
 * are still the caller's.
 */
typedef enum p_setup_error {
    P_SETUP_OK = 0,
    P_SETUP_ERR_NULL_LEVEL = 1,
    P_SETUP_ERR_NULL_MAP = 2,
    P_SETUP_ERR_MAP_NOT_OPEN = 3,
    P_SETUP_ERR_NULL_ARENA = 4,
    P_SETUP_ERR_ARENA = 5,
    P_SETUP_ERR_ALLOC = 6,
    P_SETUP_ERR_NOT_OPEN = 7
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
 *   random          the P_Random() position this level has reached. A reload
 *                   carries it over (p_level_reload), because upstream seeds the
 *                   sequence in G_InitNew and not in a level load.
 *   rules           the filters and the spawn inputs, as loaded.
 *   context         the same four things plus the rules, in the shape the spawn
 *                   functions take. It also points at the level's per-sector
 *                   sound state (`sectorSound`, p_spec.h) that P_RecursiveSound
 *                   floods; that array is allocated from this arena, not stored
 *                   in p_sector_t and not inline in the context, because the
 *                   board's .bss has no room for it and a level change frees it
 *                   with every other PU_LEVEL block.
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
 *   playerState     the console player's record (upstream players[0]): the view
 *                   height, the counters and this tic's command (p_user.h).
 *                   `player` keeps naming the avatar mobj, because the two
 *                   existing gates read that pointer and are not this task's.
 *   leveltime       P_Ticker's tic counter since the level loaded. P_CalcHeight
 *                   uses it for the bob angle; G_DoCompleted's par time (D4.3)
 *                   reads it too.
 *   scrolls         one entry per line special 48, in line order (upstream's
 *                   `linespeciallist[]`), allocated from this arena by
 *                   p_spawn_specials(). Each holds the front side and the
 *                   offset P_UpdateSpecials has accumulated on it.
 *   scrollCount     how many entries @c scrolls holds; also the number the
 *                   renderer's side seam walks.
 *   animTime        the leveltime of the last P_UpdateSpecials, i.e. the clock
 *                   the picture translations were computed with. -1
 *                   (P_ANIM_BEFORE_FIRST) until the first one, which is
 *                   upstream's identity translation array.
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
 *   exitRequest     G_ExitLevel()/G_SecretExitLevel()'s write, recorded on the
 *                   level because this port has no game globals (p_spec.h). A
 *                   level load clears it; only a line trigger sets it.
 *   open            1 after a successful load, 0 after shutdown or a refusal.
 */
typedef struct p_level {
    const doom_map_t* map;
    zone_state_t zone;
    p_thinker_list_t thinkers;
    p_action_stats_t actions;
    m_random_t random;
    int32_t leveltime;
    p_spawn_rules_t rules;
    p_mobj_context_t context;

    p_sector_t* sectors;
    uint16_t sectorCount;
    int16_t* lineSpecial;
    uint16_t lineCount;
    p_spec_stats_t specStats;
    p_scroll_t* scrolls;
    uint16_t scrollCount;
    int32_t animTime;

    r_mobj_t* player;
    p_player_t playerState;
    p_mapthing_t playerStarts[MAXPLAYERS];
    uint8_t playerStartSeen[MAXPLAYERS];
    p_mapthing_t deathmatchStarts[P_MAX_DEATHMATCH_STARTS];
    uint32_t deathmatchStartCount;

    int32_t totalKills;
    int32_t totalItems;
    int32_t totalSecrets;

    p_level_stats_t stats;
    p_setup_error_t error;
    uint8_t exitRequest; /* p_exit_request_t */
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
 * @brief Loads the same level again, over the same arena: a level reload.
 *
 * This is the half of P_SetupLevel() that a single-player death needs. Upstream
 * answers a PST_REBORN player with `gameaction = ga_loadlevel`, and
 * G_DoLoadLevel reloads the level `gamemap` still names -- the same map, not a
 * transition. This function is that reload for a caller that already owns the
 * map, the arena and the rules:
 *
 *   1. read the map, the rules, the random position and the arena (base and
 *      size) out of @p level;
 *   2. load over the same arena, which discards every PU_LEVEL block and re-runs
 *      THINGS, with the carried random position already in place;
 *   3. reinstate the caller's hooks.
 *
 * WHAT IS DELIBERATELY NOT RESET, AND WHY
 *
 * THE RANDOM POSITION. linuxdoom-1.10 seeds the sequence in M_ClearRandom(),
 * which is G_InitNew()'s -- a new game, not a level load. P_SetupLevel() draws
 * from wherever `rndindex` already is: P_LoadThings() consumes one draw per
 * spawned thing for `lastlook` (P_SpawnMobj) and P_SpawnMapThing's tic jitter,
 * then P_SpawnSpecials draws for the lights. p_level_init() reseeds because it
 * is also the first load of a level, where G_InitNew() did; a reload hands the
 * carried position to the load itself, so those draws read the same numbers
 * upstream's would. A demo that reloaded after a death would otherwise
 * desynchronise on the first monster's `lastlook`.
 *
 * THE CALLER'S HOOKS. `p_mobj_context_t`'s moveStats, attackStats, sightStats
 * and actionTrace belong to the caller (a harness, p_mobj.h), not to the level,
 * and p_level_init() cannot see them; they are reinstated after the load so a
 * counter kept across a death keeps counting.
 *
 * NOT DONE HERE, AND WHY: p_spawn_specials(). p_level_init() leaves it out for
 * the same reason it always does (see above: it consumes the random sequence,
 * and the THINGS-only gates must not), so a caller composing P_SetupLevel's
 * order calls it after this function -- which is exactly what G_DoLoadLevel's
 * caller does. g_game.c's g_demo_reborn() is that caller.
 *
 * The arena is not grown: z_init() re-lays one free block over the caller's
 * buffer, so the bytes in use after the load are the bytes in use after any
 * number of reloads. That is an invariant a host test pins, because a reload
 * that allocated a second arena would look correct for one death and fail on
 * the board at the second.
 *
 * @param level The level to reload; it must be open.
 * @return P_SETUP_OK, P_SETUP_ERR_NULL_LEVEL, P_SETUP_ERR_NOT_OPEN, or any
 *         p_level_init() code (after which the level is zeroed, as a refused
 *         load always leaves it).
 */
p_setup_error_t p_level_reload(p_level_t* level);

/**
 * @brief Loads a different level over the same arena: a level transition.
 *
 * This is the half of P_SetupLevel() that a completed level needs. Upstream's
 * G_DoCompleted() names the next map, G_DoWorldDone() writes `gamemap =
 * wminfo.next+1`, and G_DoLoadLevel() runs P_SetupLevel for that map. This
 * function is the P_SetupLevel step for a caller that has already opened the
 * next map and owns the arena, the rules and the random position:
 *
 *   1. read the rules, the random position, the arena (base and size) and the
 *      caller's hooks out of @p level;
 *   2. load @p map over the same arena, which discards every PU_LEVEL block and
 *      runs that map's THINGS, with the carried random position already in
 *      place;
 *   3. reinstate the caller's hooks.
 *
 * HOW IT DIFFERS FROM p_level_reload(): only the map. A reload names
 * `level->map`, the level the game is already on; a change names the caller's
 * next map. Everything else is the same load, for the same reasons: the arena
 * is re-laid rather than grown, `leveltime` returns to 0, the player record is
 * rewired to the avatar the new map's type-1 THING spawns, and the P_Random()
 * position is carried, because M_ClearRandom() belongs to G_InitNew() and a
 * level load has never reseeded the sequence.
 *
 * The caller must keep @p map alive for as long as @p level uses it, exactly as
 * p_level_init() requires. p_spawn_specials() is still the caller's next step
 * (P_SetupLevel's order); g_game.c's g_do_world_done() is that caller.
 *
 * A NULL @p map is refused with P_SETUP_ERR_NULL_MAP and leaves the open level
 * untouched: there is no new world to load, and zeroing a live level would turn
 * a caller's bad argument into a dead game.
 *
 * @param level The level to change; it must be open.
 * @param map   The next level's validated map view.
 * @return P_SETUP_OK, P_SETUP_ERR_NULL_LEVEL, P_SETUP_ERR_NULL_MAP,
 *         P_SETUP_ERR_NOT_OPEN, or any p_level_init() code (after which the
 *         level is zeroed, as a refused load always leaves it).
 */
p_setup_error_t p_level_change(p_level_t* level, const doom_map_t* map);

/**
 * @brief Frees every PU_LEVEL block and zeroes the level.
 *
 * The arena buffer stays owned by the caller; only the zone's view of it is
 * reset. Calling it on a closed or zeroed level is a no-op.
 */
void p_level_shutdown(p_level_t* level);

/**
 * @brief Runs one tic of the world alone: the thinkers, then P_UpdateSpecials.
 *
 * This is P_Ticker's middle, exactly: p_run_thinkers() over this list and then
 * p_update_specials() (the scroll offsets and the animation clock), both before
 * `leveltime++` -- which P_Ticker's caller owns (p_level_tic()).
 *
 * There is no player step here on purpose; p_level_tic() is the game's tic and
 * calls this function after P_PlayerThink(). The existing gates call this one
 * because their oracle has no player (see p_level_tic's comment).
 *
 * Removed mobjs are unlinked and returned to the level's zone during the walk,
 * which is the deferred half of P_RemoveMobj(). `stats` is optional.
 *
 * @return P_TICK_OK, P_TICK_ERR_NULL_LIST (no level), P_TICK_ERR_NOT_OPEN, or
 *         P_TICK_ERR_NULL_RELEASE (cannot happen with a valid level).
 */
p_tick_error_t p_level_tick(p_level_t* level, p_thinker_run_t* stats);

/**
 * @brief P_Ticker(): the player's commands, then every thinker, then time.
 *
 * This is the game's tic, in upstream's order (p_tick.c P_Ticker):
 *
 *   1. P_PlayerThink() for the level's player, when one is wired;
 *   2. p_level_tick(): P_RunThinkers() over the same list;
 *   3. leveltime++.
 *
 * WHY THIS IS A SECOND ENTRY POINT
 *
 * p_level_tick() is deliberately thinkers-only. The two existing procedural
 * gates -- tools/specials_check.py and tools/things_check.py -- simulate a level
 * *without* a player and compare it, byte for byte, with an oracle that has no
 * player either. Growing p_level_tick() to run P_PlayerThink would give both
 * gates an avatar they never asked for and change every compared line. The
 * split makes "a game tic" opt-in: a caller that wants the player calls
 * p_level_tic(), one that wants the world alone keeps p_level_tick().
 *
 * The player step is skipped when the level has no player (a map with no type-1
 * start). `stats` receives the thinker walk's counters exactly as before.
 *
 * DEFINED IN p_user.c
 *
 * p_level_tick() stays p_setup.c's; this function is the player layer's and is
 * defined in p_user.c. That is not cosmetic: tools/specials_check.py and
 * tools/things_check.py build p_setup.c with their own hard-coded source list
 * and must keep linking without p_user.c. A p_setup.c that referenced
 * p_player_think() would break those two gates' builds, which no behaviour
 * change is allowed to do.
 *
 * @param level The level; must be open.
 * @param cmd   This tic's command; NULL is one tic of no input.
 * @param stats Optional out for the thinker walk (p_tick.h).
 * @return P_TICK_OK, P_TICK_ERR_NULL_LIST (no level), or P_TICK_ERR_NOT_OPEN.
 */
p_tick_error_t p_level_tic(p_level_t* level, const p_ticcmd_t* cmd,
                           p_thinker_run_t* stats);

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
