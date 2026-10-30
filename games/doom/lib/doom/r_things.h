/*
 * Sprites and the deferred masked mid textures: upstream's R_DrawMasked()
 * (r_things.c) plus R_RenderMaskedSegRange() (r_segs.c).
 *
 * WHAT THIS LAYER IS
 *
 * The last pass of a frame. It takes a caller-supplied list of sprites, projects
 * each one exactly as R_ProjectSprite() does, sorts the survivors back to front
 * exactly as R_SortVisSprites(), and draws them over the wall and plane output
 * with the drawseg-derived clipping of R_DrawSprite(). While it draws, it also
 * renders the masked mid textures the wall pass recorded -- R_RenderMaskedSegRange
 * -- so that a transparent middle texture ends up in the right painter order
 * against the sprites on both sides of it.
 *
 * THE INPUT IS DATA, NOT GAME STATE
 *
 * The renderer knows nothing about mobjs, states or info.c. A caller describes a
 * sprite with r_sprite_t: where it is, which way its thing faces, which already
 * *translated* patch lump it draws, whether that patch is mirrored, whether it
 * is full bright, whether it ignores the drawseg clip, and the light level of
 * the sector it stands in. Rotation selection -- upstream's
 * `sprframe->lump[(ang-thing->angle+... )>>29]` -- needs the sprite definition
 * table (spriteframes, numframes, firstspritelump), which is game data, so it is
 * the caller's. Phase 4 builds this list the way R_AddSprites() does, from the
 * sectors the BSP walk visited; this pass must not.
 *
 * THE FRAME ORDER, AND WHY THIS PASS RUNS LAST
 *
 *   r_bsp_render()    the walk.
 *   r_wall_emit()     the wall tiers; also the clip snapshot and the masked
 *                     texture columns r_masked_emit() consumes.
 *   r_plane_draw()    flats and the sky.
 *   r_masked_emit()   this pass: sprites and masked mid textures.
 *
 * R_RenderPlayerView() calls them in exactly that order. The pass refuses a
 * state whose wall pass has not run (`state->clipCaptured == 0`), because the
 * drawsegs would hold no clip blocks and every sprite would be drawn unclipped.
 *
 * PAINTER ORDER INSIDE R_DrawMasked(), WHICH IS NOT "MIDS THEN SPRITES"
 *
 * The sprites are sorted by ascending scale (farthest first) and drawn in that
 * order. For each sprite, R_DrawSprite() scans the drawsegs from the last stored
 * (farthest) to the first (nearest). A drawseg whose scale is smaller than the
 * sprite's is behind it, and if that drawseg carries a masked mid texture, its
 * covered columns are rendered *immediately*, before the sprite, because the
 * mid is behind the sprite and the sprite must win. A drawseg whose scale is
 * not smaller clips the sprite instead, and its masked mid is left for the final
 * loop, which paints the untouched columns far to near after every sprite --
 * so a mid in front of a sprite wins over it. The net rule is per column and per
 * pair: whichever of the two is nearer is on top, and each masked column is
 * painted exactly once (R_MASKED_COLUMN_USED). "Draw the mids first, then the
 * sprites" is not that rule and would let a sprite in the back sector paint over
 * a mid texture in front of it.
 *
 * PROJECTION, R_ProjectSprite(), FIELD BY FIELD
 *
 *   tr_x/tr_y   sprite position - viewpoint.
 *   gxt/gyt     FixedMul(tr_x, viewcos), -FixedMul(tr_y, viewsin).
 *   tz          gxt - gyt: the distance along the view direction. `tz < MINZ`
 *               (4 map units) is the back-face rejection.
 *   xscale      FixedDiv(projection, tz), the sprite's projection scale. It is
 *               also the sprite light's column (r_light.h).
 *   tx          -(FixedMul(tr_x, viewsin) + FixedMul(tr_y, viewcos)); `abs(tx) >
 *               tz << 2` is upstream's off-the-side rejection (a 53-degree
 *               half-angle cut before the exact column test).
 *   x1/x2       the patch's left and right edge projected:
 *               `(centerxfrac + FixedMul(tx - leftoffset, xscale)) >> FRACBITS`,
 *               then `tx += width`, and x2 is that minus one. `x1 > viewwidth`
 *               and `x2 < 0` are the frustum rejections; the stored x1/x2 are
 *               clamped to [0, viewwidth-1].
 *   scale       xscale (detailshift 0).
 *   texturemid  `gzt - viewz`, where gzt = z + patch->topoffset.
 *   xiscale     `FixedDiv(FRACUNIT, xscale)`, negated for a flipped sprite.
 *   startfrac   0, or `width - 1` for a flipped sprite, then advanced by
 *               `xiscale * (stored x1 - projected x1)` when the left edge was
 *               clamped away.
 *   lightnum    `r_light_row((sprite->lightLevel >> LIGHTSEGSHIFT) +
 *               extralight)`, the row of the same scalelight table the walls
 *               use. See r_light.h for why the sprite row and column differ.
 *
 * The sprite is *not* transformed by its own `angle`; that field is carried for
 * the caller's rotation selection and for a future debug view. `flip` and the
 * lump are already the result of that selection (`sprframe->flip[rot]` and
 * `sprframe->lump[rot]`).
 *
 * CLIPPING, R_DrawSprite()
 *
 * Two screen-wide short arrays, clipbot[] and cliptop[], start at -2 ("no
 * drawseg has said anything about this column"). Every drawseg overlapping the
 * sprite contributes at most one value per column and never overwrites one
 * another value: the first drawseg that names an edge wins, and the scan runs
 * far to near, so the *nearest* drawseg that can clip that edge is the one that
 * does. Which edges it may contribute is its silhouette, after two world-height
 * tests against the sprite's own extent:
 *
 *     if (sprite->z  >= ds->bottomSilhouette) silhouette &= ~SIL_BOTTOM;
 *     if (sprite->gz + topoffset <= ds->topSilhouette) silhouette &= ~SIL_TOP;
 *
 * A drawseg whose scale is smaller than the sprite's is behind it and clips
 * nothing; upstream's tie-break, when only its *low* scale is smaller, is
 * R_PointOnSegSide(sprite position, seg): a sprite on the front side of a wall
 * that is nearly edge-on is in front of it. Columns no drawseg named are set to
 * viewheight (bottom) and -1 (top), i.e. unclipped.
 *
 * The clip arrays live in caller-supplied blocks (r_view_storage_t): block 0 and
 * block 1 are the constant "solid" arrays every single-sided wall and every
 * closed two-sided line points at, and every other block holds one fragment's
 * columns at offsets 0 .. x2-x1. Index 0 is a valid block, so every test for
 * "no block" is `== R_DRAWSEG_NO_BLOCK`, never a truth test.
 *
 * DRAWING, R_DrawVisSprite() + R_DrawMaskedColumn()
 *
 * Per column, the patch's post chain is walked: each post gives a top delta and
 * a length, the two screen rows are `(sprtopscreen + scale*topdelta + FRACUNIT-1)
 * >> FRACBITS` and `(sprtopscreen + scale*(topdelta+length) - 1) >> FRACBITS`,
 * the clip arrays narrow them (`yh = min(yh, clipbot[x]-1)`,
 * `yl = max(yl, cliptop[x]+1)`), and the post's pixels are read with
 * `frac = texturemid - (topdelta<<FRACBITS) + (yl - centery)*iscale`, advancing
 * by iscale per row and wrapping the post-relative index with upstream's
 * hard-coded `& 127`. A gap between posts and a row a post never reaches write
 * nothing: a patch is a mask, not a rectangle. The colormap is the sprite's
 * table, or map 0 for a full-bright sprite.
 *
 * This is where the port must keep upstream's `& 127` rather than a stored
 * height: a patch post is not a dense 128-row texture column, so `& 127` is the
 * index into the post's own pixel run. The shareware IWAD's longest post is 128
 * bytes, so no pixel is affected, but the mask is written out because it is the
 * rule. (`r_segs.c` documents the opposite choice for wall textures, where the
 * stored column *is* dense.)
 *
 * THE MASKED MID TEXTURES, R_RenderMaskedSegRange()
 *
 * For a two-sided line whose sidedef names a mid texture, the wall pass stored
 * one texture column per covered screen column in an opening block and set the
 * drawseg's silhouette to both edges (so sprites behind it are clipped to the
 * line's opening, not to the whole screen). This pass draws those columns:
 *
 *   scale      `ds->scale1 + (x1 - ds->x1) * ds->scaleStep`, advanced per column.
 *   light      the *wall* rule: ds->lightNum (sector light, extralight and the
 *              horizontal/vertical correction) and `scale >> LIGHTSCALESHIFT`.
 *   texturemid ML_DONTPEGBOTTOM: max(front floor, back floor) + texture height,
 *              view relative; otherwise min(front ceiling, back ceiling), view
 *              relative; then + the sidedef's rowoffset. The wall fragment
 *              already carries those four heights view relative (worldBottom /
 *              worldLow, worldTop / worldHigh), so this pass needs no map read
 *              for them.
 *   clip       the drawseg's own saved clip blocks, exactly R_RenderMaskedSegRange's
 *              `mfloorclip = ds->sprbottomclip; mceilingclip = ds->sprtopclip`.
 *   column     the opening entry, masked by the texture's width mask; an entry
 *              equal to R_MASKED_COLUMN_USED is skipped and then marked, so a
 *              column is painted once.
 *
 * The texture is a dense r_walltex_t with its optional mask plane, the same
 * record the wall pass draws, so the transparent texels of a composite are the
 * mask's zero bytes rather than a post chain. r_segs.h's r_column_draw() is the
 * shared write; the two passes differ only in which clip arrays produce yl/yh.
 *
 * ERROR CONTRACT
 *
 * Arguments first, then -- before a single pixel -- every sprite's patch
 * (resolvable and a valid patch), every drawseg's masked mid texture, every
 * drawseg's clip block index, and the vis-sprite capacity. A failure therefore
 * leaves the surface, its coverage record, its sprite mask and its counters
 * byte for byte as the caller left them. The pass never writes past the sprite
 * array, the clip pool or an opening block; a frame that does not fit is
 * R_THINGS_ERR_VISSPRITE_OVERFLOW or the pool code r_wall_emit() already
 * returned.
 *
 * This is C99 only: <stddef.h> and <stdint.h>, no allocation, no globals, no
 * stdio, no floating point. The sprite clip arrays are two 320-short stack
 * locals (1,280 B), the same shape upstream uses.
 */
#ifndef DOOM_R_THINGS_H
#define DOOM_R_THINGS_H

#include <stddef.h>
#include <stdint.h>

#include "r_bsp.h"
#include "r_defs.h"
#include "r_light.h"
#include "r_segs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One projected sprite per entry, DOOM's MAXVISSPRITES. The caller's storage
 * array may be smaller or larger; going over is a reported error here, while
 * upstream answers with a single shared "overflowsprite" object. */
#define R_MAX_VISSPRITES 128

/* R_ProjectSprite's MINZ: a sprite closer than four map units along the view
 * direction is behind the view plane. */
#define R_SPRITE_MINZ ((fixed_t)4 << FRACBITS)

/* R_DrawSprite's "no drawseg has named this column" sentinel. */
#define R_SPRITE_CLIP_UNSET ((int16_t)-2)

/* R_DrawColumn's `& 127` for a patch post's pixel run. */
#define R_SPRITE_POST_MASK 127

/* --- The caller's sprite ------------------------------------------------- */

/*
 * One sprite, as the renderer needs it. Everything else R_ProjectSprite() reads
 * from a mobj -- flags, state, sprite/frame -- is either already resolved by the
 * caller or is game data this layer must not know.
 *
 *   x, y, z       world position, 16.16. `z` is the *bottom* of the sprite
 *                 (upstream's `thing->z`); the top comes from the patch's
 *                 topoffset.
 *   angle         the thing's facing, a binary angle. Carried for the caller's
 *                 rotation selection and reporting; this pass does not read it.
 *   lump          the translated sprite patch lump's packed name, i.e.
 *                 `W_CacheLumpNum(vis->patch + firstspritelump)` after the
 *                 rotation selection. The resolver turns it into bytes.
 *   lightLevel    the light level of the sector the sprite stands in, which is
 *                 the row R_AddSprites() would pick.
 *   flip          `sprframe->flip[rotation]`: the selected lump is mirrored.
 *   fullbright    the frame's FF_FULLBRIGHT: draw through COLORMAP map 0.
 *   noClip        port-only: skip the drawseg clip scan. Vanilla has no such
 *                 flag, and the fixtures pass 0; it exists so a test (or a
 *                 future debug overlay) can prove the rest of the pipeline
 *                 without a wall in the way.
 */
typedef struct r_sprite {
    fixed_t x;
    fixed_t y;
    fixed_t z;
    angle_t angle;
    r_name_t lump;
    int16_t lightLevel;
    uint8_t flip;
    uint8_t fullbright;
    uint8_t noClip;
} r_sprite_t;

/* One patch lump, in the form r_data.h's decoder takes: the raw WAD bytes. The
 * renderer validates it with r_patch_validate(), which also proves the column
 * directory and every post before the first pixel. */
typedef struct r_sprite_patch {
    const uint8_t* data;
    size_t size;
} r_sprite_patch_t;

/**
 * @brief Resolves a packed sprite lump name to its patch bytes, or NULL.
 *
 * Called once per sprite in the pass's preflight and once more when the sprite
 * is drawn -- never per column. The returned record must stay valid until
 * r_masked_emit() returns; the emitter copies nothing.
 *
 * The policy behind the callback -- a cache, a mapped flash region, "decode it
 * now" -- is the caller's. Answering NULL is a normal outcome and produces
 * R_THINGS_ERR_SPRITE_MISSING, not a partial frame.
 */
typedef const r_sprite_patch_t* (*r_sprite_resolve_fn)(void* context, r_name_t lump);

/* How a pass finds its patches. `resolve` is required; `context` is handed back
 * unchanged and may be NULL. */
typedef struct r_sprite_source {
    r_sprite_resolve_fn resolve;
    void* context;
} r_sprite_source_t;

/* --- The projected sprite ------------------------------------------------- */

/*
 * One accepted sprite, upstream's vissprite_t minus the fields the caller
 * already holds. It is what the sort orders and the draw loop reads; it lives in
 * the caller's r_view_storage_t::visSprites array.
 *
 * `sprite` points into the caller's r_sprite_t array, which must outlive the
 * call. The rest is R_ProjectSprite()'s output, documented in the header above.
 */
typedef struct r_vissprite {
    const r_sprite_t* sprite;
    fixed_t scale;
    fixed_t xiscale;
    fixed_t startfrac;
    fixed_t texturemid;
    fixed_t gx;
    fixed_t gy;
    fixed_t gz;
    fixed_t gzt;
    int16_t x1;
    int16_t x2;
    int16_t lightnum;
} r_vissprite_t;

/* --- Result counters ------------------------------------------------------ */

/*
 * What one call did, for the VISSTAT line of the frame protocol and for the
 * tests. `submitted` is the caller's count, `drawn` the number of sprites that
 * survived projection with at least one column, and the three culls are
 * disjoint: backface (`tz < MINZ`), frustum (the off-the-side test, or both
 * projected edges off screen) and zerospan (accepted, but the clamped columns
 * are empty). `clipBlocksUsed` and `openingBlocksUsed` are state->clipBlocksUsed
 * and state->openingBlocksUsed, the wall pass's peak, repeated here so one line
 * carries the whole frame's pool use. `spritePixels` counts the pixels this pass
 * wrote, masked mids included.
 */
typedef struct r_masked_stats {
    uint16_t submitted;
    uint16_t drawn;
    uint16_t backface;
    uint16_t frustum;
    uint16_t zerospan;
    uint16_t clipBlocksUsed;
    uint16_t openingBlocksUsed;
    uint32_t spritePixels;
} r_masked_stats_t;

/* --- Error codes --------------------------------------------------------- */

/*
 *   R_THINGS_OK                    the pass ran and wrote the surface
 *   R_THINGS_ERR_NULL_STATE        no view state
 *   R_THINGS_ERR_MAP_NOT_OPEN      the state holds no walked map
 *   R_THINGS_ERR_NULL_SURFACE      no surface descriptor
 *   R_THINGS_ERR_SURFACE_PIXELS    surface->pixels is NULL
 *   R_THINGS_ERR_SURFACE_SIZE      width < R_SCREEN_WIDTH or height < R_VIEW_HEIGHT
 *   R_THINGS_ERR_NULL_TEXTURES     no wall/flat source (the masked mids need it)
 *   R_THINGS_ERR_NULL_RESOLVE      the source has no wall resolver
 *   R_THINGS_ERR_NULL_LIGHT        no initialised r_light_t
 *   R_THINGS_ERR_NOT_EMITTED       r_wall_emit() has not completed on this state
 *   R_THINGS_ERR_NULL_SPRITES      sprites is NULL while spriteCount is not 0
 *   R_THINGS_ERR_NULL_SOURCE       no sprite resolver source
 *   R_THINGS_ERR_NULL_SPRITE_RESOLVE the source has no resolver
 *   R_THINGS_ERR_NULL_VISSPRITES   storage.visSprites is NULL, or its capacity 0
 *   R_THINGS_ERR_VISSPRITE_OVERFLOW more sprites survive projection than the
 *                                  caller's array holds
 *   R_THINGS_ERR_SPRITE_MISSING    the resolver answered NULL for a lump
 *   R_THINGS_ERR_SPRITE_SHAPE      a resolved patch is unusable: NULL/short
 *                                  bytes, or r_patch_validate() rejects it
 *   R_THINGS_ERR_TEXTURE_MISSING   a masked mid texture does not resolve
 *   R_THINGS_ERR_TEXTURE_SHAPE     a masked mid texture is malformed, by the
 *                                  same geometry rules as a wall texture
 *   R_THINGS_ERR_MASKED_RANGE      a drawseg's columns are outside the screen,
 *                                  or one of its clip/opening block indices is
 *                                  outside the caller's pool
 *
 * Every one is returned before the first pixel write. The vis-sprite array may
 * have been reordered or trimmed by then, but the surface and its records are
 * untouched.
 */
typedef enum r_things_error {
    R_THINGS_OK = 0,
    R_THINGS_ERR_NULL_STATE = 1,
    R_THINGS_ERR_MAP_NOT_OPEN = 2,
    R_THINGS_ERR_NULL_SURFACE = 3,
    R_THINGS_ERR_SURFACE_PIXELS = 4,
    R_THINGS_ERR_SURFACE_SIZE = 5,
    R_THINGS_ERR_NULL_TEXTURES = 6,
    R_THINGS_ERR_NULL_RESOLVE = 7,
    R_THINGS_ERR_NULL_LIGHT = 8,
    R_THINGS_ERR_NOT_EMITTED = 9,
    R_THINGS_ERR_NULL_SPRITES = 10,
    R_THINGS_ERR_NULL_SOURCE = 11,
    R_THINGS_ERR_NULL_SPRITE_RESOLVE = 12,
    R_THINGS_ERR_NULL_VISSPRITES = 13,
    R_THINGS_ERR_VISSPRITE_OVERFLOW = 14,
    R_THINGS_ERR_SPRITE_MISSING = 15,
    R_THINGS_ERR_SPRITE_SHAPE = 16,
    R_THINGS_ERR_TEXTURE_MISSING = 17,
    R_THINGS_ERR_TEXTURE_SHAPE = 18,
    R_THINGS_ERR_MASKED_RANGE = 19
} r_things_error_t;

/* --- Public API ---------------------------------------------------------- */

/**
 * @brief Draws sprites and the deferred masked mid textures into @p surface.
 *
 * The last pass of the frame; run it after r_wall_emit() and r_plane_draw() on
 * the same state. It projects and sorts @p sprites into
 * `state->storage.visSprites`, then reproduces R_DrawMasked():
 *
 *   1. draw the accepted sprites back to front, each one first rendering the
 *      masked mids of the drawsegs behind it over the columns it covers;
 *   2. draw the remaining masked mid columns, farthest drawseg to nearest;
 *   3. write the stats.
 *
 * The surface must be at least R_SCREEN_WIDTH x R_VIEW_HEIGHT; `width` is its
 * row stride. `surface->spriteMask`, when not NULL, is cleared and then set to 1
 * for every pixel this pass writes; `surface->spritePixels` is reset and
 * counted; `surface->covered` is set for every column written (it is not
 * cleared -- the wall pass owns that record). Pixels nothing in this pass covers
 * keep whatever the planes left there.
 *
 * Validation order:
 *
 *   1. state == NULL                            R_THINGS_ERR_NULL_STATE
 *   2. state->map == NULL or not open           R_THINGS_ERR_MAP_NOT_OPEN
 *   3. surface == NULL                          R_THINGS_ERR_NULL_SURFACE
 *   4. surface->pixels == NULL                  R_THINGS_ERR_SURFACE_PIXELS
 *   5. surface->width/height too small          R_THINGS_ERR_SURFACE_SIZE
 *   6. textures == NULL                         R_THINGS_ERR_NULL_TEXTURES
 *   7. textures->resolve == NULL                R_THINGS_ERR_NULL_RESOLVE
 *   8. light == NULL or no colormaps            R_THINGS_ERR_NULL_LIGHT
 *   9. state->clipCaptured == 0                 R_THINGS_ERR_NOT_EMITTED
 *  10. sprites NULL with a non-zero count       R_THINGS_ERR_NULL_SPRITES
 *  11. spriteSource NULL / no resolver          R_THINGS_ERR_NULL_SOURCE /
 *                                               R_THINGS_ERR_NULL_SPRITE_RESOLVE
 *  12. visSprites NULL or capacity 0            R_THINGS_ERR_NULL_VISSPRITES
 *  13. a sprite's patch does not resolve        R_THINGS_ERR_SPRITE_MISSING
 *  14. a sprite's patch is malformed            R_THINGS_ERR_SPRITE_SHAPE
 *  15. more sprites survive projection than the
 *      vis-sprite array holds                 R_THINGS_ERR_VISSPRITE_OVERFLOW
 *  16. a masked mid texture does not resolve    R_THINGS_ERR_TEXTURE_MISSING
 *  17. a masked mid texture is malformed        R_THINGS_ERR_TEXTURE_SHAPE
 *  18. a drawseg's range or block index is
 *      outside the pools                       R_THINGS_ERR_MASKED_RANGE
 *
 * Steps 13 to 18 run for every sprite and every masked drawseg before the first
 * pixel is written.
 *
 * @param state        A state whose r_bsp_render() and r_wall_emit() have run.
 * @param surface      The destination surface and its records.
 * @param textures     The wall texture source the masked mids resolve through.
 * @param light        A COLORMAP initialised by r_light_init().
 * @param sprites      The caller's sprite list; may be NULL when count is 0.
 * @param spriteCount  Its length.
 * @param spriteSource How a sprite lump becomes patch bytes.
 * @param outStats     Optional result counters; untouched on failure.
 * @return R_THINGS_OK, or one of the codes above.
 */
r_things_error_t r_masked_emit(r_view_state_t* state, r_draw_surface_t* surface,
                               const r_tex_source_t* textures,
                               const r_light_t* light, const r_sprite_t* sprites,
                               uint16_t spriteCount,
                               const r_sprite_source_t* spriteSource,
                               r_masked_stats_t* outStats);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_R_THINGS_H */
