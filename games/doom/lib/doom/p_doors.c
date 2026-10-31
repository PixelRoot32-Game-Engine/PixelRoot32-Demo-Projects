/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * Doors: EV_DoDoor, EV_VerticalDoor and T_VerticalDoor.
 *
 * The reference is linuxdoom-1.10 p_doors.c. The port's differences are the
 * conventions every layer here uses: the sector is an index, the world is an
 * argument, and the release is the thinker list's deferred unlink.
 *
 * This is C99 only: no stdio, no floating point, no globals.
 */
#include "p_doors.h"

#include <string.h>

#include "p_floor.h"
#include "p_mobj.h"

/* One p_door_thinker_t, linked and owned by the sector. */
static p_door_thinker_t* new_door(struct p_mobj_context* world,
                                  uint16_t sectorIndex) {
    p_door_thinker_t* door = (p_door_thinker_t*)p_spec_alloc_thinker(
        world, sizeof(p_door_thinker_t), &p_door_thinker, P_THINKER_DOOR);

    if (door == NULL) {
        return NULL;
    }
    door->sector = sectorIndex;
    door->type = P_DOOR_NORMAL;
    door->direction = 0;
    door->topHeight = 0;
    door->speed = P_VDOORSPEED;
    door->topWait = P_VDOORWAIT;
    door->topCountdown = 0;
    world->sectors[sectorIndex].specialdata = &door->thinker;
    return door;
}

/* `P_FindLowestCeilingSurrounding(sec) - 4*FRACUNIT`, the top every opening
 * door uses. */
static fixed_t door_top_height(struct p_mobj_context* world, uint16_t sector) {
    return p_find_lowest_ceiling_surrounding(world, sector) - 4 * FRACUNIT;
}

int p_do_door(struct p_mobj_context* world, uint16_t lineIndex,
              p_door_type_t type) {
    int16_t tag;
    int secnum = -1;
    int rtn = 0;

    if (world == NULL || world->map == NULL || world->sectors == NULL) {
        return 0;
    }
    tag = p_line_tag(world->map, lineIndex);

    while ((secnum = p_find_sector_from_line_tag(world, tag, secnum)) >= 0) {
        p_sector_t* sector = &world->sectors[secnum];
        p_door_thinker_t* door;

        /* `if (sec->specialdata) continue;` -- the tag-triggered merge rule. */
        if (sector->specialdata != NULL) {
            continue;
        }
        door = new_door(world, (uint16_t)secnum);
        if (door == NULL) {
            continue;
        }
        rtn = 1;
        door->type = (int16_t)type;
        door->topWait = P_VDOORWAIT;
        door->speed = P_VDOORSPEED;

        switch (type) {
            case P_DOOR_BLAZE_CLOSE:
                door->topHeight = door_top_height(world, (uint16_t)secnum);
                door->direction = -1;
                door->speed = P_VDOORSPEED * 4;
                break;

            case P_DOOR_CLOSE:
                door->topHeight = door_top_height(world, (uint16_t)secnum);
                door->direction = -1;
                break;

            case P_DOOR_CLOSE_30_THEN_OPEN:
                door->topHeight = sector->ceilingHeight;
                door->direction = -1;
                break;

            case P_DOOR_BLAZE_RAISE:
            case P_DOOR_BLAZE_OPEN:
                door->direction = 1;
                door->topHeight = door_top_height(world, (uint16_t)secnum);
                door->speed = P_VDOORSPEED * 4;
                break;

            case P_DOOR_NORMAL:
            case P_DOOR_OPEN:
                door->direction = 1;
                door->topHeight = door_top_height(world, (uint16_t)secnum);
                break;

            default:
                break;
        }
    }
    return rtn;
}

int p_vertical_door(struct p_mobj_context* world, uint16_t lineIndex,
                    int byPlayer) {
    doom_line_t line;
    uint16_t sectorIndex = 0;
    p_sector_t* sector;
    p_door_thinker_t* door;
    int16_t special;

    if (world == NULL || world->map == NULL || world->sectors == NULL ||
        world->lineSpecial == NULL) {
        return 0;
    }
    doom_map_get_line(world->map, lineIndex, &line);
    /* `sec = sides[line->sidenum[side^1]].sector;` with side = 0. */
    if (!p_line_back_sector(world->map, &line, &sectorIndex) ||
        (uint32_t)sectorIndex >= (uint32_t)world->sectorCount) {
        return 0;
    }
    sector = &world->sectors[sectorIndex];
    special = world->lineSpecial[lineIndex];

    /* if the sector has an active thinker, use it */
    if (sector->specialdata != NULL) {
        if (sector->specialdata->kind != (int16_t)P_THINKER_DOOR) {
            if (world->specStats != NULL) {
                world->specStats->triggersRefused += 1u;
            }
            return 0;
        }
        door = (p_door_thinker_t*)sector->specialdata;
        switch (special) {
            case 1:   /* ONLY FOR "RAISE" DOORS, NOT "OPEN"s */
            case 26:
            case 27:
            case 28:
            case 117:
                if (door->direction == -1) {
                    door->direction = 1;    /* go back up */
                } else {
                    if (!byPlayer) {
                        return 0;           /* bad guys never close doors */
                    }
                    door->direction = -1;   /* start going down immediately */
                }
                return 1;
            default:
                /* Upstream allocates a second door here and overwrites
                 * specialdata, orphaning the first. Refusing is the port's
                 * deliberate difference; see p_doors.h. */
                if (world->specStats != NULL) {
                    world->specStats->triggersRefused += 1u;
                }
                return 0;
        }
    }

    door = new_door(world, sectorIndex);
    if (door == NULL) {
        return 0;
    }
    door->sector = sectorIndex;
    door->direction = 1;
    door->speed = P_VDOORSPEED;
    door->topWait = P_VDOORWAIT;

    switch (special) {
        case 1:
        case 26:
        case 27:
        case 28:
            door->type = P_DOOR_NORMAL;
            break;

        case 31:
        case 32:
        case 33:
        case 34:
            door->type = P_DOOR_OPEN;
            world->lineSpecial[lineIndex] = 0;
            break;

        case 117:
            door->type = P_DOOR_BLAZE_RAISE;
            door->speed = P_VDOORSPEED * 4;
            break;

        case 118:
            door->type = P_DOOR_BLAZE_OPEN;
            door->speed = P_VDOORSPEED * 4;
            world->lineSpecial[lineIndex] = 0;
            break;

        default:
            /* Upstream's switch has no default and leaves `type` as Z_Malloc
             * left it. A zeroed record is `normal`, which is the only sane
             * answer; the dispatch never calls this for another number. */
            door->type = P_DOOR_NORMAL;
            break;
    }

    /* find the top and bottom of the movement range */
    door->topHeight = door_top_height(world, sectorIndex);
    return 1;
}

int p_spawn_door_close_in_30(struct p_mobj_context* world, uint16_t sectorIndex) {
    p_door_thinker_t* door;

    if (world == NULL || world->sectors == NULL ||
        (uint32_t)sectorIndex >= (uint32_t)world->sectorCount) {
        return 0;
    }
    door = new_door(world, sectorIndex);
    if (door == NULL) {
        return 0;
    }
    world->sectors[sectorIndex].special = 0;
    door->direction = 0;
    door->type = P_DOOR_NORMAL;
    door->speed = P_VDOORSPEED;
    door->topCountdown = 30 * P_TICRATE;
    if (world->specStats != NULL) {
        world->specStats->doorCloseIn30 += 1u;
    }
    return 1;
}

int p_spawn_door_raise_in_5_mins(struct p_mobj_context* world,
                                 uint16_t sectorIndex) {
    p_door_thinker_t* door;

    if (world == NULL || world->sectors == NULL ||
        (uint32_t)sectorIndex >= (uint32_t)world->sectorCount) {
        return 0;
    }
    door = new_door(world, sectorIndex);
    if (door == NULL) {
        return 0;
    }
    world->sectors[sectorIndex].special = 0;
    door->direction = 2;
    door->type = P_DOOR_RAISE_IN_5_MINS;
    door->speed = P_VDOORSPEED;
    door->topHeight = door_top_height(world, sectorIndex);
    door->topWait = P_VDOORWAIT;
    door->topCountdown = 5 * 60 * P_TICRATE;
    if (world->specStats != NULL) {
        world->specStats->doorRaiseIn5Mins += 1u;
    }
    return 1;
}

/* --- T_VerticalDoor ------------------------------------------------------ */

/* Clears the sector's owner and marks the thinker for removal. */
static void finish_door(p_mobj_context_t* world, p_door_thinker_t* door) {
    if ((uint32_t)door->sector < (uint32_t)world->sectorCount &&
        world->sectors[door->sector].specialdata == &door->thinker) {
        world->sectors[door->sector].specialdata = NULL;
    }
    (void)p_thinker_remove(world->thinkers, &door->thinker);
}

void p_door_thinker(p_thinker_t* thinker, void* context) {
    p_mobj_context_t* world = (p_mobj_context_t*)context;
    p_door_thinker_t* door = (p_door_thinker_t*)thinker;
    p_sector_t* sector;
    p_move_result_t result;

    if (world == NULL || door == NULL || world->sectors == NULL ||
        (uint32_t)door->sector >= (uint32_t)world->sectorCount ||
        thinker->kind != (int16_t)P_THINKER_DOOR) {
        return;
    }
    sector = &world->sectors[door->sector];

    switch (door->direction) {
        case 0:
            /* WAITING */
            door->topCountdown -= 1;
            if (door->topCountdown != 0) {
                break;
            }
            switch (door->type) {
                case P_DOOR_BLAZE_RAISE:
                case P_DOOR_NORMAL:
                    door->direction = -1;   /* time to go back down */
                    break;
                case P_DOOR_CLOSE_30_THEN_OPEN:
                    door->direction = 1;
                    break;
                default:
                    break;
            }
            break;

        case 2:
            /* INITIAL WAIT */
            door->topCountdown -= 1;
            if (door->topCountdown != 0) {
                break;
            }
            if (door->type == P_DOOR_RAISE_IN_5_MINS) {
                door->direction = 1;
                door->type = P_DOOR_NORMAL;
            }
            break;

        case -1:
            /* DOWN */
            result = p_move_plane(world, door->sector, door->speed,
                                  sector->floorHeight, 0, 1, -1);
            if (result == P_MOVE_PASTDEST) {
                switch (door->type) {
                    case P_DOOR_BLAZE_RAISE:
                    case P_DOOR_BLAZE_CLOSE:
                        finish_door(world, door);
                        break;
                    case P_DOOR_NORMAL:
                    case P_DOOR_CLOSE:
                        finish_door(world, door);
                        break;
                    case P_DOOR_CLOSE_30_THEN_OPEN:
                        door->direction = 0;
                        door->topCountdown = 35 * 30;
                        break;
                    default:
                        break;
                }
            } else if (result == P_MOVE_CRUSHED) {
                switch (door->type) {
                    case P_DOOR_BLAZE_CLOSE:
                    case P_DOOR_CLOSE:
                        /* DO NOT GO BACK UP! */
                        break;
                    default:
                        door->direction = 1;
                        break;
                }
            }
            break;

        case 1:
            /* UP */
            result = p_move_plane(world, door->sector, door->speed,
                                  door->topHeight, 0, 1, 1);
            if (result == P_MOVE_PASTDEST) {
                switch (door->type) {
                    case P_DOOR_BLAZE_RAISE:
                    case P_DOOR_NORMAL:
                        door->direction = 0;    /* wait at top */
                        door->topCountdown = door->topWait;
                        break;
                    case P_DOOR_CLOSE_30_THEN_OPEN:
                    case P_DOOR_BLAZE_OPEN:
                    case P_DOOR_OPEN:
                        finish_door(world, door);
                        break;
                    default:
                        break;
                }
            }
            break;

        default:
            break;
    }
}
