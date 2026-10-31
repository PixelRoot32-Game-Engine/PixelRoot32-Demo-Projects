/*
 * The BSP walk: one viewpoint in, the drawsegs and visplanes one frame needs.
 *
 * This is D3.1 of the DOOM port. It turns the level graph (doom_map.h) plus a
 * view position into the two lists the rasterisers consume:
 *
 *   drawsegs   one entry per visible wall fragment, left to right in screen
 *              columns, with the scale at both ends and the silhouette a
 *              sprite behind it may be clipped against;
 *   visplanes  the floor and ceiling planes the walk marked, ready for the span
 *              emitter (D3.3) to fill.
 *
 * THE WHOLE FRAME, IN ORDER (D3.4 closes the list)
 *
 *  1. r_bsp_render()        the walk: drawsegs and the plane marks.
 *  2. r_wall_emit()         the wall tiers. It also takes the snapshot of the
 *                           clip arrays each drawseg needs for sprite clipping
 *                           and records the texture column of every two-sided
 *                           line's mid texture, because both are read from the
 *                           clip arrays as the column loop leaves them.
 *  3. r_plane_draw()        floors, ceilings and the sky.
 *  4. r_masked_emit()       sprites and the deferred masked mid textures, in
 *                           upstream's R_DrawMasked() order: a sorted sprite
 *                           list drawn back to front, each sprite first
 *                           rendering the masked mids the drawsegs behind it
 *                           recorded, then the untouched remainders far to
 *                           near.
 *
 * Steps 2 to 4 are separate calls so each can be measured and diffed on its
 * own; the order is not optional and every pass documents what it expects from
 * the one before it.
 *
 * WHAT THIS LAYER DOES NOT DO
 *
 * No texture lookup, no colormap, no pixel is written. The column and span
 * emitters are D3.2 and D3.3, and this file stops exactly where upstream's
 * R_StoreWallRange() stops before R_RenderSegLoop()'s texture drawing: the
 * column clip arrays and the plane marks are kept, the wall tiers are not
 * drawn. That split is deliberate -- the walk's output can be diffed against a
 * host oracle without a single texture being decoded.
 *
 * TRAVERSAL ORDER: NEAR FIRST, NOT REAR TO FRONT
 *
 * R_RenderBSPNode() recurses into `children[side]` first, where `side` is the
 * half-plane the viewpoint is on, and only then, if the far child's bounding
 * box survives R_CheckBBox(), into the other half (r_bsp.c, upstream). That is
 * *viewer side first* -- near to far -- and it is not an accident: a near wall
 * fills solidsegs[].last and causes the far wall behind it to be clipped away
 * before it is ever stored. A painter's-order walk (rear to front) would store
 * every occluded wall and produce a different drawseg list. This port follows
 * upstream: the list is in near-to-far order and the tests pin that.
 *
 * THE CULLING, AND WHERE EACH RULE COMES FROM
 *
 *   back face        R_AddLine(): span = angle(v1) - angle(v2); span >= ANG180
 *                    is the back side and is dropped.
 *   frustum arc      R_AddLine(): the segment's angle range is clipped to
 *                    +/- clipangle; a range entirely outside is dropped.
 *   sub-pixel        R_AddLine(): x1 == x2 after projection draws nothing.
 *   empty line       R_AddLine(): two-sided, identical floor/ceiling pictures
 *                    and light, and no mid texture -> dropped.
 *   solid occlusion  R_ClipSolidWallSegment(): the fragment is split against the
 *                    solidsegs clip posts and only its visible pieces are
 *                    stored. A fragment fully covered stores nothing.
 *   window           R_ClipPassWallSegment(): same split, but the fragment does
 *                    not join the clip list, because the sectors behind it are
 *                    still visible through the gap.
 *   BSP bounding box R_CheckBBox(): before descending into the far child, its
 *                    node bounding box is projected; if no column of it is
 *                    still open in solidsegs, the whole subtree is skipped.
 *
 * STORAGE: WHAT ONE VIEW COSTS
 *
 * Everything is caller-owned. At the default DOOM capacities (R_MAX_DRAWSEGS
 * drawsegs, R_MAX_CLIP_SEGS clip posts, 64 visplanes) one view costs:
 *
 *   drawsegs      256 * sizeof(r_drawseg_t)               = 13,312 B
 *   visplanes      64 * sizeof(r_visplane_t)              = 42,496 B
 *   clip posts     32 * sizeof(r_clip_range_t)            =    256 B
 *   column clips  2 * 320 * sizeof(int16_t)               =  1,280 B
 *   ---------------------------------------------------------------
 *   total                                                 57,344 B
 *
 * The visplanes dominate, exactly as DOOM's own visplane_t does (two 320-byte
 * arrays per plane). A caller that has measured its level may pass a smaller
 * `planeCapacity`; the real E1M1 spawn view is in the D3.1 report, and a
 * capacity that is too small is reported as R_BSP_ERR_VISPLANE_OVERFLOW, never
 * written past. The projection tables are `const` in flash and cost no DRAM
 * (r_tables.h).
 *
 * The two D3.4 pools (the sprite list and the unified clip/opening pool) are
 * *not* in that total: they belong to the passes that read them, and their
 * capacities are the caller's. They are listed in r_view_storage_t, and the
 * unified pool's peak is measured by tools/capacity_check.py (D4.2h).
 *
 * THE WALL FRAGMENT, AND WHY IT IS NOT STORED
 *
 * Upstream splits one visible wall across two functions of one file:
 * R_StoreWallRange() decides the heights, the tier textures, the light index
 * and the pegging, and leaves all of it in file-scope variables; R_RenderSegLoop()
 * then walks the fragment's columns and draws them. This port splits the same
 * work across two files -- r_bsp.c (walk) and r_segs.c (column emitter) -- so the
 * hand-off needs an explicit record. It is r_wall_fragment_t, below, and it is
 * *stack local*: r_wall_fragment_open() fills it once per fragment, in the walk
 * before the drawseg is stored and again in the emitter from the stored drawseg.
 *
 * What lives in the drawseg and what lives in the fragment is a deliberate
 * divergence from upstream's boundary, and it is decided by a byte count:
 *
 *   in r_drawseg_t (13,312 B for the 256-entry array, 52 B each)
 *     seg, x1, x2, scale1, scale2, scaleStep, silhouette, bottomSilhouette,
 *     topSilhouette, lightLevel, lightNum, rwDistance, rwOffset, rwCenterAngle,
 *     plus D3.4's three clip fields (sprTopClip, sprBottomClip,
 *     maskedTextureCol) in the padding the old layout had
 *   in r_wall_fragment_t (170 B on the stack, one fragment at a time)
 *     everything above, plus worldTop/worldBottom/worldHigh/worldLow, the three
 *     texturemids, the topfrac/bottomfrac/pixhigh/pixlow steppers,
 *     markFloor/markCeiling, the wallTop/wallMid/wallBottom tier flags and the
 *     two "solid clip" flags the sprite clip arrays need
 *
 * Only the column emitter ever reads the second group, and it is 75 B of fields
 * (76 after padding): storing it per drawseg would cost 19,456 B of DRAM for one
 * view -- about a seventh of the whole remaining budget measured in D0.4 -- for
 * data with a single consumer. The fragment struct is 168 B because it also
 * carries the map-facing fields the walk needs *before* a drawseg exists (the
 * sidedef's names and offsets, the seg's first vertex and normal angle, the
 * scale at both ends); those are on the stack for the duration of one fragment.
 *
 * The three values that *are* stored per drawseg are the ones only
 * R_StoreWallRange can produce from the seg angle and the sidedefs (the distance
 * along the wall's normal, the texture offset and the view-relative centre
 * angle), and they are stored because recomputing them in a second file would be
 * a second implementation of the highest-risk sign and orientation math in the
 * port.
 *
 * 36 B -> 48 B -> 52 B per drawseg, so 9,216 B -> 12,288 B -> 13,312 B for the
 * array. D3.4's three int16_t clip fields fit in the two padding bytes the old
 * layout had after x2 plus the four bytes the struct grows by; appending them
 * naively would have cost 56.
 *
 * ERROR BEHAVIOUR
 *
 * init() validates its arguments before writing anything. render() clears the
 * output and walks; if the walk fails (clip list full, plane capacity reached,
 * BSP deeper than the guard) it records the first error and unwinds, leaving a
 * partial but internally consistent list: drawsegCount and planes.count are
 * valid lengths, every stored entry is complete. It never writes past a
 * capacity. That is the "fail cleanly" contract the plan asks for.
 *
 * This is C99 only: <stddef.h> and <stdint.h>, no allocation, no globals, no
 * stdio, no floating point. The caller owns the state and the arrays.
 */
#ifndef DOOM_R_BSP_H
#define DOOM_R_BSP_H

#include <stdint.h>

#include "doom_map.h"
#include "r_defs.h"
#include "r_plane.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A recursion guard, not a level limit: E1M1's tree is far shallower (about
 * 20 levels). A corrupt or cyclic child graph stops here instead of exhausting
 * the 8 KB ESP32 task stack. */
#define R_BSP_MAX_DEPTH 64

/* DOOM's VIEWHEIGHT: the player's eye is 41 map units above the floor. */
#define R_VIEWHEIGHT (41 * FRACUNIT)

/* --- Error codes --------------------------------------------------------- */

/*
 *   R_BSP_OK                  the call did what it says
 *   R_BSP_ERR_NULL_STATE      no view state
 *   R_BSP_ERR_NULL_MAP        no map for init()
 *   R_BSP_ERR_MAP_NOT_OPEN    the map was never accepted by doom_map_open()
 *   R_BSP_ERR_NULL_STORAGE    no storage descriptor
 *   R_BSP_ERR_NULL_DRAWSEGS   drawsegs is NULL, or drawsegCapacity is 0
 *   R_BSP_ERR_NULL_PLANES     planes.planes is NULL, or planeCapacity is 0
 *   R_BSP_ERR_NULL_CLIPS      clipRanges is NULL
 *   R_BSP_ERR_SMALL_CLIPS     clipRangeCapacity < R_MAX_CLIP_SEGS
 *   R_BSP_ERR_NULL_CLIP_ARRAYS floorClip or ceilingClip is NULL
 *   R_BSP_ERR_NULL_PARAMS     render() was given no parameters
 *   R_BSP_ERR_NULL_OUT        an out parameter the call needs is NULL
 *   R_BSP_ERR_NO_PLAYER_START THINGS has no player start of that index
 *   R_BSP_ERR_SUBSECTOR_RANGE point_in_subsector() reached an index not in
 *                             SSECTORS (a corrupt graph that open() cannot
 *                             cheaply prove)
 *   R_BSP_ERR_DEPTH           the BSP recursed past R_BSP_MAX_DEPTH
 *   R_BSP_ERR_CLIP_OVERFLOW   the solid clip list ran out of posts
 *   R_BSP_ERR_VISPLANE_OVERFLOW the plane set ran out of capacity
 *   R_BSP_ERR_DRAWSEG_OVERFLOW the drawseg array ran out of entries
 *
 * The last three are the "fail cleanly" cases: the walk stops and keeps a
 * complete, shorter result instead of writing past an array.
 */
typedef enum r_bsp_error {
    R_BSP_OK = 0,
    R_BSP_ERR_NULL_STATE = 1,
    R_BSP_ERR_NULL_MAP = 2,
    R_BSP_ERR_MAP_NOT_OPEN = 3,
    R_BSP_ERR_NULL_STORAGE = 4,
    R_BSP_ERR_NULL_DRAWSEGS = 5,
    R_BSP_ERR_NULL_PLANES = 6,
    R_BSP_ERR_NULL_CLIPS = 7,
    R_BSP_ERR_SMALL_CLIPS = 8,
    R_BSP_ERR_NULL_CLIP_ARRAYS = 9,
    R_BSP_ERR_NULL_PARAMS = 10,
    R_BSP_ERR_NULL_OUT = 11,
    R_BSP_ERR_NO_PLAYER_START = 12,
    R_BSP_ERR_SUBSECTOR_RANGE = 13,
    R_BSP_ERR_DEPTH = 14,
    R_BSP_ERR_CLIP_OVERFLOW = 15,
    R_BSP_ERR_VISPLANE_OVERFLOW = 16,
    R_BSP_ERR_DRAWSEG_OVERFLOW = 17
} r_bsp_error_t;

/* --- View parameters ----------------------------------------------------- */

/*
 * One viewpoint. `x`, `y` and `z` are 16.16 map coordinates; `angle` is a
 * binary angle where 0 points along +x and ANG90 along +y. `extralight` is
 * DOOM's gun-flash brightness offset (0 for a plain view). `skyPicNum` is the
 * packed name of the level's sky flat (F_SKY1) or R_PLANE_NO_SKY.
 */
typedef struct r_view_params {
    fixed_t x;
    fixed_t y;
    fixed_t z;
    angle_t angle;
    int16_t extralight;
    r_name_t skyPicNum;
} r_view_params_t;

/* --- Drawseg ------------------------------------------------------------- */

/*
 * A drawseg's clip fields are *entry offsets* into the caller's single clip
 * pool (r_view_storage_t::clipPool), or one of three negative codes:
 *
 *   R_DRAWSEG_NO_BLOCK      no such clip for this drawseg.
 *   R_DRAWSEG_CLIP_TOP      every column is R_VIEW_HEIGHT (upstream's
 *                           `sprtopclip = screenheightarray`).
 *   R_DRAWSEG_CLIP_BOTTOM   every column is -1 (upstream's
 *                           `sprbottomclip = negonearray`).
 *
 * The two constants are upstream's global `screenheightarray` and
 * `negonearray`, shared by every solid drawseg. They are *not* pool entries:
 * a negative code cannot be a valid offset, and a consumer answers it with the
 * constant instead of reading the pool. That keeps the pool's entries entirely
 * variable-length wall data, which is upstream's layout: `openings[]` is one
 * bump-allocated `short` array, and a clipped span occupies `rw_stopx - start`
 * entries, not a whole screen row.
 *
 * A valid offset is >= 0 and less than the pool's capacity. A fragment's span
 * holds its own columns at offsets 0 .. x2-x1, so a consumer reads
 * `pool[offset + (x - ds->x1)]` (r_things.c's clip_value()). The fields are
 * int16_t, so the pool capacity must stay at or below INT16_MAX entries.
 */
#define R_DRAWSEG_NO_BLOCK ((int16_t)-1)
#define R_DRAWSEG_CLIP_TOP ((int16_t)-2)
#define R_DRAWSEG_CLIP_BOTTOM ((int16_t)-3)
/* The two shared constant arrays, counted as block-equivalents on the SPRSTAT
 * protocol line. They cost no pool entries in this layout. */
#define R_DRAWSEG_RESERVED_BLOCKS 2

/* The value that says "this masked column has already been drawn". Upstream's
 * MAXSHORT, written into openings[] by R_RenderMaskedSegRange(). */
#define R_MASKED_COLUMN_USED ((int16_t)32767)

/*
 * One visible wall fragment, in screen columns [x1, x2] inclusive.
 *
 * Upstream's drawseg_t also carries `maskedtexturecol` and the two sprite
 * clipping arrays. This port records the same three things as *entry offsets*
 * into the caller's single clip pool (D3.4) instead of as pointers into a
 * global `openings[]`: an index cannot go stale if the storage moves, costs two
 * bytes instead of four on the ESP32, and makes a capacity overflow a reported
 * error instead of a silent write past the array. The layout is upstream's:
 * one bump-allocated `int16_t` array, each clipped span `x2 - x1 + 1` entries.
 *
 * Field by field, and where it comes from:
 *
 *   seg              index into the map's SEGS lump. A raw pointer was the
 *                    alternative; the map view is zero-copy and the accessor is
 *                    one call, so an index keeps the struct smaller and cannot
 *                    go stale if the map is reopened in place.
 *   x1, x2           first and last screen column, inclusive. R_StoreWallRange
 *                    stores `start` and `stop` after the clip posts have
 *                    removed the covered pieces.
 *   sprTopClip       R_DrawSegRange's `sprtopclip`: the pool offset of the
 *                    ceilingclip[] values this fragment leaves for a sprite
 *                    behind it, or R_DRAWSEG_CLIP_TOP / R_DRAWSEG_NO_BLOCK.
 *                    Written by the wall pass, not by the walk, because the
 *                    values are the clip arrays after the column loop has run.
 *   sprBottomClip    the same for floorclip[].
 *   maskedTextureCol the pool offset of the texture columns a two-sided line's
 *                    masked mid texture recorded, or R_DRAWSEG_NO_BLOCK. Each
 *                    entry is the texture column of one screen column, exactly
 *                    upstream's `maskedtexturecol[x]`, and is overwritten with
 *                    R_MASKED_COLUMN_USED once drawn.
 *   scale1, scale2  16.16 texture scale at x1 and x2, from
 *                    R_ScaleFromGlobalAngle(); scale2 equals scale1 when the
 *                    fragment is one column wide.
 *   scaleStep        (scale2 - scale1) / (x2 - x1), the per-column step the
 *                    emitter adds. 0 for a one-column fragment.
 *   silhouette      R_SIL_NONE/BOTTOM/TOP/BOTH: which of the wall's edges a
 *                    sprite behind it can be clipped against. The walk stores
 *                    the geometric silhouette; r_wall_emit() adds the two bits
 *                    a masked mid texture forces, which is where upstream adds
 *                    them too (R_StoreWallRange's tail).
 *   bottomSilhouette `bsilheight`: the world z a sprite's bottom may not go
 *                    below, or INT32_MAX when the whole column is solid.
 *   topSilhouette    `tsilheight`: the world z a sprite's top may not go above,
 *                    or INT32_MIN when the whole column is solid.
 *   lightLevel       the front sector's stored light level (0..255).
 *   lightNum         `lightLevel >> 4 + extralight`, then +1 for a vertical seg
 *                    and -1 for a horizontal one, exactly as upstream picks the
 *                    wall light table. Not clamped here: the emitter owns the
 *                    clamp to [0, LIGHTLEVELS), as upstream's does.
 *   rwDistance       `rw_distance`: the distance from the viewpoint to the
 *                    seg's plane along the seg normal, 16.16. R_StoreWallRange's
 *                    R_PointToDist * sine(distance angle).
 *   rwOffset         `rw_offset`: the texture u coordinate at the fragment's
 *                    left column before the tangent term, in 16.16 texture
 *                    units. It folds in the sidedef's textureoffset and the
 *                    seg's own offset, and its sign depends on which way the
 *                    normal turns from the first vertex direction -- the one
 *                    piece of orientation math in the renderer that is easiest
 *                    to get backwards.
 *   rwCenterAngle    `rw_centerangle` = ANG90 + viewangle - rw_normalangle. It is
 *                    view *relative* on purpose: the emitter adds
 *                    xtoviewangle[x] to it and shifts by ANGLETOFINESHIFT to
 *                    index r_fine_tangent[], and the relative form is what keeps
 *                    that index inside [0, FINEANGLES/2).
 *
 * PACKING. The three int16_t clip fields are placed in the two bytes of padding
 * the old 48-byte layout had after x2 and the four bytes the struct grows by,
 * so sizeof stays at 52 bytes rather than the 56 a naive append would cost.
 * The test suite pins the number. Field order is not an ABI: the struct is
 * caller-owned and never compared as a blob.
 */
typedef struct r_drawseg {
    uint16_t seg;
    int16_t x1;
    int16_t x2;
    int16_t sprTopClip;
    int16_t sprBottomClip;
    int16_t maskedTextureCol;
    fixed_t scale1;
    fixed_t scale2;
    fixed_t scaleStep;
    uint8_t silhouette;
    fixed_t bottomSilhouette;
    fixed_t topSilhouette;
    int16_t lightLevel;
    int16_t lightNum;
    fixed_t rwDistance;
    fixed_t rwOffset;
    fixed_t rwCenterAngle;
} r_drawseg_t;

/* --- The wall fragment (shared with the column emitter, r_segs.c) --------- */

/*
 * Everything R_StoreWallRange() and R_RenderSegLoop() keep in file-scope
 * variables for one wall fragment, in one stack local. It is filled by
 *
 *   r_wall_fragment_open()   the map-facing half: sectors, sidedef, the two
 *                            sided tier predicate, the silhouette, the light
 *                            index, the pre-shift world heights and the tier
 *                            names;
 *   r_wall_fragment_step()   the 12.4 fractional screen positions and their
 *                            per-column steps, from the heights and the scale;
 *   r_wall_fragment_peg()    the three `dc_texturemid` values, which need the
 *                            tier textures' heights and therefore cannot be
 *                            computed by a layer that never resolves a texture.
 *
 * The walk calls open() + step() and stores the six range values in the
 * drawseg; the emitter calls open() on the stored drawseg, copies the six
 * values back in from it, then calls step() + peg(). No rule is written twice:
 * the predicate in open() is the *only* implementation of the two-sided
 * height/picture test, and it is the one that decides worldhigh/worldlow,
 * markFloor/markCeiling and the tier flags for both passes.
 *
 * The world heights are 16.16 and view relative (sector height - view->z). The
 * sky hack of R_StoreWallRange is applied here, so worldTop already equals
 * worldHigh for an outdoor ceiling.
 */
typedef struct r_wall_fragment {
    /* From the seg, its linedef and its sidedef. */
    uint16_t seg;
    uint16_t frontSector;
    uint16_t backSector;
    int16_t flags;            /* linedef flags, for ML_DONTPEGTOP/BOTTOM */
    fixed_t textureOffset;    /* sidedef textureoffset */
    fixed_t rowOffset;        /* sidedef rowoffset */
    r_name_t midTexture;      /* packed sidedef names, R_NAME_NONE when absent */
    r_name_t topTexture;
    r_name_t bottomTexture;
    /* From the seg's geometry, for the walk's range computation. */
    fixed_t v1x;
    fixed_t v1y;
    angle_t normalAngle;      /* seg.angle + ANG90 */
    fixed_t segOffset;        /* seg.offset, the SEGS lump's own u origin */
    /* The predicate: R_StoreWallRange's decisions. */
    fixed_t worldTop;         /* 16.16, view relative, sky hack applied */
    fixed_t worldBottom;
    fixed_t worldHigh;
    fixed_t worldLow;
    fixed_t midTextureMid;    /* dc_texturemid per tier, after peg() */
    fixed_t topTextureMid;
    fixed_t bottomTextureMid;
    /* The range: R_StoreWallRange's scales and the three stored values. */
    int16_t x1;
    int16_t x2;
    fixed_t scale1;
    fixed_t scale2;
    fixed_t scaleStep;
    fixed_t distance;         /* rwDistance */
    fixed_t offset;           /* rwOffset */
    fixed_t centerAngle;      /* rwCenterAngle */
    /* The column steppers: 12.4 fractions, after step(). */
    fixed_t topFrac;
    fixed_t topStep;
    fixed_t bottomFrac;
    fixed_t bottomStep;
    fixed_t pixHigh;
    fixed_t pixLow;
    fixed_t pixHighStep;
    fixed_t pixLowStep;
    fixed_t scale;            /* rw_scale, advanced once per column */
    /* Flags, exactly upstream's. */
    uint8_t hasBack;
    uint8_t wallMid;
    uint8_t wallTop;
    uint8_t wallBottom;
    uint8_t markFloor;
    uint8_t markCeiling;
    uint8_t silhouette;
    /* The two "closed for sprites" cases of R_StoreWallRange: the back sector's
     * ceiling is at or below the front floor (bottom solid), or its floor is at
     * or above the front ceiling (top solid). Upstream answers them by pointing
     * sprtopclip/sprbottomclip at the shared full-height arrays; this port sets
     * a flag so r_wall_emit() can point at the shared constant blocks. */
    uint8_t clipTopSolid;
    uint8_t clipBottomSolid;
    int16_t lightLevel;
    int16_t lightNum;
    fixed_t bottomSilhouette;
    fixed_t topSilhouette;
} r_wall_fragment_t;

/* One solid clip post: `first`..`last` are screen columns already covered by a
 * wall nearer than anything the walk will visit next. The list is kept sorted
 * and coalesced; solidsegs[0] is {-inf, -1} and solidsegs[1] is {width, +inf}
 * as permanent sentinels, which is what lets the scans in r_bsp.c have no
 * bounds test. */
typedef struct r_clip_range {
    int first;
    int last;
} r_clip_range_t;

/* --- Storage and state --------------------------------------------------- */

/*
 * The arrays one view needs. All pointers must stay valid for every render()
 * call; `count` in `planes` is ignored and reset by render().
 *
 * A capacity of zero is rejected. clipRangeCapacity must be at least
 * R_MAX_CLIP_SEGS (32): upstream's MAXSEGS, which is the number of interior
 * posts a 320-wide view can need. drawsegs may be smaller than R_MAX_DRAWSEGS;
 * going over is R_BSP_ERR_DRAWSEG_OVERFLOW, the walk's own error on the same
 * path as the clip-post and visplane overflows. It used to set
 * `drawsegOverflowed` alone, which upstream's `if (ds_p == &drawsegs[...])
 * return;` also does -- but a *dropped* fragment is not a shorter list the
 * caller can draw, it is a hole in the picture, and the driver never failed on
 * the flag. On E1M1 that silently lost geometry from 99 walkable viewpoints.
 *
 * THE TWO D3.4 POOLS
 *
 *   visSprites        one r_vissprite_t (r_things.h) per projected sprite,
 *                     i.e. upstream's `vissprites[MAXVISSPRITES]`. The walk does
 *                     not touch it; only the masked pass does.
 *   clipPool          `clipPoolCapacity` int16_t *entries*, the single
 *                     bump-allocated array of upstream's `openings[]`
 *                     (p_setup.c: `Z_Malloc(MAXOPENINGS * sizeof(*openings))`,
 *                     MAXOPENINGS = SCREENWIDTH * 64). A clipped edge occupies
 *                     the fragment's own `x2 - x1 + 1` entries, not a whole
 *                     R_SCREEN_WIDTH block, which is what makes a frame with 71
 *                     clipped edges cost kilobytes instead of 45,440 B. It holds
 *                     both the sprite clip arrays (`sprtopclip`/
 *                     `sprbottomclip`) and the deferred masked mid texture
 *                     columns; offset zero is a valid entry, so the fields use
 *                     the negative sentinels above rather than 0/1.
 *
 * r_bsp_init() deliberately ignores these two: the walk neither reads nor
 * writes them, and a walk-only caller (the D3.1 harness) must keep compiling
 * without them. The passes that own them validate the pointers and the
 * capacities before writing a pixel, and report a capacity that is too small
 * instead of overrunning it.
 */
struct r_vissprite; /* r_things.h: the projected sprite record */

/* --- D3.9 cycle attribution ---------------------------------------------- */

/*
 * WHAT THIS IS, AND WHY IT IS IN THE VIEW STATE
 *
 * The frame table says how many microseconds each pass took; it cannot say what
 * those microseconds were spent on, and the D3.5 lesson is that a guess about
 * that is wrong by an order of magnitude. This struct is the second opinion:
 * every pass folds the CPU cycle counter into the bucket that names the work,
 * so "per-pixel store" and "per-column setup" and "per-sprite preflight" stop
 * being an argument and become two columns of a table.
 *
 * It is a pointer in the caller's state rather than a file-scope global because
 * nothing mutable lives at file scope in this library (the same rule every pass
 * follows for upstream's globals). It is only *declared* when
 * PIXELROOT32_DOOM_RASTER_PROBE is defined -- the frame measurement environment
 * sets it and every other build, including the shipped one, compiles the
 * instrumented expressions out entirely and pays a NULL test at the jitter of
 * the previous code.
 *
 * `r_probe_cycles()` is supplied by the caller, because the cycle counter is a
 * CPU feature and this library is also built for a host with none. The frame
 * driver answers it with the Xtensa CCOUNT register; a caller that sets
 * state->probe must link it.
 *
 * The struct is declared in every build so the instrumented expressions still
 * typecheck, but only a build that defines PIXELROOT32_DOOM_RASTER_PROBE can
 * reach them: R_PROBE_HAS() is a literal 0 there, every instrumented block is
 * dead code the compiler removes, and no translation unit asks for the cycle
 * counter. A shipped image therefore carries no probe and no counter call.
 */
typedef struct r_raster_probe {
    /* R_RenderBSPNode / R_Subsector / R_AddLine: how much of the walk is
     * traversal and how much is per-seg record reading. */
    uint32_t nodeVisits;
    uint32_t subsectorVisits;
    uint32_t segVisits;
    uint32_t bboxTests;
    uint32_t planeFindCalls;
    uint32_t planeFindCycle;
    uint32_t clipScanSteps;
    uint32_t wallRangeStores;
    uint32_t walkLineCycle;    /* add_line() including its record reads */
    uint32_t walkStoreCycle;   /* store_wall_range() including the range math */
    uint32_t walkSegLoopCycle; /* render_seg_loop(): the plane marking */
    uint32_t walkPlaneCheckCycle;
    uint32_t walkMapReads;     /* doom_map_get_* calls: the mapped-flash touches */
    uint32_t walkSubsectorCycle;

    /* The wall column pass, split where the cost changes shape. */
    uint32_t wallPreflightCycle;
    uint32_t wallClipSaveCycle;
    uint32_t wallSetupCycle;   /* one fragment column: yl/yh, tangent, light */
    uint32_t wallStoreCycle;   /* the r_column_draw() call for that column */
    uint32_t wallOpenCycle;    /* the two r_wall_fragment_open() per drawseg */
    uint32_t wallPoolCycle;    /* the clip/coverage clear loop */
    uint32_t wallGenericCycle; /* the two-sided loop, all of it */
    uint32_t wallEmitCycle;    /* one emit_fragment() call, all of it */
    uint32_t wallPrologueCycle; /* fragment copy, 3 resolves, peg and step */
    uint32_t wallLoopCycle;    /* the whole drawseg loop of r_wall_emit */
    uint32_t wallColumns;
    uint32_t wallPixels;

    /* The plane pass. */
    uint32_t planePreflightCycle;
    uint32_t planeSetupCycle;  /* per span: distance, steps, xfrac/yfrac, light */
    uint32_t planePixelCycle;  /* the per-pixel flat store */
    uint32_t planeCoverCycle;  /* the per-span covered[] run */
    uint32_t planeLoopCycle;   /* the whole per-plane loop of r_plane_draw */
    uint32_t planeSpans;

    /* The masked pass, split by what it iterates over. */
    uint32_t maskedPatchProofCycle; /* resolve + r_patch_validate per sprite */
    uint32_t maskedProjectionCycle;
    uint32_t maskedPatchProofs;     /* how many full patch walks ran */
    uint32_t maskedProofCalls;      /* how many resolve_patch() calls asked */
    uint32_t maskedPatchMemoHits;   /* how many of those the memo answered */
    uint32_t maskedRangeCheckCycle; /* the per-drawseg pool proof */
    uint32_t maskedClipSeedCycle;   /* the per-sprite drawseg scan */
    uint32_t maskedClipSeedSteps;   /* drawsegs × sprites it visited */
    uint32_t maskedSpriteCycle;     /* the sprite columns and their posts */
    uint32_t maskedPostCalls;
    uint32_t maskedTailCycle;       /* the far-to-near masked mid loop */
    uint32_t maskedSortCycle;
} r_raster_probe_t;

/* The CPU cycle counter, supplied by the caller. Monotonic for the life of a
 * boot and must not be reset inside a frame. */
uint32_t r_probe_cycles(void);

#define R_PROBE_OF(state) ((state)->probe)

#if defined(PIXELROOT32_DOOM_RASTER_PROBE)
#define R_PROBE_NOW() r_probe_cycles()
#define R_PROBE_HAS(state) ((state)->probe != NULL)
#else
/* No probe in this build: every instrumented block is `if (0)` and the counter
 * is never called, so a caller that leaves the pointer set still pays nothing
 * more than the four bytes the field costs. */
#define R_PROBE_NOW() 0u
#define R_PROBE_HAS(state) 0
#endif

typedef struct r_view_storage {
    r_drawseg_t* drawsegs;
    uint16_t drawsegCapacity;
    r_plane_set_t planes;
    r_clip_range_t* clipRanges;
    uint16_t clipRangeCapacity;
    int16_t* floorClip;   /* R_SCREEN_WIDTH entries */
    int16_t* ceilingClip; /* R_SCREEN_WIDTH entries */
    /* D3.4: the pass-owned pools. See above. */
    struct r_vissprite* visSprites;
    uint16_t visSpriteCapacity;
    /* The unified clip/opening pool: upstream's openings[] as entries. */
    int16_t* clipPool;
    uint16_t clipPoolCapacity;
    /* D3.9: an optional, caller-owned proof cache for the sprite patches the
     * masked pass validates. NULL means the pass proves each distinct patch once
     * per call, which is the behaviour every earlier revision had. See
     * r_things.h's r_sprite_proofs_t. */
    struct r_sprite_proofs* spriteProofs;
} r_view_storage_t;

/*
 * One view's state, owned by the caller. Public fields describe the result;
 * fields marked INTERNAL are the walked render state upstream keeps as
 * file-scope globals, moved here so two views (a test's, the game's) can
 * coexist and so nothing mutable lives outside this struct.
 *
 * Everything R_RenderSegLoop() used to read from file scope (the fractions, the
 * steppers, the tier flags) is *not* here: it belongs to one fragment and lives
 * in the stack-local r_wall_fragment_t (see the struct's comment).
 */
typedef struct r_view_state {
    const doom_map_t* map;
    r_view_storage_t storage;

    /* Result: valid after render(), even when render() reports an error. */
    uint16_t drawsegCount;
    uint16_t drawsegOverflowed;
    r_bsp_error_t error;

    /* Result of the D3.4 clip capture, valid after a successful r_wall_emit():
     * 1 when every drawseg's clip spans and masked columns are recorded, 0
     * after a walk (so the masked pass can refuse a frame whose walls were
     * never emitted). `clipBlocksUsed` and `openingBlocksUsed` are the
     * *block-equivalent* counts the previous per-block layout reported --
     * reserved constants included -- kept unchanged because SPRSTAT compares
     * them with the oracle. `clipPoolUsed` and `clipPoolEntries` are the real
     * currency of the unified pool: the entries a frame consumed and the
     * entries it needed, both measured in int16_t entries. */
    uint8_t clipCaptured;
    uint16_t clipBlocksUsed;
    uint16_t openingBlocksUsed;
    uint16_t clipPoolEntries; /* set by the wall preflight: what the frame needs */
    uint16_t clipPoolUsed;    /* set by the wall draw: what it consumed */

    /* INTERNAL: walk state. */
    r_view_params_t params;
    r_visplane_t* floorPlane;
    r_visplane_t* ceilingPlane;
    uint16_t frontSector;
    uint16_t currentSeg;
    angle_t rwAngle1;
    uint16_t clipRangeCount;

    /* The D3.9 cycle attribution, or NULL. The caller sets it after
     * r_bsp_init(); the walk and the three draw passes only read it. */
    r_raster_probe_t* probe;
} r_view_state_t;

/* --- Public API ---------------------------------------------------------- */

/**
 * @brief Binds a map and one view's storage to a state, and validates both.
 *
 * Nothing is written to the arrays; render() does that. Validation order:
 *
 *   1. state == NULL                              R_BSP_ERR_NULL_STATE
 *   2. map == NULL                                R_BSP_ERR_NULL_MAP
 *   3. map->open == 0                             R_BSP_ERR_MAP_NOT_OPEN
 *   4. storage == NULL                            R_BSP_ERR_NULL_STORAGE
 *   5. storage->drawsegs == NULL or capacity 0    R_BSP_ERR_NULL_DRAWSEGS
 *   6. storage->planes.planes == NULL or
 *      capacity 0                                 R_BSP_ERR_NULL_PLANES
 *   7. storage->clipRanges == NULL                R_BSP_ERR_NULL_CLIPS
 *   8. clipRangeCapacity < R_MAX_CLIP_SEGS        R_BSP_ERR_SMALL_CLIPS
 *   9. floorClip or ceilingClip is NULL            R_BSP_ERR_NULL_CLIP_ARRAYS
 *
 * The map and storage pointers are copied, so they must outlive the state.
 * A rejected call leaves the state zeroed.
 *
 * @return R_BSP_OK or one of the codes above.
 */
r_bsp_error_t r_bsp_init(r_view_state_t* state, const doom_map_t* map,
                         const r_view_storage_t* storage);

/**
 * @brief Walks the BSP for one viewpoint and fills the two output lists.
 *
 * The walk is upstream's R_RenderPlayerView() minus the emitters: clear the
 * clip posts, the drawsegs and the planes; clear floorclip[]/ceilingclip[];
 * recurse from the root; mark planes as walls are stored. A map with no nodes
 * renders its single subsector, as R_RenderBSPNode(-1) does upstream.
 *
 * On success the result is:
 *
 *   drawsegCount        entries, in near-to-far BSP order;
 *   drawsegOverflowed   1 when the drawseg capacity was reached. The call then
 *                       returns R_BSP_ERR_DRAWSEG_OVERFLOW, because a dropped
 *                       fragment is a hole in the picture, not a shorter list a
 *                       caller may draw (D4.2h);
 *   planes.count        planes allocated by find()/check(), in creation order.
 *                       A plane with minX > maxX was allocated and never marked;
 *                       R_DrawPlanes() would skip it, and so does the oracle's
 *                       *drawable* count, but it is still part of the set.
 *
 * @param state  An r_bsp_init()ed state.
 * @param params The viewpoint.
 * @return R_BSP_OK, or the first walk error (the lists are then partial but
 *         consistent), or a validation code for the arguments.
 */
r_bsp_error_t r_bsp_render(r_view_state_t* state, const r_view_params_t* params);

/**
 * @brief Finds the subsector containing a world point, as R_PointInSubsector().
 *
 * With no nodes this answers subsector 0, which is upstream's special case.
 * Otherwise the tree is descended from the last node. A reached index that is
 * not in SSECTORS is reported instead of dereferenced.
 */
r_bsp_error_t r_bsp_point_in_subsector(const doom_map_t* map, fixed_t x,
                                       fixed_t y, uint16_t* out);

/**
 * @brief Builds the E1M1-style spawn viewpoint from the map's THINGS lump.
 *
 * The rule is P_SpawnPlayer()'s, not an approximation of it:
 *
 *   - the start is the **last** THINGS record whose type is `playerIndex + 1`
 *     (1..4), because P_LoadThings() overwrites `playerstarts[type-1]` for each
 *     match in order;
 *   - x and y are the record's map units shifted by FRACBITS;
 *   - angle is ANG45 * (degrees / 45), so only the eight compass directions the
 *     1993 format can express are reachable;
 *   - z is the spawn sector's floor height plus R_VIEWHEIGHT (41 units), which
 *     is P_SpawnPlayer()'s `viewz = mobj->z + VIEWHEIGHT` after P_SpawnMobj()
 *     resolves ONFLOORZ to the subsector's floor;
 *   - extralight is 0 and the sky flat is F_SKY1.
 *
 * @param map         An open map.
 * @param playerIndex 0-based player slot; type is playerIndex + 1.
 * @param out         Receives the viewpoint; untouched on failure.
 * @return R_BSP_OK, R_BSP_ERR_NO_PLAYER_START or an argument/range code.
 */
r_bsp_error_t r_bsp_spawn_view(const doom_map_t* map, int playerIndex,
                               r_view_params_t* out);

/**
 * @brief Fills the map-facing half of one wall fragment.
 *
 * This is R_StoreWallRange()'s decision block, extracted so the walk and the
 * column emitter share one implementation of it: the front and back sectors,
 * the silhouette heights, worldhigh/worldlow, the outdoor sky hack, markfloor
 * and markceiling, the three tier flags, the wall light index, and the pre-shift
 * world heights the column steppers are derived from.
 *
 * It reads the map and the view only. It does not touch the clip arrays, the
 * plane set or the drawsegs, so calling it is free of side effects and calling
 * it twice for the same fragment (walk, then emitter) cannot change a result.
 *
 * Every index it follows was proved by doom_map_open(); a seg index the graph
 * never named is a caller bug, not a condition diagnosed here.
 *
 * @param map  An open map.
 * @param view The viewpoint the fragment was stored for.
 * @param seg  Index into SEGS.
 * @param out  Receives the fragment. `x1`, `x2`, the scales and the three range
 *             values are left 0: the walk's range helper fills them, and the
 *             emitter copies them from the drawseg.
 */
void r_wall_fragment_open(const doom_map_t* map, const r_view_params_t* view,
                          uint16_t seg, r_wall_fragment_t* out);

/**
 * @brief Computes the 12.4 column steppers from the fragment's world heights.
 *
 * R_StoreWallRange()'s last block: `worldtop >>= 4` and the four
 * topfrac/topstep/bottomfrac/bottomstep values, plus pixhigh/pixlow and their
 * steps for the two visible tiers of a two-sided line. `scale1` is the
 * fragment's starting texture scale and `scaleStep` its per-column step.
 *
 * The world heights are shifted inside this call, so the fragment keeps its
 * pre-shift values and the result does not depend on how many times it runs.
 *
 * @param fragment  A fragment from r_wall_fragment_open().
 * @param scale1    rw_scale at x1.
 * @param scaleStep rw_scalestep for the fragment.
 */
void r_wall_fragment_step(r_wall_fragment_t* fragment, fixed_t scale1,
                          fixed_t scaleStep);

/**
 * @brief Computes the three `dc_texturemid` values from the tier heights.
 *
 * R_StoreWallRange()'s pegging block, which is the one part of the fragment
 * that needs data this layer does not have: the height of the tier's texture.
 * `textureheight[]` is upstream's `texture->height << FRACBITS`. The bottom
 * tier is the one that never consults it -- upstream pegs it to worldTop or
 * worldLow only -- so there is no bottomHeight argument.
 *
 *   single-sided mid   ML_DONTPEGBOTTOM: front floor + mid height - view z,
 *                      otherwise worldTop; then += sidedef rowoffset
 *   top tier           ML_DONTPEGTOP: worldTop, otherwise back ceiling + top
 *                      height - view z; then += sidedef rowoffset
 *   bottom tier        ML_DONTPEGBOTTOM: worldTop, otherwise worldLow;
 *                      then += sidedef rowoffset
 *
 * A two-sided line's mid texture is upstream's *masked* texture: R_RenderSegLoop
 * records its column for R_DrawMasked and draws nothing, so wallMid is 0 for
 * such a fragment and the mid rule cannot reach it.
 *
 * Heights for tiers the fragment does not draw are ignored, so a caller may
 * pass 0 for them.
 *
 * @param fragment   A fragment from r_wall_fragment_open().
 * @param midHeight  The mid texture's height in 16.16, 0 when there is none.
 * @param topHeight  The top texture's height in 16.16.
 */
void r_wall_fragment_peg(r_wall_fragment_t* fragment, fixed_t midHeight,
                         fixed_t topHeight);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_R_BSP_H */
