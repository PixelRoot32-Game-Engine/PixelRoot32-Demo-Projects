/*
 * Host harness for the door rules and the doorway frame.
 *
 * It loads a level through lib/doom/p_setup.c, keeps only the player mobj
 * P_LoadThings() already spawned, finds the nearest closed manual door
 * (special 1, a back sector whose ceiling meets its floor, a flat threshold),
 * stands the player just in front of it and runs a scripted tic sequence
 * through p_level_tic(), which is the same entry point the panel's frame loop
 * calls.
 *
 * tools/door_check.py compiles this file, runs it next to
 * tools/doom_ref.py --door, and compares the two outputs line for line. The
 * frame lines are C-only measurements: the oracle has no renderer that can be
 * aimed at an arbitrary viewpoint, and the assertion they carry is an
 * invariant, not an equality.
 *
 * WHAT IT MEASURES
 *
 *   1. A closed door blocks. The player stands one radius (16 map units) from
 *      the closed line; the first forward tic's P_TryMove is refused and the
 *      position does not change.
 *   2. BT_USE opens it. A held BT_USE press (the panel's B button) starts
 *      EV_VerticalDoor; the door sector's ceiling rises two map units per tic,
 *      and the line's opening becomes passable only once `openTop - mo->z`
 *      reaches the player's height (56).
 *   3. The doorway frame is not blank. The harness renders the real 320x200
 *      surface with r_bsp.c + r_segs.c + r_plane.c from four viewpoints on the
 *      walkable path -- the threshold while the door is closed, the same spot
 *      once the use press opened it, the door sector itself mid-traversal, and
 *      the far side -- and reports the fraction of the frame still at the
 *      0xFF prefill. A frame that is mostly prefill while the walk succeeded is
 *      the panel's white screen. It also prints a hash of each frame, so "the
 *      door never visibly opens" is a comparison rather than an impression:
 *      the open frame must differ from the closed one.
 *
 * THE SCRIPT (identical on both sides, documented here and in doom_ref.py)
 *
 *     tics   0        no input                          (settle; render CLOSED)
 *     tics   1..2     forwardmove 0x19                  (walk into the closed leaf)
 *     tics   3..5     no input                          (release the use latch)
 *     tics   6..12    buttons BT_USE, held              (one press)
 *     tics  13..44    no input                          (the door opens; render OPEN
 *                                                        at the first passable tic)
 *     tics  45..      forwardmove 0x19                  (walk through; render MID
 *                                                        inside the door sector and
 *                                                        AFTER on the far side)
 *
 * `blocked` on a TIC line is 1 when that tic's move was refused; `use` is 1 when
 * the newly pressed BT_USE ran a special line; `passable` is the door line's
 * `openTop - mo->z >= mo->height` at that tic; `ceiling` is the door sector's
 * live ceiling.
 *
 * THE PERTURBATION HOOK
 *
 * P_VIEWHEIGHT is a #ifndef in p_user.h, so the whole build can be perturbed
 * with -DP_VIEWHEIGHT=(40*FRACUNIT). tools/door_check.py --perturb does exactly
 * that; the per-tic viewz then diverges from the oracle and the gate fails,
 * which is how it is proved able to fail.
 *
 * Build: see tools/door_check.py. This is a desktop tool: malloc, stdio, the
 * WAD and a texture cache come from the host, none of which the firmware uses.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "doom_map.h"
#include "m_fixed.h"
#include "p_doors.h"
#include "p_map.h"
#include "p_setup.h"
#include "p_spec.h"
#include "p_tick.h"
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

#define DOOR_ARENA_BYTES (256u * 1024u)
#define DOOR_DEFAULT_TICS 120
#define DOOR_SCRIPT "closed-block-use-open-walk-through"
#define DOOR_THRESHOLD_GAP (16 * FRACUNIT) /* one player radius */

/* --- Storage ------------------------------------------------------------- */

#define HARNESS_PLANES 256
#define HARNESS_TEXTURES 128
#define HARNESS_FLATS 32
#define HARNESS_VISSPRITES 192
#define HARNESS_CLIP_BLOCKS 32
#define HARNESS_OPENING_BLOCKS 8
#define HARNESS_CLIP_POOL (HARNESS_CLIP_BLOCKS + HARNESS_OPENING_BLOCKS)

/* DOOM's stored column is 128 rows whatever the texture's declared height. */
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
static r_vissprite_t g_visSprites[HARNESS_VISSPRITES];
static int16_t g_clipPool[HARNESS_CLIP_POOL * R_SCREEN_WIDTH];

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

/* The live-sector reader the fixed render path uses: the harness binds it
 * on the map it hands r_bsp_init, exactly as FrameDriver binds it on
 * gSession.driver->map. The seven map-facing fields come from the level's
 * mutable sectors; `specialdata` is not part of doom_sector_t. */
static void liveSectorReader(void* context, uint16_t index,
                             doom_sector_t* out) {
    const p_sector_t* sector =
        &((const p_level_t*)context)->sectors[index];

    out->floorHeight = sector->floorHeight;
    out->ceilingHeight = sector->ceilingHeight;
    out->floorPic = sector->floorPic;
    out->ceilingPic = sector->ceilingPic;
    out->lightLevel = sector->lightLevel;
    out->special = sector->special;
    out->tag = sector->tag;
}

/* --- The fixture: the nearest closed manual door ------------------------- */

/* R_PointToAngle(), copied from the walk's static helper because the facing
 * rule needs it and the harness must not reach into lib/doom's internals. */
static angle_t nextPointToAngle(fixed_t x, fixed_t y, fixed_t tx, fixed_t ty) {
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

static int sectorAt(const doom_map_t* map, fixed_t x, fixed_t y, uint16_t* sector) {
    uint16_t subsector = 0;
    doom_subsector_t record;

    if (r_bsp_point_in_subsector(map, x, y, &subsector) != R_BSP_OK) {
        return 0;
    }
    doom_map_get_subsector(map, subsector, &record);
    *sector = doom_map_subsector_sector(map, &record);
    return 1;
}

typedef struct door_fixture {
    uint16_t line;
    uint16_t front;
    uint16_t back;
    fixed_t x;      /* threshold point, one player radius in front of the line */
    fixed_t y;
    angle_t angle;  /* facing the line's midpoint */
} door_fixture_t;

/*
 * The nearest closed manual door to the player start. "Manual door" is special
 * 1, "closed" is a back sector whose ceiling meets its floor, and "flat" is a
 * front floor equal to the back floor, so the threshold is a walkable line and
 * not the edge of a lift. Distance is the squared distance from the start to
 * the line's midpoint; a tie keeps the lower LINEDEF index, as every other
 * harness's repeated-minimum walk does.
 */
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

        /* One player radius along the normal, on the front side. The normal is
         * (-dy, dx); the front is whichever sign's point reports the front
         * sector. */
        nx = -(int64_t)dy;
        ny = (int64_t)dx;
        {
            const double length = sqrt((double)nx * (double)nx +
                                       (double)ny * (double)ny);
            int64_t stepX;
            int64_t stepY;

            if (length <= 0.0) {
                continue;
            }
            stepX = (int64_t)((double)nx / length * (double)DOOR_THRESHOLD_GAP);
            stepY = (int64_t)((double)ny / length * (double)DOOR_THRESHOLD_GAP);
            for (sign = 1; sign >= -1; sign -= 2) {
                const fixed_t px = (fixed_t)((uint32_t)mx + (uint32_t)(sign * stepX));
                const fixed_t py = (fixed_t)((uint32_t)my + (uint32_t)(sign * stepY));
                uint16_t sector = 0;

                if (sectorAt(map, px, py, &sector) && sector == front) {
                    out->line = (uint16_t)i;
                    out->front = front;
                    out->back = back;
                    out->x = px;
                    out->y = py;
                    out->angle = nextPointToAngle(px, py, mx, my);
                    bestDistance = distance;
                    found = 1;
                    break;
                }
            }
        }
    }
    return found;
}

/* --- Rendering ----------------------------------------------------------- */

static uint32_t fnv1a(const uint8_t* bytes, size_t size) {
    uint32_t hash = 2166136261u;
    size_t index;

    for (index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 16777619u;
    }
    return hash;
}

typedef struct render_state {
    r_view_state_t view;
    r_draw_surface_t surface;
    r_tex_source_t textures;
    r_light_t light;
} render_state_t;

static void renderView(render_state_t* render, fixed_t x, fixed_t y, fixed_t z,
                       angle_t angle, const char* label) {
    r_view_params_t params;
    uint32_t prefill = 0;
    uint32_t index;

    params.x = x;
    params.y = y;
    params.z = z;
    params.angle = angle;
    params.extralight = 0;
    params.skyPicNum = r_pack_name((const uint8_t*)"F_SKY1\0\0");

    memset(g_pixels, 0xFF, sizeof g_pixels);
    memset(g_covered, 0, sizeof g_covered);
    memset(g_planeMask, 0, sizeof g_planeMask);
    memset(g_spriteMask, 0, sizeof g_spriteMask);
    render->surface.tierPixels[R_DRAW_TIER_MID] = 0u;
    render->surface.tierPixels[R_DRAW_TIER_TOP] = 0u;
    render->surface.tierPixels[R_DRAW_TIER_BOTTOM] = 0u;
    render->surface.planePixels = 0u;
    render->surface.planeSpans = 0u;
    render->surface.spritePixels = 0u;

    if (r_bsp_render(&render->view, &params) != R_BSP_OK ||
        r_wall_emit(&render->view, &render->surface, &render->textures,
                    &render->light) != R_SEGS_OK ||
        r_plane_draw(&render->view, &render->surface, &render->textures,
                     &render->light) != R_PLANE_OK) {
        printf("FRAME %s status=refused\n", label);
        return;
    }
    for (index = 0; index < (uint32_t)sizeof g_pixels; ++index) {
        if (g_pixels[index] == 0xFFu) {
            prefill += 1u;
        }
    }
    printf("FRAME %s x=%ld y=%ld z=%ld prefill=%u/%u hash=%08lx\n", label,
           (long)x / FRACUNIT, (long)y / FRACUNIT, (long)z / FRACUNIT,
           (unsigned)prefill, (unsigned)sizeof g_pixels,
           (unsigned long)fnv1a(g_pixels, sizeof g_pixels));
}

/* --- The script ---------------------------------------------------------- */

static void commandForTic(int tic, p_ticcmd_t* cmd) {
    memset(cmd, 0, sizeof *cmd);

    if (tic == 1 || tic == 2) {
        cmd->forwardmove = 0x19;      /* walk into the closed leaf */
    } else if (tic >= 6 && tic <= 12) {
        cmd->buttons = BT_USE;        /* one press, held */
    } else if (tic >= 45) {
        cmd->forwardmove = 0x19;      /* walk through the opening */
    }
}

/* True when the door line's opening lets this body through: the two vertical
 * tests P_CheckPosition's caller applies, `openRange >= height` and
 * `openTop - z >= height`. */
static int linePassable(const p_level_t* level, uint16_t line,
                        const r_mobj_t* mobj) {
    p_line_opening_t opening;

    p_line_opening(&level->context, line, &opening);
    return opening.openRange >= mobj->height && opening.openTop - mobj->z >= mobj->height;
}

static int doorIsActive(const p_level_t* level, uint16_t sector) {
    return level->sectors[sector].specialdata != NULL &&
           level->sectors[sector].specialdata->kind == (int16_t)P_THINKER_DOOR;
}

/* The height the door's own thinker is rising to, or the current ceiling when
 * the thinker has already finished. */
static fixed_t doorTopHeight(const p_level_t* level, const door_fixture_t* door) {
    const p_door_thinker_t* thinker =
        (const p_door_thinker_t*)level->sectors[door->back].specialdata;

    if (thinker != NULL && thinker->thinker.kind == (int16_t)P_THINKER_DOOR) {
        return thinker->topHeight;
    }
    return level->sectors[door->back].ceilingHeight;
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

int main(int argc, char** argv) {
    wad_state_t wad;
    doom_map_source_t source;
    doom_map_t map;
    p_level_t level;
    p_spawn_rules_t rules;
    door_fixture_t door;
    render_state_t render;
    r_view_storage_t storage;
    texture_cache_t cache;
    flat_cache_t flatCache;
    uint8_t* bytes;
    uint8_t* arena;
    size_t size = 0;
    const char* mapName = "E1M1";
    int tics = DOOR_DEFAULT_TICS;
    int marker;
    int index;
    int tic;
    int opensAt = -1;
    int crossed = 0;
    int crossTic = -1;
    int renderedClosed = 0;
    int renderedOpen = 0;
    int renderedMid = 0;
    int renderedAfter = 0;
    fixed_t closedX = 0;
    fixed_t closedY = 0;
    fixed_t closedZ = 0;
    angle_t closedAngle = 0;
    r_mobj_t* mobj;
    p_position_result_t position;
    p_move_stats_t moveStats;
    uint32_t triggeredAtStart;

    if (argc < 2 || argc > 4) {
        fprintf(stderr, "usage: door_check <path-to-WAD> [map] [tics]\n");
        return 2;
    }
    if (argc >= 3) {
        mapName = argv[2];
    }
    if (argc >= 4) {
        tics = atoi(argv[3]);
    }
    if (tics < 0) {
        fprintf(stderr, "door_check: tics cannot be negative\n");
        return 2;
    }

    bytes = readFile(argv[1], &size);
    if (bytes == NULL) {
        fprintf(stderr, "door_check: cannot read %s\n", argv[1]);
        return 2;
    }
    if (wad_open(&wad, bytes, size) != WAD_ERR_OK) {
        fprintf(stderr, "door_check: not a WAD\n");
        free(bytes);
        return 2;
    }
    marker = W_CheckNumForName(&wad, mapName);
    if (marker < 0 || marker + 1 + DOOM_MAP_LUMP_COUNT > wad.numLumps) {
        fprintf(stderr, "door_check: %s is not in this WAD\n", mapName);
        free(bytes);
        return 2;
    }
    for (index = 0; index < DOOM_MAP_LUMP_COUNT; ++index) {
        const int lump = marker + 1 + index;

        source.data[index] = W_LumpPointer(&wad, lump);
        source.size[index] = (size_t)W_LumpLength(&wad, lump);
    }
    if (doom_map_open(&map, &source) != DOOM_MAP_OK) {
        fprintf(stderr, "door_check: map rejected with doom_map error %d\n",
                (int)map.error);
        free(bytes);
        return 2;
    }

    arena = (uint8_t*)malloc(DOOR_ARENA_BYTES);
    if (arena == NULL) {
        fprintf(stderr, "door_check: no arena\n");
        free(bytes);
        return 2;
    }
    rules = p_spawn_rules_default();
    if (p_level_init(&level, &map, arena, DOOR_ARENA_BYTES, &rules) !=
        P_SETUP_OK) {
        fprintf(stderr, "door_check: level load failed with code %d\n",
                (int)level.error);
        free(arena);
        free(bytes);
        return 2;
    }
    if (level.player == NULL) {
        fprintf(stderr, "door_check: %s has no single-player start\n", mapName);
        p_level_shutdown(&level);
        free(arena);
        free(bytes);
        return 2;
    }
    /* Keep only the avatar, exactly as movement_check.c does: the door is the
     * map's geometry, and the population is not this gate's subject. */
    for (mobj = (r_mobj_t*)p_level_first_mobj(&level); mobj != NULL;
         mobj = (r_mobj_t*)p_level_next_mobj(mobj)) {
        if (mobj != level.player) {
            (void)p_remove_mobj(&level.context, mobj);
        }
    }
    (void)p_run_thinkers(&level.thinkers, &p_level_release_thinker,
                         &level.context, NULL);
    mobj = level.player;

    /* The renderer walks the same map the level was loaded from; binding the
     * live reader here is what FrameDriver's game path does, so the gate
     * measures the fixed path and not the immutable-map fallback. */
    map.readSector = liveSectorReader;
    map.readSectorContext = &level;

    if (!findDoor(&level, mobj, &door)) {
        fprintf(stderr, "door_check: %s has no closed manual door in front of "
                        "the start\n", mapName);
        p_level_shutdown(&level);
        free(arena);
        free(bytes);
        return 2;
    }

    /* Stand at the threshold and let P_CheckPosition adopt the sector. */
    if (!p_check_position(&level.context, mobj, door.x, door.y, &position)) {
        fprintf(stderr, "door_check: the threshold is not standable\n");
        p_level_shutdown(&level);
        free(arena);
        free(bytes);
        return 2;
    }
    mobj->x = door.x;
    mobj->y = door.y;
    mobj->z = position.floorz;
    mobj->momx = mobj->momy = 0;
    mobj->floorz = position.floorz;
    mobj->ceilingz = position.ceilingz;
    mobj->subsector = position.subsector;
    mobj->sector = position.sector;
    mobj->angle = door.angle;
    memset(&moveStats, 0, sizeof moveStats);
    level.context.moveStats = &moveStats;

    /* --- The renderer's residency: the same policy rsegs_check.c uses. --- */
    memset(&cache, 0, sizeof cache);
    cache.wad = &wad;
    {
        const int texture1Lump = W_CheckNumForName(&wad, "TEXTURE1");
        const int texture2Lump = W_CheckNumForName(&wad, "TEXTURE2");
        const int pnamesLump = W_CheckNumForName(&wad, "PNAMES");
        const int colormapLump = W_CheckNumForName(&wad, "COLORMAP");

        if (texture1Lump < 0 || pnamesLump < 0 || colormapLump < 0) {
            fprintf(stderr, "door_check: TEXTURE1, PNAMES or COLORMAP is missing\n");
            p_level_shutdown(&level);
            free(arena);
            free(bytes);
            return 2;
        }
        cache.texture1 = W_LumpPointer(&wad, texture1Lump);
        cache.texture1Size = (size_t)W_LumpLength(&wad, texture1Lump);
        if (texture2Lump >= 0) {
            cache.texture2 = W_LumpPointer(&wad, texture2Lump);
            cache.texture2Size = (size_t)W_LumpLength(&wad, texture2Lump);
        }
        cache.pnames = W_LumpPointer(&wad, pnamesLump);
        cache.pnamesSize = (size_t)W_LumpLength(&wad, pnamesLump);
        memset(&render.light, 0, sizeof render.light);
        if (r_light_init(&render.light, W_LumpPointer(&wad, colormapLump),
                         (size_t)W_LumpLength(&wad, colormapLump)) != R_LIGHT_OK) {
            fprintf(stderr, "door_check: the COLORMAP lump is not 34 x 256 B\n");
            p_level_shutdown(&level);
            free(arena);
            free(bytes);
            return 2;
        }
    }
    memset(&flatCache, 0, sizeof flatCache);
    flatCache.wad = &wad;
    memset(&render.textures, 0, sizeof render.textures);
    render.textures.resolve = resolveTexture;
    render.textures.context = &cache;
    render.textures.resolveFlat = resolveFlat;
    render.textures.flatContext = &flatCache;
    render.textures.skyTexture = r_pack_name((const uint8_t*)"SKY1\0\0\0\0");

    memset(&render.view, 0, sizeof render.view);
    memset(&storage, 0, sizeof storage);
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
    if (r_bsp_init(&render.view, &map, &storage) != R_BSP_OK) {
        fprintf(stderr, "door_check: view rejected\n");
        p_level_shutdown(&level);
        free(arena);
        free(bytes);
        return 2;
    }
    memset(&render.surface, 0, sizeof render.surface);
    render.surface.pixels = g_pixels;
    render.surface.width = R_SCREEN_WIDTH;
    render.surface.height = R_VIEW_HEIGHT;
    render.surface.covered = g_covered;
    render.surface.planeMask = g_planeMask;
    render.surface.spriteMask = g_spriteMask;

    printf("DOOR %s line=%u sector=%u front=%u threshold=%ld,%ld angle=%lu "
           "tics=%d script=%s\n",
           mapName, (unsigned)door.line, (unsigned)door.back,
           (unsigned)door.front, (long)door.x / FRACUNIT,
           (long)door.y / FRACUNIT, (unsigned long)door.angle, tics,
           DOOR_SCRIPT);

    triggeredAtStart = level.specStats.triggers;
    for (tic = 0; tic < tics; ++tic) {
        p_ticcmd_t cmd;
        p_thinker_run_t run;
        uint32_t blockedBefore;

        commandForTic(tic, &cmd);
        blockedBefore = level.context.moveStats != NULL
                            ? level.context.moveStats->blocked
                            : 0u;
        (void)p_level_tic(&level, &cmd, &run);
        mobj = level.player;

        if (opensAt < 0 && linePassable(&level, door.line, mobj)) {
            opensAt = tic;
        }
        if (!crossed && mobj->sector != door.back && mobj->sector != door.front) {
            crossed = 1;
            crossTic = tic;
        }

        printf("TIC %d x=%ld y=%ld z=%ld viewz=%ld sector=%u door=%d use=%d "
               "blocked=%d ceiling=%ld passable=%d\n",
               tic, (long)mobj->x, (long)mobj->y, (long)mobj->z,
               (long)level.playerState.viewz, (unsigned)mobj->sector,
               doorIsActive(&level, door.back),
               (int)level.playerState.useTriggered,
               level.context.moveStats != NULL &&
                       level.context.moveStats->blocked != blockedBefore
                   ? 1
                   : 0,
               (long)level.sectors[door.back].ceilingHeight, 
               linePassable(&level, door.line, mobj));

        if (tic == 0) {
            /* The door is closed and the player is standing in front of it.
             * The camera is the player's own position at DOOM's VIEWHEIGHT, not
             * the still-rising viewz, so the four frames differ only in the
             * world: the same camera height is what makes "open" comparable to
             * "closed". */
            closedX = mobj->x;
            closedY = mobj->y;
            closedZ = mobj->z + P_VIEWHEIGHT;
            closedAngle = mobj->angle;
            renderView(&render, closedX, closedY, closedZ, closedAngle, "CLOSED");
            renderedClosed = 1;
        }
        if (!renderedOpen && opensAt >= 0 &&
            doorIsActive(&level, door.back) &&
            level.sectors[door.back].ceilingHeight >= doorTopHeight(&level, &door)) {
            /* The same threshold viewpoint once the use press has carried the
             * door to the top. A renderer that reads the static map shows the
             * closed leaf again; a live one shows the opening. */
            renderView(&render, closedX, closedY, closedZ, closedAngle, "OPEN");
            renderedOpen = 1;
        }
        if (mobj->sector == door.back && !renderedMid) {
            renderView(&render, mobj->x, mobj->y, mobj->z + P_VIEWHEIGHT,
                       mobj->angle, "MID");
            renderedMid = 1;
        }
        if (crossed && !renderedAfter && tic >= crossTic + 8) {
            renderView(&render, mobj->x, mobj->y, mobj->z + P_VIEWHEIGHT,
                       mobj->angle, "AFTER");
            renderedAfter = 1;
        }
    }

    printf("FRAMES closed=%d open=%d mid=%d after=%d\n", renderedClosed,
           renderedOpen, renderedMid, renderedAfter);
    printf("SUMMARY used=%lu triggered=%lu opens=%d finalceiling=%ld "
           "finalx=%ld finaly=%ld sector=%u crossed=%d\n",
           (unsigned long)level.playerState.useTriggers,
           (unsigned long)(level.specStats.triggers - triggeredAtStart),
           opensAt, (long)level.sectors[door.back].ceilingHeight,
           (long)mobj->x, (long)mobj->y, (unsigned)mobj->sector, crossed);

    p_level_shutdown(&level);
    for (index = 0; index < cache.count; ++index) {
        free(cache.entries[index].column);
    }
    free(arena);
    free(bytes);
    return 0;
}
