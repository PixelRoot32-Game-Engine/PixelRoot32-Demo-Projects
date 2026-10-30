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
 * This is C99 only: <stddef.h> and <stdint.h>, no allocation, no globals, no
 * stdio, no floating point. The caller owns the plane array and the set.
 */
#ifndef DOOM_R_PLANE_H
#define DOOM_R_PLANE_H

#include <stdint.h>

#include "r_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The sentinel that says a column has no span in this plane. */
#define R_PLANE_EMPTY 0xFFu

/* "This level has no sky flat": a value no eight-byte name can pack to. */
#define R_PLANE_NO_SKY ((r_name_t)UINT64_MAX)

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
 * bounds here: the span emitter (D3.3) must test the edge columns instead of
 * writing past them. The alternative -- adding the pads -- would change
 * sizeof(r_visplane_t) and the byte stream the host oracle hashes, so it is a
 * D3.3 decision, not one to pre-empt in the walk.
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
 */
typedef enum r_plane_error {
    R_PLANE_OK = 0,
    R_PLANE_ERR_NULL_SET = 1,
    R_PLANE_ERR_NULL_PLANES = 2,
    R_PLANE_ERR_NULL_OUT = 3,
    R_PLANE_ERR_NULL_PLANE = 4,
    R_PLANE_ERR_NO_ROOM = 5,
    R_PLANE_ERR_RANGE = 6
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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_R_PLANE_H */
