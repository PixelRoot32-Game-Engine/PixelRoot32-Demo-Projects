/*
 * Lifts (plats): EV_DoPlat, T_PlatRaise, EV_StopPlat and the in-stasis resume.
 *
 * The reference is linuxdoom-1.10 p_plats.c. The differences are the port's
 * conventions: a sector index instead of a pointer, and the thinker list instead
 * of activeplats[] (see p_plats.h).
 *
 * This is C99 only: no stdio, no floating point, no globals.
 */
#include "p_plats.h"

#include "p_floor.h"
#include "p_mobj.h"

/* One p_plat_thinker_t, linked and owned by the sector. */
static p_plat_thinker_t* new_plat(struct p_mobj_context* world,
                                  uint16_t sectorIndex, int16_t tag) {
    p_plat_thinker_t* plat = (p_plat_thinker_t*)p_spec_alloc_thinker(
        world, sizeof(p_plat_thinker_t), &p_plat_thinker, P_THINKER_PLAT);

    if (plat == NULL) {
        return NULL;
    }
    plat->sector = sectorIndex;
    plat->type = P_PLAT_PERPETUAL_RAISE;
    plat->status = P_PLAT_WAITING;
    plat->oldStatus = P_PLAT_WAITING;
    plat->crush = 0;
    plat->tag = tag;
    plat->speed = P_PLATSPEED;
    plat->low = 0;
    plat->high = 0;
    plat->wait = 0;
    plat->count = 0;
    world->sectors[sectorIndex].specialdata = &plat->thinker;
    return plat;
}

/* Clears the sector's owner and marks the thinker for removal. */
static void finish_plat(p_mobj_context_t* world, p_plat_thinker_t* plat) {
    if ((uint32_t)plat->sector < (uint32_t)world->sectorCount &&
        world->sectors[plat->sector].specialdata == &plat->thinker) {
        world->sectors[plat->sector].specialdata = NULL;
    }
    (void)p_thinker_remove(world->thinkers, &plat->thinker);
}

int p_activate_in_stasis_plats(struct p_mobj_context* world, int16_t tag) {
    const p_thinker_t* thinker;
    int rtn = 0;

    if (world == NULL || world->thinkers == NULL) {
        return 0;
    }
    for (thinker = p_thinker_first(world->thinkers); thinker != NULL;
         thinker = p_thinker_next(thinker)) {
        p_plat_thinker_t* plat;

        if (thinker->kind != (int16_t)P_THINKER_PLAT) {
            continue;
        }
        plat = (p_plat_thinker_t*)thinker;
        if (plat->tag == tag && plat->status == P_PLAT_IN_STASIS) {
            plat->status = plat->oldStatus;
            plat->thinker.function = &p_plat_thinker;
            rtn = 1;
        }
    }
    return rtn;
}

int p_do_plat(struct p_mobj_context* world, uint16_t lineIndex,
              p_plat_type_t type, int amount) {
    doom_line_t line;
    uint16_t frontSector = 0;
    int16_t tag;
    int secnum = -1;
    int rtn = 0;

    if (world == NULL || world->map == NULL || world->sectors == NULL) {
        return 0;
    }
    doom_map_get_line(world->map, lineIndex, &line);
    (void)p_line_front_sector(world->map, &line, &frontSector);
    tag = p_line_tag(world->map, lineIndex);

    /* Activate all <type> plats that are in_stasis. */
    if (type == P_PLAT_PERPETUAL_RAISE) {
        (void)p_activate_in_stasis_plats(world, tag);
    }

    while ((secnum = p_find_sector_from_line_tag(world, tag, secnum)) >= 0) {
        p_sector_t* sector = &world->sectors[secnum];
        p_plat_thinker_t* plat;

        if (sector->specialdata != NULL) {
            continue;
        }
        plat = new_plat(world, (uint16_t)secnum, tag);
        if (plat == NULL) {
            continue;
        }
        rtn = 1;
        plat->type = (int16_t)type;

        switch (type) {
            case P_PLAT_RAISE_TO_NEAREST_AND_CHANGE:
                plat->speed = P_PLATSPEED / 2;
                sector->floorPic = world->sectors[frontSector].floorPic;
                plat->high =
                    p_find_next_highest_floor(world, (uint16_t)secnum,
                                              sector->floorHeight);
                plat->wait = 0;
                plat->status = P_PLAT_UP;
                /* NO MORE DAMAGE, IF APPLICABLE */
                sector->special = 0;
                break;

            case P_PLAT_RAISE_AND_CHANGE:
                plat->speed = P_PLATSPEED / 2;
                sector->floorPic = world->sectors[frontSector].floorPic;
                plat->high = sector->floorHeight + amount * FRACUNIT;
                plat->wait = 0;
                plat->status = P_PLAT_UP;
                break;

            case P_PLAT_DOWN_WAIT_UP_STAY:
                plat->speed = P_PLATSPEED * 4;
                plat->low = p_find_lowest_floor_surrounding(world, (uint16_t)secnum);
                if (plat->low > sector->floorHeight) {
                    plat->low = sector->floorHeight;
                }
                plat->high = sector->floorHeight;
                plat->wait = 35 * P_PLATWAIT;
                plat->status = P_PLAT_DOWN;
                break;

            case P_PLAT_BLAZE_DWUS:
                plat->speed = P_PLATSPEED * 8;
                plat->low = p_find_lowest_floor_surrounding(world, (uint16_t)secnum);
                if (plat->low > sector->floorHeight) {
                    plat->low = sector->floorHeight;
                }
                plat->high = sector->floorHeight;
                plat->wait = 35 * P_PLATWAIT;
                plat->status = P_PLAT_DOWN;
                break;

            case P_PLAT_PERPETUAL_RAISE:
                plat->speed = P_PLATSPEED;
                plat->low = p_find_lowest_floor_surrounding(world, (uint16_t)secnum);
                if (plat->low > sector->floorHeight) {
                    plat->low = sector->floorHeight;
                }
                plat->high = p_find_highest_floor_surrounding(world, (uint16_t)secnum);
                if (plat->high < sector->floorHeight) {
                    plat->high = sector->floorHeight;
                }
                plat->wait = 35 * P_PLATWAIT;
                /* `plat->status = P_Random()&1;` -- one draw, at trigger time. */
                plat->status = (int16_t)(p_random(world->random) & 1);
                break;

            default:
                break;
        }
    }
    return rtn;
}

int p_stop_plat(struct p_mobj_context* world, uint16_t lineIndex) {
    int16_t tag;
    const p_thinker_t* thinker;
    int rtn = 0;

    if (world == NULL || world->map == NULL || world->thinkers == NULL) {
        return 0;
    }
    tag = p_line_tag(world->map, lineIndex);

    for (thinker = p_thinker_first(world->thinkers); thinker != NULL;
         thinker = p_thinker_next(thinker)) {
        p_plat_thinker_t* plat;

        if (thinker->kind != (int16_t)P_THINKER_PLAT) {
            continue;
        }
        plat = (p_plat_thinker_t*)thinker;
        if (plat->tag != tag || plat->status == P_PLAT_IN_STASIS) {
            continue;
        }
        plat->oldStatus = plat->status;
        plat->status = P_PLAT_IN_STASIS;
        plat->thinker.function = NULL;
        rtn = 1;
    }
    return rtn;
}

/* --- T_PlatRaise --------------------------------------------------------- */

void p_plat_thinker(p_thinker_t* thinker, void* context) {
    p_mobj_context_t* world = (p_mobj_context_t*)context;
    p_plat_thinker_t* plat = (p_plat_thinker_t*)thinker;
    p_sector_t* sector;
    p_move_result_t result;

    if (world == NULL || plat == NULL || world->sectors == NULL ||
        (uint32_t)plat->sector >= (uint32_t)world->sectorCount ||
        thinker->kind != (int16_t)P_THINKER_PLAT) {
        return;
    }
    sector = &world->sectors[plat->sector];

    switch (plat->status) {
        case P_PLAT_UP:
            result = p_move_plane(world, plat->sector, plat->speed, plat->high,
                                  plat->crush, 0, 1);
            if (result == P_MOVE_CRUSHED && !plat->crush) {
                plat->count = plat->wait;
                plat->status = P_PLAT_DOWN;
            } else if (result == P_MOVE_PASTDEST) {
                plat->count = plat->wait;
                plat->status = P_PLAT_WAITING;

                switch (plat->type) {
                    case P_PLAT_BLAZE_DWUS:
                    case P_PLAT_DOWN_WAIT_UP_STAY:
                        finish_plat(world, plat);
                        break;
                    case P_PLAT_RAISE_AND_CHANGE:
                    case P_PLAT_RAISE_TO_NEAREST_AND_CHANGE:
                        finish_plat(world, plat);
                        break;
                    default:
                        break;
                }
            }
            break;

        case P_PLAT_DOWN:
            result = p_move_plane(world, plat->sector, plat->speed, plat->low, 0,
                                  0, -1);
            if (result == P_MOVE_PASTDEST) {
                plat->count = plat->wait;
                plat->status = P_PLAT_WAITING;
            }
            break;

        case P_PLAT_WAITING:
            plat->count -= 1;
            if (plat->count == 0) {
                if (sector->floorHeight == plat->low) {
                    plat->status = P_PLAT_UP;
                } else {
                    plat->status = P_PLAT_DOWN;
                }
            }
            /* Upstream has no break here: it falls into in_stasis. */
            break;

        case P_PLAT_IN_STASIS:
        default:
            break;
    }
}
