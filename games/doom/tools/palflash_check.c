/*
 * palflash_check.c - drives lib/doom/palflash.c from a script (D5.3).
 *
 * The other half of tools/palflash_check.py. This harness owns no expectation:
 * it prints what the real code computes and the Python oracle, which shares no
 * code with it, decides whether that is DOOM's answer.
 *
 * Script lines (blank lines and '#' lines are ignored):
 *
 *   state DMG BONUS STRENGTH IRON WEAPON
 *       Fill a bare player record and print `IDX <page>`. No level is involved:
 *       this is the selection function alone, over the whole counter space.
 *   reset
 *       Zero the live player's damage, bonus and power counters (the level
 *       stays), so every scenario starts from the same state.
 *   damage N
 *       P_DamageMobj(player, no inflictor, no source, N) on the loaded E1M1
 *       avatar, with no armor: the counter is raised by the real rule.
 *   pickup NAME
 *       Spawn the item 20 units ahead of the avatar and run
 *       P_TouchSpecialThing on it, the way a step onto it would. NAME is
 *       `bonus` (health bonus), `berserk` or `suit`.
 *   tick N
 *       N x p_level_tic() with an empty ticcmd (P_PlayerThink decrements the
 *       counters). Prints `TIC <n> dmg=.. bonus=.. str=.. iron=.. idx=..` after
 *       every tic, counting from 1 within the command.
 *   now
 *       Print the same fields for the state as it stands (`TIC 0 ...`).
 *
 * Usage: palflash_check <DOOM1.WAD> <script>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p_inter.h"
#include "p_map.h"
#include "p_mobj.h"
#include "p_setup.h"
#include "p_spec.h"
#include "p_user.h"
#include "palflash.h"
#include "w_wad.h"

#define PF_ARENA_BYTES (256u * 1024u)
#define PF_REACH (20 * FRACUNIT)

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

static void printTic(const p_level_t* level, int n) {
    const p_player_t* player = &level->playerState;

    printf("TIC %d dmg=%ld bonus=%ld str=%ld iron=%ld idx=%d\n", n,
           (long)player->damagecount, (long)player->bonuscount,
           (long)player->powers[P_PW_STRENGTH], (long)player->powers[P_PW_IRONFEET],
           palflash_index(player));
}

static int runScript(p_level_t* level, const char* path) {
    FILE* script = fopen(path, "r");
    char line[256];

    if (script == NULL) {
        fprintf(stderr, "palflash_check: cannot read %s\n", path);
        return 2;
    }
    while (fgets(line, sizeof line, script) != NULL) {
        char word[32];
        char name[32];
        long a, b, c, d, e;

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        if (sscanf(line, "%31s", word) != 1) {
            continue;
        }
        if (strcmp(word, "state") == 0) {
            p_player_t bare;

            if (sscanf(line, "%*s %ld %ld %ld %ld %ld", &a, &b, &c, &d, &e) != 5) {
                fprintf(stderr, "palflash_check: bad state line: %s", line);
                return 2;
            }
            memset(&bare, 0, sizeof bare);
            bare.damagecount = (int32_t)a;
            bare.bonuscount = (int32_t)b;
            bare.powers[P_PW_STRENGTH] = (int32_t)c;
            bare.powers[P_PW_IRONFEET] = (int32_t)d;
            bare.readyweapon = (int32_t)e;
            printf("IDX %d\n", palflash_index(&bare));
        } else if (strcmp(word, "reset") == 0) {
            p_player_t* player = &level->playerState;
            int i;

            player->damagecount = 0;
            player->bonuscount = 0;
            for (i = 0; i < 6; ++i) {
                player->powers[i] = 0;
            }
        } else if (strcmp(word, "damage") == 0) {
            if (sscanf(line, "%*s %ld", &a) != 1) {
                return 2;
            }
            p_damage_mobj(&level->context, level->player, NULL, NULL, (int)a);
            printTic(level, 0);
        } else if (strcmp(word, "pickup") == 0) {
            mobjtype_t type;
            r_mobj_t* item;

            if (sscanf(line, "%*s %31s", name) != 1) {
                return 2;
            }
            if (strcmp(name, "bonus") == 0) {
                type = MT_MISC2;
            } else if (strcmp(name, "berserk") == 0) {
                type = MT_MISC13;
            } else if (strcmp(name, "suit") == 0) {
                type = MT_MISC14;
            } else {
                fprintf(stderr, "palflash_check: unknown pickup %s\n", name);
                return 2;
            }
            item = p_spawn_mobj(&level->context, level->player->x,
                                level->player->y + PF_REACH, ONFLOORZ, type, NULL);
            if (item == NULL) {
                fprintf(stderr, "palflash_check: the zone refused the item\n");
                return 2;
            }
            p_touch_special_thing(&level->context, item, level->player);
            printTic(level, 0);
        } else if (strcmp(word, "tick") == 0) {
            p_ticcmd_t cmd;
            int i;

            if (sscanf(line, "%*s %ld", &a) != 1) {
                return 2;
            }
            memset(&cmd, 0, sizeof cmd);
            for (i = 1; i <= (int)a; ++i) {
                if (p_level_tic(level, &cmd, NULL) != P_TICK_OK) {
                    fprintf(stderr, "palflash_check: the world refused a tic\n");
                    return 2;
                }
                printTic(level, i);
            }
        } else if (strcmp(word, "now") == 0) {
            printTic(level, 0);
        } else {
            fprintf(stderr, "palflash_check: unknown command %s\n", word);
            return 2;
        }
    }
    fclose(script);
    return 0;
}

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
    int result;
    r_mobj_t* mobj;

    if (argc != 3) {
        fprintf(stderr, "usage: palflash_check <path-to-WAD> <script>\n");
        return 2;
    }
    bytes = readFile(argv[1], &size);
    if (bytes == NULL || wad_open(&wad, bytes, size) != WAD_ERR_OK) {
        fprintf(stderr, "palflash_check: cannot open %s\n", argv[1]);
        return 2;
    }
    marker = W_CheckNumForName(&wad, "E1M1");
    if (marker < 0 || marker + 1 + DOOM_MAP_LUMP_COUNT > wad.numLumps) {
        fprintf(stderr, "palflash_check: E1M1 is not in this WAD\n");
        return 2;
    }
    for (index = 0; index < DOOM_MAP_LUMP_COUNT; ++index) {
        const int lump = marker + 1 + index;

        source.data[index] = W_LumpPointer(&wad, lump);
        source.size[index] = (size_t)W_LumpLength(&wad, lump);
    }
    if (doom_map_open(&map, &source) != DOOM_MAP_OK) {
        fprintf(stderr, "palflash_check: map rejected\n");
        return 2;
    }
    arena = (uint8_t*)malloc(PF_ARENA_BYTES);
    rules = p_spawn_rules_default();
    if (arena == NULL ||
        p_level_init(&level, &map, arena, PF_ARENA_BYTES, &rules) != P_SETUP_OK ||
        level.player == NULL) {
        fprintf(stderr, "palflash_check: level load failed\n");
        return 2;
    }

    /* Keep only the avatar, so the scene is the script's and nothing wakes up. */
    for (mobj = (r_mobj_t*)p_level_first_mobj(&level); mobj != NULL;
         mobj = (r_mobj_t*)p_level_next_mobj(mobj)) {
        if (mobj != level.player) {
            (void)p_remove_mobj(&level.context, mobj);
        }
    }
    (void)p_run_thinkers(&level.thinkers, &p_level_release_thinker, &level.context,
                         NULL);

    result = runScript(&level, argv[2]);

    p_level_shutdown(&level);
    free(arena);
    free(bytes);
    return result;
}
