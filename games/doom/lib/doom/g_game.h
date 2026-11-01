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
 * what they fill. This file is that pair, plus the game-state writes a level can
 * force: G_DoReborn()'s single-player branch, which is `gameaction =
 * ga_loadlevel` and therefore a reload of the same level, and the exit path --
 * G_ExitLevel()/G_SecretExitLevel(), G_DoCompleted()'s next-map decision and
 * G_DoWorldDone()'s load of the next level. There is still no intermission, no
 * world map, no finale and no menu: those are screens, and D5.2 owns them.
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
 *   G_DEMO_ERR_RELOAD      the level refused to reload, or its specials did
 *   G_DEMO_ERR_NO_PLAYER   a reloaded level produced no live player to drive
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
    G_DEMO_ERR_NULL_CMD = 14,
    G_DEMO_ERR_RELOAD = 15,
    G_DEMO_ERR_NO_PLAYER = 16
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
     * machine's; this port stops instead and reports it, because a demo that
     * reaches a level exit is a finding about the stream, not a transition the
     * demo path may follow (D4.4's scope). */
    G_DEMO_STOP_MARKER = 0,
    /* The lump ended with no marker. g_demo_open() refuses this, so a driver
     * that started from an accepted record cannot reach it; the value exists so
     * the report can name what happened rather than guess. */
    G_DEMO_STOP_END = 1,
    /* A tic's line trigger set p_level_t::exitRequest. Upstream's demo path is
     * entangled with G_DoCompleted/intermission; this port refuses to follow an
     * exit from a recording and stops with the reason named, so the run is
     * shorter than the lump and its gate fails instead of transitioning. No
     * shareware demo reaches an exit today. */
    G_DEMO_STOP_EXIT = 2
} g_demo_stop_t;

/*
 * What g_demo_play() did: how many commands it fed the world, why it stopped,
 * the leveltime the world had reached, and how many times a death was answered
 * by a reload. `tics` equals the demo's command count for a full playback from
 * a rewound record, which is the number the acceptance compares -- a driver that
 * stops early reports a smaller one rather than the lump's.
 *
 * `leveltime` is the world's clock at the end, not the stream's position: a
 * reload restarts it (P_SetupLevel's `leveltime = 0;`), so a run of 5,026
 * commands can end with a leveltime in the hundreds. `tics` is the only number
 * that describes the stream.
 */
typedef struct g_demo_run {
    uint32_t tics;
    uint32_t reloads;
    uint8_t stop;
    int32_t leveltime;
} g_demo_run_t;

/* --- The level a game is on ---------------------------------------------- */

/*
 * Upstream keeps `gameepisode`, `gamemap` and `secretexit` as globals in
 * g_game.c. This port has no globals, so the caller owns them here. The struct is
 * the data the game-state machine needs for a level transition, without the
 * states this port does not have (intermission, world map, finale, menu).
 *
 *   episode, map  upstream's gameepisode/gamemap, 1-based.
 *   secretExit    upstream's `secretexit`: the last exit was a secret one.
 *                 G_ExitLevel() clears it, G_SecretExitLevel() sets it.
 */
typedef struct g_level_state {
    uint8_t episode;
    uint8_t map;
    uint8_t secretExit;
} g_level_state_t;

/*
 * G_DoCompleted uses wminfo.next (0-based) and G_DoWorldDone writes `gamemap =
 * wminfo.next + 1`. This record is the already-1-based answer: the level to load,
 * or the episode's victory. It is filled by g_completed_next().
 *
 *   episode, map  the next level, 1-based; unchanged from the game when
 *                 victory is set.
 *   secret        the exit that requested it was a secret one.
 *   victory       1 when G_DoCompleted answered `ga_victory` instead of naming a
 *                 next map: the boss map (gamemap 8) in a non-commercial game.
 */
typedef struct g_level_next {
    uint8_t episode;
    uint8_t map;
    uint8_t secret;
    uint8_t victory;
} g_level_next_t;

/*
 * One code per failure class for a level transition, never a shared catch-all.
 *
 *   G_LEVEL_OK              the call did what it says
 *   G_LEVEL_ERR_NULL_LEVEL  no level was passed
 *   G_LEVEL_ERR_NOT_OPEN    the level is not loaded
 *   G_LEVEL_ERR_NULL_MAP    no next map view was passed
 *   G_LEVEL_ERR_MAP_NOT_OPEN the map view was never accepted by doom_map_open()
 *   G_LEVEL_ERR_SETUP       p_level_change() refused the load
 *   G_LEVEL_ERR_SPECIALS    p_spawn_specials() refused the level
 *   G_LEVEL_ERR_NO_PLAYER   the new level produced no live console player
 *   G_LEVEL_ERR_VICTORY     g_do_world_done() was given a victory record: there
 *                           is no level to load
 *   G_LEVEL_ERR_NULL_GAME   no game state was passed
 *   G_LEVEL_ERR_NULL_NEXT   no next-level record was passed
 */
typedef enum g_level_error {
    G_LEVEL_OK = 0,
    G_LEVEL_ERR_NULL_LEVEL = 1,
    G_LEVEL_ERR_NOT_OPEN = 2,
    G_LEVEL_ERR_NULL_MAP = 3,
    G_LEVEL_ERR_MAP_NOT_OPEN = 4,
    G_LEVEL_ERR_SETUP = 5,
    G_LEVEL_ERR_SPECIALS = 6,
    G_LEVEL_ERR_NO_PLAYER = 7,
    G_LEVEL_ERR_VICTORY = 8,
    G_LEVEL_ERR_NULL_GAME = 9,
    G_LEVEL_ERR_NULL_NEXT = 10
} g_level_error_t;

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
 * @brief G_DoReborn()'s single-player branch: reload the level, keep playing.
 *
 * Upstream's G_Ticker begins every tic with
 *
 *   for (i = 0; i < MAXPLAYERS; i++)
 *       if (playeringame[i] && players[i].playerstate == PST_REBORN)
 *           G_DoReborn (i);
 *
 * and G_DoReborn, when `!netgame`, does one thing:
 *
 *   // reload the level from scratch
 *   gameaction = ga_loadlevel;
 *
 * The game-action loop then runs G_DoLoadLevel() -- the same `gamemap`, not a
 * transition -- before the tic reads its command. That is what a recorded demo
 * depends on: all three shareware demos die, hold USE, and carry commands for
 * thousands of tics after the death, so a port that freezes the corpse is the
 * one that is wrong. G_CheckDemoStatus is reached at the marker, on recording
 * 'q' and on a full buffer, never on a death.
 *
 * The composition, in this order:
 *
 *   1. p_level_reload(): the level's arena is re-laid over the same buffer, the
 *      THINGS run again from the map's own records, leveltime returns to 0, the
 *      player record is G_PlayerReborn's, and the P_Random() position and the
 *      caller's hooks are carried over;
 *   2. p_spawn_specials(): P_SetupLevel's next step, so the doors, lifts, lights
 *      and the sector's sound state a reloaded level needs exist again -- and so
 *      its random draws are taken from the carried position, exactly where
 *      upstream's P_SpawnSpecials takes them.
 *
 * WHY THE RANDOM POSITION IS NOT RESET, IN ONE SENTENCE
 *
 * M_ClearRandom() is called by G_InitNew(), not by P_SetupLevel(): a reload
 * continues the sequence, and reseeding it would desynchronise every monster
 * pain chance and every pellet of the rest of the demo.
 *
 * WHEN TO CALL IT. After the tic whose P_DeathThink set PST_REBORN, and before
 * the next command is read -- G_Ticker's order. A caller that keeps reading
 * commands at a PST_REBORN player would freeze it, which is the defect this
 * function exists to remove.
 *
 * A reload is the same map only: no level transition, no intermission, no menu,
 * no `gamemap` change and no `nextmap` special line. Those are D4.5's.
 *
 * @param level A loaded level whose player is PST_REBORN (any player state is
 *              accepted; the reload itself is unconditional, as G_DoLoadLevel's
 *              is).
 * @return G_DEMO_OK, G_DEMO_ERR_NULL_LEVEL, G_DEMO_ERR_NOT_OPEN,
 *         G_DEMO_ERR_RELOAD if the level or its specials refused, or
 *         G_DEMO_ERR_NO_PLAYER if the reloaded level has no live console player
 *         to drive (a map with no type-1 THING).
 */
g_demo_error_t g_demo_reborn(p_level_t* level);

/* --- G_ExitLevel / G_DoCompleted / G_DoWorldDone ------------------------- */

/**
 * @brief G_InitNew()'s game-state write for a caller starting a game.
 *
 * Sets the episode and map the game is on and clears `secretexit`; the port has
 * no `G_InitNew` beyond this because the map, the arena and the rules belong to
 * the caller.
 */
void g_level_state_init(g_level_state_t* game, uint8_t episode, uint8_t map);

/**
 * @brief G_ExitLevel(): `secretexit = false; gameaction = ga_completed;`.
 *
 * The line-special path does not call this (p_spec.c sets
 * `p_level_t::exitRequest` instead and links without g_game.c); it is the same
 * write for a caller that has no line, e.g. the sector-11 finale or a test.
 */
void g_exit_level(g_level_state_t* game);

/**
 * @brief G_SecretExitLevel(): `secretexit = true; gameaction = ga_completed;`.
 *
 * The shareware game is non-commercial, so upstream's "if no Wolf3D levels"
 * guard does not clear the flag. A secret exit at the boss map is still
 * overridden by G_DoCompleted's victory branch, which g_completed_next()
 * reproduces.
 */
void g_secret_exit_level(g_level_state_t* game);

/**
 * @brief G_Ticker's read of a level's exit request, as G_DoCompleted's switch.
 *
 * `p_level_t::exitRequest` is P_EXIT_NONE, P_EXIT_NORMAL or P_EXIT_SECRET. A
 * normal request is G_ExitLevel()'s write (clears `secretexit`); a secret one is
 * G_SecretExitLevel()'s. A level with no request changes nothing, which is what
 * G_Ticker does when no line set `gameaction`.
 */
void g_exit_from_level(g_level_state_t* game, const p_level_t* level);

/**
 * @brief G_DoCompleted()'s next-level decision, without the intermission.
 *
 * Upstream, for a non-commercial game (`gamemode != commercial`):
 *
 *   1. `if (gamemap == 8) { gameaction = ga_victory; return; }` -- the boss map
 *      ends the episode, before `secretexit` is consulted;
 *   2. if `secretexit`, `wminfo.next = 8` -> map 9;
 *   3. else if `gamemap == 9`, the return table: episode 1 -> map 4, 2 -> 6,
 *      3 -> 7, 4 -> 3;
 *   4. else `wminfo.next = gamemap` -> map + 1.
 *
 * This function performs that decision and nothing else: no intermission, no
 * par time, no player counts. The caller loads the named map itself.
 *
 * @param game  The game state; `map` and `secretExit` are read.
 * @param out   Receives the next level (1-based) or the victory record.
 * @return 1 when @p out names a level, 0 when it names the episode's victory,
 *         -1 when the state names an episode or map upstream's table cannot
 *         describe (the port ships episode 1; the table is episode 1..4).
 */
int g_completed_next(g_level_state_t* game, g_level_next_t* out);

/**
 * @brief Writes an episode/map as the WAD's level lump name ("E1M3").
 *
 * Upstream's P_SetupLevel spells the marker with `sprintf(name, "E%iM%i", ...)`;
 * the shareware/registered maps are single-digit, so eight bytes is enough. An
 * episode or map outside 1..9 writes an empty string rather than a truncated
 * name.
 */
void g_level_name(uint8_t episode, uint8_t map, char out[8]);

/**
 * @brief G_DoWorldDone() plus G_DoLoadLevel(): the level transition itself.
 *
 * Upstream's G_DoWorldDone sets `gamestate = GS_LEVEL; gamemap = wminfo.next+1;
 * G_DoLoadLevel();`. This is that write, for a caller that has already opened
 * the next map out of the container/WAD view:
 *
 *   1. write @p next's episode and map into @p game, before the load, as
 *      upstream writes `gamemap` before G_DoLoadLevel;
 *   2. p_level_change(): the new map over the same arena, THINGS run, the player
 *      record rewired, leveltime 0, the P_Random() position carried;
 *   3. p_spawn_specials(): P_SetupLevel's next step, so the new level's movers,
 *      lights and sector sound state exist;
 *   4. require a live console player for the new map.
 *
 * A demo never calls this: g_demo_play() stops on an exit request instead.
 *
 * @return G_LEVEL_OK, or one of g_level_error_t. G_LEVEL_ERR_SETUP leaves the
 *         level zeroed by the refused load (as p_level_change documents); a
 *         later refusal leaves the new map loaded but not fully composed, and
 *         the code names the step that was missing.
 */
g_level_error_t g_do_world_done(p_level_t* level, g_level_state_t* game,
                                const g_level_next_t* next,
                                const doom_map_t* map);

/**
 * @brief Plays the record to its end through the world's own tic.
 *
 * The loop, exactly:
 *
 *   while g_demo_read(demo, &cmd) == G_DEMO_OK:
 *       p_level_tic(level, &cmd, NULL)
 *       run->tics++
 *       if level->playerState.playerstate == PST_REBORN:
 *           g_demo_reborn(level);  run->reloads++
 *       if level->exitRequest != P_EXIT_NONE:
 *           run->stop = EXIT; break
 *   run->stop = MARKER or END
 *
 * so the player's commands come from the stream and from nothing else: no input
 * device, no G_BuildTiccmd, no per-tic callback. p_level_tic() is P_Ticker's
 * order (player think, thinkers, leveltime++), which is what makes the playback a
 * game tic rather than a world-only tick. The reborn step is G_Ticker's and lands
 * between the tic that died and the command that follows it; `run->tics` counts
 * commands, so a reload never moves it.
 *
 * AN EXIT STOPS THE PLAYBACK. Upstream's demo path reaches G_DoCompleted and the
 * intermission from the same game state machine; this driver refuses to follow a
 * transition (D4.4's scope), names the reason G_DEMO_STOP_EXIT, and leaves the
 * stream short. No shareware demo reaches an exit today, so the pins do not
 * move; a demo that does is a finding the shorter `tics` reports.
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
 *         G_DEMO_ERR_NOT_OPEN, G_DEMO_ERR_TICK if the world refused a tic, or
 *         g_demo_reborn()'s code if a death could not be answered.
 */
g_demo_error_t g_demo_play(g_demo_t* demo, p_level_t* level, g_demo_run_t* run);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_G_GAME_H */
