/*
 * The BSP walk of the DOOM port (r_bsp.h), from upstream's r_bsp.c and the
 * marking half of r_segs.c.
 *
 * WHAT IS PORTED, AND FROM WHERE
 *
 *   R_PointOnSide, R_CheckBBox, R_AddLine, R_ClipSolidWallSegment,
 *   R_ClipPassWallSegment, R_ClearClipSegs, R_Subsector, R_RenderBSPNode
 *       r_bsp.c. Traversal order, clip-post arithmetic and every cull are
 *       kept; the clip list is an array in the caller instead of solidsegs[].
 *
 *   R_PointToAngle, R_PointToDist, R_ScaleFromGlobalAngle
 *       r_main.c, using the same arc-tangent table and the same wrap-around
 *       arithmetic.
 *
 *   R_StoreWallRange, and the floor/ceiling marking of R_RenderSegLoop
 *       r_segs.c. The texture-column lookup, the three wall tiers and all
 *       pixel writes are D3.2's (r_segs.c). What remains here is the silhouette
 *       decision, the light index, the two scales, the range values and the
 *       plane marks -- and that decision block is r_wall_fragment_open(), which
 *       D3.2 calls a second time from the stored drawseg rather than
 *       reimplementing.
 *
 * DELIBERATE DIVERGENCES (each is visible in behaviour and named for review)
 *
 *   1. Every global upstream uses became a field of r_view_state_t, so two
 *      views can coexist and nothing mutable is file-scope.
 *   2. The clip list has a checked capacity. Upstream writes past solidsegs[31]
 *      if a view ever needs more posts; here that is R_BSP_ERR_CLIP_OVERFLOW
 *      and the walk stops with a complete, shorter result.
 *   3. The plane set has a checked capacity (r_plane.c), for the same reason.
 *   4. fixed_div() is integer division, not the double division of upstream's
 *      FixedDiv2(). m_fixed.h documents the one-unit worst case.
 *   5. Sprite clipping arrays and masked-texture columns are not recorded by
 *      the walk: they are r_wall_emit()'s (D3.4), which read the clip arrays
 *      after its column loop. The drawseg stores them as block indices into
 *      caller-owned pools; the walk only initialises them to "none".
 *   6. The clip-list "crunch" is written as a removal (`start+1 .. next` are
 *      deleted and the tail shifts down) instead of upstream's pointer walk
 *      `while (next++ != newend) *++start = *next;`. Upstream's loop copies
 *      one element past the list end, which leaves the [width, +inf] sentinel
 *      one slot early and a stale, never-read entry inside newend. Both stop
 *      every later scan at the same sentinel, so the fragment decisions are
 *      identical; this version simply cannot let the stale slot consume clip
 *      capacity.
 *
 * This is C99 only: no allocation, no globals, no stdio, no floating point.
 */
#include "r_bsp.h"

#include "r_tables.h"

/* The eight name bytes of the sky flat, written out so no string literal is
 * read past its terminator. */
static const uint8_t kSkyName[8] = {'F', '_', 'S', 'K', 'Y', '1', 0, 0};

/* --- Small helpers ------------------------------------------------------- */

static void fail(r_view_state_t* state, r_bsp_error_t error) {
    if (state->error == R_BSP_OK) {
        state->error = error;
    }
}

static fixed_t abs_fixed(fixed_t value) {
    const int64_t wide = value;

    return (fixed_t)(wide < 0 ? -wide : wide);
}

/* -value with two's-complement wrap, so INT32_MIN negates to itself instead of
 * invoking the signed overflow rule. Upstream's `-x` on a fixed_t wraps the same
 * way on every compiler this port supports. */
static fixed_t neg_fixed(fixed_t value) {
    return (fixed_t)(0u - (uint32_t)value);
}

static int abs_angle_32(angle_t value) {
    const int32_t signed_value = (int32_t)value;

    return signed_value < 0 ? -signed_value : signed_value;
}

/* --- Geometry ------------------------------------------------------------ */

/* R_PointOnSide(): which half-plane of a node's partition a point is in. The
 * sign-bit fast path is upstream's, including the 32-bit xor trick. */
static int point_on_side(fixed_t x, fixed_t y, const doom_node_t* node) {
    fixed_t dx;
    fixed_t dy;
    fixed_t left;
    fixed_t right;

    if (node->dx == 0) {
        if (x <= node->x) {
            return node->dy > 0;
        }
        return node->dy < 0;
    }
    if (node->dy == 0) {
        if (y <= node->y) {
            return node->dx < 0;
        }
        return node->dx > 0;
    }

    dx = (fixed_t)((uint32_t)x - (uint32_t)node->x);
    dy = (fixed_t)((uint32_t)y - (uint32_t)node->y);

    if (((node->dy ^ node->dx ^ dx ^ dy) & (fixed_t)0x80000000) != 0) {
        return ((node->dy ^ dx) & (fixed_t)0x80000000) != 0 ? 1 : 0;
    }

    left = fixed_mul(node->dy >> FRACBITS, dx);
    right = fixed_mul(dy, node->dx >> FRACBITS);

    return right < left ? 0 : 1;
}

/* R_PointToAngle(): octant-normalised arc tangent through the table. */
static angle_t point_to_angle(const r_view_params_t* view, fixed_t x, fixed_t y) {
    fixed_t dx = (fixed_t)((uint32_t)x - (uint32_t)view->x);
    fixed_t dy = (fixed_t)((uint32_t)y - (uint32_t)view->y);

    if (dx == 0 && dy == 0) {
        return 0;
    }

    if (dx >= 0) {
        if (dy >= 0) {
            if (dx > dy) {
                return r_tan_to_angle[slope_div((uint32_t)dy, (uint32_t)dx)];
            }
            return ANG90 - 1u - r_tan_to_angle[slope_div((uint32_t)dx, (uint32_t)dy)];
        }
        dy = (fixed_t)(0u - (uint32_t)dy);
        if (dx > dy) {
            return 0u - r_tan_to_angle[slope_div((uint32_t)dy, (uint32_t)dx)];
        }
        return ANG270 + r_tan_to_angle[slope_div((uint32_t)dx, (uint32_t)dy)];
    }

    dx = (fixed_t)(0u - (uint32_t)dx);
    if (dy >= 0) {
        if (dx > dy) {
            return ANG180 - 1u - r_tan_to_angle[slope_div((uint32_t)dy, (uint32_t)dx)];
        }
        return ANG90 + r_tan_to_angle[slope_div((uint32_t)dx, (uint32_t)dy)];
    }
    dy = (fixed_t)(0u - (uint32_t)dy);
    if (dx > dy) {
        return ANG180 + r_tan_to_angle[slope_div((uint32_t)dy, (uint32_t)dx)];
    }
    return ANG270 - 1u - r_tan_to_angle[slope_div((uint32_t)dx, (uint32_t)dy)];
}

/* R_PointToDist(): distance from the viewpoint to a point, 16.16. */
static fixed_t point_to_dist(const r_view_params_t* view, fixed_t x, fixed_t y) {
    fixed_t dx = abs_fixed((fixed_t)((uint32_t)x - (uint32_t)view->x));
    fixed_t dy = abs_fixed((fixed_t)((uint32_t)y - (uint32_t)view->y));
    fixed_t temp;
    angle_t angle;

    if (dy > dx) {
        temp = dx;
        dx = dy;
        dy = temp;
    }
    if (dx == 0) {
        /* Both points coincide. Upstream would divide by zero here; no real
         * map reaches it, and answering zero keeps a corrupt map from reading
         * a table out of bounds. */
        return 0;
    }

    angle = (r_tan_to_angle[fixed_div(dy, dx) >> (FRACBITS - 11)] + ANG90) >>
            ANGLETOFINESHIFT;
    return fixed_div(dx, r_fine_sine[angle]);
}

/* R_ScaleFromGlobalAngle(): the texture scale at one column, 16.16. */
static fixed_t scale_from_global_angle(const r_view_params_t* view,
                                       angle_t normalAngle, fixed_t distance,
                                       angle_t visangle) {
    const angle_t anglea = ANG90 + (visangle - view->angle);
    const angle_t angleb = ANG90 + (visangle - normalAngle);
    const fixed_t sinea = r_fine_sine[anglea >> ANGLETOFINESHIFT];
    const fixed_t sineb = r_fine_sine[angleb >> ANGLETOFINESHIFT];
    const fixed_t num = fixed_mul(R_CENTER_X_FRAC, sineb);
    const fixed_t den = fixed_mul(distance, sinea);
    fixed_t scale;

    if (den > (num >> FRACBITS)) {
        scale = fixed_div(num, den);
        if (scale > 64 * FRACUNIT) {
            scale = 64 * FRACUNIT;
        } else if (scale < 256) {
            scale = 256;
        }
        return scale;
    }
    return 64 * FRACUNIT;
}

/* --- Clip posts (r_bsp.c R_ClearClipSegs and friends) -------------------- */

static void clear_clip_segs(r_view_state_t* state) {
    r_clip_range_t* solid = state->storage.clipRanges;

    solid[0].first = -0x7fffffff;
    solid[0].last = -1;
    solid[1].first = R_SCREEN_WIDTH;
    solid[1].last = 0x7fffffff;
    state->clipRangeCount = 2;
}

static int check_bbox(r_view_state_t* state, const fixed_t* bbox) {
    static const int checkcoord[11][4] = {
        {3, 0, 2, 1}, {3, 0, 2, 0}, {3, 1, 2, 0},
        {0, 0, 0, 0},
        {2, 0, 2, 1}, {0, 0, 0, 0}, {3, 1, 3, 0},
        {0, 0, 0, 0},
        {2, 0, 3, 1}, {2, 1, 3, 1}, {2, 1, 3, 0}
    };
    const r_clip_range_t* solid = state->storage.clipRanges;
    int boxx;
    int boxy;
    int boxpos;
    fixed_t x1;
    fixed_t y1;
    fixed_t x2;
    fixed_t y2;
    angle_t angle1;
    angle_t angle2;
    angle_t span;
    angle_t tspan;
    int sx1;
    int sx2;
    int start;
    r_raster_probe_t* probe = R_PROBE_OF(state);

    if (probe != NULL) {
        ++probe->bboxTests;
    }

    if (state->params.x <= bbox[2]) {
        boxx = 0;
    } else if (state->params.x < bbox[3]) {
        boxx = 1;
    } else {
        boxx = 2;
    }

    if (state->params.y >= bbox[0]) {
        boxy = 0;
    } else if (state->params.y > bbox[1]) {
        boxy = 1;
    } else {
        boxy = 2;
    }

    boxpos = (boxy << 2) + boxx;
    if (boxpos == 5) {
        return 1;
    }

    x1 = bbox[checkcoord[boxpos][0]];
    y1 = bbox[checkcoord[boxpos][1]];
    x2 = bbox[checkcoord[boxpos][2]];
    y2 = bbox[checkcoord[boxpos][3]];

    angle1 = point_to_angle(&state->params, x1, y1) - state->params.angle;
    angle2 = point_to_angle(&state->params, x2, y2) - state->params.angle;
    span = angle1 - angle2;

    if (span >= ANG180) {
        return 1;
    }

    tspan = angle1 + r_clip_angle;
    if (tspan > 2 * r_clip_angle) {
        tspan -= 2 * r_clip_angle;
        if (tspan >= span) {
            return 0;
        }
        angle1 = r_clip_angle;
    }

    tspan = r_clip_angle - angle2;
    if (tspan > 2 * r_clip_angle) {
        tspan -= 2 * r_clip_angle;
        if (tspan >= span) {
            return 0;
        }
        angle2 = (angle_t)(0u - r_clip_angle);
    }

    sx1 = r_view_angle_to_x[(angle1 + ANG90) >> ANGLETOFINESHIFT];
    sx2 = r_view_angle_to_x[(angle2 + ANG90) >> ANGLETOFINESHIFT];

    if (sx1 == sx2) {
        return 0;
    }
    sx2 -= 1;

    start = 0;
    while (solid[start].last < sx2) {
        ++start;
    }
    if (probe != NULL) {        /* `start` is the number of posts the scan walked past, so the count is
         * free: adding a counter inside the loop would cost more than the
         * loop. */
        probe->clipScanSteps += (uint32_t)start;
    }

    return !(sx1 >= solid[start].first && sx2 <= solid[start].last);
}

/* --- Wall ranges (r_segs.c R_StoreWallRange, marking half) --------------- */

/* R_RenderSegLoop's floor/ceiling marking, exactly upstream's column loop. D3.2
 * reimplements the drawing half in r_segs.c against the same fragment; this is
 * the half that fills the plane arrays, and it runs during the walk because the
 * planes are part of the walk's output. */
static void render_seg_loop(r_view_state_t* state, r_wall_fragment_t* fragment) {
    int16_t* floorclip = state->storage.floorClip;
    int16_t* ceilingclip = state->storage.ceilingClip;
    r_visplane_t* floorplane = state->floorPlane;
    r_visplane_t* ceilingplane = state->ceilingPlane;
    fixed_t topfrac = fragment->topFrac;
    fixed_t bottomfrac = fragment->bottomFrac;
    fixed_t pixhigh = fragment->pixHigh;
    fixed_t pixlow = fragment->pixLow;
    int x;

    for (x = fragment->x1; x <= fragment->x2; ++x) {
        int yl = (int)((topfrac + (1 << 12) - 1) >> 12);
        int yh;

        if (yl < ceilingclip[x] + 1) {
            yl = ceilingclip[x] + 1;
        }

        if (fragment->markCeiling) {
            int top = ceilingclip[x] + 1;
            int bottom = yl - 1;

            if (bottom >= floorclip[x]) {
                bottom = floorclip[x] - 1;
            }
            if (top <= bottom) {
                ceilingplane->top[x] = (uint8_t)top;
                ceilingplane->bottom[x] = (uint8_t)bottom;
            }
        }

        yh = (int)(bottomfrac >> 12);
        if (yh >= floorclip[x]) {
            yh = floorclip[x] - 1;
        }

        if (fragment->markFloor) {
            int top = yh + 1;
            int bottom = floorclip[x] - 1;

            if (top <= ceilingclip[x]) {
                top = ceilingclip[x] + 1;
            }
            if (top <= bottom) {
                floorplane->top[x] = (uint8_t)top;
                floorplane->bottom[x] = (uint8_t)bottom;
            }
        }

        if (fragment->wallMid) {
            ceilingclip[x] = (int16_t)R_VIEW_HEIGHT;
            floorclip[x] = -1;
        } else {
            if (fragment->wallTop) {
                int mid = (int)(pixhigh >> 12);

                pixhigh += fragment->pixHighStep;
                if (mid >= floorclip[x]) {
                    mid = floorclip[x] - 1;
                }
                if (mid >= yl) {
                    ceilingclip[x] = (int16_t)mid;
                } else {
                    ceilingclip[x] = (int16_t)(yl - 1);
                }
            } else if (fragment->markCeiling) {
                ceilingclip[x] = (int16_t)(yl - 1);
            }

            if (fragment->wallBottom) {
                int mid = (int)((pixlow + (1 << 12) - 1) >> 12);

                pixlow += fragment->pixLowStep;
                if (mid <= ceilingclip[x]) {
                    mid = ceilingclip[x] + 1;
                }
                if (mid <= yh) {
                    floorclip[x] = (int16_t)mid;
                } else {
                    floorclip[x] = (int16_t)(yh + 1);
                }
            } else if (fragment->markFloor) {
                floorclip[x] = (int16_t)(yh + 1);
            }
        }

        topfrac += fragment->topStep;
        bottomfrac += fragment->bottomStep;
    }
}

/* R_StoreWallRange's range half: rw_distance, the two scales and the three
 * values the drawseg stores (r_bsp.h explains why those three and no others).
 * This is the sign/orientation math the emitter must not repeat. */
static void wall_fragment_range(r_wall_fragment_t* fragment,
                                const r_view_params_t* view, angle_t angle1,
                                int start, int stop) {
    fixed_t offsetangle;
    fixed_t distangle;
    fixed_t hyp;
    fixed_t sineval;
    angle_t delta;
    angle_t offsetAngle;

    fragment->x1 = (int16_t)start;
    fragment->x2 = (int16_t)stop;

    offsetangle = (fixed_t)abs_angle_32(fragment->normalAngle - angle1);
    if (offsetangle > (fixed_t)ANG90) {
        offsetangle = (fixed_t)ANG90;
    }
    distangle = (fixed_t)(ANG90 - (angle_t)offsetangle);
    hyp = point_to_dist(view, fragment->v1x, fragment->v1y);
    sineval = r_fine_sine[(angle_t)distangle >> ANGLETOFINESHIFT];
    fragment->distance = fixed_mul(hyp, sineval);

    fragment->scale1 = scale_from_global_angle(
        view, fragment->normalAngle, fragment->distance,
        view->angle + r_x_to_view_angle[start]);
    if (stop > start) {
        fragment->scale2 = scale_from_global_angle(
            view, fragment->normalAngle, fragment->distance,
            view->angle + r_x_to_view_angle[stop]);
        /* C's division truncates toward zero; the columns are positive. */
        fragment->scaleStep = (fragment->scale2 - fragment->scale1) / (stop - start);
    } else {
        fragment->scale2 = fragment->scale1;
        fragment->scaleStep = 0;
    }

    /* rw_offset, R_StoreWallRange's second offset-angle block. The angle is an
     * unsigned binary angle there, so the fold and the clamp are the modular
     * ones, and the sign test is on the *unclamped* delta. */
    delta = fragment->normalAngle - angle1;
    offsetAngle = delta;
    if (offsetAngle > ANG180) {
        offsetAngle = (angle_t)(0u - offsetAngle);
    }
    if (offsetAngle > ANG90) {
        offsetAngle = ANG90;
    }
    sineval = r_fine_sine[offsetAngle >> ANGLETOFINESHIFT];
    fragment->offset = fixed_mul(hyp, sineval);
    if (delta < ANG180) {
        fragment->offset = neg_fixed(fragment->offset);
    }
    fragment->offset = (fixed_t)((uint32_t)fragment->offset
                                 + (uint32_t)fragment->textureOffset
                                 + (uint32_t)fragment->segOffset);

    /* View-relative on purpose: r_segs.c adds xtoviewangle[x] and shifts, and
     * this form is what keeps the tangent index inside [0, FINEANGLES/2). */
    fragment->centerAngle = ANG90 + view->angle - fragment->normalAngle;
}

void r_wall_fragment_open(const doom_map_t* map, const r_view_params_t* view,
                          uint16_t segIndex, r_wall_fragment_t* out) {
    doom_seg_t seg;
    doom_line_t line;
    doom_side_t sidedef;
    doom_sector_t front;
    doom_sector_t back;
    doom_vertex_t v1;
    doom_vertex_t v2;
    uint16_t frontIndex;
    uint16_t backIndex;
    int hasBack;
    fixed_t worldtop;
    fixed_t worldbottom;
    fixed_t worldhigh = 0;
    fixed_t worldlow = 0;

    *out = (r_wall_fragment_t){0};
    out->seg = segIndex;
    out->bottomSilhouette = INT32_MAX;
    out->topSilhouette = INT32_MIN;

    doom_map_get_seg(map, segIndex, &seg);
    doom_map_get_line(map, seg.linedef, &line);
    doom_map_get_side(map, seg.side != 0 ? line.sideNum[1] : line.sideNum[0],
                      &sidedef);
    doom_map_get_vertex(map, seg.v1, &v1);
    doom_map_get_vertex(map, seg.v2, &v2);
    hasBack = doom_map_seg_sectors(map, &seg, &frontIndex, &backIndex);
    doom_map_get_sector(map, frontIndex, &front);

    out->frontSector = frontIndex;
    out->hasBack = (uint8_t)(hasBack ? 1 : 0);
    out->flags = line.flags;
    out->textureOffset = sidedef.textureOffset;
    out->rowOffset = sidedef.rowOffset;
    out->midTexture = sidedef.midTexture;
    out->topTexture = sidedef.topTexture;
    out->bottomTexture = sidedef.bottomTexture;
    out->v1x = v1.x;
    out->v1y = v1.y;
    out->normalAngle = seg.angle + ANG90;
    out->segOffset = seg.offset;
    out->lightLevel = front.lightLevel;

    worldtop = front.ceilingHeight - view->z;
    worldbottom = front.floorHeight - view->z;

    if (!hasBack) {
        out->wallMid = (uint8_t)(r_has_name(sidedef.midTexture) ? 1 : 0);
        out->markFloor = 1;
        out->markCeiling = 1;
        out->silhouette = R_SIL_BOTH;
    } else {
        doom_map_get_sector(map, backIndex, &back);
        out->backSector = backIndex;

        if (front.floorHeight > back.floorHeight) {
            out->silhouette = R_SIL_BOTTOM;
            out->bottomSilhouette = front.floorHeight;
        } else if (back.floorHeight > view->z) {
            out->silhouette = R_SIL_BOTTOM;
            out->bottomSilhouette = INT32_MAX;
        }

        if (front.ceilingHeight < back.ceilingHeight) {
            out->silhouette |= R_SIL_TOP;
            out->topSilhouette = front.ceilingHeight;
        } else if (back.ceilingHeight < view->z) {
            out->silhouette |= R_SIL_TOP;
            out->topSilhouette = INT32_MIN;
        }

        if (back.ceilingHeight <= front.floorHeight) {
            out->bottomSilhouette = INT32_MAX;
            out->silhouette |= R_SIL_BOTTOM;
            out->clipBottomSolid = 1;
        }
        if (back.floorHeight >= front.ceilingHeight) {
            out->topSilhouette = INT32_MIN;
            out->silhouette |= R_SIL_TOP;
            out->clipTopSolid = 1;
        }

        worldhigh = back.ceilingHeight - view->z;
        worldlow = back.floorHeight - view->z;

        /* "hack to allow height changes in outdoor areas": both ceilings sky
         * means the wall between them is not a ceiling at all. */
        if (front.ceilingPic == view->skyPicNum &&
            back.ceilingPic == view->skyPicNum) {
            worldtop = worldhigh;
        }

        if (worldlow != worldbottom || back.floorPic != front.floorPic ||
            back.lightLevel != front.lightLevel) {
            out->markFloor = 1;
        }
        if (worldhigh != worldtop || back.ceilingPic != front.ceilingPic ||
            back.lightLevel != front.lightLevel) {
            out->markCeiling = 1;
        }
        if (back.ceilingHeight <= front.floorHeight ||
            back.floorHeight >= front.ceilingHeight) {
            out->markFloor = 1;
            out->markCeiling = 1;
        }

        if (worldhigh < worldtop) {
            out->wallTop = (uint8_t)(r_has_name(sidedef.topTexture) ? 1 : 0);
        }
        if (worldlow > worldbottom) {
            out->wallBottom = (uint8_t)(r_has_name(sidedef.bottomTexture) ? 1 : 0);
        }
    }

    /* A floor/ceiling plane on the wrong side of the view plane is invisible. */
    if (front.floorHeight >= view->z) {
        out->markFloor = 0;
    }
    if (front.ceilingHeight <= view->z &&
        front.ceilingPic != view->skyPicNum) {
        out->markCeiling = 0;
    }

    out->worldTop = worldtop;
    out->worldBottom = worldbottom;
    out->worldHigh = worldhigh;
    out->worldLow = worldlow;

    /* Wall light index: sector light, gun flash, and the axis correction. */
    out->lightNum = (int16_t)((front.lightLevel >> 4) + view->extralight);
    if (v1.y == v2.y) {
        out->lightNum = (int16_t)(out->lightNum - 1);
    } else if (v1.x == v2.x) {
        out->lightNum = (int16_t)(out->lightNum + 1);
    }
}

void r_wall_fragment_step(r_wall_fragment_t* fragment, fixed_t scale1,
                          fixed_t scaleStep) {
    const fixed_t worldtop = fragment->worldTop >> 4;
    const fixed_t worldbottom = fragment->worldBottom >> 4;

    fragment->scale = scale1;
    fragment->topStep = neg_fixed(fixed_mul(scaleStep, worldtop));
    fragment->topFrac = (R_CENTER_Y_FRAC >> 4) - fixed_mul(worldtop, scale1);
    fragment->bottomStep = neg_fixed(fixed_mul(scaleStep, worldbottom));
    fragment->bottomFrac = (R_CENTER_Y_FRAC >> 4) - fixed_mul(worldbottom, scale1);

    fragment->pixHigh = 0;
    fragment->pixLow = 0;
    fragment->pixHighStep = 0;
    fragment->pixLowStep = 0;
    if (fragment->hasBack) {
        const fixed_t worldhigh = fragment->worldHigh >> 4;
        const fixed_t worldlow = fragment->worldLow >> 4;

        if (worldhigh < worldtop) {
            fragment->pixHigh =
                (R_CENTER_Y_FRAC >> 4) - fixed_mul(worldhigh, scale1);
            fragment->pixHighStep = neg_fixed(fixed_mul(scaleStep, worldhigh));
        }
        if (worldlow > worldbottom) {
            fragment->pixLow =
                (R_CENTER_Y_FRAC >> 4) - fixed_mul(worldlow, scale1);
            fragment->pixLowStep = neg_fixed(fixed_mul(scaleStep, worldlow));
        }
    }
}

void r_wall_fragment_peg(r_wall_fragment_t* fragment, fixed_t midHeight,
                         fixed_t topHeight) {

    /* Single-sided mid texture: R_StoreWallRange's !backsector peg. A two-sided
     * line's mid texture is upstream's *masked* texture, recorded for
     * R_DrawMasked and not drawn by R_RenderSegLoop, so wallMid is 0 there and
     * this branch cannot see it. */
    if (fragment->wallMid) {
        if ((fragment->flags & DOOM_ML_DONTPEGBOTTOM) != 0) {
            fragment->midTextureMid = fragment->worldBottom + midHeight;
        } else {
            fragment->midTextureMid = fragment->worldTop;
        }
        fragment->midTextureMid += fragment->rowOffset;
    }

    if (fragment->hasBack) {
        if (fragment->wallTop) {
            if ((fragment->flags & DOOM_ML_DONTPEGTOP) != 0) {
                fragment->topTextureMid = fragment->worldTop;
            } else {
                /* back ceiling - view z is worldHigh. */
                fragment->topTextureMid = fragment->worldHigh + topHeight;
            }
            fragment->topTextureMid += fragment->rowOffset;
        }
        if (fragment->wallBottom) {
            if ((fragment->flags & DOOM_ML_DONTPEGBOTTOM) != 0) {
                fragment->bottomTextureMid = fragment->worldTop;
            } else {
                fragment->bottomTextureMid = fragment->worldLow;
            }
            fragment->bottomTextureMid += fragment->rowOffset;
        }
    }
}

static void store_wall_range(r_view_state_t* state, int start, int stop) {
    r_wall_fragment_t fragment;
    r_drawseg_t* drawseg;
    r_plane_error_t planeError;
    uint32_t mark;

    if (state->drawsegCount >= state->storage.drawsegCapacity) {
        state->drawsegOverflowed = 1;
        return;
    }

    mark = state->probe != NULL ? R_PROBE_NOW() : 0u;
    r_wall_fragment_open(state->map, &state->params, state->currentSeg, &fragment);
    wall_fragment_range(&fragment, &state->params, state->rwAngle1, start, stop);

    if (state->probe != NULL) {
        ++state->probe->wallRangeStores;
    }

    if (fragment.markCeiling) {
        const uint32_t checkStart = state->probe != NULL ? R_PROBE_NOW() : 0u;

        planeError = r_plane_check(&state->storage.planes, state->ceilingPlane,
                                   fragment.x1, fragment.x2, &state->ceilingPlane);
        if (state->probe != NULL) {
            state->probe->walkPlaneCheckCycle += R_PROBE_NOW() - checkStart;
        }
        if (planeError != R_PLANE_OK) {
            fail(state, R_BSP_ERR_VISPLANE_OVERFLOW);
            return;
        }
    }
    if (fragment.markFloor) {
        const uint32_t checkStart = state->probe != NULL ? R_PROBE_NOW() : 0u;

        planeError = r_plane_check(&state->storage.planes, state->floorPlane,
                                   fragment.x1, fragment.x2, &state->floorPlane);
        if (state->probe != NULL) {
            state->probe->walkPlaneCheckCycle += R_PROBE_NOW() - checkStart;
        }
        if (planeError != R_PLANE_OK) {
            fail(state, R_BSP_ERR_VISPLANE_OVERFLOW);
            return;
        }
    }

    r_wall_fragment_step(&fragment, fragment.scale1, fragment.scaleStep);
    {
        const uint32_t loopStart = state->probe != NULL ? R_PROBE_NOW() : 0u;

        render_seg_loop(state, &fragment);
        if (state->probe != NULL) {
            state->probe->walkSegLoopCycle += R_PROBE_NOW() - loopStart;
        }
    }

    drawseg = &state->storage.drawsegs[state->drawsegCount];
    drawseg->seg = state->currentSeg;
    drawseg->x1 = fragment.x1;
    drawseg->x2 = fragment.x2;
    drawseg->scale1 = fragment.scale1;
    drawseg->scale2 = fragment.scale2;
    drawseg->scaleStep = fragment.scaleStep;
    drawseg->silhouette = fragment.silhouette;
    drawseg->bottomSilhouette = fragment.bottomSilhouette;
    drawseg->topSilhouette = fragment.topSilhouette;
    drawseg->lightLevel = fragment.lightLevel;
    drawseg->lightNum = fragment.lightNum;
    drawseg->rwDistance = fragment.distance;
    drawseg->rwOffset = fragment.offset;
    drawseg->rwCenterAngle = fragment.centerAngle;
    /* D3.4 owns these three: r_wall_emit() fills them from the clip arrays as
     * its column loop leaves them. The walk stores the "none" value so a
     * drawseg that was never emitted cannot be read as a clip. */
    drawseg->sprTopClip = R_DRAWSEG_NO_BLOCK;
    drawseg->sprBottomClip = R_DRAWSEG_NO_BLOCK;
    drawseg->maskedTextureCol = R_DRAWSEG_NO_BLOCK;
    state->drawsegCount = (uint16_t)(state->drawsegCount + 1);
    if (state->probe != NULL) {
        state->probe->walkStoreCycle += R_PROBE_NOW() - mark;
    }
}

/* --- Wall clipping (r_bsp.c) -------------------------------------------- */

static void clip_solid_wall_segment(r_view_state_t* state, int first, int last) {
    r_clip_range_t* solid = state->storage.clipRanges;
    int start = 0;
    int next;
    int index;

    while (solid[start].last < first - 1) {
        ++start;
    }

    if (first < solid[start].first) {
        if (last < solid[start].first - 1) {
            store_wall_range(state, first, last);
            if (state->error != R_BSP_OK) {
                return;
            }
            if (state->clipRangeCount >= state->storage.clipRangeCapacity) {
                fail(state, R_BSP_ERR_CLIP_OVERFLOW);
                return;
            }
            for (index = (int)state->clipRangeCount; index > start; --index) {
                solid[index] = solid[index - 1];
            }
            solid[start].first = first;
            solid[start].last = last;
            state->clipRangeCount = (uint16_t)(state->clipRangeCount + 1);
            return;
        }
        store_wall_range(state, first, solid[start].first - 1);
        if (state->error != R_BSP_OK) {
            return;
        }
        solid[start].first = first;
    }

    if (last <= solid[start].last) {
        return;
    }

    next = start;
    while (last >= solid[next + 1].first - 1) {
        store_wall_range(state, solid[next].last + 1, solid[next + 1].first - 1);
        if (state->error != R_BSP_OK) {
            return;
        }
        ++next;
        if (last <= solid[next].last) {
            solid[start].last = solid[next].last;
            goto crunch;
        }
    }

    store_wall_range(state, solid[next].last + 1, last);
    if (state->error != R_BSP_OK) {
        return;
    }
    solid[start].last = last;

crunch:
    if (next == start) {
        return;
    }
    /* Drop solid[start+1 .. next]: `next`'s range is now start's tail. */
    for (index = next + 1; index < (int)state->clipRangeCount; ++index) {
        solid[start + 1 + (index - (next + 1))] = solid[index];
    }
    state->clipRangeCount = (uint16_t)(state->clipRangeCount - (next - start));
}

static void clip_pass_wall_segment(r_view_state_t* state, int first, int last) {
    const r_clip_range_t* solid = state->storage.clipRanges;
    int start = 0;

    while (solid[start].last < first - 1) {
        ++start;
    }

    if (first < solid[start].first) {
        if (last < solid[start].first - 1) {
            store_wall_range(state, first, last);
            return;
        }
        store_wall_range(state, first, solid[start].first - 1);
        if (state->error != R_BSP_OK) {
            return;
        }
    }

    if (last <= solid[start].last) {
        return;
    }

    while (last >= solid[start + 1].first - 1) {
        store_wall_range(state, solid[start].last + 1, solid[start + 1].first - 1);
        if (state->error != R_BSP_OK) {
            return;
        }
        ++start;
        if (last <= solid[start].last) {
            return;
        }
    }

    store_wall_range(state, solid[start].last + 1, last);
}

/* --- Segs and subsectors ------------------------------------------------- */

static void add_line(r_view_state_t* state, uint16_t segIndex) {
    doom_seg_t seg;
    doom_line_t line;
    doom_side_t sidedef;
    doom_vertex_t v1;
    doom_vertex_t v2;
    doom_sector_t front;
    doom_sector_t back;
    uint16_t frontIndex;
    uint16_t backIndex;
    int hasBack;
    angle_t angle1;
    angle_t angle2;
    angle_t span;
    angle_t tspan;
    int x1;
    int x2;

    if (state->error != R_BSP_OK) {
        return;
    }

    doom_map_get_seg(state->map, segIndex, &seg);
    doom_map_get_vertex(state->map, seg.v1, &v1);
    doom_map_get_vertex(state->map, seg.v2, &v2);

    if (state->probe != NULL) {
        ++state->probe->segVisits;
        state->probe->walkMapReads += 3u;
    }

    state->currentSeg = segIndex;
    angle1 = point_to_angle(&state->params, v1.x, v1.y);
    angle2 = point_to_angle(&state->params, v2.x, v2.y);

    span = angle1 - angle2;

    /* Back side. */
    if (span >= ANG180) {
        return;
    }

    state->rwAngle1 = angle1;
    angle1 -= state->params.angle;
    angle2 -= state->params.angle;

    tspan = angle1 + r_clip_angle;
    if (tspan > 2 * r_clip_angle) {
        tspan -= 2 * r_clip_angle;
        if (tspan >= span) {
            return;
        }
        angle1 = r_clip_angle;
    }

    tspan = r_clip_angle - angle2;
    if (tspan > 2 * r_clip_angle) {
        tspan -= 2 * r_clip_angle;
        if (tspan >= span) {
            return;
        }
        angle2 = (angle_t)(0u - r_clip_angle);
    }

    x1 = r_view_angle_to_x[(angle1 + ANG90) >> ANGLETOFINESHIFT];
    x2 = r_view_angle_to_x[(angle2 + ANG90) >> ANGLETOFINESHIFT];

    if (x1 == x2) {
        return;
    }

    doom_map_get_line(state->map, seg.linedef, &line);
    doom_map_get_side(state->map,
                      seg.side != 0 ? line.sideNum[1] : line.sideNum[0],
                      &sidedef);
    hasBack = doom_map_seg_sectors(state->map, &seg, &frontIndex, &backIndex);
    doom_map_get_sector(state->map, state->frontSector, &front);

    if (!hasBack) {
        clip_solid_wall_segment(state, x1, x2 - 1);
        return;
    }

    doom_map_get_sector(state->map, backIndex, &back);

    /* Closed door: the back sector's planes meet or cross the front's. */
    if (back.ceilingHeight <= front.floorHeight ||
        back.floorHeight >= front.ceilingHeight) {
        clip_solid_wall_segment(state, x1, x2 - 1);
        return;
    }

    /* Window: the two sides do not share both planes. */
    if (back.ceilingHeight != front.ceilingHeight ||
        back.floorHeight != front.floorHeight) {
        clip_pass_wall_segment(state, x1, x2 - 1);
        return;
    }

    /* Both sides are identical and there is no middle texture: the line exists
     * for a trigger, not for the eye. */
    if (back.ceilingPic == front.ceilingPic && back.floorPic == front.floorPic &&
        back.lightLevel == front.lightLevel && !r_has_name(sidedef.midTexture)) {
        return;
    }

    clip_pass_wall_segment(state, x1, x2 - 1);
}

static void render_subsector(r_view_state_t* state, uint16_t number) {
    doom_subsector_t subsector;
    doom_sector_t sector;
    uint16_t sectorIndex;
    uint16_t index;
    uint32_t subsectorStart;

    if (state->error != R_BSP_OK) {
        return;
    }

    subsectorStart = state->probe != NULL ? R_PROBE_NOW() : 0u;
    if (state->probe != NULL) {
        ++state->probe->subsectorVisits;
    }
    doom_map_get_subsector(state->map, number, &subsector);
    sectorIndex = doom_map_subsector_sector(state->map, &subsector);
    state->frontSector = sectorIndex;
    doom_map_get_sector(state->map, sectorIndex, &sector);

    {
        const uint32_t planeStart = R_PROBE_HAS(state) ? R_PROBE_NOW() : 0u;

        if (sector.floorHeight < state->params.z) {
            if (r_plane_find(&state->storage.planes, sector.floorHeight,
                             sector.floorPic, sector.lightLevel, state->params.skyPicNum,
                             &state->floorPlane) != R_PLANE_OK) {
                fail(state, R_BSP_ERR_VISPLANE_OVERFLOW);
                return;
            }
        } else {
            state->floorPlane = NULL;
        }

        if (sector.ceilingHeight > state->params.z ||
            sector.ceilingPic == state->params.skyPicNum) {
            if (r_plane_find(&state->storage.planes, sector.ceilingHeight,
                             sector.ceilingPic, sector.lightLevel,
                             state->params.skyPicNum, &state->ceilingPlane) != R_PLANE_OK) {
                fail(state, R_BSP_ERR_VISPLANE_OVERFLOW);
                return;
            }
        } else {
            state->ceilingPlane = NULL;
        }

        if (R_PROBE_HAS(state)) {
            r_raster_probe_t* probe = R_PROBE_OF(state);

            probe->planeFindCycle += R_PROBE_NOW() - planeStart;
            probe->planeFindCalls += 2u;
        }
    }

    for (index = 0; index < subsector.numSegs; ++index) {
        const uint32_t lineStart = state->probe != NULL ? R_PROBE_NOW() : 0u;

        add_line(state, (uint16_t)(subsector.firstSeg + index));
        if (state->probe != NULL) {
            state->probe->walkLineCycle += R_PROBE_NOW() - lineStart;
        }
        if (state->error != R_BSP_OK) {
            return;
        }
    }
    if (state->probe != NULL) {
        state->probe->walkSubsectorCycle += R_PROBE_NOW() - subsectorStart;
        state->probe->walkMapReads += 2u + (uint32_t)subsector.numSegs;
    }
}

static void render_bsp_node(r_view_state_t* state, int bspnum, int depth) {
    doom_node_t node;
    int side;

    if (state->error != R_BSP_OK) {
        return;
    }
    if (depth > R_BSP_MAX_DEPTH) {
        fail(state, R_BSP_ERR_DEPTH);
        return;
    }

    if ((bspnum & (int)R_NF_SUBSECTOR) != 0) {
        if (bspnum == -1) {
            render_subsector(state, 0);
        } else {
            render_subsector(state, (uint16_t)(bspnum & ~(int)R_NF_SUBSECTOR));
        }
        return;
    }

    doom_map_get_node(state->map, (uint16_t)bspnum, &node);
    side = point_on_side(state->params.x, state->params.y, &node);

    if (state->probe != NULL) {
        ++state->probe->nodeVisits;
        ++state->probe->walkMapReads;
    }

    render_bsp_node(state, node.children[side], depth + 1);
    if (state->error != R_BSP_OK) {
        return;
    }

    if (check_bbox(state, node.bbox[side ^ 1])) {
        render_bsp_node(state, node.children[side ^ 1], depth + 1);
    }
}

/* --- Public API ---------------------------------------------------------- */

r_bsp_error_t r_bsp_init(r_view_state_t* state, const doom_map_t* map,
                         const r_view_storage_t* storage) {
    if (state == NULL) {
        return R_BSP_ERR_NULL_STATE;
    }
    *state = (r_view_state_t){0};
    if (map == NULL) {
        state->error = R_BSP_ERR_NULL_MAP;
        return state->error;
    }
    if (map->open == 0) {
        state->error = R_BSP_ERR_MAP_NOT_OPEN;
        return state->error;
    }
    if (storage == NULL) {
        state->error = R_BSP_ERR_NULL_STORAGE;
        return state->error;
    }
    if (storage->drawsegs == NULL || storage->drawsegCapacity == 0) {
        state->error = R_BSP_ERR_NULL_DRAWSEGS;
        return state->error;
    }
    if (storage->planes.planes == NULL || storage->planes.capacity == 0) {
        state->error = R_BSP_ERR_NULL_PLANES;
        return state->error;
    }
    if (storage->clipRanges == NULL) {
        state->error = R_BSP_ERR_NULL_CLIPS;
        return state->error;
    }
    if (storage->clipRangeCapacity < R_MAX_CLIP_SEGS) {
        state->error = R_BSP_ERR_SMALL_CLIPS;
        return state->error;
    }
    if (storage->floorClip == NULL || storage->ceilingClip == NULL) {
        state->error = R_BSP_ERR_NULL_CLIP_ARRAYS;
        return state->error;
    }

    state->map = map;
    state->storage = *storage;
    state->storage.planes.count = 0;
    state->error = R_BSP_OK;
    return R_BSP_OK;
}

r_bsp_error_t r_bsp_render(r_view_state_t* state, const r_view_params_t* params) {
    int column;

    if (state == NULL) {
        return R_BSP_ERR_NULL_STATE;
    }
    if (params == NULL) {
        state->error = R_BSP_ERR_NULL_PARAMS;
        return state->error;
    }
    if (state->map == NULL || state->map->open == 0) {
        state->error = R_BSP_ERR_MAP_NOT_OPEN;
        return state->error;
    }

    state->error = R_BSP_OK;
    state->drawsegCount = 0;
    state->drawsegOverflowed = 0;
    state->clipCaptured = 0;
    state->clipBlocksUsed = 0;
    state->openingBlocksUsed = 0;
    state->params = *params;
    r_plane_clear(&state->storage.planes);
    clear_clip_segs(state);

    for (column = 0; column < R_SCREEN_WIDTH; ++column) {
        state->storage.floorClip[column] = (int16_t)R_VIEW_HEIGHT;
        state->storage.ceilingClip[column] = -1;
    }

    if (state->map->lumps[DOOM_MAP_NODES].count == 0) {
        render_subsector(state, 0);
    } else {
        render_bsp_node(state, (int)state->map->lumps[DOOM_MAP_NODES].count - 1, 0);
    }

    return state->error;
}
r_bsp_error_t r_bsp_point_in_subsector(const doom_map_t* map, fixed_t x, fixed_t y,
                                       uint16_t* out) {
    int nodenum;
    int depth = 0;

    if (map == NULL || map->open == 0) {
        return R_BSP_ERR_MAP_NOT_OPEN;
    }
    if (out == NULL) {
        return R_BSP_ERR_NULL_OUT;
    }

    if (map->lumps[DOOM_MAP_NODES].count == 0) {
        *out = 0;
        return R_BSP_OK;
    }

    nodenum = (int)map->lumps[DOOM_MAP_NODES].count - 1;
    while ((nodenum & (int)R_NF_SUBSECTOR) == 0) {
        doom_node_t node;

        if (++depth > R_BSP_MAX_DEPTH) {
            return R_BSP_ERR_DEPTH;
        }
        doom_map_get_node(map, (uint16_t)nodenum, &node);
        nodenum = node.children[point_on_side(x, y, &node)];
    }

    nodenum &= ~(int)R_NF_SUBSECTOR;
    if ((uint32_t)nodenum >= map->lumps[DOOM_MAP_SSECTORS].count) {
        return R_BSP_ERR_SUBSECTOR_RANGE;
    }
    *out = (uint16_t)nodenum;
    return R_BSP_OK;
}

r_bsp_error_t r_bsp_spawn_view(const doom_map_t* map, int playerIndex,
                               r_view_params_t* out) {
    doom_thing_t thing;
    doom_thing_t start;
    uint32_t index;
    int found = 0;
    uint16_t subsector;
    uint16_t sectorIndex;
    doom_subsector_t sub;
    doom_sector_t sector;
    r_bsp_error_t result;

    if (map == NULL || map->open == 0) {
        return R_BSP_ERR_MAP_NOT_OPEN;
    }
    if (out == NULL) {
        return R_BSP_ERR_NULL_OUT;
    }
    if (playerIndex < 0 || playerIndex > 3) {
        return R_BSP_ERR_NO_PLAYER_START;
    }

    for (index = 0; index < map->lumps[DOOM_MAP_THINGS].count; ++index) {
        doom_map_get_thing(map, index, &thing);
        if (thing.type == playerIndex + 1) {
            start = thing;
            found = 1;
        }
    }
    if (!found) {
        return R_BSP_ERR_NO_PLAYER_START;
    }

    out->x = (fixed_t)((uint32_t)(uint16_t)start.x << FRACBITS);
    out->y = (fixed_t)((uint32_t)(uint16_t)start.y << FRACBITS);
    out->angle = ANG45 * (uint32_t)(start.angle / 45);
    out->extralight = 0;
    out->skyPicNum = r_pack_name(kSkyName);

    result = r_bsp_point_in_subsector(map, out->x, out->y, &subsector);
    if (result != R_BSP_OK) {
        return result;
    }
    doom_map_get_subsector(map, subsector, &sub);
    sectorIndex = doom_map_subsector_sector(map, &sub);
    doom_map_get_sector(map, sectorIndex, &sector);
    out->z = sector.floorHeight + R_VIEWHEIGHT;
    return R_BSP_OK;
}
