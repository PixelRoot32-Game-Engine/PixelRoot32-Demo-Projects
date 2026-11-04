/*
 * Behaviour tests for the level transition.
 *
 * WHAT THESE CASES PROVE, AND WHY IT IS NOT "THE FUNCTION RETURNS OK"
 *
 * The failure these cases guard against is an exit switch that does nothing
 * when pressed. That is one statement about three separate things, so each has its own
 * case and each drives the *real* path:
 *
 *   1. THE SWITCH. The player is put in front of E1M1's own line special 11 with
 *      the fixture that finds it in the WAD, and BT_USE is delivered through
 *      p_level_tic() -> P_PlayerThink() -> p_use_lines() -> P_UseSpecialLine.
 *      Nothing calls the line trigger directly, so a press that the use latch
 *      swallows, or a trace that misses the line, fails this case.
 *   2. THE TRANSITION. The request is handed to levelExitNext() and
 *      levelChangeLoad() -- the same two calls the shipped driver makes -- and the
 *      resulting world is compared against a *fresh* E1M2 load: the map view
 *      identity (the very DOOM1.WAD lump pointers opened for E1M2), the sector and
 *      line counts, the player start and the mobj count. A loader that re-laid
 *      E1M1 would fail every one of those numbers.
 *   3. THE CARRY. The inventory is set to values a reborn player cannot have
 *      (37 % health, 60 armour, an odd ammo split, the shotgun and two keys) and
 *      asserted field by field after the load, together with the fields that must
 *      reset (powers, the flash counters, the per-level counters, the view). The
 *      no-carry control runs the same level change with carry=false and must lose
 *      that inventory -- without it, every carry assertion would also pass on a
 *      loader that never reset anything.
 *
 * The secret exit is E1M3's real line special 51 (the shareware maps' only route
 * to E1M9), taken through the same press. E1M8 has no exit line at all: its
 * ending is sector special 11, so that case stands on the floor, takes one
 * damage tic at 30 % health -- the number tools/level_check.c pins -- and asserts
 * g_completed_next() answers the episode's victory before the secret flag is
 * read.
 *
 * The save case closes the loop a player would hit next: a record written
 * on E1M2 must load back into E1M2. The transition produces that world through
 * the real path, saveWrite() stamps level 2 into it, and saveApply() is given the
 * E1M2 world the driver would have just loaded. Applying the same record to E1M1
 * must be refused, which is the record's own guard and stays measured.
 *
 * The world is a real map from tools/DOOM1.WAD, opened through the same
 * w_wad/doom_map/p_level_init/p_spawn_specials path the frame driver uses. The
 * WAD is copyrighted shareware data and is not committed; this suite needs it at
 * tools/DOOM1.WAD (or in DOOM_WAD). Its absence is a failure, not a skip.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <unity.h>

#include "doomc/LevelTransition.h"
#include "doomc/SaveGame.h"

extern "C" {
#include "doom_map.h"
#include "m_fixed.h"
#include "p_map.h"
#include "p_mobj.h"
#include "p_setup.h"
#include "p_spec.h"
#include "p_tick.h"
#include "p_user.h"
#include "r_bsp.h"
#include "r_tables.h"
#include "w_wad.h"
}

#include "../../test_config.h"

using doom::doomc::kSavePayloadMax;
using doom::doomc::levelChangeLoad;
using doom::doomc::levelExitNext;
using doom::doomc::PlayerCarry;
using doom::doomc::playerCarryApply;
using doom::doomc::playerCarryTake;
using doom::doomc::SaveHeader;
using doom::doomc::SaveStatus;
using doom::doomc::saveApply;
using doom::doomc::saveReadHeader;
using doom::doomc::saveWrite;

void setUp(void) {
    test_setup();
}

void tearDown() {
    test_teardown();
}

namespace {

/* --- The WAD and one world ------------------------------------------------ */

/* E1M2's 243 THINGS and E1M9's own world need more than E1M1's 96 KB; a host
 * arena is free, so this is the room level_check.c gives its chain. */
constexpr std::size_t kArenaBytes = 512u * 1024u;

struct World {
    alignas(8) std::uint8_t arena[kArenaBytes];
    doom_map_source_t source;
    doom_map_t map;
    p_level_t level;
    bool built;
};

World gA;      /* the level under test */
World gB;      /* the fresh reference load of a target map */
World gReset;  /* the run's reset level (E1M1 after a victory) */

std::vector<std::uint8_t> gWadBytes;
wad_state_t gWad;
bool gWadOpen = false;

const char* wadPath() {
    const char* env = std::getenv("DOOM_WAD");

    if (env != nullptr) {
        return env;
    }
    static const char* const candidates[] = {
        "tools/DOOM1.WAD",
        "../tools/DOOM1.WAD",
        "../../tools/DOOM1.WAD",
        "DOOM1.WAD",
    };
    for (std::size_t i = 0u; i < sizeof candidates / sizeof candidates[0]; ++i) {
        std::FILE* file = std::fopen(candidates[i], "rb");

        if (file != nullptr) {
            std::fclose(file);
            return candidates[i];
        }
    }
    return nullptr;
}

bool loadWad() {
    if (gWadOpen) {
        return true;
    }
    const char* path = wadPath();

    if (path == nullptr) {
        return false;
    }
    std::FILE* file = std::fopen(path, "rb");

    if (file == nullptr) {
        return false;
    }
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return false;
    }
    const long length = std::ftell(file);

    if (length <= 0 || std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        return false;
    }
    gWadBytes.resize(static_cast<std::size_t>(length));
    if (std::fread(gWadBytes.data(), 1u, gWadBytes.size(), file) !=
        gWadBytes.size()) {
        std::fclose(file);
        gWadBytes.clear();
        return false;
    }
    std::fclose(file);
    if (wad_open(&gWad, gWadBytes.data(), gWadBytes.size()) != WAD_ERR_OK) {
        return false;
    }
    gWadOpen = true;
    return true;
}

/* Opens @p mapName's ten lumps into @p world's own source, then the map view.
 * The source is the world's member on purpose: doom_map_t holds pointers into
 * these lumps, so the storage has to outlive the map exactly as the driver's
 * container does. */
bool openMap(World& world, const char* mapName) {
    const int marker = W_CheckNumForName(&gWad, mapName);

    if (marker < 0 ||
        marker + 1 + static_cast<int>(DOOM_MAP_LUMP_COUNT) > gWad.numLumps) {
        return false;
    }
    std::memset(&world.source, 0, sizeof world.source);
    for (int i = 0; i < static_cast<int>(DOOM_MAP_LUMP_COUNT); ++i) {
        const int lump = marker + 1 + i;

        world.source.data[i] = W_LumpPointer(&gWad, lump);
        world.source.size[i] = static_cast<std::size_t>(W_LumpLength(&gWad, lump));
    }
    std::memset(&world.map, 0, sizeof world.map);
    return doom_map_open(&world.map, &world.source) == DOOM_MAP_OK;
}

bool loadWorld(World& world, const char* mapName, p_skill_t skill) {
    if (!openMap(world, mapName)) {
        return false;
    }
    p_spawn_rules_t rules = p_spawn_rules_default();
    rules.skill = skill;
    if (p_level_init(&world.level, &world.map, world.arena, sizeof world.arena,
                     &rules) != P_SETUP_OK) {
        return false;
    }
    if (p_spawn_specials(&world.level) != P_SPEC_OK) {
        return false;
    }
    world.level.rules.skill = skill;
    world.built = true;
    return true;
}

void destroyWorld(World& world) {
    if (world.built) {
        p_level_shutdown(&world.level);
        world.built = false;
    }
}

/* --- Geometry the exit fixture needs -------------------------------------- */

/* R_PointToAngle2, through the simulation's own helper: a harness must not reach
 * into lib/doom's internals, and p_point_to_angle() is the function P_HitSlideLine
 * and P_DamageMobj already share. */
uint32_t pointToAngle(fixed_t x, fixed_t y, fixed_t tx, fixed_t ty) {
    return p_point_to_angle(tx - x, ty - y);
}

bool sectorAt(const doom_map_t* map, fixed_t x, fixed_t y, uint16_t* sector) {
    uint16_t subsector = 0;
    doom_subsector_t record;

    if (r_bsp_point_in_subsector(map, x, y, &subsector) != R_BSP_OK) {
        return false;
    }
    doom_map_get_subsector(map, subsector, &record);
    *sector = doom_map_subsector_sector(map, &record);
    return true;
}

/* --- The exit fixture ----------------------------------------------------- */

struct ExitFixture {
    uint16_t line = 0;
    int16_t special = 0;
    uint16_t front = 0;
    fixed_t x = 0;
    fixed_t y = 0;
    uint32_t angle = 0;
    fixed_t standoff = 0;
};

/*
 * The lowest line carrying @p special, with a point @p standoff units in front of
 * its midpoint on the *front* sector's side, facing it. This is
 * tools/level_check.c's fixture with the arithmetic done in DOOM's own fixed
 * point instead of doubles: the line's angle turns a quarter turn each way to
 * give the two normals, and the point that lands in the line's front sector is
 * the side a use press is accepted from (P_UseSpecialLine refuses the back side
 * of everything but 124).
 */
bool findExitLine(p_level_t* level, int16_t special, ExitFixture* out) {
    const doom_map_t* map = level->map;
    static const fixed_t kGaps[3] = {32 * FRACUNIT, 24 * FRACUNIT, 16 * FRACUNIT};
    static const int kThirds[5] = {6, 4, 8, 3, 9};

    for (uint32_t i = 0u; i < level->lineCount; ++i) {
        doom_line_t line;
        doom_vertex_t v1;
        doom_vertex_t v2;
        uint16_t front = 0;
        fixed_t dx;
        fixed_t dy;
        uint32_t lineAngle;

        if (level->lineSpecial[i] != special) {
            continue;
        }
        doom_map_get_line(map, static_cast<uint16_t>(i), &line);
        if (!p_line_front_sector(map, &line, &front)) {
            continue;
        }
        doom_map_get_vertex(map, line.v1, &v1);
        doom_map_get_vertex(map, line.v2, &v2);
        dx = v2.x - v1.x;
        dy = v2.y - v1.y;
        if (dx == 0 && dy == 0) {
            continue;
        }
        lineAngle = p_point_to_angle(dx, dy);

        for (int third = 0; third < 5; ++third) {
            const fixed_t mx =
                v1.x + static_cast<fixed_t>((static_cast<int64_t>(dx) * kThirds[third]) / 12);
            const fixed_t my =
                v1.y + static_cast<fixed_t>((static_cast<int64_t>(dy) * kThirds[third]) / 12);

            for (int gapIndex = 0; gapIndex < 3; ++gapIndex) {
                for (int sign = -1; sign <= 1; sign += 2) {
                    const uint32_t normal = sign > 0 ? (lineAngle + ANG90)
                                                     : (lineAngle + ANG270);
                    const fixed_t px =
                        mx + fixed_mul(kGaps[gapIndex], r_fine_cosine(normal));
                    const fixed_t py =
                        my + fixed_mul(kGaps[gapIndex], r_fine_sine[normal >> ANGLETOFINESHIFT]);
                    uint16_t sector = 0;
                    p_position_result_t position;

                    if (!sectorAt(map, px, py, &sector) || sector != front) {
                        continue;
                    }
                    if (!p_check_position(&level->context, level->player, px, py,
                                          &position)) {
                        continue;
                    }
                    out->line = static_cast<uint16_t>(i);
                    out->special = special;
                    out->front = front;
                    out->x = px;
                    out->y = py;
                    out->angle = pointToAngle(px, py, mx, my);
                    out->standoff = kGaps[gapIndex];
                    return true;
                }
            }
        }
    }
    return false;
}

/* Stands the avatar on the fixture's point, on the sector's own floor, facing the
 * line. Mirrors tools/level_check.c's standAt(). */
void standAt(p_level_t* level, const ExitFixture* fixture) {
    r_mobj_t* mobj = level->player;
    p_position_result_t position;

    if (mobj == nullptr) {
        return;
    }
    if (!p_check_position(&level->context, mobj, fixture->x, fixture->y, &position)) {
        return;
    }
    mobj->x = fixture->x;
    mobj->y = fixture->y;
    mobj->z = position.floorz;
    mobj->momx = mobj->momy = 0;
    mobj->floorz = position.floorz;
    mobj->ceilingz = position.ceilingz;
    mobj->subsector = position.subsector;
    mobj->sector = position.sector;
    mobj->angle = fixture->angle;
}

/* A standable point inside @p sector on a 16-unit grid over the map's bounds,
 * where the avatar's box fits and the sector's floor is the one it lands on. The
 * floor test matters: P_PlayerInSpecialSector ignores a thing whose z is not the
 * sector's floor, which is the "still falling" rule. */
bool findSectorStand(p_level_t* level, uint16_t sector, ExitFixture* out) {
    const doom_map_t* map = level->map;
    const uint32_t vertexCount = map->lumps[DOOM_MAP_VERTEXES].count;
    const fixed_t step = 16 * FRACUNIT;
    doom_vertex_t vertex;
    fixed_t minX;
    fixed_t minY;
    fixed_t maxX;
    fixed_t maxY;

    if (vertexCount == 0u || level->player == nullptr) {
        return false;
    }
    doom_map_get_vertex(map, 0u, &vertex);
    minX = maxX = vertex.x;
    minY = maxY = vertex.y;
    for (uint32_t i = 1u; i < vertexCount; ++i) {
        doom_map_get_vertex(map, static_cast<uint16_t>(i), &vertex);
        if (vertex.x < minX) { minX = vertex.x; }
        if (vertex.x > maxX) { maxX = vertex.x; }
        if (vertex.y < minY) { minY = vertex.y; }
        if (vertex.y > maxY) { maxY = vertex.y; }
    }
    for (fixed_t y = static_cast<fixed_t>(minY & ~(step - 1)); y <= maxY; y += step) {
        for (fixed_t x = static_cast<fixed_t>(minX & ~(step - 1)); x <= maxX; x += step) {
            uint16_t found = 0;
            p_position_result_t position;

            if (!sectorAt(map, x, y, &found) || found != sector) {
                continue;
            }
            if (!p_check_position(&level->context, level->player, x, y, &position) ||
                position.sector != sector ||
                position.floorz != level->sectors[sector].floorHeight) {
                continue;
            }
            out->line = 0u;
            out->special = level->sectors[sector].special;
            out->front = sector;
            out->x = x;
            out->y = y;
            out->angle = 0u;
            out->standoff = 0;
            return true;
        }
    }
    return false;
}

int sectorWithSpecial(const p_level_t* level, int16_t special) {
    int found = -1;

    for (uint32_t i = 0u; i < level->sectorCount; ++i) {
        if (level->sectors[i].special == special) {
            if (found >= 0) {
                return -2;
            }
            found = static_cast<int>(i);
        }
    }
    return found;
}

/* One tic through P_Ticker's entry point. */
bool runTic(p_level_t* level, uint8_t buttons) {
    p_ticcmd_t cmd;

    std::memset(&cmd, 0, sizeof cmd);
    cmd.buttons = buttons;
    return p_level_tic(level, &cmd, nullptr) == P_TICK_OK;
}

/*
 * Marks up to @p count live things for removal and lets the thinker walk unlink
 * them -- the deferred half of P_RemoveMobj(), which is how a level loses a
 * corpse. The player is skipped: it is the save's subject, and its pointer is the
 * record's `mo`. The pointers are collected before anything is marked, because
 * removing during the walk would invalidate the walk.
 */
void removeMobjs(p_level_t& level, int count) {
    const r_mobj_t* doomed[256];
    int found = 0;

    for (const r_mobj_t* mobj = p_level_first_mobj(&level);
         mobj != nullptr && found < count && found < 256;
         mobj = p_level_next_mobj(mobj)) {
        if (mobj->player != 0u) {
            continue;
        }
        doomed[found] = mobj;
        ++found;
    }
    for (int i = 0; i < found; ++i) {
        (void)p_remove_mobj(&level.context, const_cast<r_mobj_t*>(doomed[i]));
    }
    if (found > 0) {
        p_thinker_run_t stats;
        (void)p_level_tick(&level, &stats);
    }
}

/*
 * One use press on the fixture, through the latch the real game has.
 *
 * p_player_wire() is G_PlayerReborn's, and G_PlayerReborn sets `usedown = true`
 * ("don't do anything immediately"), so the first BT_USE tic after a load is
 * swallowed by design. The release tic below is upstream's `else usedown = false`
 * and is not a workaround: a player who never let go of the button never pressed
 * it.
 */
bool pressUse(p_level_t* level, const ExitFixture* fixture) {
    standAt(level, fixture);
    if (!runTic(level, 0u)) {
        return false;
    }
    return runTic(level, static_cast<uint8_t>(BT_USE));
}

/* --- Comparisons against a fresh reference load --------------------------- */

/*
 * The world must be a fresh load of @p world's map: the same lump pointers, the
 * same counts, the same player start, the same mobj count. `level.map` is
 * compared to the reference's map record by the lump views it holds, because the
 * driver's map is a copy and a pointer comparison would only prove the copy.
 */
bool sameMap(const p_level_t* level, const World& reference) {
    if (level->map == nullptr) {
        return false;
    }
    for (int i = 0; i < static_cast<int>(DOOM_MAP_LUMP_COUNT); ++i) {
        if (level->map->lumps[i].data != reference.map.lumps[i].data ||
            level->map->lumps[i].count != reference.map.lumps[i].count) {
            return false;
        }
    }
    return true;
}

void assertFreshLike(const p_level_t* level, const World& reference, const char* what) {
    TEST_ASSERT_NOT_NULL_MESSAGE(level->map, what);
    TEST_ASSERT_TRUE_MESSAGE(sameMap(level, reference), what);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(reference.level.sectorCount, level->sectorCount, what);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(reference.level.lineCount, level->lineCount, what);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(reference.level.stats.things, level->stats.things, what);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(reference.level.stats.spawned, level->stats.spawned, what);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(p_level_mobj_count(&reference.level),
                                     p_level_mobj_count(level), what);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, level->leveltime, what);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(P_EXIT_NONE), level->exitRequest);
    TEST_ASSERT_NOT_NULL(level->player);
    TEST_ASSERT_EQUAL_PTR(level->player, level->playerState.mo);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(static_cast<int32_t>(reference.level.playerStarts[0].x),
                                    static_cast<int32_t>(level->player->x / FRACUNIT), what);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(static_cast<int32_t>(reference.level.playerStarts[0].y),
                                    static_cast<int32_t>(level->player->y / FRACUNIT), what);
}

/* The inventory a transition must carry, written into a live player. */
void setDistinctInventory(p_player_t& player) {
    player.health = 37;
    player.mo->health = 37;
    player.armorpoints = 60;
    player.armortype = 1; /* green armour */
    player.ammo[P_AM_CLIP] = 61;
    player.ammo[P_AM_SHELL] = 17;
    player.ammo[P_AM_CELL] = 43;
    player.ammo[P_AM_MISL] = 9;
    player.maxammo[P_AM_CLIP] = 200;
    player.maxammo[P_AM_SHELL] = 50;
    player.maxammo[P_AM_CELL] = 300;
    player.maxammo[P_AM_MISL] = 50;
    player.weaponowned[P_WP_SHOTGUN] = 1u;
    player.weaponowned[P_WP_CHAINGUN] = 1u;
    player.backpack = 1u;
    player.cards[P_IT_BLUECARD] = 1u;
    player.cards[P_IT_REDSKULL] = 1u;
    player.readyweapon = P_WP_SHOTGUN;
    player.pendingweapon = P_WP_CHAINGUN;
    /* The fields a transition must *not* carry, set to values a reborn player
     * cannot hold so the reset assertions have something to lose. */
    player.powers[P_PW_INVULNERABILITY] = 12;
    player.powers[P_PW_INFRARED] = 34;
    player.damagecount = 56;
    player.bonuscount = 78;
    player.extralight = 2;
    player.fixedcolormap = 3;
    player.attacker = player.mo;
    player.killcount = 11;
    player.itemcount = 22;
    player.secretcount = 33;
    player.didsecret = 1u;
}

void assertCarried(const p_player_t& player) {
    TEST_ASSERT_EQUAL_INT32(37, player.health);
    TEST_ASSERT_EQUAL_INT32(37, player.mo->health);
    TEST_ASSERT_EQUAL_INT32(60, player.armorpoints);
    TEST_ASSERT_EQUAL_INT32(1, player.armortype);
    TEST_ASSERT_EQUAL_INT32(61, player.ammo[P_AM_CLIP]);
    TEST_ASSERT_EQUAL_INT32(17, player.ammo[P_AM_SHELL]);
    TEST_ASSERT_EQUAL_INT32(43, player.ammo[P_AM_CELL]);
    TEST_ASSERT_EQUAL_INT32(9, player.ammo[P_AM_MISL]);
    TEST_ASSERT_EQUAL_INT32(200, player.maxammo[P_AM_CLIP]);
    TEST_ASSERT_EQUAL_INT32(50, player.maxammo[P_AM_SHELL]);
    TEST_ASSERT_EQUAL_INT32(300, player.maxammo[P_AM_CELL]);
    TEST_ASSERT_EQUAL_INT32(50, player.maxammo[P_AM_MISL]);
    TEST_ASSERT_EQUAL_UINT8(1u, player.weaponowned[P_WP_FIST]);
    TEST_ASSERT_EQUAL_UINT8(1u, player.weaponowned[P_WP_PISTOL]);
    TEST_ASSERT_EQUAL_UINT8(1u, player.weaponowned[P_WP_SHOTGUN]);
    TEST_ASSERT_EQUAL_UINT8(1u, player.weaponowned[P_WP_CHAINGUN]);
    TEST_ASSERT_EQUAL_UINT8(0u, player.weaponowned[P_WP_BFG]);
    TEST_ASSERT_EQUAL_UINT8(1u, player.backpack);
    TEST_ASSERT_EQUAL_UINT8(1u, player.cards[P_IT_BLUECARD]);
    TEST_ASSERT_EQUAL_UINT8(1u, player.cards[P_IT_REDSKULL]);
    TEST_ASSERT_EQUAL_UINT8(0u, player.cards[P_IT_YELLOWCARD]);
    TEST_ASSERT_EQUAL_INT32(P_WP_SHOTGUN, player.readyweapon);
    TEST_ASSERT_EQUAL_INT32(P_WP_CHAINGUN, player.pendingweapon);
}

void assertReset(const p_player_t& player) {
    TEST_ASSERT_EQUAL_INT32(0, player.powers[P_PW_INVULNERABILITY]);
    TEST_ASSERT_EQUAL_INT32(0, player.powers[P_PW_INFRARED]);
    TEST_ASSERT_EQUAL_INT32(0, player.damagecount);
    TEST_ASSERT_EQUAL_INT32(0, player.bonuscount);
    TEST_ASSERT_EQUAL_INT32(0, player.extralight);
    TEST_ASSERT_EQUAL_INT32(0, player.fixedcolormap);
    TEST_ASSERT_NULL(player.attacker);
    TEST_ASSERT_EQUAL_INT32(0, player.killcount);
    TEST_ASSERT_EQUAL_INT32(0, player.itemcount);
    TEST_ASSERT_EQUAL_INT32(0, player.secretcount);
    TEST_ASSERT_EQUAL_UINT8(0u, player.didsecret);
    /* The view is the load's: P_CalcHeight computes it on the first tic, and a
     * carried height would put the new camera at the old one. */
    TEST_ASSERT_EQUAL_INT32(0, player.viewz);
    TEST_ASSERT_EQUAL_INT32(0, player.bob);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(P_PST_LIVE), player.playerstate);
}

/* --- Cases ---------------------------------------------------------------- */

/*
 * The first statement of the exit path: the press
 * on E1M1's own exit switch must arrive as P_EXIT_NORMAL.
 */
void test_e1m1_exit_switch_requests_the_exit(void) {
    ExitFixture fixture;

    TEST_ASSERT_TRUE(loadWad());
    TEST_ASSERT_TRUE(loadWorld(gA, "E1M1", P_SKILL_MEDIUM));
    TEST_ASSERT_TRUE_MESSAGE(findExitLine(&gA.level, 11, &fixture),
                             "E1M1 has no usable exit line (special 11)");

    standAt(&gA.level, &fixture);
    /* The map starts with no request: the assertion has something to change. */
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(P_EXIT_NONE),
                            gA.level.exitRequest);
    TEST_ASSERT_EQUAL_INT16(11, gA.level.lineSpecial[fixture.line]);

    /* A release tic first: G_PlayerReborn's `usedown = true` swallows the very
     * first BT_USE, which is upstream's rule and not this fixture's. */
    TEST_ASSERT_TRUE(runTic(&gA.level, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, gA.level.playerState.usePresses);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(P_EXIT_NONE),
                            gA.level.exitRequest);

    TEST_ASSERT_TRUE(runTic(&gA.level, static_cast<uint8_t>(BT_USE)));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, gA.level.playerState.usePresses,
                                     "the press never reached P_UseLines");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<std::uint8_t>(P_EXIT_NORMAL),
                                    gA.level.exitRequest,
                                    "the exit switch did not request the exit");
    /* P_ChangeSwitchTexture(line, 0): the one-shot clear, which is what makes a
     * second press on the same switch do nothing. */
    TEST_ASSERT_EQUAL_INT16(0, gA.level.lineSpecial[fixture.line]);

    /* A second press is still a press and still not a second exit: the line was
     * consumed, so the request cannot be re-raised by holding the button. */
    TEST_ASSERT_TRUE(runTic(&gA.level, 0u));
    TEST_ASSERT_TRUE(runTic(&gA.level, static_cast<uint8_t>(BT_USE)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(P_EXIT_NORMAL),
                            gA.level.exitRequest);

    destroyWorld(gA);
}

/*
 * The transition itself: E1M1 -> E1M2, the world compared against a fresh E1M2
 * load, and every carried and every reset field asserted.
 */
void test_e1m1_exit_loads_e1m2_with_the_carry(void) {
    ExitFixture fixture;
    g_level_state_t game;
    g_level_next_t next;

    TEST_ASSERT_TRUE(loadWad());
    TEST_ASSERT_TRUE(loadWorld(gA, "E1M1", P_SKILL_MEDIUM));
    TEST_ASSERT_TRUE(loadWorld(gB, "E1M2", P_SKILL_MEDIUM));
    TEST_ASSERT_TRUE_MESSAGE(findExitLine(&gA.level, 11, &fixture),
                             "E1M1 has no usable exit line (special 11)");

    /* Two tics of play, so the level's clock and the counters are not zero. */
    TEST_ASSERT_TRUE(runTic(&gA.level, 0u));
    TEST_ASSERT_TRUE(pressUse(&gA.level, &fixture));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(P_EXIT_NORMAL),
                            gA.level.exitRequest);
    setDistinctInventory(gA.level.playerState);
    const std::uint32_t ticsBefore = static_cast<std::uint32_t>(gA.level.leveltime);

    g_level_state_init(&game, 1u, 1u);
    TEST_ASSERT_EQUAL_INT(1, levelExitNext(gA.level, game, next));
    TEST_ASSERT_EQUAL_UINT8(2u, next.map);
    TEST_ASSERT_EQUAL_UINT8(0u, next.victory);
    TEST_ASSERT_EQUAL_UINT8(1u, next.episode);

    TEST_ASSERT_EQUAL_INT(static_cast<int>(G_LEVEL_OK),
                          static_cast<int>(levelChangeLoad(gA.level, game, next,
                                                           gB.map, true)));
    assertFreshLike(&gA.level, gB, "E1M2 is not a fresh load of E1M2");
    /* game state: G_DoWorldDone's write, before the load. */
    TEST_ASSERT_EQUAL_UINT8(1u, game.episode);
    TEST_ASSERT_EQUAL_UINT8(2u, game.map);
    /* the skill travels with the rules. */
    TEST_ASSERT_EQUAL_INT(static_cast<int>(P_SKILL_MEDIUM),
                          static_cast<int>(gA.level.rules.skill));
    assertCarried(gA.level.playerState);
    assertReset(gA.level.playerState);
    /* The carried weapon is what the first live tic puts in hand. */
    TEST_ASSERT_TRUE(gA.level.leveltime == 0);
    TEST_ASSERT_TRUE(ticsBefore > 0u);

    destroyWorld(gA);
    destroyWorld(gB);
}

/*
 * The must-fail control: the same level change with carry=false loses the
 * inventory. Without this, every assertion in the case above would also pass on
 * a loader that never reset the player at all.
 */
void test_without_carry_the_inventory_is_reborn(void) {
    ExitFixture fixture;
    g_level_state_t game;
    g_level_next_t next;

    TEST_ASSERT_TRUE(loadWad());
    TEST_ASSERT_TRUE(loadWorld(gA, "E1M1", P_SKILL_MEDIUM));
    TEST_ASSERT_TRUE(loadWorld(gB, "E1M2", P_SKILL_MEDIUM));
    TEST_ASSERT_TRUE_MESSAGE(findExitLine(&gA.level, 11, &fixture),
                             "E1M1 has no usable exit line (special 11)");
    TEST_ASSERT_TRUE(runTic(&gA.level, 0u));
    TEST_ASSERT_TRUE(pressUse(&gA.level, &fixture));
    setDistinctInventory(gA.level.playerState);

    g_level_state_init(&game, 1u, 1u);
    TEST_ASSERT_EQUAL_INT(1, levelExitNext(gA.level, game, next));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(G_LEVEL_OK),
                          static_cast<int>(levelChangeLoad(gA.level, game, next,
                                                           gB.map, false)));
    /* G_PlayerReborn's record: nothing of the run survives. */
    TEST_ASSERT_EQUAL_INT32(P_MAXHEALTH, gA.level.playerState.health);
    TEST_ASSERT_EQUAL_INT32(0, gA.level.playerState.armorpoints);
    TEST_ASSERT_EQUAL_INT32(0, gA.level.playerState.armortype);
    TEST_ASSERT_EQUAL_INT32(50, gA.level.playerState.ammo[P_AM_CLIP]);
    TEST_ASSERT_EQUAL_INT32(0, gA.level.playerState.ammo[P_AM_SHELL]);
    TEST_ASSERT_EQUAL_UINT8(0u, gA.level.playerState.weaponowned[P_WP_SHOTGUN]);
    TEST_ASSERT_EQUAL_UINT8(0u, gA.level.playerState.backpack);
    TEST_ASSERT_EQUAL_UINT8(0u, gA.level.playerState.cards[P_IT_BLUECARD]);
    TEST_ASSERT_EQUAL_INT32(P_WP_PISTOL, gA.level.playerState.readyweapon);
    /* and the avatar's own health is the reborn one, not the carried 37. */
    TEST_ASSERT_EQUAL_INT32(P_MAXHEALTH, gA.level.player->health);

    destroyWorld(gA);
    destroyWorld(gB);
}

/*
 * The secret exit: E1M3's real line special 51 is the shareware episode's only
 * route to E1M9, and G_DoCompleted answers a secret exit with map 9.
 */
void test_e1m3_secret_switch_reaches_e1m9(void) {
    ExitFixture fixture;
    g_level_state_t game;
    g_level_next_t next;

    TEST_ASSERT_TRUE(loadWad());
    TEST_ASSERT_TRUE(loadWorld(gA, "E1M3", P_SKILL_MEDIUM));
    TEST_ASSERT_TRUE(loadWorld(gB, "E1M9", P_SKILL_MEDIUM));
    TEST_ASSERT_TRUE_MESSAGE(findExitLine(&gA.level, 51, &fixture),
                             "E1M3 has no usable secret exit line (special 51)");

    TEST_ASSERT_TRUE(runTic(&gA.level, 0u));
    TEST_ASSERT_TRUE(pressUse(&gA.level, &fixture));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<std::uint8_t>(P_EXIT_SECRET),
                                    gA.level.exitRequest,
                                    "E1M3's secret switch did not request a secret exit");
    setDistinctInventory(gA.level.playerState);

    g_level_state_init(&game, 1u, 3u);
    TEST_ASSERT_EQUAL_INT(1, levelExitNext(gA.level, game, next));
    TEST_ASSERT_EQUAL_UINT8(9u, next.map);
    TEST_ASSERT_EQUAL_UINT8(1u, next.secret);
    TEST_ASSERT_EQUAL_UINT8(0u, next.victory);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(G_LEVEL_OK),
                          static_cast<int>(levelChangeLoad(gA.level, game, next,
                                                           gB.map, true)));
    assertFreshLike(&gA.level, gB, "E1M9 is not a fresh load of E1M9");
    TEST_ASSERT_EQUAL_UINT8(9u, game.map);
    /* the secret flag travels with the state, so G_DoCompleted's return table
     * (map 9 -> episode 1's map 4) will be read from it on the next exit. */
    TEST_ASSERT_EQUAL_UINT8(1u, game.secretExit);
    assertCarried(gA.level.playerState);

    destroyWorld(gA);
    destroyWorld(gB);
}

/*
 * E1M8: no exit line anywhere (asserted), its ending is sector special 11, and
 * G_DoCompleted answers the episode's victory before it reads `secretexit`. The
 * placeholder then loads the run's start again -- the same level change with
 * carry=false -- which is what the shipped driver does so the main menu has a
 * live frame to draw over.
 */
void test_e1m8_sector_11_ends_the_run(void) {
    g_level_state_t game;
    g_level_next_t next;
    ExitFixture sectorFixture;
    int sector;

    TEST_ASSERT_TRUE(loadWad());
    TEST_ASSERT_TRUE(loadWorld(gA, "E1M8", P_SKILL_MEDIUM));
    TEST_ASSERT_TRUE(loadWorld(gReset, "E1M1", P_SKILL_MEDIUM));

    /* The premise level_check established: E1M8 carries no exit line. */
    for (uint32_t i = 0u; i < gA.level.lineCount; ++i) {
        TEST_ASSERT_TRUE(gA.level.lineSpecial[i] != 11 && gA.level.lineSpecial[i] != 51 &&
                         gA.level.lineSpecial[i] != 52 && gA.level.lineSpecial[i] != 124);
    }

    sector = sectorWithSpecial(&gA.level, 11);
    TEST_ASSERT_TRUE_MESSAGE(sector >= 0, "E1M8 has no single sector 11");
    TEST_ASSERT_TRUE(findSectorStand(&gA.level, static_cast<uint16_t>(sector),
                                     &sectorFixture));
    standAt(&gA.level, &sectorFixture);
    gA.level.leveltime = 0;
    /* 30 % health: the first hit takes 20 and lands on the 10 the threshold asks
     * for, alive -- the number tools/level_check.c measures. */
    gA.level.playerState.health = 30;
    gA.level.player->health = 30;

    TEST_ASSERT_TRUE(runTic(&gA.level, 0u));
    TEST_ASSERT_EQUAL_INT32(10, gA.level.playerState.health);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<std::uint8_t>(P_EXIT_NORMAL),
                                    gA.level.exitRequest,
                                    "sector 11 did not request the exit");

    g_level_state_init(&game, 1u, 8u);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, levelExitNext(gA.level, game, next),
                                  "E1M8 did not answer with the episode's victory");
    TEST_ASSERT_EQUAL_UINT8(1u, next.victory);
    TEST_ASSERT_EQUAL_UINT8(8u, next.map);
    /* G_DoCompleted refuses a victory as a load: no map to open. */
    TEST_ASSERT_EQUAL_INT(static_cast<int>(G_LEVEL_ERR_VICTORY),
                          static_cast<int>(levelChangeLoad(gA.level, game, next,
                                                           gReset.map, false)));

    /* The placeholder's own step: the run's start, as a new run (no carry). */
    g_level_next_t resetNext;
    std::memset(&resetNext, 0, sizeof resetNext);
    resetNext.episode = 1u;
    resetNext.map = 1u;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(G_LEVEL_OK),
                          static_cast<int>(levelChangeLoad(gA.level, game, resetNext,
                                                           gReset.map, false)));
    assertFreshLike(&gA.level, gReset, "the run's reset is not a fresh E1M1");
    TEST_ASSERT_EQUAL_UINT8(1u, game.map);
    TEST_ASSERT_EQUAL_INT32(P_MAXHEALTH, gA.level.playerState.health);

    destroyWorld(gA);
    destroyWorld(gReset);
}

/*
 * The save implication: a record written on E1M2 names level 2 and loads back
 * into E1M2; the same record is refused by an E1M1 world. The record's format
 * does not change -- only the level number it carries becomes real.
 *
 * THE POPULATION IS REDUCED FIRST, AND WHY THAT IS NOT A CHEAT
 *
 * A save record costs one entry per live mobj, and E1M2's full population does
 * not fit the save-slot budget (the case after this one measures the refusal).
 * So this case saves the world the way a player reaches the exit: after combat
 * has removed most of the things. The removal is the level's own deferred path
 * (p_remove_mobj() marks, the thinker walk unlinks), not a smuggled list edit, and
 * the map -- therefore the record's fingerprint -- is still E1M2's.
 */
void test_save_in_e1m2_round_trips_into_e1m2(void) {
    ExitFixture fixture;
    g_level_state_t game;
    g_level_next_t next;
    std::uint8_t payload[kSavePayloadMax + 64u];
    std::size_t written = 0u;

    TEST_ASSERT_TRUE(loadWad());
    TEST_ASSERT_TRUE(loadWorld(gA, "E1M1", P_SKILL_HARD));
    TEST_ASSERT_TRUE(loadWorld(gB, "E1M2", P_SKILL_HARD));
    TEST_ASSERT_TRUE_MESSAGE(findExitLine(&gA.level, 11, &fixture),
                             "E1M1 has no usable exit line (special 11)");

    TEST_ASSERT_TRUE(runTic(&gA.level, 0u));
    TEST_ASSERT_TRUE(pressUse(&gA.level, &fixture));
    g_level_state_init(&game, 1u, 1u);
    TEST_ASSERT_EQUAL_INT(1, levelExitNext(gA.level, game, next));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(G_LEVEL_OK),
                          static_cast<int>(levelChangeLoad(gA.level, game, next,
                                                           gB.map, true)));
    TEST_ASSERT_EQUAL_UINT8(2u, game.map);

    /* A few tics on E1M2, then combat's removals, then the record. */
    for (int i = 0; i < 12; ++i) {
        TEST_ASSERT_TRUE(runTic(&gA.level, 0u));
    }
    removeMobjs(gA.level, 120);
    gA.level.playerState.killcount = 4;

    SaveStatus wrote = saveWrite(gA.level, gA.level.playerState, 2u, payload,
                                 sizeof payload, written);
    char measurement[128];
    std::snprintf(measurement, sizeof measurement,
                  "E1M2 mobjs=%u record=%u max=%u level=2 status=%d",
                  static_cast<unsigned>(p_level_mobj_count(&gA.level)),
                  static_cast<unsigned>(written),
                  static_cast<unsigned>(kSavePayloadMax), static_cast<int>(wrote));
    TEST_MESSAGE(measurement);
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(SaveStatus::Ok),
                                  static_cast<int>(wrote), measurement);
    TEST_ASSERT_TRUE(written <= kSavePayloadMax);

    SaveHeader header{};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::Ok),
                          static_cast<int>(saveReadHeader(payload, written, header)));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2u, header.level,
                                    "the record does not name E1M2");
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(P_SKILL_HARD),
                            header.skill);
    TEST_ASSERT_EQUAL_INT32(gA.level.leveltime, header.leveltime);
    TEST_ASSERT_EQUAL_INT32(4, header.kills);

    /* The load: the driver opens the level the header names, then applies. */
    TEST_ASSERT_TRUE(loadWorld(gReset, "E1M2", P_SKILL_HARD));
    const int32_t playerX = gA.level.player->x;
    const int32_t playerY = gA.level.player->y;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::Ok),
                          static_cast<int>(saveApply(payload, written, header.level,
                                                     gReset.level,
                                                     gReset.level.playerState)));
    TEST_ASSERT_EQUAL_INT32(playerX, gReset.level.player->x);
    TEST_ASSERT_EQUAL_INT32(playerY, gReset.level.player->y);
    TEST_ASSERT_EQUAL_INT32(gA.level.leveltime, gReset.level.leveltime);
    TEST_ASSERT_EQUAL_INT32(4, gReset.level.playerState.killcount);
    destroyWorld(gReset);

    /* The mismatch: the same bytes against an E1M1 world. The record's own level
     * field is the refusal, and the world is left untouched. */
    TEST_ASSERT_TRUE(loadWorld(gReset, "E1M1", P_SKILL_HARD));
    const int32_t beforeTics = gReset.level.leveltime;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::WrongLevel),
                          static_cast<int>(saveApply(payload, written, 1u, gReset.level,
                                                     gReset.level.playerState)));
    TEST_ASSERT_EQUAL_INT32(beforeTics, gReset.level.leveltime);

    destroyWorld(gReset);
    destroyWorld(gA);
    destroyWorld(gB);
}

/*
 * The measured limit: E1M2's *full* population does not fit the
 * save slot, so the save is refused cleanly instead of being truncated.
 *
 * Measured with the slot cap temporarily raised (the method, so the numbers can
 * be re-derived): skill 0/1 with 180 mobjs is 4,577 B, skill 2 with 200 is
 * 4,967 B, skill 3/4 with 239 is 5,776 B -- against a 3,968 B slot. The refusal
 * is what the six-slot budget and the current record format allow; shrinking
 * the record or widening a slot is a format/partition decision. This case pins the refusal so a future budget change is
 * visible rather than silent.
 */
void test_e1m2_full_population_is_refused_by_the_slot_budget(void) {
    std::uint8_t payload[kSavePayloadMax + 64u];
    std::size_t written = 0xFFFFFFFFu;
    std::memset(payload, 0xA5, sizeof payload);

    TEST_ASSERT_TRUE(loadWad());
    TEST_ASSERT_TRUE(loadWorld(gA, "E1M2", P_SKILL_MEDIUM));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::TooLarge),
                          static_cast<int>(saveWrite(gA.level, gA.level.playerState, 2u,
                                                     payload, sizeof payload, written)));
    /* No length was claimed: a refused write assigns `written` only on success,
     * which is what lets the menu say "not saved" honestly. The buffer itself is
     * the writer's scratch during the attempt, so it is not promised untouched. */
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, static_cast<std::uint32_t>(written));
    destroyWorld(gA);
}

/* A carry record with an impossible weapon pair must not index weaponinfo[]. */
void test_carry_repairs_an_impossible_weapon_pair(void) {
    PlayerCarry carry;

    carry.readyweapon = 42;
    carry.pendingweapon = -7;
    p_player_t player;
    std::memset(&player, 0, sizeof player);
    player.readyweapon = P_WP_PISTOL;
    player.pendingweapon = P_WP_PISTOL;

    playerCarryApply(carry, player);
    TEST_ASSERT_EQUAL_INT32(P_WP_PISTOL, player.readyweapon);
    TEST_ASSERT_EQUAL_INT32(P_WP_NOCHANGE, player.pendingweapon);

    /* And a taken record is the record: take/apply round-trips every carried
     * field, which is the unit-level proof the differential above rests on. */
    p_player_t source;
    std::memset(&source, 0, sizeof source);
    source.health = 3;
    source.armorpoints = 4;
    source.armortype = 2;
    source.ammo[P_AM_CELL] = 5;
    source.maxammo[P_AM_CELL] = 6;
    source.weaponowned[P_WP_PLASMA] = 1u;
    source.backpack = 1u;
    source.cards[P_IT_BLUESKULL] = 1u;
    source.readyweapon = P_WP_PLASMA;
    source.pendingweapon = P_WP_NOCHANGE;

    PlayerCarry taken;
    playerCarryTake(source, taken);
    p_player_t target;
    std::memset(&target, 0, sizeof target);
    playerCarryApply(taken, target);
    TEST_ASSERT_EQUAL_INT32(3, target.health);
    TEST_ASSERT_EQUAL_INT32(4, target.armorpoints);
    TEST_ASSERT_EQUAL_INT32(2, target.armortype);
    TEST_ASSERT_EQUAL_INT32(5, target.ammo[P_AM_CELL]);
    TEST_ASSERT_EQUAL_INT32(6, target.maxammo[P_AM_CELL]);
    TEST_ASSERT_EQUAL_UINT8(1u, target.weaponowned[P_WP_PLASMA]);
    TEST_ASSERT_EQUAL_UINT8(0u, target.weaponowned[P_WP_PISTOL]);
    TEST_ASSERT_EQUAL_UINT8(1u, target.backpack);
    TEST_ASSERT_EQUAL_UINT8(1u, target.cards[P_IT_BLUESKULL]);
    TEST_ASSERT_EQUAL_INT32(P_WP_PLASMA, target.readyweapon);
    TEST_ASSERT_EQUAL_INT32(P_WP_NOCHANGE, target.pendingweapon);
}

/* A level with no request names no level: the driver must not load anything. */
void test_no_request_names_no_level(void) {
    ExitFixture fixture;
    g_level_state_t game;
    g_level_next_t next;

    TEST_ASSERT_TRUE(loadWad());
    TEST_ASSERT_TRUE(loadWorld(gA, "E1M1", P_SKILL_MEDIUM));
    TEST_ASSERT_TRUE_MESSAGE(findExitLine(&gA.level, 11, &fixture),
                             "E1M1 has no usable exit line (special 11)");
    TEST_ASSERT_TRUE(runTic(&gA.level, 0u));
    g_level_state_init(&game, 1u, 1u);
    next.map = 7u; /* garbage a caller might pass in; it must be zeroed */

    TEST_ASSERT_EQUAL_INT(-1, levelExitNext(gA.level, game, next));
    TEST_ASSERT_EQUAL_UINT8(0u, next.map);
    TEST_ASSERT_EQUAL_UINT8(0u, next.victory);
    TEST_ASSERT_EQUAL_UINT8(1u, game.map);

    destroyWorld(gA);
}

} /* namespace */

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_e1m1_exit_switch_requests_the_exit);
    RUN_TEST(test_e1m1_exit_loads_e1m2_with_the_carry);
    RUN_TEST(test_without_carry_the_inventory_is_reborn);
    RUN_TEST(test_e1m3_secret_switch_reaches_e1m9);
    RUN_TEST(test_e1m8_sector_11_ends_the_run);
    RUN_TEST(test_save_in_e1m2_round_trips_into_e1m2);
    RUN_TEST(test_e1m2_full_population_is_refused_by_the_slot_budget);
    RUN_TEST(test_carry_repairs_an_impossible_weapon_pair);
    RUN_TEST(test_no_request_names_no_level);

    return UNITY_END();
}
