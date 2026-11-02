/*
 * menu_check.c - drives lib/doom/m_menu.c from a script and prints its state.
 *
 * The other half of tools/menu_check.py. This harness owns no expectation: it
 * runs the real state machine and the real settings codec and prints what they
 * did, one line per command. The Python side holds the expected lines, written
 * from linuxdoom's m_menu.c rules, and compares them.
 *
 * Script lines (blank lines and '#' lines are ignored):
 *
 *   init [SKILL VOLUME GAMMA]   m_menu_init (no arguments = defaults)
 *   open                        m_menu_open
 *   buttons 0xNN                m_menu_buttons: one frame's held bits
 *   key NAME                    up down left right enter back
 *   tick N                      m_menu_tick
 *   take                        both take functions
 *   state                       print the state only
 *   encode S V G                m_settings_encode
 *   decode HEX                  m_settings_decode over the bytes ("-" = none)
 *   clamp S V G                 m_settings_clamp
 *   gamma L V                   m_gamma_correct
 *   gammadump L                 the 256 values of level L
 *
 * Output, one line per command:
 *
 *   STATE active=A screen=S item=I skull=K skill=S volume=V gamma=G [world=0xNN]
 *   TAKE newgame=N skill=S changed=C
 *   BLOB hex
 *   DECODE ok=N skill=S volume=V gamma=G
 *   CLAMP skill=S volume=V gamma=G
 *   GAMMA level=L value=V out=O
 *   GAMMADUMP level=L v0 v1 ... v255
 *
 * Usage: menu_check <script>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m_menu.h"

static m_menu_t g_menu;

static void printState(int withWorld, unsigned world) {
    printf("STATE active=%d screen=%d item=%d skull=%d skill=%d volume=%d gamma=%d",
           (int)g_menu.active, (int)g_menu.screen, (int)g_menu.itemOn[g_menu.screen],
           (int)g_menu.whichSkull, (int)g_menu.settings.skill, (int)g_menu.settings.volume,
           (int)g_menu.settings.gamma);
    if (withWorld) {
        printf(" world=0x%02X", world);
    }
    printf("\n");
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

static size_t parseHex(const char* text, uint8_t* out, size_t capacity) {
    size_t count = 0;

    if (text[0] == '-') {
        return 0;
    }
    while (text[0] != '\0' && text[1] != '\0' && text[0] != '\n' && count < capacity) {
        char pair[3] = {text[0], text[1], 0};
        out[count++] = (uint8_t)strtoul(pair, NULL, 16);
        text += 2;
    }
    return count;
}

int main(int argc, char** argv) {
    FILE* script;
    char line[1024];

    if (argc < 2) {
        fprintf(stderr, "usage: menu_check <script>\n");
        return 2;
    }
    script = fopen(argv[1], "r");
    if (script == NULL) {
        fprintf(stderr, "menu_check: cannot read %s\n", argv[1]);
        return 2;
    }
    m_menu_init(&g_menu, NULL);

    while (fgets(line, sizeof line, script) != NULL) {
        char command[16];
        int consumed = 0;
        const char* rest;

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        if (sscanf(line, "%15s%n", command, &consumed) != 1) {
            continue;
        }
        rest = line + consumed;

        if (strcmp(command, "init") == 0) {
            int s;
            int v;
            int g;
            if (sscanf(rest, "%d %d %d", &s, &v, &g) == 3) {
                m_settings_t settings;
                settings.skill = (uint8_t)s;
                settings.volume = (uint8_t)v;
                settings.gamma = (uint8_t)g;
                m_menu_init(&g_menu, &settings);
            } else {
                m_menu_init(&g_menu, NULL);
            }
            printState(0, 0);
        } else if (strcmp(command, "open") == 0) {
            m_menu_open(&g_menu);
            printState(0, 0);
        } else if (strcmp(command, "buttons") == 0) {
            const unsigned held = (unsigned)strtoul(rest, NULL, 0);
            const unsigned world = m_menu_buttons(&g_menu, (uint8_t)held);
            printState(1, world);
        } else if (strcmp(command, "key") == 0) {
            char name[16];
            m_key_t key;
            if (sscanf(rest, "%15s", name) == 1 && parseKey(name, &key)) {
                (void)m_menu_key(&g_menu, key);
            } else {
                printf("ERROR key %s", rest);
            }
            printState(0, 0);
        } else if (strcmp(command, "tick") == 0) {
            m_menu_tick(&g_menu, (uint32_t)strtoul(rest, NULL, 10));
            printState(0, 0);
        } else if (strcmp(command, "take") == 0) {
            int skill = -1;
            const int started = m_menu_take_new_game(&g_menu, &skill);
            const int changed = m_menu_take_settings_changed(&g_menu);
            printf("TAKE newgame=%d skill=%d changed=%d\n", started, started ? skill : -1, changed);
        } else if (strcmp(command, "state") == 0) {
            printState(0, 0);
        } else if (strcmp(command, "encode") == 0) {
            int s;
            int v;
            int g;
            if (sscanf(rest, "%d %d %d", &s, &v, &g) == 3) {
                m_settings_t settings;
                uint8_t blob[M_SETTINGS_BLOB_SIZE];
                size_t index;
                settings.skill = (uint8_t)s;
                settings.volume = (uint8_t)v;
                settings.gamma = (uint8_t)g;
                m_settings_encode(&settings, blob);
                printf("BLOB ");
                for (index = 0; index < M_SETTINGS_BLOB_SIZE; ++index) {
                    printf("%02X", blob[index]);
                }
                printf("\n");
            }
        } else if (strcmp(command, "decode") == 0) {
            uint8_t blob[64];
            char hex[256];
            size_t size = 0;
            m_settings_t out;
            int ok;
            memset(&out, 0xEE, sizeof out);
            if (sscanf(rest, "%255s", hex) == 1) {
                size = parseHex(hex, blob, sizeof blob);
            }
            ok = m_settings_decode(size == 0 ? NULL : blob, size, &out);
            printf("DECODE ok=%d skill=%d volume=%d gamma=%d\n", ok, (int)out.skill,
                   (int)out.volume, (int)out.gamma);
        } else if (strcmp(command, "clamp") == 0) {
            int s;
            int v;
            int g;
            if (sscanf(rest, "%d %d %d", &s, &v, &g) == 3) {
                m_settings_t settings;
                settings.skill = (uint8_t)s;
                settings.volume = (uint8_t)v;
                settings.gamma = (uint8_t)g;
                m_settings_clamp(&settings);
                printf("CLAMP skill=%d volume=%d gamma=%d\n", (int)settings.skill,
                       (int)settings.volume, (int)settings.gamma);
            }
        } else if (strcmp(command, "gamma") == 0) {
            int l;
            int v;
            if (sscanf(rest, "%d %d", &l, &v) == 2) {
                printf("GAMMA level=%d value=%d out=%d\n", l, v,
                       (int)m_gamma_correct((uint8_t)l, (uint8_t)v));
            }
        } else if (strcmp(command, "gammadump") == 0) {
            const int l = atoi(rest);
            int v;
            printf("GAMMADUMP level=%d", l);
            for (v = 0; v < 256; ++v) {
                printf(" %d", (int)m_gamma_correct((uint8_t)l, (uint8_t)v));
            }
            printf("\n");
        }
    }
    fclose(script);
    return 0;
}
