/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * The automap, from upstream's am_map.c.
 *
 * WHAT IS PORTED, AND FROM WHERE
 *
 *   AM_LevelInit /         the window, the entry scale, AM_findMinMaxBoundaries'
 *   AM_findMinMaxBoundaries  vertex sweep and the two zoom limits
 *   AM_Start / AM_Stop /   the per-open state: window size from the entry scale,
 *   AM_initVariables       centre on the player, AM_changeWindowLoc's edge clamp
 *   AM_Ticker /            the once-per-tic window: follow, zoom, pan
 *   AM_doFollowPlayer
 *   AM_changeWindowScale,  the zoom arithmetic and the two clamps,
 *   AM_activateNewScale,   AM_minOutWindowScale / AM_maxOutWindowScale
 *   AM_saveScaleAndLoc     kept for AM_restoreScaleAndLoc's rule even though the
 *   AM_restoreScaleAndLoc  '0' key that calls it is not on the board
 *   AM_updateLightLev      the strobe table (see below: not called by the tick)
 *   AM_clipMline           the Cohen-Sutherland outcode clipper, verbatim
 *   AM_drawFline           the Bresenham rasteriser
 *   AM_drawMline           clip, then draw
 *   AM_clearFB,            the frame: background, grid, walls, players,
 *   AM_drawGrid,           crosshair, in AM_Drawer's order. AM_drawThings,
 *   AM_drawWalls,          AM_drawMarks and V_MarkRect are the three lines of
 *   AM_rotate,             that sequence this port does not have (below).
 *   AM_drawLineCharacter,
 *   AM_drawPlayers,
 *   AM_drawCrosshair,
 *   AM_Drawer
 *
 * THE ONE NUMBER THAT COMES FROM A DOUBLE
 *
 * AM_LevelInit's entry scale is `FixedDiv(min_scale_mtof, (int)(0.7*FRACUNIT))`.
 * This layer has no floating point, so the divisor is the integer upstream's
 * double expression truncates to (45,875), stated as `FRACUNIT * 7 / 10` with
 * the derivation in the comment. The same applies to `INITSCALEMTOF`,
 * `M_ZOOMIN` and `M_ZOOMOUT`.
 *
 * WHAT IS DELIBERATELY NOT PORTED, AND WHY
 *
 *   1. Marks. `AM_NUMMARKPOINTS`, `markpoints[]`, `AM_loadPics`, `AM_addMark`,
 *      `AM_clearMarks` and `AM_drawMarks`. The board has no keyboard to press
 *      `m` or `c`, the `AMMNUM*` art is not in the container, and a mark nobody
 *      can place is state and a patch lookup nothing reads.
 *   2. Things. `AM_drawThings()` and the `IDDT` cheat sequence: no cheats.
 *   3. Followers. AM_drawPlayers' netgame loop, `their_colors[]` and
 *      `cheat_player_arrow`: this port is one player on one board, and the
 *      single-player arrow is upstream's `!netgame` branch.
 *   4. The keyboard. `AM_Responder()`'s `event_t` switch and its cheat
 *      sequence. The board has buttons, so the same state changes are exposed
 *      as am_pan() / am_zoom() / am_toggle_follow() / am_toggle_grid(), called
 *      once per frame with the held buttons.
 *   5. The status bar. Upstream's `ST_Responder(&st_notify)` tells the bar the
 *      automap came and went (`AM_MSGENTERED`/`AM_MSGEXITED`). This port's bar
 *      has no message area and no responder, so there is nothing to tell.
 *   6. The strobe. `AM_updateLightLev()` is ported and exported, but
 *      `AM_Ticker()` does NOT call it: upstream 1.10's own call is commented out
 *      (`// AM_updateLightLev();`, am_map.c's AM_Ticker). The reference picture
 *      therefore has `lightlev` at 0 for the life of a level, every `+lightlev`
 *      in AM_drawWalls is the base colour, and this port reproduces exactly
 *      that. A caller that wants the strobe calls am_update_light_lev() from its
 *      tic loop; nothing else changes.
 *   7. `AM_getIslope()`. Upstream's slope helper is called by nothing in 1.10 --
 *      the rasteriser computes its own deltas -- so porting it would be a
 *      function no path can reach.
 *
 * TWO DIVERGENCES THE BOARD FORCED, BOTH NAMED
 *
 *   a. The arrow keys pan only while `followplayer` is 0 upstream, because
 *      following, they turn the player instead, and 'f' is what stops the
 *      follow. The board has no 'f', so a d-pad press clears the follow and
 *      starts the pan in the same frame (am_pan()'s comment). The window still
 *      starts from the player and still stops at the level's edges, because
 *      that is what AM_changeWindowLoc() does.
 *   b. AM_drawFline()'s out-of-range case is a debug `fprintf` upstream and its
 *      RANGECHECK build calls I_Error(). This layer has no stdio and must not
 *      abort a frame, so an out-of-range line draws nothing and is counted by
 *      the clipper's caller ([0, f_w) x [0, f_h) is unreachable for any line
 *      AM_clipMline accepts, so this is a guard on a corrupt state).
 *
 * THE FRAME BUFFER
 *
 * Upstream writes `screens[0]` through `fb` and a hard-coded `f_w` stride. Here
 * the surface and its stride are arguments to am_draw(), and the state keeps
 * them for the duration of the call; a pixel is
 * `fb[(f_y + y) * stride + f_x + x]`. The window is f_x = f_y = 0, f_w = 320 and
 * f_h = 168 (`SCREENHEIGHT - 32`), so nothing outside rows 0..167 or columns
 * 0..319 is ever written.
 *
 * This is C99 only: no allocation, no globals, no stdio, no floating point.
 */
#include "am_map.h"

#include <string.h>

#include "r_tables.h"

/* --- Upstream's constants ------------------------------------------------ */

/* Automap colours, exactly upstream's palette indices. */
#define AM_REDS (256 - 5 * 16)
#define AM_REDRANGE 16
#define AM_GREENS (7 * 16)
#define AM_GREENRANGE 16
#define AM_GRAYS (6 * 16)
#define AM_GRAYSRANGE 16
#define AM_BROWNS (4 * 16)
#define AM_BROWNRANGE 16
#define AM_YELLOWS (256 - 32 + 7)
#define AM_YELLOWRANGE 1
#define AM_BLACK 0
#define AM_WHITE (256 - 47)

#define AM_BACKGROUND AM_BLACK
#define AM_YOURCOLORS AM_WHITE
#define AM_YOURRANGE 0
#define AM_WALLCOLORS AM_REDS
#define AM_WALLRANGE AM_REDRANGE
#define AM_TSWALLCOLORS AM_GRAYS
#define AM_TSWALLRANGE AM_GRAYSRANGE
#define AM_FDWALLCOLORS AM_BROWNS
#define AM_FDWALLRANGE AM_BROWNRANGE
#define AM_CDWALLCOLORS AM_YELLOWS
#define AM_CDWALLRANGE AM_YELLOWRANGE
#define AM_THINGCOLORS AM_GREENS
#define AM_THINGRANGE AM_GREENRANGE
#define AM_SECRETWALLCOLORS AM_WALLCOLORS
#define AM_SECRETWALLRANGE AM_WALLRANGE
#define AM_GRIDCOLORS (AM_GRAYS + AM_GRAYSRANGE / 2)
#define AM_GRIDRANGE 0
#define AM_XHAIRCOLORS AM_GRAYS

/* The linedef flags the map tests that doom_map.h does not name (it lists the
 * ones the renderer tests). Both are P_LoadLineDefs' ML_SECRET / ML_DONTDRAW. */
#define AM_ML_SECRET 32
#define AM_ML_DONTDRAW 128

/* Upstream's `LINE_NEVERSEE ML_DONTDRAW`: a line the renderer never draws is
 * never seen on the map either. */
#define AM_LINE_NEVERSEE AM_ML_DONTDRAW

/* The grid is aligned to the block map: P_LoadBlockMap's MAPBLOCKUNITS. */
#define AM_MAPBLOCKUNITS (128 * FRACUNIT)

/* PLAYERRADIUS is (16*FRACUNIT) in doomdef.h; the arrow and the zoom-in limit
 * are both expressed in it. */
#define AM_PLAYERRADIUS (16 * FRACUNIT)

/* Upstream's double expressions, as the integers they truncate to:
 *   INITSCALEMTOF  (int)(0.2*FRACUNIT)    = 13,107
 *   M_ZOOMIN       (int)(1.02*FRACUNIT)   = 66,846
 *   M_ZOOMOUT      (int)(FRACUNIT/1.02)   = 64,250
 *   entry divisor  (int)(0.7*FRACUNIT)    = 45,875
 * Each is written as an exact integer expression so no floating point reaches
 * this layer, and tools/doom_ref.py derives the same four numbers the same way.
 */
#define AM_INITSCALEMTOF (FRACUNIT * 2 / 10)
#define AM_M_ZOOMIN (FRACUNIT * 102 / 100)
#define AM_M_ZOOMOUT (FRACUNIT * 100 / 102)
#define AM_ENTRYSCALE (FRACUNIT * 7 / 10)

/* The largest x/y a clipped line reaches: AM_clipMline guarantees the range, so
 * this is the rasteriser's own assertion, not a clip. */
#define AM_OUT_OF_RANGE INT32_MIN

/* The map-to-frame-buffer transforms (am_map.h's header states them). */
#define AM_FTOM(state, x) fixed_mul((fixed_t)(x) << FRACBITS, (state)->scaleFtom)
#define AM_MTOF(state, x) (fixed_mul((x), (state)->scaleMtof) >> FRACBITS)
#define AM_CXMTOF(state, x) ((state)->fX + AM_MTOF(state, (x) - (state)->mX))
#define AM_CYMTOF(state, y) \
    ((state)->fY + ((state)->fH - AM_MTOF(state, (y) - (state)->mY)))

/*
 * The vector graphics of the player: a line drawing of the player pointing
 * right, starting from the middle (upstream's `player_arrow`, R =
 * (8*PLAYERRADIUS)/7, and `NUMPLYRLINES`).
 */
#define AM_ARROW_R ((8 * AM_PLAYERRADIUS) / 7)
static const am_mline_t kPlayerArrow[] = {
    {{-AM_ARROW_R + AM_ARROW_R / 8, 0}, {AM_ARROW_R, 0}},
    {{AM_ARROW_R, 0}, {AM_ARROW_R - AM_ARROW_R / 2, AM_ARROW_R / 4}},
    {{AM_ARROW_R, 0}, {AM_ARROW_R - AM_ARROW_R / 2, -AM_ARROW_R / 4}},
    {{-AM_ARROW_R + AM_ARROW_R / 8, 0}, {-AM_ARROW_R - AM_ARROW_R / 8, AM_ARROW_R / 4}},
    {{-AM_ARROW_R + AM_ARROW_R / 8, 0}, {-AM_ARROW_R - AM_ARROW_R / 8, -AM_ARROW_R / 4}},
    {{-AM_ARROW_R + 3 * AM_ARROW_R / 8, 0},
     {-AM_ARROW_R + AM_ARROW_R / 8, AM_ARROW_R / 4}},
    {{-AM_ARROW_R + 3 * AM_ARROW_R / 8, 0},
     {-AM_ARROW_R + AM_ARROW_R / 8, -AM_ARROW_R / 4}},
};
#undef AM_ARROW_R
#define AM_NUMPLYRLINES ((int)(sizeof kPlayerArrow / sizeof kPlayerArrow[0]))

/* --- Small helpers ------------------------------------------------------- */

/* The Bresenham rasteriser's write: window-relative, through the stride. */
static void fb_put(const am_state_t* state, int x, int y, uint8_t color) {
    state->fb[(size_t)(state->fY + y) * (size_t)state->stride + (size_t)state->fX + (size_t)x] =
        color;
}

/* --- Clipping and rasterising -------------------------------------------- */

/*
 * AM_clipMline(): Cohen-Sutherland with a faster reject and precalculated
 * slopes, transcribed. The two early tests are in *map* coordinates (against the
 * window's map-space rectangle); everything after the transform is in frame
 * buffer coordinates. The divisions cannot divide by zero: an endpoint is only
 * clipped against a side it is outside of, and the other endpoint is on the
 * other side of it (otherwise the outcodes' AND would have rejected the line).
 *
 * @return 1 and @p out filled when any part of @p line is visible.
 */
static int clip_mline(const am_state_t* state, const am_mline_t* line, am_fline_t* out) {
    enum { AM_LEFT = 1, AM_RIGHT = 2, AM_BOTTOM = 4, AM_TOP = 8 };
    int outcode1 = 0;
    int outcode2 = 0;
    int outside;
    am_fpoint_t tmp;

#define AM_DOOUTCODE(oc, mx, my)         \
    do {                                 \
        (oc) = 0;                        \
        if ((my) < 0) {                  \
            (oc) |= AM_TOP;              \
        } else if ((my) >= state->fH) {  \
            (oc) |= AM_BOTTOM;           \
        }                                \
        if ((mx) < 0) {                  \
            (oc) |= AM_LEFT;             \
        } else if ((mx) >= state->fW) {  \
            (oc) |= AM_RIGHT;            \
        }                                \
    } while (0)

    if (line->a.y > state->mY2) {
        outcode1 = AM_TOP;
    } else if (line->a.y < state->mY) {
        outcode1 = AM_BOTTOM;
    }
    if (line->b.y > state->mY2) {
        outcode2 = AM_TOP;
    } else if (line->b.y < state->mY) {
        outcode2 = AM_BOTTOM;
    }
    if (outcode1 & outcode2) {
        return 0;
    }
    if (line->a.x < state->mX) {
        outcode1 |= AM_LEFT;
    } else if (line->a.x > state->mX2) {
        outcode1 |= AM_RIGHT;
    }
    if (line->b.x < state->mX) {
        outcode2 |= AM_LEFT;
    } else if (line->b.x > state->mX2) {
        outcode2 |= AM_RIGHT;
    }
    if (outcode1 & outcode2) {
        return 0;
    }

    out->a.x = AM_CXMTOF(state, line->a.x);
    out->a.y = AM_CYMTOF(state, line->a.y);
    out->b.x = AM_CXMTOF(state, line->b.x);
    out->b.y = AM_CYMTOF(state, line->b.y);

    AM_DOOUTCODE(outcode1, out->a.x, out->a.y);
    AM_DOOUTCODE(outcode2, out->b.x, out->b.y);
    if (outcode1 & outcode2) {
        return 0;
    }

    while (outcode1 | outcode2) {
        int dx;
        int dy;

        /* May be partially inside the box: clip the outside point. */
        outside = outcode1 ? outcode1 : outcode2;
        if (outside & AM_TOP) {
            dy = out->a.y - out->b.y;
            dx = out->b.x - out->a.x;
            tmp.x = out->a.x + (dx * out->a.y) / dy;
            tmp.y = 0;
        } else if (outside & AM_BOTTOM) {
            dy = out->a.y - out->b.y;
            dx = out->b.x - out->a.x;
            tmp.x = out->a.x + (dx * (out->a.y - state->fH)) / dy;
            tmp.y = state->fH - 1;
        } else if (outside & AM_RIGHT) {
            dy = out->b.y - out->a.y;
            dx = out->b.x - out->a.x;
            tmp.y = out->a.y + (dy * (state->fW - 1 - out->a.x)) / dx;
            tmp.x = state->fW - 1;
        } else {
            dy = out->b.y - out->a.y;
            dx = out->b.x - out->a.x;
            tmp.y = out->a.y + (dy * (-out->a.x)) / dx;
            tmp.x = 0;
        }

        if (outside == outcode1) {
            out->a = tmp;
            AM_DOOUTCODE(outcode1, out->a.x, out->a.y);
        } else {
            out->b = tmp;
            AM_DOOUTCODE(outcode2, out->b.x, out->b.y);
        }
        if (outcode1 & outcode2) {
            return 0;
        }
    }
#undef AM_DOOUTCODE
    return 1;
}

/*
 * AM_drawFline(): classic Bresenham. Upstream writes through `PUTDOT(xx, yy)` =
 * `fb[yy*f_w + xx]`; here the same address with the surface's stride and the
 * window's origin.
 */
static void draw_fline(am_state_t* state, const am_fline_t* line, uint8_t color) {
    int x;
    int y;
    int dx;
    int dy;
    int sx;
    int sy;
    int ax;
    int ay;
    int d;

    /* Upstream's debug branch (see the file comment). */
    if (line->a.x < 0 || line->a.x >= state->fW || line->a.y < 0
        || line->a.y >= state->fH || line->b.x < 0 || line->b.x >= state->fW
        || line->b.y < 0 || line->b.y >= state->fH) {
        return;
    }

    dx = line->b.x - line->a.x;
    ax = 2 * (dx < 0 ? -dx : dx);
    sx = dx < 0 ? -1 : 1;

    dy = line->b.y - line->a.y;
    ay = 2 * (dy < 0 ? -dy : dy);
    sy = dy < 0 ? -1 : 1;

    x = line->a.x;
    y = line->a.y;

    if (ax > ay) {
        d = ay - ax / 2;
        for (;;) {
            fb_put(state, x, y, color);
            if (x == line->b.x) {
                return;
            }
            if (d >= 0) {
                y += sy;
                d -= ax;
            }
            x += sx;
            d += ay;
        }
    } else {
        d = ax - ay / 2;
        for (;;) {
            fb_put(state, x, y, color);
            if (y == line->b.y) {
                return;
            }
            if (d >= 0) {
                x += sx;
                d -= ay;
            }
            y += sy;
            d += ax;
        }
    }
}

/* AM_drawMline(): clip, then draw. Returns 1 when the line was drawn. */
static int draw_mline(am_state_t* state, const am_mline_t* line, uint8_t color) {
    am_fline_t clipped;

    if (!clip_mline(state, line, &clipped)) {
        return 0;
    }
    draw_fline(state, &clipped, color);
    return 1;
}

/* --- The passes ---------------------------------------------------------- */

/* AM_drawGrid(): the flat-aligned grid lines. Upstream's `% ` on a negative
 * dividend truncates toward zero, which is why the remainder is adjusted here
 * rather than taken with C's own `%` (the same value, spelled so the port and
 * the Python check, tools/am_check.py, cannot disagree about the sign). */
static void draw_grid(am_state_t* state, uint8_t color) {
    fixed_t start;
    fixed_t end;
    fixed_t remainder;
    fixed_t x;
    fixed_t y;
    am_mline_t line;

    start = state->mX;
    remainder = (start - state->bmapOrgX) % AM_MAPBLOCKUNITS;
    if (remainder != 0) {
        start += AM_MAPBLOCKUNITS - remainder;
    }
    end = state->mX + state->mW;

    line.a.y = state->mY;
    line.b.y = state->mY + state->mH;
    for (x = start; x < end; x += AM_MAPBLOCKUNITS) {
        line.a.x = x;
        line.b.x = x;
        (void)draw_mline(state, &line, color);
    }

    start = state->mY;
    remainder = (start - state->bmapOrgY) % AM_MAPBLOCKUNITS;
    if (remainder != 0) {
        start += AM_MAPBLOCKUNITS - remainder;
    }
    end = state->mY + state->mH;

    line.a.x = state->mX;
    line.b.x = state->mX + state->mW;
    for (y = start; y < end; y += AM_MAPBLOCKUNITS) {
        line.a.y = y;
        line.b.y = y;
        (void)draw_mline(state, &line, color);
    }
}

/* The sector of a linedef's side, or 0 when the side is absent (upstream would
 * dereference a NULL sidedef; a map that names a side its SIDEDEFS does not hold
 * is rejected at open, so this is the "-1 on side 0" guard and nothing else). */
static int line_sector(const am_state_t* state, uint16_t sideIndex, doom_sector_t* out) {
    doom_side_t side;

    if (sideIndex == DOOM_NO_SIDE_INDEX) {
        return 0;
    }
    doom_map_get_side(state->map, sideIndex, &side);
    doom_map_get_sector(state->map, side.sector, out);
    return 1;
}

/*
 * AM_drawWalls()' colour ladder for one mapped line, or -1 when upstream draws
 * nothing: a two-sided line whose front and back sectors have the same floor and
 * ceiling is not drawn (upstream's `else if (cheating)` is the only remaining
 * branch, and cheating does not exist here).
 */
static int wall_color(const am_state_t* state, const doom_line_t* line) {
    doom_sector_t front;
    doom_sector_t back;

    if (line->sideNum[1] == DOOM_NO_SIDE_INDEX) {
        return AM_WALLCOLORS + state->lightLev;
    }
    if (line->special == 39) {
        /* Teleporters: upstream's WALLCOLORS+WALLRANGE/2, and no light term. */
        return AM_WALLCOLORS + AM_WALLRANGE / 2;
    }
    if ((line->flags & AM_ML_SECRET) != 0) {
        /* A secret door looks like a wall: SECRETWALLCOLORS only when cheating. */
        return AM_WALLCOLORS + state->lightLev;
    }
    if (!line_sector(state, line->sideNum[0], &front)) {
        return AM_WALLCOLORS + state->lightLev;
    }
    if (!line_sector(state, line->sideNum[1], &back)) {
        return AM_WALLCOLORS + state->lightLev;
    }
    if (back.floorHeight != front.floorHeight) {
        return AM_FDWALLCOLORS + state->lightLev;
    }
    if (back.ceilingHeight != front.ceilingHeight) {
        return AM_CDWALLCOLORS + state->lightLev;
    }
    return -1;
}

/* AM_drawWalls(): every linedef whose mapped bit is set, in map order. */
static int draw_walls(am_state_t* state) {
    uint32_t index;
    int drawn = 0;

    for (index = 0; index < state->lineCount; ++index) {
        doom_line_t line;
        doom_vertex_t v1;
        doom_vertex_t v2;
        am_mline_t shape;
        int color;

        if (!am_mapped(state, index)) {
            if (!state->allMap) {
                continue;
            }
            /* The computer-map power: every line faintly, never a secret or a
             * line the renderer refuses to draw. */
            doom_map_get_line(state->map, (uint16_t)index, &line);
            if ((line.flags & AM_LINE_NEVERSEE) != 0) {
                continue;
            }
            doom_map_get_vertex(state->map, line.v1, &v1);
            doom_map_get_vertex(state->map, line.v2, &v2);
            shape.a.x = v1.x;
            shape.a.y = v1.y;
            shape.b.x = v2.x;
            shape.b.y = v2.y;
            if (draw_mline(state, &shape, (uint8_t)(AM_GRAYS + 3))) {
                ++drawn;
            }
            continue;
        }

        doom_map_get_line(state->map, (uint16_t)index, &line);
        if ((line.flags & AM_LINE_NEVERSEE) != 0) {
            continue;
        }
        color = wall_color(state, &line);
        if (color < 0) {
            continue;
        }
        doom_map_get_vertex(state->map, line.v1, &v1);
        doom_map_get_vertex(state->map, line.v2, &v2);
        shape.a.x = v1.x;
        shape.a.y = v1.y;
        shape.b.x = v2.x;
        shape.b.y = v2.y;
        if (draw_mline(state, &shape, (uint8_t)color)) {
            ++drawn;
        }
    }
    return drawn;
}

/* AM_rotate(): rotate one point by a binary angle, using the fine tables. */
static void rotate_point(fixed_t* x, fixed_t* y, angle_t angle) {
    const fixed_t tmpx = fixed_mul(*x, r_fine_cosine(angle))
                         - fixed_mul(*y, r_fine_sine[angle >> ANGLETOFINESHIFT]);

    *y = fixed_mul(*x, r_fine_sine[angle >> ANGLETOFINESHIFT])
         + fixed_mul(*y, r_fine_cosine(angle));
    *x = tmpx;
}

/* AM_drawLineCharacter(): one vector character, scaled, rotated, translated. */
static void draw_line_character(am_state_t* state, const am_mline_t* character, int lines,
                                fixed_t scale, angle_t angle, uint8_t color, fixed_t x,
                                fixed_t y) {
    int i;

    for (i = 0; i < lines; ++i) {
        am_mline_t line;

        line.a.x = character[i].a.x;
        line.a.y = character[i].a.y;
        if (scale != 0) {
            line.a.x = fixed_mul(scale, line.a.x);
            line.a.y = fixed_mul(scale, line.a.y);
        }
        if (angle != 0) {
            rotate_point(&line.a.x, &line.a.y, angle);
        }
        line.a.x += x;
        line.a.y += y;

        line.b.x = character[i].b.x;
        line.b.y = character[i].b.y;
        if (scale != 0) {
            line.b.x = fixed_mul(scale, line.b.x);
            line.b.y = fixed_mul(scale, line.b.y);
        }
        if (angle != 0) {
            rotate_point(&line.b.x, &line.b.y, angle);
        }
        line.b.x += x;
        line.b.y += y;

        (void)draw_mline(state, &line, color);
    }
}

/* AM_drawPlayers(): the single-player arrow, upstream's `!netgame` branch. */
static void draw_players(am_state_t* state) {
    draw_line_character(state, kPlayerArrow, AM_NUMPLYRLINES, 0, state->playerAngle,
                        (uint8_t)AM_WHITE, state->playerX, state->playerY);
}

/* AM_drawCrosshair(): one point, `fb[(f_w*(f_h+1))/2]` upstream. With the window
 * at (0,0) and a 320-pixel stride that index is column 160 of row 84, which is
 * the form written here so a padded stride stays inside the window. */
static void draw_crosshair(am_state_t* state, uint8_t color) {
    fb_put(state, state->fW / 2, (state->fH + 1) / 2, color);
}

/* --- The window ---------------------------------------------------------- */

/* AM_changeWindowLoc(): pan, cancel the follow, then clamp the window's centre
 * to the level's bounding box. */
static void change_window_loc(am_state_t* state) {
    if (state->panIncX != 0 || state->panIncY != 0) {
        state->followPlayer = 0;
        state->oldLoc.x = INT32_MAX;
    }

    state->mX += state->panIncX;
    state->mY += state->panIncY;

    if (state->mX + state->mW / 2 > state->maxX) {
        state->mX = state->maxX - state->mW / 2;
    } else if (state->mX + state->mW / 2 < state->minX) {
        state->mX = state->minX - state->mW / 2;
    }
    if (state->mY + state->mH / 2 > state->maxY) {
        state->mY = state->maxY - state->mH / 2;
    } else if (state->mY + state->mH / 2 < state->minY) {
        state->mY = state->minY - state->mH / 2;
    }

    state->mX2 = state->mX + state->mW;
    state->mY2 = state->mY + state->mH;
}

/* AM_activateNewScale(): keep the centre, take the new window size. */
static void activate_new_scale(am_state_t* state) {
    state->mX += state->mW / 2;
    state->mY += state->mH / 2;
    state->mW = AM_FTOM(state, state->fW);
    state->mH = AM_FTOM(state, state->fH);
    state->mX -= state->mW / 2;
    state->mY -= state->mH / 2;
    state->mX2 = state->mX + state->mW;
    state->mY2 = state->mY + state->mH;
}

/* AM_minOutWindowScale(): the window at the level's full extent. */
static void min_out_window_scale(am_state_t* state) {
    state->scaleMtof = state->minScaleMtof;
    state->scaleFtom = fixed_div(FRACUNIT, state->scaleMtof);
    activate_new_scale(state);
}

/* AM_maxOutWindowScale(): the window at two player radii. */
static void max_out_window_scale(am_state_t* state) {
    state->scaleMtof = state->maxScaleMtof;
    state->scaleFtom = fixed_div(FRACUNIT, state->scaleMtof);
    activate_new_scale(state);
}

/* AM_changeWindowScale(): one tic of zoom, clamped at both limits. */
static void change_window_scale(am_state_t* state) {
    state->scaleMtof = fixed_mul(state->scaleMtof, state->mtofZoomMul);
    state->scaleFtom = fixed_div(FRACUNIT, state->scaleMtof);

    if (state->scaleMtof < state->minScaleMtof) {
        min_out_window_scale(state);
    } else if (state->scaleMtof > state->maxScaleMtof) {
        max_out_window_scale(state);
    } else {
        activate_new_scale(state);
    }
}

/* AM_doFollowPlayer(): centre on the player when it moved. */
static void do_follow_player(am_state_t* state) {
    if (state->oldLoc.x != state->playerX || state->oldLoc.y != state->playerY) {
        state->mX = AM_FTOM(state, AM_MTOF(state, state->playerX)) - state->mW / 2;
        state->mY = AM_FTOM(state, AM_MTOF(state, state->playerY)) - state->mH / 2;
        state->mX2 = state->mX + state->mW;
        state->mY2 = state->mY + state->mH;
        state->oldLoc.x = state->playerX;
        state->oldLoc.y = state->playerY;
    }
}

/* --- Public API ---------------------------------------------------------- */

void am_clear_state(am_state_t* state) {
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof *state);
    state->stopped = 1;
    state->followPlayer = 1;
    state->panIncX = 0;
    state->panIncY = 0;
    state->mtofZoomMul = FRACUNIT;
    state->ftomZoomMul = FRACUNIT;
    state->scaleMtof = AM_INITSCALEMTOF;
    state->scaleFtom = FRACUNIT;
}

/* AM_findMinMaxBoundaries(): the vertex bounding box, then the two zoom limits,
 * with upstream's if/else-if form (a vertex cannot satisfy both comparisons). */
static void find_min_max_boundaries(am_state_t* state) {
    uint32_t index;
    fixed_t a;
    fixed_t b;

    state->minX = state->minY = INT32_MAX;
    state->maxX = state->maxY = -INT32_MAX;

    for (index = 0; index < state->map->lumps[DOOM_MAP_VERTEXES].count; ++index) {
        doom_vertex_t vertex;

        doom_map_get_vertex(state->map, (uint16_t)index, &vertex);
        if (vertex.x < state->minX) {
            state->minX = vertex.x;
        } else if (vertex.x > state->maxX) {
            state->maxX = vertex.x;
        }
        if (vertex.y < state->minY) {
            state->minY = vertex.y;
        } else if (vertex.y > state->maxY) {
            state->maxY = vertex.y;
        }
    }

    state->maxW = state->maxX - state->minX;
    state->maxH = state->maxY - state->minY;
    state->minW = 2 * AM_PLAYERRADIUS;
    state->minH = 2 * AM_PLAYERRADIUS;

    a = fixed_div((fixed_t)(state->fW << FRACBITS), state->maxW);
    b = fixed_div((fixed_t)(state->fH << FRACBITS), state->maxH);
    state->minScaleMtof = a < b ? a : b;
    state->maxScaleMtof = fixed_div((fixed_t)(state->fH << FRACBITS), 2 * AM_PLAYERRADIUS);
}

int am_level_init(am_state_t* state, const doom_map_t* map, uint8_t* mapped,
                  uint32_t mappedBytes) {
    const doom_map_lump_view_t* blockmap;

    if (state == NULL || map == NULL || map->open == 0) {
        return 0;
    }
    if (map->lumps[DOOM_MAP_LINEDEFS].count == 0u
        || map->lumps[DOOM_MAP_VERTEXES].count == 0u) {
        return 0;
    }

    state->map = map;
    state->mapped = mapped;
    state->mappedBytes = mappedBytes;
    state->lineCount = map->lumps[DOOM_MAP_LINEDEFS].count;
    state->active = 0;
    state->stopped = 1;

    /* f_x = f_y = 0; f_w = finit_width; f_h = finit_height. */
    state->fX = 0;
    state->fY = 0;
    state->fW = AM_WINDOW_WIDTH;
    state->fH = AM_WINDOW_HEIGHT;

    /* P_LoadBlockMap(): bmaporgx = SHORT(blockmap[0]) << FRACBITS. A map without
     * one grids from the world origin, which is upstream's zero-initialised
     * global. */
    state->bmapOrgX = 0;
    state->bmapOrgY = 0;
    blockmap = &map->lumps[DOOM_MAP_BLOCKMAP];
    if (blockmap->data != NULL && blockmap->size >= 4u) {
        const int16_t x = (int16_t)(uint16_t)((uint16_t)blockmap->data[0]
                                              | (uint16_t)((uint16_t)blockmap->data[1] << 8));
        const int16_t y = (int16_t)(uint16_t)((uint16_t)blockmap->data[2]
                                              | (uint16_t)((uint16_t)blockmap->data[3] << 8));
        state->bmapOrgX = (fixed_t)x << FRACBITS;
        state->bmapOrgY = (fixed_t)y << FRACBITS;
    }

    find_min_max_boundaries(state);

    state->scaleMtof = fixed_div(state->minScaleMtof, AM_ENTRYSCALE);
    if (state->scaleMtof > state->maxScaleMtof) {
        state->scaleMtof = state->minScaleMtof;
    }
    state->scaleFtom = fixed_div(FRACUNIT, state->scaleMtof);
    return 1;
}

int am_start(am_state_t* state, fixed_t x, fixed_t y) {
    if (state == NULL || state->map == NULL) {
        return 0;
    }

    state->active = 1;
    state->stopped = 0;
    state->playerX = x;
    state->playerY = y;

    state->oldLoc.x = INT32_MAX;
    state->amClock = 0;
    state->lightLev = 0;

    state->panIncX = 0;
    state->panIncY = 0;
    state->ftomZoomMul = FRACUNIT;
    state->mtofZoomMul = FRACUNIT;

    state->mW = AM_FTOM(state, state->fW);
    state->mH = AM_FTOM(state, state->fH);

    state->mX = state->playerX - state->mW / 2;
    state->mY = state->playerY - state->mH / 2;
    change_window_loc(state);

    /* For saving and restoring (upstream's old_m_*). */
    state->oldMX = state->mX;
    state->oldMY = state->mY;
    state->oldMW = state->mW;
    state->oldMH = state->mH;
    return 1;
}

void am_stop(am_state_t* state) {
    if (state == NULL) {
        return;
    }
    state->active = 0;
    state->stopped = 1;
}

void am_ticker(am_state_t* state, fixed_t x, fixed_t y, angle_t angle) {
    if (state == NULL) {
        return;
    }
    state->playerX = x;
    state->playerY = y;
    state->playerAngle = angle;
    if (!state->active) {
        return;
    }

    ++state->amClock;

    if (state->followPlayer) {
        do_follow_player(state);
    }
    if (state->ftomZoomMul != FRACUNIT) {
        change_window_scale(state);
    }
    if (state->panIncX != 0 || state->panIncY != 0) {
        change_window_loc(state);
    }

    /* Upstream 1.10's `// AM_updateLightLev();` is commented out: see the file
     * comment. The strobe stays off, which is the reference picture. */
}

void am_update_light_lev(am_state_t* state) {
    static const int kLiteLevels[] = {0, 4, 7, 10, 12, 14, 15, 15};
    const int count = (int)(sizeof kLiteLevels / sizeof kLiteLevels[0]);

    if (state == NULL) {
        return;
    }
    if (state->amClock > state->lightNextTic) {
        state->lightLev = kLiteLevels[state->lightCount];
        ++state->lightCount;
        if (state->lightCount == count) {
            state->lightCount = 0;
        }
        state->lightNextTic = state->amClock + 6 - (state->amClock % 6);
    }
}

int am_pan(am_state_t* state, uint8_t bits) {
    fixed_t step;

    if (state == NULL || !state->active) {
        return 0;
    }
    if (bits != AM_PAN_NONE) {
        /* The board has no 'f' key: a d-pad press is what stops the follow, and
         * the pan starts from the player's centred window (am_map.h). */
        state->followPlayer = 0;
    }
    state->panIncX = 0;
    state->panIncY = 0;
    step = AM_FTOM(state, AM_PAN_STEP);
    if ((bits & AM_PAN_RIGHT) != 0u) {
        state->panIncX = step;
    }
    if ((bits & AM_PAN_LEFT) != 0u) {
        state->panIncX = -step;
    }
    if ((bits & AM_PAN_UP) != 0u) {
        state->panIncY = step;
    }
    if ((bits & AM_PAN_DOWN) != 0u) {
        state->panIncY = -step;
    }
    return 1;
}

int am_zoom(am_state_t* state, int direction) {
    if (state == NULL || !state->active) {
        return 0;
    }
    if (direction == AM_ZOOM_IN) {
        state->mtofZoomMul = AM_M_ZOOMIN;
        state->ftomZoomMul = AM_M_ZOOMOUT;
    } else if (direction == AM_ZOOM_OUT) {
        state->mtofZoomMul = AM_M_ZOOMOUT;
        state->ftomZoomMul = AM_M_ZOOMIN;
    } else {
        state->mtofZoomMul = FRACUNIT;
        state->ftomZoomMul = FRACUNIT;
    }
    return 1;
}

int am_toggle_follow(am_state_t* state) {
    if (state == NULL || !state->active) {
        return 0;
    }
    state->followPlayer = !state->followPlayer;
    state->oldLoc.x = INT32_MAX;
    return 1;
}

int am_toggle_grid(am_state_t* state) {
    if (state == NULL || !state->active) {
        return 0;
    }
    state->grid = !state->grid;
    return 1;
}

int am_draw(am_state_t* state, uint8_t* surface, int32_t stride) {
    int row;

    if (state == NULL || surface == NULL || !state->active) {
        return 0;
    }
    if (stride < AM_WINDOW_WIDTH) {
        return 0;
    }

    state->fb = surface;
    state->stride = stride;

    /* AM_clearFB(BACKGROUND): the window's rows, and only those. */
    for (row = 0; row < state->fH; ++row) {
        memset(surface + (size_t)(state->fY + row) * (size_t)stride + (size_t)state->fX,
               AM_BACKGROUND, (size_t)state->fW);
    }

    if (state->grid) {
        draw_grid(state, (uint8_t)AM_GRIDCOLORS);
    }
    {
        const int drawn = draw_walls(state);
        draw_players(state);
        draw_crosshair(state, (uint8_t)AM_XHAIRCOLORS);
        /* V_MarkRect() is a renderer-side FLAT_SPAN emulation upstream; there is
         * no such surface here (see the file comment). */
        state->fb = NULL;
        state->stride = 0;
        return drawn;
    }
}

int am_active(const am_state_t* state) { return state != NULL && state->active; }

int am_follow(const am_state_t* state) { return state != NULL && state->followPlayer; }

int am_grid(const am_state_t* state) { return state != NULL && state->grid; }

fixed_t am_scale_mtof(const am_state_t* state) {
    return state != NULL ? state->scaleMtof : 0;
}

int am_mapped(const am_state_t* state, uint32_t line) {
    if (state == NULL || state->mapped == NULL) {
        return 0;
    }
    if (line >= state->lineCount || line / 8u >= state->mappedBytes) {
        return 0;
    }
    return (state->mapped[line / 8u] >> (line % 8u)) & 1u;
}
