/*
 * Host harness for the real WAD (D3.1): walks E1M1's BSP from the player start
 * through lib/doom/r_bsp.c and prints the drawsegs, the visplanes and their
 * hashes.
 *
 * This is a desktop tool, not firmware: it reads the WAD into malloc'd memory
 * and prints through stdio, neither of which the renderer does. tools/
 * rbsp_check.py compiles it, runs it next to tools/doom_ref.py --bsp and diffs
 * the two outputs.
 *
 * HASH PROTOCOL (identical to doom_ref.py; stated in both so neither has to be
 * read to compare them)
 *
 *   DRAWSEGS   for every drawseg in emission order: int32 x1, int32 x2,
 *              uint8 silhouette, all little-endian.
 *   VISPLANES  for every plane in creation order: int32 height, the eight
 *              picture-name bytes in WAD order, int16 light, int16 minX,
 *              int16 maxX, then top[320], then bottom[320].
 *   BOTH       the DRAWSEGS bytes followed by the VISPLANES bytes.
 *
 * Output lines:
 *
 *   BSP <map> x=<int32> y=<int32> z=<int32> angle=0x%08x
 *   DRAWSEG <index> <x1>,<x2>,<silhouette>
 *   DRAWSEG-COUNT <n>
 *   DRAWSEG-OVERFLOW                       (only when the walk hit its cap)
 *   VISPLANE <index> <height> <pic> <light> <minx> <maxx>
 *   VISPLANE-COUNT <n>
 *   VISPLANE-MARKED <n>
 *   HASH DRAWSEGS <sha256>
 *   HASH VISPLANES <sha256>
 *   HASH BOTH <sha256>
 *   ERROR <r_bsp_error>                    (only when the walk failed)
 *
 * No map coordinate beyond the spawn viewpoint is printed, and the viewpoint
 * is one line of a shareware map; the output is a hash protocol, not map data.
 *
 * Build:
 *   gcc -std=gnu99 -O2 -Wall -Wextra -Ilib/doom -o rbsp_check \
 *       tools/rbsp_check.c lib/doom/r_bsp.c lib/doom/r_plane.c \
 *       lib/doom/r_tables.c lib/doom/doom_map.c lib/doom/w_wad.c
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doom_map.h"
#include "r_bsp.h"
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


/* --- Map source from the WAD -------------------------------------------- */


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

/* --- Storage: one view, the DOOM capacities ------------------------------ */

static r_drawseg_t g_drawsegs[R_MAX_DRAWSEGS];
static r_visplane_t g_planes[128];
static r_clip_range_t g_clips[R_MAX_CLIP_SEGS];
static int16_t g_floorClip[R_SCREEN_WIDTH];
static int16_t g_ceilingClip[R_SCREEN_WIDTH];

static void printPlaneName(r_name_t packed) {
    char name[9];
    int length;

    r_unpack_name(packed, name);
    length = 8;
    while (length > 0 && name[length - 1] == '\0') {
        --length;
    }
    printf("%.*s", length, name);
}

int main(int argc, char** argv) {
    wad_state_t wad;
    doom_map_source_t source;
    doom_map_t map;
    r_view_params_t params;
    r_view_storage_t storage;
    r_view_state_t view;
    uint8_t* bytes;
    size_t size = 0;
    int marker;
    int index;
    char hex[65];
    char drawsegHex[65];
    uint8_t digest[32];
    sha256_t context;
    int marked = 0;

    (void)argc;
    if (argc != 2) {
        fprintf(stderr, "usage: rbsp_check <path-to-WAD>\n");
        return 2;
    }
    if (!sha256SelfTest()) {
        fprintf(stderr, "rbsp_check: SHA-256 self test failed\n");
        return 2;
    }

    bytes = readFile(argv[1], &size);
    if (bytes == NULL) {
        fprintf(stderr, "rbsp_check: cannot read %s\n", argv[1]);
        return 2;
    }
    if (wad_open(&wad, bytes, size) != WAD_ERR_OK) {
        fprintf(stderr, "rbsp_check: not a WAD\n");
        free(bytes);
        return 2;
    }
    marker = W_CheckNumForName(&wad, "E1M1");
    if (marker < 0 || marker + 1 + DOOM_MAP_LUMP_COUNT > wad.numLumps) {
        fprintf(stderr, "rbsp_check: E1M1 is not in this WAD\n");
        free(bytes);
        return 2;
    }

    for (index = 0; index < DOOM_MAP_LUMP_COUNT; ++index) {
        const int lump = marker + 1 + index;

        source.data[index] = W_LumpPointer(&wad, lump);
        source.size[index] = (size_t)W_LumpLength(&wad, lump);
    }
    if (doom_map_open(&map, &source) != DOOM_MAP_OK) {
        fprintf(stderr, "rbsp_check: map rejected with doom_map error %d\n",
                (int)map.error);
        free(bytes);
        return 2;
    }

    if (r_bsp_spawn_view(&map, 0, &params) != R_BSP_OK) {
        fprintf(stderr, "rbsp_check: no spawn viewpoint\n");
        free(bytes);
        return 2;
    }

    storage.drawsegs = g_drawsegs;
    storage.drawsegCapacity = R_MAX_DRAWSEGS;
    storage.planes.planes = g_planes;
    storage.planes.capacity = 128;
    storage.planes.count = 0;
    storage.clipRanges = g_clips;
    storage.clipRangeCapacity = R_MAX_CLIP_SEGS;
    storage.floorClip = g_floorClip;
    storage.ceilingClip = g_ceilingClip;

    if (r_bsp_init(&view, &map, &storage) != R_BSP_OK) {
        fprintf(stderr, "rbsp_check: view rejected\n");
        free(bytes);
        return 2;
    }
    if (r_bsp_render(&view, &params) != R_BSP_OK) {
        printf("ERROR %d\n", (int)view.error);
    }

    printf("BSP E1M1 x=%d y=%d z=%d angle=0x%08x\n", (int)params.x, (int)params.y,
           (int)params.z, (unsigned)params.angle);

    /* Drawsegs and their hash. */
    sha256Init(&context);
    for (index = 0; index < (int)view.drawsegCount; ++index) {
        const r_drawseg_t* drawseg = &g_drawsegs[index];
        uint8_t record[9];
        int at = 0;
        int byte;

        for (byte = 0; byte < 4; ++byte) {
            record[at++] = (uint8_t)((uint16_t)drawseg->x1 >> (byte * 8));
        }
        for (byte = 0; byte < 4; ++byte) {
            record[at++] = (uint8_t)((uint16_t)drawseg->x2 >> (byte * 8));
        }
        record[at++] = drawseg->silhouette;
        sha256Update(&context, record, (size_t)at);

        printf("DRAWSEG %d %d,%d,%u\n", index, (int)drawseg->x1, (int)drawseg->x2,
               (unsigned)drawseg->silhouette);
    }
    printf("DRAWSEG-COUNT %d\n", (int)view.drawsegCount);
    if (view.drawsegOverflowed) {
        printf("DRAWSEG-OVERFLOW\n");
    }
    sha256Final(&context, digest);
    digestToHex(digest, drawsegHex);

    /* Visplanes and their hash. */
    sha256Init(&context);
    for (index = 0; index < (int)view.storage.planes.count; ++index) {
        const r_visplane_t* plane = &view.storage.planes.planes[index];
        uint8_t record[18];
        int at = 0;
        int byte;

        record[at++] = (uint8_t)(plane->height & 0xff);
        record[at++] = (uint8_t)((uint32_t)plane->height >> 8);
        record[at++] = (uint8_t)((uint32_t)plane->height >> 16);
        record[at++] = (uint8_t)((uint32_t)plane->height >> 24);
        for (byte = 7; byte >= 0; --byte) {
            record[at++] = (uint8_t)(plane->picNum >> (byte * 8));
        }
        record[at++] = (uint8_t)((uint16_t)plane->lightLevel & 0xff);
        record[at++] = (uint8_t)((uint16_t)plane->lightLevel >> 8);
        record[at++] = (uint8_t)((uint16_t)plane->minX & 0xff);
        record[at++] = (uint8_t)((uint16_t)plane->minX >> 8);
        record[at++] = (uint8_t)((uint16_t)plane->maxX & 0xff);
        record[at++] = (uint8_t)((uint16_t)plane->maxX >> 8);
        sha256Update(&context, record, (size_t)at);
        sha256Update(&context, plane->top, R_SCREEN_WIDTH);
        sha256Update(&context, plane->bottom, R_SCREEN_WIDTH);

        printf("VISPLANE %d %d ", index, (int)plane->height);
        printPlaneName(plane->picNum);
        printf(" %d %d %d\n", (int)plane->lightLevel, (int)plane->minX,
               (int)plane->maxX);
        if (plane->minX <= plane->maxX) {
            ++marked;
        }
    }
    printf("VISPLANE-COUNT %d\n", (int)view.storage.planes.count);
    printf("VISPLANE-MARKED %d\n", marked);
    sha256Final(&context, digest);
    digestToHex(digest, hex);
    printf("HASH DRAWSEGS %s\n", drawsegHex);
    printf("HASH VISPLANES %s\n", hex);

    /* Combined hash: the two byte streams, in order. */
    sha256Init(&context);
    for (index = 0; index < (int)view.drawsegCount; ++index) {
        const r_drawseg_t* drawseg = &g_drawsegs[index];
        uint8_t record[9];
        int at = 0;
        int byte;

        for (byte = 0; byte < 4; ++byte) {
            record[at++] = (uint8_t)((uint16_t)drawseg->x1 >> (byte * 8));
        }
        for (byte = 0; byte < 4; ++byte) {
            record[at++] = (uint8_t)((uint16_t)drawseg->x2 >> (byte * 8));
        }
        record[at++] = drawseg->silhouette;
        sha256Update(&context, record, (size_t)at);
    }
    for (index = 0; index < (int)view.storage.planes.count; ++index) {
        const r_visplane_t* plane = &view.storage.planes.planes[index];
        uint8_t record[18];
        int at = 0;
        int byte;

        record[at++] = (uint8_t)(plane->height & 0xff);
        record[at++] = (uint8_t)((uint32_t)plane->height >> 8);
        record[at++] = (uint8_t)((uint32_t)plane->height >> 16);
        record[at++] = (uint8_t)((uint32_t)plane->height >> 24);
        for (byte = 7; byte >= 0; --byte) {
            record[at++] = (uint8_t)(plane->picNum >> (byte * 8));
        }
        record[at++] = (uint8_t)((uint16_t)plane->lightLevel & 0xff);
        record[at++] = (uint8_t)((uint16_t)plane->lightLevel >> 8);
        record[at++] = (uint8_t)((uint16_t)plane->minX & 0xff);
        record[at++] = (uint8_t)((uint16_t)plane->minX >> 8);
        record[at++] = (uint8_t)((uint16_t)plane->maxX & 0xff);
        record[at++] = (uint8_t)((uint16_t)plane->maxX >> 8);
        sha256Update(&context, record, (size_t)at);
        sha256Update(&context, plane->top, R_SCREEN_WIDTH);
        sha256Update(&context, plane->bottom, R_SCREEN_WIDTH);
    }
    sha256Final(&context, digest);
    digestToHex(digest, hex);
    printf("HASH BOTH %s\n", hex);

    free(bytes);
    return 0;
}
