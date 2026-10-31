/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * The level's sector and line specials: the spawn scan, the surrounding-sector
 * queries every mover needs, and the line-trigger dispatch.
 *
 * The reference is linuxdoom-1.10 p_spec.c (P_SpawnSpecials, the P_Find*
 * helpers, P_CrossSpecialLine, P_ShootSpecialLine), p_switch.c
 * (P_UseSpecialLine) and p_setup.c (P_LoadSectors / P_LoadLineDefs). Every
 * non-obvious rule cites the upstream statement; p_spec.h carries the
 * implemented/deferred checklist.
 *
 * This is C99 only: <string.h> for memset, no stdio, no floating point, no
 * globals. The level owns the zone, the thinkers and the sector array.
 */
#include "p_spec.h"

#include <string.h>

#include "p_ceilng.h"
#include "p_doors.h"
#include "p_floor.h"
#include "p_lights.h"
#include "p_mobj.h"
#include "p_plats.h"
#include "p_setup.h"

/* --- Line geometry ------------------------------------------------------- */

int p_line_front_sector(const doom_map_t* map, const doom_line_t* line,
                        uint16_t* out) {
    doom_side_t side;

    if (map == NULL || line == NULL || out == NULL ||
        line->sideNum[0] == DOOM_NO_SIDE_INDEX) {
        return 0;
    }
    doom_map_get_side(map, line->sideNum[0], &side);
    *out = side.sector;
    return 1;
}

int p_line_back_sector(const doom_map_t* map, const doom_line_t* line,
                       uint16_t* out) {
    doom_side_t side;

    if (map == NULL || line == NULL || out == NULL ||
        (line->flags & DOOM_ML_TWOSIDED) == 0 ||
        line->sideNum[1] == DOOM_NO_SIDE_INDEX) {
        return 0;
    }
    doom_map_get_side(map, line->sideNum[1], &side);
    *out = side.sector;
    return 1;
}

int p_line_other_sector(const doom_map_t* map, const doom_line_t* line,
                        uint16_t sector, uint16_t* out) {
    uint16_t front;
    uint16_t back;

    if (map == NULL || line == NULL || out == NULL ||
        (line->flags & DOOM_ML_TWOSIDED) == 0) {
        return 0;
    }
    if (!p_line_front_sector(map, line, &front) ||
        !p_line_back_sector(map, line, &back)) {
        return 0;
    }
    /* getNextSector(): front -> back, otherwise front. Callers only pass lines
     * that belong to the sector, which is why the otherwise branch is safe. */
    *out = (front == sector) ? back : front;
    return 1;
}

/* --- Allocation ---------------------------------------------------------- */

void* p_spec_alloc_thinker(struct p_mobj_context* world, size_t size,
                           p_thinker_fn fn, p_thinker_kind_t kind) {
    p_thinker_t* node;
    zone_error_t error = ZONE_ERR_OK;

    if (world == NULL || world->zone == NULL || world->zone->open == 0 ||
        world->thinkers == NULL || size == 0u) {
        return NULL;
    }
    node = (p_thinker_t*)z_alloc(world->zone, size, PU_LEVEL, &error);
    if (node == NULL) {
        if (world->specStats != NULL) {
            world->specStats->allocationFailures += 1u;
        }
        return NULL;
    }
    memset(node, 0, size);
    if (p_thinker_init(node, fn, kind) != P_TICK_OK ||
        p_thinker_add(world->thinkers, node) != P_TICK_OK) {
        (void)z_free(world->zone, node);
        if (world->specStats != NULL) {
            world->specStats->allocationFailures += 1u;
        }
        return NULL;
    }
    return node;
}

/* --- Cached heights ------------------------------------------------------ */

void p_spec_refresh_things(struct p_mobj_context* world, uint16_t sectorIndex) {
    const p_thinker_t* thinker;

    if (world == NULL || world->thinkers == NULL || world->sectors == NULL ||
        (uint32_t)sectorIndex >= (uint32_t)world->sectorCount) {
        return;
    }
    for (thinker = p_thinker_first(world->thinkers); thinker != NULL;
         thinker = p_thinker_next(thinker)) {
        r_mobj_t* mobj;

        if (thinker->kind != (int16_t)P_THINKER_MOBJ) {
            continue;
        }
        mobj = p_mobj_of((p_thinker_t*)thinker);
        if (mobj == NULL || mobj->sector != sectorIndex) {
            continue;
        }
        /* The two fields P_SpawnMobj copied. `z` is P_ZMovement's, D4.2. */
        mobj->floorz = world->sectors[sectorIndex].floorHeight;
        mobj->ceilingz = world->sectors[sectorIndex].ceilingHeight;
        if (world->specStats != NULL) {
            world->specStats->thingRefreshes += 1u;
        }
    }
}

/* --- Surrounding-sector queries ------------------------------------------ */

/* True when the line joins @p sector to another, and answers that other sector.
 * P_GroupLines' per-sector line list is exactly these lines. */
static int neighbour(const p_mobj_context_t* world, const doom_line_t* line,
                     uint16_t sector, uint16_t* out) {
    uint16_t front;
    uint16_t back;

    if ((line->flags & DOOM_ML_TWOSIDED) == 0) {
        return 0;
    }
    if (!p_line_front_sector(world->map, line, &front) ||
        !p_line_back_sector(world->map, line, &back)) {
        return 0;
    }
    if (front == sector) {
        *out = back;
        return 1;
    }
    if (back == sector) {
        *out = front;
        return 1;
    }
    return 0;
}

fixed_t p_find_lowest_floor_surrounding(const struct p_mobj_context* world,
                                        uint16_t sector) {
    fixed_t floor;
    uint32_t i;
    uint32_t lineCount;

    if (world == NULL || world->map == NULL || world->sectors == NULL ||
        (uint32_t)sector >= (uint32_t)world->sectorCount) {
        return 0;
    }
    floor = world->sectors[sector].floorHeight;
    lineCount = world->map->lumps[DOOM_MAP_LINEDEFS].count;
    for (i = 0; i < lineCount; ++i) {
        doom_line_t line;
        uint16_t other;

        doom_map_get_line(world->map, (uint16_t)i, &line);
        if (!neighbour(world, &line, sector, &other)) {
            continue;
        }
        if (world->sectors[other].floorHeight < floor) {
            floor = world->sectors[other].floorHeight;
        }
    }
    return floor;
}

fixed_t p_find_highest_floor_surrounding(const struct p_mobj_context* world,
                                         uint16_t sector) {
    fixed_t floor = -500 * FRACUNIT;
    uint32_t i;
    uint32_t lineCount;

    if (world == NULL || world->map == NULL || world->sectors == NULL ||
        (uint32_t)sector >= (uint32_t)world->sectorCount) {
        return floor;
    }
    lineCount = world->map->lumps[DOOM_MAP_LINEDEFS].count;
    for (i = 0; i < lineCount; ++i) {
        doom_line_t line;
        uint16_t other;

        doom_map_get_line(world->map, (uint16_t)i, &line);
        if (!neighbour(world, &line, sector, &other)) {
            continue;
        }
        if (world->sectors[other].floorHeight > floor) {
            floor = world->sectors[other].floorHeight;
        }
    }
    return floor;
}

fixed_t p_find_next_highest_floor(const struct p_mobj_context* world,
                                  uint16_t sector, fixed_t currentHeight) {
    fixed_t heightlist[P_MAX_ADJOINING_SECTORS];
    fixed_t height = currentHeight;
    fixed_t min;
    uint32_t i;
    int h = 0;
    int j;
    uint32_t lineCount;

    if (world == NULL || world->map == NULL || world->sectors == NULL ||
        (uint32_t)sector >= (uint32_t)world->sectorCount) {
        return currentHeight;
    }
    lineCount = world->map->lumps[DOOM_MAP_LINEDEFS].count;
    for (i = 0; i < lineCount; ++i) {
        doom_line_t line;
        uint16_t other;

        doom_map_get_line(world->map, (uint16_t)i, &line);
        if (!neighbour(world, &line, sector, &other)) {
            continue;
        }
        if (world->sectors[other].floorHeight > height) {
            if (h < P_MAX_ADJOINING_SECTORS) {
                heightlist[h++] = world->sectors[other].floorHeight;
            } else if (world->specStats != NULL) {
                /* Upstream prints a warning and stops collecting. */
                world->specStats->adjacentOverflow += 1u;
                break;
            }
        }
    }
    if (h == 0) {
        return currentHeight;
    }
    min = heightlist[0];
    for (j = 1; j < h; ++j) {
        if (heightlist[j] < min) {
            min = heightlist[j];
        }
    }
    return min;
}

fixed_t p_find_lowest_ceiling_surrounding(const struct p_mobj_context* world,
                                          uint16_t sector) {
    fixed_t height = INT32_MAX;
    uint32_t i;
    uint32_t lineCount;

    if (world == NULL || world->map == NULL || world->sectors == NULL ||
        (uint32_t)sector >= (uint32_t)world->sectorCount) {
        return INT32_MAX;
    }
    lineCount = world->map->lumps[DOOM_MAP_LINEDEFS].count;
    for (i = 0; i < lineCount; ++i) {
        doom_line_t line;
        uint16_t other;

        doom_map_get_line(world->map, (uint16_t)i, &line);
        if (!neighbour(world, &line, sector, &other)) {
            continue;
        }
        if (world->sectors[other].ceilingHeight < height) {
            height = world->sectors[other].ceilingHeight;
        }
    }
    return height;
}

fixed_t p_find_highest_ceiling_surrounding(const struct p_mobj_context* world,
                                           uint16_t sector) {
    fixed_t height = 0;
    uint32_t i;
    uint32_t lineCount;

    if (world == NULL || world->map == NULL || world->sectors == NULL ||
        (uint32_t)sector >= (uint32_t)world->sectorCount) {
        return 0;
    }
    lineCount = world->map->lumps[DOOM_MAP_LINEDEFS].count;
    for (i = 0; i < lineCount; ++i) {
        doom_line_t line;
        uint16_t other;

        doom_map_get_line(world->map, (uint16_t)i, &line);
        if (!neighbour(world, &line, sector, &other)) {
            continue;
        }
        if (world->sectors[other].ceilingHeight > height) {
            height = world->sectors[other].ceilingHeight;
        }
    }
    return height;
}

int p_find_min_surrounding_light(const struct p_mobj_context* world,
                                 uint16_t sector, int max) {
    int min = max;
    uint32_t i;
    uint32_t lineCount;

    if (world == NULL || world->map == NULL || world->sectors == NULL ||
        (uint32_t)sector >= (uint32_t)world->sectorCount) {
        return max;
    }
    lineCount = world->map->lumps[DOOM_MAP_LINEDEFS].count;
    for (i = 0; i < lineCount; ++i) {
        doom_line_t line;
        uint16_t other;

        doom_map_get_line(world->map, (uint16_t)i, &line);
        if (!neighbour(world, &line, sector, &other)) {
            continue;
        }
        if (world->sectors[other].lightLevel < min) {
            min = world->sectors[other].lightLevel;
        }
    }
    return min;
}

int p_find_sector_from_line_tag(const struct p_mobj_context* world, int16_t tag,
                                int start) {
    int i;

    if (world == NULL || world->sectors == NULL) {
        return -1;
    }
    for (i = start + 1; i < (int)world->sectorCount; ++i) {
        if (world->sectors[i].tag == tag) {
            return i;
        }
    }
    return -1;
}

/* --- P_SpawnSpecials ----------------------------------------------------- */

p_spec_error_t p_spawn_specials(struct p_level* level) {
    p_mobj_context_t* world;
    uint16_t i;

    if (level == NULL) {
        return P_SPEC_ERR_NULL_LEVEL;
    }
    if (!level->open) {
        return P_SPEC_ERR_NOT_OPEN;
    }
    if (level->sectors == NULL || level->sectorCount == 0u) {
        return P_SPEC_ERR_NO_SECTORS;
    }
    world = &level->context;
    if (world->zone == NULL || world->thinkers == NULL || world->map == NULL ||
        world->random == NULL) {
        return P_SPEC_ERR_NO_WORLD;
    }

    memset(&level->specStats, 0, sizeof level->specStats);
    world->specStats = &level->specStats;
    world->sectors = level->sectors;
    world->sectorCount = level->sectorCount;
    world->lineSpecial = level->lineSpecial;
    world->lineCount = level->lineCount;

    /* P_SetupLevel spawns deathmatch players before P_SpawnSpecials. The
     * reference has no P_SpawnDeathmatchSpecials; the deathmatch-only level
     * start is G_DeathMatchSpawnPlayer, owned by the player layer (D5). The
     * mode flag is still threaded here so the branch is visible and the gate can
     * assert it. */
    if (level->rules.deathmatch) {
        level->specStats.deathmatchSpecials += 1u;
    }

    /* Init special SECTORs. Sector order is the thinker order. */
    for (i = 0; i < level->sectorCount; ++i) {
        int16_t special = level->sectors[i].special;

        if (special == 0) {
            continue;
        }
        switch (special) {
            case 1:     /* FLICKERING LIGHTS */
                if (p_spawn_light_flash(world, i)) {
                    level->specStats.spawned += 1u;
                }
                break;

            case 2:     /* STROBE FAST */
                if (p_spawn_strobe_flash(world, i, P_FASTDARK, 0)) {
                    level->specStats.spawned += 1u;
                }
                break;

            case 3:     /* STROBE SLOW */
                if (p_spawn_strobe_flash(world, i, P_SLOWDARK, 0)) {
                    level->specStats.spawned += 1u;
                }
                break;

            case 4:     /* STROBE FAST/DEATH SLIME */
                if (p_spawn_strobe_flash(world, i, P_FASTDARK, 0)) {
                    level->specStats.spawned += 1u;
                }
                /* P_SpawnStrobeFlash clears the sector; upstream puts 4 back
                 * because the damage half still needs it. */
                level->sectors[i].special = 4;
                break;

            case 8:     /* GLOWING LIGHT */
                if (p_spawn_glowing_light(world, i)) {
                    level->specStats.spawned += 1u;
                }
                break;

            case 9:     /* SECRET SECTOR */
                level->totalSecrets += 1;
                level->specStats.secrets += 1u;
                break;

            case 10:    /* DOOR CLOSE IN 30 SECONDS */
                if (p_spawn_door_close_in_30(world, i)) {
                    level->specStats.spawned += 1u;
                }
                break;

            case 12:    /* SYNC STROBE SLOW */
                if (p_spawn_strobe_flash(world, i, P_SLOWDARK, 1)) {
                    level->specStats.spawned += 1u;
                }
                break;

            case 13:    /* SYNC STROBE FAST */
                if (p_spawn_strobe_flash(world, i, P_FASTDARK, 1)) {
                    level->specStats.spawned += 1u;
                }
                break;

            case 14:    /* DOOR RAISE IN 5 MINUTES */
                if (p_spawn_door_raise_in_5_mins(world, i)) {
                    level->specStats.spawned += 1u;
                }
                break;

            case 17:    /* FIRE FLICKER */
                if (p_spawn_fire_flicker(world, i)) {
                    level->specStats.spawned += 1u;
                }
                break;

            default:
                /* 5/7/11/16 are damage/exit floors (the player-in-sector
                 * effect, D4.2); anything else is unknown to the port. */
                level->specStats.unsupportedSectorSpecial += 1u;
                break;
        }
    }

    /* Init line EFFECTs: 48 is a texture scroll, P_UpdateSpecials' job. */
    for (i = 0; i < level->lineCount; ++i) {
        if (p_line_special(level->map, i) == 48) {
            level->specStats.lineScrolls += 1u;
        }
    }

    return P_SPEC_OK;
}

/* --- Trigger dispatch ---------------------------------------------------- */

/* Clears a one-shot line, as `line->special = 0` does. */
static void consume_line(struct p_level* level, uint16_t lineIndex) {
    if (level->lineSpecial != NULL && lineIndex < level->lineCount) {
        level->lineSpecial[lineIndex] = 0;
    }
}

/* The short list a non-player thing may cross (P_CrossSpecialLine). */
static int thing_can_cross(int16_t special) {
    switch (special) {
        case 39:    /* TELEPORT TRIGGER */
        case 97:    /* TELEPORT RETRIGGER */
        case 125:   /* TELEPORT MONSTERONLY TRIGGER */
        case 126:   /* TELEPORT MONSTERONLY RETRIGGER */
        case 4:     /* RAISE DOOR */
        case 10:    /* PLAT DOWN-WAIT-UP-STAY TRIGGER */
        case 88:    /* PLAT DOWN-WAIT-UP-STAY RETRIGGER */
            return 1;
        default:
            return 0;
    }
}

static int trigger_cross(struct p_level* level, uint16_t lineIndex,
                         int byPlayer) {
    p_mobj_context_t* world = &level->context;
    int16_t special = level->lineSpecial[lineIndex];

    if (!byPlayer && !thing_can_cross(special)) {
        level->specStats.triggersRefused += 1u;
        return 0;
    }

    switch (special) {
        /* TRIGGERS */
        case 2:
            (void)p_do_door(world, lineIndex, P_DOOR_OPEN);
            consume_line(level, lineIndex);
            return 1;
        case 3:
            (void)p_do_door(world, lineIndex, P_DOOR_CLOSE);
            consume_line(level, lineIndex);
            return 1;
        case 4:
            (void)p_do_door(world, lineIndex, P_DOOR_NORMAL);
            consume_line(level, lineIndex);
            return 1;
        case 5:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE);
            consume_line(level, lineIndex);
            return 1;
        case 6:
            (void)p_do_ceiling(world, lineIndex, P_CEIL_FAST_CRUSH_AND_RAISE);
            consume_line(level, lineIndex);
            return 1;
        case 10:
            (void)p_do_plat(world, lineIndex, P_PLAT_DOWN_WAIT_UP_STAY, 0);
            consume_line(level, lineIndex);
            return 1;
        case 12:
            (void)p_light_turn_on(world, lineIndex, 0);
            consume_line(level, lineIndex);
            return 1;
        case 13:
            (void)p_light_turn_on(world, lineIndex, 255);
            consume_line(level, lineIndex);
            return 1;
        case 16:
            (void)p_do_door(world, lineIndex, P_DOOR_CLOSE_30_THEN_OPEN);
            consume_line(level, lineIndex);
            return 1;
        case 17:
            (void)p_start_light_strobing(world, lineIndex);
            consume_line(level, lineIndex);
            return 1;
        case 19:
            (void)p_do_floor(world, lineIndex, P_FLOOR_LOWER);
            consume_line(level, lineIndex);
            return 1;
        case 22:
            (void)p_do_plat(world, lineIndex, P_PLAT_RAISE_TO_NEAREST_AND_CHANGE, 0);
            consume_line(level, lineIndex);
            return 1;
        case 25:
            (void)p_do_ceiling(world, lineIndex, P_CEIL_CRUSH_AND_RAISE);
            consume_line(level, lineIndex);
            return 1;
        case 30:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE_TO_TEXTURE);
            consume_line(level, lineIndex);
            return 1;
        case 35:
            (void)p_light_turn_on(world, lineIndex, 35);
            consume_line(level, lineIndex);
            return 1;
        case 36:
            (void)p_do_floor(world, lineIndex, P_FLOOR_TURBO_LOWER);
            consume_line(level, lineIndex);
            return 1;
        case 37:
            (void)p_do_floor(world, lineIndex, P_FLOOR_LOWER_AND_CHANGE);
            consume_line(level, lineIndex);
            return 1;
        case 38:
            (void)p_do_floor(world, lineIndex, P_FLOOR_LOWER_TO_LOWEST);
            consume_line(level, lineIndex);
            return 1;
        case 40:
            (void)p_do_ceiling(world, lineIndex, P_CEIL_RAISE_TO_HIGHEST);
            (void)p_do_floor(world, lineIndex, P_FLOOR_LOWER_TO_LOWEST);
            consume_line(level, lineIndex);
            return 1;
        case 44:
            (void)p_do_ceiling(world, lineIndex, P_CEIL_LOWER_AND_CRUSH);
            consume_line(level, lineIndex);
            return 1;
        case 53:
            (void)p_do_plat(world, lineIndex, P_PLAT_PERPETUAL_RAISE, 0);
            consume_line(level, lineIndex);
            return 1;
        case 54:
            (void)p_stop_plat(world, lineIndex);
            consume_line(level, lineIndex);
            return 1;
        case 56:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE_CRUSH);
            consume_line(level, lineIndex);
            return 1;
        case 57:
            (void)p_ceiling_crush_stop(world, lineIndex);
            consume_line(level, lineIndex);
            return 1;
        case 58:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE_24);
            consume_line(level, lineIndex);
            return 1;
        case 59:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE_24_AND_CHANGE);
            consume_line(level, lineIndex);
            return 1;
        case 104:
            (void)p_turn_tag_lights_off(world, lineIndex);
            consume_line(level, lineIndex);
            return 1;
        case 108:
            (void)p_do_door(world, lineIndex, P_DOOR_BLAZE_RAISE);
            consume_line(level, lineIndex);
            return 1;
        case 109:
            (void)p_do_door(world, lineIndex, P_DOOR_BLAZE_OPEN);
            consume_line(level, lineIndex);
            return 1;
        case 110:
            (void)p_do_door(world, lineIndex, P_DOOR_BLAZE_CLOSE);
            consume_line(level, lineIndex);
            return 1;
        case 119:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE_TO_NEAREST);
            consume_line(level, lineIndex);
            return 1;
        case 121:
            (void)p_do_plat(world, lineIndex, P_PLAT_BLAZE_DWUS, 0);
            consume_line(level, lineIndex);
            return 1;
        case 130:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE_TURBO);
            consume_line(level, lineIndex);
            return 1;
        case 141:
            (void)p_do_ceiling(world, lineIndex, P_CEIL_SILENT_CRUSH_AND_RAISE);
            consume_line(level, lineIndex);
            return 1;

        /* RETRIGGERS: the number stays. */
        case 72:
            (void)p_do_ceiling(world, lineIndex, P_CEIL_LOWER_AND_CRUSH);
            return 1;
        case 73:
            (void)p_do_ceiling(world, lineIndex, P_CEIL_CRUSH_AND_RAISE);
            return 1;
        case 74:
            (void)p_ceiling_crush_stop(world, lineIndex);
            return 1;
        case 75:
            (void)p_do_door(world, lineIndex, P_DOOR_CLOSE);
            return 1;
        case 76:
            (void)p_do_door(world, lineIndex, P_DOOR_CLOSE_30_THEN_OPEN);
            return 1;
        case 77:
            (void)p_do_ceiling(world, lineIndex, P_CEIL_FAST_CRUSH_AND_RAISE);
            return 1;
        case 79:
            (void)p_light_turn_on(world, lineIndex, 35);
            return 1;
        case 80:
            (void)p_light_turn_on(world, lineIndex, 0);
            return 1;
        case 81:
            (void)p_light_turn_on(world, lineIndex, 255);
            return 1;
        case 82:
            (void)p_do_floor(world, lineIndex, P_FLOOR_LOWER_TO_LOWEST);
            return 1;
        case 83:
            (void)p_do_floor(world, lineIndex, P_FLOOR_LOWER);
            return 1;
        case 84:
            (void)p_do_floor(world, lineIndex, P_FLOOR_LOWER_AND_CHANGE);
            return 1;
        case 86:
            (void)p_do_door(world, lineIndex, P_DOOR_OPEN);
            return 1;
        case 87:
            (void)p_do_plat(world, lineIndex, P_PLAT_PERPETUAL_RAISE, 0);
            return 1;
        case 88:
            (void)p_do_plat(world, lineIndex, P_PLAT_DOWN_WAIT_UP_STAY, 0);
            return 1;
        case 89:
            (void)p_stop_plat(world, lineIndex);
            return 1;
        case 90:
            (void)p_do_door(world, lineIndex, P_DOOR_NORMAL);
            return 1;
        case 91:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE);
            return 1;
        case 92:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE_24);
            return 1;
        case 93:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE_24_AND_CHANGE);
            return 1;
        case 94:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE_CRUSH);
            return 1;
        case 95:
            (void)p_do_plat(world, lineIndex, P_PLAT_RAISE_TO_NEAREST_AND_CHANGE, 0);
            return 1;
        case 96:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE_TO_TEXTURE);
            return 1;
        case 98:
            (void)p_do_floor(world, lineIndex, P_FLOOR_TURBO_LOWER);
            return 1;
        case 105:
            (void)p_do_door(world, lineIndex, P_DOOR_BLAZE_RAISE);
            return 1;
        case 106:
            (void)p_do_door(world, lineIndex, P_DOOR_BLAZE_OPEN);
            return 1;
        case 107:
            (void)p_do_door(world, lineIndex, P_DOOR_BLAZE_CLOSE);
            return 1;
        case 120:
            (void)p_do_plat(world, lineIndex, P_PLAT_BLAZE_DWUS, 0);
            return 1;
        case 128:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE_TO_NEAREST);
            return 1;
        case 129:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE_TURBO);
            return 1;

        default:
            /* 8/100 stairs, 39/97/125/126 teleports, 52/124 exits, 48 scroll. */
            level->specStats.unsupportedLineSpecial += 1u;
            return 0;
    }
}

/* The switch-side numbers P_UseSpecialLine dispatches. `useAgain` is true for
 * the button specials, which upstream leaves reusable; `clear` says whether a
 * one-shot switch clears its line. The texture change itself is deferred (see
 * p_switch in this file's report). */
static int trigger_use(struct p_level* level, uint16_t lineIndex, int byPlayer) {
    p_mobj_context_t* world = &level->context;
    int16_t special = level->lineSpecial[lineIndex];

    if (!byPlayer) {
        /* `never open secret doors` aside, only these four are allowed. */
        if (!(special == 1 || special == 32 || special == 33 || special == 34)) {
            level->specStats.triggersRefused += 1u;
            return 0;
        }
    }

    switch (special) {
        /* MANUALS */
        case 1:
        case 26:
        case 27:
        case 28:
        case 31:
        case 32:
        case 33:
        case 34:
        case 117:
        case 118:
            return p_vertical_door(world, lineIndex, byPlayer);

        /* SWITCHES (P_ChangeSwitchTexture(line, 0): one-shot, clears) */
        case 9:
            (void)p_do_donut(world, lineIndex);
            level->specStats.deferredLineSpecial += 1u;
            consume_line(level, lineIndex);
            return 1;
        case 11:
        case 51:
            /* Exit level / secret exit: no level transition in this layer. */
            level->specStats.deferredLineSpecial += 1u;
            consume_line(level, lineIndex);
            return 0;
        case 14:
            (void)p_do_plat(world, lineIndex, P_PLAT_RAISE_AND_CHANGE, 32);
            level->specStats.deferredLineSpecial += 1u;
            consume_line(level, lineIndex);
            return 1;
        case 15:
            (void)p_do_plat(world, lineIndex, P_PLAT_RAISE_AND_CHANGE, 24);
            level->specStats.deferredLineSpecial += 1u;
            consume_line(level, lineIndex);
            return 1;
        case 18:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE_TO_NEAREST);
            level->specStats.deferredLineSpecial += 1u;
            consume_line(level, lineIndex);
            return 1;
        case 20:
            (void)p_do_plat(world, lineIndex, P_PLAT_RAISE_TO_NEAREST_AND_CHANGE, 0);
            level->specStats.deferredLineSpecial += 1u;
            consume_line(level, lineIndex);
            return 1;
        case 21:
            (void)p_do_plat(world, lineIndex, P_PLAT_DOWN_WAIT_UP_STAY, 0);
            level->specStats.deferredLineSpecial += 1u;
            consume_line(level, lineIndex);
            return 1;
        case 23:
            (void)p_do_floor(world, lineIndex, P_FLOOR_LOWER_TO_LOWEST);
            level->specStats.deferredLineSpecial += 1u;
            consume_line(level, lineIndex);
            return 1;
        case 29:
            (void)p_do_door(world, lineIndex, P_DOOR_NORMAL);
            level->specStats.deferredLineSpecial += 1u;
            consume_line(level, lineIndex);
            return 1;
        case 41:
        case 43:
            (void)p_do_ceiling(world, lineIndex, P_CEIL_LOWER_TO_FLOOR);
            level->specStats.deferredLineSpecial += 1u;
            if (special == 41) {
                consume_line(level, lineIndex);
            }
            return 1;
        case 49:
            (void)p_do_ceiling(world, lineIndex, P_CEIL_CRUSH_AND_RAISE);
            level->specStats.deferredLineSpecial += 1u;
            consume_line(level, lineIndex);
            return 1;
        case 50:
            (void)p_do_door(world, lineIndex, P_DOOR_CLOSE);
            level->specStats.deferredLineSpecial += 1u;
            consume_line(level, lineIndex);
            return 1;
        case 55:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE_CRUSH);
            level->specStats.deferredLineSpecial += 1u;
            consume_line(level, lineIndex);
            return 1;
        case 71:
            (void)p_do_floor(world, lineIndex, P_FLOOR_TURBO_LOWER);
            level->specStats.deferredLineSpecial += 1u;
            consume_line(level, lineIndex);
            return 1;
        case 101:
        case 64:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE);
            level->specStats.deferredLineSpecial += 1u;
            if (special == 101) {
                consume_line(level, lineIndex);
            }
            return 1;
        case 102:
            (void)p_do_floor(world, lineIndex, P_FLOOR_LOWER);
            level->specStats.deferredLineSpecial += 1u;
            consume_line(level, lineIndex);
            return 1;
        case 103:
            (void)p_do_door(world, lineIndex, P_DOOR_OPEN);
            level->specStats.deferredLineSpecial += 1u;
            consume_line(level, lineIndex);
            return 1;
        case 111:
        case 114:
            (void)p_do_door(world, lineIndex, P_DOOR_BLAZE_RAISE);
            level->specStats.deferredLineSpecial += 1u;
            if (special == 111) {
                consume_line(level, lineIndex);
            }
            return 1;
        case 112:
        case 115:
            (void)p_do_door(world, lineIndex, P_DOOR_BLAZE_OPEN);
            level->specStats.deferredLineSpecial += 1u;
            if (special == 112) {
                consume_line(level, lineIndex);
            }
            return 1;
        case 113:
        case 116:
            (void)p_do_door(world, lineIndex, P_DOOR_BLAZE_CLOSE);
            level->specStats.deferredLineSpecial += 1u;
            if (special == 113) {
                consume_line(level, lineIndex);
            }
            return 1;
        case 122:
        case 123:
            (void)p_do_plat(world, lineIndex, P_PLAT_BLAZE_DWUS, 0);
            level->specStats.deferredLineSpecial += 1u;
            if (special == 122) {
                consume_line(level, lineIndex);
            }
            return 1;
        case 131:
        case 132:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE_TURBO);
            level->specStats.deferredLineSpecial += 1u;
            if (special == 131) {
                consume_line(level, lineIndex);
            }
            return 1;
        case 133:
        case 135:
        case 137:
        case 99:
        case 134:
        case 136:
            /* EV_DoLockedDoor(blazeOpen). No key inventory yet (D5), and
             * byPlayer was checked above. */
            (void)p_do_door(world, lineIndex, P_DOOR_BLAZE_OPEN);
            level->specStats.deferredLineSpecial += 1u;
            if (special == 133 || special == 135 || special == 137) {
                consume_line(level, lineIndex);
            }
            return 1;
        case 140:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE_512);
            level->specStats.deferredLineSpecial += 1u;
            consume_line(level, lineIndex);
            return 1;
        case 42:
            (void)p_do_door(world, lineIndex, P_DOOR_CLOSE);
            level->specStats.deferredLineSpecial += 1u;
            return 1;
        case 45:
            (void)p_do_floor(world, lineIndex, P_FLOOR_LOWER);
            level->specStats.deferredLineSpecial += 1u;
            return 1;
        case 60:
            (void)p_do_floor(world, lineIndex, P_FLOOR_LOWER_TO_LOWEST);
            level->specStats.deferredLineSpecial += 1u;
            return 1;
        case 61:
            (void)p_do_door(world, lineIndex, P_DOOR_OPEN);
            level->specStats.deferredLineSpecial += 1u;
            return 1;
        case 62:
            (void)p_do_plat(world, lineIndex, P_PLAT_DOWN_WAIT_UP_STAY, 1);
            level->specStats.deferredLineSpecial += 1u;
            return 1;
        case 63:
            (void)p_do_door(world, lineIndex, P_DOOR_NORMAL);
            level->specStats.deferredLineSpecial += 1u;
            return 1;
        case 65:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE_CRUSH);
            level->specStats.deferredLineSpecial += 1u;
            return 1;
        case 66:
            (void)p_do_plat(world, lineIndex, P_PLAT_RAISE_AND_CHANGE, 24);
            level->specStats.deferredLineSpecial += 1u;
            return 1;
        case 67:
            (void)p_do_plat(world, lineIndex, P_PLAT_RAISE_AND_CHANGE, 32);
            level->specStats.deferredLineSpecial += 1u;
            return 1;
        case 68:
            (void)p_do_plat(world, lineIndex, P_PLAT_RAISE_TO_NEAREST_AND_CHANGE, 0);
            level->specStats.deferredLineSpecial += 1u;
            return 1;
        case 69:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE_TO_NEAREST);
            level->specStats.deferredLineSpecial += 1u;
            return 1;
        case 70:
            (void)p_do_floor(world, lineIndex, P_FLOOR_TURBO_LOWER);
            level->specStats.deferredLineSpecial += 1u;
            return 1;
        case 138:
            (void)p_light_turn_on(world, lineIndex, 255);
            level->specStats.deferredLineSpecial += 1u;
            return 1;
        case 139:
            (void)p_light_turn_on(world, lineIndex, 35);
            level->specStats.deferredLineSpecial += 1u;
            return 1;

        /* Stairs (7, 127) have no EV here. */
        case 7:
        case 127:
            level->specStats.unsupportedLineSpecial += 1u;
            return 0;

        default:
            level->specStats.unsupportedLineSpecial += 1u;
            return 0;
    }
}

static int trigger_shoot(struct p_level* level, uint16_t lineIndex,
                         int byPlayer) {
    p_mobj_context_t* world = &level->context;
    int16_t special = level->lineSpecial[lineIndex];

    if (!byPlayer && special != 46) {
        level->specStats.triggersRefused += 1u;
        return 0;
    }
    switch (special) {
        case 24:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE);
            level->specStats.deferredLineSpecial += 1u;
            return 1;
        case 46:
            (void)p_do_door(world, lineIndex, P_DOOR_OPEN);
            level->specStats.deferredLineSpecial += 1u;
            return 1;
        case 47:
            (void)p_do_plat(world, lineIndex, P_PLAT_RAISE_TO_NEAREST_AND_CHANGE, 0);
            level->specStats.deferredLineSpecial += 1u;
            return 1;
        default:
            level->specStats.unsupportedLineSpecial += 1u;
            return 0;
    }
}

int p_trigger_special_line(struct p_level* level, uint16_t lineIndex,
                           p_trigger_kind_t kind, int byPlayer) {
    int result;

    if (level == NULL || !level->open || level->lineSpecial == NULL ||
        lineIndex >= level->lineCount) {
        return 0;
    }
    if (level->lineSpecial[lineIndex] == 0) {
        return 0;
    }

    switch (kind) {
        case P_TRIGGER_USE:
            result = trigger_use(level, lineIndex, byPlayer);
            break;
        case P_TRIGGER_SHOOT:
            result = trigger_shoot(level, lineIndex, byPlayer);
            break;
        case P_TRIGGER_CROSS:
        default:
            result = trigger_cross(level, lineIndex, byPlayer);
            break;
    }
    if (result) {
        level->specStats.triggers += 1u;
    }
    return result;
}