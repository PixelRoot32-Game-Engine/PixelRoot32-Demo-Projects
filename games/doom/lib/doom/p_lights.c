/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * Sector lighting: the four light thinkers and the EV functions that start them.
 *
 * The reference is linuxdoom-1.10 p_lights.c. Each function cites the upstream
 * routine it reproduces; p_lights.h lists the rules that are easy to get wrong.
 *
 * This is C99 only: no stdio, no floating point, no globals.
 */
#include "p_lights.h"

#include "p_mobj.h"

/* One p_light_thinker_t, linked. Lights are not movers, so they do not take
 * `sector->specialdata` (upstream does not either). */
static p_light_thinker_t* new_light(struct p_mobj_context* world,
                                    uint16_t sectorIndex, int16_t type) {
    p_light_thinker_t* light = (p_light_thinker_t*)p_spec_alloc_thinker(
        world, sizeof(p_light_thinker_t), &p_light_thinker,
        P_THINKER_LIGHTFLASH);

    if (light == NULL) {
        return NULL;
    }
    light->sector = sectorIndex;
    light->type = type;
    light->direction = 0;
    light->count = 0;
    light->maxLight = 0;
    light->minLight = 0;
    light->maxTime = 0;
    light->minTime = 0;
    light->darkTime = 0;
    light->brightTime = 0;
    return light;
}

int p_spawn_light_flash(struct p_mobj_context* world, uint16_t sectorIndex) {
    p_light_thinker_t* light;
    p_sector_t* sector;

    if (world == NULL || world->sectors == NULL ||
        (uint32_t)sectorIndex >= (uint32_t)world->sectorCount) {
        return 0;
    }
    sector = &world->sectors[sectorIndex];
    light = new_light(world, sectorIndex, P_LIGHT_FLASH);
    if (light == NULL) {
        return 0;
    }
    /* nothing special about it during gameplay */
    sector->special = 0;
    light->maxLight = sector->lightLevel;
    light->minLight = p_find_min_surrounding_light(world, sectorIndex,
                                                   sector->lightLevel);
    light->maxTime = 64;
    light->minTime = 7;
    light->count = (int32_t)(p_random(world->random) & light->maxTime) + 1;
    if (world->specStats != NULL) {
        world->specStats->lightFlashes += 1u;
    }
    return 1;
}

int p_spawn_strobe_flash(struct p_mobj_context* world, uint16_t sectorIndex,
                         int fastOrSlow, int inSync) {
    p_light_thinker_t* light;
    p_sector_t* sector;

    if (world == NULL || world->sectors == NULL ||
        (uint32_t)sectorIndex >= (uint32_t)world->sectorCount) {
        return 0;
    }
    sector = &world->sectors[sectorIndex];
    light = new_light(world, sectorIndex, P_LIGHT_STROBE);
    if (light == NULL) {
        return 0;
    }
    light->darkTime = fastOrSlow;
    light->brightTime = P_STROBEBRIGHT;
    light->maxLight = sector->lightLevel;
    light->minLight = p_find_min_surrounding_light(world, sectorIndex,
                                                   sector->lightLevel);
    if (light->minLight == light->maxLight) {
        light->minLight = 0;
    }
    /* nothing special about it during gameplay */
    sector->special = 0;

    if (!inSync) {
        light->count = (int32_t)(p_random(world->random) & 7) + 1;
    } else {
        light->count = 1;
    }
    if (world->specStats != NULL) {
        if (fastOrSlow == P_FASTDARK) {
            world->specStats->fastStrobes += 1u;
        } else {
            world->specStats->slowStrobes += 1u;
        }
    }
    return 1;
}

int p_spawn_glowing_light(struct p_mobj_context* world, uint16_t sectorIndex) {
    p_light_thinker_t* light;
    p_sector_t* sector;

    if (world == NULL || world->sectors == NULL ||
        (uint32_t)sectorIndex >= (uint32_t)world->sectorCount) {
        return 0;
    }
    sector = &world->sectors[sectorIndex];
    light = new_light(world, sectorIndex, P_LIGHT_GLOW);
    if (light == NULL) {
        return 0;
    }
    light->minLight = p_find_min_surrounding_light(world, sectorIndex,
                                                   sector->lightLevel);
    light->maxLight = sector->lightLevel;
    light->direction = -1;
    sector->special = 0;
    if (world->specStats != NULL) {
        world->specStats->glows += 1u;
    }
    return 1;
}

int p_spawn_fire_flicker(struct p_mobj_context* world, uint16_t sectorIndex) {
    p_light_thinker_t* light;
    p_sector_t* sector;

    if (world == NULL || world->sectors == NULL ||
        (uint32_t)sectorIndex >= (uint32_t)world->sectorCount) {
        return 0;
    }
    sector = &world->sectors[sectorIndex];
    light = new_light(world, sectorIndex, P_LIGHT_FIRE_FLICKER);
    if (light == NULL) {
        return 0;
    }
    /* Note that we are resetting sector attributes. */
    sector->special = 0;
    light->maxLight = sector->lightLevel;
    light->minLight =
        p_find_min_surrounding_light(world, sectorIndex, sector->lightLevel) + 16;
    light->count = 4;
    if (world->specStats != NULL) {
        world->specStats->fireFlickers += 1u;
    }
    return 1;
}

int p_start_light_strobing(struct p_mobj_context* world, uint16_t lineIndex) {
    int16_t tag;
    int secnum = -1;
    int rtn = 0;

    if (world == NULL || world->map == NULL || world->sectors == NULL) {
        return 0;
    }
    tag = p_line_tag(world->map, lineIndex);

    while ((secnum = p_find_sector_from_line_tag(world, tag, secnum)) >= 0) {
        if (world->sectors[secnum].specialdata != NULL) {
            continue;
        }
        if (p_spawn_strobe_flash(world, (uint16_t)secnum, P_SLOWDARK, 0)) {
            rtn = 1;
        }
    }
    return rtn;
}

int p_light_turn_on(struct p_mobj_context* world, uint16_t lineIndex, int bright) {
    int16_t tag;
    uint16_t i;

    if (world == NULL || world->map == NULL || world->sectors == NULL) {
        return 0;
    }
    tag = p_line_tag(world->map, lineIndex);

    for (i = 0; i < world->sectorCount; ++i) {
        p_sector_t* sector = &world->sectors[i];

        if (sector->tag != tag) {
            continue;
        }
        /* bright = 0 means to search for the highest light level surrounding
         * sector. Upstream accumulates into `bright`, so the second tagged
         * sector reuses the first one's answer; reproduced. */
        if (bright == 0) {
            int j;
            uint32_t lineCount = world->map->lumps[DOOM_MAP_LINEDEFS].count;

            for (j = 0; (uint32_t)j < lineCount; ++j) {
                doom_line_t line;
                uint16_t other;

                doom_map_get_line(world->map, (uint16_t)j, &line);
                if (!p_line_other_sector(world->map, &line, i, &other)) {
                    continue;
                }
                if (world->sectors[other].lightLevel > bright) {
                    bright = world->sectors[other].lightLevel;
                }
            }
        }
        sector->lightLevel = (int16_t)bright;
    }
    return 1;
}

int p_turn_tag_lights_off(struct p_mobj_context* world, uint16_t lineIndex) {
    int16_t tag;
    uint16_t i;

    if (world == NULL || world->map == NULL || world->sectors == NULL) {
        return 0;
    }
    tag = p_line_tag(world->map, lineIndex);

    for (i = 0; i < world->sectorCount; ++i) {
        p_sector_t* sector = &world->sectors[i];
        int min;
        int j;
        uint32_t lineCount;

        if (sector->tag != tag) {
            continue;
        }
        min = sector->lightLevel;
        lineCount = world->map->lumps[DOOM_MAP_LINEDEFS].count;
        for (j = 0; (uint32_t)j < lineCount; ++j) {
            doom_line_t line;
            uint16_t other;

            doom_map_get_line(world->map, (uint16_t)j, &line);
            if (!p_line_other_sector(world->map, &line, i, &other)) {
                continue;
            }
            if (world->sectors[other].lightLevel < min) {
                min = world->sectors[other].lightLevel;
            }
        }
        sector->lightLevel = (int16_t)min;
    }
    return 1;
}

/* --- The thinker --------------------------------------------------------- */

void p_light_thinker(p_thinker_t* thinker, void* context) {
    p_mobj_context_t* world = (p_mobj_context_t*)context;
    p_light_thinker_t* light = (p_light_thinker_t*)thinker;
    p_sector_t* sector;

    if (world == NULL || light == NULL || world->sectors == NULL ||
        (uint32_t)light->sector >= (uint32_t)world->sectorCount ||
        thinker->kind != (int16_t)P_THINKER_LIGHTFLASH) {
        return;
    }
    sector = &world->sectors[light->sector];

    switch (light->type) {
        case P_LIGHT_FLASH:
            light->count -= 1;
            if (light->count != 0) {
                return;
            }
            if (sector->lightLevel == light->maxLight) {
                sector->lightLevel = (int16_t)light->minLight;
                light->count =
                    (int32_t)(p_random(world->random) & light->maxTime) + 1;
            } else {
                sector->lightLevel = (int16_t)light->maxLight;
                light->count =
                    (int32_t)(p_random(world->random) & light->minTime) + 1;
            }
            break;

        case P_LIGHT_STROBE:
            light->count -= 1;
            if (light->count != 0) {
                return;
            }
            if (sector->lightLevel == light->minLight) {
                sector->lightLevel = (int16_t)light->maxLight;
                light->count = light->brightTime;
            } else {
                sector->lightLevel = (int16_t)light->minLight;
                light->count = light->darkTime;
            }
            break;

        case P_LIGHT_GLOW:
            if (light->direction == -1) {
                /* DOWN */
                sector->lightLevel =
                    (int16_t)(sector->lightLevel - P_GLOWSPEED);
                if (sector->lightLevel <= light->minLight) {
                    sector->lightLevel =
                        (int16_t)(sector->lightLevel + P_GLOWSPEED);
                    light->direction = 1;
                }
            } else if (light->direction == 1) {
                /* UP */
                sector->lightLevel =
                    (int16_t)(sector->lightLevel + P_GLOWSPEED);
                if (sector->lightLevel >= light->maxLight) {
                    sector->lightLevel =
                        (int16_t)(sector->lightLevel - P_GLOWSPEED);
                    light->direction = -1;
                }
            }
            break;

        case P_LIGHT_FIRE_FLICKER: {
            int amount;

            light->count -= 1;
            if (light->count != 0) {
                return;
            }
            amount = (p_random(world->random) & 3) * 16;
            if (sector->lightLevel - amount < light->minLight) {
                sector->lightLevel = (int16_t)light->minLight;
            } else {
                sector->lightLevel = (int16_t)(light->maxLight - amount);
            }
            light->count = 4;
            break;
        }

        default:
            break;
    }
}
