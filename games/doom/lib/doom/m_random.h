/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * DOOM's random number generator: a 256-entry table and one index.
 *
 * WHY A TABLE AND NOT rand()
 *
 * P_Random() is the simulation's only source of randomness, and every consumer
 * of it -- a monster's pain chance, a shotgun's pellet spread, a nightmare
 * respawn -- is part of demo playback fidelity. The 1993 code does not generate
 * numbers: it reads them from a fixed table, advancing `prndindex` first and
 * reading after. Reproducing that exact sequence is the whole point; a
 * generator with the same distribution but a different order would desynchronise
 * a recorded demo on the first shot.
 *
 * The sequence is therefore *data*: `p_random()` bumps the index with
 * `(index + 1) & 0xff` and answers `table[index]`, so the first call answers
 * table[1] = 8, not table[0] = 0. Upstream's `M_Random()` keeps a second,
 * independent index for menu/effect pacing; this port has not needed it yet and
 * does not pretend to implement it.
 *
 * CALLER-OWNED, LIKE EVERYTHING ELSE HERE
 *
 * The index lives in a caller-owned m_random_t, not in a file-scope global, so
 * two worlds (the game and a test, or a future demo verifier replaying a demo
 * beside the live game) can each hold their own position in the sequence. There
 * is no state in this translation unit.
 *
 * This is C99 only: <stdint.h>, no allocation, no globals, no stdio, no
 * floating point.
 */
#ifndef DOOM_M_RANDOM_H
#define DOOM_M_RANDOM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Upstream's table is 256 bytes and the index is masked to it, which is why
 * there is no "seed" and no period to configure: the sequence is that long and
 * repeats exactly. */
#define M_RANDOM_TABLE_SIZE 256u

/*
 * One generator position. `index` is the entry the last call read; a fresh
 * state starts at 0, which is what upstream's M_ClearRandom() does (both
 * indices to zero) and therefore means the first P_Random() answers table[1].
 *
 * The field is public because the harness prints it: the number of calls a
 * level load consumed is half of the proof that the C loader and the oracle
 * consumed the same ones.
 */
typedef struct m_random {
    uint32_t index;
} m_random_t;

/**
 * @brief Sets @p random to the start of the sequence (index 0).
 *
 * A NULL pointer is a no-op: there is no state to reset and no error to report
 * that a caller could act on.
 */
void m_random_init(m_random_t* random);

/**
 * @brief Upstream's P_Random(): advances one step and answers 0..255.
 *
 * Pre-increment, then read: the first call after m_random_init() answers 8.
 * A NULL pointer answers 0 without touching anything, so a caller that has not
 * wired a generator (a unit test of a spawn path that does not care) gets a
 * deterministic number instead of a crash.
 *
 * @return A value in [0, 255].
 */
int32_t p_random(m_random_t* random);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_M_RANDOM_H */
