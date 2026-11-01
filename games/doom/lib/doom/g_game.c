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
 * file has no gameaction, no gamemap, no globals at all: the header is data, the
 * cursor is data, and the world is the caller's. That also keeps it linkable by
 * a host harness that has no engine, no display and no .bss to spend.
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
    }
    local.leveltime = level->leveltime;
    if (run != NULL) {
        *run = local;
    }
    return G_DEMO_OK;
}
