/*
 * Host harness for sight and waking (T15a).
 *
 * It loads E1M1 through lib/doom/p_setup.c and runs four short scenes, each one
 * a fresh level with the avatar and one zombieman:
 *
 *   sees    the monster 40 map units along the player's spawn facing, in the
 *           same room: its first state cycle must set `target` and enter the see
 *           state (S_POSS_RUN1).
 *   wall    the same monster at a spot whose straight line to the player crosses
 *           a one-sided linedef: it must stay in its spawn state for the whole
 *           script.
 *   noise   the wall monster again, and the player fires at tic 20 through the
 *           real weapon path: P_FireWeapon's P_NoiseAlert must flood the sector,
 *           set its soundTarget and wake the monster even though it cannot see
 *           the player.
 *   door    a monster standing inside the nearest closed manual door's sector,
 *           the player fires: a closed door's opening is zero, so the flood must
 *           not cross it and the monster must not wake.
 *
 * Then it compares P_CheckSight against the oracle for a deterministic sweep of
 * mobj pairs from E1M1's own THINGS population, so both the REJECT pre-test and
 * the BSP walk are exercised on real geometry.
 *
 * THE PROTOCOL
 *
 *   ENEMY <map> script=sight-and-waking
 *   FIXTURE sees   x=<i32> y=<i32> sector=<n>
 *   FIXTURE wall   x=<i32> y=<i32> sector=<n> from=<slot>
 *   FIXTURE door   x=<i32> y=<i32> sector=<n> line=<n> front=<n> back=<n>
 *   SWEEP <map> mobjs=<n> pairs=<n>
 *   SIGHT <i> <j> v=<0|1> s1=<n> s2=<n>
 *   SCENE-BEGIN name=<name>
 *   STEP <n> op=<name> rng=<n>
 *   PSP <0|1> state=<s> tics=<t> sx=<i32> sy=<i32>
 *   MOBJ <slot> type=<t> x=<i32> y=<i32> z=<i32> health=<i32> flags=0x<8x>
 *       state=<s> tics=<t> tgt=<slot|-1> momx=<i32> momy=<i32>
 *   PLAYER hp=<i32> ammo=<4> w=<mask> pend=<n> ready=<n> refire=<n> atk=<n>
 *       lite=<n> snd=<n> noise=<n>
 *   SCENE-END name=<name> tics=<n> woketic=<n|-1> target=<slot|-1>
 *       state=<s> stayed=<0|1> sndtarget=<slot|-1> rng=<n>
 *   CSTAT sight_checks=<n> reject=<n> reject_missing=<n> bsp=<n>
 *       bsp_blocked=<n> accepted=<n>
 *
 * CSTAT is a C-only measurement of which paths P_CheckSight took; the Python
 * gate asserts that all three (REJECT, BSP accept, BSP block) were exercised.
 *
 * WHY A WEAK FALLBACK EXISTS
 *
 * The gate has to be runnable against the tree *before* p_sight.c exists, so its
 * RED is an assertion rather than a link error. The two weak definitions below
 * are overridden by the real ones as soon as p_sight.c joins the link; they are a
 * property of the gate, not a behaviour the shipped firmware can reach.
 *
 * Build: see tools/enemy_check.py. This is a desktop tool: malloc, stdio and the
 * WAD come from the host, none of which the firmware uses.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p_enemy.h"
#include "p_inter.h"
#include "p_map.h"
#include "p_mobj.h"
#include "p_pspr.h"
#include "p_setup.h"
#include "p_sight.h"
#include "p_spec.h"
#include "p_user.h"
#include "r_bsp.h"
#include "r_tables.h"
#include "w_wad.h"

/* --- The gate's RED fallback (see the header comment) --------------------- */

#if defined(__GNUC__)
__attribute__((weak))
int p_check_sight(p_mobj_context_t* world, const r_mobj_t* t1,
                  const r_mobj_t* t2) {
    (void)world;
    (void)t1;
    (void)t2;
    return 0;
}

__attribute__((weak))
r_mobj_t* p_sector_sound_target(const p_mobj_context_t* world,
                                uint16_t sector) {
    (void)world;
    (void)sector;
    return NULL;
}
#endif

#define ENEMY_ARENA_BYTES (256u * 1024u)
#define ENEMY_SCRIPT "sight-and-waking"
#define ENEMY_TICS 40
#define ENEMY_FIRE_TIC 20
#define ENEMY_MONSTER MT_POSSESSED

/* The sweep's pair rule: every ordered pair (i, j) with i != j and
 * (17i + 31j) % 13 == 0. Deterministic, independent of the map, and 644 pairs on
 * E1M1's 92 spawned mobjs -- several hundred, as the task asks. */
#define ENEMY_PAIR_MOD 13

#define ENEMY_MAX_MOBJS 256
#define ENEMY_MAX_SECTORS 512

/* --- Reading and printing ------------------------------------------------- */

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

static int slot_of(const p_level_t* level, const r_mobj_t* mobj) {
    const r_mobj_t* each;
    int slot = 0;

    if (mobj == NULL) {
        return -1;
    }
    for (each = p_level_first_mobj(level); each != NULL;
         each = p_level_next_mobj(each)) {
        if (each == mobj) {
            return slot;
        }
        slot += 1;
    }
    return -1;
}

static void print_mobj(const p_level_t* level, int slot, const r_mobj_t* mobj) {
    printf("MOBJ %d type=%d x=%ld y=%ld z=%ld health=%ld flags=0x%08lx "
           "state=%d tics=%d tgt=%d momx=%ld momy=%ld\n",
           slot, (int)mobj->type, (long)mobj->x, (long)mobj->y, (long)mobj->z,
           (long)mobj->health, (unsigned long)mobj->flags, (int)mobj->state,
           (int)mobj->tics, slot_of(level, mobj->target), (long)mobj->momx,
           (long)mobj->momy);
}

static void print_psprites(const p_player_t* player) {
    int position;

    for (position = 0; position < P_NUMPSPRITES; ++position) {
        const p_pspdef_t* psp = &player->psprites[position];

        printf("PSP %d state=%d tics=%ld sx=%ld sy=%ld\n", position,
               (int)psp->state, (long)psp->tics, (long)psp->sx, (long)psp->sy);
    }
}

static void print_player(const p_player_t* player) {
    unsigned weapons = 0u;
    int i;

    for (i = 0; i < P_NUMWEAPONS; ++i) {
        if (player->weaponowned[i]) {
            weapons |= 1u << i;
        }
    }

    printf("PLAYER hp=%ld ammo=%ld,%ld,%ld,%ld w=%u pend=%ld ready=%ld "
           "refire=%ld atk=%ld lite=%ld snd=%lu noise=%lu\n",
           (long)player->health, (long)player->ammo[P_AM_CLIP],
           (long)player->ammo[P_AM_SHELL], (long)player->ammo[P_AM_CELL],
           (long)player->ammo[P_AM_MISL], weapons, (long)player->pendingweapon,
           (long)player->readyweapon, (long)player->refire,
           (long)player->attackdown, (long)player->extralight,
           (unsigned long)player->silentSounds,
           (unsigned long)player->noiseAlerts);
}

static void emit(p_level_t* level, int* step, const char* op) {
    const r_mobj_t* mobj;
    int slot = 0;

    printf("STEP %d op=%s rng=%u\n", *step, op, (unsigned)level->random.index);
    print_psprites(&level->playerState);
    for (mobj = p_level_first_mobj(level); mobj != NULL;
         mobj = p_level_next_mobj(mobj)) {
        print_mobj(level, slot, mobj);
        slot += 1;
    }
    print_player(&level->playerState);
    *step += 1;
}

/* --- Fixture geometry ----------------------------------------------------- */

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

/*
 * A strict straddle test between segment (a,b) and segment (c,d). It is the
 * fixture selector's own geometry, not the sight rule: a segment that crosses a
 * one-sided linedef has a wall between its endpoints, whatever P_CheckSight
 * thinks. The products are 64-bit because a map coordinate is a 16.16 int32.
 */
static int segmentsCross(fixed_t ax, fixed_t ay, fixed_t bx, fixed_t by,
                         fixed_t cx, fixed_t cy, fixed_t dx, fixed_t dy) {
    const int64_t d1 = (int64_t)(bx - ax) * (int64_t)(cy - ay) -
                       (int64_t)(by - ay) * (int64_t)(cx - ax);
    const int64_t d2 = (int64_t)(bx - ax) * (int64_t)(dy - ay) -
                       (int64_t)(by - ay) * (int64_t)(dx - ax);
    const int64_t d3 = (int64_t)(dx - cx) * (int64_t)(ay - cy) -
                       (int64_t)(dy - cy) * (int64_t)(ax - cx);
    const int64_t d4 = (int64_t)(dx - cx) * (int64_t)(by - cy) -
                       (int64_t)(dy - cy) * (int64_t)(bx - cx);

    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}

static int segmentCrossesWall(const doom_map_t* map, fixed_t ax, fixed_t ay,
                              fixed_t bx, fixed_t by) {
    uint32_t i;

    for (i = 0; i < map->lumps[DOOM_MAP_LINEDEFS].count; ++i) {
        doom_line_t line;
        doom_vertex_t v1;
        doom_vertex_t v2;

        doom_map_get_line(map, (uint16_t)i, &line);
        if ((line.flags & DOOM_ML_TWOSIDED) != 0) {
            continue;
        }
        doom_map_get_vertex(map, line.v1, &v1);
        doom_map_get_vertex(map, line.v2, &v2);
        if (segmentsCross(ax, ay, bx, by, v1.x, v1.y, v2.x, v2.y)) {
            return 1;
        }
    }
    return 0;
}

/*
 * Can the sound flood reach @p target from @p start through open two-sided
 * lines? This is the *fixture selector's* reachability test, not the flood: it
 * ignores ML_SOUNDBLOCK and keeps no state, so it can only over-estimate, and the
 * noise assertion itself decides whether the flood actually arrived.
 */
static int soundReachable(const p_mobj_context_t* world, uint16_t start,
                          uint16_t target) {
    uint8_t seen[ENEMY_MAX_SECTORS];
    uint16_t stack[ENEMY_MAX_SECTORS];
    uint32_t top = 0u;
    const doom_map_t* map = world->map;

    if (world->sectorCount > ENEMY_MAX_SECTORS ||
        start >= world->sectorCount || target >= world->sectorCount) {
        return 0;
    }
    memset(seen, 0, sizeof seen);
    seen[start] = 1u;
    stack[top++] = start;

    while (top != 0u) {
        const uint16_t sector = stack[--top];
        uint32_t i;

        if (sector == target) {
            return 1;
        }
        for (i = 0; i < world->lineCount; ++i) {
            doom_line_t line;
            uint16_t front;
            uint16_t back;
            uint16_t other;
            p_line_opening_t opening;

            doom_map_get_line(map, (uint16_t)i, &line);
            if ((line.flags & DOOM_ML_TWOSIDED) == 0) {
                continue;
            }
            if (!p_line_front_sector(map, &line, &front) ||
                !p_line_back_sector(map, &line, &back)) {
                continue;
            }
            if (front != sector && back != sector) {
                continue;
            }
            other = (front == sector) ? back : front;
            if (other >= world->sectorCount || seen[other] != 0u) {
                continue;
            }
            p_line_opening(world, (uint16_t)i, &opening);
            if (opening.openRange <= 0) {
                continue;
            }
            seen[other] = 1u;
            if (top < ENEMY_MAX_SECTORS) {
                stack[top++] = other;
            }
        }
    }
    return 0;
}

typedef struct door_fixture {
    uint16_t line;
    uint16_t front;
    uint16_t back;
    fixed_t x;
    fixed_t y;
} door_fixture_t;

/* door_check.c's selection: the nearest closed manual door to the player start,
 * with the additional fixture requirements that its front sector is reachable by
 * sound from the player and its back sector is not. The point is 8 map units
 * inside the back sector, along the line's normal. */
static int findDoor(const p_level_t* level, const r_mobj_t* start,
                    door_fixture_t* out) {
    const doom_map_t* map = level->map;
    uint32_t i;
    int64_t bestDistance = 0;
    int found = 0;

    for (i = 0; i < level->lineCount; ++i) {
        doom_line_t line;
        doom_vertex_t v1;
        doom_vertex_t v2;
        uint16_t front = 0;
        uint16_t back = 0;
        fixed_t dx;
        fixed_t dy;
        fixed_t mx;
        fixed_t my;
        int64_t ddx;
        int64_t ddy;
        int64_t distance;
        int64_t nx;
        int64_t ny;
        double length;
        int sign;

        if (level->lineSpecial[i] != 1) {
            continue;
        }
        doom_map_get_line(map, (uint16_t)i, &line);
        if (!p_line_front_sector(map, &line, &front) ||
            !p_line_back_sector(map, &line, &back)) {
            continue;
        }
        if (level->sectors[back].ceilingHeight > level->sectors[back].floorHeight ||
            level->sectors[front].floorHeight != level->sectors[back].floorHeight) {
            continue;
        }
        if (!soundReachable(&level->context, start->sector, front) ||
            soundReachable(&level->context, start->sector, back)) {
            continue;
        }
        doom_map_get_vertex(map, line.v1, &v1);
        doom_map_get_vertex(map, line.v2, &v2);
        dx = v2.x - v1.x;
        dy = v2.y - v1.y;
        mx = v1.x + dx / 2;
        my = v1.y + dy / 2;
        ddx = (int64_t)mx - (int64_t)start->x;
        ddy = (int64_t)my - (int64_t)start->y;
        distance = ddx * ddx + ddy * ddy;
        if (found && distance >= bestDistance) {
            continue;
        }

        nx = -(int64_t)dy;
        ny = (int64_t)dx;
        length = sqrt((double)nx * (double)nx + (double)ny * (double)ny);
        if (length <= 0.0) {
            continue;
        }
        for (sign = 1; sign >= -1; sign -= 2) {
            const double stepX = (double)nx / length * 8.0 * (double)FRACUNIT;
            const double stepY = (double)ny / length * 8.0 * (double)FRACUNIT;
            const fixed_t px =
                (fixed_t)((uint32_t)mx + (uint32_t)(sign * (int64_t)stepX));
            const fixed_t py =
                (fixed_t)((uint32_t)my + (uint32_t)(sign * (int64_t)stepY));
            uint16_t sector = 0;

            if (sectorAt(map, px, py, &sector) && sector == back) {
                out->line = (uint16_t)i;
                out->front = front;
                out->back = back;
                out->x = px;
                out->y = py;
                bestDistance = distance;
                found = 1;
                break;
            }
        }
    }
    return found;
}

/* --- The scenes ----------------------------------------------------------- */

typedef struct scene_result {
    int wakeTic;
    int targetSlot;
    int soundSlot;
    int finalState;
    int stayed;
} scene_result_t;

static int run_scene(const doom_map_t* map, uint8_t* arena, size_t arenaBytes,
                     const char* name, fixed_t monsterX, fixed_t monsterY,
                     int fire, scene_result_t* result) {
    p_level_t level;
    p_spawn_rules_t rules;
    r_mobj_t* player;
    r_mobj_t* monster;
    r_mobj_t* each;
    int spawnState;
    int altState;
    int step = 0;
    int tic;

    memset(result, 0, sizeof *result);
    result->wakeTic = -1;
    result->targetSlot = -1;
    result->soundSlot = -1;
    result->stayed = 1;

    rules = p_spawn_rules_default();
    if (p_level_init(&level, map, arena, arenaBytes, &rules) != P_SETUP_OK) {
        fprintf(stderr, "enemy_check: %s: level load failed with code %d\n",
                name, (int)level.error);
        return 2;
    }
    player = level.player;
    if (player == NULL) {
        fprintf(stderr, "enemy_check: %s: no single-player start\n", name);
        p_level_shutdown(&level);
        return 2;
    }

    /* Keep the avatar; remove E1M1's population so it cannot block a shot, absorb
     * it, be heard, or enter the mobj lines. The removal is deferred, so one
     * thinker walk finishes it. */
    for (each = (r_mobj_t*)p_level_first_mobj(&level); each != NULL;
         each = (r_mobj_t*)p_level_next_mobj(each)) {
        if (each != player) {
            (void)p_remove_mobj(&level.context, each);
        }
    }
    (void)p_run_thinkers(&level.thinkers, &p_level_release_thinker,
                         &level.context, NULL);

    monster = p_spawn_mobj(&level.context, monsterX, monsterY, ONFLOORZ,
                           ENEMY_MONSTER, NULL);
    if (monster == NULL) {
        fprintf(stderr, "enemy_check: %s: the zone refused the monster\n", name);
        p_level_shutdown(&level);
        return 2;
    }
    monster->angle = p_point_to_angle(player->x - monster->x,
                                      player->y - monster->y);

    spawnState = mobjinfo[ENEMY_MONSTER].spawnstate;
    altState = states[spawnState].nextstate;

    printf("SCENE-BEGIN name=%s\n", name);
    emit(&level, &step, "setup");

    for (tic = 0; tic < ENEMY_TICS; ++tic) {
        p_ticcmd_t cmd;

        memset(&cmd, 0, sizeof cmd);
        cmd.buttons = (uint8_t)((fire && tic == ENEMY_FIRE_TIC) ? BT_ATTACK : 0);
        (void)p_level_tic(&level, &cmd, NULL);

        if (result->wakeTic < 0 && monster->target != NULL) {
            result->wakeTic = tic;
            result->targetSlot = slot_of(&level, monster->target);
        }
        if (monster->target != NULL ||
            (monster->state != spawnState && monster->state != altState)) {
            result->stayed = 0;
        }
        emit(&level, &step, (fire && tic == ENEMY_FIRE_TIC) ? "fire" : "tick");
    }

    if (monster->target == NULL) {
        result->targetSlot = -1;
    }
    result->soundSlot = slot_of(
        &level, p_sector_sound_target(&level.context, monster->sector));
    result->finalState = monster->state;

    printf("SCENE-END name=%s tics=%d woketic=%d target=%d state=%d stayed=%d "
           "sndtarget=%d rng=%u\n",
           name, ENEMY_TICS, result->wakeTic, result->targetSlot,
           result->finalState, result->stayed, result->soundSlot,
           (unsigned)level.random.index);

    p_level_shutdown(&level);
    return 0;
}

/* --- The sight sweep ------------------------------------------------------ */

static int run_sweep(const doom_map_t* map, uint8_t* arena, size_t arenaBytes,
                     const char* mapName, p_sight_stats_t* stats,
                     fixed_t* seesX, fixed_t* seesY, uint16_t* seesSector,
                     fixed_t* wallX, fixed_t* wallY, uint16_t* wallSector,
                     int* wallFrom) {
    p_level_t level;
    p_spawn_rules_t rules;
    r_mobj_t* mobjs[ENEMY_MAX_MOBJS];
    int count = 0;
    int i;
    int j;
    int pairs = 0;
    int result = 0;
    const r_mobj_t* each;
    r_mobj_t* player;

    rules = p_spawn_rules_default();
    if (p_level_init(&level, map, arena, arenaBytes, &rules) != P_SETUP_OK) {
        fprintf(stderr, "enemy_check: level load failed with code %d\n",
                (int)level.error);
        return 2;
    }
    player = level.player;
    if (player == NULL) {
        fprintf(stderr, "enemy_check: no single-player start\n");
        p_level_shutdown(&level);
        return 2;
    }

    for (each = p_level_first_mobj(&level); each != NULL;
         each = p_level_next_mobj(each)) {
        if (count >= ENEMY_MAX_MOBJS) {
            fprintf(stderr, "enemy_check: more than %d mobjs\n", ENEMY_MAX_MOBJS);
            p_level_shutdown(&level);
            return 2;
        }
        mobjs[count++] = (r_mobj_t*)each;
    }

    printf("ENEMY %s script=%s\n", mapName, ENEMY_SCRIPT);

    /* The sees fixture: 40 map units along the avatar's spawn facing, exactly as
     * tools/attack_check.c places its target. */
    {
        const uint32_t fine = player->angle >> ANGLETOFINESHIFT;
        uint16_t sector = 0;

        *seesX = player->x +
                 (int32_t)40 * r_fine_sine[fine + (uint32_t)(FINEANGLES / 4)];
        *seesY = player->y + (int32_t)40 * r_fine_sine[fine];
        if (!sectorAt(map, *seesX, *seesY, &sector)) {
            fprintf(stderr, "enemy_check: the sees spot is not in a sector\n");
            p_level_shutdown(&level);
            return 2;
        }
        *seesSector = sector;
        printf("FIXTURE sees x=%ld y=%ld sector=%u\n", (long)*seesX,
               (long)*seesY, (unsigned)sector);
    }

    /* The wall fixture: the first spawned thing, in file order, whose segment to
     * the player crosses a one-sided line, in another sector, reachable by sound.
     * It is the shotgun's own spot on E1M1 (thing 1), and any map with the same
     * properties will do. */
    *wallFrom = -1;
    for (i = 0; i < count; ++i) {
        if (mobjs[i] == player || mobjs[i]->sector == player->sector) {
            continue;
        }
        if (!segmentCrossesWall(map, mobjs[i]->x, mobjs[i]->y, player->x,
                                player->y)) {
            continue;
        }
        if (!soundReachable(&level.context, player->sector, mobjs[i]->sector)) {
            continue;
        }
        *wallX = mobjs[i]->x;
        *wallY = mobjs[i]->y;
        *wallSector = mobjs[i]->sector;
        *wallFrom = i;
        break;
    }
    if (*wallFrom < 0) {
        fprintf(stderr, "enemy_check: no wall-separated, sound-reachable spot\n");
        p_level_shutdown(&level);
        return 2;
    }
    printf("FIXTURE wall x=%ld y=%ld sector=%u from=%d\n", (long)*wallX,
           (long)*wallY, (unsigned)*wallSector, *wallFrom);

    /* The sweep: every ordered pair (i, j), i != j, with (17i + 31j) % 13 == 0.
     * Both endpoints are real mobjs from the THINGS run, so z and height are the
     * ones the map produced. */
    level.context.sightStats = stats;
    printf("SWEEP %s mobjs=%d pairs=", mapName, count);
    for (i = 0; i < count; ++i) {
        for (j = 0; j < count; ++j) {
            if (i == j || (i * 17 + j * 31) % ENEMY_PAIR_MOD != 0) {
                continue;
            }
            pairs += 1;
        }
    }
    printf("%d\n", pairs);

    for (i = 0; i < count; ++i) {
        for (j = 0; j < count; ++j) {
            int visible;

            if (i == j || (i * 17 + j * 31) % ENEMY_PAIR_MOD != 0) {
                continue;
            }
            visible = p_check_sight(&level.context, mobjs[i], mobjs[j]);
            printf("SIGHT %d %d v=%d s1=%u s2=%u\n", i, j, visible,
                   (unsigned)mobjs[i]->sector, (unsigned)mobjs[j]->sector);
        }
    }
    level.context.sightStats = NULL;

    printf("CSTAT sight_checks=%lu reject=%lu reject_missing=%lu bsp=%lu "
           "bsp_blocked=%lu accepted=%lu\n",
           (unsigned long)stats->checks, (unsigned long)stats->rejectHits,
           (unsigned long)stats->rejectMissing, (unsigned long)stats->bspWalks,
           (unsigned long)stats->bspBlocked, (unsigned long)stats->accepted);

    p_level_shutdown(&level);
    return result;
}

/* --- Main ----------------------------------------------------------------- */

int main(int argc, char** argv) {
    wad_state_t wad;
    doom_map_source_t source;
    doom_map_t map;
    uint8_t* bytes;
    uint8_t* arena;
    size_t size = 0;
    const char* mapName = "E1M1";
    int marker;
    int index;
    int result;
    p_sight_stats_t stats;
    fixed_t seesX = 0;
    fixed_t seesY = 0;
    fixed_t wallX = 0;
    fixed_t wallY = 0;
    uint16_t seesSector = 0;
    uint16_t wallSector = 0;
    int wallFrom = -1;
    door_fixture_t door;
    scene_result_t scene;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: enemy_check <path-to-WAD> [map]\n");
        return 2;
    }
    if (argc == 3) {
        mapName = argv[2];
    }

    bytes = readFile(argv[1], &size);
    if (bytes == NULL) {
        fprintf(stderr, "enemy_check: cannot read %s\n", argv[1]);
        return 2;
    }
    if (wad_open(&wad, bytes, size) != WAD_ERR_OK) {
        fprintf(stderr, "enemy_check: not a WAD\n");
        free(bytes);
        return 2;
    }
    marker = W_CheckNumForName(&wad, mapName);
    if (marker < 0 || marker + 1 + DOOM_MAP_LUMP_COUNT > wad.numLumps) {
        fprintf(stderr, "enemy_check: %s is not in this WAD\n", mapName);
        free(bytes);
        return 2;
    }
    for (index = 0; index < DOOM_MAP_LUMP_COUNT; ++index) {
        const int lump = marker + 1 + index;

        source.data[index] = W_LumpPointer(&wad, lump);
        source.size[index] = (size_t)W_LumpLength(&wad, lump);
    }
    if (doom_map_open(&map, &source) != DOOM_MAP_OK) {
        fprintf(stderr, "enemy_check: map rejected with doom_map error %d\n",
                (int)map.error);
        free(bytes);
        return 2;
    }

    arena = (uint8_t*)malloc(ENEMY_ARENA_BYTES);
    if (arena == NULL) {
        fprintf(stderr, "enemy_check: no arena\n");
        free(bytes);
        return 2;
    }

    memset(&stats, 0, sizeof stats);
    result = run_sweep(&map, arena, ENEMY_ARENA_BYTES, mapName, &stats, &seesX,
                       &seesY, &seesSector, &wallX, &wallY, &wallSector,
                       &wallFrom);
    if (result != 0) {
        free(arena);
        free(bytes);
        return result;
    }

    /* The door fixture needs its own clean level: it scans the mutable sector
     * heights and the line specials, and the sweep's level has been shut down. */
    {
        p_level_t level;
        p_spawn_rules_t rules = p_spawn_rules_default();

        if (p_level_init(&level, &map, arena, ENEMY_ARENA_BYTES, &rules) !=
            P_SETUP_OK) {
            fprintf(stderr, "enemy_check: door level load failed\n");
            free(arena);
            free(bytes);
            return 2;
        }
        if (!findDoor(&level, level.player, &door)) {
            fprintf(stderr, "enemy_check: no closed manual door fixture\n");
            p_level_shutdown(&level);
            free(arena);
            free(bytes);
            return 2;
        }
        printf("FIXTURE door x=%ld y=%ld sector=%u line=%u front=%u back=%u\n",
               (long)door.x, (long)door.y, (unsigned)door.back,
               (unsigned)door.line, (unsigned)door.front, (unsigned)door.back);
        p_level_shutdown(&level);
    }

    result = run_scene(&map, arena, ENEMY_ARENA_BYTES, "sees", seesX, seesY, 0,
                       &scene);
    if (result != 0) {
        free(arena);
        free(bytes);
        return result;
    }
    result = run_scene(&map, arena, ENEMY_ARENA_BYTES, "wall", wallX, wallY, 0,
                       &scene);
    if (result != 0) {
        free(arena);
        free(bytes);
        return result;
    }
    result = run_scene(&map, arena, ENEMY_ARENA_BYTES, "noise", wallX, wallY, 1,
                       &scene);
    if (result != 0) {
        free(arena);
        free(bytes);
        return result;
    }
    result = run_scene(&map, arena, ENEMY_ARENA_BYTES, "door", door.x, door.y, 1,
                       &scene);
    if (result != 0) {
        free(arena);
        free(bytes);
        return result;
    }

    free(arena);
    free(bytes);
    return 0;
}
