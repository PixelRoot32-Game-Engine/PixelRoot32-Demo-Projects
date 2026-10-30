/*
 * D3.10's nine-level referee: every shareware map opens out of one container.
 *
 * WHY THIS IS C AND NOT PYTHON
 *
 * The bug class this has to catch is the one D3.6 shipped: doom_map_open()
 * validates record sizes and index ranges, so a map-lump set shifted by one
 * lump can open cleanly and render garbage. Only the same code the board runs
 * can prove the container feeds it the ten lumps in the order it expects. This
 * harness links lib/doom/whd.c (the container reader) and lib/doom/doom_map.c
 * (the map validator), reads the level from the container exactly as
 * src/doomc/FrameDriver.cpp does, and prints what each level's lumps and record
 * counts are.
 *
 * Every map lump must be RAW: the check asks whd_lump_view() for a pointer, not
 * just for a size, because a deflated level would cost DRAM on the board and
 * would not be the container this task is required to ship.
 *
 * INPUT
 *
 * One PWHD container whose payloads include E1M1..E1M9. Nothing is written.
 * tools/whd_encode.py --check-levels compiles and runs this file, and compares
 * the printed sizes and counts with the source WAD's own directory entries.
 *
 * OUTPUT
 *
 *   LEVEL E1M1 status=ok total=55714 THINGS=1380/138 LINEDEFS=... 
 *   LEVEL E1M2 status=missing
 *   LEVEL E1M2 status=fail raw=0 map=5
 *
 * One line per level; `bytes/count` is the lump size and the record count
 * doom_map_open() accepted. Exit 0 when all nine opened, 1 otherwise, 2 when the
 * container itself cannot be read.
 *
 * This file is compiled by tools/whd_encode.py, not by PlatformIO: it lives
 * under test/unit/test_whd/tool/ and env:whd_test ignores that directory.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doom_map.h"
#include "whd.h"

#define LEVEL_COUNT 9
#define MAP_LUMP_COUNT 10

static const char* const kLevels[LEVEL_COUNT] = {
    "E1M1", "E1M2", "E1M3", "E1M4", "E1M5", "E1M6", "E1M7", "E1M8", "E1M9"};

static const char* const kLumpNames[MAP_LUMP_COUNT] = {
    "THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS",
    "SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP"};

static uint8_t* read_file(const char* path, size_t* size) {
    FILE* file = fopen(path, "rb");
    long length;
    uint8_t* bytes;

    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    bytes = (uint8_t*)malloc((size_t)length);
    if (bytes == NULL) {
        fclose(file);
        return NULL;
    }
    if (fread(bytes, 1u, (size_t)length, file) != (size_t)length) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return bytes;
}

int main(int argc, char** argv) {
    uint8_t* bytes;
    size_t size = 0u;
    whd_state_t state;
    int level;
    int failures = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: levels_check <container>\n");
        return 2;
    }

    bytes = read_file(argv[1], &size);
    if (bytes == NULL) {
        fprintf(stderr, "levels_check: cannot read %s\n", argv[1]);
        return 2;
    }
    if (whd_open(&state, bytes, size) != WHD_ERR_OK) {
        fprintf(stderr, "levels_check: the container was rejected\n");
        free(bytes);
        return 2;
    }

    for (level = 0; level < LEVEL_COUNT; ++level) {
        const int marker = whd_lump_index_for_name(&state, kLevels[level]);
        doom_map_source_t source;
        doom_map_t map;
        uint32_t total = 0u;
        int raw = 1;
        int lump;
        doom_map_error_t opened;

        if (marker < 0) {
            printf("LEVEL %s status=missing\n", kLevels[level]);
            ++failures;
            continue;
        }

        memset(&source, 0, sizeof source);
        for (lump = 0; lump < MAP_LUMP_COUNT; ++lump) {
            const int index = marker + 1 + lump;
            const int32_t bytesInLump = whd_lump_size(&state, index);
            const uint8_t* data = NULL;
            uint32_t viewSize = 0u;

            if (bytesInLump < 0
                || whd_lump_view(&state, index, &data, &viewSize) != WHD_ERR_OK
                || viewSize != (uint32_t)bytesInLump) {
                raw = 0;
                continue;
            }
            source.data[lump] = data;
            source.size[lump] = (size_t)bytesInLump;
            total += (uint32_t)bytesInLump;
        }

        opened = doom_map_open(&map, &source);
        if (!raw || opened != DOOM_MAP_OK) {
            printf("LEVEL %s status=fail raw=%d map=%d\n", kLevels[level], raw,
                   (int)opened);
            ++failures;
            continue;
        }

        printf("LEVEL %s status=ok total=%u", kLevels[level], (unsigned)total);
        for (lump = 0; lump < MAP_LUMP_COUNT; ++lump) {
            printf(" %s=%zu/%u", kLumpNames[lump], map.lumps[lump].size,
                   (unsigned)map.lumps[lump].count);
        }
        printf("\n");
    }

    free(bytes);
    return failures == 0 ? 0 : 1;
}
