/*
 * Host harness for the sector and line specials (D4.1b).
 *
 * It loads E1M1 through lib/doom/p_setup.c, runs P_SpawnSpecials through
 * lib/doom/p_spec.c, triggers every line special the map carries at tic 0, then
 * steps a fixed number of tics and dumps the whole world every tic.
 *
 * tools/specials_check.py compiles this file, runs it next to
 * tools/doom_ref.py --specials, and compares the two outputs line for line.
 *
 * PROTOCOL (identical to doom_ref.py's --specials output; stated in both so
 * neither has to be read to compare them)
 *
 *   SPECIALS <map> tics=<N> sectors=<n> lines=<n>
 *   TRIGGER <tic> line=<i> special=<n> kind=<use|cross|shoot> result=<0|1>
 *   SECTOR <tic> <index> floor=<i32> ceil=<i32> light=<i32>
 *   THINKER <tic> <slot> kind=<ceiling|floor|plat|door|light> sector=<i32>
 *           type=<i32> dir=<i32> speed=<i32> count=<i32> a=<i32> b=<i32> c=<i32>
 *   DAMAGE map=<map> sector=<i32> special=<n> health0=<n> suit=<0|1> tics=<n>
 *   HEALTH <tic> health=<n> exit=<none|normal> secret=<n> special=<n>
 *   SUMMARY spawned=<n> secrets=<n> totalSecrets=<n> changeSector=<n>
 *           unsupportedSector=<n> unsupportedLine=<n> deferredLine=<n>
 *           lineScrolls=<n> triggers=<n> refused=<n>
 *
 * `<slot>` counts sector thinkers in thinker-list order from zero; mobjs are
 * skipped, because the THINGS run is tools/things_check.py's protocol and a
 * mobj has none of the fields this dump carries.
 *
 * THE TRIGGER SCRIPT (documented, and identical in the oracle)
 *
 * Every line whose special is nonzero is triggered once, in ascending line
 * index, at tic 0, before the first tick. The kind is chosen by
 * trigger_kind_for(): the switch/manual numbers of P_UseSpecialLine are `use`,
 * and every other special is `cross`. E1M1 therefore triggers:
 *
 *   line 151, 152, 247, 248, 324, 325, 340, 341  special 1   use   (manual doors)
 *   line 195                                    special 88  cross (lift)
 *   line 308                                    special 36  cross (turbo lower)
 *   line 330                                    special 11  use   (exit: the
 *                                                         request is recorded,
 *                                                         the line is cleared)
 *   line 352..359                               special 48  cross (scroll, deferred)
 *
 * Tic 0 is the state after the spawn scan and the triggers; tic 1 is after one
 * p_level_tick(). The run is 420 tics, long enough for a door to open, wait 150
 * and close again.
 *
 * THE SECTOR-DAMAGE SECTION (P_PlayerInSpecialSector)
 *
 * P_PlayerInSpecialSector is P_PlayerThink's per-tic question about the avatar's
 * own sector, and its damage depends on the level clock (`!(leveltime&0x1f)`),
 * the suit and the player's health. So it is measured on its own, once per
 * (map, special, suit, starting health) fixture, from a *fresh load* of the map:
 *
 *   DAMAGE map=... sector=<i> special=<s> health0=<n> suit=<0|1> tics=<n>
 *   HEALTH 0 health=<h0> exit=none secret=0 special=<s>      (before any tic)
 *   HEALTH <k> health=... exit=... secret=... special=...    (after k tics)
 *
 * `HEALTH <k>` is the state after k applications of the effect, with the clock
 * advanced one tic between them, which is P_Ticker's shape minus P_RunThinkers:
 * the fixture is about one rule, and no monster may throw a claw into it. The
 * unit tests in test/unit/test_world drive the same code through p_level_tic,
 * so the hook itself is covered there.
 *
 * The fixture set, identical in the oracle:
 *
 *   4,5,7,9,11,16   the only numbers this effect can see after P_SpawnSpecials,
 *                   ascending, one sector each: the lowest sector index whose
 *                   post-spawn special is that number;
 *   every case     suit=0, health0=100, tics=96   (four hit windows)
 *   4 and 16       suit=1, health0=100, tics=96   (the roll is still drawn: 96
 *                   draws, damage only when `P_Random()<5`)
 *   5 and 7        suit=1, health0=100, tics=64   (no draw, no damage)
 *   11             suit=0, health0=30,  tics=1    (10 <= the threshold: the tic
 *                   damages to 10 and G_ExitLevel is asked for)
 *                  suit=1, health0=45,  tics=33   (the suit does not shield 11;
 *                   the tic at leveltime 32 lands 5 and exits)
 *                  suit=0, health0=100, tics=129  (the whole run: 80, 60, 40,
 *                   20, then P_DamageMobj's "end of game hell" clamps the fifth
 *                   hit to 19 -> health 1, and the player is alive at the exit)
 *
 * The player is placed inside the fixture sector by scanning the map's bounds on
 * a 16-unit grid for the first point whose subsector's sector is the target and
 * where P_CheckPosition accepts the avatar. The point is not part of the
 * protocol: the oracle re-derives the sector, and the rule needs nothing else.
 *
 * Lines starting with `INFO ` are C-only measurements and are excluded from the
 * comparison.
 *
 * Build: see tools/specials_check.py. This is a desktop tool: malloc, stdio and
 * the WAD come from the host, none of which the firmware uses.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p_setup.h"
#include "p_ceilng.h"
#include "p_doors.h"
#include "p_floor.h"
#include "p_lights.h"
#include "p_map.h"
#include "p_mobj.h"
#include "p_plats.h"
#include "p_spec.h"
#include "p_user.h"
#include "r_bsp.h"
#include "w_wad.h"

#define SPECIALS_ARENA_BYTES (256u * 1024u)
#define DEFAULT_TICS 420

static uint8_t* readFile(const char* path, size_t* size) {
    FILE* handle = fopen(path, "rb");
    uint8_t* data;
    long length;

    if (handle == NULL) {
        return NULL;
    }
    if (fseek(handle, 0, SEEK_END) != 0) {
        fclose(handle);
        return NULL;
    }
    length = ftell(handle);
    if (length < 0) {
        fclose(handle);
        return NULL;
    }
    if (fseek(handle, 0, SEEK_SET) != 0) {
        fclose(handle);
        return NULL;
    }
    data = (uint8_t*)malloc((size_t)length);
    if (data == NULL) {
        fclose(handle);
        return NULL;
    }
    if (fread(data, 1, (size_t)length, handle) != (size_t)length) {
        free(data);
        fclose(handle);
        return NULL;
    }
    fclose(handle);
    *size = (size_t)length;
    return data;
}

/* The scripted kind. P_UseSpecialLine owns the switch/manual numbers; every
 * other special line in this gate is walked over. */
static const char* trigger_kind_for(int16_t special) {
    switch (special) {
        case 1: case 9: case 11: case 14: case 15: case 18: case 20: case 21:
        case 23: case 26: case 27: case 28: case 29: case 31: case 32:
        case 33: case 34: case 41: case 42: case 43: case 45: case 49:
        case 50: case 51: case 55: case 60: case 61: case 62: case 63:
        case 64: case 65: case 66: case 67: case 68: case 69: case 70:
        case 71: case 99: case 101: case 102: case 103: case 111: case 112:
        case 113: case 114: case 115: case 116: case 117: case 118: case 122:
        case 123: case 127: case 131: case 132: case 133: case 134: case 135:
        case 136: case 137: case 138: case 139: case 140:
            return "use";
        default:
            return "cross";
    }
}

static p_trigger_kind_t trigger_kind_value(const char* name) {
    if (strcmp(name, "use") == 0) {
        return P_TRIGGER_USE;
    }
    return P_TRIGGER_CROSS;
}

static const char* sector_thinker_kind(int16_t kind) {
    switch (kind) {
        case P_THINKER_CEILING: return "ceiling";
        case P_THINKER_FLOOR: return "floor";
        case P_THINKER_PLAT: return "plat";
        case P_THINKER_DOOR: return "door";
        case P_THINKER_LIGHTFLASH: return "light";
        default: return "mobj";
    }
}

/* The normalized viewer's fields: type/dir/speed/count plus three kind-specific
 * numbers, documented in the header and mirrored by the oracle. */
static void printThinker(uint32_t tic, uint32_t slot, const p_thinker_t* thinker) {
    int32_t type = 0;
    int32_t dir = 0;
    int32_t speed = 0;
    int32_t count = 0;
    int32_t a = 0;
    int32_t b = 0;
    int32_t c = 0;
    int32_t sector = -1;

    switch (thinker->kind) {
        case P_THINKER_DOOR: {
            const p_door_thinker_t* door = (const p_door_thinker_t*)thinker;

            sector = door->sector;
            type = door->type;
            dir = door->direction;
            speed = door->speed;
            count = door->topCountdown;
            a = door->topHeight;
            b = door->topWait;
            break;
        }
        case P_THINKER_FLOOR: {
            const p_floor_thinker_t* floor = (const p_floor_thinker_t*)thinker;

            sector = floor->sector;
            type = floor->type;
            dir = floor->direction;
            speed = floor->speed;
            a = floor->destHeight;
            b = floor->crush;
            c = floor->newspecial;
            break;
        }
        case P_THINKER_PLAT: {
            const p_plat_thinker_t* plat = (const p_plat_thinker_t*)thinker;

            sector = plat->sector;
            type = plat->type;
            dir = plat->status;
            speed = plat->speed;
            count = plat->count;
            a = plat->high;
            b = plat->low;
            c = plat->wait;
            break;
        }
        case P_THINKER_CEILING: {
            const p_ceiling_thinker_t* ceiling = (const p_ceiling_thinker_t*)thinker;

            sector = ceiling->sector;
            type = ceiling->type;
            dir = ceiling->direction;
            speed = ceiling->speed;
            a = ceiling->topHeight;
            b = ceiling->bottomHeight;
            c = ceiling->tag;
            break;
        }
        case P_THINKER_LIGHTFLASH: {
            const p_light_thinker_t* light = (const p_light_thinker_t*)thinker;

            sector = light->sector;
            type = light->type;
            dir = light->direction;
            count = light->count;
            a = light->maxLight;
            b = light->minLight;
            c = light->type == P_LIGHT_STROBE ? light->darkTime : light->maxTime;
            break;
        }
        default:
            return;
    }

    printf("THINKER %u %u kind=%s sector=%d type=%d dir=%d speed=%d count=%d "
           "a=%d b=%d c=%d\n",
           (unsigned)tic, (unsigned)slot, sector_thinker_kind(thinker->kind),
           (int)sector, (int)type, (int)dir, (int)speed, (int)count, (int)a,
           (int)b, (int)c);
}

static void dumpTic(const p_level_t* level, uint32_t tic) {
    const p_thinker_t* thinker;
    uint16_t i;
    uint32_t slot = 0u;

    for (i = 0; i < level->sectorCount; ++i) {
        printf("SECTOR %u %u floor=%ld ceil=%ld light=%d\n", (unsigned)tic,
               (unsigned)i, (long)level->sectors[i].floorHeight,
               (long)level->sectors[i].ceilingHeight,
               (int)level->sectors[i].lightLevel);
    }
    for (thinker = p_thinker_first(&level->thinkers); thinker != NULL;
         thinker = p_thinker_next(thinker)) {
        if (thinker->kind == (int16_t)P_THINKER_MOBJ) {
            continue;
        }
        printThinker(tic, slot, thinker);
        slot += 1u;
    }
}

/* --- The sector-damage section ------------------------------------------- */

/*
 * The six numbers P_PlayerInSpecialSector switches on. Every other sector
 * special a map carries is cleared by its spawn effect before a player can
 * stand in it, so this list is the whole set and the gate asserts that the
 * spawn scan found nothing outside it (SUMMARY unsupportedSector=0).
 */
static const int16_t kPlayerSpecials[] = {4, 5, 7, 9, 11, 16};

typedef struct damage_scenario {
    int16_t special;
    uint16_t sector;
    int32_t health;
    int suit;
    int tics;
    int synthetic; /* 1 when the number was written into a real sector */
} damage_scenario_t;

/* The scenario count is bounded: 6 specials, at most four fixtures each. */
#define MAX_SCENARIOS 24

/* A point inside @p target, on a 16-unit grid over the map's bounds, that
 * P_CheckPosition accepts for the avatar. Row-major and deterministic. */
static int findStandingPoint(p_level_t* level, uint16_t target, fixed_t* outX,
                             fixed_t* outY) {
    const doom_map_t* map = level->map;
    const uint32_t vertexCount = map->lumps[DOOM_MAP_VERTEXES].count;
    const fixed_t step = 16 * FRACUNIT;
    doom_vertex_t vertex;
    p_position_result_t position;
    fixed_t minX;
    fixed_t minY;
    fixed_t maxX;
    fixed_t maxY;
    fixed_t x;
    fixed_t y;
    uint32_t i;

    if (vertexCount == 0u || level->player == NULL) {
        return 0;
    }
    doom_map_get_vertex(map, 0u, &vertex);
    minX = maxX = vertex.x;
    minY = maxY = vertex.y;
    for (i = 1u; i < vertexCount; ++i) {
        doom_map_get_vertex(map, (uint16_t)i, &vertex);
        if (vertex.x < minX) { minX = vertex.x; }
        if (vertex.x > maxX) { maxX = vertex.x; }
        if (vertex.y < minY) { minY = vertex.y; }
        if (vertex.y > maxY) { maxY = vertex.y; }
    }

    /* The grid is anchored on the bounds' floor multiples of 16 so a second
     * run of the same map walks the same points. */
    for (y = (fixed_t)(minY & ~(step - 1)); y <= maxY; y += step) {
        for (x = (fixed_t)(minX & ~(step - 1)); x <= maxX; x += step) {
            uint16_t subsector = 0;
            doom_subsector_t record;

            if (r_bsp_point_in_subsector(map, x, y, &subsector) != R_BSP_OK) {
                continue;
            }
            doom_map_get_subsector(map, subsector, &record);
            if (doom_map_subsector_sector(map, &record) != target) {
                continue;
            }
            if (!p_check_position(&level->context, level->player, x, y,
                                  &position) ||
                position.sector != target ||
                position.floorz != level->sectors[target].floorHeight) {
                continue;
            }
            *outX = x;
            *outY = y;
            return 1;
        }
    }
    return 0;
}

/* Stands the avatar in @p sector and writes @p health into both records.
 *
 * `z` is the *sector's own* floor, not P_CheckPosition's `floorz`: the latter is
 * raised by a line's opening when the avatar's box reaches a step, and a thing
 * 24 units above the floor is a thing still falling -- which
 * P_PlayerInSpecialSector must, and does, ignore.
 */
static int placePlayer(p_level_t* level, uint16_t sector, int32_t health,
                       int suit, fixed_t* outX, fixed_t* outY) {
    r_mobj_t* mobj = level->player;
    p_position_result_t position;
    fixed_t x = 0;
    fixed_t y = 0;

    if (mobj == NULL) {
        return 0;
    }
    if (!findStandingPoint(level, sector, &x, &y)) {
        return 0;
    }
    if (!p_check_position(&level->context, mobj, x, y, &position)) {
        return 0;
    }
    mobj->x = x;
    mobj->y = y;
    mobj->z = level->sectors[sector].floorHeight;
    mobj->momx = 0;
    mobj->momy = 0;
    mobj->momz = 0;
    mobj->floorz = position.floorz;
    mobj->ceilingz = position.ceilingz;
    mobj->subsector = position.subsector;
    mobj->sector = position.sector;
    mobj->health = health;
    level->playerState.health = health;
    level->playerState.powers[P_PW_IRONFEET] = suit ? 35 * 60 : 0;
    *outX = x;
    *outY = y;
    return 1;
}

static const char* exit_name(uint8_t request) {
    if (request == (uint8_t)P_EXIT_NORMAL) {
        return "normal";
    }
    if (request == (uint8_t)P_EXIT_SECRET) {
        return "secret";
    }
    return "none";
}

static void printHealth(const p_level_t* level, uint32_t tic, uint16_t sector) {
    printf("HEALTH %u health=%ld exit=%s secret=%ld special=%d\n", (unsigned)tic,
           (long)level->playerState.health, exit_name(level->exitRequest),
           (long)level->playerState.secretcount,
           (int)level->sectors[sector].special);
}

/* One fixture: fresh load, stand the avatar in @p sector, run @p tics effects. */
static int runDamageScenario(const doom_map_t* map, p_level_t* level, void* arena,
                             const p_spawn_rules_t* rules, const char* mapName,
                             const damage_scenario_t* scenario) {
    uint32_t tic;

    if (p_level_init(level, map, arena, SPECIALS_ARENA_BYTES, rules) !=
        P_SETUP_OK) {
        return 0;
    }
    if (p_spawn_specials(level) != P_SPEC_OK || level->player == NULL) {
        return 0;
    }
    if (scenario->synthetic) {
        /* No episode-1 map carries sector 4 or 5, so the gate writes the number
         * into a sector the map really has (the one its own 7 uses) instead of
         * leaving two implemented branches unmeasured. Both sides do it. */
        level->sectors[scenario->sector].special = scenario->special;
    }
    if (level->sectors[scenario->sector].special != scenario->special) {
        return 0;
    }
    {
        fixed_t x = 0;
        fixed_t y = 0;

        if (!placePlayer(level, scenario->sector, scenario->health,
                         scenario->suit, &x, &y)) {
            return 0;
        }
    }
    printf("DAMAGE map=%s sector=%u special=%d health0=%ld suit=%d tics=%d "
           "synthetic=%d\n",
           mapName, (unsigned)scenario->sector, (int)scenario->special,
           (long)scenario->health, scenario->suit, scenario->tics,
           scenario->synthetic);
    printHealth(level, 0u, scenario->sector);
    for (tic = 1u; tic <= (uint32_t)scenario->tics; ++tic) {
        p_player_in_special_sector(level, &level->playerState);
        level->leveltime += 1;
        printHealth(level, tic, scenario->sector);
    }
    return 1;
}

/*
 * The fixture set, derived from the map's own post-spawn sectors: the lowest
 * sector index carrying each of the six numbers, ascending, and then the
 * (suit, health, tics) list documented in the header.
 *
 * No episode-1 map carries sector 4 or 5, so those two fixtures borrow the
 * sector the map's own 7 uses and write the number into it (`synthetic=1`).
 * Without that, two of upstream's six branches would be unreachable from the
 * gate; with it, the report says which runs used a real sector and which did
 * not.
 */
static int sectorCarrying(const p_level_t* level, int16_t special) {
    uint32_t i;

    for (i = 0u; i < level->sectorCount; ++i) {
        if (level->sectors[i].special == special) {
            return (int)i;
        }
    }
    return -1;
}

static void addScenario(damage_scenario_t* out, int* count, int16_t special,
                        int sector, int synthetic, int32_t health, int suit,
                        int tics) {
    out[*count].special = special;
    out[*count].sector = (uint16_t)sector;
    out[*count].health = health;
    out[*count].suit = suit;
    out[*count].tics = tics;
    out[*count].synthetic = synthetic;
    *count += 1;
}

static int buildScenarios(const p_level_t* level, damage_scenario_t* out) {
    int count = 0;
    uint32_t s;

    for (s = 0; s < (uint32_t)(sizeof kPlayerSpecials / sizeof kPlayerSpecials[0]);
         ++s) {
        const int16_t special = kPlayerSpecials[s];
        int sector = sectorCarrying(level, special);
        int synthetic = 0;

        if (sector < 0 && (special == 4 || special == 5)) {
            sector = sectorCarrying(level, 7);
            synthetic = 1;
        }
        if (sector < 0) {
            continue;
        }
        addScenario(out, &count, special, sector, synthetic, 100, 0, 96);

        if (special == 4 || special == 16) {
            addScenario(out, &count, special, sector, synthetic, 100, 1, 96);
        }
        if (special == 5 || special == 7) {
            addScenario(out, &count, special, sector, synthetic, 100, 1, 64);
        }
        if (special == 11) {
            addScenario(out, &count, special, sector, synthetic, 30, 0, 1);
            addScenario(out, &count, special, sector, synthetic, 45, 1, 33);
            addScenario(out, &count, special, sector, synthetic, 100, 0, 129);
        }
    }
    return count;
}

int main(int argc, char** argv) {
    wad_state_t wad;
    doom_map_source_t source;
    doom_map_t map;
    p_level_t level;
    p_spawn_rules_t rules;
    uint8_t* bytes;
    uint8_t* arena;
    size_t size = 0;
    const char* mapName = "E1M1";
    int tics = DEFAULT_TICS;
    int marker;
    int index;
    uint32_t tic;
    uint16_t line;
    const char* mode = "full";
    damage_scenario_t scenarios[MAX_SCENARIOS];
    int scenarioCount = 0;

    if (argc < 2 || argc > 5) {
        fprintf(stderr,
                "usage: specials_check <path-to-WAD> [map] [tics] [full|damage]\n");
        return 2;
    }
    if (argc >= 3) {
        mapName = argv[2];
    }
    if (argc >= 4) {
        tics = atoi(argv[3]);
    }
    if (argc >= 5) {
        mode = argv[4];
    }
    if (tics < 0) {
        fprintf(stderr, "specials_check: tics cannot be negative\n");
        return 2;
    }
    if (strcmp(mode, "full") != 0 && strcmp(mode, "damage") != 0) {
        fprintf(stderr, "specials_check: mode is 'full' or 'damage'\n");
        return 2;
    }

    bytes = readFile(argv[1], &size);
    if (bytes == NULL) {
        fprintf(stderr, "specials_check: cannot read %s\n", argv[1]);
        return 2;
    }
    if (wad_open(&wad, bytes, size) != WAD_ERR_OK) {
        fprintf(stderr, "specials_check: not a WAD\n");
        free(bytes);
        return 2;
    }
    marker = W_CheckNumForName(&wad, mapName);
    if (marker < 0 || marker + 1 + DOOM_MAP_LUMP_COUNT > wad.numLumps) {
        fprintf(stderr, "specials_check: %s is not in this WAD\n", mapName);
        free(bytes);
        return 2;
    }
    for (index = 0; index < DOOM_MAP_LUMP_COUNT; ++index) {
        const int lump = marker + 1 + index;

        source.data[index] = W_LumpPointer(&wad, lump);
        source.size[index] = (size_t)W_LumpLength(&wad, lump);
    }
    if (doom_map_open(&map, &source) != DOOM_MAP_OK) {
        fprintf(stderr, "specials_check: map rejected with doom_map error %d\n",
                (int)map.error);
        free(bytes);
        return 2;
    }

    arena = (uint8_t*)malloc(SPECIALS_ARENA_BYTES);
    if (arena == NULL) {
        fprintf(stderr, "specials_check: no arena\n");
        free(bytes);
        return 2;
    }

    rules = p_spawn_rules_default();
    if (p_level_init(&level, &map, arena, SPECIALS_ARENA_BYTES, &rules) != P_SETUP_OK) {
        fprintf(stderr, "specials_check: level load failed with code %d\n",
                (int)level.error);
        free(arena);
        free(bytes);
        return 2;
    }
    if (p_spawn_specials(&level) != P_SPEC_OK) {
        fprintf(stderr, "specials_check: P_SpawnSpecials failed\n");
        p_level_shutdown(&level);
        free(arena);
        free(bytes);
        return 2;
    }

    /* The sector-damage fixtures come from the pristine post-spawn state: the
     * 420-tic run below moves floors and rewrites specials. */
    scenarioCount = buildScenarios(&level, scenarios);

    if (strcmp(mode, "damage") == 0) {
        int damageIndex;
        int damageFailed = 0;

        for (damageIndex = 0; damageIndex < scenarioCount; ++damageIndex) {
            if (!runDamageScenario(&map, &level, arena, &rules, mapName,
                                   &scenarios[damageIndex])) {
                fprintf(stderr,
                        "specials_check: the sector-%d fixture did not load on %s\n",
                        (int)scenarios[damageIndex].special, mapName);
                damageFailed = 1;
                break;
            }
        }
        p_level_shutdown(&level);
        free(arena);
        free(bytes);
        return damageFailed ? 2 : 0;
    }

    printf("SPECIALS %s tics=%d sectors=%u lines=%u\n", mapName, tics,
           (unsigned)level.sectorCount, (unsigned)level.lineCount);

    /* Tic 0: every special line, ascending. */
    for (line = 0; line < level.lineCount; ++line) {
        const int16_t special = p_line_special(&map, line);
        const char* kindName;
        int result;

        if (special == 0) {
            continue;
        }
        kindName = trigger_kind_for(special);
        result = p_trigger_special_line(&level, line, trigger_kind_value(kindName), 1);
        printf("TRIGGER 0 line=%u special=%d kind=%s result=%d\n", (unsigned)line,
               (int)special, kindName, result);
    }

    dumpTic(&level, 0u);
    for (tic = 1; tic <= (uint32_t)tics; ++tic) {
        (void)p_level_tick(&level, NULL);
        dumpTic(&level, tic);
    }

    printf("SUMMARY spawned=%u secrets=%u totalSecrets=%ld changeSector=%u "
           "unsupportedSector=%u unsupportedLine=%u deferredLine=%u "
           "lineScrolls=%u triggers=%u refused=%u\n",
           level.specStats.spawned, level.specStats.secrets,
           (long)level.totalSecrets, level.specStats.changeSectorCalls,
           level.specStats.unsupportedSectorSpecial,
           level.specStats.unsupportedLineSpecial,
           level.specStats.deferredLineSpecial, level.specStats.lineScrolls,
           level.specStats.triggers, level.specStats.triggersRefused);

    /* --- The sector-damage section: P_PlayerInSpecialSector ---------------- */
    {
        if (scenarioCount == 0) {
            /* A map with none of the six numbers is legal (E1M1 has 7 and 9,
             * so this is the fixture's own sanity check, not a map's). */
            printf("DAMAGE map=%s count=0\n", mapName);
        }
        for (index = 0; index < scenarioCount; ++index) {
            if (!runDamageScenario(&map, &level, arena, &rules, mapName,
                                   &scenarios[index])) {
                fprintf(stderr,
                        "specials_check: the sector-%d fixture did not load on %s\n",
                        (int)scenarios[index].special, mapName);
                p_level_shutdown(&level);
                free(arena);
                free(bytes);
                return 2;
            }
        }
    }

    printf("INFO sizes sector=%u door=%u floor=%u plat=%u ceiling=%u light=%u "
           "level=%u specStats=%u\n",
           (unsigned)sizeof(p_sector_t), (unsigned)sizeof(p_door_thinker_t),
           (unsigned)sizeof(p_floor_thinker_t), (unsigned)sizeof(p_plat_thinker_t),
           (unsigned)sizeof(p_ceiling_thinker_t), (unsigned)sizeof(p_light_thinker_t),
           (unsigned)sizeof(p_level_t), (unsigned)sizeof(p_spec_stats_t));

    p_level_shutdown(&level);
    free(arena);
    free(bytes);
    return 0;
}
