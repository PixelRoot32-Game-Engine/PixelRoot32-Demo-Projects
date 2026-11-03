/*
 * The level transition (S21, task T57): what an exit request becomes.
 *
 * WHAT WAS MISSING, AND WHY IT LIVES HERE
 *
 * The two halves of a level change are already in lib/doom: g_completed_next()
 * is G_DoCompleted's switch (map 8 ends the episode, a secret exit names map 9,
 * map 9 returns to its episode's map, otherwise map + 1) and g_do_world_done() is
 * G_DoWorldDone + G_DoLoadLevel for an already-opened map. What no call site had
 * was the composition between them -- read the level's request, name the next
 * level, load it -- plus the one rule this port's loader cannot express on its
 * own: a *completed* level does not reborn the player.
 *
 * That composition is here, engine-free, so the shipped driver
 * (FrameDriver.cpp) and the host suite (test/unit/test_level) run the same
 * function. A second copy in the test would prove a code path the board never
 * runs, which is the defect this file exists to remove.
 *
 * WHY THE PLAYER HAS TO BE CARRIED AT ALL
 *
 * Upstream linuxdoom-1.10 finishes a level with G_PlayerFinishLevel() and then
 * G_DoLoadLevel() runs P_SetupLevel -> P_LoadThings -> P_SpawnPlayer. That last
 * function calls G_PlayerReborn() only `if (p->playerstate == PST_REBORN)`, and
 * PST_REBORN is set by a *death* (P_DeathThink), not by a completed level -- so
 * a player who walks out of E1M1 keeps the health, armour, ammo, weapons and
 * keys he arrived with, and only the transient fields are cleared.
 *
 * This port's loader cannot express that: p_spawn_map_thing() answers a type-1
 * record with p_player_wire(), which is G_PlayerReborn's body, so every load --
 * the first one, a New Game, a save restore and a transition -- starts from a
 * fresh record. (The comment in g_game.c and p_setup.h that says
 * "G_DoLoadLevel's G_PlayerReborn loop happens inside p_level_init()'s THINGS
 * run" is not what upstream does: G_DoLoadLevel contains no such loop, and
 * upstream does not reborn a player for a completed level at all.) The carry
 * below is therefore applied *after* the load, and it is the whole difference
 * between a load and a transition.
 *
 * WHAT CARRIES, WHAT RESETS, AND THE UPSTREAM RULE EACH ONE SERVES
 *
 *   carried (a completed level does not reborn the player)
 *     health, armorpoints, armortype   the player's damage state; P_DamageMobj
 *                                      reads and writes them, and a level change
 *                                      never touches them upstream.
 *     ammo[], maxammo[]                the pools and their ceilings (a backpack
 *                                      raises the ceilings; the raised ceilings
 *                                      travel with the backpack).
 *     weaponowned[], backpack          what a pickup granted stays granted.
 *     cards[]                          carried by this port per S21/T57 ("the
 *                                      keys" are named as part of the carry).
 *                                      RECORDED DEVIATION: upstream's
 *                                      G_PlayerFinishLevel does clear the keys
 *                                      on a completed level; the brief and the
 *                                      spec name them as carried, so they are.
 *     readyweapon, pendingweapon       the weapon in hand and the one asked for;
 *                                      P_SetupPsprites' first run raises it.
 *
 *   reset (G_PlayerFinishLevel's own list, plus the fields a load recomputes)
 *     powers[]                         `memset (p->powers, 0, ...)`: an
 *                                      invulnerability does not survive a level.
 *     damagecount, bonuscount          `p->damagecount = 0; p->bonuscount = 0;`
 *                                      -- "no palette changes" into a new level.
 *     extralight, fixedcolormap        `p->extralight = 0; p->fixedcolormap = 0;`
 *                                      -- "cancel gun flashes / ir goggles".
 *     attacker                         the thing that last hurt the player; a
 *                                      pointer into the level being left, so it
 *                                      must never cross (P_DeathThink reads it).
 *     killcount, itemcount,            per-level counters: they are the
 *     secretcount                      intermission's numerators, and
 *                                      p_level_init() re-counts the denominators
 *                                      (totalKills/totalItems/totalSecrets).
 *     didsecret                        "the secret level was finished"; it
 *                                      describes one level, not the run.
 *     viewz, viewheight,               recomputed by P_CalcHeight on the new
 *     deltaviewheight, bob             level's first tic; carrying them would
 *                                      put the new camera at the old height.
 *     psprites[], pspritesStarted      P_SetupPsprites' output; the load's wire
 *                                      clears them and the first live tic rebuilds
 *                                      them from the carried readyweapon.
 *     message, usedown, attackdown,    a new level's own text and input edges;
 *     refire, onground, playerstate    G_PlayerReborn's `usedown = attackdown =
 *                                      true` ("don't do anything immediately")
 *                                      is exactly right after a load too.
 *     cmd, useTriggers/usePresses and  this tic's command and the diagnostic
 *     the silentSounds/noiseAlerts      counters; nothing reads them to decide a
 *     counters                         tic and they belong to the level.
 *
 * This file is engine-free: lib/doom headers and nothing from PixelRoot32. No
 * allocation, no stdio, no floating point.
 */
#ifndef DOOM_DOOMC_LEVELTRANSITION_H
#define DOOM_DOOMC_LEVELTRANSITION_H

#include <cstdint>

#include "doom_map.h"
#include "g_game.h"
#include "p_setup.h"
#include "p_user.h"

namespace doom {
namespace doomc {

/**
 * @brief The player fields a completed level carries, as one record.
 *
 * A plain value: taken from the level being left before the load zeroes the
 * record, and written back into the level that was loaded. See the file comment
 * for the rule behind each field and for the ones deliberately absent.
 */
struct PlayerCarry {
    std::int32_t health = 0;
    std::int32_t armorpoints = 0;
    std::int32_t armortype = 0;
    std::int32_t ammo[P_NUMAMMO] = {};
    std::int32_t maxammo[P_NUMAMMO] = {};
    std::uint8_t weaponowned[P_NUMWEAPONS] = {};
    std::uint8_t backpack = 0;
    std::uint8_t cards[P_NUMCARDS] = {};
    std::int32_t readyweapon = P_WP_PISTOL;
    std::int32_t pendingweapon = P_WP_PISTOL;
};

/// Copies the carried fields out of @p player. Touches nothing else.
void playerCarryTake(const p_player_t& player, PlayerCarry& out);

/**
 * @brief Writes a carried record back over a freshly loaded player.
 *
 * Only the carried fields are written, so the load's own reset survives for
 * everything else (powers, the flash counters, the per-level counters, the
 * view). `readyweapon` and `pendingweapon` are range-checked because they index
 * weaponinfo[]: an out-of-range value is replaced with the pistol (ready) and
 * "no change" (pending), which is what a wire record would hold anyway.
 *
 * @param carry  The record taken from the previous level.
 * @param player The new level's player record (its `mo` stays the new avatar).
 */
void playerCarryApply(const PlayerCarry& carry, p_player_t& player);

/**
 * @brief G_DoCompleted's switch: the level's exit request as the next level.
 *
 * g_exit_from_level() (G_ExitLevel / G_SecretExitLevel's `secretexit` write)
 * followed by g_completed_next() (map 8 = victory, secret = 9, 9 = the return
 * table, else + 1). One call, because the two are one decision upstream and a
 * caller that ran only one of them would read a stale `secretexit`.
 *
 * @param level The level that set the request.
 * @param game  The game state; `map` and `secretExit` are read and `secretExit`
 *              may be updated by the exit request.
 * @param next  Receives the next level (1-based) or the victory record.
 * @return 1 when @p next names a level, 0 when it names the episode's victory,
 *         -1 when there is no request or the state is outside upstream's table.
 *         -1 writes a zeroed @p next, so a caller cannot load a level it was
 *         never given.
 */
int levelExitNext(const p_level_t& level, g_level_state_t& game, g_level_next_t& next);

/**
 * @brief G_DoWorldDone + G_DoLoadLevel for an already-opened map.
 *
 * The load is g_do_world_done(): the game state takes @p next's episode and map,
 * p_level_change() re-lays the level over the same arena, and p_spawn_specials()
 * composes it. When @p carry is true the player's carried fields are taken
 * before that call and written back after it (see the file comment); when it is
 * false they are not, which is the right answer for a *new run* (New Game, and
 * the E1M8 placeholder's return) -- G_InitNew is a reborn.
 *
 * The renderer's live reader is the caller's problem and must be unbound before
 * this runs: p_level_change() copies SECTORS through it, so a reader still bound
 * to the level being rebuilt answers stack garbage (the defect
 * reloadWorldAtSkill() documents). This module knows nothing about the
 * renderer; FrameDriver.cpp owns that bind/unbind.
 *
 * @param level The open level to change.
 * @param game  The game state g_do_world_done() writes.
 * @param next  The level to load (or the run's reset), from levelExitNext().
 * @param map   The next level's validated map view; must outlive @p level.
 * @param carry true to carry the player's inventory across the load.
 * @return G_LEVEL_OK, or g_do_world_done()'s code. G_LEVEL_ERR_SETUP leaves the
 *         level zeroed (as p_level_change documents); a later refusal leaves the
 *         new map loaded but not fully composed, and the code names the step.
 */
g_level_error_t levelChangeLoad(p_level_t& level, g_level_state_t& game,
                                const g_level_next_t& next, const doom_map_t& map,
                                bool carry);

}  // namespace doomc
}  // namespace doom

#endif  // DOOM_DOOMC_LEVELTRANSITION_H
