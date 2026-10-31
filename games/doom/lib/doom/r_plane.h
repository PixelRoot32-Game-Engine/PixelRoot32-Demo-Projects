/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * Visplane bookkeeping: the set of floor and ceiling spans one view will fill.
 *
 * DOOM does not draw floors and ceilings as spans while it walks the BSP. It
 * records, per view, a small set of horizontal planes -- one per distinct
 * (height, picture, light level) -- and, for each screen column covered by that
 * plane, the first and last row. After the walk, R_DrawPlanes() turns those
 * column runs into spans. This file is the recording half.
 *
 * THE TWO RULES THAT MATTER
 *
 *   find   A plane is identified by exactly three values: its world height, its
 *          flat picture and its light level. Two sectors that agree on all
 *          three share one plane, even when they are different sectors, which
 *          is what makes a corridor of identical floor tiles one plane.
 *
 *   check  A wall fragment asks for a column range. If the requested range
 *          does not touch any column the plane has already recorded, the range
 *          is folded into that plane (its min/max widen). If it does touch one,
 *          a second plane with the same three keys is allocated for the new
 *          range. This is DOOM's R_CheckPlane() split: two disjoint runs of the
 *          same floor cannot share one top[]/bottom[] array without the
 *          emitter confusing them.
 *
 * THE SKY RULE
 *
 * DOOM gives every sky flat the same identity: in R_FindPlane(), a sky
 * `picnum` is stored as height 0 and light 0, so every outdoor ceiling in the
 * level collapses into one plane regardless of its sector's ceiling height.
 * E1M1 depends on it. `skyPicNum` is the packed name of the sky flat (F_SKY1);
 * pass R_PLANE_NO_SKY to disable the rule for a level with no sky.
 *
 * THE CAP
 *
 * Upstream's MAXVISPLANES is 128 and it calls I_Error() on overflow. Here the
 * caller owns the array and its capacity, and going over is a reported error:
 * the plane that would have been created is not created and nothing already
 * recorded is touched, so a view that fails this way is partial but not
 * corrupt. The real E1M1 spawn view needs far fewer than 128; the measured
 * number is in the D3.1 report.
 *
 * WHY BOTH ARRAYS ARE 0xFF-FILLED
 *
 * Upstream fills `top[]` only and leaves `bottom[]` stale, because a column
 * whose top is 0xFF is never read. This port fills both, for the same reason it
 * fills both in r_plane_check(): a dumped plane is then deterministic byte for
 * byte, which is what lets the host oracle compare the whole plane rather than
 * only the marked columns. It cannot change a pixel: every write path sets top
 * and bottom together.
 *
 * THE OTHER HALF: THE SPAN EMITTER (D3.3)
 *
 * r_plane_draw() is upstream's R_DrawPlanes(): it runs after the wall pass, in
 * plane index order, and fills the column runs the walk marked. The full frame
 * is therefore BSP walk -> wall pass -> plane pass, which is R_RenderPlayerView
 * minus sprites and the masked mid textures.
 *
 *   R_MakeSpans     The column runs are not drawn column by column. For each
 *                   plane, a per-row state machine walks the plane's columns
 *                   from minX to maxX+1 and remembers, in spanstart[row], the
 *                   column where the current run of that row started. When a
 *                   row's run ends (the next column's top is below it, or its
 *                   bottom is above it), the whole horizontal run is drawn in
 *                   one R_MapPlane call. That is why a row is one span however
 *                   many columns it covers, and why an empty column in the
 *                   middle of a plane closes one run and starts another.
 *
 *   no pad words    Upstream writes top[maxX+1] = top[minX-1] = 0xff before the
 *                   walk so that the two columns outside the plane read as
 *                   empty. This struct has no pads, so the emitter substitutes
 *                   the value instead of writing it: the column before minX and
 *                   the column after maxX are passed to the state machine as
 *                   (R_PLANE_EMPTY, 0). The proof that no pixel changes: the
 *                   only comparison the sentinel write can influence is
 *                   "top[x] == R_PLANE_EMPTY", because every branch below reads
 *                   top first and a top of 0xFF makes the bottom irrelevant --
 *                   a span is only drawn for rows `t..b` with t <= b, and t is
 *                   the top. The edge test produces that same comparison
 *                   result for the two outside columns. (The bottom value 0 is
 *                   not a second rule: upstream never writes bottom[] outside
 *                   the range and its array is zero-initialised, so 0 is what
 *                   the state machine sees there; using R_PLANE_EMPTY would
 *                   instead make its `b1 > b2 && b1 >= t1` test true for a row
 *                   255 that cannot exist.)
 *
 *   the span maths  R_MapPlane() recomputes, for each span:
 *                     distance = FixedMul(|planeheight - viewz|, yslope[y])
 *                     xstep    = FixedMul(distance, basexscale)
 *                     ystep    = FixedMul(distance, baseyscale)
 *                     length   = FixedMul(distance, distscale[x1])
 *                     xfrac    = viewx  + FixedMul(cos(viewangle+xtoviewangle[x1]), length)
 *                     yfrac    = -viewy - FixedMul(sin(viewangle+xtoviewangle[x1]), length)
 *                   and R_DrawSpan() walks x from x1 to x2 adding the two steps
 *                   and reading
 *                     spot = ((yfrac >> 10) & 4032) + ((xfrac >> 16) & 63)
 *                   from the 64 x 64 row-major flat. basexscale and baseyscale
 *                   are R_ClearPlanes()'s two values for the view angle; yslope
 *                   and distscale are generated per-view constants (r_tables.h),
 *                   because this port has one view size and upstream rebuilds
 *                   them in R_ExecuteSetViewSize().
 *
 *   plane light     R_DrawPlanes() selects the plane's colormap from two
 *                   numbers, and neither is the wall's: the row is
 *                   `(plane->lightLevel >> LIGHTSEGSHIFT) + extralight`, clamped
 *                   to LIGHTLEVELS, with no per-seg-axis correction; the column
 *                   is `distance >> LIGHTZSHIFT`, clamped to MAXLIGHTZ-1, i.e.
 *                   the distance from the eye to the plane along that row's
 *                   view ray, not a texture scale. The table is zlight[][]
 *                   (r_plane_light), built by R_InitLightTables(), not the
 *                   wall's scalelight[][] (r_light_level). They differ visibly
 *                   at close range: the plane table's darkest light row starts
 *                   at map 0 (its nearest column is 80 maps brighter than
 *                   startmap), while the wall table's row is a constant 31.
 *
 *   the sky         A plane whose picture equals the frame's sky flat is not a
 *                   flat at all. R_DrawPlanes() draws it with R_DrawColumn(),
 *                   once per marked column, from the level's sky *texture*
 *                   (upstream's skytexture, "SKY1" in episode 1), through
 *                   COLORMAP table 0 -- full bright, and the identity map, so
 *                   the texel is stored directly with no colormap read -- and
 *                   with the column chosen by
 *                   by `(viewangle + xtoviewangle[x]) >> ANGLETOSKYSHIFT`,
 *                   masked by the sky texture's width mask. Its vertical scale
 *                   is pspriteiscale -- FRACUNIT * SCREENWIDTH / viewwidth,
 *                   which is FRACUNIT for this port's full-width 320 column --
 *                   and its texturemid is skytexturemid, upstream's literal
 *                   100*FRACUNIT. For this port's 200-row view 100 is
 *                   R_CENTER_Y, so the horizon row shows texture row 100 and
 *                   every row is one texel. The row wrap is the stored column's
 *                   height mask, exactly as in the wall pass, so DOOM's 128-row
 *                   SKY1 repeats every 128 rows: a 200-row view shows rows
 *                   0..127 and then 0..71. A sky texture declared taller than
 *                   its stored column is wrapped by that mask; vanilla's
 *                   hard-coded `& 127` would wrap it at 128 whatever its
 *                   declared height, which is what the harness's 128-row
 *                   storage reproduces. The sky does not go through the span
 *                   maker: it overwrites its column run directly, which is why
 *                   its pixels are the texture's bytes and not a flat's.
 *
 *   flat residency  The emitter never composes or caches a flat. It resolves
 *                   one through r_tex_source_t::resolveFlat per drawable plane,
 *                   validates it, and reads it; the returned record must stay
 *                   valid until the call returns. The sky texture comes through
 *                   the same source's wall resolver, named by
 *                   r_tex_source_t::skyTexture.
 *
 *   error contract  r_plane_draw() validates its arguments, then resolves and
 *                   validates every drawable plane's flat (or sky texture), and
 *                   only then writes the first byte. A missing or malformed
 *                   flat -- or a sky plane with no resolvable sky texture --
 *                   therefore leaves the surface, its coverage mask, its plane
 *                   mask and its counters byte for byte as the caller left
 *                   them. This is the same preflight shape r_wall_emit() uses,
 *                   and it costs two resolver calls per drawable plane (one to
 *                   prove the frame resolvable, one to draw it).
 *
 * This is C99 only: <stddef.h> and <stdint.h>, no allocation, no globals, no
 * stdio, no floating point. The caller owns the plane array and the set.
 */
#ifndef DOOM_R_PLANE_H
#define DOOM_R_PLANE_H

#include <stdint.h>

#include "r_defs.h"
#include "r_light.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The sentinel that says a column has no span in this plane. */
#define R_PLANE_EMPTY 0xFFu

/* "This level has no sky flat": a value no eight-byte name can pack to. */
#define R_PLANE_NO_SKY ((r_name_t)UINT64_MAX)

/*
 * The three sky constants, from upstream's r_sky.c and r_main.c.
 *
 *   R_PLANE_SKY_ANGLE_SHIFT  ANGLETOSKYSHIFT. The sky is a 1024-column texture
 *                            wrapped around the full turn: a 32-bit angle
 *                            >> 22 is a column of the 1024-wide strip, and the
 *                            texture's own width mask folds that into its 256
 *                            columns four times around.
 *   R_PLANE_SKY_ISCALE       R_DrawPlanes' `dc_iscale = pspriteiscale >>
 *                            detailshift`, and R_ExecuteSetViewSize()'s
 *                            pspriteiscale = FRACUNIT * SCREENWIDTH / viewwidth.
 *                            This port's view is the full 320 columns, so it is
 *                            FRACUNIT: one texel per screen row.
 *   R_PLANE_SKY_TEXTURE_MID  skytexturemid, which R_InitSkyMap() sets to
 *                            upstream's literal 100*FRACUNIT. This port's full
 *                            200-row view has R_CENTER_Y == 100, so the horizon
 *                            row shows texture row 100 and writing the constant
 *                            as R_CENTER_Y_FRAC keeps the two in step. Vanilla
 *                            keeps the literal 100 when it shrinks the view
 *                            window, so a future HUD that changes R_VIEW_HEIGHT
 *                            must decide whether to keep that literal or re-peg;
 *                            the full-frame harness does not reach the choice.
 */
#define R_PLANE_SKY_ANGLE_SHIFT 22
#define R_PLANE_SKY_ISCALE FRACUNIT
#define R_PLANE_SKY_TEXTURE_MID R_CENTER_Y_FRAC

/* The two types the emitter names but does not define. Keeping them incomplete
 * here is what lets r_plane.h stay independent of r_bsp.h and r_segs.h, both of
 * which include it. */
struct r_view_state; /* r_bsp.h: the walked view, its planes and its params */
struct r_tex_source; /* r_segs.h: the wall/flat/sky residency contract */

/*
 * One plane. `minX > maxX` means nothing has been recorded yet; a plane is
 * allocated by find() and may stay empty for the whole view (upstream keeps
 * those in the array too and R_DrawPlanes skips them).
 *
 * `top[x]` and `bottom[x]` are inclusive screen rows, or R_PLANE_EMPTY in
 * `top` for a column this plane does not cover. Rows are screen coordinates:
 * 0 is the top of the 200-row surface.
 *
 * Upstream's visplane_t carries pad bytes so R_DrawPlanes() can write
 * `top[maxX+1]` and `top[minX-1]` as sentinels before walking the column runs.
 * This struct is exactly R_SCREEN_WIDTH wide, so those two writes are not in
 * bounds here. D3.3 keeps that layout (adding the pads would change
 * sizeof(r_visplane_t) and the byte stream the D3.1 oracle hashes) and replaces
 * the sentinel writes with an edge test in the span maker. r_plane_draw()
 * documents the equivalence: the sentinels only ever set a top byte to
 * R_PLANE_EMPTY, which is exactly what the edge test asks, so no pixel can
 * depend on the missing pad words. `bottom[]` outside [minX, maxX] is
 * R_PLANE_EMPTY here because this port fills both arrays (see above); the span
 * maker therefore treats an empty top as an empty span regardless of the bottom
 * byte, which is what upstream's zero-initialised bottom[] gives it in
 * practice.
 */
typedef struct r_visplane {
    fixed_t height;      /* world z of the plane, 16.16 */
    r_name_t picNum;     /* flat picture identity */
    int16_t lightLevel;  /* sector light level the plane was found with */
    int16_t minX;        /* first recorded column, or > maxX when empty */
    int16_t maxX;        /* last recorded column */
    uint8_t top[R_SCREEN_WIDTH];
    uint8_t bottom[R_SCREEN_WIDTH];
} r_visplane_t;

/*
 * The caller's plane storage plus the live count. `planes` and `capacity` are
 * set once; find() and check() append and never reorder, so plane index i is
 * the order R_DrawPlanes() would visit them in.
 */
typedef struct r_plane_set {
    r_visplane_t* planes;
    uint16_t capacity;
    uint16_t count;
} r_plane_set_t;

/* --- Flats --------------------------------------------------------------- */

/*
 * One flat, in the form R_DrawSpan() reads. A DOOM flat is a 64 x 64 square of
 * raw palette indices with no header (r_data.h, R_FLAT_BYTES), stored row-major:
 * R_DrawSpan() addresses it as `spot = row * 64 + column`.
 *
 *   width, height      64 and 64. They are carried rather than assumed so the
 *                      emitter can reject a misshapen flat before writing; the
 *                      span loop's wrap is upstream's hard-coded `& 63`, not a
 *                      computed mask, because DOOM has no other flat size.
 *   widthMask          63, the `& 63` of R_DrawSpan's u coordinate.
 *   heightMask         63, the `& 4032` of its v coordinate (63 * 64).
 *
 * A flat that is *not resident* is never composed here: the resolver reports it
 * and r_plane_draw() fails before writing. A caller that resolves a flat from a
 * mapped region needs no copy: `pixels` points at the 4096 raw bytes.
 */
typedef struct r_flat {
    const uint8_t* pixels;
    int16_t width;
    int16_t height;
    int16_t widthMask;
    int16_t heightMask;
} r_flat_t;

/**
 * @brief Resolves a packed flat name to bytes, or NULL when not resident.
 *
 * Called once per drawable plane in the pass's preflight and once more when that
 * plane is drawn; never per span and never per pixel. The returned record must
 * stay valid until r_plane_draw() returns; the emitter copies nothing.
 *
 * The policy behind the callback -- a cache, a zone block, a mapped flash
 * region, "decode it now" -- is the caller's. Answering NULL is a normal
 * outcome and produces R_PLANE_ERR_FLAT_MISSING, not a partial frame.
 */
typedef const r_flat_t* (*r_flat_resolve_fn)(void* context, r_name_t name);

/* --- Error codes --------------------------------------------------------- */

/*
 *   R_PLANE_OK            the call did what it says
 *   R_PLANE_ERR_NULL_SET  the caller passed no set
 *   R_PLANE_ERR_NULL_PLANES the set has no storage
 *   R_PLANE_ERR_NULL_OUT  the out parameter the call needs is NULL
 *   R_PLANE_ERR_NULL_PLANE check() was given no plane
 *   R_PLANE_ERR_NO_ROOM   the capacity is reached; no plane was added
 *   R_PLANE_ERR_RANGE     the column range is not inside the screen, or
 *                         start > stop
 *
 * The emitter's codes. They are appended so the D3.1/D3.2 numbers do not move.
 *
 *   R_PLANE_ERR_NULL_STATE        no view state
 *   R_PLANE_ERR_MAP_NOT_OPEN      the state holds no walked map
 *   R_PLANE_ERR_NULL_SURFACE      no surface descriptor
 *   R_PLANE_ERR_SURFACE_PIXELS    surface->pixels is NULL
 *   R_PLANE_ERR_SURFACE_SIZE      width < R_SCREEN_WIDTH or height < R_VIEW_HEIGHT
 *   R_PLANE_ERR_NULL_SOURCE       no texture/flat source
 *   R_PLANE_ERR_NULL_RESOLVE      no flat resolver, or a sky plane with no wall
 *                                 resolver
 *   R_PLANE_ERR_NULL_LIGHT        no initialised r_light_t
 *   R_PLANE_ERR_PLANE_RANGE       a drawable plane's minX/maxX is outside the
 *                                 screen
 *   R_PLANE_ERR_FLAT_MISSING      the flat resolver answered NULL
 *   R_PLANE_ERR_FLAT_SHAPE        a resolved flat is not a usable 64 x 64
 *                                 square: NULL pixels, width/height other than
 *                                 64, or a mask other than 63
 *   R_PLANE_ERR_SKY_MISSING       a sky plane exists but the source names no
 *                                 sky texture or its resolver answered NULL
 *   R_PLANE_ERR_SKY_SHAPE         the resolved sky texture is unusable, by the
 *                                 same geometry rules as a wall texture
 *
 * Every emitter code is returned before the first pixel write, so the surface,
 * its coverage record, its plane mask and its counters are untouched on any
 * failure.
 */
typedef enum r_plane_error {
    R_PLANE_OK = 0,
    R_PLANE_ERR_NULL_SET = 1,
    R_PLANE_ERR_NULL_PLANES = 2,
    R_PLANE_ERR_NULL_OUT = 3,
    R_PLANE_ERR_NULL_PLANE = 4,
    R_PLANE_ERR_NO_ROOM = 5,
    R_PLANE_ERR_RANGE = 6,
    R_PLANE_ERR_NULL_STATE = 7,
    R_PLANE_ERR_MAP_NOT_OPEN = 8,
    R_PLANE_ERR_NULL_SURFACE = 9,
    R_PLANE_ERR_SURFACE_PIXELS = 10,
    R_PLANE_ERR_SURFACE_SIZE = 11,
    R_PLANE_ERR_NULL_SOURCE = 12,
    R_PLANE_ERR_NULL_RESOLVE = 13,
    R_PLANE_ERR_NULL_LIGHT = 14,
    R_PLANE_ERR_PLANE_RANGE = 15,
    R_PLANE_ERR_FLAT_MISSING = 16,
    R_PLANE_ERR_FLAT_SHAPE = 17,
    R_PLANE_ERR_SKY_MISSING = 18,
    R_PLANE_ERR_SKY_SHAPE = 19
} r_plane_error_t;

/* --- Public API ---------------------------------------------------------- */

/**
 * @brief Empties the set: the next find() starts again at index 0.
 *
 * The plane bytes are not cleared; find() and check() initialise every plane
 * they return, so a stale plane can never be handed out. A NULL set is a no-op.
 */
void r_plane_clear(r_plane_set_t* set);

/**
 * @brief Finds or allocates the plane with one (height, picture, light) key.
 *
 * The sky rule runs first: when @p picNum equals @p skyPicNum the stored height
 * and light are both forced to zero, so all sky planes share one identity.
 *
 * The scan is upstream's: plane order, first exact key wins. A miss appends a
 * new plane with `minX = R_SCREEN_WIDTH` and `maxX = -1` (empty), and fills
 * top[] and bottom[] with R_PLANE_EMPTY.
 *
 * Validation order:
 *
 *   1. set == NULL                            R_PLANE_ERR_NULL_SET
 *   2. set->planes == NULL                    R_PLANE_ERR_NULL_PLANES
 *   3. out == NULL                            R_PLANE_ERR_NULL_OUT
 *   4. count == capacity                      R_PLANE_ERR_NO_ROOM
 *
 * @param set       Caller-owned plane set.
 * @param height    World z of the plane, 16.16.
 * @param picNum    Packed flat name.
 * @param lightLevel Sector light level.
 * @param skyPicNum Packed name of the sky flat, or R_PLANE_NO_SKY.
 * @param out       Receives the plane; untouched on failure.
 * @return R_PLANE_OK or one of the codes above.
 */
r_plane_error_t r_plane_find(r_plane_set_t* set, fixed_t height, r_name_t picNum,
                             int16_t lightLevel, r_name_t skyPicNum,
                             r_visplane_t** out);

/**
 * @brief Folds a column range into @p plane, or splits a new plane off it.
 *
 * The intersection of [start, stop] with the plane's recorded range is scanned:
 * if no column in it has a top byte, the run is disjoint from what the plane
 * holds, so the plane's min/max are widened to the union and @p plane is
 * returned. Otherwise a new plane with the same three keys is appended with
 * exactly [start, stop], and the plane that was passed in is left as it was.
 *
 * Validation order:
 *
 *   1. set == NULL                            R_PLANE_ERR_NULL_SET
 *   2. set->planes == NULL                    R_PLANE_ERR_NULL_PLANES
 *   3. plane == NULL                          R_PLANE_ERR_NULL_PLANE
 *   4. out == NULL                            R_PLANE_ERR_NULL_OUT
 *   5. start < 0, stop >= R_SCREEN_WIDTH, or
 *      start > stop                           R_PLANE_ERR_RANGE
 *   6. a split is needed and count == capacity R_PLANE_ERR_NO_ROOM
 *
 * @param set   Caller-owned plane set.
 * @param plane The plane the current floor/ceiling was found in.
 * @param start First screen column, inclusive.
 * @param stop  Last screen column, inclusive.
 * @param out   Receives the plane to mark; untouched on failure.
 * @return R_PLANE_OK or one of the codes above.
 */
r_plane_error_t r_plane_check(r_plane_set_t* set, r_visplane_t* plane, int start,
                              int stop, r_visplane_t** out);

/**
 * @brief Draws every marked plane into @p surface: upstream's R_DrawPlanes().
 *
 * Runs after r_wall_emit(), in plane index order (the order r_plane_find() and
 * r_plane_check() allocated them), over `state->storage.planes.planes[0 ..
 * count)`. A plane with `minX > maxX` was allocated and never marked; it is
 * skipped, as upstream's `if (pl->minx > pl->maxx) continue;` does. Nothing the
 * wall pass set is cleared: the pixels, the covered columns and the wall tier
 * counters are left alone, and the pass only writes.
 *
 * Two paths, one entry point:
 *
 *   flat    A plane whose picture is not the frame's sky flat. Its flat is
 *           resolved once in preflight and again when it is drawn, validated,
 *           and turned into spans by R_MakeSpans(): for x from minX to maxX+1,
 *           the column runs still open are closed and the new ones recorded, and
 *           R_MapPlane() draws each closed run. The plane's light is
 *           r_plane_light[row][distance >> LIGHTZSHIFT], with
 *           row = (lightLevel >> LIGHTSEGSHIFT) + extralight clamped to
 *           LIGHTLEVELS -- one dimension per plane, not the wall's two.
 *
 *   sky     A plane whose picture equals `state->params.skyPicNum` (and the
 *           frame was given a sky; R_PLANE_NO_SKY disables the path). Each
 *           marked column is drawn directly from the source's sky texture,
 *           full bright, with the column chosen by
 *           `(params.angle + r_x_to_view_angle[x]) >> R_PLANE_SKY_ANGLE_SHIFT`
 *           masked by the texture's width mask and the rows by its height mask.
 *           The sky never goes through the span maker, so `planeSpans` does not
 *           count its columns. A sky plane with no resolvable texture is
 *           R_PLANE_ERR_SKY_MISSING, never a blank ceiling.
 *
 * The surface must be at least R_SCREEN_WIDTH x R_VIEW_HEIGHT; `width` is its
 * row stride. `surface->planeMask`, when not NULL, is cleared and then set to 1
 * for every pixel this pass writes; `surface->covered` is set to 1 for every
 * column it writes (it is not cleared, because the wall pass owns the wall
 * coverage); `surface->planePixels` and `surface->planeSpans` are reset and then
 * counted. Pixels no plane covers keep whatever the wall pass or the caller put
 * there.
 *
 * Validation order:
 *
 *   1. state == NULL                          R_PLANE_ERR_NULL_STATE
 *   2. state->map == NULL or not open         R_PLANE_ERR_MAP_NOT_OPEN
 *   3. surface == NULL                        R_PLANE_ERR_NULL_SURFACE
 *   4. surface->pixels == NULL                R_PLANE_ERR_SURFACE_PIXELS
 *   5. surface->width < R_SCREEN_WIDTH or
 *      surface->height < R_VIEW_HEIGHT        R_PLANE_ERR_SURFACE_SIZE
 *   6. textures == NULL                       R_PLANE_ERR_NULL_SOURCE
 *   7. textures->resolveFlat == NULL          R_PLANE_ERR_NULL_RESOLVE
 *   8. light == NULL or light->colormaps == NULL R_PLANE_ERR_NULL_LIGHT
 *   9. a drawable plane's minX/maxX is
 *      outside [0, R_SCREEN_WIDTH)            R_PLANE_ERR_PLANE_RANGE
 *  10. a flat does not resolve               R_PLANE_ERR_FLAT_MISSING
 *  11. a flat is not 64 x 64 with masks 63    R_PLANE_ERR_FLAT_SHAPE
 *  12. a sky plane has no sky texture name or
 *      it does not resolve                    R_PLANE_ERR_SKY_MISSING
 *  13. the sky texture is unusable            R_PLANE_ERR_SKY_SHAPE
 *  14. a sky plane exists and the source has
 *      no wall resolver                       R_PLANE_ERR_NULL_RESOLVE
 *
 * Steps 9 to 14 run for every drawable plane before the first pixel is written,
 * so any failure leaves the surface exactly as the caller left it.
 *
 * @param state    A state whose r_bsp_render() has run.
 * @param surface  The destination surface and its records.
 * @param textures The source: its flat resolver, its flat context and its sky
 *                 texture name (r_segs.h).
 * @param light    A COLORMAP initialised by r_light_init().
 * @return R_PLANE_OK, or one of the codes above.
 */
r_plane_error_t r_plane_draw(struct r_view_state* state, r_draw_surface_t* surface,
                             const struct r_tex_source* textures,
                             const r_light_t* light);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_R_PLANE_H */
