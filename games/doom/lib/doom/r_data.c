/*
 * The picture decoders: see r_data.h for the three formats, the validation
 * order of every entry point and the column form this file produces.
 *
 * Three properties are worth naming here because they are why the code looks
 * the way it does:
 *
 *  - every integer is reconstructed from its bytes in explicit little-endian
 *    order, and the signed ones are converted without relying on a wrap of an
 *    out-of-range value. memcpy into an integer and a cast of the buffer to a
 *    wider type both assume alignment and host byte order, and a lump inside a
 *    flash mapping is not guaranteed to be aligned;
 *  - one post walker serves all three users: validation, column materialisation
 *    and the composite blit. Bounds, the height rule and the "the chain must end
 *    inside the lump" rule therefore cannot drift apart between them;
 *  - no call writes before it has proved the whole structure it is about to
 *    draw. The composition even fetches and validates every patch before it
 *    fills the surface, which is why a corrupt second patch leaves the caller's
 *    buffer byte for byte as it was.
 *
 * Compiled by the C compiler under the firmware's C99 flags; no C++ construct,
 * no allocation, no stdio, no floating point, no mutable global.
 */
#include "r_data.h"

/* --- Little-endian field readers ----------------------------------------- */

static uint16_t readUint16(const uint8_t* at) {
    return (uint16_t)((uint16_t)at[0] | (uint16_t)((uint16_t)at[1] << 8));
}

static uint32_t readUint32(const uint8_t* at) {
    return (uint32_t)at[0]
         | ((uint32_t)at[1] << 8)
         | ((uint32_t)at[2] << 16)
         | ((uint32_t)at[3] << 24);
}

/* Signed 16-bit, without casting a value above INT16_MAX to int16_t: that
 * conversion is implementation-defined and a decoder should not depend on it.
 * ~value & 0xFFFF is the 16-bit complement, which is at most 0x7FFF here. */
static int32_t readInt16(const uint8_t* at) {
    const uint32_t value = (uint32_t)readUint16(at);

    if ((value & 0x8000ul) != 0ul) {
        return -(int32_t)(~value & 0xFFFFul) - 1;
    }

    return (int32_t)value;
}

/* Signed 32-bit, same reasoning: ~value is at most 0x7FFFFFFF when the sign bit
 * is set, so the cast and the negation are both in range. */
static int32_t readInt32(const uint8_t* at) {
    const uint32_t value = readUint32(at);

    if ((value & 0x80000000ul) != 0ul) {
        return -(int32_t)(~value) - 1;
    }

    return (int32_t)value;
}

/* ASCII upper case for name queries. The 1993 format stores names upper case
 * and the port does not pull in <ctype.h>, which is locale-dependent. */
static unsigned char upperAscii(unsigned char character) {
    if (character >= (unsigned char)'a' && character <= (unsigned char)'z') {
        return (unsigned char)(character - 32u);
    }

    return character;
}

/* Compares a query against a fixed 8-byte name field the way this port's WAD
 * readers do: exactly the 8 field bytes are compared, the query is upper-cased,
 * and once the query's own terminator is reached the rest of the query counts as
 * NUL, so the field must be NUL padded there. A query longer than 8 is compared
 * on its first 8 characters. */
static int nameMatchesField(const char* name, const uint8_t* field) {
    size_t index;
    int terminated = 0;

    for (index = 0; index < (size_t)R_TEXTURE_NAME_SIZE; ++index) {
        unsigned char query = 0u;

        if (terminated == 0) {
            query = (unsigned char)name[index];
            if (query == 0u) {
                terminated = 1;
            } else {
                query = upperAscii(query);
            }
        }

        if (query != field[index]) {
            return 0;
        }
    }

    return 1;
}

/* --- The post walker ----------------------------------------------------- */

/*
 * Follows one column chain from `start` to its 0xFF terminator.
 *
 * The rules it enforces are the patch format's: the three header bytes are
 * inside the lump, the post's pixels are inside the lump, topdelta + length does
 * not pass the patch height, and the byte that carries the next topdelta -- the
 * unused byte plus the terminator or the following post -- is inside the lump.
 * Because a post advances by length + 4 the loop always makes progress.
 *
 * `visit` is called for every post with its top delta, its length and a pointer
 * to its first pixel. `decodedLength` receives the size of the normalised column
 * this file produces: sum(length + 4) + 1 for the terminator.
 */
typedef void (*r_post_visit_fn)(void* user, uint32_t topDelta, uint32_t length,
                               const uint8_t* pixels);

static r_error_t walkColumn(const uint8_t* data, size_t size, size_t start,
                            uint32_t height, r_post_visit_fn visit, void* user,
                            size_t* decodedLength) {
    size_t at = start;
    size_t total = 1u; /* the terminator */

    for (;;) {
        uint32_t topDelta;
        uint32_t length;

        if (at >= size) {
            return R_ERR_PATCH_COLUMN_END;
        }

        /* The terminator is one byte, so it is read before the post header is
         * required: a column may end exactly at the last byte of the lump. */
        topDelta = (uint32_t)data[at];
        if (topDelta == (uint32_t)R_PATCH_POST_END) {
            break;
        }

        if (at + (size_t)R_PATCH_POST_HEADER_SIZE > size) {
            return R_ERR_PATCH_POST_RANGE;
        }

        length = (uint32_t)data[at + 1u];
        if (at + (size_t)R_PATCH_POST_HEADER_SIZE + (size_t)length > size) {
            return R_ERR_PATCH_POST_RANGE;
        }
        if (topDelta + length > height) {
            return R_ERR_PATCH_POST_BOUNDS;
        }
        if (at + (size_t)length + (size_t)R_PATCH_POST_HEADER_SIZE
                + (size_t)R_PATCH_POST_TRAILER_SIZE >= size) {
            return R_ERR_PATCH_COLUMN_END;
        }

        if (visit != NULL) {
            visit(user, topDelta, length, data + at + (size_t)R_PATCH_POST_HEADER_SIZE);
        }

        at += (size_t)length + (size_t)R_PATCH_POST_HEADER_SIZE
            + (size_t)R_PATCH_POST_TRAILER_SIZE;
        total += (size_t)length + (size_t)R_PATCH_POST_HEADER_SIZE
               + (size_t)R_PATCH_POST_TRAILER_SIZE;
    }

    if (decodedLength != NULL) {
        *decodedLength = total;
    }

    return R_ERR_OK;
}

/* Address of one column offset inside a validated patch. */
static size_t columnOffsetAt(int column) {
    return (size_t)R_PATCH_HEADER_SIZE
         + (size_t)column * (size_t)R_PATCH_COLUMN_ENTRY_SIZE;
}

/* --- r_patch_validate ---------------------------------------------------- */

r_error_t r_patch_validate(const uint8_t* data, size_t size, r_patch_info_t* info) {
    int32_t width;
    int32_t height;
    size_t columnTableEnd;
    int column;

    if (info == NULL) {
        return R_ERR_NULL_INFO;
    }
    if (data == NULL) {
        return R_ERR_NULL_DATA;
    }
    if (size < (size_t)R_PATCH_HEADER_SIZE) {
        return R_ERR_LUMP_TOO_SMALL;
    }
    if (size > (size_t)R_MAX_LUMP_SIZE) {
        return R_ERR_LUMP_TOO_LARGE;
    }

    width = readInt16(data);
    height = readInt16(data + 2);
    if (width < 1) {
        return R_ERR_PATCH_WIDTH;
    }
    if (height < 1) {
        return R_ERR_PATCH_HEIGHT;
    }

    columnTableEnd = (size_t)R_PATCH_HEADER_SIZE
                   + (size_t)width * (size_t)R_PATCH_COLUMN_ENTRY_SIZE;
    if (columnTableEnd > size) {
        return R_ERR_PATCH_COLUMN_TABLE;
    }

    for (column = 0; column < (int)width; ++column) {
        const int32_t offset = readInt32(data + columnOffsetAt(column));
        r_error_t error;

        if (offset < 0 || (size_t)offset < columnTableEnd
                || (size_t)offset >= size) {
            return R_ERR_PATCH_COLUMN_OFFSET;
        }

        error = walkColumn(data, size, (size_t)offset, (uint32_t)height, NULL,
                           NULL, NULL);
        if (error != R_ERR_OK) {
            return error;
        }
    }

    info->width = width;
    info->height = height;
    info->leftOffset = readInt16(data + 4);
    info->topOffset = readInt16(data + 6);
    info->columnTableEnd = columnTableEnd;

    return R_ERR_OK;
}

/* --- r_patch_column ------------------------------------------------------ */

/* The visitor that writes the normalised post chain. */
typedef struct r_column_writer {
    uint8_t* out;
    size_t at;
} r_column_writer_t;

static void writePost(void* user, uint32_t topDelta, uint32_t length,
                      const uint8_t* pixels) {
    r_column_writer_t* writer = (r_column_writer_t*)user;
    uint32_t index;

    writer->out[writer->at] = (uint8_t)topDelta;
    ++writer->at;
    writer->out[writer->at] = (uint8_t)length;
    ++writer->at;
    writer->out[writer->at] = (uint8_t)R_PATCH_UNUSED_VALUE;
    ++writer->at;

    for (index = 0; index < length; ++index) {
        writer->out[writer->at] = pixels[index];
        ++writer->at;
    }

    writer->out[writer->at] = (uint8_t)R_PATCH_UNUSED_VALUE;
    ++writer->at;
}

r_error_t r_patch_column(const r_patch_info_t* info, const uint8_t* data,
                         size_t size, int column, uint8_t* out, size_t capacity,
                         size_t* requiredSize) {
    size_t start;
    size_t decodedLength = 0u;
    r_error_t error;
    r_column_writer_t writer;

    if (info == NULL) {
        return R_ERR_NULL_INFO;
    }
    if (data == NULL) {
        return R_ERR_NULL_DATA;
    }
    if (column < 0 || column >= info->width) {
        return R_ERR_COLUMN_INDEX;
    }
    if (size < (size_t)R_PATCH_HEADER_SIZE
            || (size - (size_t)R_PATCH_HEADER_SIZE)
                   / (size_t)R_PATCH_COLUMN_ENTRY_SIZE < (size_t)info->width) {
        /* The caller handed a shorter buffer than `info` was validated against;
         * the column table alone does not fit, so no offset may be read. */
        return R_ERR_PATCH_COLUMN_TABLE;
    }

    start = (size_t)readInt32(data + columnOffsetAt(column));
    if (start >= size) {
        /* Only reachable when the caller passes a shorter buffer than the one
         * `info` was validated against. */
        return R_ERR_PATCH_COLUMN_OFFSET;
    }

    /* First walk: the decoded length, so a short buffer writes nothing. */
    error = walkColumn(data, size, start, (uint32_t)info->height, NULL, NULL,
                       &decodedLength);
    if (error != R_ERR_OK) {
        return error;
    }
    if (requiredSize != NULL) {
        *requiredSize = decodedLength;
    }

    if (out == NULL) {
        return R_ERR_NULL_OUTPUT;
    }
    if (capacity < decodedLength) {
        return R_ERR_BUFFER_TOO_SMALL;
    }

    /* Second walk: the bytes. */
    writer.out = out;
    writer.at = 0u;
    error = walkColumn(data, size, start, (uint32_t)info->height, writePost,
                       &writer, NULL);
    if (error != R_ERR_OK) {
        return error;
    }

    out[writer.at] = (uint8_t)R_PATCH_POST_END;

    return R_ERR_OK;
}

/* --- r_texture_lookup ---------------------------------------------------- */

/* Validates one texture lump's header and directory, then scans it for a name.
 * Returns R_ERR_TEXTURE_NAME when the name is absent, a structural code when the
 * lump is malformed, and R_ERR_OK with `info` filled on a match. */
static r_error_t lookupInLump(const uint8_t* lump, size_t size, int32_t source,
                              const char* name, r_texture_info_t* info) {
    int32_t count;
    size_t directoryEnd;
    int32_t index;

    if (size < (size_t)R_TEXTURE_COUNT_SIZE) {
        return R_ERR_LUMP_TOO_SMALL;
    }
    if (size > (size_t)R_MAX_LUMP_SIZE) {
        return R_ERR_LUMP_TOO_LARGE;
    }

    count = readInt32(lump);
    if (count < 0) {
        return R_ERR_TEXTURE_COUNT;
    }
    /* Compare before multiplying: on a 32-bit size_t (the target) a huge count
     * times four would wrap to a small number and pass a naive check. `size` is
     * at least the count field, proved above, so the subtraction is safe. */
    if ((size_t)count > (size - (size_t)R_TEXTURE_COUNT_SIZE)
                            / (size_t)R_TEXTURE_DIRECTORY_ENTRY_SIZE) {
        return R_ERR_TEXTURE_DIRECTORY;
    }

    directoryEnd = (size_t)R_TEXTURE_COUNT_SIZE
                 + (size_t)count * (size_t)R_TEXTURE_DIRECTORY_ENTRY_SIZE;

    for (index = 0; index < count; ++index) {
        const size_t at = (size_t)R_TEXTURE_COUNT_SIZE
                        + (size_t)index * (size_t)R_TEXTURE_DIRECTORY_ENTRY_SIZE;
        const int32_t offset = readInt32(lump + at);
        size_t definition;
        int32_t width;
        int32_t height;
        int32_t patchCount;
        size_t definitionEnd;
        char field[R_TEXTURE_NAME_SIZE + 1];
        size_t byte;

        if (offset < 0 || (size_t)offset < directoryEnd
                || (size_t)offset >= size) {
            return R_ERR_TEXTURE_DIRECTORY;
        }
        definition = (size_t)offset;

        if (definition + (size_t)R_TEXTURE_HEADER_SIZE > size) {
            return R_ERR_TEXTURE_HEADER;
        }

        for (byte = 0; byte < (size_t)R_TEXTURE_NAME_SIZE; ++byte) {
            field[byte] = (char)lump[definition + byte];
        }
        field[R_TEXTURE_NAME_SIZE] = '\0';

        if (nameMatchesField(name, lump + definition) == 0) {
            continue;
        }

        width = readInt16(lump + definition + 12u);
        height = readInt16(lump + definition + 14u);
        patchCount = readInt16(lump + definition + 20u);
        if (width < 1 || height < 1) {
            return R_ERR_TEXTURE_DIMENSION;
        }
        if (patchCount < 0) {
            return R_ERR_TEXTURE_PATCHCOUNT;
        }

        definitionEnd = definition + (size_t)R_TEXTURE_HEADER_SIZE
                      + (size_t)patchCount * (size_t)R_TEXTURE_PATCH_SIZE;
        if (definitionEnd > size) {
            return R_ERR_TEXTURE_HEADER;
        }

        info->lump = lump;
        info->lumpSize = size;
        info->offset = definition;
        info->index = index;
        info->source = source;
        info->masked = readInt32(lump + definition + 8u);
        info->width = width;
        info->height = height;
        info->patchCount = patchCount;
        for (byte = 0; byte < (size_t)R_TEXTURE_NAME_SIZE + 1u; ++byte) {
            info->name[byte] = field[byte];
        }

        return R_ERR_OK;
    }

    return R_ERR_TEXTURE_NAME;
}

r_error_t r_texture_lookup(const uint8_t* texture1, size_t size1,
                           const uint8_t* texture2, size_t size2,
                           const char* name, r_texture_info_t* info) {
    r_error_t error;

    if (info == NULL) {
        return R_ERR_NULL_INFO;
    }
    if (name == NULL) {
        return R_ERR_NULL_DATA;
    }
    if ((texture1 == NULL && size1 != 0u) || (texture2 == NULL && size2 != 0u)) {
        return R_ERR_NULL_DATA;
    }
    if (texture1 == NULL && texture2 == NULL) {
        return R_ERR_NULL_DATA;
    }

    if (texture1 != NULL) {
        error = lookupInLump(texture1, size1, 1, name, info);
        if (error != R_ERR_TEXTURE_NAME) {
            return error;
        }
    }
    if (texture2 != NULL) {
        return lookupInLump(texture2, size2, 2, name, info);
    }

    return R_ERR_TEXTURE_NAME;
}

/* --- r_texture_compose --------------------------------------------------- */

/* Reads patch entry `index` of a definition that was already range-checked. */
static const uint8_t* texturePatch(const r_texture_info_t* info, int32_t index) {
    return info->lump + info->offset + (size_t)R_TEXTURE_HEADER_SIZE
         + (size_t)index * (size_t)R_TEXTURE_PATCH_SIZE;
}

/* Copies the 8-byte PNAMES name of one patch index into a NUL-terminated buffer
 * for the fetch callback. */
static void patchNameAt(const uint8_t* pnames, int32_t index, char* out) {
    size_t byte;

    for (byte = 0; byte < (size_t)R_PATCH_NAME_SIZE; ++byte) {
        out[byte] = (char)pnames[(size_t)R_PNAMES_HEADER_SIZE
                               + (size_t)index * (size_t)R_PATCH_NAME_SIZE + byte];
    }
    out[R_PATCH_NAME_SIZE] = '\0';
}

/* One column of the surface being composed. */
typedef struct r_blit_target {
    uint8_t* pixels;
    uint8_t* mask;
    size_t height;
    size_t column;
    int32_t originY;
} r_blit_target_t;

/*
 * Blits one post into the target column.
 *
 * Clipping matches R_DrawColumnInCache() -- a post that starts above the texture
 * loses the pixels that are out of range, and one that runs past the bottom is
 * shortened -- with one deliberate exception: the source pointer advances with
 * the clipped amount, so the post's pixels land at rows originy + topdelta as
 * the format says. DOOM's original copies from the first source byte and shifts
 * the visible part up; that is a bug, and r_data.h documents the choice, the
 * seven shareware textures it affects and why the comparison tool cannot prove
 * either side matches vanilla here.
 */
static void blitPost(void* user, uint32_t topDelta, uint32_t length,
                     const uint8_t* pixels) {
    r_blit_target_t* target = (r_blit_target_t*)user;
    int32_t position = target->originY + (int32_t)topDelta;
    int32_t count = (int32_t)length;
    size_t destination;
    int32_t index;

    if (position < 0) {
        pixels -= position; /* the pixels above the surface are dropped */
        count += position;
        position = 0;
    }
    if (position + count > (int32_t)target->height) {
        count = (int32_t)target->height - position;
    }
    if (count <= 0) {
        return;
    }

    destination = target->column * target->height + (size_t)position;
    for (index = 0; index < count; ++index) {
        target->pixels[destination + (size_t)index] = pixels[index];
        if (target->mask != NULL) {
            target->mask[destination + (size_t)index] = (uint8_t)R_TEXTURE_OPAQUE;
        }
    }
}

/* Fetches and validates one texture patch, without drawing it. */
static r_error_t validateTexturePatch(const r_texture_info_t* info, int32_t index,
                                      const uint8_t* pnames, r_patch_fetch_fn fetch,
                                      void* user, r_patch_info_t* out) {
    const int32_t patchIndex = readInt16(texturePatch(info, index) + 4);
    const uint8_t* data = NULL;
    size_t size = 0u;
    char name[R_PATCH_NAME_SIZE + 1];
    r_error_t error;

    if (patchIndex < 0) {
        return R_ERR_PATCH_INDEX;
    }

    patchNameAt(pnames, patchIndex, name);
    error = fetch(user, name, &data, &size);
    if (error != R_ERR_OK || data == NULL || size == 0u) {
        return R_ERR_PATCH_FETCH;
    }

    return r_patch_validate(data, size, out);
}

r_error_t r_texture_compose(const r_texture_info_t* info, const uint8_t* pnames,
                            size_t pnamesSize, r_patch_fetch_fn fetch,
                            void* user, uint8_t* pixels, size_t capacity,
                            uint8_t* mask, size_t maskCapacity,
                            size_t* requiredSize) {
    size_t surface;
    size_t byte;
    int32_t pnamesCount;
    int32_t index;
    r_error_t error;

    if (info == NULL) {
        return R_ERR_NULL_INFO;
    }
    if (info->lump == NULL || pnames == NULL) {
        return R_ERR_NULL_DATA;
    }
    if (fetch == NULL) {
        return R_ERR_NULL_CALLBACK;
    }
    if (info->width < 1 || info->height < 1) {
        return R_ERR_TEXTURE_DIMENSION;
    }
    if (info->patchCount < 0) {
        return R_ERR_TEXTURE_PATCHCOUNT;
    }
    if (info->offset > info->lumpSize
            || info->lumpSize - info->offset
                   < (size_t)R_TEXTURE_HEADER_SIZE
                   + (size_t)info->patchCount * (size_t)R_TEXTURE_PATCH_SIZE) {
        return R_ERR_TEXTURE_HEADER;
    }

    if (pnamesSize < (size_t)R_PNAMES_HEADER_SIZE) {
        return R_ERR_LUMP_TOO_SMALL;
    }
    pnamesCount = readInt32(pnames);
    if (pnamesCount < 0) {
        return R_ERR_PNAMES_COUNT;
    }
    /* Compare before multiplying, for the same 32-bit wrap reason as the
     * texture directory: count * 8 must not be allowed to fold. */
    if ((size_t)pnamesCount > (pnamesSize - (size_t)R_PNAMES_HEADER_SIZE)
                                / (size_t)R_PATCH_NAME_SIZE) {
        return R_ERR_PNAMES_RANGE;
    }

    surface = (size_t)info->width * (size_t)info->height;
    if (requiredSize != NULL) {
        *requiredSize = surface;
    }
    if (pixels == NULL) {
        return R_ERR_NULL_OUTPUT;
    }
    if (capacity < surface) {
        return R_ERR_BUFFER_TOO_SMALL;
    }
    if (mask != NULL && maskCapacity < surface) {
        return R_ERR_BUFFER_TOO_SMALL;
    }

    /* Pass one: every patch index, every fetch and every patch, before the
     * surface is touched. */
    for (index = 0; index < info->patchCount; ++index) {
        r_patch_info_t patch;

        if (readInt16(texturePatch(info, index) + 4) >= pnamesCount) {
            return R_ERR_PATCH_INDEX;
        }

        error = validateTexturePatch(info, index, pnames, fetch, user, &patch);
        if (error != R_ERR_OK) {
            return error;
        }
    }

    /* Fill, then draw. */
    for (byte = 0; byte < surface; ++byte) {
        pixels[byte] = (uint8_t)R_TEXTURE_TRANSPARENT;
        if (mask != NULL) {
            mask[byte] = 0u;
        }
    }

    for (index = 0; index < info->patchCount; ++index) {
        const uint8_t* entry = texturePatch(info, index);
        const int32_t originX = readInt16(entry);
        const int32_t originY = readInt16(entry + 2);
        const int32_t patchIndex = readInt16(entry + 4);
        const uint8_t* data = NULL;
        size_t size = 0u;
        char name[R_PATCH_NAME_SIZE + 1];
        r_patch_info_t patch;
        const int32_t x1 = originX < 0 ? 0 : originX;
        int32_t x2;
        int32_t x;

        patchNameAt(pnames, patchIndex, name);
        error = fetch(user, name, &data, &size);
        if (error != R_ERR_OK || data == NULL || size == 0u) {
            return R_ERR_PATCH_FETCH;
        }
        error = r_patch_validate(data, size, &patch);
        if (error != R_ERR_OK) {
            return error;
        }

        x2 = originX + patch.width;
        if (x2 > info->width) {
            x2 = info->width;
        }

        for (x = x1; x < x2; ++x) {
            const int32_t column = x - originX;
            r_blit_target_t target;

            target.pixels = pixels;
            target.mask = mask;
            target.height = (size_t)info->height;
            target.column = (size_t)x;
            target.originY = originY;

            error = walkColumn(data, size,
                               (size_t)readInt32(data + columnOffsetAt(column)),
                               (uint32_t)patch.height, blitPost, &target, NULL);
            if (error != R_ERR_OK) {
                return error;
            }
        }
    }

    return R_ERR_OK;
}

/* --- r_flat_validate ----------------------------------------------------- */

r_error_t r_flat_validate(const uint8_t* data, size_t size) {
    if (data == NULL) {
        return R_ERR_NULL_DATA;
    }
    if (size != R_FLAT_BYTES) {
        return R_ERR_FLAT_SIZE;
    }

    return R_ERR_OK;
}
