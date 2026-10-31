/*
 * Host harness for movement and collision (D4.2b).
 *
 * It loads a level through lib/doom/p_setup.c, keeps only the player mobj that
 * P_LoadThings() already spawned (the first type-1 THING, through
 * P_SpawnPlayer), runs the scripted 160-tic walk against the real p_map.c, and
 * prints the state after every tic.
 *
 * tools/movement_check.py compiles this file, runs it next to
 * tools/doom_ref.py --movement, and compares the two outputs line for line.
 *
 * PROTOCOL (identical to doom_ref.py's --movement output; stated in both so
 * neither has to be read to compare them)
 *
 *   MOVEMENT <map> tics=<N> script=<name>
 *   TIC <n> x=<i32> y=<i32> z=<i32> momx=<i32> momy=<i32> floorz=<i32>
 *       ceilingz=<i32> sector=<i32> subsector=<i32> blocked=<0|1> cross=<n>
 *   SUMMARY tics=<N> finalx=<i32> finaly=<i32> finalz=<i32> blocked=<n>
 *           cross=<n> checks=<n>
 *
 * THE SCRIPT (documented here and in doom_ref.py; identical in both)
 *
 *   tic 0 is the state after the first thrust and the first thinker run; tic n
 *   is after n + 1. The mobj starts as p_spawn_player() leaves it (angle from
 *   the THING record) and the script overrides the angle to 0 and health to
 *   spawnhealth before tic 0.
 *
 *     tics   0..31   angle 0,      thrust 0x19 * 2048  (walk speed)
 *     tics  32..63   angle 90,     thrust 0x19 * 2048  (into a wall, slides)
 *     tics  64..95   angle 45,     thrust 0x32 * 2048  (run speed, into a corner)
 *     tics  96..127  no thrust                         (friction only)
 *     tics 128..159  angle 180,    thrust 0x19 * 2048  (back into the room)
 *
 * `blocked` on a TIC line is 1 when that tic had at least one P_TryMove refusal
 * (the first refusal is what a slide then recovers from). `cross` is the number
 * of P_CrossSpecialLine invocations from that tic's move.
 *
 * Lines starting with `INFO ` are C-only measurements: the mean and peak
 * microseconds of one P_CheckPosition over a fixed number of probes, and the
 * number of linedefs the brute-force walk examines. They are excluded from the
 * comparison because the oracle has no timers.
 *
 * THE PERTURBATION HOOK
 *
 * P_ORIG_FRICTION is a #ifndef in p_map.h, so the whole build can be perturbed
 * with -DP_ORIG_FRICTION=0xE000. tools/movement_check.py --perturb friction does
 * exactly that, which is how the gate is proved able to fail.
 *
 * Build: see tools/movement_check.py. This is a desktop tool: malloc, stdio, the
 * WAD and a wall clock come from the host, none of which the firmware uses.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#include "p_map.h"
#include "p_setup.h"
#include "p_spec.h"
#include "w_wad.h"

#define MOVEMENT_ARENA_BYTES (256u * 1024u)
#define MOVEMENT_DEFAULT_TICS 160
#define MOVEMENT_PROBES 2000

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

static double nowMicroseconds(void) {
#ifdef _WIN32
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;

    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000000.0 / (double)frequency.QuadPart;
#else
    struct timespec stamp;

    clock_gettime(CLOCK_MONOTONIC, &stamp);
    return (double)stamp.tv_sec * 1000000.0 + (double)stamp.tv_nsec / 1000.0;
#endif
}

/* The scripted angle and thrust for one tic, and whether to thrust at all. */
static int phaseForTic(int tic, angle_t* angle, fixed_t* move) {
    if (tic < 32) {
        *angle = 0;
        *move = 0x19 * 2048;
        return 1;
    }
    if (tic < 64) {
        *angle = ANG90;
        *move = 0x19 * 2048;
        return 1;
    }
    if (tic < 96) {
        *angle = ANG45;
        *move = 0x32 * 2048;
        return 1;
    }
    if (tic < 128) {
        return 0; /* friction only */
    }
    if (tic < 160) {
        *angle = ANG180;
        *move = 0x19 * 2048;
        return 1;
    }
    return 0;
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
    int tics = MOVEMENT_DEFAULT_TICS;
    int marker;
    int index;
    int tic;
    r_mobj_t* mobj;
    p_move_stats_t stats;
    p_position_result_t probe;
    uint32_t lines;
    uint32_t blockedAtStart;
    uint32_t crossedAtStart;
    int removed;

    if (argc < 2 || argc > 4) {
        fprintf(stderr, "usage: movement_check <path-to-WAD> [map] [tics]\n");
        return 2;
    }
    if (argc >= 3) {
        mapName = argv[2];
    }
    if (argc >= 4) {
        tics = atoi(argv[3]);
    }
    if (tics < 0) {
        fprintf(stderr, "movement_check: tics cannot be negative\n");
        return 2;
    }

    bytes = readFile(argv[1], &size);
    if (bytes == NULL) {
        fprintf(stderr, "movement_check: cannot read %s\n", argv[1]);
        return 2;
    }
    if (wad_open(&wad, bytes, size) != WAD_ERR_OK) {
        fprintf(stderr, "movement_check: not a WAD\n");
        free(bytes);
        return 2;
    }
    marker = W_CheckNumForName(&wad, mapName);
    if (marker < 0 || marker + 1 + DOOM_MAP_LUMP_COUNT > wad.numLumps) {
        fprintf(stderr, "movement_check: %s is not in this WAD\n", mapName);
        free(bytes);
        return 2;
    }
    for (index = 0; index < DOOM_MAP_LUMP_COUNT; ++index) {
        const int lump = marker + 1 + index;

        source.data[index] = W_LumpPointer(&wad, lump);
        source.size[index] = (size_t)W_LumpLength(&wad, lump);
    }
    if (doom_map_open(&map, &source) != DOOM_MAP_OK) {
        fprintf(stderr, "movement_check: map rejected with doom_map error %d\n",
                (int)map.error);
        free(bytes);
        return 2;
    }

    arena = (uint8_t*)malloc(MOVEMENT_ARENA_BYTES);
    if (arena == NULL) {
        fprintf(stderr, "movement_check: no arena\n");
        free(bytes);
        return 2;
    }

    rules = p_spawn_rules_default();
    if (p_level_init(&level, &map, arena, MOVEMENT_ARENA_BYTES, &rules) !=
        P_SETUP_OK) {
        fprintf(stderr, "movement_check: level load failed with code %d\n",
                (int)level.error);
        free(arena);
        free(bytes);
        return 2;
    }

    /*
     * P_LoadThings() already ran P_SpawnPlayer() for the first type-1 record,
     * which is exactly the scripted spawn. Keep that mobj and remove every other
     * thing, so the walk isolates the map's linedefs from E1M1's population.
     * The removal is deferred, so one thinker walk finishes it; the player's own
     * think does nothing here (no momentum, z on the floor).
     */
    if (level.player == NULL) {
        fprintf(stderr, "movement_check: %s has no single-player start\n", mapName);
        p_level_shutdown(&level);
        free(arena);
        free(bytes);
        return 2;
    }
    for (mobj = (r_mobj_t*)p_level_first_mobj(&level); mobj != NULL;
         mobj = (r_mobj_t*)p_level_next_mobj(mobj)) {
        if (mobj != level.player) {
            (void)p_remove_mobj(&level.context, mobj);
        }
    }
    (void)p_run_thinkers(&level.thinkers, &p_level_release_thinker,
                         &level.context, NULL);

    mobj = level.player;
    mobj->angle = 0;
    mobj->health = mobjinfo[MT_PLAYER].spawnhealth;

    /* C-only measurement: one P_CheckPosition, at four positions around the
     * spawn, over a fixed number of probes. The move counters are NULL here so
     * the script's `checks` is not polluted. */
    lines = map.lumps[DOOM_MAP_LINEDEFS].count;
    {
        static const fixed_t offsets[4] = {0, 8 * FRACUNIT, -8 * FRACUNIT,
                                           24 * FRACUNIT};
        double total = 0.0;
        double peak = 0.0;
        int samples = 0;
        uint32_t i;

        for (i = 0; i < 4u; ++i) {
            int probeIndex;

            for (probeIndex = 0; probeIndex < MOVEMENT_PROBES; ++probeIndex) {
                const fixed_t px = mobj->x + offsets[i];
                const fixed_t py = mobj->y + offsets[(i + 1) & 3u];
                double start = nowMicroseconds();
                double elapsed;

                (void)p_check_position(&level.context, mobj, px, py, &probe);
                elapsed = nowMicroseconds() - start;
                total += elapsed;
                if (elapsed > peak) {
                    peak = elapsed;
                }
                samples += 1;
            }
        }
        /* 475 is the whole candidate set: the brute-force walk decodes every
         * LINEDEF and lets the bbox test reject most of them. */
        printf("INFO check mean_us=%.3f peak_us=%.3f samples=%d lines=%u\n",
               total / (double)samples, peak, samples, (unsigned)lines);
        printf("INFO level baseline=%u lines=%u sector=%u subsector=%u\n",
               (unsigned)sizeof(p_level_t), (unsigned)map.lumps[DOOM_MAP_LINEDEFS].count,
               (unsigned)map.lumps[DOOM_MAP_SECTORS].count,
               (unsigned)map.lumps[DOOM_MAP_SSECTORS].count);
    }

    memset(&stats, 0, sizeof stats);
    level.context.moveStats = &stats;

    printf("MOVEMENT %s tics=%d script=walk-slide-corner-friction-return\n",
           mapName, tics);

    for (tic = 0; tic < tics; ++tic) {
        angle_t angle = mobj->angle;
        fixed_t move = 0;
        int thrust = phaseForTic(tic, &angle, &move);

        if (thrust) {
            mobj->angle = angle;
            p_thrust(&level.context, mobj, angle, move);
        }

        blockedAtStart = stats.blocked;
        crossedAtStart = stats.crossed;
        p_mobj_thinker(&mobj->thinker, &level.context);
        removed = mobj->thinker.removed != 0u;

        printf("TIC %d x=%ld y=%ld z=%ld momx=%ld momy=%ld floorz=%ld "
               "ceilingz=%ld sector=%u subsector=%u blocked=%d cross=%u\n",
               tic, (long)mobj->x, (long)mobj->y, (long)mobj->z,
               (long)mobj->momx, (long)mobj->momy, (long)mobj->floorz,
               (long)mobj->ceilingz, (unsigned)mobj->sector,
               (unsigned)mobj->subsector,
               stats.blocked != blockedAtStart ? 1 : 0,
               (unsigned)(stats.crossed - crossedAtStart));

        if (removed) {
            break;
        }
    }

    printf("SUMMARY tics=%d finalx=%ld finaly=%ld finalz=%ld blocked=%u "
           "cross=%u checks=%u\n",
           tics, (long)mobj->x, (long)mobj->y, (long)mobj->z,
           (unsigned)stats.blocked, (unsigned)stats.crossed,
           (unsigned)stats.checks);

    p_level_shutdown(&level);
    free(arena);
    free(bytes);
    return 0;
}
