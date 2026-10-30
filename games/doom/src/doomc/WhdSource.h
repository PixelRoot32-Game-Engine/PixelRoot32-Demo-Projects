/*
 * The device side of the compressed container: a read-only memory mapping of
 * the `wad` flash partition, opened through the same C reader a host test uses.
 *
 * WHY THIS EXISTS NEXT TO WadSource
 *
 * WadSource maps the same partition and opens w_wad.h over a raw WAD. A raw WAD
 * can never be there: the shareware DOOM1.WAD is 4,196,020 B, larger than the
 * whole 4 MB chip, and the partition is 2 MB. What the image at 0x200000 holds
 * is the PWHD container from lib/doom/whd.h -- 1,913,167 B for the same 1264
 * lumps. So this file is WadSource's shape over the container reader, and both
 * adapters exist because both readers do.
 *
 * THE ONE PLACE THIS IS NOT A FORWARD
 *
 * whd_open() accepts a buffer only when the header's totalSize equals the size
 * handed to it. The partition is 2,097,152 B and the container inside it is
 * smaller; the rest of the partition is erased flash, not container bytes. So
 * open() reads totalSize from the header (offset 28, the field whd.h documents)
 * and gives the reader exactly that many bytes. The consequence is deliberate:
 * the 32-bit length in the header is the one the mapping is checked against, so
 * a container that grew or shrank under the firmware is rejected here instead of
 * decoding from the wrong offsets.
 *
 * Mapping, in this order, with the code each failure reports:
 *
 *   1. state == NULL                          ERR_STATE_NULL
 *   2. a mapping is already live              ERR_ALREADY_OPEN
 *   3. no `wad` data partition in this image  ERR_PARTITION_MISSING
 *   4. esp_partition_mmap() fails             ERR_MMAP_FAILED
 *   5. mapping is shorter than a header       ERR_CONTAINER_INVALID (released)
 *   6. totalSize is under 40 or over the
 *      mapping                                ERR_CONTAINER_INVALID (released)
 *   7. whd_open() rejects the image           ERR_CONTAINER_INVALID (released)
 *   8. accepted: the mapping stays live       OK
 *
 * Nothing here writes to the console. WadSource logs because it is also the
 * thing a maintainer pokes from the bench; this adapter is opened by whatever
 * layer needs the lumps, and that layer owns its own record of the outcome. The
 * D2.3 probe in DoomScene::init() is one such caller and prints the code.
 *
 * The mapping is read-only and lives until doom_whd_source_close(). The reader
 * state is owned by the caller, exactly as whd.h requires, so the accessors take
 * it as an argument and this file keeps no view of it -- the only state it owns
 * is the mapping handle.
 *
 * Everything is C-callable: this header is valid C and every entry point is
 * `extern "C"`, so the DOOM core can read the container without knowing that
 * this side is C++.
 */
#ifndef DOOM_WHD_SOURCE_H
#define DOOM_WHD_SOURCE_H

#include <stddef.h>
#include <stdint.h>

#include "whd.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Error codes --------------------------------------------------------- */

/*
 * One code per failure class, in the same spirit as whd_error_t. The open path
 * uses the first seven; the accessors use the rest, so a caller that already
 * knows which call failed does not have to read a second number to learn why.
 *
 *   DOOM_WHD_SOURCE_OK                    the mapping is live and the reader
 *                                         accepted it
 *   DOOM_WHD_SOURCE_ERR_STATE_NULL        a NULL state struct was passed
 *   DOOM_WHD_SOURCE_ERR_ALREADY_OPEN      a mapping is already live; close first
 *   DOOM_WHD_SOURCE_ERR_PARTITION_MISSING no `wad` partition in this image
 *   DOOM_WHD_SOURCE_ERR_MMAP_FAILED       the partition exists but did not map
 *   DOOM_WHD_SOURCE_ERR_CONTAINER_INVALID the mapping is not a container this
 *                                         reader accepts, or its own length
 *                                         cannot describe this mapping; the
 *                                         mapping was released again
 *   DOOM_WHD_SOURCE_ERR_UNAVAILABLE       this build has no flash mapping (a
 *                                         host build compiled this file)
 *   DOOM_WHD_SOURCE_ERR_LUMP_INDEX        the lump index is outside the
 *                                         directory (WHD_ERR_LUMP_INDEX)
 *   DOOM_WHD_SOURCE_ERR_NULL_OUTPUT       no buffer was given for a lump with
 *                                         bytes (WHD_ERR_NULL_OUTPUT)
 *   DOOM_WHD_SOURCE_ERR_BUFFER_TOO_SMALL  the buffer cannot hold the decoded
 *                                         lump; nothing was written, and the
 *                                         required size comes back through
 *                                         requiredSize
 *                                         (WHD_ERR_BUFFER_TOO_SMALL)
 *   DOOM_WHD_SOURCE_ERR_DECODE            the payload is malformed or does not
 *                                         decode to its declared size
 *                                         (WHD_ERR_CORRUPT_PAYLOAD)
 */
#define DOOM_WHD_SOURCE_OK 0
#define DOOM_WHD_SOURCE_ERR_STATE_NULL 1
#define DOOM_WHD_SOURCE_ERR_ALREADY_OPEN 2
#define DOOM_WHD_SOURCE_ERR_PARTITION_MISSING 3
#define DOOM_WHD_SOURCE_ERR_MMAP_FAILED 4
#define DOOM_WHD_SOURCE_ERR_CONTAINER_INVALID 5
#define DOOM_WHD_SOURCE_ERR_UNAVAILABLE 6
#define DOOM_WHD_SOURCE_ERR_LUMP_INDEX 7
#define DOOM_WHD_SOURCE_ERR_NULL_OUTPUT 8
#define DOOM_WHD_SOURCE_ERR_BUFFER_TOO_SMALL 9
#define DOOM_WHD_SOURCE_ERR_DECODE 10

/* --- Public API ---------------------------------------------------------- */

/**
 * @brief Maps the `wad` partition read-only and opens @p state over the
 *        container it holds.
 *
 * Validation order and codes: see the file comment. On every failure the
 * mapping is released and @p state is left closed by whd_open(); on success the
 * mapping stays alive until doom_whd_source_close().
 *
 * @param state Caller-owned reader state, rewritten by whd_open().
 * @return DOOM_WHD_SOURCE_OK or one of the codes above.
 */
int doom_whd_source_open(whd_state_t* state);

/**
 * @brief Releases the mapping taken by doom_whd_source_open().
 *
 * Idempotent: calling it when nothing is mapped does nothing.
 *
 * A reader state opened over this mapping must not be used afterwards: its base
 * pointer now refers to unmapped flash. The reader owns no resource of its own,
 * so there is nothing else to release; call doom_whd_source_open() again (which
 * rewrites @p state) before reading.
 */
void doom_whd_source_close(void);

/**
 * @brief Returns the mapped byte count: how much flash this adapter mapped.
 *
 * This is the partition size, not the container length. The container is
 * shorter than the partition -- the remainder is erased flash -- and its own
 * length is the accepted reader's @c state->size. 0 when nothing is mapped, so
 * the same call answers "how much did we map" and "is anything mapped".
 *
 * @return Bytes mapped, or 0 when no mapping is live (including every host
 *         build, which maps nothing).
 */
uint32_t doom_whd_source_mapped_size(void);

/**
 * @brief How many lumps the container holds.
 *
 * @param state Open reader.
 * @return The lump count, or 0 for a closed reader.
 */
int32_t doom_whd_source_lump_count(const whd_state_t* state);

/**
 * @brief Resolves a lump name to its index, case-insensitively.
 *
 * The rules are whd_lump_index_for_name()'s, unchanged: the query is upper-cased
 * byte by byte and compared against the 8-byte name field, a query longer than
 * 8 characters matches on its first 8, and the first match in file order wins.
 *
 * @param state Open reader.
 * @param name  NUL-terminated ASCII name, in any case.
 * @return The lump index, or -1 when the name is absent, the reader is not open
 *         or @p name is NULL.
 */
int doom_whd_source_lump_index(const whd_state_t* state, const char* name);

/**
 * @brief Returns a lump's uncompressed size, without decoding anything.
 *
 * This is the number a caller sizes its decode buffer from.
 *
 * @param state Open reader.
 * @param lump  Lump index.
 * @return The decoded size in bytes, or 0 when @p lump is outside the directory
 *         or the reader is not open. A genuinely empty lump also answers 0.
 */
int32_t doom_whd_source_lump_size(const whd_state_t* state, int lump);

/**
 * @brief Decodes one lump into a caller-supplied bounded buffer.
 *
 * The buffer is never overrun: when it cannot hold the lump the call reports
 * DOOM_WHD_SOURCE_ERR_BUFFER_TOO_SMALL without writing a single byte, and
 * reports the required size through @p requiredSize. A lump of zero bytes
 * succeeds with @p out NULL and any capacity.
 *
 * A payload that fails to decode reports DOOM_WHD_SOURCE_ERR_DECODE and leaves
 * whatever partial bytes it produced in @p out; only the size checks leave the
 * buffer untouched.
 *
 * The caller owns the buffer because it owns the size: a lump of DOOM1.WAD can
 * decode to 68,168 B, more than the board's task stack, so where that memory
 * comes from is a decision for the layer that reads the lump, not for this one.
 *
 * @param state        Open reader.
 * @param lump         Lump index.
 * @param out          Destination buffer; may be NULL only when the lump is
 *                     empty.
 * @param capacity     Bytes available at @p out.
 * @param requiredSize Optional out parameter: receives the lump's uncompressed
 *                     size on success, on BUFFER_TOO_SMALL and on NULL_OUTPUT.
 *                     Untouched otherwise. May be NULL.
 * @return DOOM_WHD_SOURCE_OK, ERR_STATE_NULL, ERR_LUMP_INDEX, ERR_NULL_OUTPUT,
 *         ERR_BUFFER_TOO_SMALL or ERR_DECODE.
 */
int doom_whd_source_read_lump(const whd_state_t* state, int lump, uint8_t* out,
                              size_t capacity, uint32_t* requiredSize);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_WHD_SOURCE_H */
