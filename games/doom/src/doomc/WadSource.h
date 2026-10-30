/*
 * The device side of the WAD reader: a read-only memory mapping of the `wad`
 * flash partition, opened through the same C reader a host test uses.
 *
 * The partition table this targets is the one D0.5 measured and wrote down in
 * games/doom/bench/partitions_wad.csv: one `data` partition labelled `wad`,
 * subtype 0x40, after the app slot. This translation unit does not create that
 * table; the environment that ships the WAD is the one that selects it. When
 * there is no such partition -- the default table, a host build, a board
 * flashed without the data image -- open() reports the condition and returns a
 * distinct code. It never returns a reader over invalid memory.
 *
 * The mapping is read-only and lives until doom_wad_source_close(). The reader
 * state is owned by the caller, exactly as w_wad.h requires, so two callers can
 * hold their own view of the same mapping if a later task needs that.
 *
 * Everything here is C-callable: the header is valid C and the two entry points
 * are `extern "C"`, so the DOOM core can reach the flash mapping without
 * knowing that this side is C++.
 */
#ifndef DOOM_WAD_SOURCE_H
#define DOOM_WAD_SOURCE_H

#include <stdint.h>

#include "w_wad.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Error codes --------------------------------------------------------- */

/*
 * One code per failure class, in the same spirit as wad_error_t:
 *
 *   DOOM_WAD_SOURCE_OK                    the mapping is live and the reader
 *                                         accepted it
 *   DOOM_WAD_SOURCE_ERR_STATE_NULL        open() was called without a state
 *   DOOM_WAD_SOURCE_ERR_ALREADY_OPEN      a mapping is already live; close first
 *   DOOM_WAD_SOURCE_ERR_PARTITION_MISSING no `wad` partition in this image
 *   DOOM_WAD_SOURCE_ERR_MMAP_FAILED       the partition exists but did not map
 *   DOOM_WAD_SOURCE_ERR_WAD_INVALID       the mapping is not a valid WAD; the
 *                                         mapping was released again
 *   DOOM_WAD_SOURCE_ERR_UNAVAILABLE       this build has no flash mapping (a
 *                                         host build compiled this file)
 */
#define DOOM_WAD_SOURCE_OK 0
#define DOOM_WAD_SOURCE_ERR_STATE_NULL 1
#define DOOM_WAD_SOURCE_ERR_ALREADY_OPEN 2
#define DOOM_WAD_SOURCE_ERR_PARTITION_MISSING 3
#define DOOM_WAD_SOURCE_ERR_MMAP_FAILED 4
#define DOOM_WAD_SOURCE_ERR_WAD_INVALID 5
#define DOOM_WAD_SOURCE_ERR_UNAVAILABLE 6

/* --- Public API ---------------------------------------------------------- */

/**
 * @brief Maps the `wad` partition read-only and opens @p state over it.
 *
 * On success the mapping stays alive until doom_wad_source_close(). Every
 * failure is logged as one `WAD,...` line through the port's console, except
 * on a host build, where there is no partition to report.
 *
 * @param state Caller-owned reader state, rewritten by wad_open().
 * @return DOOM_WAD_SOURCE_OK or one of the codes above. On every failure the
 *         mapping is released and @p state is left closed.
 */
int doom_wad_source_open(wad_state_t* state);

/**
 * @brief Releases the mapping taken by doom_wad_source_open().
 *
 * Idempotent: calling it when nothing is mapped does nothing.
 *
 * A reader state opened over this mapping must not be used afterwards: its
 * base pointer now refers to unmapped flash. The reader owns no resource of its
 * own, so there is nothing else to release; call doom_wad_source_open() again
 * (which rewrites @c state) before reading.
 */
void doom_wad_source_close(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_WAD_SOURCE_H */
