/*
 * weapon_draw_check.c - drives lib/doom/r_psprite.c from a script and dumps pixels.
 *
 * The other half of tools/weapon_draw_check.py. This harness owns no
 * expectation: it reads a WAD and a command script, runs the real
 * R_DrawPlayerSprites() against the real patches and the real COLORMAP, and
 * prints the whole 320x200 index surface after each `draw`. The Python oracle
 * runs the same script independently and the two are compared byte for byte.
 *
 * Script lines (blank lines and '#' lines are ignored):
 *
 *   light SECTORLIGHT EXTRALIGHT   the player's sector light and the view's extralight
 *   layer I STATE SX SY            psprite I (0 weapon, 1 flash): statenum, 16.16 sx/sy;
 *                                  STATE 0 makes the layer inactive
 *   draw NAME                      fill the background, draw the weapon, dump
 *
 * The background is a ramp over the whole surface (so a pixel the weapon should
 * not touch, including the bar rows, is visible as a difference).
 *
 * Output: `DRAW NAME missing=N written=N` then 200 lines of 640 hex digits.
 *
 * Usage: weapon_draw_check <DOOM1.WAD> <script>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "info_tables.h"
#include "p_user.h"
#include "r_defs.h"
#include "r_light.h"
#include "r_psprite.h"
#include "r_things.h"

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

static const uint8_t* lookup(void* context, const char* name, size_t* size) {
    uint32_t index;

    (void)context;
    for (index = 0; index < g_lumpCount; ++index) {
        if (strcmp(g_lumps[index].name, name) == 0) {
            *size = g_lumps[index].size;
            return g_wad + g_lumps[index].offset;
        }
    }
    return NULL;
}

static p_player_t g_player;
static r_view_state_t g_view;
static r_light_t g_light;
static r_draw_surface_t g_drawSurface;
static uint8_t g_surface[R_SCREEN_WIDTH * R_SCREEN_HEIGHT];
static uint8_t g_covered[R_SCREEN_WIDTH];
static uint8_t g_mask[R_SCREEN_WIDTH * R_SCREEN_HEIGHT];
static int g_sectorLight = 160;
static int g_extraLight = 0;

static void background(void) {
    int row;
    int column;

    for (row = 0; row < R_SCREEN_HEIGHT; ++row) {
        for (column = 0; column < R_SCREEN_WIDTH; ++column) {
            g_surface[row * R_SCREEN_WIDTH + column] = (uint8_t)((column * 3 + row * 5) & 0xFF);
        }
    }
    memset(g_covered, 0, sizeof g_covered);
    memset(g_mask, 0, sizeof g_mask);
}

static void dump(const char* name) {
    uint32_t written = 0u;
    int missing;
    int row;
    int column;

    background();
    missing = r_psprites_draw(&g_view, &g_drawSurface, &g_light, &g_player, g_sectorLight,
                              g_extraLight, lookup, NULL, &written);
    printf("DRAW %s missing=%d written=%u\n", name, missing, (unsigned)written);
    for (row = 0; row < R_SCREEN_HEIGHT; ++row) {
        for (column = 0; column < R_SCREEN_WIDTH; ++column) {
            printf("%02x", g_surface[row * R_SCREEN_WIDTH + column]);
        }
        printf("\n");
    }
}

int main(int argc, char** argv) {
    FILE* script;
    char line[512];
    const uint8_t* colormap;
    size_t colormapSize = 0u;

    if (argc < 3) {
        fprintf(stderr, "usage: weapon_draw_check <DOOM1.WAD> <script>\n");
        return 2;
    }
    if (!loadWad(argv[1])) {
        fprintf(stderr, "weapon_draw_check: cannot read %s\n", argv[1]);
        return 2;
    }
    script = fopen(argv[2], "r");
    if (script == NULL) {
        fprintf(stderr, "weapon_draw_check: cannot read %s\n", argv[2]);
        return 2;
    }
    colormap = lookup(NULL, "COLORMAP", &colormapSize);
    if (colormap == NULL || r_light_init(&g_light, colormap, colormapSize) != R_LIGHT_OK) {
        fprintf(stderr, "weapon_draw_check: COLORMAP is missing or malformed\n");
        return 2;
    }

    memset(&g_player, 0, sizeof g_player);
    memset(&g_view, 0, sizeof g_view);
    memset(&g_drawSurface, 0, sizeof g_drawSurface);
    g_drawSurface.pixels = g_surface;
    g_drawSurface.width = R_SCREEN_WIDTH;
    g_drawSurface.height = R_SCREEN_HEIGHT;
    g_drawSurface.covered = g_covered;
    g_drawSurface.spriteMask = g_mask;

    while (fgets(line, sizeof line, script) != NULL) {
        char command[16];
        int consumed = 0;

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        if (sscanf(line, "%15s%n", command, &consumed) != 1) {
            continue;
        }
        if (strcmp(command, "light") == 0) {
            sscanf(line + consumed, "%d %d", &g_sectorLight, &g_extraLight);
        } else if (strcmp(command, "layer") == 0) {
            int layer;
            int state;
            long sx;
            long sy;

            if (sscanf(line + consumed, "%d %d %ld %ld", &layer, &state, &sx, &sy) == 4
                && layer >= 0 && layer < P_NUMPSPRITES) {
                g_player.psprites[layer].state = (int16_t)state;
                g_player.psprites[layer].sx = (fixed_t)sx;
                g_player.psprites[layer].sy = (fixed_t)sy;
            }
        } else if (strcmp(command, "draw") == 0) {
            char name[64];

            if (sscanf(line + consumed, "%63s", name) == 1) {
                dump(name);
            }
        }
    }
    fclose(script);
    return 0;
}
