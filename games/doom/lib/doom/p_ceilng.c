/*
 * Ceilings: EV_DoCeiling, T_MoveCeiling, EV_CeilingCrushStop.
 *
 * The reference is linuxdoom-1.10 p_ceilng.c. The switch fall-throughs in
 * T_MoveCeiling are reproduced deliberately, including the one that sets
 * CEILSPEED twice; p_ceilng.h says what each type does.
 *
 * This is C99 only: no stdio, no floating point, no globals.
 */
#include "p_ceilng.h"

#include "p_floor.h"
#include "p_mobj.h"

/* One p_ceiling_thinker_t, linked and owned by the sector. */
static p_ceiling_thinker_t* new_ceiling(struct p_mobj_context* world,
                                        uint16_t sectorIndex, int16_t tag) {
    p_ceiling_thinker_t* ceiling = (p_ceiling_thinker_t*)p_spec_alloc_thinker(
        world, sizeof(p_ceiling_thinker_t), &p_ceiling_thinker,
        P_THINKER_CEILING);

    if (ceiling == NULL) {
        return NULL;
    }
    ceiling->sector = sectorIndex;
    ceiling->type = P_CEIL_LOWER_TO_FLOOR;
    ceiling->direction = 0;
    ceiling->crush = 0;
    ceiling->tag = tag;
    ceiling->oldDirection = 0;
    ceiling->bottomHeight = 0;
    ceiling->topHeight = 0;
    ceiling->speed = P_CEILSPEED;
    world->sectors[sectorIndex].specialdata = &ceiling->thinker;
    return ceiling;
}

/* Clears the sector's owner and marks the thinker for removal. */
static void finish_ceiling(p_mobj_context_t* world, p_ceiling_thinker_t* ceiling) {
    if ((uint32_t)ceiling->sector < (uint32_t)world->sectorCount &&
        world->sectors[ceiling->sector].specialdata == &ceiling->thinker) {
        world->sectors[ceiling->sector].specialdata = NULL;
    }
    (void)p_thinker_remove(world->thinkers, &ceiling->thinker);
}

int p_activate_in_stasis_ceilings(struct p_mobj_context* world, int16_t tag) {
    const p_thinker_t* thinker;
    int rtn = 0;

    if (world == NULL || world->thinkers == NULL) {
        return 0;
    }
    for (thinker = p_thinker_first(world->thinkers); thinker != NULL;
         thinker = p_thinker_next(thinker)) {
        p_ceiling_thinker_t* ceiling;

        if (thinker->kind != (int16_t)P_THINKER_CEILING) {
            continue;
        }
        ceiling = (p_ceiling_thinker_t*)thinker;
        if (ceiling->tag == tag && ceiling->direction == 0) {
            ceiling->direction = ceiling->oldDirection;
            ceiling->thinker.function = &p_ceiling_thinker;
            rtn = 1;
        }
    }
    return rtn;
}

int p_ceiling_crush_stop(struct p_mobj_context* world, uint16_t lineIndex) {
    int16_t tag;
    const p_thinker_t* thinker;
    int rtn = 0;

    if (world == NULL || world->map == NULL || world->thinkers == NULL) {
        return 0;
    }
    tag = p_line_tag(world->map, lineIndex);

    for (thinker = p_thinker_first(world->thinkers); thinker != NULL;
         thinker = p_thinker_next(thinker)) {
        p_ceiling_thinker_t* ceiling;

        if (thinker->kind != (int16_t)P_THINKER_CEILING) {
            continue;
        }
        ceiling = (p_ceiling_thinker_t*)thinker;
        if (ceiling->tag != tag || ceiling->direction == 0) {
            continue;
        }
        ceiling->oldDirection = ceiling->direction;
        ceiling->thinker.function = NULL;
        ceiling->direction = 0;    /* in-stasis */
        rtn = 1;
    }
    return rtn;
}

int p_do_ceiling(struct p_mobj_context* world, uint16_t lineIndex,
                 p_ceiling_type_t type) {
    int16_t tag;
    int secnum = -1;
    int rtn = 0;

    if (world == NULL || world->map == NULL || world->sectors == NULL) {
        return 0;
    }
    tag = p_line_tag(world->map, lineIndex);

    /* Reactivate in-stasis ceilings... for certain types. */
    if (type == P_CEIL_FAST_CRUSH_AND_RAISE ||
        type == P_CEIL_SILENT_CRUSH_AND_RAISE ||
        type == P_CEIL_CRUSH_AND_RAISE) {
        (void)p_activate_in_stasis_ceilings(world, tag);
    }

    while ((secnum = p_find_sector_from_line_tag(world, tag, secnum)) >= 0) {
        p_sector_t* sector = &world->sectors[secnum];
        p_ceiling_thinker_t* ceiling;

        /* `if (sec->specialdata) continue;` */
        if (sector->specialdata != NULL) {
            continue;
        }
        ceiling = new_ceiling(world, (uint16_t)secnum, tag);
        if (ceiling == NULL) {
            continue;
        }
        rtn = 1;
        ceiling->crush = 0;

        switch (type) {
            case P_CEIL_FAST_CRUSH_AND_RAISE:
                ceiling->crush = 1;
                ceiling->topHeight = sector->ceilingHeight;
                ceiling->bottomHeight = sector->floorHeight + 8 * FRACUNIT;
                ceiling->direction = -1;
                ceiling->speed = P_CEILSPEED * 2;
                break;

            case P_CEIL_SILENT_CRUSH_AND_RAISE:
            case P_CEIL_CRUSH_AND_RAISE:
                ceiling->crush = 1;
                ceiling->topHeight = sector->ceilingHeight;
                /* fall through */
            case P_CEIL_LOWER_AND_CRUSH:
            case P_CEIL_LOWER_TO_FLOOR:
                ceiling->bottomHeight = sector->floorHeight;
                if (type != P_CEIL_LOWER_TO_FLOOR) {
                    ceiling->bottomHeight += 8 * FRACUNIT;
                }
                ceiling->direction = -1;
                ceiling->speed = P_CEILSPEED;
                break;

            case P_CEIL_RAISE_TO_HIGHEST:
                ceiling->topHeight =
                    p_find_highest_ceiling_surrounding(world, (uint16_t)secnum);
                ceiling->direction = 1;
                ceiling->speed = P_CEILSPEED;
                break;

            default:
                break;
        }

        ceiling->tag = sector->tag;
        ceiling->type = (int16_t)type;
    }
    return rtn;
}

/* --- T_MoveCeiling ------------------------------------------------------- */

void p_ceiling_thinker(p_thinker_t* thinker, void* context) {
    p_mobj_context_t* world = (p_mobj_context_t*)context;
    p_ceiling_thinker_t* ceiling = (p_ceiling_thinker_t*)thinker;
    p_move_result_t result;

    if (world == NULL || ceiling == NULL || world->sectors == NULL ||
        (uint32_t)ceiling->sector >= (uint32_t)world->sectorCount ||
        thinker->kind != (int16_t)P_THINKER_CEILING) {
        return;
    }

    switch (ceiling->direction) {
        case 0:
            /* IN STASIS */
            break;

        case 1:
            /* UP */
            result = p_move_plane(world, ceiling->sector, ceiling->speed,
                                  ceiling->topHeight, 0, 1, 1);
            if (result == P_MOVE_PASTDEST) {
                switch (ceiling->type) {
                    case P_CEIL_RAISE_TO_HIGHEST:
                        finish_ceiling(world, ceiling);
                        break;

                    case P_CEIL_SILENT_CRUSH_AND_RAISE:
                    case P_CEIL_FAST_CRUSH_AND_RAISE:
                    case P_CEIL_CRUSH_AND_RAISE:
                        ceiling->direction = -1;
                        break;

                    default:
                        break;
                }
            }
            break;

        case -1:
            /* DOWN */
            result = p_move_plane(world, ceiling->sector, ceiling->speed,
                                  ceiling->bottomHeight, ceiling->crush, 1, -1);
            if (result == P_MOVE_PASTDEST) {
                switch (ceiling->type) {
                    case P_CEIL_SILENT_CRUSH_AND_RAISE:
                        /* fall through: silent also sets CEILSPEED */
                    case P_CEIL_CRUSH_AND_RAISE:
                        ceiling->speed = P_CEILSPEED;
                        /* fall through */
                    case P_CEIL_FAST_CRUSH_AND_RAISE:
                        ceiling->direction = 1;
                        break;

                    case P_CEIL_LOWER_AND_CRUSH:
                    case P_CEIL_LOWER_TO_FLOOR:
                        finish_ceiling(world, ceiling);
                        break;

                    default:
                        break;
                }
            } else if (result == P_MOVE_CRUSHED) {
                switch (ceiling->type) {
                    case P_CEIL_SILENT_CRUSH_AND_RAISE:
                    case P_CEIL_CRUSH_AND_RAISE:
                    case P_CEIL_LOWER_AND_CRUSH:
                        ceiling->speed = P_CEILSPEED / 8;
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
