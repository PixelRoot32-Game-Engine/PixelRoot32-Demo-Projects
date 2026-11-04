/*
 * am_check.c - drives lib/doom/am_map.c from a script and dumps pixels.
 *
 * The other half of tools/am_check.py. This harness owns no expectation: it
 * reads a WAD and a command script, opens one map lump set through the real
 * doom_map.c reader, runs the real automap, and prints the whole 320x200 index
 * surface after each `draw`. The Python oracle runs the same script
 * independently and the two are compared byte for byte. Rows 168..199 are never
 * written by the automap and are printed with them so the gate can prove the
 * status bar's rows are still the caller's.
 *
 * It also links the real BSP walk and wall pass (r_bsp.c / r_segs.c), because
 * the automap's visibility is the mapped bit those passes set: `walk` renders
 * one viewpoint into a scratch surface nobody compares, and the shadow bitmap
 * the wall pass fills is what the map then draws. The textures are a synthetic
 * column (the wall pixels are discarded), so this needs no texture decode.
 *
 * Script lines (blank lines and '#' lines are ignored):
 *
 *   map NAME                      open the level and AM_LevelInit; zero the mapped bits
 *   player X Y ANGLE              the player, in map units, and a binary angle
 *   walk X Y ANGLE                walk + wall pass from that viewpoint, marking what it stores
 *   clear                         zero the shadow bitmap
 *   grid 0|1
 *   follow 0|1
 *   allmap 0|1
 *   start                         AM_Start at the player
 *   stop                          AM_Stop
 *   zoom in|out|release           the '=' / '-' keys
 *   pan none|up|down|left|right   the four arrow keys, '+'-joined for several
 *   tick N                        N x AM_Ticker at the player
 *   light N                       N x AM_updateLightLev
 *   draw NAME                     repaint and dump
 *
 * Output: `WALK NAME segs=N marked=N of M`, then `MAP NAME active=N follow=N ...
 * mx=N my=N mw=N mh=N scale=N clock=N light=N drawn=N`, 200 lines of 640 hex
 * digits and `BOTTOM NAME unchanged=1`.
 *
 * Usage: am_check <DOOM1.WAD> <script>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "am_map.h"
#include "doom_map.h"
#include "m_fixed.h"
#include "r_bsp.h"
#include "r_defs.h"
#include "r_light.h"
#include "r_segs.h"

/* DOOM's PLAYERRADIUS and the sky flat, as the renderer's view needs them. */
#define AM_CHECK_SKY "F_SKY1"

typedef struct wad_lump {
    char name[9];
    uint32_t offset;
    uint32_t size;
} wad_lump_t;

static uint8_t* g_wad;
static size_t g_wadSize;
static wad_lump_t* g_lumps;
static uint32_t g_lumpCount;

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
           | ((uint32_t)p[3] << 24);
}

static uint8_t* readFile(const char* path, size_t* size) {
    FILE* handle = fopen(path, "rb");
    uint8_t* data;
    long length;

    if (handle == NULL) {
        return NULL;
    }
    fseek(handle, 0, SEEK_END);
    length = ftell(handle);
    fseek(handle, 0, SEEK_SET);
    data = (uint8_t*)malloc((size_t)length + 1u);
    if (data == NULL || fread(data, 1, (size_t)length, handle) != (size_t)length) {
        fclose(handle);
        free(data);
        return NULL;
    }
    fclose(handle);
    *size = (size_t)length;
    return data;
}

static int loadWad(const char* path) {
    uint32_t index;
    uint32_t directory;

    g_wad = readFile(path, &g_wadSize);
    if (g_wad == NULL || g_wadSize < 12u) {
        return 0;
    }
    g_lumpCount = le32(g_wad + 4);
    directory = le32(g_wad + 8);
    g_lumps = (wad_lump_t*)calloc(g_lumpCount, sizeof(wad_lump_t));
    for (index = 0; index < g_lumpCount; ++index) {
        const uint8_t* entry = g_wad + directory + index * 16u;
        memcpy(g_lumps[index].name, entry + 8, 8);
        g_lumps[index].name[8] = '\0';
        g_lumps[index].offset = le32(entry);
        g_lumps[index].size = le32(entry + 4);
    }
    return 1;
}

/* The first lump with this name in directory order, or -1. */
static int lumpIndex(const char* name) {
    uint32_t index;

    for (index = 0; index < g_lumpCount; ++index) {
        if (strcmp(g_lumps[index].name, name) == 0) {
            return (int)index;
        }
    }
    return -1;
}

/* --- The map, the view and the shadow bitmap ------------------------------ */

static doom_map_t g_map;
static am_state_t g_amap;
static uint8_t g_mapped[(DOOM_MAP_MAX_ITEMS + 7u) / 8u];
static uint32_t g_mappedBytes;
static int g_mapOpen;

/* The automap's own surface: rows 0..167 draw, 168..199 are the sentinel. */
#define AM_CHECK_SURFACE_PIXELS (R_SCREEN_WIDTH * R_SCREEN_HEIGHT)
#define AM_CHECK_SENTINEL 0xEEu
static uint8_t g_surface[AM_CHECK_SURFACE_PIXELS];

/* The wall pass's scratch surface: same window, nobody reads its pixels. */
static uint8_t g_wallPixels[R_SCREEN_WIDTH * R_VIEW_HEIGHT];

static r_drawseg_t g_drawsegs[256];
static r_visplane_t g_planes[64];
static r_clip_range_t g_clipRanges[32];
static int16_t g_floorClip[R_SCREEN_WIDTH];
static int16_t g_ceilingClip[R_SCREEN_WIDTH];
static int16_t g_clipPool[3200];
static r_view_storage_t g_storage;
static r_view_state_t g_view;

/* A 64 x 128 opaque column with the shape r_walltex_is_usable() demands. */
static uint8_t g_texturePixels[64 * 128];
static const r_walltex_t g_texture = {g_texturePixels, NULL, 64, 128, 63, 127};

static const r_walltex_t* resolveTexture(void* context, r_name_t name) {
    (void)context;
    (void)name;
    return &g_texture;
}

/* COLORMAP: 34 maps of 256 bytes, darkest to normal. The values are irrelevant
 * (no wall pixel is compared); r_light_init only checks the size. */
static uint8_t g_colormap[34 * 256];

static int openLevel(const char* name) {
    doom_map_source_t source;
    static const char* const kOrder[DOOM_MAP_LUMP_COUNT] = {
        "THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS",
        "SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP"};
    int marker;
    int lump;

    marker = lumpIndex(name);
    if (marker < 0) {
        fprintf(stderr, "am_check: no map %s\n", name);
        return 0;
    }
    memset(&source, 0, sizeof source);
    for (lump = 0; lump < DOOM_MAP_LUMP_COUNT; ++lump) {
        const int index = marker + 1 + lump;
        if (index >= (int)g_lumpCount) {
            continue;
        }
        source.data[lump] = g_wad + g_lumps[index].offset;
        source.size[lump] = g_lumps[index].size;
        (void)kOrder[lump];
    }
    if (doom_map_open(&g_map, &source) != DOOM_MAP_OK) {
        fprintf(stderr, "am_check: %s rejected (%d)\n", name, (int)g_map.error);
        return 0;
    }

    g_mappedBytes = (g_map.lumps[DOOM_MAP_LINEDEFS].count + 7u) / 8u;
    if (g_mappedBytes > sizeof g_mapped) {
        fprintf(stderr, "am_check: %s has too many linedefs\n", name);
        return 0;
    }
    memset(g_mapped, 0, sizeof g_mapped);
    memset(&g_amap, 0, sizeof g_amap);
    am_clear_state(&g_amap);
    if (!am_level_init(&g_amap, &g_map, g_mapped, g_mappedBytes)) {
        fprintf(stderr, "am_check: am_level_init refused %s\n", name);
        return 0;
    }
    g_mapOpen = 1;
    printf("LEVEL %s lines=%u bytes=%u\n", name, (unsigned)g_map.lumps[DOOM_MAP_LINEDEFS].count,
           (unsigned)g_mappedBytes);
    return 1;
}

/* The real walk and wall pass from one viewpoint; the shadow bitmap is the
 * output this harness keeps, the pixels are thrown away. */
static int walkLevel(fixed_t x, fixed_t y, angle_t angle, fixed_t z, const char* name) {
    r_view_params_t params;
    r_draw_surface_t surface;
    r_tex_source_t textures;
    r_light_t light;
    r_bsp_error_t walked;
    r_segs_error_t emitted;
    uint32_t marked = 0u;
    uint32_t line;

    if (!g_mapOpen) {
        return 0;
    }
    memset(&params, 0, sizeof params);
    params.x = x;
    params.y = y;
    params.z = z;
    params.angle = angle;
    params.extralight = 0;
    params.skyPicNum = r_pack_name((const uint8_t*)AM_CHECK_SKY);

    memset(&surface, 0, sizeof surface);
    surface.pixels = g_wallPixels;
    surface.width = R_SCREEN_WIDTH;
    surface.height = R_VIEW_HEIGHT;

    textures.resolve = resolveTexture;
    light.colormaps = g_colormap;

    walked = r_bsp_render(&g_view, &params);
    if (walked != R_BSP_OK) {
        fprintf(stderr, "am_check: walk refused (%d)\n", (int)walked);
        return 0;
    }
    emitted = r_wall_emit(&g_view, &surface, &textures, &light);
    if (emitted != R_SEGS_OK) {
        fprintf(stderr, "am_check: wall pass refused (%d)\n", (int)emitted);
        return 0;
    }
    for (line = 0; line < g_map.lumps[DOOM_MAP_LINEDEFS].count; ++line) {
        if (am_mapped(&g_amap, line)) {
            ++marked;
        }
    }
    printf("WALK %s segs=%u marked=%u of %u bits=", name, (unsigned)g_view.drawsegCount,
           (unsigned)marked, (unsigned)g_map.lumps[DOOM_MAP_LINEDEFS].count);
    {
        uint32_t byte;
        for (byte = 0; byte < g_mappedBytes; ++byte) {
            printf("%02x", g_mapped[byte]);
        }
    }
    printf("\n");
    return 1;
}

static void dumpSurface(const char* name) {
    int row;
    int column;

    memset(g_surface, AM_CHECK_SENTINEL, sizeof g_surface);
    printf("MAP %s active=%d follow=%d grid=%d mx=%ld my=%ld mw=%ld mh=%ld scale=%ld "
           "clock=%d light=%d allmap=%d ",
           name, am_active(&g_amap), am_follow(&g_amap), am_grid(&g_amap),
           (long)g_amap.mX, (long)g_amap.mY, (long)g_amap.mW, (long)g_amap.mH,
           (long)am_scale_mtof(&g_amap), g_amap.amClock, g_amap.lightLev, g_amap.allMap);
    {
        uint32_t marked = 0u;
        uint32_t line;
        for (line = 0; line < g_map.lumps[DOOM_MAP_LINEDEFS].count; ++line) {
            if (am_mapped(&g_amap, line)) {
                ++marked;
            }
        }
        printf("mapped=%u\n", (unsigned)marked);
    }
    printf("DRAW %s drawn=%d\n", name, am_draw(&g_amap, g_surface, R_SCREEN_WIDTH));
    for (row = 0; row < R_SCREEN_HEIGHT; ++row) {
        for (column = 0; column < R_SCREEN_WIDTH; ++column) {
            printf("%02x", g_surface[row * R_SCREEN_WIDTH + column]);
        }
        printf("\n");
    }
    {
        int unchanged = 1;
        int index;
        for (index = R_VIEW_HEIGHT * R_SCREEN_WIDTH; index < AM_CHECK_SURFACE_PIXELS; ++index) {
            if (g_surface[index] != AM_CHECK_SENTINEL) {
                unchanged = 0;
            }
        }
        printf("BOTTOM %s unchanged=%d\n", name, unchanged);
    }
}

static void setPlayer(const char* rest) {
    long x = 0;
    long y = 0;
    unsigned long angle = 0;

    if (sscanf(rest, "%ld %ld %lu", &x, &y, &angle) != 3) {
        return;
    }
    g_amap.playerX = (fixed_t)(x * FRACUNIT);
    g_amap.playerY = (fixed_t)(y * FRACUNIT);
    g_amap.playerAngle = (angle_t)angle;
}

static void doWalk(const char* rest) {
    long x = 0;
    long y = 0;
    unsigned long angle = 0;
    long z = 41; /* DOOM's VIEWHEIGHT, in map units */

    if (sscanf(rest, "%ld %ld %lu %ld", &x, &y, &angle, &z) < 3) {
        return;
    }
    (void)walkLevel((fixed_t)(x * FRACUNIT), (fixed_t)(y * FRACUNIT), (angle_t)angle,
                    (fixed_t)(z * FRACUNIT), "view");
}

static uint8_t parsePan(const char* rest) {
    uint8_t bits = AM_PAN_NONE;
    char word[64];

    if (sscanf(rest, "%63s", word) != 1) {
        return AM_PAN_NONE;
    }
    if (strstr(word, "up") != NULL) {
        bits = (uint8_t)(bits | AM_PAN_UP);
    }
    if (strstr(word, "down") != NULL) {
        bits = (uint8_t)(bits | AM_PAN_DOWN);
    }
    if (strstr(word, "left") != NULL) {
        bits = (uint8_t)(bits | AM_PAN_LEFT);
    }
    if (strstr(word, "right") != NULL) {
        bits = (uint8_t)(bits | AM_PAN_RIGHT);
    }
    return bits;
}

static void initView(void) {
    int index;

    for (index = 0; index < 34 * 256; ++index) {
        g_colormap[index] = (uint8_t)((index % 256) * (index / 256 + 1) / 34);
    }
    memset(g_texturePixels, 0x60, sizeof g_texturePixels);
    memset(&g_storage, 0, sizeof g_storage);
    g_storage.drawsegs = g_drawsegs;
    g_storage.drawsegCapacity = 256;
    g_storage.planes.planes = g_planes;
    g_storage.planes.capacity = 64;
    g_storage.clipRanges = g_clipRanges;
    g_storage.clipRangeCapacity = 32;
    g_storage.floorClip = g_floorClip;
    g_storage.ceilingClip = g_ceilingClip;
    g_storage.clipPool = g_clipPool;
    g_storage.clipPoolCapacity = 3200;
    /* The shadow bitmap the wall pass marks. It is the same block the
     * automap reads, which is what makes `walk` then `draw` one proof. */
    g_storage.mapped = g_mapped;
    g_storage.mappedBytes = g_mappedBytes;
    if (r_bsp_init(&g_view, &g_map, &g_storage) != R_BSP_OK) {
        fprintf(stderr, "am_check: r_bsp_init refused\n");
    }
}

int main(int argc, char** argv) {
    FILE* script;
    char line[512];

    if (argc < 3) {
        fprintf(stderr, "usage: am_check <DOOM1.WAD> <script>\n");
        return 2;
    }
    if (!loadWad(argv[1])) {
        fprintf(stderr, "am_check: cannot read %s\n", argv[1]);
        return 2;
    }
    script = fopen(argv[2], "r");
    if (script == NULL) {
        fprintf(stderr, "am_check: cannot read %s\n", argv[2]);
        return 2;
    }

    am_clear_state(&g_amap);

    while (fgets(line, sizeof line, script) != NULL) {
        char command[16];
        int consumed = 0;

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        if (sscanf(line, "%15s%n", command, &consumed) != 1) {
            continue;
        }
        if (strcmp(command, "map") == 0) {
            char name[32];
            if (sscanf(line + consumed, "%31s", name) == 1) {
                (void)openLevel(name);
                initView();
            }
        } else if (strcmp(command, "player") == 0) {
            setPlayer(line + consumed);
        } else if (strcmp(command, "walk") == 0) {
            doWalk(line + consumed);
        } else if (strcmp(command, "clear") == 0) {
            memset(g_mapped, 0, sizeof g_mapped);
            g_amap.mapped = g_mapped;
            g_amap.mappedBytes = g_mappedBytes;
        } else if (strcmp(command, "grid") == 0) {
            g_amap.grid = atoi(line + consumed) != 0;
        } else if (strcmp(command, "follow") == 0) {
            g_amap.followPlayer = atoi(line + consumed) != 0;
        } else if (strcmp(command, "allmap") == 0) {
            g_amap.allMap = atoi(line + consumed) != 0;
        } else if (strcmp(command, "start") == 0) {
            (void)am_start(&g_amap, g_amap.playerX, g_amap.playerY);
        } else if (strcmp(command, "stop") == 0) {
            am_stop(&g_amap);
        } else if (strcmp(command, "zoom") == 0) {
            char word[16];
            int direction = AM_ZOOM_RELEASE;
            if (sscanf(line + consumed, "%15s", word) == 1) {
                if (strcmp(word, "in") == 0) {
                    direction = AM_ZOOM_IN;
                } else if (strcmp(word, "out") == 0) {
                    direction = AM_ZOOM_OUT;
                }
            }
            (void)am_zoom(&g_amap, direction);
        } else if (strcmp(command, "pan") == 0) {
            (void)am_pan(&g_amap, parsePan(line + consumed));
        } else if (strcmp(command, "tick") == 0) {
            int count = atoi(line + consumed);
            while (count-- > 0) {
                am_ticker(&g_amap, g_amap.playerX, g_amap.playerY, g_amap.playerAngle);
            }
        } else if (strcmp(command, "light") == 0) {
            int count = atoi(line + consumed);
            while (count-- > 0) {
                am_update_light_lev(&g_amap);
            }
        } else if (strcmp(command, "draw") == 0) {
            char name[64];
            if (sscanf(line + consumed, "%63s", name) == 1) {
                dumpSurface(name);
            }
        }
    }
    fclose(script);
    return 0;
}
