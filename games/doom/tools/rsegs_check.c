/*
 * Host harness for the real WAD (D3.2): walks E1M1's BSP and renders the 20
 * sampled wall-only frames through lib/doom/r_bsp.c + r_segs.c + r_light.c,
 * then prints one hash per frame.
 *
 * This is a desktop tool, not firmware: it reads the WAD into malloc'd memory,
 * composes textures with r_data.c on demand and prints through stdio, none of
 * which the renderer does. tools/rsegs_check.py compiles it, runs it next to
 * tools/doom_ref.py --segs and diffs the two outputs.
 *
 * THE 20 FRAMES (the same rule the oracle computes, stated here so neither has
 * to be read to compare them)
 *
 *   positions  the first four THINGS records, in file order, whose type is 1, 2,
 *              3, 4 or 11 (a player or deathmatch start), padded by repeating
 *              the spawn viewpoint when fewer than four exist;
 *   angles     k * 2^32 / 5 for k = 0..4;
 *   frame      position * 5 + angle;
 *   z          the spawn rule: the floor of the subsector at (x, y) plus
 *              R_VIEWHEIGHT;
 *   surface    0xFF everywhere (an uncovered pixel), plus a per-column covered
 *              mask, cleared before each pass.
 *
 * Because the walk depends on the texture/light path only through the emitter,
 * the drawsegs come from r_bsp_render() and the pixels from r_wall_emit().
 *
 * HASH PROTOCOL (identical to doom_ref.py; stated in both so neither has to be
 * read to compare them)
 *
 *   A frame is the 320*200 pixel bytes in row-major order followed by the 320
 *   covered bytes, hashed with SHA-256. PIXELHASH covers the pixels alone and
 *   COVERHASH the mask alone, so a disagreement about which columns were
 *   written cannot hide behind an all-0xFF column.
 *
 * Output lines:
 *
 *   SEGS <map> frames=<n>
 *   frame NN: <sha256>
 *   FRAMESTAT NN drawsegs=<n> columns=<n> pixels=<n> mid=<n> top=<n> bottom=<n>
 *            pixelhash=<sha256> coverhash=<sha256>
 *   SEGS-SUMMARY frames=<n> drawsegs=<n> columns=<n> pixels=<n> mid=<n> top=<n>
 *            bottom=<n>
 *   COLUMN NN <x> <sha256>                 (only with --detail NN)
 *   ROW NN <x> <y> <pixel-hex> <covered>   (only with --column NN,X)
 *   ERROR <n>                              (only when the emitter failed)
 *
 * No map coordinate, texture byte or palette index is printed except in the
 * ROW lines, which exist to localise a mismatch and are the only place a
 * decoded byte appears; nothing of that output is committed.
 *
 * Build:
 *   gcc -std=gnu99 -O2 -Wall -Wextra -Ilib/doom -o rsegs_check \
 *       tools/rsegs_check.c lib/doom/r_segs.c lib/doom/r_light.c \
 *       lib/doom/r_bsp.c lib/doom/r_plane.c lib/doom/r_tables.c \
 *       lib/doom/r_data.c lib/doom/doom_map.c lib/doom/w_wad.c
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doom_map.h"
#include "r_bsp.h"
#include "r_data.h"
#include "r_defs.h"
#include "r_light.h"
#include "r_segs.h"
#include "r_tables.h"
#include "w_wad.h"

/* --- SHA-256 (FIPS 180-4) ------------------------------------------------ */

typedef struct sha256 {
    uint32_t state[8];
    uint64_t bits;
    uint8_t buffer[64];
    size_t used;
} sha256_t;

static const uint32_t kRound[64] = {
    0x428A2F98ul, 0x71374491ul, 0xB5C0FBCFul, 0xE9B5DBA5ul,
    0x3956C25Bul, 0x59F111F1ul, 0x923F82A4ul, 0xAB1C5ED5ul,
    0xD807AA98ul, 0x12835B01ul, 0x243185BEul, 0x550C7DC3ul,
    0x72BE5D74ul, 0x80DEB1FEul, 0x9BDC06A7ul, 0xC19BF174ul,
    0xE49B69C1ul, 0xEFBE4786ul, 0x0FC19DC6ul, 0x240CA1CCul,
    0x2DE92C6Ful, 0x4A7484AAul, 0x5CB0A9DCul, 0x76F988DAul,
    0x983E5152ul, 0xA831C66Dul, 0xB00327C8ul, 0xBF597FC7ul,
    0xC6E00BF3ul, 0xD5A79147ul, 0x06CA6351ul, 0x14292967ul,
    0x27B70A85ul, 0x2E1B2138ul, 0x4D2C6DFCul, 0x53380D13ul,
    0x650A7354ul, 0x766A0ABBul, 0x81C2C92Eul, 0x92722C85ul,
    0xA2BFE8A1ul, 0xA81A664Bul, 0xC24B8B70ul, 0xC76C51A3ul,
    0xD192E819ul, 0xD6990624ul, 0xF40E3585ul, 0x106AA070ul,
    0x19A4C116ul, 0x1E376C08ul, 0x2748774Cul, 0x34B0BCB5ul,
    0x391C0CB3ul, 0x4ED8AA4Aul, 0x5B9CCA4Ful, 0x682E6FF3ul,
    0x748F82EEul, 0x78A5636Ful, 0x84C87814ul, 0x8CC70208ul,
    0x90BEFFFAul, 0xA4506CEBul, 0xBEF9A3F7ul, 0xC67178F2ul
};

static uint32_t rotateRight(uint32_t value, unsigned int count) {
    return (value >> count) | (value << (32u - count));
}

static void sha256Block(sha256_t* context, const uint8_t* block) {
    uint32_t words[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    unsigned int index;

    for (index = 0; index < 16u; ++index) {
        words[index] = ((uint32_t)block[index * 4u] << 24)
                     | ((uint32_t)block[index * 4u + 1u] << 16)
                     | ((uint32_t)block[index * 4u + 2u] << 8)
                     | (uint32_t)block[index * 4u + 3u];
    }
    for (index = 16u; index < 64u; ++index) {
        const uint32_t s0 = rotateRight(words[index - 15u], 7u)
                          ^ rotateRight(words[index - 15u], 18u)
                          ^ (words[index - 15u] >> 3);
        const uint32_t s1 = rotateRight(words[index - 2u], 17u)
                          ^ rotateRight(words[index - 2u], 19u)
                          ^ (words[index - 2u] >> 10);
        words[index] = words[index - 16u] + s0 + words[index - 7u] + s1;
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];

    for (index = 0; index < 64u; ++index) {
        const uint32_t s1 = rotateRight(e, 6u) ^ rotateRight(e, 11u) ^ rotateRight(e, 25u);
        const uint32_t choose = (e & f) ^ (~e & g);
        const uint32_t temp1 = h + s1 + choose + kRound[index] + words[index];
        const uint32_t s0 = rotateRight(a, 2u) ^ rotateRight(a, 13u) ^ rotateRight(a, 22u);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = s0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

static void sha256Init(sha256_t* context) {
    context->state[0] = 0x6A09E667ul;
    context->state[1] = 0xBB67AE85ul;
    context->state[2] = 0x3C6EF372ul;
    context->state[3] = 0xA54FF53Aul;
    context->state[4] = 0x510E527Ful;
    context->state[5] = 0x9B05688Cul;
    context->state[6] = 0x1F83D9ABul;
    context->state[7] = 0x5BE0CD19ul;
    context->bits = 0u;
    context->used = 0u;
}

static void sha256Update(sha256_t* context, const void* data, size_t size) {
    const uint8_t* bytes = (const uint8_t*)data;
    size_t index = 0u;

    context->bits += (uint64_t)size * 8u;

    while (index < size) {
        const size_t room = 64u - context->used;
        const size_t take = (size - index) < room ? (size - index) : room;

        memcpy(context->buffer + context->used, bytes + index, take);
        context->used += take;
        index += take;

        if (context->used == 64u) {
            sha256Block(context, context->buffer);
            context->used = 0u;
        }
    }
}

static void sha256Final(sha256_t* context, uint8_t out[32]) {
    const uint64_t bits = context->bits;
    static const uint8_t pad = 0x80u;
    uint8_t zero = 0u;
    uint8_t length[8];
    unsigned int index;

    sha256Update(context, &pad, 1u);
    while (context->used != 56u) {
        sha256Update(context, &zero, 1u);
    }
    for (index = 0; index < 8u; ++index) {
        length[7u - index] = (uint8_t)((bits >> (index * 8u)) & 0xFFu);
    }
    sha256Update(context, length, sizeof length);

    for (index = 0; index < 8u; ++index) {
        out[index * 4u] = (uint8_t)((context->state[index] >> 24) & 0xFFu);
        out[index * 4u + 1u] = (uint8_t)((context->state[index] >> 16) & 0xFFu);
        out[index * 4u + 2u] = (uint8_t)((context->state[index] >> 8) & 0xFFu);
        out[index * 4u + 3u] = (uint8_t)(context->state[index] & 0xFFu);
    }
}

static void digestToHex(const uint8_t digest[32], char hex[65]) {
    static const char digits[] = "0123456789abcdef";
    unsigned int index;

    for (index = 0; index < 32u; ++index) {
        hex[index * 2u] = digits[(digest[index] >> 4) & 0x0Fu];
        hex[index * 2u + 1u] = digits[digest[index] & 0x0Fu];
    }
    hex[64] = '\0';
}

/* Three published vectors plus the million-'a' one, so a broken round function,
 * a broken padding rule or a broken 64-bit length would be caught before any
 * comparison is trusted. */
static int sha256SelfTest(void) {
    static const char* vectors[] = {
        "",
        "abc",
        "The quick brown fox jumps over the lazy dog",
    };
    static const char* expected[] = {
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592",
    };
    static const char millionExpected[] =
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";
    char hex[65];
    unsigned int index;
    sha256_t context;
    uint8_t digest[32];

    for (index = 0; index < 3u; ++index) {
        sha256_t one;
        sha256Init(&one);
        sha256Update(&one, vectors[index], strlen(vectors[index]));
        sha256Final(&one, digest);
        digestToHex(digest, hex);
        if (strcmp(hex, expected[index]) != 0) {
            return 0;
        }
    }

    sha256Init(&context);
    {
        uint8_t chunk[1000];
        memset(chunk, 'a', sizeof chunk);
        for (index = 0; index < 1000u; ++index) {
            sha256Update(&context, chunk, sizeof chunk);
        }
    }
    sha256Final(&context, digest);
    digestToHex(digest, hex);

    return strcmp(hex, millionExpected) == 0;
}

/* --- Storage ------------------------------------------------------------- */

#define HARNESS_PLANES 128
#define HARNESS_TEXTURES 128

/* DOOM's stored column is 128 rows whatever the texture's declared height,
 * because R_DrawColumn() wraps with a hard-coded `& 127`. */
#define STORED_COLUMN_HEIGHT 128

static r_drawseg_t g_drawsegs[R_MAX_DRAWSEGS];
static r_visplane_t g_planes[HARNESS_PLANES];
static r_clip_range_t g_clips[R_MAX_CLIP_SEGS];
static int16_t g_floorClip[R_SCREEN_WIDTH];
static int16_t g_ceilingClip[R_SCREEN_WIDTH];
static uint8_t g_pixels[R_SCREEN_WIDTH * R_SCREEN_HEIGHT];
static uint8_t g_covered[R_SCREEN_WIDTH];

/* --- Texture residency --------------------------------------------------- */

/*
 * The harness's policy, not the renderer's: a texture is composed the first
 * time a fragment asks for it and then kept for the rest of the run. Everything
 * is malloc'd because this is a desktop tool; the emitter only ever sees the
 * r_walltex_t.
 */
typedef struct tex_entry {
    r_name_t name;
    uint8_t* composite;
    uint8_t* column;
    r_walltex_t texture;
} tex_entry_t;

typedef struct texture_cache {
    const wad_state_t* wad;
    const uint8_t* texture1;
    size_t texture1Size;
    const uint8_t* texture2;
    size_t texture2Size;
    const uint8_t* pnames;
    size_t pnamesSize;
    tex_entry_t entries[HARNESS_TEXTURES];
    int count;
} texture_cache_t;

/* r_texture_compose()'s patch resolver: a PNAMES name is a lump name. */
static r_error_t fetch_patch(void* user, const char* name, const uint8_t** data,
                             size_t* size) {
    const wad_state_t* wad = (const wad_state_t*)user;
    const int lump = W_CheckNumForName(wad, name);

    if (lump < 0) {
        return R_ERR_PATCH_FETCH;
    }
    *data = W_LumpPointer(wad, lump);
    *size = (size_t)W_LumpLength(wad, lump);
    return R_ERR_OK;
}

static void freeTextures(texture_cache_t* cache) {
    int index;

    for (index = 0; index < cache->count; ++index) {
        free(cache->entries[index].composite);
        free(cache->entries[index].column);
    }
    cache->count = 0;
}

static const r_walltex_t* resolveTexture(void* context, r_name_t name) {
    texture_cache_t* cache = (texture_cache_t*)context;
    tex_entry_t* entry;
    r_texture_info_t info;
    char text[9];
    size_t bytes;
    uint8_t* composite;
    uint8_t* column;
    int x;
    int index;

    for (index = 0; index < cache->count; ++index) {
        if (cache->entries[index].name == name) {
            return &cache->entries[index].texture;
        }
    }
    if (cache->count >= HARNESS_TEXTURES) {
        return NULL;
    }

    r_unpack_name(name, text);
    if (r_texture_lookup(cache->texture1, cache->texture1Size, cache->texture2,
                         cache->texture2Size, text, &info) != R_ERR_OK) {
        return NULL;
    }
    bytes = (size_t)info.width * (size_t)info.height;
    composite = (uint8_t*)malloc(bytes);
    column = (uint8_t*)calloc((size_t)info.width * STORED_COLUMN_HEIGHT, 1u);
    if (composite == NULL || column == NULL) {
        free(composite);
        free(column);
        return NULL;
    }
    if (r_texture_compose(&info, cache->pnames, cache->pnamesSize, fetch_patch,
                          (void*)cache->wad, composite, bytes, NULL, 0u,
                          NULL) != R_ERR_OK) {
        free(composite);
        free(column);
        return NULL;
    }

    /* Pad every column to DOOM's 128 addressable rows. The texture's own rows
     * are [0, height); the rows above it are never addressed by a texture this
     * size unless the wall is taller than the texture, and DOOM leaves those
     * undefined. */
    for (x = 0; x < info.width; ++x) {
        memcpy(column + (size_t)x * STORED_COLUMN_HEIGHT,
               composite + (size_t)x * (size_t)info.height, (size_t)info.height);
    }
    free(composite);

    entry = &cache->entries[cache->count];
    entry->name = name;
    entry->composite = NULL;
    entry->column = column;
    entry->texture.pixels = column;
    entry->texture.mask = NULL;
    entry->texture.width = (int16_t)info.width;
    entry->texture.height = (int16_t)info.height;
    entry->texture.widthMask = r_texture_width_mask(info.width);
    entry->texture.heightMask = STORED_COLUMN_HEIGHT - 1;
    cache->count += 1;
    return &entry->texture;
}

/* --- The 20 sampled viewpoints ------------------------------------------- */

typedef struct frame_view {
    fixed_t x;
    fixed_t y;
    fixed_t z;
    angle_t angle;
} frame_view_t;

#define FRAME_COUNT 20

static const uint8_t kSkyName[8] = {'F', '_', 'S', 'K', 'Y', '1', 0, 0};

static int isPlayerStart(int16_t type) {
    return type == 1 || type == 2 || type == 3 || type == 4 || type == 11;
}

/* The spawn rule's z: the floor of the subsector at (x, y) plus VIEWHEIGHT. */
static int positionView(const doom_map_t* map, fixed_t x, fixed_t y,
                        frame_view_t* out) {
    uint16_t subsector;
    uint16_t sector;
    doom_subsector_t sub;
    doom_sector_t record;

    if (r_bsp_point_in_subsector(map, x, y, &subsector) != R_BSP_OK) {
        return 0;
    }
    doom_map_get_subsector(map, subsector, &sub);
    sector = doom_map_subsector_sector(map, &sub);
    doom_map_get_sector(map, sector, &record);
    out->x = x;
    out->y = y;
    out->z = record.floorHeight + R_VIEWHEIGHT;
    out->angle = 0;
    return 1;
}

static int buildFrames(const doom_map_t* map, frame_view_t frames[FRAME_COUNT]) {
    frame_view_t positions[4];
    r_view_params_t spawn;
    doom_thing_t thing;
    uint32_t index;
    int found = 0;
    int position;
    int k;

    for (index = 0; index < map->lumps[DOOM_MAP_THINGS].count && found < 4; ++index) {
        doom_map_get_thing(map, index, &thing);
        if (!isPlayerStart(thing.type)) {
            continue;
        }
        if (!positionView(map, (fixed_t)((uint32_t)(uint16_t)thing.x << FRACBITS),
                          (fixed_t)((uint32_t)(uint16_t)thing.y << FRACBITS),
                          &positions[found])) {
            return 0;
        }
        ++found;
    }
    if (found < 4) {
        if (r_bsp_spawn_view(map, 0, &spawn) != R_BSP_OK) {
            return 0;
        }
        while (found < 4) {
            positions[found].x = spawn.x;
            positions[found].y = spawn.y;
            positions[found].z = spawn.z;
            positions[found].angle = 0;
            ++found;
        }
    }

    for (position = 0; position < 4; ++position) {
        for (k = 0; k < 5; ++k) {
            frame_view_t* frame = &frames[position * 5 + k];

            *frame = positions[position];
            frame->angle = (angle_t)(((uint64_t)k << 32) / 5u);
        }
    }
    return 1;
}

/* --- Hashing ------------------------------------------------------------- */

static void hashFrame(const uint8_t* pixels, size_t pixelBytes, const uint8_t* covered,
                      size_t coveredBytes, char combined[65], char pixelHash[65],
                      char coverHash[65]) {
    sha256_t context;
    uint8_t digest[32];

    sha256Init(&context);
    sha256Update(&context, pixels, pixelBytes);
    sha256Update(&context, covered, coveredBytes);
    sha256Final(&context, digest);
    digestToHex(digest, combined);

    sha256Init(&context);
    sha256Update(&context, pixels, pixelBytes);
    sha256Final(&context, digest);
    digestToHex(digest, pixelHash);

    sha256Init(&context);
    sha256Update(&context, covered, coveredBytes);
    sha256Final(&context, digest);
    digestToHex(digest, coverHash);
}

/* --- main ---------------------------------------------------------------- */

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

int main(int argc, char** argv) {
    wad_state_t wad;
    doom_map_source_t source;
    doom_map_t map;
    r_view_storage_t storage;
    r_view_state_t view;
    r_draw_surface_t surface;
    r_light_t light;
    texture_cache_t cache;
    r_tex_source_t texSource;
    frame_view_t frames[FRAME_COUNT];
    uint8_t* bytes;
    size_t size = 0;
    int marker;
    int index;
    int colormapLump;
    int texture1Lump;
    int pnamesLump;
    int detail = -1;
    int detailFrame = -1;
    int detailColumn = -1;
    int frame;
    char combined[65];
    char pixelHash[65];
    char coverHash[65];
    uint32_t totalDrawsegs = 0u;
    uint32_t totalColumns = 0u;
    uint32_t totalPixels = 0u;
    uint32_t totalMid = 0u;
    uint32_t totalTop = 0u;
    uint32_t totalBottom = 0u;

    if (argc < 2) {
        fprintf(stderr, "usage: rsegs_check <path-to-WAD> [--detail N] "
                        "[--column N,X]\n");
        return 2;
    }
    for (index = 2; index < argc; ++index) {
        if (strcmp(argv[index], "--detail") == 0 && index + 1 < argc) {
            detail = atoi(argv[++index]);
        } else if (strcmp(argv[index], "--column") == 0 && index + 1 < argc) {
            if (sscanf(argv[++index], "%d,%d", &detailFrame, &detailColumn) != 2) {
                fprintf(stderr, "rsegs_check: --column wants FRAME,COLUMN\n");
                return 2;
            }
        } else {
            fprintf(stderr, "rsegs_check: unknown argument %s\n", argv[index]);
            return 2;
        }
    }
    if (!sha256SelfTest()) {
        fprintf(stderr, "rsegs_check: SHA-256 self test failed\n");
        return 2;
    }

    bytes = readFile(argv[1], &size);
    if (bytes == NULL) {
        fprintf(stderr, "rsegs_check: cannot read %s\n", argv[1]);
        return 2;
    }
    if (wad_open(&wad, bytes, size) != WAD_ERR_OK) {
        fprintf(stderr, "rsegs_check: not a WAD\n");
        free(bytes);
        return 2;
    }
    marker = W_CheckNumForName(&wad, "E1M1");
    colormapLump = W_CheckNumForName(&wad, "COLORMAP");
    texture1Lump = W_CheckNumForName(&wad, "TEXTURE1");
    pnamesLump = W_CheckNumForName(&wad, "PNAMES");
    if (marker < 0 || marker + 1 + DOOM_MAP_LUMP_COUNT > wad.numLumps ||
        colormapLump < 0 || texture1Lump < 0 || pnamesLump < 0) {
        fprintf(stderr, "rsegs_check: E1M1, COLORMAP, TEXTURE1 or PNAMES is missing\n");
        free(bytes);
        return 2;
    }

    memset(&cache, 0, sizeof cache);
    cache.wad = &wad;
    cache.texture1 = W_LumpPointer(&wad, texture1Lump);
    cache.texture1Size = (size_t)W_LumpLength(&wad, texture1Lump);
    index = W_CheckNumForName(&wad, "TEXTURE2");
    if (index >= 0) {
        cache.texture2 = W_LumpPointer(&wad, index);
        cache.texture2Size = (size_t)W_LumpLength(&wad, index);
    }
    cache.pnames = W_LumpPointer(&wad, pnamesLump);
    cache.pnamesSize = (size_t)W_LumpLength(&wad, pnamesLump);

    if (r_light_init(&light, W_LumpPointer(&wad, colormapLump),
                     (size_t)W_LumpLength(&wad, colormapLump)) != R_LIGHT_OK) {
        fprintf(stderr, "rsegs_check: the COLORMAP lump is not 34 x 256 B\n");
        free(bytes);
        return 2;
    }

    for (index = 0; index < DOOM_MAP_LUMP_COUNT; ++index) {
        const int lump = marker + 1 + index;

        source.data[index] = W_LumpPointer(&wad, lump);
        source.size[index] = (size_t)W_LumpLength(&wad, lump);
    }
    if (doom_map_open(&map, &source) != DOOM_MAP_OK) {
        fprintf(stderr, "rsegs_check: map rejected with doom_map error %d\n",
                (int)map.error);
        free(bytes);
        return 2;
    }

    storage.drawsegs = g_drawsegs;
    storage.drawsegCapacity = R_MAX_DRAWSEGS;
    storage.planes.planes = g_planes;
    storage.planes.capacity = HARNESS_PLANES;
    storage.planes.count = 0;
    storage.clipRanges = g_clips;
    storage.clipRangeCapacity = R_MAX_CLIP_SEGS;
    storage.floorClip = g_floorClip;
    storage.ceilingClip = g_ceilingClip;
    if (r_bsp_init(&view, &map, &storage) != R_BSP_OK) {
        fprintf(stderr, "rsegs_check: view rejected\n");
        free(bytes);
        return 2;
    }

    if (!buildFrames(&map, frames)) {
        fprintf(stderr, "rsegs_check: cannot build the 20 sampled viewpoints\n");
        free(bytes);
        return 2;
    }

    texSource.resolve = resolveTexture;
    texSource.context = &cache;
    surface.pixels = g_pixels;
    surface.width = R_SCREEN_WIDTH;
    surface.height = R_VIEW_HEIGHT;
    surface.covered = g_covered;

    printf("SEGS E1M1 frames=%d\n", FRAME_COUNT);
    for (frame = 0; frame < FRAME_COUNT; ++frame) {
        r_view_params_t params;
        r_segs_error_t result;
        uint32_t columns = 0u;
        int column;

        params.x = frames[frame].x;
        params.y = frames[frame].y;
        params.z = frames[frame].z;
        params.angle = frames[frame].angle;
        params.extralight = 0;
        params.skyPicNum = r_pack_name(kSkyName);

        if (r_bsp_render(&view, &params) != R_BSP_OK) {
            printf("ERROR %d\n", (int)view.error);
            freeTextures(&cache);
            free(bytes);
            return 2;
        }

        memset(g_pixels, 0xFF, sizeof g_pixels);
        memset(g_covered, 0, sizeof g_covered);
        result = r_wall_emit(&view, &surface, &texSource, &light);
        if (result != R_SEGS_OK) {
            printf("ERROR %d\n", (int)result);
            freeTextures(&cache);
            free(bytes);
            return 2;
        }

        for (column = 0; column < R_SCREEN_WIDTH; ++column) {
            columns += g_covered[column] != 0u ? 1u : 0u;
        }
        hashFrame(g_pixels, sizeof g_pixels, g_covered, sizeof g_covered, combined,
                  pixelHash, coverHash);
        printf("frame %02d: %s\n", frame, combined);
        printf("FRAMESTAT %02d drawsegs=%d columns=%u pixels=%u mid=%u top=%u "
               "bottom=%u pixelhash=%s coverhash=%s\n",
               frame, (int)view.drawsegCount, (unsigned)columns,
               (unsigned)(surface.tierPixels[R_DRAW_TIER_MID]
                          + surface.tierPixels[R_DRAW_TIER_TOP]
                          + surface.tierPixels[R_DRAW_TIER_BOTTOM]),
               (unsigned)surface.tierPixels[R_DRAW_TIER_MID],
               (unsigned)surface.tierPixels[R_DRAW_TIER_TOP],
               (unsigned)surface.tierPixels[R_DRAW_TIER_BOTTOM], pixelHash,
               coverHash);

        totalDrawsegs += view.drawsegCount;
        totalColumns += columns;
        totalPixels += surface.tierPixels[R_DRAW_TIER_MID]
                       + surface.tierPixels[R_DRAW_TIER_TOP]
                       + surface.tierPixels[R_DRAW_TIER_BOTTOM];
        totalMid += surface.tierPixels[R_DRAW_TIER_MID];
        totalTop += surface.tierPixels[R_DRAW_TIER_TOP];
        totalBottom += surface.tierPixels[R_DRAW_TIER_BOTTOM];

        if (detail == frame) {
            for (column = 0; column < R_SCREEN_WIDTH; ++column) {
                sha256_t context;
                uint8_t digest[32];
                char hex[65];
                int row;

                sha256Init(&context);
                for (row = 0; row < R_SCREEN_HEIGHT; ++row) {
                    const uint8_t pixel = g_pixels[row * R_SCREEN_WIDTH + column];

                    sha256Update(&context, &pixel, 1u);
                }
                sha256Update(&context, &g_covered[column], 1u);
                sha256Final(&context, digest);
                digestToHex(digest, hex);
                printf("COLUMN %02d %d %s\n", frame, column, hex);
            }
        }
        if (detailFrame == frame && detailColumn >= 0 &&
            detailColumn < R_SCREEN_WIDTH) {
            int row;

            for (row = 0; row < R_SCREEN_HEIGHT; ++row) {
                printf("ROW %02d %d %d %02x %02x\n", frame, detailColumn, row,
                       (unsigned)g_pixels[row * R_SCREEN_WIDTH + detailColumn],
                       (unsigned)g_covered[detailColumn]);
            }
        }
    }

    printf("SEGS-SUMMARY frames=%d drawsegs=%u columns=%u pixels=%u mid=%u top=%u "
           "bottom=%u\n",
           FRAME_COUNT, (unsigned)totalDrawsegs, (unsigned)totalColumns,
           (unsigned)totalPixels, (unsigned)totalMid, (unsigned)totalTop,
           (unsigned)totalBottom);

    freeTextures(&cache);
    free(bytes);
    return 0;
}
