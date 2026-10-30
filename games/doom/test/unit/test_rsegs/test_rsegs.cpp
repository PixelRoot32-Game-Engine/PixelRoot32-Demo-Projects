/*
 * Behaviour tests for the wall column emitter (D3.2: lib/doom/r_segs.c,
 * r_light.c), and for the fragment predicate it shares with the walk.
 *
 * Every map, texture and colormap here is assembled byte for byte by this file.
 * No WAD, no binary fixture and no copyrighted data is involved: the textures
 * are one-texel-wide colour bands and the colormap is an identity table, so
 * every expected pixel value is a number this file wrote.
 *
 * What the suite pins, one case at a time:
 *
 *  - a one-sided wall that fills the frame writes every pixel of all 320
 *    columns and collapses the clip arrays;
 *  - a far wall behind a two-sided line is clipped by ceilingclip[]/floorclip[]
 *    to the gap the near line's tiers left, in the right order (the far wall is
 *    emitted second and must not overwrite a single near pixel);
 *  - the upper and lower tiers of a two-sided line draw their own textures and
 *    leave the opening between them untouched when there is no mid texture;
 *  - a mid texture with a transparency mask leaves a zero-mask pixel alone;
 *  - an 8-texel-wide texture repeats over a 21-column fragment, and the value
 *    each column shows matches rw_offset - tan(angle)*rw_distance with the
 *    texture's own width mask;
 *  - a sky ceiling does not touch ceilingclip[] (the outdoor height hack makes
 *    markCeiling false), while a two-sided line between different ceiling
 *    pictures does move it;
 *  - light level 0 and light level 255 select different tables of the same
 *    COLORMAP lump, and each pixel is that table's entry for the texel;
 *  - a non-resident texture fails the whole call and the surface keeps its
 *    bytes; a column storage height that is not a power of two is a shape error;
 *  - r_light_init validates the lump's layout and r_light_wall clamps the row;
 *  - the emitter rejects a surface smaller than the view before writing.
 *
 * The tests go through the public C API only. They never inspect the emitter's
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
#include "r_light.h"
#include "r_plane.h"
#include "r_segs.h"
#include "r_tables.h"

#include "../../test_config.h"

namespace {

constexpr std::size_t kScreenWidth = 320u;
constexpr std::size_t kScreenHeight = 200u;
constexpr fixed_t kFracUnit = 65536;
constexpr std::uint8_t kUncovered = 0xFFu;

void putLe16(std::vector<std::uint8_t>& out, std::int16_t value) {
    const std::uint16_t bits = static_cast<std::uint16_t>(value);
    out.push_back(static_cast<std::uint8_t>(bits & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((bits >> 8) & 0xFFu));
}

/// An 8-byte WAD name field: upper case, NUL padded, never longer than eight.
void putName(std::vector<std::uint8_t>& out, const char* name) {
    const std::size_t length = std::strlen(name);

    for (std::size_t i = 0; i < 8u; ++i) {
        out.push_back(i < length ? static_cast<std::uint8_t>(name[i]) : 0u);
    }
}

/* --- The 1993 map layout, built record by record ------------------------- */

struct MapBuilder {
    std::vector<std::uint8_t> things;
    std::vector<std::uint8_t> linedefs;
    std::vector<std::uint8_t> sidedefs;
    std::vector<std::uint8_t> vertexes;
    std::vector<std::uint8_t> segs;
    std::vector<std::uint8_t> ssectors;
    std::vector<std::uint8_t> nodes;
    std::vector<std::uint8_t> sectors;

    std::uint16_t addVertex(std::int16_t x, std::int16_t y) {
        const auto index = static_cast<std::uint16_t>(vertexes.size() / 4u);
        putLe16(vertexes, x);
        putLe16(vertexes, y);
        return index;
    }

    std::uint16_t addSector(std::int16_t floor, std::int16_t ceiling,
                            const char* floorPic, const char* ceilingPic,
                            std::int16_t light) {
        const auto index = static_cast<std::uint16_t>(sectors.size() / 26u);
        putLe16(sectors, floor);
        putLe16(sectors, ceiling);
        putName(sectors, floorPic);
        putName(sectors, ceilingPic);
        putLe16(sectors, light);
        putLe16(sectors, 0);  // special
        putLe16(sectors, 0);  // tag
        return index;
    }

    std::uint16_t addSide(std::int16_t textureOffset, std::int16_t rowOffset,
                          const char* top, const char* bottom, const char* middle,
                          std::uint16_t sector) {
        const auto index = static_cast<std::uint16_t>(sidedefs.size() / 30u);
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
        const auto index = static_cast<std::uint16_t>(linedefs.size() / 14u);
        putLe16(linedefs, static_cast<std::int16_t>(v1));
        putLe16(linedefs, static_cast<std::int16_t>(v2));
        putLe16(linedefs, flags);
        putLe16(linedefs, 0);  // special
        putLe16(linedefs, 0);  // tag
        putLe16(linedefs, static_cast<std::int16_t>(side0));
        putLe16(linedefs, side1 == DOOM_NO_SIDE_INDEX
                              ? static_cast<std::int16_t>(-1)
                              : static_cast<std::int16_t>(side1));
        return index;
    }

    std::uint16_t addSeg(std::uint16_t v1, std::uint16_t v2, std::uint16_t angle,
                         std::uint16_t linedef, std::uint16_t side,
                         std::int16_t offset = 0) {
        const auto index = static_cast<std::uint16_t>(segs.size() / 12u);
        putLe16(segs, static_cast<std::int16_t>(v1));
        putLe16(segs, static_cast<std::int16_t>(v2));
        putLe16(segs, static_cast<std::int16_t>(angle));
        putLe16(segs, static_cast<std::int16_t>(linedef));
        putLe16(segs, static_cast<std::int16_t>(side));
        putLe16(segs, offset);
        return index;
    }

    std::uint16_t addSubsector(std::uint16_t numSegs, std::uint16_t firstSeg) {
        const auto index = static_cast<std::uint16_t>(ssectors.size() / 4u);
        putLe16(ssectors, static_cast<std::int16_t>(numSegs));
        putLe16(ssectors, static_cast<std::int16_t>(firstSeg));
        return index;
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
        return doom_map_open(map, &source);
    }
};

/* --- Textures and the resolver ------------------------------------------ */

/*
 * One texture: `columns` holds `width` distinct texel values and every row of a
 * column shows that value, so a pixel is `colormap[columns[x]]` and the test
 * reads the texture column straight off the screen.
 */
struct TestTexture {
    std::vector<std::uint8_t> pixels;
    std::vector<std::uint8_t> mask;
    r_walltex_t texture{};

    /// A solid texture: every texel of every column is `value`.
    void setSolid(std::uint8_t value, int width, int height) {
        pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                      value);
        mask.clear();
        texture.pixels = pixels.data();
        texture.mask = nullptr;
        texture.width = static_cast<std::int16_t>(width);
        texture.height = static_cast<std::int16_t>(height);
        texture.widthMask = r_texture_width_mask(width);
        texture.heightMask = static_cast<std::int16_t>(height - 1);
    }

    /// One texel value per column, one row tall.
    void setColumns(const std::vector<std::uint8_t>& values) {
        pixels = values;
        mask.clear();
        texture.pixels = pixels.data();
        texture.mask = nullptr;
        texture.width = static_cast<std::int16_t>(values.size());
        texture.height = 1;
        texture.widthMask = r_texture_width_mask(static_cast<int>(values.size()));
        texture.heightMask = 0;
    }

    void setMask(std::vector<std::uint8_t> values) {
        mask = std::move(values);
        texture.mask = mask.data();
    }
};

struct TexEntry {
    r_name_t name;
    const r_walltex_t* texture;
};

/// The residency policy of these tests: a fixed table of names.
struct TextureTable {
    std::vector<TexEntry> entries;

    void add(const char* name, const r_walltex_t* texture) {
        std::array<std::uint8_t, 8> field{};
        for (std::size_t i = 0; i < 8u && name[i] != '\0'; ++i) {
            field[i] = static_cast<std::uint8_t>(name[i]);
        }
        entries.push_back({r_pack_name(field.data()), texture});
    }

    const r_walltex_t* find(r_name_t name) const {
        for (const TexEntry& entry : entries) {
            if (entry.name == name) {
                return entry.texture;
            }
        }
        return nullptr;
    }
};

const r_walltex_t* resolveTexture(void* context, r_name_t name) {
    return static_cast<const TextureTable*>(context)->find(name);
}

/* --- One view: storage, state, surface, light ---------------------------- */

struct Fixture {
    MapBuilder mapBytes;
    doom_map_t map{};
    std::array<r_drawseg_t, 16> drawsegs{};
    std::array<r_visplane_t, 16> planes{};
    std::array<r_clip_range_t, R_MAX_CLIP_SEGS> clips{};
    std::array<std::int16_t, kScreenWidth> floorClip{};
    std::array<std::int16_t, kScreenWidth> ceilingClip{};
    r_view_state_t view{};

    std::vector<std::uint8_t> pixels;
    std::vector<std::uint8_t> covered;
    r_draw_surface_t surface{};
    std::array<std::uint8_t, R_LIGHT_LUMP_MAPS * 256> colormap{};
    r_light_t light{};
    TextureTable textures{};
    r_tex_source_t source{};

    Fixture() {
        pixels.assign(kScreenWidth * kScreenHeight, kUncovered);
        covered.assign(kScreenWidth, 0u);
        surface.pixels = pixels.data();
        surface.width = static_cast<std::int32_t>(kScreenWidth);
        surface.height = static_cast<std::int32_t>(kScreenHeight);
        surface.covered = covered.data();
        surface.tierPixels[R_DRAW_TIER_MID] = 0u;
        surface.tierPixels[R_DRAW_TIER_TOP] = 0u;
        surface.tierPixels[R_DRAW_TIER_BOTTOM] = 0u;

        /* Map 0 is the identity (the brightest table) and map 31 inverts, so a
         * pixel's value says both which texel and which light table was used.
         * Every other map is the identity too. */
        for (std::size_t table = 0; table < R_LIGHT_LUMP_MAPS; ++table) {
            for (std::size_t entry = 0; entry < 256u; ++entry) {
                colormap[table * 256u + entry] =
                    table == 31u ? static_cast<std::uint8_t>(255u - entry)
                                 : static_cast<std::uint8_t>(entry);
            }
        }
        TEST_ASSERT_EQUAL_INT(R_LIGHT_OK,
                              r_light_init(&light, colormap.data(), colormap.size()));

        source.resolve = resolveTexture;
        source.context = &textures;
    }

    void prefill(std::uint8_t value) {
        std::fill(pixels.begin(), pixels.end(), value);
        std::fill(covered.begin(), covered.end(), 0u);
        surface.covered = covered.data();
        surface.tierPixels[R_DRAW_TIER_MID] = 0u;
        surface.tierPixels[R_DRAW_TIER_TOP] = 0u;
        surface.tierPixels[R_DRAW_TIER_BOTTOM] = 0u;
    }

    r_bsp_error_t start() {
        r_view_storage_t storage{};

        TEST_ASSERT_EQUAL_INT(DOOM_MAP_OK, mapBytes.open(&map));
        storage.drawsegs = drawsegs.data();
        storage.drawsegCapacity = static_cast<std::uint16_t>(drawsegs.size());
        storage.planes.planes = planes.data();
        storage.planes.capacity = static_cast<std::uint16_t>(planes.size());
        storage.clipRanges = clips.data();
        storage.clipRangeCapacity = static_cast<std::uint16_t>(clips.size());
        storage.floorClip = floorClip.data();
        storage.ceilingClip = ceilingClip.data();
        return r_bsp_init(&view, &map, &storage);
    }

    r_bsp_error_t walk(fixed_t x, fixed_t y, fixed_t z, angle_t angle,
                       r_name_t sky = R_PLANE_NO_SKY) {
        r_view_params_t params{};

        params.x = x;
        params.y = y;
        params.z = z;
        params.angle = angle;
        params.extralight = 0;
        params.skyPicNum = sky;
        return r_bsp_render(&view, &params);
    }

    r_segs_error_t emit() {
        return r_wall_emit(&view, &surface, &source, &light);
    }

    std::uint8_t at(int x, int y) const {
        return pixels[static_cast<std::size_t>(x)
                      + static_cast<std::size_t>(y) * kScreenWidth];
    }
};

const r_name_t kSky = r_pack_name([] {
    static const std::array<std::uint8_t, 8> field{{'F', '_', 'S', 'K', 'Y', '1', 0, 0}};
    return field.data();
}());

/* --- Scenes -------------------------------------------------------------- */

/*
 * One sector, one subsector, one vertical seg at x = `x` running from +y to -y,
 * so the wall faces a viewer at the origin looking along +x. `high`/`low` are
 * map units and must be large enough for the fragment to cover the frustum.
 */
struct OneWall {
    void build(MapBuilder& map, std::int16_t x, std::int16_t high, std::int16_t low,
               const char* middle, std::int16_t light = 255,
               std::int16_t ceiling = 128, std::int16_t floor = 0) {
        const std::uint16_t sector = map.addSector(floor, ceiling, "FLOOR0", "CEIL0", light);
        const std::uint16_t v1 = map.addVertex(x, high);
        const std::uint16_t v2 = map.addVertex(x, low);
        const std::uint16_t side = map.addSide(0, 0, "-", "-", middle, sector);
        const std::uint16_t line = map.addLine(v1, v2, 0, side);
        map.addSeg(v1, v2, high > low ? 49152u : 16384u, line, 0);
        map.addSubsector(1, 0);
    }
};

/* --- Tests --------------------------------------------------------------- */

void test_one_sided_wall_fills_the_frame() {
    Fixture fixture;
    TestTexture wall;

    wall.setSolid(42u, 8, 8);
    fixture.textures.add("STONE", &wall.texture);
    OneWall().build(fixture.mapBytes, 64, 100, -100, "STONE");

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_UINT16(1u, fixture.view.drawsegCount);
    TEST_ASSERT_EQUAL_INT(R_SEGS_OK, fixture.emit());

    // Every pixel of every column is the one texel the texture holds, through
    // the identity light table: the wall covers the whole 320x200 view.
    for (std::size_t y = 0; y < kScreenHeight; ++y) {
        for (std::size_t x = 0; x < kScreenWidth; ++x) {
            if (fixture.pixels[x + y * kScreenWidth] != 42u) {
                TEST_FAIL_MESSAGE("a pixel of the full-screen wall is not the texel");
            }
        }
    }
    for (std::size_t x = 0; x < kScreenWidth; ++x) {
        TEST_ASSERT_EQUAL_UINT8(1u, fixture.covered[x]);
    }
    TEST_ASSERT_EQUAL_UINT32(320u * 200u, fixture.surface.tierPixels[R_DRAW_TIER_MID]);
    TEST_ASSERT_EQUAL_UINT32(0u, fixture.surface.tierPixels[R_DRAW_TIER_TOP]);
    TEST_ASSERT_EQUAL_UINT32(0u, fixture.surface.tierPixels[R_DRAW_TIER_BOTTOM]);

    // A single-sided line is terminal: the clip arrays collapse to "solid".
    TEST_ASSERT_EQUAL_INT16(R_VIEW_HEIGHT, fixture.ceilingClip[160]);
    TEST_ASSERT_EQUAL_INT16(-1, fixture.floorClip[160]);
}

void test_far_wall_is_clipped_to_the_opening_of_a_two_sided_line() {
    Fixture fixture;
    TestTexture top;
    TestTexture bottom;
    TestTexture far;

    top.setSolid(11u, 8, 8);
    bottom.setSolid(22u, 8, 8);
    far.setSolid(33u, 8, 8);
    fixture.textures.add("TOPTX", &top.texture);
    fixture.textures.add("BOTTXT", &bottom.texture);
    fixture.textures.add("FARTX", &far.texture);

    MapBuilder& map = fixture.mapBytes;

    // Front room: floor 0, ceiling 128. Back room: floor 32, ceiling 72. The
    // near line therefore has both an upper and a lower tier, and the eye at
    // 41 looks through the 72..32 opening.
    const std::uint16_t front = map.addSector(0, 128, "FLOOR0", "CEIL0", 255);
    const std::uint16_t back = map.addSector(32, 72, "FLOOR1", "CEIL1", 255);
    const std::uint16_t nearV1 = map.addVertex(64, 100);
    const std::uint16_t nearV2 = map.addVertex(64, -100);
    const std::uint16_t nearSide0 = map.addSide(0, 0, "TOPTX", "BOTTXT", "-", front);
    const std::uint16_t nearSide1 = map.addSide(0, 0, "-", "-", "-", back);
    const std::uint16_t nearLine =
        map.addLine(nearV1, nearV2, DOOM_ML_TWOSIDED, nearSide0, nearSide1);
    map.addSeg(nearV1, nearV2, 49152u, nearLine, 0);

    // A solid wall behind it, 64 units further away, filling the frustum.
    const std::uint16_t solidSector = map.addSector(0, 128, "FLOOR0", "CEIL0", 255);
    const std::uint16_t farV1 = map.addVertex(128, 200);
    const std::uint16_t farV2 = map.addVertex(128, -200);
    const std::uint16_t farSide = map.addSide(0, 0, "-", "-", "FARTX", solidSector);
    const std::uint16_t farLine = map.addLine(farV1, farV2, 0, farSide);
    map.addSeg(farV1, farV2, 49152u, farLine, 0);

    map.addSubsector(2, 0);

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_UINT16(2u, fixture.view.drawsegCount);
    TEST_ASSERT_EQUAL_INT(R_SEGS_OK, fixture.emit());

    // Column 160, top to bottom: the upper tier, then the opening the far wall
    // fills, then the lower tier. Each run is contiguous and non-empty, and no
    // other value appears -- in particular the far wall was emitted second and
    // overwrote none of the near tiers.
    int topRows = 0;
    int farRows = 0;
    int bottomRows = 0;
    int stage = 0;

    for (int y = 0; y < static_cast<int>(kScreenHeight); ++y) {
        const std::uint8_t value = fixture.at(160, y);

        if (stage == 0) {
            if (value == 11u) {
                ++topRows;
                continue;
            }
            TEST_ASSERT_TRUE_MESSAGE(topRows > 0, "the upper tier drew nothing");
            stage = 1;
        }
        if (stage == 1) {
            if (value == 33u) {
                ++farRows;
                continue;
            }
            TEST_ASSERT_TRUE_MESSAGE(farRows > 0, "the far wall drew nothing");
            stage = 2;
        }
        TEST_ASSERT_EQUAL_UINT8(22u, value);
        ++bottomRows;
    }

    TEST_ASSERT_TRUE(bottomRows > 0);
    // Every one of the 320 columns shows the same three runs, so the counters
    // are the column's row counts times the number of columns.
    TEST_ASSERT_EQUAL_UINT32(static_cast<std::uint32_t>(topRows)
                                 * static_cast<std::uint32_t>(kScreenWidth),
                             fixture.surface.tierPixels[R_DRAW_TIER_TOP]);
    TEST_ASSERT_EQUAL_UINT32(static_cast<std::uint32_t>(bottomRows)
                                 * static_cast<std::uint32_t>(kScreenWidth),
                             fixture.surface.tierPixels[R_DRAW_TIER_BOTTOM]);
    TEST_ASSERT_EQUAL_UINT8(1u, fixture.covered[160]);

    // The far fragment is a single-sided wall, so it collapsed the clip arrays
    // once it drew: what proves the clipping is the pixel runs above, not the
    // final clip state.
    TEST_ASSERT_EQUAL_INT16(R_VIEW_HEIGHT, fixture.ceilingClip[160]);
    TEST_ASSERT_EQUAL_INT16(-1, fixture.floorClip[160]);
}

void test_mid_texture_mask_leaves_transparent_texels_alone() {
    Fixture fixture;
    TestTexture wall;

    wall.setSolid(42u, 4, 8);
    wall.setMask({1u, 1u, 1u, 1u, 0u, 0u, 0u, 0u});
    fixture.textures.add("MASKED", &wall.texture);
    OneWall().build(fixture.mapBytes, 64, 100, -100, "MASKED");

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_INT(R_SEGS_OK, fixture.emit());

    std::uint32_t written = 0u;
    for (std::size_t y = 0; y < kScreenHeight; ++y) {
        for (std::size_t x = 0; x < kScreenWidth; ++x) {
            const std::uint8_t value = fixture.pixels[x + y * kScreenWidth];

            TEST_ASSERT_TRUE(value == 42u || value == kUncovered);
            if (value == 42u) {
                ++written;
            }
        }
    }

    // Half the texels are transparent, so half the wall's rows stay uncovered.
    TEST_ASSERT_TRUE(written > 0u);
    TEST_ASSERT_TRUE(written < 320u * 200u);
    TEST_ASSERT_EQUAL_UINT32(written, fixture.surface.tierPixels[R_DRAW_TIER_MID]);
}

void test_texture_column_wraps_at_the_texture_width() {
    Fixture fixture;
    TestTexture wall;

    // Eight texels wide, one row tall, one distinct value per column.
    const std::vector<std::uint8_t> values{10u, 11u, 12u, 13u, 14u, 15u, 16u, 17u};
    wall.setColumns(values);
    fixture.textures.add("BANDS", &wall.texture);

    // A short segment on a 100-unit-distant wall: 12 map units of height at
    // that distance is about +-3.4 degrees, which the 320-wide 90-degree
    // projection turns into a little over twenty columns.
    OneWall().build(fixture.mapBytes, 100, 6, -6, "BANDS");

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_UINT16(1u, fixture.view.drawsegCount);
    TEST_ASSERT_EQUAL_INT(R_SEGS_OK, fixture.emit());

    const r_drawseg_t& drawseg = fixture.drawsegs[0];
    const int columns = static_cast<int>(drawseg.x2) - static_cast<int>(drawseg.x1) + 1;

    TEST_ASSERT_TRUE(columns > 8);   // the texture wraps at least once
    TEST_ASSERT_TRUE(columns < 32);  // and stays the small fragment it claims

    // The documented rule, from the public data: texturecolumn = rwOffset -
    // FixedMul(tangent(centerAngle + xtoviewangle[x]), rwDistance), then masked
    // with the texture's width mask.
    for (int x = static_cast<int>(drawseg.x1); x <= static_cast<int>(drawseg.x2); ++x) {
        const angle_t angle = drawseg.rwCenterAngle + r_x_to_view_angle[x];
        const fixed_t textureColumn =
            (drawseg.rwOffset
             - static_cast<fixed_t>(
                 (static_cast<std::int64_t>(r_fine_tangent_lookup(angle))
                  * static_cast<std::int64_t>(drawseg.rwDistance)) >> 16))
            >> 16;
        const int texel = static_cast<int>(textureColumn) & wall.texture.widthMask;
        const std::uint8_t expected = values[static_cast<std::size_t>(texel)];

        TEST_ASSERT_EQUAL_UINT8(expected, fixture.at(x, 20));
        TEST_ASSERT_TRUE(fixture.at(x, 20) >= 10u && fixture.at(x, 20) <= 17u);
    }

    // Outside the fragment nothing was written.
    TEST_ASSERT_EQUAL_UINT8(kUncovered,
                            fixture.at(static_cast<int>(drawseg.x1) - 1, 20));
}

void test_sky_ceiling_leaves_the_clip_array_alone() {
    Fixture fixture;
    TestTexture top;
    TestTexture bottom;

    top.setSolid(11u, 8, 8);
    bottom.setSolid(22u, 8, 8);
    fixture.textures.add("TOPTX", &top.texture);
    fixture.textures.add("BOTTXT", &bottom.texture);

    MapBuilder& map = fixture.mapBytes;

    // Both ceilings are the sky flat and the floors are equal: the outdoor
    // height hack makes worldTop == worldHigh, worldLow == worldBottom, and with
    // equal pictures and light levels neither plane is marked. The fragment
    // exists, and it must draw nothing and leave ceilingclip[] at -1.
    const std::uint16_t front = map.addSector(0, 128, "FLOOR0", "F_SKY1", 255);
    const std::uint16_t back = map.addSector(0, 72, "FLOOR0", "F_SKY1", 255);
    const std::uint16_t v1 = map.addVertex(64, 100);
    const std::uint16_t v2 = map.addVertex(64, -100);
    const std::uint16_t side0 = map.addSide(0, 0, "TOPTX", "BOTTXT", "-", front);
    const std::uint16_t side1 = map.addSide(0, 0, "-", "-", "-", back);
    const std::uint16_t line = map.addLine(v1, v2, DOOM_ML_TWOSIDED, side0, side1);
    map.addSeg(v1, v2, 49152u, line, 0);
    map.addSubsector(1, 0);

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0, kSky));
    TEST_ASSERT_EQUAL_UINT16(1u, fixture.view.drawsegCount);
    TEST_ASSERT_EQUAL_INT(R_SEGS_OK, fixture.emit());

    for (std::size_t x = 0; x < kScreenWidth; ++x) {
        TEST_ASSERT_EQUAL_INT16(-1, fixture.ceilingClip[x]);
        TEST_ASSERT_EQUAL_INT16(R_VIEW_HEIGHT, fixture.floorClip[x]);
        TEST_ASSERT_EQUAL_UINT8(0u, fixture.covered[x]);
    }
    for (std::size_t y = 0; y < kScreenHeight; ++y) {
        for (std::size_t x = 0; x < kScreenWidth; ++x) {
            TEST_ASSERT_EQUAL_UINT8(kUncovered, fixture.pixels[x + y * kScreenWidth]);
        }
    }
    TEST_ASSERT_EQUAL_UINT32(0u, fixture.surface.tierPixels[R_DRAW_TIER_TOP]);

    // The contrast: the same geometry with two different, non-sky ceiling
    // pictures marks the ceiling, and then ceilingclip[] does move.
    Fixture other;
    TestTexture plain;

    plain.setSolid(11u, 8, 8);
    other.textures.add("TOPTX", &plain.texture);
    other.textures.add("BOTTXT", &plain.texture);

    MapBuilder& otherMap = other.mapBytes;
    const std::uint16_t otherFront =
        otherMap.addSector(0, 81, "FLOOR0", "CEILA", 255);
    const std::uint16_t otherBack = otherMap.addSector(0, 81, "FLOOR0", "CEILB", 255);
    const std::uint16_t otherV1 = otherMap.addVertex(160, 200);
    const std::uint16_t otherV2 = otherMap.addVertex(160, -200);
    const std::uint16_t otherSide0 =
        otherMap.addSide(0, 0, "TOPTX", "BOTTXT", "-", otherFront);
    const std::uint16_t otherSide1 = otherMap.addSide(0, 0, "-", "-", "-", otherBack);
    const std::uint16_t otherLine =
        otherMap.addLine(otherV1, otherV2, DOOM_ML_TWOSIDED, otherSide0, otherSide1);
    otherMap.addSeg(otherV1, otherV2, 49152u, otherLine, 0);
    otherMap.addSubsector(1, 0);

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, other.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, other.walk(0, 0, 41 * kFracUnit, 0, kSky));
    TEST_ASSERT_EQUAL_UINT16(1u, other.view.drawsegCount);
    TEST_ASSERT_EQUAL_INT(R_SEGS_OK, other.emit());

    TEST_ASSERT_TRUE(other.ceilingClip[160] >= 0);
    TEST_ASSERT_TRUE(other.ceilingClip[160] < static_cast<std::int16_t>(R_VIEW_HEIGHT));
    // No tier exists, so no pixel was written; only the clip array moved.
    TEST_ASSERT_EQUAL_UINT8(kUncovered, other.at(160, 20));
    TEST_ASSERT_EQUAL_UINT8(0u, other.covered[160]);
}

void test_light_level_selects_a_different_table() {
    Fixture bright;
    Fixture dark;
    TestTexture wall;
    TestTexture wallDark;

    wall.setSolid(42u, 8, 8);
    wallDark.setSolid(42u, 8, 8);
    bright.textures.add("STONE", &wall.texture);
    dark.textures.add("STONE", &wallDark.texture);
    OneWall().build(bright.mapBytes, 64, 100, -100, "STONE", 255);
    OneWall().build(dark.mapBytes, 64, 100, -100, "STONE", 0);

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, bright.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, bright.walk(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_INT(R_SEGS_OK, bright.emit());

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, dark.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, dark.walk(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_INT(R_SEGS_OK, dark.emit());

    // Light 255 with a vertical seg is lightNum 16, clamped to row 15, whose
    // start map is 0: the identity table, so the texel appears unchanged.
    TEST_ASSERT_EQUAL_INT16(16, bright.drawsegs[0].lightNum);
    TEST_ASSERT_EQUAL_UINT8(42u, bright.at(160, 100));
    // Light 0 is lightNum 1, whose row clamps to map 31 for every scale, and
    // map 31 inverts.
    TEST_ASSERT_EQUAL_INT16(1, dark.drawsegs[0].lightNum);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(255u - 42u), dark.at(160, 100));
    TEST_ASSERT_NOT_EQUAL(bright.at(160, 100), dark.at(160, 100));

    // The level lookup itself, and its two clamps.
    TEST_ASSERT_EQUAL_UINT8(42u, r_light_wall(&bright.light, 15, 0)[42]);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(255u - 42u),
                            r_light_wall(&bright.light, 0, 0)[42]);
    // Row 0 is the darkest band and its start map is 60, past the last usable
    // map for every scale, so a negative light number clamps to the darkest map.
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(255u - 42u),
                            r_light_wall(&bright.light, -5, 0)[42]);
    TEST_ASSERT_EQUAL_UINT8(42u, r_light_wall(&bright.light, 99, 0)[42]);
    TEST_ASSERT_EQUAL_UINT8(42u, r_light_wall_level(&bright.light, 15)[42]);
    TEST_ASSERT_TRUE(r_light_wall(NULL, 15, 0) == nullptr);
}

void test_a_non_resident_texture_fails_the_whole_call() {
    Fixture fixture;

    // No texture is registered, so "STONE" resolves to NULL.
    OneWall().build(fixture.mapBytes, 64, 100, -100, "STONE");
    fixture.prefill(0x5Au);

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_UINT16(1u, fixture.view.drawsegCount);

    TEST_ASSERT_EQUAL_INT(R_SEGS_ERR_TEXTURE_MISSING, fixture.emit());

    for (std::size_t i = 0; i < fixture.pixels.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT8(0x5Au, fixture.pixels[i]);
    }
    for (std::size_t x = 0; x < kScreenWidth; ++x) {
        TEST_ASSERT_EQUAL_UINT8(0u, fixture.covered[x]);
    }
    TEST_ASSERT_EQUAL_UINT32(0u, fixture.surface.tierPixels[R_DRAW_TIER_MID]);
}

void test_a_column_storage_height_that_is_not_a_power_of_two_is_rejected() {
    Fixture fixture;
    TestTexture wall;

    wall.setSolid(42u, 8, 8);
    // 72 declared rows stored as 72 rows: 71 is not a power-of-two-minus-one,
    // so the vertical AND would not be a wrap. DOOM stores 128 rows per column
    // whatever the texture's height, and so must a caller with such a texture.
    wall.pixels.assign(8u * 72u, 42u);
    wall.texture.pixels = wall.pixels.data();
    wall.texture.height = 72;
    wall.texture.heightMask = 71;
    fixture.textures.add("STONE", &wall.texture);
    OneWall().build(fixture.mapBytes, 64, 100, -100, "STONE");
    fixture.prefill(0x11u);

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_INT(R_SEGS_ERR_TEXTURE_SHAPE, fixture.emit());

    for (std::size_t i = 0; i < fixture.pixels.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT8(0x11u, fixture.pixels[i]);
    }

    // A mask smaller than the declared height is the other unusable shape: the
    // stored column could not hold the texture at all.
    wall.pixels.assign(8u * 8u, 42u);
    wall.texture.pixels = wall.pixels.data();
    wall.texture.height = 72;
    wall.texture.heightMask = 7;
    TEST_ASSERT_EQUAL_INT(R_SEGS_ERR_TEXTURE_SHAPE, fixture.emit());

    // ... while 72 declared rows in a 128-row column is exactly what DOOM does.
    wall.pixels.assign(8u * 128u, 42u);
    wall.texture.pixels = wall.pixels.data();
    wall.texture.height = 72;
    wall.texture.heightMask = 127;
    TEST_ASSERT_EQUAL_INT(R_SEGS_OK, fixture.emit());
    TEST_ASSERT_EQUAL_UINT8(42u, fixture.at(160, 100));
}

void test_a_width_mask_that_is_not_the_documented_one_is_rejected() {
    Fixture fixture;
    TestTexture wall;

    // 72 wide: DOOM's mask is 63, not 71.
    std::vector<std::uint8_t> pixels(72u * 8u, 42u);
    wall.pixels = std::move(pixels);
    wall.texture.pixels = wall.pixels.data();
    wall.texture.mask = nullptr;
    wall.texture.width = 72;
    wall.texture.height = 8;
    wall.texture.heightMask = 7;
    wall.texture.widthMask = 71;
    fixture.textures.add("STONE", &wall.texture);
    OneWall().build(fixture.mapBytes, 64, 100, -100, "STONE");

    TEST_ASSERT_EQUAL_INT(63, r_texture_width_mask(72));
    TEST_ASSERT_EQUAL_INT(0, r_texture_width_mask(1));
    TEST_ASSERT_EQUAL_INT(7, r_texture_width_mask(8));

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_INT(R_SEGS_ERR_TEXTURE_SHAPE, fixture.emit());
}

void test_small_surface_is_rejected_before_any_write() {
    Fixture fixture;
    TestTexture wall;

    wall.setSolid(42u, 8, 8);
    fixture.textures.add("STONE", &wall.texture);
    OneWall().build(fixture.mapBytes, 64, 100, -100, "STONE");

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));

    fixture.surface.width = 160;
    TEST_ASSERT_EQUAL_INT(R_SEGS_ERR_SURFACE_SIZE, fixture.emit());

    fixture.surface.width = static_cast<std::int32_t>(kScreenWidth);
    fixture.surface.height = 100;
    TEST_ASSERT_EQUAL_INT(R_SEGS_ERR_SURFACE_SIZE, fixture.emit());

    fixture.surface.height = static_cast<std::int32_t>(kScreenHeight);
    fixture.surface.pixels = nullptr;
    TEST_ASSERT_EQUAL_INT(R_SEGS_ERR_SURFACE_PIXELS, fixture.emit());

    fixture.surface.pixels = fixture.pixels.data();
    fixture.surface.covered = nullptr;
    TEST_ASSERT_EQUAL_INT(R_SEGS_OK, fixture.emit());
    TEST_ASSERT_EQUAL_UINT8(42u, fixture.at(160, 100));
}

void test_light_init_validates_the_lump_layout() {
    Fixture fixture;
    r_light_t light{};
    std::vector<std::uint8_t> shortLump(256u * 8u, 0u);
    std::vector<std::uint8_t> ragged(300u, 0u);

    TEST_ASSERT_EQUAL_INT(R_LIGHT_ERR_NULL_STATE,
                          r_light_init(nullptr, fixture.colormap.data(),
                                       fixture.colormap.size()));
    TEST_ASSERT_EQUAL_INT(R_LIGHT_ERR_NULL_DATA,
                          r_light_init(&light, nullptr, fixture.colormap.size()));
    TEST_ASSERT_EQUAL_INT(R_LIGHT_ERR_LUMP_SIZE,
                          r_light_init(&light, ragged.data(), ragged.size()));
    TEST_ASSERT_EQUAL_INT(R_LIGHT_ERR_MAP_COUNT,
                          r_light_init(&light, shortLump.data(), shortLump.size()));
    TEST_ASSERT_TRUE(light.colormaps == nullptr);

    TEST_ASSERT_EQUAL_INT(R_LIGHT_OK,
                          r_light_init(&light, fixture.colormap.data(),
                                       fixture.colormap.size()));
    TEST_ASSERT_TRUE(light.colormaps == fixture.colormap.data());
}

void test_drawseg_records_the_three_range_values() {
    Fixture fixture;
    TestTexture wall;

    wall.setSolid(42u, 8, 8);
    fixture.textures.add("STONE", &wall.texture);
    OneWall().build(fixture.mapBytes, 64, 100, -100, "STONE");

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));

    const r_drawseg_t& drawseg = fixture.drawsegs[0];

    // The wall is 64 units away along its normal (the x axis). The fine tables
    // are truncated, so the sine of the distance angle is a few thousandths of
    // a unit short of the exact 64: one unit of tolerance, not one step.
    TEST_ASSERT_INT32_WITHIN(kFracUnit, 64 * kFracUnit, drawseg.rwDistance);
    // The seg's own offset and the sidedef's texture offset are both zero, and
    // the normal turns +ANG90 from the direction to the first vertex, so the u
    // origin is the first vertex's tangential distance: +100 units for a wall
    // centred on the view direction.
    TEST_ASSERT_INT32_WITHIN(kFracUnit, 100 * kFracUnit, drawseg.rwOffset);
    // The view looks along the normal, so the centre angle is ANG90.
    TEST_ASSERT_EQUAL_UINT32(ANG90, drawseg.rwCenterAngle);
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    test_setup();
    UNITY_BEGIN();

    RUN_TEST(test_one_sided_wall_fills_the_frame);
    RUN_TEST(test_far_wall_is_clipped_to_the_opening_of_a_two_sided_line);
    RUN_TEST(test_mid_texture_mask_leaves_transparent_texels_alone);
    RUN_TEST(test_texture_column_wraps_at_the_texture_width);
    RUN_TEST(test_sky_ceiling_leaves_the_clip_array_alone);
    RUN_TEST(test_light_level_selects_a_different_table);
    RUN_TEST(test_a_non_resident_texture_fails_the_whole_call);
    RUN_TEST(test_a_column_storage_height_that_is_not_a_power_of_two_is_rejected);
    RUN_TEST(test_a_width_mask_that_is_not_the_documented_one_is_rejected);
    RUN_TEST(test_small_surface_is_rejected_before_any_write);
    RUN_TEST(test_light_init_validates_the_lump_layout);
    RUN_TEST(test_drawseg_records_the_three_range_values);

    const int result = UNITY_END();
    test_teardown();
    return result;
}
