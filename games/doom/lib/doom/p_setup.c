/*
 * Level initialisation: the THINGS run and the level's world.
 *
 * The reference is linuxdoom-1.10 P_SetupLevel()'s THINGS step: P_LoadThings()
 * (p_setup.c) walks the lump in file order and calls P_SpawnMapThing() for every
 * record, after the game-mode filter that drops the commercial-only monsters on
 * a shareware/registered game. This layer is that walk plus the counters, the
 * zone and the thinker list the walk leaves behind.
 *
 * WHAT IT DOES NOT DO
 *
 * It does not read the map: the caller has already run doom_map_open() over the
 * validated lumps. It does not build the sector or blockmap links, the specials
 * or the line lists -- those are P_GroupLines/P_SpawnSpecials, and D4.1b owns
 * the second.
 *
 * ONE DELIBERATE DEVIATION
 *
 * P_LoadThings() silently stops loading when it meets a commercial-only monster
 * on a non-commercial game (`if (spawn == false) break;` -- note `break`, not
 * `continue`, which is upstream's own quirk). This port does not reproduce that
 * filter, for two reasons: the shipped container is the shareware WAD, whose
 * maps contain no commercial-only monsters, and a filter that stops reading the
 * rest of the lump is a data-loss bug rather than a rule worth copying. A type
 * that is not in mobjinfo[] is reported by p_spawn_map_thing() as
 * P_THING_UNKNOWN_TYPE and counted, which is the same information without the
 * truncation.
 *
 * This is C99 only: no stdio, no floating point, no globals. The arena, the
 * state struct and the map belong to the caller.
 */
#include "p_setup.h"

#include <string.h>

/* --- The release callback ------------------------------------------------ */

void p_level_release_thinker(void* context, p_thinker_t* thinker) {
    p_mobj_context_t* world = (p_mobj_context_t*)context;

    if (world == NULL || world->zone == NULL || thinker == NULL) {
        return;
    }
    /* The node is the mobj: p_thinker_t is r_mobj_t's first member. z_free()
     * proves the pointer is a live block of this arena before it writes, so a
     * wrong one is refused rather than corrupting the arena. */
    (void)z_free(world->zone, thinker);
}

/* --- Load ---------------------------------------------------------------- */

/* Records one outcome in the level's counters, and the start arrays. */
static void account(p_level_t* level, const p_mapthing_t* thing,
                    const p_thing_outcome_t* outcome) {
    level->stats.things += 1u;

    if (outcome->isPlayerStart) {
        const uint8_t slot = (uint8_t)(thing->type - 1);

        level->playerStarts[slot] = *thing;
        level->playerStartSeen[slot] = 1u;
        level->stats.playerStarts += 1u;
    }
    if (outcome->isDeathmatchStart) {
        /* `if (deathmatch_p < &deathmatchstarts[10]) { *deathmatch_p++ = *mthing; }`
         * -- the extra records are dropped, and counted as seen. */
        if (level->deathmatchStartCount < P_MAX_DEATHMATCH_STARTS) {
            level->deathmatchStarts[level->deathmatchStartCount] = *thing;
            level->deathmatchStartCount += 1u;
        }
        level->stats.deathmatchStarts += 1u;
    }

    if (outcome->mobj != NULL) {
        level->stats.spawned += 1u;
        if ((outcome->mobj->flags & MF_COUNTKILL) != 0u) {
            level->totalKills += 1;
        }
        if ((outcome->mobj->flags & MF_COUNTITEM) != 0u) {
            level->totalItems += 1;
        }
        /* The console player is slot 0, which is the only player this port has
         * animated so far; a second slot's avatar is still counted and listed,
         * it just is not `level->player`. */
        if (outcome->isPlayerStart && thing->type == 1 && level->player == NULL) {
            level->player = outcome->mobj;
        }
        return;
    }

    switch (outcome->result) {
        case P_THING_SKIPPED_SKILL:
            level->stats.skippedSkill += 1u;
            break;
        case P_THING_SKIPPED_SINGLE:
            level->stats.skippedSingle += 1u;
            break;
        case P_THING_SKIPPED_NOMONSTERS:
            level->stats.skippedNoMonsters += 1u;
            break;
        case P_THING_SKIPPED_DEATHMATCH:
            level->stats.skippedDeathmatch += 1u;
            break;
        case P_THING_UNKNOWN_TYPE:
            level->stats.unknownType += 1u;
            break;
        case P_THING_SPAWN_FAILED:
            level->stats.spawnFailed += 1u;
            break;
        case P_THING_INVALID:
            level->stats.invalidThing += 1u;
            break;
        default:
            /* A player start that spawned nothing (slot not playing) and a
             * deathmatch start are already counted above. */
            break;
    }
}

p_setup_error_t p_level_init(p_level_t* level, const doom_map_t* map, void* arena,
                             size_t size, const p_spawn_rules_t* rules) {
    uint32_t index;
    uint32_t things;

    if (level == NULL) {
        return P_SETUP_ERR_NULL_LEVEL;
    }
    /* A rejected init zeroes the state, so a stale level can never be ticked. */
    memset(level, 0, sizeof *level);

    if (map == NULL) {
        level->error = P_SETUP_ERR_NULL_MAP;
        return level->error;
    }
    if (map->open == 0) {
        level->error = P_SETUP_ERR_MAP_NOT_OPEN;
        return level->error;
    }
    if (arena == NULL) {
        level->error = P_SETUP_ERR_NULL_ARENA;
        return level->error;
    }

    /* The arena is the level's: re-initialising it is the level change, and it
     * discards whatever the previous level left (all of it was PU_LEVEL). */
    if (z_init(&level->zone, arena, size) != ZONE_ERR_OK) {
        memset(level, 0, sizeof *level);
        level->error = P_SETUP_ERR_ARENA;
        return level->error;
    }

    level->map = map;
    level->rules = rules != NULL ? *rules : p_spawn_rules_default();
    (void)p_thinker_list_init(&level->thinkers);
    p_action_init(&level->actions);
    m_random_init(&level->random);

    /* The context points at this struct's own fields; see the header's note
     * about copying the level. */
    level->context.zone = &level->zone;
    level->context.thinkers = &level->thinkers;
    level->context.map = map;
    level->context.random = &level->random;
    level->context.actions = &level->actions;
    level->context.rules = level->rules;

    level->open = 1;

    /* P_LoadThings(): every record, in file order. THINGS is optional in a map
     * view, in which case the level simply has no things. */
    things = map->lumps[DOOM_MAP_THINGS].count;
    for (index = 0; index < things; ++index) {
        doom_thing_t record;
        p_mapthing_t thing;
        p_thing_outcome_t outcome;

        doom_map_get_thing(map, index, &record);
        thing.x = record.x;
        thing.y = record.y;
        thing.angle = record.angle;
        thing.type = record.type;
        thing.options = record.options;

        outcome = p_spawn_map_thing(&level->context, &thing);
        account(level, &thing, &outcome);
    }

    level->error = P_SETUP_OK;
    return P_SETUP_OK;
}

void p_level_shutdown(p_level_t* level) {
    uint32_t freed = 0u;

    if (level == NULL) {
        return;
    }
    if (level->open) {
        /* One tag, one call: PU_LEVEL is exactly what a level owns. */
        (void)z_free_tag(&level->zone, PU_LEVEL, &freed);
    }
    memset(level, 0, sizeof *level);
}

/* --- Ticking and reporting ----------------------------------------------- */

p_tick_error_t p_level_tick(p_level_t* level, p_thinker_run_t* stats) {
    if (level == NULL) {
        return P_TICK_ERR_NULL_LIST;
    }
    if (!level->open) {
        return P_TICK_ERR_NOT_OPEN;
    }
    return p_run_thinkers(&level->thinkers, &p_level_release_thinker,
                          &level->context, stats);
}

uint32_t p_level_mobj_count(const p_level_t* level) {
    const p_thinker_t* thinker;
    uint32_t count = 0u;

    if (level == NULL) {
        return 0u;
    }
    for (thinker = p_thinker_first(&level->thinkers); thinker != NULL;
         thinker = p_thinker_next(thinker)) {
        if (thinker->kind == (int16_t)P_THINKER_MOBJ) {
            count += 1u;
        }
    }
    return count;
}

const r_mobj_t* p_level_first_mobj(const p_level_t* level) {
    const p_thinker_t* thinker;

    if (level == NULL) {
        return NULL;
    }
    for (thinker = p_thinker_first(&level->thinkers); thinker != NULL;
         thinker = p_thinker_next(thinker)) {
        if (thinker->kind == (int16_t)P_THINKER_MOBJ) {
            return p_mobj_of_const(thinker);
        }
    }
    return NULL;
}

const r_mobj_t* p_level_next_mobj(const r_mobj_t* mobj) {
    const p_thinker_t* thinker;

    if (mobj == NULL) {
        return NULL;
    }
    for (thinker = p_thinker_next(&mobj->thinker); thinker != NULL;
         thinker = p_thinker_next(thinker)) {
        if (thinker->kind == (int16_t)P_THINKER_MOBJ) {
            return p_mobj_of_const(thinker);
        }
    }
    return NULL;
}
