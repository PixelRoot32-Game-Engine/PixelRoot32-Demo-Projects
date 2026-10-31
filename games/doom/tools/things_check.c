/*
 * Host harness for the THINGS run (D4.1a): loads a level's THINGS lump through
 * lib/doom/p_setup.c and prints every mobj it produced, the counters, the
 * recorded starts and the random position the load reached.
 *
 * This is a desktop tool, not firmware: it reads the WAD into malloc'd memory,
 * allocates the level's arena with malloc and prints through stdio, none of
 * which the port does. tools/things_check.py compiles it, runs it next to
 * tools/doom_ref.py --things and diffs the two outputs line by line.
 *
 * PROTOCOL (identical to doom_ref.py's --things output; stated in both so
 * neither has to be read to compare them)
 *
 *   THINGS <map> skill=<0..4> things=<count>
 *   MOBJ <index> type=<n> x=<i32> y=<i32> z=<i32> angle=0x%08x
 *        subsector=<n> sector=<n> flags=0x%08x health=<i32> state=<i32>
 *   MOBJ-COUNT <n>
 *   COUNTERS things=<n> spawned=<n> kills=<n> items=<n> secrets=<n>
 *            skill=<n> single=<n> nomonsters=<n> dmskip=<n> unknown=<n>
 *            invalid=<n> failed=<n> playerstarts=<n> deathmatchstarts=<n>
 *   PLAYERSTART <slot> x=<n> y=<n> angle=<n> type=<n> options=<n>
 *   DEATHMATCHSTART <index> x=<n> y=<n> angle=<n> type=<n> options=<n>
 *   PLAYER <mobj index, or -1>
 *   RNG <index>
 *
 * MOBJ indices are positions in the thinker list, which for a level with no
 * other thinkers is THINGS order among the records that spawned. Coordinates
 * are 16.16; the PLAYERSTART/DEATHMATCHSTART lines carry map units, because
 * that is what the level records.
 *
 * Lines starting with `INFO ` are C-only measurements (record sizes, the
 * generated table sizes, the arena's accounting) and are excluded from the
 * comparison; the oracle has no arena to measure.
 *
 * Map coordinates beyond the THINGS records are not printed, and the records
 * are shareware map data the repository does not carry; this output is a
 * comparison protocol, not a copy of the map.
 *
 * Build:
 *   gcc -std=gnu99 -O2 -Wall -Wextra -Ilib/doom -o things_check \
 *       tools/things_check.c lib/doom/p_setup.c lib/doom/p_mobj.c \
 *       lib/doom/p_tick.c lib/doom/p_action.c lib/doom/m_random.c \
 *       lib/doom/z_zone.c lib/doom/doom_map.c lib/doom/r_bsp.c \
 *       lib/doom/r_plane.c lib/doom/r_tables.c lib/doom/w_wad.c
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "info_tables.h"
#include "p_setup.h"
#include "w_wad.h"

/* The level's arena. E1M1's 138 records produce at most 138 mobjs at 136 B
 * each (19 KB), so 64 KB is generous and still small enough to prove the cost
 * is the record, not the buffer. */
#define THINGS_ARENA_BYTES (64u * 1024u)

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

/* One start record, in map units, as the level recorded it. */
static void printStart(const char* label, int index, const p_mapthing_t* start) {
    printf("%s %d x=%d y=%d angle=%d type=%d options=%d\n", label, index, (int)start->x,
           (int)start->y, (int)start->angle, (int)start->type, (int)start->options);
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
    int skill = 2;
    int marker;
    int index;
    const r_mobj_t* mobj;
    uint32_t mobjIndex;
    uint32_t playerIndex = 0u;
    int playerFound = 0;
    zone_stats_t zone;

    if (argc < 2 || argc > 4) {
        fprintf(stderr, "usage: things_check <path-to-WAD> [skill 0..4] [map]\n");
        return 2;
    }
    if (argc >= 3) {
        skill = atoi(argv[2]);
    }
    if (argc >= 4) {
        mapName = argv[3];
    }
    if (skill < (int)P_SKILL_BABY || skill > (int)P_SKILL_NIGHTMARE) {
        fprintf(stderr, "things_check: skill must be 0..4\n");
        return 2;
    }

    bytes = readFile(argv[1], &size);
    if (bytes == NULL) {
        fprintf(stderr, "things_check: cannot read %s\n", argv[1]);
        return 2;
    }
    if (wad_open(&wad, bytes, size) != WAD_ERR_OK) {
        fprintf(stderr, "things_check: not a WAD\n");
        free(bytes);
        return 2;
    }
    marker = W_CheckNumForName(&wad, mapName);
    if (marker < 0 || marker + 1 + DOOM_MAP_LUMP_COUNT > wad.numLumps) {
        fprintf(stderr, "things_check: %s is not in this WAD\n", mapName);
        free(bytes);
        return 2;
    }
    for (index = 0; index < DOOM_MAP_LUMP_COUNT; ++index) {
        const int lump = marker + 1 + index;

        source.data[index] = W_LumpPointer(&wad, lump);
        source.size[index] = (size_t)W_LumpLength(&wad, lump);
    }
    if (doom_map_open(&map, &source) != DOOM_MAP_OK) {
        fprintf(stderr, "things_check: map rejected with doom_map error %d\n",
                (int)map.error);
        free(bytes);
        return 2;
    }

    arena = (uint8_t*)malloc(THINGS_ARENA_BYTES);
    if (arena == NULL) {
        fprintf(stderr, "things_check: no arena\n");
        free(bytes);
        return 2;
    }

    rules = p_spawn_rules_default();
    rules.skill = (p_skill_t)skill;

    if (p_level_init(&level, &map, arena, THINGS_ARENA_BYTES, &rules) != P_SETUP_OK) {
        fprintf(stderr, "things_check: level load failed with code %d\n", (int)level.error);
        free(arena);
        free(bytes);
        return 2;
    }

    printf("THINGS %s skill=%d things=%u\n", mapName, skill, level.stats.things);

    mobjIndex = 0u;
    for (mobj = p_level_first_mobj(&level); mobj != NULL; mobj = p_level_next_mobj(mobj)) {
        if (mobj == level.player) {
            playerIndex = mobjIndex;
            playerFound = 1;
        }
        printf("MOBJ %u type=%d x=%ld y=%ld z=%ld angle=0x%08lx subsector=%u sector=%u "
               "flags=0x%08lx health=%ld state=%d\n",
               mobjIndex, (int)mobj->type, (long)mobj->x, (long)mobj->y, (long)mobj->z,
               (unsigned long)mobj->angle, (unsigned)mobj->subsector,
               (unsigned)mobj->sector, (unsigned long)mobj->flags, (long)mobj->health,
               (int)mobj->state);
        mobjIndex += 1u;
    }
    printf("MOBJ-COUNT %u\n", mobjIndex);

    printf("COUNTERS things=%u spawned=%u kills=%ld items=%ld secrets=%ld skill=%u "
           "single=%u nomonsters=%u dmskip=%u unknown=%u invalid=%u failed=%u "
           "playerstarts=%u deathmatchstarts=%u\n",
           level.stats.things, level.stats.spawned, (long)level.totalKills,
           (long)level.totalItems, (long)level.totalSecrets, level.stats.skippedSkill,
           level.stats.skippedSingle, level.stats.skippedNoMonsters,
           level.stats.skippedDeathmatch, level.stats.unknownType,
           level.stats.invalidThing, level.stats.spawnFailed, level.stats.playerStarts,
           level.stats.deathmatchStarts);

    for (index = 0; index < MAXPLAYERS; ++index) {
        if (level.playerStartSeen[index]) {
            printStart("PLAYERSTART", index, &level.playerStarts[index]);
        }
    }
    for (index = 0; index < (int)level.deathmatchStartCount; ++index) {
        printStart("DEATHMATCHSTART", index, &level.deathmatchStarts[index]);
    }

    printf("PLAYER %d\n", playerFound ? (int)playerIndex : -1);
    printf("RNG %u\n", level.random.index);

    /* C-only measurements, excluded from the comparison. */
    zone = z_stats(&level.zone);
    printf("INFO commit %s\n", DOOM_INFO_UPSTREAM_COMMIT);
    printf("INFO hostsizes mobj=%u header=%u perMobj=%u level=%u\n",
           (unsigned)sizeof(r_mobj_t), (unsigned)ZONE_HEADER_SIZE,
           (unsigned)(sizeof(r_mobj_t) + ZONE_HEADER_SIZE), (unsigned)sizeof(p_level_t));
    printf("INFO tables mobjinfo=%u states=%u sprnames=%u\n",
           (unsigned)(sizeof(mobjinfo) / sizeof(mobjinfo[0])),
           (unsigned)(sizeof(states) / sizeof(states[0])),
           (unsigned)(sizeof(sprnames) / sizeof(sprnames[0])));
    printf("INFO zone arena=%lu inUse=%lu free=%lu overhead=%lu largest=%lu failures=%lu\n",
           (unsigned long)zone.arenaSize, (unsigned long)zone.inUse,
           (unsigned long)zone.freeBytes, (unsigned long)zone.overhead,
           (unsigned long)zone.largestFree, (unsigned long)zone.failures);
    printf("INFO actions calls=%u done=%u stubs=%u notimplemented=%u unknown=%u last=%u\n",
           level.actions.calls, level.actions.done, level.actions.stubs,
           level.actions.notImplemented, level.actions.unknown,
           level.actions.lastNotImplemented);

    p_level_shutdown(&level);
    free(arena);
    free(bytes);
    return 0;
}
