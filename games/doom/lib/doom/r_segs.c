/*
 * The wall column emitter of the DOOM port (r_segs.h), from upstream's
 * R_RenderSegLoop() in r_segs.c and R_DrawColumn() in r_draw.c.
 *
 * WHAT IS PORTED, AND FROM WHERE
 *
 *   R_RenderSegLoop()        the whole column loop: yl/yh from the 12.4
 *                            fractions and the clip arrays, the three tiers in
 *                            upstream's order, the clip array updates, the
 *                            texture column, the light index and dc_iscale.
 *   R_DrawColumn()           the inner write: frac from dc_texturemid and the
 *                            row's distance from centery, one colormap read per
 *                            pixel, the texture column wrap.
 *   R_ClearPlanes()          the clip array reset that starts a frame
 *                            (floorclip = viewheight, ceilingclip = -1); the
 *                            rest of that function is the plane pass, D3.3.
 *   R_StoreWallRange()       not reimplemented: r_wall_fragment_open/step/peg
 *                            (r_bsp.c) are, and the drawseg's three range values
 *                            are read from the walk's record.
 *
 * DELIBERATE DIVERGENCES (each visible in behaviour and named for review)
 *
 *   1. Masked mid textures of two-sided lines are not drawn. R_RenderSegLoop()
 *      does not draw them either -- it records their texture column for
 *      R_DrawMasked() -- so this is not a divergence from the loop, only from
 *      "the frame is finished". D3.4 owns that pass.
 *   2. A tier may carry a transparency mask (r_walltex_t::mask), and a zero mask
 *      byte leaves the destination untouched. Vanilla has no such rule inside
 *      R_DrawColumn(); every tier it draws is opaque. The rule exists for the
 *      composite holes r_data.h measures and for D3.4's masked pass; a caller
 *      that passes NULL gets vanilla's behaviour exactly.
 *   3. The texture column wrap is `& heightMask` rather than R_DrawColumn()'s
 *      hard-coded `& 127`, and the horizontal wrap is the texture's own
 *      texturewidthmask. They are the same AND for every texture whose stored
 *      column is 128 rows, which is what the real-data harness uses; a caller
 *      whose textures are all powers of two may store exactly `height` rows.
 *
 *      One consequence is measurable and worth naming: thirteen textures in the
 *      shareware IWAD are not 128 rows tall (24, 56, 72, 96, 112), and when one
 *      of them covers a wall taller than itself, rows past its height are
 *      addressed. DOOM's `& 127` then reads whatever follows the column in the
 *      lump -- the next composite column's bytes, or past the end of the block
 *      for the last column -- which is a vanilla bug, not a texture rule, and
 *      cannot be expressed by a dense column layout. This port stores 128 rows
 *      per column, so those rows read as 0. On E1M1 one of the twenty sampled
 *      frames (frame 13) reaches them, for a 64 x 72 texture; the host oracle
 *      makes the same choice, so the comparison agrees there by construction,
 *      and no other frame depends on it.
 *   4. Rows outside [0, R_VIEW_HEIGHT) are not written. Upstream's RANGECHECK
 *      build calls I_Error() for them and its release build writes whatever is
 *      past the end of the view; the clip arrays make the range unreachable for
 *      any fragment the walk stores, so this is a guard on a caller's corrupt
 *      drawseg, not a behaviour.
 *
 * This is C99 only: no allocation, no globals, no stdio, no floating point.
 */
#include "r_segs.h"

#include "r_tables.h"

/* --- Texture validation -------------------------------------------------- */

/* Everything the column loop assumes about a texture, proved once per fragment
 * before a pixel is written. See R_SEGS_ERR_TEXTURE_SHAPE in r_segs.h. */
int r_walltex_is_usable(const r_walltex_t* texture) {
    if (texture == NULL || texture->pixels == NULL) {
        return 0;
    }
    if (texture->width < 1 || texture->height < 1) {
        return 0;
    }
    /* The stored column must be a power of two rows long, because the vertical
     * wrap is an AND, and it must hold the declared texture. */
    if (texture->heightMask < 0) {
        return 0;
    }
    if (((int)texture->heightMask & ((int)texture->heightMask + 1)) != 0) {
        return 0;
    }
    if (texture->heightMask < (int16_t)(texture->height - 1)) {
        return 0;
    }
    if (texture->widthMask != r_texture_width_mask((int)texture->width)) {
        return 0;
    }
    return 1;
}

/* Resolves one tier and proves it usable. A NULL answer is "not resident", a
 * malformed struct is "not a texture": two different reports because the
 * caller's next step is different. */
static r_segs_error_t resolve_tier(const r_tex_source_t* textures, r_name_t name,
                                   const r_walltex_t** out) {
    const r_walltex_t* texture = textures->resolve(textures->context, name);

    if (texture == NULL) {
        return R_SEGS_ERR_TEXTURE_MISSING;
    }
    if (!r_walltex_is_usable(texture)) {
        return R_SEGS_ERR_TEXTURE_SHAPE;
    }
    *out = texture;
    return R_SEGS_OK;
}

/* Resolves every texture /p drawseg will draw. Runs before the first write, so a
 * frame with one non-resident texture changes nothing.
 *
 * A name already proved in this call is not resolved again. The preflight's job
 * is a proof, and one proof per name is enough: the resolver contract requires
 * the same name to answer consistently for the duration of the call, and the
 * draw pass resolves every tier again anyway, so a resolver that changes its
 * mind still cannot corrupt the surface -- it skips the tier, as it always did.
 * The saving is the duplicate compose the old preflight paid for every fragment
 * that used a texture an earlier fragment had already proved. The table is the
 * frame's distinct wall textures (measured peak 8) with headroom; a name that
 * does not fit is simply resolved, never dropped.
 */
#define R_SEGS_PREFLIGHT_MEMO 16

static r_segs_error_t preflight_texture(const r_tex_source_t* textures, r_name_t name,
                                        r_name_t* proven, int* provenCount) {
    int index;
    const r_walltex_t* texture;
    r_segs_error_t result;

    for (index = 0; index < *provenCount; ++index) {
        if (proven[index] == name) {
            return R_SEGS_OK;
        }
    }

    result = resolve_tier(textures, name, &texture);
    if (result != R_SEGS_OK) {
        return result;
    }
    if (*provenCount < R_SEGS_PREFLIGHT_MEMO) {
        proven[*provenCount] = name;
        ++(*provenCount);
    }
    return R_SEGS_OK;
}

/* --- Pool demand (D3.4 / D4.2h) ------------------------------------------ */

/*
 * The entries this frame needs, computed from the same predicate the save step
 * uses and *before* the first pixel. The preflight loop opens each fragment
 * once, for both its texture proof and its span count, so the pass pays one
 * r_wall_fragment_open() per drawseg instead of two.
 *
 * This is upstream's `openings[]` accounting: p_setup.c allocates one
 * `MAXOPENINGS` (SCREENWIDTH * 64) short array per level, and R_StoreWallRange
 * advances `lastopening` by `rw_stopx - start` for each clipped edge it saves.
 * This port previously charged a whole 320-entry block per edge, which is why
 * 71 edges needed 45,440 B; the run below is `x2 - x1 + 1` entries, exactly
 * upstream's span.
 */

/* Copies one fragment's clip array into the pool, fragment-relative:
 * pool[offset + (x - x1)] is clip[x], which is what every consumer indexes. */
static void save_clip_run(int16_t* pool, uint16_t offset, const int16_t* clip,
                          int x1, int x2) {
    int x;

    for (x = x1; x <= x2; ++x) {
        pool[(size_t)offset + (size_t)(x - x1)] = clip[x];
    }
}

/* Bumps the pool cursor by one span and answers its first entry. 0 means the
 * capacity no longer holds the run: unreachable after a successful
 * r_wall_preflight(), which counted the same demand with the same predicate, so
 * it guards a hand-built plan rather than a frame. */
static int reserve_clip_run(const r_view_state_t* state, uint16_t* next,
                            uint16_t span, uint16_t* offset) {
    if ((uint32_t)*next + (uint32_t)span
        > (uint32_t)state->storage.clipPoolCapacity) {
        return 0;
    }
    *offset = *next;
    *next = (uint16_t)(*next + span);
    return 1;
}

/* --- One column of one tier (R_DrawColumn) ------------------------------- */

/*
 * Writes `column` into rows yl..yh of screen column x.
 *
 * `frac` is R_DrawColumn()'s texture position: it starts at the texture row the
 * first screen row shows and advances by dc_iscale per row. The arithmetic is
 * int32, as upstream's is, and cannot overflow: R_ScaleFromGlobalAngle() clamps
 * rw_scale to at least 256, so dc_iscale is at most 0xFFFFFFFF/256 = 16,777,215,
 * and the largest row offset from centery is R_CENTER_Y = 100.
 */
void r_column_draw(r_draw_surface_t* surface, int x, int yl, int yh,
                   fixed_t textureMid, fixed_t iscale, const uint8_t* column,
                   int16_t heightMask, const uint8_t* mask,
                   const uint8_t* colormap, uint8_t* pixelMask,
                   uint32_t* written) {
    fixed_t frac;
    const size_t stride = (size_t)surface->width;
    size_t at;
    uint32_t count = 0u;
    int row;

    /* R_DrawColumn's `count = dc_yh - dc_yl; if (count < 0) return;`. */
    if (yl > yh) {
        return;
    }
    /* The clip arrays keep this range inside the view for any fragment the walk
     * stored; clamping here is the guard named in the file header, and it keeps
     * frac aligned because it starts at the first row actually written. */
    if (yl < 0) {
        yl = 0;
    }
    if (yh > R_VIEW_HEIGHT - 1) {
        yh = R_VIEW_HEIGHT - 1;
    }
    if (yh > surface->height - 1) {
        yh = surface->height - 1;
    }
    if (yl > yh) {
        return;
    }

    frac = textureMid + (fixed_t)((yl - R_CENTER_Y) * iscale);
    /* The strided address is advanced, not recomputed: the row multiply was the
     * only one in the loop, and a wall column is a run of rows at a fixed x. */
    at = (size_t)x + (size_t)yl * stride;

    if (mask == NULL) {
        /* Every tier DOOM's R_RenderSegLoop draws is opaque, and so is every
         * composite in the sampled map; this is the path that matters.
         *
         * D3.9 splits the coverage write out of the loop instead of testing the
         * pointer per pixel: `surface->pixels[at] = ...` and `pixelMask[at] = 1`
         * are two stores through two byte pointers, and the test is a branch the
         * compiler cannot fold away. The two forms write exactly the same bytes.
         * The destination is a pointer, not an index, for the same reason the
         * address was already being advanced. */
        uint8_t* destination = surface->pixels + at;

        if (pixelMask == NULL) {
            for (row = yl; row <= yh; ++row) {
                *destination = colormap[column[(int)((frac >> FRACBITS) & heightMask)]];
                destination += stride;
                frac += iscale;
            }
        } else {
            uint8_t* maskAt = pixelMask + at;

            for (row = yl; row <= yh; ++row) {
                *destination = colormap[column[(int)((frac >> FRACBITS) & heightMask)]];
                *maskAt = 1u;
                destination += stride;
                maskAt += stride;
                frac += iscale;
            }
        }
        count = (uint32_t)(yh - yl + 1);
    } else {
        uint8_t* destination = surface->pixels + at;

        for (row = yl; row <= yh; ++row) {
            const int texel = (int)((frac >> FRACBITS) & heightMask);

            if (mask[texel] != 0u) {
                *destination = colormap[column[texel]];
                if (pixelMask != NULL) {
                    pixelMask[at] = 1u;
                }
                ++count;
            }
            ++destination;
            destination += stride;
            frac += iscale;
            at += stride;
        }
    }

    /* One coverage write and one counter update per column instead of per pixel:
     * the counter's total is the same, and a column with no written row leaves
     * the coverage byte alone exactly as before. */
    if (count != 0u) {
        if (surface->covered != NULL) {
            surface->covered[x] = 1u;
        }
        if (written != NULL) {
            *written += count;
        }
    }
}

/* R_RenderSegLoop's `dc_iscale = 0xffffffffu / (unsigned)rw_scale`. The divisor
 * cannot be zero for a scale the walk produced; the guard keeps a corrupt
 * drawseg from dividing by it. */
static fixed_t column_iscale(fixed_t scale) {
    const unsigned int bits = (unsigned int)scale;

    if (bits == 0u) {
        return INT32_MAX;
    }
    return (fixed_t)(0xFFFFFFFFu / bits);
}

/* --- One fragment (R_RenderSegLoop) -------------------------------------- */

static void emit_fragment(const r_view_state_t* state, r_draw_surface_t* surface,
                          const r_tex_source_t* textures, const r_light_t* light,
                          const r_drawseg_t* drawseg, const r_wall_fragment_t* opened,
                          int16_t* maskedColumns) {
    r_wall_fragment_t fragment = *opened;
    const r_walltex_t* mid = NULL;
    const r_walltex_t* top = NULL;
    const r_walltex_t* bottom = NULL;
    int16_t* floorclip = state->storage.floorClip;
    int16_t* ceilingclip = state->storage.ceilingClip;
    fixed_t topfrac;
    fixed_t bottomfrac;
    fixed_t pixhigh;
    fixed_t pixlow;
    fixed_t scale;
    uint32_t* tierPixels = surface->tierPixels;
    fixed_t scaleStep;
    fixed_t topStep;
    fixed_t bottomStep;
    uint32_t genericStart = 0u;
    const uint32_t prologueStart = R_PROBE_HAS(state) ? R_PROBE_NOW() : 0u;
    int x;

    /* The predicate arrived from the caller's single r_wall_fragment_open(); ...
     * and the range, from the walk's record rather than a recomputation. */
    fragment.x1 = drawseg->x1;
    fragment.x2 = drawseg->x2;
    fragment.scale1 = drawseg->scale1;
    fragment.scale2 = drawseg->scale2;
    fragment.scaleStep = drawseg->scaleStep;
    fragment.distance = drawseg->rwDistance;
    fragment.offset = drawseg->rwOffset;
    fragment.centerAngle = drawseg->rwCenterAngle;
    fragment.lightNum = drawseg->lightNum;

    /* Preflight proved every one of these resolves; the second call is what
     * hands the bytes over. A resolver that answers differently between the two
     * passes breaks the r_tex_source_t contract, and the tier is then skipped
     * (with its clip update) rather than dereferenced: a wrong frame is a bug
     * report, a crash is a lost frame. */
    if (fragment.wallMid) {
        (void)resolve_tier(textures, fragment.midTexture, &mid);
    }
    if (fragment.wallTop) {
        (void)resolve_tier(textures, fragment.topTexture, &top);
    }
    if (fragment.wallBottom) {
        (void)resolve_tier(textures, fragment.bottomTexture, &bottom);
    }
    if (mid == NULL) {
        fragment.wallMid = 0;
    }
    if (top == NULL) {
        fragment.wallTop = 0;
    }
    if (bottom == NULL) {
        fragment.wallBottom = 0;
    }

    r_wall_fragment_peg(&fragment, mid != NULL ? (fixed_t)mid->height << FRACBITS : 0,
                        top != NULL ? (fixed_t)top->height << FRACBITS : 0);
    r_wall_fragment_step(&fragment, fragment.scale1, fragment.scaleStep);
    if (R_PROBE_HAS(state)) {
        R_PROBE_OF(state)->wallPrologueCycle += R_PROBE_NOW() - prologueStart;
    }

    /* The steppers are r_wall_fragment_step()'s output, so they are read after
     * it: taking them from the opened fragment would freeze every column at the
     * first column's value. */
    scaleStep = fragment.scaleStep;
    topStep = fragment.topStep;
    bottomStep = fragment.bottomStep;

    topfrac = fragment.topFrac;
    bottomfrac = fragment.bottomFrac;
    pixhigh = fragment.pixHigh;
    pixlow = fragment.pixLow;
    scale = fragment.scale;

    if (fragment.wallMid && maskedColumns == NULL) {
        /* The single-sided line is the sampled map's hot case: 666,303 of its
         * 682,590 wall pixels come through this branch. Split out of the generic
         * loop so the per-column texture lookup, light and scale are unconditional
         * and no top/bottom arithmetic is done for a tier that does not exist.
         * The generic loop below keeps every rule for the two-sided case. */
        for (x = fragment.x1; x <= fragment.x2; ++x) {
            int yl = (int)((topfrac + (1 << 12) - 1) >> 12);
            int yh;
            const angle_t angle = fragment.centerAngle + r_x_to_view_angle[x];
            fixed_t texturecolumn =
                fragment.offset - fixed_mul(r_fine_tangent_lookup(angle), fragment.distance);
            const uint8_t* colormap;
            fixed_t iscale;
            int textureColumn;
            const uint32_t setupStart = R_PROBE_HAS(state) ? R_PROBE_NOW() : 0u;

            if (yl < ceilingclip[x] + 1) {
                yl = ceilingclip[x] + 1;
            }
            yh = (int)(bottomfrac >> 12);
            if (yh >= floorclip[x]) {
                yh = floorclip[x] - 1;
            }

            texturecolumn >>= FRACBITS;
            textureColumn = (int)texturecolumn;
            colormap = r_light_wall(light, fragment.lightNum, scale);
            iscale = column_iscale(scale);
            if (R_PROBE_HAS(state)) {
                r_raster_probe_t* probe = R_PROBE_OF(state);
                const uint32_t now = R_PROBE_NOW();

                probe->wallSetupCycle += now - setupStart;
                ++probe->wallColumns;
                /* The store is timed around the call, not inside it: the inner
                 * loop is the one thing whose per-iteration cost must not
                 * change. */
                r_column_draw(surface, x, yl, yh, fragment.midTextureMid, iscale,
                              mid->pixels + (size_t)(textureColumn & mid->widthMask)
                                                * (size_t)(mid->heightMask + 1),
                              mid->heightMask, mid->mask, colormap, NULL,
                              &tierPixels[R_DRAW_TIER_MID]);
                probe->wallStoreCycle += R_PROBE_NOW() - now;
            } else {
                r_column_draw(surface, x, yl, yh, fragment.midTextureMid, iscale,
                              mid->pixels + (size_t)(textureColumn & mid->widthMask)
                                                * (size_t)(mid->heightMask + 1),
                              mid->heightMask, mid->mask, colormap, NULL,
                              &tierPixels[R_DRAW_TIER_MID]);
            }

            ceilingclip[x] = (int16_t)R_VIEW_HEIGHT;
            floorclip[x] = -1;
            scale += scaleStep;
            topfrac += topStep;
            bottomfrac += bottomStep;
        }
        return;
    }

    if (R_PROBE_HAS(state)) {
        genericStart = R_PROBE_NOW();
    }
    for (x = fragment.x1; x <= fragment.x2; ++x) {
        int yl = (int)((topfrac + (1 << 12) - 1) >> 12);
        int yh;
        const uint8_t* colormap = NULL;
        fixed_t iscale = 0;
        int textureColumn = 0;

        if (yl < ceilingclip[x] + 1) {
            yl = ceilingclip[x] + 1;
        }
        yh = (int)(bottomfrac >> 12);
        if (yh >= floorclip[x]) {
            yh = floorclip[x] - 1;
        }

        /* "texturecolumn and lighting are independent of wall tiers" -- computed
         * once per column, from the scale *before* it advances. A masked mid
         * texture also needs it (segtextured includes maskedtexture upstream). */
        if (fragment.wallMid || fragment.wallTop || fragment.wallBottom
            || maskedColumns != NULL) {
            const angle_t angle = fragment.centerAngle + r_x_to_view_angle[x];
            fixed_t texturecolumn =
                fragment.offset
                - fixed_mul(r_fine_tangent_lookup(angle), fragment.distance);

            texturecolumn >>= FRACBITS;
            textureColumn = (int)texturecolumn;
            colormap = r_light_wall(light, fragment.lightNum, scale);
            iscale = column_iscale(scale);
        }

        if (fragment.wallMid) {
            /* Single sided line: the middle texture covers the fragment. */
            r_column_draw(surface, x, yl, yh, fragment.midTextureMid, iscale,
                          mid->pixels + (size_t)(textureColumn & mid->widthMask)
                                            * (size_t)(mid->heightMask + 1),
                          mid->heightMask, mid->mask, colormap, NULL,
                          &tierPixels[R_DRAW_TIER_MID]);
            ceilingclip[x] = (int16_t)R_VIEW_HEIGHT;
            floorclip[x] = -1;
        } else {
            if (fragment.wallTop) {
                int midRow = (int)(pixhigh >> 12);

                pixhigh += fragment.pixHighStep;
                if (midRow >= floorclip[x]) {
                    midRow = floorclip[x] - 1;
                }
                if (midRow >= yl) {
                    r_column_draw(surface, x, yl, midRow, fragment.topTextureMid,
                                  iscale,
                                  top->pixels
                                      + (size_t)(textureColumn & top->widthMask)
                                            * (size_t)(top->heightMask + 1),
                                  top->heightMask, top->mask, colormap, NULL,
                                  &tierPixels[R_DRAW_TIER_TOP]);
                    ceilingclip[x] = (int16_t)midRow;
                } else {
                    ceilingclip[x] = (int16_t)(yl - 1);
                }
            } else if (fragment.markCeiling) {
                ceilingclip[x] = (int16_t)(yl - 1);
            }

            if (fragment.wallBottom) {
                int midRow = (int)((pixlow + (1 << 12) - 1) >> 12);

                pixlow += fragment.pixLowStep;
                if (midRow <= ceilingclip[x]) {
                    midRow = ceilingclip[x] + 1;
                }
                if (midRow <= yh) {
                    r_column_draw(surface, x, midRow, yh, fragment.bottomTextureMid,
                                  iscale,
                                  bottom->pixels
                                      + (size_t)(textureColumn & bottom->widthMask)
                                            * (size_t)(bottom->heightMask + 1),
                                  bottom->heightMask, bottom->mask, colormap, NULL,
                                  &tierPixels[R_DRAW_TIER_BOTTOM]);
                    floorclip[x] = (int16_t)midRow;
                } else {
                    floorclip[x] = (int16_t)(yh + 1);
                }
            } else if (fragment.markFloor) {
                floorclip[x] = (int16_t)(yh + 1);
            }

            /* "save texturecol for backdrawing of masked mid texture": the
             * column value, truncated to 16 bits exactly as upstream's
             * `short maskedtexturecol[]` truncates it. */
            if (maskedColumns != NULL) {
                maskedColumns[x - fragment.x1] = (int16_t)textureColumn;
            }
        }

        scale += scaleStep;
        topfrac += topStep;
        bottomfrac += bottomStep;
    }
    if (R_PROBE_HAS(state)) {
        R_PROBE_OF(state)->wallGenericCycle += R_PROBE_NOW() - genericStart;
    }
}

/* --- Public API ---------------------------------------------------------- */

r_segs_error_t r_wall_draw(r_view_state_t* state, r_draw_surface_t* surface,
                           const r_tex_source_t* textures,
                           const r_light_t* light, const r_wall_plan_t* plan) {
    uint16_t index;
    uint16_t nextEntry = 0u;
    int16_t* clipPool;
    int column;
    uint32_t clipSaveStart = 0u;

    if (state == NULL) {
        return R_SEGS_ERR_NULL_STATE;
    }
    if (state->map == NULL || state->map->open == 0) {
        return R_SEGS_ERR_MAP_NOT_OPEN;
    }
    if (plan == NULL) {
        return R_SEGS_ERR_NULL_PLAN;
    }
    if (surface == NULL) {
        return R_SEGS_ERR_NULL_SURFACE;
    }
    if (surface->pixels == NULL) {
        return R_SEGS_ERR_SURFACE_PIXELS;
    }
    if (surface->width < R_SCREEN_WIDTH || surface->height < R_VIEW_HEIGHT) {
        return R_SEGS_ERR_SURFACE_SIZE;
    }
    if (textures == NULL) {
        return R_SEGS_ERR_NULL_SOURCE;
    }
    if (textures->resolve == NULL) {
        return R_SEGS_ERR_NULL_RESOLVE;
    }
    if (light == NULL || light->colormaps == NULL) {
        return R_SEGS_ERR_NULL_LIGHT;
    }
    /* The preflight proved the pool and every texture; these two checks keep a
     * caller that hand-built a plan from writing past the array. They are
     * contract guards, not frame behaviour: r_wall_emit() cannot reach them. */
    if (plan->clipPoolEntries > 0u && state->storage.clipPool == NULL) {
        return R_SEGS_ERR_NULL_CLIP_POOL;
    }
    if (plan->clipPoolEntries > (uint32_t)state->storage.clipPoolCapacity) {
        return R_SEGS_ERR_SMALL_CLIP_POOL;
    }
    clipPool = state->storage.clipPool;

    /* R_ClearPlanes()' opening/closing determination, and this pass's records. */
    {
        const uint32_t poolStart = R_PROBE_HAS(state) ? R_PROBE_NOW() : 0u;

        for (column = 0; column < R_SCREEN_WIDTH; ++column) {
            state->storage.floorClip[column] = (int16_t)R_VIEW_HEIGHT;
            state->storage.ceilingClip[column] = -1;
            if (surface->covered != NULL) {
                surface->covered[column] = 0u;
            }
        }
        surface->tierPixels[R_DRAW_TIER_MID] = 0u;
        surface->tierPixels[R_DRAW_TIER_TOP] = 0u;
        surface->tierPixels[R_DRAW_TIER_BOTTOM] = 0u;
        /* No constant blocks to fill: R_DRAWSEG_CLIP_TOP/BOTTOM are codes the
         * consumers answer with R_VIEW_HEIGHT/-1, not pool contents. */
        if (R_PROBE_HAS(state)) {
            R_PROBE_OF(state)->wallPoolCycle += R_PROBE_NOW() - poolStart;
        }
    }

    for (index = 0; index < state->drawsegCount; ++index) {
        r_drawseg_t* drawseg = &state->storage.drawsegs[index];
        r_wall_fragment_t fragment;
        int16_t* maskedColumns = NULL;
        int masked;
        int silhouette;
        int16_t topBlock = R_DRAWSEG_NO_BLOCK;
        int16_t bottomBlock = R_DRAWSEG_NO_BLOCK;
        int16_t maskedBlock = R_DRAWSEG_NO_BLOCK;
        uint16_t span = (uint16_t)(drawseg->x2 - drawseg->x1 + 1);
        uint16_t offset = 0u;
        uint32_t openStart;

        openStart = R_PROBE_HAS(state) ? R_PROBE_NOW() : 0u;
        r_wall_fragment_open(state->map, &state->params, drawseg->seg, &fragment);
        if (R_PROBE_HAS(state)) {
            R_PROBE_OF(state)->wallOpenCycle += R_PROBE_NOW() - openStart;
        }
        masked = fragment.hasBack && r_has_name(fragment.midTexture) ? 1 : 0;
        if (masked) {
            if (!reserve_clip_run(state, &nextEntry, span, &offset)) {
                return R_SEGS_ERR_SMALL_CLIP_POOL;
            }
            maskedBlock = (int16_t)offset;
            maskedColumns = clipPool + (size_t)offset;
            /* Every entry starts unused, so the masked pass can tell a column
             * the loop wrote from one outside [x1, x2]. Only the fragment's own
             * span is reserved; upstream's openings[] is fragment-relative too. */
            for (column = 0; column < (int)span; ++column) {
                maskedColumns[column] = R_MASKED_COLUMN_USED;
            }
        }

        if (textures->beginFragment != NULL) {
            textures->beginFragment(textures->context);
        }
        {
            const uint32_t emitStart = R_PROBE_HAS(state) ? R_PROBE_NOW() : 0u;

            emit_fragment(state, surface, textures, light, drawseg, &fragment,
                          maskedColumns);
            if (R_PROBE_HAS(state)) {
                R_PROBE_OF(state)->wallEmitCycle += R_PROBE_NOW() - emitStart;
            }
        }

        /* R_StoreWallRange's tail: the clip arrays as the column loop left
         * them, then the edges a masked texture forces. */
        silhouette = fragment.silhouette;
        clipSaveStart = R_PROBE_HAS(state) ? R_PROBE_NOW() : 0u;
        if (fragment.hasBack) {
            if (fragment.clipTopSolid) {
                topBlock = R_DRAWSEG_CLIP_TOP;
            } else if (!masked && (silhouette & R_SIL_TOP) == 0) {
                topBlock = R_DRAWSEG_NO_BLOCK;
            } else {
                if (!reserve_clip_run(state, &nextEntry, span, &offset)) {
                    return R_SEGS_ERR_SMALL_CLIP_POOL;
                }
                topBlock = (int16_t)offset;
                save_clip_run(clipPool, offset, state->storage.ceilingClip,
                              drawseg->x1, drawseg->x2);
            }
            if (fragment.clipBottomSolid) {
                bottomBlock = R_DRAWSEG_CLIP_BOTTOM;
            } else if (!masked && (silhouette & R_SIL_BOTTOM) == 0) {
                bottomBlock = R_DRAWSEG_NO_BLOCK;
            } else {
                if (!reserve_clip_run(state, &nextEntry, span, &offset)) {
                    return R_SEGS_ERR_SMALL_CLIP_POOL;
                }
                bottomBlock = (int16_t)offset;
                save_clip_run(clipPool, offset, state->storage.floorClip,
                              drawseg->x1, drawseg->x2);
            }
        } else {
            topBlock = R_DRAWSEG_CLIP_TOP;
            bottomBlock = R_DRAWSEG_CLIP_BOTTOM;
        }
        if (R_PROBE_HAS(state)) {
            R_PROBE_OF(state)->wallClipSaveCycle += R_PROBE_NOW() - clipSaveStart;
        }

        if (masked) {
            if ((silhouette & R_SIL_TOP) == 0) {
                silhouette |= R_SIL_TOP;
                drawseg->topSilhouette = INT32_MIN;
            }
            if ((silhouette & R_SIL_BOTTOM) == 0) {
                silhouette |= R_SIL_BOTTOM;
                drawseg->bottomSilhouette = INT32_MAX;
            }
        }

        drawseg->silhouette = (uint8_t)silhouette;
        drawseg->sprTopClip = topBlock;
        drawseg->sprBottomClip = bottomBlock;
        drawseg->maskedTextureCol = maskedBlock;
    }

    state->clipBlocksUsed = plan->clipBlocks;
    state->openingBlocksUsed = plan->openingBlocks;
    state->clipPoolUsed = nextEntry;
    state->clipCaptured = 1;
    return R_SEGS_OK;
}

r_segs_error_t r_wall_preflight(r_view_state_t* state, const r_tex_source_t* textures,
                                r_wall_plan_t* plan) {
    uint32_t demand = 0u;
    uint16_t clipBlocks = R_DRAWSEG_RESERVED_BLOCKS;
    uint16_t openingBlocks = 0u;
    uint16_t index;
    r_name_t proven[R_SEGS_PREFLIGHT_MEMO];
    int provenCount = 0;
    uint32_t preflightStart = 0u;

    if (state == NULL) {
        return R_SEGS_ERR_NULL_STATE;
    }
    if (state->map == NULL || state->map->open == 0) {
        return R_SEGS_ERR_MAP_NOT_OPEN;
    }
    if (textures == NULL) {
        return R_SEGS_ERR_NULL_SOURCE;
    }
    if (textures->resolve == NULL) {
        return R_SEGS_ERR_NULL_RESOLVE;
    }
    if (plan == NULL) {
        return R_SEGS_ERR_NULL_PLAN;
    }

    /* Nothing is written until every texture the pass will draw is resident and
     * well formed, and every pool entry the clip capture needs exists. One
     * r_wall_fragment_open() per drawseg serves both proofs. */
    if (R_PROBE_HAS(state)) {
        preflightStart = R_PROBE_NOW();
    }
    for (index = 0; index < state->drawsegCount; ++index) {
        const r_drawseg_t* drawseg = &state->storage.drawsegs[index];
        r_wall_fragment_t fragment;
        r_segs_error_t result;
        uint32_t span = (uint32_t)(drawseg->x2 - drawseg->x1 + 1);
        int masked;
        int silhouette;

        if (textures->beginFragment != NULL) {
            textures->beginFragment(textures->context);
        }
        r_wall_fragment_open(state->map, &state->params, drawseg->seg, &fragment);

        if (fragment.wallMid) {
            result = preflight_texture(textures, fragment.midTexture, proven, &provenCount);
            if (result != R_SEGS_OK) {
                return result;
            }
        }
        if (fragment.wallTop) {
            result = preflight_texture(textures, fragment.topTexture, proven, &provenCount);
            if (result != R_SEGS_OK) {
                return result;
            }
        }
        if (fragment.wallBottom) {
            result = preflight_texture(textures, fragment.bottomTexture, proven, &provenCount);
            if (result != R_SEGS_OK) {
                return result;
            }
        }
        if (fragment.hasBack && r_has_name(fragment.midTexture)) {
            result = preflight_texture(textures, fragment.midTexture, proven, &provenCount);
            if (result != R_SEGS_OK) {
                return result;
            }
        }

        masked = fragment.hasBack && r_has_name(fragment.midTexture) ? 1 : 0;
        silhouette = fragment.silhouette;
        if (masked) {
            /* R_StoreWallRange's tail: a masked texture forces both edges. */
            silhouette |= R_SIL_BOTH;
            openingBlocks = (uint16_t)(openingBlocks + 1u);
            demand += span;
        }
        if (fragment.hasBack) {
            if (!fragment.clipTopSolid && (silhouette & R_SIL_TOP) != 0) {
                clipBlocks = (uint16_t)(clipBlocks + 1u);
                demand += span;
            }
            if (!fragment.clipBottomSolid && (silhouette & R_SIL_BOTTOM) != 0) {
                clipBlocks = (uint16_t)(clipBlocks + 1u);
                demand += span;
            }
        }
    }
    if (R_PROBE_HAS(state)) {
        R_PROBE_OF(state)->wallPreflightCycle += R_PROBE_NOW() - preflightStart;
    }
    if (demand > 0u && state->storage.clipPool == NULL) {
        return R_SEGS_ERR_NULL_CLIP_POOL;
    }
    if (demand > (uint32_t)state->storage.clipPoolCapacity) {
        return R_SEGS_ERR_SMALL_CLIP_POOL;
    }

    plan->clipPoolEntries = demand;
    plan->clipBlocks = clipBlocks;
    plan->openingBlocks = openingBlocks;
    state->clipPoolEntries = (uint16_t)demand;
    return R_SEGS_OK;
}

r_segs_error_t r_wall_emit(r_view_state_t* state, r_draw_surface_t* surface,
                           const r_tex_source_t* textures, const r_light_t* light) {
    r_wall_plan_t plan;
    r_segs_error_t result = r_wall_preflight(state, textures, &plan);

    if (result != R_SEGS_OK) {
        return result;
    }
    return r_wall_draw(state, surface, textures, light, &plan);
}
