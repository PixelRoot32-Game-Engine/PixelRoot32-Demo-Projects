/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

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
 * D4.2c owns the walking avatar: turning, forward and side momentum, friction
 * through the real collision core (p_mobj.c/p_map.c), the view height that follows
 * the floor and the bob, and the use trace that opens a door. D4.2d1 added damage,
 * death and pickups; D4.2d2 adds the weapon state machine, the weapon change and
 * the fire button, each at its own site below. P_DeathThink is real since D4.4;
 * monster AI and the HUD are other units, and every deferred field and button
 * below names its owner instead of being dropped silently.
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
#include <string.h>

#include "m_fixed.h"
#include "p_mobj.h"
#include "p_pspr.h"

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

/* --- Item and weapon enumeration (doomdef.h) ------------------------------ */

/*
 * The four enumerations d_player.h and doomdef.h define, with upstream's values
 * and upstream's order. The numbers are the save/demo layout and the ammo
 * table's indices, so they are not free to renumber; the `P_` prefix only says
 * that this port owns the definition.
 *
 *   p_ammo_t    ammotype_t.  The four pools, then NUMAMMO, then the "unlimited"
 *               sentinel a weapon with no ammo names (fist, chainsaw).
 *   p_weapon_t  weapontype_t.  Nine slots; `pendingweapon` holds one of these
 *               (or P_WP_NOCHANGE, upstream's wp_nochange, one past the end).
 *   p_card_t    card_t.  The three keycards and the three skull keys, in
 *               upstream's order.
 *   p_power_t   powertype_t.  The six power-ups; the array holds tic counters,
 *               not flags, because P_GivePower stores a duration.
 */
typedef enum p_ammo {
    P_AM_CLIP = 0,
    P_AM_SHELL = 1,
    P_AM_CELL = 2,
    P_AM_MISL = 3,
    P_NUMAMMO = 4,
    P_AM_NOAMMO = 5
} p_ammo_t;

typedef enum p_weapon {
    P_WP_FIST = 0,
    P_WP_PISTOL = 1,
    P_WP_SHOTGUN = 2,
    P_WP_CHAINGUN = 3,
    P_WP_MISSILE = 4,
    P_WP_PLASMA = 5,
    P_WP_BFG = 6,
    P_WP_CHAINSAW = 7,
    P_WP_SUPERSHOTGUN = 8,
    P_NUMWEAPONS = 9,
    P_WP_NOCHANGE = 9
} p_weapon_t;

typedef enum p_card {
    P_IT_BLUECARD = 0,
    P_IT_YELLOWCARD = 1,
    P_IT_REDCARD = 2,
    P_IT_BLUESKULL = 3,
    P_IT_YELLOWSKULL = 4,
    P_IT_REDSKULL = 5,
    P_NUMCARDS = 6
} p_card_t;

typedef enum p_power {
    P_PW_INVULNERABILITY = 0,
    P_PW_STRENGTH = 1,
    P_PW_INVISIBILITY = 2,
    P_PW_IRONFEET = 3,
    P_PW_ALLMAP = 4,
    P_PW_INFRARED = 5,
    P_NUMPOWERS = 6
} p_power_t;

/* doomdef.h's powerduration_t: the tic count P_GivePower stores. P_TICRATE (35)
 * is p_spec.h's and reaches this header through p_mobj.h. */
#define P_INVULNTICS (30 * P_TICRATE)
#define P_INVISTICS (60 * P_TICRATE)
#define P_INFRATICS (120 * P_TICRATE)
#define P_IRONTICS (60 * P_TICRATE)

/* d_items.c globals: `maxammo[NUMAMMO] = {200,50,300,50}` and
 * `clipammo[NUMAMMO] = {10,4,20,1}`. G_PlayerReborn copies maxammo into the
 * player record, so these are the level-start ceilings; clipammo is the unit
 * P_GiveAmmo's `num` parameter is counted in and lives in p_inter.c. */
#define P_MAXAMMO_CLIP 200
#define P_MAXAMMO_SHELL 50
#define P_MAXAMMO_CELL 300
#define P_MAXAMMO_MISL 50

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
    BT_ATTACK = 1,              /* fire; honoured by A_WeaponReady -> P_FireWeapon */
    BT_USE = 2,                 /* honoured: P_UseLines through the latch */
    BT_CHANGE = 4,              /* weapon change; honoured by P_PlayerThink */
    BT_WEAPONMASK = (8 + 16 + 32), /* the weapon number inside BT_CHANGE */
    BT_WEAPONSHIFT = 3,         /* BT_WEAPONMASK >> BT_WEAPONSHIFT */
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
 *   P_PST_DEAD    dead; P_DeathThink runs for as long as this is set
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
 *   armorpoints      the armor total, up to 200; P_DamageMobj spends it and
 *                    P_TouchSpecialThing raises it (D4.2d1).
 *   armortype        0 none, 1 green (damage/3 absorbed), 2 blue (damage/2);
 *                    P_DamageMobj reads it (D4.2d1).
 *   powers[]         the six power-up tic counters, P_INVULNTICS and friends.
 *                    P_DamageMobj reads pw_invulnerability; the counters tick
 *                    down in P_PlayerThink, which is T14's half (D4.2d1).
 *   ammo[], maxammo[] the four pools and their ceilings, P_GiveAmmo's subject
 *                    (D4.2d1). G_PlayerReborn's 50 bullets and the {200,50,
 *                    300,50} ceilings are p_player_wire's.
 *   weaponowned[]    one byte per weapon; P_GiveWeapon sets it (D4.2d1).
 *   pendingweapon    the weapon a pickup selected; P_GiveWeapon writes it and
 *                    T14's P_BringUpWeapon consumes it.
 *   cards[], backpack  the six keys and the doubled-ammo flag: P_GiveCard and
 *                    the backpack arm of P_TouchSpecialThing (D4.2d1).
 *   attackdown       "button down last tic"; A_WeaponReady's fire latch (here).
 *   usedown          "button down last tic"; P_PlayerThink's use latch (here).
 *   refire           A_ReFire's refire counter (here): nonzero makes P_GunShot's
 *                    bullets drift.
 *   readyweapon      the weapon actually in hand. It is what A_WeaponReady, every
 *                    A_Fire*, P_CheckAmmo, P_BringUpWeapon and P_DropWeapon read,
 *                    and what P_GiveAmmo's auto-select tail compares. G_PlayerReborn
 *                    sets it to the pistol.
 *   psprites[]       p_pspr.h's two overlay layers: the gun and its flash. The
 *                    machine that advances them is p_pspr.c's (D4.2d2).
 *   pspritesStarted  G_PlayerReborn calls P_SetupPsprites. This port's reborn step
 *                    is the header-only p_player_wire() (so p_setup.c keeps
 *                    linking without p_user.c) and the weapon table lives in
 *                    p_pspr.c, so the one call is deferred to the first live
 *                    P_PlayerThink; this bit makes it happen exactly once.
 *   killcount        intermission totals; P_KillMobj raises them (D4.2d1).
 *   itemcount        intermission totals; P_TouchSpecialThing raises them.
 *   secretcount      intermission totals; P_PlayerInSpecialSector raises it for
 *                    a sector special 9 (p_spec.c) and the sector is cleared.
 *   damagecount      the red screen-flash counter; P_DamageMobj raises it and
 *                    P_PlayerThink decrements it.
 *   bonuscount       the gold screen-flash counter; a pickup adds BONUSADD.
 *   extralight       muzzle-flash light; A_Light0/1/2 (p_action.c) write it, read
 *                    by the HUD (D5).
 *   fixedcolormap    the power-up colormap; powers are D4.2d1's, the colormap
 *                    selection is the HUD's (D5), so it stays 0.
 *   attacker         who last dealt damage; P_DamageMobj (D4.2d1).
 *   message          the HUD's one-line message; the HUD owns the text (D5).
 *   didsecret        "the secret level was finished"; G_DoCompleted (D4.3).
 *
 * DELIBERATELY ABSENT, with the owner:
 *
 *   frags[MAXPLAYERS]                            deathmatch; D5.
 *   cheats                                       no cheats exist; D5.
 *   colormap                                     the translation map; D5.
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
 *   usePresses             P_UseLines() calls the live player actually made. A
 *                          demo's USE edges are not all presses: P_DeathThink
 *                          consumes its own BT_USE, and a player killed earlier in
 *                          the same tic never reaches P_PlayerThink's live body.
 *                          The gate compares this against the demo's edges and
 *                          uses it to place each press's ray.
 *   silentSounds           every S_* call the weapon actions and P_FireWeapon make.
 *                          D6.1 plays the same calls through DOOM_SOUND_START under
 *                          DOOM_AUDIO; the count is what the attack gates assert, so
 *                          "the shot was loud" stays a number a gate can pin.
 *                          D4.2d1's death sounds are A_* stubs that count and, since
 *                          D6.1, play; these weapon calls have no stub action to hang
 *                          a counter on, so it lives here.
 *   noiseAlerts            P_FireWeapon's `P_NoiseAlert (mo, mo)`. Waking monsters
 *                          needs P_CheckSight, which is T15's, so a shot counts the
 *                          call instead of making it.
 *   onground               P_MovePlayer's file-scope `onground`; 1 when the
 *                          avatar's z is on its floor. P_DeathThink writes it too,
 *                          because P_MovePlayer does not run for a corpse.
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

    int32_t ammo[P_NUMAMMO];
    int32_t maxammo[P_NUMAMMO];
    int32_t powers[P_NUMPOWERS];
    int32_t pendingweapon;
    int32_t readyweapon;
    p_pspdef_t psprites[P_NUMPSPRITES];
    uint8_t weaponowned[P_NUMWEAPONS];
    uint8_t cards[P_NUMCARDS];
    uint8_t backpack;

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
    uint8_t pspritesStarted;

    uint32_t useTriggers;
    uint32_t usePresses;
    uint32_t silentSounds;
    uint32_t noiseAlerts;
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
 *   p->readyweapon = p->pendingweapon = wp_pistol;
 *   p->weaponowned[wp_fist] = p->weaponowned[wp_pistol] = true;
 *   p->ammo[am_clip] = 50;
 *   for (i = 0; i < NUMAMMO; ++i) p->maxammo[i] = maxammo[i];
 *
 * The ammo and weapon lines are G_PlayerReborn's too, and they matter here
 * because a pickup adds to what the player already carries: a clip on a reborn
 * player is 50 -> 60 rounds, not 0 -> 10.
 *
 * NOT HERE: `P_SetupPsprites(p)`, which G_PlayerReborn also calls. The psprite
 * record needs the weapon table, which is p_pspr.c's, and this function is
 * `static inline` so p_setup.c keeps linking without p_user.c; the call is
 * therefore deferred to the first live P_PlayerThink and made once (see
 * `pspritesStarted`). The psprite timeline is upstream's either way, because
 * P_SetupPsprites runs A_Raise once and P_MovePsprites then continues from there.
 *
 * It is `static inline` because p_setup.c must wire the record during the
 * THINGS scan without hard-linking p_user.c: the two pre-existing gates
 * (tools/specials_check.py and tools/things_check.py) build p_setup.c with
 * their own source list and must keep building unchanged. The level-bound
 * wrapper below is the same rule with the record already located.
 */
static inline void p_player_wire(p_player_t* player, r_mobj_t* mobj) {
    p_player_t zero;

    if (player == NULL) {
        return;
    }
    /* G_PlayerReborn's `memset (p, 0, sizeof(*p))`. Written as memset rather than
     * a `{0}` initialiser because the record carries arrays of records now
     * (psprites[NUMPSPRITES]) and `{0}` makes -Wmissing-field-initializers fire,
     * which this project's builds reject. */
    memset(&zero, 0, sizeof zero);
    *player = zero;
    player->mo = mobj;
    if (mobj == NULL) {
        return;
    }

    player->usedown = 1;
    player->attackdown = 1;
    player->playerstate = P_PST_LIVE;
    player->health = P_MAXHEALTH;

    /* G_PlayerReborn's item state: the pistol is what is in hand and what a
     * pickup asks for (`p->readyweapon = p->pendingweapon = wp_pistol;`). */
    player->readyweapon = P_WP_PISTOL;
    player->pendingweapon = P_WP_PISTOL;
    player->weaponowned[P_WP_FIST] = 1u;
    player->weaponowned[P_WP_PISTOL] = 1u;
    player->ammo[P_AM_CLIP] = 50;
    player->maxammo[P_AM_CLIP] = P_MAXAMMO_CLIP;
    player->maxammo[P_AM_SHELL] = P_MAXAMMO_SHELL;
    player->maxammo[P_AM_CELL] = P_MAXAMMO_CELL;
    player->maxammo[P_AM_MISL] = P_MAXAMMO_MISL;
}

/**
 * @brief G_PlayerReborn() for a level: wire `level->playerState` to @p mobj.
 *
 * The opening ammo, weapons and both weapon slots are G_PlayerReborn's and are
 * set by p_player_wire(); the psprite setup it also calls (P_SetupPsprites) is
 * deferred to the first live P_PlayerThink, and p_pspr.h says why.
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
 *   3. P_CalcHeight() (P_DeathThink calls it too, when it lands);
 *   3a. P_PlayerInSpecialSector() when the avatar's sector special is nonzero:
 *      the damage floors, the secret sector and E1M8's exit;
 *   4. BT_SPECIAL is cleared: "a special event has no other buttons";
 *   5. BT_CHANGE selects a weapon when it is owned and not already in hand, and
 *      the change is *asked for* through `pendingweapon` -- the actual switch
 *      happens when the weapon psprite can lower itself;
 *   6. BT_USE runs P_UseLines() once per press through `usedown`;
 *   7. P_MovePsprites(): the weapon and its flash advance one tic, which is where
 *      A_WeaponReady sees BT_ATTACK and fires;
 *   8. the power-up counters tick down (strength counts *up*, which is upstream's
 *      fade) and the two screen-flash counters tick down.
 *
 * A PST_DEAD player runs P_DeathThink() and returns: the view falls to six map
 * units, turns toward `attacker` in ANG5 steps, fades `damagecount`, advances the
 * weapon psprite and sets PST_REBORN on BT_USE. PST_REBORN is G_Ticker's state --
 * G_DoReborn reloads the level before P_Ticker runs in vanilla, and g_game.c's
 * g_demo_reborn() is that step here -- so a tic that reaches this layer in that
 * state stops instead of running the live body against a zero-health player.
 *
 * P_PlayerInSpecialSector() is p_spec.c's and runs at step 3a: the avatar's own
 * sector is asked once per tic when its special is nonzero, which is where the
 * damage floors, the secret count and E1M8's sector-11 exit live. The power-up
 * colormaps are the HUD's (D5), so `fixedcolormap` stays 0 while the tic counters
 * themselves are real.
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
