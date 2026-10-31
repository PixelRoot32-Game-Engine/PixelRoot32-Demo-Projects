/*
 * Behaviour tests for the BSP walk (D3.1: lib/doom/r_bsp.c, r_plane.c,
 * doom_map.c).
 *
 * Every map here is assembled byte for byte by this file, in the 1993 WAD
 * layout, and fed to the walk through the public C API. No WAD, no binary
 * fixture and no copyrighted data is involved. The synthetic BSPs are the
 * cases the task names: one subsector, a two-node tree, a seg outside the
 * frustum, a seg behind the viewer, a back-facing seg, a near solid wall
 * occluding a far one, the plane split, and the plane cap.
 *
 * What the suite pins, one case at a time:
 *
 *  - a single subsector stores one solid wall fragment with its exact screen
 *    columns and silhouette, allocates the floor and ceiling planes and marks
 *    only the floor (the wall reaches above the screen, so no ceiling shows);
 *  - a two-node tree renders the viewer's side first, then the far side, and
 *    the far fragment is clipped to the column the near one leaves open;
 *  - a wall entirely outside the 90-degree frustum, a wall behind the viewer
 *    and a back-facing wall each store nothing;
 *  - a near solid wall hides a far one completely: one drawseg, not two;
 *  - R_CheckPlane splits a plane when the requested columns overlap recorded
 *    ones, widens it when they are disjoint, and reports the cap instead of
 *    writing past it;
 *  - the walk reports a plane overflow with a complete but shorter result;
 *  - the map loader rejects an out-of-range linedef vertex;
 *  - the spawn viewpoint uses the last matching player start, the P_SpawnPlayer
 *    rule, and derives z from the spawn sector's floor plus VIEWHEIGHT.
 *
 * The tests go through the public C API only. They never inspect the walk's
 * internals, so an implementation that passes here is free to change them.
 */
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <unity.h>

#include "doom_map.h"
#include "r_bsp.h"
#include "r_plane.h"

#include "../../test_config.h"

namespace {

constexpr std::size_t kScreenWidth = 320u;
constexpr fixed_t kFracUnit = 65536;

void putLe16(std::vector<std::uint8_t>& out, std::int16_t value) {
    const std::uint16_t bits = static_cast<std::uint16_t>(value);
    out.push_back(static_cast<std::uint8_t>(bits & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((bits >> 8) & 0xFFu));
}

void putLe16At(std::vector<std::uint8_t>& out, std::size_t at, std::int16_t value) {
    const std::uint16_t bits = static_cast<std::uint16_t>(value);
    out[at] = static_cast<std::uint8_t>(bits & 0xFFu);
    out[at + 1] = static_cast<std::uint8_t>((bits >> 8) & 0xFFu);
}

/// An 8-byte WAD name field: upper case, NUL padded, never longer than eight.
void putName(std::vector<std::uint8_t>& out, const char* name) {
    const std::size_t length = std::strlen(name);

    for (std::size_t i = 0; i < 8u; ++i) {
        out.push_back(i < length ? static_cast<std::uint8_t>(name[i]) : 0u);
    }
}

/*
 * Assembles a map's ten lumps in WAD record layout. The getters return the
 * index of the record they appended, so a test reads like the map it builds.
 */
struct MapBuilder {
    std::vector<std::uint8_t> things;
    std::vector<std::uint8_t> linedefs;
    std::vector<std::uint8_t> sidedefs;
    std::vector<std::uint8_t> vertexes;
    std::vector<std::uint8_t> segs;
    std::vector<std::uint8_t> ssectors;
    std::vector<std::uint8_t> nodes;
    std::vector<std::uint8_t> sectors;
    std::vector<std::uint8_t> reject;
    std::vector<std::uint8_t> blockmap;

    std::uint16_t addVertex(std::int16_t x, std::int16_t y) {
        const std::uint16_t index = static_cast<std::uint16_t>(vertexes.size() / 4u);
        putLe16(vertexes, x);
        putLe16(vertexes, y);
        return index;
    }

    std::uint16_t addSector(std::int16_t floor, std::int16_t ceiling,
                            const char* floorPic, const char* ceilingPic,
                            std::int16_t light) {
        const std::uint16_t index = static_cast<std::uint16_t>(sectors.size() / 26u);
        putLe16(sectors, floor);
        putLe16(sectors, ceiling);
        putName(sectors, floorPic);
        putName(sectors, ceilingPic);
        putLe16(sectors, light);
        putLe16(sectors, 0); // special
        putLe16(sectors, 0); // tag
        return index;
    }

    std::uint16_t addSide(std::int16_t textureOffset, std::int16_t rowOffset,
                          const char* top, const char* bottom, const char* middle,
                          std::uint16_t sector) {
        const std::uint16_t index = static_cast<std::uint16_t>(sidedefs.size() / 30u);
        putLe16(sidedefs, textureOffset);
        putLe16(sidedefs, rowOffset);
        putName(sidedefs, top);
        putName(sidedefs, bottom);
        putName(sidedefs, middle);
        putLe16(sidedefs, static_cast<std::int16_t>(sector));
        return index;
    }

    std::uint16_t addLine(std::uint16_t v1, std::uint16_t v2, std::int16_t flags,
                          std::uint16_t side0,
                          std::uint16_t side1 = DOOM_NO_SIDE_INDEX) {
        const std::uint16_t index = static_cast<std::uint16_t>(linedefs.size() / 14u);
        putLe16(linedefs, static_cast<std::int16_t>(v1));
        putLe16(linedefs, static_cast<std::int16_t>(v2));
        putLe16(linedefs, flags);
        putLe16(linedefs, 0); // special
        putLe16(linedefs, 0); // tag
        putLe16(linedefs, static_cast<std::int16_t>(side0));
        putLe16(linedefs, side1 == DOOM_NO_SIDE_INDEX
                              ? static_cast<std::int16_t>(-1)
                              : static_cast<std::int16_t>(side1));
        return index;
    }

    std::uint16_t addSeg(std::uint16_t v1, std::uint16_t v2, std::uint16_t angle,
                         std::uint16_t linedef, std::uint16_t side,
                         std::int16_t offset = 0) {
        const std::uint16_t index = static_cast<std::uint16_t>(segs.size() / 12u);
        putLe16(segs, static_cast<std::int16_t>(v1));
        putLe16(segs, static_cast<std::int16_t>(v2));
        putLe16(segs, static_cast<std::int16_t>(angle));
        putLe16(segs, static_cast<std::int16_t>(linedef));
        putLe16(segs, static_cast<std::int16_t>(side));
        putLe16(segs, offset);
        return index;
    }

    std::uint16_t addSubsector(std::uint16_t numSegs, std::uint16_t firstSeg) {
        const std::uint16_t index = static_cast<std::uint16_t>(ssectors.size() / 4u);
        putLe16(ssectors, static_cast<std::int16_t>(numSegs));
        putLe16(ssectors, static_cast<std::int16_t>(firstSeg));
        return index;
    }

    std::uint16_t addNode(std::int16_t x, std::int16_t y, std::int16_t dx,
                          std::int16_t dy,
                          const std::array<std::int16_t, 4>& bbox0,
                          const std::array<std::int16_t, 4>& bbox1,
                          std::uint16_t child0, std::uint16_t child1) {
        const std::uint16_t index = static_cast<std::uint16_t>(nodes.size() / 28u);
        putLe16(nodes, x);
        putLe16(nodes, y);
        putLe16(nodes, dx);
        putLe16(nodes, dy);
        for (std::int16_t value : bbox0) {
            putLe16(nodes, value);
        }
        for (std::int16_t value : bbox1) {
            putLe16(nodes, value);
        }
        putLe16(nodes, static_cast<std::int16_t>(child0));
        putLe16(nodes, static_cast<std::int16_t>(child1));
        return index;
    }

    std::uint32_t addThing(std::int16_t x, std::int16_t y, std::int16_t angle,
                           std::int16_t type) {
        ++thingCount;
        putLe16(things, x);
        putLe16(things, y);
        putLe16(things, angle);
        putLe16(things, type);
        putLe16(things, 0); // options
        return thingCount - 1u;
    }

    doom_map_error_t open(doom_map_t* map) const {
        doom_map_source_t source{};

        source.data[DOOM_MAP_THINGS] = things.empty() ? nullptr : things.data();
        source.size[DOOM_MAP_THINGS] = things.size();
        source.data[DOOM_MAP_LINEDEFS] = linedefs.data();
        source.size[DOOM_MAP_LINEDEFS] = linedefs.size();
        source.data[DOOM_MAP_SIDEDEFS] = sidedefs.data();
        source.size[DOOM_MAP_SIDEDEFS] = sidedefs.size();
        source.data[DOOM_MAP_VERTEXES] = vertexes.data();
        source.size[DOOM_MAP_VERTEXES] = vertexes.size();
        source.data[DOOM_MAP_SEGS] = segs.data();
        source.size[DOOM_MAP_SEGS] = segs.size();
        source.data[DOOM_MAP_SSECTORS] = ssectors.data();
        source.size[DOOM_MAP_SSECTORS] = ssectors.size();
        source.data[DOOM_MAP_NODES] = nodes.empty() ? nullptr : nodes.data();
        source.size[DOOM_MAP_NODES] = nodes.size();
        source.data[DOOM_MAP_SECTORS] = sectors.data();
        source.size[DOOM_MAP_SECTORS] = sectors.size();
        source.data[DOOM_MAP_REJECT] = reject.empty() ? nullptr : reject.data();
        source.size[DOOM_MAP_REJECT] = reject.size();
        source.data[DOOM_MAP_BLOCKMAP] = blockmap.empty() ? nullptr : blockmap.data();
        source.size[DOOM_MAP_BLOCKMAP] = blockmap.size();

        return doom_map_open(map, &source);
    }

  private:
    std::uint32_t thingCount = 0u;
};

/// One view's caller-owned storage, at a capacity small enough for a stack.
struct Fixture {
    std::array<r_drawseg_t, 8> drawsegs{};
    std::array<r_visplane_t, 8> planes{};
    std::array<r_clip_range_t, R_MAX_CLIP_SEGS> clips{};
    std::array<std::int16_t, kScreenWidth> floorClip{};
    std::array<std::int16_t, kScreenWidth> ceilingClip{};
    r_view_state_t view{};

    r_bsp_error_t init(const doom_map_t* map, std::uint16_t planeCapacity = 8u) {
        r_view_storage_t storage{};

        storage.drawsegs = drawsegs.data();
        storage.drawsegCapacity = static_cast<std::uint16_t>(drawsegs.size());
        storage.planes.planes = planes.data();
        storage.planes.capacity = planeCapacity;
        storage.planes.count = 0;
        storage.clipRanges = clips.data();
        storage.clipRangeCapacity = static_cast<std::uint16_t>(clips.size());
        storage.floorClip = floorClip.data();
        storage.ceilingClip = ceilingClip.data();
        return r_bsp_init(&view, map, &storage);
    }

    r_bsp_error_t render(fixed_t x, fixed_t y, fixed_t z, angle_t angle) {
        r_view_params_t params{};

        params.x = x;
        params.y = y;
        params.z = z;
        params.angle = angle;
        params.extralight = 0;
        params.skyPicNum = R_PLANE_NO_SKY;
        return r_bsp_render(&view, &params);
    }
};

/// One sector, one subsector, one solid seg from (x, high) to (x, low).
/// v1 on the +y side makes the seg front-facing from a viewpoint on -x.
struct OneWall {
    MapBuilder map;
    doom_map_t view{};

    OneWall(std::int16_t x, std::int16_t high, std::int16_t low) {
        const std::uint16_t sector = map.addSector(0, 128, "FLOOR0", "CEIL0", 160);
        const std::uint16_t v1 = map.addVertex(x, high);
        const std::uint16_t v2 = map.addVertex(x, low);
        const std::uint16_t side = map.addSide(0, 0, "-", "-", "STONE", sector);
        const std::uint16_t line = map.addLine(v1, v2, 0, side);
        map.addSeg(v1, v2, high > low ? 49152u : 16384u, line, 0);
        map.addSubsector(1, 0);
    }
};

void test_single_subsector_emits_one_drawseg_and_two_planes() {
    OneWall world(100, 50, -50);
    Fixture fixture;

    TEST_ASSERT_EQUAL_INT(DOOM_MAP_OK, world.map.open(&world.view));
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.init(&world.view));
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.render(0, 0, 41 * kFracUnit, 0));

    // One solid wall: x1 is column 81, x2 is 239 (the range 81..240 with the
    // exclusive end DOOM's clip posts use).
    TEST_ASSERT_EQUAL_UINT16(1u, fixture.view.drawsegCount);
    TEST_ASSERT_EQUAL_INT16(81, fixture.drawsegs[0].x1);
    TEST_ASSERT_EQUAL_INT16(239, fixture.drawsegs[0].x2);
    TEST_ASSERT_EQUAL_UINT8(R_SIL_BOTH, fixture.drawsegs[0].silhouette);
    TEST_ASSERT_EQUAL_INT32(INT32_MAX, fixture.drawsegs[0].bottomSilhouette);
    TEST_ASSERT_EQUAL_INT32(INT32_MIN, fixture.drawsegs[0].topSilhouette);
    TEST_ASSERT_EQUAL_INT16(160, fixture.drawsegs[0].lightLevel);
    TEST_ASSERT_EQUAL_INT16(11, fixture.drawsegs[0].lightNum); // 160>>4, vertical seg +1
    TEST_ASSERT_GREATER_THAN_INT32(0, fixture.drawsegs[0].scale1);

    // Two planes: the floor is marked across the wall's columns because the
    // wall bottom is above the screen bottom; the ceiling is allocated but
    // never marked because the wall reaches above the screen top.
    TEST_ASSERT_EQUAL_UINT16(2u, fixture.view.storage.planes.count);
    const r_visplane_t* floorPlane = &fixture.planes[0];
    const r_visplane_t* ceilingPlane = &fixture.planes[1];

    TEST_ASSERT_EQUAL_INT32(0, floorPlane->height);
    TEST_ASSERT_EQUAL_INT16(160, floorPlane->lightLevel);
    TEST_ASSERT_EQUAL_INT16(81, floorPlane->minX);
    TEST_ASSERT_EQUAL_INT16(239, floorPlane->maxX);
    TEST_ASSERT_EQUAL_UINT8(166, floorPlane->top[160]);
    TEST_ASSERT_EQUAL_UINT8(199, floorPlane->bottom[160]);

    TEST_ASSERT_EQUAL_INT32(128 * kFracUnit, ceilingPlane->height);
    // The wall reaches above the screen top, so no ceiling column is marked,
    // but R_CheckPlane still widened the plane's range: the same edge case
    // upstream has, and the reason an empty plane must be judged by top[].
    TEST_ASSERT_EQUAL_INT16(81, ceilingPlane->minX);
    TEST_ASSERT_EQUAL_INT16(239, ceilingPlane->maxX);
    TEST_ASSERT_EQUAL_UINT8(R_PLANE_EMPTY, ceilingPlane->top[160]);
}

void test_two_node_tree_visits_the_near_side_first() {
    MapBuilder builder;
    doom_map_t map;

    const std::uint16_t nearSector = builder.addSector(0, 128, "FLOOR0", "CEIL0", 160);
    const std::uint16_t farSector = builder.addSector(0, 128, "FLOOR0", "CEIL0", 160);

    // Near wall: (100,50) -> (100,0), columns 81..160.
    const std::uint16_t nearV1 = builder.addVertex(100, 50);
    const std::uint16_t nearV2 = builder.addVertex(100, 0);
    const std::uint16_t nearSide = builder.addSide(0, 0, "-", "-", "STONE", nearSector);
    const std::uint16_t nearLine = builder.addLine(nearV1, nearV2, 0, nearSide);
    builder.addSeg(nearV1, nearV2, 49152u, nearLine, 0);
    builder.addSubsector(1, 0);

    // Far wall: (200,150) -> (200,100), columns 41..81.
    const std::uint16_t farV1 = builder.addVertex(200, 150);
    const std::uint16_t farV2 = builder.addVertex(200, 100);
    const std::uint16_t farSide = builder.addSide(0, 0, "-", "-", "STONE", farSector);
    const std::uint16_t farLine = builder.addLine(farV1, farV2, 0, farSide);
    builder.addSeg(farV1, farV2, 49152u, farLine, 0);
    builder.addSubsector(1, 1);

    // Partition y = 100, dx = 1: the viewer at y = 0 is on side 0.
    builder.addNode(0, 100, 1, 0, {60, -10, 50, 150}, {180, 90, 150, 250},
                    static_cast<std::uint16_t>(0x8000u | 0u),
                    static_cast<std::uint16_t>(0x8000u | 1u));

    Fixture fixture;
    TEST_ASSERT_EQUAL_INT(DOOM_MAP_OK, builder.open(&map));
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.init(&map));
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.render(0, 0, 41 * kFracUnit, 0));

    // The viewer's half renders first, then the far child: near-to-far order.
    TEST_ASSERT_EQUAL_UINT16(2u, fixture.view.drawsegCount);
    TEST_ASSERT_EQUAL_INT16(81, fixture.drawsegs[0].x1);
    TEST_ASSERT_EQUAL_INT16(159, fixture.drawsegs[0].x2);
    TEST_ASSERT_EQUAL_INT16(41, fixture.drawsegs[1].x1);
    TEST_ASSERT_EQUAL_INT16(80, fixture.drawsegs[1].x2);

    // Both sectors share one floor key, so the near fragment widened the plane
    // and the far fragment joined the same range: [41, 159].
    TEST_ASSERT_EQUAL_UINT16(2u, fixture.view.storage.planes.count);
    TEST_ASSERT_EQUAL_INT16(41, fixture.planes[0].minX);
    TEST_ASSERT_EQUAL_INT16(159, fixture.planes[0].maxX);
}

void test_segment_outside_the_frustum_is_dropped() {
    // Both endpoints are beyond the 45-degree half-angle; the segment is
    // front-facing but entirely off the left edge of the screen.
    OneWall world(50, 600, 500);
    Fixture fixture;

    TEST_ASSERT_EQUAL_INT(DOOM_MAP_OK, world.map.open(&world.view));
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.init(&world.view));
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.render(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_UINT16(0u, fixture.view.drawsegCount);
}

void test_segment_behind_the_viewer_is_dropped() {
    // Front-facing (span < 180) but entirely behind the viewpoint: the frustum
    // test must drop it rather than project it across the screen.
    OneWall world(-100, -50, 50);
    Fixture fixture;

    TEST_ASSERT_EQUAL_INT(DOOM_MAP_OK, world.map.open(&world.view));
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.init(&world.view));
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.render(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_UINT16(0u, fixture.view.drawsegCount);
}

void test_back_facing_segment_is_dropped() {
    // The reverse winding of a visible wall: span >= ANG180 is the back side.
    OneWall world(-100, 50, -50);
    Fixture fixture;

    TEST_ASSERT_EQUAL_INT(DOOM_MAP_OK, world.map.open(&world.view));
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.init(&world.view));
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.render(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_UINT16(0u, fixture.view.drawsegCount);
}

void test_near_solid_wall_occludes_the_far_one() {
    MapBuilder builder;
    doom_map_t map;

    const std::uint16_t sector = builder.addSector(0, 128, "FLOOR0", "CEIL0", 160);

    // Near wall at x = 50, y in [-30, 30]: columns 65..256.
    const std::uint16_t nearA = builder.addVertex(50, 30);
    const std::uint16_t nearB = builder.addVertex(50, -30);
    const std::uint16_t nearSide = builder.addSide(0, 0, "-", "-", "STONE", sector);
    const std::uint16_t nearLine = builder.addLine(nearA, nearB, 0, nearSide);
    builder.addSeg(nearA, nearB, 49152u, nearLine, 0);

    // Far wall at x = 100, y in [-10, 10]: columns 145..176, inside the near one.
    const std::uint16_t farA = builder.addVertex(100, 10);
    const std::uint16_t farB = builder.addVertex(100, -10);
    const std::uint16_t farSide = builder.addSide(0, 0, "-", "-", "STONE", sector);
    const std::uint16_t farLine = builder.addLine(farA, farB, 0, farSide);
    builder.addSeg(farA, farB, 49152u, farLine, 0);

    builder.addSubsector(2, 0);

    Fixture fixture;
    TEST_ASSERT_EQUAL_INT(DOOM_MAP_OK, builder.open(&map));
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.init(&map));
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.render(0, 0, 41 * kFracUnit, 0));

    TEST_ASSERT_EQUAL_UINT16(1u, fixture.view.drawsegCount);
    TEST_ASSERT_EQUAL_INT16(65, fixture.drawsegs[0].x1);
    TEST_ASSERT_EQUAL_INT16(255, fixture.drawsegs[0].x2);
}

void test_plane_check_splits_when_columns_overlap() {
    std::array<r_visplane_t, 4> planes{};
    r_plane_set_t set{};
    r_visplane_t* plane = nullptr;
    r_visplane_t* split = nullptr;

    set.planes = planes.data();
    set.capacity = static_cast<std::uint16_t>(planes.size());
    set.count = 0;

    TEST_ASSERT_EQUAL_INT(R_PLANE_OK,
                          r_plane_find(&set, 0, r_pack_name(
                                            reinterpret_cast<const std::uint8_t*>("FLOOR0\0\0")),
                                       160, R_PLANE_NO_SKY, &plane));
    TEST_ASSERT_EQUAL_UINT16(1u, set.count);
    TEST_ASSERT_EQUAL_INT16(kScreenWidth, plane->minX);
    TEST_ASSERT_EQUAL_INT16(-1, plane->maxX);

    // Record columns 4..6, then ask for 3..5: the intersection 4..5 is already
    // recorded, so a second plane with the same key is appended.
    plane->minX = 4;
    plane->maxX = 6;
    plane->top[4] = 10;
    plane->bottom[4] = 20;
    plane->top[5] = 11;
    plane->bottom[5] = 21;
    plane->top[6] = 12;
    plane->bottom[6] = 22;

    TEST_ASSERT_EQUAL_INT(R_PLANE_OK, r_plane_check(&set, plane, 3, 5, &split));
    TEST_ASSERT_EQUAL_UINT16(2u, set.count);
    TEST_ASSERT_TRUE(split != plane);
    TEST_ASSERT_EQUAL_INT16(3, split->minX);
    TEST_ASSERT_EQUAL_INT16(5, split->maxX);
    TEST_ASSERT_EQUAL_INT32(plane->height, split->height);
    TEST_ASSERT_EQUAL_UINT8(R_PLANE_EMPTY, split->top[3]);
    TEST_ASSERT_EQUAL_UINT8(R_PLANE_EMPTY, split->bottom[3]);

    // A disjoint range still shares the plane and widens it.
    TEST_ASSERT_EQUAL_INT(R_PLANE_OK, r_plane_check(&set, plane, 8, 9, &split));
    TEST_ASSERT_EQUAL_UINT16(2u, set.count);
    TEST_ASSERT_TRUE(split == plane);
    TEST_ASSERT_EQUAL_INT16(4, plane->minX);
    TEST_ASSERT_EQUAL_INT16(9, plane->maxX);
}

void test_plane_split_preserves_the_original_plane() {
    std::array<r_visplane_t, 2> planes{};
    r_plane_set_t set{};
    r_visplane_t* plane = nullptr;
    r_visplane_t* split = nullptr;

    set.planes = planes.data();
    set.capacity = static_cast<std::uint16_t>(planes.size());
    set.count = 0;
    TEST_ASSERT_EQUAL_INT(R_PLANE_OK,
                          r_plane_find(&set, -1, static_cast<r_name_t>(7), 1,
                                       R_PLANE_NO_SKY, &plane));
    plane->minX = 100;
    plane->maxX = 110;
    plane->top[105] = 3;
    plane->bottom[105] = 4;

    TEST_ASSERT_EQUAL_INT(R_PLANE_OK, r_plane_check(&set, plane, 104, 106, &split));
    TEST_ASSERT_EQUAL_UINT16(2u, set.count);
    TEST_ASSERT_EQUAL_INT16(100, plane->minX);
    TEST_ASSERT_EQUAL_INT16(110, plane->maxX);
    TEST_ASSERT_EQUAL_UINT8(3, plane->top[105]);
    TEST_ASSERT_EQUAL_UINT8(4, plane->bottom[105]);
}

void test_plane_capacity_fails_without_writing() {
    std::array<r_visplane_t, 1> planes{};
    r_plane_set_t set{};
    r_visplane_t* plane = nullptr;
    r_visplane_t* other = nullptr;

    set.planes = planes.data();
    set.capacity = 1;
    set.count = 0;
    TEST_ASSERT_EQUAL_INT(R_PLANE_OK,
                          r_plane_find(&set, 0, static_cast<r_name_t>(1), 0,
                                       R_PLANE_NO_SKY, &plane));
    plane->minX = 7;
    plane->maxX = 9;
    plane->top[8] = 5;

    TEST_ASSERT_EQUAL_INT(R_PLANE_ERR_NO_ROOM,
                          r_plane_find(&set, 1, static_cast<r_name_t>(2), 0,
                                       R_PLANE_NO_SKY, &other));
    TEST_ASSERT_EQUAL_UINT16(1u, set.count);
    TEST_ASSERT_EQUAL_INT16(7, plane->minX);
    TEST_ASSERT_EQUAL_INT16(9, plane->maxX);
    TEST_ASSERT_EQUAL_UINT8(5, plane->top[8]);
}

void test_walk_reports_plane_overflow() {
    OneWall world(100, 50, -50);
    Fixture fixture;

    TEST_ASSERT_EQUAL_INT(DOOM_MAP_OK, world.map.open(&world.view));
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.init(&world.view, 1u));
    TEST_ASSERT_EQUAL_INT(R_BSP_ERR_VISPLANE_OVERFLOW,
                          fixture.render(0, 0, 41 * kFracUnit, 0));

    // The floor plane was created; the ceiling could not be, and no wall was
    // stored, so the partial result is consistent and within capacity.
    TEST_ASSERT_EQUAL_UINT16(1u, fixture.view.storage.planes.count);
    TEST_ASSERT_EQUAL_UINT16(0u, fixture.view.drawsegCount);
}

/* D4.2h: a view that outgrows the drawseg array used to lose its later
 * fragments silently -- `drawsegOverflowed` was a flag nobody failed on, so a
 * player walking E1M1 saw walls vanish. It is now the walk's own error, on the
 * same path as R_BSP_ERR_CLIP_OVERFLOW, and the partial list is consistent. */
void test_walk_that_outgrows_the_drawseg_array_fails_instead_of_dropping() {
    MapBuilder builder;
    doom_map_t map;

    const std::uint16_t nearSector = builder.addSector(0, 128, "FLOOR0", "CEIL0", 160);
    const std::uint16_t farSector = builder.addSector(0, 128, "FLOOR0", "CEIL0", 160);

    // Two disjoint walls, so the walk stores two drawsegs.
    const std::uint16_t nearV1 = builder.addVertex(100, 50);
    const std::uint16_t nearV2 = builder.addVertex(100, 0);
    const std::uint16_t nearSide = builder.addSide(0, 0, "-", "-", "STONE", nearSector);
    const std::uint16_t nearLine = builder.addLine(nearV1, nearV2, 0, nearSide);
    builder.addSeg(nearV1, nearV2, 49152u, nearLine, 0);
    builder.addSubsector(1, 0);

    const std::uint16_t farV1 = builder.addVertex(200, 150);
    const std::uint16_t farV2 = builder.addVertex(200, 100);
    const std::uint16_t farSide = builder.addSide(0, 0, "-", "-", "STONE", farSector);
    const std::uint16_t farLine = builder.addLine(farV1, farV2, 0, farSide);
    builder.addSeg(farV1, farV2, 49152u, farLine, 0);
    builder.addSubsector(1, 1);

    builder.addNode(0, 100, 1, 0, {60, -10, 50, 150}, {180, 90, 150, 250},
                    static_cast<std::uint16_t>(0x8000u | 0u),
                    static_cast<std::uint16_t>(0x8000u | 1u));

    Fixture fixture;
    TEST_ASSERT_EQUAL_INT(DOOM_MAP_OK, builder.open(&map));
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.init(&map));
    // Only one entry: the second visible wall cannot be stored.
    fixture.view.storage.drawsegCapacity = 1u;

    TEST_ASSERT_EQUAL_INT(R_BSP_ERR_DRAWSEG_OVERFLOW,
                          fixture.render(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_UINT16(1u, fixture.view.drawsegCount);
    TEST_ASSERT_EQUAL_UINT8(1u, fixture.view.drawsegOverflowed);
    // The stored fragment is complete, so the shorter result is still usable.
    TEST_ASSERT_EQUAL_INT16(81, fixture.drawsegs[0].x1);
}

void test_map_loader_rejects_a_linedef_vertex() {
    OneWall world(100, 50, -50);
    doom_map_t map;

    // LINEDEFS record 0, field v1 is the first two bytes.
    putLe16At(world.map.linedefs, 0u, 99);

    TEST_ASSERT_EQUAL_INT(DOOM_MAP_ERR_LINEDEF_VERTEX, world.map.open(&map));
    TEST_ASSERT_EQUAL_INT(0, map.open);
}

void test_spawn_view_uses_the_last_player_start() {
    OneWall world(100, 50, -50);
    doom_map_t map;
    r_view_params_t params{};

    // Two type-1 starts: P_LoadThings overwrites playerstarts[0], so the last
    // one (10, 20, facing 180) is the spawn, not the first.
    world.map.addThing(1, 2, 90, 1);
    world.map.addThing(10, 20, 180, 1);

    TEST_ASSERT_EQUAL_INT(DOOM_MAP_OK, world.map.open(&map));
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, r_bsp_spawn_view(&map, 0, &params));

    TEST_ASSERT_EQUAL_INT32(10 * kFracUnit, params.x);
    TEST_ASSERT_EQUAL_INT32(20 * kFracUnit, params.y);
    TEST_ASSERT_EQUAL_INT32(41 * kFracUnit, params.z);
    TEST_ASSERT_EQUAL_UINT32(ANG180, params.angle);
    TEST_ASSERT_EQUAL_INT16(0, params.extralight);
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    test_setup();
    UNITY_BEGIN();

    RUN_TEST(test_single_subsector_emits_one_drawseg_and_two_planes);
    RUN_TEST(test_two_node_tree_visits_the_near_side_first);
    RUN_TEST(test_segment_outside_the_frustum_is_dropped);
    RUN_TEST(test_segment_behind_the_viewer_is_dropped);
    RUN_TEST(test_back_facing_segment_is_dropped);
    RUN_TEST(test_near_solid_wall_occludes_the_far_one);
    RUN_TEST(test_plane_check_splits_when_columns_overlap);
    RUN_TEST(test_plane_split_preserves_the_original_plane);
    RUN_TEST(test_plane_capacity_fails_without_writing);
    RUN_TEST(test_walk_reports_plane_overflow);
    RUN_TEST(test_walk_that_outgrows_the_drawseg_array_fails_instead_of_dropping);
    RUN_TEST(test_map_loader_rejects_a_linedef_vertex);
    RUN_TEST(test_spawn_view_uses_the_last_player_start);

    const int result = UNITY_END();
    test_teardown();
    return result;
}
