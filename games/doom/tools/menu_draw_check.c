/*
 * menu_draw_check.c - drives lib/doom/m_menu.c's drawing from a script and dumps pixels.
 *
 * The other half of tools/menu_draw_check.py. This harness owns no expectation: it
 * reads a WAD and a command script, runs the real menu against the real patches and
 * prints the whole 320x200 index surface after each `draw`. The Python oracle runs
 * the same script independently and the two are compared byte for byte.
 *
 * Script lines (blank lines and '#' lines are ignored):
 *
 *   init S V G [M]      m_menu_init with those settings (M = mute, default 0)
 *   open                m_menu_open
 *   key NAME            up down left right enter back
 *   tick N              m_menu_tick
 *   slots L0|..|L5      m_menu_set_slot_labels ('-' is a NULL row)
 *   draw NAME           clear the surface to 0xEE, m_menu_draw, dump
 *
 * Output: `DRAW NAME active=A screen=S item=I skull=K missing=N` followed by 200
 * lines of 640 hex digits.
 *
 * Usage: menu_draw_check <DOOM1.WAD> <script>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m_menu.h"
#include "r_defs.h"

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

static m_menu_t g_menu;
static uint8_t g_surface[R_SCREEN_WIDTH * R_SCREEN_HEIGHT];

static void dump(const char* name) {
    int missing;
    int row;
    int column;

    memset(g_surface, 0xEE, sizeof g_surface);
    missing = m_menu_draw(&g_menu, lookup, NULL, g_surface, R_SCREEN_WIDTH);
    printf("DRAW %s active=%d screen=%d item=%d skull=%d missing=%d\n", name, (int)g_menu.active,
           (int)g_menu.screen, (int)g_menu.itemOn[g_menu.screen], (int)g_menu.whichSkull,
           missing);
    for (row = 0; row < R_SCREEN_HEIGHT; ++row) {
        for (column = 0; column < R_SCREEN_WIDTH; ++column) {
            printf("%02x", g_surface[row * R_SCREEN_WIDTH + column]);
        }
        printf("\n");
    }
}

static int parseKey(const char* name, m_key_t* key) {
    if (strcmp(name, "up") == 0) {
        *key = M_KEY_UP;
    } else if (strcmp(name, "down") == 0) {
        *key = M_KEY_DOWN;
    } else if (strcmp(name, "left") == 0) {
        *key = M_KEY_LEFT;
    } else if (strcmp(name, "right") == 0) {
        *key = M_KEY_RIGHT;
    } else if (strcmp(name, "enter") == 0) {
        *key = M_KEY_ENTER;
    } else if (strcmp(name, "back") == 0) {
        *key = M_KEY_BACK;
    } else {
        return 0;
    }
    return 1;
}

int main(int argc, char** argv) {
    FILE* script;
    char line[512];

    if (argc < 3) {
        fprintf(stderr, "usage: menu_draw_check <DOOM1.WAD> <script>\n");
        return 2;
    }
    if (!loadWad(argv[1])) {
        fprintf(stderr, "menu_draw_check: cannot read %s\n", argv[1]);
        return 2;
    }
    script = fopen(argv[2], "r");
    if (script == NULL) {
        fprintf(stderr, "menu_draw_check: cannot read %s\n", argv[2]);
        return 2;
    }
    m_menu_init(&g_menu, NULL);

    while (fgets(line, sizeof line, script) != NULL) {
        char command[16];
        int consumed = 0;

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        if (sscanf(line, "%15s%n", command, &consumed) != 1) {
            continue;
        }
        if (strcmp(command, "init") == 0) {
            int s;
            int v;
            int g;
            int m = 0;
            const int values = sscanf(line + consumed, "%d %d %d %d", &s, &v, &g, &m);
            if (values >= 3) {
                m_settings_t settings;
                settings.skill = (uint8_t)s;
                settings.volume = (uint8_t)v;
                settings.gamma = (uint8_t)g;
                settings.muted = values >= 4 ? (uint8_t)m : 0u;
                m_menu_init(&g_menu, &settings);
            } else {
                m_menu_init(&g_menu, NULL);
            }
        } else if (strcmp(command, "open") == 0) {
            m_menu_open(&g_menu);
        } else if (strcmp(command, "key") == 0) {
            char name[16];
            m_key_t key;
            if (sscanf(line + consumed, "%15s", name) == 1 && parseKey(name, &key)) {
                (void)m_menu_key(&g_menu, key);
            }
        } else if (strcmp(command, "tick") == 0) {
            m_menu_tick(&g_menu, (uint32_t)strtoul(line + consumed, NULL, 10));
        } else if (strcmp(command, "slots") == 0) {
            /* Six labels separated by '|' (see menu_check.c). */
            const char* labels[M_SLOT_COUNT];
            char storage[M_SLOT_COUNT][M_SLOT_LABEL_SIZE];
            char fields[M_SLOT_COUNT][M_SLOT_LABEL_SIZE + 1];
            const char* cursor = line + consumed;
            size_t index = 0u;

            memset(fields, 0, sizeof fields);
            /* Skip the one separator space after the command word; a leading
             * space inside the first label would shift it 4 px when drawn. */
            while (*cursor == ' ' || *cursor == '\t') {
                ++cursor;
            }
            while (index < M_SLOT_COUNT && *cursor != '\0') {
                size_t at = 0u;
                while (*cursor != '\0' && *cursor != '|' && *cursor != '\n'
                       && *cursor != '\r' && at < M_SLOT_LABEL_SIZE) {
                    fields[index][at++] = *cursor++;
                }
                fields[index][at] = '\0';
                while (*cursor != '\0' && *cursor != '|' && *cursor != '\n'
                       && *cursor != '\r') {
                    ++cursor;
                }
                if (*cursor == '|') {
                    ++cursor;
                }
                ++index;
            }
            for (index = 0; index < M_SLOT_COUNT; ++index) {
                if (fields[index][0] != '\0' && strcmp(fields[index], "-") != 0) {
                    memcpy(storage[index], fields[index], M_SLOT_LABEL_SIZE);
                    labels[index] = storage[index];
                } else {
                    labels[index] = NULL;
                }
            }
            m_menu_set_slot_labels(&g_menu, labels);
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
