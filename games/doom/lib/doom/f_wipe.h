/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * Vanilla's screen melt (f_wipe.c), caller-owned and allocation-free.
 *
 * WHY THIS SHAPE
 *
 * Upstream keeps `screens[2]` / `screens[3]` as zone-allocated copies of the two
 * frames and a file-scope `int* y` of column positions. This port already owns
 * both frames: under DOOM_FULL_PROFILE the scene holds two 320x200 index planes
 * and the old level's last frame must survive the transition. Copying them into
 * a third and fourth 64,000 B buffer would spend 128,000 B to reproduce a
 * picture the caller already has, and a file-scope `y` would make two melts
 * (the game and a host test) share one position.
 *
 * So the screens are borrowed pointers and the column positions live in the
 * caller's wipe_state_t. The composite is written into a destination the caller
 * provides, which lets the scene composite in place: every melt frame renders
 * the new level into the same plane and this module overwrites it with the
 * two-screen picture before the RGB565 expansion reads it. The old plane is
 * only ever read.
 *
 * WHAT IS KEPT FROM UPSTREAM
 *
 *  - the melt front of each column starts at a small random negative offset
 *    (a random walk in [-15, 0]) so the screen drips in unevenly;
 *  - a column advances `ticks` times per call, one tick = one row step (the
 *    front accelerates for its first 16 rows, then moves 8 rows a tick);
 *  - once every column reaches the bottom the composite is the end screen
 *    pixel for pixel, deterministically.
 *
 * Columns are two pixels wide, as upstream's `short` column-major transform
 * makes them (`width /= 2`): the two adjacent bytes move together.
 */
#ifndef DOOM_F_WIPE_H
#define DOOM_F_WIPE_H

#include <stdint.h>

#include "m_random.h"

#ifdef __cplusplus
extern "C" {
#endif

/// The port's logical frame width. Upstream's SCREENWIDTH.
#define WIPE_SCREEN_WIDTH 320

/// The port's logical frame height, status bar included.
#define WIPE_SCREEN_HEIGHT 200

/// Melt columns in the port's frame: upstream moves two pixels at a time.
#define WIPE_MAX_COLUMNS (WIPE_SCREEN_WIDTH / 2)

/**
 * @brief One melt's column state.
 *
 * Owned by the caller and reused across transitions, so a second level change
 * costs no allocation. `random` is the M_Random() position the initial offsets
 * are drawn from; like upstream's global, it continues across melts, so two
 * transitions do not drip in exactly the same pattern.
 */
typedef struct wipe_state_s {
    /// The old frame: read on every call, never written. Not owned.
    const uint8_t *start;
    /// The new frame: read on every call; the destination may alias it.
    const uint8_t *end;
    /// Frame width in pixels; must be even and at most WIPE_SCREEN_WIDTH.
    int width;
    /// Frame height in rows; at most WIPE_SCREEN_HEIGHT.
    int height;
    /// `width / 2`: the number of two-pixel melt columns actually used.
    int columns;
    /// Non-zero between wipe_StartScreen() and the call that reports done.
    int active;
    /// Current front of each column in rows; negative while the column waits.
    int y[WIPE_MAX_COLUMNS];
    /// Upstream's M_Random(), owned here so no transition shares a position.
    m_random_t random;
} wipe_state_t;

/**
 * @brief Prepares an empty melt state (upstream's wipe_Init()).
 *
 * Clears the borrowed screens and marks the state idle, so a construction-time
 * call is enough before the first transition. The random position starts at the
 * table's beginning, as M_ClearRandom() leaves it.
 *
 * @param state State to initialise; a NULL pointer is a no-op.
 */
void wipe_Init(wipe_state_t *state);

/**
 * @brief Begins a melt from @p start and draws the per-column start offsets.
 *
 * Upstream's wipe_StartScreen() copies the screen into `screens[2]`; this port
 * only borrows it, so the caller keeps the old frame intact and no 64,000 B
 * copy is made. The offsets are the vanilla random walk: column 0 starts in
 * [-15, 0] and each following column drifts by -1, 0 or +1, clamped to the same
 * range, which is what makes the melt edge uneven.
 *
 * @param state  State to start; must have been through wipe_Init().
 * @param start  The old frame, width * height bytes, row-major.
 * @param width  Frame width in pixels; even and <= WIPE_SCREEN_WIDTH.
 * @param height Frame height in rows; <= WIPE_SCREEN_HEIGHT.
 * @return 0 on success, -1 for a NULL argument, an unsupported geometry or an
 *         odd width.
 */
int wipe_StartScreen(wipe_state_t *state, const uint8_t *start, int width, int height);

/**
 * @brief Records @p end as the new frame for the running melt.
 *
 * Upstream's wipe_EndScreen() also restores the start screen to the display;
 * this port draws nothing (it has no engine), so the caller's next composition
 * shows the old frame until a front moves. The pointer is borrowed, not copied.
 *
 * @param state State started by wipe_StartScreen().
 * @param end   The new frame, width * height bytes, row-major.
 * @return 0 on success, -1 for a NULL argument or a melt that is not active.
 */
int wipe_EndScreen(wipe_state_t *state, const uint8_t *end);

/**
 * @brief Advances every column by @p ticks and writes the composite into @p dest.
 *
 * One tick moves a waiting column one row closer to the screen and a melting
 * column by its current step (y+1 for the first 16 rows, then 8), clamped so a
 * column lands exactly on the bottom. After the advance, each column is painted
 * whole: rows above its front come from @p end, rows at and below it from
 * @p start. Painting every row (rather than only the newly exposed ones) is
 * what lets the destination be the caller's freshly rendered new-frame plane:
 * the composite is a pure function of the column fronts and the two sources.
 *
 * @param state State started by wipe_StartScreen() and armed by wipe_EndScreen().
 * @param dest  Destination, width * height bytes. Must not alias `start`; it
 *              may alias `end` when the caller regenerates `end` before the next
 *              call, which is exactly what the scene's frozen plane does.
 * @param ticks Ticks to advance this call; 0 draws the current composite.
 * @return 1 when every column has reached the bottom (the composite is then
 *         `end` byte for byte), 0 while the melt runs, -1 for an invalid state.
 */
int wipe_DoMelt(wipe_state_t *state, uint8_t *dest, int ticks);

/**
 * @brief Releases a melt (upstream's wipe_Shutdown()).
 *
 * There is no allocation to free: the equivalent is clearing the borrowed
 * pointers and the active flag so a stale pointer cannot be read, while leaving
 * the random position where the next transition continues from.
 *
 * @param state State to release; a NULL pointer is a no-op.
 */
void wipe_Shutdown(wipe_state_t *state);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_F_WIPE_H */
