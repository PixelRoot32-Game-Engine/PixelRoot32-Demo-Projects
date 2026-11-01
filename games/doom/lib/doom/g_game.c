/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * Demo playback: parse the header, count the stream, decode one command, and
 * drive the world's tic.
 *
 * The reference is linuxdoom-1.10 g_game.c: G_DoPlayDemo() for the header,
 * G_ReadDemoTiccmd() for the four-byte command. The rules that are not obvious
 * from the byte layout -- the marker tested on the first byte, angleturn as the
 * high byte, the signed moves -- are documented in g_game.h next to the format,
 * because they are properties of the format and a reader who changes them
 * breaks every recorded demo without breaking a single visible line of code.
 *
 * WHAT IS NOT HERE, ON PURPOSE
 *
 * Upstream's demo path is entangled with the game state machine: a marker calls
 * G_ExitLevel(), G_DoPlayDemo sets gameaction, and the level, intermission and
 * menu are all reachable from the same switch. D4.4 is playback only, so this
 * file carries one game action and no more: G_DoReborn's single-player branch,
 * which is `gameaction = ga_loadlevel` -- a reload of the level the header
 * named, never a transition. There are no globals at all: the header is data,
 * the cursor is data, and the world is the caller's. That also keeps it linkable
 * by a host harness that has no engine, no display and no .bss to spend.
 *
 * This is C99 only: <stddef.h>, <stdint.h>, no allocation, no globals, no stdio,
 * no floating point.
 */
#include "g_game.h"

#include <string.h>

/* --- Header and stream --------------------------------------------------- */

/*
 * Counts the commands with G_ReadDemoTiccmd's rule and requires the marker.
 *
 * Upstream loops `for (;;)` and trusts the lump; a truncated lump would read
 * past its end there. Here the walk is bounded, and the two ways a stream can
 * fail end in different codes: a command that would cross the end is
 * TRUNCATED, and a lump that simply stops without a marker is NO_MARKER. Both
 * are refusals, so a playback can never start on a stream whose length is a
 * guess.
 */
static g_demo_error_t count_commands(g_demo_t* demo) {
    uint32_t cursor = G_DEMO_HEADER_SIZE;

    while (cursor < demo->size) {
        if (demo->data[cursor] == (uint8_t)G_DEMO_MARKER) {
            demo->tics = (cursor - G_DEMO_HEADER_SIZE) / G_DEMO_TICCMD_SIZE;
            demo->cursor = G_DEMO_HEADER_SIZE;
            return G_DEMO_OK;
        }
        if (cursor + G_DEMO_TICCMD_SIZE > demo->size) {
            return G_DEMO_ERR_TRUNCATED;
        }
        cursor += G_DEMO_TICCMD_SIZE;
    }
    return G_DEMO_ERR_NO_MARKER;
}

g_demo_error_t g_demo_open(g_demo_t* demo, const uint8_t* data, uint32_t size) {
    g_demo_error_t result;

    if (demo == NULL) {
        return G_DEMO_ERR_NULL_DEMO;
    }
    memset(demo, 0, sizeof *demo);
    if (data == NULL) {
        return G_DEMO_ERR_NULL_DATA;
    }
    /* A header and at least the terminator: 13 bytes of fields plus one marker
     * byte is the smallest legal lump. A stream with no commands is legal too --
     * a version-109 lump whose first byte after the header is the marker -- and
     * the first read ends its playback immediately. */
    if (size < G_DEMO_HEADER_SIZE + 1u) {
        return G_DEMO_ERR_SHORT;
    }
    if (data[0] != (uint8_t)G_DEMO_VERSION) {
        return G_DEMO_ERR_VERSION;
    }
    if (data[1] > (uint8_t)G_DEMO_MAX_SKILL) {
        return G_DEMO_ERR_SKILL;
    }
    /* `if (!episode || !map) I_Error ("bad demo lump")`: a header that names no
     * level cannot be played, and a caller that opened one would be reading the
     * wrong map. */
    if (data[2] == 0u || data[3] == 0u) {
        return G_DEMO_ERR_NO_LEVEL;
    }

    demo->data = data;
    demo->size = size;
    demo->header.version = data[0];
    demo->header.skill = data[1];
    demo->header.episode = data[2];
    demo->header.map = data[3];
    demo->header.deathmatch = data[4];
    demo->header.respawn = data[5];
    demo->header.fast = data[6];
    demo->header.nomonsters = data[7];
    demo->header.consoleplayer = data[8];
    memcpy(demo->header.playerInGame, data + 9, MAXPLAYERS);

    result = count_commands(demo);
    if (result != G_DEMO_OK) {
        /* A stream that does not end in a marker is not a demo this reader
         * understands; leaving the header in place would invite a caller to
         * play it anyway. */
        memset(demo, 0, sizeof *demo);
        return result;
    }
    return G_DEMO_OK;
}

void g_demo_rewind(g_demo_t* demo) {
    if (demo == NULL || demo->data == NULL) {
        return;
    }
    demo->cursor = G_DEMO_HEADER_SIZE;
}

uint32_t g_demo_tics(const g_demo_t* demo) {
    if (demo == NULL || demo->data == NULL) {
        return 0u;
    }
    return demo->tics;
}

uint32_t g_demo_cursor(const g_demo_t* demo) {
    if (demo == NULL) {
        return 0u;
    }
    return demo->cursor;
}

g_demo_error_t g_demo_read(g_demo_t* demo, p_ticcmd_t* cmd) {
    const uint8_t* bytes;
    int16_t angle;

    if (demo == NULL) {
        return G_DEMO_ERR_NULL_DEMO;
    }
    if (cmd == NULL) {
        return G_DEMO_ERR_NULL_CMD;
    }
    if (demo->data == NULL || demo->cursor == 0u) {
        /* Never opened, or opened and refused: there is no stream to read. */
        return G_DEMO_ERR_END;
    }
    if (demo->cursor >= demo->size) {
        return G_DEMO_ERR_END;
    }
    if (demo->data[demo->cursor] == (uint8_t)G_DEMO_MARKER) {
        /* Upstream's `demo_p++` happens before the return, so the marker is
         * consumed; g_demo_cursor() then equals the lump size, which is how a
         * caller proves the whole stream was read. */
        demo->cursor += 1u;
        return G_DEMO_ERR_MARKER;
    }
    if (demo->cursor + G_DEMO_TICCMD_SIZE > demo->size) {
        return G_DEMO_ERR_TRUNCATED;
    }

    bytes = demo->data + demo->cursor;
    /* `cmd->forwardmove = ((signed char)*demo_p++);` -- int8_t is that value on
     * every target, and p_ticcmd_t stores it as int8_t for the same reason. */
    cmd->forwardmove = (int8_t)bytes[0];
    cmd->sidemove = (int8_t)bytes[1];
    /* `cmd->angleturn = ((unsigned char)*demo_p++)<<8;` into a signed short: the
     * cast through uint16_t is the two's-complement value the signed shift would
     * have produced, without shifting a negative value (undefined in C99). */
    angle = (int16_t)((uint16_t)bytes[2] << 8);
    cmd->angleturn = angle;
    cmd->buttons = bytes[3];
    demo->cursor += G_DEMO_TICCMD_SIZE;
    return G_DEMO_OK;
}

/* --- The game-state write ------------------------------------------------ */

void g_demo_rules(const g_demo_t* demo, p_spawn_rules_t* rules) {
    uint32_t i;

    if (demo == NULL || rules == NULL) {
        return;
    }
    *rules = p_spawn_rules_default();
    rules->skill = (p_skill_t)demo->header.skill;
    for (i = 0; i < MAXPLAYERS; ++i) {
        rules->playerInGame[i] = demo->header.playerInGame[i];
    }
    /* G_DoPlayDemo leaves `netgame` alone: the demo path is single player, and
     * G_InitNew is called with the globals the player's menu set. A fresh
     * process has netgame false, which is what a recorded single-player demo
     * was recorded under. */
    rules->netgame = 0u;
    rules->deathmatch = demo->header.deathmatch;
    rules->nomonsters = demo->header.nomonsters;
}

/* --- G_DoReborn ---------------------------------------------------------- */

g_demo_error_t g_demo_reborn(p_level_t* level) {
    if (level == NULL) {
        return G_DEMO_ERR_NULL_LEVEL;
    }
    if (!level->open) {
        return G_DEMO_ERR_NOT_OPEN;
    }

    /* G_DoReborn's `if (!netgame) { gameaction = ga_loadlevel; }`, and the
     * G_DoLoadLevel() the action loop runs next: the map `gamemap` still names,
     * loaded from scratch. */
    if (p_level_reload(level) != P_SETUP_OK) {
        return G_DEMO_ERR_RELOAD;
    }
    /* P_SetupLevel's following step, and the only one p_level_init() leaves out
     * on purpose: the movers, the light thinkers and the sector sound state. It
     * draws P_Random() from the position the reload carried over, which is where
     * upstream's P_SpawnSpecials draws too. */
    if (p_spawn_specials(level) != P_SPEC_OK) {
        return G_DEMO_ERR_RELOAD;
    }
    /* G_DoLoadLevel's `for (i...) if (playeringame[i]) G_PlayerReborn (i);`
     * happens inside p_level_init()'s THINGS run, which wires the player record
     * to the avatar P_SpawnPlayer() made. Both halves must have happened: a
     * reloaded level with no player is one the driver cannot tick. */
    if (level->player == NULL || level->playerState.mo != level->player) {
        return G_DEMO_ERR_NO_PLAYER;
    }
    return G_DEMO_OK;
}

/* --- G_ExitLevel / G_DoCompleted / G_DoWorldDone ------------------------- */

void g_level_state_init(g_level_state_t* game, uint8_t episode, uint8_t map) {
    if (game == NULL) {
        return;
    }
    game->episode = episode;
    game->map = map;
    game->secretExit = 0u;
}

void g_exit_level(g_level_state_t* game) {
    if (game == NULL) {
        return;
    }
    /* `secretexit = false; gameaction = ga_completed;` -- the action is the
     * caller calling g_completed_next(), because this port has no action loop. */
    game->secretExit = 0u;
}

void g_secret_exit_level(g_level_state_t* game) {
    if (game == NULL) {
        return;
    }
    /* Shareware is not `commercial`, so upstream's "IF NO WOLF3D LEVELS, NO
     * SECRET EXIT!" guard does not apply: `secretexit = true`. */
    game->secretExit = 1u;
}

void g_exit_from_level(g_level_state_t* game, const p_level_t* level) {
    if (game == NULL || level == NULL) {
        return;
    }
    if (level->exitRequest == (uint8_t)P_EXIT_SECRET) {
        g_secret_exit_level(game);
    } else if (level->exitRequest == (uint8_t)P_EXIT_NORMAL) {
        g_exit_level(game);
    }
    /* P_EXIT_NONE: no line set the request, so G_Ticker would not run
     * G_DoCompleted at all and the game state must not change. */
}

int g_completed_next(g_level_state_t* game, g_level_next_t* out) {
    if (game == NULL || out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof *out);
    out->episode = game->episode;
    out->map = game->map;
    out->secret = game->secretExit;

    /* `if ((gamemap == 8) && (gamemode != commercial)) { gameaction = ga_victory;
     * return; }` -- the boss map ends the episode before the secret exit is
     * consulted, so a secret exit on E1M8 still goes to the finale. */
    if (game->map == 8u) {
        out->victory = 1u;
        return 0;
    }
    /* `if (secretexit) wminfo.next = 8;` and G_DoWorldDone's `next+1`: map 9. */
    if (game->secretExit != 0u) {
        out->map = 9u;
        return 1;
    }
    /* `else if (gamemap == 9) { // returning from secret level ... }` */
    if (game->map == 9u) {
        switch (game->episode) {
            case 1: out->map = 4u; break;
            case 2: out->map = 6u; break;
            case 3: out->map = 7u; break;
            case 4: out->map = 3u; break;
            default: return -1;
        }
        return 1;
    }
    if (game->map < 1u || game->map > 8u) {
        return -1;
    }
    out->map = (uint8_t)(game->map + 1u);
    return 1;
}

void g_level_name(uint8_t episode, uint8_t map, char out[8]) {
    if (out == NULL) {
        return;
    }
    if (episode < 1u || episode > 9u || map < 1u || map > 9u) {
        out[0] = '\0';
        return;
    }
    out[0] = 'E';
    out[1] = (char)('0' + episode);
    out[2] = 'M';
    out[3] = (char)('0' + map);
    out[4] = '\0';
}

g_level_error_t g_do_world_done(p_level_t* level, g_level_state_t* game,
                                const g_level_next_t* next,
                                const doom_map_t* map) {
    if (level == NULL) {
        return G_LEVEL_ERR_NULL_LEVEL;
    }
    if (!level->open) {
        return G_LEVEL_ERR_NOT_OPEN;
    }
    if (game == NULL) {
        return G_LEVEL_ERR_NULL_GAME;
    }
    if (next == NULL) {
        return G_LEVEL_ERR_NULL_NEXT;
    }
    if (next->victory != 0u) {
        return G_LEVEL_ERR_VICTORY;
    }
    if (map == NULL) {
        return G_LEVEL_ERR_NULL_MAP;
    }
    if (map->open == 0) {
        return G_LEVEL_ERR_MAP_NOT_OPEN;
    }

    /* `gamestate = GS_LEVEL; gamemap = wminfo.next+1; G_DoLoadLevel();` -- the
     * state is written before the load, as upstream does. */
    game->episode = next->episode;
    game->map = next->map;
    game->secretExit = next->secret;

    if (p_level_change(level, map) != P_SETUP_OK) {
        return G_LEVEL_ERR_SETUP;
    }
    if (p_spawn_specials(level) != P_SPEC_OK) {
        return G_LEVEL_ERR_SPECIALS;
    }
    /* G_DoLoadLevel's `for (...) if (playeringame[i]) G_PlayerReborn (i);` is
     * p_level_init()'s THINGS run; a new level with no wired avatar cannot be
     * played, so it is a refusal and not a warning. */
    if (level->player == NULL || level->playerState.mo != level->player) {
        return G_LEVEL_ERR_NO_PLAYER;
    }
    return G_LEVEL_OK;
}

/* --- Playback ------------------------------------------------------------ */

g_demo_error_t g_demo_play(g_demo_t* demo, p_level_t* level, g_demo_run_t* run) {
    g_demo_run_t local;
    p_ticcmd_t cmd;

    memset(&local, 0, sizeof local);

    if (demo == NULL) {
        return G_DEMO_ERR_NULL_DEMO;
    }
    if (level == NULL) {
        return G_DEMO_ERR_NULL_LEVEL;
    }
    if (!level->open) {
        return G_DEMO_ERR_NOT_OPEN;
    }

    local.leveltime = level->leveltime;
    for (;;) {
        const g_demo_error_t read = g_demo_read(demo, &cmd);

        if (read == G_DEMO_ERR_MARKER) {
            local.stop = (uint8_t)G_DEMO_STOP_MARKER;
            break;
        }
        if (read == G_DEMO_ERR_END) {
            local.stop = (uint8_t)G_DEMO_STOP_END;
            break;
        }
        if (read != G_DEMO_OK) {
            return read;
        }
        /* P_Ticker's order, through the player layer's entry point: the command
         * reaches P_PlayerThink, the thinkers run, leveltime++. */
        if (p_level_tic(level, &cmd, NULL) != P_TICK_OK) {
            return G_DEMO_ERR_TICK;
        }
        local.tics += 1u;
        /* G_Ticker's first statement on the next tic. P_DeathThink sets
         * PST_REBORN when the corpse's command holds USE; G_DoReborn answers it
         * with a reload of the same level, and playback continues on the stream
         * as if nothing had happened -- which is what the recorded demo does. */
        if (level->playerState.playerstate == P_PST_REBORN) {
            const g_demo_error_t reborn = g_demo_reborn(level);

            if (reborn != G_DEMO_OK) {
                return reborn;
            }
            local.reloads += 1u;
        }
        /* A line trigger set an exit. Upstream's demo path would enter the game
         * state machine's G_DoCompleted; this driver refuses to follow a
         * transition and names why, leaving the stream short so the gate fails
         * rather than silently playing a different level. None of the shareware
         * demos reaches one. */
        if (level->exitRequest != (uint8_t)P_EXIT_NONE) {
            local.stop = (uint8_t)G_DEMO_STOP_EXIT;
            break;
        }
    }
    local.leveltime = level->leveltime;
    if (run != NULL) {
        *run = local;
    }
    return G_DEMO_OK;
}
