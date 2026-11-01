/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

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

#include "p_map.h"
#include "p_sight.h"

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

/* --- P_LoadSectors / P_LoadLineDefs -------------------------------------- */

/*
 * Copies the two map arrays the simulation mutates into the level's arena:
 * `sectors[]` (P_LoadSectors' step) and `lineSpecial[]` (P_LoadLineDefs'
 * `ld->special`). doom_map is const zero-copy and cannot hold either; see
 * p_spec.h for the full reason. Both are PU_LEVEL, so p_level_shutdown() and
 * the next level load free them together with the mobjs.
 */
static p_setup_error_t load_mutable_map(p_level_t* level) {
    const doom_map_t* map = level->map;
    uint32_t sectorCount = map->lumps[DOOM_MAP_SECTORS].count;
    uint32_t lineCount = map->lumps[DOOM_MAP_LINEDEFS].count;
    uint32_t i;
    zone_error_t zoneError = ZONE_ERR_OK;
    p_sector_t* sectors;
    int16_t* lineSpecial;
    p_sector_sound_t* sectorSound;

    if (sectorCount == 0u || lineCount == 0u ||
        sectorCount > 0xFFFFu || lineCount > 0xFFFFu) {
        return P_SETUP_ERR_ALLOC;
    }

    sectors = (p_sector_t*)z_alloc(&level->zone,
                                   (size_t)sectorCount * sizeof(p_sector_t),
                                   PU_LEVEL, &zoneError);
    if (sectors == NULL) {
        return P_SETUP_ERR_ALLOC;
    }
    for (i = 0; i < sectorCount; ++i) {
        doom_sector_t record;

        doom_map_get_sector(map, (uint16_t)i, &record);
        sectors[i].floorHeight = record.floorHeight;
        sectors[i].ceilingHeight = record.ceilingHeight;
        sectors[i].floorPic = record.floorPic;
        sectors[i].ceilingPic = record.ceilingPic;
        sectors[i].lightLevel = record.lightLevel;
        sectors[i].special = record.special;
        sectors[i].tag = record.tag;
        sectors[i].specialdata = NULL;
    }

    lineSpecial = (int16_t*)z_alloc(&level->zone,
                                    (size_t)lineCount * sizeof(int16_t),
                                    PU_LEVEL, &zoneError);
    if (lineSpecial == NULL) {
        return P_SETUP_ERR_ALLOC;
    }
    for (i = 0; i < lineCount; ++i) {
        lineSpecial[i] = p_line_special(map, (uint16_t)i);
    }

    /* T15a's per-sector sound state: P_RecursiveSound's soundtarget,
     * soundtraversed and validcount, one record per sector, zeroed so "never
     * flooded" is the initial state. It lives in the arena (not in p_sector_t and
     * not inline in the context) because the board's .bss has no room for it and
     * a level change frees it with everything else. */
    sectorSound = (p_sector_sound_t*)z_alloc(
        &level->zone, (size_t)sectorCount * sizeof(p_sector_sound_t), PU_LEVEL,
        &zoneError);
    if (sectorSound == NULL) {
        return P_SETUP_ERR_ALLOC;
    }
    memset(sectorSound, 0, (size_t)sectorCount * sizeof(p_sector_sound_t));

    level->sectors = sectors;
    level->sectorCount = (uint16_t)sectorCount;
    level->lineSpecial = lineSpecial;
    level->lineCount = (uint16_t)lineCount;
    level->context.sectors = sectors;
    level->context.sectorCount = (uint16_t)sectorCount;
    level->context.lineSpecial = lineSpecial;
    level->context.lineCount = (uint16_t)lineCount;
    level->context.sectorSound = sectorSound;
    level->context.sectorSoundCount = (uint16_t)sectorCount;
    level->context.soundValidCount = 0;
    return P_SETUP_OK;
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
            /* G_PlayerReborn(): wire the one record this port drives to the
             * avatar P_SpawnPlayer() just made. This is the single assignment
             * of `level->player`, so the two cannot disagree. p_player_wire()
             * is header-only so this file keeps linking without p_user.c --
             * the two pre-existing gates build it with their own source list. */
            p_player_wire(&level->playerState, outcome->mobj);
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

/*
 * P_SetupLevel's body, with the random position as an input.
 *
 * `carried` is NULL for a first load, which is where upstream's G_InitNew() has
 * already called M_ClearRandom(): the sequence starts at zero. A reload passes
 * the position the world had reached, because P_SetupLevel does not seed
 * anything and its very first draw is P_LoadThings' -- every P_SpawnMapThing
 * draws once for `lastlook` in P_SpawnMobj -- so the carried position has to be
 * in place *before* the THINGS run, not restored after it. Restoring it after
 * would leave the same final index and the wrong `lastlook` and spawn `tics`.
 */
static p_setup_error_t level_load(p_level_t* level, const doom_map_t* map,
                                  void* arena, size_t size,
                                  const p_spawn_rules_t* rules,
                                  const m_random_t* carried) {
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
    if (carried != NULL) {
        level->random = *carried;
    } else {
        m_random_init(&level->random);
    }

    /* The context points at this struct's own fields; see the header's note
     * about copying the level. */
    level->context.zone = &level->zone;
    level->context.thinkers = &level->thinkers;
    level->context.map = map;
    level->context.random = &level->random;
    level->context.actions = &level->actions;
    level->context.rules = level->rules;
    level->context.specStats = &level->specStats;
    /* The context does not own the level, but P_ChangeSector needs the level's
     * mobj walk (p_level_first_mobj/p_level_next_mobj) and p_try_move needs the
     * level's lineSpecial[] to trigger a crossed line. A hand-built context
     * leaves this NULL; both call sites then fall back to the thinker list and
     * skip the trigger, which is what a unit test wants. */
    level->context.level = level;

    /* P_LoadSectors and P_LoadLineDefs' mutable half. A level without them
     * cannot be simulated, so this is a load failure, not a counter. */
    {
        const p_setup_error_t loaded = load_mutable_map(level);

        if (loaded != P_SETUP_OK) {
            (void)z_free_tag(&level->zone, PU_LEVEL, NULL);
            memset(level, 0, sizeof *level);
            level->error = loaded;
            return loaded;
        }
    }

    level->open = 1;

    /* P_InitPicAnims' identity: upstream's translation arrays map every picture
     * to itself until the first P_UpdateSpecials() writes them. -1 is that
     * "nothing has been computed yet" (p_spec.h). */
    level->animTime = P_ANIM_BEFORE_FIRST;

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

p_setup_error_t p_level_init(p_level_t* level, const doom_map_t* map, void* arena,
                             size_t size, const p_spawn_rules_t* rules) {
    /* G_InitNew() has already run M_ClearRandom() for a first load, so the
     * sequence starts at zero: no carried position. */
    return level_load(level, map, arena, size, rules, NULL);
}

/* --- Reload and level change --------------------------------------------- */

/*
 * The second load of a level over the same arena: p_level_reload() names the
 * map already loaded, p_level_change() names a different one. Everything else
 * is one rule, and it lives here once so the two cannot drift.
 *
 * Everything the caller owns has to be read before the load, because it memsets
 * the level state -- including the zone struct that holds the arena this
 * function passes back in.
 */
static p_setup_error_t level_load_over(p_level_t* level, const doom_map_t* map) {
    p_spawn_rules_t rules;
    m_random_t random;
    p_move_stats_t* moveStats;
    p_attack_stats_t* attackStats;
    p_sight_stats_t* sightStats;
    p_action_trace_t* actionTrace;
    p_spec_trace_t* specTrace;
    void* arena;
    size_t size;
    p_setup_error_t loaded;

    rules = level->rules;
    random = level->random;
    arena = level->zone.base;
    size = (size_t)level->zone.arenaSize;
    moveStats = level->context.moveStats;
    attackStats = level->context.attackStats;
    sightStats = level->context.sightStats;
    actionTrace = level->context.actionTrace;
    specTrace = level->context.specTrace;

    /* The carried position goes *into* the load, not back on top of it: the
     * THINGS run draws (P_SpawnMobj's `lastlook`, P_SpawnMapThing's tic jitter)
     * and upstream's P_LoadThings draws from wherever the sequence already is. */
    loaded = level_load(level, map, arena, size, &rules, &random);
    if (loaded != P_SETUP_OK) {
        return loaded;
    }

    /* The harness hooks are the caller's, and the load cannot see them. */
    level->context.moveStats = moveStats;
    level->context.attackStats = attackStats;
    level->context.sightStats = sightStats;
    level->context.actionTrace = actionTrace;
    level->context.specTrace = specTrace;
    return P_SETUP_OK;
}

p_setup_error_t p_level_reload(p_level_t* level) {
    if (level == NULL) {
        return P_SETUP_ERR_NULL_LEVEL;
    }
    if (!level->open || level->map == NULL || level->zone.base == NULL) {
        return P_SETUP_ERR_NOT_OPEN;
    }
    return level_load_over(level, level->map);
}

p_setup_error_t p_level_change(p_level_t* level, const doom_map_t* map) {
    if (level == NULL) {
        return P_SETUP_ERR_NULL_LEVEL;
    }
    if (map == NULL) {
        /* No next world: leave the open level answering rather than zeroing a
         * live game because the caller passed the wrong pointer. */
        return P_SETUP_ERR_NULL_MAP;
    }
    if (!level->open || level->zone.base == NULL) {
        return P_SETUP_ERR_NOT_OPEN;
    }
    return level_load_over(level, map);
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
    p_tick_error_t result;

    if (level == NULL) {
        return P_TICK_ERR_NULL_LIST;
    }
    if (!level->open) {
        return P_TICK_ERR_NOT_OPEN;
    }
    result = p_run_thinkers(&level->thinkers, &p_level_release_thinker,
                            &level->context, stats);
    /* P_Ticker's next statement, P_UpdateSpecials(): the scrolls advance and the
     * picture clock is stamped, both before `leveltime++` (p_spec.h). It runs
     * here, in the world half, because p_level_tic() is this plus the player and
     * P_Ticker runs it whether or not a player exists. */
    p_update_specials(level);
    return result;
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
