/*
 * The baked-graphics referee: composes every baked texture from the container's
 * own patches through lib/doom/r_data.c and compares the result byte for byte
 * with the raw payload the encoder wrote.
 *
 * WHY THIS IS C AND NOT PYTHON
 *
 * tools/bake_graphics.py has its own composer so the encoder cannot share a bug
 * with tools/doom_ref.py. A second Python composer would still be a second
 * Python composer. The only implementation that can prove the bake is the one
 * the board runs: this harness links lib/doom/r_data.c (the C decoder) and
 * lib/doom/whd.c (the container reader), decodes the patches out of the
 * container itself, composes, expands to the 128-row stored form, and compares
 * pixels and mask.
 *
 * INPUT
 *
 * One PWHD container. The manifest lump "~PWBAKE~" says what was baked; the
 * container still holds TEXTURE1, PNAMES and every patch the baked textures
 * name. Nothing else is read and nothing is written.
 *
 * OUTPUT
 *
 * One BAKECHECK line with the counts, and one MISMATCH line per difference with
 * the first differing byte offset. Exit 0 when every baked texture matched,
 * 1 when at least one differed, 2 when the container could not be read.
 *
 * This file is compiled by tools/bake_graphics.py, not by PlatformIO: it lives
 * under test/unit/test_baked/tool/ and env:baked_test ignores that directory.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "r_data.h"
#include "whd.h"

#define MANIFEST_NAME "~PWBAKE~"
#define MANIFEST_MAGIC_0 'P'
#define MANIFEST_MAGIC_1 'W'
#define MANIFEST_MAGIC_2 'B'
#define MANIFEST_MAGIC_3 'K'
#define MANIFEST_VERSION 1u
#define MANIFEST_HEADER_SIZE 16u
#define MANIFEST_RECORD_SIZE 20u

#define KIND_WALL 1u
#define KIND_SKY 2u
#define KIND_FLAT 3u
#define KIND_SPRITE 4u
#define FLAG_MASK 0x01u

#define STORED_ROWS 128u
#define FLAT_BYTES 4096u
#define PATCH_SCRATCH_BYTES 68168u

typedef struct manifest_entry {
    char name[9];
    uint16_t width;
    uint16_t rows;
    uint8_t flags;
    uint8_t kind;
    uint32_t lumpIndex;
} manifest_entry_t;

static uint32_t le16(const uint8_t* at) {
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8);
}

static uint32_t le32(const uint8_t* at) {
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8)
           | ((uint32_t)at[2] << 16) | ((uint32_t)at[3] << 24);
}

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

typedef struct fetch_context {
    const whd_state_t* state;
    uint8_t* scratch;
    size_t scratchBytes;
} fetch_context_t;

static r_error_t fetch_patch(void* user, const char* name, const uint8_t** data,
                             size_t* size) {
    fetch_context_t* context = (fetch_context_t*)user;
    const int lump = whd_lump_index_for_name(context->state, name);
    uint32_t required = 0u;

    if (lump < 0) {
        return R_ERR_PATCH_FETCH;
    }
    if (whd_read_lump(context->state, lump, context->scratch, context->scratchBytes,
                      &required) != WHD_ERR_OK) {
        return R_ERR_PATCH_FETCH;
    }
    *data = context->scratch;
    *size = (size_t)required;
    return R_ERR_OK;
}

/* Reads a lump into a fresh buffer. The caller owns the result. */
static uint8_t* read_lump(const whd_state_t* state, int lump, size_t* size) {
    const int32_t declared = whd_lump_size(state, lump);
    uint32_t required = 0u;
    uint8_t* bytes;

    if (declared <= 0) {
        return NULL;
    }
    bytes = (uint8_t*)malloc((size_t)declared);
    if (bytes == NULL) {
        return NULL;
    }
    if (whd_read_lump(state, lump, bytes, (size_t)declared, &required) != WHD_ERR_OK
        || required != (uint32_t)declared) {
        free(bytes);
        return NULL;
    }
    *size = (size_t)declared;
    return bytes;
}

static int parse_manifest(const uint8_t* data, size_t size, manifest_entry_t** entries,
                          uint32_t* count) {
    uint32_t recordSize;
    uint32_t reserved;
    uint32_t index;
    uint32_t version;

    if (size < MANIFEST_HEADER_SIZE) {
        return 0;
    }
    if (data[0] != MANIFEST_MAGIC_0 || data[1] != MANIFEST_MAGIC_1
        || data[2] != MANIFEST_MAGIC_2 || data[3] != MANIFEST_MAGIC_3) {
        return 0;
    }
    version = le16(data + 4);
    recordSize = le16(data + 6);
    *count = le32(data + 8);
    reserved = le32(data + 12);
    if (version != MANIFEST_VERSION || recordSize != MANIFEST_RECORD_SIZE || reserved != 0u) {
        return 0;
    }
    if (MANIFEST_HEADER_SIZE + (size_t)*count * MANIFEST_RECORD_SIZE > size) {
        return 0;
    }

    *entries = (manifest_entry_t*)calloc(*count, sizeof(manifest_entry_t));
    if (*entries == NULL) {
        return 0;
    }
    for (index = 0u; index < *count; ++index) {
        const uint8_t* at = data + MANIFEST_HEADER_SIZE + index * MANIFEST_RECORD_SIZE;
        manifest_entry_t* entry = &(*entries)[index];
        memcpy(entry->name, at, 8u);
        entry->name[8] = '\0';
        entry->width = (uint16_t)le16(at + 8);
        entry->rows = (uint16_t)le16(at + 10);
        entry->flags = at[12];
        entry->kind = at[13];
        entry->lumpIndex = le32(at + 16);
    }
    return 1;
}

/* The expansion the encoder performs: column-major, 128 rows, zero fill above
 * the declared height, and mask 1 in that fill because it is opaque. */
static void expand(const uint8_t* pixels, const uint8_t* mask, uint32_t width,
                   uint32_t height, int hasMask, uint8_t* stored, uint8_t* storedMask) {
    uint32_t x;

    memset(stored, 0, (size_t)width * STORED_ROWS);
    if (hasMask) {
        memset(storedMask, 0, (size_t)width * STORED_ROWS);
    }
    for (x = 0u; x < width; ++x) {
        const size_t source = (size_t)x * height;
        const size_t destination = (size_t)x * STORED_ROWS;
        memcpy(stored + destination, pixels + source, height);
        if (hasMask) {
            memcpy(storedMask + destination, mask + source, height);
            memset(storedMask + destination + height, 1,
                   (size_t)STORED_ROWS - height);
        }
    }
}

static int compare_bytes(const char* what, const char* name, const uint8_t* baked,
                         const uint8_t* decoded, size_t size, size_t base) {
    size_t index;
    for (index = 0u; index < size; ++index) {
        if (baked[index] != decoded[index]) {
            printf("MISMATCH kind=%s name=%s offset=%lu baked=%02x decoded=%02x\n",
                   what, name, (unsigned long)(base + index),
                   (unsigned)baked[index], (unsigned)decoded[index]);
            return 0;
        }
    }
    return 1;
}

int main(int argc, char** argv) {
    size_t containerBytes = 0u;
    uint8_t* container;
    whd_state_t state;
    manifest_entry_t* entries = NULL;
    uint32_t count = 0u;
    uint8_t* texture1 = NULL;
    size_t texture1Bytes = 0u;
    uint8_t* pnames = NULL;
    size_t pnamesBytes = 0u;
    uint8_t* scratch = NULL;
    int textureLump;
    int pnamesLump;
    int manifestLump;
    const uint8_t* manifestData = NULL;
    uint32_t manifestBytes = 0u;
    uint32_t index;
    uint32_t walls = 0u;
    uint32_t skies = 0u;
    uint32_t flats = 0u;
    uint32_t sprites = 0u;
    uint32_t compared = 0u;
    uint32_t mismatches = 0u;
    uint64_t bakedBytes = 0u;

    if (argc != 2) {
        fprintf(stderr, "usage: bake_check <container>\n");
        return 2;
    }
    container = read_file(argv[1], &containerBytes);
    if (container == NULL) {
        fprintf(stderr, "bake_check: cannot read %s\n", argv[1]);
        return 2;
    }
    if (whd_open(&state, container, containerBytes) != WHD_ERR_OK) {
        fprintf(stderr, "bake_check: the container does not open\n");
        free(container);
        return 2;
    }

    manifestLump = whd_lump_index_for_name(&state, MANIFEST_NAME);
    if (manifestLump < 0
        || whd_lump_view(&state, manifestLump, &manifestData, &manifestBytes) != WHD_ERR_OK) {
        fprintf(stderr, "bake_check: no raw " MANIFEST_NAME " lump\n");
        free(container);
        return 2;
    }
    if (!parse_manifest(manifestData, manifestBytes, &entries, &count)) {
        fprintf(stderr, "bake_check: the manifest is malformed\n");
        free(container);
        return 2;
    }

    textureLump = whd_lump_index_for_name(&state, "TEXTURE1");
    pnamesLump = whd_lump_index_for_name(&state, "PNAMES");
    if (textureLump < 0 || pnamesLump < 0) {
        fprintf(stderr, "bake_check: TEXTURE1 or PNAMES is missing\n");
        free(entries);
        free(container);
        return 2;
    }
    texture1 = read_lump(&state, textureLump, &texture1Bytes);
    pnames = read_lump(&state, pnamesLump, &pnamesBytes);
    scratch = (uint8_t*)malloc(PATCH_SCRATCH_BYTES);
    if (texture1 == NULL || pnames == NULL || scratch == NULL) {
        fprintf(stderr, "bake_check: cannot allocate the decode scratch\n");
        free(texture1);
        free(pnames);
        free(scratch);
        free(entries);
        free(container);
        return 2;
    }

    for (index = 0u; index < count; ++index) {
        const manifest_entry_t* entry = &entries[index];
        const uint8_t* baked = NULL;
        uint32_t bakedSize = 0u;

        if (whd_lump_view(&state, (int)entry->lumpIndex, &baked, &bakedSize) != WHD_ERR_OK) {
            printf("MISMATCH kind=payload name=%s offset=0 baked=00 decoded=00\n",
                   entry->name);
            ++mismatches;
            continue;
        }
        bakedBytes += bakedSize;

        if (entry->kind == KIND_WALL || entry->kind == KIND_SKY) {
            r_texture_info_t info;
            const uint32_t width = entry->width;
            const uint32_t height = entry->rows;
            const int hasMask = (entry->flags & FLAG_MASK) != 0u;
            const size_t surfaceBytes = (size_t)width * height;
            const size_t expectedBytes = (size_t)width * STORED_ROWS * (hasMask ? 2u : 1u);
            uint8_t* pixels = (uint8_t*)malloc(surfaceBytes);
            uint8_t* mask = hasMask ? (uint8_t*)malloc(surfaceBytes) : NULL;
            uint8_t* stored = (uint8_t*)malloc((size_t)width * STORED_ROWS);
            uint8_t* storedMask = hasMask
                                      ? (uint8_t*)malloc((size_t)width * STORED_ROWS) : NULL;
            fetch_context_t fetchContext;
            size_t required = 0u;
            int matched = 1;

            if (entry->kind == KIND_WALL) {
                ++walls;
            } else {
                ++skies;
            }
            ++compared;

            if (pixels == NULL || stored == NULL || (hasMask && (mask == NULL || storedMask == NULL))) {
                printf("MISMATCH kind=alloc name=%s offset=0 baked=00 decoded=00\n",
                       entry->name);
                ++mismatches;
                free(pixels);
                free(mask);
                free(stored);
                free(storedMask);
                continue;
            }

            if (r_texture_lookup(texture1, texture1Bytes, NULL, 0u, entry->name, &info)
                != R_ERR_OK
                || info.width != (int32_t)entry->width
                || info.height != (int32_t)entry->rows) {
                printf("MISMATCH kind=lookup name=%s offset=0 baked=00 decoded=00\n",
                       entry->name);
                ++mismatches;
                free(pixels);
                free(mask);
                free(stored);
                free(storedMask);
                continue;
            }
            fetchContext.state = &state;
            fetchContext.scratch = scratch;
            fetchContext.scratchBytes = PATCH_SCRATCH_BYTES;
            if (r_texture_compose(&info, pnames, pnamesBytes, fetch_patch, &fetchContext,
                                  pixels, surfaceBytes, mask, hasMask ? surfaceBytes : 0u,
                                  &required) != R_ERR_OK
                || required != surfaceBytes) {
                printf("MISMATCH kind=compose name=%s offset=0 baked=00 decoded=00\n",
                       entry->name);
                ++mismatches;
                free(pixels);
                free(mask);
                free(stored);
                free(storedMask);
                continue;
            }
            expand(pixels, mask, width, height, hasMask, stored, storedMask);
            if (bakedSize != expectedBytes) {
                printf("MISMATCH kind=size name=%s offset=%lu baked=%lu decoded=%lu\n",
                       entry->name, (unsigned long)expectedBytes,
                       (unsigned long)bakedSize, (unsigned long)expectedBytes);
                ++mismatches;
                matched = 0;
            } else {
                matched = compare_bytes(entry->kind == KIND_WALL ? "wall" : "sky",
                                        entry->name, baked, stored,
                                        (size_t)width * STORED_ROWS, 0u);
                if (matched && hasMask) {
                    matched = compare_bytes("mask", entry->name,
                                            baked + (size_t)width * STORED_ROWS, storedMask,
                                            (size_t)width * STORED_ROWS,
                                            (size_t)width * STORED_ROWS);
                }
            }
            if (!matched) {
                ++mismatches;
            }
            free(pixels);
            free(mask);
            free(stored);
            free(storedMask);
        } else if (entry->kind == KIND_FLAT) {
            ++flats;
            ++compared;
            if (bakedSize != FLAT_BYTES || r_flat_validate(baked, bakedSize) != R_ERR_OK) {
                printf("MISMATCH kind=flat name=%s offset=0 baked=%lu decoded=%lu\n",
                       entry->name, (unsigned long)bakedSize, (unsigned long)FLAT_BYTES);
                ++mismatches;
            }
        } else if (entry->kind == KIND_SPRITE) {
            r_patch_info_t info;
            ++sprites;
            ++compared;
            if (r_patch_validate(baked, bakedSize, &info) != R_ERR_OK) {
                printf("MISMATCH kind=sprite name=%s offset=0 baked=%lu decoded=%lu\n",
                       entry->name, (unsigned long)bakedSize, (unsigned long)bakedSize);
                ++mismatches;
            }
        } else {
            printf("MISMATCH kind=kind name=%s offset=0 baked=%02x decoded=00\n",
                   entry->name, (unsigned)entry->kind);
            ++mismatches;
        }
    }

    printf("BAKECHECK,entries=%lu,walls=%lu,sky=%lu,flats=%lu,sprites=%lu,"
           "compared=%lu,mismatches=%lu,bytes=%lu\n",
           (unsigned long)count, (unsigned long)walls, (unsigned long)skies,
           (unsigned long)flats, (unsigned long)sprites, (unsigned long)compared,
           (unsigned long)mismatches, (unsigned long)bakedBytes);

    free(texture1);
    free(pnames);
    free(scratch);
    free(entries);
    free(container);
    return mismatches == 0u ? 0 : 1;
}
