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
#include "p_inter.h"
#include "p_lights.h"
#include "p_map.h"
#include "p_mobj.h"
#include "p_plats.h"
#include "p_setup.h"
#include "p_user.h"
#include "r_tables.h"
#include "s_sound.h"

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
                /* 5/7/11/16 are the player-in-sector damage floors and 9 is the
                 * secret: they spawn nothing and keep their number for
                 * P_PlayerInSpecialSector, so they are not gaps. Everything
                 * else is a number neither upstream function names. */
                if (special != 5 && special != 7 && special != 11 &&
                    special != 16) {
                    level->specStats.unsupportedSectorSpecial += 1u;
                }
                break;
        }
    }

    /* Init line EFFECTs. Upstream's P_SpawnSpecials fills `linespeciallist[]`
     * with every line whose special is 48; this port allocates that list from
     * the level's arena, one entry per line, so it is sized to the map instead
     * of to upstream's MAXLINEANIMS (64) -- see p_spec.h. The scan is in line
     * order, which is upstream's, and `lineScrolls` still counts every line the
     * special names, including one with no front side (upstream would index
     * sides[0xffff] there; that record cannot scroll, so it is counted and not
     * stored). */
    {
        uint16_t scrolls = 0u;

        for (i = 0; i < level->lineCount; ++i) {
            if (p_line_special(level->map, i) == 48) {
                level->specStats.lineScrolls += 1u;
                scrolls += 1u;
            }
        }
        level->scrolls = NULL;
        level->scrollCount = 0u;
        if (scrolls != 0u) {
            zone_error_t error = ZONE_ERR_OK;
            p_scroll_t* table = (p_scroll_t*)z_alloc(
                &level->zone, (size_t)scrolls * sizeof(p_scroll_t), PU_LEVEL,
                &error);

            if (table == NULL) {
                level->specStats.allocationFailures += 1u;
            } else {
                uint16_t at = 0u;

                memset(table, 0, (size_t)scrolls * sizeof(p_scroll_t));
                for (i = 0; i < level->lineCount; ++i) {
                    doom_line_t line;

                    if (p_line_special(level->map, i) != 48) {
                        continue;
                    }
                    if (!p_line_decode(level->map, i, &line) ||
                        line.sideNum[0] == DOOM_NO_SIDE_INDEX) {
                        continue;
                    }
                    table[at].line = i;
                    table[at].side = line.sideNum[0];
                    table[at].dx = 0;
                    table[at].dy = 0;
                    at += 1u;
                }
                level->scrolls = table;
                level->scrollCount = at;
            }
        }
    }

    return P_SPEC_OK;
}

/* --- P_PlayerInSpecialSector --------------------------------------------- */

void p_player_in_special_sector(p_level_t* level, p_player_t* player) {
    uint16_t index;
    int16_t special;
    r_mobj_t* mo;

    if (level == NULL || player == NULL || level->sectors == NULL) {
        return;
    }
    mo = player->mo;
    if (mo == NULL) {
        return;
    }
    index = mo->sector;
    if ((uint32_t)index >= (uint32_t)level->sectorCount) {
        return;
    }

    /* "Falling, not all the way down yet?" A thing still on its way to the
     * floor takes nothing, however long the sector's special is on. */
    if (mo->z != level->sectors[index].floorHeight) {
        return;
    }

    special = level->sectors[index].special;
    switch (special) {
        case 5:
            /* HELLSLIME DAMAGE */
            if (player->powers[P_PW_IRONFEET] == 0 &&
                (level->leveltime & P_SECTOR_DAMAGE_MASK) == 0) {
                p_damage_mobj(&level->context, mo, NULL, NULL,
                              P_SECTOR_DAMAGE_HELLSLIME);
            }
            break;

        case 7:
            /* NUKAGE DAMAGE */
            if (player->powers[P_PW_IRONFEET] == 0 &&
                (level->leveltime & P_SECTOR_DAMAGE_MASK) == 0) {
                p_damage_mobj(&level->context, mo, NULL, NULL,
                              P_SECTOR_DAMAGE_NUKAGE);
            }
            break;

        case 16:
        case 4:
            /* SUPER HELLSLIME DAMAGE / STROBE HURT.
             *
             * `if (!player->powers[pw_ironfeet] || (P_Random()<5))`: the `||`
             * short-circuits, so the draw happens only when the suit is on --
             * and then it happens every tic, whether or not the roll hurts.
             * Losing that draw would shift every later P_Random() in the level. */
            if (player->powers[P_PW_IRONFEET] == 0 ||
                p_random(level->context.random) < P_SECTOR_DAMAGE_SUIT_ROLL) {
                if ((level->leveltime & P_SECTOR_DAMAGE_MASK) == 0) {
                    p_damage_mobj(&level->context, mo, NULL, NULL,
                                  P_SECTOR_DAMAGE_STRONG);
                }
            }
            break;

        case 9:
            /* SECRET SECTOR: the count is the player's and the clear is the
             * one-shot, so a second tic in the same sector does nothing. */
            player->secretcount += 1;
            level->sectors[index].special = 0;
            break;

        case 11:
            /* EXIT SUPER DAMAGE! (for E1M8 finale)
             *
             * `player->cheats &= ~CF_GODMODE;` -- there are no cheats (D5), so
             * there is nothing to clear. The order below is upstream's: the
             * damage lands first and the threshold is read after it. */
            if ((level->leveltime & P_SECTOR_DAMAGE_MASK) == 0) {
                p_damage_mobj(&level->context, mo, NULL, NULL,
                              P_SECTOR_DAMAGE_STRONG);
            }
            if (player->health <= P_SECTOR_EXIT_THRESHOLD) {
                /* G_ExitLevel(): `secretexit = false; gameaction =
                 * ga_completed`. p_spec.c cannot call g_game.c, so the request
                 * is the level's (p_exit_request_t) and the game state machine
                 * reads it -- the same path the exit line specials use. */
                level->exitRequest = (uint8_t)P_EXIT_NORMAL;
            }
            break;

        default:
            /* Upstream's `I_Error ("P_PlayerInSpecialSector: unknown special
             * %i")`. Nothing to do and nothing to fake: after
             * P_SpawnSpecials the field holds 4, 5, 7, 9, 11 or 16, because
             * every other spawn effect clears it. */
            break;
    }
}

/* --- EV_Teleport --------------------------------------------------------- */

int p_do_teleport(struct p_level* level, uint16_t lineIndex, r_mobj_t* thing,
                  int side) {
    int16_t tag;
    int secnum = -1;

    if (level == NULL || thing == NULL || level->sectors == NULL ||
        level->map == NULL) {
        return 0;
    }
    /* don't teleport missiles */
    if ((thing->flags & MF_MISSILE) != 0u) {
        return 0;
    }
    /* Don't teleport if hit back of line, so you can get out of teleporter. */
    if (side == 1) {
        return 0;
    }

    tag = p_line_tag(level->map, lineIndex);

    while ((secnum = p_find_sector_from_line_tag(&level->context, tag, secnum)) >= 0) {
        r_mobj_t* man;

        /* `thinker = thinkercap.next; ... if (m->type != MT_TELEPORTMAN)
         * continue; sector = m->subsector->sector; if (sector-sectors != i)
         * continue;` -- the list order is the spawn order and the sector test is
         * the teleportman's own subsector, not its record's sector field. */
        for (man = (r_mobj_t*)p_level_first_mobj(level); man != NULL;
             man = (r_mobj_t*)p_level_next_mobj(man)) {
            fixed_t oldx;
            fixed_t oldy;
            fixed_t oldz;

            if (man->type != (int16_t)MT_TELEPORTMAN) {
                continue;
            }
            if ((int)man->sector != secnum) {
                continue;
            }

            oldx = thing->x;
            oldy = thing->y;
            oldz = thing->z;

            if (!p_teleport_move(&level->context, thing, man->x, man->y)) {
                return 0;
            }
            thing->z = thing->floorz;
            if (thing->player != 0u) {
                level->playerState.viewz =
                    thing->z + level->playerState.viewheight;
            }

            /* spawn teleport fog at source and destination. The sound is
             * upstream's `S_StartSound (fog, sfx_telept)`: the *first* fog, the
             * one at the source, is the origin. */
            {
                r_mobj_t* fog = p_spawn_mobj(&level->context, oldx, oldy, oldz,
                                             MT_TFOG, NULL);

                if (fog != NULL) {
                    DOOM_SOUND_START(fog, sfx_telept);
                }
            }
            (void)p_spawn_mobj(
                &level->context,
                man->x + P_TELEPORT_FOG_DISTANCE * r_fine_cosine(man->angle),
                man->y + P_TELEPORT_FOG_DISTANCE *
                             r_fine_sine[man->angle >> ANGLETOFINESHIFT],
                thing->z, MT_TFOG, NULL);

            /* don't move for a bit */
            if (thing->player != 0u) {
                thing->reactiontime = P_TELEPORT_REACTIONTIME;
            }
            thing->angle = man->angle;
            thing->momx = 0;
            thing->momy = 0;
            thing->momz = 0;
            return 1;
        }
    }
    return 0;
}

/* --- Trigger dispatch ---------------------------------------------------- */

/* Clears a one-shot line, as `line->special = 0` does. */
static void consume_line(struct p_level* level, uint16_t lineIndex) {
    if (level->lineSpecial != NULL && lineIndex < level->lineCount) {
        level->lineSpecial[lineIndex] = 0;
    }
}

/*
 * Does upstream's trigger function act on this number?
 *
 * This is the line between "a handler this port has not written" and "upstream
 * does nothing here". P_CrossSpecialLine, P_UseSpecialLine and
 * P_ShootSpecialLine each switch over a fixed set; a number outside it is a
 * no-op -- P_CrossSpecialLine falls out of the switch, and P_UseSpecialLine
 * even returns true without doing anything -- so a port that also does nothing
 * is faithful and must not be counted as missing. Each list is copied from
 * linuxdoom-1.10's switch, in ascending order.
 */
static int upstream_handles(p_trigger_kind_t kind, int16_t special) {
    if (kind == P_TRIGGER_SHOOT) {
        switch (special) {
            case 24:
            case 46:
            case 47:
                return 1;
            default:
                return 0;
        }
    }
    if (kind == P_TRIGGER_USE) {
        switch (special) {
            /* manuals */
            case 1: case 26: case 27: case 28: case 31: case 32: case 33:
            case 34: case 117: case 118:
            /* switches and buttons */
            case 7: case 9: case 11: case 14: case 15: case 18: case 20:
            case 21: case 23: case 29: case 41: case 42: case 43: case 45:
            case 49: case 50: case 51: case 55: case 60: case 61: case 62:
            case 63: case 64: case 65: case 66: case 67: case 68: case 69:
            case 70: case 71: case 99: case 101: case 102: case 103: case 111:
            case 112: case 113: case 114: case 115: case 116: case 122:
            case 123: case 127: case 131: case 132: case 133: case 134:
            case 135: case 136: case 137: case 138: case 139: case 140:
                return 1;
            default:
                return 0;
        }
    }
    switch (special) {
        case 2: case 3: case 4: case 5: case 6: case 8: case 10: case 12:
        case 13: case 16: case 17: case 19: case 22: case 25: case 30:
        case 35: case 36: case 37: case 38: case 39: case 40: case 44:
        case 52: case 53: case 54: case 56: case 57: case 58: case 59:
        case 72: case 73: case 74: case 75: case 76: case 77: case 79:
        case 80: case 81: case 82: case 83: case 84: case 86: case 87:
        case 88: case 89: case 90: case 91: case 92: case 93: case 94:
        case 95: case 96: case 97: case 98: case 100: case 104: case 105:
        case 106: case 107: case 108: case 109: case 110: case 119:
        case 120: case 121: case 124: case 125: case 126: case 128:
        case 129: case 130: case 141:
            return 1;
        default:
            return 0;
    }
}

/*
 * Records a trigger that reached a line special this port does not implement.
 * The entry is marked `unsupported` only when upstream's trigger function acts
 * on the number; a number upstream ignores is recorded with the flag clear,
 * because doing nothing is its faithful answer and it must not be counted as a
 * missing handler. The first tic a pair is reached wins; later hits only add to
 * `hits`. A full table is reported through `overflow` instead of being silently
 * dropped.
 */
static void note_line_special(struct p_level* level, uint16_t lineIndex,
                              int16_t special, p_trigger_kind_t kind) {
    p_spec_trace_t* trace;
    const int unsupported = upstream_handles(kind, special);
    uint32_t index;

    if (unsupported) {
        level->specStats.unsupportedLineSpecial += 1u;
    }
    trace = level->context.specTrace;
    if (trace == NULL) {
        return;
    }
    for (index = 0u; index < trace->count; ++index) {
        if (trace->entries[index].special == special &&
            trace->entries[index].kind == (int16_t)kind) {
            trace->entries[index].hits += 1u;
            return;
        }
    }
    if (trace->count >= (uint32_t)P_SPEC_UNSUPPORTED_MAX) {
        trace->overflow += 1u;
        return;
    }
    trace->entries[trace->count].special = special;
    trace->entries[trace->count].kind = (int16_t)kind;
    trace->entries[trace->count].line = lineIndex;
    trace->entries[trace->count].unsupported = (uint8_t)unsupported;
    trace->entries[trace->count].firstTic = trace->tic;
    trace->entries[trace->count].hits = 1u;
    trace->count += 1u;
}

/* The short list a non-player thing may cross (P_CrossSpecialLine), plus the
 * two prologues upstream runs before it:
 *
 *   `switch(thing->type)` returns for MT_ROCKET, MT_PLASMA, MT_BFG,
 *   MT_TROOPSHOT, MT_HEADSHOT and MT_BRUISERSHOT -- a projectile never triggers
 *   a line -- and then the ok-list below decides. A NULL thing (the gate's "no
 *   actor" fixture) has no type to refuse, so it takes the ok-list alone.
 */
static int thing_is_missile(int16_t type) {
    switch (type) {
        case MT_ROCKET:
        case MT_PLASMA:
        case MT_BFG:
        case MT_TROOPSHOT:
        case MT_HEADSHOT:
        case MT_BRUISERSHOT:
            return 1;
        default:
            return 0;
    }
}

static int thing_can_cross(const r_mobj_t* thing, int16_t special) {
    if (thing != NULL && thing_is_missile(thing->type)) {
        return 0;
    }
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
                         r_mobj_t* thing, int side) {
    p_mobj_context_t* world = &level->context;
    int16_t special = level->lineSpecial[lineIndex];
    const int byPlayer = (thing != NULL && thing->player != 0u) ? 1 : 0;

    if (!byPlayer && !thing_can_cross(thing, special)) {
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
        /* Build Stairs. P_CrossSpecialLine ignores EV_BuildStairs' answer and
         * clears the line either way. */
        case 8:
            (void)p_do_stairs(world, lineIndex, P_STAIR_BUILD8);
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
        case 39:
            /* TELEPORT! `EV_Teleport(line, side, thing); line->special = 0;` --
             * the clear is unconditional, so a refused teleport still consumes
             * a one-shot line. */
            (void)p_do_teleport(level, lineIndex, thing, side);
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
        /* EXIT! / SECRET EXIT. Upstream's P_CrossSpecialLine has no
         * `line->special = 0` for either number, so a walk-over exit stays on the
         * line; G_ExitLevel()/G_SecretExitLevel() are the write. */
        case 52:
            level->exitRequest = (uint8_t)P_EXIT_NORMAL;
            return 1;
        case 124:
            level->exitRequest = (uint8_t)P_EXIT_SECRET;
            return 1;
        case 125:
            /* TELEPORT MonsterONLY: `if (!thing->player) { EV_Teleport(...);
             * line->special = 0; }` -- a player walks over it and nothing
             * happens, not even the clear. */
            if (!byPlayer) {
                (void)p_do_teleport(level, lineIndex, thing, side);
                consume_line(level, lineIndex);
                return 1;
            }
            return 0;
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
        case 100:
            /* Build Stairs Turbo 16: EV_BuildStairs(line, turbo16). */
            (void)p_do_stairs(world, lineIndex, P_STAIR_TURBO16);
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
        case 97:
            /* TELEPORT! A retrigger: no clear. */
            (void)p_do_teleport(level, lineIndex, thing, side);
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
        case 126:
            /* TELEPORT MonsterONLY, retrigger: no clear when it fires. */
            if (!byPlayer) {
                (void)p_do_teleport(level, lineIndex, thing, side);
                return 1;
            }
            return 0;
        case 128:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE_TO_NEAREST);
            return 1;
        case 129:
            (void)p_do_floor(world, lineIndex, P_FLOOR_RAISE_TURBO);
            return 1;

        default:
            /* 48 scroll, and every number upstream's cross switch does not
             * name, do nothing. Stairs (8, 100) and teleports (39, 97, 125,
             * 126) are handled above. */
            note_line_special(level, lineIndex, special, P_TRIGGER_CROSS);
            return 0;
    }
}

/* The switch-side numbers P_UseSpecialLine dispatches. `useAgain` is true for
 * the button specials, which upstream leaves reusable; `clear` says whether a
 * one-shot switch clears its line. The texture change itself is deferred (see
 * p_switch in this file's report). */
static int trigger_use_arm(struct p_level* level, uint16_t lineIndex,
                           r_mobj_t* thing, int side) {
    p_mobj_context_t* world = &level->context;
    int16_t special = level->lineSpecial[lineIndex];
    const int byPlayer = (thing != NULL && thing->player != 0u) ? 1 : 0;

    /* `if (side) { switch(line->special) { case 124: break; default: return
     * false; } }` -- "Use the back sides of VERY SPECIAL lines". 124 is the
     * sliding door upstream compiled out, so reaching the switch with it does
     * nothing at all; the port's use switch has no case for it either. */
    if (side != 0 && special != 124) {
        return 0;
    }

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
            /* EXIT LEVEL / SECRET EXIT. Upstream runs P_ChangeSwitchTexture(line,
             * 0) first -- which clears `line->special` and starts the switch
             * sound -- then G_ExitLevel()/G_SecretExitLevel(), which set
             * `gameaction = ga_completed` and `secretexit`. The texture half has
             * no texture to draw (p_spec.h's measurement), so the one-shot clear
             * is the part of it the simulation keeps; the exit request is the
             * level-level form of the two globals. */
            consume_line(level, lineIndex);
            level->exitRequest = (uint8_t)(special == 11 ? P_EXIT_NORMAL
                                                         : P_EXIT_SECRET);
            return 1;
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

        /* Stairs. Upstream is `if (EV_BuildStairs(line, type))
         * P_ChangeSwitchTexture(line, 0);` -- the only switch in this switch
         * whose EV answer gates the one-shot clear, which is why the clear and
         * the deferred count are inside the `if` here. */
        case 7:
            if (p_do_stairs(world, lineIndex, P_STAIR_BUILD8)) {
                level->specStats.deferredLineSpecial += 1u;
                consume_line(level, lineIndex);
            }
            return 1;
        case 127:
            if (p_do_stairs(world, lineIndex, P_STAIR_TURBO16)) {
                level->specStats.deferredLineSpecial += 1u;
                consume_line(level, lineIndex);
            }
            return 1;

        default:
            note_line_special(level, lineIndex, special, P_TRIGGER_USE);
            return 0;
    }
}

/*
 * P_ChangeSwitchTexture's audible half, in one place.
 *
 * The arms do not each call it: they already mark the deferred texture change
 * with `deferredLineSpecial`, so this wrapper starts the cue for exactly the
 * arms that deferred one. Upstream's rule is `line->special == 11 ? sfx_swtchx
 * : sfx_swtchn`; the two exit specials write their one-shot clear directly and
 * defer no texture, so they are named here to keep their switch audible.
 *
 * The port's counter is broader than upstream's P_ChangeSwitchTexture: a few
 * door and floor arms that upstream clears without a texture change count too.
 * They get the same cue -- the port's existing model, made audible -- and that
 * small overshoot is stated in the report rather than hidden.
 */
static int trigger_use(struct p_level* level, uint16_t lineIndex,
                       r_mobj_t* thing, int side) {
    const int16_t special = level->lineSpecial[lineIndex];
    const uint32_t before = level->specStats.deferredLineSpecial;
    const int handled = trigger_use_arm(level, lineIndex, thing, side);

    if (handled
        && (level->specStats.deferredLineSpecial != before || special == 11
            || special == 51)) {
        DOOM_SOUND_START(thing, special == 11 ? sfx_swtchx : sfx_swtchn);
    }
    return handled;
}

static int trigger_shoot(struct p_level* level, uint16_t lineIndex,
                         r_mobj_t* thing, int side) {
    p_mobj_context_t* world = &level->context;
    int16_t special = level->lineSpecial[lineIndex];
    const int byPlayer = (thing != NULL && thing->player != 0u) ? 1 : 0;

    (void)side;
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
            note_line_special(level, lineIndex, special, P_TRIGGER_SHOOT);
            return 0;
    }
}

int p_trigger_special_line(struct p_level* level, uint16_t lineIndex,
                           p_trigger_kind_t kind, r_mobj_t* thing, int side) {
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
            result = trigger_use(level, lineIndex, thing, side);
            break;
        case P_TRIGGER_SHOOT:
            result = trigger_shoot(level, lineIndex, thing, side);
            break;
        case P_TRIGGER_CROSS:
        default:
            result = trigger_cross(level, lineIndex, thing, side);
            break;
    }
    if (result) {
        level->specStats.triggers += 1u;
    }
    return result;
}

/* --- P_UpdateSpecials: scrolling walls and animated pictures -------------- */

/*
 * The animdefs[] families the shareware WAD actually has.
 *
 * Upstream's table has 22 entries and P_InitPicAnims skips every one whose
 * start name the WAD does not carry; measured, DOOM1.WAD carries two of them
 * (see p_spec.h). `base` is the family's own base index in the WAD's flat or
 * texture table, because upstream's `pic = basepic + ((leveltime/speed + i) %
 * numpics)` uses the *absolute* index `i` and therefore the base's phase. Both
 * numbers are measurements, and the scrolls stage of tools/specials_check.py
 * recomputes them from the WAD it is given, so a WAD with different tables
 * diverges instead of silently animating to the wrong frame.
 */
typedef struct p_anim_family {
    uint8_t isTexture;
    uint8_t numpics;
    uint8_t speed;
    int16_t base;
    r_name_t frames[P_ANIM_MAXPICS];
} p_anim_family_t;

/* The packed name of an eight-byte WAD name field, most significant byte first
 * (r_pack_name's order). A short name is NUL padded, as the field is. */
#define P_NAME8(a, b, c, d, e, f, g, h)                                       \
    ((((r_name_t)(uint8_t)(a)) << 56) | (((r_name_t)(uint8_t)(b)) << 48) |     \
     (((r_name_t)(uint8_t)(c)) << 40) | (((r_name_t)(uint8_t)(d)) << 32) |     \
     (((r_name_t)(uint8_t)(e)) << 24) | (((r_name_t)(uint8_t)(f)) << 16) |     \
     (((r_name_t)(uint8_t)(g)) << 8) | ((r_name_t)(uint8_t)(h)))

static const p_anim_family_t kAnimFamilies[P_ANIM_FAMILIES] = {
    /* {false, "NUKAGE3", "NUKAGE1", 8}: base 51, three frames. */
    {
        0u, 3u, 8u, 51,
        {
            P_NAME8('N', 'U', 'K', 'A', 'G', 'E', '1', 0),
            P_NAME8('N', 'U', 'K', 'A', 'G', 'E', '2', 0),
            P_NAME8('N', 'U', 'K', 'A', 'G', 'E', '3', 0),
        },
    },
    /* {true, "SLADRIP3", "SLADRIP1", 8}: base 61, three frames. */
    {
        1u, 3u, 8u, 61,
        {
            P_NAME8('S', 'L', 'A', 'D', 'R', 'I', 'P', '1'),
            P_NAME8('S', 'L', 'A', 'D', 'R', 'I', 'P', '2'),
            P_NAME8('S', 'L', 'A', 'D', 'R', 'I', 'P', '3'),
        },
    },
};

/// The family member @p name is, or -1 when no picture of that family holds it.
static int anim_picture_index(int family, r_name_t name) {
    const p_anim_family_t* anim = &kAnimFamilies[family];
    int k;

    for (k = 0; k < (int)anim->numpics; ++k) {
        if (anim->frames[k] == name) {
            return k;
        }
    }
    return -1;
}

void p_anim_names(r_name_t out[P_ANIM_PICTURES]) {
    int family;
    int k;

    if (out == NULL) {
        return;
    }
    for (family = 0; family < P_ANIM_FAMILIES; ++family) {
        const p_anim_family_t* anim = &kAnimFamilies[family];

        for (k = 0; k < (int)anim->numpics; ++k) {
            out[family * P_ANIM_MAXPICS + k] = anim->frames[k];
        }
    }
}

r_name_t p_anim_frame(int32_t animTime, r_name_t name) {
    int family;
    int k;

    if (animTime < 0 || !r_has_name(name)) {
        return name;
    }
    for (family = 0; family < P_ANIM_FAMILIES; ++family) {
        const p_anim_family_t* anim = &kAnimFamilies[family];

        k = anim_picture_index(family, name);
        if (k < 0) {
            continue;
        }
        {
            /* `pic = basepic + ((leveltime/speed + i) % numpics)` with i the
             * family member's absolute index, base + k. animTime is
             * non-negative and the sum is positive, so C's truncating % is
             * upstream's here. */
            const int32_t pic =
                ((animTime / (int32_t)anim->speed) + (int32_t)anim->base + k)
                % (int32_t)anim->numpics;

            return anim->frames[pic];
        }
    }
    return name;
}

int p_anim_is_animated(r_name_t name) {
    int family;

    if (!r_has_name(name)) {
        return 0;
    }
    for (family = 0; family < P_ANIM_FAMILIES; ++family) {
        if (anim_picture_index(family, name) >= 0) {
            return 1;
        }
    }
    return 0;
}

void p_update_specials(p_level_t* level) {
    uint16_t i;
    int family;
    int k;

    if (level == NULL || !level->open) {
        return;
    }

    /* "ANIMATE LINE SPECIALS": upstream re-reads `line->special` every tic, so a
     * number a trigger cleared stops scrolling. */
    for (i = 0; i < level->scrollCount; ++i) {
        p_scroll_t* scroll = &level->scrolls[i];

        if (p_line_special(level->map, scroll->line) != 48) {
            continue;
        }
        scroll->dx += P_SCROLL_STEP;
        /* `dy` stays 0: this upstream's case 48 has no vertical term. */
    }

    /* "ANIMATE FLATS AND TEXTURES GLOBALLY": the translations are keyed by the
     * clock they were computed with, which is this tic's leveltime -- P_Ticker
     * runs this before `leveltime++`, and p_anim_frame() reads what it stored.
     *
     * Upstream writes `flattranslation[]`/`texturetranslation[]` here, once per
     * tic, and every draw then looks a picture up through those arrays. This
     * port has no picture numbers, so the same write is a six-name table plus
     * each sector's index into it: the renderer's live reader answers from these
     * two values, and a per-segment read no longer runs the animation at all. */
    level->animTime = level->leveltime;
    for (family = 0; family < P_ANIM_FAMILIES; ++family) {
        const p_anim_family_t* anim = &kAnimFamilies[family];

        for (k = 0; k < (int)anim->numpics; ++k) {
            level->animFrames[family * P_ANIM_MAXPICS + k] =
                p_anim_frame(level->animTime, anim->frames[k]);
        }
    }
    /* The source names come from the level's RAM copy (p_anim_names), not from
     * kAnimFamilies: this loop is per sector per tic, right after the thinker
     * walk, and the flash cache is cold there. Only a sector whose frame really
     * changed is written, so the array is not dirtied every tic. */
    for (i = 0; i < level->sectorCount; ++i) {
        p_sector_t* sector = &level->sectors[i];
        uint8_t floorFrame = P_ANIM_IDENTITY;
        uint8_t ceilingFrame = P_ANIM_IDENTITY;

        for (k = 0; k < P_ANIM_MAXPICS; ++k) {
            if (level->animNames[k] == sector->floorPic) {
                floorFrame = (uint8_t)k;
            }
            if (level->animNames[k] == sector->ceilingPic) {
                ceilingFrame = (uint8_t)k;
            }
        }
        if (sector->floorFrame != floorFrame) {
            sector->floorFrame = floorFrame;
        }
        if (sector->ceilingFrame != ceilingFrame) {
            sector->ceilingFrame = ceilingFrame;
        }
    }
}

/* --- The renderer's live seams -------------------------------------------- */

/// One picture of the family @p family at the level's clock, resolved here so a
/// sector read is two array lookups instead of two animation runs.
static r_name_t sector_picture(const p_level_t* level, r_name_t raw,
                               uint8_t frame) {
    if (frame == P_ANIM_IDENTITY) {
        return raw;
    }
    return level->animFrames[frame];
}

void p_level_read_sector(void* context, uint16_t index, doom_sector_t* out) {
    const p_level_t* level = (const p_level_t*)context;
    const p_sector_t* sector;

    if (level == NULL || out == NULL || level->sectors == NULL ||
        (uint32_t)index >= (uint32_t)level->sectorCount) {
        return;
    }
    sector = &level->sectors[index];
    out->floorHeight = sector->floorHeight;
    out->ceilingHeight = sector->ceilingHeight;
    /* The WAD's name through P_UpdateSpecials' translation, which is what
     * upstream's R_FlatNumForName(flattranslation[...]) resolves to. */
    out->floorPic = sector_picture(level, sector->floorPic, sector->floorFrame);
    out->ceilingPic =
        sector_picture(level, sector->ceilingPic, sector->ceilingFrame);
    out->lightLevel = sector->lightLevel;
    out->special = sector->special;
    out->tag = sector->tag;
}

void p_level_adjust_side(void* context, uint16_t index, doom_side_t* side) {
    const p_level_t* level = (const p_level_t*)context;

    if (level == NULL || side == NULL) {
        return;
    }
    p_scroll_adjust_side(level->scrolls, level->scrollCount, index, side);
}

r_name_t p_level_translate_name(void* context, r_name_t name) {
    const p_level_t* level = (const p_level_t*)context;

    if (level == NULL) {
        return name;
    }
    return p_anim_frame(level->animTime, name);
}

/// True when a side of @p map names a texture some family can animate.
///
/// A map that names none of them has no side a translation could change, which
/// is E1M1's case (and every episode-1 map but E1M2 and E1M4, whose SLADRIP
/// sides do animate). The scan runs once per bind, over the immutable records
/// with no seam installed, so it cannot see a translated name.
static int map_animates_texture(const doom_map_t* map) {
    uint32_t index;

    if (map == NULL) {
        return 0;
    }
    for (index = 0; index < map->lumps[DOOM_MAP_SIDEDEFS].count; ++index) {
        doom_side_t side;

        doom_map_get_side(map, (uint16_t)index, &side);
        if (anim_picture_index(1, side.topTexture) >= 0 ||
            anim_picture_index(1, side.bottomTexture) >= 0 ||
            anim_picture_index(1, side.midTexture) >= 0) {
            return 1;
        }
    }
    return 0;
}

void p_level_bind_render(p_level_t* level, doom_map_t* map) {
    if (level == NULL || map == NULL) {
        return;
    }
    /* Clear first: the scan below must read the immutable records even when the
     * caller re-binds a map that is already bound. */
    p_level_unbind_render(map);
    map->readSector = p_level_read_sector;
    map->readSectorContext = level;
    map->adjustSide = p_level_adjust_side;
    map->adjustSideContext = level;
    /* The texture half of the animation is installed only where it can change a
     * name (p_spec.h). The flat half needs no seam: p_level_read_sector answers
     * the frame P_UpdateSpecials() selected. */
    if (map_animates_texture(map)) {
        map->translateName = p_level_translate_name;
        map->translateNameContext = level;
    }
}

void p_level_unbind_render(doom_map_t* map) {
    if (map == NULL) {
        return;
    }
    map->readSector = NULL;
    map->readSectorContext = NULL;
    map->adjustSide = NULL;
    map->adjustSideContext = NULL;
    map->translateName = NULL;
    map->translateNameContext = NULL;
}
