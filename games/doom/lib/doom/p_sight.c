/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * P_CheckSight() and P_NoiseAlert(): line of sight through the BSP, and the
 * per-sector sound flood. See p_sight.h for the contract and the two deliberate
 * deviations (no per-line validcount mark, and upstream's P_DivlineSide typo).
 *
 * This is C99 only: <stdint.h>, no allocation inside a call, no globals, no
 * stdio, no floating point.
 */
#include "p_sight.h"

#include <stddef.h>

#include "p_map.h"

/* --- Upstream's divline --------------------------------------------------- */

typedef struct p_divline {
    fixed_t x;
    fixed_t y;
    fixed_t dx;
    fixed_t dy;
} p_divline_t;

/* int32 arithmetic as upstream's fixed_t performs it. The explicit wrap keeps the
 * two implementations (this file and tools/doom_ref.py) identical even where C's
 * signed overflow would be undefined. */
static fixed_t wrap_sub(fixed_t a, fixed_t b) {
    return (fixed_t)((uint32_t)a - (uint32_t)b);
}

static fixed_t wrap_add(fixed_t a, fixed_t b) {
    return (fixed_t)((uint32_t)a + (uint32_t)b);
}

static fixed_t unit_mul(fixed_t a, fixed_t b) {
    return (fixed_t)((uint32_t)a * (uint32_t)b);
}

/*
 * P_DivlineSide(): 0 front, 1 back, 2 on.
 *
 * The vertical-partition branch compares `x` with `node->y`, which is a typo in
 * linuxdoom-1.10 (`x` is compared with `y`, not with `node->x`). It is kept: it
 * is the rule every node partition was built against, and tools/doom_ref.py has
 * the same line.
 */
static int divline_side(fixed_t x, fixed_t y, const p_divline_t* node) {
    fixed_t dx;
    fixed_t dy;
    fixed_t left;
    fixed_t right;

    if (node->dx == 0) {
        if (x == node->x) {
            return 2;
        }
        if (x <= node->x) {
            return node->dy > 0;
        }
        return node->dy < 0;
    }
    if (node->dy == 0) {
        if (x == node->y) {
            return 2;
        }
        if (y <= node->y) {
            return node->dx < 0;
        }
        return node->dx > 0;
    }

    dx = wrap_sub(x, node->x);
    dy = wrap_sub(y, node->y);
    left = unit_mul(node->dy >> FRACBITS, dx >> FRACBITS);
    right = unit_mul(dy >> FRACBITS, node->dx >> FRACBITS);

    if (right < left) {
        return 0; /* front side */
    }
    if (left == right) {
        return 2;
    }
    return 1; /* back side */
}

/*
 * P_InterceptVector2(): the fraction along the trace where it meets the line.
 *
 * Upstream's signature is `P_InterceptVector2 (divline_t* v2, divline_t* v1)` and
 * it is called as `P_InterceptVector2 (&strace, &divl)`, so `v2` is the trace and
 * `v1` the line. Every direction factor in the numerator is the line's. The
 * `>>8` shifts before FixedMul are upstream's, not FRACBITS.
 */
static fixed_t intercept_vector(const p_divline_t* trace,
                                const p_divline_t* line) {
    fixed_t den;
    fixed_t num;

    den = wrap_sub(fixed_mul(line->dy >> 8, trace->dx),
                   fixed_mul(line->dx >> 8, trace->dy));
    if (den == 0) {
        return 0; /* I_Error ("P_InterceptVector: parallel"); */
    }
    num = wrap_add(fixed_mul(wrap_sub(line->x, trace->x) >> 8, line->dy),
                   fixed_mul(wrap_sub(trace->y, line->y) >> 8, line->dx));
    return fixed_div(num, den);
}

/* One sector's height pair: the level's mutable array when it has one, otherwise
 * the immutable SECTORS record (a hand-built test context). */
static int sight_sector_heights(const p_mobj_context_t* world, uint16_t index,
                                fixed_t* floorHeight, fixed_t* ceilingHeight) {
    doom_sector_t record;

    if (index >= world->sectorCount && world->map != NULL &&
        (uint32_t)index >= world->map->lumps[DOOM_MAP_SECTORS].count) {
        return 0;
    }
    if (world->sectors != NULL && index < world->sectorCount) {
        *floorHeight = world->sectors[index].floorHeight;
        *ceilingHeight = world->sectors[index].ceilingHeight;
        return 1;
    }
    if (world->map == NULL) {
        return 0;
    }
    doom_map_get_sector(world->map, index, &record);
    *floorHeight = record.floorHeight;
    *ceilingHeight = record.ceilingHeight;
    return 1;
}

/* P_CrossSubsector(): 1 when the segment crosses this subsector cleanly. */
static int cross_subsector(p_mobj_context_t* world, uint16_t subsectorIndex,
                           const p_divline_t* trace, fixed_t t2x, fixed_t t2y,
                           fixed_t sightzstart, fixed_t* topSlope,
                           fixed_t* bottomSlope) {
    const doom_map_t* map = world->map;
    doom_subsector_t subsector;
    uint32_t i;

    doom_map_get_subsector(map, subsectorIndex, &subsector);
    for (i = 0; i < subsector.numSegs; ++i) {
        doom_seg_t seg;
        doom_line_t line;
        doom_vertex_t v1;
        doom_vertex_t v2;
        p_divline_t divl;
        uint16_t front = 0;
        uint16_t back = 0;
        fixed_t frontFloor;
        fixed_t frontCeiling;
        fixed_t backFloor;
        fixed_t backCeiling;
        fixed_t opentop;
        fixed_t openbottom;
        fixed_t frac;
        fixed_t slope;

        doom_map_get_seg(map, (uint16_t)(subsector.firstSeg + i), &seg);
        doom_map_get_line(map, seg.linedef, &line);
        doom_map_get_vertex(map, line.v1, &v1);
        doom_map_get_vertex(map, line.v2, &v2);

        /* line isn't crossed? */
        if (divline_side(v1.x, v1.y, trace) == divline_side(v2.x, v2.y, trace)) {
            continue;
        }

        divl.x = v1.x;
        divl.y = v1.y;
        divl.dx = wrap_sub(v2.x, v1.x);
        divl.dy = wrap_sub(v2.y, v1.y);
        if (divline_side(trace->x, trace->y, &divl) ==
            divline_side(t2x, t2y, &divl)) {
            continue;
        }

        /* stop because it is not two sided anyway */
        if ((line.flags & DOOM_ML_TWOSIDED) == 0) {
            return 0;
        }
        if (!doom_map_seg_sectors(map, &seg, &front, &back)) {
            return 0;
        }
        if (!sight_sector_heights(world, front, &frontFloor, &frontCeiling) ||
            !sight_sector_heights(world, back, &backFloor, &backCeiling)) {
            return 0;
        }

        /* no wall to block sight with? */
        if (frontFloor == backFloor && frontCeiling == backCeiling) {
            continue;
        }

        opentop = frontCeiling < backCeiling ? frontCeiling : backCeiling;
        openbottom = frontFloor > backFloor ? frontFloor : backFloor;

        /* quick test for totally closed doors */
        if (openbottom >= opentop) {
            return 0; /* stop */
        }

        frac = intercept_vector(trace, &divl);

        if (frontFloor != backFloor) {
            slope = fixed_div(wrap_sub(openbottom, sightzstart), frac);
            if (slope > *bottomSlope) {
                *bottomSlope = slope;
            }
        }
        if (frontCeiling != backCeiling) {
            slope = fixed_div(wrap_sub(opentop, sightzstart), frac);
            if (slope < *topSlope) {
                *topSlope = slope;
            }
        }
        if (*topSlope <= *bottomSlope) {
            return 0; /* stop */
        }
    }
    return 1; /* passed the subsector ok */
}

/* P_CrossBSPNode(): 1 when the segment crosses this node's subtree cleanly. */
static int cross_bsp_node(p_mobj_context_t* world, int32_t bspnum,
                          const p_divline_t* trace, fixed_t t2x, fixed_t t2y,
                          fixed_t sightzstart, fixed_t* topSlope,
                          fixed_t* bottomSlope) {
    const doom_map_t* map = world->map;
    doom_node_t node;
    p_divline_t partition;
    int side;

    if ((bspnum & (int32_t)R_NF_SUBSECTOR) != 0) {
        if (bspnum == -1) {
            return cross_subsector(world, 0, trace, t2x, t2y, sightzstart,
                                   topSlope, bottomSlope);
        }
        return cross_subsector(world, (uint16_t)(bspnum & ~(int32_t)R_NF_SUBSECTOR),
                               trace, t2x, t2y, sightzstart, topSlope,
                               bottomSlope);
    }

    doom_map_get_node(map, (uint16_t)bspnum, &node);
    partition.x = node.x;
    partition.y = node.y;
    partition.dx = node.dx;
    partition.dy = node.dy;

    /* decide which side the start point is on */
    side = divline_side(trace->x, trace->y, &partition);
    if (side == 2) {
        side = 0; /* an "on" should cross both sides */
    }

    /* cross the starting side */
    if (!cross_bsp_node(world, (int32_t)node.children[side], trace, t2x, t2y,
                        sightzstart, topSlope, bottomSlope)) {
        return 0;
    }

    /* the partition plane is crossed here */
    if (side == divline_side(t2x, t2y, &partition)) {
        /* the line doesn't touch the other side */
        return 1;
    }

    /* cross the ending side */
    return cross_bsp_node(world, (int32_t)node.children[side ^ 1], trace, t2x,
                          t2y, sightzstart, topSlope, bottomSlope);
}

/* --- P_CheckSight --------------------------------------------------------- */

int p_check_sight(p_mobj_context_t* world, const r_mobj_t* t1,
                  const r_mobj_t* t2) {
    const doom_map_t* map;
    const uint8_t* reject;
    size_t rejectSize;
    uint32_t sectorCount;
    uint32_t pnum;
    int32_t nodeCount;
    fixed_t sightzstart;
    fixed_t topslope;
    fixed_t bottomslope;
    p_divline_t trace;
    int visible;

    if (world == NULL || t1 == NULL || t2 == NULL) {
        return 0;
    }
    map = world->map;
    if (map == NULL || map->open == 0) {
        return 0;
    }

    sectorCount = world->sectorCount != 0u
                      ? world->sectorCount
                      : (uint32_t)map->lumps[DOOM_MAP_SECTORS].count;
    if (sectorCount == 0u || (uint32_t)t1->sector >= sectorCount ||
        (uint32_t)t2->sector >= sectorCount) {
        return 0;
    }
    if (world->sightStats != NULL) {
        world->sightStats->checks += 1u;
    }

    /* First check for trivial rejection: the REJECT table's sector pair. */
    pnum = (uint32_t)t1->sector * sectorCount + (uint32_t)t2->sector;
    reject = map->lumps[DOOM_MAP_REJECT].data;
    rejectSize = map->lumps[DOOM_MAP_REJECT].size;
    if (reject != NULL && (size_t)(pnum >> 3) < rejectSize) {
        if ((reject[pnum >> 3] & (uint8_t)(1u << (pnum & 7u))) != 0u) {
            if (world->sightStats != NULL) {
                world->sightStats->rejectHits += 1u;
            }
            return 0; /* can't possibly be connected */
        }
    } else if (world->sightStats != NULL) {
        world->sightStats->rejectMissing += 1u;
    }

    /* An unobstructed LOS is possible. Now look from eyes of t1 to any part of
     * t2. */
    if (world->sightStats != NULL) {
        world->sightStats->bspWalks += 1u;
    }
    sightzstart = wrap_sub(wrap_add(t1->z, t1->height), t1->height >> P_SIGHT_EYE_SHIFT);
    topslope = wrap_sub(wrap_add(t2->z, t2->height), sightzstart);
    bottomslope = wrap_sub(t2->z, sightzstart);

    trace.x = t1->x;
    trace.y = t1->y;
    trace.dx = wrap_sub(t2->x, t1->x);
    trace.dy = wrap_sub(t2->y, t1->y);

    /* the head node is the last node output; a map with no nodes is subsector 0 */
    nodeCount = (int32_t)map->lumps[DOOM_MAP_NODES].count;
    visible = cross_bsp_node(world, nodeCount - 1, &trace, t2->x, t2->y,
                             sightzstart, &topslope, &bottomslope);
    if (world->sightStats != NULL) {
        if (visible) {
            world->sightStats->accepted += 1u;
        } else {
            world->sightStats->bspBlocked += 1u;
        }
    }
    return visible;
}

/* --- P_RecursiveSound / P_NoiseAlert -------------------------------------- */

/*
 * P_RecursiveSound(): wake every sector this one can carry sound to.
 *
 * Upstream iterates `sec->lines[]`; this port scans LINEDEFS for two-sided lines
 * that touch the sector (p_spec.h's documented adjacency pattern). `openrange`
 * comes from P_LineOpening, so a closed door blocks the flood exactly as it
 * blocks a body. ML_SOUNDBLOCK lines are crossed once and only once.
 */
static void recursive_sound(p_mobj_context_t* world, uint16_t sectorIndex,
                            int soundblocks, r_mobj_t* target) {
    p_sector_sound_t* state;
    uint32_t i;

    if (sectorIndex >= world->sectorSoundCount ||
        world->sectorSound == NULL) {
        return;
    }
    state = &world->sectorSound[sectorIndex];

    /* already flooded? */
    if (state->validCount == world->soundValidCount &&
        state->soundTraversed <= soundblocks + 1) {
        return;
    }

    /* wake up all monsters in this sector */
    state->validCount = world->soundValidCount;
    state->soundTraversed = (int16_t)(soundblocks + 1);
    state->soundTarget = target;

    for (i = 0; i < world->lineCount; ++i) {
        doom_line_t line;
        uint16_t front = 0;
        uint16_t back = 0;
        uint16_t other;
        p_line_opening_t opening;

        doom_map_get_line(world->map, (uint16_t)i, &line);
        if ((line.flags & DOOM_ML_TWOSIDED) == 0) {
            continue;
        }
        if (!p_line_front_sector(world->map, &line, &front) ||
            !p_line_back_sector(world->map, &line, &back)) {
            continue;
        }
        if (front != sectorIndex && back != sectorIndex) {
            continue;
        }

        p_line_opening(world, (uint16_t)i, &opening);
        if (opening.openRange <= 0) {
            continue; /* closed door */
        }

        other = (front == sectorIndex) ? back : front;
        if ((line.flags & DOOM_ML_SOUNDBLOCK) != 0) {
            if (soundblocks == 0) {
                recursive_sound(world, other, 1, target);
            }
        } else {
            recursive_sound(world, other, soundblocks, target);
        }
    }
}

void p_noise_alert(p_mobj_context_t* world, r_mobj_t* target,
                   r_mobj_t* emitter) {
    if (world == NULL || emitter == NULL || world->sectorSound == NULL) {
        return;
    }
    if (emitter->sector >= world->sectorSoundCount) {
        return;
    }

    world->soundValidCount += 1u;
    recursive_sound(world, emitter->sector, 0, target);
}

r_mobj_t* p_sector_sound_target(const p_mobj_context_t* world,
                                uint16_t sector) {
    if (world == NULL || world->sectorSound == NULL ||
        sector >= world->sectorSoundCount) {
        return NULL;
    }
    return world->sectorSound[sector].soundTarget;
}
