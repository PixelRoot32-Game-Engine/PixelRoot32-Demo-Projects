/*
 * Sprites and the deferred masked mid textures (r_things.h), from upstream's
 * R_ProjectSprite(), R_SortVisSprites(), R_DrawSprite(), R_DrawVisSprite() and
 * R_DrawMaskedColumn() (r_things.c), R_RenderMaskedSegRange() (r_segs.c) and
 * R_DrawColumn() (r_draw.c).
 *
 * The header states the rules and cites them. This file is the implementation,
 * and the comments below only mark the places where the code's shape is not
 * obvious:
 *
 *   - the patch post walk reads the raw WAD bytes, because r_patch_validate()
 *     has already proved every offset and every post and a dense column would
 *     lose the transparent gaps that *are* the sprite;
 *   - the sort is upstream's selection pass written as a stable removal, not a
 *     swap, because a swap changes the order of equal-scale sprites and that
 *     order is observable when two of them overlap;
 *   - the masked mid texture reuses r_segs.c's r_column_draw(), so the vertical
 *     wrap and the transparency mask have one implementation;
 *   - the 1,280 B clip arrays are stack locals, exactly as upstream's are.
 *
 * This is C99 only: no allocation, no globals, no stdio, no floating point.
 */
#include "r_things.h"

#include <string.h>

#include "r_data.h"
#include "r_tables.h"

/* --- Little-endian patch fields ------------------------------------------ */

/* A patch is a 1993 lump: little-endian throughout, whatever the target. */
static int32_t read_le32(const uint8_t* bytes) {
    return (int32_t)((uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8)
                     | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24));
}

/* --- Sprite patches ------------------------------------------------------- */

/*
 * THE PATCH PROOF MEMO (D3.9)
 *
 * r_patch_validate() walks every column of a patch and every post in it, and the
 * bytes it walks are a mapped-flash lump. Measured on the board, that walk is
 * 5,599 us of a 7,555 us masked phase -- the largest single item in the pass --
 * for 159 sprites that ask for 32 distinct lumps, because the pass proves a
 * patch once per sprite in the preflight and again once per drawn sprite.
 *
 * One proof per *patch* is enough for exactly the reason r_segs.h gives for the
 * name memo: the resolver contract requires the same lump to answer
 * consistently for the duration of the call, and the draw still resolves the
 * lump again, so a resolver that changes its mind is caught by the pointer test
 * below and re-proved rather than trusted.
 *
 * The memo is keyed on the resolved record's address, not on the name, so it
 * cannot answer for a patch the resolver handed out for a different lump. When
 * the resolver returns a fresh record per call the memo simply never hits and
 * the pass costs what it always did.
 *
 * The table is the caller's when the caller supplies one (r_things.h's
 * r_sprite_proofs_t, which moves the proof to the residency), and a stack table
 * of this size otherwise. Both share one implementation.
 */
static r_things_error_t resolve_patch(const r_sprite_source_t* source, r_name_t lump,
                                      r_sprite_proofs_t* memo,
                                      const r_sprite_patch_t** out,
                                      r_patch_info_t* info) {
    const r_sprite_patch_t* patch = source->resolve(source->context, lump);
    int index;

    if (patch == NULL) {
        return R_THINGS_ERR_SPRITE_MISSING;
    }
    if (patch->data == NULL || patch->size < R_PATCH_HEADER_SIZE) {
        return R_THINGS_ERR_SPRITE_SHAPE;
    }

    for (index = 0; index < memo->count; ++index) {
        if (memo->entries[index].patch == patch) {
            info->width = memo->entries[index].width;
            info->height = memo->entries[index].height;
            info->leftOffset = memo->entries[index].leftOffset;
            info->topOffset = memo->entries[index].topOffset;
            info->columnTableEnd =
                (size_t)R_PATCH_HEADER_SIZE
                + (size_t)info->width * (size_t)R_PATCH_COLUMN_ENTRY_SIZE;
            *out = patch;
            return R_THINGS_OK;
        }
    }

    if (r_patch_validate(patch->data, patch->size, info) != R_ERR_OK) {
        return R_THINGS_ERR_SPRITE_SHAPE;
    }
    if (info->width < 1 || info->height < 1) {
        return R_THINGS_ERR_SPRITE_SHAPE;
    }
    if (memo->count < R_SPRITE_PROOF_CAPACITY) {
        r_sprite_proof_t* entry = &memo->entries[memo->count];

        entry->patch = patch;
        entry->width = info->width;
        entry->height = info->height;
        entry->leftOffset = info->leftOffset;
        entry->topOffset = info->topOffset;
        ++memo->count;
    }
    *out = patch;
    return R_THINGS_OK;
}
/* --- R_ProjectSprite() ---------------------------------------------------- */

/*
 * One accepted sprite's projection, stored in `out`. Returns 1 when the sprite
 * is accepted, 0 with `*cull` set to the counter it belongs to.
 *
 * R_ProjectSprite()'s order is kept exactly: back face, off the side, then the
 * two projected edges and the frustum tests, then the clamps and startfrac.
 */
static int project_sprite(const r_view_state_t* state, const r_sprite_t* sprite,
                          const r_patch_info_t* patch, r_vissprite_t* out,
                          int* cull) {
    const r_view_params_t* view = &state->params;
    const fixed_t viewSin = r_fine_sine[(view->angle >> ANGLETOFINESHIFT)];
    const fixed_t viewCos = r_fine_cosine(view->angle);
    fixed_t trX = sprite->x - view->x;
    fixed_t trY = sprite->y - view->y;
    fixed_t gxt;
    fixed_t gyt;
    fixed_t tz;
    fixed_t xscale;
    fixed_t tx;
    int x1;
    int x2;
    fixed_t iscale;
    fixed_t startfrac;

    gxt = fixed_mul(trX, viewCos);
    gyt = -fixed_mul(trY, viewSin);
    tz = gxt - gyt;
    if (tz < R_SPRITE_MINZ) {
        *cull = 1; /* backface */
        return 0;
    }

    xscale = fixed_div(R_CENTER_X_FRAC, tz);

    gxt = -fixed_mul(trX, viewSin);
    gyt = fixed_mul(trY, viewCos);
    tx = -(gyt + gxt);
    {
        const fixed_t absTx = tx < 0 ? -tx : tx;

        if (absTx > (tz << 2)) {
            *cull = 2; /* frustum */
            return 0;
        }
    }

    /* calculate edges of the shape */
    tx -= (fixed_t)patch->leftOffset * FRACUNIT;
    x1 = (int)((R_CENTER_X_FRAC + fixed_mul(tx, xscale)) >> FRACBITS);
    if (x1 > R_SCREEN_WIDTH) {
        *cull = 2;
        return 0;
    }
    tx += (fixed_t)patch->width * FRACUNIT;
    x2 = (int)(((R_CENTER_X_FRAC + fixed_mul(tx, xscale)) >> FRACBITS) - 1);
    if (x2 < 0) {
        *cull = 2;
        return 0;
    }

    out->sprite = sprite;
    out->scale = xscale;
    out->gx = sprite->x;
    out->gy = sprite->y;
    out->gz = sprite->z;
    out->gzt = sprite->z + (fixed_t)patch->topOffset * FRACUNIT;
    out->texturemid = out->gzt - view->z;
    out->x1 = (int16_t)(x1 < 0 ? 0 : x1);
    out->x2 = (int16_t)(x2 >= R_SCREEN_WIDTH ? R_SCREEN_WIDTH - 1 : x2);
    out->lightnum = (int16_t)r_light_row(
        (sprite->lightLevel >> R_LIGHT_SEG_SHIFT) + view->extralight);

    iscale = fixed_div(FRACUNIT, xscale);
    if (sprite->flip) {
        startfrac = ((fixed_t)patch->width - 1) * FRACUNIT;
        out->xiscale = -iscale;
    } else {
        startfrac = 0;
        out->xiscale = iscale;
    }
    if (out->x1 > x1) {
        startfrac += out->xiscale * (fixed_t)(out->x1 - x1);
    }
    out->startfrac = startfrac;

    /* Accepted, but a left edge clamped to 320 leaves no column (upstream's
     * R_DrawVisSprite loops `for (x = 320; x <= 319; ...)`, i.e. not at all). */
    if (out->x1 > out->x2) {
        *cull = 3; /* zerospan */
        return 0;
    }
    *cull = 0;
    return 1;
}

/* --- R_SortVisSprites() --------------------------------------------------- */

/*
 * Ascending by scale: the smallest scale (the farthest sprite) is pulled out
 * first and appended to the sorted list, so the draw loop runs back to front.
 *
 * Upstream removes the chosen entry from a doubly linked list, which leaves the
 * relative order of the rest untouched, and picks the *first* entry with a
 * strictly smaller scale, so equal scales keep their submission order. Swapping
 * the chosen entry with position i would scramble that order; the shift below
 * is the removal.
 */
static void sort_vissprites(r_vissprite_t* list, uint16_t count) {
    uint16_t i;

    for (i = 0; i < count; ++i) {
        uint16_t best = i;
        uint16_t j;
        fixed_t bestScale = list[i].scale;
        r_vissprite_t chosen;

        for (j = (uint16_t)(i + 1u); j < count; ++j) {
            if (list[j].scale < bestScale) {
                bestScale = list[j].scale;
                best = j;
            }
        }
        if (best == i) {
            continue;
        }
        chosen = list[best];
        for (j = best; j > i; --j) {
            list[j] = list[j - 1u];
        }
        list[i] = chosen;
    }
}

/* --- R_PointOnSegSide() --------------------------------------------------- */

/*
 * The bit-trick predicate R_DrawSprite()'s scale tie-break uses: which side of
 * the seg a point is on, without a division. Reproduced including the
 * sign-bit shortcut, because the shortcut is not just an optimisation -- it is
 * the branch structure upstream's answer comes from for the exact cases the
 * tie-break reaches.
 */
static int point_on_seg_side(const doom_map_t* map, uint16_t segIndex, fixed_t x,
                             fixed_t y) {
    doom_seg_t seg;
    doom_vertex_t v1;
    doom_vertex_t v2;
    fixed_t ldx;
    fixed_t ldy;
    fixed_t dx;
    fixed_t dy;
    fixed_t left;
    fixed_t right;

    doom_map_get_seg(map, segIndex, &seg);
    doom_map_get_vertex(map, seg.v1, &v1);
    doom_map_get_vertex(map, seg.v2, &v2);

    ldx = v2.x - v1.x;
    ldy = v2.y - v1.y;

    if (ldx == 0) {
        if (x <= v1.x) {
            return ldy > 0;
        }
        return ldy < 0;
    }
    if (ldy == 0) {
        if (y <= v1.y) {
            return ldx < 0;
        }
        return ldx > 0;
    }

    dx = x - v1.x;
    dy = y - v1.y;

    if ((((uint32_t)ldy ^ (uint32_t)ldx ^ (uint32_t)dx ^ (uint32_t)dy)
         & 0x80000000u) != 0u) {
        return (((uint32_t)ldy ^ (uint32_t)dx) & 0x80000000u) != 0u;
    }

    left = fixed_mul(ldy >> FRACBITS, dx);
    right = fixed_mul(dy, ldx >> FRACBITS);
    return right < left ? 0 : 1;
}

/* --- Saved clip columns --------------------------------------------------- */

/*
 * One saved clip column. A valid code is an entry offset into the caller's
 * single pool; the two negative sentinels answer upstream's shared constants
 * (`sprtopclip = screenheightarray`, `sprbottomclip = negonearray`) without
 * reading the pool, which is why they cost no storage.
 */
static int16_t clip_value(const int16_t* pool, int16_t code, const r_drawseg_t* drawseg,
                          int x) {
    if (code == R_DRAWSEG_CLIP_TOP) {
        return (int16_t)R_VIEW_HEIGHT;
    }
    if (code == R_DRAWSEG_CLIP_BOTTOM) {
        return -1;
    }
    return pool[(size_t)code + (size_t)(x - drawseg->x1)];
}

/* --- One post (R_DrawMaskedColumn's inner R_DrawColumn) ------------------- */

/*
 * Draws one post of a patch column. `pixels` is the post's first pixel byte,
 * `textureMid` is `basetexturemid - (topdelta<<FRACBITS)`, and the source index
 * wraps with upstream's `& 127` because the run is the post's own bytes, not a
 * dense column.
 */
static void draw_post(r_draw_surface_t* surface, int x, int yl, int yh,
                      fixed_t textureMid, fixed_t iscale, const uint8_t* pixels,
                      const uint8_t* colormap, uint8_t* pixelMask,
                      uint32_t* written) {
    fixed_t frac;
    int row;

    if (yl > yh) {
        return;
    }
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
    {
        const int stride = surface->width;
        size_t at = (size_t)x + (size_t)yl * (size_t)stride;
        const uint32_t count = (uint32_t)(yh - yl + 1);

        for (row = yl; row <= yh; ++row) {
            surface->pixels[at] =
                colormap[pixels[(frac >> FRACBITS) & R_SPRITE_POST_MASK]];
            if (pixelMask != NULL) {
                pixelMask[at] = 1u;
            }
            at += (size_t)stride;
            frac += iscale;
        }
        if (surface->covered != NULL) {
            surface->covered[x] = 1u;
        }
        if (written != NULL) {
            *written += count;
        }
    }
}

/* --- R_DrawVisSprite() ---------------------------------------------------- */

/*
 * Draws the sprite's columns through the post chains. `patch` was validated in
 * the preflight, so the column offsets and every post are inside the lump.
 */
static void draw_vis_sprite(r_view_state_t* state, r_draw_surface_t* surface,
                            const r_light_t* light, const r_vissprite_t* vis,
                            const r_sprite_patch_t* patch, const r_patch_info_t* info,
                            const int16_t* clipbot, const int16_t* cliptop,
                            uint8_t* pixelMask, uint32_t* written) {
    const uint8_t* data = patch->data;
    const uint8_t* colormap;
    const fixed_t iscale = vis->xiscale < 0 ? -vis->xiscale : vis->xiscale;
    const fixed_t spryscale = vis->scale;
    const fixed_t sprtopscreen =
        R_CENTER_Y_FRAC - fixed_mul(vis->texturemid, spryscale);
    fixed_t frac = vis->startfrac;
    int x;

    if (vis->sprite->fullbright) {
        colormap = light->colormaps;
    } else {
        colormap = r_light_sprite(light, vis->lightnum, vis->scale);
    }
    if (colormap == NULL) {
        return;
    }

    {
        const uint32_t spriteStart = R_PROBE_HAS(state) ? R_PROBE_NOW() : 0u;

        for (x = vis->x1; x <= vis->x2; ++x, frac += vis->xiscale) {
            const int textureColumn = (int)(frac >> FRACBITS);
            const uint8_t* post;
            int32_t columnOffset;

            /* RANGECHECK's test, as a guard: valid projection leaves the column
             * inside [0, width), so this cannot reject a frame upstream would draw. */
            if (textureColumn < 0 || textureColumn >= info->width) {
                continue;
            }
            columnOffset =
                read_le32(data + R_PATCH_HEADER_SIZE + (size_t)textureColumn * 4u);
            if (columnOffset < 0
                || (size_t)columnOffset >= patch->size) {
                continue;
            }
            post = data + columnOffset;

            while (post[0] != R_PATCH_POST_END) {
                const int topdelta = (int)post[0];
                const int length = (int)post[1];
                const fixed_t topscreen =
                    sprtopscreen + (fixed_t)((int64_t)spryscale * topdelta);
                const fixed_t bottomscreen =
                    topscreen + (fixed_t)((int64_t)spryscale * length);
                int yl = (int)((topscreen + FRACUNIT - 1) >> FRACBITS);
                int yh = (int)((bottomscreen - 1) >> FRACBITS);

                if (R_PROBE_HAS(state)) {
                    ++R_PROBE_OF(state)->maskedPostCalls;
                }
                if (yh >= clipbot[x]) {
                    yh = clipbot[x] - 1;
                }
                if (yl <= cliptop[x]) {
                    yl = cliptop[x] + 1;
                }
                if (yl <= yh) {
                    draw_post(surface, x, yl, yh,
                              vis->texturemid - ((fixed_t)topdelta * FRACUNIT),
                              iscale, post + R_PATCH_POST_HEADER_SIZE, colormap,
                              pixelMask, written);
                }
                post += length + R_PATCH_POST_HEADER_SIZE
                        + R_PATCH_POST_TRAILER_SIZE;
            }
        }
        if (R_PROBE_HAS(state)) {
            R_PROBE_OF(state)->maskedSpriteCycle += R_PROBE_NOW() - spriteStart;
        }
    }
}

/* --- R_RenderMaskedSegRange() --------------------------------------------- */

/*
 * Draws the columns [x1, x2] of one drawseg's deferred masked mid texture.
 *
 * The texture column comes from the pool run the wall pass recorded, the
 * clip arrays are the drawseg's own saved blocks (upstream's
 * `mfloorclip = ds->sprbottomclip`), and the light is the *wall* rule, not the
 * sprite rule. A column is consumed as it is drawn, so a later call -- the
 * sprite loop and the final far-to-near loop both call this -- cannot paint it
 * twice.
 */
static void render_masked_range(r_view_state_t* state, r_draw_surface_t* surface,
                                const r_tex_source_t* textures,
                                const r_light_t* light, r_drawseg_t* drawseg,
                                int x1, int x2, uint8_t* pixelMask,
                                uint32_t* written) {
    r_wall_fragment_t fragment;
    const r_walltex_t* texture = NULL;
    int16_t* clipPool = state->storage.clipPool;
    int16_t* columns;
    fixed_t textureMid;
    fixed_t spryscale;
    int x;

    r_wall_fragment_open(state->map, &state->params, drawseg->seg, &fragment);
    texture = textures->resolve(textures->context, fragment.midTexture);
    if (texture == NULL || !r_walltex_is_usable(texture)) {
        return;
    }

    /* The masked mid's own run; the clip arrays are the drawseg's saved runs or
     * the shared constants, answered by clip_value(). */
    columns = clipPool + (size_t)drawseg->maskedTextureCol;

    /* R_RenderMaskedSegRange's pegging, from the fragment's view-relative
     * heights: max(front floor, back floor) + height, or min(front ceiling,
     * back ceiling), then the sidedef rowoffset. */
    if ((fragment.flags & DOOM_ML_DONTPEGBOTTOM) != 0) {
        const fixed_t floor =
            fragment.worldBottom > fragment.worldLow ? fragment.worldBottom
                                                     : fragment.worldLow;

        textureMid = floor + ((fixed_t)texture->height << FRACBITS);
    } else {
        textureMid =
            fragment.worldTop < fragment.worldHigh ? fragment.worldTop
                                                   : fragment.worldHigh;
    }
    textureMid += fragment.rowOffset;

    spryscale = drawseg->scale1
                + (fixed_t)((int64_t)(x1 - drawseg->x1) * drawseg->scaleStep);

    for (x = x1; x <= x2; ++x) {
        const int16_t column = columns[x - drawseg->x1];

        if (column != R_MASKED_COLUMN_USED) {
            const int yl = clip_value(clipPool, drawseg->sprTopClip, drawseg, x) + 1;
            const int yh = clip_value(clipPool, drawseg->sprBottomClip, drawseg, x) - 1;
            const uint8_t* colormap =
                r_light_wall(light, drawseg->lightNum, spryscale);
            const fixed_t iscale =
                spryscale == 0 ? INT32_MAX
                               : (fixed_t)(0xFFFFFFFFu / (unsigned)spryscale);

            if (colormap != NULL && yl <= yh) {
                r_column_draw(
                    surface, x, yl, yh, textureMid, iscale,
                    texture->pixels
                        + (size_t)((int)column & texture->widthMask)
                              * (size_t)(texture->heightMask + 1),
                    texture->heightMask, texture->mask, colormap, pixelMask,
                    written);
            }
            columns[x - drawseg->x1] = R_MASKED_COLUMN_USED;
        }
        spryscale += drawseg->scaleStep;
    }
}

/* --- R_DrawSprite() ------------------------------------------------------- */

/*
 * Clips and draws one sprite: seed the two clip arrays from every drawseg the
 * sprite overlaps, rendering masked mids that are behind it on the way, then
 * draw the columns the drawsegs left open.
 */
static void draw_sprite(r_view_state_t* state, r_draw_surface_t* surface,
                        const r_tex_source_t* textures, const r_light_t* light,
                        const r_sprite_patch_t* patch, const r_patch_info_t* info,
                        r_vissprite_t* vis, uint8_t* pixelMask,
                        uint32_t* written) {
    int16_t clipbot[R_SCREEN_WIDTH];
    int16_t cliptop[R_SCREEN_WIDTH];
    uint16_t index;
    int x;
    const uint32_t seedStart = R_PROBE_HAS(state) ? R_PROBE_NOW() : 0u;

    for (x = vis->x1; x <= vis->x2; ++x) {
        clipbot[x] = R_SPRITE_CLIP_UNSET;
        cliptop[x] = R_SPRITE_CLIP_UNSET;
    }

    if (!vis->sprite->noClip) {
        for (index = state->drawsegCount; index-- > 0;) {
            r_drawseg_t* drawseg = &state->storage.drawsegs[index];
            fixed_t scale;
            fixed_t lowscale;
            int silhouette;
            int r1;
            int r2;

            if (R_PROBE_HAS(state)) {
                ++R_PROBE_OF(state)->maskedClipSeedSteps;
            }
            if (drawseg->x1 > vis->x2 || drawseg->x2 < vis->x1) {
                continue;
            }
            if (drawseg->silhouette == R_SIL_NONE
                && drawseg->maskedTextureCol == R_DRAWSEG_NO_BLOCK) {
                continue;
            }

            r1 = drawseg->x1 < vis->x1 ? vis->x1 : drawseg->x1;
            r2 = drawseg->x2 > vis->x2 ? vis->x2 : drawseg->x2;

            if (drawseg->scale1 > drawseg->scale2) {
                lowscale = drawseg->scale2;
                scale = drawseg->scale1;
            } else {
                lowscale = drawseg->scale1;
                scale = drawseg->scale2;
            }

            if (scale < vis->scale
                || (lowscale < vis->scale
                    && !point_on_seg_side(state->map, drawseg->seg, vis->gx,
                                          vis->gy))) {
                /* seg is behind sprite */
                if (drawseg->maskedTextureCol != R_DRAWSEG_NO_BLOCK) {
                    render_masked_range(state, surface, textures, light, drawseg,
                                        r1, r2, pixelMask, written);
                }
                continue;
            }

            silhouette = drawseg->silhouette;
            if (vis->gz >= drawseg->bottomSilhouette) {
                silhouette &= ~R_SIL_BOTTOM;
            }
            if (vis->gzt <= drawseg->topSilhouette) {
                silhouette &= ~R_SIL_TOP;
            }

            if (silhouette == R_SIL_BOTTOM) {
                for (x = r1; x <= r2; ++x) {
                    if (clipbot[x] == R_SPRITE_CLIP_UNSET) {
                        clipbot[x] = clip_value(state->storage.clipPool,
                                                      drawseg->sprBottomClip,
                                                      drawseg, x);
                    }
                }
            } else if (silhouette == R_SIL_TOP) {
                for (x = r1; x <= r2; ++x) {
                    if (cliptop[x] == R_SPRITE_CLIP_UNSET) {
                        cliptop[x] = clip_value(state->storage.clipPool,
                                                      drawseg->sprTopClip,
                                                      drawseg, x);
                    }
                }
            } else if (silhouette == R_SIL_BOTH) {
                for (x = r1; x <= r2; ++x) {
                    if (clipbot[x] == R_SPRITE_CLIP_UNSET) {
                        clipbot[x] = clip_value(state->storage.clipPool,
                                                      drawseg->sprBottomClip,
                                                      drawseg, x);
                    }
                    if (cliptop[x] == R_SPRITE_CLIP_UNSET) {
                        cliptop[x] = clip_value(state->storage.clipPool,
                                                      drawseg->sprTopClip,
                                                      drawseg, x);
                    }
                }
            }
        }
    }

    for (x = vis->x1; x <= vis->x2; ++x) {
        if (clipbot[x] == R_SPRITE_CLIP_UNSET) {
            clipbot[x] = (int16_t)R_VIEW_HEIGHT;
        }
        if (cliptop[x] == R_SPRITE_CLIP_UNSET) {
            cliptop[x] = -1;
        }
    }
    if (R_PROBE_HAS(state)) {
        R_PROBE_OF(state)->maskedClipSeedCycle += R_PROBE_NOW() - seedStart;
    }

    draw_vis_sprite(state, surface, light, vis, patch, info, clipbot, cliptop,
                    pixelMask, written);
}

/* --- Validation helpers ---------------------------------------------------- */

/* One clip code must name a run the pool actually holds (or one of the two
 * shared constants). A valid code is >= 0 and its whole fragment-relative span
 * is inside clipPoolCapacity: a drawseg's run is x2 - x1 + 1 entries, so a code
 * near the end with a wide span is as invalid as one past the end. */
static int saved_clip_ok(const r_view_state_t* state, int16_t code,
                         const r_drawseg_t* drawseg) {
    const uint32_t span = (uint32_t)(drawseg->x2 - drawseg->x1 + 1);

    if (code == R_DRAWSEG_CLIP_TOP || code == R_DRAWSEG_CLIP_BOTTOM) {
        return 1;
    }
    if (code < 0) {
        return 0;
    }
    if (state->storage.clipPool == NULL) {
        return 0;
    }
    return (uint32_t)code + span <= (uint32_t)state->storage.clipPoolCapacity;
}

/* A drawseg's saved columns must be inside the screen and inside the caller's
 * pool before anything is drawn. The wall pass produced them, so a violation is
 * a caller that changed the storage between the two passes. */
static r_things_error_t validate_drawseg_clips(const r_view_state_t* state,
                                               const r_drawseg_t* drawseg) {
    if (drawseg->x1 < 0 || drawseg->x2 >= R_SCREEN_WIDTH
        || drawseg->x1 > drawseg->x2) {
        return R_THINGS_ERR_MASKED_RANGE;
    }
    if (drawseg->sprTopClip != R_DRAWSEG_NO_BLOCK
        && !saved_clip_ok(state, drawseg->sprTopClip, drawseg)) {
        return R_THINGS_ERR_MASKED_RANGE;
    }
    if (drawseg->sprBottomClip != R_DRAWSEG_NO_BLOCK
        && !saved_clip_ok(state, drawseg->sprBottomClip, drawseg)) {
        return R_THINGS_ERR_MASKED_RANGE;
    }
    /* A masked mid's run is an offset, never one of the shared constants. */
    if (drawseg->maskedTextureCol != R_DRAWSEG_NO_BLOCK) {
        const uint32_t span = (uint32_t)(drawseg->x2 - drawseg->x1 + 1);

        if (drawseg->maskedTextureCol < 0 || state->storage.clipPool == NULL
            || (uint32_t)drawseg->maskedTextureCol + span
                   > (uint32_t)state->storage.clipPoolCapacity) {
            return R_THINGS_ERR_MASKED_RANGE;
        }
    }
    return R_THINGS_OK;
}

/* --- Public API ----------------------------------------------------------- */

r_things_error_t r_masked_emit(r_view_state_t* state, r_draw_surface_t* surface,
                               const r_tex_source_t* textures,
                               const r_light_t* light, const r_sprite_t* sprites,
                               uint16_t spriteCount,
                               const r_sprite_source_t* spriteSource,
                               r_masked_stats_t* outStats) {
    r_masked_stats_t stats;
    r_vissprite_t* visList;
    uint16_t visCount = 0u;
    uint16_t index;
    uint8_t* pixelMask;
    uint32_t pixels = 0u;
    r_sprite_proofs_t localProofs;
    r_sprite_proofs_t* proofs = NULL;
    int proofStartCount;

    if (state == NULL) {
        return R_THINGS_ERR_NULL_STATE;
    }
    if (state->map == NULL || state->map->open == 0) {
        return R_THINGS_ERR_MAP_NOT_OPEN;
    }
    if (surface == NULL) {
        return R_THINGS_ERR_NULL_SURFACE;
    }
    if (surface->pixels == NULL) {
        return R_THINGS_ERR_SURFACE_PIXELS;
    }
    if (surface->width < R_SCREEN_WIDTH || surface->height < R_VIEW_HEIGHT) {
        return R_THINGS_ERR_SURFACE_SIZE;
    }
    if (textures == NULL) {
        return R_THINGS_ERR_NULL_TEXTURES;
    }
    if (textures->resolve == NULL) {
        return R_THINGS_ERR_NULL_RESOLVE;
    }
    if (light == NULL || light->colormaps == NULL) {
        return R_THINGS_ERR_NULL_LIGHT;
    }
    if (state->clipCaptured == 0) {
        return R_THINGS_ERR_NOT_EMITTED;
    }
    if (sprites == NULL && spriteCount != 0u) {
        return R_THINGS_ERR_NULL_SPRITES;
    }
    if (spriteSource == NULL) {
        return R_THINGS_ERR_NULL_SOURCE;
    }
    if (spriteSource->resolve == NULL) {
        return R_THINGS_ERR_NULL_SPRITE_RESOLVE;
    }
    if (state->storage.visSprites == NULL || state->storage.visSpriteCapacity == 0) {
        return R_THINGS_ERR_NULL_VISSPRITES;
    }

    memset(&stats, 0, sizeof stats);
    stats.submitted = spriteCount;
    visList = state->storage.visSprites;
    proofs = state->storage.spriteProofs;
    if (proofs == NULL) {
        /* No caller-owned cache: prove each distinct patch once for this call. */
        localProofs.count = 0;
        proofs = &localProofs;
    }
    proofStartCount = proofs->count;

    /* Preflight: every sprite's patch, then its projection. Nothing is written
     * to the surface until both loops -- and the masked texture check below --
     * have passed. */
    for (index = 0; index < spriteCount; ++index) {
        const r_sprite_patch_t* patch = NULL;
        r_patch_info_t info;
        r_vissprite_t candidate;
        r_things_error_t result;
        uint32_t mark;
        int cull = 0;
        int accepted;

        if (textures->beginFragment != NULL) {
            textures->beginFragment(textures->context);
        }
        mark = R_PROBE_HAS(state) ? R_PROBE_NOW() : 0u;
        result = resolve_patch(spriteSource, sprites[index].lump, proofs, &patch, &info);
        if (R_PROBE_HAS(state)) {
            r_raster_probe_t* probe = R_PROBE_OF(state);

            probe->maskedPatchProofCycle += R_PROBE_NOW() - mark;
            ++probe->maskedProofCalls;
            mark = R_PROBE_NOW();
        }
        if (result != R_THINGS_OK) {
            return result;
        }
        accepted = project_sprite(state, &sprites[index], &info, &candidate, &cull);
        if (R_PROBE_HAS(state)) {
            R_PROBE_OF(state)->maskedProjectionCycle += R_PROBE_NOW() - mark;
        }
        if (!accepted) {
            if (cull == 1) {
                ++stats.backface;
            } else if (cull == 2) {
                ++stats.frustum;
            } else {
                ++stats.zerospan;
            }
            continue;
        }
        if (visCount >= state->storage.visSpriteCapacity) {
            return R_THINGS_ERR_VISSPRITE_OVERFLOW;
        }
        visList[visCount] = candidate;
        ++visCount;
    }
    stats.drawn = visCount;

    /* Every masked mid texture the pass may draw must resolve and be well
     * formed, and every drawseg's block indices must be inside the pools. */
    {
        const uint32_t checkStart = R_PROBE_HAS(state) ? R_PROBE_NOW() : 0u;

        for (index = 0; index < state->drawsegCount; ++index) {
            const r_drawseg_t* drawseg = &state->storage.drawsegs[index];
            r_things_error_t result = validate_drawseg_clips(state, drawseg);

            if (result != R_THINGS_OK) {
                return result;
            }
            if (drawseg->maskedTextureCol != R_DRAWSEG_NO_BLOCK) {
                r_wall_fragment_t fragment;
                const r_walltex_t* texture;

                r_wall_fragment_open(state->map, &state->params, drawseg->seg,
                                     &fragment);
                texture = textures->resolve(textures->context, fragment.midTexture);
                if (texture == NULL) {
                    return R_THINGS_ERR_TEXTURE_MISSING;
                }
                if (!r_walltex_is_usable(texture)) {
                    return R_THINGS_ERR_TEXTURE_SHAPE;
                }
            }
        }
        if (R_PROBE_HAS(state)) {
            R_PROBE_OF(state)->maskedRangeCheckCycle += R_PROBE_NOW() - checkStart;
        }
    }

    /* From here on the call cannot fail: every resource is proved. */
    pixelMask = surface->spriteMask;
    if (pixelMask != NULL) {
        const size_t bytes = (size_t)surface->width * (size_t)surface->height;

        memset(pixelMask, 0, bytes);
    }
    surface->spritePixels = 0u;
    if (R_PROBE_HAS(state)) {
        /* The proofs this call had to run, not the ones the cache answered: the
         * difference between the two counts is the point of the cache. */
        R_PROBE_OF(state)->maskedPatchProofs =
            (uint32_t)(proofs->count - proofStartCount);
    }

    {
        const uint32_t sortStart = R_PROBE_HAS(state) ? R_PROBE_NOW() : 0u;

        sort_vissprites(visList, visCount);
        if (R_PROBE_HAS(state)) {
            R_PROBE_OF(state)->maskedSortCycle += R_PROBE_NOW() - sortStart;
        }
    }

    for (index = 0; index < visCount; ++index) {
        const r_sprite_patch_t* patch = NULL;
        r_patch_info_t info;
        uint32_t mark;

        /* The resolver answered in the preflight; a second answer that differs
         * breaks the r_sprite_source_t contract, and the sprite is skipped
         * rather than dereferenced. The pin reset comes first, so the sprite
         * patch this draw holds cannot be evicted by a masked mid texture the
         * same draw composes. */
        if (textures->beginFragment != NULL) {
            textures->beginFragment(textures->context);
        }
        mark = R_PROBE_HAS(state) ? R_PROBE_NOW() : 0u;
        if (resolve_patch(spriteSource, visList[index].sprite->lump, proofs, &patch, &info)
            != R_THINGS_OK) {
            if (R_PROBE_HAS(state)) {
                R_PROBE_OF(state)->maskedPatchProofCycle += R_PROBE_NOW() - mark;
                ++R_PROBE_OF(state)->maskedProofCalls;
            }
            continue;
        }
        if (R_PROBE_HAS(state)) {
            R_PROBE_OF(state)->maskedPatchProofCycle += R_PROBE_NOW() - mark;
            ++R_PROBE_OF(state)->maskedProofCalls;
        }
        draw_sprite(state, surface, textures, light, patch, &info, &visList[index],
                    pixelMask, &pixels);
    }

    /* "render any remaining masked mid textures": far to near. */
    {
        const uint32_t tailStart = R_PROBE_HAS(state) ? R_PROBE_NOW() : 0u;

        for (index = state->drawsegCount; index-- > 0;) {
            r_drawseg_t* drawseg = &state->storage.drawsegs[index];

            if (drawseg->maskedTextureCol != R_DRAWSEG_NO_BLOCK) {
                if (textures->beginFragment != NULL) {
                    textures->beginFragment(textures->context);
                }
                render_masked_range(state, surface, textures, light, drawseg,
                                    drawseg->x1, drawseg->x2, pixelMask, &pixels);
            }
        }
        if (R_PROBE_HAS(state)) {
            R_PROBE_OF(state)->maskedTailCycle += R_PROBE_NOW() - tailStart;
        }
    }

    surface->spritePixels = pixels;
    stats.clipBlocksUsed = state->clipBlocksUsed;
    stats.openingBlocksUsed = state->openingBlocksUsed;
    stats.spritePixels = pixels;
    if (R_PROBE_HAS(state)) {
        const uint32_t calls = R_PROBE_OF(state)->maskedProofCalls;
        const uint32_t proved = R_PROBE_OF(state)->maskedPatchProofs;

        R_PROBE_OF(state)->maskedPatchMemoHits = calls > proved ? calls - proved : 0u;
    }
    if (outStats != NULL) {
        *outStats = stats;
    }
    return R_THINGS_OK;
}
