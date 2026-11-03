/*
 * The sprite-submission gate: the world's live mobjs, not the map's THINGS.
 *
 * WHY THIS GATE EXISTS
 *
 * A sprite list built from E1M1's THINGS records alone never submits the mobjs
 * the simulation creates after the level loads -- a shot's puff, a blood spray,
 * a rocket, a monster's pain, death and corpse frames -- so a frame that should
 * show one shows nothing, and a frame that submits an unbakeable lump is refused
 * whole.
 *
 * This tool is the gate for the live submission: the caller walks the live world
 * (p_level_first_mobj()/p_level_next_mobj()), derives each mobj's lump from its
 * own state's sprite and frame, and hands that list to the real masked pass. It
 * renders whole frames through r_bsp_render(), r_wall_emit(), r_plane_draw()
 * and r_masked_emit(), the same four passes the board runs, and asserts:
 *
 *   A. NO PIXEL MOVED AT SPAWN. The static THINGS list and
 *      a live-mobj list PINNED to each mobj's spawn-state row render the
 *      identical frame at the host harness's frame-00 viewpoint and at the
 *      player's spawn viewpoint. Pinning is the honest comparison: the static
 *      list is a load-time snapshot, so the assertion is "the live submission
 *      derives the same sprite for the same mobj at the same position", not
 *      "the panel is frozen". The light still comes from the live world (D).
 *      This is the host-side counterpart of the board's fffb3c5c / 50fb3d9c
 *      invariants.
 *   A2b. THE LIVE LIST ADVANCES A CHAIN. At tics 10 and 35 at least one
 *      submitted mobj asks for a baked frame other than its type's spawn row,
 *      and no chain frame falls back. This holds because a
 *      monster's walk/pain/death frames and an item's blink frames are baked,
 *      so the world's state machines actually change what is drawn.
 *   B. A LIVE PUFF IS SUBMITTED. The same viewpoint rendered with a live puff
 *      differs from the same viewpoint without one, and the masked pass asks
 *      the resolver for the puff's own lump (PUFFA0), not for "nothing" and not
 *      for the spawn row of some other type.
 *   C. THE STATE CHAIN ADVANCES THE FRAME. Walking the puff through S_PUFF1..
 *      S_PUFF4 requests PUFFA0, PUFFB0, PUFFC0 and PUFFD0 in that order, and
 *      each renders a different frame at a fixed position, so the change is the
 *      sprite and not the puff's z drift.
 *   D. THE LIGHT THE WORLD REPORTS IS THE LIGHT THAT DRAWS. The live list reads
 *      the level's sector (upstream R_AddSprites), not the immutable map copy,
 *      so the comparison is repeated after the world has ticked past the first
 *      sector-light thinkers.
 *
 * WHAT IT DELIBERATELY DOES NOT SHARE WITH THE BOARD
 *
 * The rule under test is "the world supplies the frame, the baked fixture says
 * which frames exist". This tool reproduces that rule in C against p_level_t
 * and tools/e1m1_sprites.inc, and exercises the real renderer. It cannot call
 * src/doomc/FrameDriver.cpp: that translation unit's board half is compiled
 * only for ESP32 and owns esp_timer and the flash mapping. The board's own
 * evidence is therefore the two frame hashes and `refused=0` in its serial log;
 * this tool is the pixel proof that the rule it implements is the right one.
 *
 * WHAT A FRAME THE FIXTURE DOES NOT NAME DOES
 *
 * The container bakes the frames E1M1's THINGS show, every state chain of the
 * mobj types it places, and the puff, blood and rocket chains. The only submission left without a row is the player avatar
 * (MT_PLAYER's S_PLAY_STND), which is deliberately not baked because it stands
 * at the viewpoint and is skipped. Any other unbaked frame still draws -- the
 * fallback is its type's spawn-state row -- and the tool counts it, so a gap is
 * a number and not a silence. A2b asserts that the count stays at that one
 * avatar: a chain frame that fell back fails the gate.
 *
 * PROVING THE GATE CAN FAIL
 *
 *     python tools/sprite_check.py --wad ... --perturb frame
 *
 * compiles the harness with SPRITE_CHECK_PERTURB_FRAME, which makes every live
 * list use each mobj's spawn-state row instead of its live frame -- the static
 * behaviour. A2b then sees zero advances, the puff stops
 * advancing, assertion C fails and the process exits 1. A gate that cannot fail
 * only proves it runs.
 *
 * USAGE
 *
 *     sprite_check <path-to-DOOM1.WAD>
 *
 * The WAD is copyrighted shareware data and is never committed. The harness is
 * compiled into a temporary directory by tools/sprite_check.py and removed
 * before the script returns. Nothing here writes to the worktree.
 *
 * EXIT CODES
 *
 *     0  every assertion passed
 *     1  at least one assertion failed
 *     2  the command line, the compiler, the WAD or the map was unusable
 *
 * Standard C99 plus the DOOM core's own C headers. No framework.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doom_map.h"
#include "p_mobj.h"
#include "p_setup.h"
#include "p_spec.h"
#include "p_user.h"
#include "r_bsp.h"
#include "r_data.h"
#include "r_defs.h"
#include "r_light.h"
#include "r_plane.h"
#include "r_segs.h"
#include "r_tables.h"
#include "r_things.h"
#include "w_wad.h"

/* The sprite fixture: DoomEd / mobjtype number -> the frame's rotation lumps.
 * Fixture data generated by tools/gen_e1m1_sprites.py; lib/doom/ never sees it. */
#include "e1m1_sprites.inc"

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

static void hashTwo(const uint8_t* first, size_t firstSize, const uint8_t* second,
                    size_t secondSize, char out[65]) {
    sha256_t context;
    uint8_t digest[32];

    sha256Init(&context);
    sha256Update(&context, first, firstSize);
    sha256Update(&context, second, secondSize);
    sha256Final(&context, digest);
    digestToHex(digest, out);
}

/* --- Storage ------------------------------------------------------------- */

#define HARNESS_PLANES 128
#define HARNESS_TEXTURES 128
#define STORED_COLUMN_HEIGHT 128

static r_drawseg_t g_drawsegs[R_MAX_DRAWSEGS];
static r_visplane_t g_planes[HARNESS_PLANES];
static r_clip_range_t g_clips[R_MAX_CLIP_SEGS];
static int16_t g_floorClip[R_SCREEN_WIDTH];
static int16_t g_ceilingClip[R_SCREEN_WIDTH];
static uint8_t g_pixels[R_SCREEN_WIDTH * R_SCREEN_HEIGHT];
static uint8_t g_planeMask[R_SCREEN_WIDTH * R_SCREEN_HEIGHT];
static uint8_t g_spriteMask[R_SCREEN_WIDTH * R_SCREEN_HEIGHT];
static uint8_t g_covered[R_SCREEN_WIDTH];

#define HARNESS_VISSPRITES 192
#define HARNESS_CLIP_BLOCKS 32
#define HARNESS_OPENING_BLOCKS 8
#define HARNESS_CLIP_POOL (HARNESS_CLIP_BLOCKS + HARNESS_OPENING_BLOCKS)
#define HARNESS_SPRITES 256

static r_vissprite_t g_visSprites[HARNESS_VISSPRITES];
static int16_t g_clipPool[HARNESS_CLIP_POOL * R_SCREEN_WIDTH];
static r_sprite_t g_sprites[HARNESS_SPRITES];

/* --- Texture residency --------------------------------------------------- */

typedef struct tex_entry {
    r_name_t name;
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
    for (x = 0; x < info.width; ++x) {
        memcpy(column + (size_t)x * STORED_COLUMN_HEIGHT,
               composite + (size_t)x * (size_t)info.height, (size_t)info.height);
    }
    free(composite);

    entry = &cache->entries[cache->count];
    entry->name = name;
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

/* --- Flat residency ------------------------------------------------------ */

#define HARNESS_FLATS 32

typedef struct flat_entry {
    r_name_t name;
    r_flat_t flat;
} flat_entry_t;

typedef struct flat_cache {
    const wad_state_t* wad;
    flat_entry_t entries[HARNESS_FLATS];
    int count;
} flat_cache_t;

static const r_flat_t* resolveFlat(void* context, r_name_t name) {
    flat_cache_t* cache = (flat_cache_t*)context;
    flat_entry_t* entry;
    char text[9];
    int lump;
    int index;

    for (index = 0; index < cache->count; ++index) {
        if (cache->entries[index].name == name) {
            return &cache->entries[index].flat;
        }
    }
    if (cache->count >= HARNESS_FLATS) {
        return NULL;
    }
    r_unpack_name(name, text);
    lump = W_CheckNumForName(cache->wad, text);
    if (lump < 0 || (size_t)W_LumpLength(cache->wad, lump) != R_FLAT_BYTES) {
        return NULL;
    }

    entry = &cache->entries[cache->count];
    entry->name = name;
    entry->flat.pixels = W_LumpPointer(cache->wad, lump);
    entry->flat.width = R_FLAT_SIZE;
    entry->flat.height = R_FLAT_SIZE;
    entry->flat.widthMask = R_FLAT_SIZE - 1;
    entry->flat.heightMask = R_FLAT_SIZE - 1;
    cache->count += 1;
    return &entry->flat;
}

/* --- Sprite residency ---------------------------------------------------- */

#define HARNESS_SPRITE_LUMPS 128
/* The names the last render actually asked the resolver for. A frame that
 * submits a puff must make the pass ask for the puff's lump; this is the
 * observable the perturbed build changes. */
#define HARNESS_RESOLVED_NAMES 128

typedef struct sprite_entry {
    r_name_t name;
    r_sprite_patch_t patch;
} sprite_entry_t;

typedef struct sprite_cache {
    const wad_state_t* wad;
    sprite_entry_t entries[HARNESS_SPRITE_LUMPS];
    char resolved[HARNESS_RESOLVED_NAMES][9];
    int resolvedCount;
    int count;
} sprite_cache_t;

static const r_sprite_patch_t* resolveSprite(void* context, r_name_t name) {
    sprite_cache_t* cache = (sprite_cache_t*)context;
    sprite_entry_t* entry;
    char text[9];
    int lump;
    int index;

    r_unpack_name(name, text);
    if (cache->resolvedCount < HARNESS_RESOLVED_NAMES) {
        memcpy(cache->resolved[cache->resolvedCount], text, 8);
        cache->resolved[cache->resolvedCount][8] = '\0';
        cache->resolvedCount += 1;
    }

    for (index = 0; index < cache->count; ++index) {
        if (cache->entries[index].name == name) {
            return &cache->entries[index].patch;
        }
    }
    if (cache->count >= HARNESS_SPRITE_LUMPS) {
        return NULL;
    }
    lump = W_CheckNumForName(cache->wad, text);
    if (lump < 0) {
        return NULL;
    }
    entry = &cache->entries[cache->count];
    entry->name = name;
    entry->patch.data = W_LumpPointer(cache->wad, lump);
    entry->patch.size = (size_t)W_LumpLength(cache->wad, lump);
    cache->count += 1;
    return &entry->patch;
}

/* --- R_PointToAngle, for the rotation rule ------------------------------- */

static angle_t nextPointToAngle(const r_view_params_t* view, fixed_t x, fixed_t y) {
    fixed_t dx = (fixed_t)((uint32_t)x - (uint32_t)view->x);
    fixed_t dy = (fixed_t)((uint32_t)y - (uint32_t)view->y);

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

/* --- The fixture's lookup rules ------------------------------------------ */

static const e1m1_sprite_rule_t* spriteRule(int type) {
    int index;

    for (index = 0; index < E1M1_SPRITE_RULE_COUNT; ++index) {
        if (kE1M1Sprites[index].type == type) {
            return &kE1M1Sprites[index];
        }
    }
    return NULL;
}

/* The row for one *frame* of one sprite. The rule: a static row per
 * type can express the spawn frame only, and the world supplies the frame. */
static const e1m1_sprite_rule_t* spriteFrameRule(const char* sprite, int frame) {
    int index;

    for (index = 0; index < E1M1_SPRITE_RULE_COUNT; ++index) {
        if (kE1M1Sprites[index].frame == (signed char)frame
            && strncmp(kE1M1Sprites[index].sprite, sprite, 4) == 0) {
            return &kE1M1Sprites[index];
        }
    }
    return NULL;
}

/* A mobj whose live frame has no baked lump falls back to its type's spawn-state
 * row: the frame a static THINGS list would show. The alternative -- refusing
 * the whole frame over one unbakeable frame -- would blank the panel. */
static const e1m1_sprite_rule_t* spawnRuleForType(int mobjType) {
    const doom_mobjinfo_t* info;
    const doom_state_t* spawn;
    int frame;

    if (mobjType < 0 || mobjType >= NUM_MOBJ_TYPES) {
        return NULL;
    }
    info = &mobjinfo[mobjType];
    if (info->spawnstate < 0 || info->spawnstate >= NUM_STATES) {
        return NULL;
    }
    spawn = &states[info->spawnstate];
    frame = (int)(spawn->frame & 0x7FFF);
    return spriteFrameRule(sprnames[spawn->sprite], frame);
}

/* --- The two sprite-list builders ---------------------------------------- */

/* The old behaviour: every THINGS record the fixture covers, on its sector's
 * floor, with the rotation R_ProjectSprite's rule selects. This is what
 * src/doomc/FrameDriver.cpp's buildSpriteBase()/buildSprites() still do for the
 * measurement probes, and it is the baseline assertion A compares against. */
static int buildStaticSprites(const doom_map_t* map, const r_view_params_t* view,
                              r_sprite_t* out, int capacity) {
    uint32_t index;
    int count = 0;

    for (index = 0; index < map->lumps[DOOM_MAP_THINGS].count; ++index) {
        doom_thing_t thing;
        const e1m1_sprite_rule_t* rule;
        uint16_t subsector;
        uint16_t sectorIndex;
        doom_subsector_t sub;
        doom_sector_t sector;
        fixed_t x;
        fixed_t y;
        angle_t angle;
        angle_t facing;
        int rotation;
        int lumpIndex;
        char lumpName[9];
        int i;

        doom_map_get_thing(map, index, &thing);
        rule = spriteRule(thing.type);
        if (rule == NULL) {
            continue;
        }
        if (count >= capacity) {
            return -1;
        }
        x = (fixed_t)((uint32_t)(uint16_t)thing.x << FRACBITS);
        y = (fixed_t)((uint32_t)(uint16_t)thing.y << FRACBITS);
        if (r_bsp_point_in_subsector(map, x, y, &subsector) != R_BSP_OK) {
            return -1;
        }
        doom_map_get_subsector(map, subsector, &sub);
        sectorIndex = doom_map_subsector_sector(map, &sub);
        doom_map_get_sector(map, sectorIndex, &sector);

        angle = ANG45 * (angle_t)(thing.angle / 45);
        facing = nextPointToAngle(view, x, y);
        rotation = (int)((facing - angle + ((ANG45 / 2u) * 9u)) >> 29);
        lumpIndex = rule->rotations ? rotation : 0;
        for (i = 0; i < 8; ++i) {
            lumpName[i] = rule->lumps[lumpIndex].name[i];
        }
        lumpName[8] = '\0';

        out[count].x = x;
        out[count].y = y;
        out[count].z = sector.floorHeight;
        out[count].angle = angle;
        out[count].lump = r_pack_name((const uint8_t*)lumpName);
        out[count].lightLevel = sector.lightLevel;
        out[count].flip = rule->lumps[lumpIndex].flip;
        out[count].fullbright = rule->fullbright;
        out[count].noClip = 0;
        ++count;
    }
    return count;
}

static int worldContainsMobj(const p_level_t* level, const r_mobj_t* wanted) {
    const r_mobj_t* mobj = p_level_first_mobj(level);

    while (mobj != NULL) {
        if (mobj == wanted) {
            return 1;
        }
        mobj = p_level_next_mobj(mobj);
    }
    return 0;
}

/* The replacement: every live mobj, removed ones skipped, its own state's
 * sprite and frame naming the lump. `skip` lets the same world render with and
 * without one mobj (the puff) so the difference is the submission alone.
 *
 * `pinSpawn` draws each mobj's type spawn-state row instead of its live frame.
 * A2 asks for that on purpose. The static THINGS list is a load-time snapshot
 * by definition: it can only ever show the spawn frame, so "the live list draws
 * the same frame as the static list" is only an honest assertion at spawn. What
 * A2 proves is that the live submission derives that same sprite for the same
 * mobj at the same position -- and the light still comes from the live world,
 * which is the half of A2 the sector-light thinkers could move.
 *
 * `advancedCount` counts the submissions whose live frame resolved to a row
 * other than the type's spawn row: the chain actually moved. A2b asserts it is
 * non-zero at tics 10 and 35, so a fixture -- or the perturbation -- that pins
 * every frame to spawn cannot pass by drawing the static list twice. */
static int buildLiveSprites(const p_level_t* level, const r_view_params_t* view,
                            r_sprite_t* out, int capacity, const r_mobj_t* skip,
                            int pinSpawn, int* fallbackCount,
                            int* advancedCount) {
    const r_mobj_t* mobj;
    int count = 0;

    *fallbackCount = 0;
    *advancedCount = 0;
#if defined(SPRITE_CHECK_PERTURB_FRAME)
    /* The failing perturbation: force the spawn row everywhere, exactly the
     * static behaviour. A2b then reports zero advances. */
    pinSpawn = 1;
#endif
    mobj = p_level_first_mobj(level);
    while (mobj != NULL) {
        const e1m1_sprite_rule_t* rule;
        const e1m1_sprite_rule_t* spawn;
        const char* spriteName;
        angle_t facing;
        int rotation;
        int frame;
        int lumpIndex;
        char lumpName[9];
        int i;

        if (mobj == skip || mobj->thinker.removed != 0u) {
            mobj = p_level_next_mobj(mobj);
            continue;
        }
        if (count >= capacity) {
            return -1;
        }

        frame = (int)(mobj->frame & 0x7FFF);
        spriteName = sprnames[mobj->sprite];
        spawn = spawnRuleForType(mobj->type);
        rule = spriteFrameRule(spriteName, frame);
        if (rule == NULL) {
            /* No baked lump for this frame: draw the type's spawn row, counted.
             * The only E1M1 mobj here is the player avatar, whose S_PLAY_STND
             * is deliberately not baked (it stands at the viewpoint). */
            *fallbackCount += 1;
            rule = spawn;
            if (rule == NULL) {
                mobj = p_level_next_mobj(mobj);
                continue;
            }
        } else if (pinSpawn) {
            /* A2's pinned list: the same mobj, the spawn frame it was placed in. */
            rule = spawn;
            if (rule == NULL) {
                mobj = p_level_next_mobj(mobj);
                continue;
            }
        } else if (rule != spawn) {
            /* The live frame is not the spawn row: a baked frame of the chain. */
            *advancedCount += 1;
        }

        facing = nextPointToAngle(view, mobj->x, mobj->y);
        rotation = (int)((facing - mobj->angle + ((ANG45 / 2u) * 9u)) >> 29);
        lumpIndex = rule->rotations ? rotation : 0;
        /* The per-mobj lump: rebuilt from this mobj's frame, never a static row
         * per type. It lives in `lumpName` for this submission, which is the
         * whole point -- two mobjs of one type can name two different lumps. */
        for (i = 0; i < 8; ++i) {
            lumpName[i] = rule->lumps[lumpIndex].name[i];
        }
        lumpName[8] = '\0';

        out[count].x = mobj->x;
        out[count].y = mobj->y;
        out[count].z = mobj->z;
        out[count].angle = mobj->angle;
        out[count].lump = r_pack_name((const uint8_t*)lumpName);
        out[count].lightLevel = level->sectors[mobj->sector].lightLevel;
        out[count].flip = rule->lumps[lumpIndex].flip;
        out[count].fullbright = (mobj->frame & 0x8000) != 0;
        out[count].noClip = 0;
        ++count;

        mobj = p_level_next_mobj(mobj);
    }
    return count;
}

/* --- One rendered frame -------------------------------------------------- */

typedef struct render_result {
    char hash[65];
    int spriteCount;
    int fallbackCount;
    int advancedCount;
    r_masked_stats_t stats;
    int error;
} render_result_t;

static int renderFrame(const doom_map_t* map, r_view_state_t* view, r_draw_surface_t* surface,
                       const r_tex_source_t* texSource, const r_light_t* light,
                       const r_view_params_t* params, const p_level_t* level,
                       const r_mobj_t* skip, int pinSpawn, sprite_cache_t* spriteCache,
                       r_sprite_source_t* spriteSource, render_result_t* out) {
    r_segs_error_t walls;
    r_plane_error_t planes;
    r_things_error_t masked;

    memset(out, 0, sizeof *out);
    if (r_bsp_render(view, params) != R_BSP_OK) {
        out->error = (int)view->error;
        return 0;
    }

    memset(g_pixels, 0xFF, sizeof g_pixels);
    memset(g_covered, 0, sizeof g_covered);
    memset(g_planeMask, 0, sizeof g_planeMask);
    memset(g_spriteMask, 0, sizeof g_spriteMask);
    surface->tierPixels[R_DRAW_TIER_MID] = 0u;
    surface->tierPixels[R_DRAW_TIER_TOP] = 0u;
    surface->tierPixels[R_DRAW_TIER_BOTTOM] = 0u;
    surface->planePixels = 0u;
    surface->planeSpans = 0u;
    surface->spritePixels = 0u;

    walls = r_wall_emit(view, surface, texSource, light);
    if (walls != R_SEGS_OK) {
        out->error = (int)walls;
        return 0;
    }
    planes = r_plane_draw(view, surface, texSource, light);
    if (planes != R_PLANE_OK) {
        out->error = (int)planes;
        return 0;
    }

    spriteCache->resolvedCount = 0;
    if (level != NULL) {
        out->spriteCount = buildLiveSprites(level, params, g_sprites, HARNESS_SPRITES, skip,
                                            pinSpawn, &out->fallbackCount,
                                            &out->advancedCount);
    } else {
        out->spriteCount = buildStaticSprites(map, params, g_sprites, HARNESS_SPRITES);
        out->fallbackCount = -1;
        out->advancedCount = -1;
    }
    if (out->spriteCount < 0) {
        out->error = -1;
        return 0;
    }

    masked = r_masked_emit(view, surface, texSource, light, g_sprites,
                           (uint16_t)out->spriteCount, spriteSource, &out->stats);
    if (masked != R_THINGS_OK) {
        out->error = (int)masked;
        return 0;
    }

    hashTwo(g_pixels, sizeof g_pixels, g_covered, sizeof g_covered, out->hash);
    return 1;
}

/* --- Assertions ---------------------------------------------------------- */

static int g_failures = 0;

static void report(const char* status, const char* what, const char* detail) {
    printf("SPRITE-CHECK %s %s%s%s\n", what, status,
           detail != NULL ? " " : "", detail != NULL ? detail : "");
    if (strcmp(status, "status=fail") == 0) {
        g_failures += 1;
    }
}

static int resolvedHas(const sprite_cache_t* cache, const char* name) {
    int index;

    for (index = 0; index < cache->resolvedCount; ++index) {
        if (strncmp(cache->resolved[index], name, 8) == 0) {
            return 1;
        }
    }
    return 0;
}

static const char* fixtureLumpForFrame(const char* sprite, int frame) {
    const e1m1_sprite_rule_t* rule = spriteFrameRule(sprite, frame);

    return rule != NULL ? rule->lumps[0].name : "(none)";
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

#define SPRITE_CHECK_ARENA_BYTES (256u * 1024u)

int main(int argc, char** argv) {
    wad_state_t wad;
    doom_map_source_t source;
    doom_map_t map;
    p_level_t level;
    p_spawn_rules_t rules;
    uint8_t* bytes;
    uint8_t* arena;
    size_t size = 0;
    int marker;
    int index;
    int colormapLump;
    int texture1Lump;
    int pnamesLump;
    r_view_storage_t storage;
    r_view_state_t view;
    r_draw_surface_t surface;
    r_light_t light;
    texture_cache_t cache;
    flat_cache_t flatCache;
    r_tex_source_t texSource;
    sprite_cache_t spriteCache;
    r_sprite_source_t spriteSource;
    render_result_t staticResult;
    render_result_t liveResult;
    render_result_t pinnedResult;
    render_result_t withoutPuff;
    render_result_t withPuff;
    r_view_params_t params;
    r_mobj_t* puff;
    uint16_t startSector;
    doom_thing_t start;
    fixed_t playerX;
    fixed_t playerY;
    fixed_t playerZ;
    angle_t playerAngle;
    int sawFrames[4] = {0, 0, 0, 0};
    int chainStep;
    int foundStart = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: sprite_check <path-to-DOOM1.WAD>\n");
        return 2;
    }

    bytes = readFile(argv[1], &size);
    if (bytes == NULL) {
        fprintf(stderr, "sprite_check: cannot read %s\n", argv[1]);
        return 2;
    }
    if (wad_open(&wad, bytes, size) != WAD_ERR_OK) {
        fprintf(stderr, "sprite_check: not a WAD\n");
        free(bytes);
        return 2;
    }

    marker = W_CheckNumForName(&wad, "E1M1");
    colormapLump = W_CheckNumForName(&wad, "COLORMAP");
    texture1Lump = W_CheckNumForName(&wad, "TEXTURE1");
    pnamesLump = W_CheckNumForName(&wad, "PNAMES");
    if (marker < 0 || marker + 1 + DOOM_MAP_LUMP_COUNT > wad.numLumps
        || colormapLump < 0 || texture1Lump < 0 || pnamesLump < 0) {
        fprintf(stderr, "sprite_check: E1M1, COLORMAP, TEXTURE1 or PNAMES is missing\n");
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
        fprintf(stderr, "sprite_check: the COLORMAP lump is not 34 x 256 B\n");
        free(bytes);
        return 2;
    }

    for (index = 0; index < DOOM_MAP_LUMP_COUNT; ++index) {
        const int lump = marker + 1 + index;

        source.data[index] = W_LumpPointer(&wad, lump);
        source.size[index] = (size_t)W_LumpLength(&wad, lump);
    }
    if (doom_map_open(&map, &source) != DOOM_MAP_OK) {
        fprintf(stderr, "sprite_check: map rejected with doom_map error %d\n",
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
    storage.visSprites = g_visSprites;
    storage.visSpriteCapacity = HARNESS_VISSPRITES;
    storage.clipPool = g_clipPool;
    storage.clipPoolCapacity = (uint16_t)(HARNESS_CLIP_POOL * R_SCREEN_WIDTH);
    storage.spriteProofs = NULL;
    if (r_bsp_init(&view, &map, &storage) != R_BSP_OK) {
        fprintf(stderr, "sprite_check: view rejected\n");
        free(bytes);
        return 2;
    }

    texSource.resolve = resolveTexture;
    texSource.context = &cache;
    memset(&flatCache, 0, sizeof flatCache);
    flatCache.wad = &wad;
    texSource.resolveFlat = resolveFlat;
    texSource.flatContext = &flatCache;
    texSource.skyTexture = r_pack_name((const uint8_t*)"SKY1");
    memset(&spriteCache, 0, sizeof spriteCache);
    spriteCache.wad = &wad;
    spriteSource.resolve = resolveSprite;
    spriteSource.context = &spriteCache;

    surface.pixels = g_pixels;
    surface.width = R_SCREEN_WIDTH;
    surface.height = R_VIEW_HEIGHT;
    surface.covered = g_covered;
    surface.planeMask = g_planeMask;
    surface.spriteMask = g_spriteMask;
    surface.tierPixels[R_DRAW_TIER_MID] = 0u;
    surface.tierPixels[R_DRAW_TIER_TOP] = 0u;
    surface.tierPixels[R_DRAW_TIER_BOTTOM] = 0u;
    surface.planePixels = 0u;
    surface.planeSpans = 0u;
    surface.spritePixels = 0u;

    /* --- The world ------------------------------------------------------ */

    arena = (uint8_t*)malloc(SPRITE_CHECK_ARENA_BYTES);
    if (arena == NULL) {
        fprintf(stderr, "sprite_check: no arena\n");
        free(bytes);
        return 2;
    }
    rules = p_spawn_rules_default();
    if (p_level_init(&level, &map, arena, SPRITE_CHECK_ARENA_BYTES, &rules) != P_SETUP_OK) {
        fprintf(stderr, "sprite_check: level load failed with code %d\n", (int)level.error);
        free(arena);
        free(bytes);
        return 2;
    }
    if (p_spawn_specials(&level) != P_SPEC_OK) {
        fprintf(stderr, "sprite_check: p_spawn_specials failed\n");
        p_level_shutdown(&level);
        free(arena);
        free(bytes);
        return 2;
    }
    if (level.player == NULL) {
        fprintf(stderr, "sprite_check: E1M1 has no single-player start\n");
        p_level_shutdown(&level);
        free(arena);
        free(bytes);
        return 2;
    }

    printf("SPRITE-CHECK setup map=E1M1 things=%u spawned=%u mobjs=%u\n",
           (unsigned)level.stats.things, (unsigned)level.stats.spawned,
           (unsigned)p_level_mobj_count(&level));

    /* --- A1: the harness's frame-00 viewpoint --------------------------- */

    for (index = 0; index < (int)map.lumps[DOOM_MAP_THINGS].count; ++index) {
        doom_map_get_thing(&map, (uint32_t)index, &start);
        if (start.type == 1) {
            foundStart = 1;
            break;
        }
    }
    if (!foundStart) {
        fprintf(stderr, "sprite_check: no type-1 start record\n");
        p_level_shutdown(&level);
        free(arena);
        free(bytes);
        return 2;
    }
    params.x = (fixed_t)((uint32_t)(uint16_t)start.x << FRACBITS);
    params.y = (fixed_t)((uint32_t)(uint16_t)start.y << FRACBITS);
    if (r_bsp_point_in_subsector(&map, params.x, params.y, &startSector) != R_BSP_OK) {
        fprintf(stderr, "sprite_check: the start point is not in a subsector\n");
        p_level_shutdown(&level);
        free(arena);
        free(bytes);
        return 2;
    }
    {
        doom_subsector_t sub;
        doom_sector_t sector;

        doom_map_get_subsector(&map, startSector, &sub);
        doom_map_get_sector(&map, doom_map_subsector_sector(&map, &sub), &sector);
        params.z = sector.floorHeight + R_VIEWHEIGHT;
    }
    params.angle = 0;
    params.extralight = 0;
    params.skyPicNum = r_pack_name((const uint8_t*)"F_SKY1");

    if (!renderFrame(&map, &view, &surface, &texSource, &light, &params, NULL, NULL,
                     0, &spriteCache, &spriteSource, &staticResult)
        || !renderFrame(&map, &view, &surface, &texSource, &light, &params, &level, NULL,
                        0, &spriteCache, &spriteSource, &liveResult)) {
        fprintf(stderr, "sprite_check: frame00 render failed (%d)\n", staticResult.error);
        p_level_shutdown(&level);
        free(arena);
        free(bytes);
        return 2;
    }
    {
        char detail[256];

        snprintf(detail, sizeof detail,
                 "view=frame00 static=%s live=%s static_sprites=%d live_sprites=%d "
                 "fallback=%d",
                 staticResult.hash, liveResult.hash, staticResult.spriteCount,
                 liveResult.spriteCount, liveResult.fallbackCount);
        report(strcmp(staticResult.hash, liveResult.hash) == 0 ? "status=pass" : "status=fail",
               "a1-spawn-frame00", detail);
    }

    /* --- A2/A3: the player's view, at tic 1 and after the world ran ----- */

    (void)p_level_tic(&level, NULL, NULL); /* P_CalcHeight: viewz is 0 before it */
    playerX = level.player->x;
    playerY = level.player->y;
    playerZ = level.playerState.viewz;
    playerAngle = level.player->angle;
    params.x = playerX;
    params.y = playerY;
    params.z = playerZ;
    params.angle = playerAngle;
    printf("SPRITE-CHECK player x=%ld y=%ld z=%ld angle=%lu leveltime=%ld\n",
           (long)playerX, (long)playerY, (long)playerZ, (unsigned long)playerAngle,
           (long)level.leveltime);

    {
        static const int kTics[] = {0, 9, 25};
        const int ticCount = (int)(sizeof kTics / sizeof kTics[0]);
        int step;

        for (step = 0; step < ticCount; ++step) {
            int owed = kTics[step];
            char detail[288];

            while (owed-- > 0) {
                (void)p_level_tic(&level, NULL, NULL);
            }
            /* The view the game actually draws: P_CalcHeight raises viewz from
             * half height toward VIEWHEIGHT over the first ~82 tics, so the
             * comparison must follow it instead of freezing the tic-1 value. */
            params.z = level.playerState.viewz;
            /* A2 compares two load-time-equivalent lists: the static THINGS
             * build and a live build pinned to each mobj's spawn-state row. The
             * static list is a snapshot, so animation is out of its reach by
             * definition; what the comparison isolates is the submission -- the
             * same mobj, at the same position, on the frame it was placed with,
             * and its light read from the LIVE world, which is the part the
             * sector-light thinkers could quietly move. */
            if (!renderFrame(&map, &view, &surface, &texSource, &light, &params, NULL, NULL,
                             0, &spriteCache, &spriteSource, &staticResult)
                || !renderFrame(&map, &view, &surface, &texSource, &light, &params, &level,
                                NULL, 1, &spriteCache, &spriteSource, &pinnedResult)
                || !renderFrame(&map, &view, &surface, &texSource, &light, &params, &level,
                                NULL, 0, &spriteCache, &spriteSource, &liveResult)) {
                fprintf(stderr, "sprite_check: player-view render failed (%d)\n",
                        staticResult.error);
                p_level_shutdown(&level);
                free(arena);
                free(bytes);
                return 2;
            }
            snprintf(detail, sizeof detail,
                     "leveltime=%ld static=%s pinned=%s fallback=%d",
                     (long)level.leveltime, staticResult.hash, pinnedResult.hash,
                     pinnedResult.fallbackCount);
            report(strcmp(staticResult.hash, pinnedResult.hash) == 0 ? "status=pass"
                                                                     : "status=fail",
                   "a2-spawn-player", detail);
            /* A2b: the live list actually advances a chain. `advanced` counts
             * the submissions whose live frame resolved to a row other than the type's spawn
             * row -- every one of those is a baked lump by construction. The
             * one fallback is the player avatar, whose S_PLAY_STND is
             * deliberately not baked (it stands at the viewpoint); a chain
             * frame that fell back would raise the count and fail. Under
             * --perturb frame every submission is pinned, so `advanced` is 0. */
            if (step > 0) {
                snprintf(detail, sizeof detail,
                         "leveltime=%ld advanced=%d fallback=%d",
                         (long)level.leveltime, liveResult.advancedCount,
                         liveResult.fallbackCount);
                report(liveResult.advancedCount > 0 && liveResult.fallbackCount == 1
                           ? "status=pass" : "status=fail",
                       "a2b-live-chain", detail);
            }
            if (step == ticCount - 1) {
                // The board's settled spawn-view hash. The host frame
                // at the same viewpoint must reproduce it, which is what makes
                // "the live list draws what the static list drew" a gate and not
                // a claim. It is the last tic because P_CalcHeight is still
                // raising viewz through the earlier ones.
                static const char kSettledSpawnHash[] =
                    "fffb3c5c9d70f04be11f2aa376ad835cc37bf4f94d10cb491c5795f2d12f451a";
                char sentinel[288];

                snprintf(sentinel, sizeof sentinel, "leveltime=%ld expected=%s got=%s",
                         (long)level.leveltime, kSettledSpawnHash, liveResult.hash);
                report(strcmp(liveResult.hash, kSettledSpawnHash) == 0 ? "status=pass"
                                                                       : "status=fail",
                       "a3-known-spawn-hash", sentinel);
            }
        }
    }

    /* --- B: a live puff is submitted and changes the frame ------------- */

    /* The view has settled as far as this run reaches; render the puff at the
     * same eye level the baseline used. */
    playerZ = level.playerState.viewz;
    params.z = playerZ;
    if (!renderFrame(&map, &view, &surface, &texSource, &light, &params, &level, NULL,
                     0, &spriteCache, &spriteSource, &withoutPuff)) {
        fprintf(stderr, "sprite_check: baseline render failed (%d)\n", withoutPuff.error);
        p_level_shutdown(&level);
        free(arena);
        free(bytes);
        return 2;
    }

    /* 40 map units along the player's facing (north from E1M1's start), at eye
     * level: inside the start room and closer than MELEERANGE, so the puff is
     * drawn and not clipped by the wall behind it. */
    puff = p_spawn_puff(&level.context, playerX, playerY + (40 << FRACBITS),
                        playerZ - (8 << FRACBITS), 0);
    if (puff == NULL) {
        fprintf(stderr, "sprite_check: P_SpawnPuff refused\n");
        p_level_shutdown(&level);
        free(arena);
        free(bytes);
        return 2;
    }

    if (!renderFrame(&map, &view, &surface, &texSource, &light, &params, &level, NULL,
                     0, &spriteCache, &spriteSource, &withPuff)) {
        fprintf(stderr, "sprite_check: puff render failed (%d)\n", withPuff.error);
        p_level_shutdown(&level);
        free(arena);
        free(bytes);
        return 2;
    }
    {
        char detail[320];
        int changed = strcmp(withoutPuff.hash, withPuff.hash) != 0;
        int asked = resolvedHas(&spriteCache, "PUFFA0");

        snprintf(detail, sizeof detail,
                 "without=%s with=%s sprite_pixels=%u asked_puff=%s",
                 withoutPuff.hash, withPuff.hash, (unsigned)withPuff.stats.spritePixels,
                 asked ? "yes" : "no");
        report(changed && asked && withPuff.stats.spritePixels > withoutPuff.stats.spritePixels
                   ? "status=pass"
                   : "status=fail",
               "b1-puff-changes-frame", detail);
    }

    /* --- C: the state chain advances the frame ------------------------- */

    chainStep = 0;
    while (chainStep < 64) {
        int state;

        if (!worldContainsMobj(&level, puff)) {
            break;
        }
        state = (int)puff->state;
        if (state == (int)S_PUFF1) {
            sawFrames[0] = 1;
        } else if (state == (int)S_PUFF2) {
            sawFrames[1] = 1;
        } else if (state == (int)S_PUFF3) {
            sawFrames[2] = 1;
        } else if (state == (int)S_PUFF4) {
            sawFrames[3] = 1;
        }
        (void)p_level_tic(&level, NULL, NULL);
        ++chainStep;
    }
    {
        char detail[192];

        snprintf(detail, sizeof detail, "tics=%d states=%d%d%d%d", chainStep, sawFrames[0],
                 sawFrames[1], sawFrames[2], sawFrames[3]);
        report(sawFrames[0] && sawFrames[1] && sawFrames[2] && sawFrames[3] ? "status=pass"
                                                                           : "status=fail",
               "c1-puff-chain", detail);
    }

    /* Each state at one fixed position: the only difference is the sprite, not
     * the z drift a naturally ticked puff pays. */
    puff = p_spawn_puff(&level.context, playerX, playerY + (40 << FRACBITS),
                        playerZ - (8 << FRACBITS), 0);
    if (puff == NULL) {
        fprintf(stderr, "sprite_check: second P_SpawnPuff refused\n");
        p_level_shutdown(&level);
        free(arena);
        free(bytes);
        return 2;
    }
    puff->momz = 0;
    {
        static const statenum_t kStates[4] = {S_PUFF1, S_PUFF2, S_PUFF3, S_PUFF4};
        static const char* const kLumps[4] = {"PUFFA0", "PUFFB0", "PUFFC0", "PUFFD0"};
        char hashes[4][65];
        int step;

        for (step = 0; step < 4; ++step) {
            render_result_t one;
            char detail[320];
            int asked;

            (void)p_set_mobj_state(&level.context, puff, kStates[step], NULL);
            if (!renderFrame(&map, &view, &surface, &texSource, &light, &params, &level, NULL,
                             0, &spriteCache, &spriteSource, &one)) {
                fprintf(stderr, "sprite_check: state render failed (%d)\n", one.error);
                p_level_shutdown(&level);
                free(arena);
                free(bytes);
                return 2;
            }
            asked = resolvedHas(&spriteCache, kLumps[step]);
            memcpy(hashes[step], one.hash, sizeof hashes[step]);
            snprintf(detail, sizeof detail, "step=%d state=%d lump=%s asked=%s hash=%s",
                     step + 1, (int)kStates[step],
                     fixtureLumpForFrame("PUFF", (int)states[kStates[step]].frame & 0x7FFF),
                     asked ? "yes" : "no", one.hash);
            report(asked ? "status=pass" : "status=fail", "c2-puff-frame", detail);
        }
        {
            int distinct = strcmp(hashes[0], hashes[1]) != 0
                           && strcmp(hashes[0], hashes[2]) != 0
                           && strcmp(hashes[0], hashes[3]) != 0
                           && strcmp(hashes[1], hashes[2]) != 0
                           && strcmp(hashes[1], hashes[3]) != 0
                           && strcmp(hashes[2], hashes[3]) != 0;
            char detail[384];

            snprintf(detail, sizeof detail, "hashes=%s,%s,%s,%s", hashes[0], hashes[1],
                     hashes[2], hashes[3]);
            report(distinct ? "status=pass" : "status=fail", "c3-puff-frames-differ", detail);
        }
    }

    /* --- Summary -------------------------------------------------------- */

    printf("SPRITE-CHECK summary failures=%d status=%s\n", g_failures,
           g_failures == 0 ? "pass" : "fail");

    p_level_shutdown(&level);
    free(arena);
    freeTextures(&cache);
    free(bytes);
    return g_failures == 0 ? 0 : 1;
}
