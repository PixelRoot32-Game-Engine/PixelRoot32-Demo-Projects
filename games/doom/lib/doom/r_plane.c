/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * Visplane bookkeeping of the DOOM port (r_plane.h).
 *
 * This is upstream's R_FindPlane()/R_CheckPlane(), with the arrays moved into
 * the caller and the I_Error() overflow turned into a return code. The
 * algorithms are otherwise line for line: the same key comparison, the same
 * sky collapse, the same intersection scan that decides between widening a
 * plane and splitting a new one.
 *
 * The second half is upstream's R_DrawPlanes()/R_MapPlane()/R_MakeSpans() and
 * R_DrawSpan() (r_plane.c + r_draw.c): the span maker, the two span tables, the
 * plane light lookup and the sky column path. r_plane.h states every rule and
 * the two deliberate divergences (no pad words on r_visplane_t, and the sky
 * texture's row wrap following the stored column's mask rather than a literal
 * & 127).
 *
 * Both arrays are filled with R_PLANE_EMPTY when a plane is allocated. Upstream
 * fills only top[]; filling bottom[] too makes a dumped plane deterministic
 * without changing a pixel, because no consumer reads a column whose top is
 * empty. r_plane.h states the reasoning.
 *
 * This is C99 only: no allocation, no globals, no stdio, no floating point.
 */
#include <stddef.h>

#include "r_plane.h"

#include "r_bsp.h"
#include "r_data.h"
#include "r_segs.h"
#include "r_tables.h"

void r_plane_clear(r_plane_set_t* set) {
    if (set != NULL) {
        set->count = 0;
    }
}

r_plane_error_t r_plane_find(r_plane_set_t* set, fixed_t height, r_name_t picNum,
                             int16_t lightLevel, r_name_t skyPicNum,
                             r_visplane_t** out) {
    r_visplane_t* check;
    uint16_t index;

    if (set == NULL) {
        return R_PLANE_ERR_NULL_SET;
    }
    if (set->planes == NULL) {
        return R_PLANE_ERR_NULL_PLANES;
    }
    if (out == NULL) {
        return R_PLANE_ERR_NULL_OUT;
    }

    /* All sky flats share one plane, whatever their sector height or light. */
    if (picNum == skyPicNum) {
        height = 0;
        lightLevel = 0;
    }

    for (index = 0; index < set->count; ++index) {
        check = &set->planes[index];
        if (check->height == height && check->picNum == picNum &&
            check->lightLevel == lightLevel) {
            *out = check;
            return R_PLANE_OK;
        }
    }

    if (set->count >= set->capacity) {
        return R_PLANE_ERR_NO_ROOM;
    }

    check = &set->planes[set->count++];
    check->height = height;
    check->picNum = picNum;
    check->lightLevel = lightLevel;
    check->minX = R_SCREEN_WIDTH;
    check->maxX = -1;

    for (index = 0; index < R_SCREEN_WIDTH; ++index) {
        check->top[index] = R_PLANE_EMPTY;
        check->bottom[index] = R_PLANE_EMPTY;
    }

    *out = check;
    return R_PLANE_OK;
}

r_plane_error_t r_plane_check(r_plane_set_t* set, r_visplane_t* plane, int start,
                              int stop, r_visplane_t** out) {
    r_visplane_t* split;
    int intrl;
    int intrh;
    int unionl;
    int unionh;
    int x;

    if (set == NULL) {
        return R_PLANE_ERR_NULL_SET;
    }
    if (set->planes == NULL) {
        return R_PLANE_ERR_NULL_PLANES;
    }
    if (plane == NULL) {
        return R_PLANE_ERR_NULL_PLANE;
    }
    if (out == NULL) {
        return R_PLANE_ERR_NULL_OUT;
    }
    if (start < 0 || stop >= R_SCREEN_WIDTH || start > stop) {
        return R_PLANE_ERR_RANGE;
    }

    if (start < plane->minX) {
        intrl = plane->minX;
        unionl = start;
    } else {
        unionl = plane->minX;
        intrl = start;
    }

    if (stop > plane->maxX) {
        intrh = plane->maxX;
        unionh = stop;
    } else {
        unionh = plane->maxX;
        intrh = stop;
    }

    /* If the intersection of the two ranges holds no recorded column, the run
     * is disjoint from this plane and can share it. */
    for (x = intrl; x <= intrh; ++x) {
        if (plane->top[x] != R_PLANE_EMPTY) {
            break;
        }
    }

    if (x > intrh) {
        plane->minX = (int16_t)unionl;
        plane->maxX = (int16_t)unionh;
        *out = plane;
        return R_PLANE_OK;
    }

    if (set->count >= set->capacity) {
        return R_PLANE_ERR_NO_ROOM;
    }

    split = &set->planes[set->count++];
    split->height = plane->height;
    split->picNum = plane->picNum;
    split->lightLevel = plane->lightLevel;
    split->minX = (int16_t)start;
    split->maxX = (int16_t)stop;

    for (x = 0; x < R_SCREEN_WIDTH; ++x) {
        split->top[x] = R_PLANE_EMPTY;
        split->bottom[x] = R_PLANE_EMPTY;
    }

    *out = split;
    return R_PLANE_OK;
}

/* --- The span emitter (R_DrawPlanes) ------------------------------------- */

/*
 * THE TWO SHAPES THE PASS VALIDATES BEFORE IT WRITES
 *
 * A flat is exactly R_FLAT_SIZE square: the span loop's wrap is R_DrawSpan's
 * own `& 63` and `& (63*64)`, so a flat of any other size cannot be addressed by
 * the upstream rule and is a shape error rather than a silent read past the
 * block.
 */
static int flat_is_usable(const r_flat_t* flat) {
    if (flat == NULL || flat->pixels == NULL) {
        return 0;
    }
    if (flat->width != R_FLAT_SIZE || flat->height != R_FLAT_SIZE) {
        return 0;
    }
    return flat->widthMask == R_FLAT_SIZE - 1 && flat->heightMask == R_FLAT_SIZE - 1;
}

/*
 * A sky texture is a wall texture, but the sky column loop reads only its two
 * masks: it never pegs by the declared height and never scales by it, because
 * R_DrawPlanes' sky branch sets dc_texturemid to skytexturemid and dc_iscale to
 * pspriteiscale instead. Its requirements are therefore strictly narrower than
 * r_segs.c's texture_is_usable(): both masks must be 2^k - 1 (the column
 * indexing is an AND), and the width mask must actually select a column inside
 * the texture. A sky whose stored column is shorter than its declared height is
 * drawable here, which is what vanilla's hard-coded `& 127` does too.
 */
static int sky_is_usable(const r_walltex_t* sky) {
    if (sky == NULL || sky->pixels == NULL) {
        return 0;
    }
    if (sky->width < 1 || sky->widthMask < 0 || sky->widthMask >= sky->width) {
        return 0;
    }
    if (((int)sky->widthMask & ((int)sky->widthMask + 1)) != 0) {
        return 0;
    }
    if (sky->heightMask < 0 || ((int)sky->heightMask & ((int)sky->heightMask + 1)) != 0) {
        return 0;
    }
    return 1;
}

/* A plane is the sky plane when the frame named a sky and this plane carries its
 * picture. r_plane_find() stores every sky plane as (0, skyPicNum, 0), so this
 * is the same identity R_DrawPlanes compares. */
static int is_sky_plane(const r_view_state_t* state, const r_visplane_t* plane) {
    return state->params.skyPicNum != R_PLANE_NO_SKY
           && plane->picNum == state->params.skyPicNum;
}

/* `planeheight = abs(pl->height - viewz)` with the difference taken in 64 bits
 * so a corrupt pair of extremes cannot overflow. */
static fixed_t plane_height(fixed_t height, fixed_t viewz) {
    int64_t difference = (int64_t)height - (int64_t)viewz;

    if (difference < 0) {
        difference = -difference;
    }
    return difference > INT32_MAX ? INT32_MAX : (fixed_t)difference;
}

/*
 * R_ClearPlanes()'s two view-relative scales:
 *
 *     angle      = (viewangle - ANG90) >> ANGLETOFINESHIFT
 *     basexscale = FixedDiv(finecosine[angle], centerxfrac)
 *     baseyscale = -FixedDiv(finesine[angle], centerxfrac)
 *
 * They are computed once per plane pass rather than once per span. The two
 * table reads are the same ones upstream's finecosine/finesine pointers make:
 * finecosine[i] is finesine[i + FINEANGLES/4].
 */
static void plane_base_scales(const r_view_params_t* view, fixed_t* baseXScale,
                              fixed_t* baseYScale) {
    const unsigned index = (unsigned)((view->angle - ANG90) >> ANGLETOFINESHIFT);
    const fixed_t centerX = ((fixed_t)(R_SCREEN_WIDTH / 2)) << FRACBITS;

    *baseXScale = fixed_div(r_fine_sine[index + FINEANGLES / 4], centerX);
    *baseYScale = -fixed_div(r_fine_sine[index], centerX);
}

/*
 * R_MapPlane() + R_DrawSpan(): one horizontal run of one plane at screen row y,
 * from column x1 to x2 inclusive.
 *
 * The per-row distance is recomputed instead of cached in upstream's
 * cachedheight/cacheddistance/cachedxstep/cachedystep arrays. That cache is pure
 * memoisation keyed on planeheight, and recomputing the same FixedMul products
 * from the same inputs is bit-identical; it costs three multiplications per span
 * and saves 4 * R_VIEW_HEIGHT bytes of per-view state.
 */
static void map_plane(const r_view_state_t* state, r_draw_surface_t* surface,
                      const r_flat_t* flat, const r_light_t* light,
                      fixed_t planeHeight, fixed_t baseXScale, fixed_t baseYScale,
                      int lightRow, int y, int x1, int x2) {
    fixed_t distance;
    fixed_t xstep;
    fixed_t ystep;
    fixed_t length;
    fixed_t xfrac;
    fixed_t yfrac;
    unsigned index;
    unsigned z;
    const uint8_t* colormap;
    int x;
    const uint32_t setupStart = R_PROBE_HAS(state) ? R_PROBE_NOW() : 0u;

    /* A guard on a caller's corrupt plane, not a behaviour: the clip arrays keep
     * every real span inside the view, exactly as draw_column's row clamp does
     * for the wall pass. */
    if (y < 0 || y >= R_VIEW_HEIGHT || x1 < 0 || x2 >= R_SCREEN_WIDTH || x1 > x2) {
        return;
    }

    distance = fixed_mul(planeHeight, r_plane_yslope[y]);
    xstep = fixed_mul(distance, baseXScale);
    ystep = fixed_mul(distance, baseYScale);
    length = fixed_mul(distance, r_plane_dist_scale[x1]);

    index = (unsigned)((state->params.angle + r_x_to_view_angle[x1]) >> ANGLETOFINESHIFT);
    xfrac = state->params.x + fixed_mul(r_fine_sine[index + FINEANGLES / 4], length);
    yfrac = -state->params.y - fixed_mul(r_fine_sine[index], length);

    z = (unsigned)(distance >> R_PLANE_LIGHT_Z_SHIFT);
    if (z >= R_PLANE_LIGHT_Z_LEVELS) {
        z = R_PLANE_LIGHT_Z_LEVELS - 1;
    }
    colormap = light->colormaps
               + (size_t)r_plane_light[lightRow][z] * (size_t)R_LIGHT_MAP_BYTES;

    if (R_PROBE_HAS(state)) {
        R_PROBE_OF(state)->planeSetupCycle += R_PROBE_NOW() - setupStart;
    }

    {
        uint8_t* destination =
            surface->pixels + (size_t)x1 + (size_t)y * (size_t)surface->width;
        uint8_t* maskAt = surface->planeMask != NULL
                              ? surface->planeMask + (size_t)x1
                                    + (size_t)y * (size_t)surface->width
                              : NULL;
        /* The coverage record is written where the pixel is (D3.9). The separate
         * pass over [x1, x2] that used to follow the loop set the same bytes --
         * every column a span covers gets 1 -- but it re-walked a 320-byte array
         * the pixel writes had just evicted, and it cost 1,276 us a frame for
         * 29,870 one-byte stores.
         *
         * The flat's bytes and the two optional records are hoisted out of the
         * loop, and the case a frame actually runs (a flat and the coverage
         * record, no per-pixel attribution mask) has its own loop: the two
         * pointers are tests per span instead of tests per pixel. Both forms
         * write exactly the same bytes. */
        const uint8_t* flatPixels = flat->pixels;
        uint8_t* coverAt = surface->covered != NULL ? surface->covered + (size_t)x1 : NULL;
        const uint32_t pixelStart = R_PROBE_HAS(state) ? R_PROBE_NOW() : 0u;

        if (maskAt == NULL && coverAt != NULL) {
            for (x = x1; x <= x2; ++x) {
                const int spot = (int)(((yfrac >> (FRACBITS - 6)) & (63 * 64))
                                       + ((xfrac >> FRACBITS) & 63));

                *destination++ = colormap[flatPixels[spot]];
                *coverAt++ = 1u;
                xfrac += xstep;
                yfrac += ystep;
            }
        } else {
            for (x = x1; x <= x2; ++x) {
                /* R_DrawSpan's spot: the v coordinate is the high six bits of yfrac
                 * and the u coordinate the low six of xfrac, both wrapped inside the
                 * 64 x 64 square by the masks the format hard-codes. */
                const int spot = (int)(((yfrac >> (FRACBITS - 6)) & (63 * 64))
                                       + ((xfrac >> FRACBITS) & 63));

                *destination++ = colormap[flatPixels[spot]];
                if (maskAt != NULL) {
                    *maskAt++ = 1u;
                }
                if (coverAt != NULL) {
                    *coverAt++ = 1u;
                }
                xfrac += xstep;
                yfrac += ystep;
            }
        }
        if (R_PROBE_HAS(state)) {
            R_PROBE_OF(state)->planePixelCycle += R_PROBE_NOW() - pixelStart;
            ++R_PROBE_OF(state)->planeSpans;
        }
    }
    surface->planePixels += (uint32_t)(x2 - x1 + 1);
    ++surface->planeSpans;
}

/*
 * R_MakeSpans()'s state machine, with the two no-pad edge columns passed as
 * (R_PLANE_EMPTY, 0) instead of being written into a pad byte.
 *
 * WHY THE BOUNDARY TEST CANNOT CHANGE A PIXEL
 *
 * Upstream writes top[maxx+1] = top[minx-1] = 0xff before the walk. The only
 * thing that write can influence is the comparison `top[x] == 0xff`, because
 * every loop below reads a top first: a span is drawn for rows t..b with t <= b,
 * and t is the top. For an outside column the edge test produces exactly that
 * comparison result (top == R_PLANE_EMPTY), so the same loops run and the same
 * rows are closed or opened.
 *
 * bottom[] is different: upstream does not write it outside [minx, maxx], and
 * its array is zero-initialised, so the state machine sees 0 there. This port
 * fills bottom[] with 0xFF (r_plane.h's byte-determinism note), and feeding that
 * 0xFF to the `b1 > b2 && b1 >= t1` test would fire for row 255, which does not
 * exist. The sides are therefore normalised to bottom = 0 whenever top is
 * R_PLANE_EMPTY -- inside the range too, because an empty column's pair is
 * always (0xFF, 0xFF) here. That is the value upstream's zeroed array supplies,
 * and it is what keeps the state machine's four comparisons identical.
 */
static void make_spans(const r_view_state_t* state, r_draw_surface_t* surface,
                       const r_flat_t* flat, const r_light_t* light,
                       fixed_t planeHeight, fixed_t baseXScale, fixed_t baseYScale,
                       int lightRow, int* spanStart, int x,
                       int t1, int b1, int t2, int b2) {
    if (t1 == R_PLANE_EMPTY) {
        b1 = 0;
    }
    if (t2 == R_PLANE_EMPTY) {
        b2 = 0;
    }

    while (t1 < t2 && t1 <= b1) {
        map_plane(state, surface, flat, light, planeHeight, baseXScale, baseYScale,
                  lightRow, t1, spanStart[t1], x - 1);
        ++t1;
    }
    while (b1 > b2 && b1 >= t1) {
        map_plane(state, surface, flat, light, planeHeight, baseXScale, baseYScale,
                  lightRow, b1, spanStart[b1], x - 1);
        --b1;
    }
    while (t2 < t1 && t2 <= b2) {
        spanStart[t2] = x;
        ++t2;
    }
    while (b2 > b1 && b2 >= t2) {
        spanStart[b2] = x;
        --b2;
    }
}

/* One non-sky plane: R_DrawPlanes' regular-flat branch. */
static void draw_flat_plane(const r_view_state_t* state, r_draw_surface_t* surface,
                            const r_flat_t* flat, const r_light_t* light,
                            const r_visplane_t* plane, fixed_t baseXScale,
                            fixed_t baseYScale, int lightRow) {
    const fixed_t height = plane_height(plane->height, state->params.z);
    int spanStart[R_VIEW_HEIGHT];
    int x;

    /* Upstream's spanstart[] is a global that starts zeroed once. Every run this
     * pass draws begins at a column it has already visited, so the zeros are
     * never read as a real start; initialising them keeps a corrupt plane from
     * reading uninitialised stack. */
    for (x = 0; x < R_VIEW_HEIGHT; ++x) {
        spanStart[x] = 0;
    }

    /* `stop = pl->maxx + 1`, so the last iteration visits the empty column that
     * closes every still-open run. */
    for (x = plane->minX; x <= (int)plane->maxX + 1; ++x) {
        const int t1 = x > plane->minX ? (int)plane->top[x - 1] : R_PLANE_EMPTY;
        const int b1 = x > plane->minX ? (int)plane->bottom[x - 1] : 0;
        const int t2 = x <= (int)plane->maxX ? (int)plane->top[x] : R_PLANE_EMPTY;
        const int b2 = x <= (int)plane->maxX ? (int)plane->bottom[x] : 0;

        make_spans(state, surface, flat, light, height, baseXScale, baseYScale,
                   lightRow, spanStart, x, t1, b1, t2, b2);
    }
}

/*
 * R_DrawPlanes' sky branch: one R_DrawColumn per marked column, from the sky
 * texture, full bright.
 *
 * dc_iscale is pspriteiscale >> detailshift. R_ExecuteSetViewSize() sets
 * pspriteiscale = FRACUNIT * SCREENWIDTH / viewwidth; this port renders the
 * full width, so it is FRACUNIT and R_PLANE_SKY_ISCALE says so.
 * dc_texturemid is skytexturemid = 100 * FRACUNIT (R_InitSkyMap), which is
 * R_CENTER_Y_FRAC here: the horizon row shows texture row 100 and each screen
 * row advances one texel. The column is
 * (viewangle + xtoviewangle[x]) >> ANGLETOSKYSHIFT masked by the sky's width
 * mask; R_GetColumn applies that mask, and the row wrap is the stored column's
 * height mask exactly as R_DrawColumn's `& 127` is in vanilla.
 *
 * Upstream's sky column reads through dc_colormap = colormaps, i.e. COLORMAP
 * table 0. Table 0 is the identity map in every IWAD -- it is the unmodified
 * palette -- and this port does not even look it up: the texel it would return
 * is the texel already in hand, so the sky is a direct store. That is also why
 * the sky is never darkened by INVUL or by the plane light.
 */
static void draw_sky_plane(const r_view_state_t* state, r_draw_surface_t* surface,
                           const r_walltex_t* sky, const r_visplane_t* plane) {
    int x;

    for (x = plane->minX; x <= plane->maxX; ++x) {
        int yl = (int)plane->top[x];
        int yh = (int)plane->bottom[x];
        unsigned angle;
        const uint8_t* column;
        fixed_t frac;
        int row;

        if (yl == R_PLANE_EMPTY || yl > yh) {
            continue;
        }
        if (yl < 0) {
            yl = 0;
        }
        if (yh > R_VIEW_HEIGHT - 1) {
            yh = R_VIEW_HEIGHT - 1;
        }
        if (yl > yh) {
            continue;
        }

        angle = (unsigned)((state->params.angle + r_x_to_view_angle[x])
                           >> R_PLANE_SKY_ANGLE_SHIFT);
        column = sky->pixels
                 + (size_t)(angle & (unsigned)sky->widthMask)
                       * (size_t)(sky->heightMask + 1);
        frac = R_PLANE_SKY_TEXTURE_MID
               + (fixed_t)(yl - R_CENTER_Y) * R_PLANE_SKY_ISCALE;

        {
            const int stride = surface->width;
            uint8_t* destination =
                surface->pixels + (size_t)x + (size_t)yl * (size_t)stride;
            uint8_t* maskAt = surface->planeMask != NULL
                                  ? surface->planeMask + (size_t)x
                                        + (size_t)yl * (size_t)stride
                                  : NULL;

            for (row = yl; row <= yh; ++row) {
                *destination = column[(frac >> FRACBITS) & sky->heightMask];
                if (maskAt != NULL) {
                    *maskAt = 1u;
                    maskAt += (size_t)stride;
                }
                destination += (size_t)stride;
                frac += R_PLANE_SKY_ISCALE;
            }
        }
        if (surface->covered != NULL) {
            surface->covered[x] = 1u;
        }
        surface->planePixels += (uint32_t)(yh - yl + 1);
    }
}

/* Resolves and validates every drawable plane's flat (or sky texture) before a
 * byte is written. Anything that fails here leaves the frame untouched.
 *
 * A flat already proved in this call is not resolved again: up to 22 planes
 * share 6 distinct flats on the sampled views, and the second resolve used to
 * decode the same 4,096 bytes for every plane that used it. One proof per name
 * is enough because the flat resolver must answer consistently for the call,
 * and the draw loop resolves the flat again before it reads it anyway. */
#define R_PLANE_PREFLIGHT_MEMO 8

static int flat_proven(r_name_t name, r_name_t* proven, int provenCount) {
    int index;

    for (index = 0; index < provenCount; ++index) {
        if (proven[index] == name) {
            return 1;
        }
    }
    return 0;
}

static r_plane_error_t preflight_planes(const r_view_state_t* state,
                                        const struct r_tex_source* textures) {
    unsigned index;
    r_name_t proven[R_PLANE_PREFLIGHT_MEMO];
    int provenCount = 0;

    for (index = 0; index < state->storage.planes.count; ++index) {
        const r_visplane_t* plane = &state->storage.planes.planes[index];

        if (plane->minX > plane->maxX) {
            continue;
        }
        if (plane->minX < 0 || plane->maxX >= R_SCREEN_WIDTH) {
            return R_PLANE_ERR_PLANE_RANGE;
        }
        if (textures->beginFragment != NULL) {
            textures->beginFragment(textures->context);
        }
        if (is_sky_plane(state, plane)) {
            const r_walltex_t* sky;

            if (textures->skyTexture == R_NAME_NONE) {
                return R_PLANE_ERR_SKY_MISSING;
            }
            if (textures->resolve == NULL) {
                return R_PLANE_ERR_NULL_RESOLVE;
            }
            sky = textures->resolve(textures->context, textures->skyTexture);
            if (sky == NULL) {
                return R_PLANE_ERR_SKY_MISSING;
            }
            if (!sky_is_usable(sky)) {
                return R_PLANE_ERR_SKY_SHAPE;
            }
        } else {
            const r_flat_t* flat;

            if (flat_proven(plane->picNum, proven, provenCount)) {
                continue;
            }
            flat = textures->resolveFlat(textures->flatContext, plane->picNum);
            if (flat == NULL) {
                return R_PLANE_ERR_FLAT_MISSING;
            }
            if (!flat_is_usable(flat)) {
                return R_PLANE_ERR_FLAT_SHAPE;
            }
            if (provenCount < R_PLANE_PREFLIGHT_MEMO) {
                proven[provenCount] = plane->picNum;
                ++provenCount;
            }
        }
    }
    return R_PLANE_OK;
}

r_plane_error_t r_plane_draw(struct r_view_state* state, r_draw_surface_t* surface,
                             const struct r_tex_source* textures,
                             const r_light_t* light) {
    r_plane_error_t result;
    fixed_t baseXScale;
    fixed_t baseYScale;
    unsigned index;

    if (state == NULL) {
        return R_PLANE_ERR_NULL_STATE;
    }
    if (state->map == NULL || state->map->open == 0) {
        return R_PLANE_ERR_MAP_NOT_OPEN;
    }
    if (surface == NULL) {
        return R_PLANE_ERR_NULL_SURFACE;
    }
    if (surface->pixels == NULL) {
        return R_PLANE_ERR_SURFACE_PIXELS;
    }
    if (surface->width < R_SCREEN_WIDTH || surface->height < R_VIEW_HEIGHT) {
        return R_PLANE_ERR_SURFACE_SIZE;
    }
    if (textures == NULL) {
        return R_PLANE_ERR_NULL_SOURCE;
    }
    if (textures->resolveFlat == NULL) {
        return R_PLANE_ERR_NULL_RESOLVE;
    }
    if (light == NULL || light->colormaps == NULL) {
        return R_PLANE_ERR_NULL_LIGHT;
    }

    /* Nothing is written until every flat and the sky texture are resident and
     * well formed. */
    {
        const uint32_t preflightStart = R_PROBE_HAS(state) ? R_PROBE_NOW() : 0u;

        result = preflight_planes(state, textures);
        if (R_PROBE_HAS(state)) {
            R_PROBE_OF(state)->planePreflightCycle += R_PROBE_NOW() - preflightStart;
        }
    }
    if (result != R_PLANE_OK) {
        return result;
    }

    plane_base_scales(&state->params, &baseXScale, &baseYScale);

    surface->planePixels = 0u;
    surface->planeSpans = 0u;
    if (surface->planeMask != NULL) {
        size_t at;

        for (at = 0; at < (size_t)surface->width * (size_t)surface->height; ++at) {
            surface->planeMask[at] = 0u;
        }
    }

    for (index = 0; index < state->storage.planes.count; ++index) {
        const r_visplane_t* plane = &state->storage.planes.planes[index];
        const uint32_t planeStart = R_PROBE_HAS(state) ? R_PROBE_NOW() : 0u;

        if (plane->minX > plane->maxX) {
            continue;
        }
        if (textures->beginFragment != NULL) {
            textures->beginFragment(textures->context);
        }
        if (is_sky_plane(state, plane)) {            /* Preflight proved this resolves and is usable; a resolver that
             * answers differently between the two calls breaks the
             * r_tex_source_t contract, and the plane is skipped rather than
             * dereferenced. */
            const r_walltex_t* sky = textures->resolve(textures->context,
                                                       textures->skyTexture);

            if (sky != NULL && sky_is_usable(sky)) {
                draw_sky_plane(state, surface, sky, plane);
            }
        } else {
            const r_flat_t* flat =
                textures->resolveFlat(textures->flatContext, plane->picNum);

            if (flat != NULL && flat_is_usable(flat)) {
                const int lightRow =
                    r_light_row((int)(plane->lightLevel >> R_LIGHT_SEG_SHIFT)
                                + (int)state->params.extralight);

                draw_flat_plane(state, surface, flat, light, plane, baseXScale,
                                baseYScale, lightRow);
            }
        }
        if (R_PROBE_HAS(state)) {
            R_PROBE_OF(state)->planeLoopCycle += R_PROBE_NOW() - planeStart;
        }
    }
    return R_PLANE_OK;
}
