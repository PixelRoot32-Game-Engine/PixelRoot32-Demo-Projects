/*
 * The level transition (S21, task T57). See LevelTransition.h for the rule each
 * carried and each reset field serves; this file is only the two halves.
 */
#include "doomc/LevelTransition.h"

#include <string.h>

namespace doom {
namespace doomc {

void playerCarryTake(const p_player_t& player, PlayerCarry& out) {
    out.health = player.health;
    out.armorpoints = player.armorpoints;
    out.armortype = player.armortype;
    for (int i = 0; i < P_NUMAMMO; ++i) {
        out.ammo[i] = player.ammo[i];
        out.maxammo[i] = player.maxammo[i];
    }
    /* The three array fields are byte arrays, and the record keeps them as byte
     * arrays: memcpy is the copy, and it also carries a slot the enumeration may
     * grow without this function needing a second list. */
    memcpy(out.weaponowned, player.weaponowned, sizeof out.weaponowned);
    memcpy(out.cards, player.cards, sizeof out.cards);
    out.backpack = player.backpack;
    out.readyweapon = player.readyweapon;
    out.pendingweapon = player.pendingweapon;
}

void playerCarryApply(const PlayerCarry& carry, p_player_t& player) {
    player.health = carry.health;
    player.armorpoints = carry.armorpoints;
    player.armortype = carry.armortype;
    for (int i = 0; i < P_NUMAMMO; ++i) {
        player.ammo[i] = carry.ammo[i];
        player.maxammo[i] = carry.maxammo[i];
    }
    memcpy(player.weaponowned, carry.weaponowned, sizeof player.weaponowned);
    memcpy(player.cards, carry.cards, sizeof player.cards);
    player.backpack = carry.backpack;

    /* `readyweapon` indexes weaponinfo[]; `pendingweapon` additionally allows
     * wp_nochange (one past the end). P_SetupPsprites() does
     * `if (pendingweapon == wp_nochange) pendingweapon = readyweapon;`, so an
     * impossible pair has to be repaired here rather than indexed there. */
    player.readyweapon = (carry.readyweapon >= 0 && carry.readyweapon < P_NUMWEAPONS)
                             ? carry.readyweapon
                             : static_cast<std::int32_t>(P_WP_PISTOL);
    player.pendingweapon =
        (carry.pendingweapon >= 0 && carry.pendingweapon <= P_WP_NOCHANGE)
            ? carry.pendingweapon
            : static_cast<std::int32_t>(P_WP_NOCHANGE);
}

int levelExitNext(const p_level_t& level, g_level_state_t& game, g_level_next_t& next) {
    memset(&next, 0, sizeof next);
    if (level.exitRequest == static_cast<std::uint8_t>(P_EXIT_NONE)) {
        /* G_Ticker would not run G_DoCompleted at all: no line set an action, so
         * there is no level to name. */
        return -1;
    }
    /* G_ExitLevel()/G_SecretExitLevel(): the request becomes `secretexit`. */
    g_exit_from_level(&game, &level);
    /* G_DoCompleted's switch, without the intermission. 0 = ga_victory. */
    return g_completed_next(&game, &next);
}

g_level_error_t levelChangeLoad(p_level_t& level, g_level_state_t& game,
                                const g_level_next_t& next, const doom_map_t& map,
                                bool carry) {
    PlayerCarry carried;
    const bool hadPlayer = level.playerState.mo != nullptr;

    if (carry && hadPlayer) {
        /* Before the load: p_level_change() zeroes the level, so the record is
         * the only place the inventory can be during the rebuild. */
        playerCarryTake(level.playerState, carried);
    }

    const g_level_error_t changed = g_do_world_done(&level, &game, &next, &map);
    if (changed != G_LEVEL_OK) {
        /* The caller owns the recovery (FrameDriver closes the world so the
         * frame keeps its last picture); a carry written into a partially
         * composed level would be a second invention this function must not
         * make. */
        return changed;
    }

    if (carry && hadPlayer && level.playerState.mo != nullptr) {
        playerCarryApply(carried, level.playerState);
        /* P_SpawnPlayer's `mobj->health = p->health`: the new avatar is spawned
         * with spawnhealth (100), and P_DamageMobj decrements the *mobj's*
         * health as well as the record's, so a carried 40 % health has to reach
         * both or the next hit kills a 40 % player at 100. */
        level.playerState.mo->health = level.playerState.health;
    }
    return G_LEVEL_OK;
}

}  // namespace doomc
}  // namespace doom
