/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * Monster AI: A_Look/P_LookForPlayers and the chase and attack half --
 * A_Chase, P_Move/P_TryWalk/P_NewChaseDir, P_CheckMeleeRange,
 * P_CheckMissileRange, A_FaceTarget, A_PosAttack, A_TroopAttack and A_Explode.
 *
 * See p_enemy.h for the upstream order, the one-slot reduction of the player
 * scan, the see sound's draw and the three sound sites the chase and the attack
 * actions start (see/active/attack), and the deliberate gaps in the chase
 * (a monster cannot open a door it bumps).
 *
 * This is C99 only: <stdint.h>, no allocation, no globals, no stdio, no floating
 * point.
 */
#include "p_enemy.h"

#include "m_random.h"
#include "p_inter.h"
#include "p_map.h"
#include "p_setup.h"
#include "p_sight.h"
#include "s_sound.h"

/* --- The direction table P_NewChaseDir walks ------------------------------ */

typedef enum p_dirtype {
    P_DI_EAST = 0,
    P_DI_NORTHEAST,
    P_DI_NORTH,
    P_DI_NORTHWEST,
    P_DI_WEST,
    P_DI_SOUTHWEST,
    P_DI_SOUTH,
    P_DI_SOUTHEAST,
    P_DI_NODIR,
    P_NUMDIRS
} p_dirtype_t;

static const p_dirtype_t p_opposite[P_NUMDIRS] = {
    P_DI_WEST,      P_DI_SOUTHWEST, P_DI_SOUTH, P_DI_SOUTHEAST,
    P_DI_EAST,      P_DI_NORTHEAST, P_DI_NORTH, P_DI_NORTHWEST,
    P_DI_NODIR
};

/* Indexed by `((deltay<0)<<1) + (deltax>0)`. */
static const p_dirtype_t p_diags[4] = {
    P_DI_NORTHWEST, P_DI_NORTHEAST, P_DI_SOUTHWEST, P_DI_SOUTHEAST
};

static const fixed_t p_xspeed[8] = {FRACUNIT, 47000,    0,       -47000,
                                    -FRACUNIT, -47000,  0,       47000};
static const fixed_t p_yspeed[8] = {0,       47000,    FRACUNIT, 47000,
                                    0,       -47000,   -FRACUNIT, -47000};

/* --- P_LookForPlayers ------------------------------------------------------ */

int p_look_for_players(p_mobj_context_t* world, r_mobj_t* actor, int allaround) {
    const p_level_t* level;
    r_mobj_t* playerMobj;
    int c;
    int stop;

    if (world == NULL || actor == NULL) {
        return 0;
    }
    level = world->level;
    if (level == NULL || level->player == NULL) {
        return 0;
    }
    playerMobj = level->player;

    c = 0;
    stop = (actor->lastlook - 1) & 3;

    for (;; actor->lastlook = (int16_t)((actor->lastlook + 1) & 3)) {
        const uint8_t slot = (uint8_t)actor->lastlook;

        /* if (!playeringame[actor->lastlook]) continue; */
        if (slot >= MAXPLAYERS || world->rules.playerInGame[slot] == 0u) {
            continue;
        }

        /* if (c++ == 2 || actor->lastlook == stop) return false; */
        if (c == 2 || actor->lastlook == stop) {
            return 0;
        }
        c += 1;

        /* This port simulates slot 0 only; a higher slot that is "in game" has no
         * mobj, which is the same answer upstream gives a slot whose player is
         * not playing. */
        if (slot != 0u) {
            continue;
        }

        /* if (player->health <= 0) continue; -- dead */
        if (level->playerState.health <= 0) {
            continue;
        }

        /* if (!P_CheckSight (actor, player->mo)) continue; */
        if (!p_check_sight(world, actor, playerMobj)) {
            continue;
        }

        if (!allaround) {
            const angle_t an =
                p_point_to_angle(playerMobj->x - actor->x,
                                 playerMobj->y - actor->y) -
                actor->angle;

            if (an > ANG90 && an < ANG270) {
                const fixed_t dist =
                    p_aprox_distance(playerMobj->x - actor->x,
                                     playerMobj->y - actor->y);

                /* if real close, react anyway */
                if (dist > P_MELEERANGE) {
                    continue; /* behind back */
                }
            }
        }

        actor->target = playerMobj;
        return 1;
    }
}

/* --- A_Look ---------------------------------------------------------------- */

/*
 * The see sound. Upstream's switch chooses it with one P_Random() draw for
 * sfx_posit1..3 (36..38) and one for sfx_bgsit1..2 (39..40); every other see
 * sound takes the default arm and draws nothing. The draw is kept for the random
 * sequence and its value now picks the id that is played -- one switch, both
 * jobs, exactly upstream's shape.
 */
static void see_sound(p_mobj_context_t* world, r_mobj_t* actor,
                      int32_t seesound) {
    int32_t sound = seesound;

    if (world == NULL || world->random == NULL) {
        return;
    }
    if (seesound >= (int32_t)sfx_posit1 && seesound <= (int32_t)sfx_posit3) {
        sound = (int32_t)sfx_posit1 + (p_random(world->random) % 3);
    } else if (seesound >= (int32_t)sfx_bgsit1
               && seesound <= (int32_t)sfx_bgsit2) {
        sound = (int32_t)sfx_bgsit1 + (p_random(world->random) % 2);
    }
    if (sound > 0 && sound < (int32_t)NUMSFX) {
        DOOM_SOUND_START(actor, sound);
    }
}

void p_enemy_look(p_mobj_context_t* world, r_mobj_t* actor) {
    const doom_mobjinfo_t* info;
    r_mobj_t* targ;

    if (world == NULL || actor == NULL) {
        return;
    }
    if ((uint32_t)actor->type >= NUM_MOBJ_TYPES) {
        return;
    }
    info = &mobjinfo[actor->type];

    actor->threshold = 0; /* any shot will wake up */
    targ = p_sector_sound_target(world, actor->sector);
    if (targ != NULL && (targ->flags & MF_SHOOTABLE) != 0u) {
        actor->target = targ;

        if ((actor->flags & MF_AMBUSH) != 0u) {
            if (p_check_sight(world, actor, actor->target)) {
                goto seeyou;
            }
        } else {
            goto seeyou;
        }
    }

    if (!p_look_for_players(world, actor, 0)) {
        return;
    }

seeyou:
    see_sound(world, actor, info->seesound);
    (void)p_set_mobj_state(world, actor, (statenum_t)info->seestate, NULL);
}

/* --- Range tests ---------------------------------------------------------- */

int p_check_melee_range(p_mobj_context_t* world, r_mobj_t* actor) {
    fixed_t dist;
    const r_mobj_t* target;

    if (world == NULL || actor == NULL || actor->target == NULL) {
        return 0;
    }
    target = actor->target;
    dist = p_aprox_distance(target->x - actor->x, target->y - actor->y);
    if (dist >= P_MELEERANGE - 20 * FRACUNIT + target->radius) {
        return 0;
    }
    return p_check_sight(world, actor, target);
}

/*
 * P_CheckMissileRange(). Upstream's four type-specific arms (MT_VILE's 14*64
 * cap, MT_UNDEAD's 196 floor and halving, the cyborg/spider/skull halving and
 * the cyborg's 160 cap) name types this port does not simulate; the two E1M1
 * enemies -- the zombieman and the imp -- take only the generic path, and that
 * is all that is written.
 *
 * The draw happens after `dist` is clamped, so its position in the sequence
 * depends on the distance, exactly as upstream's does.
 */
int p_check_missile_range(p_mobj_context_t* world, r_mobj_t* actor) {
    const doom_mobjinfo_t* info;
    fixed_t dist;

    if (world == NULL || actor == NULL || actor->target == NULL) {
        return 0;
    }
    info = &mobjinfo[actor->type];
    if (!p_check_sight(world, actor, actor->target)) {
        return 0;
    }
    if ((actor->flags & MF_JUSTHIT) != 0u) {
        /* the target just hit the enemy, so fight back! */
        actor->flags &= ~MF_JUSTHIT;
        return 1;
    }
    if (actor->reactiontime) {
        return 0; /* do not attack yet */
    }
    dist = p_aprox_distance(actor->x - actor->target->x,
                            actor->y - actor->target->y) -
           64 * FRACUNIT;
    if (info->meleestate == 0) {
        dist -= 128 * FRACUNIT; /* no melee attack, so fire more */
    }
    dist >>= 16;
    if (dist > 200) {
        dist = 200;
    }
    if (p_random(world->random) < dist) {
        return 0;
    }
    return 1;
}

/* --- P_Move, P_TryWalk, P_NewChaseDir ------------------------------------ */

/*
 * P_Move(): one eight-map-unit step in `movedir`, through P_TryMove.
 *
 * A walker's mobjinfo `speed` is 8 map units, not 8*FRACUNIT, and upstream
 * multiplies it by the 16.16 xspeed[] table with a plain integer product; that
 * is kept. A missile's speed is 16.16 and P_SpawnMissile uses FixedMul; the two
 * shapes are upstream's and both are needed.
 *
 * THE DOOR BRANCH IS A DECLARED GAP. When P_TryMove refuses, upstream opens any
 * special line the move touched and returns "moved" if one opened. This port's
 * p_try_move() consumes that list itself (P_CrossSpecialLine) and does not hand
 * it back, so a monster that bumps a closed door stops instead of opening it.
 * The two E1M1 enemies reach the player through their own room's opening, and
 * no scene here needs a monster to open a door; making it work would mean a new
 * out-parameter on p_try_move, a wider seam than this behaviour justifies.
 *
 * The MF_FLOAT `floatok` branch is absent for the same reason: it needs the
 * position check's `floatok`, and no E1M1 monster floats (the lost soul is out of
 * scope). A floater would stop instead of hovering.
 */
static int p_move(p_mobj_context_t* world, r_mobj_t* actor) {
    fixed_t tryx;
    fixed_t tryy;
    int32_t speed;

    if (actor->movedir == (int16_t)P_DI_NODIR) {
        return 0;
    }
    if (actor->movedir < 0 || actor->movedir >= 8) {
        actor->movedir = (int16_t)P_DI_NODIR;
        return 0;
    }
    speed = mobjinfo[actor->type].speed;
    tryx = actor->x + speed * p_xspeed[actor->movedir];
    tryy = actor->y + speed * p_yspeed[actor->movedir];

    if (!p_try_move(world, actor, tryx, tryy)) {
        return 0;
    }
    if ((actor->flags & MF_FLOAT) == 0u) {
        actor->z = actor->floorz;
    }
    return 1;
}

static int p_try_walk(p_mobj_context_t* world, r_mobj_t* actor) {
    if (!p_move(world, actor)) {
        return 0;
    }
    actor->movecount = (int16_t)(p_random(world->random) & 15);
    return 1;
}

void p_new_chase_dir(p_mobj_context_t* world, r_mobj_t* actor) {
    fixed_t deltax;
    fixed_t deltay;
    p_dirtype_t d[3];
    int tdir;
    p_dirtype_t olddir;
    p_dirtype_t turnaround;

    if (actor == NULL || actor->target == NULL) {
        return;
    }
    olddir = (p_dirtype_t)actor->movedir;
    turnaround = p_opposite[(int)olddir];

    deltax = actor->target->x - actor->x;
    deltay = actor->target->y - actor->y;

    if (deltax > 10 * FRACUNIT) {
        d[1] = P_DI_EAST;
    } else if (deltax < -10 * FRACUNIT) {
        d[1] = P_DI_WEST;
    } else {
        d[1] = P_DI_NODIR;
    }

    if (deltay < -10 * FRACUNIT) {
        d[2] = P_DI_SOUTH;
    } else if (deltay > 10 * FRACUNIT) {
        d[2] = P_DI_NORTH;
    } else {
        d[2] = P_DI_NODIR;
    }

    /* try direct route */
    if (d[1] != P_DI_NODIR && d[2] != P_DI_NODIR) {
        actor->movedir = (int16_t)
            p_diags[((deltay < 0 ? 1 : 0) << 1) + (deltax > 0 ? 1 : 0)];
        if (actor->movedir != (int16_t)turnaround &&
            p_try_walk(world, actor)) {
            return;
        }
    }

    /* try other directions */
    if (p_random(world->random) > 200 ||
        (deltay < 0 ? -deltay : deltay) > (deltax < 0 ? -deltax : deltax)) {
        tdir = (int)d[1];
        d[1] = d[2];
        d[2] = (p_dirtype_t)tdir;
    }

    if (d[1] == turnaround) {
        d[1] = P_DI_NODIR;
    }
    if (d[2] == turnaround) {
        d[2] = P_DI_NODIR;
    }

    if (d[1] != P_DI_NODIR) {
        actor->movedir = (int16_t)d[1];
        if (p_try_walk(world, actor)) {
            return;
        }
    }
    if (d[2] != P_DI_NODIR) {
        actor->movedir = (int16_t)d[2];
        if (p_try_walk(world, actor)) {
            return;
        }
    }

    /* there is no direct path to the player, so pick another direction */
    if (olddir != P_DI_NODIR) {
        actor->movedir = (int16_t)olddir;
        if (p_try_walk(world, actor)) {
            return;
        }
    }

    /* randomly determine direction of search */
    if ((p_random(world->random) & 1) != 0) {
        for (tdir = (int)P_DI_EAST; tdir <= (int)P_DI_SOUTHEAST; ++tdir) {
            if ((p_dirtype_t)tdir != turnaround) {
                actor->movedir = (int16_t)tdir;
                if (p_try_walk(world, actor)) {
                    return;
                }
            }
        }
    } else {
        for (tdir = (int)P_DI_SOUTHEAST; tdir != (int)P_DI_EAST - 1; --tdir) {
            if ((p_dirtype_t)tdir != turnaround) {
                actor->movedir = (int16_t)tdir;
                if (p_try_walk(world, actor)) {
                    return;
                }
            }
        }
    }

    if (turnaround != P_DI_NODIR) {
        actor->movedir = (int16_t)turnaround;
        if (p_try_walk(world, actor)) {
            return;
        }
    }
    actor->movedir = (int16_t)P_DI_NODIR; /* can not move */
}

/* --- A_Chase ------------------------------------------------------------- */

void p_enemy_chase(p_mobj_context_t* world, r_mobj_t* actor) {
    const doom_mobjinfo_t* info;
    int32_t delta;
    int nightmare;

    if (world == NULL || actor == NULL) {
        return;
    }
    if ((uint32_t)actor->type >= NUM_MOBJ_TYPES) {
        return;
    }
    info = &mobjinfo[actor->type];
    nightmare = (world->rules.skill == P_SKILL_NIGHTMARE);

    if (actor->reactiontime) {
        actor->reactiontime = (int16_t)(actor->reactiontime - 1);
    }

    /* modify target threshold */
    if (actor->threshold) {
        if (actor->target == NULL || actor->target->health <= 0) {
            actor->threshold = 0;
        } else {
            actor->threshold = (int16_t)(actor->threshold - 1);
        }
    }

    /* turn towards movement direction if not there yet */
    if (actor->movedir < 8) {
        actor->angle &= (angle_t)(7u << 29);
        delta = (int32_t)(actor->angle - ((angle_t)actor->movedir << 29));
        if (delta > 0) {
            actor->angle -= ANG90 / 2;
        } else if (delta < 0) {
            actor->angle += ANG90 / 2;
        }
    }

    if (actor->target == NULL ||
        (actor->target->flags & MF_SHOOTABLE) == 0u) {
        if (p_look_for_players(world, actor, 1)) {
            return; /* got a new target */
        }
        (void)p_set_mobj_state(world, actor,
                               (statenum_t)info->spawnstate, NULL);
        return;
    }

    /* do not attack twice in a row */
    if ((actor->flags & MF_JUSTATTACKED) != 0u) {
        actor->flags &= ~MF_JUSTATTACKED;
        if (!nightmare) {
            p_new_chase_dir(world, actor);
        }
        return;
    }

    /* check for melee attack */
    if (info->meleestate != 0 && p_check_melee_range(world, actor)) {
        (void)p_set_mobj_state(world, actor, (statenum_t)info->meleestate,
                               NULL);
        return;
    }

    /* check for missile attack */
    if (info->missilestate != 0) {
        if (nightmare || actor->movecount == 0) {
            if (p_check_missile_range(world, actor)) {
                (void)p_set_mobj_state(world, actor,
                                       (statenum_t)info->missilestate, NULL);
                actor->flags |= MF_JUSTATTACKED;
                return;
            }
        }
    }

    /* possibly choose another target */
    if (world->rules.netgame != 0u && actor->threshold == 0 &&
        !p_check_sight(world, actor, actor->target)) {
        if (p_look_for_players(world, actor, 1)) {
            return; /* got a new target */
        }
    }

    /* chase towards player */
    actor->movecount = (int16_t)(actor->movecount - 1);
    if (actor->movecount < 0 || !p_move(world, actor)) {
        p_new_chase_dir(world, actor);
    }

    /* make active sound: upstream draws and plays it on `P_Random () < 3`, so
     * the draw happens whether or not the sound does. */
    if (info->activesound != 0 && world->random != NULL) {
        if (p_random(world->random) < 3) {
            DOOM_SOUND_START(actor, info->activesound);
        }
    }
}

/* --- The attack actions --------------------------------------------------- */

void p_enemy_face_target(p_mobj_context_t* world, r_mobj_t* actor) {
    if (actor == NULL || actor->target == NULL) {
        return;
    }
    actor->flags &= ~MF_AMBUSH;
    actor->angle = p_point_to_angle(actor->target->x - actor->x,
                                    actor->target->y - actor->y);
    if ((actor->target->flags & MF_SHADOW) != 0u &&
        world != NULL && world->random != NULL) {
        actor->angle +=
            (angle_t)((p_random(world->random) - p_random(world->random)) << 21);
    }
}

/*
 * A_PosAttack(): the zombieman's pistol. Aim along the exact bearing, then
 * drift the shot by one random pair and roll 3..15 points. Upstream's
 * `S_StartSound (actor, sfx_pistol)` sits between the facing and the first
 * draw.
 */
void p_enemy_pos_attack(p_mobj_context_t* world, r_mobj_t* actor) {
    angle_t angle;
    fixed_t slope;
    int damage;

    if (world == NULL || actor == NULL || actor->target == NULL) {
        return;
    }
    p_enemy_face_target(world, actor);
    DOOM_SOUND_START(actor, sfx_pistol);
    angle = actor->angle;
    slope = p_aim_line_attack(world, actor, angle, P_MISSILERANGE, NULL);

    angle +=
        (angle_t)((p_random(world->random) - p_random(world->random)) << 20);
    damage = ((int)p_random(world->random) % 5 + 1) * 3;
    p_line_attack(world, actor, angle, P_MISSILERANGE, slope, damage);
}

/*
 * A_SPosAttack(): the shotgun guy's three-shot spread. Upstream plays
 * `sfx_shotgn` *before* the facing (its only attack action where the sound
 * comes first), then takes the aim once on the actor's facing, and each pass
 * spreads *from that base* (not from the previous pass's angle) and rolls its
 * own 3..15 points. The order is draw-drift, draw-drift, draw-damage, shoot,
 * three times.
 */
void p_enemy_spos_attack(p_mobj_context_t* world, r_mobj_t* actor) {
    angle_t base;
    fixed_t slope;
    int pass;

    if (world == NULL || actor == NULL || actor->target == NULL) {
        return;
    }
    DOOM_SOUND_START(actor, sfx_shotgn);
    p_enemy_face_target(world, actor);
    base = actor->angle;
    slope = p_aim_line_attack(world, actor, base, P_MISSILERANGE, NULL);

    for (pass = 0; pass < 3; ++pass) {
        const angle_t angle =
            base + (angle_t)((p_random(world->random) -
                              p_random(world->random)) << 20);
        const int damage = ((int)p_random(world->random) % 5 + 1) * 3;

        p_line_attack(world, actor, angle, P_MISSILERANGE, slope, damage);
    }
}

/*
 * A_SargAttack(): the demon's bite. A_FaceTarget first, then the range test,
 * and the single draw only when the bite can land.
 */
void p_enemy_sarg_attack(p_mobj_context_t* world, r_mobj_t* actor) {
    int damage;

    if (world == NULL || actor == NULL || actor->target == NULL) {
        return;
    }
    p_enemy_face_target(world, actor);
    if (p_check_melee_range(world, actor)) {
        damage = ((int)p_random(world->random) % 10 + 1) * 4;
        p_damage_mobj(world, actor->target, actor, actor, damage);
    }
}

/*
 * A_TroopAttack(): the imp decides at the moment the state runs -- claw if the
 * player is inside P_CheckMeleeRange, fireball otherwise. Both E1M1 enemies'
 * meleestate and missilestate point at this one state for the imp, which is why
 * the decision cannot live in A_Chase.
 */
void p_enemy_troop_attack(p_mobj_context_t* world, r_mobj_t* actor) {
    int damage;

    if (world == NULL || actor == NULL || actor->target == NULL) {
        return;
    }
    p_enemy_face_target(world, actor);
    if (p_check_melee_range(world, actor)) {
        DOOM_SOUND_START(actor, sfx_claw);
        damage = ((int)p_random(world->random) % 8 + 1) * 3;
        p_damage_mobj(world, actor->target, actor, actor, damage);
        return;
    }
    (void)p_spawn_missile(world, actor, actor->target, MT_TROOPSHOT);
}

/*
 * A_Explode(): `P_RadiusAttack (thingy, thingy->target, 128)`. The imp's
 * fireball does *not* carry it -- its burst rows are inert and its damage is the
 * direct missile hit in PIT_CheckThing -- but the barrel's S_BEXP chain and a
 * rocket's S_EXPLODE1 do, and a fireball that hits a barrel reaches them.
 */
void p_enemy_explode(p_mobj_context_t* world, r_mobj_t* actor) {
    if (world == NULL || actor == NULL) {
        return;
    }
    p_radius_attack(world, actor, actor->target, 128);
}

