/*
 * Behaviour tests for the world core (D4.1a): the mobj spawn path, the thinker
 * list, the state machine, the action dispatch and the random sequence.
 *
 * Every map here is assembled byte for byte in the 1993 WAD record layout, so
 * this suite needs no WAD, no container, no display and no engine. The zone is
 * a local array, one per test, so each case starts from an arena whose bytes
 * the test wrote.
 *
 * The tests go through the public C API only. They read `p_level_stats_t` and
 * the level's counters, never an internal field that the API does not expose,
 * and expected numbers are derived from the generated tables (`mobjinfo[]`,
 * `states[]`) rather than written as literals: a test that hard-codes "radius
 * is 1048576" cannot notice the table moving under it.
 *
 * WHAT EACH CASE IS FOR
 *
 *   spawn      ONFLOORZ, ONCEILINGZ, an explicit z, MF_SPAWNCEILING, and the
 *              refusal of a bad type and of an arena too small for the mobj.
 *   thinker    insertion order, deferred removal, a thinker that removes
 *              itself and one that removes a thinker that has not run yet.
 *   states     a state sets sprite/frame/tics, a zero-tic chain is followed,
 *              and S_NULL removes the mobj.
 *   rng        upstream's first sixteen numbers, and two independent indices.
 *   filters    the skill bit, the single-player bit, MF_COUNTKILL/COUNTITEM,
 *              player starts and deathmatch starts.
 *   actions    A_Look is a counted stub; anything else is counted as not
 *              implemented; ACT_NULL is neither.
 */
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <unity.h>

#include "p_action.h"
#include "p_ceilng.h"
#include "p_doors.h"
#include "p_floor.h"
#include "p_lights.h"
#include "p_map.h"
#include "p_plats.h"
#include "p_setup.h"
#include "p_spec.h"
#include "p_user.h"
#include "r_tables.h"

#include "../../test_config.h"

namespace {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using i16 = std::int16_t;
using u32 = std::uint32_t;
using i32 = std::int32_t;

constexpr fixed_t kFrac = FRACUNIT;
constexpr i16 kFloorHeight = 0;
constexpr i16 kCeilingHeight = 128;
constexpr int16_t kNoSide = static_cast<int16_t>(DOOM_NO_SIDE_INDEX);

/// Enough for a handful of mobjs plus the block headers around them.
constexpr std::size_t kArenaBytes = 4096u;

// --- The 1993 map layout ----------------------------------------------------

void put16(std::vector<u8>& out, i16 value) {
    const u16 bits = static_cast<u16>(value);
    out.push_back(static_cast<u8>(bits & 0xFFu));
    out.push_back(static_cast<u8>((bits >> 8) & 0xFFu));
}

void putName(std::vector<u8>& out, const char* name) {
    const std::size_t length = std::strlen(name);

    for (std::size_t i = 0; i < 8u; ++i) {
        out.push_back(i < length ? static_cast<u8>(name[i]) : 0u);
    }
}

/*
 * One sector, one subsector and one seg: the smallest map the spawn path needs.
 * doom_map_open() accepts it because the five required lumps are non-empty and
 * NODES may be empty (a map with no nodes is subsector 0, R_PointInSubsector's
 * own special case). Every point is therefore in subsector 0, sector 0.
 */
struct MapBuilder {
    std::vector<u8> things;
    std::vector<u8> linedefs;
    std::vector<u8> sidedefs;
    std::vector<u8> vertexes;
    std::vector<u8> segs;
    std::vector<u8> ssectors;
    std::vector<u8> sectors;

    MapBuilder() {
        put16(vertexes, 0);
        put16(vertexes, 0);
        put16(vertexes, 64);
        put16(vertexes, 0);

        put16(linedefs, 0);          // v1
        put16(linedefs, 1);          // v2
        put16(linedefs, 1);          // flags: blocking
        put16(linedefs, 0);          // special
        put16(linedefs, 0);          // tag
        put16(linedefs, 0);          // side 0
        put16(linedefs, kNoSide);    // no back side

        put16(sidedefs, 0);          // texture offset
        put16(sidedefs, 0);          // row offset
        putName(sidedefs, "-");
        putName(sidedefs, "-");
        putName(sidedefs, "STARTAN3");
        put16(sidedefs, 0);          // sector 0

        put16(segs, 0);              // v1
        put16(segs, 1);              // v2
        put16(segs, 0);              // angle
        put16(segs, 0);              // linedef 0
        put16(segs, 0);              // side 0
        put16(segs, 0);              // offset

        put16(ssectors, 1);          // one seg
        put16(ssectors, 0);          // starting at seg 0

        put16(sectors, kFloorHeight);
        put16(sectors, kCeilingHeight);
        putName(sectors, "FLOOR4_8");
        putName(sectors, "CEIL3_5");
        put16(sectors, 160);         // light
        put16(sectors, 0);           // special
        put16(sectors, 0);           // tag
    }

    /// One THINGS record; the caller decides the options bits.
    void addThing(i16 x, i16 y, i16 angle, i16 type, i16 options) {
        put16(things, x);
        put16(things, y);
        put16(things, angle);
        put16(things, type);
        put16(things, options);
    }

    doom_map_error_t open(doom_map_t* map) const {
        doom_map_source_t source{};

        source.data[DOOM_MAP_THINGS] = things.empty() ? nullptr : things.data();
        source.size[DOOM_MAP_THINGS] = things.size();
        source.data[DOOM_MAP_LINEDEFS] = linedefs.data();
        source.size[DOOM_MAP_LINEDEFS] = linedefs.size();
        source.data[DOOM_MAP_SIDEDEFS] = sidedefs.data();
        source.size[DOOM_MAP_SIDEDEFS] = sidedefs.size();
        source.data[DOOM_MAP_VERTEXES] = vertexes.data();
        source.size[DOOM_MAP_VERTEXES] = vertexes.size();
        source.data[DOOM_MAP_SEGS] = segs.data();
        source.size[DOOM_MAP_SEGS] = segs.size();
        source.data[DOOM_MAP_SSECTORS] = ssectors.data();
        source.size[DOOM_MAP_SSECTORS] = ssectors.size();
        source.data[DOOM_MAP_SECTORS] = sectors.data();
        source.size[DOOM_MAP_SECTORS] = sectors.size();

        return doom_map_open(map, &source);
    }
};

/* --- A whole world on the stack -------------------------------------------- */

struct World {
    MapBuilder builder;
    doom_map_t map{};
    alignas(8) u8 arena[kArenaBytes]{};
    zone_state_t zone{};
    p_thinker_list_t thinkers{};
    m_random_t random{};
    p_action_stats_t actions{};
    p_mobj_context_t context{};

    /// Builds the map, the zone and the context; aborts the test on failure.
    void open() {
        TEST_ASSERT_EQUAL_INT(DOOM_MAP_OK, builder.open(&map));
        TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, arena, sizeof arena));
        TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_thinker_list_init(&thinkers));
        m_random_init(&random);
        p_action_init(&actions);

        context.zone = &zone;
        context.thinkers = &thinkers;
        context.map = &map;
        context.random = &random;
        context.actions = &actions;
        context.rules = p_spawn_rules_default();
    }

    /// The zone's accounting, for the cases that must change nothing.
    zone_stats_t stats() const { return z_stats(&zone); }

    /// The number of live mobjs, by walking the thinker list.
    u32 mobjCount() const {
        u32 count = 0;

        for (const p_thinker_t* thinker = p_thinker_first(&thinkers); thinker != nullptr;
             thinker = p_thinker_next(thinker)) {
            if (thinker->kind == P_THINKER_MOBJ) {
                ++count;
            }
        }
        return count;
    }
};

/// The first mobj type whose flags carry @p flag, so a test never hard-codes a
/// table index that the generated table could move.
mobjtype_t typeWithFlag(u32 flag) {
    for (i32 type = 0; type < NUM_MOBJ_TYPES; ++type) {
        if ((mobjinfo[type].flags & flag) != 0u) {
            return static_cast<mobjtype_t>(type);
        }
    }
    return MT_PLAYER;
}

/// The first state that chains through at least one zero-tic state.
statenum_t zeroTicState() {
    for (i32 state = 1; state < NUM_STATES; ++state) {
        if (states[state].tics == 0 && states[state].nextstate != S_NULL) {
            return static_cast<statenum_t>(state);
        }
    }
    return S_NULL;
}

/// The state p_set_mobj_state() must stop at when it starts at @p start: the
/// first state in the chain whose tics are not zero. Recomputed from the table
/// here, independently of the implementation.
statenum_t firstTimedState(statenum_t start) {
    statenum_t state = start;

    while (state != S_NULL && states[state].tics == 0) {
        state = static_cast<statenum_t>(states[state].nextstate);
    }
    return state;
}

// --- Spawn position ---------------------------------------------------------

void test_spawn_onfloorz_uses_the_sector_floor(void) {
    World world;
    p_mobj_error_t error = P_MOBJ_ERR_NULL_MOBJ;

    world.open();

    r_mobj_t* mobj = p_spawn_mobj(&world.context, 10 * kFrac, 20 * kFrac, ONFLOORZ,
                                  MT_PLAYER, &error);
    TEST_ASSERT_NOT_NULL(mobj);
    TEST_ASSERT_EQUAL_INT(P_MOBJ_OK, error);
    TEST_ASSERT_EQUAL_INT32(10 * kFrac, mobj->x);
    TEST_ASSERT_EQUAL_INT32(20 * kFrac, mobj->y);
    TEST_ASSERT_EQUAL_INT32(kFloorHeight * kFrac, mobj->z);
    TEST_ASSERT_EQUAL_INT32(kFloorHeight * kFrac, mobj->floorz);
    TEST_ASSERT_EQUAL_INT32(kCeilingHeight * kFrac, mobj->ceilingz);
    TEST_ASSERT_EQUAL_UINT16(0u, mobj->subsector);
    TEST_ASSERT_EQUAL_UINT16(0u, mobj->sector);
    TEST_ASSERT_EQUAL_INT32(mobjinfo[MT_PLAYER].radius, mobj->radius);
    TEST_ASSERT_EQUAL_INT32(mobjinfo[MT_PLAYER].height, mobj->height);
    TEST_ASSERT_EQUAL_INT32(mobjinfo[MT_PLAYER].spawnhealth, mobj->health);
    TEST_ASSERT_EQUAL_UINT32(mobjinfo[MT_PLAYER].flags, mobj->flags);
    TEST_ASSERT_EQUAL_INT(S_PLAY, mobj->state);
    TEST_ASSERT_EQUAL_INT(states[S_PLAY].sprite, mobj->sprite);
    TEST_ASSERT_EQUAL_INT(states[S_PLAY].tics, mobj->tics);
    TEST_ASSERT_EQUAL_UINT32(1u, world.thinkers.count);
    TEST_ASSERT_TRUE((mobj->flags & MF_SOLID) != 0u);
}

void test_spawn_onceilingz_subtracts_the_height(void) {
    World world;
    p_mobj_error_t error = P_MOBJ_ERR_NULL_MOBJ;

    world.open();

    r_mobj_t* mobj = p_spawn_mobj(&world.context, 0, 0, ONCEILINGZ, MT_PLAYER, &error);
    TEST_ASSERT_NOT_NULL(mobj);
    TEST_ASSERT_EQUAL_INT(P_MOBJ_OK, error);
    TEST_ASSERT_EQUAL_INT32((kCeilingHeight * kFrac) - mobjinfo[MT_PLAYER].height,
                            mobj->z);
}

void test_spawn_explicit_z_is_not_clamped(void) {
    World world;
    p_mobj_error_t error = P_MOBJ_ERR_NULL_MOBJ;
    const fixed_t z = 40 * kFrac;

    world.open();

    // Vanilla's P_SpawnMobj leaves an explicit z alone, even outside the
    // sector: P_ZMovement() is what brings the mobj back on the first tic.
    r_mobj_t* mobj = p_spawn_mobj(&world.context, 0, 0, z, MT_PLAYER, &error);
    TEST_ASSERT_NOT_NULL(mobj);
    TEST_ASSERT_EQUAL_INT32(z, mobj->z);
    TEST_ASSERT_EQUAL_INT32(kFloorHeight * kFrac, mobj->floorz);
    TEST_ASSERT_EQUAL_INT32(kCeilingHeight * kFrac, mobj->ceilingz);
}

void test_spawn_map_thing_hanging_type_uses_onceilingz(void) {
    World world;
    p_mapthing_t thing{};

    world.open();

    const mobjtype_t hanging = typeWithFlag(MF_SPAWNCEILING);
    TEST_ASSERT_TRUE((mobjinfo[hanging].flags & MF_SPAWNCEILING) != 0u);
    TEST_ASSERT_TRUE(mobjinfo[hanging].doomednum >= 0);

    thing.x = 4;
    thing.y = 8;
    thing.angle = 90;
    thing.type = static_cast<i16>(mobjinfo[hanging].doomednum);
    thing.options = 7;

    const p_thing_outcome_t outcome = p_spawn_map_thing(&world.context, &thing);
    TEST_ASSERT_EQUAL_INT(P_THING_SPAWNED, outcome.result);
    TEST_ASSERT_NOT_NULL(outcome.mobj);
    TEST_ASSERT_EQUAL_INT32((kCeilingHeight * kFrac) - mobjinfo[hanging].height,
                            outcome.mobj->z);
    TEST_ASSERT_TRUE((outcome.mobj->flags & MF_SPAWNCEILING) != 0u);
    TEST_ASSERT_EQUAL_INT(hanging, outcome.mobj->type);
    TEST_ASSERT_EQUAL_INT32(4 * kFrac, outcome.mobj->x);
    TEST_ASSERT_EQUAL_UINT32(static_cast<u32>(ANG45 * (90 / 45)), outcome.mobj->angle);
}

void test_spawn_rejects_a_bad_type_and_touches_nothing(void) {
    World world;
    p_mobj_error_t error = P_MOBJ_ERR_NULL_MOBJ;

    world.open();

    const zone_stats_t empty = world.stats();
    r_mobj_t* mobj = p_spawn_mobj(&world.context, 0, 0, ONFLOORZ,
                                  static_cast<mobjtype_t>(NUM_MOBJ_TYPES), &error);
    TEST_ASSERT_NULL(mobj);
    TEST_ASSERT_EQUAL_INT(P_MOBJ_ERR_BAD_TYPE, error);
    TEST_ASSERT_EQUAL_UINT32(0u, world.thinkers.count);
    TEST_ASSERT_EQUAL_UINT32(empty.inUse, world.stats().inUse);
    TEST_ASSERT_EQUAL_UINT32(empty.freeBytes, world.stats().freeBytes);
    TEST_ASSERT_EQUAL_UINT32(empty.failures, world.stats().failures);
}

void test_zone_too_small_reports_failure_without_touching_the_arena(void) {
    // One block header and one minimum payload: enough for z_init(), never
    // enough for a mobj.
    alignas(8) u8 tiny[ZONE_MIN_ARENA]{};
    zone_state_t zone;
    p_thinker_list_t thinkers;
    MapBuilder builder;
    doom_map_t map{};
    m_random_t random;
    p_action_stats_t actions;
    p_mobj_context_t context{};
    p_mobj_error_t error = P_MOBJ_ERR_NULL_MOBJ;

    TEST_ASSERT_EQUAL_INT(DOOM_MAP_OK, builder.open(&map));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, tiny, sizeof tiny));
    TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_thinker_list_init(&thinkers));
    m_random_init(&random);
    p_action_init(&actions);
    context.zone = &zone;
    context.thinkers = &thinkers;
    context.map = &map;
    context.random = &random;
    context.actions = &actions;
    context.rules = p_spawn_rules_default();

    const zone_stats_t before = z_stats(&zone);
    r_mobj_t* mobj = p_spawn_mobj(&context, 0, 0, ONFLOORZ, MT_PLAYER, &error);
    const zone_stats_t after = z_stats(&zone);

    TEST_ASSERT_NULL(mobj);
    TEST_ASSERT_EQUAL_INT(P_MOBJ_ERR_NO_MEMORY, error);
    TEST_ASSERT_EQUAL_UINT32(0u, thinkers.count);
    TEST_ASSERT_EQUAL_UINT32(before.inUse, after.inUse);
    TEST_ASSERT_EQUAL_UINT32(before.freeBytes, after.freeBytes);
    TEST_ASSERT_EQUAL_UINT32(before.overhead, after.overhead);
    TEST_ASSERT_EQUAL_UINT32(before.largestFree, after.largestFree);
    TEST_ASSERT_EQUAL_UINT32(before.failures + 1u, after.failures);
    TEST_ASSERT_TRUE(sizeof(r_mobj_t) > (ZONE_MIN_ARENA - ZONE_HEADER_SIZE));
}

// --- The thinker list -------------------------------------------------------

void test_thinkers_run_in_insertion_order(void) {
    World world;
    static int order[4];
    static int position;

    struct Recorder {
        p_thinker_t thinker;
        int value;
    };
    static Recorder recorders[4];

    struct Context {
        int* order;
        int* position;
    };
    static Context context;

    world.open();

    struct Runner {
        static void run(p_thinker_t* thinker, void* raw) {
            Recorder* self = reinterpret_cast<Recorder*>(thinker);
            Context* ctx = static_cast<Context*>(raw);

            ctx->order[(*ctx->position)++] = self->value;
        }

        // Nothing is removed here, so the release is never called; it exists
        // because a walk without one is refused (it would leak every removal).
        static void release(void* raw, p_thinker_t* thinker) {
            (void)raw;
            (void)thinker;
        }
    };

    context.order = order;
    context.position = &position;
    position = 0;

    for (int i = 0; i < 4; ++i) {
        recorders[i].value = i;
        TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_thinker_init(&recorders[i].thinker,
                                                        &Runner::run, P_THINKER_MOBJ));
        TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_thinker_add(&world.thinkers,
                                                       &recorders[i].thinker));
    }
    TEST_ASSERT_EQUAL_UINT32(4u, world.thinkers.count);

    p_thinker_run_t stats{};
    TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_run_thinkers(&world.thinkers, &Runner::release,
                                                    &context, &stats));
    TEST_ASSERT_EQUAL_UINT32(4u, stats.executed);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.removed);
    TEST_ASSERT_EQUAL_INT(0, order[0]);
    TEST_ASSERT_EQUAL_INT(1, order[1]);
    TEST_ASSERT_EQUAL_INT(2, order[2]);
    TEST_ASSERT_EQUAL_INT(3, order[3]);
}

void test_thinker_removing_itself_and_a_later_one(void) {
    static int self_calls;
    static int second_calls;
    static int third_calls;
    static u32 released;

    struct Node {
        p_thinker_t thinker;
        int* calls;
    };

    struct Context {
        p_thinker_list_t* list;
        p_thinker_t* victim;
    };

    // A -- removes itself; B -- removes C, which has not run yet; C -- must
    // never run.
    struct Runners {
        static void selfRemover(p_thinker_t* thinker, void* raw) {
            Node* self = reinterpret_cast<Node*>(thinker);
            Context* ctx = static_cast<Context*>(raw);

            *self->calls += 1;
            TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_thinker_remove(ctx->list, thinker));
        }

        static void remover(p_thinker_t* thinker, void* raw) {
            Node* self = reinterpret_cast<Node*>(thinker);
            Context* ctx = static_cast<Context*>(raw);

            *self->calls += 1;
            TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_thinker_remove(ctx->list, ctx->victim));
        }

        static void plain(p_thinker_t* thinker, void* raw) {
            Node* self = reinterpret_cast<Node*>(thinker);

            (void)raw;
            *self->calls += 1;
        }

        static void release(void* raw, p_thinker_t* thinker) {
            (void)raw;
            (void)thinker;
            ++released;
        }
    };

    p_thinker_list_t list;
    Node a{{}, &self_calls};
    Node b{{}, &second_calls};
    Node c{{}, &third_calls};
    Context context{&list, &c.thinker};
    p_thinker_run_t stats{};

    self_calls = second_calls = third_calls = 0;
    released = 0;

    TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_thinker_list_init(&list));
    TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_thinker_init(&a.thinker, &Runners::selfRemover,
                                                    P_THINKER_MOBJ));
    TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_thinker_init(&b.thinker, &Runners::remover,
                                                    P_THINKER_MOBJ));
    TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_thinker_init(&c.thinker, &Runners::plain,
                                                    P_THINKER_MOBJ));
    TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_thinker_add(&list, &a.thinker));
    TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_thinker_add(&list, &b.thinker));
    TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_thinker_add(&list, &c.thinker));

    TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_run_thinkers(&list, &Runners::release,
                                                    &context, &stats));

    TEST_ASSERT_EQUAL_INT(1, self_calls);
    TEST_ASSERT_EQUAL_INT(1, second_calls);
    TEST_ASSERT_EQUAL_INT(0, third_calls);
    TEST_ASSERT_EQUAL_UINT32(2u, stats.executed);
    TEST_ASSERT_EQUAL_UINT32(2u, stats.removed);
    TEST_ASSERT_EQUAL_UINT32(2u, released);
    // A removed itself and C was removed before its turn; B is untouched and
    // is still the only entry, with no dangling next pointer.
    TEST_ASSERT_EQUAL_UINT32(1u, list.count);
    TEST_ASSERT_EQUAL_PTR(&b.thinker, p_thinker_first(&list));
    TEST_ASSERT_EQUAL_UINT8(0u, b.thinker.removed);
    TEST_ASSERT_NULL(p_thinker_next(&b.thinker));
}

void test_thinker_misuse_changes_nothing(void) {
    p_thinker_list_t list;
    p_thinker_t thinker{};
    p_thinker_t alien{};
    p_thinker_run_t stats{};

    TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_thinker_list_init(&list));
    TEST_ASSERT_EQUAL_INT(P_TICK_ERR_NULL_LIST, p_thinker_add(nullptr, &thinker));
    TEST_ASSERT_EQUAL_INT(P_TICK_ERR_NULL_THINKER, p_thinker_add(&list, nullptr));
    TEST_ASSERT_EQUAL_INT(P_TICK_ERR_NOT_LINKED, p_thinker_remove(&list, &thinker));

    TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_thinker_init(&thinker, nullptr, P_THINKER_MOBJ));
    TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_thinker_add(&list, &thinker));
    TEST_ASSERT_EQUAL_INT(P_TICK_ERR_ALREADY_LINKED, p_thinker_add(&list, &thinker));
    TEST_ASSERT_EQUAL_UINT32(1u, list.count);

    // A thinker from another list is not this list's to remove.
    p_thinker_list_t other;
    TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_thinker_list_init(&other));
    TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_thinker_init(&alien, nullptr, P_THINKER_MOBJ));
    TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_thinker_add(&other, &alien));
    TEST_ASSERT_EQUAL_INT(P_TICK_ERR_NOT_LINKED, p_thinker_remove(&list, &alien));
    TEST_ASSERT_EQUAL_UINT32(1u, other.count);

    // A run with no release callback is refused before it walks anything.
    TEST_ASSERT_EQUAL_INT(P_TICK_ERR_NULL_RELEASE, p_run_thinkers(&list, nullptr,
                                                                  nullptr, &stats));
    TEST_ASSERT_EQUAL_UINT32(1u, list.count);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.executed);
}

// --- The state machine ------------------------------------------------------

void test_set_state_advances_state_and_resets_tics(void) {
    World world;
    p_mobj_error_t error = P_MOBJ_ERR_NULL_MOBJ;

    world.open();
    r_mobj_t* mobj = p_spawn_mobj(&world.context, 0, 0, ONFLOORZ, MT_PLAYER, &error);
    TEST_ASSERT_NOT_NULL(mobj);

    TEST_ASSERT_EQUAL_INT(1, p_set_mobj_state(&world.context, mobj, S_PLAY_RUN1, &error));
    TEST_ASSERT_EQUAL_INT(P_MOBJ_OK, error);
    TEST_ASSERT_EQUAL_INT(S_PLAY_RUN1, mobj->state);
    TEST_ASSERT_EQUAL_INT(states[S_PLAY_RUN1].tics, mobj->tics);
    TEST_ASSERT_EQUAL_INT(states[S_PLAY_RUN1].sprite, mobj->sprite);
    TEST_ASSERT_EQUAL_INT32(states[S_PLAY_RUN1].frame, mobj->frame);
    TEST_ASSERT_TRUE(mobj->tics > 0);
}

void test_set_state_follows_a_zero_tic_chain(void) {
    World world;
    p_mobj_error_t error = P_MOBJ_ERR_NULL_MOBJ;

    world.open();
    r_mobj_t* mobj = p_spawn_mobj(&world.context, 0, 0, ONFLOORZ, MT_PLAYER, &error);
    TEST_ASSERT_NOT_NULL(mobj);

    const statenum_t start = zeroTicState();
    TEST_ASSERT_TRUE(start != S_NULL);
    TEST_ASSERT_EQUAL_INT(states[start].tics, 0);

    const statenum_t expected = firstTimedState(start);
    TEST_ASSERT_TRUE(expected != S_NULL);

    TEST_ASSERT_EQUAL_INT(1, p_set_mobj_state(&world.context, mobj, start, &error));
    TEST_ASSERT_EQUAL_INT(expected, mobj->state);
    TEST_ASSERT_EQUAL_INT(states[expected].tics, mobj->tics);
    TEST_ASSERT_TRUE(mobj->tics > 0);
}

void test_set_state_to_null_removes_the_mobj(void) {
    World world;
    p_mobj_error_t error = P_MOBJ_ERR_NULL_MOBJ;

    world.open();
    r_mobj_t* mobj = p_spawn_mobj(&world.context, 0, 0, ONFLOORZ, MT_PLAYER, &error);
    TEST_ASSERT_NOT_NULL(mobj);

    TEST_ASSERT_EQUAL_INT(0, p_set_mobj_state(&world.context, mobj, S_NULL, &error));
    TEST_ASSERT_EQUAL_INT(P_MOBJ_OK, error);
    TEST_ASSERT_EQUAL_INT(S_NULL, mobj->state);
    TEST_ASSERT_EQUAL_UINT8(1u, mobj->thinker.removed);
}

void test_remove_twice_is_a_noop_with_an_error(void) {
    World world;
    p_mobj_error_t error = P_MOBJ_ERR_NULL_MOBJ;
    p_thinker_run_t stats{};

    world.open();
    r_mobj_t* mobj = p_spawn_mobj(&world.context, 0, 0, ONFLOORZ, MT_PLAYER, &error);
    TEST_ASSERT_NOT_NULL(mobj);

    TEST_ASSERT_EQUAL_INT(P_MOBJ_OK, p_remove_mobj(&world.context, mobj));
    TEST_ASSERT_EQUAL_UINT8(1u, mobj->thinker.removed);
    TEST_ASSERT_EQUAL_UINT32(1u, world.thinkers.count);

    const zone_stats_t held = world.stats();
    TEST_ASSERT_EQUAL_INT(P_MOBJ_ERR_ALREADY_REMOVED,
                          p_remove_mobj(&world.context, mobj));
    TEST_ASSERT_EQUAL_UINT32(held.inUse, world.stats().inUse);
    TEST_ASSERT_EQUAL_UINT32(held.freeBytes, world.stats().freeBytes);
    TEST_ASSERT_EQUAL_UINT32(1u, world.thinkers.count);

    // The removal is deferred: the next tick unlinks and frees it.
    TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_run_thinkers(&world.thinkers,
                                                    &p_level_release_thinker,
                                                    &world.context, &stats));
    TEST_ASSERT_EQUAL_UINT32(1u, stats.removed);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.executed);
    TEST_ASSERT_EQUAL_UINT32(0u, world.thinkers.count);
    TEST_ASSERT_EQUAL_UINT32(0u, world.stats().inUse);
    TEST_ASSERT_EQUAL_UINT32(sizeof world.arena - ZONE_HEADER_SIZE,
                             world.stats().freeBytes);
}

// --- The random sequence ----------------------------------------------------

void test_rng_returns_upstreams_first_sixteen_values(void) {
    static const i32 expected[16] = {8, 109, 220, 222, 241, 149, 107, 75,
                                     248, 254, 140, 16, 66, 74, 21, 211};
    m_random_t random;
    m_random_t other;

    m_random_init(&random);
    m_random_init(&other);

    for (int i = 0; i < 16; ++i) {
        const i32 value = p_random(&random);

        TEST_ASSERT_EQUAL_INT32(expected[i], value);
        TEST_ASSERT_TRUE(value >= 0 && value <= 255);
        // A second world keeps its own index.
        TEST_ASSERT_EQUAL_INT32(expected[i], p_random(&other));
    }
    TEST_ASSERT_EQUAL_UINT32(16u, random.index);
}

void test_rng_wraps_after_a_full_table(void) {
    m_random_t random;
    i32 first = 0;
    i32 after = 0;

    m_random_init(&random);
    first = p_random(&random);
    for (int i = 0; i < 255; ++i) {
        (void)p_random(&random);
    }
    after = p_random(&random);

    TEST_ASSERT_EQUAL_INT32(first, after);
}

// --- The map-thing filters --------------------------------------------------

void test_skill_filter_accepts_and_rejects_the_right_things(void) {
    World world;
    p_mobj_context_t context{};

    world.open();
    context = world.context;
    context.rules = p_spawn_rules_default();
    context.rules.skill = P_SKILL_MEDIUM;   // bit 2

    p_mapthing_t all{};
    p_mapthing_t hardOnly{};
    p_mapthing_t mediumOnly{};
    p_mapthing_t notSingle{};

    all.type = 3004;
    all.options = 7;
    hardOnly.type = 3004;
    hardOnly.options = 4;
    mediumOnly.type = 3004;
    mediumOnly.options = 2;
    notSingle.type = 3004;
    notSingle.options = static_cast<i16>(2 | 16);

    TEST_ASSERT_EQUAL_INT(P_THING_SPAWNED, p_spawn_map_thing(&context, &all).result);
    TEST_ASSERT_EQUAL_INT(P_THING_SKIPPED_SKILL,
                          p_spawn_map_thing(&context, &hardOnly).result);
    TEST_ASSERT_EQUAL_INT(P_THING_SPAWNED,
                          p_spawn_map_thing(&context, &mediumOnly).result);
    TEST_ASSERT_EQUAL_INT(P_THING_SKIPPED_SINGLE,
                          p_spawn_map_thing(&context, &notSingle).result);
    TEST_ASSERT_EQUAL_UINT32(2u, world.mobjCount());
}

void test_unknown_doomednum_is_refused_not_spawned(void) {
    World world;
    p_mapthing_t thing{};

    world.open();
    thing.type = 32000;
    thing.options = 7;

    const p_thing_outcome_t outcome = p_spawn_map_thing(&world.context, &thing);
    TEST_ASSERT_EQUAL_INT(P_THING_UNKNOWN_TYPE, outcome.result);
    TEST_ASSERT_NULL(outcome.mobj);
    TEST_ASSERT_EQUAL_UINT32(0u, world.mobjCount());
}

void test_level_counts_kills_items_and_player_starts(void) {
    MapBuilder builder;
    doom_map_t map{};
    alignas(8) u8 arena[kArenaBytes]{};
    p_level_t level{};

    // One monster (MF_COUNTKILL), one health bonus (MF_COUNTITEM), four player
    // starts and one deathmatch start.
    builder.addThing(0, 0, 0, 3004, 7);
    builder.addThing(8, 0, 0, 2014, 7);
    builder.addThing(0, 8, 90, 1, 7);
    builder.addThing(8, 8, 90, 2, 7);
    builder.addThing(16, 8, 90, 3, 7);
    builder.addThing(24, 8, 90, 4, 7);
    builder.addThing(32, 8, 180, 11, 7);
    TEST_ASSERT_EQUAL_INT(DOOM_MAP_OK, builder.open(&map));

    TEST_ASSERT_EQUAL_INT(P_SETUP_OK,
                          p_level_init(&level, &map, arena, sizeof arena, nullptr));
    TEST_ASSERT_EQUAL_INT32(1, level.totalKills);
    TEST_ASSERT_EQUAL_INT32(1, level.totalItems);
    TEST_ASSERT_EQUAL_UINT32(7u, level.stats.things);
    TEST_ASSERT_EQUAL_UINT32(3u, level.stats.spawned);       // monster, bonus, player 1
    TEST_ASSERT_EQUAL_UINT32(4u, level.stats.playerStarts);
    TEST_ASSERT_EQUAL_UINT32(1u, level.stats.deathmatchStarts);
    TEST_ASSERT_EQUAL_UINT32(1u, level.deathmatchStartCount);
    TEST_ASSERT_NOT_NULL(level.player);
    TEST_ASSERT_EQUAL_INT(MT_PLAYER, level.player->type);
    TEST_ASSERT_EQUAL_INT32(0, level.player->x);   // the type-1 record
    TEST_ASSERT_EQUAL_INT32(8 * kFrac, level.player->y);
    TEST_ASSERT_EQUAL_UINT32(static_cast<u32>(ANG45 * 2), level.player->angle);
    TEST_ASSERT_EQUAL_INT32(100, level.player->health);
    TEST_ASSERT_EQUAL_UINT32(3u, p_level_mobj_count(&level));

    p_level_shutdown(&level);
    TEST_ASSERT_EQUAL_INT(0, level.open);
    TEST_ASSERT_EQUAL_UINT32(0u, level.thinkers.count);
}

void test_level_medium_skill_drops_hard_only_monsters(void) {
    MapBuilder builder;
    doom_map_t map{};
    alignas(8) u8 arena[kArenaBytes]{};
    p_level_t level{};
    p_spawn_rules_t rules = p_spawn_rules_default();

    builder.addThing(0, 0, 0, 3004, 7);
    builder.addThing(8, 0, 0, 3004, 4);     // hard only: not on medium
    builder.addThing(16, 0, 0, 3004, 1);    // baby only: not on medium
    TEST_ASSERT_EQUAL_INT(DOOM_MAP_OK, builder.open(&map));

    rules.skill = P_SKILL_MEDIUM;
    TEST_ASSERT_EQUAL_INT(P_SETUP_OK,
                          p_level_init(&level, &map, arena, sizeof arena, &rules));
    TEST_ASSERT_EQUAL_INT32(1, level.totalKills);
    TEST_ASSERT_EQUAL_UINT32(1u, level.stats.spawned);
    TEST_ASSERT_EQUAL_UINT32(2u, level.stats.skippedSkill);

    p_level_shutdown(&level);
}

// --- Action dispatch --------------------------------------------------------

void test_action_dispatch_counts_what_is_missing(void) {
    World world;
    p_mobj_error_t error = P_MOBJ_ERR_NULL_MOBJ;

    world.open();
    r_mobj_t* mobj = p_spawn_mobj(&world.context, 0, 0, ONFLOORZ, MT_PLAYER, &error);
    TEST_ASSERT_NOT_NULL(mobj);

    TEST_ASSERT_EQUAL_INT(P_ACTION_NONE, p_action_run(&world.context, mobj, ACT_NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, world.actions.calls);

    // A_Look is the one action D4.1a ships, as a deliberate stub.
    TEST_ASSERT_EQUAL_INT(P_ACTION_STUB, p_action_run(&world.context, mobj, A_Look));
    TEST_ASSERT_EQUAL_UINT32(1u, world.actions.calls);
    TEST_ASSERT_EQUAL_UINT32(1u, world.actions.stubs);
    TEST_ASSERT_EQUAL_UINT32(0u, world.actions.notImplemented);

    // Everything else is counted, never silently ignored.
    TEST_ASSERT_EQUAL_INT(P_ACTION_NOT_IMPLEMENTED,
                          p_action_run(&world.context, mobj, A_Chase));
    TEST_ASSERT_EQUAL_UINT32(2u, world.actions.calls);
    TEST_ASSERT_EQUAL_UINT32(1u, world.actions.notImplemented);
    TEST_ASSERT_EQUAL_UINT32(static_cast<u32>(A_Chase),
                             world.actions.lastNotImplemented);

    TEST_ASSERT_EQUAL_INT(P_ACTION_UNKNOWN,
                          p_action_run(&world.context, mobj,
                                       static_cast<action_t>(NUM_ACTIONS + 3)));
    TEST_ASSERT_EQUAL_UINT32(1u, world.actions.unknown);
    TEST_ASSERT_EQUAL_UINT32(3u, world.actions.calls);

    TEST_ASSERT_EQUAL_INT(P_ACTION_NULL_MOBJ,
                          p_action_run(&world.context, nullptr, A_Look));
    TEST_ASSERT_EQUAL_UINT32(1u, world.actions.nullMobj);
}

void test_state_transition_dispatches_the_new_state_action(void) {
    World world;
    p_mobj_error_t error = P_MOBJ_ERR_NULL_MOBJ;

    world.open();
    r_mobj_t* mobj = p_spawn_mobj(&world.context, 0, 0, ONFLOORZ, MT_PLAYER, &error);
    TEST_ASSERT_NOT_NULL(mobj);

    // S_POSS_STND's action is A_Look: setting the state must run it, and the
    // dispatch must see it as the stub it is.
    TEST_ASSERT_EQUAL_INT(A_Look, states[S_POSS_STND].action);
    TEST_ASSERT_EQUAL_INT(1, p_set_mobj_state(&world.context, mobj, S_POSS_STND, &error));
    TEST_ASSERT_EQUAL_UINT32(1u, world.actions.calls);
    TEST_ASSERT_EQUAL_UINT32(1u, world.actions.stubs);
}

// --- The mobj thinker -------------------------------------------------------

void test_ticking_a_mobj_advances_its_state(void) {
    World world;
    p_mobj_error_t error = P_MOBJ_ERR_NULL_MOBJ;
    p_thinker_run_t stats{};

    world.open();
    r_mobj_t* mobj = p_spawn_mobj(&world.context, 0, 0, ONFLOORZ, MT_PLAYER, &error);
    TEST_ASSERT_NOT_NULL(mobj);
    // Returns 1 while the mobj is still present: S_PLAY_RUN1 does not reach
    // S_NULL, so it is not removed.
    TEST_ASSERT_EQUAL_INT(1, p_set_mobj_state(&world.context, mobj, S_PLAY_RUN1,
                                              &error));
    const i16 tics = mobj->tics;
    TEST_ASSERT_TRUE(tics > 0);

    TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_run_thinkers(&world.thinkers,
                                                    &p_level_release_thinker,
                                                    &world.context, &stats));
    TEST_ASSERT_EQUAL_UINT32(1u, stats.executed);
    TEST_ASSERT_EQUAL_INT(tics - 1, mobj->tics);
}
// --- D4.1b: sector and line specials ----------------------------------------

/*
 * One sector and one line as a test writes them. The builder below emits the
 * 1993 record layout byte for byte, like MapBuilder; it exists separately
 * because these cases need several sectors with heights that decide each
 * other's targets, and an arbitrary number of tagged, two-sided lines.
 */
struct SpecSectorSpec {
    i16 floor;
    i16 ceiling;
    i16 light;
    i16 special;
    i16 tag;
};

struct SpecMapBuilder {
    std::vector<u8> things;
    std::vector<u8> linedefs;
    std::vector<u8> sidedefs;
    std::vector<u8> vertexes;
    std::vector<u8> segs;
    std::vector<u8> ssectors;
    std::vector<u8> sectors;
    i16 lineCount = 0;
    i16 sideCount = 0;
    i16 segCount = 0;

    explicit SpecMapBuilder(const std::vector<SpecSectorSpec>& specs) {
        for (const SpecSectorSpec& sector : specs) {
            put16(sectors, sector.floor);
            put16(sectors, sector.ceiling);
            putName(sectors, "FLOOR4_8");
            putName(sectors, "CEIL3_5");
            put16(sectors, sector.light);
            put16(sectors, sector.special);
            put16(sectors, sector.tag);
        }
    }

    void addSide(i16 sector) {
        put16(sidedefs, 0);          // texture offset
        put16(sidedefs, 0);          // row offset
        putName(sidedefs, "-");
        putName(sidedefs, "-");
        putName(sidedefs, "STARTAN3");
        put16(sidedefs, sector);
        sideCount = static_cast<i16>(sideCount + 1);
    }

    /// One line, two vertices and one seg (side 0). `twoSided` adds side 1.
    void addLine(i16 flags, i16 special, i16 tag, i16 front, i16 back,
                 bool twoSided) {
        const i16 index = lineCount;

        put16(vertexes, static_cast<i16>(index * 64));
        put16(vertexes, 0);
        put16(vertexes, static_cast<i16>(index * 64 + 32));
        put16(vertexes, 0);

        put16(linedefs, static_cast<i16>(index * 2));
        put16(linedefs, static_cast<i16>(index * 2 + 1));
        put16(linedefs, flags);
        put16(linedefs, special);
        put16(linedefs, tag);
        put16(linedefs, sideCount);
        if (twoSided) {
            put16(linedefs, static_cast<i16>(sideCount + 1));
        } else {
            put16(linedefs, kNoSide);
        }
        addSide(front);
        if (twoSided) {
            addSide(back);
        }

        put16(segs, static_cast<i16>(index * 2));
        put16(segs, static_cast<i16>(index * 2 + 1));
        put16(segs, 0);
        put16(segs, index);
        put16(segs, 0);
        put16(segs, 0);
        lineCount = static_cast<i16>(lineCount + 1);
        segCount = static_cast<i16>(segCount + 1);
    }

    doom_map_error_t open(doom_map_t* map) {
        doom_map_source_t source{};

        ssectors.clear();
        put16(ssectors, segCount);
        put16(ssectors, 0);

        source.data[DOOM_MAP_THINGS] = things.empty() ? nullptr : things.data();
        source.size[DOOM_MAP_THINGS] = things.size();
        source.data[DOOM_MAP_LINEDEFS] = linedefs.data();
        source.size[DOOM_MAP_LINEDEFS] = linedefs.size();
        source.data[DOOM_MAP_SIDEDEFS] = sidedefs.data();
        source.size[DOOM_MAP_SIDEDEFS] = sidedefs.size();
        source.data[DOOM_MAP_VERTEXES] = vertexes.data();
        source.size[DOOM_MAP_VERTEXES] = vertexes.size();
        source.data[DOOM_MAP_SEGS] = segs.data();
        source.size[DOOM_MAP_SEGS] = segs.size();
        source.data[DOOM_MAP_SSECTORS] = ssectors.data();
        source.size[DOOM_MAP_SSECTORS] = ssectors.size();
        source.data[DOOM_MAP_SECTORS] = sectors.data();
        source.size[DOOM_MAP_SECTORS] = sectors.size();
        return doom_map_open(map, &source);
    }
};

/* A whole level on the stack, with p_spawn_specials() already run. */
struct SpecFixture {
    SpecMapBuilder builder;
    doom_map_t map{};
    alignas(8) u8 arena[32768]{};
    p_level_t level{};

    explicit SpecFixture(const std::vector<SpecSectorSpec>& sectors)
        : builder(sectors) {}

    bool open() {
        if (builder.open(&map) != DOOM_MAP_OK) {
            return false;
        }
        if (p_level_init(&level, &map, arena, sizeof arena, nullptr) != P_SETUP_OK) {
            return false;
        }
        return p_spawn_specials(&level) == P_SPEC_OK;
    }

    void tick(int count = 1) {
        for (int i = 0; i < count; ++i) {
            (void)p_level_tick(&level, nullptr);
        }
    }

    p_thinker_t* first(i16 kind) {
        for (const p_thinker_t* thinker = p_thinker_first(&level.thinkers);
             thinker != nullptr; thinker = p_thinker_next(thinker)) {
            if (thinker->kind == kind) {
                return const_cast<p_thinker_t*>(thinker);
            }
        }
        return nullptr;
    }

    int count(i16 kind) const {
        int found = 0;

        for (const p_thinker_t* thinker = p_thinker_first(&level.thinkers);
             thinker != nullptr; thinker = p_thinker_next(thinker)) {
            if (thinker->kind == kind) {
                found += 1;
            }
        }
        return found;
    }

    i16 sectorOfSlot(int slot) const {
        int index = 0;

        for (const p_thinker_t* thinker = p_thinker_first(&level.thinkers);
             thinker != nullptr; thinker = p_thinker_next(thinker)) {
            if (thinker->kind == P_THINKER_MOBJ) {
                continue;
            }
            if (index == slot) {
                if (thinker->kind == P_THINKER_DOOR) {
                    return static_cast<i16>(
                        reinterpret_cast<const p_door_thinker_t*>(thinker)->sector);
                }
                if (thinker->kind == P_THINKER_FLOOR) {
                    return static_cast<i16>(
                        reinterpret_cast<const p_floor_thinker_t*>(thinker)->sector);
                }
                if (thinker->kind == P_THINKER_PLAT) {
                    return static_cast<i16>(
                        reinterpret_cast<const p_plat_thinker_t*>(thinker)->sector);
                }
                if (thinker->kind == P_THINKER_CEILING) {
                    return static_cast<i16>(
                        reinterpret_cast<const p_ceiling_thinker_t*>(thinker)->sector);
                }
                return static_cast<i16>(
                    reinterpret_cast<const p_light_thinker_t*>(thinker)->sector);
            }
            index += 1;
        }
        return -1;
    }
};

p_door_thinker_t* doorOf(p_thinker_t* thinker) {
    return reinterpret_cast<p_door_thinker_t*>(thinker);
}

p_plat_thinker_t* platOf(p_thinker_t* thinker) {
    return reinterpret_cast<p_plat_thinker_t*>(thinker);
}

p_ceiling_thinker_t* ceilingOf(p_thinker_t* thinker) {
    return reinterpret_cast<p_ceiling_thinker_t*>(thinker);
}

p_light_thinker_t* lightOf(p_thinker_t* thinker) {
    return reinterpret_cast<p_light_thinker_t*>(thinker);
}

/// Ticks until the predicate says stop, with a hard cap so a broken mover fails
/// the case instead of hanging it.
template <typename Predicate>
int tickUntil(SpecFixture& fixture, Predicate done, int cap = 400) {
    for (int i = 0; i < cap; ++i) {
        if (done()) {
            return i;
        }
        fixture.tick();
    }
    return -1;
}

void test_specs_door_opens_to_its_top_and_stops(void) {
    // The door sector's ceiling is at the floor; the neighbour's is 128, so the
    // top is 128 - 4 = 124 (p_doors.c EV_DoDoor).
    SpecFixture fixture({{0, 0, 160, 0, 1}, {0, 128, 160, 0, 0}});
    fixture.builder.addLine(4, 4, 1, 0, 1, true);

    TEST_ASSERT_TRUE(fixture.open());
    TEST_ASSERT_EQUAL_INT(0, fixture.count(P_THINKER_DOOR));
    TEST_ASSERT_EQUAL_INT(1, p_trigger_special_line(&fixture.level, 0,
                                                    P_TRIGGER_CROSS, 1));

    p_thinker_t* door = fixture.first(P_THINKER_DOOR);
    TEST_ASSERT_NOT_NULL(door);
    TEST_ASSERT_EQUAL_INT(P_DOOR_NORMAL, doorOf(door)->type);
    TEST_ASSERT_EQUAL_INT(1, doorOf(door)->direction);
    TEST_ASSERT_EQUAL_INT32(124 * kFrac, doorOf(door)->topHeight);
    TEST_ASSERT_EQUAL_INT32(2 * kFrac, doorOf(door)->speed);

    TEST_ASSERT_TRUE(tickUntil(fixture, [&] {
        return doorOf(door)->direction == 0;
    }) >= 0);
    TEST_ASSERT_EQUAL_INT32(124 * kFrac, fixture.level.sectors[0].ceilingHeight);
    TEST_ASSERT_EQUAL_INT32(150, doorOf(door)->topCountdown);
    TEST_ASSERT_EQUAL_INT(1, fixture.count(P_THINKER_DOOR));
}

void test_specs_door_waits_then_closes_and_removes_itself(void) {
    SpecFixture fixture({{0, 0, 160, 0, 1}, {0, 128, 160, 0, 0}});
    fixture.builder.addLine(4, 4, 1, 0, 1, true);

    TEST_ASSERT_TRUE(fixture.open());
    TEST_ASSERT_EQUAL_INT(1, p_trigger_special_line(&fixture.level, 0,
                                                    P_TRIGGER_CROSS, 1));
    p_thinker_t* door = fixture.first(P_THINKER_DOOR);
    TEST_ASSERT_NOT_NULL(door);
    TEST_ASSERT_TRUE(tickUntil(fixture, [&] {
        return doorOf(door)->direction == 0;
    }) >= 0);

    // One tic short of the wait, the door is still waiting.
    fixture.tick(149);
    TEST_ASSERT_EQUAL_INT(0, doorOf(door)->direction);
    fixture.tick();
    TEST_ASSERT_EQUAL_INT(-1, doorOf(door)->direction);

    TEST_ASSERT_TRUE(tickUntil(fixture, [&] {
        return fixture.count(P_THINKER_DOOR) == 0;
    }) >= 0);
    TEST_ASSERT_EQUAL_INT32(0, fixture.level.sectors[0].ceilingHeight);
    TEST_ASSERT_NULL(fixture.level.sectors[0].specialdata);
}

void test_specs_door_closing_over_a_thing_stops_at_its_height(void) {
    // A player stands in the door sector. P_ChangeSector now runs the real
    // P_ThingHeightClip, so the closing door stops at the player's height and
    // reports "does not fit" every tic instead of reaching the floor. Special 3
    // is this port's P_DOOR_CLOSE, whose crushed branch does not go back up
    // (p_doors.c), so the door hovers; the player is not damaged (D4.2d owns
    // P_DamageMobj) and the counter says so.
    SpecFixture fixture({{0, 128, 160, 0, 1}, {0, 128, 160, 0, 0}});
    fixture.builder.addLine(4, 3, 1, 0, 1, true);

    TEST_ASSERT_TRUE(fixture.open());
    p_mobj_error_t error = P_MOBJ_ERR_NULL_MOBJ;
    r_mobj_t* mobj = p_spawn_mobj(&fixture.level.context, 0, 0, ONFLOORZ,
                                  MT_PLAYER, &error);
    TEST_ASSERT_NOT_NULL(mobj);
    TEST_ASSERT_EQUAL_UINT16(0u, mobj->sector);
    TEST_ASSERT_EQUAL_INT32(128 * kFrac, mobj->ceilingz);

    TEST_ASSERT_EQUAL_INT(1, p_trigger_special_line(&fixture.level, 0,
                                                    P_TRIGGER_CROSS, 1));
    p_thinker_t* door = fixture.first(P_THINKER_DOOR);
    TEST_ASSERT_NOT_NULL(door);

    // Open, wait 150, close: the first downward direction is -1.
    TEST_ASSERT_TRUE(tickUntil(fixture, [&] {
        return doorOf(door)->direction == -1;
    }) >= 0);

    // The close is refused when the next step would leave less than the
    // player's height; the plane is reverted to the last height that fits.
    TEST_ASSERT_TRUE(tickUntil(fixture, [&] {
        return fixture.level.sectors[0].ceilingHeight == mobj->height;
    }) >= 0);
    fixture.tick(50);
    TEST_ASSERT_EQUAL_INT32(mobj->height,
                            fixture.level.sectors[0].ceilingHeight);
    TEST_ASSERT_EQUAL_INT(-1, doorOf(door)->direction);
    TEST_ASSERT_EQUAL_INT(1, fixture.count(P_THINKER_DOOR));
    TEST_ASSERT_EQUAL_INT32(mobj->height, mobj->ceilingz);
    TEST_ASSERT_TRUE(fixture.level.specStats.unsupportedCrush > 0u);
    TEST_ASSERT_TRUE(fixture.level.specStats.changeSectorClear <
                     fixture.level.specStats.changeSectorCalls);
    TEST_ASSERT_EQUAL_INT(0, p_spec_p_change_sector_is_stubbed());
    TEST_ASSERT_TRUE(fixture.level.specStats.thingRefreshes > 0u);
}

void test_specs_plat_goes_down_waits_and_returns(void) {
    SpecFixture fixture({{64, 128, 160, 0, 1}, {0, 128, 160, 0, 0}});
    fixture.builder.addLine(4, 88, 1, 0, 1, true);

    TEST_ASSERT_TRUE(fixture.open());
    TEST_ASSERT_EQUAL_INT(1, p_trigger_special_line(&fixture.level, 0,
                                                    P_TRIGGER_CROSS, 1));
    p_thinker_t* thinker = fixture.first(P_THINKER_PLAT);
    TEST_ASSERT_NOT_NULL(thinker);
    TEST_ASSERT_EQUAL_INT(P_PLAT_DOWN_WAIT_UP_STAY, platOf(thinker)->type);
    TEST_ASSERT_EQUAL_INT(P_PLAT_DOWN, platOf(thinker)->status);
    TEST_ASSERT_EQUAL_INT32(0, platOf(thinker)->low);
    TEST_ASSERT_EQUAL_INT32(64 * kFrac, platOf(thinker)->high);
    TEST_ASSERT_EQUAL_INT32(35 * P_PLATWAIT, platOf(thinker)->wait);

    TEST_ASSERT_TRUE(tickUntil(fixture, [&] {
        return platOf(thinker)->status == P_PLAT_WAITING;
    }) >= 0);
    TEST_ASSERT_EQUAL_INT32(0, fixture.level.sectors[0].floorHeight);
    TEST_ASSERT_EQUAL_INT32(35 * P_PLATWAIT, platOf(thinker)->count);

    fixture.tick(35 * P_PLATWAIT);
    TEST_ASSERT_EQUAL_INT(P_PLAT_UP, platOf(thinker)->status);

    TEST_ASSERT_TRUE(tickUntil(fixture, [&] {
        return fixture.count(P_THINKER_PLAT) == 0;
    }) >= 0);
    TEST_ASSERT_EQUAL_INT32(64 * kFrac, fixture.level.sectors[0].floorHeight);
    TEST_ASSERT_NULL(fixture.level.sectors[0].specialdata);
}

void test_specs_raise_floor_stops_at_the_lowest_surrounding_ceiling(void) {
    // The target is the neighbour's ceiling, not the sector's own (p_floor.c
    // EV_DoFloor, raiseFloor).
    SpecFixture fixture({{0, 256, 160, 0, 1}, {0, 128, 160, 0, 0}});
    fixture.builder.addLine(4, 5, 1, 0, 1, true);

    TEST_ASSERT_TRUE(fixture.open());
    TEST_ASSERT_EQUAL_INT(1, p_trigger_special_line(&fixture.level, 0,
                                                    P_TRIGGER_CROSS, 1));
    TEST_ASSERT_EQUAL_INT(1, fixture.count(P_THINKER_FLOOR));
    TEST_ASSERT_EQUAL_INT32(128 * kFrac,
                            reinterpret_cast<p_floor_thinker_t*>(
                                fixture.first(P_THINKER_FLOOR))->destHeight);

    TEST_ASSERT_TRUE(tickUntil(fixture, [&] {
        return fixture.count(P_THINKER_FLOOR) == 0;
    }) >= 0);
    TEST_ASSERT_EQUAL_INT32(128 * kFrac, fixture.level.sectors[0].floorHeight);
}

void test_specs_lower_floor_stops_at_the_highest_surrounding_floor(void) {
    // Two neighbours at 0 and 64: lowerFloor targets the higher one.
    SpecFixture fixture({{128, 256, 160, 0, 1},
                         {0, 256, 160, 0, 0},
                         {64, 256, 160, 0, 0}});
    fixture.builder.addLine(4, 19, 1, 0, 1, true);
    fixture.builder.addLine(4, 0, 0, 0, 2, true);

    TEST_ASSERT_TRUE(fixture.open());
    TEST_ASSERT_EQUAL_INT(1, p_trigger_special_line(&fixture.level, 0,
                                                    P_TRIGGER_CROSS, 1));
    TEST_ASSERT_EQUAL_INT32(64 * kFrac,
                            reinterpret_cast<p_floor_thinker_t*>(
                                fixture.first(P_THINKER_FLOOR))->destHeight);

    TEST_ASSERT_TRUE(tickUntil(fixture, [&] {
        return fixture.count(P_THINKER_FLOOR) == 0;
    }) >= 0);
    TEST_ASSERT_EQUAL_INT32(64 * kFrac, fixture.level.sectors[0].floorHeight);
}

void test_specs_ceiling_crush_and_raise_reverses(void) {
    SpecFixture fixture({{0, 128, 160, 0, 1}, {0, 128, 160, 0, 0}});
    fixture.builder.addLine(4, 25, 1, 0, 1, true);

    TEST_ASSERT_TRUE(fixture.open());
    TEST_ASSERT_EQUAL_INT(1, p_trigger_special_line(&fixture.level, 0,
                                                    P_TRIGGER_CROSS, 1));
    p_thinker_t* thinker = fixture.first(P_THINKER_CEILING);
    TEST_ASSERT_NOT_NULL(thinker);
    TEST_ASSERT_EQUAL_INT(P_CEIL_CRUSH_AND_RAISE, ceilingOf(thinker)->type);
    TEST_ASSERT_EQUAL_INT32(8 * kFrac, ceilingOf(thinker)->bottomHeight);

    TEST_ASSERT_TRUE(tickUntil(fixture, [&] {
        return ceilingOf(thinker)->direction == 1;
    }) >= 0);
    TEST_ASSERT_EQUAL_INT32(8 * kFrac, fixture.level.sectors[0].ceilingHeight);

    TEST_ASSERT_TRUE(tickUntil(fixture, [&] {
        return ceilingOf(thinker)->direction == -1;
    }) >= 0);
    TEST_ASSERT_EQUAL_INT32(128 * kFrac, fixture.level.sectors[0].ceilingHeight);
    TEST_ASSERT_EQUAL_INT(1, fixture.count(P_THINKER_CEILING));
}

void test_specs_light_flash_period_uses_both_random_draws(void) {
    // With no THINGS, p_level_init() leaves the RNG at 0, so the draws are
    // upstream's 8, 109, 220, ... The first count is (8 & 64) + 1 = 9; the
    // down phase uses maxtime (64), the up phase mintime (7).
    SpecFixture fixture({{0, 128, 200, 1, 0}, {0, 128, 100, 0, 0}});
    fixture.builder.addLine(4, 0, 0, 0, 1, true);

    TEST_ASSERT_TRUE(fixture.open());
    TEST_ASSERT_EQUAL_INT(1, fixture.count(P_THINKER_LIGHTFLASH));
    p_thinker_t* thinker = fixture.first(P_THINKER_LIGHTFLASH);
    TEST_ASSERT_NOT_NULL(thinker);
    TEST_ASSERT_EQUAL_INT(P_LIGHT_FLASH, lightOf(thinker)->type);
    TEST_ASSERT_EQUAL_INT32(200, lightOf(thinker)->maxLight);
    TEST_ASSERT_EQUAL_INT32(100, lightOf(thinker)->minLight);
    TEST_ASSERT_EQUAL_INT32(1, lightOf(thinker)->count);   // (8 & 64) + 1
    TEST_ASSERT_EQUAL_INT(200, fixture.level.sectors[0].lightLevel);

    fixture.tick();
    TEST_ASSERT_EQUAL_INT(100, fixture.level.sectors[0].lightLevel);
    TEST_ASSERT_EQUAL_INT32(65, lightOf(thinker)->count);   // (109 & 64) + 1

    fixture.tick(64);
    TEST_ASSERT_EQUAL_INT32(1, lightOf(thinker)->count);
    fixture.tick();
    TEST_ASSERT_EQUAL_INT(200, fixture.level.sectors[0].lightLevel);
    TEST_ASSERT_EQUAL_INT32(5, lightOf(thinker)->count);    // (220 & 7) + 1
}

void test_specs_strobe_two_phase_timing(void) {
    // Sector special 12 is a slow strobe in sync: its count starts at 1 and
    // then alternates dark 35 / bright 5.
    SpecFixture fixture({{0, 128, 200, 12, 0}, {0, 128, 100, 0, 0}});
    fixture.builder.addLine(4, 0, 0, 0, 1, true);

    TEST_ASSERT_TRUE(fixture.open());
    p_thinker_t* thinker = fixture.first(P_THINKER_LIGHTFLASH);
    TEST_ASSERT_NOT_NULL(thinker);
    TEST_ASSERT_EQUAL_INT(P_LIGHT_STROBE, lightOf(thinker)->type);
    TEST_ASSERT_EQUAL_INT32(1, lightOf(thinker)->count);
    TEST_ASSERT_EQUAL_INT32(35, lightOf(thinker)->darkTime);
    TEST_ASSERT_EQUAL_INT32(5, lightOf(thinker)->brightTime);

    fixture.tick();
    TEST_ASSERT_EQUAL_INT(100, fixture.level.sectors[0].lightLevel);
    TEST_ASSERT_EQUAL_INT32(35, lightOf(thinker)->count);

    fixture.tick(34);
    TEST_ASSERT_EQUAL_INT(100, fixture.level.sectors[0].lightLevel);
    fixture.tick();
    TEST_ASSERT_EQUAL_INT(200, fixture.level.sectors[0].lightLevel);
    TEST_ASSERT_EQUAL_INT32(5, lightOf(thinker)->count);

    fixture.tick(4);
    TEST_ASSERT_EQUAL_INT(200, fixture.level.sectors[0].lightLevel);
    fixture.tick();
    TEST_ASSERT_EQUAL_INT(100, fixture.level.sectors[0].lightLevel);
    TEST_ASSERT_EQUAL_INT32(35, lightOf(thinker)->count);
}

void test_specs_perpetual_plat_stops_and_resumes_then_reverses(void) {
    // 87/89 are the retrigger forms, so the line survives the first use.
    SpecFixture fixture({{64, 128, 160, 0, 1},
                         {0, 128, 160, 0, 0},
                         {128, 256, 160, 0, 0}});
    fixture.builder.addLine(4, 87, 1, 0, 1, true);
    fixture.builder.addLine(4, 89, 1, 0, 2, true);

    TEST_ASSERT_TRUE(fixture.open());
    TEST_ASSERT_EQUAL_INT(1, p_trigger_special_line(&fixture.level, 0,
                                                    P_TRIGGER_CROSS, 1));
    p_thinker_t* thinker = fixture.first(P_THINKER_PLAT);
    TEST_ASSERT_NOT_NULL(thinker);
    TEST_ASSERT_EQUAL_INT32(0, platOf(thinker)->low);
    TEST_ASSERT_EQUAL_INT32(128 * kFrac, platOf(thinker)->high);
    // P_Random() at 0 answers 8, so 8 & 1 == 0 -> up.
    TEST_ASSERT_EQUAL_INT(P_PLAT_UP, platOf(thinker)->status);

    fixture.tick(10);
    const fixed_t halfway = fixture.level.sectors[0].floorHeight;
    TEST_ASSERT_TRUE(halfway > 64 * kFrac);

    TEST_ASSERT_EQUAL_INT(1, p_trigger_special_line(&fixture.level, 1,
                                                    P_TRIGGER_CROSS, 1));
    TEST_ASSERT_EQUAL_INT(P_PLAT_IN_STASIS, platOf(thinker)->status);
    fixture.tick(10);
    TEST_ASSERT_EQUAL_INT32(halfway, fixture.level.sectors[0].floorHeight);

    TEST_ASSERT_EQUAL_INT(1, p_trigger_special_line(&fixture.level, 0,
                                                    P_TRIGGER_CROSS, 1));
    TEST_ASSERT_EQUAL_INT(P_PLAT_UP, platOf(thinker)->status);

    TEST_ASSERT_TRUE(tickUntil(fixture, [&] {
        return platOf(thinker)->status == P_PLAT_WAITING;
    }) >= 0);
    TEST_ASSERT_EQUAL_INT32(128 * kFrac, fixture.level.sectors[0].floorHeight);
    fixture.tick(35 * P_PLATWAIT);
    TEST_ASSERT_EQUAL_INT(P_PLAT_DOWN, platOf(thinker)->status);
    fixture.tick(10);
    TEST_ASSERT_TRUE(fixture.level.sectors[0].floorHeight < 128 * kFrac);
}

void test_specs_tag_door_twice_creates_one_thinker(void) {
    SpecFixture fixture({{0, 0, 160, 0, 1}, {0, 128, 160, 0, 0}});
    fixture.builder.addLine(4, 2, 1, 0, 1, true);

    TEST_ASSERT_TRUE(fixture.open());
    TEST_ASSERT_EQUAL_INT(1, p_do_door(&fixture.level.context, 0, P_DOOR_OPEN));
    // The second call is EV_DoDoor's merge rule: the sector already has a
    // thinker, so nothing is created and the call reports no change.
    TEST_ASSERT_EQUAL_INT(0, p_do_door(&fixture.level.context, 0, P_DOOR_OPEN));
    TEST_ASSERT_EQUAL_INT(1, fixture.count(P_THINKER_DOOR));
    TEST_ASSERT_EQUAL_INT(1, doorOf(fixture.first(P_THINKER_DOOR))->direction);
}

void test_specs_manual_door_twice_reverses_instead_of_duplicating(void) {
    // The manual door uses the line's *back* sector (p_doors.c
    // EV_VerticalDoor), so side 0 is the room and side 1 is the door.
    SpecFixture fixture({{0, 0, 160, 0, 1}, {0, 128, 160, 0, 0}});
    fixture.builder.addLine(4, 1, 0, 1, 0, true);

    TEST_ASSERT_TRUE(fixture.open());
    TEST_ASSERT_EQUAL_INT(1, p_trigger_special_line(&fixture.level, 0,
                                                    P_TRIGGER_USE, 1));
    p_thinker_t* door = fixture.first(P_THINKER_DOOR);
    TEST_ASSERT_NOT_NULL(door);
    TEST_ASSERT_EQUAL_INT(1, doorOf(door)->direction);

    TEST_ASSERT_EQUAL_INT(1, p_trigger_special_line(&fixture.level, 0,
                                                    P_TRIGGER_USE, 1));
    TEST_ASSERT_EQUAL_INT(1, fixture.count(P_THINKER_DOOR));
    TEST_ASSERT_EQUAL_INT(-1, doorOf(door)->direction);
}

void test_specs_secret_sectors_and_unsupported_specials_are_counted(void) {
    SpecFixture fixture({{0, 128, 160, 9, 0},
                         {0, 128, 160, 9, 0},
                         {0, 128, 160, 9, 0},
                         {0, 128, 160, 7, 0}});
    fixture.builder.addLine(4, 0, 0, 0, 1, true);

    TEST_ASSERT_EQUAL_INT(DOOM_MAP_OK, fixture.builder.open(&fixture.map));
    TEST_ASSERT_EQUAL_INT(P_SETUP_OK,
                          p_level_init(&fixture.level, &fixture.map, fixture.arena,
                                       sizeof fixture.arena, nullptr));
    // p_level_init alone is the THINGS step: no special has run yet.
    TEST_ASSERT_EQUAL_INT32(0, fixture.level.totalSecrets);
    TEST_ASSERT_EQUAL_INT(0, fixture.count(P_THINKER_LIGHTFLASH));

    TEST_ASSERT_EQUAL_INT(P_SPEC_OK, p_spawn_specials(&fixture.level));
    TEST_ASSERT_EQUAL_INT32(3, fixture.level.totalSecrets);
    TEST_ASSERT_EQUAL_UINT32(3u, fixture.level.specStats.secrets);
    TEST_ASSERT_EQUAL_UINT32(1u, fixture.level.specStats.unsupportedSectorSpecial);
    TEST_ASSERT_EQUAL_UINT32(0u, fixture.level.specStats.spawned);
    TEST_ASSERT_EQUAL_INT(0, fixture.count(P_THINKER_LIGHTFLASH));
}

void test_specs_spawn_order_is_sector_index_order(void) {
    // 13 (sync fast strobe) on sector 0, 9 (secret) on 1, 1 (flash) on 2,
    // 8 (glow) on 3: the thinkers must appear as 0, 2, 3.
    SpecFixture fixture({{0, 128, 160, 13, 0},
                         {0, 128, 160, 9, 0},
                         {0, 128, 160, 1, 0},
                         {0, 128, 160, 8, 0}});
    fixture.builder.addLine(4, 0, 0, 0, 2, true);
    fixture.builder.addLine(4, 0, 0, 2, 3, true);

    TEST_ASSERT_TRUE(fixture.open());
    TEST_ASSERT_EQUAL_INT(3, fixture.count(P_THINKER_LIGHTFLASH));
    TEST_ASSERT_EQUAL_INT(0, fixture.sectorOfSlot(0));
    TEST_ASSERT_EQUAL_INT(2, fixture.sectorOfSlot(1));
    TEST_ASSERT_EQUAL_INT(3, fixture.sectorOfSlot(2));
    TEST_ASSERT_EQUAL_INT(P_LIGHT_STROBE,
                          lightOf(fixture.first(P_THINKER_LIGHTFLASH))->type);
}

void test_specs_p_change_sector_counts_and_answers_clear(void) {
    SpecFixture fixture({{0, 128, 160, 0, 0}});
    fixture.builder.addLine(1, 0, 0, 0, 0, false);

    TEST_ASSERT_TRUE(fixture.open());
    const u32 before = fixture.level.specStats.changeSectorCalls;
    // With nothing in the sector the real rule answers "clear" like the stub
    // did, but the function is no longer a stub.
    TEST_ASSERT_EQUAL_INT(0, p_spec_p_change_sector_is_stubbed());
    TEST_ASSERT_EQUAL_INT(0, p_spec_p_change_sector(&fixture.level.context, 0));
    TEST_ASSERT_EQUAL_INT(0, p_spec_p_change_sector(&fixture.level.context, 0));
    TEST_ASSERT_EQUAL_UINT32(before + 2u,
                             fixture.level.specStats.changeSectorCalls);
    TEST_ASSERT_EQUAL_UINT32(before + 2u,
                             fixture.level.specStats.changeSectorClear);
}

// --- D4.2a: the line record carries its special and tag ---------------------

/*
 * doom_line_t is the one description of a LINEDEF, so it carries the two fields
 * every trigger reads instead of each of them re-reading the record's bytes.
 * The map below stores distinctive values in both, and the assertions go
 * through the public accessors only: the decoded record, the two accessors, and
 * the size the on-disk layout requires.
 */
void test_line_record_exposes_special_and_tag(void) {
    SpecFixture fixture({{0, 128, 160, 0, 0}, {0, 128, 160, 0, 0}});
    doom_line_t line{};

    fixture.builder.addLine(4, 88, 3, 0, 1, true);
    fixture.builder.addLine(4, 0, 0, 0, 1, true);

    TEST_ASSERT_EQUAL_INT(DOOM_MAP_OK, fixture.builder.open(&fixture.map));

    doom_map_get_line(&fixture.map, 0, &line);
    TEST_ASSERT_EQUAL_INT16(88, line.special);
    TEST_ASSERT_EQUAL_INT16(3, line.tag);
    TEST_ASSERT_EQUAL_UINT16(0u, line.v1);
    TEST_ASSERT_EQUAL_UINT16(1u, line.v2);
    TEST_ASSERT_EQUAL_INT16(4, line.flags);
    TEST_ASSERT_EQUAL_UINT16(0u, line.sideNum[0]);
    TEST_ASSERT_EQUAL_UINT16(1u, line.sideNum[1]);

    doom_map_get_line(&fixture.map, 1, &line);
    TEST_ASSERT_EQUAL_INT16(0, line.special);
    TEST_ASSERT_EQUAL_INT16(0, line.tag);

    // The accessors answer the same two fields, decoded once per call.
    TEST_ASSERT_EQUAL_INT16(88, p_line_special(&fixture.map, 0));
    TEST_ASSERT_EQUAL_INT16(3, p_line_tag(&fixture.map, 0));
    TEST_ASSERT_EQUAL_INT16(0, p_line_special(&fixture.map, 1));
    TEST_ASSERT_EQUAL_INT16(0, p_line_tag(&fixture.map, 1));

    // A line index outside the lump, and no map at all, still read 0.
    TEST_ASSERT_EQUAL_INT16(0, p_line_special(&fixture.map, 2));
    TEST_ASSERT_EQUAL_INT16(0, p_line_tag(&fixture.map, 2));
    TEST_ASSERT_EQUAL_INT16(0, p_line_special(nullptr, 0));
    TEST_ASSERT_EQUAL_INT16(0, p_line_tag(nullptr, 0));

    // 14 bytes: the two new shorts fill what used to be padding, so the struct
    // is exactly the on-disk record's size and no caller pays for a wider one.
    TEST_ASSERT_EQUAL_UINT32(DOOM_LINEDEF_SIZE, sizeof(doom_line_t));
}

// --- D4.2b: movement and collision ------------------------------------------

/*
 * A room with real geometry: four one-sided walls and, when a case needs one, a
 * two-sided line whose back sector decides the floor under it. Every point is
 * still in subsector 0 (the map has no NODES), so p_spawn_mobj places every
 * thing in the first line's front sector -- which is exactly what the height
 * rules then operate on.
 */
struct MoveMapBuilder {
    std::vector<u8> things;
    std::vector<u8> linedefs;
    std::vector<u8> sidedefs;
    std::vector<u8> vertexes;
    std::vector<u8> segs;
    std::vector<u8> ssectors;
    std::vector<u8> sectors;
    u16 vertexCount = 0;
    i16 lineCount = 0;
    i16 sideCount = 0;
    i16 segCount = 0;

    explicit MoveMapBuilder(const std::vector<SpecSectorSpec>& specs) {
        for (const SpecSectorSpec& sector : specs) {
            put16(sectors, sector.floor);
            put16(sectors, sector.ceiling);
            putName(sectors, "FLOOR4_8");
            putName(sectors, "CEIL3_5");
            put16(sectors, sector.light);
            put16(sectors, sector.special);
            put16(sectors, sector.tag);
        }
    }

    u16 addVertex(i16 x, i16 y) {
        put16(vertexes, x);
        put16(vertexes, y);
        return vertexCount++;
    }

    void addSide(i16 sector) {
        put16(sidedefs, 0);
        put16(sidedefs, 0);
        putName(sidedefs, "-");
        putName(sidedefs, "-");
        putName(sidedefs, "STARTAN3");
        put16(sidedefs, sector);
        sideCount = static_cast<i16>(sideCount + 1);
    }

    /// One line and one seg. `back` of -1 is a one-sided line.
    void addLine(u16 v1, u16 v2, i16 flags, i16 special, i16 tag, i16 front,
                 i16 back) {
        const i16 index = lineCount;

        put16(linedefs, static_cast<i16>(v1));
        put16(linedefs, static_cast<i16>(v2));
        put16(linedefs, flags);
        put16(linedefs, special);
        put16(linedefs, tag);
        put16(linedefs, sideCount);
        if (back >= 0) {
            put16(linedefs, static_cast<i16>(sideCount + 1));
        } else {
            put16(linedefs, kNoSide);
        }
        addSide(front);
        if (back >= 0) {
            addSide(back);
        }

        put16(segs, static_cast<i16>(v1));
        put16(segs, static_cast<i16>(v2));
        put16(segs, 0);
        put16(segs, index);
        put16(segs, 0);
        put16(segs, 0);
        lineCount = static_cast<i16>(lineCount + 1);
        segCount = static_cast<i16>(segCount + 1);
    }

    /// The four walls of a 256 x 256 room from (-128,-128) to (128,128).
    void addRoom(i16 front) {
        const u16 v0 = addVertex(-128, -128);
        const u16 v1 = addVertex(128, -128);
        const u16 v2 = addVertex(128, 128);
        const u16 v3 = addVertex(-128, 128);

        addLine(v0, v1, DOOM_ML_BLOCKING, 0, 0, front, -1);
        addLine(v1, v2, DOOM_ML_BLOCKING, 0, 0, front, -1);
        addLine(v2, v3, DOOM_ML_BLOCKING, 0, 0, front, -1);
        addLine(v3, v0, DOOM_ML_BLOCKING, 0, 0, front, -1);
    }

    /// One THINGS record, in the 1993 layout the loader reads.
    void addThing(i16 x, i16 y, i16 angle, i16 type, i16 options) {
        put16(things, x);
        put16(things, y);
        put16(things, angle);
        put16(things, type);
        put16(things, options);
    }

    doom_map_error_t open(doom_map_t* map) {
        doom_map_source_t source{};

        ssectors.clear();
        put16(ssectors, segCount);
        put16(ssectors, 0);

        source.data[DOOM_MAP_THINGS] = things.empty() ? nullptr : things.data();
        source.size[DOOM_MAP_THINGS] = things.size();
        source.data[DOOM_MAP_LINEDEFS] = linedefs.data();
        source.size[DOOM_MAP_LINEDEFS] = linedefs.size();
        source.data[DOOM_MAP_SIDEDEFS] = sidedefs.data();
        source.size[DOOM_MAP_SIDEDEFS] = sidedefs.size();
        source.data[DOOM_MAP_VERTEXES] = vertexes.data();
        source.size[DOOM_MAP_VERTEXES] = vertexes.size();
        source.data[DOOM_MAP_SEGS] = segs.data();
        source.size[DOOM_MAP_SEGS] = segs.size();
        source.data[DOOM_MAP_SSECTORS] = ssectors.data();
        source.size[DOOM_MAP_SSECTORS] = ssectors.size();
        source.data[DOOM_MAP_SECTORS] = sectors.data();
        source.size[DOOM_MAP_SECTORS] = sectors.size();
        return doom_map_open(map, &source);
    }
};

struct MoveFixture {
    MoveMapBuilder builder;
    doom_map_t map{};
    alignas(8) u8 arena[16384]{};
    zone_state_t zone{};
    p_thinker_list_t thinkers{};
    m_random_t random{};
    p_action_stats_t actions{};
    p_mobj_context_t context{};

    explicit MoveFixture(const std::vector<SpecSectorSpec>& sectors)
        : builder(sectors) {}

    bool open() {
        if (builder.open(&map) != DOOM_MAP_OK) {
            return false;
        }
        if (z_init(&zone, arena, sizeof arena) != ZONE_ERR_OK) {
            return false;
        }
        if (p_thinker_list_init(&thinkers) != P_TICK_OK) {
            return false;
        }
        m_random_init(&random);
        p_action_init(&actions);
        context.zone = &zone;
        context.thinkers = &thinkers;
        context.map = &map;
        context.random = &random;
        context.actions = &actions;
        context.rules = p_spawn_rules_default();
        return true;
    }

    r_mobj_t* player(fixed_t x, fixed_t y, fixed_t z = ONFLOORZ) {
        return p_spawn_mobj(&context, x, y, z, MT_PLAYER, nullptr);
    }
};

constexpr fixed_t kThrustWalk = 0x19 * 2048;

void test_move_check_position_refuses_a_one_sided_wall(void) {
    // The room's bottom wall is at y = -128. A 32-unit box centred at y = -128
    // straddles it, which is PIT_CheckLine's crossing test; a box that stays
    // above it is a legal position and only the fit rules can refuse it.
    MoveFixture fixture({{0, 128, 160, 0, 0}});
    fixture.builder.addRoom(0);
    TEST_ASSERT_TRUE(fixture.open());
    r_mobj_t* mobj = fixture.player(0, 0);
    TEST_ASSERT_NOT_NULL(mobj);

    p_position_result_t result{};
    TEST_ASSERT_EQUAL_INT(0, p_check_position(&fixture.context, mobj, 0,
                                              -128 * kFrac, &result));
    TEST_ASSERT_EQUAL_INT32(0, result.floorz);
    TEST_ASSERT_EQUAL_INT32(128 * kFrac, result.ceilingz);
    // A point that leaves the box clear of the wall is free.
    TEST_ASSERT_EQUAL_INT(1, p_check_position(&fixture.context, mobj, 0,
                                              -64 * kFrac, &result));
}

void test_move_try_move_keeps_a_blocked_thing_in_place(void) {
    MoveFixture fixture({{0, 128, 160, 0, 0}});
    fixture.builder.addRoom(0);
    TEST_ASSERT_TRUE(fixture.open());
    r_mobj_t* mobj = fixture.player(10 * kFrac, 20 * kFrac);
    TEST_ASSERT_NOT_NULL(mobj);
    p_move_stats_t stats{};
    fixture.context.moveStats = &stats;

    TEST_ASSERT_EQUAL_INT(0, p_try_move(&fixture.context, mobj, 10 * kFrac,
                                        -128 * kFrac));
    TEST_ASSERT_EQUAL_INT32(10 * kFrac, mobj->x);
    TEST_ASSERT_EQUAL_INT32(20 * kFrac, mobj->y);
    TEST_ASSERT_EQUAL_INT32(0, mobj->floorz);
    TEST_ASSERT_EQUAL_UINT32(1u, stats.blocked);
    TEST_ASSERT_EQUAL_UINT32(1u, stats.checks);

    // A move inside the room succeeds and copies the opening's heights.
    TEST_ASSERT_EQUAL_INT(1, p_try_move(&fixture.context, mobj, 10 * kFrac,
                                        20 * kFrac + 16 * kFrac));
    TEST_ASSERT_EQUAL_INT32(20 * kFrac + 16 * kFrac, mobj->y);
    TEST_ASSERT_EQUAL_UINT32(1u, stats.blocked);
    TEST_ASSERT_EQUAL_UINT32(2u, stats.checks);
}

void test_move_step_up_is_the_twenty_four_unit_rule(void) {
    // A two-sided line at y = 64: the back sector's floor is the step. 8 units
    // is walkable; 32 is not (p_map.c's `tmfloorz - thing->z > 24*FRACUNIT`).
    for (const i16 step : {static_cast<i16>(8), static_cast<i16>(32)}) {
        MoveFixture fixture({{0, 128, 160, 0, 0}, {step, 128, 160, 0, 0}});
        const u16 a = fixture.builder.addVertex(-128, 64);
        const u16 b = fixture.builder.addVertex(128, 64);
        fixture.builder.addLine(a, b, DOOM_ML_TWOSIDED, 0, 0, 0, 1);
        TEST_ASSERT_TRUE(fixture.open());
        r_mobj_t* mobj = fixture.player(0, 0);
        TEST_ASSERT_NOT_NULL(mobj);

        const int moved = p_try_move(&fixture.context, mobj, 0, 79 * kFrac);

        if (step == 8) {
            TEST_ASSERT_EQUAL_INT(1, moved);
            TEST_ASSERT_EQUAL_INT32(8 * kFrac, mobj->floorz);
        } else {
            TEST_ASSERT_EQUAL_INT(0, moved);
            TEST_ASSERT_EQUAL_INT32(0, mobj->floorz);
            TEST_ASSERT_EQUAL_INT32(0, mobj->y);
        }
    }
}

void test_move_drop_off_needs_mf_dropoff(void) {
    // A 64-unit drop over a two-sided line: refused for a thing without
    // MF_DROPOFF, accepted with it. The floor itself stays the front sector's.
    for (const bool dropoff : {false, true}) {
        MoveFixture fixture({{0, 128, 160, 0, 0}, {-64, 128, 160, 0, 0}});
        const u16 a = fixture.builder.addVertex(-128, 64);
        const u16 b = fixture.builder.addVertex(128, 64);
        fixture.builder.addLine(a, b, DOOM_ML_TWOSIDED, 0, 0, 0, 1);
        TEST_ASSERT_TRUE(fixture.open());
        r_mobj_t* mobj = fixture.player(0, 0);
        TEST_ASSERT_NOT_NULL(mobj);
        if (dropoff) {
            mobj->flags |= MF_DROPOFF;
        } else {
            mobj->flags &= ~MF_DROPOFF;
        }

        TEST_ASSERT_EQUAL_INT(dropoff ? 1 : 0,
                              p_try_move(&fixture.context, mobj, 0, 79 * kFrac));
        TEST_ASSERT_EQUAL_INT32(0, mobj->floorz);
    }
}

void test_move_thrust_and_friction_reach_the_stop_speed(void) {
    MoveFixture fixture({{0, 128, 160, 0, 0}});
    fixture.builder.addRoom(0);
    TEST_ASSERT_TRUE(fixture.open());
    r_mobj_t* mobj = fixture.player(0, 0);
    TEST_ASSERT_NOT_NULL(mobj);

    p_thrust(&fixture.context, mobj, 0, kThrustWalk);
    // The generated cosine at zero is 65535, not FRACUNIT: the table's
    // half-angle offset leaves a one-unit rounding error the rule carries
    // (documented in r_tables.h). The oracle's table has the same value.
    TEST_ASSERT_EQUAL_INT32(static_cast<i32>(fixed_mul(kThrustWalk,
                                                       r_fine_sine[FINEANGLES / 4])),
                            mobj->momx);
    // finesine[0] is 25, not 0: the table's half-angle offset gives a thrust
    // along +x a 19-unit +y component, exactly as upstream's table does.
    TEST_ASSERT_EQUAL_INT32(static_cast<i32>(fixed_mul(kThrustWalk,
                                                       r_fine_sine[0])),
                            mobj->momy);

    {
        const fixed_t before = mobj->momx;
        p_xy_movement(&fixture.context, mobj);
        TEST_ASSERT_EQUAL_INT32(static_cast<i32>(fixed_mul(before,
                                                           P_ORIG_FRICTION)),
                                mobj->momx);
    }

    // Below STOPSPEED in both axes the momentum is dropped, not scaled.
    mobj->momx = P_STOPSPEED - 1;
    mobj->momy = 0;
    p_xy_movement(&fixture.context, mobj);
    TEST_ASSERT_EQUAL_INT32(0, mobj->momx);
    TEST_ASSERT_EQUAL_INT32(0, mobj->momy);

    mobj->momx = P_STOPSPEED + 0x1000;
    p_xy_movement(&fixture.context, mobj);
    TEST_ASSERT_EQUAL_INT32(static_cast<i32>(fixed_mul(P_STOPSPEED + 0x1000,
                                                       P_ORIG_FRICTION)),
                            mobj->momx);
}

void test_move_z_movement_gravity_floor_and_ceiling(void) {
    MoveFixture fixture({{0, 128, 160, 0, 0}});
    fixture.builder.addRoom(0);
    TEST_ASSERT_TRUE(fixture.open());
    r_mobj_t* mobj = fixture.player(0, 0, 64 * kFrac);
    TEST_ASSERT_NOT_NULL(mobj);

    // Airborne with no vertical momentum: one tic sets -2*GRAVITY, the next
    // subtracts another GRAVITY (p_mobj.c P_ZMovement).
    p_z_movement(&fixture.context, mobj);
    TEST_ASSERT_EQUAL_INT32(-2 * kFrac, mobj->momz);
    TEST_ASSERT_EQUAL_INT32(64 * kFrac, mobj->z);
    p_z_movement(&fixture.context, mobj);
    TEST_ASSERT_EQUAL_INT32(62 * kFrac, mobj->z);
    TEST_ASSERT_EQUAL_INT32(-3 * kFrac, mobj->momz);

    // Landing clamps z to the floor and stops the descent.
    mobj->z = kFrac / 2;
    mobj->momz = -2 * kFrac;
    p_z_movement(&fixture.context, mobj);
    TEST_ASSERT_EQUAL_INT32(0, mobj->z);
    TEST_ASSERT_EQUAL_INT32(0, mobj->momz);

    // The ceiling clips the top of the thing, not its origin.
    mobj->z = 100 * kFrac;
    mobj->momz = 10 * kFrac;
    p_z_movement(&fixture.context, mobj);
    TEST_ASSERT_EQUAL_INT32(128 * kFrac - mobj->height, mobj->z);
    TEST_ASSERT_EQUAL_INT32(0, mobj->momz);
}

void test_move_change_sector_steps_up_and_counts_an_unsupported_crush(void) {
    SpecFixture fixture({{0, 128, 160, 0, 0}});
    fixture.builder.addLine(1, 0, 0, 0, 0, false);
    TEST_ASSERT_TRUE(fixture.open());
    r_mobj_t* mobj = p_spawn_mobj(&fixture.level.context, 0, 0, ONFLOORZ,
                                  MT_PLAYER, nullptr);
    TEST_ASSERT_NOT_NULL(mobj);
    TEST_ASSERT_EQUAL_INT(0, p_spec_p_change_sector_is_stubbed());

    // A floor rising under a standing thing lifts it.
    fixture.level.sectors[0].floorHeight = 10 * kFrac;
    TEST_ASSERT_EQUAL_INT(0, p_spec_p_change_sector(&fixture.level.context, 0));
    TEST_ASSERT_EQUAL_INT32(10 * kFrac, mobj->z);
    TEST_ASSERT_EQUAL_INT32(10 * kFrac, mobj->floorz);

    // A ceiling that leaves less room than the thing's height does not fit.
    const u32 calls = fixture.level.specStats.changeSectorCalls;
    const u32 clear = fixture.level.specStats.changeSectorClear;
    fixture.level.sectors[0].ceilingHeight = 40 * kFrac;
    TEST_ASSERT_EQUAL_INT(1, p_spec_p_change_sector(&fixture.level.context, 0));
    TEST_ASSERT_EQUAL_UINT32(calls + 1u, fixture.level.specStats.changeSectorCalls);
    TEST_ASSERT_EQUAL_UINT32(clear, fixture.level.specStats.changeSectorClear);
    TEST_ASSERT_EQUAL_UINT32(1u, fixture.level.specStats.unsupportedCrush);

    // The plane itself is never written by the change: only things move.
    TEST_ASSERT_EQUAL_INT32(40 * kFrac, fixture.level.sectors[0].ceilingHeight);
}

void test_move_use_lines_triggers_the_special_line_in_front(void) {
    // The player stands 16 units behind a two-sided special line and looks
    // along +y. P_UseLines traces USERANGE and uses the line it reaches.
    SpecFixture fixture({{0, 128, 160, 0, 0}, {0, 128, 160, 0, 0}});
    fixture.builder.addLine(DOOM_ML_TWOSIDED, 1, 0, 0, 1, true);
    TEST_ASSERT_TRUE(fixture.open());
    // x is the middle of the line, not its endpoint: the generated cosine at
    // a right angle is -25/65536, so a ray from the endpoint drifts outside the
    // line and upstream's divline test correctly reports no crossing.
    r_mobj_t* mobj = p_spawn_mobj(&fixture.level.context, 16 * kFrac,
                                  -16 * kFrac, ONFLOORZ, MT_PLAYER, nullptr);
    TEST_ASSERT_NOT_NULL(mobj);
    mobj->angle = ANG90;

    p_use_lines(&fixture.level.context, mobj);

    // Special 1 is a manual door on the line's back sector.
    TEST_ASSERT_EQUAL_INT(1, fixture.count(P_THINKER_DOOR));
}

// --- D4.2c: the player ------------------------------------------------------

/*
 * A whole level with a type-1 THING, so p_level_init runs P_SpawnPlayer and
 * wires the player record the way the game does. The room is 256 x 256 with a
 * 128-unit ceiling: enough to walk, turn and clamp the view without a WAD.
 */
struct PlayerFixture {
    MoveMapBuilder builder;
    doom_map_t map{};
    alignas(8) u8 arena[32768]{};
    p_level_t level{};

    PlayerFixture() : builder({{0, 128, 160, 0, 0}}) {
        builder.addRoom(0);
        builder.addThing(0, 0, 90, 1, 7);   // player 1, facing +y
    }

    bool open() {
        if (builder.open(&map) != DOOM_MAP_OK) {
            return false;
        }
        return p_level_init(&level, &map, arena, sizeof arena, nullptr) ==
               P_SETUP_OK;
    }

    p_player_t& player() { return level.playerState; }
    r_mobj_t* mo() { return level.player; }
};

// A room with no THING: the caller spawns and wires its own avatar.
struct SpawnFixture {
    SpecMapBuilder builder;
    doom_map_t map{};
    alignas(8) u8 arena[32768]{};
    p_level_t level{};

    SpawnFixture() : builder({{0, 128, 160, 0, 0}, {0, 128, 160, 0, 0}}) {}

    bool open() {
        if (builder.open(&map) != DOOM_MAP_OK) {
            return false;
        }
        return p_level_init(&level, &map, arena, sizeof arena, nullptr) ==
               P_SETUP_OK;
    }

    p_player_t& player() { return level.playerState; }

    int count(i16 kind) const {
        int found = 0;

        for (const p_thinker_t* thinker = p_thinker_first(&level.thinkers);
             thinker != nullptr; thinker = p_thinker_next(thinker)) {
            if (thinker->kind == kind) {
                found += 1;
            }
        }
        return found;
    }
};

void test_player_init_wires_the_avatar_and_reborn_defaults(void) {
    PlayerFixture fixture;
    TEST_ASSERT_TRUE(fixture.open());
    TEST_ASSERT_NOT_NULL(fixture.level.player);

    const p_player_t& player = fixture.level.playerState;
    TEST_ASSERT_EQUAL_PTR(fixture.level.player, player.mo);
    TEST_ASSERT_EQUAL_INT(P_PST_LIVE, player.playerstate);
    TEST_ASSERT_EQUAL_INT32(P_MAXHEALTH, player.health);
    // `p->usedown = p->attackdown = true;	// don't do anything immediately`
    TEST_ASSERT_EQUAL_INT32(1, player.usedown);
    TEST_ASSERT_EQUAL_INT32(1, player.attackdown);
    // G_PlayerReborn memsets the record: the view grows from zero on tic 0.
    TEST_ASSERT_EQUAL_INT32(0, player.viewheight);
    TEST_ASSERT_EQUAL_INT32(0, player.viewz);
    TEST_ASSERT_EQUAL_INT32(0, fixture.level.leveltime);
    TEST_ASSERT_EQUAL_INT(0, player.cmd.forwardmove);
    TEST_ASSERT_EQUAL_UINT8(0u, player.cmd.buttons);
}

void test_player_think_turns_walks_and_raises_the_view(void) {
    PlayerFixture fixture;
    TEST_ASSERT_TRUE(fixture.open());

    p_ticcmd_t cmd{};
    cmd.forwardmove = 0x19;
    cmd.angleturn = -640;               // one slow-turn step, clockwise

    const fixed_t walk = 0x19 * 2048;
    const angle_t startAngle = fixture.mo()->angle;
    p_player_think(&fixture.level, &cmd);

    // `mo->angle += (cmd->angleturn<<16)`
    const angle_t turned =
        static_cast<angle_t>(startAngle + (static_cast<angle_t>(
                                              static_cast<u32>(
                                                  static_cast<i32>(-640))
                                              << 16)));
    TEST_ASSERT_EQUAL_UINT32(turned, fixture.mo()->angle);

    // `P_Thrust (player, mo->angle, cmd->forwardmove*2048)`
    TEST_ASSERT_EQUAL_INT32(
        static_cast<i32>(fixed_mul(walk,
                                   r_fine_sine[(turned >> ANGLETOFINESHIFT) +
                                               FINEANGLES / 4])),
        fixture.mo()->momx);
    TEST_ASSERT_EQUAL_INT32(
        static_cast<i32>(fixed_mul(walk, r_fine_sine[turned >>
                                                     ANGLETOFINESHIFT])),
        fixture.mo()->momy);

    // The first P_CalcHeight grows the view from 0 to VIEWHEIGHT/2 and starts
    // the half step: `deltaviewheight = 1` then `+= FRACUNIT/4`.
    TEST_ASSERT_EQUAL_INT32(P_VIEWHEIGHT / 2, fixture.player().viewheight);
    TEST_ASSERT_EQUAL_INT32(1 + kFrac / 4,
                            fixture.player().deltaviewheight);

    // bob is bounded horizontal momentum, and viewz is the first P_CalcHeight's
    // `mo->z + viewheight + FixedMul(bob/2, finesine[0])` at leveltime 0.
    const fixed_t expectedBob =
        (fixed_mul(fixture.mo()->momx, fixture.mo()->momx) +
         fixed_mul(fixture.mo()->momy, fixture.mo()->momy)) >> 2;
    TEST_ASSERT_EQUAL_INT32(expectedBob, fixture.player().bob);
    TEST_ASSERT_EQUAL_INT32(fixture.mo()->z + fixture.player().viewheight +
                                fixed_mul(expectedBob / 2, r_fine_sine[0]),
                            fixture.player().viewz);
    TEST_ASSERT_EQUAL_INT(1, fixture.player().onground);
    TEST_ASSERT_EQUAL_UINT8(0u, fixture.player().useTriggered);
}

void test_player_think_strafes_at_minus_ninety_degrees(void) {
    PlayerFixture fixture;
    TEST_ASSERT_TRUE(fixture.open());
    fixture.mo()->angle = 0;

    p_ticcmd_t cmd{};
    cmd.sidemove = 0x19;
    p_player_think(&fixture.level, &cmd);

    // `P_Thrust (player, mo->angle-ANG90, cmd->sidemove*2048)`: positive
    // sidemove is strafe right, which at angle 0 is -y.
    const fixed_t side = 0x19 * 2048;
    const uint32_t index = ANG270 >> ANGLETOFINESHIFT;
    TEST_ASSERT_EQUAL_INT32(
        static_cast<i32>(fixed_mul(side, r_fine_sine[index + FINEANGLES / 4])),
        fixture.mo()->momx);
    TEST_ASSERT_EQUAL_INT32(static_cast<i32>(fixed_mul(
                                side, r_fine_sine[index])),
                            fixture.mo()->momy);
    TEST_ASSERT_TRUE(fixture.mo()->momy < 0);
}

void test_player_think_reactiontime_blocks_movement_and_keeps_the_view_low(void) {
    PlayerFixture fixture;
    TEST_ASSERT_TRUE(fixture.open());
    fixture.mo()->reactiontime = 2;

    p_ticcmd_t cmd{};
    cmd.forwardmove = 0x19;
    const angle_t startAngle = fixture.mo()->angle;

    // `if (player->mo->reactiontime) player->mo->reactiontime--;`
    p_player_think(&fixture.level, &cmd);
    TEST_ASSERT_EQUAL_INT16(1, fixture.mo()->reactiontime);
    TEST_ASSERT_EQUAL_INT32(0, fixture.mo()->momx);
    TEST_ASSERT_EQUAL_INT32(0, fixture.mo()->momy);
    TEST_ASSERT_EQUAL_UINT32(startAngle, fixture.mo()->angle);

    // P_MovePlayer never ran, so upstream's `onground` was never written: it
    // is still 0, and P_CalcHeight takes the airborne branch `viewz = mo->z +
    // viewheight` with viewheight still zero.
    TEST_ASSERT_EQUAL_UINT8(0u, fixture.player().onground);
    TEST_ASSERT_EQUAL_INT32(0, fixture.player().viewheight);
    TEST_ASSERT_EQUAL_INT32(fixture.mo()->z, fixture.player().viewz);

    // The next tic decrements to zero (still no movement), and the one after
    // it moves: `if (reactiontime) reactiontime--; else P_MovePlayer`.
    p_player_think(&fixture.level, &cmd);
    TEST_ASSERT_EQUAL_INT16(0, fixture.mo()->reactiontime);
    TEST_ASSERT_EQUAL_INT32(0, fixture.mo()->momx);
    TEST_ASSERT_EQUAL_INT32(0, fixture.mo()->momy);

    p_player_think(&fixture.level, &cmd);
    TEST_ASSERT_NOT_EQUAL(0, fixture.mo()->momx + fixture.mo()->momy);
    TEST_ASSERT_EQUAL_UINT8(1u, fixture.player().onground);
}

void test_player_think_use_latches_and_counts_the_trigger(void) {
    // A two-sided special line (1: manual door) in front of the avatar. The
    // spawn record sets usedown, so the very first press is ignored, exactly as
    // G_PlayerReborn's "don't do anything immediately" intends.
    SpawnFixture fixture;
    fixture.builder.addLine(DOOM_ML_TWOSIDED, 1, 0, 0, 1, true);
    TEST_ASSERT_TRUE(fixture.open());

    r_mobj_t* mobj = p_spawn_mobj(&fixture.level.context, 16 * kFrac,
                                  -16 * kFrac, ONFLOORZ, MT_PLAYER, nullptr);
    TEST_ASSERT_NOT_NULL(mobj);
    mobj->angle = ANG90;
    p_player_init(&fixture.level, mobj);

    p_ticcmd_t cmd{};
    cmd.buttons = BT_USE;
    p_player_think(&fixture.level, &cmd);
    TEST_ASSERT_EQUAL_UINT32(0u, fixture.player().useTriggers);
    TEST_ASSERT_EQUAL_UINT8(0u, fixture.player().useTriggered);
    TEST_ASSERT_EQUAL_INT(0, fixture.count(P_THINKER_DOOR));

    // Release clears the latch.
    cmd.buttons = 0;
    p_player_think(&fixture.level, &cmd);
    TEST_ASSERT_EQUAL_INT32(0, fixture.player().usedown);

    // A new press runs P_UseLines once and opens the door.
    cmd.buttons = BT_USE;
    p_player_think(&fixture.level, &cmd);
    TEST_ASSERT_EQUAL_UINT32(1u, fixture.player().useTriggers);
    TEST_ASSERT_EQUAL_UINT8(1u, fixture.player().useTriggered);
    TEST_ASSERT_EQUAL_INT(1, fixture.count(P_THINKER_DOOR));

    // Holding the button does not use twice.
    p_player_think(&fixture.level, &cmd);
    TEST_ASSERT_EQUAL_UINT32(1u, fixture.player().useTriggers);
    TEST_ASSERT_EQUAL_INT(1, fixture.count(P_THINKER_DOOR));
}

void test_player_think_counts_the_deferred_buttons_and_death(void) {
    PlayerFixture fixture;
    TEST_ASSERT_TRUE(fixture.open());

    p_ticcmd_t attack{};
    attack.buttons = BT_ATTACK;
    p_player_think(&fixture.level, &attack);
    TEST_ASSERT_EQUAL_UINT32(1u, fixture.player().unsupportedAttack);
    TEST_ASSERT_EQUAL_INT32(0, fixture.player().refire);   // D4.2d's counter

    p_ticcmd_t change{};
    change.buttons = BT_CHANGE;
    p_player_think(&fixture.level, &change);
    TEST_ASSERT_EQUAL_UINT32(1u, fixture.player().unsupportedWeaponChange);

    // `if (cmd->buttons & BT_SPECIAL) cmd->buttons = 0;` clears every bit,
    // including a fire bit sharing the word.
    p_ticcmd_t special{};
    special.buttons = static_cast<u8>(BT_SPECIAL | BT_ATTACK);
    p_player_think(&fixture.level, &special);
    TEST_ASSERT_EQUAL_UINT8(0u, fixture.player().cmd.buttons);
    TEST_ASSERT_EQUAL_UINT32(1u, fixture.player().unsupportedAttack);

    // PST_DEAD is P_DeathThink's (D4.2d): counted, not faked.
    fixture.player().playerstate = P_PST_DEAD;
    const fixed_t viewz = fixture.player().viewz;
    p_ticcmd_t walk{};
    walk.forwardmove = 0x19;
    p_player_think(&fixture.level, &walk);
    TEST_ASSERT_EQUAL_UINT32(1u, fixture.player().unsupportedDeath);
    TEST_ASSERT_EQUAL_INT(P_PST_DEAD, fixture.player().playerstate);
    TEST_ASSERT_EQUAL_INT32(0, fixture.mo()->momx);
    TEST_ASSERT_EQUAL_INT32(viewz, fixture.player().viewz);
    fixture.player().playerstate = P_PST_LIVE;
}

void test_player_calc_height_clamps_bob_and_ceiling(void) {
    PlayerFixture fixture;
    TEST_ASSERT_TRUE(fixture.open());

    // MAXMOVE in both axes: bob is clamped to MAXBOB, as
    // `if (player->bob>MAXBOB) player->bob = MAXBOB;` does.
    fixture.mo()->momx = 30 * kFrac;
    fixture.mo()->momy = 30 * kFrac;
    p_ticcmd_t idle{};
    p_player_think(&fixture.level, &idle);
    TEST_ASSERT_EQUAL_INT32(P_MAXBOB, fixture.player().bob);

    // An active delta of one unit overshoots VIEWHEIGHT and settles: the view
    // is clamped and the delta is zeroed, so the next tic does not grow it.
    fixture.player().viewheight = P_VIEWHEIGHT - kFrac / 8;
    fixture.player().deltaviewheight = kFrac;
    p_player_think(&fixture.level, &idle);
    TEST_ASSERT_EQUAL_INT32(P_VIEWHEIGHT, fixture.player().viewheight);
    TEST_ASSERT_EQUAL_INT32(0, fixture.player().deltaviewheight);

    // `if (player->viewz > mo->ceilingz-4*FRACUNIT) viewz = ...`
    fixture.mo()->z = 0;
    fixture.mo()->ceilingz = 10 * kFrac;
    fixture.player().viewheight = P_VIEWHEIGHT;
    fixture.player().deltaviewheight = 0;
    p_player_think(&fixture.level, &idle);
    TEST_ASSERT_EQUAL_INT32(10 * kFrac - 4 * kFrac, fixture.player().viewz);
}

void test_level_tic_runs_the_player_then_the_thinkers(void) {
    PlayerFixture fixture;
    TEST_ASSERT_TRUE(fixture.open());

    p_ticcmd_t cmd{};
    cmd.forwardmove = 0x19;
    p_thinker_run_t stats{};
    TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_level_tic(&fixture.level, &cmd, &stats));

    // `leveltime++` is P_Ticker's last statement.
    TEST_ASSERT_EQUAL_INT32(1, fixture.level.leveltime);
    // The player's own mobj thinker is in the walk, so the avatar moved and the
    // view was computed before the move, as upstream orders it.
    TEST_ASSERT_TRUE(stats.executed >= 1u);
    TEST_ASSERT_TRUE(fixture.mo()->y > 0);
    TEST_ASSERT_TRUE(fixture.player().viewz != 0);

    // The split is real: p_level_tick alone never runs the player think, so it
    // changes neither the view nor the clock. This is what keeps the two
    // player-less gates byte-identical.
    const fixed_t viewz = fixture.player().viewz;
    TEST_ASSERT_EQUAL_INT(P_TICK_OK, p_level_tick(&fixture.level, nullptr));
    TEST_ASSERT_EQUAL_INT32(1, fixture.level.leveltime);
    TEST_ASSERT_EQUAL_INT32(viewz, fixture.player().viewz);
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    test_setup();
    UNITY_BEGIN();

    RUN_TEST(test_spawn_onfloorz_uses_the_sector_floor);
    RUN_TEST(test_spawn_onceilingz_subtracts_the_height);
    RUN_TEST(test_spawn_explicit_z_is_not_clamped);
    RUN_TEST(test_spawn_map_thing_hanging_type_uses_onceilingz);
    RUN_TEST(test_spawn_rejects_a_bad_type_and_touches_nothing);
    RUN_TEST(test_zone_too_small_reports_failure_without_touching_the_arena);
    RUN_TEST(test_thinkers_run_in_insertion_order);
    RUN_TEST(test_thinker_removing_itself_and_a_later_one);
    RUN_TEST(test_thinker_misuse_changes_nothing);
    RUN_TEST(test_set_state_advances_state_and_resets_tics);
    RUN_TEST(test_set_state_follows_a_zero_tic_chain);
    RUN_TEST(test_set_state_to_null_removes_the_mobj);
    RUN_TEST(test_remove_twice_is_a_noop_with_an_error);
    RUN_TEST(test_rng_returns_upstreams_first_sixteen_values);
    RUN_TEST(test_rng_wraps_after_a_full_table);
    RUN_TEST(test_skill_filter_accepts_and_rejects_the_right_things);
    RUN_TEST(test_unknown_doomednum_is_refused_not_spawned);
    RUN_TEST(test_level_counts_kills_items_and_player_starts);
    RUN_TEST(test_level_medium_skill_drops_hard_only_monsters);
    RUN_TEST(test_action_dispatch_counts_what_is_missing);
    RUN_TEST(test_state_transition_dispatches_the_new_state_action);
    RUN_TEST(test_ticking_a_mobj_advances_its_state);
    RUN_TEST(test_specs_door_opens_to_its_top_and_stops);
    RUN_TEST(test_specs_door_waits_then_closes_and_removes_itself);
    RUN_TEST(test_specs_door_closing_over_a_thing_stops_at_its_height);
    RUN_TEST(test_specs_plat_goes_down_waits_and_returns);
    RUN_TEST(test_specs_raise_floor_stops_at_the_lowest_surrounding_ceiling);
    RUN_TEST(test_specs_lower_floor_stops_at_the_highest_surrounding_floor);
    RUN_TEST(test_specs_ceiling_crush_and_raise_reverses);
    RUN_TEST(test_specs_light_flash_period_uses_both_random_draws);
    RUN_TEST(test_specs_strobe_two_phase_timing);
    RUN_TEST(test_specs_perpetual_plat_stops_and_resumes_then_reverses);
    RUN_TEST(test_specs_tag_door_twice_creates_one_thinker);
    RUN_TEST(test_specs_manual_door_twice_reverses_instead_of_duplicating);
    RUN_TEST(test_specs_secret_sectors_and_unsupported_specials_are_counted);
    RUN_TEST(test_specs_spawn_order_is_sector_index_order);
    RUN_TEST(test_specs_p_change_sector_counts_and_answers_clear);
    RUN_TEST(test_line_record_exposes_special_and_tag);
    RUN_TEST(test_move_check_position_refuses_a_one_sided_wall);
    RUN_TEST(test_move_try_move_keeps_a_blocked_thing_in_place);
    RUN_TEST(test_move_step_up_is_the_twenty_four_unit_rule);
    RUN_TEST(test_move_drop_off_needs_mf_dropoff);
    RUN_TEST(test_move_thrust_and_friction_reach_the_stop_speed);
    RUN_TEST(test_move_z_movement_gravity_floor_and_ceiling);
    RUN_TEST(test_move_change_sector_steps_up_and_counts_an_unsupported_crush);
    RUN_TEST(test_move_use_lines_triggers_the_special_line_in_front);
    RUN_TEST(test_player_init_wires_the_avatar_and_reborn_defaults);
    RUN_TEST(test_player_think_turns_walks_and_raises_the_view);
    RUN_TEST(test_player_think_strafes_at_minus_ninety_degrees);
    RUN_TEST(test_player_think_reactiontime_blocks_movement_and_keeps_the_view_low);
    RUN_TEST(test_player_think_use_latches_and_counts_the_trigger);
    RUN_TEST(test_player_think_counts_the_deferred_buttons_and_death);
    RUN_TEST(test_player_calc_height_clamps_bob_and_ceiling);
    RUN_TEST(test_level_tic_runs_the_player_then_the_thinkers);

    const int result = UNITY_END();
    test_teardown();
    return result;
}
