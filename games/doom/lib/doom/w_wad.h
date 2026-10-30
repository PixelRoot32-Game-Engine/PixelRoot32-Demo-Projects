/*
 * The WAD directory reader of the DOOM port.
 *
 * The reader works over a caller-supplied byte buffer and never asks where that
 * buffer came from. On the board it is the `wad` flash partition mapped
 * read-only with esp_partition_mmap(); in a host test it is an array. Keeping
 * the reader this ignorant is what makes the same code testable on a desktop
 * and provable with synthetic WADs, without a filesystem, stdio or allocation.
 *
 * The WAD layout this file parses (little-endian, exactly as the 1993 format):
 *
 *   offset 0   char     identification[4]   "IWAD" or "PWAD"
 *   offset 4   int32    numlumps
 *   offset 8   int32    infotableofs
 *            ...
 *   info+0     int32    filepos
 *   info+4     int32    size
 *   info+8     char     name[8]             NUL padded, upper case
 *
 * Every integer is read byte by byte with an explicit little-endian
 * reconstruction. Nothing is memcpy'd into an int and nothing casts the buffer
 * to int32_t*, so an unaligned directory and a big-endian host are both safe.
 *
 * A failed wad_open() reports one distinct code per failure class and leaves the
 * state closed and empty: there is never a partially open reader. The exact
 * validation order is documented on wad_open().
 *
 * This is C99 only: <stddef.h> and <stdint.h>, no allocation, no globals, no
 * stdio, no floating point. The caller owns the one state struct.
 */
#ifndef DOOM_W_WAD_H
#define DOOM_W_WAD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Format constants ---------------------------------------------------- */

/* The four identification bytes at the start of every WAD. */
#define WAD_IDENTIFICATION_SIZE 4u

/* identification[4] + numlumps (4) + infotableofs (4). */
#define WAD_HEADER_SIZE 12u

/* filepos (4) + size (4) + name[8]. */
#define WAD_DIRECTORY_ENTRY_SIZE 16u

/* The name field, NUL padded, upper case, not necessarily NUL terminated. */
#define WAD_NAME_SIZE 8u

/* The WAD directory addresses everything with a signed 32-bit offset, so a
 * buffer larger than INT32_MAX cannot be described by it. */
#define WAD_MAX_BUFFER_SIZE 2147483647L

/* --- Error codes --------------------------------------------------------- */

/*
 * One code per failure class, never a shared "bad WAD" catch-all:
 *
 *   WAD_ERR_OK                  the buffer was accepted; the reader is open
 *   WAD_ERR_NULL_STATE          the caller passed no state struct
 *   WAD_ERR_NULL_BUFFER         the state is valid but base is NULL
 *   WAD_ERR_SIZE_RANGE          size does not fit the format's int32 offsets
 *   WAD_ERR_TOO_SMALL           size cannot hold the 12-byte header
 *   WAD_ERR_BAD_IDENTIFICATION  neither "IWAD" nor "PWAD"
 *   WAD_ERR_NUMLUMPS            numlumps is negative or cannot fit the buffer
 *   WAD_ERR_INFOTABLE_RANGE     infotableofs is negative, past the buffer, or
 *                               leaves no room for numlumps directory entries
 *   WAD_ERR_INFOTABLE_OVERLAP   the directory starts inside the header
 *   WAD_ERR_LUMP_RANGE          a filepos/size pair is negative or extends past
 *                               the buffer
 *
 * With a C99 compiler these are enumeration constants, not macros, so a
 * debugger can name the value and no call site needs a magic number.
 */
typedef enum wad_error {
    WAD_ERR_OK = 0,
    WAD_ERR_NULL_STATE = 1,
    WAD_ERR_NULL_BUFFER = 2,
    WAD_ERR_SIZE_RANGE = 3,
    WAD_ERR_TOO_SMALL = 4,
    WAD_ERR_BAD_IDENTIFICATION = 5,
    WAD_ERR_NUMLUMPS = 6,
    WAD_ERR_INFOTABLE_RANGE = 7,
    WAD_ERR_INFOTABLE_OVERLAP = 8,
    WAD_ERR_LUMP_RANGE = 9
} wad_error_t;

/* --- Reader state -------------------------------------------------------- */

/*
 * Everything the reader knows, owned by the caller. There is no static data in
 * this translation unit, so two readers can coexist (a test opens one per
 * case) and the state can live on the stack or next to the scene.
 *
 * Treat the fields as read-only after a successful open: `open` is 1 and
 * `error` is WAD_ERR_OK only for an accepted buffer. A rejected open zeroes all
 * of them, so a stale state can never answer a unlucky lump lookup.
 */
typedef struct wad_state {
    const uint8_t* base;      /* start of the mapped WAD bytes */
    size_t size;              /* bytes available at base */
    int32_t numLumps;         /* directory entry count */
    int32_t infoTableOffset;  /* directory start, relative to base */
    int open;                 /* 1 after a successful wad_open() */
    wad_error_t error;        /* result of the last wad_open() */
} wad_state_t;

/* --- Public API ---------------------------------------------------------- */

/**
 * @brief Opens the reader over a caller-supplied WAD image.
 *
 * Validation order, and the code each step reports:
 *
 *   1. state == NULL                              WAD_ERR_NULL_STATE
 *   2. base == NULL                               WAD_ERR_NULL_BUFFER
 *   3. size > WAD_MAX_BUFFER_SIZE                 WAD_ERR_SIZE_RANGE
 *   4. size < WAD_HEADER_SIZE                     WAD_ERR_TOO_SMALL
 *   5. identification is "IWAD" or "PWAD"         WAD_ERR_BAD_IDENTIFICATION
 *   6. numlumps >= 0 and fits the buffer          WAD_ERR_NUMLUMPS
 *   7. infotableofs >= 0 and <= size              WAD_ERR_INFOTABLE_RANGE
 *   8. infotableofs >= WAD_HEADER_SIZE            WAD_ERR_INFOTABLE_OVERLAP
 *   9. directory end <= size                      WAD_ERR_INFOTABLE_RANGE
 *  10. every filepos >= 0, size >= 0 and
 *      filepos + size <= buffer size              WAD_ERR_LUMP_RANGE
 *
 * @param state Caller-owned reader state; rewritten on every call.
 * @param base  Read-only WAD bytes (flash mapping or a host array).
 * @param size  Bytes available at @p base.
 * @return WAD_ERR_OK when the whole directory was accepted, one of the codes
 *         above otherwise. On any error all state fields are cleared.
 */
wad_error_t wad_open(wad_state_t* state, const uint8_t* base, size_t size);

/**
 * @brief Finds a lump by name, case-insensitively.
 *
 * The query is upper-cased byte by byte and compared against the 8-byte name
 * field, with the query treated as NUL padded once its terminator is reached.
 * A query longer than 8 characters matches its first 8 characters, which is
 * what DOOM's own name8 comparison does.
 *
 * Duplicate names follow DOOM's rule: the **first match wins**. The directory
 * is scanned from lump 0 upward and the first entry whose name matches is
 * returned, so a later duplicate never shadows an earlier one.
 *
 * @param state Open reader.
 * @param name  NUL-terminated ASCII name, in any case.
 * @return The lump index, or -1 when the name is absent, the state is not open
 *         or @p name is NULL.
 */
int W_CheckNumForName(const wad_state_t* state, const char* name);

/**
 * @brief Returns the byte length of a lump.
 *
 * @param state Open reader.
 * @param lump  Lump index.
 * @return The validated size, or 0 when @p lump is outside the directory or
 *         the reader is not open. A genuinely empty lump also answers 0, so
 *         callers that need to distinguish the two check the index first.
 */
int32_t W_LumpLength(const wad_state_t* state, int lump);

/**
 * @brief Returns a pointer into the caller's buffer for a lump.
 *
 * @param state Open reader.
 * @param lump  Lump index.
 * @return base + filepos for a valid index, or NULL for an invalid index or a
 *         closed reader. For a zero-length lump the pointer is still the
 *         validated one-past-the-data position inside the buffer.
 */
const uint8_t* W_LumpPointer(const wad_state_t* state, int lump);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_W_WAD_H */
