/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * Level transitions: the exit specials, the next-map table and the load of the
 * next level, measured on the shareware container and checked against the WAD.
 *
 * WHAT THIS PROVES
 *
 * D4.5's acceptance is "a scripted run reaches E1M9". This harness opens the
 * shareware container (the same `PWHD` image the board reads) and loads a level
 * through lib/doom, stands the player in front of that level's own exit line,
 * presses USE through p_level_tic() -- the same entry point the panel's frame
 * loop calls -- and follows the transition the game state machine computes:
 *
 *   1. THE LINE TRIGGER. Special 11 (S1 exit switch) and 51 (S1 secret exit)
 *      arrive through P_UseLines -> P_UseSpecialLine; 52 and 124 (W1 walk-over)
 *      arrive through P_CrossSpecialLine. The use numbers clear their line
 *      (P_ChangeSwitchTexture(line, 0) does that upstream); the walk-over numbers
 *      do not (upstream's cross switch does not clear them). The harness asserts
 *      both facts, for both kinds, including the walk-over fixture that no
 *      shareware map carries: E1's nine maps have only 11 and 51 lines.
 *
 *   2. THE NEXT MAP. G_DoCompleted's non-commercial decision is
 *      g_completed_next(): map 8 is the episode's victory (before `secretexit`
 *      is consulted), a secret exit names map 9, map 9 returns to its episode's
 *      map (E1 -> 4), and every other map names the next one.
 *
 *   3. THE LOAD. g_do_world_done() is G_DoWorldDone + G_DoLoadLevel: the new
 *      map over the same arena, that map's THINGS run, the player respawned at
 *      that map's own type-1 record, the specials rebuilt, leveltime 0. The
 *      harness compares every one of those against a fresh reference load of the
 *      target map, and the Python side reads the WAD independently: the map the
 *      world holds is the map the name says.
 *
 *   4. THE ARENA. A transition re-lays the arena; it does not grow it. Each
 *      transition's `inUse` must equal a fresh load of the same map over the same
 *      size, and the gate reports all nine level changes.
 *
 * THE CHAIN, AND WHY IT IS NOT NINE NORMAL TRANSITIONS
 *
 * Upstream's E1M8 has no exit line: its exit is sector special 11, the "EXIT
 * SUPER DAMAGE" floor that P_PlayerInSpecialSector reads, and G_DoCompleted also
 * answers gamemap 8 with ga_victory before looking at `secretexit`, so no
 * E1M8 -> E1M9 transition exists in linuxdoom-1.10. The chain therefore visits
 * all nine maps in eight transitions:
 *
 *   E1M1 -11-> E1M2 -11-> E1M3 -51-> E1M9 -11-> E1M4 -11-> E1M5
 *        -11-> E1M6 -11-> E1M7 -11-> E1M8 (victory)
 *
 * and the ninth level change is the separate secret exit E1M1 -> E1M9, asserted
 * through g_secret_exit_level() because E1M1 carries no 51/124 line. The count
 * the acceptance asks for (nine level changes) is at the bottom of this file;
 * the *literal* "E1M1 -> E1M2 -> ... -> E1M9 by normal exits" is not upstream's
 * world, and the gate says so rather than inventing it.
 *
 * THE FINALE: E1M8's own ending, which is sector 11
 *
 * The chain stops on E1M8 because the map has no line to press, not because the
 * map has no ending. P_PlayerInSpecialSector's case 11 is that ending: the floor
 * takes 20 points on tics 0, 32, ..., it cannot take the last point
 * (P_DamageMobj's "end of game hell" clamp turns `damage >= health` into
 * `health - 1`), and `if (player->health <= 10) G_ExitLevel();` follows the
 * damage. The gate stands the player on the WAD's one sector-11 floor with 30
 * health, runs one tic through p_level_tic(), and asserts 30 -> 10, the normal
 * exit request, a live player, G_DoCompleted's victory, and that the victory
 * loads nothing: g_do_world_done refuses it with G_LEVEL_ERR_VICTORY and the
 * world is still E1M8's, over the arena a fresh E1M8 load lays out.
 *
 * OUTPUT
 *
 *   LEVEL map=E1M1 things=... spawned=... sectors=... lines=... playerstart=x,y
 *         arena=...            one per map: a fresh reference load
 *   TRANSITION n=1 from=E1M1 to=E1M2 exit=use:11 line=330 standoff=32
 *         things=... spawned=... mobjs=... sectors=... lines=...
 *         playerstart=x,y worldstart=x,y alive=1 leveltime=0 arena=... ref=...
 *         consumed=1 ok=1
 *   CROSS special=52 ...      the synthetic walk-over fixture and its guard
 *   FINALE map=E1M8 exitlines=0 sector=<i> special=11 health=<n> before=<n>
 *         damage=<n> exit=normal alive=1 victory=1 next=0 world=E1M8
 *         leveltime=1 arena=... ref=...
 *   SECRET from=E1M1 to=E1M9 ...   the ninth level change
 *   SUMMARY transitions=9 ... ok=1
 *
 * EXIT CODES
 *
 *   0  every assertion held
 *   1  an assertion failed (the line and the reason are printed)
 *   2  the input, the container or a map was unusable
 *
 * This is a desktop tool: malloc, stdio and the container bytes come from the
 * host. The container is the same image the board maps, so a map view that is
 * not raw here is a map view the board cannot bind.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doom_map.h"
#include "g_game.h"
#include "m_fixed.h"
#include "p_map.h"
#include "p_mobj.h"
#include "p_setup.h"
#include "p_spec.h"
#include "p_user.h"
#include "r_bsp.h"
#include "r_tables.h"
#include "whd.h"

#define LEVEL_COUNT 9
#define ARENA_BYTES (1024u * 1024u)
#define STANDOFFS 3

/* --- Input --------------------------------------------------------------- */

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
    if (length < 0 || fseek(handle, 0, SEEK_SET) != 0) {
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

static void levelName(int map, char out[8]) {
    g_level_name(1u, (uint8_t)map, out);
}

/*
 * The map marker plus its ten lumps, viewable inside the container exactly as
 * the board reads them: FrameDriver's flash build binds every map lump with a
 * raw view and refuses the level when a lump is not raw. The host gate uses the
 * same rule, so "the next level is opened from the container's map view" is the
 * thing measured, not a WAD shortcut.
 */
static int openMap(const whd_state_t* container, int map, doom_map_t* out,
                   doom_map_source_t* source) {
    char name[8];
    int marker;
    int index;

    levelName(map, name);
    marker = whd_lump_index_for_name(container, name);
    if (marker < 0) {
        return 0;
    }
    for (index = 0; index < DOOM_MAP_LUMP_COUNT; ++index) {
        const uint8_t* view = NULL;
        uint32_t size = 0u;

        if (whd_lump_view(container, marker + 1 + index, &view, &size) !=
            WHD_ERR_OK) {
            return 0;
        }
        source->data[index] = view;
        source->size[index] = (size_t)size;
    }
    return doom_map_open(out, source) == DOOM_MAP_OK;
}

/* --- Geometry the fixture needs ------------------------------------------ */

/* R_PointToAngle(), copied from the walk's own helper exactly as door_check.c
 * does: a harness must not reach into lib/doom's internals. */
static angle_t pointToAngle(fixed_t x, fixed_t y, fixed_t tx, fixed_t ty) {
    fixed_t dx = (fixed_t)((uint32_t)tx - (uint32_t)x);
    fixed_t dy = (fixed_t)((uint32_t)ty - (uint32_t)y);

    if (dx == 0 && dy == 0) {
        return 0;
    }
    if (dx >= 0) {
        if (dy >= 0) {
            if (dx > dy) {
                return r_tan_to_angle[slope_div((uint32_t)dy, (uint32_t)dx)];
            }
            return ANG90 - 1u - r_tan_to_angle[slope_div((uint32_t)dx, (uint32_t)dy)];
        }
        dy = (fixed_t)(0u - (uint32_t)dy);
        if (dx > dy) {
            return 0u - r_tan_to_angle[slope_div((uint32_t)dy, (uint32_t)dx)];
        }
        return ANG270 + r_tan_to_angle[slope_div((uint32_t)dx, (uint32_t)dy)];
    }
    dx = (fixed_t)(0u - (uint32_t)dx);
    if (dy >= 0) {
        if (dx > dy) {
            return ANG180 - 1u - r_tan_to_angle[slope_div((uint32_t)dy, (uint32_t)dx)];
        }
        return ANG90 + r_tan_to_angle[slope_div((uint32_t)dx, (uint32_t)dy)];
    }
    dy = (fixed_t)(0u - (uint32_t)dy);
    if (dx > dy) {
        return ANG180 + r_tan_to_angle[slope_div((uint32_t)dy, (uint32_t)dx)];
    }
    return ANG270 - 1u - r_tan_to_angle[slope_div((uint32_t)dx, (uint32_t)dy)];
}

static int sectorAt(const doom_map_t* map, fixed_t x, fixed_t y,
                    uint16_t* sector) {
    uint16_t subsector = 0;
    doom_subsector_t record;

    if (r_bsp_point_in_subsector(map, x, y, &subsector) != R_BSP_OK) {
        return 0;
    }
    doom_map_get_subsector(map, subsector, &record);
    *sector = doom_map_subsector_sector(map, &record);
    return 1;
}

/* --- Assertions ---------------------------------------------------------- */

static int failures = 0;

static void expect(int condition, const char* what) {
    if (!condition) {
        printf("FAIL %s\n", what);
        failures += 1;
    }
}

/* --- The exit fixture ---------------------------------------------------- */

typedef struct exit_fixture {
    uint16_t line;
    int16_t special;
    uint16_t front;
    fixed_t x;
    fixed_t y;
    angle_t angle;
    fixed_t standoff;
} exit_fixture_t;

/*
 * The lowest line index carrying @p special, with a point @p standoff map units
 * in front of its midpoint on the front sector's side, facing it. `standoff` is
 * tried from 32 down to 16 map units: a prop or a jamb narrows some alcoves.
 */
static int findExitLine(p_level_t* level, int16_t special,
                        exit_fixture_t* out) {
    const doom_map_t* map = level->map;
    uint32_t i;
    static const fixed_t gaps[STANDOFFS] = {32 * FRACUNIT, 24 * FRACUNIT,
                                            16 * FRACUNIT};
    /* Points along the line, as twelfths: the midpoint first, then the shoulders,
     * so a monster standing on the middle of the line does not hide the exit. */
    static const int kThirds[5] = {6, 4, 8, 3, 9};

    for (i = 0; i < level->lineCount; ++i) {
        doom_line_t line;
        doom_vertex_t v1;
        doom_vertex_t v2;
        uint16_t front = 0;
        fixed_t dx;
        fixed_t dy;
        int64_t nx;
        int64_t ny;
        double length;
        int pointIndex;
        int gapIndex;
        int sign;

        if (level->lineSpecial[i] != special) {
            continue;
        }
        doom_map_get_line(map, (uint16_t)i, &line);
        if (!p_line_front_sector(map, &line, &front)) {
            continue;
        }
        doom_map_get_vertex(map, line.v1, &v1);
        doom_map_get_vertex(map, line.v2, &v2);
        dx = v2.x - v1.x;
        dy = v2.y - v1.y;
        nx = -(int64_t)dy;
        ny = (int64_t)dx;
        length = sqrt((double)nx * (double)nx + (double)ny * (double)ny);
        if (length <= 0.0) {
            continue;
        }
        for (pointIndex = 0; pointIndex < 5; ++pointIndex) {
            const fixed_t mx =
                v1.x + (fixed_t)(((int64_t)dx * kThirds[pointIndex]) / 12);
            const fixed_t my =
                v1.y + (fixed_t)(((int64_t)dy * kThirds[pointIndex]) / 12);

            for (gapIndex = 0; gapIndex < STANDOFFS; ++gapIndex) {
                const int64_t stepX = (int64_t)((double)nx / length *
                                                (double)gaps[gapIndex]);
                const int64_t stepY = (int64_t)((double)ny / length *
                                                (double)gaps[gapIndex]);

                for (sign = 1; sign >= -1; sign -= 2) {
                    const fixed_t px =
                        (fixed_t)((uint32_t)mx + (uint32_t)(sign * stepX));
                    const fixed_t py =
                        (fixed_t)((uint32_t)my + (uint32_t)(sign * stepY));
                    uint16_t sector = 0;
                    p_position_result_t position;

                    if (!sectorAt(map, px, py, &sector) || sector != front) {
                        continue;
                    }
                    if (!p_check_position(&level->context, level->player, px,
                                          py, &position)) {
                        continue;
                    }
                    out->line = (uint16_t)i;
                    out->special = special;
                    out->front = front;
                    out->x = px;
                    out->y = py;
                    out->angle = pointToAngle(px, py, mx, my);
                    out->standoff = gaps[gapIndex];
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* --- The sector-11 fixture (E1M8's ending) ------------------------------- */

/*
 * A point inside @p sector, on a 16-unit grid over the map's bounds, where the
 * avatar can stand on that sector's *own* floor. Deterministic: the grid's
 * origin is the bounds' floor multiple of 16 and the scan is row-major.
 *
 * The floor test matters: P_CheckPosition raises `floorz` when the avatar's box
 * reaches a step, and P_PlayerInSpecialSector ignores a thing whose `z` is not
 * the sector's floor -- which is exactly the "still falling" rule. So the
 * fixture writes the sector's floor into `z` and this scan only accepts points
 * where the two agree.
 */
static int findSectorStand(p_level_t* level, uint16_t sector,
                           exit_fixture_t* out) {
    const doom_map_t* map = level->map;
    const uint32_t vertexCount = map->lumps[DOOM_MAP_VERTEXES].count;
    const fixed_t step = 16 * FRACUNIT;
    doom_vertex_t vertex;
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
    for (y = (fixed_t)(minY & ~(step - 1)); y <= maxY; y += step) {
        for (x = (fixed_t)(minX & ~(step - 1)); x <= maxX; x += step) {
            uint16_t found = 0;
            p_position_result_t position;

            if (!sectorAt(map, x, y, &found) || found != sector) {
                continue;
            }
            if (!p_check_position(&level->context, level->player, x, y,
                                  &position) ||
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
            return 1;
        }
    }
    return 0;
}

/* The one sector whose special is @p special, or -1. */
static int sectorWithSpecial(const p_level_t* level, int16_t special) {
    uint32_t i;
    int found = -1;

    for (i = 0u; i < level->sectorCount; ++i) {
        if (level->sectors[i].special == special) {
            if (found >= 0) {
                return -2;   /* more than one: not the fixture this gate wants */
            }
            found = (int)i;
        }
    }
    return found;
}

/* Stands the player at the fixture and returns the position adopted. */
static void standAt(p_level_t* level, const exit_fixture_t* fixture) {    r_mobj_t* mobj = level->player;
    p_position_result_t position;

    if (mobj == NULL) {
        return;
    }
    if (!p_check_position(&level->context, mobj, fixture->x, fixture->y,
                          &position)) {
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

/* One tic with @p buttons. Returns p_level_tic()'s code. */
static int runTic(p_level_t* level, uint8_t buttons) {
    p_ticcmd_t cmd;

    memset(&cmd, 0, sizeof cmd);
    cmd.buttons = buttons;
    return p_level_tic(level, &cmd, NULL) == P_TICK_OK;
}

/* --- Reference loads and the live world ---------------------------------- */

typedef struct reference {
    uint32_t things;
    uint32_t spawned;
    uint32_t sectors;
    uint32_t lines;
    int32_t startX;
    int32_t startY;
    uint32_t arena;
    uint32_t arenaSize;
} reference_t;

static int referenceLoad(const doom_map_t* map, const p_spawn_rules_t* rules,
                         reference_t* out) {
    p_level_t level;
    void* arena = malloc(ARENA_BYTES);
    int ok = 0;

    if (arena == NULL) {
        return 0;
    }
    memset(&level, 0, sizeof level);
    if (p_level_init(&level, map, arena, ARENA_BYTES, rules) == P_SETUP_OK &&
        p_spawn_specials(&level) == P_SPEC_OK && level.player != NULL) {
        const zone_stats_t zone = z_stats(&level.zone);

        out->things = level.stats.things;
        out->spawned = level.stats.spawned;
        out->sectors = level.sectorCount;
        out->lines = level.lineCount;
        out->startX = (int32_t)level.playerStarts[0].x;
        out->startY = (int32_t)level.playerStarts[0].y;
        out->arena = zone.inUse;
        out->arenaSize = zone.arenaSize;
        ok = 1;
    }
    p_level_shutdown(&level);
    free(arena);
    return ok;
}

static int loadWorld(p_level_t* level, const doom_map_t* map, void* arena,
                     const p_spawn_rules_t* rules) {
    if (p_level_init(level, map, arena, ARENA_BYTES, rules) != P_SETUP_OK) {
        return 0;
    }
    return p_spawn_specials(level) == P_SPEC_OK && level->player != NULL;
}

static int alive(const p_level_t* level) {
    return level->playerState.playerstate == P_PST_LIVE &&
           level->playerState.health > 0 &&
           level->playerState.mo == level->player;
}

/* --- The scripted chain -------------------------------------------------- */

typedef struct chain_step {
    uint8_t from;
    int16_t special; /* the exit line to press; 0 means the map has none */
    uint8_t to;      /* the expected next map; 0 is the episode's victory */
} chain_step_t;

/*
 * The faithful route through all nine maps. E1M3's secret switch is the real
 * E1M9 entrance in the shareware maps; E1M8 has no line at all (sector 11 ends
 * the episode), so the chain stops there.
 */
static const chain_step_t kChain[] = {
    {1, 11, 2}, {2, 11, 3}, {3, 51, 9}, {9, 11, 4},
    {4, 11, 5}, {5, 11, 6}, {6, 11, 7}, {7, 11, 8},
    {8, 0, 0},
};
#define CHAIN_STEPS (sizeof kChain / sizeof kChain[0])

static uint32_t transitionCount = 0u;
static uint32_t maxArena = 0u;
static char chainText[96];

static void appendChain(const char* name) {
    const size_t used = strlen(chainText);

    if (used != 0u && used + 1u < sizeof chainText) {
        chainText[used] = '>';
        chainText[used + 1u] = '\0';
    }
    if (strlen(chainText) + strlen(name) + 1u < sizeof chainText) {
        strcat(chainText, name);
    }
}

/*
 * One transition, given the exit a level just requested and the target the
 * script expects. Prints the TRANSITION line and asserts every property the
 * acceptance names.
 */
static int transition(p_level_t* level, g_level_state_t* game,
                      const g_level_next_t* next, const doom_map_t* nextMap,
                      const reference_t* ref, uint8_t from, uint8_t to,
                      const char* exitText, uint16_t line, fixed_t standoff,
                      int consumed) {
    char fromName[8];
    char toName[8];
    zone_stats_t zone;
    int ok;

    levelName(from, fromName);
    levelName(to, toName);
    if (next->map != to || next->victory != 0u) {
        printf("FAIL transition %s -> %s: G_DoCompleted named map=%u victory=%u\n",
               fromName, toName, (unsigned)next->map, (unsigned)next->victory);
        failures += 1;
        return 0;
    }
    if (g_do_world_done(level, game, next, nextMap) != G_LEVEL_OK) {
        printf("FAIL transition %s -> %s: g_do_world_done refused\n", fromName,
               toName);
        failures += 1;
        return 0;
    }
    zone = z_stats(&level->zone);
    transitionCount += 1u;
    if (zone.inUse > maxArena) {
        maxArena = zone.inUse;
    }

    ok = alive(level) && level->map == nextMap &&
         level->stats.things == ref->things &&
         level->stats.spawned == ref->spawned &&
         p_level_mobj_count(level) == ref->spawned &&
         level->sectorCount == ref->sectors &&
         level->lineCount == ref->lines &&
         (int32_t)level->playerStarts[0].x == ref->startX &&
         (int32_t)level->playerStarts[0].y == ref->startY &&
         level->player->x == (fixed_t)((int32_t)ref->startX * FRACUNIT) &&
         level->player->y == (fixed_t)((int32_t)ref->startY * FRACUNIT) &&
         level->leveltime == 0 && level->exitRequest == (uint8_t)P_EXIT_NONE &&
         zone.inUse == ref->arena && zone.arenaSize == ref->arenaSize;

    printf("TRANSITION n=%u from=%s to=%s exit=%s line=%u standoff=%ld "
           "things=%u spawned=%u mobjs=%u sectors=%u lines=%u "
           "playerstart=%ld,%ld worldstart=%ld,%ld alive=%d leveltime=%d "
           "arena=%u ref=%u consumed=%d ok=%d\n",
           transitionCount, fromName, toName, exitText, (unsigned)line,
           (long)(standoff / FRACUNIT), (unsigned)level->stats.things,
           (unsigned)level->stats.spawned, (unsigned)p_level_mobj_count(level),
           (unsigned)level->sectorCount, (unsigned)level->lineCount,
           (long)ref->startX, (long)ref->startY,
           (long)(level->player->x / FRACUNIT),
           (long)(level->player->y / FRACUNIT), alive(level),
           (int)level->leveltime, (unsigned)zone.inUse, (unsigned)ref->arena,
           consumed, ok ? 1 : 0);
    if (!ok) {
        failures += 1;
    }
    expect(ok, "transition: the new world is not a fresh load of the target map");
    return ok;
}

int main(int argc, char** argv) {
    whd_state_t container;
    doom_map_source_t sources[LEVEL_COUNT];
    doom_map_t maps[LEVEL_COUNT];
    reference_t references[LEVEL_COUNT];
    p_spawn_rules_t rules;
    p_level_t level;
    g_level_state_t game;
    void* arena;
    uint8_t* bytes;
    size_t size = 0;
    int mapIndex;
    int chainIndex;

    if (argc < 2) {
        fprintf(stderr, "usage: level_check <path-to-container>\n");
        return 2;
    }
    bytes = readFile(argv[1], &size);
    if (bytes == NULL) {
        fprintf(stderr, "level_check: cannot read %s\n", argv[1]);
        return 2;
    }
    if (whd_open(&container, bytes, size) != WHD_ERR_OK) {
        fprintf(stderr, "level_check: %s is not a PWHD container\n", argv[1]);
        free(bytes);
        return 2;
    }
    printf("CONTAINER lumps=%u names=%u size=%u\n",
           (unsigned)container.numLumps, (unsigned)container.nameCount,
           (unsigned)size);
    memset(sources, 0, sizeof sources);
    memset(maps, 0, sizeof maps);
    for (mapIndex = 1; mapIndex <= LEVEL_COUNT; ++mapIndex) {
        if (!openMap(&container, mapIndex, &maps[mapIndex - 1],
                     &sources[mapIndex - 1])) {
            fprintf(stderr, "level_check: E1M%d is not a raw view in the container\n",
                    mapIndex);
            free(bytes);
            return 2;
        }
    }

    /* P_SetupLevel's inputs. The demo path's defaults are the single-player
     * medium-skill rules; a level's exits do not depend on the skill. */
    rules = p_spawn_rules_default();
    rules.skill = P_SKILL_MEDIUM;

    /* A fresh load of every map: the target each transition must reproduce. */
    for (mapIndex = 0; mapIndex < LEVEL_COUNT; ++mapIndex) {
        char name[8];

        if (!referenceLoad(&maps[mapIndex], &rules, &references[mapIndex])) {
            fprintf(stderr, "level_check: E1M%d refused a fresh load\n",
                    mapIndex + 1);
            free(bytes);
            return 2;
        }
        levelName(mapIndex + 1, name);
        printf("LEVEL map=%s things=%u spawned=%u sectors=%u lines=%u "
               "playerstart=%ld,%ld arena=%u arenaSize=%u\n",
               name, (unsigned)references[mapIndex].things,
               (unsigned)references[mapIndex].spawned,
               (unsigned)references[mapIndex].sectors,
               (unsigned)references[mapIndex].lines,
               (long)references[mapIndex].startX,
               (long)references[mapIndex].startY,
               (unsigned)references[mapIndex].arena,
               (unsigned)references[mapIndex].arenaSize);
    }

    /* --- The live chain --- */
    arena = malloc(ARENA_BYTES);
    if (arena == NULL) {
        free(bytes);
        return 2;
    }
    memset(&level, 0, sizeof level);
    if (!loadWorld(&level, &maps[0], arena, &rules)) {
        fprintf(stderr, "level_check: E1M1 refused the live load\n");
        free(arena);
        free(bytes);
        return 2;
    }
    g_level_state_init(&game, 1u, 1u);
    chainText[0] = '\0';
    appendChain("E1M1");

    for (chainIndex = 0; chainIndex < (int)CHAIN_STEPS; ++chainIndex) {
        const chain_step_t step = kChain[chainIndex];
        char fromName[8];
        char toName[8];
        exit_fixture_t fixture;
        int nextKind;
        g_level_next_t next;

        levelName(step.from, fromName);
        if (level.map != &maps[step.from - 1] || game.map != step.from) {
            printf("FAIL chain: the world is not on %s\n", fromName);
            failures += 1;
            break;
        }

        if (step.special == 0) {
            /* E1M8: no exit line, and G_DoCompleted's victory branch is the
             * answer. Assert both, then stop the chain here. */
            int found = 0;
            uint32_t i;
            int sector;
            exit_fixture_t sectorFixture;
            zone_stats_t zone;
            int32_t before;
            int32_t after;

            for (i = 0; i < level.lineCount; ++i) {
                if (level.lineSpecial[i] == 11 || level.lineSpecial[i] == 51 ||
                    level.lineSpecial[i] == 52 || level.lineSpecial[i] == 124) {
                    found = 1;
                }
            }
            expect(found == 0, "E1M8 carries an exit line after all");

            /* P_PlayerInSpecialSector's case 11 is the boss map's only ending:
             * the floor damages first, then `health <= 10` asks G_ExitLevel().
             * The fixture starts at 30 health so the first hit (20) lands the
             * player on the threshold with 10 left -- alive, which is what
             * P_DamageMobj's "end of game hell" clamp guarantees. */
            sector = sectorWithSpecial(&level, 11);
            expect(sector >= 0, "E1M8 does not carry exactly one sector 11");
            if (sector >= 0 &&
                findSectorStand(&level, (uint16_t)sector, &sectorFixture)) {
                standAt(&level, &sectorFixture);
                level.leveltime = 0;
                level.playerState.health = 30;
                level.player->health = 30;
                before = level.playerState.health;
                if (!runTic(&level, 0u)) {
                    printf("FAIL finale: E1M8 refused the damage tic\n");
                    failures += 1;
                }
                after = level.playerState.health;
                zone = z_stats(&level.zone);

                expect(after == 10,
                       "sector 11 did not take its 20 points from 30");
                expect(level.exitRequest == (uint8_t)P_EXIT_NORMAL,
                       "sector 11 did not ask G_ExitLevel at health 10");
                expect(alive(&level),
                       "sector 11 killed the player it was told to spare");

                g_exit_from_level(&game, &level);
                nextKind = g_completed_next(&game, &next);
                expect(nextKind == 0 && next.victory == 1u,
                       "E1M8 did not answer with G_DoCompleted's victory");
                /* linuxdoom-1.10 has no E1M8 -> E1M9 step: `gamemap == 8`
                 * returns ga_victory before `secretexit` is read. So the load
                 * refuses the missing next map and the world stays where it is;
                 * asserting that refusal is the faithful form of "the episode
                 * ends here". */
                expect(g_do_world_done(&level, &game, &next, &maps[7]) ==
                           G_LEVEL_ERR_VICTORY,
                       "the victory was followed by a level load");
                expect(level.map == &maps[7] && game.map == 8u,
                       "the world left E1M8 on a victory");
                printf("FINALE map=%s exitlines=%d sector=%d special=11 "
                       "health=%ld before=%ld damage=%ld exit=%s alive=%d "
                       "victory=%d next=%d world=%s leveltime=%d arena=%u ref=%u\n",
                       fromName, found, sector, (long)after, (long)before,
                       (long)(before - after),
                       level.exitRequest == (uint8_t)P_EXIT_NORMAL ? "normal" : "other",
                       alive(&level), next.victory, nextKind,
                       level.map == &maps[7] ? "E1M8" : "changed",
                       (int)level.leveltime, (unsigned)zone.inUse,
                       (unsigned)references[7].arena);
                /* The world after its completion is still a fresh E1M8 load's:
                 * one damage tic allocates nothing. */
                expect(zone.inUse == references[7].arena,
                       "the E1M8 completion changed the arena");
            } else {
                printf("FAIL finale: E1M8 has no standable sector-11 floor\n");
                failures += 1;
            }
            break;
        }

        if (!findExitLine(&level, step.special, &fixture)) {
            printf("FAIL chain: %s has no standable special-%d line\n", fromName,
                   (int)step.special);
            failures += 1;
            break;
        }
        standAt(&level, &fixture);
        /* G_PlayerReborn's `usedown = true`: one tic without input releases the
         * latch, then the press is a new edge and P_UseLines runs. */
        if (!runTic(&level, 0u)) {
            printf("FAIL chain: %s refused the settle tic\n", fromName);
            failures += 1;
            break;
        }
        if (!runTic(&level, BT_USE)) {
            printf("FAIL chain: %s refused the use tic\n", fromName);
            failures += 1;
            break;
        }
        expect(level.exitRequest ==
                   (uint8_t)(step.special == 11 ? P_EXIT_NORMAL : P_EXIT_SECRET),
               "the use press did not record the exit kind");
        g_exit_from_level(&game, &level);
        nextKind = g_completed_next(&game, &next);
        levelName(step.to, toName);
        expect(nextKind == 1 && next.map == step.to,
               "G_DoCompleted named a different next map");
        if (nextKind != 1 || next.map != step.to) {
            printf("FAIL chain: %s exit named map=%u victory=%u, expected %u\n",
                   fromName, (unsigned)next.map, (unsigned)next.victory,
                   (unsigned)step.to);
            break;
        }
        {
            char exitText[16];

            snprintf(exitText, sizeof exitText, "use:%d", (int)step.special);
            if (!transition(&level, &game, &next, &maps[step.to - 1],
                            &references[step.to - 1], step.from, step.to,
                            exitText, fixture.line, fixture.standoff,
                            level.lineSpecial[fixture.line] == 0)) {
                break;
            }
        }
        appendChain(toName);
    }

    /* --- The walk-over fixtures: 52 and 124, which E1 carries no line for --- */
    {
        p_level_t cross;
        p_spawn_rules_t crossRules = rules;
        void* crossArena = malloc(ARENA_BYTES);
        exit_fixture_t fixture;
        int result;

        if (crossArena == NULL) {
            failures += 1;
        } else {
            memset(&cross, 0, sizeof cross);
            if (!loadWorld(&cross, &maps[0], crossArena, &crossRules) ||
                !findExitLine(&cross, 11, &fixture)) {
                printf("FAIL cross: the E1M1 fixture did not load\n");
                failures += 1;
            } else {
                const uint16_t line = fixture.line;
                const uint32_t triggers = cross.specStats.triggers;
                const uint32_t refused = cross.specStats.triggersRefused;

                cross.lineSpecial[line] = 52;
                cross.exitRequest = (uint8_t)P_EXIT_NONE;
                result = p_trigger_special_line(&cross, line, P_TRIGGER_CROSS, 1);
                printf("CROSS special=52 kind=cross result=%d exit=%s consumed=%d\n",
                       result,
                       cross.exitRequest == (uint8_t)P_EXIT_NORMAL ? "normal" : "other",
                       cross.lineSpecial[line] == 0);
                expect(result == 1 &&
                           cross.exitRequest == (uint8_t)P_EXIT_NORMAL &&
                           cross.lineSpecial[line] == 52 &&
                           cross.specStats.triggers == triggers + 1u,
                       "cross 52: G_ExitLevel's request, with the line left on");

                cross.lineSpecial[line] = 124;
                cross.exitRequest = (uint8_t)P_EXIT_NONE;
                result = p_trigger_special_line(&cross, line, P_TRIGGER_CROSS, 1);
                printf("CROSS special=124 kind=cross result=%d exit=%s consumed=%d\n",
                       result,
                       cross.exitRequest == (uint8_t)P_EXIT_SECRET ? "secret" : "other",
                       cross.lineSpecial[line] == 0);
                expect(result == 1 &&
                           cross.exitRequest == (uint8_t)P_EXIT_SECRET &&
                           cross.lineSpecial[line] == 124,
                       "cross 124: G_SecretExitLevel's request, with the line left on");

                /* P_CrossSpecialLine's non-player list does not include either
                 * number: a monster crossing an exit line must not end the
                 * level. */
                cross.lineSpecial[line] = 52;
                cross.exitRequest = (uint8_t)P_EXIT_NONE;
                result = p_trigger_special_line(&cross, line, P_TRIGGER_CROSS, 0);
                printf("CROSS special=52 kind=cross byplayer=0 result=%d refused=%u\n",
                       result, (unsigned)(cross.specStats.triggersRefused - refused));
                expect(result == 0 &&
                           cross.exitRequest == (uint8_t)P_EXIT_NONE &&
                           cross.specStats.triggersRefused == refused + 1u,
                       "cross 52: a non-player thing ended the level");
            }
            p_level_shutdown(&cross);
            free(crossArena);
        }
    }

    p_level_shutdown(&level);
    free(arena);

    /* --- The ninth level change: E1M1's secret exit, which has no line --- */
    {
        p_level_t secret;
        g_level_state_t secretGame;
        g_level_next_t next;
        void* secretArena = malloc(ARENA_BYTES);
        int result;

        if (secretArena == NULL) {
            failures += 1;
        } else {
            memset(&secret, 0, sizeof secret);
            if (!loadWorld(&secret, &maps[0], secretArena, &rules)) {
                printf("FAIL secret: E1M1 refused the load\n");
                failures += 1;
            } else {
                g_level_state_init(&secretGame, 1u, 1u);
                g_secret_exit_level(&secretGame);
                result = g_completed_next(&secretGame, &next);
                expect(result == 1 && next.map == 9u && next.secret == 1u,
                       "G_SecretExitLevel from E1M1 did not name E1M9");
                printf("SECRET from=E1M1 to=E1M9 via=g_secret_exit_level "
                       "next=%d secret=%d\n", result, next.secret);
                transition(&secret, &secretGame, &next, &maps[8], &references[8],
                           1u, 9u, "secret", 0u, 0, 0);
            }
            p_level_shutdown(&secret);
            free(secretArena);
        }
    }

    printf("SUMMARY transitions=%u chain=%s arena=%u ok=%d\n", transitionCount,
           chainText, maxArena, failures == 0 ? 1 : 0);
    free(bytes);
    return failures == 0 ? 0 : 1;
}
