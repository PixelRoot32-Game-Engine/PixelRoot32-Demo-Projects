/*
 * Floors: T_MovePlane and EV_DoFloor.
 *
 * The reference is linuxdoom-1.10 p_floor.c. Each rule cites the upstream
 * statement it reproduces; the differences are the port's conventions (a sector
 * is an index, the context is an argument, P_ChangeSector walks a derived
 * sector box instead of a blockmap) and they are stated at the site.
 *
 * This is C99 only: no stdio, no floating point, no globals. The context owns
 * the zone, the thinker list and the sector array.
 */
#include "p_floor.h"

#include <string.h>

#include "p_map.h"
#include "p_mobj.h"

/* --- P_ChangeSector ------------------------------------------------------ */

/* The rule itself lives in p_map.c with the tests it needs (P_ThingHeightClip
 * and PIT_ChangeSector); this is T_MovePlane's entry point, kept where the
 * movers have always called it. */
int p_spec_p_change_sector(struct p_mobj_context* world, uint16_t sector) {
    return p_change_sector(world, sector);
}

int p_spec_p_change_sector_is_stubbed(void) {
    return 0;
}

/* --- T_MovePlane --------------------------------------------------------- */

p_move_result_t p_move_plane(struct p_mobj_context* world, uint16_t sectorIndex,
                             fixed_t speed, fixed_t dest, int crush,
                             int floorOrCeiling, int direction) {
    p_sector_t* sector;
    fixed_t lastPos;
    int blocked;

    if (world == NULL || world->sectors == NULL ||
        (uint32_t)sectorIndex >= (uint32_t)world->sectorCount) {
        return P_MOVE_OK;
    }
    sector = &world->sectors[sectorIndex];

    if (floorOrCeiling == 0) {
        /* FLOOR */
        if (direction == -1) {
            /* `if (sector->floorheight - speed < dest)` -- the overshoot branch
             * clamps and answers pastdest even if the hook complains, because
             * the revert has already happened. */
            if (sector->floorHeight - speed < dest) {
                lastPos = sector->floorHeight;
                sector->floorHeight = dest;
                blocked = p_spec_p_change_sector(world, sectorIndex);
                if (blocked) {
                    sector->floorHeight = lastPos;
                    (void)p_spec_p_change_sector(world, sectorIndex);
                }
                p_spec_refresh_things(world, sectorIndex);
                return P_MOVE_PASTDEST;
            }
            lastPos = sector->floorHeight;
            sector->floorHeight -= speed;
            blocked = p_spec_p_change_sector(world, sectorIndex);
            if (blocked) {
                sector->floorHeight = lastPos;
                (void)p_spec_p_change_sector(world, sectorIndex);
                p_spec_refresh_things(world, sectorIndex);
                return P_MOVE_CRUSHED;
            }
        } else if (direction == 1) {
            if (sector->floorHeight + speed > dest) {
                lastPos = sector->floorHeight;
                sector->floorHeight = dest;
                blocked = p_spec_p_change_sector(world, sectorIndex);
                if (blocked) {
                    sector->floorHeight = lastPos;
                    (void)p_spec_p_change_sector(world, sectorIndex);
                }
                p_spec_refresh_things(world, sectorIndex);
                return P_MOVE_PASTDEST;
            }
            /* COULD GET CRUSHED: a crushing raise keeps the new height. */
            lastPos = sector->floorHeight;
            sector->floorHeight += speed;
            blocked = p_spec_p_change_sector(world, sectorIndex);
            if (blocked) {
                if (crush) {
                    p_spec_refresh_things(world, sectorIndex);
                    return P_MOVE_CRUSHED;
                }
                sector->floorHeight = lastPos;
                (void)p_spec_p_change_sector(world, sectorIndex);
                p_spec_refresh_things(world, sectorIndex);
                return P_MOVE_CRUSHED;
            }
        }
    } else if (floorOrCeiling == 1) {
        /* CEILING */
        if (direction == -1) {
            if (sector->ceilingHeight - speed < dest) {
                lastPos = sector->ceilingHeight;
                sector->ceilingHeight = dest;
                blocked = p_spec_p_change_sector(world, sectorIndex);
                if (blocked) {
                    sector->ceilingHeight = lastPos;
                    (void)p_spec_p_change_sector(world, sectorIndex);
                }
                p_spec_refresh_things(world, sectorIndex);
                return P_MOVE_PASTDEST;
            }
            lastPos = sector->ceilingHeight;
            sector->ceilingHeight -= speed;
            blocked = p_spec_p_change_sector(world, sectorIndex);
            if (blocked) {
                if (crush) {
                    p_spec_refresh_things(world, sectorIndex);
                    return P_MOVE_CRUSHED;
                }
                sector->ceilingHeight = lastPos;
                (void)p_spec_p_change_sector(world, sectorIndex);
                p_spec_refresh_things(world, sectorIndex);
                return P_MOVE_CRUSHED;
            }
        } else if (direction == 1) {
            if (sector->ceilingHeight + speed > dest) {
                lastPos = sector->ceilingHeight;
                sector->ceilingHeight = dest;
                blocked = p_spec_p_change_sector(world, sectorIndex);
                if (blocked) {
                    sector->ceilingHeight = lastPos;
                    (void)p_spec_p_change_sector(world, sectorIndex);
                }
                p_spec_refresh_things(world, sectorIndex);
                return P_MOVE_PASTDEST;
            }
            lastPos = sector->ceilingHeight;
            sector->ceilingHeight += speed;
            /* p_floor.c's revert in this branch is `#if 0`: a ceiling moving up
             * never undoes itself, so the hook is called and the answer ignored.
             */
            (void)p_spec_p_change_sector(world, sectorIndex);
            (void)lastPos;
        }
    }

    p_spec_refresh_things(world, sectorIndex);
    return P_MOVE_OK;
}

/* --- Floor helpers ------------------------------------------------------- */

/* The sector's front (side 0) sector, which is what `line->frontsector` is. */
static int front_sector(const doom_map_t* map, const doom_line_t* line,
                        uint16_t* out) {
    return p_line_front_sector(map, line, out);
}

/* One p_floor_thinker_t, linked and owned by the sector. */
static p_floor_thinker_t* new_floor(struct p_mobj_context* world,
                                    uint16_t sectorIndex, p_floor_type_t type) {
    p_floor_thinker_t* floor = (p_floor_thinker_t*)p_spec_alloc_thinker(
        world, sizeof(p_floor_thinker_t), &p_floor_thinker, P_THINKER_FLOOR);

    if (floor == NULL) {
        return NULL;
    }
    floor->sector = sectorIndex;
    floor->type = (int16_t)type;
    floor->crush = 0;
    floor->direction = 0;
    floor->newspecial = 0;
    floor->texture = R_NAME_NONE;
    floor->destHeight = 0;
    floor->speed = P_FLOORSPEED;
    world->sectors[sectorIndex].specialdata = &floor->thinker;
    return floor;
}

/* EV_DoFloor's switch, one case per floor_e. */
static int start_floor(struct p_mobj_context* world, uint16_t sectorIndex,
                       uint16_t lineIndex, p_floor_type_t type) {
    p_sector_t* sector = &world->sectors[sectorIndex];
    p_floor_thinker_t* floor;
    doom_line_t line;
    uint16_t front = 0;
    uint16_t other;
    int i;
    uint32_t lineCount;

    /* `textureheight[side->bottomtexture]` is r_data's table and is not in this
     * layer. No thinker is created, so nothing is left in the list. */
    if (type == P_FLOOR_RAISE_TO_TEXTURE) {
        if (world->specStats != NULL) {
            world->specStats->deferredLineSpecial += 1u;
        }
        return 0;
    }

    floor = new_floor(world, sectorIndex, type);
    if (floor == NULL) {
        return 0;
    }

    switch (type) {
        case P_FLOOR_LOWER:
            floor->direction = -1;
            floor->speed = P_FLOORSPEED;
            floor->destHeight = p_find_highest_floor_surrounding(world, sectorIndex);
            break;

        case P_FLOOR_LOWER_TO_LOWEST:
            floor->direction = -1;
            floor->speed = P_FLOORSPEED;
            floor->destHeight = p_find_lowest_floor_surrounding(world, sectorIndex);
            break;

        case P_FLOOR_TURBO_LOWER:
            floor->direction = -1;
            floor->speed = P_FLOORSPEED * 4;
            floor->destHeight = p_find_highest_floor_surrounding(world, sectorIndex);
            if (floor->destHeight != sector->floorHeight) {
                floor->destHeight += 8 * FRACUNIT;
            }
            break;

        case P_FLOOR_RAISE_CRUSH:
            floor->crush = 1;
            /* fall through */
        case P_FLOOR_RAISE:
            floor->direction = 1;
            floor->speed = P_FLOORSPEED;
            floor->destHeight = p_find_lowest_ceiling_surrounding(world, sectorIndex);
            if (floor->destHeight > sector->ceilingHeight) {
                floor->destHeight = sector->ceilingHeight;
            }
            if (type == P_FLOOR_RAISE_CRUSH) {
                floor->destHeight -= 8 * FRACUNIT;
            }
            break;

        case P_FLOOR_RAISE_TURBO:
            floor->direction = 1;
            floor->speed = P_FLOORSPEED * 4;
            floor->destHeight =
                p_find_next_highest_floor(world, sectorIndex, sector->floorHeight);
            break;

        case P_FLOOR_RAISE_TO_NEAREST:
            floor->direction = 1;
            floor->speed = P_FLOORSPEED;
            floor->destHeight =
                p_find_next_highest_floor(world, sectorIndex, sector->floorHeight);
            break;

        case P_FLOOR_RAISE_24:
            floor->direction = 1;
            floor->speed = P_FLOORSPEED;
            floor->destHeight = sector->floorHeight + 24 * FRACUNIT;
            break;

        case P_FLOOR_RAISE_512:
            floor->direction = 1;
            floor->speed = P_FLOORSPEED;
            floor->destHeight = sector->floorHeight + 512 * FRACUNIT;
            break;

        case P_FLOOR_RAISE_24_AND_CHANGE:
            floor->direction = 1;
            floor->speed = P_FLOORSPEED;
            floor->destHeight = sector->floorHeight + 24 * FRACUNIT;
            doom_map_get_line(world->map, lineIndex, &line);
            if (front_sector(world->map, &line, &front)) {
                sector->floorPic = world->sectors[front].floorPic;
                sector->special = world->sectors[front].special;
            }
            break;

        case P_FLOOR_RAISE_TO_TEXTURE:
            /* Handled before the thinker is allocated; unreachable here. */
            break;

        case P_FLOOR_LOWER_AND_CHANGE:
            floor->direction = -1;
            floor->speed = P_FLOORSPEED;
            floor->destHeight = p_find_lowest_floor_surrounding(world, sectorIndex);
            floor->texture = sector->floorPic;
            /* The neighbour whose floor is already at the destination donates
             * its floor picture and special. Upstream's `getSide(secnum,i,0)`
             * test is "side 0 belongs to this sector". */
            lineCount = world->map->lumps[DOOM_MAP_LINEDEFS].count;
            for (i = 0; (uint32_t)i < lineCount; ++i) {
                uint16_t side0Sector;
                uint16_t side1Sector;

                doom_map_get_line(world->map, (uint16_t)i, &line);
                if ((line.flags & DOOM_ML_TWOSIDED) == 0) {
                    continue;
                }
                if (!p_line_front_sector(world->map, &line, &side0Sector) ||
                    !p_line_back_sector(world->map, &line, &side1Sector)) {
                    continue;
                }
                if (side0Sector == sectorIndex) {
                    other = side1Sector;
                } else if (side1Sector == sectorIndex) {
                    other = side0Sector;
                } else {
                    continue;
                }
                if (world->sectors[other].floorHeight == floor->destHeight) {
                    floor->texture = world->sectors[other].floorPic;
                    floor->newspecial = world->sectors[other].special;
                    break;
                }
            }
            break;

        case P_FLOOR_DONUT_RAISE:
            /* Only EV_DoDonut creates this type; it fills both heights itself. */
            floor->direction = 1;
            floor->speed = P_FLOORSPEED / 2;
            break;

        default:
            break;
    }
    return 1;
}

int p_do_floor(struct p_mobj_context* world, uint16_t lineIndex,
               p_floor_type_t type) {
    int16_t tag;
    int secnum = -1;
    int rtn = 0;

    if (world == NULL || world->map == NULL || world->sectors == NULL) {
        return 0;
    }
    tag = p_line_tag(world->map, lineIndex);

    while ((secnum = p_find_sector_from_line_tag(world, tag, secnum)) >= 0) {
        /* ALREADY MOVING? IF SO, KEEP GOING... (p_floor.c EV_DoFloor) */
        if (world->sectors[secnum].specialdata != NULL) {
            continue;
        }
        rtn = start_floor(world, (uint16_t)secnum, lineIndex, type);
    }
    return rtn;
}

/* --- EV_DoDonut ---------------------------------------------------------- */

int p_do_donut(struct p_mobj_context* world, uint16_t lineIndex) {
    int16_t tag;
    int secnum = -1;
    int rtn = 0;

    if (world == NULL || world->map == NULL || world->sectors == NULL) {
        return 0;
    }
    tag = p_line_tag(world->map, lineIndex);

    while ((secnum = p_find_sector_from_line_tag(world, tag, secnum)) >= 0) {
        uint16_t s1 = (uint16_t)secnum;
        uint16_t s2 = 0;
        uint16_t s3 = 0;
        uint16_t other;
        uint32_t lineCount = world->map->lumps[DOOM_MAP_LINEDEFS].count;
        uint32_t i;
        doom_line_t line;
        int found = 0;

        if (world->sectors[s1].specialdata != NULL) {
            continue;
        }

        /* s2 = getNextSector(s1->lines[0], s1). The port's neighbour scan finds
         * the first two-sided neighbour in linedef order, which is the same
         * line P_GroupLines stores first for the sector in the maps this port
         * loads; the difference is documented rather than hidden. */
        for (i = 0; (uint32_t)i < lineCount; ++i) {
            doom_map_get_line(world->map, (uint16_t)i, &line);
            if (p_line_other_sector(world->map, &line, s1, &other) &&
                other != s1) {
                s2 = other;
                found = 1;
                break;
            }
        }
        if (!found) {
            continue;
        }

        /* s3 = the first other side of s2 that is not s1. */
        found = 0;
        for (i = 0; (uint32_t)i < lineCount; ++i) {
            uint16_t candidate;

            doom_map_get_line(world->map, (uint16_t)i, &line);
            if ((line.flags & DOOM_ML_TWOSIDED) == 0) {
                continue;
            }
            if (!p_line_other_sector(world->map, &line, s2, &candidate)) {
                continue;
            }
            if (candidate == s1 || candidate == s2) {
                continue;
            }
            s3 = candidate;
            found = 1;
            break;
        }
        if (!found) {
            continue;
        }

        rtn = 1;
        /* Rising slime: s2 rises to s3's floor and takes its texture. */
        {
            p_floor_thinker_t* floor = new_floor(world, s2, P_FLOOR_DONUT_RAISE);

            if (floor != NULL) {
                floor->type = P_FLOOR_DONUT_RAISE;
                floor->crush = 0;
                floor->direction = 1;
                floor->speed = P_FLOORSPEED / 2;
                floor->texture = world->sectors[s3].floorPic;
                floor->newspecial = 0;
                floor->destHeight = world->sectors[s3].floorHeight;
            }
        }
        /* Lowering donut-hole: s1 falls to s3's floor. */
        {
            p_floor_thinker_t* floor = new_floor(world, s1, P_FLOOR_LOWER);

            if (floor != NULL) {
                floor->type = P_FLOOR_LOWER;
                floor->crush = 0;
                floor->direction = -1;
                floor->speed = P_FLOORSPEED / 2;
                floor->destHeight = world->sectors[s3].floorHeight;
            }
        }
    }
    return rtn;
}

/* --- T_MoveFloor --------------------------------------------------------- */

void p_floor_thinker(p_thinker_t* thinker, void* context) {
    p_mobj_context_t* world = (p_mobj_context_t*)context;
    p_floor_thinker_t* floor = (p_floor_thinker_t*)thinker;
    p_sector_t* sector;
    p_move_result_t result;

    if (world == NULL || floor == NULL || world->sectors == NULL ||
        (uint32_t)floor->sector >= (uint32_t)world->sectorCount ||
        thinker->kind != (int16_t)P_THINKER_FLOOR) {
        return;
    }
    sector = &world->sectors[floor->sector];

    result = p_move_plane(world, floor->sector, floor->speed, floor->destHeight,
                          floor->crush, 0, floor->direction);
    if (result != P_MOVE_PASTDEST) {
        return;
    }

    sector->specialdata = NULL;
    if (floor->direction == 1 && floor->type == P_FLOOR_DONUT_RAISE) {
        sector->special = floor->newspecial;
        sector->floorPic = floor->texture;
    } else if (floor->direction == -1 && floor->type == P_FLOOR_LOWER_AND_CHANGE) {
        sector->special = floor->newspecial;
        sector->floorPic = floor->texture;
    }
    (void)p_thinker_remove(world->thinkers, thinker);
}
