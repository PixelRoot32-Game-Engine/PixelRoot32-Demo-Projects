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
 * ARENA MODE (built with -DMENU_CHECK_ARENA and the world's sources):
 *
 *   menu_check --arena <container> [--bound]
 *
 * The menu's New Game reloads E1M1 at the chosen skill over the arena the board
 * sized at boot, which it measured at MEDIUM skill (peak + 25 %, rounded up to
 * 1 KB). A harder skill spawns more things, and a refused reload zeroes the level,
 * so this mode proves the reload fits before the board ever tries it: it measures
 * the medium peak, derives the board's arena by that rule, loads E1M1 into exactly
 * that arena, then for every skill runs the REAL path (`rules.skill`, a cleared
 * random position, g_demo_reborn) and ticks 35 tics. It prints one
 * `ARENA skill=S ...` line per skill and `ARENASUMMARY ... ok=N`.
 *
 * Usage: menu_check <script>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m_menu.h"

#ifdef MENU_CHECK_ARENA
#include "doom_map.h"
#include "g_game.h"
#include "m_random.h"
#include "p_mobj.h"
#include "p_setup.h"
#include "p_spec.h"
#include "p_user.h"
#include "whd.h"
#include "z_zone.h"
#endif

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

#ifdef MENU_CHECK_ARENA

/* FrameDriver.cpp's sizing rule: peak + kArenaHeadroomPercent, rounded up to 1 KB. */
#define ARENA_HEADROOM_PERCENT 25u
#define ARENA_ROUND 1024u
#define ARENA_PROBE_BYTES (1024u * 1024u)

static int arenaFileRead(const char* path, uint8_t** data, size_t* size) {
    FILE* handle = fopen(path, "rb");
    long length;

    if (handle == NULL) {
        return 0;
    }
    fseek(handle, 0, SEEK_END);
    length = ftell(handle);
    fseek(handle, 0, SEEK_SET);
    *data = (uint8_t*)malloc((size_t)length);
    if (*data == NULL || fread(*data, 1, (size_t)length, handle) != (size_t)length) {
        fclose(handle);
        return 0;
    }
    fclose(handle);
    *size = (size_t)length;
    return 1;
}

static int arenaOpenMap(const whd_state_t* container, doom_map_t* map, doom_map_source_t* source) {
    const int marker = whd_lump_index_for_name(container, "E1M1");
    int index;

    if (marker < 0) {
        return 0;
    }
    for (index = 0; index < DOOM_MAP_LUMP_COUNT; ++index) {
        const uint8_t* view = NULL;
        uint32_t size = 0u;
        if (whd_lump_view(container, marker + 1 + index, &view, &size) != WHD_ERR_OK) {
            return 0;
        }
        source->data[index] = view;
        source->size[index] = (size_t)size;
    }
    return doom_map_open(map, source) == DOOM_MAP_OK;
}

/* The sectors whose floor or ceiling picture the renderer's live reader would name
 * but the container does not hold: what the frame driver reports as FLAT_MISSING. */
static unsigned unresolvedPictures(const whd_state_t* container, const doom_map_t* map) {
    unsigned bad = 0;
    uint16_t index;

    for (index = 0; index < map->lumps[DOOM_MAP_SECTORS].count; ++index) {
        doom_sector_t sector;
        char name[9];
        doom_map_get_sector(map, index, &sector);
        r_unpack_name(sector.floorPic, name);
        bad += whd_lump_index_for_name(container, name) < 0;
        r_unpack_name(sector.ceilingPic, name);
        bad += whd_lump_index_for_name(container, name) < 0;
    }
    return bad;
}

/* The level's mutable sectors against the map's own records, through a second view of
 * the same lumps that has no live reader bound. @p strict compares heights and light
 * too (right after a load nothing has moved); otherwise only what a tic never edits. */
static unsigned sectorMismatches(const p_level_t* level, const doom_map_t* plain, int strict) {
    unsigned bad = 0;
    uint16_t index;

    for (index = 0; index < plain->lumps[DOOM_MAP_SECTORS].count; ++index) {
        doom_sector_t want;
        const p_sector_t* have = &level->sectors[index];
        doom_map_get_sector(plain, index, &want);
        /* `special` is not compared: P_SpawnSpecials clears it on the sectors whose
         * special it turned into a light thinker, which is correct. */
        if (have->floorPic != want.floorPic || have->ceilingPic != want.ceilingPic
            || have->tag != want.tag) {
            ++bad;
        } else if (strict && (have->floorHeight != want.floorHeight
                              || have->ceilingHeight != want.ceilingHeight
                              || have->lightLevel != want.lightLevel)) {
            ++bad;
        }
    }
    return bad;
}

static int arenaMain(const char* path, int bound) {
    whd_state_t container;
    doom_map_source_t source;
    doom_map_t map;
    doom_map_t plain;
    p_level_t level;
    p_spawn_rules_t rules;
    uint8_t* bytes = NULL;
    size_t size = 0;
    void* probe;
    void* arena;
    zone_stats_t zone;
    uint32_t peak;
    uint32_t chosen;
    int skill;
    int ok = 1;

    if (!arenaFileRead(path, &bytes, &size) || whd_open(&container, bytes, size) != WHD_ERR_OK) {
        fprintf(stderr, "menu_check: %s is not a readable PWHD container\n", path);
        return 2;
    }
    memset(&source, 0, sizeof source);
    memset(&map, 0, sizeof map);
    memset(&plain, 0, sizeof plain);
    if (!arenaOpenMap(&container, &map, &source) || doom_map_open(&plain, &source) != DOOM_MAP_OK) {
        fprintf(stderr, "menu_check: E1M1 is not a raw view in the container\n");
        return 2;
    }

    /* The board's measurement: one generous load at medium, peak out of the zone. */
    probe = malloc(ARENA_PROBE_BYTES);
    rules = p_spawn_rules_default();
    rules.skill = P_SKILL_NIGHTMARE; /* FrameDriver.cpp measures at nightmare under DOOM_MENU */
    if (p_level_init(&level, &map, probe, ARENA_PROBE_BYTES, &rules) != P_SETUP_OK
        || p_spawn_specials(&level) != P_SPEC_OK) {
        fprintf(stderr, "menu_check: the probe load of E1M1 refused\n");
        return 2;
    }
    zone = z_stats(&level.zone);
    peak = zone.peakInUse;
    p_level_shutdown(&level);
    chosen = peak + peak * ARENA_HEADROOM_PERCENT / 100u;
    chosen = (chosen + ARENA_ROUND - 1u) / ARENA_ROUND * ARENA_ROUND;
    printf("ARENA nightmare_peak=%u chosen=%u rule=peak+%u%%,round=%u\n", (unsigned)peak,
           (unsigned)chosen, (unsigned)ARENA_HEADROOM_PERCENT, (unsigned)ARENA_ROUND);

    /* ... and the world itself starts at the default skill, as the board's does. */
    rules = p_spawn_rules_default();
    arena = malloc(chosen);
    if (p_level_init(&level, &map, arena, chosen, &rules) != P_SETUP_OK
        || p_spawn_specials(&level) != P_SPEC_OK) {
        fprintf(stderr, "menu_check: the board-sized arena refused the medium load\n");
        return 2;
    }

    /* The board binds the renderer's live seams once, right after the first load, and
     * leaves them bound for the whole session. */
    p_level_bind_render(&level, &map);

    for (skill = 0; skill < M_SKILL_COUNT; ++skill) {
        g_demo_error_t loaded;
        uint32_t tic;
        uint32_t ticked = 0;
        unsigned wrongBefore = 0;
        unsigned wrongAfter = 0;
        unsigned unresolvedBefore = 0;
        unsigned unresolvedAfter = 0;
        p_ticcmd_t cmd;

        /* The scene's New Game, step for step: the skill into the rules, the random
         * position cleared (G_InitNew's M_ClearRandom), the renderer's live reader
         * unbound, then G_DoReborn's reload, then the reader bound again.
         *
         * The unbind is not optional. p_level_reload() copies the map's sectors with
         * doom_map_get_sector(), which answers through a bound live reader -- and the
         * level is being rebuilt, so the reader returns without writing and the copy
         * is the stack's garbage (T39: every frame after the first board New Game
         * was refused with FLAT_MISSING). `--bound` skips the unbind to prove this
         * gate sees that defect. */
        level.rules.skill = (p_skill_t)skill;
        memset(&level.random, 0, sizeof level.random);
        if (!bound) {
            p_level_unbind_render(&map);
        }
        loaded = g_demo_reborn(&level);
        if (!bound) {
            p_level_bind_render(&level, &map);
        }

        memset(&cmd, 0, sizeof cmd);
        if (loaded == G_DEMO_OK) {
            /* The renderer draws the frame right after the reload, before any tic. */
            wrongBefore = sectorMismatches(&level, &plain, 1);
            unresolvedBefore = unresolvedPictures(&container, &map);
            for (tic = 0; tic < 35u; ++tic) {
                if (p_level_tic(&level, &cmd, NULL) != P_TICK_OK) {
                    break;
                }
                ++ticked;
            }
            wrongAfter = sectorMismatches(&level, &plain, 0);
            unresolvedAfter = unresolvedPictures(&container, &map);
        }
        zone = z_stats(&level.zone);
        printf("ARENA skill=%d load=%d ticked=%u sectors_wrong=%u/%u unresolved=%u/%u "
               "in_use=%u peak=%u free=%u spawned=%u mobjs=%u health=%d failures=%u\n",
               skill, (int)loaded, (unsigned)ticked, wrongBefore, wrongAfter, unresolvedBefore,
               unresolvedAfter, (unsigned)zone.inUse, (unsigned)zone.peakInUse,
               (unsigned)zone.freeBytes, (unsigned)level.stats.spawned,
               (unsigned)p_level_mobj_count(&level),
               level.player != NULL ? (int)level.playerState.health : -1,
               (unsigned)zone.failures);
        if (loaded != G_DEMO_OK || ticked != 35u || zone.failures != 0u || wrongBefore != 0u
            || wrongAfter != 0u || unresolvedBefore != 0u || unresolvedAfter != 0u) {
            ok = 0;
        }
    }
    printf("ARENASUMMARY chosen=%u skills=%d ok=%d\n", (unsigned)chosen, M_SKILL_COUNT, ok);
    return ok ? 0 : 1;
}

#endif /* MENU_CHECK_ARENA */

int main(int argc, char** argv) {
    FILE* script;
    char line[1024];

#ifdef MENU_CHECK_ARENA
    if (argc >= 3 && strcmp(argv[1], "--arena") == 0) {
        return arenaMain(argv[2], argc >= 4 && strcmp(argv[3], "--bound") == 0);
    }
#endif
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
