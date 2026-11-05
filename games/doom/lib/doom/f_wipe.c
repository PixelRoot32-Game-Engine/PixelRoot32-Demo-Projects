/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * Vanilla's screen melt, written as a pure composite over caller-owned planes.
 *
 * The header states the contract and why the shape differs from upstream. This
 * file holds only the arithmetic: the initial random walk of the column fronts,
 * the per-tick advance, and the full repaint that makes the result independent
 * of what the destination held before the call.
 */
#include "f_wipe.h"

#include <stddef.h>

void wipe_Init(wipe_state_t *state) {
    int index;

    if (state == NULL) {
        return;
    }
    state->start = NULL;
    state->end = NULL;
    state->width = 0;
    state->height = 0;
    state->columns = 0;
    state->active = 0;
    for (index = 0; index < WIPE_MAX_COLUMNS; ++index) {
        state->y[index] = 0;
    }
    m_random_init(&state->random);
}

int wipe_StartScreen(wipe_state_t *state, const uint8_t *start, int width, int height) {
    int index;

    if (state == NULL || start == NULL) {
        return -1;
    }
    if (width <= 0 || height <= 0 || width > WIPE_SCREEN_WIDTH
        || height > WIPE_SCREEN_HEIGHT || (width & 1) != 0) {
        return -1;
    }

    state->start = start;
    state->end = NULL;
    state->width = width;
    state->height = height;
    state->columns = width / 2;
    state->active = 1;

    /* Upstream's wipe_initMelt(): one random walk, clamped to [-15, 0]. A
     * column that is still negative holds the old screen at the top edge, which
     * is the delay that makes the melt edge uneven instead of a straight line. */
    state->y[0] = -(int)(m_random(&state->random) % 16);
    for (index = 1; index < state->columns; ++index) {
        const int drift = (int)(m_random(&state->random) % 3) - 1;
        int value = state->y[index - 1] + drift;
        if (value > 0) {
            value = 0;
        } else if (value == -16) {
            value = -15;
        }
        state->y[index] = value;
    }
    return 0;
}

int wipe_EndScreen(wipe_state_t *state, const uint8_t *end) {
    if (state == NULL || end == NULL || !state->active) {
        return -1;
    }
    state->end = end;
    return 0;
}

int wipe_DoMelt(wipe_state_t *state, uint8_t *dest, int ticks) {
    int column;
    int row;
    int done;

    if (state == NULL || dest == NULL || !state->active || state->start == NULL
        || state->end == NULL) {
        return -1;
    }
    if (ticks < 0) {
        ticks = 0;
    }

    /* Upstream's wipe_doMelt(): advance the fronts, accelerating out of the top
     * edge and then moving eight rows a tick. */
    while (ticks-- > 0) {
        for (column = 0; column < state->columns; ++column) {
            if (state->y[column] < 0) {
                state->y[column] += 1;
            } else if (state->y[column] < state->height) {
                int step = (state->y[column] < 16) ? state->y[column] + 1 : 8;
                if (state->y[column] + step >= state->height) {
                    step = state->height - state->y[column];
                }
                state->y[column] += step;
            }
        }
    }

    /* Repaint every column whole. Rows above the front are the new screen; rows
     * at and below it are the old one. This is why the destination may be the
     * caller's regenerated new-frame plane: each byte is written once per call
     * and no later row reads a byte this loop overwrote. */
    for (column = 0; column < state->columns; ++column) {
        const int front = state->y[column];
        const size_t x = (size_t)column * 2u;
        for (row = 0; row < state->height; ++row) {
            const uint8_t *source = (row < front) ? state->end : state->start;
            const size_t at = (size_t)row * (size_t)state->width + x;
            dest[at] = source[at];
            dest[at + 1u] = source[at + 1u];
        }
    }

    done = 1;
    for (column = 0; column < state->columns; ++column) {
        if (state->y[column] < state->height) {
            done = 0;
            break;
        }
    }
    if (done) {
        /* Same lifecycle as upstream's exitMelt(): the wipe is over and the
         * next transition starts with a fresh wipe_StartScreen(). */
        state->active = 0;
    }
    return done;
}

void wipe_Shutdown(wipe_state_t *state) {
    if (state == NULL) {
        return;
    }
    state->start = NULL;
    state->end = NULL;
    state->active = 0;
}
