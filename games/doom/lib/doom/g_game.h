/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * Demo playback: the recorded command stream, and the driver that feeds it to
 * the world one tic at a time.
 *
 * THE REFERENCE
 *
 * linuxdoom-1.10 g_game.c: G_DoPlayDemo() reads the header, and
 * G_ReadDemoTiccmd() decodes one four-byte command. d_ticcmd.h's ticcmd_t is
 * what they fill. This file is that pair, with the game-state machine around
 * them removed: no level transitions, no intermission, no menus, no save
 * events. A demo is a level, a header that says which level, and a stream of
 * commands.
 *
 * THE FORMAT (shareware WAD, version 109)
 *
 *   offset  size  field
 *   0       1     version        109
 *   1       1     skill          0 baby .. 4 nightmare (p_skill_t)
 *   2       1     episode        1-based
 *   3       1     map            1-based
 *   4       1     deathmatch     0/1
 *   5       1     respawnparm    0/1
 *   6       1     fastparm       0/1
 *   7       1     nomonsters     0/1
 *   8       1     consoleplayer  0..3
 *   9       4     playeringame[] one byte per MAXPLAYERS slot
 *   13      4*n   tic commands   forwardmove, sidemove, angleturn>>8, buttons
 *   13+4n   1     DEMOMARKER     0x80
 *
 * Four bytes per command, and the third is the *high* byte of `angleturn`:
 * upstream's G_WriteDemoTiccmd stores `cmd->angleturn>>8`, so a recorded turn
 * is a multiple of 256 binary-angle units. G_ReadDemoTiccmd puts it back with
 * `cmd->angleturn = ((unsigned char)*demo_p++)<<8;`, which is what this reader
 * does -- as int16_t, so a byte of 0xFF is -256, exactly as upstream's signed
 * short is. The first two bytes are signed chars (`(signed char)*demo_p++`), so
 * a decoded int8_t is the same value on every target.
 *
 * THE MARKER IS TESTED ON THE FIRST BYTE
 *
 * Upstream tests `*demo_p == DEMOMARKER` before reading a command, which means a
 * forwardmove of -128 could never be recorded: it would read back as the end of
 * the demo. That is a property of the format, not a bug to fix here -- a reader
 * that tested the last byte, or that looked for the marker anywhere, would
 * accept streams upstream rejects and would disagree with the 1993 recorder.
 *
 * WHERE THE BUILD SPLIT IS
 *
 * Upstream's input layer is G_BuildTiccmd(): it turns key state into a
 * ticcmd_t, applying `forwardmove[] = {0x19, 0x32}`, `angleturn[] = {640, 1280,
 * 320}`, the side-move scalings and the button latch. A demo *is* the command
 * G_BuildTiccmd would have produced, so playback runs no builder and applies no
 * speed table: the reader's only job is the bytes -> p_ticcmd_t decode. That is
 * the same split the engine has -- P_BuildTiccmd builds, G_ReadDemoTiccmd
 * copies -- with the builder half simply absent from the demo path.
 *
 * NO FILE-SCOPE STREAM
 *
 * A demo lump is 8,550 to 20,118 bytes in the shareware WAD; on the board it is
 * read from the container's flash, and the port's rule is that nothing per-level
 * lives in .bss (`seg_free` is 3,004 B and is a gate). So this layer owns no
 * storage at all: the caller passes the lump's bytes and keeps them alive, and
 * `g_demo_t` is a cursor into them. The board would hand it a zone block; a host
 * harness hands it a malloc'd buffer. Nothing here allocates.
 *
 * This is C99 only: <stdint.h>, no allocation, no globals, no stdio, no floating
 * point.
 */
#ifndef DOOM_G_GAME_H
#define DOOM_G_GAME_H

#include <stdint.h>

#include "p_mobj.h"
#include "p_setup.h"
#include "p_user.h"

#ifdef __cplusplus
extern "C" {
#endif

/* g_game.c's DEMOMARKER, and the size of one command (G_ReadDemoTiccmd). */
#define G_DEMO_MARKER 0x80u
#define G_DEMO_VERSION 109u
#define G_DEMO_HEADER_SIZE 13u
#define G_DEMO_TICCMD_SIZE 4u

/* The largest skill value a version-109 header can carry (sk_nightmare). */
#define G_DEMO_MAX_SKILL 4u

/* --- The header ---------------------------------------------------------- */

/*
 * G_DoPlayDemo()'s locals, kept in the record it reads them from. The names are
 * upstream's except `respawn` and `nomonsters`, which G_DoPlayDemo calls
 * respawnparm and nomonsters and this struct does not abbreviate.
 *
 * `playerInGame` is one byte per MAXPLAYERS slot, not a bit word: upstream
 * copies `playeringame[i] = *demo_p++` byte by byte and P_SpawnMapThing tests
 * each slot, so the byte is the value.
 */
typedef struct g_demo_header {
    uint8_t version;
    uint8_t skill;
    uint8_t episode;
    uint8_t map;
    uint8_t deathmatch;
    uint8_t respawn;
    uint8_t fast;
    uint8_t nomonsters;
    uint8_t consoleplayer;
    uint8_t playerInGame[MAXPLAYERS];
} g_demo_header_t;

/* --- Error codes --------------------------------------------------------- */

/*
 * One code per failure class, never a shared "demo error" catch-all. A refused
 * g_demo_open() leaves the record zeroed, so a later read cannot answer from a
 * header that was never accepted.
 *
 *   G_DEMO_OK              the call did what it says
 *   G_DEMO_ERR_NULL_DEMO   no record was passed
 *   G_DEMO_ERR_NULL_DATA   no lump bytes were passed
 *   G_DEMO_ERR_SHORT       fewer bytes than a header and a marker
 *   G_DEMO_ERR_VERSION     the version byte is not 109
 *   G_DEMO_ERR_SKILL       the skill byte is above nightmare
 *   G_DEMO_ERR_NO_LEVEL    episode or map is 0: a header with no level
 *   G_DEMO_ERR_TRUNCATED   a command starts past the end of the lump
 *   G_DEMO_ERR_NO_MARKER   the stream runs out without a 0x80 terminator
 *   G_DEMO_ERR_MARKER      g_demo_read() reached the terminator (not a failure
 *                          of the format: it is how a stream ends)
 *   G_DEMO_ERR_END         g_demo_read() is past the lump
 *   G_DEMO_ERR_NULL_LEVEL  g_demo_play() was given no level
 *   G_DEMO_ERR_NOT_OPEN    g_demo_play() was given a level that is not loaded
 *   G_DEMO_ERR_TICK        the world refused a tic (p_level_tic's code)
 *   G_DEMO_ERR_NULL_CMD    g_demo_read() was given nowhere to put the command
 */
typedef enum g_demo_error {
    G_DEMO_OK = 0,
    G_DEMO_ERR_NULL_DEMO = 1,
    G_DEMO_ERR_NULL_DATA = 2,
    G_DEMO_ERR_SHORT = 3,
    G_DEMO_ERR_VERSION = 4,
    G_DEMO_ERR_SKILL = 5,
    G_DEMO_ERR_NO_LEVEL = 6,
    G_DEMO_ERR_TRUNCATED = 7,
    G_DEMO_ERR_NO_MARKER = 8,
    G_DEMO_ERR_MARKER = 9,
    G_DEMO_ERR_END = 10,
    G_DEMO_ERR_NULL_LEVEL = 11,
    G_DEMO_ERR_NOT_OPEN = 12,
    G_DEMO_ERR_TICK = 13,
    G_DEMO_ERR_NULL_CMD = 14
} g_demo_error_t;

/* --- One playback -------------------------------------------------------- */

/*
 * The stream, as a cursor. `tics` is the number of commands the lump carries,
 * counted once by g_demo_open() with upstream's rule (four bytes until the
 * 0x80 marker byte), and `cursor` is the byte offset of the next command.
 *
 * `data` and `size` are the caller's: nothing here copies or frees them.
 */
typedef struct g_demo {
    const uint8_t* data;
    uint32_t size;
    uint32_t cursor;
    uint32_t tics;
    g_demo_header_t header;
} g_demo_t;

/* Why a playback stopped. Both are normal ends of a stream. */
typedef enum g_demo_stop {
    /* G_ReadDemoTiccmd's `if (*demo_p == DEMOMARKER)`: the recorded stream ends
     * here. Upstream answers this with G_ExitLevel(), which is the game state
     * machine's; this port stops instead (no level transitions, D4.4's scope). */
    G_DEMO_STOP_MARKER = 0,
    /* The lump ended with no marker. g_demo_open() refuses this, so a driver
     * that started from an accepted record cannot reach it; the value exists so
     * the report can name what happened rather than guess. */
    G_DEMO_STOP_END = 1
} g_demo_stop_t;

/*
 * What g_demo_play() did: how many commands it fed the world, why it stopped,
 * and the leveltime the world had reached. `tics` equals the demo's command
 * count for a full playback from a rewound record, which is the number the
 * acceptance compares -- a driver that stops early reports a smaller one rather
 * than the lump's.
 */
typedef struct g_demo_run {
    uint32_t tics;
    uint8_t stop;
    int32_t leveltime;
} g_demo_run_t;

/* --- Public API ---------------------------------------------------------- */

/**
 * @brief Reads and validates the header, and counts the stream's commands.
 *
 * This is G_DoPlayDemo()'s header half: the version check, the nine fields and
 * the four playeringame bytes. It also counts the commands by walking the
 * stream with G_ReadDemoTiccmd's marker rule, because "how many tics does this
 * demo have" is a fact about the lump that every caller wants before playing it.
 *
 * Validation, in this order:
 *
 *   1. demo == NULL                                  G_DEMO_ERR_NULL_DEMO
 *   2. data == NULL                                  G_DEMO_ERR_NULL_DATA
 *   3. size < header + one marker byte               G_DEMO_ERR_SHORT
 *   4. data[0] != 109                                G_DEMO_ERR_VERSION
 *   5. data[1] > sk_nightmare                        G_DEMO_ERR_SKILL
 *   6. episode == 0 || map == 0                      G_DEMO_ERR_NO_LEVEL
 *   7. a command crosses the end of the lump         G_DEMO_ERR_TRUNCATED
 *   8. no 0x80 marker at all                         G_DEMO_ERR_NO_MARKER
 *
 * A refused open writes nothing: the record is zeroed first, so `tics` is 0 and
 * `data` is NULL afterwards.
 *
 * @param demo Caller-owned record.
 * @param data The demo lump's bytes; must outlive every use of @p demo.
 * @param size Bytes available at @p data.
 * @return G_DEMO_OK or one of the codes above.
 */
g_demo_error_t g_demo_open(g_demo_t* demo, const uint8_t* data, uint32_t size);

/**
 * @brief Puts the cursor back at the first command, for a second playback.
 *
 * Touches `cursor` only. A record that was never opened has cursor 0, which a
 * read refuses as G_DEMO_ERR_END rather than re-parsing bytes it does not own.
 */
void g_demo_rewind(g_demo_t* demo);

/**
 * @brief How many commands the lump carries. 0 for a NULL or unopened record.
 *
 * This is the demo's own recorded length: the acceptance's "exact tic count"
 * compared against the stream, not against whatever a playback managed to run.
 */
uint32_t g_demo_tics(const g_demo_t* demo);

/**
 * @brief The byte offset of the next command. Equal to the lump size once the
 *        marker has been consumed, which is what "the whole stream was read"
 *        means.
 */
uint32_t g_demo_cursor(const g_demo_t* demo);

/**
 * @brief One command: G_ReadDemoTiccmd().
 *
 * Decodes the four bytes at the cursor into @p cmd and advances it by four, or
 * consumes the marker byte and reports G_DEMO_ERR_MARKER. The three cases, in
 * upstream's order:
 *
 *   cursor >= size                G_DEMO_ERR_END        nothing is written
 *   data[cursor] == 0x80          G_DEMO_ERR_MARKER     cursor advances by 1
 *   cursor + 4 > size             G_DEMO_ERR_TRUNCATED  nothing is written
 *   otherwise                     G_DEMO_OK             cursor advances by 4
 *
 * G_DEMO_ERR_MARKER is the end of the stream, not a misuse: upstream returns
 * false there and the game exits the level. A caller that loops on this function
 * stops on it.
 *
 * @param demo An open, rewound record.
 * @param cmd  Receives the command; untouched when the call refuses.
 * @return G_DEMO_OK, G_DEMO_ERR_MARKER, G_DEMO_ERR_END or G_DEMO_ERR_TRUNCATED.
 */
g_demo_error_t g_demo_read(g_demo_t* demo, p_ticcmd_t* cmd);

/**
 * @brief G_DoPlayDemo()'s game-state write, as a spawn-rules record.
 *
 * Upstream assigns the header's fields to the game's globals just before
 * G_InitNew():
 *
 *   gameskill = skill;  gameepisode = episode;  gamemap = map;
 *   deathmatch = dm;    respawnparm = rsp;      fastparm = fast;
 *   nomonsters = nomo;  consoleplayer = con;
 *   for (i = 0; i < MAXPLAYERS; i++) playeringame[i] = *demo_p++;
 *
 * p_spawn_rules_t is the subset of those that changes what a level load spawns,
 * which is why it is the whole answer here: `consoleplayer` and the episode/map
 * are the caller's (it opened the map), and `respawn`/`fast` have no consumer in
 * this port yet -- p_mobj.h says so at the record. They are still carried in the
 * header so a caller can report them instead of losing them.
 *
 * @param demo   An open record.
 * @param rules  Receives the rules. A NULL @p demo or @p rules is a no-op.
 */
void g_demo_rules(const g_demo_t* demo, p_spawn_rules_t* rules);

/**
 * @brief Plays the record to its end through the world's own tic.
 *
 * The loop, exactly:
 *
 *   while g_demo_read(demo, &cmd) == G_DEMO_OK:
 *       p_level_tic(level, &cmd, NULL)
 *       run->tics++
 *   run->stop = MARKER or END
 *
 * so the player's commands come from the stream and from nothing else: no input
 * device, no G_BuildTiccmd, no per-tic callback. p_level_tic() is P_Ticker's
 * order (player think, thinkers, leveltime++), which is what makes the playback a
 * game tic rather than a world-only tick.
 *
 * The world must already be loaded at the level the header names: a map is
 * doom_map_open()'s and an arena is the caller's, so loading it is not this
 * layer's. A playback that ends on the marker is a complete one, and the caller
 * compares `run->tics` with g_demo_tics() to say so.
 *
 * @param demo  An open record; playback starts at the current cursor, so a caller
 *              that wants a full run calls g_demo_rewind() first.
 * @param level A loaded level (p_level_init).
 * @param run   Receives the counts; untouched on refusal.
 * @return G_DEMO_OK, G_DEMO_ERR_NULL_DEMO, G_DEMO_ERR_NULL_LEVEL,
 *         G_DEMO_ERR_NOT_OPEN, or G_DEMO_ERR_TICK if the world refused a tic.
 */
g_demo_error_t g_demo_play(g_demo_t* demo, p_level_t* level, g_demo_run_t* run);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_G_GAME_H */
