/*
 * The frame's per-record state, as console lines (r_view_diag.h).
 *
 * Part of the PixelRoot32 port of DOOM. See NOTICE and LICENSE in this
 * directory for the provenance and the terms.
 *
 * This is C99 only: no allocation, no globals, no stdio, no floating point.
 */
#include "r_view_diag.h"

#include <stdint.h>

#include "r_segs.h"
#include "r_things.h"

/* --- A bounded line writer ------------------------------------------------ */

/*
 * Nothing here trusts the record it prints. A diagnostic that can write past
 * its buffer -- or read past an array while a frame is already wrong -- would
 * replace a rendering defect with a memory defect, so every append checks for
 * room (including the NUL) and the readers check their own index.
 */
typedef struct line_writer {
    char* text;
    size_t capacity;
    size_t length;
} line_writer_t;

static void writer_begin(line_writer_t* writer, char* out, size_t capacity) {
    writer->text = out;
    writer->capacity = capacity;
    writer->length = 0u;
    if (out != NULL && capacity > 0u) {
        out[0] = '\0';
    }
}

/// Appends one character when there is room for it and the NUL.
static void writer_char(line_writer_t* writer, char c) {
    if (writer->text == NULL || writer->length + 1u >= writer->capacity) {
        return;
    }
    writer->text[writer->length++] = c;
    writer->text[writer->length] = '\0';
}

static void writer_text(line_writer_t* writer, const char* text) {
    if (text == NULL) {
        return;
    }
    while (*text != '\0') {
        writer_char(writer, *text++);
    }
}

/// A decimal uint32, without the leading zeros printf would not add either.
static void writer_u32(line_writer_t* writer, uint32_t value) {
    char digits[11];
    int at = 0;

    do {
        digits[at++] = (char)('0' + (int)(value % 10u));
        value /= 10u;
    } while (value != 0u && at < (int)sizeof digits);
    while (at > 0) {
        writer_char(writer, digits[--at]);
    }
}

static void writer_i32(line_writer_t* writer, int32_t value) {
    if (value < 0) {
        writer_char(writer, '-');
        /* Negating INT32_MIN is undefined; take it through uint32. */
        writer_u32(writer, (uint32_t)0u - (uint32_t)value);
    } else {
        writer_u32(writer, (uint32_t)value);
    }
}

/// Eight uppercase hex digits: fixed width, so two captures align column-wise
/// and a fixed_t needs no sign convention of its own.
static void writer_hex8(line_writer_t* writer, uint32_t value) {
    static const char kDigits[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                     '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    int shift;

    for (shift = 28; shift >= 0; shift -= 4) {
        writer_char(writer, kDigits[(value >> shift) & 0xFu]);
    }
}

static void writer_sep(line_writer_t* writer) {
    writer_char(writer, ',');
}

/// A packed texture/flat/sprite name as its eight characters, or `-`.
static void writer_name(line_writer_t* writer, r_name_t name) {
    char text[9];
    int at;

    if (name == R_NAME_NONE) {
        writer_char(writer, '-');
        return;
    }
    r_unpack_name(name, text);
    for (at = 0; at < 8 && text[at] != '\0'; ++at) {
        writer_char(writer, text[at]);
    }
}

static int writer_finish(const line_writer_t* writer) {
    return (int)writer->length;
}

/* --- The lines ------------------------------------------------------------ */

int r_view_diag_config(const r_view_storage_t* storage, int callerViewHeight,
                       int callerCenterY, char* out, size_t capacity) {
    line_writer_t writer;

    if (storage == NULL) {
        return 0;
    }
    writer_begin(&writer, out, capacity);
    writer_text(&writer, "VD,cfg,");
    writer_i32(&writer, callerViewHeight);
    writer_sep(&writer);
    writer_i32(&writer, callerCenterY);
    writer_sep(&writer);
    /* lib/doom's own pair, from this translation unit's macros. */
    writer_i32(&writer, R_VIEW_HEIGHT);
    writer_sep(&writer);
    writer_i32(&writer, R_CENTER_Y);
    writer_sep(&writer);
    writer_i32(&writer, R_SCREEN_WIDTH);
    writer_sep(&writer);
    writer_i32(&writer, (int32_t)storage->drawsegCapacity);
    writer_sep(&writer);
    writer_i32(&writer, (int32_t)storage->planes.capacity);
    writer_sep(&writer);
    writer_i32(&writer, (int32_t)storage->clipPoolCapacity);
    writer_sep(&writer);
    writer_i32(&writer, (int32_t)storage->visSpriteCapacity);
    return writer_finish(&writer);
}

int r_view_diag_pool(const r_view_state_t* state, char* out, size_t capacity) {
    line_writer_t writer;

    if (state == NULL) {
        return 0;
    }
    writer_begin(&writer, out, capacity);
    writer_text(&writer, "VD,pool,");
    writer_i32(&writer, (int32_t)state->clipPoolEntries);
    writer_sep(&writer);
    writer_i32(&writer, (int32_t)state->clipPoolUsed);
    writer_sep(&writer);
    writer_i32(&writer, (int32_t)state->drawsegCount);
    writer_sep(&writer);
    writer_i32(&writer, (int32_t)state->storage.planes.count);
    return writer_finish(&writer);
}

int r_view_diag_drawseg(const r_view_state_t* state, unsigned index, char* out,
                        size_t capacity) {
    const r_drawseg_t* drawseg;
    line_writer_t writer;

    if (state == NULL || state->storage.drawsegs == NULL) {
        return 0;
    }
    if (index >= (unsigned)state->drawsegCount) {
        return 0;
    }
    drawseg = &state->storage.drawsegs[index];

    writer_begin(&writer, out, capacity);
    writer_text(&writer, "VD,d,");
    writer_u32(&writer, index);
    writer_sep(&writer);
    writer_u32(&writer, (uint32_t)drawseg->seg);
    writer_sep(&writer);
    /* The masked mid's texture: the fragment seam answers the same name the
     * masked pass will resolve, so a wrong picture is named, not guessed. */
    if (state->map != NULL && drawseg->maskedTextureCol != R_DRAWSEG_NO_BLOCK) {
        r_wall_fragment_t fragment;

        r_wall_fragment_open(state->map, &state->params, drawseg->seg, &fragment);
        writer_name(&writer, fragment.midTexture);
    } else {
        writer_char(&writer, '-');
    }
    writer_sep(&writer);
    writer_i32(&writer, (int32_t)drawseg->x1);
    writer_sep(&writer);
    writer_i32(&writer, (int32_t)drawseg->x2);
    writer_sep(&writer);
    writer_hex8(&writer, (uint32_t)drawseg->scale1);
    writer_sep(&writer);
    writer_hex8(&writer, (uint32_t)drawseg->scale2);
    writer_sep(&writer);
    writer_hex8(&writer, (uint32_t)drawseg->scaleStep);
    writer_sep(&writer);
    writer_i32(&writer, (int32_t)drawseg->maskedTextureCol);
    writer_sep(&writer);
    writer_i32(&writer, (int32_t)drawseg->sprTopClip);
    writer_sep(&writer);
    writer_i32(&writer, (int32_t)drawseg->sprBottomClip);
    writer_sep(&writer);
    writer_i32(&writer, (int32_t)drawseg->silhouette);
    writer_sep(&writer);
    writer_i32(&writer, (int32_t)drawseg->lightNum);
    return writer_finish(&writer);
}

int r_view_diag_plane(const r_view_state_t* state, unsigned index, char* out,
                      size_t capacity) {
    const r_visplane_t* plane;
    line_writer_t writer;
    int spans = 0;
    int topMin = R_PLANE_EMPTY;
    int topMax = R_PLANE_EMPTY;
    int bottomMin = R_PLANE_EMPTY;
    int bottomMax = R_PLANE_EMPTY;
    int x;

    if (state == NULL || state->storage.planes.planes == NULL) {
        return 0;
    }
    if (index >= (unsigned)state->storage.planes.count) {
        return 0;
    }
    plane = &state->storage.planes.planes[index];

    /* The extrema of the recorded columns: a plane whose top[] was written by
     * something else shows it here even when its identity (height/pic/light)
     * is right, which is the case a diff of the header alone would miss. */
    for (x = 0; x < R_SCREEN_WIDTH; ++x) {
        const int top = (int)plane->top[x];
        const int bottom = (int)plane->bottom[x];

        if (top == R_PLANE_EMPTY) {
            continue;
        }
        ++spans;
        if (topMin == R_PLANE_EMPTY || top < topMin) {
            topMin = top;
        }
        if (topMax == R_PLANE_EMPTY || top > topMax) {
            topMax = top;
        }
        if (bottomMin == R_PLANE_EMPTY || bottom < bottomMin) {
            bottomMin = bottom;
        }
        if (bottomMax == R_PLANE_EMPTY || bottom > bottomMax) {
            bottomMax = bottom;
        }
    }

    writer_begin(&writer, out, capacity);
    writer_text(&writer, "VD,p,");
    writer_u32(&writer, index);
    writer_sep(&writer);
    writer_hex8(&writer, (uint32_t)plane->height);
    writer_sep(&writer);
    writer_name(&writer, plane->picNum);
    writer_sep(&writer);
    writer_i32(&writer, (int32_t)plane->lightLevel);
    writer_sep(&writer);
    writer_i32(&writer, (int32_t)plane->minX);
    writer_sep(&writer);
    writer_i32(&writer, (int32_t)plane->maxX);
    writer_sep(&writer);
    writer_i32(&writer, spans);
    writer_sep(&writer);
    writer_i32(&writer, topMin);
    writer_sep(&writer);
    writer_i32(&writer, topMax);
    writer_sep(&writer);
    writer_i32(&writer, bottomMin);
    writer_sep(&writer);
    writer_i32(&writer, bottomMax);
    return writer_finish(&writer);
}

int r_view_diag_sprite(const r_sprite_t* sprites, unsigned count, unsigned index,
                       char* out, size_t capacity) {
    const r_sprite_t* sprite;
    line_writer_t writer;

    if (sprites == NULL || index >= count) {
        return 0;
    }
    sprite = &sprites[index];

    writer_begin(&writer, out, capacity);
    writer_text(&writer, "VD,s,");
    writer_u32(&writer, index);
    writer_sep(&writer);
    writer_hex8(&writer, (uint32_t)sprite->x);
    writer_sep(&writer);
    writer_hex8(&writer, (uint32_t)sprite->y);
    writer_sep(&writer);
    writer_hex8(&writer, (uint32_t)sprite->z);
    writer_sep(&writer);
    writer_hex8(&writer, (uint32_t)sprite->angle);
    writer_sep(&writer);
    writer_i32(&writer, (int32_t)sprite->lightLevel);
    writer_sep(&writer);
    writer_name(&writer, sprite->lump);
    return writer_finish(&writer);
}
