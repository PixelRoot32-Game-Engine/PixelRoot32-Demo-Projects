/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * The automap: upstream's am_map.c, drawn into the caller's surface.
 *
 * WHAT THIS IS
 *
 * Upstream keeps the automap as a dozen file-scope statics -- the window, the
 * scale, the pan rate, the zoom multiplier, the light level -- and draws it into
 * `screens[0]` through `V_DrawPatch`-free vector primitives: clipped lines, a
 * Bresenham rasteriser, a player arrow, a crosshair and a block-aligned grid.
 * This port keeps every one of those numbers in one caller-owned `am_state_t`
 * (this library has no mutable file scope, the same rule r_bsp.c and r_segs.c
 * follow) and takes the frame buffer as an argument instead of a global.
 *
 * THE WINDOW IS THE VIEW ROWS
 *
 * Upstream's `finit_height = SCREENHEIGHT - 32` is the 168 rows the status bar
 * does not own, and this port keeps exactly that: the automap fills rows 0..167
 * of the 320-wide surface `ST_Drawer` leaves to the world, and `st_stuff.c` still
 * paints rows 168..199. The width is `finit_width = SCREENWIDTH` = 320. The
 * surface's stride is a parameter, so a caller with padding between rows gets
 * the same picture; the map never writes past column 319 or row 167.
 *
 * THE MAPPED BIT IS A SHADOW BITMAP
 *
 * Upstream sets `linedef->flags |= ML_MAPPED` in R_StoreWallRange and the
 * automap draws a line only when that bit is set. This port's LINEDEFS lump is
 * immutable (on the board it is a mapped container page), so the bit cannot live
 * in the record: the session owns a `uint8_t` bitmap with one bit per linedef
 * (`(numlines + 7) / 8` bytes, 60 for E1M1 and 130 for E1M2), the wall pass sets
 * it for every wall range it stores (r_segs.c), and this module reads it. A NULL
 * bitmap means "nothing is mapped yet", which is the state before the first
 * frame and the one a caller that does not care passes.
 *
 * WHAT IS DELIBERATELY NOT PORTED, AND WHY
 *
 *   marks       `AM_NUMMARKPOINTS`, `markpoints[]`, the `AMMNUM*` art and the
 *               `m` / `c` keys. The board has no keyboard, the art is not in the
 *               container, and a mark nobody can place is 80 bytes of state and
 *               a patch lookup per frame.
 *   things      `AM_drawThings()` and its `IDDT` cheat sequence. No cheats.
 *   followers   the deathmatch/netgame branch of `AM_drawPlayers()`. This port
 *               is one player on one board; the single-player arrow is the whole
 *               of it, and `cheat_player_arrow` goes with the cheat sequence.
 *   the keys    `AM_Responder()`'s `event_t` switch. The board has buttons, not
 *               keys, so the seam is am_pan() / am_zoom() / am_toggle_grid() /
 *               am_toggle_follow(): the same state changes `AM_Responder` makes,
 *               called once per frame with the held buttons.
 *   the strobe  `AM_updateLightLev()` is ported and exported, but `AM_Ticker()`
 *               does NOT call it, because upstream 1.10's own call is commented
 *               out (`// AM_updateLightLev();`, am_map.c). `lightlev` therefore
 *               stays 0 for the life of the level and every wall colour is the
 *               base one -- exactly the picture the reference produces. A build
 *               that wants the strobe calls am_update_light_lev() from its tic
 *               loop; nothing else changes.
 *   ST_Responder  upstream tells the status bar that the automap came and went
 *               (`ST_MSGENTERED`). This port's bar has no message area and no
 *               responder, so there is nothing to tell.
 *
 * COORDINATE SPACES, AS UPSTREAM NAMES THEM
 *
 *   map    16.16 world units, the map lumps' own coordinates.
 *   fb     frame-buffer pixels, column 0 at the left edge of row 0.
 *
 *   MTOF(x)    map distance -> fb distance, `FixedMul(x, scale_mtof) >> 16`
 *   FTOM(x)    fb distance  -> map distance, `FixedMul(x << 16, scale_ftom)`
 *   CXMTOF(x)  f_x + MTOF(x - m_x)      CYMTOF(y)  f_y + f_h - MTOF(y - m_y)
 *
 * The y axis flips here and nowhere else: DOOM's world y grows north, the
 * frame buffer's grows down.
 *
 * This is C99 only: no allocation, no globals, no stdio, no floating point.
 */
#ifndef DOOM_AM_MAP_H
#define DOOM_AM_MAP_H

#include <stdint.h>

#include "doom_map.h"
#include "r_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The window on the surface: `finit_width` and `finit_height` upstream, which
 * DOOM derives from the status bar rather than from a build flag. */
#define AM_WINDOW_WIDTH (R_SCREEN_WIDTH)
#define AM_WINDOW_HEIGHT (R_SCREEN_HEIGHT - 32)

/* How much one tic of the d-pad moves the window, in frame-buffer pixels
 * (upstream's F_PANINC: 140 pixels a second). */
#define AM_PAN_STEP 4

/* The pan bits am_pan() takes: the engine's d-pad order, restated here because
 * this is C (the C++ side static_asserts that the two agree). */
#define AM_PAN_UP 0x01u
#define AM_PAN_DOWN 0x02u
#define AM_PAN_LEFT 0x04u
#define AM_PAN_RIGHT 0x08u
#define AM_PAN_NONE 0x00u

/* The two zoom directions am_zoom() takes: upstream's '=' and '-'. */
#define AM_ZOOM_RELEASE 0
#define AM_ZOOM_IN 1
#define AM_ZOOM_OUT -1

/* --- The geometry types upstream's helpers use ---------------------------- */

/* A point in frame-buffer pixels. */
typedef struct am_fpoint {
    int x;
    int y;
} am_fpoint_t;

/* A line in frame-buffer pixels: what am_clip_mline() produces. */
typedef struct am_fline {
    am_fpoint_t a;
    am_fpoint_t b;
} am_fline_t;

/* A point in 16.16 map coordinates. */
typedef struct am_mpoint {
    fixed_t x;
    fixed_t y;
} am_mpoint_t;

/* A line in 16.16 map coordinates: what every drawing pass hands the clipper. */
typedef struct am_mline {
    am_mpoint_t a;
    am_mpoint_t b;
} am_mline_t;

/* --- The state ------------------------------------------------------------ */

/*
 * One automap. The caller owns it and it must outlive the level; upstream's
 * file-scope statics map onto the fields one for one, in upstream's order, so a
 * reader holding am_map.c open can find each one.
 *
 * The level fields are written by am_level_init(), the view fields by the calls
 * below, and the drawing fields by am_draw(). Nothing here is touched outside
 * those paths, and nothing is allocated: the mapped bitmap and the surface are
 * the caller's.
 */
typedef struct am_state {
    /* --- The level -------------------------------------------------------- */
    const doom_map_t* map;   /* the open map this automap describes, or NULL */
    uint8_t* mapped;         /* the shadow bitmap: one bit per linedef, LSB first */
    uint32_t mappedBytes;    /* its size in bytes; a line past it reads 0 */
    uint32_t lineCount;      /* numlines */
    fixed_t bmapOrgX;        /* bmaporgx: the BLOCKMAP's origin, for the grid */
    fixed_t bmapOrgY;
    fixed_t minX, minY;      /* the vertex bounding box (AM_findMinMaxBoundaries) */
    fixed_t maxX, maxY;
    fixed_t maxW, maxH;      /* max_x-min_x, max_y-min_y */
    fixed_t minW, minH;      /* 2*PLAYERRADIUS: the smallest window, unzoomed */
    fixed_t minScaleMtof;    /* the zoom-out limit: the whole level fits */
    fixed_t maxScaleMtof;    /* the zoom-in limit: two player radii fill f_h */

    /* --- The window ------------------------------------------------------- */
    fixed_t mX, mY;          /* LL corner of the window, map coords */
    fixed_t mX2, mY2;        /* UR corner */
    fixed_t mW, mH;          /* its size, map coords */
    fixed_t scaleMtof;       /* map -> fb, 16.16 */
    fixed_t scaleFtom;       /* fb -> map, 16.16 (== 1/scaleMtof) */
    fixed_t oldMW, oldMH;    /* AM_saveScaleAndLoc: for AM_restoreScaleAndLoc */
    fixed_t oldMX, oldMY;
    am_mpoint_t oldLoc;      /* f_oldloc: the last followed position */
    fixed_t panIncX, panIncY;   /* m_paninc: this tic's pan, map coords */
    fixed_t mtofZoomMul;     /* mtof_zoommul */
    fixed_t ftomZoomMul;     /* ftom_zoommul */
    int followPlayer;        /* followplayer */
    int grid;                /* grid */
    int lightLev;            /* lightlev; 0 unless a caller strobes */
    int lightNextTic;        /* AM_updateLightLev's static `nexttic` */
    int lightCount;          /* AM_updateLightLev's static `litelevelscnt` */
    int amClock;             /* amclock, the tic counter the strobe reads */
    int active;              /* automapactive */
    int stopped;             /* upstream's `stopped` */

    /* --- The player the arrow is drawn at --------------------------------- */
    fixed_t playerX;
    fixed_t playerY;
    angle_t playerAngle;
    int allMap;              /* pw_allmap: draw every line faintly when set */

    /* --- The frame, valid inside am_draw() -------------------------------- */
    uint8_t* fb;
    int32_t stride;
    int fX, fY;              /* the window's origin on the surface (0, 0) */
    int fW, fH;              /* its size in pixels (320 x 168) */
} am_state_t;

/* --- Lifecycle ------------------------------------------------------------ */

/**
 * @brief Zeroes the state and marks it stopped. Safe with a NULL pointer.
 *
 * A zeroed state draws nothing; am_level_init() is what makes it usable.
 */
void am_clear_state(am_state_t* state);

/**
 * @brief AM_LevelInit(): the level's bounds, zoom limits and entry scale.
 *
 * Reads every vertex once to find the bounding box the zoom-out limit is
 * computed from, and the BLOCKMAP header for the grid's origin (P_LoadBlockMap's
 * `bmaporgx = SHORT(blockmap[0]) << 16`), then sets `scale_mtof` to upstream's
 * entry scale:
 *
 *     scale_mtof = FixedDiv(min_scale_mtof, (int)(0.7 * FRACUNIT));
 *     if (scale_mtof > max_scale_mtof) scale_mtof = min_scale_mtof;
 *
 * The 0.7 is written as the integer 45,875 upstream's double expression
 * truncates to, because this layer has no floating point.
 *
 * @param state       A state; its view fields are left as am_clear_state() left
 *                    them.
 * @param map         An open map (doom_map_open() accepted it).
 * @param mapped      The shadow bitmap, or NULL for "nothing mapped".
 * @param mappedBytes Its size in bytes: `(numlines + 7) / 8`.
 * @return 1 on success, 0 when @p state or @p map is NULL, the map is not open,
 *         or it has no linedefs or vertexes. A rejected call leaves the state
 *         stopped and inactive.
 */
int am_level_init(am_state_t* state, const doom_map_t* map, uint8_t* mapped,
                  uint32_t mappedBytes);

/**
 * @brief AM_Start(): centres on the player and opens the map.
 *
 * AM_initVariables(): the window size from the entry scale, the pan and zoom
 * multipliers reset, the clock and the light reset, and `m_x/m_y` centred on
 * @p x / @p y with AM_changeWindowLoc()'s edge clamp. `followplayer` is NOT
 * reset: upstream's static keeps it, so closing and reopening the map keeps the
 * pan the player made. Upstream's "new level" branch (AM_LevelInit when the
 * game map changed) is the caller's: the session calls am_level_init() once per
 * level load.
 *
 * @param state A state that am_level_init() accepted.
 * @param x     The player's position, 16.16 map units.
 * @param y     The same.
 * @return 1 when the map is now active.
 */
int am_start(am_state_t* state, fixed_t x, fixed_t y);

/**
 * @brief AM_Stop(): the map goes down.
 *
 * `automapactive` clears and the state is stopped. Upstream also unloads the
 * mark pictures and tells the status bar; neither exists here (see the file
 * comment). The window, the scale and the follow flag are kept, as upstream
 * keeps them, so the next am_start() resumes where the last one left off.
 */
void am_stop(am_state_t* state);

/* --- The tic ------------------------------------------------------------- */

/**
 * @brief AM_Ticker(): one 35 Hz tic of the map, or nothing when it is down.
 *
 * Follows the player (AM_doFollowPlayer), applies a zoom in progress
 * (AM_changeWindowScale) and applies a pan in progress (AM_changeWindowLoc),
 * in upstream's order. The player's position is a parameter instead of
 * `plr->mo`, so this layer needs no mobj.
 *
 * It does not call AM_updateLightLev(): upstream's own call is commented out.
 * See the file comment.
 *
 * @param state A state.
 * @param x     The player's position this tic, 16.16 map units.
 * @param y     The same.
 * @param angle The player's facing this tic. The arrow reads the state's copy,
 *              which this call updates.
 */
void am_ticker(am_state_t* state, fixed_t x, fixed_t y, angle_t angle);

/**
 * @brief AM_updateLightLev(): the wall-light strobe, one step per six tics.
 *
 * Ported, exported and NOT called from am_ticker() -- upstream 1.10's call is
 * commented out, so the reference picture has `lightlev` at 0 for the whole
 * level. A caller that wants the strobe calls this once per tic after
 * am_ticker(); no other function changes.
 */
void am_update_light_lev(am_state_t* state);

/* --- The buttons --------------------------------------------------------- */

/**
 * @brief The four arrow keys, as one call per frame: sets the pan rate.
 *
 * Upstream's AM_Responder keydown/keyup pair, collapsed: the bits held this
 * frame decide `m_paninc`, and a released axis is zero. AM_PAN_NONE stops both.
 *
 * DIVERGENCE, DECIDED BY THE BOARD: upstream pans only when `followplayer` is
 * already 0 (its arrow keys turn the player while following, and 'f' is what
 * stops the follow). The board has no 'f', so a d-pad press while following
 * clears the follow and starts the pan in the same frame -- which is the state
 * AM_changeWindowLoc()'s own `if (m_paninc.x || m_paninc.y) followplayer = 0`
 * expects. The window that pans is therefore centred on the player (follow kept
 * it there) and AM_changeWindowLoc()'s clamp stops it at the level's edges,
 * exactly as upstream's pan does.
 *
 * @param state A state.
 * @param bits  AM_PAN_UP/DOWN/LEFT/RIGHT ORed, or AM_PAN_NONE.
 * @return 1 when the map is active and the bits were applied, 0 otherwise (a
 *         stopped map ignores them, as upstream's inactive automap does).
 */
int am_pan(am_state_t* state, uint8_t bits);

/**
 * @brief The '=' / '-' keys: one zoom step per tic while held.
 *
 * AM_ZOOM_IN multiplies the scale by 1.02 a tic (twice in a second), AM_ZOOM_OUT
 * divides it by 1.02, and AM_ZOOM_RELEASE restores both multipliers to FRACUNIT,
 * exactly upstream's keydown/keyup. The clamp to [min_scale_mtof,
 * max_scale_mtof] is applied by the tic.
 *
 * @return 1 when the map is active, 0 otherwise.
 */
int am_zoom(am_state_t* state, int direction);

/* The 'f' and 'g' keys. Both return 1 when the map is active. */
int am_toggle_follow(am_state_t* state);
int am_toggle_grid(am_state_t* state);

/* --- Drawing ------------------------------------------------------------- */

/**
 * @brief AM_Drawer(): paints the map into rows 0..167 of @p surface.
 *
 * AM_clearFB(BACKGROUND), then the grid when it is on, the walls
 * (AM_drawWalls), the player arrow (AM_drawPlayers) and the crosshair
 * (AM_drawCrosshair), in upstream's order. A stopped map paints nothing and
 * returns 0 without touching a byte.
 *
 * The caller guarantees @p surface has at least AM_WINDOW_HEIGHT rows and
 * @p stride bytes between rows, and that column 0 of each row is the window's
 * left edge (upstream's f_x = f_y = 0; this port keeps the fields, and
 * am_level_init() sets them). Rows 168..199 and every byte past column 319 of
 * the window's rows belong to the caller: the map writes neither.
 *
 * @param state   A state.
 * @param surface The 8bpp surface, DOOM palette indices.
 * @param stride  Bytes between rows (>= AM_WINDOW_WIDTH).
 * @return The number of lines drawn (a wall line counts once), or 0 when the map
 *         is down or the arguments are unusable.
 */
int am_draw(am_state_t* state, uint8_t* surface, int32_t stride);

/* --- The answers the caller reports -------------------------------------- */

/* 1 while the map is up. */
int am_active(const am_state_t* state);

/* 1 when the window follows the player. */
int am_follow(const am_state_t* state);

/* 1 when the grid is on. */
int am_grid(const am_state_t* state);

/* `scale_mtof`, 16.16: the zoom, in the driver's own unit. */
fixed_t am_scale_mtof(const am_state_t* state);

/* 1 when the shadow bitmap marks @p line. Out-of-range lines, a NULL bitmap and
 * a bitmap too small for the line all answer 0. */
int am_mapped(const am_state_t* state, uint32_t line);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_AM_MAP_H */
