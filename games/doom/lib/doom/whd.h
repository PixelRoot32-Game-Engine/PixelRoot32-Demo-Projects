/*
 * The compressed container reader of the DOOM port.
 *
 * WHY THIS CONTAINER EXISTS
 *
 * The shareware DOOM1.WAD is 4,196,020 bytes. The board's flash is 4,194,304
 * bytes in total and the application partition needs roughly 2 MB, so the WAD
 * does not fit anywhere: it is larger than the whole chip. The renderer still
 * needs random access to any lump, and needs to know a lump's uncompressed size
 * before deciding how much buffer to give it.
 *
 * The rp2040-doom project solves the same problem with a container whose magic
 * is "IWHD"/"IWHX". Its writer (src/whd_gen/wad.cpp on branch rp2) and its
 * format header (src/whddata.h) were both read to write this file. That format
 * is NOT this one, for two reasons:
 *
 *  1. It stores lump payloads verbatim. It has no per-lump compression at all;
 *     the size it saves comes from a second, DOOM-specific pass in whd_gen that
 *     re-encodes graphics, flats, sprites and map data into its own structs
 *     (whdpatch_t, whdnode_t, whdsidedef_t, vpatch column segments) and
 *     Huffman-compresses only the music. Repacking a WAD that way without the
 *     re-encode pass saves the directory overhead and nothing else, so it cannot
 *     bring 4.2 MB under 2 MB, and the re-encode pass cannot be written before
 *     the renderer that consumes those structs exists.
 *  2. Its directory keeps only a 2-bit correction to a word-aligned size, so a
 *     lump's uncompressed size and its stored size are the same number. This
 *     port needs both, because every lump is DEFLATE-compressed.
 *
 * So this file defines a format of its own. It borrows the one idea that
 * matters from the upstream container -- a compact integer-only directory that
 * points at a flat payload area, so a lookup is an add and not a parse -- and
 * nothing else. The magic is "PWHD" so a container written here can never be
 * mistaken for an upstream "IWHD" file.
 *
 * CONTAINER LAYOUT (all integers little-endian; every field read byte by byte)
 *
 *   offset  size  field
 *   0       4     magic[4]            'P','W','H','D'
 *   4       2     version             1
 *   6       2     flags               must be 0
 *   8       4     numLumps
 *   12      4     nameCount           name-table entries
 *   16      4     directoryOffset     must be WHD_HEADER_SIZE
 *   20      4     nameTableOffset     must be directoryOffset + numLumps * 16
 *   24      4     dataOffset          must be nameTableOffset + nameCount * 12
 *   28      4     totalSize           must equal the buffer size
 *   32      4     sourceCrc32         CRC-32 of the source WAD, for identity
 *   36      4     reserved            must be 0
 *
 *   directory, per lump, 16 bytes:
 *   0       4     payloadOffset       relative to dataOffset
 *   4       4     storedSize          bytes in the payload area
 *   8       4     uncompressedSize    bytes the lump decodes to
 *   12      1     method              WHD_METHOD_STORED or WHD_METHOD_DEFLATE
 *   13      3     reserved            must be 0
 *
 *   name table, per entry, 12 bytes:
 *   0       8     name[8]             as in the source WAD, NUL padded
 *   8       4     lumpIndex
 *
 *   data area at dataOffset: each lump's payload, back to back in directory
 *   order with no gap and no padding.
 *
 * A method of WHD_METHOD_STORED means storedSize == uncompressedSize and the
 * payload is the lump. A method of WHD_METHOD_DEFLATE means the payload is one
 * RFC 1951 stream with no zlib wrapper (a raw DEFLATE stream, what zlib calls
 * windowBits = -15), and it must decode to exactly uncompressedSize bytes.
 *
 * The encoder that writes this layout is tools/whd_encode.py. Its --check mode
 * re-decodes a container in memory and compares every lump against the source
 * WAD, and a container it produced was decoded on the host through this reader
 * for every one of the 1264 lumps of the shareware WAD.
 *
 * WHAT THIS READER DELIBERATELY DOES NOT DO
 *
 * It is the primitive only. It holds no decoded-lump cache (that is the next
 * layer), it does not interpret lump contents, it needs a buffer as large as the
 * whole lump because it decodes in one pass, and name lookup is a linear scan.
 *
 * This is C99 only: <stddef.h> and <stdint.h>, no allocation, no globals, no
 * stdio, no floating point. Nothing here depends on w_wad.c. The caller owns the
 * one state struct and every buffer.
 */
#ifndef DOOM_WHD_H
#define DOOM_WHD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Format constants ---------------------------------------------------- */

/* "PWHD". Not "IWHD": see the header comment for why this is a distinct
 * container and not the rp2040-doom one. */
#define WHD_MAGIC_0 ((uint8_t)'P')
#define WHD_MAGIC_1 ((uint8_t)'W')
#define WHD_MAGIC_2 ((uint8_t)'H')
#define WHD_MAGIC_3 ((uint8_t)'D')

/* The only version this reader accepts. */
#define WHD_VERSION 1u

/* magic (4) + version (2) + flags (2) + numLumps (4) + nameCount (4) +
 * directoryOffset (4) + nameTableOffset (4) + dataOffset (4) + totalSize (4) +
 * sourceCrc32 (4) + reserved (4). */
#define WHD_HEADER_SIZE 40u

/* payloadOffset (4) + storedSize (4) + uncompressedSize (4) + method (1) +
 * reserved (3). */
#define WHD_DIRECTORY_ENTRY_SIZE 16u

/* name[8] + lumpIndex (4). */
#define WHD_NAME_ENTRY_SIZE 12u
#define WHD_NAME_SIZE 8u

/* Method byte values. */
#define WHD_METHOD_STORED 0u
#define WHD_METHOD_DEFLATE 1u

/* The container addresses everything with uint32_t, so a larger buffer cannot
 * be described by it. */
#define WHD_MAX_BUFFER_SIZE 4294967295UL

/* --- Error codes --------------------------------------------------------- */

/*
 * One code per failure class, never a shared "bad container" catch-all, so a
 * rejection at a boundary can be told apart from a rejection of the payload:
 *
 *   WHD_ERR_OK                  the buffer was accepted; the reader is open
 *   WHD_ERR_NULL_STATE          the caller passed no state struct
 *   WHD_ERR_NULL_BUFFER         the state is valid but base is NULL
 *   WHD_ERR_SIZE_RANGE          size does not fit the format's uint32_t fields
 *   WHD_ERR_TOO_SMALL           size cannot hold the 40-byte header
 *   WHD_ERR_BAD_MAGIC           the first four bytes are not "PWHD"
 *   WHD_ERR_VERSION             version is not WHD_VERSION
 *   WHD_ERR_FLAGS               flags is not zero
 *   WHD_ERR_RESERVED            the header reserved word is not zero
 *   WHD_ERR_TOTAL_SIZE          totalSize does not equal the buffer size
 *   WHD_ERR_NUMLUMPS            numLumps cannot fit the buffer
 *   WHD_ERR_DIRECTORY_RANGE     directoryOffset is not WHD_HEADER_SIZE, or the
 *                               directory does not fit the buffer
 *   WHD_ERR_NAME_TABLE_RANGE    nameTableOffset is not the directory end, or
 *                               nameCount cannot fit the buffer
 *   WHD_ERR_DATA_RANGE          dataOffset is not the name-table end, or is
 *                               past the buffer
 *   WHD_ERR_LUMP_RANGE          a payload is outside the data area, or a
 *                               payload's sizes are impossible for its method
 *   WHD_ERR_LUMP_ORDER          a payload starts before the previous one ends
 *   WHD_ERR_METHOD              the method byte is not a known method
 *   WHD_ERR_STORED_SIZE         a stored entry's two sizes disagree
 *   WHD_ERR_NAME_RANGE          a name entry points outside the directory
 *   WHD_ERR_LUMP_INDEX          the requested lump is outside the directory
 *   WHD_ERR_NULL_OUTPUT         the output is NULL for a lump with bytes
 *   WHD_ERR_BUFFER_TOO_SMALL    the output cannot hold the decoded lump
 *   WHD_ERR_CORRUPT_PAYLOAD     the DEFLATE stream is malformed or does not
 *                               decode to the declared uncompressed size
 *
 * With a C99 compiler these are enumeration constants, not macros, so a
 * debugger can name the value and no call site needs a magic number.
 */
typedef enum whd_error {
    WHD_ERR_OK = 0,
    WHD_ERR_NULL_STATE = 1,
    WHD_ERR_NULL_BUFFER = 2,
    WHD_ERR_SIZE_RANGE = 3,
    WHD_ERR_TOO_SMALL = 4,
    WHD_ERR_BAD_MAGIC = 5,
    WHD_ERR_VERSION = 6,
    WHD_ERR_FLAGS = 7,
    WHD_ERR_RESERVED = 8,
    WHD_ERR_TOTAL_SIZE = 9,
    WHD_ERR_NUMLUMPS = 10,
    WHD_ERR_DIRECTORY_RANGE = 11,
    WHD_ERR_NAME_TABLE_RANGE = 12,
    WHD_ERR_DATA_RANGE = 13,
    WHD_ERR_LUMP_RANGE = 14,
    WHD_ERR_LUMP_ORDER = 15,
    WHD_ERR_METHOD = 16,
    WHD_ERR_STORED_SIZE = 17,
    WHD_ERR_NAME_RANGE = 18,
    WHD_ERR_LUMP_INDEX = 19,
    WHD_ERR_NULL_OUTPUT = 20,
    WHD_ERR_BUFFER_TOO_SMALL = 21,
    WHD_ERR_CORRUPT_PAYLOAD = 22
} whd_error_t;

/* --- Reader state -------------------------------------------------------- */

/*
 * Everything the reader knows, owned by the caller. There is no static data in
 * this translation unit, so two readers can coexist (a test opens one per case)
 * and the state can live on the stack or next to the scene.
 *
 * Treat the fields as read-only after a successful open: `open` is 1 and
 * `error` is WHD_ERR_OK only for an accepted buffer. A rejected open zeroes all
 * of them, so a stale state can never answer an unlucky lump lookup.
 */
typedef struct whd_state {
    const uint8_t* base;          /* start of the container bytes */
    size_t size;                  /* bytes available at base */
    uint32_t numLumps;            /* directory entry count */
    uint32_t nameCount;           /* name-table entry count */
    uint32_t directoryOffset;     /* directory start, relative to base */
    uint32_t nameTableOffset;     /* name table start, relative to base */
    uint32_t dataOffset;          /* payload area start, relative to base */
    int open;                     /* 1 after a successful whd_open() */
    whd_error_t error;            /* result of the last whd_open() */
} whd_state_t;

/* --- Public API ---------------------------------------------------------- */

/**
 * @brief Opens the reader over a caller-supplied container image.
 *
 * Validation order, and the code each step reports:
 *
 *   1. state == NULL                                  WHD_ERR_NULL_STATE
 *   2. base == NULL                                   WHD_ERR_NULL_BUFFER
 *   3. size > WHD_MAX_BUFFER_SIZE                     WHD_ERR_SIZE_RANGE
 *   4. size < WHD_HEADER_SIZE                         WHD_ERR_TOO_SMALL
 *   5. magic is "PWHD"                                WHD_ERR_BAD_MAGIC
 *   6. version == WHD_VERSION                         WHD_ERR_VERSION
 *   7. flags == 0                                     WHD_ERR_FLAGS
 *   8. reserved == 0                                  WHD_ERR_RESERVED
 *   9. totalSize == size                              WHD_ERR_TOTAL_SIZE
 *  10. numLumps fits the buffer after the header       WHD_ERR_NUMLUMPS
 *  11. directoryOffset == WHD_HEADER_SIZE and the
 *      directory fits the buffer                       WHD_ERR_DIRECTORY_RANGE
 *  12. nameTableOffset == directory end and
 *      nameCount fits the buffer                       WHD_ERR_NAME_TABLE_RANGE
 *  13. dataOffset == name-table end and
 *      dataOffset <= size                              WHD_ERR_DATA_RANGE
 *  14. per entry: method is known                     WHD_ERR_METHOD
 *  15. per entry: the two sizes agree for a stored
 *      entry, and a deflate entry with an
 *      uncompressed size has a payload                WHD_ERR_LUMP_RANGE
 *  16. per entry: payloadOffset >= previous payload
 *      end (no gap is required, overlap is not)       WHD_ERR_LUMP_ORDER
 *  17. per entry: payload end <= data area length     WHD_ERR_LUMP_RANGE
 *  18. per name entry: lumpIndex < numLumps           WHD_ERR_NAME_RANGE
 *
 * @param state Caller-owned reader state; rewritten on every call.
 * @param base  Read-only container bytes (flash mapping or a host array).
 * @param size  Bytes available at @p base.
 * @return WHD_ERR_OK when the whole container was accepted, one of the codes
 *         above otherwise. On any error all state fields are cleared.
 */
whd_error_t whd_open(whd_state_t* state, const uint8_t* base, size_t size);

/**
 * @brief Returns how many lumps the container holds.
 *
 * @param state Open reader.
 * @return The lump count, or 0 for a closed reader.
 */
int32_t whd_lump_count(const whd_state_t* state);

/**
 * @brief Returns a lump's uncompressed size, without decoding anything.
 *
 * This is the number a caller sizes its output buffer from.
 *
 * @param state Open reader.
 * @param lump  Lump index.
 * @return The decoded size in bytes, or 0 when @p lump is outside the directory
 *         or the reader is not open. A genuinely empty lump also answers 0.
 */
int32_t whd_lump_size(const whd_state_t* state, int lump);

/**
 * @brief Returns how many bytes the lump's payload occupies in the container.
 *
 * Equal to the uncompressed size for a stored lump. Useful for measuring how
 * much of the container a lump costs; decoding does not need it.
 *
 * @param state Open reader.
 * @param lump  Lump index.
 * @return The stored size in bytes, or 0 for an invalid index or closed reader.
 */
int32_t whd_lump_stored_size(const whd_state_t* state, int lump);

/**
 * @brief Resolves a lump name to its index, case-insensitively.
 *
 * The query is upper-cased byte by byte and compared against the 8-byte name
 * field with the query treated as NUL padded once its terminator is reached, so
 * a query longer than 8 characters matches on its first 8 -- the same rule as
 * DOOM's own name8 comparison. The name table is scanned in file order and the
 * first match wins, so a later duplicate cannot shadow an earlier one.
 *
 * The scan is linear: 8 bytes per entry, in file order.
 *
 * @param state Open reader.
 * @param name  NUL-terminated ASCII name, in any case.
 * @return The lump index, or -1 when the name is absent, the reader is not open
 *         or @p name is NULL.
 */
int whd_lump_index_for_name(const whd_state_t* state, const char* name);

/**
 * @brief Decodes one lump into a caller-supplied bounded buffer.
 *
 * The buffer is never overrun: when it cannot hold the lump's uncompressed size
 * the call reports WHD_ERR_BUFFER_TOO_SMALL without writing a single byte, and
 * reports the required size through @p requiredSize. A lump of zero bytes
 * succeeds with @p out NULL and any capacity.
 *
 * A payload that fails to decode leaves whatever partial bytes it produced in
 * @p out and reports WHD_ERR_CORRUPT_PAYLOAD; only the size checks above leave
 * the buffer untouched.
 *
 * @param state      Open reader.
 * @param lump       Lump index.
 * @param out        Destination buffer; may be NULL only when the lump is empty.
 * @param capacity   Bytes available at @p out.
 * @param requiredSize Optional out parameter: receives the lump's uncompressed
 *                     size on success, on WHD_ERR_BUFFER_TOO_SMALL and on
 *                     WHD_ERR_NULL_OUTPUT. Untouched otherwise. May be NULL.
 * @return WHD_ERR_OK, WHD_ERR_LUMP_INDEX, WHD_ERR_NULL_OUTPUT,
 *         WHD_ERR_BUFFER_TOO_SMALL or WHD_ERR_CORRUPT_PAYLOAD.
 */
whd_error_t whd_read_lump(const whd_state_t* state, int lump, uint8_t* out,
                          size_t capacity, uint32_t* requiredSize);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_WHD_H */
