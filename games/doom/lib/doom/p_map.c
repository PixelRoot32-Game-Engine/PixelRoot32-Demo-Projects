/*
 * Movement and collision.
 *
 * The reference is linuxdoom-1.10 p_map.c and p_maputl.c. Each function below
 * names the upstream one it reproduces; the two structural differences are
 * stated at the site:
 *
 *   1. there is no blockmap, so lines and things are walked by brute force and
 *      filtered with upstream's own tests (p_map.h explains why the result is
 *      identical);
 *   2. upstream's globals (tmfloorz, tmceilingz, tmdropoffz, spechit[],
 *      bestslidefrac, slidemo) are one p_position_result_t and one
 *      slide_trace_t on the stack.
 *
 * This is C99 only: no allocation, no globals, no stdio, no floating point.
 */
#include "p_map.h"

#include <string.h>

#include "p_setup.h"
#include "p_spec.h"
#include "r_bsp.h"
#include "r_tables.h"

/* --- Small helpers ------------------------------------------------------- */

/* abs() on a fixed_t without the INT32_MIN undefined behaviour. */
static fixed_t abs_fixed(fixed_t value) {
    return value < 0 ? (fixed_t)(-(int64_t)value) : value;
}

static int64_t distance_abs(fixed_t a, fixed_t b) {
    const int64_t difference = (int64_t)a - (int64_t)b;

    return difference < 0 ? -difference : difference;
}

/* P_AproxDistance(): the cheap distance an angle/speed decision needs, not the
 * Euclidean one. */
fixed_t p_aprox_distance(fixed_t dx, fixed_t dy) {
    dx = abs_fixed(dx);
    dy = abs_fixed(dy);
    if (dx < dy) {
        return dx + dy - (dx >> 1);
    }
    return dx + dy - (dy >> 1);
}

/* finecosine[] / finesine[]: r_fine_sine is 5/4 of a turn long so the cosine is
 * an offset, exactly as tables.h's pointer is. The index is already shifted. */
static fixed_t fine_cos(uint32_t index) {
    return r_fine_sine[index + (FINEANGLES / 4)];
}

static fixed_t fine_sin(uint32_t index) {
    return r_fine_sine[index];
}

/* The four slopetypes P_LoadLineDefs derives from dx/dy. */
enum {
    P_SLOPE_HORIZONTAL = 0,
    P_SLOPE_VERTICAL = 1,
    P_SLOPE_POSITIVE = 2,
    P_SLOPE_NEGATIVE = 3
};

static int slope_type(fixed_t dx, fixed_t dy) {
    if (dx == 0) {
        return P_SLOPE_VERTICAL;
    }
    if (dy == 0) {
        return P_SLOPE_HORIZONTAL;
    }
    /* FixedDiv(dy,dx) > 0; the saturation branches keep the sign, so the two
     * sign bits are the whole answer. */
    return ((dx < 0) != (dy < 0)) ? P_SLOPE_NEGATIVE : P_SLOPE_POSITIVE;
}

/* P_PointOnLineSide(): 0 for the front half-plane, 1 for the back. */
static int point_on_line_side(fixed_t x, fixed_t y, const doom_vertex_t* v1,
                              fixed_t dx, fixed_t dy) {
    fixed_t px;
    fixed_t py;
    fixed_t left;
    fixed_t right;

    if (dx == 0) {
        if (x <= v1->x) {
            return dy > 0;
        }
        return dy < 0;
    }
    if (dy == 0) {
        if (y <= v1->y) {
            return dx < 0;
        }
        return dx > 0;
    }

    px = x - v1->x;
    py = y - v1->y;
    left = fixed_mul(dy >> FRACBITS, px);
    right = fixed_mul(py, dx >> FRACBITS);

    if (right < left) {
        return 0; /* front side */
    }
    return 1; /* back side */
}

/* P_PointOnDivlineSide(): the same test against a ray (p_maputl.c). The fast
 * sign-bit branch and the >>8 products are upstream's, including their loss of
 * precision: a trace shorter than 16 units takes the other branch in
 * PIT_AddLineIntercepts, and this one only sees the long case. */
static int point_on_divline_side(fixed_t x, fixed_t y, fixed_t lx, fixed_t ly,
                                 fixed_t ldx, fixed_t ldy) {
    fixed_t dx;
    fixed_t dy;
    fixed_t left;
    fixed_t right;

    if (ldx == 0) {
        if (x <= lx) {
            return ldy > 0;
        }
        return ldy < 0;
    }
    if (ldy == 0) {
        if (y <= ly) {
            return ldx < 0;
        }
        return ldx > 0;
    }

    dx = x - lx;
    dy = y - ly;

    if ((((uint32_t)ldy ^ (uint32_t)ldx ^ (uint32_t)dx ^ (uint32_t)dy) &
         0x80000000u) != 0u) {
        if ((((uint32_t)ldy ^ (uint32_t)dx) & 0x80000000u) != 0u) {
            return 1; /* left is negative */
        }
        return 0;
    }

    left = fixed_mul(ldy >> 8, dx >> 8);
    right = fixed_mul(dy >> 8, ldx >> 8);

    if (right < left) {
        return 0; /* front side */
    }
    return 1; /* back side */
}

/* P_BoxOnLineSide(): the box's two corners against the infinite line. -1 means
 * the box crosses it. */
static int box_on_line_side(const fixed_t box[4], const doom_vertex_t* v1,
                            fixed_t dx, fixed_t dy) {
    int p1 = 0;
    int p2 = 0;

    switch (slope_type(dx, dy)) {
        case P_SLOPE_HORIZONTAL:
            p1 = box[P_BOXTOP] > v1->y;
            p2 = box[P_BOXBOTTOM] > v1->y;
            if (dx < 0) {
                p1 ^= 1;
                p2 ^= 1;
            }
            break;
        case P_SLOPE_VERTICAL:
            p1 = box[P_BOXRIGHT] < v1->x;
            p2 = box[P_BOXLEFT] < v1->x;
            if (dy < 0) {
                p1 ^= 1;
                p2 ^= 1;
            }
            break;
        case P_SLOPE_POSITIVE:
            p1 = point_on_line_side(box[P_BOXLEFT], box[P_BOXTOP], v1, dx, dy);
            p2 = point_on_line_side(box[P_BOXRIGHT], box[P_BOXBOTTOM], v1, dx,
                                    dy);
            break;
        default:
            p1 = point_on_line_side(box[P_BOXRIGHT], box[P_BOXTOP], v1, dx, dy);
            p2 = point_on_line_side(box[P_BOXLEFT], box[P_BOXBOTTOM], v1, dx, dy);
            break;
    }

    if (p1 == p2) {
        return p1;
    }
    return -1;
}

/* P_InterceptVector(): the fraction along v1 where the two rays meet. */
static fixed_t intercept_vector(fixed_t tx, fixed_t ty, fixed_t tdx, fixed_t tdy,
                                fixed_t lx, fixed_t ly, fixed_t ldx,
                                fixed_t ldy) {
    fixed_t den;
    fixed_t num;

    den = fixed_mul(tdy >> 8, ldx) - fixed_mul(tdx >> 8, ldy);
    if (den == 0) {
        return 0;
    }
    num = fixed_mul((tx - lx) >> 8, tdy) + fixed_mul((ly - ty) >> 8, tdx);
    return fixed_div(num, den);
}

/* R_PointToAngle() with the view at the origin, which is all P_HitSlideLine
 * asks for (R_PointToAngle2 (0,0, dx, dy)). */
static angle_t point_to_angle(fixed_t x, fixed_t y) {
    if (x == 0 && y == 0) {
        return 0;
    }

    if (x >= 0) {
        if (y >= 0) {
            if (x > y) {
                return r_tan_to_angle[slope_div((uint32_t)y, (uint32_t)x)];
            }
            return ANG90 - 1 - r_tan_to_angle[slope_div((uint32_t)x, (uint32_t)y)];
        }
        y = -y;
        if (x > y) {
            return (angle_t)(-(int32_t)r_tan_to_angle[slope_div((uint32_t)y,
                                                                 (uint32_t)x)]);
        }
        return ANG270 + r_tan_to_angle[slope_div((uint32_t)x, (uint32_t)y)];
    }

    x = -x;
    if (y >= 0) {
        if (x > y) {
            return ANG180 - 1 - r_tan_to_angle[slope_div((uint32_t)y, (uint32_t)x)];
        }
        return ANG90 + r_tan_to_angle[slope_div((uint32_t)x, (uint32_t)y)];
    }
    y = -y;
    if (x > y) {
        return ANG180 + r_tan_to_angle[slope_div((uint32_t)y, (uint32_t)x)];
    }
    return ANG270 - 1 - r_tan_to_angle[slope_div((uint32_t)x, (uint32_t)y)];
}

/* A line's two vertices and direction. doom_map_open() proved both indices, so
 * no range check is repeated here. */
static void line_geometry(const doom_map_t* map, const doom_line_t* line,
                          doom_vertex_t* v1, doom_vertex_t* v2, fixed_t* dx,
                          fixed_t* dy) {
    doom_map_get_vertex(map, line->v1, v1);
    doom_map_get_vertex(map, line->v2, v2);
    *dx = v2->x - v1->x;
    *dy = v2->y - v1->y;
}

static void line_bounding_box(const doom_vertex_t* v1, const doom_vertex_t* v2,
                              fixed_t box[4]) {
    box[P_BOXTOP] = v1->y > v2->y ? v1->y : v2->y;
    box[P_BOXBOTTOM] = v1->y < v2->y ? v1->y : v2->y;
    box[P_BOXLEFT] = v1->x < v2->x ? v1->x : v2->x;
    box[P_BOXRIGHT] = v1->x > v2->x ? v1->x : v2->x;
}

/* The level's mutable sector array is the truth; a hand-built context without
 * one reads the map, which is what D4.1a's tests do. */
static int sector_heights(const p_mobj_context_t* world, uint16_t index,
                          fixed_t* floorOut, fixed_t* ceilingOut) {
    doom_sector_t record;

    if (world == NULL || world->map == NULL) {
        return 0;
    }
    if (world->sectors != NULL && (uint32_t)index < (uint32_t)world->sectorCount) {
        *floorOut = world->sectors[index].floorHeight;
        *ceilingOut = world->sectors[index].ceilingHeight;
        return 1;
    }
    if ((uint32_t)index >= world->map->lumps[DOOM_MAP_SECTORS].count) {
        return 0;
    }
    doom_map_get_sector(world->map, index, &record);
    *floorOut = record.floorHeight;
    *ceilingOut = record.ceilingHeight;
    return 1;
}

/* The line's current special: the mutable copy when the level has one (a
 * one-shot trigger writes 0 there), the map otherwise. */
static int16_t current_line_special(const p_mobj_context_t* world,
                                    uint16_t lineIndex,
                                    const doom_line_t* line) {
    if (world->lineSpecial != NULL && lineIndex < world->lineCount) {
        return world->lineSpecial[lineIndex];
    }
    return line->special;
}

/* The mobj walk. p_level_first_mobj()/p_level_next_mobj() are the level's own
 * walk; a hand-built context (a unit test) has no level and gets the same list
 * filtered here. */
static const r_mobj_t* mobj_first(const p_mobj_context_t* world) {
    const p_thinker_t* thinker;

    if (world == NULL) {
        return NULL;
    }
    if (world->level != NULL) {
        return p_level_first_mobj(world->level);
    }
    if (world->thinkers == NULL) {
        return NULL;
    }
    for (thinker = p_thinker_first(world->thinkers); thinker != NULL;
         thinker = p_thinker_next(thinker)) {
        if (thinker->kind == (int16_t)P_THINKER_MOBJ) {
            return p_mobj_of_const(thinker);
        }
    }
    return NULL;
}

static const r_mobj_t* mobj_next(const p_mobj_context_t* world,
                                 const r_mobj_t* mobj) {
    const p_thinker_t* thinker;

    if (world == NULL || mobj == NULL) {
        return NULL;
    }
    if (world->level != NULL) {
        return p_level_next_mobj(mobj);
    }
    if (world->thinkers == NULL) {
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

/* --- P_LineOpening ------------------------------------------------------- */

void p_line_opening(const p_mobj_context_t* world, uint16_t lineIndex,
                    p_line_opening_t* out) {
    doom_line_t line;
    uint16_t front;
    uint16_t back;
    fixed_t frontFloor;
    fixed_t frontCeiling;
    fixed_t backFloor;
    fixed_t backCeiling;

    if (out == NULL) {
        return;
    }
    out->openTop = 0;
    out->openBottom = 0;
    out->openRange = 0;
    out->lowFloor = 0;

    if (world == NULL || world->map == NULL) {
        return;
    }
    if (!p_line_decode(world->map, lineIndex, &line)) {
        return;
    }
    /* `if (linedef->sidenum[1] == -1) { openrange = 0; return; }` */
    if (line.sideNum[1] == DOOM_NO_SIDE_INDEX) {
        return;
    }
    if (!p_line_front_sector(world->map, &line, &front) ||
        !p_line_back_sector(world->map, &line, &back)) {
        return;
    }
    if (!sector_heights(world, front, &frontFloor, &frontCeiling) ||
        !sector_heights(world, back, &backFloor, &backCeiling)) {
        return;
    }

    out->openTop = frontCeiling < backCeiling ? frontCeiling : backCeiling;
    if (frontFloor > backFloor) {
        out->openBottom = frontFloor;
        out->lowFloor = backFloor;
    } else {
        out->openBottom = backFloor;
        out->lowFloor = frontFloor;
    }
    out->openRange = out->openTop - out->openBottom;
}

/* --- PIT_CheckLine / PIT_CheckThing -------------------------------------- */

/*
 * PIT_CheckLine: one linedef against the destination box.
 *
 * The two rejections are upstream's: the line's stored bbox (computed here from
 * its vertices, because doom_map reads the record on demand) and
 * P_BoxOnLineSide. A line that passes both is a hit; a one-sided line refuses
 * the move, ML_BLOCKING refuses everything and ML_BLOCKMONSTERS refuses a
 * non-player. The opening then raises the floor and lowers the ceiling.
 */
static int check_line(const p_mobj_context_t* world, const r_mobj_t* thing,
                      const fixed_t box[4], p_position_result_t* out,
                      uint16_t lineIndex) {
    doom_line_t line;
    doom_vertex_t v1;
    doom_vertex_t v2;
    fixed_t lineBox[4];
    fixed_t dx;
    fixed_t dy;
    uint16_t back;
    p_line_opening_t opening;

    if (!p_line_decode(world->map, lineIndex, &line)) {
        return 1;
    }
    line_geometry(world->map, &line, &v1, &v2, &dx, &dy);
    line_bounding_box(&v1, &v2, lineBox);

    if (box[P_BOXRIGHT] <= lineBox[P_BOXLEFT] ||
        box[P_BOXLEFT] >= lineBox[P_BOXRIGHT] ||
        box[P_BOXTOP] <= lineBox[P_BOXBOTTOM] ||
        box[P_BOXBOTTOM] >= lineBox[P_BOXTOP]) {
        return 1;
    }
    if (box_on_line_side(box, &v1, dx, dy) != -1) {
        return 1;
    }

    /* `if (!ld->backsector) return false;` -- a one-sided line is a wall. */
    if (!p_line_back_sector(world->map, &line, &back)) {
        return 0;
    }

    if ((thing->flags & MF_MISSILE) == 0u) {
        if ((line.flags & DOOM_ML_BLOCKING) != 0) {
            return 0; /* explicitly blocking everything */
        }
        if (!thing->player && (line.flags & P_ML_BLOCKMONSTERS) != 0) {
            return 0; /* block monsters only */
        }
    }

    p_line_opening(world, lineIndex, &opening);

    if (opening.openTop < out->ceilingz) {
        out->ceilingz = opening.openTop;
        out->ceilingLine = lineIndex;
        out->hasCeilingLine = 1u;
    }
    if (opening.openBottom > out->floorz) {
        out->floorz = opening.openBottom;
    }
    if (opening.lowFloor < out->dropoffz) {
        out->dropoffz = opening.lowFloor;
    }

    if (current_line_special(world, lineIndex, &line) != 0) {
        if (out->lineCount < P_MAXSPECIALCROSS) {
            out->lines[out->lineCount] = lineIndex;
            out->lineCount = (uint8_t)(out->lineCount + 1u);
        } else {
            /* Upstream writes past spechit[8]. Counting the overflow is the
             * only safe answer; a 32-unit box cannot reach nine lines. */
            out->lineOverflow = 1u;
        }
    }
    return 1;
}

/*
 * PIT_CheckThing: one thing against the destination box.
 *
 * The damage and pickup branches are not here: P_DamageMobj and
 * P_TouchSpecialThing are p_inter.c's, which D4.2d owns. What each branch
 * decides about *blocking* is reproduced, because that is what P_CheckPosition
 * returns and the move depends on:
 *
 *   - a flying skull stops, clears MF_SKULLFLY and returns to its spawn state;
 *   - a missile stops on the thing it would have damaged, and passes over or
 *     under it otherwise;
 *   - a non-solid special is walked over;
 *   - any other MF_SOLID thing blocks.
 */
static int check_thing(const p_mobj_context_t* world, r_mobj_t* thing,
                       const r_mobj_t* other, fixed_t x, fixed_t y) {
    int64_t blockDistance;

    if ((other->flags & (MF_SOLID | MF_SPECIAL | MF_SHOOTABLE)) == 0u) {
        return 1;
    }
    blockDistance = (int64_t)other->radius + (int64_t)thing->radius;
    if (distance_abs(other->x, x) >= blockDistance ||
        distance_abs(other->y, y) >= blockDistance) {
        return 1; /* didn't hit it */
    }
    if (other == thing) {
        return 1; /* don't clip against self */
    }

    /* `if (tmthing->flags & MF_SKULLFLY)` -- upstream damages, then stops. */
    if ((thing->flags & MF_SKULLFLY) != 0u) {
        thing->flags &= ~MF_SKULLFLY;
        thing->momx = 0;
        thing->momy = 0;
        thing->momz = 0;
        (void)p_set_mobj_state((p_mobj_context_t*)world, thing,
                               (statenum_t)mobjinfo[thing->type].spawnstate, NULL);
        return 0; /* stop moving */
    }

    if ((thing->flags & MF_MISSILE) != 0u) {
        if (thing->z > other->z + other->height) {
            return 1; /* overhead */
        }
        if (thing->z + thing->height < other->z) {
            return 1; /* underneath */
        }
        if (thing->target != NULL &&
            (thing->target->type == other->type ||
             (thing->target->type == MT_KNIGHT && other->type == MT_BRUISER) ||
             (thing->target->type == MT_BRUISER && other->type == MT_KNIGHT))) {
            if (other == thing->target) {
                return 1;
            }
            if (other->type != MT_PLAYER) {
                return 0; /* explode, but do no damage */
            }
        }
        if ((other->flags & MF_SHOOTABLE) == 0u) {
            return (other->flags & MF_SOLID) == 0u;
        }
        /* `P_DamageMobj (thing, tmthing, tmthing->target, damage)` is D4.2d. */
        return 0; /* don't traverse any more */
    }

    if ((other->flags & MF_SPECIAL) != 0u) {
        /* `P_TouchSpecialThing()` is D4.2d/D5; only the blocking answer is
         * this layer's. */
        return (other->flags & MF_SOLID) == 0u;
    }

    return (other->flags & MF_SOLID) == 0u;
}

/* --- P_CheckPosition ----------------------------------------------------- */

int p_check_position(p_mobj_context_t* world, r_mobj_t* mobj, fixed_t x,
                     fixed_t y, p_position_result_t* out) {
    fixed_t box[4];
    uint16_t subsector = 0;
    uint16_t sector;
    doom_subsector_t subsectorRecord;
    fixed_t floor;
    fixed_t ceiling;
    uint32_t lineCount;
    uint32_t i;
    const r_mobj_t* other;

    if (world == NULL || mobj == NULL || out == NULL || world->map == NULL) {
        return 0;
    }
    memset(out, 0, sizeof *out);
    out->ceilingLine = P_NO_LINE;

    if (world->moveStats != NULL) {
        world->moveStats->checks += 1u;
    }

    box[P_BOXTOP] = y + mobj->radius;
    box[P_BOXBOTTOM] = y - mobj->radius;
    box[P_BOXRIGHT] = x + mobj->radius;
    box[P_BOXLEFT] = x - mobj->radius;

    /* `newsubsec = R_PointInSubsector (x,y);` and the base heights from it. */
    if (r_bsp_point_in_subsector(world->map, x, y, &subsector) != R_BSP_OK) {
        return 0;
    }
    if ((uint32_t)subsector >= world->map->lumps[DOOM_MAP_SSECTORS].count) {
        return 0;
    }
    doom_map_get_subsector(world->map, subsector, &subsectorRecord);
    sector = doom_map_subsector_sector(world->map, &subsectorRecord);
    if (!sector_heights(world, sector, &floor, &ceiling)) {
        return 0;
    }
    out->subsector = subsector;
    out->sector = sector;
    out->floorz = floor;
    out->dropoffz = floor;
    out->ceilingz = ceiling;

    /* `if ( tmflags & MF_NOCLIP ) return true;` */
    if ((mobj->flags & MF_NOCLIP) != 0u) {
        return 1;
    }

    /* Things first, as upstream: a blocking thing ends the check before any
     * line is examined. The brute-force walk visits every mobj, not only those
     * in a block. */
    for (other = mobj_first(world); other != NULL;
         other = mobj_next(world, other)) {
        if (!check_thing(world, mobj, other, x, y)) {
            return 0;
        }
    }

    lineCount = world->map->lumps[DOOM_MAP_LINEDEFS].count;
    for (i = 0; i < lineCount; ++i) {
        if (!check_line(world, mobj, box, out, (uint16_t)i)) {
            return 0;
        }
    }
    return 1;
}

/* --- P_TryMove ----------------------------------------------------------- */

static int try_move(p_mobj_context_t* world, r_mobj_t* mobj, fixed_t x,
                    fixed_t y, p_position_result_t* outResult) {
    p_position_result_t result;
    fixed_t oldx;
    fixed_t oldy;
    uint32_t i;

    if (world == NULL || mobj == NULL || world->map == NULL) {
        return 0;
    }

    if (!p_check_position(world, mobj, x, y, &result)) {
        if (outResult != NULL) {
            *outResult = result;
        }
        if (world->moveStats != NULL) {
            world->moveStats->blocked += 1u;
        }
        return 0; /* solid wall or thing */
    }

    if ((mobj->flags & MF_NOCLIP) == 0u) {
        if (result.ceilingz - result.floorz < mobj->height) {
            if (world->moveStats != NULL) {
                world->moveStats->blocked += 1u;
            }
            if (outResult != NULL) {
                *outResult = result;
            }
            return 0; /* doesn't fit */
        }
        if ((mobj->flags & MF_TELEPORT) == 0u &&
            result.ceilingz - mobj->z < mobj->height) {
            if (world->moveStats != NULL) {
                world->moveStats->blocked += 1u;
            }
            if (outResult != NULL) {
                *outResult = result;
            }
            return 0; /* mobj must lower itself to fit */
        }
        if ((mobj->flags & MF_TELEPORT) == 0u &&
            result.floorz - mobj->z > P_STEPUP) {
            if (world->moveStats != NULL) {
                world->moveStats->blocked += 1u;
            }
            if (outResult != NULL) {
                *outResult = result;
            }
            return 0; /* too big a step up */
        }
        if ((mobj->flags & (MF_DROPOFF | MF_FLOAT)) == 0u &&
            result.floorz - result.dropoffz > P_STEPUP) {
            if (world->moveStats != NULL) {
                world->moveStats->blocked += 1u;
            }
            if (outResult != NULL) {
                *outResult = result;
            }
            return 0; /* don't stand over a dropoff */
        }
    }

    /* The move is ok: P_SetThingPosition's two fields, then the position. The
     * subsector was already resolved by P_CheckPosition, so the BSP is not
     * walked twice. */
    oldx = mobj->x;
    oldy = mobj->y;
    mobj->floorz = result.floorz;
    mobj->ceilingz = result.ceilingz;
    mobj->subsector = result.subsector;
    mobj->sector = result.sector;
    mobj->x = x;
    mobj->y = y;

    if (outResult != NULL) {
        *outResult = result;
    }

    /* `while (numspechit--)`: the list is walked in reverse. A line whose side
     * changed is crossed; a special that an earlier crossing cleared is
     * skipped, exactly as upstream's second `if (ld->special)` does. */
    if ((mobj->flags & (MF_TELEPORT | MF_NOCLIP)) == 0u) {
        for (i = result.lineCount; i > 0u; --i) {
            const uint16_t lineIndex = result.lines[i - 1u];
            doom_line_t line;
            doom_vertex_t v1;
            doom_vertex_t v2;
            fixed_t dx;
            fixed_t dy;
            int side;
            int oldSide;

            if (!p_line_decode(world->map, lineIndex, &line)) {
                continue;
            }
            if (current_line_special(world, lineIndex, &line) == 0) {
                continue;
            }
            line_geometry(world->map, &line, &v1, &v2, &dx, &dy);
            side = point_on_line_side(mobj->x, mobj->y, &v1, dx, dy);
            oldSide = point_on_line_side(oldx, oldy, &v1, dx, dy);
            if (side != oldSide && world->level != NULL) {
                (void)p_trigger_special_line(world->level, lineIndex,
                                             P_TRIGGER_CROSS,
                                             mobj->player ? 1 : 0);
                if (world->moveStats != NULL) {
                    world->moveStats->crossed += 1u;
                }
            }
        }
    }
    return 1;
}

int p_try_move(p_mobj_context_t* world, r_mobj_t* mobj, fixed_t x, fixed_t y) {
    return try_move(world, mobj, x, y, NULL);
}

/* --- P_SlideMove ---------------------------------------------------------- */

/* The state a slide keeps between its three rays and its retries. Upstream's
 * bestslidefrac/secondslidefrac/slidemo/bestslideline in one struct. The second
 * best line is not kept: upstream assigns it and never reads it. */
typedef struct slide_trace {
    const p_mobj_context_t* world;
    const r_mobj_t* mo;
    fixed_t bestFrac;
    uint16_t bestLine;
} slide_trace_t;

/*
 * True when a line crossed by the trace blocks the slide, as
 * PTR_SlideTraverse decides. A one-sided line blocks only from its front side
 * ("don't hit the back side"); a two-sided line blocks when its opening is too
 * short for the thing, when its ceiling is below the thing's head, or when its
 * floor is more than a 24-unit step up.
 */
static int slide_line_blocks(const slide_trace_t* trace, uint16_t lineIndex) {
    doom_line_t line;
    doom_vertex_t v1;
    doom_vertex_t v2;
    fixed_t dx;
    fixed_t dy;
    p_line_opening_t opening;

    if (!p_line_decode(trace->world->map, lineIndex, &line)) {
        return 0;
    }
    line_geometry(trace->world->map, &line, &v1, &v2, &dx, &dy);

    if ((line.flags & DOOM_ML_TWOSIDED) == 0) {
        if (point_on_line_side(trace->mo->x, trace->mo->y, &v1, dx, dy)) {
            return 0; /* don't hit the back side */
        }
        return 1;
    }

    p_line_opening(trace->world, lineIndex, &opening);

    if (opening.openRange < trace->mo->height) {
        return 1; /* doesn't fit */
    }
    if (opening.openTop - trace->mo->z < trace->mo->height) {
        return 1; /* mobj is too high */
    }
    if (opening.openBottom - trace->mo->z > P_STEPUP) {
        return 1; /* too big a step up */
    }
    return 0;
}

/*
 * PIT_AddLineIntercepts' geometry, over every LINEDEF: does the ray from
 * (x1,y1) by (dx,dy) cross this line, and at what fraction? The short-trace
 * branch is upstream's: under 16 units it compares P_PointOnLineSide at the
 * ray's two endpoints, above it P_PointOnDivlineSide at the line's endpoints,
 * which avoids the precision loss a tiny divline would have.
 *
 * @return 1 and writes @p frac when the line is crossed at 0..FRACUNIT.
 */
static int trace_line(const p_mobj_context_t* world, fixed_t x1, fixed_t y1,
                      fixed_t dx, fixed_t dy, uint16_t lineIndex,
                      fixed_t* fracOut) {
    doom_line_t line;
    doom_vertex_t v1;
    doom_vertex_t v2;
    fixed_t ldx;
    fixed_t ldy;
    int s1;
    int s2;
    fixed_t frac;

    if (!p_line_decode(world->map, lineIndex, &line)) {
        return 0;
    }
    line_geometry(world->map, &line, &v1, &v2, &ldx, &ldy);

    if (dx > 16 * FRACUNIT || dy > 16 * FRACUNIT || dx < -16 * FRACUNIT ||
        dy < -16 * FRACUNIT) {
        s1 = point_on_divline_side(v1.x, v1.y, x1, y1, dx, dy);
        s2 = point_on_divline_side(v2.x, v2.y, x1, y1, dx, dy);
    } else {
        s1 = point_on_line_side(x1, y1, &v1, ldx, ldy);
        s2 = point_on_line_side(x1 + dx, y1 + dy, &v1, ldx, ldy);
    }
    if (s1 == s2) {
        return 0; /* line isn't crossed */
    }

    frac = intercept_vector(x1, y1, dx, dy, v1.x, v1.y, ldx, ldy);
    if (frac < 0) {
        return 0; /* behind source */
    }
    /* P_TraverseIntercepts' `if (dist > maxfrac) return true;` */
    if (frac > FRACUNIT) {
        return 0;
    }
    *fracOut = frac;
    return 1;
}

/* One P_PathTraverse's worth: every line the ray crosses, keeping the nearest
 * blocking one. Non-blocking lines are upstream's `return true` and have no
 * effect on the best. */
static void slide_trace_ray(slide_trace_t* trace, fixed_t x1, fixed_t y1,
                            fixed_t dx, fixed_t dy) {
    const uint32_t lineCount =
        trace->world->map->lumps[DOOM_MAP_LINEDEFS].count;
    uint32_t i;

    for (i = 0; i < lineCount; ++i) {
        fixed_t frac;

        if (!trace_line(trace->world, x1, y1, dx, dy, (uint16_t)i, &frac)) {
            continue;
        }
        if (slide_line_blocks(trace, (uint16_t)i) && frac < trace->bestFrac) {
            trace->bestFrac = frac;
            trace->bestLine = (uint16_t)i;
        }
    }
}

/* P_HitSlideLine(): project the momentum onto the wall's direction. */
static void hit_slide_line(const r_mobj_t* mo, uint16_t lineIndex,
                           const p_mobj_context_t* world, fixed_t* tmxmove,
                           fixed_t* tmymove) {
    doom_line_t line;
    doom_vertex_t v1;
    doom_vertex_t v2;
    fixed_t dx;
    fixed_t dy;
    int side;
    int slope;
    angle_t lineAngle;
    angle_t moveAngle;
    angle_t deltaAngle;
    fixed_t moveLength;
    fixed_t newLength;

    if (!p_line_decode(world->map, lineIndex, &line)) {
        return;
    }
    line_geometry(world->map, &line, &v1, &v2, &dx, &dy);
    slope = slope_type(dx, dy);

    if (slope == P_SLOPE_HORIZONTAL) {
        *tmymove = 0;
        return;
    }
    if (slope == P_SLOPE_VERTICAL) {
        *tmxmove = 0;
        return;
    }

    side = point_on_line_side(mo->x, mo->y, &v1, dx, dy);
    lineAngle = point_to_angle(dx, dy);
    if (side == 1) {
        lineAngle += ANG180;
    }

    moveAngle = point_to_angle(*tmxmove, *tmymove);
    deltaAngle = moveAngle - lineAngle;
    if (deltaAngle > ANG180) {
        deltaAngle += ANG180;
    }

    lineAngle >>= ANGLETOFINESHIFT;
    deltaAngle >>= ANGLETOFINESHIFT;

    moveLength = p_aprox_distance(*tmxmove, *tmymove);
    newLength = fixed_mul(moveLength, fine_cos(deltaAngle));

    *tmxmove = fixed_mul(newLength, fine_cos(lineAngle));
    *tmymove = fixed_mul(newLength, fine_sin(lineAngle));
}

void p_slide_move(p_mobj_context_t* world, r_mobj_t* mobj) {
    fixed_t leadx;
    fixed_t leady;
    fixed_t trailx;
    fixed_t traily;
    fixed_t newx;
    fixed_t newy;
    fixed_t tmxmove;
    fixed_t tmymove;
    int hitcount = 0;
    slide_trace_t trace;

    if (world == NULL || mobj == NULL || world->map == NULL) {
        return;
    }
    if (world->moveStats != NULL) {
        world->moveStats->slides += 1u;
    }

    trace.world = world;
    trace.mo = mobj;

retry:
    if (++hitcount == 3) {
        goto stairstep; /* don't loop forever */
    }

    /* Trace along the three leading corners. The leading corner is the one in
     * the direction of travel, which is what `momx > 0` selects. */
    if (mobj->momx > 0) {
        leadx = mobj->x + mobj->radius;
        trailx = mobj->x - mobj->radius;
    } else {
        leadx = mobj->x - mobj->radius;
        trailx = mobj->x + mobj->radius;
    }
    if (mobj->momy > 0) {
        leady = mobj->y + mobj->radius;
        traily = mobj->y - mobj->radius;
    } else {
        leady = mobj->y - mobj->radius;
        traily = mobj->y + mobj->radius;
    }

    trace.bestFrac = FRACUNIT + 1;
    trace.bestLine = P_NO_LINE;
    slide_trace_ray(&trace, leadx, leady, mobj->momx, mobj->momy);
    slide_trace_ray(&trace, trailx, leady, mobj->momx, mobj->momy);
    slide_trace_ray(&trace, leadx, traily, mobj->momx, mobj->momy);

    /* Move up to the wall. No blocking line on any of the three rays is
     * upstream's "the move must have hit the middle" and takes the stairstep. */
    if (trace.bestFrac == FRACUNIT + 1) {
stairstep:
        if (!p_try_move(world, mobj, mobj->x, mobj->y + mobj->momy)) {
            (void)p_try_move(world, mobj, mobj->x + mobj->momx, mobj->y);
        }
        return;
    }

    /* Fudge a bit so the move does not land on the line. */
    trace.bestFrac -= 0x800;
    if (trace.bestFrac > 0) {
        newx = fixed_mul(mobj->momx, trace.bestFrac);
        newy = fixed_mul(mobj->momy, trace.bestFrac);

        if (!p_try_move(world, mobj, mobj->x + newx, mobj->y + newy)) {
            goto stairstep;
        }
    }

    /* Now continue along the wall: the remainder after the fudge. */
    trace.bestFrac = FRACUNIT - (trace.bestFrac + 0x800);
    if (trace.bestFrac > FRACUNIT) {
        trace.bestFrac = FRACUNIT;
    }
    if (trace.bestFrac <= 0) {
        return;
    }

    tmxmove = fixed_mul(mobj->momx, trace.bestFrac);
    tmymove = fixed_mul(mobj->momy, trace.bestFrac);

    hit_slide_line(mobj, trace.bestLine, world, &tmxmove, &tmymove);

    mobj->momx = tmxmove;
    mobj->momy = tmymove;

    if (!p_try_move(world, mobj, mobj->x + tmxmove, mobj->y + tmymove)) {
        goto retry;
    }
}

/* --- P_ThingHeightClip --------------------------------------------------- */

int p_thing_height_clip(p_mobj_context_t* world, r_mobj_t* mobj) {
    p_position_result_t result;
    int onFloor;

    if (world == NULL || mobj == NULL || world->map == NULL) {
        return 1;
    }

    onFloor = (mobj->z == mobj->floorz);

    /* Upstream ignores P_CheckPosition's boolean and uses the heights it found;
     * a "blocked" answer still fills them. */
    (void)p_check_position(world, mobj, mobj->x, mobj->y, &result);

    mobj->floorz = result.floorz;
    mobj->ceilingz = result.ceilingz;

    if (onFloor) {
        /* walking things rise and fall with the floor */
        mobj->z = mobj->floorz;
    } else if (mobj->z + mobj->height > mobj->ceilingz) {
        /* don't adjust a floating thing unless forced to */
        mobj->z = mobj->ceilingz - mobj->height;
    }

    return (mobj->ceilingz - mobj->floorz >= mobj->height) ? 1 : 0;
}

/* --- P_ChangeSector ------------------------------------------------------ */

/*
 * The moving sector's block box, derived the way P_GroupLines derives it: the
 * bounding box of every linedef that touches the sector, widened by MAXRADIUS
 * because a thing's body can reach into the box from a neighbouring block.
 *
 * Without a blockmap this is the only way to keep P_ChangeSector off every mobj
 * on every plane step. A thing not in the box cannot touch a line of the moving
 * sector, so P_ThingHeightClip would read the same heights it already has.
 */
static int sector_bounds(const p_mobj_context_t* world, uint16_t sectorIndex,
                         fixed_t box[4]) {
    const uint32_t lineCount =
        world->map->lumps[DOOM_MAP_LINEDEFS].count;
    fixed_t top = INT32_MIN;
    fixed_t bottom = INT32_MAX;
    fixed_t left = INT32_MAX;
    fixed_t right = INT32_MIN;
    uint32_t i;
    int found = 0;

    for (i = 0; i < lineCount; ++i) {
        doom_line_t line;
        doom_vertex_t v1;
        doom_vertex_t v2;
        fixed_t dx;
        fixed_t dy;
        uint16_t side;
        int touches = 0;

        doom_map_get_line(world->map, (uint16_t)i, &line);
        if (p_line_front_sector(world->map, &line, &side) &&
            side == sectorIndex) {
            touches = 1;
        }
        if (!touches && p_line_back_sector(world->map, &line, &side) &&
            side == sectorIndex) {
            touches = 1;
        }
        if (!touches) {
            continue;
        }

        line_geometry(world->map, &line, &v1, &v2, &dx, &dy);
        if (v1.y > top) {
            top = v1.y;
        }
        if (v2.y > top) {
            top = v2.y;
        }
        if (v1.y < bottom) {
            bottom = v1.y;
        }
        if (v2.y < bottom) {
            bottom = v2.y;
        }
        if (v1.x < left) {
            left = v1.x;
        }
        if (v2.x < left) {
            left = v2.x;
        }
        if (v1.x > right) {
            right = v1.x;
        }
        if (v2.x > right) {
            right = v2.x;
        }
        found = 1;
    }

    if (!found) {
        return 0;
    }
    box[P_BOXTOP] = top + P_MAXRADIUS;
    box[P_BOXBOTTOM] = bottom - P_MAXRADIUS;
    box[P_BOXLEFT] = left - P_MAXRADIUS;
    box[P_BOXRIGHT] = right + P_MAXRADIUS;
    return 1;
}

/*
 * PIT_ChangeSector: one thing against the changed sector.
 *
 * The damage half of the last branch is P_DamageMobj's (p_inter.c, D4.2d). The
 * counter is the honest substitute: every thing upstream would have damaged is
 * one unsupportedCrush.
 */
static void change_sector_thing(p_mobj_context_t* world, r_mobj_t* mobj,
                                int* nofit) {
    if (p_thing_height_clip(world, mobj)) {
        return; /* keep checking */
    }

    /* Crunch bodies to giblets. */
    if (mobj->health <= 0) {
        (void)p_set_mobj_state(world, mobj, S_GIBS, NULL);
        mobj->flags &= ~MF_SOLID;
        mobj->height = 0;
        mobj->radius = 0;
        return;
    }

    /* Crunch dropped items. */
    if ((mobj->flags & MF_DROPPED) != 0u) {
        (void)p_remove_mobj(world, mobj);
        return;
    }

    /* Assume it is bloody gibs or something. */
    if ((mobj->flags & MF_SHOOTABLE) == 0u) {
        return;
    }

    *nofit = 1;

    /* `if (crushchange && !(leveltime&3)) P_DamageMobj (thing,NULL,NULL,10);`
     * -- P_DamageMobj does not exist yet (D4.2d), and this entry point does not
     * receive `crushchange`, so the counter is a superset of the damage calls:
     * it counts the thing, not the tic the damage would have landed on. */
    if (world->specStats != NULL) {
        world->specStats->unsupportedCrush += 1u;
    }
}

int p_change_sector(p_mobj_context_t* world, uint16_t sector) {
    const r_mobj_t* mobj;
    fixed_t box[4];
    int nofit = 0;
    int haveBox;

    if (world == NULL || world->map == NULL) {
        return 0;
    }

    if (world->specStats != NULL) {
        world->specStats->changeSectorCalls += 1u;
    }

    haveBox = sector_bounds(world, sector, box);

    for (mobj = mobj_first(world); mobj != NULL;
         mobj = mobj_next(world, mobj)) {
        if (haveBox &&
            (mobj->x < box[P_BOXLEFT] || mobj->x > box[P_BOXRIGHT] ||
             mobj->y < box[P_BOXBOTTOM] || mobj->y > box[P_BOXTOP])) {
            continue;
        }
        /* p_level_first_mobj()/p_level_next_mobj() hand out a const view; the
         * mobjs in the level's zone are mutable, and the height clip is exactly
         * what P_ChangeSector does to them. */
        change_sector_thing(world, (r_mobj_t*)mobj, &nofit);
    }

    if (!nofit && world->specStats != NULL) {
        world->specStats->changeSectorClear += 1u;
    }
    return nofit;
}

/* --- P_UseLines ---------------------------------------------------------- */

void p_use_lines(p_mobj_context_t* world, r_mobj_t* mobj) {
    fixed_t x1;
    fixed_t y1;
    fixed_t x2;
    fixed_t y2;
    fixed_t dx;
    fixed_t dy;
    fixed_t lastFrac;
    uint32_t lineCount;
    uint32_t i;
    uint32_t angle;

    if (world == NULL || mobj == NULL || world->map == NULL) {
        return;
    }

    angle = mobj->angle >> ANGLETOFINESHIFT;
    x1 = mobj->x;
    y1 = mobj->y;
    /* `x2 = x1 + (USERANGE>>FRACBITS)*finecosine[angle]`: 64 map units, not a
     * FixedMul. */
    x2 = x1 + (P_USERANGE >> FRACBITS) * fine_cos(angle);
    y2 = y1 + (P_USERANGE >> FRACBITS) * fine_sin(angle);
    dx = x2 - x1;
    dy = y2 - y1;

    lineCount = world->map->lumps[DOOM_MAP_LINEDEFS].count;

    /* P_TraverseIntercepts in ascending frac order, selected by repeated minimum
     * instead of an intercept array. Ties at one fraction are processed in
     * LINEDEF order up to the first one, which is the measure-zero divergence
     * p_map.h documents. */
    lastFrac = -1;
    for (;;) {
        fixed_t bestFrac = FRACUNIT + 1;
        uint16_t bestLine = P_NO_LINE;
        int have = 0;

        for (i = 0; i < lineCount; ++i) {
            fixed_t frac;

            if (!trace_line(world, x1, y1, dx, dy, (uint16_t)i, &frac)) {
                continue;
            }
            if (frac <= lastFrac) {
                continue; /* already visited */
            }
            if (frac < bestFrac) {
                bestFrac = frac;
                bestLine = (uint16_t)i;
                have = 1;
            }
        }
        if (!have) {
            return;
        }
        lastFrac = bestFrac;

        {
            doom_line_t line;

            if (!p_line_decode(world->map, bestLine, &line)) {
                continue;
            }
            if (current_line_special(world, bestLine, &line) == 0) {
                p_line_opening_t opening;

                /* `if (openrange <= 0) return false;` -- can't use through a
                 * wall; a non-special line with an opening keeps checking. */
                p_line_opening(world, bestLine, &opening);
                if (opening.openRange <= 0) {
                    return;
                }
                continue;
            }

            /* Upstream computes the user's side and passes it to
             * P_UseSpecialLine; this port's dispatch has no side argument, so
             * the crossing itself is the decision. */
            if (world->level != NULL) {
                (void)p_trigger_special_line(world->level, bestLine,
                                             P_TRIGGER_USE, 1);
            }
            /* Can't use more than one special line in a row. */
            return;
        }
    }
}
