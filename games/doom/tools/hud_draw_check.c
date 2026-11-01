/*
 * hud_draw_check.c - drives lib/doom/st_stuff.c from a script and dumps pixels.
 *
 * The other half of tools/hud_draw_check.py. This harness owns no expectation:
 * it reads a WAD and a command script, runs the real status bar against the real
 * patches, and prints the whole 320x200 index surface after each `draw`. The
 * Python oracle runs the same script independently and the two are compared byte
 * for byte.
 *
 * Script lines (blank lines and '#' lines are ignored):
 *
 *   set health N | armor N | armortype N | ready N | angle N | damagecount N |
 *       bonuscount N | attackdown N
 *   set ammo A B C D | maxammo A B C D | weapons 9 values | cards 6 values |
 *       powers 6 values
 *   set attacker none | DX DY       attacker's offset from the player (map units)
 *   start                           ST_Start()
 *   tick N                          N x ST_Ticker()
 *   draw NAME FACES                 repaint (FACES 0 or 1) and dump
 *
 * Output: `FACE NAME index=N count=N priority=N`, then `DRAW NAME FACES missing=N`
 * followed by 200 lines of 640 hex digits.
 *
 * Usage: hud_draw_check <DOOM1.WAD> <script>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m_fixed.h"
#include "p_mobj.h"
#include "p_user.h"
#include "r_defs.h"
#include "st_stuff.h"

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
static r_mobj_t g_avatar;
static r_mobj_t g_attacker;
static st_state_t g_state;
static uint8_t g_surface[R_SCREEN_WIDTH * R_SCREEN_HEIGHT];

static void setValues(const char* key, int32_t* out, int count, char* rest) {
    int index;
    (void)key;
    for (index = 0; index < count; ++index) {
        out[index] = (int32_t)strtol(rest, &rest, 10);
    }
}

static void setCommand(char* rest) {
    char key[32];
    int consumed = 0;
    int32_t values[9];
    int index;

    if (sscanf(rest, "%31s%n", key, &consumed) != 1) {
        return;
    }
    rest += consumed;
    if (strcmp(key, "health") == 0) {
        setValues(key, values, 1, rest);
        g_player.health = values[0];
        g_avatar.health = values[0];
    } else if (strcmp(key, "armor") == 0) {
        setValues(key, values, 1, rest);
        g_player.armorpoints = values[0];
    } else if (strcmp(key, "armortype") == 0) {
        setValues(key, values, 1, rest);
        g_player.armortype = values[0];
    } else if (strcmp(key, "ready") == 0) {
        setValues(key, values, 1, rest);
        g_player.readyweapon = values[0];
    } else if (strcmp(key, "angle") == 0) {
        g_avatar.angle = (angle_t)strtoul(rest, NULL, 10);
    } else if (strcmp(key, "damagecount") == 0) {
        setValues(key, values, 1, rest);
        g_player.damagecount = values[0];
    } else if (strcmp(key, "bonuscount") == 0) {
        setValues(key, values, 1, rest);
        g_player.bonuscount = values[0];
    } else if (strcmp(key, "attackdown") == 0) {
        setValues(key, values, 1, rest);
        g_player.attackdown = values[0];
    } else if (strcmp(key, "ammo") == 0) {
        setValues(key, g_player.ammo, 4, rest);
    } else if (strcmp(key, "maxammo") == 0) {
        setValues(key, g_player.maxammo, 4, rest);
    } else if (strcmp(key, "weapons") == 0) {
        setValues(key, values, 9, rest);
        for (index = 0; index < 9; ++index) {
            g_player.weaponowned[index] = (uint8_t)values[index];
        }
    } else if (strcmp(key, "cards") == 0) {
        setValues(key, values, 6, rest);
        for (index = 0; index < 6; ++index) {
            g_player.cards[index] = (uint8_t)values[index];
        }
    } else if (strcmp(key, "powers") == 0) {
        setValues(key, g_player.powers, 6, rest);
    } else if (strcmp(key, "attacker") == 0) {
        if (strstr(rest, "none") != NULL) {
            g_player.attacker = NULL;
        } else {
            setValues(key, values, 2, rest);
            g_attacker.x = g_avatar.x + values[0] * FRACUNIT;
            g_attacker.y = g_avatar.y + values[1] * FRACUNIT;
            g_player.attacker = &g_attacker;
        }
    } else if (strcmp(key, "self") == 0) {
        g_player.attacker = &g_avatar;
    }
}

static void dump(const char* name, int faces) {
    int missing;
    int row;
    int column;

    memset(g_surface, 0xEE, sizeof g_surface);
    missing = st_draw(&g_state, &g_player, lookup, NULL, g_surface, R_SCREEN_WIDTH, faces);
    printf("FACE %s index=%d count=%d priority=%d\n", name, (int)g_state.faceIndex,
           (int)g_state.faceCount, (int)g_state.priority);
    printf("DRAW %s %d missing=%d\n", name, faces, missing);
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

    if (argc < 3) {
        fprintf(stderr, "usage: hud_draw_check <DOOM1.WAD> <script>\n");
        return 2;
    }
    if (!loadWad(argv[1])) {
        fprintf(stderr, "hud_draw_check: cannot read %s\n", argv[1]);
        return 2;
    }
    script = fopen(argv[2], "r");
    if (script == NULL) {
        fprintf(stderr, "hud_draw_check: cannot read %s\n", argv[2]);
        return 2;
    }

    memset(&g_player, 0, sizeof g_player);
    memset(&g_avatar, 0, sizeof g_avatar);
    memset(&g_attacker, 0, sizeof g_attacker);
    g_player.mo = &g_avatar;
    g_player.playerstate = P_PST_LIVE;

    while (fgets(line, sizeof line, script) != NULL) {
        char command[16];
        int consumed = 0;

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        if (sscanf(line, "%15s%n", command, &consumed) != 1) {
            continue;
        }
        if (strcmp(command, "set") == 0) {
            setCommand(line + consumed);
        } else if (strcmp(command, "start") == 0) {
            st_start(&g_state, &g_player);
        } else if (strcmp(command, "tick") == 0) {
            int count = atoi(line + consumed);
            while (count-- > 0) {
                st_ticker(&g_state, &g_player);
            }
        } else if (strcmp(command, "draw") == 0) {
            char name[64];
            int faces = 1;
            if (sscanf(line + consumed, "%63s %d", name, &faces) >= 1) {
                dump(name, faces);
            }
        }
    }
    fclose(script);
    return 0;
}
