/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * Interactions: damage, death and pickups.
 *
 * The reference is linuxdoom-1.10 p_inter.c, reproduced rule for rule. The
 * differences are the port's conventions and each is stated at its site:
 *
 *   1. the player record is the level's (`world->level->playerState`), not a
 *      pointer stored on the mobj;
 *   2. `I_Error` has no equivalent, so a branch upstream treats as impossible
 *      refuses locally instead of aborting;
 *   3. the HUD message text and haptics are absent (D5); the pickup sounds are
 *      now wired -- one cue per accepted item, chosen by the arm that grants it
 *      and started only after every refusing arm has returned, which is
 *      upstream's own "a refused pickup is silent" shape;
 *   4. `readyweapon` is read by two rules -- P_GiveAmmo's auto-select tail and
 *      P_TouchSpecialThing's berserk branch -- and D4.2d2 added the field they
 *      needed, so neither is omitted any more.
 *
 * The random draws are upstream's, in upstream's order, because the sequence is
 * part of demo fidelity: the fall-forwards coin, the pain chance, the death-tic
 * jitter and (in p_mobj.c) the blood and puff jitter.
 *
 * This is C99 only: no stdio, no floating point, no globals. The zone, the
 * random generator and the player record all come from the caller.
 */
#include "p_inter.h"

#include "p_map.h"
#include "p_pspr.h"
#include "p_setup.h"
#include "r_tables.h"
#include "s_sound.h"

/* --- Small helpers -------------------------------------------------------- */

/*
 * The player record behind a mobj, or NULL.
 *
 * Upstream: `player = target->player;`. The port has one player and the record
 * lives on the level, so this is the whole lookup. A hand-built context (a unit
 * test) has no level and therefore no player: every caller then takes its "not a
 * player" path instead of dereferencing anything.
 */
static p_player_t* player_of(const p_mobj_context_t* world,
                             const r_mobj_t* mobj) {
    if (world == NULL || world->level == NULL || mobj == NULL || !mobj->player) {
        return NULL;
    }
    return &world->level->playerState;
}

/* The sector the thing stands in, from the level's mutable array when it has one
 * (a hand-built context reads the map, which is what the spawn path does too). */
static int16_t sector_special(const p_mobj_context_t* world, uint16_t sector) {
    doom_sector_t record;

    if (world == NULL || world->map == NULL) {
        return 0;
    }
    if (world->sectors != NULL && (uint32_t)sector < (uint32_t)world->sectorCount) {
        return world->sectors[sector].special;
    }
    if ((uint32_t)sector >= world->map->lumps[DOOM_MAP_SECTORS].count) {
        return 0;
    }
    doom_map_get_sector(world->map, sector, &record);
    return record.special;
}

/* --- GET STUFF ----------------------------------------------------------- */

/* d_items.c's clipammo[NUMAMMO] = {10,4,20,1}: rounds in one load of each pool,
 * which is the unit P_GiveAmmo's `num` parameter counts. */
static const int32_t ammo_clip_size[P_NUMAMMO] = {10, 4, 20, 1};

/* The ammo column of d_items.c's weaponinfo[] is p_pspr.c's table now, not a copy
 * here: one row per weapon, with the five psprite state numbers upstream's records
 * also carry, lives in p_weapon_info(). Until D4.2d2 this file could only keep the
 * one column it read, and said so. */

int p_give_ammo(const p_mobj_context_t* world, p_player_t* player, p_ammo_t ammo,
                int num) {
    int oldammo;

    /* `if (ammo == am_noammo) return false;` then upstream's I_Error for a bad
     * type; the port refuses both, because a wrong type is a caller bug and this
     * layer has nothing to abort. */
    if (player == NULL || ammo == P_AM_NOAMMO) {
        return 0;
    }
    if ((int)ammo < 0 || (int)ammo >= (int)P_NUMAMMO) {
        return 0;
    }

    if (player->ammo[ammo] == player->maxammo[ammo]) {
        return 0;
    }

    /* `if (num) num *= clipammo[ammo]; else num = clipammo[ammo]/2;` */
    if (num) {
        num *= (int)ammo_clip_size[ammo];
    } else {
        num = (int)ammo_clip_size[ammo] / 2;
    }

    /* `if (gameskill == sk_baby || gameskill == sk_nightmare) num <<= 1;` --
     * "give double ammo in trainer mode, you'll need in nightmare". */
    if (world != NULL && (world->rules.skill == P_SKILL_BABY ||
                          world->rules.skill == P_SKILL_NIGHTMARE)) {
        num <<= 1;
    }

    /* `oldammo = player->ammo[ammo];` -- captured before the add, because the
     * auto-select tail below only runs when the player was empty. */
    oldammo = player->ammo[ammo];
    player->ammo[ammo] += num;
    if (player->ammo[ammo] > player->maxammo[ammo]) {
        player->ammo[ammo] = player->maxammo[ammo];
    }

    /* "If non zero ammo, don't change up weapons, player was lower on purpose." */
    if (oldammo != 0) {
        return 1;
    }

    /*
     * "We were down to zero, so select a new weapon. Preferences are not user
     * selectable." Upstream switches on the *pool*, and only when the weapon in
     * hand is the one the new ammo would replace: a clip re-arms a bare-fisted
     * player, shells re-arm a fist or a pistol, and so on.
     *
     * This tail is why `readyweapon` had to exist before it could be written:
     * until D4.2d2 there was no field to compare, and both this file and
     * tools/doom_ref.py omitted it. The chainsaw arm upstream also has (a
     * cell/misl case falling through to `default`) is not reachable here.
     */
    switch (ammo) {
        case P_AM_CLIP:
            if (player->readyweapon == (int32_t)P_WP_FIST) {
                if (player->weaponowned[P_WP_CHAINGUN]) {
                    player->pendingweapon = (int32_t)P_WP_CHAINGUN;
                } else {
                    player->pendingweapon = (int32_t)P_WP_PISTOL;
                }
            }
            break;

        case P_AM_SHELL:
            if (player->readyweapon == (int32_t)P_WP_FIST ||
                player->readyweapon == (int32_t)P_WP_PISTOL) {
                if (player->weaponowned[P_WP_SHOTGUN]) {
                    player->pendingweapon = (int32_t)P_WP_SHOTGUN;
                }
            }
            break;

        case P_AM_CELL:
            if (player->readyweapon == (int32_t)P_WP_FIST ||
                player->readyweapon == (int32_t)P_WP_PISTOL) {
                if (player->weaponowned[P_WP_PLASMA]) {
                    player->pendingweapon = (int32_t)P_WP_PLASMA;
                }
            }
            break;

        case P_AM_MISL:
            if (player->readyweapon == (int32_t)P_WP_FIST) {
                if (player->weaponowned[P_WP_MISSILE]) {
                    player->pendingweapon = (int32_t)P_WP_MISSILE;
                }
            }
            break;

        default:
            break;
    }

    return 1;
}

int p_give_weapon(const p_mobj_context_t* world, p_player_t* player,
                  p_weapon_t weapon, int dropped) {
    int gaveammo = 0;
    int gaveweapon;
    p_ammo_t ammo;

    if (player == NULL) {
        return 0;
    }
    if ((int)weapon < 0 || (int)weapon >= (int)P_NUMWEAPONS) {
        return 0;
    }

    /* `if (netgame && deathmatch != 2 && !dropped)` leaves a placed weapon in
     * the world forever. Net play is D5 and the flag is 0 here, so the branch
     * has no observable form in this port. */

    ammo = (p_ammo_t)p_weapon_info((int32_t)weapon)->ammo;
    if (ammo != P_AM_NOAMMO) {
        /* "give one clip with a dropped weapon, two clips with a found one" */
        gaveammo = p_give_ammo(world, player, ammo, dropped ? 1 : 2);
    }

    if (player->weaponowned[weapon]) {
        gaveweapon = 0;
    } else {
        gaveweapon = 1;
        player->weaponowned[weapon] = 1u;
        player->pendingweapon = (int32_t)weapon;
    }

    return gaveweapon || gaveammo;
}

int p_give_body(p_player_t* player, int num) {
    if (player == NULL) {
        return 0;
    }
    if (player->health >= P_MAXHEALTH) {
        return 0;
    }

    player->health += num;
    if (player->health > P_MAXHEALTH) {
        player->health = P_MAXHEALTH;
    }
    /* "mirror mobj health here for Dave": the two are kept equal, which is what
     * P_DamageMobj's two writes rely on. */
    if (player->mo != NULL) {
        player->mo->health = player->health;
    }
    return 1;
}

int p_give_armor(p_player_t* player, int armortype) {
    int32_t hits;

    if (player == NULL) {
        return 0;
    }

    /* `hits = armortype*100; if (player->armorpoints >= hits) return false;`
     * -- armor replaces, it never stacks, and the better kind is the only one a
     * better-armoured player accepts. */
    hits = (int32_t)armortype * 100;
    if (player->armorpoints >= hits) {
        return 0;
    }

    player->armortype = armortype;
    player->armorpoints = hits;
    return 1;
}

void p_give_card(p_player_t* player, p_card_t card) {
    if (player == NULL) {
        return;
    }
    if ((int)card < 0 || (int)card >= (int)P_NUMCARDS) {
        return;
    }
    if (player->cards[card]) {
        return;
    }

    /* `player->bonuscount = BONUSADD;` -- assignment, not addition. */
    player->bonuscount = P_BONUSADD;
    player->cards[card] = 1u;
}

int p_give_power(p_player_t* player, p_power_t power) {
    if (player == NULL) {
        return 0;
    }

    switch (power) {
        case P_PW_INVULNERABILITY:
            player->powers[power] = P_INVULNTICS;
            return 1;
        case P_PW_INVISIBILITY:
            player->powers[power] = P_INVISTICS;
            if (player->mo != NULL) {
                player->mo->flags |= MF_SHADOW;
            }
            return 1;
        case P_PW_INFRARED:
            player->powers[power] = P_INFRATICS;
            return 1;
        case P_PW_IRONFEET:
            player->powers[power] = P_IRONTICS;
            return 1;
        case P_PW_STRENGTH:
            (void)p_give_body(player, 100);
            player->powers[power] = 1;
            return 1;
        default:
            break;
    }

    /* pw_allmap and pw_infrared past their timers: `if (powers[power]) return
     * false; powers[power] = 1;`. */
    if ((int)power < 0 || (int)power >= (int)P_NUMPOWERS) {
        return 0;
    }
    if (player->powers[power]) {
        return 0;
    }
    player->powers[power] = 1;
    return 1;
}

/* --- P_TouchSpecialThing -------------------------------------------------- */

/*
 * The switch below is upstream's, on `special->sprite` rather than on the mobj
 * type: a sprite is what the item looks like and what a dropped copy keeps, so
 * two mobj types that share a sprite (MT_MISC0 and its variants) behave alike by
 * construction.
 *
 * The one-line messages (`player->message = GOTARMOR;`) are the HUD's and are
 * not written here. The pickup sound is: `pickupSfx` starts as sfx_itemup and
 * the weapon and power-up arms raise it to sfx_wpnup / sfx_getpow, so the cue
 * follows the arm that granted the item and a refused arm stays silent.
 */
void p_touch_special_thing(p_mobj_context_t* world, r_mobj_t* special,
                           r_mobj_t* toucher) {
    p_player_t* player;
    int32_t delta;
    int i;
    int dropped;
    /* Upstream starts the pickup cue inside the arm that grants the item. This
     * port carries the arm's choice to the tail and starts it once: every
     * refusing arm returns before then, so a refused pickup is still silent. */
    int32_t pickupSfx = sfx_itemup;

    if (world == NULL || special == NULL || toucher == NULL) {
        return;
    }
    player = player_of(world, toucher);
    if (player == NULL) {
        return;
    }

    /* `delta = special->z - toucher->z; if (delta > toucher->height || delta <
     * -8*FRACUNIT) return;` -- out of reach. */
    delta = special->z - toucher->z;
    if (delta > toucher->height || delta < -8 * FRACUNIT) {
        return;
    }

    /* "Dead thing touching. Can happen with a sliding player corpse." */
    if (toucher->health <= 0) {
        return;
    }

    dropped = (special->flags & MF_DROPPED) != 0u;

    switch ((spritenum_t)special->sprite) {
        /* armor */
        case SPR_ARM1:
            if (!p_give_armor(player, 1)) {
                return;
            }
            break;

        case SPR_ARM2:
            if (!p_give_armor(player, 2)) {
                return;
            }
            break;

        /* bonus items */
        case SPR_BON1:
            /* `player->health++` can go over 100%, up to 200. */
            player->health += 1;
            if (player->health > 200) {
                player->health = 200;
            }
            if (player->mo != NULL) {
                player->mo->health = player->health;
            }
            break;

        case SPR_BON2:
            player->armorpoints += 1;
            if (player->armorpoints > 200) {
                player->armorpoints = 200;
            }
            if (!player->armortype) {
                player->armortype = 1;
            }
            break;

        case SPR_SOUL:
            player->health += 100;
            if (player->health > 200) {
                player->health = 200;
            }
            if (player->mo != NULL) {
                player->mo->health = player->health;
            }
            break;

        case SPR_MEGA:
            /* `if (gamemode != commercial) return;` -- this port loads DOOM 1,
             * whose shareware/registered mode is not `commercial`, so a
             * megasphere is refused and left on the floor. */
            return;

        /* cards: `P_GiveCard (player, card);` then `if (!netgame) break; return;`
         * -- net play is D5, so every key is taken here. */
        case SPR_BKEY:
            p_give_card(player, P_IT_BLUECARD);
            break;

        case SPR_YKEY:
            p_give_card(player, P_IT_YELLOWCARD);
            break;

        case SPR_RKEY:
            p_give_card(player, P_IT_REDCARD);
            break;

        case SPR_BSKU:
            p_give_card(player, P_IT_BLUESKULL);
            break;

        case SPR_YSKU:
            p_give_card(player, P_IT_YELLOWSKULL);
            break;

        case SPR_RSKU:
            p_give_card(player, P_IT_REDSKULL);
            break;

        /* medikits, heals */
        case SPR_STIM:
            if (!p_give_body(player, 10)) {
                return;
            }
            break;

        case SPR_MEDI:
            if (!p_give_body(player, 25)) {
                return;
            }
            break;

        /* power ups */
        case SPR_PINV:
            if (!p_give_power(player, P_PW_INVULNERABILITY)) {
                return;
            }
            pickupSfx = sfx_getpow;
            break;

        case SPR_PSTR:
            if (!p_give_power(player, P_PW_STRENGTH)) {
                return;
            }
            pickupSfx = sfx_getpow;
            /* `if (player->readyweapon != wp_fist) player->pendingweapon =
             * wp_fist;` -- berserk switches to the fist unless the fist is
             * already out, which is the whole point of the power-up. */
            if (player->readyweapon != (int32_t)P_WP_FIST) {
                player->pendingweapon = (int32_t)P_WP_FIST;
            }
            break;

        case SPR_PINS:
            if (!p_give_power(player, P_PW_INVISIBILITY)) {
                return;
            }
            pickupSfx = sfx_getpow;
            break;

        case SPR_SUIT:
            if (!p_give_power(player, P_PW_IRONFEET)) {
                return;
            }
            pickupSfx = sfx_getpow;
            break;

        case SPR_PMAP:
            if (!p_give_power(player, P_PW_ALLMAP)) {
                return;
            }
            pickupSfx = sfx_getpow;
            break;

        case SPR_PVIS:
            if (!p_give_power(player, P_PW_INFRARED)) {
                return;
            }
            pickupSfx = sfx_getpow;
            break;

        /* ammo */
        case SPR_CLIP:
            /* A dropped clip is half a load: `P_GiveAmmo (player, am_clip, 0)`. */
            if (!p_give_ammo(world, player, P_AM_CLIP, dropped ? 0 : 1)) {
                return;
            }
            break;

        case SPR_AMMO:
            if (!p_give_ammo(world, player, P_AM_CLIP, 5)) {
                return;
            }
            break;

        case SPR_ROCK:
            if (!p_give_ammo(world, player, P_AM_MISL, 1)) {
                return;
            }
            break;

        case SPR_BROK:
            if (!p_give_ammo(world, player, P_AM_MISL, 5)) {
                return;
            }
            break;

        case SPR_CELL:
            if (!p_give_ammo(world, player, P_AM_CELL, 1)) {
                return;
            }
            break;

        case SPR_CELP:
            if (!p_give_ammo(world, player, P_AM_CELL, 5)) {
                return;
            }
            break;

        case SPR_SHEL:
            if (!p_give_ammo(world, player, P_AM_SHELL, 1)) {
                return;
            }
            break;

        case SPR_SBOX:
            if (!p_give_ammo(world, player, P_AM_SHELL, 5)) {
                return;
            }
            break;

        case SPR_BPAK:
            /* The first backpack doubles every ceiling, then every pool gets one
             * load. `backpack` is the "already doubled" flag. */
            if (!player->backpack) {
                for (i = 0; i < (int)P_NUMAMMO; ++i) {
                    player->maxammo[i] *= 2;
                }
                player->backpack = 1u;
            }
            for (i = 0; i < (int)P_NUMAMMO; ++i) {
                (void)p_give_ammo(world, player, (p_ammo_t)i, 1);
            }
            break;

        /* weapons */
        case SPR_BFUG:
            if (!p_give_weapon(world, player, P_WP_BFG, 0)) {
                return;
            }
            pickupSfx = sfx_wpnup;
            break;

        case SPR_MGUN:
            if (!p_give_weapon(world, player, P_WP_CHAINGUN, dropped)) {
                return;
            }
            pickupSfx = sfx_wpnup;
            break;

        case SPR_CSAW:
            if (!p_give_weapon(world, player, P_WP_CHAINSAW, 0)) {
                return;
            }
            pickupSfx = sfx_wpnup;
            break;

        case SPR_LAUN:
            if (!p_give_weapon(world, player, P_WP_MISSILE, 0)) {
                return;
            }
            pickupSfx = sfx_wpnup;
            break;

        case SPR_PLAS:
            if (!p_give_weapon(world, player, P_WP_PLASMA, 0)) {
                return;
            }
            pickupSfx = sfx_wpnup;
            break;

        case SPR_SHOT:
            if (!p_give_weapon(world, player, P_WP_SHOTGUN, dropped)) {
                return;
            }
            pickupSfx = sfx_wpnup;
            break;

        case SPR_SGN2:
            if (!p_give_weapon(world, player, P_WP_SUPERSHOTGUN, dropped)) {
                return;
            }
            pickupSfx = sfx_wpnup;
            break;

        default:
            /* `I_Error ("P_SpecialThing: Unknown gettable thing");` -- the port
             * leaves the item in the world instead of aborting, which is the
             * same "nothing was picked up" answer a caller can observe. */
            return;
    }

    if ((special->flags & MF_COUNTITEM) != 0u) {
        player->itemcount += 1;
    }
    (void)p_remove_mobj(world, special);
    player->bonuscount += P_BONUSADD;

    /* `S_StartSound (player->mo, sfx_itemup|sfx_wpnup|sfx_getpow)` -- the
     * signal the pickup reached the player, after the item is gone. */
    if (player->mo != NULL) {
        DOOM_SOUND_START(player->mo, pickupSfx);
    }
}

/* --- P_KillMobj ----------------------------------------------------------- */

void p_kill_mobj(p_mobj_context_t* world, r_mobj_t* source, r_mobj_t* target) {
    const doom_mobjinfo_t* info;
    mobjtype_t item;
    r_mobj_t* mo;
    p_player_t* sourcePlayer;
    p_player_t* victimPlayer;

    if (world == NULL || target == NULL) {
        return;
    }
    info = &mobjinfo[target->type];

    target->flags &= ~(MF_SHOOTABLE | MF_FLOAT | MF_SKULLFLY);

    /* "a flying skull keeps its gravity" -- it is the one type that falls
     * because it is flying, so its MF_NOGRAVITY survives death. */
    if (target->type != (int16_t)MT_SKULL) {
        target->flags &= ~MF_NOGRAVITY;
    }

    target->flags |= MF_CORPSE | MF_DROPOFF;
    target->height >>= 2;

    if (source != NULL && source->player) {
        sourcePlayer = player_of(world, source);
        /* count for intermission */
        if (sourcePlayer != NULL && (target->flags & MF_COUNTKILL) != 0u) {
            sourcePlayer->killcount += 1;
        }
        /* `source->player->frags[target->player - players]++` when a player
         * kills a player: deathmatch's, D5. */
    } else if (!world->rules.netgame && (target->flags & MF_COUNTKILL) != 0u) {
        /* "count all monster deaths, even those caused by other monsters" --
         * single player credits player 0. */
        if (world->level != NULL) {
            world->level->playerState.killcount += 1;
        }
    }

    if (target->player) {
        victimPlayer = player_of(world, target);
        /* `if (!source) target->player->frags[...]++` -- "count environment
         * kills against you"; deathmatch's, D5. */
        target->flags &= ~MF_SOLID;
        if (victimPlayer != NULL) {
            victimPlayer->playerstate = P_PST_DEAD;
            /* `P_DropWeapon (target->player);` -- the weapon psprite goes down
             * its lower chain, which stops at WEAPONBOTTOM because A_Lower sees
             * PST_DEAD. No item is dropped: the clip a dying monster leaves is the
             * `switch (target->type)` below, not the player's gun. */
            p_drop_weapon(world, victimPlayer);
        }
    }

    if (target->health < -info->spawnhealth && info->xdeathstate != 0) {
        (void)p_set_mobj_state(world, target, (statenum_t)info->xdeathstate,
                               NULL);
    } else {
        (void)p_set_mobj_state(world, target, (statenum_t)info->deathstate, NULL);
    }
    target->tics = (int16_t)(target->tics -
                             (int16_t)(p_random(world->random) & 3));
    if (target->tics < 1) {
        target->tics = 1;
    }

    /* Drop stuff: the object spawned during the death frame of a thing. */
    switch ((mobjtype_t)target->type) {
        case MT_WOLFSS:
        case MT_POSSESSED:
            item = MT_CLIP;
            break;

        case MT_SHOTGUY:
            item = MT_SHOTGUN;
            break;

        case MT_CHAINGUY:
            item = MT_CHAINGUN;
            break;

        default:
            return;
    }

    /* `mo = P_SpawnMobj (target->x, target->y, ONFLOORZ, item); mo->flags |=
     * MF_DROPPED;` -- a dropped item is half a load and is not queued for a
     * deathmatch respawn. */
    mo = p_spawn_mobj(world, target->x, target->y, ONFLOORZ, item, NULL);
    if (mo != NULL) {
        mo->flags |= MF_DROPPED;
    }
}

/* --- P_DamageMobj --------------------------------------------------------- */

/*
 * The thrust upstream applies to a damaged thing that is not clipped:
 *
 *   ang    = R_PointToAngle2 (inflictor->x, inflictor->y, target->x, target->y)
 *   thrust = damage*(FRACUNIT>>3)*100/target->info->mass
 *
 * with the "make fall forwards sometimes" variant on top. The product is
 * evaluated in 64 bits: the 1993 expression overflows a 32-bit int for damage
 * above about 2,600, and no value this port produces is near that, so the two
 * agree everywhere the C original is defined.
 */
static void damage_thrust(p_mobj_context_t* world, r_mobj_t* target,
                          const r_mobj_t* inflictor, int damage) {
    angle_t ang;
    fixed_t thrust;

    if (inflictor == NULL || (target->flags & MF_NOCLIP) != 0u) {
        return;
    }
    /* `|| source->player->readyweapon != wp_chainsaw` is the exception upstream
     * carves out for the chainsaw; `readyweapon` is T14's and nothing in this
     * port can be a chainsaw, so the exception cannot fire and the condition is
     * unconditional here. */

    /* Upstream divides by `target->info->mass` with no test. No shootable type
     * in info_tables.h has zero mass, and the guard is here so a corrupt table
     * refuses instead of trapping. */
    if (mobjinfo[target->type].mass == 0) {
        return;
    }

    ang = p_point_to_angle(target->x - inflictor->x, target->y - inflictor->y);

    thrust = (fixed_t)(((int64_t)damage * (FRACUNIT >> 3) * 100) /
                       (int64_t)mobjinfo[target->type].mass);

    /* `if (damage < 40 && damage > target->health && target->z - inflictor->z >
     * 64*FRACUNIT && (P_Random ()&1))` -- the coin is a real draw from the
     * sequence, so it is taken exactly here. */
    if (damage < 40 && damage > target->health &&
        ((int64_t)target->z - (int64_t)inflictor->z) > (int64_t)64 * FRACUNIT &&
        (p_random(world->random) & 1) != 0) {
        ang += ANG180;
        thrust *= 4;
    }

    ang >>= ANGLETOFINESHIFT;
    target->momx += fixed_mul(thrust, r_fine_sine[ang + (FINEANGLES / 4)]);
    target->momy += fixed_mul(thrust, r_fine_sine[ang]);
}

void p_damage_mobj(p_mobj_context_t* world, r_mobj_t* target,
                   r_mobj_t* inflictor, r_mobj_t* source, int damage) {
    const doom_mobjinfo_t* info;
    p_player_t* player;
    int saved;

    if (world == NULL || target == NULL) {
        return;
    }
    if ((target->flags & MF_SHOOTABLE) == 0u) {
        return; /* shouldn't happen */
    }
    if (target->health <= 0) {
        return;
    }
    info = &mobjinfo[target->type];

    if ((target->flags & MF_SKULLFLY) != 0u) {
        /* the skull slammed into something */
        target->momx = 0;
        target->momy = 0;
        target->momz = 0;
    }

    player = player_of(world, target);
    if (player != NULL && world->rules.skill == P_SKILL_BABY) {
        damage >>= 1; /* take half damage in trainer mode */
    }

    damage_thrust(world, target, inflictor, damage);

    /* player specific */
    if (player != NULL) {
        /* "end of game hell hack": a sector whose special is 11 cannot kill. */
        if (sector_special(world, target->sector) == 11 &&
            damage >= target->health) {
            damage = (int)target->health - 1;
        }

        /* "Below certain threshold, ignore damage in GOD mode, or with INVUL
         * power." CF_GODMODE is a cheat and there are no cheats (D5). */
        if (damage < 1000 && player->powers[P_PW_INVULNERABILITY] != 0) {
            return;
        }

        if (player->armortype != 0) {
            /* `if (armortype == 1) saved = damage/3; else saved = damage/2;` */
            if (player->armortype == 1) {
                saved = damage / P_ARMOR_GREEN_DIVISOR;
            } else {
                saved = damage / P_ARMOR_BLUE_DIVISOR;
            }

            if (player->armorpoints <= saved) {
                /* armor is used up */
                saved = (int)player->armorpoints;
                player->armortype = 0;
            }
            player->armorpoints -= saved;
            damage -= saved;
        }

        player->health -= damage; /* "mirror mobj health here for Dave" */
        if (player->health < 0) {
            player->health = 0;
        }

        player->attacker = source;
        player->damagecount += damage; /* "add damage after armor / invuln" */
        if (player->damagecount > 100) {
            player->damagecount = 100; /* "teleport stomp does 10k points..." */
        }
        /* I_Tactile (haptics) is the platform's, and D5's; no rumble exists. */
    }

    /* do the damage */
    target->health -= damage;
    if (target->health <= 0) {
        p_kill_mobj(world, source, target);
        return;
    }

    /* `if ((P_Random () < target->info->painchance) && !(flags&MF_SKULLFLY))`
     * -- the pain chance is the second draw of the damage path. */
    if (p_random(world->random) < info->painchance &&
        (target->flags & MF_SKULLFLY) == 0u) {
        target->flags |= MF_JUSTHIT; /* fight back! */
        (void)p_set_mobj_state(world, target, (statenum_t)info->painstate, NULL);
    }

    target->reactiontime = 0; /* we're awake now... */

    /* `if ((!target->threshold || target->type == MT_VILE) && source && source
     * != target && source->type != MT_VILE)` -- a thing not intent on another
     * player chases after this one. */
    if ((target->threshold == 0 || target->type == (int16_t)MT_VILE) &&
        source != NULL && source != target &&
        source->type != (int16_t)MT_VILE) {
        target->target = source;
        target->threshold = P_BASETHRESHOLD;
        if (target->state == (int16_t)info->spawnstate && info->seestate != 0) {
            (void)p_set_mobj_state(world, target, (statenum_t)info->seestate,
                                   NULL);
        }
    }
}
