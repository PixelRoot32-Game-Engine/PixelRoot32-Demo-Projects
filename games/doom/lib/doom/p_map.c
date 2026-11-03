/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

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

#include "p_inter.h"
#include "p_setup.h"
#include "p_sight.h"
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

/* Upstream's `leveltime` is a global; here it is p_level_t::leveltime. A
 * hand-built context (a unit test) has no level and answers 0, which is the tic
 * PIT_ChangeSector's crush gate reads before any world has ticked. */
static int32_t level_time(const p_mobj_context_t* world) {
    return world->level != NULL ? world->level->leveltime : 0;
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

/* P_InterceptVector(): the fraction along the trace where it meets the line.
 *
 * Upstream's signature is `P_InterceptVector (divline_t* v2, divline_t* v1)` and
 * it is called as `P_InterceptVector (&trace, &dl)`, so `v2` is the trace and
 * `v1` the line. Every direction factor in the numerator is the line's:
 *
 *   den = FixedMul (v1->dy>>8, v2->dx) - FixedMul (v1->dx>>8, v2->dy);
 *   num = FixedMul ((v1->x - v2->x)>>8, v1->dy)
 *       + FixedMul ((v2->y - v1->y)>>8, v1->dx);
 */
static fixed_t intercept_vector(fixed_t tx, fixed_t ty, fixed_t tdx, fixed_t tdy,
                                fixed_t lx, fixed_t ly, fixed_t ldx,
                                fixed_t ldy) {
    fixed_t den;
    fixed_t num;

    den = fixed_mul(ldy >> 8, tdx) - fixed_mul(ldx >> 8, tdy);
    if (den == 0) {
        return 0;
    }
    num = fixed_mul((lx - tx) >> 8, ldy) + fixed_mul((ty - ly) >> 8, ldx);
    return fixed_div(num, den);
}

/* R_PointToAngle() with the view at the origin: what P_HitSlideLine asks for as
 * R_PointToAngle2 (0,0, dx, dy), and what P_DamageMobj's thrust asks for from
 * the inflictor's position. Because both callers need the same rule, it is
 * public (p_map.h) rather than repeated per file. */
angle_t p_point_to_angle(fixed_t x, fixed_t y) {
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
 * one reads the map, which is what the hand-built unit tests do. */
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
 * The three branches upstream wires into this test are all here now:
 *
 *   - a flying skull stops, clears MF_SKULLFLY and returns to its spawn state;
 *   - a missile stops on the thing it would have damaged, passes over or under
 *     it otherwise, and otherwise rolls `((P_Random()%8)+1) *
 *     tmthing->info->damage` and hands it to P_DamageMobj -- the draw is part of
 *     the sequence, so it happens exactly here;
 *   - a non-solid MF_SPECIAL is walked over, and P_TouchSpecialThing gets the
 *     chance to pick it up.
 *
 * A thing already marked for removal is skipped. Upstream's P_RemoveMobj unlinks
 * the item from the blockmap, so the iterator never offers it again; this walk
 * reads the thinker list, where the node survives until the next
 * p_run_thinkers(), so the same exclusion has to be written down.
 */
static int check_thing(const p_mobj_context_t* world, r_mobj_t* thing,
                       const r_mobj_t* other, fixed_t x, fixed_t y) {
    p_mobj_context_t* context = (p_mobj_context_t*)world;
    int64_t blockDistance;

    if (other->thinker.removed != 0u) {
        return 1; /* already gone: nothing to hit, nothing to pick up */
    }
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

    /* `if (tmthing->flags & MF_SKULLFLY)` -- upstream stops the skull here and
     * lets P_DamageMobj's SKULLFLY branch (a no-op once the flag is gone) be
     * skipped; the port keeps the same stop. */
    if ((thing->flags & MF_SKULLFLY) != 0u) {
        thing->flags &= ~MF_SKULLFLY;
        thing->momx = 0;
        thing->momy = 0;
        thing->momz = 0;
        (void)p_set_mobj_state(context, thing,
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
        /* `damage = ((P_Random()%8)+1)*tmthing->info->damage;` then
         * `P_DamageMobj (thing, tmthing, tmthing->target, damage);` -- the draw
         * is part of the sequence, so it is taken before the call. */
        p_damage_mobj(context, (r_mobj_t*)other, thing, thing->target,
                      ((int)p_random(context->random) % 8 + 1) *
                          (int)mobjinfo[thing->type].damage);
        return 0; /* don't traverse any more */
    }

    if ((other->flags & MF_SPECIAL) != 0u) {
        /* `P_TouchSpecialThing (thing, tmthing)` -- the candidate first. */
        p_touch_special_thing(context, (r_mobj_t*)other, thing);
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

/* --- P_TeleportMove ------------------------------------------------------ */

int p_teleport_move(p_mobj_context_t* world, r_mobj_t* mobj, fixed_t x,
                    fixed_t y) {
    uint16_t subsector = 0;
    uint16_t sector;
    doom_subsector_t record;
    fixed_t floor;
    fixed_t ceiling;
    const r_mobj_t* other;

    if (world == NULL || mobj == NULL || world->map == NULL) {
        return 0;
    }

    /* `newsubsec = R_PointInSubsector (x,y);` and the base heights from it. No
     * line is examined: P_TeleportMove trusts the map's teleport destination. */
    if (r_bsp_point_in_subsector(world->map, x, y, &subsector) != R_BSP_OK) {
        return 0;
    }
    if ((uint32_t)subsector >= world->map->lumps[DOOM_MAP_SSECTORS].count) {
        return 0;
    }
    doom_map_get_subsector(world->map, subsector, &record);
    sector = doom_map_subsector_sector(world->map, &record);
    if (!sector_heights(world, sector, &floor, &ceiling)) {
        return 0;
    }

    /* PIT_StompThing over every thing in the world, in list order. A match that
     * the teleporter's own class refuses ends the move before it is written. */
    for (other = mobj_first(world); other != NULL;
         other = mobj_next(world, other)) {
        fixed_t blockdist;

        if ((other->flags & MF_SHOOTABLE) == 0u) {
            continue;
        }
        blockdist = other->radius + mobj->radius;
        if (distance_abs(other->x, x) >= (int64_t)blockdist ||
            distance_abs(other->y, y) >= (int64_t)blockdist) {
            continue;
        }
        if (other == mobj) {
            continue; /* don't clip against self */
        }
        /* `if ( !tmthing->player && gamemap != 30) return false;` -- monsters
         * don't stomp things except on the boss level. DOOM1's episode 1 is
         * gamemap 1..9, so a non-player teleporter always refuses here. */
        if (mobj->player == 0u) {
            return 0;
        }
        p_damage_mobj(world, (r_mobj_t*)other, mobj, mobj, 10000);
    }

    mobj->floorz = floor;
    mobj->ceilingz = ceiling;
    mobj->subsector = subsector;
    mobj->sector = sector;
    mobj->x = x;
    mobj->y = y;
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
                /* P_CrossSpecialLine (ld-lines, oldside, thing): the side is the
                 * one the thing came from, which is what EV_Teleport's
                 * `if (side == 1) return 0;` reads. */
                (void)p_trigger_special_line(world->level, lineIndex,
                                             P_TRIGGER_CROSS, mobj, oldSide);
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
    lineAngle = p_point_to_angle(dx, dy);
    if (side == 1) {
        lineAngle += ANG180;
    }

    moveAngle = p_point_to_angle(*tmxmove, *tmymove);
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
 * Upstream's body, in upstream's order, and the damage half is the reason this
 * task exists:
 *
 *   - a thing that fits is left alone;
 *   - a corpse is crunched to S_GIBS and loses its box;
 *   - a dropped item is removed;
 *   - anything else does not fit: the sector reports it, and if the move may
 *     crush, ten points land every fourth tic and a blood mobj is sprayed.
 *
 * The blood spray's four draws (`(P_Random()-P_Random())<<12` twice) are part of
 * the sequence and happen after the damage, exactly as upstream. The mobj is
 * spawned at the victim's mid-height, which after a fatal crush is the *halved*
 * height P_KillMobj wrote -- upstream measures the same value because it reads
 * `thing->height` after the damage call.
 */
static void change_sector_thing(p_mobj_context_t* world, r_mobj_t* mobj,
                                int crushChange, int* nofit) {
    r_mobj_t* blood;

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

    /* `if (crushchange && !(leveltime&3)) { P_DamageMobj (thing,NULL,NULL,10);
     * P_SpawnMobj (..., MT_BLOOD) with a random momentum; }` */
    if (crushChange && (level_time(world) & 3) == 0) {
        p_damage_mobj(world, mobj, NULL, NULL, P_CRUSH_DAMAGE);
        if (world->specStats != NULL) {
            world->specStats->crushedThings += 1u;
        }

        /* spray blood in a random direction */
        blood = p_spawn_mobj(world, mobj->x, mobj->y,
                             mobj->z + mobj->height / 2, MT_BLOOD, NULL);
        if (blood != NULL) {
            blood->momx =
                (p_random(world->random) - p_random(world->random)) << 12;
            blood->momy =
                (p_random(world->random) - p_random(world->random)) << 12;
        }
    }
}

int p_change_sector(p_mobj_context_t* world, uint16_t sector, int crushChange) {
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
        if (mobj->thinker.removed != 0u) {
            /* Already gone: upstream unlinked it from the blockmap, this walk
             * still sees the thinker node. */
            continue;
        }
        if (haveBox &&
            (mobj->x < box[P_BOXLEFT] || mobj->x > box[P_BOXRIGHT] ||
             mobj->y < box[P_BOXBOTTOM] || mobj->y > box[P_BOXTOP])) {
            continue;
        }
        /* p_level_first_mobj()/p_level_next_mobj() hand out a const view; the
         * mobjs in the level's zone are mutable, and the height clip is exactly
         * what P_ChangeSector does to them. */
        change_sector_thing(world, (r_mobj_t*)mobj, crushChange, &nofit);
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

            /* Upstream computes the user's side with
             * `P_PointOnLineSide (usething->x, usething->y, in->d.line)` and
             * passes it to P_UseSpecialLine, whose prologue refuses a back-side
             * use (124 is the unused exception). `useLineHits` is PTR_UseTraverse's
             * answer -- a special line was reached -- and is recorded apart from
             * `triggers` because an EV that declines still means the ray got
             * there. */
            if (world->level != NULL) {
                doom_vertex_t v1;
                doom_vertex_t v2;
                fixed_t lineDx;
                fixed_t lineDy;

                if (world->specStats != NULL) {
                    world->specStats->useLineHits += 1u;
                }
                line_geometry(world->map, &line, &v1, &v2, &lineDx, &lineDy);
                (void)p_trigger_special_line(
                    world->level, bestLine, P_TRIGGER_USE, mobj,
                    point_on_line_side(mobj->x, mobj->y, &v1, lineDx, lineDy));
            }
            /* Can't use more than one special line in a row. */
            return;
        }
    }
}

/* --- Shooting and aiming ------------------------------------------------- */

/*
 * P_PathTraverse, PIT_AddLineIntercepts, PIT_AddThingIntercepts, P_TraverseIntercepts,
 * PTR_AimTraverse, PTR_ShootTraverse, P_AimLineAttack, P_LineAttack and
 * P_ShootSpecialLine (p_map.c and p_maputl.c).
 *
 * THE INTERCEPT LIST IS THE ONE ARRAY THIS FILE PUTS ON THE STACK
 *
 * Upstream collects the ray's crossings into `intercepts[MAXINTERCEPTS]` and then
 * walks them in ascending fraction. The port does the same, because the
 * alternative -- re-scanning every line for each successive minimum, which is what
 * p_use_lines does -- would decode the map 475 times per intercept of a 2,048-unit
 * bullet, and a chaingun fires two bullets a tic. One array of P_MAXINTERCEPTS
 * entries is 768 B on the 32-bit target and a shot is never re-entered.
 *
 * The candidate set is the same superset p_map.h documents for movement: every
 * LINEDEF and every mobj is offered, filtered by upstream's own tests
 * (P_PointOnDivlineSide at the line's endpoints or the trace's, P_InterceptVector
 * for the fraction, the corner-to-corner cross-section for a thing). Two
 * differences follow, and both are stated rather than hidden:
 *
 *   1. upstream only *offers* a thing when the ray steps through the block the
 *      thing's origin is registered in, so a graze with a thing whose origin block
 *      the ray never enters is invisible to it and visible here. A shot aimed at a
 *      target whose origin is on the ray -- every shot that matters -- resolves
 *      identically;
 *   2. ties at exactly equal fractions are broken by collection order, which is
 *      LINEDEF-then-thinker order here and block-walk order upstream. That is the
 *      same measure-zero divergence P_SlideMove and P_UseLines already document.
 *
 * P_PathTraverse also nudges its start by one unit when the start sits exactly on
 * a blockmap grid line (`((x1-bmaporgx)&(MAPBLOCKSIZE-1)) == 0`). There is no
 * blockmap here and therefore no grid to sit on, and the nudge changed the answer
 * only for a shooter exactly on that grid; it is not reproduced, and both sides of
 * the gate agree on that.
 */

/* r_defs.h packs a texture or flat name as its eight name bytes, most significant
 * first. DOOM's sky flat is "F_SKY1" in every episode of DOOM and DOOM II, which
 * is what upstream's `skyflatnum` global holds (r_sky.c:
 * `skyflatnum = R_FlatNumForName (SKYFLATNAME);`). The port has no such global --
 * the renderer is handed the level's sky flat as a parameter (r_plane.h) -- so the
 * one comparison PTR_ShootTraverse makes names the constant directly. */
#define P_SKY_FLAT_NAME ((r_name_t)0x465F534B59310000ull)

/* One crossing: a fraction along the trace and either a linedef or a thing. */
typedef struct attack_intercept {
    fixed_t frac;
    const r_mobj_t* thing; /* NULL for a linedef */
    uint16_t line;         /* the linedef index when `thing` is NULL */
} attack_intercept_t;

/* The state a trace runs with: upstream's shootthing, shootz, la_damage,
 * attackrange, aimslope, linetarget, topslope, bottomslope, trace and the
 * intercept array, in one struct on the stack. */
typedef struct attack_trace {
    p_mobj_context_t* world;
    const r_mobj_t* shooter;
    fixed_t shootz;
    fixed_t range;
    fixed_t aimSlope;
    fixed_t topSlope;
    fixed_t bottomSlope;
    fixed_t x1;
    fixed_t y1;
    fixed_t dx;
    fixed_t dy;
    r_mobj_t* lineTarget;
    int damage;
    uint16_t count;
    uint8_t overflow;
    attack_intercept_t intercepts[P_MAXINTERCEPTS];
} attack_trace_t;

static void stat_attack(p_mobj_context_t* world,
                        void (*bump)(p_attack_stats_t*)) {
    if (world != NULL && world->attackStats != NULL) {
        bump(world->attackStats);
    }
}

/* Small named increments, so the call sites read as the rule they count. */
static void count_trace(p_attack_stats_t* stats) {
    stats->traces += 1u;
}

static void count_intercept(p_attack_stats_t* stats) {
    stats->intercepts += 1u;
}

static void count_overflow(p_attack_stats_t* stats) {
    stats->interceptOverflow += 1u;
}

static void count_sky(p_attack_stats_t* stats) {
    stats->skyHits += 1u;
}

static void count_special(p_attack_stats_t* stats) {
    stats->specialsShot += 1u;
}

static void count_zero_distance(p_attack_stats_t* stats) {
    stats->zeroDistance += 1u;
}

static int add_intercept(attack_trace_t* trace) {
    if (trace->count >= (uint16_t)P_MAXINTERCEPTS) {
        trace->overflow = 1u;
        stat_attack(trace->world, count_overflow);
        return 0;
    }
    trace->count = (uint16_t)(trace->count + 1u);
    stat_attack(trace->world, count_intercept);
    return 1;
}

/* PIT_AddLineIntercepts' geometry. The fraction comes from the shared
 * intercept_vector() above, so a shot and a slide agree on where a ray meets a
 * line. The short-trace branch is upstream's: under 16 units it compares
 * P_PointOnLineSide at the trace's two endpoints, above it P_PointOnDivlineSide
 * at the line's. */
static int attack_line_frac(const p_mobj_context_t* world, fixed_t x1, fixed_t y1,
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
    if (frac > FRACUNIT) {
        return 0; /* beyond the trace */
    }
    *fracOut = frac;
    return 1;
}

/*
 * PIT_AddLineIntercepts, over every LINEDEF.
 */
static void collect_lines(attack_trace_t* trace) {
    const uint32_t lineCount = trace->world->map->lumps[DOOM_MAP_LINEDEFS].count;
    uint32_t i;

    for (i = 0; i < lineCount; ++i) {
        fixed_t frac;

        if (trace->overflow != 0u) {
            return;
        }
        if (!attack_line_frac(trace->world, trace->x1, trace->y1, trace->dx,
                              trace->dy, (uint16_t)i, &frac)) {
            continue;
        }
        if (add_intercept(trace)) {
            attack_intercept_t* entry = &trace->intercepts[trace->count - 1u];

            entry->frac = frac;
            entry->thing = NULL;
            entry->line = (uint16_t)i;
        }
    }
}

/*
 * PIT_AddThingIntercepts, over every mobj in thinker order.
 *
 * The cross-section is the thing's bounding box's diagonal, chosen by the sign of
 * `trace.dx ^ trace.dy` exactly as upstream chooses it, and the intercept is where
 * the ray crosses that diagonal. A thing already marked for removal is skipped for
 * the same reason PIT_CheckThing skips it: upstream unlinked it from the blockmap.
 */
static void collect_things(attack_trace_t* trace) {
    const r_mobj_t* mobj;
    const int32_t xored = (int32_t)((uint32_t)trace->dx ^ (uint32_t)trace->dy);
    const int tracePositive = xored > 0;

    for (mobj = mobj_first(trace->world); mobj != NULL;
         mobj = mobj_next(trace->world, mobj)) {
        fixed_t x1;
        fixed_t y1;
        fixed_t x2;
        fixed_t y2;
        int s1;
        int s2;
        fixed_t frac;

        if (trace->overflow != 0u) {
            return;
        }
        if (mobj->thinker.removed != 0u) {
            continue;
        }

        if (tracePositive) {
            x1 = mobj->x - mobj->radius;
            y1 = mobj->y + mobj->radius;
            x2 = mobj->x + mobj->radius;
            y2 = mobj->y - mobj->radius;
        } else {
            x1 = mobj->x - mobj->radius;
            y1 = mobj->y - mobj->radius;
            x2 = mobj->x + mobj->radius;
            y2 = mobj->y + mobj->radius;
        }

        s1 = point_on_divline_side(x1, y1, trace->x1, trace->y1, trace->dx,
                                   trace->dy);
        s2 = point_on_divline_side(x2, y2, trace->x1, trace->y1, trace->dx,
                                   trace->dy);
        if (s1 == s2) {
            continue; /* line isn't crossed */
        }

        frac = intercept_vector(trace->x1, trace->y1, trace->dx, trace->dy,
                                x1, y1, x2 - x1, y2 - y1);
        if (frac < 0) {
            continue; /* behind source */
        }
        if (frac > FRACUNIT) {
            continue; /* beyond the trace */
        }
        if (add_intercept(trace)) {
            attack_intercept_t* entry = &trace->intercepts[trace->count - 1u];

            entry->frac = frac;
            entry->thing = mobj;
            entry->line = P_NO_LINE;
        }
    }
}

/*
 * The two sector-height comparisons PTR_AimTraverse and PTR_ShootTraverse both
 * make: `frontsector->floorheight != backsector->floorheight` and the ceiling's
 * pair. A side that does not exist (a one-sided line) reads as equal, which is
 * what upstream's P_LineOpening would have left stale; neither traverser reaches a
 * one-sided line's heights because both test ML_TWOSIDED first.
 */
static int two_sided_heights(const p_mobj_context_t* world, const doom_line_t* line,
                             fixed_t* frontFloor, fixed_t* frontCeiling,
                             fixed_t* backFloor, fixed_t* backCeiling) {
    uint16_t front;
    uint16_t back;

    if (!p_line_front_sector(world->map, line, &front) ||
        !p_line_back_sector(world->map, line, &back)) {
        return 0;
    }
    if (!sector_heights(world, front, frontFloor, frontCeiling)) {
        return 0;
    }
    if (!sector_heights(world, back, backFloor, backCeiling)) {
        return 0;
    }
    return 1;
}

/* A sector's ceiling flat, packed, or R_NAME_NONE when the index is not one. The
 * sky hack is the only reader: upstream compares `ceilingpic` with its
 * `skyflatnum` global. */
static r_name_t sector_ceiling_pic(const p_mobj_context_t* world,
                                   uint16_t sector) {
    doom_sector_t record;

    if (world == NULL || world->map == NULL) {
        return R_NAME_NONE;
    }
    if ((uint32_t)sector >= world->map->lumps[DOOM_MAP_SECTORS].count) {
        return R_NAME_NONE;
    }
    doom_map_get_sector(world->map, sector, &record);
    return record.ceilingPic;
}

/* `FixedDiv (a, dist)` with the one guard upstream does not have. A zero-length
 * distance cannot be turned into a slope; upstream divides by zero there and this
 * port skips the comparison and counts it. */
static int divide_slope(attack_trace_t* trace, fixed_t numerator, fixed_t dist,
                        fixed_t* out) {
    if (dist == 0) {
        stat_attack(trace->world, count_zero_distance);
        return 0;
    }
    *out = fixed_div(numerator, dist);
    return 1;
}

/*
 * PTR_AimTraverse: a line constrains the aim band, a thing inside the band ends
 * the trace and becomes `lineTarget`.
 *
 * The band starts at +-100/160 of a unit of slope and only narrows: a two-sided
 * line whose floors differ raises `bottomSlope`, one whose ceilings differ lowers
 * `topSlope`, and `topSlope <= bottomSlope` ends the trace. A thing is aimed at
 * when its top is above the floor bound and its bottom below the ceiling bound;
 * the answer is the band's midpoint, clamped into the band. That clamping is what
 * makes a bullet aimed at the *middle* of whatever it can see.
 */
static int aim_traverse_line(attack_trace_t* trace, fixed_t frac,
                             uint16_t lineIndex) {
    doom_line_t line;
    p_line_opening_t opening;
    fixed_t frontFloor;
    fixed_t frontCeiling;
    fixed_t backFloor;
    fixed_t backCeiling;
    fixed_t dist;
    fixed_t slope;

    if (!p_line_decode(trace->world->map, lineIndex, &line)) {
        return 1;
    }
    /* `if (!(li->flags & ML_TWOSIDED)) return false; // stop` */
    if ((line.flags & DOOM_ML_TWOSIDED) == 0) {
        return 0;
    }

    /* "Crosses a two sided line. A two sided line will restrict the possible
     * target ranges." */
    p_line_opening(trace->world, lineIndex, &opening);

    /* `if (openbottom >= opentop) return false; // stop` */
    if (opening.openBottom >= opening.openTop) {
        return 0;
    }
    if (!two_sided_heights(trace->world, &line, &frontFloor, &frontCeiling,
                           &backFloor, &backCeiling)) {
        return 0;
    }

    dist = fixed_mul(trace->range, frac);

    if (frontFloor != backFloor) {
        if (divide_slope(trace, opening.openBottom - trace->shootz, dist,
                         &slope) &&
            slope > trace->bottomSlope) {
            trace->bottomSlope = slope;
        }
    }
    if (frontCeiling != backCeiling) {
        if (divide_slope(trace, opening.openTop - trace->shootz, dist, &slope) &&
            slope < trace->topSlope) {
            trace->topSlope = slope;
        }
    }

    if (trace->topSlope <= trace->bottomSlope) {
        return 0; /* stop */
    }
    return 1; /* shot continues */
}

static int aim_traverse_thing(attack_trace_t* trace, fixed_t frac,
                              const r_mobj_t* thing) {
    fixed_t dist;
    fixed_t thingTopSlope;
    fixed_t thingBottomSlope;

    /* `if (th == shootthing) return true; // can't shoot self` */
    if (thing == trace->shooter) {
        return 1;
    }
    /* `if (!(th->flags&MF_SHOOTABLE)) return true; // corpse or something` */
    if ((thing->flags & MF_SHOOTABLE) == 0u) {
        return 1;
    }

    dist = fixed_mul(trace->range, frac);

    if (!divide_slope(trace, thing->z + thing->height - trace->shootz, dist,
                      &thingTopSlope)) {
        return 1;
    }
    if (thingTopSlope < trace->bottomSlope) {
        return 1; /* shot over the thing */
    }
    if (!divide_slope(trace, thing->z - trace->shootz, dist,
                      &thingBottomSlope)) {
        return 1;
    }
    if (thingBottomSlope > trace->topSlope) {
        return 1; /* shot under the thing */
    }

    /* "this thing can be hit!" */
    if (thingTopSlope > trace->topSlope) {
        thingTopSlope = trace->topSlope;
    }
    if (thingBottomSlope < trace->bottomSlope) {
        thingBottomSlope = trace->bottomSlope;
    }

    /* `aimslope = (thingtopslope+thingbottomslope)/2;`. The sum is evaluated in
     * 64 bits: two saturated slopes overflow a 32-bit int, which is undefined in
     * upstream's expression and is not worth reproducing. */
    trace->aimSlope =
        (fixed_t)(((int64_t)thingTopSlope + (int64_t)thingBottomSlope) / 2);
    trace->lineTarget = (r_mobj_t*)thing;

    return 0; /* don't go any farther */
}

/*
 * PTR_ShootTraverse: resolve what the shot hit and put the effect there.
 *
 * The "position a bit closer" step is upstream's, and the two constants in it
 * matter: four units short of a wall, ten units short of a thing, so a puff is not
 * buried in the surface and a blood spray is not inside the body.
 */
static int shoot_traverse_line(attack_trace_t* trace, fixed_t inFrac,
                               uint16_t lineIndex) {
    doom_line_t line;
    p_line_opening_t opening;
    fixed_t frontFloor;
    fixed_t frontCeiling;
    fixed_t backFloor;
    fixed_t backCeiling;
    fixed_t dist;
    fixed_t slope;

    if (!p_line_decode(trace->world->map, lineIndex, &line)) {
        return 1;
    }

    /* `if (li->special) P_ShootSpecialLine (shootthing, li);` -- the *current*
     * special, which an earlier one-shot trigger may already have cleared. */
    if (current_line_special(trace->world, lineIndex, &line) != 0) {
        p_shoot_special_line(trace->world, (r_mobj_t*)trace->shooter, lineIndex);
    }

    if ((line.flags & DOOM_ML_TWOSIDED) == 0) {
        goto hitline;
    }

    /* "crosses a two sided line" */
    p_line_opening(trace->world, lineIndex, &opening);
    if (!two_sided_heights(trace->world, &line, &frontFloor, &frontCeiling,
                           &backFloor, &backCeiling)) {
        goto hitline;
    }

    dist = fixed_mul(trace->range, inFrac);

    if (frontFloor != backFloor) {
        if (divide_slope(trace, opening.openBottom - trace->shootz, dist,
                         &slope) &&
            slope > trace->aimSlope) {
            goto hitline;
        }
    }
    if (frontCeiling != backCeiling) {
        if (divide_slope(trace, opening.openTop - trace->shootz, dist, &slope) &&
            slope < trace->aimSlope) {
            goto hitline;
        }
    }

    /* "shot continues" */
    return 1;

hitline : {
    const fixed_t frac = inFrac - fixed_div(4 * FRACUNIT, trace->range);
    const fixed_t x = trace->x1 + fixed_mul(trace->dx, frac);
    const fixed_t y = trace->y1 + fixed_mul(trace->dy, frac);
    const fixed_t z =
        trace->shootz + fixed_mul(trace->aimSlope, fixed_mul(frac, trace->range));
    uint16_t front = 0;
    uint16_t back = 0;
    fixed_t frontFloor;
    fixed_t frontCeiling;

    /* `if (li->frontsector->ceilingpic == skyflatnum)`: "don't shoot the sky!",
     * and a wall with sky on both sides is the sky hack, which a bullet passes
     * through. The ceiling height compared is the *front* sector's, which is the
     * one upstream reads through `li->frontsector`. */
    if (p_line_front_sector(trace->world->map, &line, &front) &&
        sector_ceiling_pic(trace->world, front) == P_SKY_FLAT_NAME) {
        if (sector_heights(trace->world, front, &frontFloor, &frontCeiling) &&
            z > frontCeiling) {
            stat_attack(trace->world, count_sky);
            return 0;
        }
        if (p_line_back_sector(trace->world->map, &line, &back) &&
            sector_ceiling_pic(trace->world, back) == P_SKY_FLAT_NAME) {
            stat_attack(trace->world, count_sky);
            return 0;
        }
    }

    /* "Spawn bullet puffs." */
    (void)p_spawn_puff(trace->world, x, y, z, trace->range);

    /* "don't go any farther" */
    return 0;
}
}

static int shoot_traverse_thing(attack_trace_t* trace, fixed_t inFrac,
                                const r_mobj_t* thing) {
    fixed_t dist;
    fixed_t thingTopSlope;
    fixed_t thingBottomSlope;
    fixed_t frac;
    fixed_t x;
    fixed_t y;
    fixed_t z;

    if (thing == trace->shooter) {
        return 1; /* can't shoot self */
    }
    if ((thing->flags & MF_SHOOTABLE) == 0u) {
        return 1; /* corpse or something */
    }

    dist = fixed_mul(trace->range, inFrac);

    if (!divide_slope(trace, thing->z + thing->height - trace->shootz, dist,
                      &thingTopSlope)) {
        return 1;
    }
    if (thingTopSlope < trace->aimSlope) {
        return 1; /* shot over the thing */
    }
    if (!divide_slope(trace, thing->z - trace->shootz, dist,
                      &thingBottomSlope)) {
        return 1;
    }
    if (thingBottomSlope > trace->aimSlope) {
        return 1; /* shot under the thing */
    }

    /* "hit thing -- position a bit closer" */
    frac = inFrac - fixed_div(10 * FRACUNIT, trace->range);
    x = trace->x1 + fixed_mul(trace->dx, frac);
    y = trace->y1 + fixed_mul(trace->dy, frac);
    z = trace->shootz + fixed_mul(trace->aimSlope, fixed_mul(frac, trace->range));

    /* "Spawn bullet puffs or blood spots, depending on target type." */
    if ((thing->flags & MF_NOBLOOD) != 0u) {
        (void)p_spawn_puff(trace->world, x, y, z, trace->range);
    } else {
        (void)p_spawn_blood(trace->world, x, y, z, trace->damage);
    }

    if (trace->damage != 0) {
        p_damage_mobj(trace->world, (r_mobj_t*)thing, (r_mobj_t*)trace->shooter,
                      (r_mobj_t*)trace->shooter, trace->damage);
    }

    return 0; /* don't go any farther */
}

/*
 * P_TraverseIntercepts: the nearest unvisited crossing, over and over.
 *
 * Upstream scans the whole array for a strictly smaller fraction on every pass and
 * marks each entry it visits with MAXINT, so every tie is visited and the ties of
 * one pass are visited in collection order. Both properties are kept.
 */
static void run_traverse(attack_trace_t* trace, int shooting) {
    uint16_t pass;

    for (pass = 0; pass < trace->count; ++pass) {
        fixed_t dist = INT32_MAX;
        attack_intercept_t* entry = NULL;
        uint16_t i;

        for (i = 0; i < trace->count; ++i) {
            if (trace->intercepts[i].frac < dist) {
                dist = trace->intercepts[i].frac;
                entry = &trace->intercepts[i];
            }
        }
        /* `if (dist > maxfrac) return true; // checked everything in range` */
        if (entry == NULL || dist > FRACUNIT) {
            return;
        }

        {
            int keepGoing;

            if (entry->thing != NULL) {
                keepGoing = shooting
                                ? shoot_traverse_thing(trace, entry->frac,
                                                       entry->thing)
                                : aim_traverse_thing(trace, entry->frac,
                                                     entry->thing);
            } else {
                keepGoing = shooting
                                ? shoot_traverse_line(trace, entry->frac,
                                                      entry->line)
                                : aim_traverse_line(trace, entry->frac,
                                                    entry->line);
            }

            /* `in->frac = MAXINT;` marks the entry as visited, and
             * `if (!func (in)) return false;` stops the walk. */
            entry->frac = INT32_MAX;
            if (!keepGoing) {
                return;
            }
        }
    }
}

static void collect_intercepts(attack_trace_t* trace) {
    if (trace->world->attackStats != NULL) {
        stat_attack(trace->world, count_trace);
    }
    collect_lines(trace);
    collect_things(trace);
}

fixed_t p_aim_line_attack(p_mobj_context_t* world, r_mobj_t* shooter,
                          angle_t angle, fixed_t distance, r_mobj_t** target) {
    attack_trace_t trace;
    const uint32_t fine = angle >> ANGLETOFINESHIFT;
    fixed_t x2;
    fixed_t y2;

    if (target != NULL) {
        *target = NULL;
    }
    if (world == NULL || shooter == NULL || world->map == NULL) {
        return 0;
    }

    memset(&trace, 0, sizeof trace);
    trace.world = world;
    trace.shooter = shooter;
    trace.range = distance;
    trace.aimSlope = 0;
    trace.damage = 0;
    /* `shootz = t1->z + (t1->height>>1) + 8*FRACUNIT;` -- the shooter's own
     * mid-height plus eight, which is the eye a monster aims with too. */
    trace.shootz = shooter->z + (shooter->height >> 1) + 8 * FRACUNIT;

    /* "can't shoot outside view angles" */
    trace.topSlope = P_AIM_TOP_SLOPE;
    trace.bottomSlope = -P_AIM_TOP_SLOPE;

    trace.x1 = shooter->x;
    trace.y1 = shooter->y;
    /* `x2 = t1->x + (distance>>FRACBITS)*finecosine[angle];` -- map units times
     * the fine cosine, not a FixedMul. */
    x2 = trace.x1 + (int32_t)(distance >> FRACBITS) * fine_cos(fine);
    y2 = trace.y1 + (int32_t)(distance >> FRACBITS) * fine_sin(fine);
    trace.dx = x2 - trace.x1;
    trace.dy = y2 - trace.y1;

    collect_intercepts(&trace);
    run_traverse(&trace, 0);

    if (target != NULL) {
        *target = trace.lineTarget;
    }
    if (trace.lineTarget != NULL) {
        return trace.aimSlope;
    }
    /* This port has no pitch and no `lookdir`: a trace that found nothing aims
     * dead level, which is vanilla's own answer. */
    return 0;
}

void p_line_attack(p_mobj_context_t* world, r_mobj_t* shooter, angle_t angle,
                   fixed_t distance, fixed_t slope, int damage) {
    attack_trace_t trace;
    const uint32_t fine = angle >> ANGLETOFINESHIFT;
    fixed_t x2;
    fixed_t y2;

    if (world == NULL || shooter == NULL || world->map == NULL) {
        return;
    }

    memset(&trace, 0, sizeof trace);
    trace.world = world;
    trace.shooter = shooter;
    trace.range = distance;
    trace.aimSlope = slope;
    trace.damage = damage;
    trace.shootz = shooter->z + (shooter->height >> 1) + 8 * FRACUNIT;

    trace.x1 = shooter->x;
    trace.y1 = shooter->y;
    x2 = trace.x1 + (int32_t)(distance >> FRACBITS) * fine_cos(fine);
    y2 = trace.y1 + (int32_t)(distance >> FRACBITS) * fine_sin(fine);
    trace.dx = x2 - trace.x1;
    trace.dy = y2 - trace.y1;

    collect_intercepts(&trace);
    run_traverse(&trace, 1);
}

void p_shoot_special_line(p_mobj_context_t* world, r_mobj_t* shooter,
                          uint16_t lineIndex) {
    int result;

    if (world == NULL || shooter == NULL || world->level == NULL) {
        return;
    }

    /* Upstream's `if (!thing->player) return;` and its switch both live in
     * p_spec.c's trigger_shoot(), which is the one place a line special is
     * dispatched. The shooter is passed through, so a monster's shot is refused
     * there exactly as upstream refuses it here. */
    result = p_trigger_special_line(world->level, lineIndex, P_TRIGGER_SHOOT,
                                    shooter, 0);
    if (result) {
        stat_attack(world, count_special);
    }
}



/*
 * P_RadiusAttack(): everything MF_SHOOTABLE within `damage` map units of the
 * spot takes `damage - distance` through P_DamageMobj, and only with sight.
 *
 * Upstream's PIT_RadiusAttack, per thing: skip anything not MF_SHOOTABLE, skip
 * the boss spider and cyborg ("take no damage from concussion"), take the larger
 * of |dx| and |dy|, subtract the thing's radius, clamp at zero, refuse when the
 * result is at least the damage, and then require P_CheckSight(thing, spot)
 * before applying `damage - dist`.
 *
 * The blockmap walk becomes a thinker-list walk. The set differs only in that a
 * thing whose origin is outside the box but whose radius reaches into it is
 * visited here and might not be in a block upstream would have looked at; the
 * distance test above is what decides, and it is the same test. A thing spawned
 * by the damage (a dying monster's dropped clip) is not visited: the next node is
 * taken before the damage, which is the order upstream's unlink/relink implies.
 */
void p_radius_attack(p_mobj_context_t* world, r_mobj_t* spot,
                     r_mobj_t* source, int damage) {
    const r_mobj_t* thing;
    const r_mobj_t* next;

    if (world == NULL || spot == NULL || world->level == NULL) {
        return;
    }
    for (thing = p_level_first_mobj(world->level); thing != NULL;
         thing = next) {
        int64_t dx;
        int64_t dy;
        int64_t dist;

        next = p_level_next_mobj(thing);
        if ((thing->flags & MF_SHOOTABLE) == 0u) {
            continue;
        }
        /* Boss spider and cyborg take no damage from concussion. */
        if (thing->type == MT_CYBORG || thing->type == MT_SPIDER) {
            continue;
        }
        if (thing->thinker.removed != 0u) {
            continue;
        }
        dx = (int64_t)thing->x - (int64_t)spot->x;
        dy = (int64_t)thing->y - (int64_t)spot->y;
        if (dx < 0) {
            dx = -dx;
        }
        if (dy < 0) {
            dy = -dy;
        }
        dist = dx > dy ? dx : dy;
        dist = (dist - (int64_t)thing->radius) >> FRACBITS;
        if (dist < 0) {
            dist = 0;
        }
        if (dist >= damage) {
            continue; /* out of range */
        }
        if (p_check_sight(world, thing, spot)) {
            /* must be in direct path */
            p_damage_mobj(world, (r_mobj_t*)thing, spot, source,
                          damage - (int)dist);
        }
    }
}
