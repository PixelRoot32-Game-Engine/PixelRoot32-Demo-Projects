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
 *   line 330                                    special 11  use   (exit, deferred)
 *   line 352..359                               special 48  cross (scroll, deferred)
 *
 * Tic 0 is the state after the spawn scan and the triggers; tic 1 is after one
 * p_level_tick(). The run is 420 tics, long enough for a door to open, wait 150
 * and close again.
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
#include "p_plats.h"
#include "p_spec.h"
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

    if (argc < 2 || argc > 4) {
        fprintf(stderr, "usage: specials_check <path-to-WAD> [map] [tics]\n");
        return 2;
    }
    if (argc >= 3) {
        mapName = argv[2];
    }
    if (argc >= 4) {
        tics = atoi(argv[3]);
    }
    if (tics < 0) {
        fprintf(stderr, "specials_check: tics cannot be negative\n");
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
