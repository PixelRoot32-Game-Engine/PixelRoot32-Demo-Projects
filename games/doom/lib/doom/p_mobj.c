/*
 * Map objects: spawn, remove, state transitions, and the mobj thinker.
 *
 * The reference is linuxdoom-1.10 p_mobj.c: P_SpawnMobj, P_RemoveMobj,
 * P_SetMobjState, P_SpawnPlayer and P_SpawnMapThing. Every rule below cites the
 * upstream function it reproduces; where the port differs (indices instead of
 * pointers, counters instead of globals) the difference is stated at the site.
 *
 * THIS TRANSLATION UNIT OWNS THE GENERATED TABLES
 *
 * `info_tables.h` declares mobjinfo[]/states[]/sprnames[] as extern and defines
 * them only for the file that asks for it, so a second includer cannot create a
 * second copy in flash. p_mobj.c is that file: it is the layer that reads the
 * tables most, and every other translation unit reaches them through this
 * header's extern declarations.
 *
 * This is C99 only: no stdio, no floating point, no globals. The arena, the
 * thinker list, the generator and the counters all come from the caller.
 */
#define DOOM_INFO_TABLES_DEFINE 1

#include "p_mobj.h"

#include <string.h>

#include "p_action.h"
#include "p_map.h"
#include "r_bsp.h"
#include "r_tables.h"

/* --- Helpers ------------------------------------------------------------ */

static void set_error(p_mobj_error_t* error, p_mobj_error_t value) {
    if (error != NULL) {
        *error = value;
    }
}

/*
 * A map short widened to 16.16. The shift is done on the unsigned 16-bit value
 * so a negative map coordinate is a defined conversion rather than a signed
 * left shift (doom_map.c's read_fixed16 does the same). P_SpawnPlayer and
 * P_SpawnMapThing both do `mthing->x << FRACBITS`; this is that operation with
 * the undefined part removed.
 */
static fixed_t map_units(int16_t value) {
    return (fixed_t)((uint32_t)(uint16_t)value << FRACBITS);
}

/* The inverse of P_SpawnMapThing's `1 << (gameskill-1)`, with upstream's two
 * special cases: baby uses the easy bit, nightmare uses the hard one. */
static int32_t skill_bit(p_skill_t skill) {
    switch (skill) {
        case P_SKILL_BABY:
            return 1;
        case P_SKILL_NIGHTMARE:
            return 4;
        default:
            return (int32_t)1 << ((int32_t)skill - 1);
    }
}

/* The first mobjinfo record whose doomednum is @p doomednum, or -1. Upstream
 * scans the array the same way (`for (i=0 ; i< NUMMOBJTYPES ; i++) if
 * (mthing->type == mobjinfo[i].doomednum) break;`), and the first match wins:
 * several types share -1, which no THINGS record can name because its type is
 * read as a short and -1 is not a doomednum a map carries. */
static int32_t type_for_doomednum(int16_t doomednum) {
    int32_t type;

    for (type = 0; type < NUM_MOBJ_TYPES; ++type) {
        if (mobjinfo[type].doomednum == (int32_t)doomednum) {
            return type;
        }
    }
    return -1;
}

/* --- Spawn position ------------------------------------------------------ */

/*
 * P_SpawnMobj's P_SetThingPosition step: the subsector the point falls in, and
 * the sector that subsector belongs to (P_GroupLines derives it from the first
 * seg's sidedef, which is what doom_map_subsector_sector() does).
 *
 * Answers P_MOBJ_OK and fills both indices, or P_MOBJ_ERR_NO_SUBSECTOR. The
 * count checks are not defensive padding: doom_map_t exposes the record counts,
 * and a subsector or sector index the map never named would otherwise be read
 * out of the mapped lump.
 */
static p_mobj_error_t locate(const p_mobj_context_t* world, fixed_t x, fixed_t y,
                             uint16_t* subsectorOut, uint16_t* sectorOut,
                             fixed_t* floorOut, fixed_t* ceilingOut) {
    const doom_map_t* map = world->map;
    uint16_t subsector = 0;
    uint16_t sector;
    doom_subsector_t subsectorRecord;
    doom_sector_t sectorRecord;

    if (r_bsp_point_in_subsector(map, x, y, &subsector) != R_BSP_OK) {
        return P_MOBJ_ERR_NO_SUBSECTOR;
    }
    if ((uint32_t)subsector >= map->lumps[DOOM_MAP_SSECTORS].count) {
        return P_MOBJ_ERR_NO_SUBSECTOR;
    }
    doom_map_get_subsector(map, subsector, &subsectorRecord);
    sector = doom_map_subsector_sector(map, &subsectorRecord);
    if ((uint32_t)sector >= map->lumps[DOOM_MAP_SECTORS].count) {
        return P_MOBJ_ERR_NO_SUBSECTOR;
    }

    /* The level's mutable sector array is the truth once it exists (D4.1b);
     * a hand-built context without one reads the map, which is what D4.1a's
     * tests do. */
    if (world->sectors != NULL && (uint32_t)sector < (uint32_t)world->sectorCount) {
        *floorOut = world->sectors[sector].floorHeight;
        *ceilingOut = world->sectors[sector].ceilingHeight;
    } else {
        doom_map_get_sector(map, sector, &sectorRecord);
        *floorOut = sectorRecord.floorHeight;
        *ceilingOut = sectorRecord.ceilingHeight;
    }

    *subsectorOut = subsector;
    *sectorOut = sector;
    return P_MOBJ_OK;
}

p_spawn_rules_t p_spawn_rules_default(void) {
    p_spawn_rules_t rules;

    rules.skill = P_SKILL_MEDIUM;
    rules.playerInGame[0] = 1u;
    rules.playerInGame[1] = 0u;
    rules.playerInGame[2] = 0u;
    rules.playerInGame[3] = 0u;
    rules.netgame = 0u;
    rules.deathmatch = 0u;
    rules.nomonsters = 0u;
    return rules;
}

/* --- P_SpawnMobj -------------------------------------------------------- */

r_mobj_t* p_spawn_mobj(p_mobj_context_t* world, fixed_t x, fixed_t y, fixed_t z,
                       mobjtype_t type, p_mobj_error_t* error) {
    const doom_mobjinfo_t* info;
    const doom_state_t* spawn;
    r_mobj_t* mobj;
    zone_error_t zoneError = ZONE_ERR_OK;
    uint16_t subsector = 0;
    uint16_t sector = 0;
    fixed_t floorHeight = 0;
    fixed_t ceilingHeight = 0;
    p_mobj_error_t located;

    set_error(error, P_MOBJ_OK);
    if (world == NULL) {
        set_error(error, P_MOBJ_ERR_NULL_WORLD);
        return NULL;
    }
    if (world->zone == NULL || world->zone->open == 0) {
        set_error(error, P_MOBJ_ERR_NO_ZONE);
        return NULL;
    }
    if (world->thinkers == NULL) {
        set_error(error, P_MOBJ_ERR_NO_THINKERS);
        return NULL;
    }
    if (world->map == NULL || world->map->open == 0) {
        set_error(error, P_MOBJ_ERR_NO_MAP);
        return NULL;
    }
    if ((uint32_t)type >= NUM_MOBJ_TYPES) {
        set_error(error, P_MOBJ_ERR_BAD_TYPE);
        return NULL;
    }

    /* P_SpawnMobj: `mobj = Z_Malloc (sizeof(*mobj), PU_LEVEL, NULL); memset(...)`.
     * The size is this port's record, not upstream's. */
    mobj = (r_mobj_t*)z_alloc(world->zone, sizeof(r_mobj_t), PU_LEVEL, &zoneError);
    if (mobj == NULL) {
        set_error(error, P_MOBJ_ERR_NO_MEMORY);
        return NULL;
    }
    memset(mobj, 0, sizeof *mobj);

    info = &mobjinfo[type];
    mobj->type = (int16_t)type;
    mobj->x = x;
    mobj->y = y;
    mobj->radius = info->radius;
    mobj->height = info->height;
    mobj->flags = info->flags;
    mobj->health = info->spawnhealth;

    /* `if (gameskill != sk_nightmare) mobj->reactiontime = info->reactiontime;`
     * On nightmare every monster attacks the moment it sees the player, which is
     * exactly the reaction time staying zero. */
    if (world->rules.skill != P_SKILL_NIGHTMARE) {
        mobj->reactiontime = (int16_t)info->reactiontime;
    }

    /* `mobj->lastlook = P_Random () % MAXPLAYERS;` -- one draw per spawn, before
     * the state is set, which is part of the sequence a level load consumes. */
    mobj->lastlook = (int16_t)(p_random(world->random) % MAXPLAYERS);

    /* "do not set the state with P_SetMobjState, because action routines can not
     * be called yet" (p_mobj.c). Copy the four fields directly. */
    spawn = &states[info->spawnstate];
    mobj->state = (int16_t)info->spawnstate;
    mobj->tics = spawn->tics;
    mobj->sprite = spawn->sprite;
    mobj->frame = spawn->frame;

    located = locate(world, x, y, &subsector, &sector, &floorHeight,
                     &ceilingHeight);
    if (located != P_MOBJ_OK) {
        /* Nothing is linked yet, so the block goes straight back and the
         * arena's accounting is what it was. */
        (void)z_free(world->zone, mobj);
        set_error(error, located);
        return NULL;
    }
    mobj->subsector = subsector;
    mobj->sector = sector;
    mobj->floorz = floorHeight;
    mobj->ceilingz = ceilingHeight;

    /* P_SpawnMobj's three cases, and no clamp: see p_mobj.h's header. */
    if (z == ONFLOORZ) {
        mobj->z = mobj->floorz;
    } else if (z == ONCEILINGZ) {
        mobj->z = mobj->ceilingz - info->height;
    } else {
        mobj->z = z;
    }

    /* `mobj->thinker.function.acp1 = (actionf_p1)P_MobjThinker;
     *  P_AddThinker (&mobj->thinker);` */
    if (p_thinker_init(&mobj->thinker, &p_mobj_thinker, P_THINKER_MOBJ) != P_TICK_OK ||
        p_thinker_add(world->thinkers, &mobj->thinker) != P_TICK_OK) {
        /* A fresh block is never already linked, so this is a corrupt context
         * rather than a caller mistake; the block still goes back. */
        (void)z_free(world->zone, mobj);
        set_error(error, P_MOBJ_ERR_NO_THINKERS);
        return NULL;
    }

    return mobj;
}

/* --- P_SpawnPlayer ------------------------------------------------------ */

r_mobj_t* p_spawn_player(p_mobj_context_t* world, const p_mapthing_t* start,
                         p_mobj_error_t* error) {
    r_mobj_t* mobj;

    set_error(error, P_MOBJ_OK);
    if (world == NULL) {
        set_error(error, P_MOBJ_ERR_NULL_WORLD);
        return NULL;
    }
    if (start == NULL) {
        set_error(error, P_MOBJ_ERR_NULL_THING);
        return NULL;
    }
    if (start->type < 1 || start->type > MAXPLAYERS) {
        set_error(error, P_MOBJ_ERR_BAD_PLAYER_START);
        return NULL;
    }
    /* `if (!playeringame[mthing->type-1]) return;` */
    if (!world->rules.playerInGame[start->type - 1]) {
        set_error(error, P_MOBJ_ERR_NOT_IN_GAME);
        return NULL;
    }

    mobj = p_spawn_mobj(world, map_units(start->x), map_units(start->y), ONFLOORZ,
                        MT_PLAYER, error);
    if (mobj == NULL) {
        return NULL;
    }

    /* `if (mthing->type > 1) mobj->flags |= (mthing->type-1)<<MF_TRANSSHIFT;` */
    if (start->type > 1) {
        mobj->flags |= (uint32_t)(start->type - 1) << MF_TRANSSHIFT;
    }
    mobj->angle = ANG45 * (angle_t)(start->angle / 45);
    /* Upstream copies p->health, which is 100 for a reborn player; spawnhealth is
     * the same number and is what this layer can know. */
    mobj->health = mobjinfo[MT_PLAYER].spawnhealth;
    mobj->player = 1u;
    return mobj;
}

/* --- P_SpawnMapThing ---------------------------------------------------- */

p_thing_outcome_t p_spawn_map_thing(p_mobj_context_t* world,
                                    const p_mapthing_t* thing) {
    p_thing_outcome_t outcome;
    int32_t type;
    fixed_t z;

    outcome.result = P_THING_INVALID;
    outcome.mobj = NULL;
    outcome.isPlayerStart = 0u;
    outcome.isDeathmatchStart = 0u;

    if (world == NULL || thing == NULL) {
        return outcome;
    }

    /* 1. Deathmatch starts: recorded by the caller, never a mobj. */
    if (thing->type == 11) {
        outcome.result = P_THING_DEATHMATCH_START;
        outcome.isDeathmatchStart = 1u;
        return outcome;
    }

    /* 2. Player starts: recorded always, spawned only for a playing slot and
     *    only outside deathmatch. This branch is above the skill filter, which
     *    is why a start with options 1 still works on medium. */
    if (thing->type >= 1 && thing->type <= MAXPLAYERS) {
        outcome.result = P_THING_PLAYER_START;
        outcome.isPlayerStart = 1u;
        if (!world->rules.deathmatch && world->rules.playerInGame[thing->type - 1]) {
            outcome.mobj = p_spawn_player(world, thing, NULL);
            if (outcome.mobj == NULL) {
                outcome.result = P_THING_SPAWN_FAILED;
            }
        }
        return outcome;
    }

    /* 3. Anything below a player start is not a thing. Upstream indexes
     *    playerstarts[mthing->type-1] and would read out of bounds; the port
     *    reports the record instead. */
    if (thing->type <= 0) {
        outcome.result = P_THING_INVALID;
        return outcome;
    }

    /* 4. `if (!netgame && (mthing->options & 16)) return;` -- "not in single
     *    player". */
    if (!world->rules.netgame && (thing->options & 16) != 0) {
        outcome.result = P_THING_SKIPPED_SINGLE;
        return outcome;
    }

    /* 5. The skill bit. */
    if ((thing->options & (int16_t)skill_bit(world->rules.skill)) == 0) {
        outcome.result = P_THING_SKIPPED_SKILL;
        return outcome;
    }

    /* 6. The doomednum scan. Upstream I_Errors here; the port names the record
     *    so a level can still load and the caller can count it. */
    type = type_for_doomednum(thing->type);
    if (type < 0) {
        outcome.result = P_THING_UNKNOWN_TYPE;
        return outcome;
    }

    /* 7. `if (deathmatch && mobjinfo[i].flags & MF_NOTDMATCH) return;` */
    if (world->rules.deathmatch && (mobjinfo[type].flags & MF_NOTDMATCH) != 0u) {
        outcome.result = P_THING_SKIPPED_DEATHMATCH;
        return outcome;
    }

    /* 8. `if (nomonsters && (i == MT_SKULL || flags & MF_COUNTKILL)) return;` */
    if (world->rules.nomonsters &&
        ((mobjtype_t)type == MT_SKULL || (mobjinfo[type].flags & MF_COUNTKILL) != 0u)) {
        outcome.result = P_THING_SKIPPED_NOMONSTERS;
        return outcome;
    }

    /* 9. On the ceiling or on the floor. */
    z = (mobjinfo[type].flags & MF_SPAWNCEILING) != 0u ? ONCEILINGZ : ONFLOORZ;

    outcome.mobj = p_spawn_mobj(world, map_units(thing->x), map_units(thing->y), z,
                                (mobjtype_t)type, NULL);
    if (outcome.mobj == NULL) {
        outcome.result = P_THING_SPAWN_FAILED;
        return outcome;
    }

    /* 10. The tail of P_SpawnMapThing, in upstream's order. */
    outcome.mobj->spawnpoint = *thing;
    if (outcome.mobj->tics > 0) {
        outcome.mobj->tics =
            (int16_t)(1 + p_random(world->random) % outcome.mobj->tics);
    }
    outcome.mobj->angle = ANG45 * (angle_t)(thing->angle / 45);
    if ((thing->options & MTF_AMBUSH) != 0) {
        outcome.mobj->flags |= MF_AMBUSH;
    }
    outcome.result = P_THING_SPAWNED;
    return outcome;
}

/* --- Removal ------------------------------------------------------------ */

p_mobj_error_t p_remove_mobj(p_mobj_context_t* world, r_mobj_t* mobj) {
    p_tick_error_t removed;

    if (world == NULL || world->thinkers == NULL) {
        return P_MOBJ_ERR_NULL_WORLD;
    }
    if (mobj == NULL) {
        return P_MOBJ_ERR_NULL_MOBJ;
    }
    if (mobj->thinker.removed != 0u) {
        return P_MOBJ_ERR_ALREADY_REMOVED;
    }

    /* P_RemoveThinker(): the mark, not the unlink. A node that is not in this
     * list is refused and left exactly as it was. */
    removed = p_thinker_remove(world->thinkers, &mobj->thinker);
    if (removed == P_TICK_ERR_NOT_LINKED) {
        return P_MOBJ_ERR_NOT_LINKED;
    }
    if (removed != P_TICK_OK) {
        return P_MOBJ_ERR_NULL_WORLD;
    }
    return P_MOBJ_OK;
}

/* --- P_SetMobjState ----------------------------------------------------- */

/* Upstream has no bound: a table whose zero-tic states form a cycle spins
 * forever. This one gives up after a chain far longer than any real one. */
#define P_MOBJ_STATE_CHAIN_LIMIT 256

int p_set_mobj_state(p_mobj_context_t* world, r_mobj_t* mobj, statenum_t state,
                     p_mobj_error_t* error) {
    int32_t guard = 0;

    set_error(error, P_MOBJ_OK);
    if (world == NULL) {
        set_error(error, P_MOBJ_ERR_NULL_WORLD);
        return 0;
    }
    if (mobj == NULL) {
        set_error(error, P_MOBJ_ERR_NULL_MOBJ);
        return 0;
    }
    if ((uint32_t)state >= NUM_STATES) {
        set_error(error, P_MOBJ_ERR_BAD_STATE);
        return 1;
    }

    for (;;) {
        const doom_state_t* st;

        if (state == S_NULL) {
            /* `mobj->state = (state_t *) S_NULL; P_RemoveMobj (mobj);
             *  return false;` */
            mobj->state = (int16_t)S_NULL;
            (void)p_remove_mobj(world, mobj);
            return 0;
        }

        st = &states[state];
        mobj->state = (int16_t)state;
        mobj->tics = st->tics;
        mobj->sprite = st->sprite;
        mobj->frame = st->frame;

        /* "Call action functions when the state is set" -- and only here, which
         * is why a spawn copies the four fields itself. */
        if (st->action != (uint8_t)ACT_NULL) {
            (void)p_action_run(world, mobj, (action_t)st->action);
        }

        state = (statenum_t)st->nextstate;
        if (st->tics != 0) {
            break;
        }
        /* `} while (!mobj->tics);` -- a zero-tic state is never visible, so its
         * action runs and the chain continues. */
        if (++guard > P_MOBJ_STATE_CHAIN_LIMIT) {
            set_error(error, P_MOBJ_ERR_STATE_CHAIN);
            break;
        }
    }
    return 1;
}

/* --- P_Thrust / P_XYMovement / P_ZMovement ------------------------------- */

void p_thrust(p_mobj_context_t* world, r_mobj_t* mobj, angle_t angle,
              fixed_t move) {
    const uint32_t index = angle >> ANGLETOFINESHIFT;

    (void)world;
    if (mobj == NULL) {
        return;
    }
    /* `mo->momx += FixedMul (move, finecosine[angle>>ANGLETOFINESHIFT]);
     *  mo->momy += FixedMul (move, finesine[angle>>ANGLETOFINESHIFT]);`
     * r_fine_sine is 5/4 of a turn long, so the cosine is the pointer offset. */
    mobj->momx += fixed_mul(move, r_fine_sine[index + (FINEANGLES / 4)]);
    mobj->momy += fixed_mul(move, r_fine_sine[index]);
}

/* The sector floor a thing stands on, from the mutable array when the level has
 * one. */
static fixed_t sector_floor(const p_mobj_context_t* world, uint16_t sector) {
    doom_sector_t record;

    if (world->sectors != NULL && sector < world->sectorCount) {
        return world->sectors[sector].floorHeight;
    }
    if (world->map == NULL ||
        (uint32_t)sector >= world->map->lumps[DOOM_MAP_SECTORS].count) {
        return 0;
    }
    doom_map_get_sector(world->map, sector, &record);
    return record.floorHeight;
}

/*
 * P_ExplodeMissile(): stop, enter the death state and lose MF_MISSILE.
 *
 * The deathsound is D4.2e's (no sound layer yet) and the sky-wall hack that
 * decides whether a missile explodes at all lives with the rest of the missile
 * work in D4.2d; everything that changes the thing is here.
 */
static void explode_missile(p_mobj_context_t* world, r_mobj_t* mobj) {
    mobj->momx = 0;
    mobj->momy = 0;
    mobj->momz = 0;

    (void)p_set_mobj_state(world, mobj,
                           (statenum_t)mobjinfo[mobj->type].deathstate, NULL);
    mobj->tics = (int16_t)(mobj->tics - (p_random(world->random) & 3));
    if (mobj->tics < 1) {
        mobj->tics = 1;
    }
    mobj->flags &= ~MF_MISSILE;
}

void p_xy_movement(p_mobj_context_t* world, r_mobj_t* mobj) {
    fixed_t xmove;
    fixed_t ymove;
    fixed_t ptryx;
    fixed_t ptryy;

    if (world == NULL || mobj == NULL) {
        return;
    }

    /* `if (!mo->momx && !momy) { if (MF_SKULLFLY) ... return; }` */
    if (mobj->momx == 0 && mobj->momy == 0) {
        if ((mobj->flags & MF_SKULLFLY) != 0u) {
            /* the skull slammed into something */
            mobj->flags &= ~MF_SKULLFLY;
            mobj->momx = 0;
            mobj->momy = 0;
            mobj->momz = 0;
            (void)p_set_mobj_state(world, mobj,
                                   (statenum_t)mobjinfo[mobj->type].spawnstate,
                                   NULL);
        }
        return;
    }

    /* Momentum is clamped to one tic's MAXMOVE. The halving test below is
     * upstream's `xmove > MAXMOVE/2` and only looks at the positive side; the
     * negative side is left to P_CheckPosition, which is what upstream does. */
    if (mobj->momx > P_MAXMOVE) {
        mobj->momx = P_MAXMOVE;
    } else if (mobj->momx < -P_MAXMOVE) {
        mobj->momx = -P_MAXMOVE;
    }
    if (mobj->momy > P_MAXMOVE) {
        mobj->momy = P_MAXMOVE;
    } else if (mobj->momy < -P_MAXMOVE) {
        mobj->momy = -P_MAXMOVE;
    }

    xmove = mobj->momx;
    ymove = mobj->momy;

    do {
        if (xmove > P_MAXMOVE / 2 || ymove > P_MAXMOVE / 2) {
            ptryx = mobj->x + xmove / 2;
            ptryy = mobj->y + ymove / 2;
            xmove >>= 1;
            ymove >>= 1;
        } else {
            ptryx = mobj->x + xmove;
            ptryy = mobj->y + ymove;
            xmove = 0;
            ymove = 0;
        }

        if (!p_try_move(world, mobj, ptryx, ptryy)) {
            /* blocked move */
            if (mobj->player) {
                /* try to slide along it */
                p_slide_move(world, mobj);
            } else if ((mobj->flags & MF_MISSILE) != 0u) {
                /* Upstream's sky-wall check reads the ceilingline P_CheckPosition
                 * kept; the port keeps no such global, and the check is part of
                 * the missile work D4.2d owns. The explosion itself is below. */
                explode_missile(world, mobj);
            } else {
                mobj->momx = 0;
                mobj->momy = 0;
            }
        }
    } while (xmove != 0 || ymove != 0);

    /* `if (player && player->cheats & CF_NOMOMENTUM)`: there are no cheats in
     * this port (D5), so the branch is absent. */

    if ((mobj->flags & (MF_MISSILE | MF_SKULLFLY)) != 0u) {
        return; /* no friction for missiles ever */
    }
    if (mobj->z > mobj->floorz) {
        return; /* no friction when airborne */
    }

    if ((mobj->flags & MF_CORPSE) != 0u) {
        /* do not stop sliding if halfway off a step with some momentum */
        if (mobj->momx > FRACUNIT / 4 || mobj->momx < -FRACUNIT / 4 ||
            mobj->momy > FRACUNIT / 4 || mobj->momy < -FRACUNIT / 4) {
            if (mobj->floorz != sector_floor(world, mobj->sector)) {
                return;
            }
        }
    }

    if (mobj->momx > -P_STOPSPEED && mobj->momx < P_STOPSPEED &&
        mobj->momy > -P_STOPSPEED && mobj->momy < P_STOPSPEED) {
        /* if in a walking frame, stop moving */
        if (mobj->player &&
            (uint32_t)(mobj->state - S_PLAY_RUN1) < 4u) {
            (void)p_set_mobj_state(world, mobj, S_PLAY, NULL);
        }
        mobj->momx = 0;
        mobj->momy = 0;
    } else {
        mobj->momx = fixed_mul(mobj->momx, P_ORIG_FRICTION);
        mobj->momy = fixed_mul(mobj->momy, P_ORIG_FRICTION);
    }
}

void p_z_movement(p_mobj_context_t* world, r_mobj_t* mobj) {
    fixed_t dist;
    fixed_t delta;

    if (world == NULL || mobj == NULL) {
        return;
    }

    /* `if (mo->player && mo->z < mo->floorz)` squashes the player's viewheight.
     * This port has no player_t view state yet (D5), so nothing happens here. */

    /* adjust height */
    mobj->z += mobj->momz;

    if ((mobj->flags & MF_FLOAT) != 0u && mobj->target != NULL) {
        /* float down towards target if too close */
        if ((mobj->flags & (MF_SKULLFLY | MF_INFLOAT)) == 0u) {
            dist = p_aprox_distance(mobj->x - mobj->target->x,
                                    mobj->y - mobj->target->y);
            delta = (mobj->target->z + (mobj->height >> 1)) - mobj->z;

            if (delta < 0 && dist < -(delta * 3)) {
                mobj->z -= P_FLOATSPEED;
            } else if (delta > 0 && dist < (delta * 3)) {
                mobj->z += P_FLOATSPEED;
            }
        }
    }

    /* clip movement */
    if (mobj->z <= mobj->floorz) {
        /* hit the floor */
        if ((mobj->flags & MF_SKULLFLY) != 0u) {
            /* the skull slammed into something */
            mobj->momz = -mobj->momz;
        }
        if (mobj->momz < 0) {
            /* Upstream's player branch here squashes the viewheight and plays
             * sfx_oof when the landing is hard; no player view (D5) and no
             * sound layer (D4.2e) exist yet. */
            mobj->momz = 0;
        }
        mobj->z = mobj->floorz;

        if ((mobj->flags & MF_MISSILE) != 0u &&
            (mobj->flags & MF_NOCLIP) == 0u) {
            explode_missile(world, mobj);
            return;
        }
    } else if ((mobj->flags & MF_NOGRAVITY) == 0u) {
        if (mobj->momz == 0) {
            mobj->momz = -P_GRAVITY * 2;
        } else {
            mobj->momz -= P_GRAVITY;
        }
    }

    if (mobj->z + mobj->height > mobj->ceilingz) {
        /* hit the ceiling */
        if (mobj->momz > 0) {
            mobj->momz = 0;
        }
        mobj->z = mobj->ceilingz - mobj->height;

        if ((mobj->flags & MF_SKULLFLY) != 0u) {
            /* the skull slammed into something */
            mobj->momz = -mobj->momz;
        }

        if ((mobj->flags & MF_MISSILE) != 0u &&
            (mobj->flags & MF_NOCLIP) == 0u) {
            explode_missile(world, mobj);
            return;
        }
    }
}

/* --- P_MobjThinker ------------------------------------------------------ */

void p_mobj_thinker(p_thinker_t* thinker, void* context) {
    p_mobj_context_t* world = (p_mobj_context_t*)context;
    r_mobj_t* mobj = p_mobj_of(thinker);

    if (mobj == NULL || world == NULL) {
        return;
    }

    /* Upstream's two momenta tests and the `acv == -1` ("was I removed?")
     * return after each. P_CheckSight/P_NoiseAlert are D4.2e's and are not
     * called here. */
    if (mobj->momx != 0 || mobj->momy != 0 ||
        (mobj->flags & MF_SKULLFLY) != 0u) {
        p_xy_movement(world, mobj);
        if (mobj->thinker.removed != 0u) {
            return; /* mobj was removed */
        }
    }
    if (mobj->z != mobj->floorz || mobj->momz != 0) {
        p_z_movement(world, mobj);
        if (mobj->thinker.removed != 0u) {
            return; /* mobj was removed */
        }
    }

    if (mobj->tics != -1) {
        mobj->tics -= 1;
        if (mobj->tics == 0) {
            /* `if (!P_SetMobjState (mobj, mobj->state->nextstate)) return;` --
             * the state machine may free-reach S_NULL, which marks the mobj for
             * removal; the walk finishes the unlink. */
            (void)p_set_mobj_state(world, mobj,
                                   (statenum_t)states[mobj->state].nextstate, NULL);
        }
    }
    /* A state with tics == -1 is permanent (upstream's else branch is the
     * nightmare respawn, which D4.2 owns). */
}

/* --- Casting ------------------------------------------------------------ */

r_mobj_t* p_mobj_of(p_thinker_t* thinker) {
    if (thinker == NULL || thinker->kind != (int16_t)P_THINKER_MOBJ) {
        return NULL;
    }
    return (r_mobj_t*)thinker;
}

const r_mobj_t* p_mobj_of_const(const p_thinker_t* thinker) {
    return p_mobj_of((p_thinker_t*)thinker);
}
