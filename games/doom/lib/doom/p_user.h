/*
 * The player: the command a tic carries, the player's state record, and the
 * think function that drives them.
 *
 * THE REFERENCE
 *
 * linuxdoom-1.10 p_user.c (P_PlayerThink, P_MovePlayer, P_CalcHeight,
 * P_Thrust) plus d_ticcmd.h's ticcmd_t and d_player.h's player_t. Every
 * non-obvious rule below cites the upstream statement it reproduces. This is
 * C99 only: no allocation, no globals, no stdio, no floating point.
 *
 * WHAT D4.2c OWNS AND WHAT IT DOES NOT
 *
 * D4.2c owns the walking avatar and nothing else: turning, forward and side
 * momentum, friction through the real collision core (p_mobj.c/p_map.c), the
 * view height that follows the floor and the bob, and the use trace that opens
 * a door. Damage, weapons, monsters and P_DeathThink are later units, and every
 * deferred field and button below names its owner instead of being dropped
 * silently.
 *
 * THE COMMAND IS NOT THE STATE
 *
 * Upstream keeps the ticcmd in `player_t.cmd`, copied from the network buffer
 * by G_Ticker before P_PlayerThink runs (g_game.c). p_player_think() receives
 * the command and copies it into the same field, so a caller cannot hand the
 * player a command that outlives the call and a future demo/save layer can read
 * it back from one place.
 *
 * WHY NO `onground` GLOBAL
 *
 * Upstream P_MovePlayer and P_DeathThink write a file-scope `onground` that
 * P_CalcHeight reads. This port has no globals, so the same byte lives in
 * p_player_t and keeps upstream's timing: only P_MovePlayer writes it, so a tic
 * that skips P_MovePlayer (reactiontime) reuses the previous value, exactly as
 * upstream's global does.
 */
#ifndef DOOM_P_USER_H
#define DOOM_P_USER_H

#include <stdint.h>

#include "m_fixed.h"
#include "p_mobj.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Upstream constants --------------------------------------------------- */

/* p_local.h's MAXHEALTH: G_PlayerReborn() sets `p->health = MAXHEALTH`. */
#define P_MAXHEALTH 100

/* p_local.h's VIEWHEIGHT (`41*FRACUNIT`) and p_user.c's MAXBOB (`0x100000`,
 * "16 pixels of bob"). P_VIEWHEIGHT is overridable so the movement gate can
 * perturb it and prove its comparison fails; no shipped build defines it. */
#ifndef P_VIEWHEIGHT
#define P_VIEWHEIGHT (41 * FRACUNIT)
#endif
#define P_MAXBOB 0x100000

/* p_user.c's bob step: `(FINEANGLES/20*leveltime)&FINEMASK`. Integer division
 * is upstream's: 8192/20 is 409 fine angles per tic. */
#define P_BOB_FINE_STEP (FINEANGLES / 20)

/* --- One tic's command ---------------------------------------------------- */

/*
 * ticcmd_t (d_ticcmd.h), reduced to the four fields this port has a consumer
 * for. Upstream's two extra fields are network bookkeeping and are named here
 * so their absence is a decision, not an oversight:
 *
 *   consistancy  the net-game checksum (G_Ticker); D5 owns net play.
 *   chatchar     the chat character (HU_dequeueChatChar); D5 owns chat.
 *
 * Units and signs, field by field:
 *
 *   forwardmove  signed 8-bit speed. P_MovePlayer multiplies it by 2048 to a
 *                16.16 impulse before P_Thrust, so 0x19 (25) is the walk speed
 *                and 0x32 (50) the run speed of G_BuildTiccmd's forwardmove[].
 *                Negative backpedals. Upstream's `char` is signed on every
 *                target this port supports, and the demo reader signs it
 *                explicitly (`(signed char)*demo_p++`), so int8_t is the same
 *                value without depending on char's signedness.
 *   sidemove     the same units. Positive strafes right, which is why
 *                P_MovePlayer applies it at `player->mo->angle - ANG90`.
 *   angleturn    signed 16-bit binary-angle delta in 1/65536 turns. P_MovePlayer
 *                shifts it left 16 before adding, so 640 is one slow-turn step
 *                (G_BuildTiccmd's angleturn[] = {640, 1280, 320}); negative
 *                turns clockwise.
 *   buttons      the BT_* bit word below. One byte, as upstream's `byte`.
 */
typedef struct p_ticcmd {
    int8_t forwardmove;
    int8_t sidemove;
    int16_t angleturn;
    uint8_t buttons;
} p_ticcmd_t;

/*
 * doomdef.h's buttoncode_t, with upstream's values. A bit is listed here once
 * and its comment says whether D4.2c honours it, clears it, or counts it for a
 * later unit.
 *
 * BT_SPECIALMASK overlaps BT_ATTACK|BT_USE on purpose: a special button word
 * is `BT_SPECIAL | BTS_*`, and G_Ticker tests the low two bits after masking.
 */
typedef enum p_button {
    BT_ATTACK = 1,              /* fire; D4.2d (P_FireWeapon) */
    BT_USE = 2,                 /* honoured: P_UseLines through the latch */
    BT_CHANGE = 4,              /* weapon change; D4.2d */
    BT_WEAPONMASK = (8 + 16 + 32), /* the weapon number inside BT_CHANGE; D4.2d */
    BT_WEAPONSHIFT = 3,         /* BT_WEAPONMASK >> BT_WEAPONSHIFT; D4.2d */
    BT_SPECIAL = 128,           /* pause/save event, not a player action */
    BT_SPECIALMASK = 3,         /* BTS_* selector inside BT_SPECIAL */
    BTS_PAUSE = 1,              /* G_Ticker's pause toggle; the game loop's */
    BTS_SAVEGAME = 2,           /* G_Ticker's save request; the game loop's */
    BTS_SAVEMASK = (4 + 8 + 16), /* save slot inside BTS_SAVEGAME */
    BTS_SAVESHIFT = 2           /* BTS_SAVEMASK >> BTS_SAVESHIFT */
} p_button_t;

/*
 * d_player.h's playerstate_t. The values are upstream's: PST_LIVE is 0, so a
 * zeroed record is a live player, which is what G_PlayerReborn leaves.
 *
 *   P_PST_LIVE    playing; P_PlayerThink runs the whole body
 *   P_PST_DEAD    dead; P_DeathThink is D4.2d's, counted here
 *   P_PST_REBORN  the game loop respawns before the next tic (G_Ticker)
 */
typedef enum p_playerstate {
    P_PST_LIVE = 0,
    P_PST_DEAD = 1,
    P_PST_REBORN = 2
} p_playerstate_t;

/* The level whose player this is; the context that owns the sectors and
 * thinkers. Forward-declared so p_setup.h can include this header. */
struct p_level;

/* --- The player record ---------------------------------------------------- */

/*
 * player_t (d_player.h), reduced to the fields D4.2c can mean. Upstream's
 * field order is kept where the fields are kept, because a save/demo layer
 * (D4.3) will archive this record in that order.
 *
 * KEPT NOW, with the upstream rule each one serves:
 *
 *   mo               the avatar mobj (p_spawn_player).
 *   playerstate      PST_LIVE here; PST_DEAD is counted, not faked.
 *   cmd              this tic's command (G_Ticker's copy).
 *   viewz            P_CalcHeight's output: the eye height the renderer uses.
 *   viewheight       the base above `mo->z`; starts at 0 (G_PlayerReborn).
 *   deltaviewheight  the per-tic view-height step; P_CalcHeight's half step.
 *   bob              the bounded, scaled horizontal momentum (MAXBOB).
 *   health           P_MAXHEALTH at spawn (G_PlayerReborn).
 *   armorpoints      the armor totals; items grant them (D4.2d).
 *   armortype        0-2, as upstream; D4.2d's P_DamageMobj reads it.
 *   attackdown       "button down last tic"; P_FireWeapon's latch (D4.2d).
 *   usedown          "button down last tic"; P_PlayerThink's use latch (here).
 *   refire           P_FireWeapon's less-accurate-refire counter (D4.2d).
 *   killcount        intermission totals; P_KillMobj raises them (D4.2d).
 *   itemcount        intermission totals; P_TouchSpecialThing (D4.2d).
 *   secretcount      intermission totals; P_PlayerInSpecialSector (D4.2d).
 *   damagecount      the red screen-flash counter; P_PlayerThink decrements it.
 *   bonuscount       the gold screen-flash counter; P_PlayerThink decrements it.
 *   extralight       muzzle-flash light; P_FireWeapon (D4.2d), read by the HUD.
 *   fixedcolormap    the power-up colormap; powers are D4.2d's, so it stays 0.
 *   attacker         who last dealt damage; P_DamageMobj (D4.2d).
 *   message          the HUD's one-line message; the HUD owns the text (D5).
 *   didsecret        "the secret level was finished"; G_DoCompleted (D4.3).
 *
 * DELIBERATELY ABSENT, with the owner:
 *
 *   powers[NUMPOWERS], cards[NUMCARDS], backpack  power-ups and keys; D4.2d.
 *   frags[MAXPLAYERS]                            deathmatch; D5.
 *   readyweapon, pendingweapon, weaponowned[]    weapons; D4.2d.
 *   ammo[NUMAMMO], maxammo[NUMAMMO]              weapons; D4.2d.
 *   cheats                                       no cheats exist; D5.
 *   colormap                                     the translation map; D5.
 *   psprites[NUMPSPRITES]                        the weapon sprite state
 *                                                machine; D4.2d (p_pspr.c), and
 *                                                declaring it would drag in that
 *                                                state type.
 *
 * Upstream has no momx/momy on player_t: the momentum lives on `mo`, and
 * P_CalcHeight reads `mo->momx` directly. The port follows that, so no copy
 * can disagree with the mobj.
 *
 * THE DIAGNOSTIC COUNTERS
 *
 * Upstream has nowhere to put "this input did nothing yet". Four counters make
 * every deferred path observable in a test or a log:
 *
 *   useTriggers            use presses that triggered a special line.
 *   useTriggered           this tic's answer, cleared at the top of the think.
 *   unsupportedAttack      tics a fire button was pressed (D4.2d owns firing).
 *   unsupportedWeaponChange tics a weapon-change button was pressed (D4.2d).
 *   unsupportedDeath       tics the think ran with playerstate != LIVE (D4.2d
 *                          owns P_DeathThink).
 *   onground               P_MovePlayer's file-scope `onground`; 1 when the
 *                          avatar's z is on its floor.
 */
typedef struct p_player {
    r_mobj_t* mo;
    p_playerstate_t playerstate;
    p_ticcmd_t cmd;

    fixed_t viewz;
    fixed_t viewheight;
    fixed_t deltaviewheight;
    fixed_t bob;

    int32_t health;
    int32_t armorpoints;
    int32_t armortype;

    int32_t attackdown;
    int32_t usedown;
    int32_t refire;

    int32_t killcount;
    int32_t itemcount;
    int32_t secretcount;

    int32_t damagecount;
    int32_t bonuscount;
    int32_t extralight;
    int32_t fixedcolormap;

    r_mobj_t* attacker;
    const char* message;

    uint8_t didsecret;
    uint8_t onground;
    uint8_t useTriggered;

    uint32_t useTriggers;
    uint32_t unsupportedAttack;
    uint32_t unsupportedWeaponChange;
    uint32_t unsupportedDeath;
} p_player_t;

/* --- Public API ---------------------------------------------------------- */

/**
 * @brief G_PlayerReborn()'s live fields, written into a standalone record.
 *
 * This is the part of G_PlayerReborn() every caller needs: clear the record
 * (`memset (p, 0, sizeof(*p))`), then
 *
 *   p->usedown = p->attackdown = true;   "don't do anything immediately"
 *   p->playerstate = PST_LIVE;
 *   p->health = MAXHEALTH;
 *
 * It is `static inline` because p_setup.c must wire the record during the
 * THINGS scan without hard-linking p_user.c: the two pre-existing gates
 * (tools/specials_check.py and tools/things_check.py) build p_setup.c with
 * their own source list and must keep building unchanged. The level-bound
 * wrapper below is the same rule with the record already located.
 */
static inline void p_player_wire(p_player_t* player, r_mobj_t* mobj) {
    const p_player_t zero = {0};

    if (player == NULL) {
        return;
    }
    *player = zero;
    player->mo = mobj;
    if (mobj == NULL) {
        return;
    }

    player->usedown = 1;
    player->attackdown = 1;
    player->playerstate = P_PST_LIVE;
    player->health = P_MAXHEALTH;
}

/**
 * @brief G_PlayerReborn() for a level: wire `level->playerState` to @p mobj.
 *
 * The weapon/ammo defaults are D4.2d's and are named in the struct's comment.
 *
 * @param level The level whose player record is written.
 * @param mobj  The avatar; NULL leaves the record empty.
 */
void p_player_init(struct p_level* level, r_mobj_t* mobj);

/**
 * @brief P_PlayerThink(): one player's tic.
 *
 * Upstream's body, minus the parts later units own:
 *
 *   1. copy the command into `player->cmd` (G_Ticker's memcpy);
 *   2. a live player runs the reactiontime gate and P_MovePlayer;
 *   3. P_CalcHeight() (P_DeathThink calls it too, in D4.2d);
 *   4. BT_SPECIAL is cleared: "a special event has no other buttons";
 *   5. BT_CHANGE and BT_ATTACK are counted, because their handlers belong to
 *      D4.2d; BT_USE runs P_UseLines() once per press through `usedown`;
 *   6. the two screen-flash counters tick down.
 *
 * A player that is not PST_LIVE increments `unsupportedDeath` and returns:
 * P_DeathThink() is D4.2d's and is not faked here.
 *
 * P_PlayerInSpecialSector() (sector damage), P_MovePsprites() and the power-up
 * colormaps are D4.2d's; fixedcolormap therefore stays 0.
 *
 * @param level The level; its playerState record is the subject.
 * @param cmd   This tic's command. NULL is one tic of no input: the caller may
 *              tick a level without an input device.
 */
void p_player_think(struct p_level* level, const p_ticcmd_t* cmd);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_P_USER_H */
