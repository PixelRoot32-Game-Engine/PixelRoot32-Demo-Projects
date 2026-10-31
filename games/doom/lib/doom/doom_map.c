/*
 * The map-lump view of the DOOM port (doom_map.h).
 *
 * This file is the validating parser for the ten lumps of one level. It holds
 * no state of its own: every accessor reads the caller's bytes with explicit
 * little-endian reconstruction, so a record can be unaligned and the host can
 * be big-endian.
 *
 * The one design decision worth repeating here is that the validation is
 * whole-graph and up front. DOOM's P_SetupLevel() trusts the WAD and installs
 * pointers straight into it; a single bad index then surfaces as a wild read
 * inside the renderer, which on a board with no fault handler is a reboot with
 * no message. Every index the walk can follow -- line to vertex, seg to line,
 * side to sector, subsector to seg range, node to child -- is proved here
 * instead, once, and the walk's accessors can then treat an index as a fact.
 *
 * The source bytes are never copied. A record is decoded on access, which is a
 * few shifts per node and keeps the level out of the DRAM budget entirely.
 *
 * This is C99 only: no allocation, no globals, no stdio, no floating point.
 */
#include "doom_map.h"

/* --- Little-endian record readers ---------------------------------------- */

static int16_t read_i16(const uint8_t* at) {
    return (int16_t)((uint16_t)at[0] | ((uint16_t)at[1] << 8));
}

static uint16_t read_u16(const uint8_t* at) {
    return (uint16_t)((uint16_t)at[0] | ((uint16_t)at[1] << 8));
}

/* A map short widened to 16.16. The shift is done on the unsigned 16-bit value
 * so a negative coordinate is not a left shift of a negative signed integer
 * (undefined in C99); the bit pattern is identical. */
static fixed_t read_fixed16(const uint8_t* at) {
    return (fixed_t)((uint32_t)read_u16(at) << FRACBITS);
}

static const uint8_t* record_at(const doom_map_lump_view_t* lump, uint32_t index,
                                uint32_t stride) {
    return lump->data + (size_t)index * stride;
}

/* --- Open ---------------------------------------------------------------- */

static void clear_lump_views(doom_map_t* map) {
    int i;

    for (i = 0; i < DOOM_MAP_LUMP_COUNT; ++i) {
        map->lumps[i].data = NULL;
        map->lumps[i].size = 0;
        map->lumps[i].count = 0;
    }
    map->open = 0;
    map->error = DOOM_MAP_OK;
}

static uint32_t stride_for_lump(int lump) {
    switch (lump) {
        case DOOM_MAP_THINGS: return DOOM_THING_SIZE;
        case DOOM_MAP_LINEDEFS: return DOOM_LINEDEF_SIZE;
        case DOOM_MAP_SIDEDEFS: return DOOM_SIDEDEF_SIZE;
        case DOOM_MAP_VERTEXES: return DOOM_VERTEX_SIZE;
        case DOOM_MAP_SEGS: return DOOM_SEG_SIZE;
        case DOOM_MAP_SSECTORS: return DOOM_SSECTOR_SIZE;
        case DOOM_MAP_NODES: return DOOM_NODE_SIZE;
        case DOOM_MAP_SECTORS: return DOOM_SECTOR_SIZE;
        default: return 0; /* REJECT and BLOCKMAP are byte maps, not records */
    }
}

/* The seven lumps the renderer follows. THINGS is optional (a view can be
 * supplied directly), and REJECT/BLOCKMAP are never read by the walk. */
static int lump_is_required(int lump) {
    return lump == DOOM_MAP_LINEDEFS || lump == DOOM_MAP_SIDEDEFS ||
           lump == DOOM_MAP_VERTEXES || lump == DOOM_MAP_SEGS ||
           lump == DOOM_MAP_SSECTORS || lump == DOOM_MAP_SECTORS;
}

doom_map_error_t doom_map_open(doom_map_t* map, const doom_map_source_t* source) {
    doom_map_t built;
    uint32_t index;
    int lump;

    if (map == NULL) {
        return DOOM_MAP_ERR_NULL_MAP;
    }
    clear_lump_views(map);
    if (source == NULL) {
        map->error = DOOM_MAP_ERR_NULL_SOURCE;
        return map->error;
    }

    /* Steps 3-6: each lump's shape, then the seven the walk needs. */
    for (lump = 0; lump < DOOM_MAP_LUMP_COUNT; ++lump) {
        const uint32_t stride = stride_for_lump(lump);
        const size_t size = source->size[lump];
        const uint8_t* data = source->data[lump];

        if (data == NULL && size != 0) {
            map->error = DOOM_MAP_ERR_NULL_LUMP;
            return map->error;
        }
        if (stride != 0 && size % stride != 0) {
            map->error = DOOM_MAP_ERR_LUMP_SIZE;
            return map->error;
        }
        if (stride != 0 && size / stride > DOOM_MAP_MAX_ITEMS) {
            map->error = DOOM_MAP_ERR_ITEM_COUNT;
            return map->error;
        }
        if (stride == DOOM_NODE_SIZE && size / stride > DOOM_MAP_MAX_NODES) {
            map->error = DOOM_MAP_ERR_ITEM_COUNT;
            return map->error;
        }
        if (lump_is_required(lump) && size == 0) {
            map->error = DOOM_MAP_ERR_REQUIRED_LUMP;
            return map->error;
        }
    }

    clear_lump_views(&built);
    for (lump = 0; lump < DOOM_MAP_LUMP_COUNT; ++lump) {
        const uint32_t stride = stride_for_lump(lump);

        built.lumps[lump].data = source->data[lump];
        built.lumps[lump].size = source->size[lump];
        built.lumps[lump].count = stride == 0 ? 0u
                                              : (uint32_t)(source->size[lump] / stride);
    }

    /* Steps 7-8: linedefs. */
    for (index = 0; index < built.lumps[DOOM_MAP_LINEDEFS].count; ++index) {
        const uint8_t* line = record_at(&built.lumps[DOOM_MAP_LINEDEFS], index,
                                        DOOM_LINEDEF_SIZE);
        const int16_t side0 = read_i16(line + 10);
        const int16_t side1 = read_i16(line + 12);
        const int16_t flags = read_i16(line + 4);

        if (read_u16(line) >= built.lumps[DOOM_MAP_VERTEXES].count ||
            read_u16(line + 2) >= built.lumps[DOOM_MAP_VERTEXES].count) {
            map->error = DOOM_MAP_ERR_LINEDEF_VERTEX;
            return map->error;
        }
        if (side0 < -1 || side0 >= (int)built.lumps[DOOM_MAP_SIDEDEFS].count ||
            side1 < -1 || side1 >= (int)built.lumps[DOOM_MAP_SIDEDEFS].count) {
            map->error = DOOM_MAP_ERR_LINEDEF_SIDE;
            return map->error;
        }
        if ((flags & DOOM_ML_TWOSIDED) != 0 && (side0 < 0 || side1 < 0)) {
            map->error = DOOM_MAP_ERR_LINEDEF_SIDE;
            return map->error;
        }
    }

    /* Step 9: sidedefs. */
    for (index = 0; index < built.lumps[DOOM_MAP_SIDEDEFS].count; ++index) {
        const uint8_t* side = record_at(&built.lumps[DOOM_MAP_SIDEDEFS], index,
                                        DOOM_SIDEDEF_SIZE);
        const int16_t sector = read_i16(side + 28);

        if (sector < 0 || sector >= (int)built.lumps[DOOM_MAP_SECTORS].count) {
            map->error = DOOM_MAP_ERR_SIDEDEF_SECTOR;
            return map->error;
        }
    }

    /* Steps 10-12: segs. */
    for (index = 0; index < built.lumps[DOOM_MAP_SEGS].count; ++index) {
        const uint8_t* seg = record_at(&built.lumps[DOOM_MAP_SEGS], index,
                                       DOOM_SEG_SIZE);
        const uint16_t linedef = read_u16(seg + 6);
        const uint16_t side = read_u16(seg + 8);

        if (read_u16(seg) >= built.lumps[DOOM_MAP_VERTEXES].count ||
            read_u16(seg + 2) >= built.lumps[DOOM_MAP_VERTEXES].count) {
            map->error = DOOM_MAP_ERR_SEG_VERTEX;
            return map->error;
        }
        if (linedef >= built.lumps[DOOM_MAP_LINEDEFS].count) {
            map->error = DOOM_MAP_ERR_SEG_LINEDEF;
            return map->error;
        }
        if (side > 1) {
            map->error = DOOM_MAP_ERR_SEG_SIDE;
            return map->error;
        }
        {
            /* The sidedef this seg faces must exist: P_LoadSegs() would index
             * sides[-1] on a seg whose linedef does not name that side. */
            const uint8_t* line = record_at(&built.lumps[DOOM_MAP_LINEDEFS],
                                            linedef, DOOM_LINEDEF_SIZE);
            if (read_i16(line + 10 + (int)side * 2) < 0) {
                map->error = DOOM_MAP_ERR_LINEDEF_SIDE;
                return map->error;
            }
        }
    }

    /* Step 13: subsector seg ranges. */
    for (index = 0; index < built.lumps[DOOM_MAP_SSECTORS].count; ++index) {
        const uint8_t* subsector = record_at(&built.lumps[DOOM_MAP_SSECTORS],
                                             index, DOOM_SSECTOR_SIZE);
        const uint32_t first = read_u16(subsector + 2);
        const uint32_t count = read_u16(subsector);

        if (count == 0 || first + count > built.lumps[DOOM_MAP_SEGS].count) {
            map->error = DOOM_MAP_ERR_SUBSECTOR_RANGE;
            return map->error;
        }
    }

    /* Step 14: node children. */
    for (index = 0; index < built.lumps[DOOM_MAP_NODES].count; ++index) {
        const uint8_t* node = record_at(&built.lumps[DOOM_MAP_NODES], index,
                                        DOOM_NODE_SIZE);
        int child;

        for (child = 0; child < 2; ++child) {
            const uint16_t value = read_u16(node + 24 + child * 2);

            if ((value & R_NF_SUBSECTOR) != 0) {
                if ((uint32_t)(value & ~R_NF_SUBSECTOR) >=
                    built.lumps[DOOM_MAP_SSECTORS].count) {
                    map->error = DOOM_MAP_ERR_NODE_CHILD;
                    return map->error;
                }
            } else if ((uint32_t)value >= built.lumps[DOOM_MAP_NODES].count) {
                map->error = DOOM_MAP_ERR_NODE_CHILD;
                return map->error;
            }
        }
    }

    built.open = 1;
    built.error = DOOM_MAP_OK;
    *map = built;
    return DOOM_MAP_OK;
}

/* --- Accessors ----------------------------------------------------------- */

void doom_map_get_vertex(const doom_map_t* map, uint16_t index, doom_vertex_t* out) {
    const uint8_t* at = record_at(&map->lumps[DOOM_MAP_VERTEXES], index,
                                  DOOM_VERTEX_SIZE);

    out->x = read_fixed16(at);
    out->y = read_fixed16(at + 2);
}

void doom_map_get_thing(const doom_map_t* map, uint32_t index, doom_thing_t* out) {
    const uint8_t* at = record_at(&map->lumps[DOOM_MAP_THINGS], index,
                                  DOOM_THING_SIZE);

    out->x = read_i16(at);
    out->y = read_i16(at + 2);
    out->angle = read_i16(at + 4);
    out->type = read_i16(at + 6);
    out->options = read_i16(at + 8);
}

void doom_map_get_line(const doom_map_t* map, uint16_t index, doom_line_t* out) {
    const uint8_t* at = record_at(&map->lumps[DOOM_MAP_LINEDEFS], index,
                                  DOOM_LINEDEF_SIZE);
    const int16_t side0 = read_i16(at + 10);
    const int16_t side1 = read_i16(at + 12);

    out->v1 = read_u16(at);
    out->v2 = read_u16(at + 2);
    out->flags = read_i16(at + 4);
    /* special at +6 and tag at +8 are read as they are stored: DOOM validates
     * neither, and an unknown special is a no-op at run time. */
    out->special = read_i16(at + 6);
    out->tag = read_i16(at + 8);
    out->sideNum[0] = side0 < 0 ? DOOM_NO_SIDE_INDEX : (uint16_t)side0;
    out->sideNum[1] = side1 < 0 ? DOOM_NO_SIDE_INDEX : (uint16_t)side1;
}

void doom_map_get_side(const doom_map_t* map, uint16_t index, doom_side_t* out) {
    const uint8_t* at = record_at(&map->lumps[DOOM_MAP_SIDEDEFS], index,
                                  DOOM_SIDEDEF_SIZE);

    out->textureOffset = read_fixed16(at);
    out->rowOffset = read_fixed16(at + 2);
    out->topTexture = r_pack_name(at + 4);
    out->bottomTexture = r_pack_name(at + 12);
    out->midTexture = r_pack_name(at + 20);
    out->sector = read_u16(at + 28);
}

void doom_map_get_seg(const doom_map_t* map, uint16_t index, doom_seg_t* out) {
    const uint8_t* at = record_at(&map->lumps[DOOM_MAP_SEGS], index,
                                  DOOM_SEG_SIZE);

    out->v1 = read_u16(at);
    out->v2 = read_u16(at + 2);
    out->angle = (uint32_t)read_u16(at + 4) << 16;
    out->linedef = read_u16(at + 6);
    out->side = read_u16(at + 8);
    out->offset = read_fixed16(at + 10);
}

void doom_map_get_subsector(const doom_map_t* map, uint16_t index,
                            doom_subsector_t* out) {
    const uint8_t* at = record_at(&map->lumps[DOOM_MAP_SSECTORS], index,
                                  DOOM_SSECTOR_SIZE);

    out->numSegs = read_u16(at);
    out->firstSeg = read_u16(at + 2);
}

void doom_map_get_node(const doom_map_t* map, uint16_t index, doom_node_t* out) {
    const uint8_t* at = record_at(&map->lumps[DOOM_MAP_NODES], index,
                                  DOOM_NODE_SIZE);
    int child;
    int axis;

    out->x = read_fixed16(at);
    out->y = read_fixed16(at + 2);
    out->dx = read_fixed16(at + 4);
    out->dy = read_fixed16(at + 6);
    for (child = 0; child < 2; ++child) {
        for (axis = 0; axis < 4; ++axis) {
            out->bbox[child][axis] = read_fixed16(at + 8 + child * 8 + axis * 2);
        }
        out->children[child] = read_u16(at + 24 + child * 2);
    }
}

void doom_map_get_sector(const doom_map_t* map, uint16_t index,
                         doom_sector_t* out) {
    const uint8_t* at = record_at(&map->lumps[DOOM_MAP_SECTORS], index,
                                  DOOM_SECTOR_SIZE);

    out->floorHeight = read_fixed16(at);
    out->ceilingHeight = read_fixed16(at + 2);
    out->floorPic = r_pack_name(at + 4);
    out->ceilingPic = r_pack_name(at + 12);
    out->lightLevel = read_i16(at + 20);
    out->special = read_i16(at + 22);
    out->tag = read_i16(at + 24);
}

uint16_t doom_map_subsector_sector(const doom_map_t* map,
                                   const doom_subsector_t* subsector) {
    doom_seg_t seg;
    doom_line_t line;
    doom_side_t side;

    doom_map_get_seg(map, subsector->firstSeg, &seg);
    doom_map_get_line(map, seg.linedef, &line);
    doom_map_get_side(map, seg.side != 0 ? line.sideNum[1] : line.sideNum[0], &side);
    return side.sector;
}

int doom_map_seg_sectors(const doom_map_t* map, const doom_seg_t* seg,
                         uint16_t* front, uint16_t* back) {
    doom_line_t line;
    doom_side_t side;

    doom_map_get_line(map, seg->linedef, &line);
    doom_map_get_side(map, seg->side != 0 ? line.sideNum[1] : line.sideNum[0], &side);
    *front = side.sector;

    if ((line.flags & DOOM_ML_TWOSIDED) == 0) {
        return 0;
    }
    doom_map_get_side(map, seg->side != 0 ? line.sideNum[0] : line.sideNum[1], &side);
    if (back != NULL) {
        *back = side.sector;
    }
    return 1;
}
