/*
 * A read-only view of one DOOM level's map lumps.
 *
 * The ten lumps are the 1993 on-disk format, little-endian and packed. This
 * layer validates the whole graph once -- every vertex, side, sector, seg,
 * subsector and node the renderer (and later the simulation) will follow -- and
 * then answers typed accessors over the caller's bytes. It never copies a
 * record, allocates, or asks where the bytes came from: on the board they are
 * inside the mapped WAD image (or a decoded lump buffer), in a host test they
 * are an array the test assembled byte for byte.
 *
 * WHAT IS READ, AND WHAT THE RENDERER ACTUALLY NEEDS
 *
 * A map is the marker lump (`E1M1`) followed by these lumps in this order:
 *
 *   THINGS LINEDEFS SIDEDEFS VERTEXES SEGS SSECTORS NODES SECTORS
 *   REJECT BLOCKMAP
 *
 * The BSP walk (r_bsp.c, D3.1) reads **NODES, SSECTORS, SEGS, SECTORS,
 * SIDEDEFS, LINEDEFS and VERTEXES**. It does not read REJECT or BLOCKMAP at
 * all: those are line-of-sight and collision inputs for the simulation, and
 * they are exposed here only so the loader can prove their sizes and so D4.x
 * does not have to reopen the map. THINGS is read by r_bsp_spawn_view() to find
 * the player start; a view supplied directly needs none of it.
 *
 * NODES may be empty. DOOM's R_PointInSubsector() special-cases a map with no
 * nodes (`if (!numnodes) return subsectors;`), and the walk mirrors that, so a
 * synthetic one-subsector map is legal.
 *
 * ZERO-COPY, AND WHAT THAT COSTS
 *
 * A record is parsed on access, with explicit little-endian reconstruction from
 * bytes. That is a handful of shifts per node, and it is deliberate: a node is
 * visited once per view, and copying the map into a runtime struct would cost
 * several KB of DRAM on a board where the world budget is about 137 KB. The
 * source buffer must stay alive and unchanged for as long as the map view is.
 *
 * This is C99 only: <stddef.h> and <stdint.h>, no allocation, no globals, no
 * stdio, no floating point. The caller owns the one state struct.
 */
#ifndef DOOM_MAP_H
#define DOOM_MAP_H

#include <stddef.h>
#include <stdint.h>

#include "r_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Lump order and record sizes ----------------------------------------- */

/* The ten lumps every map carries, in WAD directory order after the marker. */
typedef enum doom_map_lump {
    DOOM_MAP_THINGS = 0,
    DOOM_MAP_LINEDEFS = 1,
    DOOM_MAP_SIDEDEFS = 2,
    DOOM_MAP_VERTEXES = 3,
    DOOM_MAP_SEGS = 4,
    DOOM_MAP_SSECTORS = 5,
    DOOM_MAP_NODES = 6,
    DOOM_MAP_SECTORS = 7,
    DOOM_MAP_REJECT = 8,
    DOOM_MAP_BLOCKMAP = 9,
    DOOM_MAP_LUMP_COUNT = 10
} doom_map_lump_t;

/* One record's size in the WAD, as packed by the 1993 tools. */
#define DOOM_THING_SIZE 10u      /* x, y, angle, type, options (int16 each) */
#define DOOM_LINEDEF_SIZE 14u    /* v1, v2, flags, special, tag, side[2] */
#define DOOM_SIDEDEF_SIZE 30u    /* textureoffset, rowoffset, 3 names, sector */
#define DOOM_VERTEX_SIZE 4u      /* x, y */
#define DOOM_SEG_SIZE 12u        /* v1, v2, angle, linedef, side, offset */
#define DOOM_SSECTOR_SIZE 4u     /* numsegs, firstseg */
#define DOOM_NODE_SIZE 28u       /* x, y, dx, dy, bbox[2][4], children[2] */
#define DOOM_SECTOR_SIZE 26u     /* floorh, ceilh, 2 names, light, special, tag */

/* The largest index a map record can hold. SEGS, SIDEDEFS, VERTEXES and
 * SECTORS are addressed by an unsigned short in the format; a node child is a
 * short with the top bit reserved for the subsector flag, so a node count of
 * 32768 or more cannot be described. */
#define DOOM_MAP_MAX_ITEMS 65535u
#define DOOM_MAP_MAX_NODES 32767u

/* LineDef flags the renderer tests. Only the ones with a rendering meaning are
 * listed; the simulation's flags are not this layer's business. */
#define DOOM_ML_BLOCKING 1
#define DOOM_ML_TWOSIDED 4
#define DOOM_ML_DONTPEGTOP 8
#define DOOM_ML_DONTPEGBOTTOM 16
#define DOOM_ML_MAPPED 256

/* A sidedef's "no texture" field is "-" padded with NULs, which DOOM maps to
 * texture 0. */
#define DOOM_NO_SIDE_INDEX 0xFFFFu

/* --- Error codes --------------------------------------------------------- */

/*
 * One code per failure class, never a shared "bad map" catch-all:
 *
 *   DOOM_MAP_OK                    the whole graph was validated
 *   DOOM_MAP_ERR_NULL_MAP          the caller passed no map state
 *   DOOM_MAP_ERR_NULL_SOURCE       the caller passed no lump array
 *   DOOM_MAP_ERR_NULL_LUMP         a lump has a size but no data pointer
 *   DOOM_MAP_ERR_LUMP_SIZE         a lump size is not a whole number of records
 *   DOOM_MAP_ERR_REQUIRED_LUMP     one of the seven the walk needs is empty
 *   DOOM_MAP_ERR_ITEM_COUNT        a lump has more records than the format can
 *                                  address (65535, or 32767 for NODES)
 *   DOOM_MAP_ERR_LINEDEF_VERTEX    a linedef's vertex index is out of range
 *   DOOM_MAP_ERR_LINEDEF_SIDE      a linedef's sidedef index is out of range,
 *                                  or a two-sided linedef has no back side
 *   DOOM_MAP_ERR_SIDEDEF_SECTOR    a sidedef's sector index is out of range
 *   DOOM_MAP_ERR_SEG_VERTEX        a seg's vertex index is out of range
 *   DOOM_MAP_ERR_SEG_LINEDEF       a seg's linedef index is out of range
 *   DOOM_MAP_ERR_SEG_SIDE          a seg's side is not 0 or 1
 *   DOOM_MAP_ERR_SUBSECTOR_RANGE   a subsector's seg range does not fit SEGS,
 *                                  or the subsector has no seg
 *   DOOM_MAP_ERR_NODE_CHILD        a node child is neither a node index nor a
 *                                  subsector index
 *
 * A rejected open leaves the map state zeroed, so a stale view can never walk
 * a half-validated graph.
 */
typedef enum doom_map_error {
    DOOM_MAP_OK = 0,
    DOOM_MAP_ERR_NULL_MAP = 1,
    DOOM_MAP_ERR_NULL_SOURCE = 2,
    DOOM_MAP_ERR_NULL_LUMP = 3,
    DOOM_MAP_ERR_LUMP_SIZE = 4,
    DOOM_MAP_ERR_REQUIRED_LUMP = 5,
    DOOM_MAP_ERR_ITEM_COUNT = 6,
    DOOM_MAP_ERR_LINEDEF_VERTEX = 7,
    DOOM_MAP_ERR_LINEDEF_SIDE = 8,
    DOOM_MAP_ERR_SIDEDEF_SECTOR = 9,
    DOOM_MAP_ERR_SEG_VERTEX = 10,
    DOOM_MAP_ERR_SEG_LINEDEF = 11,
    DOOM_MAP_ERR_SEG_SIDE = 12,
    DOOM_MAP_ERR_SUBSECTOR_RANGE = 13,
    DOOM_MAP_ERR_NODE_CHILD = 14
} doom_map_error_t;

/* --- Parsed records ------------------------------------------------------ */

/*
 * Each struct is the runtime form the renderer consumes: coordinates are
 * already 16.16 and angles are already binary angles, so no call site repeats
 * the conversion and no call site can forget it.
 */
typedef struct doom_vertex {
    fixed_t x;
    fixed_t y;
} doom_vertex_t;

typedef struct doom_thing {
    int16_t x;      /* map units, not shifted */
    int16_t y;
    int16_t angle;  /* degrees */
    int16_t type;
    int16_t options;
} doom_thing_t;

/*
 * `special` is the line's action number (0 = none) and `tag` is the sector tag
 * it addresses. Both are read verbatim from the record and neither is
 * validated: DOOM does not validate them either, and an unknown special is a
 * no-op at run time, so a bogus value is the map's problem and not a reason to
 * reject the level. The field order is the on-disk order (v1, v2, flags,
 * special, tag, side[2]).
 */
typedef struct doom_line {
    uint16_t v1;
    uint16_t v2;
    int16_t flags;
    int16_t special;
    int16_t tag;
    uint16_t sideNum[2]; /* DOOM_NO_SIDE_INDEX when absent */
} doom_line_t;

typedef struct doom_side {
    fixed_t textureOffset;
    fixed_t rowOffset;
    r_name_t topTexture;
    r_name_t bottomTexture;
    r_name_t midTexture;
    uint16_t sector;
} doom_side_t;

typedef struct doom_seg {
    uint16_t v1;
    uint16_t v2;
    uint32_t angle;  /* BAM: the map's 16-bit angle << 16, as P_LoadSegs does */
    uint16_t linedef;
    uint16_t side;
    fixed_t offset;
} doom_seg_t;

typedef struct doom_subsector {
    uint16_t numSegs;
    uint16_t firstSeg;
} doom_subsector_t;

typedef struct doom_node {
    fixed_t x;
    fixed_t y;
    fixed_t dx;
    fixed_t dy;
    fixed_t bbox[2][4]; /* [child][BOXTOP, BOXBOTTOM, BOXLEFT, BOXRIGHT] */
    uint16_t children[2];
} doom_node_t;

typedef struct doom_sector {
    fixed_t floorHeight;
    fixed_t ceilingHeight;
    r_name_t floorPic;
    r_name_t ceilingPic;
    int16_t lightLevel;
    int16_t special;
    int16_t tag;
} doom_sector_t;

/* One lump as the map view holds it: the caller's bytes and the record count. */
typedef struct doom_map_lump_view {
    const uint8_t* data; /* NULL for an absent lump */
    size_t size;
    uint32_t count;
} doom_map_lump_view_t;

/* --- Source and state ---------------------------------------------------- */

/*
 * The ten lumps, in WAD order. A caller that reads a level out of a WAD fills
 * this from W_LumpPointer()/W_LumpLength() starting at the marker + 1; a host
 * test fills the seven the walk needs and leaves the rest empty.
 */
typedef struct doom_map_source {
    const uint8_t* data[DOOM_MAP_LUMP_COUNT];
    size_t size[DOOM_MAP_LUMP_COUNT];
} doom_map_source_t;

/**
 * The validated map view. Treat the lump views as read-only after a successful
 * open; the accessors below are the interface. `open` is 1 and `error` is
 * DOOM_MAP_OK only for an accepted graph, and a rejected open zeroes all
 * fields.
 */
typedef struct doom_map {
    doom_map_lump_view_t lumps[DOOM_MAP_LUMP_COUNT];
    int open;
    doom_map_error_t error;
} doom_map_t;

/* --- Public API ---------------------------------------------------------- */

/**
 * @brief Validates a level's lump graph and binds it to @p map.
 *
 * Every check runs before @p map is written, so a rejected open cannot leave a
 * half-valid map behind. Validation order, and the code each step reports:
 *
 *   1. map == NULL                                  DOOM_MAP_ERR_NULL_MAP
 *   2. source == NULL                               DOOM_MAP_ERR_NULL_SOURCE
 *   3. per lump: data == NULL with size != 0        DOOM_MAP_ERR_NULL_LUMP
 *   4. per lump: size % record size != 0            DOOM_MAP_ERR_LUMP_SIZE
 *   5. per lump: count > format limit               DOOM_MAP_ERR_ITEM_COUNT
 *   6. one of the seven the walk needs is empty     DOOM_MAP_ERR_REQUIRED_LUMP
 *   7. per linedef: vertex indices in range         DOOM_MAP_ERR_LINEDEF_VERTEX
 *   8. per linedef: side indices in range, and a
 *      two-sided linedef names both sides          DOOM_MAP_ERR_LINEDEF_SIDE
 *   9. per sidedef: sector index in range           DOOM_MAP_ERR_SIDEDEF_SECTOR
 *  10. per seg: vertex indices in range            DOOM_MAP_ERR_SEG_VERTEX
 *  11. per seg: linedef index in range             DOOM_MAP_ERR_SEG_LINEDEF
 *  12. per seg: side is 0 or 1                     DOOM_MAP_ERR_SEG_SIDE
 *  13. per subsector: numSegs >= 1 and its range
 *      fits SEGS, and the first seg's sidedef has
 *      a valid sector (proved by 9 and 11)         DOOM_MAP_ERR_SUBSECTOR_RANGE
 *  14. per node: each child is a node index
 *      < node count or has R_NF_SUBSECTOR set and
 *      addresses a subsector                      DOOM_MAP_ERR_NODE_CHILD
 *
 * REJECT and BLOCKMAP are only size-checked; their contents are the
 * simulation's concern. THINGS is not validated beyond its record size.
 *
 * @param map    Caller-owned state; zeroed on failure.
 * @param source The ten lump pointers and sizes; the bytes must outlive @p map.
 * @return DOOM_MAP_OK, or one of the codes above.
 */
doom_map_error_t doom_map_open(doom_map_t* map, const doom_map_source_t* source);

/* Typed accessors. Indices are trusted: doom_map_open() proved every index the
 * walk can reach. Passing an index the graph never named is a caller bug, not
 * a condition these functions diagnose. */

void doom_map_get_vertex(const doom_map_t* map, uint16_t index, doom_vertex_t* out);
void doom_map_get_thing(const doom_map_t* map, uint32_t index, doom_thing_t* out);
void doom_map_get_line(const doom_map_t* map, uint16_t index, doom_line_t* out);
void doom_map_get_side(const doom_map_t* map, uint16_t index, doom_side_t* out);
void doom_map_get_seg(const doom_map_t* map, uint16_t index, doom_seg_t* out);
void doom_map_get_subsector(const doom_map_t* map, uint16_t index,
                            doom_subsector_t* out);
void doom_map_get_node(const doom_map_t* map, uint16_t index, doom_node_t* out);
void doom_map_get_sector(const doom_map_t* map, uint16_t index, doom_sector_t* out);

/**
 * @brief The sector a subsector belongs to, as DOOM derives it.
 *
 * mapsubsector_t stores no sector: P_GroupLines() takes it from the first
 * seg's sidedef (`ss->sector = seg[ss->firstline].sidedef->sector`). This is
 * the same rule, and it is why a subsector with no segs is rejected at open.
 */
uint16_t doom_map_subsector_sector(const doom_map_t* map,
                                   const doom_subsector_t* subsector);

/**
 * @brief The front and back sectors of a seg.
 *
 * The front sector is always present. The back sector is present only when the
 * linedef carries DOOM_ML_TWOSIDED, which is exactly when P_LoadSegs() sets
 * `li->backsector`; a physically two-sided line without the flag is a solid
 * wall to the renderer, and this function reports it that way.
 *
 * @param back Optional out: the back sector index; untouched when there is none.
 * @return 1 when a back sector exists, 0 when the seg is single sided.
 */
int doom_map_seg_sectors(const doom_map_t* map, const doom_seg_t* seg,
                         uint16_t* front, uint16_t* back);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_MAP_H */
