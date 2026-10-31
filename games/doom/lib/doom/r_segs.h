/*
 * The wall column emitter: the second pass over the drawsegs, from upstream's
 * R_RenderSegLoop() (r_segs.c) and R_DrawColumn() (r_draw.c).
 *
 * WHAT THIS LAYER IS
 *
 * The BSP walk (r_bsp.c) decided, per visible wall fragment, which screen
 * columns it occupies, the texture scale at both ends, the three world heights
 * that bound it, which of its three tiers exist, and how brightly it is lit. It
 * wrote no pixel. This file takes that list -- in the order the walk stored it,
 * near to far -- and reproduces R_RenderSegLoop() column for column: the clip
 * array updates, the three tiers, the texture column, the light table and the
 * scaled column write. The output is palette indices in a caller-owned surface.
 *
 * THE TWO-PASS SHAPE, AND WHERE THE FRAGMENT LIVES
 *
 * Upstream does both halves inside one R_StoreWallRange() call and keeps the
 * fragment in file-scope variables. This port splits it: the walk stores a
 * drawseg, and the emitter re-opens the same fragment from that drawseg later.
 * The reason is measured in bytes. Of everything the column loop reads, only
 * three values (rwDistance, rwOffset, rwCenterAngle) are both expensive to
 * recompute and impossible to recover from the drawseg's other fields, so only
 * those three join r_drawseg_t (36 B -> 48 B, 9,216 B -> 12,288 B for the
 * 256-entry array). The other 75 B of fragment state -- the four world heights,
 * the three texturemids, the eight steppers, the plane marks and the tier flags
 * -- lives in a stack-local r_wall_fragment_t that r_wall_fragment_open()
 * (r_bsp.h) refills for each drawseg. Storing it would have cost 19,456 B per
 * view of DRAM for data that only this file reads. The boundary is documented in
 * r_bsp.h from the other side.
 *
 * No rule is implemented twice: the two-sided height/picture predicate that
 * decides worldhigh/worldlow, markFloor/markCeiling and the tier flags is
 * r_wall_fragment_open(), which is the same function the walk calls, and the
 * column steppers and pegging are r_wall_fragment_step() and
 * r_wall_fragment_peg().
 *
 * THE COLUMN LOOP, RULE BY RULE, AND WHERE EACH COMES FROM
 *
 *   clip arrays     ceilingclip[]/floorclip[] are reset to -1 / R_VIEW_HEIGHT
 *                   before the pass, exactly as R_ClearPlanes() (r_plane.c)
 *                   does at the start of R_RenderPlayerView(). Each column then
 *                   updates them exactly as R_RenderSegLoop() does.
 *   yl, yh          `yl = (topfrac + HEIGHTUNIT-1) >> HEIGHTBITS`, clamped up to
 *                   ceilingclip[x]+1; `yh = bottomfrac >> HEIGHTBITS`, clamped
 *                   down to floorclip[x]-1. HEIGHTBITS is 12, so the fractions
 *                   are 12.4 screen positions.
 *   the three tiers R_RenderSegLoop() draws at most three of them, in this
 *                   order and with these rules:
 *                     mid    (single-sided line only) rows yl..yh, then the
 *                            column becomes solid: ceilingclip = viewheight,
 *                            floorclip = -1.
 *                     upper  rows yl..pixhigh, where pixhigh is clamped to
 *                            floorclip-1, and only if pixhigh >= yl; either way
 *                            ceilingclip becomes the tier's last row or yl-1.
 *                     lower  rows pixlow..yh, where pixlow is clamped up to
 *                            ceilingclip+1, and only if pixlow <= yh; either
 *                            way floorclip becomes the tier's first row or
 *                            yh+1.
 *                   When a tier does not exist, the clip array still moves if
 *                   the plane behind it is marked (markCeiling/markFloor).
 *   sky             There is no sky branch in this loop, because there is none
 *                   in R_RenderSegLoop(): the sky is a *plane*, drawn by
 *                   R_DrawPlanes (D3.3), and the only thing the wall pass owes
 *                   it is the outdoor height hack, which is part of the shared
 *                   fragment predicate. See r_wall_fragment_open() and the
 *                   proof below.
 *   texture column  `texturecolumn = rw_offset - FixedMul(finetangent[angle],
 *                   rw_distance)`, then `>>= FRACBITS`; the horizontal wrap is
 *                   `& texturewidthmask` (r_defs.h) and the vertical one is
 *                   `& heightMask`. The angle is `rw_centerangle +
 *                   xtoviewangle[x]` shifted down by ANGLETOFINESHIFT, and
 *                   r_fine_tangent_lookup() (r_tables.h) proves the index stays
 *                   inside the table.
 *   colormap        `walllights[index]` with `index = rw_scale >> LIGHTSCALESHIFT`
 *                   clamped to MAXLIGHTSCALE-1, where walllights is the row
 *                   r_light_wall() selects from the drawseg's lightNum.
 *   the write       `dc_iscale = 0xffffffffu / (unsigned)rw_scale`, and
 *                   `frac = dc_texturemid + (dc_yl - centery) * dc_iscale`,
 *                   advancing by dc_iscale per row, reading
 *                   `colormap[column[(frac >> FRACBITS) & heightMask]]` into
 *                   `pixels[x + y*width]`. centery is R_CENTER_Y (r_defs.h).
 *
 * THE SKY EQUIVALENCE, PROVED
 *
 * Upstream's wall code asks whether the *plane* it is looking through is the sky
 * (`frontsector->ceilingpic == skyflatnum`, r_segs.c R_StoreWallRange). This
 * layer has no plane pointer -- the visplanes are the walk's output, and the
 * emitter must not depend on which of them the walk happened to allocate -- so
 * the predicate asks the *sector* instead. The two questions have the same
 * answer, for two reasons that are both in the code:
 *
 *   1. R_FindPlane() keys a plane on (height, picnum, lightlevel) and stores a
 *      sky plane as (0, skyflatnum, 0). The picture is *part of the key*, so no
 *      sector whose ceilingpic is not the sky flat can ever be folded into the
 *      sky plane, and the sky plane's picnum is skyflatnum exactly when the
 *      sector's ceilingpic is. `ceilingplane->picnum == skyflatnum` iff
 *      `frontsector->ceilingpic == skyflatnum`.
 *   2. The wall pass marks columns of a plane the walk found with the *front*
 *      sector's own key, so on the fragment that matters the plane the wall
 *      would have tested is the plane its front sector resolves to. The
 *      substitution therefore cannot reach a different plane.
 *
 * Floors work the same way (R_FindPlane() is one function for both), and the
 * port's own r_plane_find() implements the same key rule.
 *
 * WHAT IS DELIBERATELY NOT HERE
 *
 *   - the masked mid texture of a two-sided line is not *drawn*: R_RenderSegLoop()
 *     does not draw it either. D3.4 records its texture column here, because this
 *     is the pass that runs the column loop, and draws it in r_things.c's
 *     R_DrawMasked() after the planes. The storage (upstream's openings[]) is a
 *     caller-owned pool in r_view_storage_t.
 *   - flats, sprites, the sky texture, the status bar, the palette, the present
 *     path. The plane pass (D3.3) draws the flats and the sky in r_plane.c;
 *     sprites and the masked mid textures are D3.4.
 *   - any texture decoding or residency policy. A tier's texture arrives as an
 *     r_walltex_t through r_tex_source_t, and the *caller* decides where the
 *     bytes come from and which of them are resident. D3.3's flats use the same
 *     source's resolveFlat, with the same rule.
 *   - any mutable global: the state is the caller's r_view_state_t, the surface
 *     is the caller's buffer, the light lump is the caller's bytes.
 *
 * THE CLIP CAPTURE AND THE MASKED RECORDING (D3.4)
 *
 * Upstream's R_StoreWallRange(), after R_RenderSegLoop() has run, copies the
 * clip arrays into openings[] for every drawseg a sprite behind it may be
 * clipped against, records the texture column of each two-sided line's mid
 * texture *during* the loop, and then completes the drawseg's silhouette with
 * the two edges the masked texture forces. This file does the same, in the same
 * place, because it is the pass that reproduces the clip array updates:
 *
 *   top clip     if the fragment is not single-sided and is not one of the two
 *                "closed" cases, and (the geometric silhouette has SIL_TOP or
 *                the line has a masked mid texture), the current ceilingclip[]
 *                over [x1, x2] is copied into a run of `x2 - x1 + 1` entries in
 *                the caller's single clip pool. A single-sided line, and a
 *                closed two-sided one, answer with the shared constant
 *                R_DRAWSEG_CLIP_TOP instead (upstream points at
 *                `screenheightarray`).
 *   bottom clip  the same for floorclip[] and R_DRAWSEG_CLIP_BOTTOM
 *                (`negonearray`).
 *   masked mid   a two-sided line whose sidedef names a mid texture gets a run
 *                of `x2 - x1 + 1` entries; the column loop writes each column's
 *                texture column into it, exactly
 *                `maskedtexturecol[rw_x] = texturecolumn`. A column is consumed
 *                (set to R_MASKED_COLUMN_USED) when the masked mid is drawn,
 *                which is how R_DrawMasked() avoids drawing it twice.
 *
 * The pool is upstream's `openings[]`: one bump-allocated `short` array, so a
 * clipped edge costs its own span rather than a 320-entry block. The two
 * "everything" answers are not entries at all; they are the negative
 * R_DRAWSEG_CLIP_TOP/BOTTOM codes, which keeps every pool entry variable-length
 * wall data and keeps the reserved pair from costing DRAM.
 *   silhouette   a masked mid forces both edges: if the geometric silhouette
 *                did not have SIL_TOP it gains it and `tsilheight` becomes
 *                INT32_MIN; the same for SIL_BOTTOM and INT32_MAX. This happens
 *                in this pass, not in the walk, so the D3.1 drawseg record and
 *                its oracle stay byte-identical.
 *
 * The pool demand is computed in the preflight, before the first pixel, from the
 * same predicate the save uses. A pool that cannot hold the frame's entries is
 * therefore a reported error with the surface untouched, never an overrun, and
 * the column loop itself allocates nothing.
 *
 * THE PREFLIGHT AND THE DRAW ARE TWO CALLS (D4.2h)
 *
 * r_wall_preflight() is the last gate before the frame's first pixel, and the
 * frame driver needs to run it *before* it prefills the surface: a refusal after
 * the 0xFF prefill leaves the white or half-drawn frame the panel showed. So the
 * pass is exposed as preflight + draw, and r_wall_emit() is the two calls in
 * order for every caller that does not need the seam. The plan the preflight
 * returns is the proof the draw consumes; it carries the frame's clip-pool
 * demand and the block-equivalent counters SPRSTAT reports.
 *
 * ERROR CONTRACT
 *
 * r_wall_emit() validates every argument, then resolves and validates *every*
 * texture the pass will draw, and only then writes the first byte. A missing or
 * malformed texture therefore leaves the surface byte for byte as the caller
 * left it -- which is what makes "not resident" a reportable condition rather
 * than a half-painted frame. The preflight resolves a name once per call rather
 * than once per fragment (R_SEGS_PREFLIGHT_MEMO), so the proof costs one resolve
 * per distinct texture; the draw loop still resolves every tier, which is what
 * keeps the resolver's lifetime contract local to one fragment.
 *
 * This is C99 only: <stddef.h> and <stdint.h>, no allocation, no globals, no
 * stdio, no floating point.
 */
#ifndef DOOM_R_SEGS_H
#define DOOM_R_SEGS_H

#include <stddef.h>
#include <stdint.h>

#include "r_bsp.h"
#include "r_defs.h"
#include "r_light.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Textures ------------------------------------------------------------ */

/*
 * One wall texture in the form the column loop reads: `pixels` is column major
 * and dense, exactly the layout r_texture_compose() produces -- pixel (x, y) at
 * pixels[x * columnHeight + y], so a texture column is a contiguous run and the
 * inner loop is `column[frac >> FRACBITS]`.
 *
 *   width           in texels.
 *   height          the *declared* height of the texture, as DOOM's
 *                   textureheight[] has it. This is the number the pegging uses
 *                   (r_wall_fragment_peg), and it is not necessarily a power of
 *                   two: thirteen textures in the shareware IWAD are 24, 56, 72,
 *                   96 or 112 rows tall, and four of them appear in E1M1.
 *   heightMask      the wrap mask of the stored column, i.e. the storage height
 *                   minus one, which must be 2^k - 1. DOOM's R_DrawColumn()
 *                   wraps with a hard-coded `& 127`, so vanilla always stores
 *                   128 rows per column whatever the texture's height; a caller
 *                   with power-of-two textures may store exactly `height` rows
 *                   and pass `height - 1`. `pixels` must hold
 *                   width * (heightMask + 1) bytes.
 *   widthMask       DOOM's texturewidthmask: the largest power of two that does
 *                   not exceed width, minus one (r_texture_width_mask()). It is
 *                   carried rather than recomputed so the column loop does no
 *                   division; the emitter validates it against the rule.
 *   mask            optional, one byte per texel, 0 = transparent (the pixel is
 *                   not written and the surface keeps whatever it held), 1 or
 *                   anything else = opaque. NULL means fully opaque.
 *
 * The mask is not part of DOOM's wall tiers: every tier R_RenderSegLoop() draws
 * reads its composite column opaquely, and the one transparent wall texture in
 * DOOM -- a two-sided line's mid texture -- is drawn by R_DrawMasked(), which
 * this pass does not implement. It is here because a composite can have holes
 * (r_data.h measures nine such textures) and because D3.4's masked pass needs
 * exactly this record; a caller that models a wall as opaque passes NULL, which
 * is what the real-data harness does.
 *
 * A texture that is *not resident* is never composed here: the resolver below
 * reports it and r_wall_emit() fails before writing.
 */
typedef struct r_walltex {
    const uint8_t* pixels;
    const uint8_t* mask;
    int16_t width;
    int16_t height;
    int16_t widthMask;
    int16_t heightMask;
} r_walltex_t;

/**
 * @brief True when a resolved texture has the geometry the column writer needs.
 *
 * The shape contract r_segs.c's preflight applies to a wall tier, exposed so the
 * masked mid texture (r_things.c) cannot disagree with it: non-NULL pixels,
 * width and height at least 1, a heightMask that is 2^k - 1 and at least
 * height - 1, and a widthMask equal to r_texture_width_mask(width).
 */
int r_walltex_is_usable(const r_walltex_t* texture);

/**
 * @brief Resolves a packed texture name to bytes, or NULL when not resident.
 *
 * Called once per tier in the draw loop, and once per *distinct* name in the
 * preflight: the preflight stops resolving a name it has already proved in the
 * same call (a resolver that answers the same name inconsistently is already
 * outside this contract, and the draw still skips a tier it cannot resolve).
 * Never per column. The returned struct must stay valid until r_wall_emit()
 * returns; the emitter copies nothing.
 *
 * The policy behind the callback -- a cache, a zone block, a mapped flash
 * region, "decode it now" -- is the caller's. Answering NULL is a normal
 * outcome and produces R_SEGS_ERR_TEXTURE_MISSING, not a partial frame.
 */
typedef const r_walltex_t* (*r_tex_resolve_fn)(void* context, r_name_t name);

/*
 * How a fragment finds its textures. `resolve` is required; `context` is handed
 * back unchanged and may be NULL.
 *
 * D3.3 (the plane pass) extends the same source with the two things a flat
 * needs, so one struct describes everything one frame draws:
 *
 *   resolveFlat  Resolves a packed flat name to bytes, or NULL when the flat is
 *                not resident. Called once per drawable plane in the pass's
 *                preflight and once more when that plane is drawn -- never per
 *                span and never per pixel. The returned flat must stay valid
 *                until r_plane_draw() returns; the emitter copies nothing.
 *                The policy behind the callback (a cache, a zone block, a
 *                mapped flash region) is the caller's, exactly as it is for
 *                wall textures. Answering NULL is a normal outcome and
 *                produces R_PLANE_ERR_FLAT_MISSING, not a partial frame.
 *   flatContext  The pointer handed to resolveFlat. It is separate from
 *                `context` because a caller usually keeps flats and wall
 *                textures in different stores; pass the same pointer in both
 *                when it has one.
 *   skyTexture   The packed name of the level's sky *texture* (upstream's
 *                `skytexture`, "SKY1" for episode 1), or R_NAME_NONE when the
 *                level has no sky. The sky plane is identified by its flat
 *                (`plane->picNum == state->params.skyPicNum`), but it is drawn
 *                from a wall texture, because DOOM stores the sky as one
 *                (r_plane.c, sky path). A sky plane with no resolvable texture
 *                is an error, not a blank ceiling.
 *
 * The wall pass reads only `resolve` and `context`; the plane pass reads only
 * `resolveFlat`, `flatContext` and `skyTexture` (plus `resolve`/`context` for
 * that one sky texture). A source that is used by only one pass may leave the
 * other pass's fields NULL/NONE.
 */
typedef struct r_tex_source {
    r_tex_resolve_fn resolve;
    void* context;
    r_flat_resolve_fn resolveFlat;
    void* flatContext;
    r_name_t skyTexture;
    /*
     * Optional. Called at the start of every fragment's preflight and again at
     * the start of that fragment's draw. A caller-owned cache uses it to scope
     * its "do not evict this" pins to one fragment: a fragment resolves its
     * tiers one after the other and then reads all of them, so those results
     * must survive the next resolve, while a previous fragment's are free to be
     * evicted. NULL means the caller needs no signal, which is what a resolver
     * with stable storage passes.
     */
    void (*beginFragment)(void* context);
} r_tex_source_t;

/* --- Error codes --------------------------------------------------------- */

/*
 *   R_SEGS_OK                    the pass ran and wrote the surface
 *   R_SEGS_ERR_NULL_STATE        no view state
 *   R_SEGS_ERR_MAP_NOT_OPEN      the state holds no walked map
 *   R_SEGS_ERR_NULL_SURFACE      no surface descriptor
 *   R_SEGS_ERR_SURFACE_PIXELS    surface->pixels is NULL
 *   R_SEGS_ERR_SURFACE_SIZE      width < R_SCREEN_WIDTH or height < R_VIEW_HEIGHT
 *   R_SEGS_ERR_NULL_SOURCE       no texture source
 *   R_SEGS_ERR_NULL_RESOLVE      the source has no resolver
 *   R_SEGS_ERR_NULL_LIGHT        no initialised r_light_t
 *   R_SEGS_ERR_TEXTURE_MISSING   the resolver answered NULL for a tier texture
 *   R_SEGS_ERR_TEXTURE_SHAPE     a resolved texture's geometry is unusable:
 *                                NULL pixels, width/height < 1, a heightMask
 *                                that is not 2^k - 1 or that is smaller than
 *                                height - 1 (the stored column would not hold
 *                                the texture), or a widthMask that is not
 *                                r_texture_width_mask(width)
 *   R_SEGS_ERR_NULL_CLIP_POOL    storage.clipPool is NULL while the frame
 *                                needs at least one clipped span
 *   R_SEGS_ERR_SMALL_CLIP_POOL   clipPoolCapacity cannot hold the entries this
 *                                frame needs (one run of x2-x1+1 per clipped
 *                                edge and per masked mid)
 *   R_SEGS_ERR_NULL_OPENING_POOL retired: the unified pool reports both pool
 *                                failures through the two codes above. Kept so
 *                                the printed stage codes do not renumber.
 *   R_SEGS_ERR_SMALL_OPENING_POOL retired, for the same reason.
 *   R_SEGS_ERR_NULL_PLAN         r_wall_draw() was given no plan
 *
 * Every code is returned before the first pixel write, so the surface, its
 * coverage record and its tier counters are untouched on any failure. The
 * preflight returns the two pool codes; the draw returns only a plan or surface
 * error, because the preflight is what proves the pools.
 */
typedef enum r_segs_error {
    R_SEGS_OK = 0,
    R_SEGS_ERR_NULL_STATE = 1,
    R_SEGS_ERR_MAP_NOT_OPEN = 2,
    R_SEGS_ERR_NULL_SURFACE = 3,
    R_SEGS_ERR_SURFACE_PIXELS = 4,
    R_SEGS_ERR_SURFACE_SIZE = 5,
    R_SEGS_ERR_NULL_SOURCE = 6,
    R_SEGS_ERR_NULL_RESOLVE = 7,
    R_SEGS_ERR_NULL_LIGHT = 8,
    R_SEGS_ERR_TEXTURE_MISSING = 9,
    R_SEGS_ERR_TEXTURE_SHAPE = 10,
    R_SEGS_ERR_NULL_CLIP_POOL = 11,
    R_SEGS_ERR_SMALL_CLIP_POOL = 12,
    R_SEGS_ERR_NULL_OPENING_POOL = 13,
    R_SEGS_ERR_SMALL_OPENING_POOL = 14,
    R_SEGS_ERR_NULL_PLAN = 15
} r_segs_error_t;

/* --- Public API ---------------------------------------------------------- */

/*
 * What one frame's wall pass will need, produced by r_wall_preflight() and
 * consumed by r_wall_draw(). It exists so the frame driver can run every gate
 * that can refuse a frame before it prefills the surface (D4.2h): a refusal
 * after the prefill leaves the previous frame destroyed.
 *
 *   clipPoolEntries  int16_t entries the unified pool must hold: one run of
 *                    (x2 - x1 + 1) per copied clip edge and per masked mid.
 *                    Zero when no drawseg needs a pool run.
 *   clipBlocks       the block-equivalent count the pre-block layout reported,
 *                    reserved constants included; SPRSTAT compares it with the
 *                    oracle's own count, so its value is unchanged by the pool.
 *   openingBlocks    the same for masked mids.
 */
typedef struct r_wall_plan {
    uint32_t clipPoolEntries;
    uint16_t clipBlocks;
    uint16_t openingBlocks;
} r_wall_plan_t;

/**
 * @brief Proves a frame's wall pass will not refuse, before any pixel.
 *
 * Validates the state, the map and the texture source, resolves and validates
 * every texture the pass will draw (one resolve per distinct name), and counts
 * the clip-pool entries from the *same* predicate the draw saves with. This is
 * the call the frame driver makes between the walk and the surface prefill.
 *
 * On success @p plan receives the demand and the counters; nothing is written
 * to the surface, the clip arrays, the pool or any drawseg. The pool's entries
 * are not touched: a pool pointer is required only when the frame needs one.
 *
 * @param state    A state whose r_bsp_render() has run.
 * @param textures How to resolve a tier's packed texture name.
 * @param plan     Receives the plan; untouched on failure.
 * @return R_SEGS_OK, or one of the argument/texture/pool codes above.
 */
r_segs_error_t r_wall_preflight(r_view_state_t* state, const r_tex_source_t* textures,
                                r_wall_plan_t* plan);

/**
 * @brief Draws the walls a successful r_wall_preflight() proved.
 *
 * The draw half of r_wall_emit(): resets the clip arrays and the coverage
 * record, then walks the stored drawsegs and reproduces R_RenderSegLoop(). It
 * trusts @p plan for capacity -- that is the seam's contract -- and bumps the
 * unified pool exactly as the preflight counted. It also completes each
 * drawseg's clip fields and masked columns for the masked pass.
 *
 * @param state    The state the plan was made for.
 * @param surface  The destination surface.
 * @param textures How to resolve a tier's packed texture name.
 * @param light    A COLORMAP initialised by r_light_init().
 * @param plan     The plan r_wall_preflight() produced for this frame.
 * @return R_SEGS_OK, or R_SEGS_ERR_NULL_PLAN or a surface/state code, all
 *         before the first pixel.
 */
r_segs_error_t r_wall_draw(r_view_state_t* state, r_draw_surface_t* surface,
                           const r_tex_source_t* textures, const r_light_t* light,
                           const r_wall_plan_t* plan);

/**
 * @brief Draws every stored wall fragment into @p surface.
 *
 * A second pass over `state->storage.drawsegs[0 .. drawsegCount)` in the order
 * the walk stored them (near to far), reproducing R_RenderSegLoop() column for
 * column. ceilingclip[] and floorclip[] are reset first, exactly as
 * R_ClearPlanes() does at the start of a frame, so the pass does not depend on
 * whatever the walk or a previous pass left there.
 *
 * It reads what the walk stored and nothing else about the view: the seg index,
 * the scale at both ends, the three range values (rwDistance, rwOffset,
 * rwCenterAngle), the light number; plus `state->params` for the viewpoint (the
 * view z the pegging needs and the sky flat the predicate compares against).
 * It does not read a visplane and does not touch the plane set -- the walk
 * already marked those, and marking them twice would double-count.
 *
 * The surface must be at least R_SCREEN_WIDTH x R_VIEW_HEIGHT; `width` is its
 * row stride. Pixels the pass does not cover keep whatever the caller put there
 * (the frame protocol prefills 0xFF, which is what makes "uncovered" visible
 * without a second buffer). When `surface->covered` is not NULL it is cleared
 * and then set to 1 for every column that received at least one pixel;
 * `surface->tierPixels[]` is cleared and counts the pixels each tier wrote.
 *
 * It also completes the drawsegs for the masked pass: see "THE CLIP CAPTURE AND
 * THE MASKED RECORDING" in this file's header. On success it sets
 * `state->clipCaptured`, reports `state->clipBlocksUsed` /
 * `state->openingBlocksUsed` (the block-equivalent protocol counts) and the
 * unified pool's real figures, `state->clipPoolEntries` (needed) and
 * `state->clipPoolUsed` (consumed). On any error those fields keep their
 * previous values and no drawseg is modified.
 *
 * This is r_wall_preflight() followed by r_wall_draw(); a caller that must
 * prefill the surface between the two gates calls those directly.
 *
 * @param state    A state whose r_bsp_render() has run.
 * @param surface  The destination surface and its coverage counters.
 * @param textures How to resolve a tier's packed texture name.
 * @param light    A COLORMAP initialised by r_light_init().
 * @return R_SEGS_OK, or one of the codes above. On any error nothing was
 *         written.
 */
r_segs_error_t r_wall_emit(r_view_state_t* state, r_draw_surface_t* surface,
                           const r_tex_source_t* textures,
                           const r_light_t* light);

/**
 * @brief One texture column, the write R_DrawColumn() performs.
 *
 * Shared by the wall pass and by the masked mid textures (r_things.c), so the
 * vertical wrap, the transparency mask, the colormap read and the counter
 * update have one implementation. Rows are clipped to the view and to
 * `surface->height`; the caller has already applied its own clip arrays.
 *
 * @param column    The texture's column, dense, `heightMask + 1` rows.
 * @param heightMask The stored column's wrap mask (2^k - 1).
 * @param mask      Optional per-texel opacity, 0 = leave the pixel alone.
 * @param colormap  One 256-byte COLORMAP table.
 * @param pixelMask Optional per-pixel record set to 1 where a pixel is written.
 * @param written   Optional counter advanced by the number of pixels written.
 */
void r_column_draw(r_draw_surface_t* surface, int x, int yl, int yh,
                   fixed_t textureMid, fixed_t iscale, const uint8_t* column,
                   int16_t heightMask, const uint8_t* mask,
                   const uint8_t* colormap, uint8_t* pixelMask,
                   uint32_t* written);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_R_SEGS_H */
