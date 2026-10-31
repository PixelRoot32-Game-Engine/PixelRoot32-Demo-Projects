/*
 * Map objects: the thing itself, how it spawns, how it is removed, and how its
 * state machine advances.
 *
 * This is upstream's p_mobj.h/p_mobj.c reduced to what D4.1a needs and shaped
 * so D4.2 does not have to restructure it. Every field below is one upstream
 * reads; the comments say which. The simulation that *uses* most of them
 * (movement, line attacks, the blockmap) is D4.2's, not this layer's.
 *
 * THE ZONE IS THE ALLOCATOR
 *
 * P_SpawnMobj() allocates the mobj from the caller's zone with PU_LEVEL
 * (p_mobj.c: `mobj = Z_Malloc (sizeof(*mobj), PU_LEVEL, NULL)`), so a level's
 * whole population is freed as one group on level change and the board's RAM
 * budget stays an invariant instead of a hope. This port does the same, with
 * its own record size: sizeof(r_mobj_t) is 120 bytes on the 32-bit target
 * (measured in the xtensa build), against upstream's record, which is larger
 * because it also carries the sector and blockmap link pointers this one does
 * not yet have. A zone block costs ZONE_HEADER_SIZE (16) more, so one mobj is
 * 136 B: E1M1's 138 THINGS records do not all become mobjs -- the skill and
 * single-player filters drop some -- but the cost per mobj is 136 B either way.
 *
 * WHAT IS NOT HERE YET, DELIBERATELY
 *
 *   - the sector and blockmap links (`snext`/`sprev`, `bnext`/`bprev` and the
 *     `P_SetThingPosition` lists). They are collision and sound inputs, so they
 *     arrive with D4.2's movement; a mobj holds `subsector` and `sector` already
 *     because the spawn position needs them.
 *   - P_XYMovement, P_ZMovement and P_MobjThinker's nightmare respawn. The
 *     thinker below advances states only.
 *
 * SPAWN POSITION: WHAT VANILLA DOES, AND WHAT IT DOES NOT
 *
 * P_SpawnMobj() has three cases and no fourth:
 *
 *   z == ONFLOORZ     mobj->z = floorz
 *   z == ONCEILINGZ   mobj->z = ceilingz - info->height
 *   otherwise         mobj->z = z
 *
 * floorz and ceilingz are the heights of the sector the thing's own subsector
 * belongs to (P_SetThingPosition then `subsector->sector`). There is **no
 * clamping**: a thing taller than its sector, or one whose THINGS record names
 * a z outside the sector, spawns where vanilla puts it and P_ZMovement() moves
 * it on the first tic. Adding a clamp here would change the first tic, and the
 * first tic is what a demo's random sequence is measured against.
 *
 * ONFLOORZ/ONCEILINGZ are upstream's MININT/MAXINT sentinels (p_local.h), used
 * as macros with the same names so a reader coming from the 1993 source
 * recognises the call sites.
 *
 * SPAWNING DOES NOT RUN ACTIONS
 *
 * P_SpawnMobj() sets sprite/frame/tics/state directly and says why: "do not set
 * the state with P_SetMobjState, because action routines can not be called
 * yet". This port copies that: only p_set_mobj_state() dispatches actions.
 *
 * This is C99 only: <stdint.h>, no allocation, no globals, no stdio, no
 * floating point. The caller owns the arena, the thinker list and the counters.
 */
#ifndef DOOM_P_MOBJ_H
#define DOOM_P_MOBJ_H

#include <stdint.h>

#include "doom_map.h"
#include "info_tables.h"
#include "m_fixed.h"
#include "m_random.h"
#include "p_tick.h"
#include "z_zone.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Upstream's MAXPLAYERS: the player-start filter's index range and the modulus
 * of P_SpawnMobj()'s `lastlook`. */
#define MAXPLAYERS 4

/* Upstream's ONFLOORZ/ONCEILINGZ (p_local.h), used as the z argument of
 * p_spawn_mobj() to mean "the sector's floor" and "the sector's ceiling minus
 * my height". No real spawn passes either value literally. */
#define ONFLOORZ INT32_MIN
#define ONCEILINGZ INT32_MAX

/* Upstream's MTF_AMBUSH (doomdef.h): the THINGS options bit that makes a
 * monster deaf. Bit 4 (value 16) is "not in single player" and is tested
 * literally by P_SpawnMapThing(); bit 8 is this one. */
#define MTF_AMBUSH 8

/* Upstream's skill_t (doomdef.h). The values are the order of P_SpawnMapThing's
 * `1 << (gameskill-1)` and of the reaction-time test, so they are not free. */
typedef enum p_skill {
    P_SKILL_BABY = 0,
    P_SKILL_EASY = 1,
    P_SKILL_MEDIUM = 2,
    P_SKILL_HARD = 3,
    P_SKILL_NIGHTMARE = 4
} p_skill_t;

/*
 * One THINGS record (upstream mapthing_t): map units, degrees, and the two
 * type/options shorts. It is kept verbatim inside a mobj as `spawnpoint`,
 * because item respawning and nightmare respawn both re-read it.
 */
typedef struct p_mapthing {
    int16_t x;
    int16_t y;
    int16_t angle;
    int16_t type;
    int16_t options;
} p_mapthing_t;

/*
 * The inputs P_SpawnMapThing() tests, all of them upstream globals.
 *
 *   skill         gameskill: selects the options bit and the reaction time.
 *   playerInGame  playeringame[]: a player start spawns a mobj only for a slot
 *                 that is playing. Single player is {1, 0, 0, 0}.
 *   netgame       -netgame: disables the "not in single player" options test.
 *   deathmatch    deathmatch mode: disables player spawning at starts and
 *                 enables the MF_NOTDMATCH filter.
 *   nomonsters    -nomonsters: drops MF_COUNTKILL things and MT_SKULL.
 */
typedef struct p_spawn_rules {
    p_skill_t skill;
    uint8_t playerInGame[MAXPLAYERS];
    uint8_t netgame;
    uint8_t deathmatch;
    uint8_t nomonsters;
} p_spawn_rules_t;

/**
 * @brief The rules a level loads with: skill 3 of 5 ("Hurt me plenty"), one
 *        player in the game, no netgame, no deathmatch, monsters on.
 *
 * Medium is DOOM's own default (the menu's initial selection), so a level
 * loaded without a choice spawns what the 1993 game spawns on a fresh start.
 */
p_spawn_rules_t p_spawn_rules_default(void);

/* The dispatch counters live in p_action.h; the context only points at them, so
 * this header does not have to include that one (which includes this one). */
struct p_action_stats;

/*
 * Everything a spawn needs that is not the spawn's own arguments.
 *
 * `random` may be NULL: the spawn path then leaves `lastlook` at zero and does
 * not consume the sequence, which is what a unit test that does not care about
 * randomness wants. A real level always passes a generator, because the
 * sequence position is part of demo fidelity.
 */
typedef struct p_mobj_context {
    zone_state_t* zone;
    p_thinker_list_t* thinkers;
    const doom_map_t* map;
    m_random_t* random;
    struct p_action_stats* actions;
    p_spawn_rules_t rules;
} p_mobj_context_t;

/*
 * One map object.
 *
 * `thinker` is first and must stay first: the list hands a p_thinker_t* to the
 * thinker function and to the release callback, and both recover the mobj by
 * the address alone.
 *
 * Field by field, against upstream's mobj_t:
 *
 *   x, y, z            position, 16.16. `z` is the bottom of the thing.
 *   momx, momy, momz   momentum, 16.16. Zero for a freshly spawned thing.
 *   angle              facing, a binary angle. Set from the THINGS record.
 *   sprite, frame      the current state's sprite and frame; `frame` carries
 *                      FF_FULLBRIGHT (0x8000), which is why it is 32-bit.
 *   tics, state        the state tic counter and the state's index. `tics` of
 *                      -1 is a permanent state.
 *   floorz, ceilingz   the heights of the sector the thing stands in, copied at
 *                      spawn. Movement keeps them current (D4.2).
 *   radius, height     the collision box and the sprite's extent, from
 *                      mobjinfo. `height` is what ONCEILINGZ subtracts.
 *   flags              the MF_* word, from mobjinfo, plus MF_AMBUSH and the
 *                      player translation bits.
 *   health             spawnhealth, then damage (D4.2).
 *   type               mobjtype_t: the index into mobjinfo[].
 *   subsector, sector  where the thing is, resolved through the BSP at spawn.
 *                      Indices, not pointers: the map is zero-copy and a
 *                      pointer into a mapped flash page is a pointer into a page
 *                      that may be unmapped.
 *   reactiontime       ticks before a monster may attack; 0 on nightmare.
 *   lastlook           the player slot last checked for sight, from P_Random().
 *   movedir, movecount the zig-zag and respawn counters A_Chase uses.
 *   threshold          "chase no matter what" distance.
 *   target, tracer     what it chases, and what a tracer follows.
 *   spawnpoint         the THINGS record it came from, verbatim.
 *   player             1 when this mobj is a player avatar (upstream's `player`
 *                      pointer; the port has one player, D5 owns the rest).
 */
typedef struct r_mobj {
    p_thinker_t thinker;

    fixed_t x;
    fixed_t y;
    fixed_t z;
    fixed_t momx;
    fixed_t momy;
    fixed_t momz;
    angle_t angle;

    int16_t sprite;
    int32_t frame;
    int16_t tics;
    int16_t state;

    fixed_t floorz;
    fixed_t ceilingz;
    fixed_t radius;
    fixed_t height;

    uint32_t flags;
    int32_t health;
    int16_t type;
    uint16_t subsector;
    uint16_t sector;

    int16_t reactiontime;
    int16_t lastlook;
    int16_t movedir;
    int16_t movecount;
    int16_t threshold;

    struct r_mobj* target;
    struct r_mobj* tracer;

    p_mapthing_t spawnpoint;
    uint8_t player;
} r_mobj_t;

/*
 * One code per failure class:
 *
 *   P_MOBJ_OK                  the call did what it says
 *   P_MOBJ_ERR_NULL_WORLD      no context was passed
 *   P_MOBJ_ERR_NO_ZONE         the context has no open zone
 *   P_MOBJ_ERR_NO_THINKERS     the context has no thinker list
 *   P_MOBJ_ERR_NO_MAP          the context has no open map
 *   P_MOBJ_ERR_NULL_MOBJ       no mobj was passed
 *   P_MOBJ_ERR_BAD_TYPE        the type is outside mobjtype_t
 *   P_MOBJ_ERR_NO_MEMORY       the zone refused the allocation
 *   P_MOBJ_ERR_NO_SUBSECTOR    the map could not place the point, or the
 *                              subsector names a sector outside SECTORS
 *   P_MOBJ_ERR_ALREADY_REMOVED the mobj is already marked for removal
 *   P_MOBJ_ERR_NOT_LINKED      the mobj is not in this world's thinker list
 *   P_MOBJ_ERR_BAD_STATE       the state index is outside statenum_t
 *   P_MOBJ_ERR_STATE_CHAIN     the zero-tic state chain did not terminate
 *   P_MOBJ_ERR_NULL_THING      no THINGS record was passed
 *   P_MOBJ_ERR_BAD_PLAYER_START the record's type is not a player slot
 *   P_MOBJ_ERR_NOT_IN_GAME     that player slot is not playing
 *
 * A refused call that could allocate (a bad type, an unplaceable point) frees
 * what it took, so the zone's accounting is exactly what it was.
 */
typedef enum p_mobj_error {
    P_MOBJ_OK = 0,
    P_MOBJ_ERR_NULL_WORLD = 1,
    P_MOBJ_ERR_NO_ZONE = 2,
    P_MOBJ_ERR_NO_THINKERS = 3,
    P_MOBJ_ERR_NO_MAP = 4,
    P_MOBJ_ERR_NULL_MOBJ = 5,
    P_MOBJ_ERR_BAD_TYPE = 6,
    P_MOBJ_ERR_NO_MEMORY = 7,
    P_MOBJ_ERR_NO_SUBSECTOR = 8,
    P_MOBJ_ERR_ALREADY_REMOVED = 9,
    P_MOBJ_ERR_NOT_LINKED = 10,
    P_MOBJ_ERR_BAD_STATE = 11,
    P_MOBJ_ERR_STATE_CHAIN = 12,
    P_MOBJ_ERR_NULL_THING = 13,
    P_MOBJ_ERR_BAD_PLAYER_START = 14,
    P_MOBJ_ERR_NOT_IN_GAME = 15
} p_mobj_error_t;

/*
 * What P_SpawnMapThing() did with one THINGS record. Upstream returns early from
 * each filter with no value at all; naming the branch is what lets p_setup.c
 * keep its counters without re-deriving the decision.
 *
 *   P_THING_SPAWNED            a mobj was created (`mobj` is set)
 *   P_THING_PLAYER_START       a player start record; `isPlayerStart` is set and
 *                              `mobj` is set when that slot is playing
 *   P_THING_DEATHMATCH_START   a type-11 record; recorded, never a mobj
 *   P_THING_SKIPPED_SKILL      the record's options do not include this skill
 *   P_THING_SKIPPED_SINGLE     options bit 16 in a single-player game
 *   P_THING_SKIPPED_NOMONSTERS -nomonsters dropped a monster
 *   P_THING_SKIPPED_DEATHMATCH deathmatch dropped an MF_NOTDMATCH thing
 *   P_THING_UNKNOWN_TYPE       no mobjinfo record has this doomednum
 *   P_THING_SPAWN_FAILED       the zone or the map refused the spawn
 *   P_THING_INVALID            the record is null or its type is not a thing
 */
typedef enum p_thing_result {
    P_THING_SPAWNED = 0,
    P_THING_PLAYER_START = 1,
    P_THING_DEATHMATCH_START = 2,
    P_THING_SKIPPED_SKILL = 3,
    P_THING_SKIPPED_SINGLE = 4,
    P_THING_SKIPPED_NOMONSTERS = 5,
    P_THING_SKIPPED_DEATHMATCH = 6,
    P_THING_UNKNOWN_TYPE = 7,
    P_THING_SPAWN_FAILED = 8,
    P_THING_INVALID = 9
} p_thing_result_t;

/*
 * What one THINGS record produced. `isPlayerStart` and `isDeathmatchStart` are
 * separate flags because those records are *recorded* by the caller whether or
 * not a mobj came out of them: upstream keeps playerstarts[] and
 * deathmatchstarts[] for respawning, and Phase 4's totals depend on the
 * recording, not only on the spawning.
 */
typedef struct p_thing_outcome {
    p_thing_result_t result;
    r_mobj_t* mobj;
    uint8_t isPlayerStart;
    uint8_t isDeathmatchStart;
} p_thing_outcome_t;

/* --- Public API ---------------------------------------------------------- */

/**
 * @brief P_SpawnMobj(): allocates, positions and links one thing.
 *
 * Order of operations, all of it upstream's (p_mobj.c P_SpawnMobj):
 *
 *   1. allocate sizeof(r_mobj_t) from the context's zone with PU_LEVEL, and
 *      zero it;
 *   2. copy type, x, y, radius, height, flags and health from mobjinfo[type];
 *   3. reactiontime, except on nightmare;
 *   4. lastlook = P_Random() % MAXPLAYERS (skipped when the context has no
 *      generator);
 *   5. set sprite/frame/tics/state from states[spawnstate] *without* running the
 *      action;
 *   6. find the subsector through the BSP and the sector through that
 *      subsector, and copy floorz/ceilingz from it;
 *   7. resolve z: ONFLOORZ -> floorz, ONCEILINGZ -> ceilingz - height, else z;
 *   8. make it a MOBJ thinker and append it to the list.
 *
 * Validation order:
 *
 *   1. world == NULL                            P_MOBJ_ERR_NULL_WORLD
 *   2. zone missing or not open                 P_MOBJ_ERR_NO_ZONE
 *   3. thinker list missing                     P_MOBJ_ERR_NO_THINKERS
 *   4. map missing or not open                  P_MOBJ_ERR_NO_MAP
 *   5. type outside mobjtype_t                  P_MOBJ_ERR_BAD_TYPE
 *   6. the zone refuses the block               P_MOBJ_ERR_NO_MEMORY
 *   7. the point is not in any subsector        P_MOBJ_ERR_NO_SUBSECTOR
 *
 * Cases 6 and 7 free nothing because nothing was linked, and case 6 leaves the
 * arena's accounting untouched (z_alloc() only increments `failures`).
 *
 * @param world The level's context: arena, thinker list, map, generator.
 * @param x,y   World position, 16.16.
 * @param z     ONFLOORZ, ONCEILINGZ, or an explicit 16.16 height.
 * @param type  The mobj type; a doomednum is *not* accepted here.
 * @param error Optional out: P_MOBJ_OK on success, or the failure code.
 * @return The mobj, or NULL on any failure.
 */
r_mobj_t* p_spawn_mobj(p_mobj_context_t* world, fixed_t x, fixed_t y, fixed_t z,
                       mobjtype_t type, p_mobj_error_t* error);

/**
 * @brief P_SpawnPlayer(): the avatar for one player start.
 *
 * Upstream's rule, and only the parts of it this layer can carry:
 *
 *   - the slot must be playing; otherwise the call is refused
 *     (P_MOBJ_ERR_NOT_IN_GAME) and no mobj exists;
 *   - the mobj spawns at the start's x/y shifted to 16.16, at ONFLOORZ, as
 *     MT_PLAYER;
 *   - `angle = ANG45 * (degrees / 45)`, so only eight compass directions exist;
 *   - a slot above 0 adds `(type-1) << MF_TRANSSHIFT` to the flags, which is
 *     how DOOM colours the other players' sprites;
 *   - health is the mobj's spawnhealth, which is what upstream copies from the
 *     reborn player (100 at level start).
 *
 * Not carried: P_SetupPsprites (the weapon), the card grant in deathmatch, and
 * ST_Start/HU_Start. Those belong to the HUD and item layers.
 *
 * @param world The level's context.
 * @param start The player start record (type 1..4).
 * @param error Optional out.
 * @return The mobj, or NULL.
 */
r_mobj_t* p_spawn_player(p_mobj_context_t* world, const p_mapthing_t* start,
                         p_mobj_error_t* error);

/**
 * @brief P_SpawnMapThing(): the filters, then a spawn.
 *
 * Upstream's decision order, reproduced exactly (p_mobj.c P_SpawnMapThing):
 *
 *   1. type 11                     a deathmatch start: recorded, returned
 *   2. type 1..4                   a player start: recorded; P_SpawnPlayer()
 *                                  runs unless deathmatch
 *   3. type <= 0                   not a thing -> P_THING_INVALID
 *   4. options & 16 in single play drop it
 *   5. the skill bit: 1 for baby, 4 for nightmare, else 1 << (skill-1)
 *   6. the doomednum scan of mobjinfo[]; no match is an error upstream (I_Error)
 *                                  and P_THING_UNKNOWN_TYPE here
 *   7. deathmatch and MF_NOTDMATCH drop it
 *   8. -nomonsters drops MT_SKULL and every MF_COUNTKILL
 *   9. MF_SPAWNCEILING selects ONCEILINGZ, everything else ONFLOORZ
 *  10. spawn, copy the THINGS record into `spawnpoint`, randomise `tics` when
 *      the state's tics are positive (`1 + P_Random() % tics`), set the angle,
 *      and OR in MF_AMBUSH for options bit 8
 *
 * Counters are *not* kept here: the outcome tells p_setup.c which branch ran,
 * and the mobj tells it which totals to raise. That keeps this file free of
 * level bookkeeping and keeps the counters in one place (p_setup.h).
 *
 * @param world The level's context.
 * @param thing One THINGS record; map units and degrees.
 * @return The outcome; `mobj` is set only when something was created.
 */
p_thing_outcome_t p_spawn_map_thing(p_mobj_context_t* world,
                                    const p_mapthing_t* thing);

/**
 * @brief P_RemoveMobj(): marks the mobj for removal, frees nothing yet.
 *
 * Upstream does three things here; this port does the third and leaves the
 * first two to the layers that own them:
 *
 *   - the item respawn queue (`itemrespawnque`), which needs level time and is
 *     a D4.x special;
 *   - P_UnsetThingPosition (the sector/blockmap links), which do not exist yet;
 *   - P_RemoveThinker(), which is `p_thinker_remove()` on the mobj's node.
 *
 * The mobj stays valid until a p_run_thinkers() walk reaches it; that walk
 * unlinks it and hands it to the release callback. A second call is refused
 * with P_MOBJ_ERR_ALREADY_REMOVED and changes nothing.
 *
 * @return P_MOBJ_OK, P_MOBJ_ERR_ALREADY_REMOVED, P_MOBJ_ERR_NULL_MOBJ,
 *         P_MOBJ_ERR_NOT_LINKED, or P_MOBJ_ERR_NULL_WORLD.
 */
p_mobj_error_t p_remove_mobj(p_mobj_context_t* world, r_mobj_t* mobj);

/**
 * @brief P_SetMobjState(): sets a state, runs its action, follows zero-tic
 *        states, and removes the mobj when the chain reaches S_NULL.
 *
 * The loop is upstream's (p_mobj.c):
 *
 *   do {
 *       if (state == S_NULL) { state = S_NULL; P_RemoveMobj(mobj); return false; }
 *       set state, tics, sprite, frame;
 *       if (action != ACT_NULL) run it;
 *       state = nextstate;
 *   } while (tics == 0);
 *
 * So a state with `tics == 0` is never visible: the chain runs to the first
 * state with a positive tic count (or -1), and every action on the way runs.
 *
 * The one addition is a bound: a table whose zero-tic states form a cycle would
 * hang the loop forever, so after 256 iterations the call reports
 * P_MOBJ_ERR_STATE_CHAIN and leaves the mobj at its current state. No real
 * chain is longer than a few states.
 *
 * @param world  The level's context (for the action counters).
 * @param mobj   The thing.
 * @param state  The state to enter.
 * @param error  Optional out.
 * @return 1 while the mobj is still in the world, 0 when the chain reached
 *         S_NULL and marked it for removal. A refused call returns 1 with the
 *         error set (nothing changed), except for a NULL mobj, which returns 0.
 */
int p_set_mobj_state(p_mobj_context_t* world, r_mobj_t* mobj, statenum_t state,
                     p_mobj_error_t* error);

/**
 * @brief The thinker function every mobj is created with.
 *
 * This is P_MobjThinker() minus movement: it decrements `tics`, and when it
 * reaches zero it enters the state's `nextstate` through p_set_mobj_state(),
 * which may remove the mobj (and does when the chain ends at S_NULL). A state
 * with `tics == -1` is permanent and is not advanced.
 *
 * D4.2 replaces the two commented-out calls with P_XYMovement/P_ZMovement, in
 * that order and with the same "was I removed?" test between them.
 */
void p_mobj_thinker(p_thinker_t* thinker, void* context);

/**
 * @brief The mobj a thinker node is, or NULL when the node is another kind.
 *
 * The check is `kind == P_THINKER_MOBJ`, not a pointer cast, so a sector
 * thinker added in D4.1b cannot be walked as a mobj by accident.
 */
r_mobj_t* p_mobj_of(p_thinker_t* thinker);

/* The same, for a read-only walk. */
const r_mobj_t* p_mobj_of_const(const p_thinker_t* thinker);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_P_MOBJ_H */
