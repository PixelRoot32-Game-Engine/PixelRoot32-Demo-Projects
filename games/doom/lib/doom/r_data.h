/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * Decoders for the three picture formats a DOOM WAD stores: patches, composite
 * textures and flats.
 *
 * This is the layer above the container and below the renderer. It takes bytes
 * that are already in memory -- a mapped flash region, a zone block, a host
 * array -- and turns them into pixels. It never asks where they came from, so
 * it holds no dependency on w_wad, whd or z_zone, and a host test can hand it
 * synthetic lumps built byte by byte.
 *
 * WHY VALIDATION COMES FIRST
 *
 * DOOM's own readers trust the WAD: `R_GetColumn` adds a stored offset to a
 * lump pointer and the drawing code walks the result. On a desktop with a
 * verified IWAD that is fine. Here the bytes come from a compressed container
 * on a board nobody inspects, and a single bad offset would read past the
 * buffer into the rest of the arena. So every entry point proves the whole
 * structure before the first byte is written:
 *
 *   - a patch is validated once, end to end: header, column directory, every
 *     column offset, every post, every post's pixels, the terminator of every
 *     column, and that no post reaches past the patch height;
 *   - a texture lookup validates the directory it scans and the definition it
 *     matches;
 *   - a composition validates the texture, PNAMES and *every* patch it will
 *     blit before it touches the output surface;
 *   - a flat is exactly R_FLAT_BYTES.
 *
 * When a call fails it reports one distinct code for the failure class and
 * writes nothing: a rejected patch, texture or flat cannot leave a partially
 * decoded result behind. The only exception is documented on
 * r_texture_compose(): a fetch callback that answers differently in the second
 * pass of the same call.
 *
 * THE FORMATS, AS DOOM STORES THEM
 *
 * All integers little-endian and packed, read byte by byte. Nothing here
 * memcpy's into an integer or casts the buffer to a wider type, so an unaligned
 * lump and a big-endian host are both safe.
 *
 *   patch (patch_t in r_defs.h, R_GetColumn / R_DrawMaskedColumn in r_data.c)
 *     int16  width
 *     int16  height
 *     int16  leftoffset        sprite placement, may be negative
 *     int16  topoffset         sprite placement, may be negative
 *     int32  columnofs[width]  inside this lump
 *     ...columns...
 *
 *   A column is a chain of posts, each post:
 *     uint8  topdelta          row of the post's first pixel
 *     uint8  length            pixels in the post
 *     uint8  unused1           read by nobody, written as zero here
 *     uint8  pixels[length]
 *     uint8  unused2           read by nobody, written as zero here
 *   and the chain ends with a single topdelta byte of 0xFF. The next post starts
 *   length + 4 bytes after the current one, which is why "unused2" is a byte of
 *   the format and not padding: it is what makes the stride four plus the
 *   payload. Columns are stored top down, and the pixels of a post live at rows
 *   topdelta .. topdelta+length-1 of the patch. Gaps between posts and rows a
 *   column never reaches are transparent, not black: a patch is a mask.
 *
 *   texture directory (TEXTURE1 / TEXTURE2, maptexture_t in r_data.c)
 *     int32  numtextures
 *     int32  offsets[numtextures]    inside this lump
 *     ...definitions...
 *
 *   each definition:
 *     char   name[8]         NUL padded, upper case
 *     int32  masked          non-zero when the texture has transparent pixels
 *     int16  width
 *     int16  height
 *     int32  obsolete        the old "columndirectory", ignored by DOOM
 *     int16  patchcount
 *     struct patches[patchcount]  (mappatch_t)
 *       int16 originx        upper-left origin of the patch in the texture
 *       int16 originy
 *       int16 patch          index into PNAMES
 *       int16 stepdir        ignored by DOOM, ignored here
 *       int16 colormap       ignored by DOOM, ignored here
 *
 *   PNAMES
 *     int32  nummappatches
 *     char   names[nummappatches][8]
 *
 *   flat: 64 x 64 = 4096 raw palette bytes, no header at all.
 *
 * THE COLUMN FORM THIS FILE PRODUCES
 *
 * r_patch_column() emits the post chain in DOOM's own column_t form -- the form
 * R_GetColumn() returns and R_DrawMaskedColumn() consumes -- normalised so the
 * two unused bytes are zero and the terminator is present:
 *
 *   [topdelta][length][0][pixels...][0] ... [0xFF]
 *
 * so the stream is sum(length + 4) + 1 bytes and a consumer advances by
 * length + 4 exactly as DOOM does. A height-byte array was the alternative; it
 * was rejected because a patch is masked, and a dense array cannot say
 * "transparent" without inventing a palette index that means it.
 *
 * THE COMPOSITE SURFACE
 *
 * r_texture_compose() fills every byte of the surface with
 * R_TEXTURE_TRANSPARENT (0) and every byte of the optional mask with 0, then
 * blits each patch post at (originx, originy + topdelta), clipping to the
 * texture rectangle and to the surface. A blitted pixel sets its mask byte to
 * R_TEXTURE_OPAQUE (1); pixels no patch wrote stay 0/0.
 *
 * The fill value is therefore not an invisible palette index -- index 0 is a
 * real colour -- it is simply the documented "nothing here". A caller that must
 * distinguish the two asks for the mask; a caller drawing an unmasked texture
 * can pass NULL and ignore it.
 *
 * The mask is not a formality on the real data. Every one of the 125 shareware
 * textures has `masked == 0`, yet nine of them leave pixels unwritten -- the
 * brown pillar pieces (BRNBIGC, BRNBIGL, BRNBIGR), the BRNSMAL family and SKY1,
 * because a patch is shorter than the texture it is placed in. DOOM's
 * R_GenerateLookup() gives up on such a texture with "column without a patch"
 * and leaves the lookup half-built; this decoder reports the holes through the
 * mask and composes the rest. A renderer that only ever draws opaque surfaces
 * can ignore the plane, but nothing else can tell it where those holes are.
 *
 * One deliberate difference from vanilla composition is worth naming. DOOM
 * applies a patch's origin only to columns it composites (R_GenerateLookup
 * marks a column composite when more than one patch covers it); a column
 * covered by a single patch is read straight from the patch, origin ignored.
 * This decoder applies the origin to every column, which is what "blit the
 * patches at their origins" means and the only coherent texture model. On the
 * shareware data that changes pixels for seven textures whose patches carry a
 * negative originy: AASTINKY, BROWN144, SKY1, SW1DIRT, SW2DIRT, TEKWALL1 and
 * TEKWALL5. Only TEKWALL1's affected columns are also multi-patch, so vanilla
 * applies the origin there too and the two agree except for the clipping below;
 * SKY1 never reaches a compositor in vanilla because the sky has its own draw
 * path.
 *
 * Where DOOM's R_DrawColumnInCache() clips a post that starts above the texture
 * (position < 0) it reduces the count but still copies from the post's first
 * pixel, which shifts the visible part up by the clipped amount. That is a
 * vanilla bug, not a format rule: this decoder advances the source pointer, so
 * a post's pixels really do land at rows originy + topdelta, which is what the
 * format says they are. The real data reaches the path (816 post placements
 * across those seven textures, of which TEKWALL1's 128 composited columns are
 * the ones vanilla would apply an origin to at all), so this is a visible
 * choice and not a theoretical one. tools/doom_ref.py makes the same choice,
 * which is why the comparison in tools/rdat_check.py proves the two
 * implementations agree and cannot prove either matches vanilla.
 *
 * `python tools/doom_ref.py --wad <path> --stats` reproduces every number in
 * the two paragraphs above, so they are measurements and not assertions.
 *
 * WHAT THIS LAYER DELIBERATELY DOES NOT DO
 *
 * No lump cache, no zone, no WAD or container reader: it takes buffers. No
 * decoded-texture cache and no name hash table: the renderer or the cache layer
 * above decides when to decode and keeps the result. No sprite rendering: a
 * patch's leftoffset/topoffset are reported, not applied. No colormap, no light
 * levels, no scaling, no clamping against a view window. It decodes; it does
 * not draw.
 *
 * This is C99 only: <stddef.h> and <stdint.h>, no allocation, no globals, no
 * stdio, no floating point. The caller owns every buffer.
 */
#ifndef DOOM_R_DATA_H
#define DOOM_R_DATA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Format constants ---------------------------------------------------- */

/* int16 width, height, leftoffset, topoffset. */
#define R_PATCH_HEADER_SIZE 8u

/* One int32 column offset per patch column. */
#define R_PATCH_COLUMN_ENTRY_SIZE 4u

/* topdelta + length + unused1, before the post's pixels. */
#define R_PATCH_POST_HEADER_SIZE 3u

/* The unused byte between a post's pixels and the next post's topdelta. */
#define R_PATCH_POST_TRAILER_SIZE 1u

/* What a topdelta byte is when the column has no more posts. */
#define R_PATCH_POST_END 0xFFu

/* What the decoder writes into the two bytes the format calls unused. */
#define R_PATCH_UNUSED_VALUE 0x00u

/* The 8-byte name field shared by texture definitions and PNAMES. */
#define R_TEXTURE_NAME_SIZE 8u

/* int32 numtextures at the start of TEXTURE1/TEXTURE2. */
#define R_TEXTURE_COUNT_SIZE 4u

/* One int32 definition offset per texture. */
#define R_TEXTURE_DIRECTORY_ENTRY_SIZE 4u

/* name[8] + masked (4) + width (2) + height (2) + obsolete (4) +
 * patchcount (2). maptexture_t is packed, so this is the exact stride. */
#define R_TEXTURE_HEADER_SIZE 22u

/* originx (2) + originy (2) + patch (2) + stepdir (2) + colormap (2). */
#define R_TEXTURE_PATCH_SIZE 10u

/* int32 nummappatches. */
#define R_PNAMES_HEADER_SIZE 4u

/* One PNAMES name field: char name[8]. */
#define R_PATCH_NAME_SIZE R_TEXTURE_NAME_SIZE

/* A flat is a square of raw palette indices. */
#define R_FLAT_SIZE 64
#define R_FLAT_BYTES ((size_t)R_FLAT_SIZE * (size_t)R_FLAT_SIZE)

/* The fill byte of a composite surface where no patch wrote a pixel. */
#define R_TEXTURE_TRANSPARENT 0x00u

/* The mask byte of a composite pixel a patch did write. */
#define R_TEXTURE_OPAQUE 0x01u

/* A lump this layer accepts is addressed by the format's signed 32-bit offsets,
 * so a larger buffer cannot be described by it. */
#define R_MAX_LUMP_SIZE 2147483647L

/* --- Error codes --------------------------------------------------------- */

/*
 * One code per failure class, never a shared "bad lump" catch-all, so a caller
 * can tell a truncated lump from a corrupt one and from an output buffer that
 * was too small:
 *
 *   R_ERR_OK                     the call did what it says
 *   R_ERR_NULL_DATA              a required input pointer is NULL
 *   R_ERR_NULL_INFO              the info struct a call needs is NULL
 *   R_ERR_NULL_OUTPUT            the output pointer is NULL for a result with
 *                                at least one byte (a zero-size result accepts
 *                                NULL)
 *   R_ERR_NULL_CALLBACK          r_texture_compose() got no fetch callback
 *   R_ERR_BUFFER_TOO_SMALL       the output cannot hold the result; nothing was
 *                                written and requiredSize says what it needs
 *   R_ERR_LUMP_TOO_SMALL         the lump is shorter than the header it must
 *                                hold
 *   R_ERR_LUMP_TOO_LARGE         the lump cannot be addressed by the format's
 *                                signed 32-bit offsets
 *   R_ERR_PATCH_WIDTH            width < 1
 *   R_ERR_PATCH_HEIGHT           height < 1
 *   R_ERR_PATCH_COLUMN_TABLE     the header plus width*4 does not fit the lump
 *   R_ERR_PATCH_COLUMN_OFFSET    a column offset is before the column table or
 *                                at/after the end of the lump
 *   R_ERR_PATCH_POST_RANGE       a post's header or pixels extend past the end
 *                                of the lump
 *   R_ERR_PATCH_POST_BOUNDS      topdelta + length > height
 *   R_ERR_PATCH_COLUMN_END       a column chain has no 0xFF terminator inside
 *                                the lump, or the byte carrying the next
 *                                topdelta is outside it
 *   R_ERR_COLUMN_INDEX           the requested column is not in [0, width)
 *   R_ERR_TEXTURE_COUNT          numtextures is negative
 *   R_ERR_TEXTURE_DIRECTORY      the offset array or a scanned definition
 *                                offset does not fit the lump
 *   R_ERR_TEXTURE_NAME           the name is not in TEXTURE1 or TEXTURE2
 *   R_ERR_TEXTURE_HEADER         the matched definition's header or patch list
 *                                does not fit the lump
 *   R_ERR_TEXTURE_DIMENSION      the matched texture has width < 1 or
 *                                height < 1
 *   R_ERR_TEXTURE_PATCHCOUNT     the matched texture has patchcount < 0
 *   R_ERR_PNAMES_COUNT           nummappatches is negative
 *   R_ERR_PNAMES_RANGE           the PNAMES name array does not fit the lump
 *   R_ERR_PATCH_INDEX            a texture patch index is not in
 *                                [0, nummappatches)
 *   R_ERR_PATCH_FETCH            the fetch callback refused, or answered with
 *                                no bytes
 *   R_ERR_FLAT_SIZE              a flat is not exactly R_FLAT_BYTES long
 *
 * With a C99 compiler these are enumeration constants, not macros, so a
 * debugger can name the value and no call site needs a magic number.
 */
typedef enum r_error {
    R_ERR_OK = 0,
    R_ERR_NULL_DATA = 1,
    R_ERR_NULL_INFO = 2,
    R_ERR_NULL_OUTPUT = 3,
    R_ERR_NULL_CALLBACK = 4,
    R_ERR_BUFFER_TOO_SMALL = 5,
    R_ERR_LUMP_TOO_SMALL = 6,
    R_ERR_LUMP_TOO_LARGE = 7,
    R_ERR_PATCH_WIDTH = 8,
    R_ERR_PATCH_HEIGHT = 9,
    R_ERR_PATCH_COLUMN_TABLE = 10,
    R_ERR_PATCH_COLUMN_OFFSET = 11,
    R_ERR_PATCH_POST_RANGE = 12,
    R_ERR_PATCH_POST_BOUNDS = 13,
    R_ERR_PATCH_COLUMN_END = 14,
    R_ERR_COLUMN_INDEX = 15,
    R_ERR_TEXTURE_COUNT = 16,
    R_ERR_TEXTURE_DIRECTORY = 17,
    R_ERR_TEXTURE_NAME = 18,
    R_ERR_TEXTURE_HEADER = 19,
    R_ERR_TEXTURE_DIMENSION = 20,
    R_ERR_TEXTURE_PATCHCOUNT = 21,
    R_ERR_PNAMES_COUNT = 22,
    R_ERR_PNAMES_RANGE = 23,
    R_ERR_PATCH_INDEX = 24,
    R_ERR_PATCH_FETCH = 25,
    R_ERR_FLAT_SIZE = 26
} r_error_t;

/* --- Descriptors --------------------------------------------------------- */

/*
 * What r_patch_validate() proved about one patch lump. `columnTableEnd` is the
 * first byte a column may start at -- the header plus width * 4 -- and is the
 * lower bound every column offset was checked against, so a caller that wants
 * to know where the column data area begins reads it here.
 *
 * A successful validate fills every field. A rejected validate leaves the
 * struct untouched, so a caller that reuses one info across lumps can tell a
 * stale success from a fresh one by the return code only.
 */
typedef struct r_patch_info {
    int32_t width;          /* columns */
    int32_t height;         /* rows */
    int32_t leftOffset;     /* patch_t leftoffset, may be negative */
    int32_t topOffset;      /* patch_t topoffset, may be negative */
    size_t columnTableEnd;  /* first byte a column may start at */
} r_patch_info_t;

/*
 * What r_texture_lookup() proved about one texture definition. `lump` and
 * `lumpSize` are the caller's buffer and are not copied: the definition lives
 * inside it, so the buffer must stay alive and unchanged for as long as the
 * info is used. `offset` is the definition's start inside that lump, and
 * `source` is 1 for TEXTURE1 and 2 for TEXTURE2 so a caller can report which
 * lump answered.
 */
typedef struct r_texture_info {
    const uint8_t* lump;    /* TEXTURE1 or TEXTURE2 bytes, caller owned */
    size_t lumpSize;        /* bytes available at lump */
    size_t offset;          /* definition start inside lump */
    int32_t index;          /* index inside that lump's directory */
    int32_t source;         /* 1 for TEXTURE1, 2 for TEXTURE2 */
    char name[R_TEXTURE_NAME_SIZE + 1]; /* NUL-terminated copy of the field */
    int32_t masked;         /* non-zero when the definition says masked */
    int32_t width;          /* columns */
    int32_t height;         /* rows */
    int32_t patchCount;     /* entries in the patch list */
} r_texture_info_t;

/*
 * Resolves a PNAMES name to the patch lump that carries it.
 *
 * The name handed to the callback is the 8-byte PNAMES field copied into a
 * 9-byte buffer with a NUL terminator, so a caller can pass it straight to
 * w_wad's W_CheckNumForName(). It is written upper case in the shareware WAD
 * and may be lower case in a PWAD; the callback owns any case folding, exactly
 * as the WAD reader does.
 *
 * On success the callback returns R_ERR_OK and sets *data and *size to the
 * patch lump. It must answer the same name with the same bytes for the whole
 * duration of one r_texture_compose() call: the composition fetches every patch
 * twice (once to validate the lot before writing, once to blit).
 *
 * Any other return value, or R_ERR_OK with *data == NULL or *size == 0, is
 * reported by r_texture_compose() as R_ERR_PATCH_FETCH.
 */
typedef r_error_t (*r_patch_fetch_fn)(void* user, const char* name,
                                      const uint8_t** data, size_t* size);

/* --- Public API ---------------------------------------------------------- */

/**
 * @brief Validates one patch lump end to end and reports its geometry.
 *
 * Validation order, and the code each step reports:
 *
 *   1. info == NULL                              R_ERR_NULL_INFO
 *   2. data == NULL                              R_ERR_NULL_DATA
 *   3. size < R_PATCH_HEADER_SIZE                R_ERR_LUMP_TOO_SMALL
 *   4. size > R_MAX_LUMP_SIZE                    R_ERR_LUMP_TOO_LARGE
 *   5. width >= 1                                R_ERR_PATCH_WIDTH
 *   6. height >= 1                               R_ERR_PATCH_HEIGHT
 *   7. R_PATCH_HEADER_SIZE + width * 4 <= size   R_ERR_PATCH_COLUMN_TABLE
 *   8. per column, offset >= column table end
 *      and offset < size                          R_ERR_PATCH_COLUMN_OFFSET
 *   9. per post: the 3 header bytes are inside
 *      the lump                                   R_ERR_PATCH_POST_RANGE
 *  10. per post: the length byte's pixels are
 *      inside the lump                            R_ERR_PATCH_POST_RANGE
 *  11. per post: topdelta + length <= height      R_ERR_PATCH_POST_BOUNDS
 *  12. per post: the byte carrying the next
 *      topdelta is inside the lump                R_ERR_PATCH_COLUMN_END
 *  13. the chain reaches 0xFF                     R_ERR_PATCH_COLUMN_END
 *
 * leftoffset and topoffset are not range-checked: a sprite legitimately places
 * a patch with a negative origin, and applying them is the renderer's job. A
 * post of length zero is accepted, as DOOM accepts it; it draws nothing.
 *
 * @param data Patch lump bytes.
 * @param size Bytes available at @p data.
 * @param info Caller-owned descriptor; filled only on R_ERR_OK.
 * @return R_ERR_OK, or one of the codes above.
 */
r_error_t r_patch_validate(const uint8_t* data, size_t size, r_patch_info_t* info);

/**
 * @brief Materialises one column of an already-validated patch.
 *
 * The output is the normalised post chain documented at the top of this file:
 * `[topdelta][length][0][pixels...][0]` per post, then a single 0xFF. A column
 * that is empty in the lump decodes to the one byte 0xFF, so a successful call
 * never produces a zero-byte result. @p requiredSize receives that length on
 * success and on R_ERR_BUFFER_TOO_SMALL; the buffer is written only when it is
 * large enough, so a rejected call changes no byte of @p out.
 *
 * The patch must still be the one @p info validated: this function walks the
 * offsets @p info already proved and does not re-validate the other columns.
 *
 * Validation order:
 *
 *   1. info == NULL                              R_ERR_NULL_INFO
 *   2. data == NULL                              R_ERR_NULL_DATA
 *   3. column < 0 or column >= info->width       R_ERR_COLUMN_INDEX
 *   4. info->width's column table does not
 *      fit @p size                                R_ERR_PATCH_COLUMN_TABLE
 *   5. the column offset is not inside @p size   R_ERR_PATCH_COLUMN_OFFSET
 *   6. the chain does not decode inside @p size  R_ERR_PATCH_POST_RANGE /
 *                                                R_ERR_PATCH_POST_BOUNDS /
 *                                                R_ERR_PATCH_COLUMN_END
 *   7. out == NULL                               R_ERR_NULL_OUTPUT
 *   8. capacity < the column's decoded length    R_ERR_BUFFER_TOO_SMALL
 *
 * @param info   A patch successfully validated from @p data.
 * @param data   The same patch lump bytes.
 * @param size   Bytes available at @p data.
 * @param column Column index in [0, width).
 * @param out    Destination for the post chain.
 * @param capacity Bytes available at @p out.
 * @param requiredSize Optional out: the decoded length. Untouched when NULL.
 * @return R_ERR_OK, or one of the codes above.
 */
r_error_t r_patch_column(const r_patch_info_t* info, const uint8_t* data,
                         size_t size, int column, uint8_t* out, size_t capacity,
                         size_t* requiredSize);

/**
 * @brief Finds a texture by name in TEXTURE1 and/or TEXTURE2.
 *
 * The name is matched the way DOOM matches it: case-insensitively, on at most
 * the 8 bytes of the field, with the query treated as NUL padded once its own
 * terminator is reached. TEXTURE1 is scanned first and wins ties, and inside one
 * lump the first entry with the name wins -- the same order as DOOM's texture
 * array, which is built TEXTURE1 then TEXTURE2.
 *
 * DOOM's "NoTexture" marker (a name starting with '-') is *not* special-cased
 * here: R_CheckTextureNumForName() maps it to texture 0 before the lookup, and
 * that is the caller's decision, not a property of the directory.
 *
 * Validation order, and the code each step reports:
 *
 *   1. info == NULL                              R_ERR_NULL_INFO
 *   2. name == NULL                              R_ERR_NULL_DATA
 *   3. texture1 == NULL with size1 != 0, or
 *      texture2 == NULL with size2 != 0, or
 *      both lumps NULL                             R_ERR_NULL_DATA
 *   4. per lump passed, size >= 4 and
 *      numtextures >= 0                            R_ERR_LUMP_TOO_SMALL /
 *                                                  R_ERR_TEXTURE_COUNT
 *   5. per lump passed, the offset array fits       R_ERR_TEXTURE_DIRECTORY
 *   6. per scanned entry, its offset is past the
 *      array and inside the lump                   R_ERR_TEXTURE_DIRECTORY
 *   7. the matched definition: width and height
 *      are >= 1                                    R_ERR_TEXTURE_DIMENSION
 *   8. the matched definition: patchcount >= 0     R_ERR_TEXTURE_PATCHCOUNT
 *   9. the matched definition: header and patch
 *      list fit the lump                           R_ERR_TEXTURE_HEADER
 *  10. no entry matched                           R_ERR_TEXTURE_NAME
 *
 * Only the matched definition is validated beyond its directory offset; a
 * malformed definition the query never reaches cannot make an unrelated lookup
 * fail. Entries before the match are still walked, so a broken offset earlier
 * in the lump is reported.
 *
 * @param texture1 TEXTURE1 bytes, or NULL when size1 is 0.
 * @param size1    Bytes available at @p texture1.
 * @param texture2 TEXTURE2 bytes, or NULL when size2 is 0.
 * @param size2    Bytes available at @p texture2.
 * @param name     NUL-terminated ASCII name, in any case.
 * @param info     Caller-owned descriptor; filled only on R_ERR_OK.
 * @return R_ERR_OK, or one of the codes above.
 */
r_error_t r_texture_lookup(const uint8_t* texture1, size_t size1,
                           const uint8_t* texture2, size_t size2,
                           const char* name, r_texture_info_t* info);

/**
 * @brief Composes a texture's patches into one width x height surface.
 *
 * The surface is filled with R_TEXTURE_TRANSPARENT and, when @p mask is not
 * NULL, the mask with 0. Then every texture patch is fetched through
 * @p fetch and blitted at its origin, in patch order, so a later patch
 * overwrites an earlier one exactly as DOOM's R_GenerateComposite() does. Each
 * post lands at (originx, originy + topdelta) and is clipped to the texture
 * rectangle, exactly as R_DrawColumnInCache() clips it except for the vanilla
 * source-shift bug documented at the top of this file.
 *
 * The surface is column-major, exactly like DOOM's texturecomposite: pixel
 * (x, y) lives at pixels[x * height + y], so one texture column is contiguous
 * for the column drawer and R_GetColumn()'s composite form maps onto it without
 * a conversion.
 *
 * Every patch is fetched and validated in a first pass, before a byte of the
 * surface is written; a texture whose second patch is corrupt therefore leaves
 * the caller's buffer untouched. The one documented exception is the fetch
 * callback disagreeing with itself: if it answers the second pass with a
 * failure the call reports R_ERR_PATCH_FETCH with the surface partially drawn,
 * which the caller must discard. A callback that keeps its promise (same name,
 * same bytes, for the length of the call) cannot reach that path.
 *
 * requiredSize is width * height, the same length for the pixel surface and for
 * the mask, and is reported even when a buffer is too small. The mask is
 * optional: pass NULL and any size to skip it.
 *
 * Validation order, and the code each step reports:
 *
 *   1. info == NULL                              R_ERR_NULL_INFO
 *   2. info->lump == NULL, or pnames == NULL     R_ERR_NULL_DATA
 *   3. fetch == NULL                             R_ERR_NULL_CALLBACK
 *   4. width < 1 or height < 1                   R_ERR_TEXTURE_DIMENSION
 *   5. patchCount < 0                            R_ERR_TEXTURE_PATCHCOUNT
 *   6. the header and patch list do not fit
 *      info->lump                                R_ERR_TEXTURE_HEADER
 *   7. pnamesSize < 4                            R_ERR_LUMP_TOO_SMALL
 *   8. nummappatches < 0                         R_ERR_PNAMES_COUNT
 *   9. the name array does not fit pnames        R_ERR_PNAMES_RANGE
 *  10. pixels == NULL                            R_ERR_NULL_OUTPUT
 *  11. capacity, or maskCapacity when a mask is
 *      given, is smaller than width * height     R_ERR_BUFFER_TOO_SMALL
 *  12. a texture patch index outside PNAMES      R_ERR_PATCH_INDEX
 *  13. the fetch refused or answered no bytes    R_ERR_PATCH_FETCH
 *  14. a fetched patch does not validate         that patch's code
 *
 * Steps 12 to 14 run for every patch before the surface is written, so a
 * texture with any bad patch leaves the caller's buffers exactly as they were.
 *
 * @param info       A texture successfully returned by r_texture_lookup().
 * @param pnames     PNAMES lump bytes.
 * @param pnamesSize Bytes available at @p pnames.
 * @param fetch      Resolves a PNAMES name to a patch lump; must not be NULL.
 * @param user       Opaque pointer handed back to @p fetch.
 * @param pixels     Destination surface of width * height bytes.
 * @param capacity   Bytes available at @p pixels.
 * @param mask       Optional destination of width * height mask bytes.
 * @param maskCapacity Bytes available at @p mask; ignored when mask is NULL.
 * @param requiredSize Optional out: width * height. Untouched when NULL.
 * @return R_ERR_OK, or one of the codes above.
 */
r_error_t r_texture_compose(const r_texture_info_t* info, const uint8_t* pnames,
                            size_t pnamesSize, r_patch_fetch_fn fetch,
                            void* user, uint8_t* pixels, size_t capacity,
                            uint8_t* mask, size_t maskCapacity,
                            size_t* requiredSize);

/**
 * @brief Checks that a flat lump is the square of raw pixels DOOM expects.
 *
 * A flat has no header and no length field of its own: 4096 bytes are the whole
 * format. DOOM never checks, and a short flat read through a cached lump
 * pointer renders whatever follows it; this decoder refuses instead.
 *
 * Validation order:
 *
 *   1. data == NULL                               R_ERR_NULL_DATA
 *   2. size != R_FLAT_BYTES                       R_ERR_FLAT_SIZE
 *
 * @param data Flat lump bytes.
 * @param size Bytes available at @p data.
 * @return R_ERR_OK, or one of the codes above.
 */
r_error_t r_flat_validate(const uint8_t* data, size_t size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_R_DATA_H */
