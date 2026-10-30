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
 * D3.3 adds the plane pass (r_plane.c), on hand-built visplanes rather than on a
 * walked map, because the walk cannot produce the runs these cases need:
 *
 *  - one plane covering the whole surface draws every pixel and 200 spans;
 *  - two disjoint column runs in one plane are two spans, and the gap between
 *    them keeps the caller's bytes;
 *  - a column whose top is below its bottom is skipped, with no pixel and no
 *    span;
 *  - a flat longer than 64 columns wraps with R_DrawSpan's `& 63`, asserted
 *    against the documented span formula at every column;
 *  - light level 0 and 255 select different plane maps (zlight, not the wall's
 *    scalelight), and the expected map is the generated table's entry;
 *  - the sky plane overwrites its columns with the sky texture's bytes and never
 *    resolves a flat, while a non-sky plane draws its flat;
 *  - two planes in index order do not share a span run;
 *  - a missing flat, a misshapen flat, a missing sky texture and a malformed sky
 *    texture each fail the call and leave the surface, plane mask and counters
 *    byte for byte as they were.
 *
 * The tests go through the public C API only. They never inspect the emitter's
 * internals, so an implementation that passes here is free to change them.
 */
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

#include <unity.h>

#include "doom_map.h"
#include "r_bsp.h"
#include "r_light.h"
#include "r_plane.h"
#include "r_segs.h"
#include "r_tables.h"
#include "r_things.h"

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

/* --- Flats and the flat resolver ----------------------------------------- */

/*
 * One flat: 64 x 64 raw bytes, exactly what R_DrawSpan addresses. `setSolid`
 * gives every texel the same value, `setPattern` gives each texel its linear
 * index (so a pixel's value is also its position in the flat), and the row/column
 * accessor is there for tests that need a known value at one texel.
 */
struct TestFlat {
    std::vector<std::uint8_t> pixels;
    r_flat_t flat{};

    void setSolid(std::uint8_t value) {
        pixels.assign(64u * 64u, value);
        flat.pixels = pixels.data();
        flat.width = 64;
        flat.height = 64;
        flat.widthMask = 63;
        flat.heightMask = 63;
    }

    void setPattern() {
        pixels.resize(64u * 64u);
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            pixels[i] = static_cast<std::uint8_t>(i & 0xFFu);
        }
        flat.pixels = pixels.data();
        flat.width = 64;
        flat.height = 64;
        flat.widthMask = 63;
        flat.heightMask = 63;
    }

    std::uint8_t at(int row, int column) const {
        return pixels[static_cast<std::size_t>(row) * 64u
                      + static_cast<std::size_t>(column)];
    }
};

struct FlatEntry {
    r_name_t name;
    const r_flat_t* flat;
};

/// The residency policy of these tests: a fixed table of flat names. `lookups`
/// counts the resolver calls, so a test can prove the sky path never asks for a
/// flat.
struct FlatTable {
    std::vector<FlatEntry> entries;
    int lookups = 0;

    void add(const char* name, const r_flat_t* flat) {
        std::array<std::uint8_t, 8> field{};
        for (std::size_t i = 0; i < 8u && name[i] != '\0'; ++i) {
            field[i] = static_cast<std::uint8_t>(name[i]);
        }
        entries.push_back({r_pack_name(field.data()), flat});
    }

    const r_flat_t* find(r_name_t name) {
        ++lookups;
        for (const FlatEntry& entry : entries) {
            if (entry.name == name) {
                return entry.flat;
            }
        }
        return nullptr;
    }
};

const r_flat_t* resolveFlat(void* context, r_name_t name) {
    return static_cast<FlatTable*>(context)->find(name);
}

/* --- Sprite patches and the sprite resolver (D3.4) ----------------------- */

/*
 * A patch built byte for byte: `patch_t` header, the column-offset table with
 * the four zero bytes DOOM writes between columns, then one normalised post
 * chain per column. Every sprite in this suite is a column of solid bands, so a
 * pixel's value says which texture column and which post row drew it.
 */
struct TestPatch {
    std::vector<std::uint8_t> data;
    r_sprite_patch_t patch{};

    /// One post per column covering every row; `values[x]` is column x's texel.
    void setBands(const std::vector<std::uint8_t>& values, int height, int left,
                  int top) {
        build(static_cast<int>(values.size()), height, left, top,
              [&](int column) {
                  return std::vector<std::pair<int, std::vector<std::uint8_t>>>{
                      {0, std::vector<std::uint8_t>(
                              static_cast<std::size_t>(height), values[column])}};
              });
    }

    /// A solid patch: every texel of every column is `value`.
    void setSolid(std::uint8_t value, int width, int height, int left, int top) {
        setBands(std::vector<std::uint8_t>(static_cast<std::size_t>(width), value),
                 height, left, top);
    }

    /// The general form: per column, a list of (topdelta, pixels) posts.
    void build(int width, int height, int left, int top,
               const std::function<std::vector<std::pair<int, std::vector<std::uint8_t>>>(
                   int)>& column) {
        std::vector<std::uint8_t> body;
        std::vector<std::int32_t> offsets(static_cast<std::size_t>(width));

        putLe16(data, static_cast<std::int16_t>(width));
        putLe16(data, static_cast<std::int16_t>(height));
        putLe16(data, static_cast<std::int16_t>(left));
        putLe16(data, static_cast<std::int16_t>(top));
        const std::size_t tableEnd = 8u + static_cast<std::size_t>(width) * 4u;

        for (int x = 0; x < width; ++x) {
            offsets[static_cast<std::size_t>(x)] =
                static_cast<std::int32_t>(tableEnd + body.size());
            for (const auto& post : column(x)) {
                body.push_back(static_cast<std::uint8_t>(post.first));
                body.push_back(static_cast<std::uint8_t>(post.second.size()));
                body.push_back(0u);
                body.insert(body.end(), post.second.begin(), post.second.end());
                body.push_back(0u);
            }
            body.push_back(0xFFu);
        }

        for (std::int32_t offset : offsets) {
            data.push_back(static_cast<std::uint8_t>(offset & 0xFF));
            data.push_back(static_cast<std::uint8_t>((offset >> 8) & 0xFF));
            data.push_back(static_cast<std::uint8_t>((offset >> 16) & 0xFF));
            data.push_back(static_cast<std::uint8_t>((offset >> 24) & 0xFF));
        }
        data.insert(data.end(), body.begin(), body.end());
        patch.data = data.data();
        patch.size = data.size();
    }
};

struct SpriteEntry {
    r_name_t name;
    const r_sprite_patch_t* patch;
};

/// The tests' sprite residency policy: a fixed table of lump names.
struct SpriteTable {
    std::vector<SpriteEntry> entries;
    int lookups = 0;

    void add(const char* name, const r_sprite_patch_t* patch) {
        std::array<std::uint8_t, 8> field{};
        for (std::size_t i = 0; i < 8u && name[i] != '\0'; ++i) {
            field[i] = static_cast<std::uint8_t>(name[i]);
        }
        entries.push_back({r_pack_name(field.data()), patch});
    }

    const r_sprite_patch_t* find(r_name_t name) {
        ++lookups;
        for (const SpriteEntry& entry : entries) {
            if (entry.name == name) {
                return entry.patch;
            }
        }
        return nullptr;
    }
};

const r_sprite_patch_t* resolveSprite(void* context, r_name_t name) {
    return static_cast<SpriteTable*>(context)->find(name);
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
    std::array<r_vissprite_t, 16> visSprites{};
    std::array<std::int16_t, 32u * kScreenWidth> clipBlocks{};
    std::array<std::int16_t, 8u * kScreenWidth> openingBlocks{};
    r_view_state_t view{};

    std::vector<std::uint8_t> pixels;
    std::vector<std::uint8_t> covered;
    std::vector<std::uint8_t> planeMask;
    std::vector<std::uint8_t> spriteMask;
    r_draw_surface_t surface{};
    std::array<std::uint8_t, R_LIGHT_LUMP_MAPS * 256> colormap{};
    r_light_t light{};
    TextureTable textures{};
    FlatTable flats{};
    SpriteTable sprites{};
    r_tex_source_t source{};
    r_sprite_source_t spriteSource{};
    r_masked_stats_t stats{};

    Fixture() {
        pixels.assign(kScreenWidth * kScreenHeight, kUncovered);
        covered.assign(kScreenWidth, 0u);
        planeMask.assign(kScreenWidth * kScreenHeight, 0u);
        spriteMask.assign(kScreenWidth * kScreenHeight, 0u);
        surface.pixels = pixels.data();
        surface.width = static_cast<std::int32_t>(kScreenWidth);
        surface.height = static_cast<std::int32_t>(kScreenHeight);
        surface.covered = covered.data();
        surface.planeMask = planeMask.data();
        surface.spriteMask = spriteMask.data();
        surface.tierPixels[R_DRAW_TIER_MID] = 0u;
        surface.tierPixels[R_DRAW_TIER_TOP] = 0u;
        surface.tierPixels[R_DRAW_TIER_BOTTOM] = 0u;
        surface.planePixels = 0u;
        surface.planeSpans = 0u;
        surface.spritePixels = 0u;

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
        source.resolveFlat = resolveFlat;
        source.flatContext = &flats;
        source.skyTexture = R_NAME_NONE;
        spriteSource.resolve = resolveSprite;
        spriteSource.context = &sprites;
    }

    void prefill(std::uint8_t value) {
        std::fill(pixels.begin(), pixels.end(), value);
        std::fill(covered.begin(), covered.end(), 0u);
        std::fill(planeMask.begin(), planeMask.end(), 0u);
        std::fill(spriteMask.begin(), spriteMask.end(), 0u);
        surface.covered = covered.data();
        surface.planeMask = planeMask.data();
        surface.spriteMask = spriteMask.data();
        surface.tierPixels[R_DRAW_TIER_MID] = 0u;
        surface.tierPixels[R_DRAW_TIER_TOP] = 0u;
        surface.tierPixels[R_DRAW_TIER_BOTTOM] = 0u;
        surface.planePixels = 0u;
        surface.planeSpans = 0u;
        surface.spritePixels = 0u;
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
        storage.visSprites = visSprites.data();
        storage.visSpriteCapacity = static_cast<std::uint16_t>(visSprites.size());
        storage.clipBlocks = clipBlocks.data();
        storage.clipBlockCapacity = 32u;
        storage.openingBlocks = openingBlocks.data();
        storage.openingBlockCapacity = 8u;
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

    r_plane_error_t drawPlanes() {
        return r_plane_draw(&view, &surface, &source, &light);
    }

    r_things_error_t drawMasked(const r_sprite_t* list, std::uint16_t count) {
        return r_masked_emit(&view, &surface, &source, &light, list, count,
                             &spriteSource, &stats);
    }

    std::uint8_t at(int x, int y) const {
        return pixels[static_cast<std::size_t>(x)
                      + static_cast<std::size_t>(y) * kScreenWidth];
    }

    std::uint8_t maskAt(int x, int y) const {
        return planeMask[static_cast<std::size_t>(x)
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

/* --- Hand-built visplanes ------------------------------------------------ */

r_name_t packName(const char* name) {
    std::array<std::uint8_t, 8> field{};

    for (std::size_t i = 0; i < 8u && name[i] != '\0'; ++i) {
        field[i] = static_cast<std::uint8_t>(name[i]);
    }
    return r_pack_name(field.data());
}

/*
 * The plane pass reads the plane set the walk produced, so a test that needs a
 * run the walk could not produce (a whole-surface plane, a plane with an empty
 * column in the middle, a top below its own bottom) walks a trivial map for the
 * viewpoint and then replaces the set. `state.params` is what
 * r_bsp_render() left there: (0, 0, 41, 0), the same viewpoint every existing
 * test uses.
 */
void clearPlanes(Fixture& fixture) {
    fixture.view.storage.planes.count = 0;
}

r_visplane_t* addPlane(Fixture& fixture, fixed_t height, const char* picture,
                       std::int16_t light) {
    r_visplane_t* plane = &fixture.planes[fixture.view.storage.planes.count++];

    std::memset(plane->top, 0xFF, sizeof plane->top);
    std::memset(plane->bottom, 0xFF, sizeof plane->bottom);
    plane->height = height;
    plane->picNum = packName(picture);
    plane->lightLevel = light;
    plane->minX = static_cast<std::int16_t>(kScreenWidth);
    plane->maxX = -1;
    return plane;
}

void setRun(r_visplane_t* plane, int first, int last, int top, int bottom) {
    for (int x = first; x <= last; ++x) {
        plane->top[x] = static_cast<std::uint8_t>(top);
        plane->bottom[x] = static_cast<std::uint8_t>(bottom);
    }
    if (first < plane->minX) {
        plane->minX = static_cast<std::int16_t>(first);
    }
    if (last > plane->maxX) {
        plane->maxX = static_cast<std::int16_t>(last);
    }
}

const r_name_t kSkyTexture = packName("SKY1");

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

/* --- D3.3: the plane pass ------------------------------------------------ */

void test_a_single_plane_fills_the_whole_surface() {
    Fixture fixture;
    TestFlat flat;

    flat.setSolid(0x37u);
    fixture.flats.add("FLAT1", &flat.flat);
    OneWall().build(fixture.mapBytes, 64, 100, -100, "STONE");

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));

    clearPlanes(fixture);
    setRun(addPlane(fixture, 0, "FLAT1", 255), 0, 319, 0, 199);
    fixture.prefill(0x5Au);

    TEST_ASSERT_EQUAL_INT(R_PLANE_OK, fixture.drawPlanes());

    for (std::size_t y = 0; y < kScreenHeight; ++y) {
        for (std::size_t x = 0; x < kScreenWidth; ++x) {
            if (fixture.pixels[x + y * kScreenWidth] != 0x37u) {
                TEST_FAIL_MESSAGE("a pixel of the full-surface plane is not the flat's texel");
            }
            TEST_ASSERT_EQUAL_UINT8(1u, fixture.planeMask[x + y * kScreenWidth]);
        }
    }
    for (std::size_t x = 0; x < kScreenWidth; ++x) {
        TEST_ASSERT_EQUAL_UINT8(1u, fixture.covered[x]);
    }
    // One span per row: the run is the same on all 200 rows.
    TEST_ASSERT_EQUAL_UINT32(320u * 200u, fixture.surface.planePixels);
    TEST_ASSERT_EQUAL_UINT32(200u, fixture.surface.planeSpans);
    // The wall counters are not this pass's to touch.
    TEST_ASSERT_EQUAL_UINT32(0u, fixture.surface.tierPixels[R_DRAW_TIER_MID]);
    TEST_ASSERT_EQUAL_UINT32(0u, fixture.surface.tierPixels[R_DRAW_TIER_TOP]);
    TEST_ASSERT_EQUAL_UINT32(0u, fixture.surface.tierPixels[R_DRAW_TIER_BOTTOM]);
}

void test_two_disjoint_runs_in_one_plane_are_two_spans() {
    Fixture fixture;
    TestFlat flat;

    flat.setSolid(0x21u);
    fixture.flats.add("FLAT2", &flat.flat);
    OneWall().build(fixture.mapBytes, 64, 100, -100, "STONE");

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));

    clearPlanes(fixture);
    {
        r_visplane_t* plane = addPlane(fixture, 0, "FLAT2", 255);

        setRun(plane, 10, 49, 10, 10);
        setRun(plane, 100, 149, 10, 10);
        TEST_ASSERT_EQUAL_INT16(10, plane->minX);
        TEST_ASSERT_EQUAL_INT16(149, plane->maxX);
    }
    fixture.prefill(0x5Au);

    TEST_ASSERT_EQUAL_INT(R_PLANE_OK, fixture.drawPlanes());

    for (int x = 10; x <= 49; ++x) {
        TEST_ASSERT_EQUAL_UINT8(0x21u, fixture.at(x, 10));
        TEST_ASSERT_EQUAL_UINT8(1u, fixture.maskAt(x, 10));
    }
    for (int x = 100; x <= 149; ++x) {
        TEST_ASSERT_EQUAL_UINT8(0x21u, fixture.at(x, 10));
        TEST_ASSERT_EQUAL_UINT8(1u, fixture.maskAt(x, 10));
    }
    // The empty column between the runs closes one span and starts the other,
    // and it is never written.
    TEST_ASSERT_EQUAL_UINT8(0x5Au, fixture.at(75, 10));
    TEST_ASSERT_EQUAL_UINT8(0u, fixture.maskAt(75, 10));
    TEST_ASSERT_EQUAL_UINT8(0u, fixture.covered[75]);
    TEST_ASSERT_EQUAL_UINT32(40u + 50u, fixture.surface.planePixels);
    TEST_ASSERT_EQUAL_UINT32(2u, fixture.surface.planeSpans);
}

void test_a_column_with_top_below_its_bottom_is_skipped() {
    Fixture fixture;
    TestFlat flat;

    flat.setSolid(0x31u);
    fixture.flats.add("FLAT3", &flat.flat);
    OneWall().build(fixture.mapBytes, 64, 100, -100, "STONE");

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));

    clearPlanes(fixture);
    // A marked column whose top is below its bottom: R_MakeSpans' `t1 <= b1`
    // tests are false for it, so it is a break in the run, not a span.
    setRun(addPlane(fixture, 0, "FLAT3", 255), 160, 160, 50, 40);
    fixture.prefill(0x5Au);

    TEST_ASSERT_EQUAL_INT(R_PLANE_OK, fixture.drawPlanes());

    for (std::size_t i = 0; i < fixture.pixels.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT8(0x5Au, fixture.pixels[i]);
        TEST_ASSERT_EQUAL_UINT8(0u, fixture.planeMask[i]);
    }
    TEST_ASSERT_EQUAL_UINT32(0u, fixture.surface.planePixels);
    TEST_ASSERT_EQUAL_UINT32(0u, fixture.surface.planeSpans);
    TEST_ASSERT_EQUAL_UINT8(0u, fixture.covered[160]);
}

void test_the_flat_wraps_at_sixty_four_texels() {
    Fixture fixture;
    TestFlat flat;

    flat.setPattern();
    fixture.flats.add("FLAT4", &flat.flat);
    OneWall().build(fixture.mapBytes, 64, 100, -100, "STONE");

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));

    clearPlanes(fixture);
    setRun(addPlane(fixture, 0, "FLAT4", 255), 0, 319, 100, 100);
    fixture.prefill(0x00u);

    TEST_ASSERT_EQUAL_INT(R_PLANE_OK, fixture.drawPlanes());

    // The documented span formula, column by column. A 320-column run crosses
    // the 64-texel flat five times, so every pixel checks the `& 63` wrap.
    const int y = 100;
    const fixed_t planeHeight = 41 * kFracUnit;
    const fixed_t distance = fixed_mul(planeHeight, r_plane_yslope[y]);
    const fixed_t centerX = static_cast<fixed_t>(kScreenWidth / 2) << 16;
    const unsigned baseIndex =
        static_cast<unsigned>((0u - ANG90) >> ANGLETOFINESHIFT);
    const fixed_t baseXScale =
        fixed_div(r_fine_sine[baseIndex + FINEANGLES / 4], centerX);
    const fixed_t baseYScale = -fixed_div(r_fine_sine[baseIndex], centerX);
    const fixed_t xstep = fixed_mul(distance, baseXScale);
    const fixed_t ystep = fixed_mul(distance, baseYScale);
    const fixed_t length = fixed_mul(distance, r_plane_dist_scale[0]);
    const unsigned index =
        static_cast<unsigned>((0u + r_x_to_view_angle[0]) >> ANGLETOFINESHIFT);
    fixed_t xfrac = fixed_mul(r_fine_sine[index + FINEANGLES / 4], length);
    fixed_t yfrac = -fixed_mul(r_fine_sine[index], length);
    int wraps = 0;
    fixed_t previousV = (yfrac >> (FRACBITS - 6)) & (63 * 64);

    for (int x = 0; x < static_cast<int>(kScreenWidth); ++x) {
        const int spot =
            static_cast<int>(((yfrac >> (FRACBITS - 6)) & (63 * 64))
                             + ((xfrac >> FRACBITS) & 63));
        const fixed_t v = (yfrac >> (FRACBITS - 6)) & (63 * 64);

        TEST_ASSERT_EQUAL_UINT8(flat.pixels[static_cast<std::size_t>(spot)],
                                fixture.at(x, y));
        if (x > 0 && v < previousV) {
            ++wraps;
        }
        previousV = v;
        xfrac += xstep;
        yfrac += ystep;
    }
    TEST_ASSERT_TRUE_MESSAGE(wraps >= 4, "the run did not wrap the flat five times");
    TEST_ASSERT_EQUAL_UINT32(320u, fixture.surface.planePixels);
    TEST_ASSERT_EQUAL_UINT32(1u, fixture.surface.planeSpans);
}

void test_plane_light_is_the_zlight_table_not_the_wall_table() {
    Fixture bright;
    Fixture dark;
    TestFlat brightFlat;
    TestFlat darkFlat;

    brightFlat.setSolid(42u);
    darkFlat.setSolid(42u);
    bright.flats.add("FLAT5", &brightFlat.flat);
    dark.flats.add("FLAT5", &darkFlat.flat);
    OneWall().build(bright.mapBytes, 64, 100, -100, "STONE");
    OneWall().build(dark.mapBytes, 64, 100, -100, "STONE");

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, bright.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, bright.walk(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, dark.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, dark.walk(0, 0, 41 * kFracUnit, 0));

    clearPlanes(bright);
    setRun(addPlane(bright, 0, "FLAT5", 255), 0, 319, 100, 100);
    clearPlanes(dark);
    setRun(addPlane(dark, 0, "FLAT5", 0), 0, 319, 100, 100);
    bright.prefill(0x00u);
    dark.prefill(0x00u);

    TEST_ASSERT_EQUAL_INT(R_PLANE_OK, bright.drawPlanes());
    TEST_ASSERT_EQUAL_INT(R_PLANE_OK, dark.drawPlanes());

    // The plane light's column is the row's distance >> LIGHTZSHIFT, not the
    // wall's rw_scale >> LIGHTSCALESHIFT, and the table is zlight, not
    // scalelight. Row 15 is full bright; row 0 at any real distance is the
    // darkest map, which the test's colormap inverts.
    const fixed_t distance =
        fixed_mul(41 * kFracUnit, r_plane_yslope[100]);
    unsigned z = static_cast<unsigned>(distance >> R_PLANE_LIGHT_Z_SHIFT);
    if (z >= R_PLANE_LIGHT_Z_LEVELS) {
        z = R_PLANE_LIGHT_Z_LEVELS - 1;
    }
    TEST_ASSERT_EQUAL_UINT8(0u, r_plane_light[15][z]);
    TEST_ASSERT_EQUAL_UINT8(31u, r_plane_light[0][z]);
    TEST_ASSERT_EQUAL_UINT8(42u, bright.at(160, 100));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(255u - 42u),
                            dark.at(160, 100));

    // The two generated tables are not the same lookup. At its nearest column
    // the plane table's row 0 is map 0; the wall table's row 0 is map 31 for
    // every scale, which is the difference r_tables.h names.
    TEST_ASSERT_EQUAL_UINT8(0u, r_plane_light[0][0]);
    TEST_ASSERT_EQUAL_UINT8(31u, r_light_level[0][0]);
    TEST_ASSERT_EQUAL_UINT8(31u, r_plane_light[0][R_PLANE_LIGHT_Z_LEVELS - 1]);
}

void test_the_sky_plane_draws_the_sky_texture_and_never_a_flat() {
    Fixture fixture;
    TestTexture sky;
    TestFlat flat;

    // A 256-wide, 128-row sky with a distinct byte at every (column, row).
    sky.pixels.assign(256u * 128u, 0u);
    for (int column = 0; column < 256; ++column) {
        for (int row = 0; row < 128; ++row) {
            sky.pixels[static_cast<std::size_t>(column) * 128u
                       + static_cast<std::size_t>(row)] =
                static_cast<std::uint8_t>((column * 3 + row * 7 + 1) & 0xFF);
        }
    }
    sky.texture.pixels = sky.pixels.data();
    sky.texture.mask = nullptr;
    sky.texture.width = 256;
    sky.texture.height = 128;
    sky.texture.widthMask = r_texture_width_mask(256);
    sky.texture.heightMask = 127;

    flat.setSolid(0x11u);
    fixture.textures.add("SKY1", &sky.texture);
    fixture.flats.add("F_SKY1", &flat.flat);
    fixture.source.skyTexture = kSkyTexture;

    OneWall().build(fixture.mapBytes, 64, 100, -100, "STONE");
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0, kSky));

    clearPlanes(fixture);
    setRun(addPlane(fixture, 0, "F_SKY1", 0), 0, 319, 0, 9);
    fixture.prefill(0xAAu);

    TEST_ASSERT_EQUAL_INT(R_PLANE_OK, fixture.drawPlanes());

    // The sky is drawn from the texture, column by column: the plane's flat
    // picture was never resolved, and the pixels are the sky's bytes, not 0x11.
    TEST_ASSERT_EQUAL_INT(0, fixture.flats.lookups);
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < static_cast<int>(kScreenWidth); ++x) {
            const unsigned angle =
                static_cast<unsigned>((0u + r_x_to_view_angle[x])
                                      >> R_PLANE_SKY_ANGLE_SHIFT);
            const int column = static_cast<int>(angle & 255u);
            const int row = y & 127;
            const std::uint8_t expected =
                sky.pixels[static_cast<std::size_t>(column) * 128u
                           + static_cast<std::size_t>(row)];

            TEST_ASSERT_EQUAL_UINT8(expected, fixture.at(x, y));
        }
    }
    TEST_ASSERT_EQUAL_UINT32(320u * 10u, fixture.surface.planePixels);
    // A sky column is R_DrawColumn, not R_MapPlane: it is not a span.
    TEST_ASSERT_EQUAL_UINT32(0u, fixture.surface.planeSpans);
    TEST_ASSERT_EQUAL_UINT8(1u, fixture.covered[160]);

    // The contrast: a plane with a non-sky picture draws its flat, and the sky
    // texture is never needed.
    Fixture other;
    TestFlat otherFlat;

    otherFlat.setSolid(0x11u);
    other.flats.add("CEIL", &otherFlat.flat);
    OneWall().build(other.mapBytes, 64, 100, -100, "STONE");
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, other.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, other.walk(0, 0, 41 * kFracUnit, 0, kSky));

    clearPlanes(other);
    setRun(addPlane(other, 0, "CEIL", 255), 0, 319, 0, 9);
    other.prefill(0xAAu);

    TEST_ASSERT_EQUAL_INT(R_PLANE_OK, other.drawPlanes());
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < static_cast<int>(kScreenWidth); ++x) {
            TEST_ASSERT_EQUAL_UINT8(0x11u, other.at(x, y));
        }
    }
    TEST_ASSERT_EQUAL_UINT32(320u * 10u, other.surface.planePixels);
    TEST_ASSERT_EQUAL_UINT32(10u, other.surface.planeSpans);
}

void test_two_planes_keep_their_own_runs_in_index_order() {
    Fixture fixture;
    TestFlat lower;
    TestFlat later;

    lower.setSolid(0x11u);
    later.setSolid(0x22u);
    fixture.flats.add("FLATA", &lower.flat);
    fixture.flats.add("FLATB", &later.flat);
    OneWall().build(fixture.mapBytes, 64, 100, -100, "STONE");

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));

    clearPlanes(fixture);
    setRun(addPlane(fixture, 0, "FLATA", 255), 0, 99, 10, 10);
    setRun(addPlane(fixture, 0, "FLATB", 255), 200, 319, 10, 10);
    fixture.prefill(0x5Au);

    TEST_ASSERT_EQUAL_INT(R_PLANE_OK, fixture.drawPlanes());

    for (int x = 0; x < 100; ++x) {
        TEST_ASSERT_EQUAL_UINT8(0x11u, fixture.at(x, 10));
    }
    for (int x = 200; x < static_cast<int>(kScreenWidth); ++x) {
        TEST_ASSERT_EQUAL_UINT8(0x22u, fixture.at(x, 10));
    }
    for (int x = 100; x < 200; ++x) {
        TEST_ASSERT_EQUAL_UINT8(0x5Au, fixture.at(x, 10));
    }
    TEST_ASSERT_EQUAL_UINT32(100u + 120u, fixture.surface.planePixels);
    TEST_ASSERT_EQUAL_UINT32(2u, fixture.surface.planeSpans);
}

void test_a_missing_flat_leaves_the_frame_untouched() {
    Fixture fixture;

    OneWall().build(fixture.mapBytes, 64, 100, -100, "STONE");
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));

    clearPlanes(fixture);
    setRun(addPlane(fixture, 0, "NOPE", 255), 0, 319, 0, 199);
    fixture.prefill(0x5Au);
    fixture.surface.planePixels = 123u;
    fixture.surface.planeSpans = 456u;

    TEST_ASSERT_EQUAL_INT(R_PLANE_ERR_FLAT_MISSING, fixture.drawPlanes());

    for (std::size_t i = 0; i < fixture.pixels.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT8(0x5Au, fixture.pixels[i]);
        TEST_ASSERT_EQUAL_UINT8(0u, fixture.planeMask[i]);
    }
    for (std::size_t x = 0; x < kScreenWidth; ++x) {
        TEST_ASSERT_EQUAL_UINT8(0u, fixture.covered[x]);
    }
    TEST_ASSERT_EQUAL_UINT32(123u, fixture.surface.planePixels);
    TEST_ASSERT_EQUAL_UINT32(456u, fixture.surface.planeSpans);
}

void test_a_misshapen_flat_leaves_the_frame_untouched() {
    Fixture fixture;
    TestFlat flat;

    flat.setSolid(0x11u);
    // Not 64 x 64: the flat's square is what `& 63` and `& 4032` address.
    flat.flat.height = 32;
    flat.flat.heightMask = 31;
    fixture.flats.add("FLAT6", &flat.flat);
    OneWall().build(fixture.mapBytes, 64, 100, -100, "STONE");

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));

    clearPlanes(fixture);
    setRun(addPlane(fixture, 0, "FLAT6", 255), 0, 319, 0, 199);
    fixture.prefill(0x5Au);

    TEST_ASSERT_EQUAL_INT(R_PLANE_ERR_FLAT_SHAPE, fixture.drawPlanes());
    for (std::size_t i = 0; i < fixture.pixels.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT8(0x5Au, fixture.pixels[i]);
        TEST_ASSERT_EQUAL_UINT8(0u, fixture.planeMask[i]);
    }

    // A width that is not 64 is the other unusable shape.
    flat.flat.height = 64;
    flat.flat.heightMask = 63;
    flat.flat.width = 128;
    flat.flat.widthMask = 127;
    TEST_ASSERT_EQUAL_INT(R_PLANE_ERR_FLAT_SHAPE, fixture.drawPlanes());
    for (std::size_t i = 0; i < fixture.pixels.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT8(0x5Au, fixture.pixels[i]);
    }
}

void test_a_sky_plane_with_no_resolvable_texture_fails() {
    Fixture fixture;
    TestFlat flat;

    flat.setSolid(0x11u);
    fixture.flats.add("F_SKY1", &flat.flat);
    OneWall().build(fixture.mapBytes, 64, 100, -100, "STONE");

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0, kSky));

    clearPlanes(fixture);
    setRun(addPlane(fixture, 0, "F_SKY1", 0), 0, 319, 0, 9);
    fixture.prefill(0x5Au);

    // The source names no sky texture: a sky picture with no texture is an
    // error, not a blank ceiling.
    TEST_ASSERT_EQUAL_INT(R_PLANE_ERR_SKY_MISSING, fixture.drawPlanes());
    for (std::size_t i = 0; i < fixture.pixels.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT8(0x5Au, fixture.pixels[i]);
    }

    // The name is set but nothing resolves it.
    fixture.source.skyTexture = kSkyTexture;
    TEST_ASSERT_EQUAL_INT(R_PLANE_ERR_SKY_MISSING, fixture.drawPlanes());
    for (std::size_t i = 0; i < fixture.pixels.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT8(0x5Au, fixture.pixels[i]);
    }

    // A resident texture whose column mask is not an AND mask is unusable.
    TestTexture sky;

    sky.setSolid(1u, 256, 128);
    sky.texture.widthMask = 100;
    fixture.textures.add("SKY1", &sky.texture);
    TEST_ASSERT_EQUAL_INT(R_PLANE_ERR_SKY_SHAPE, fixture.drawPlanes());
    for (std::size_t i = 0; i < fixture.pixels.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT8(0x5Au, fixture.pixels[i]);
    }
}

void test_the_plane_pass_validates_its_arguments_before_writing() {
    Fixture fixture;
    TestFlat flat;

    flat.setSolid(0x11u);
    fixture.flats.add("FLAT7", &flat.flat);
    OneWall().build(fixture.mapBytes, 64, 100, -100, "STONE");

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));

    clearPlanes(fixture);
    setRun(addPlane(fixture, 0, "FLAT7", 255), 0, 319, 0, 199);
    fixture.prefill(0x5Au);

    TEST_ASSERT_EQUAL_INT(R_PLANE_ERR_NULL_STATE,
                          r_plane_draw(nullptr, &fixture.surface, &fixture.source,
                                       &fixture.light));

    fixture.surface.width = 160;
    TEST_ASSERT_EQUAL_INT(R_PLANE_ERR_SURFACE_SIZE, fixture.drawPlanes());
    fixture.surface.width = static_cast<std::int32_t>(kScreenWidth);

    fixture.surface.pixels = nullptr;
    TEST_ASSERT_EQUAL_INT(R_PLANE_ERR_SURFACE_PIXELS, fixture.drawPlanes());
    fixture.surface.pixels = fixture.pixels.data();

    TEST_ASSERT_EQUAL_INT(R_PLANE_ERR_NULL_SOURCE,
                          r_plane_draw(&fixture.view, &fixture.surface, nullptr,
                                       &fixture.light));

    fixture.source.resolveFlat = nullptr;
    TEST_ASSERT_EQUAL_INT(R_PLANE_ERR_NULL_RESOLVE, fixture.drawPlanes());
    fixture.source.resolveFlat = resolveFlat;

    TEST_ASSERT_EQUAL_INT(R_PLANE_ERR_NULL_LIGHT,
                          r_plane_draw(&fixture.view, &fixture.surface,
                                       &fixture.source, nullptr));

    for (std::size_t i = 0; i < fixture.pixels.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT8(0x5Au, fixture.pixels[i]);
    }
    TEST_ASSERT_EQUAL_UINT32(0u, fixture.surface.planePixels);
    TEST_ASSERT_EQUAL_UINT32(0u, fixture.surface.planeSpans);
}

}  // namespace

/* --- D3.4: sprites and the masked mid textures -------------------------- */

r_sprite_t makeSprite(fixed_t x, fixed_t y, fixed_t z, const char* lump) {
    r_sprite_t sprite{};

    sprite.x = x;
    sprite.y = y;
    sprite.z = z;
    sprite.angle = 0;
    sprite.lump = packName(lump);
    sprite.lightLevel = 255;  // row 15, map 0: the identity colormap
    sprite.flip = 0;
    sprite.fullbright = 0;
    sprite.noClip = 0;
    return sprite;
}

int countValue(const Fixture& fixture, std::uint8_t value) {
    int count = 0;

    for (std::uint8_t pixel : fixture.pixels) {
        count += pixel == value ? 1 : 0;
    }
    return count;
}

/// The bounding box of one texel value in the surface, for assertions that do
/// not want to re-derive DOOM's projection arithmetic in the test.
struct ValueBox {
    int x1 = 100000;
    int x2 = -1;
    int y1 = 100000;
    int y2 = -1;
    int count = 0;

    int width() const { return x2 - x1 + 1; }
    int height() const { return y2 - y1 + 1; }
};

ValueBox findValue(const Fixture& fixture, std::uint8_t value) {
    ValueBox box;

    for (int y = 0; y < static_cast<int>(kScreenHeight); ++y) {
        for (int x = 0; x < static_cast<int>(kScreenWidth); ++x) {
            if (fixture.at(x, y) != value) {
                continue;
            }
            box.x1 = x < box.x1 ? x : box.x1;
            box.x2 = x > box.x2 ? x : box.x2;
            box.y1 = y < box.y1 ? y : box.y1;
            box.y2 = y > box.y2 ? y : box.y2;
            ++box.count;
        }
    }
    return box;
}

void test_a_sprite_is_projected_and_drawn_over_the_wall_behind_it() {
    Fixture fixture;
    TestTexture wall;
    TestPatch patch;

    wall.setSolid(7u, 8, 8);
    fixture.textures.add("STONE", &wall.texture);
    patch.setSolid(42u, 8, 16, 4, 16);
    fixture.sprites.add("SPRTA0", &patch.patch);
    OneWall().build(fixture.mapBytes, 200, 100, -100, "STONE");

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_INT(R_SEGS_OK, fixture.emit());

    // The wall at x=200 is behind the sprite at x=64, so it must not clip it:
    // this is the ordering half of the test. The patch's 8 x 16 shape at scale
    // 160/64 = 2.5 projects to columns 150..169 and rows 80..119.
    {
        r_sprite_t sprite = makeSprite(64 * kFracUnit, 0, 33 * kFracUnit, "SPRTA0");

        TEST_ASSERT_EQUAL_INT(R_THINGS_OK, fixture.drawMasked(&sprite, 1u));
        TEST_ASSERT_EQUAL_UINT16(1u, fixture.stats.submitted);
        TEST_ASSERT_EQUAL_UINT16(1u, fixture.stats.drawn);
        TEST_ASSERT_EQUAL_UINT16(0u, fixture.stats.backface);
    }
    // The patch is solid, so its pixels are one rectangle. Its exact edges are
    // DOOM's projection; what the pass owes is a rectangle of the right shape
    // with the wall visible on all four sides.
    const ValueBox box = findValue(fixture, 42u);

    TEST_ASSERT_TRUE(box.width() >= 19 && box.width() <= 21);
    TEST_ASSERT_TRUE(box.height() >= 39 && box.height() <= 42);
    TEST_ASSERT_EQUAL_INT(box.width() * box.height(), box.count);
    TEST_ASSERT_EQUAL_UINT32(static_cast<std::uint32_t>(box.count),
                             fixture.surface.spritePixels);
    TEST_ASSERT_EQUAL_UINT8(7u, fixture.at(box.x1 - 1, (box.y1 + box.y2) / 2));
    TEST_ASSERT_EQUAL_UINT8(7u, fixture.at(box.x2 + 1, (box.y1 + box.y2) / 2));
    TEST_ASSERT_EQUAL_UINT8(7u, fixture.at((box.x1 + box.x2) / 2, box.y1 - 1));
    TEST_ASSERT_EQUAL_UINT8(7u, fixture.at((box.x1 + box.x2) / 2, box.y2 + 1));
    TEST_ASSERT_EQUAL_UINT8(1u, fixture.covered[160]);
    TEST_ASSERT_EQUAL_UINT8(1u, fixture.spriteMask[160u + 100u * kScreenWidth]);
}

void test_a_nearer_wall_clips_exactly_the_columns_it_covers() {
    Fixture fixture;
    TestTexture wall;
    TestPatch patch;

    wall.setSolid(7u, 8, 8);
    fixture.textures.add("STONE", &wall.texture);
    patch.setSolid(42u, 8, 16, 4, 16);
    fixture.sprites.add("SPRTA0", &patch.patch);

    MapBuilder& map = fixture.mapBytes;
    const std::uint16_t sector = map.addSector(0, 128, "FLOOR0", "CEIL0", 255);
    const std::uint16_t v1 = map.addVertex(50, 4);
    const std::uint16_t v2 = map.addVertex(50, -1);
    const std::uint16_t side = map.addSide(0, 0, "-", "-", "STONE", sector);
    const std::uint16_t line = map.addLine(v1, v2, 0, side);
    map.addSeg(v1, v2, 49152u, line, 0);
    map.addSubsector(1, 0);

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_INT(R_SEGS_OK, fixture.emit());

    const r_drawseg_t* near = &fixture.view.storage.drawsegs[0];
    const int wallX1 = near->x1;
    const int wallX2 = near->x2;

    // A single-sided line is the shared constant pair, never a per-fragment
    // copy: that is the D3.4 clip capture's first case.
    TEST_ASSERT_EQUAL_INT16(R_DRAWSEG_CLIP_TOP, near->sprTopClip);
    TEST_ASSERT_EQUAL_INT16(R_DRAWSEG_CLIP_BOTTOM, near->sprBottomClip);
    TEST_ASSERT_EQUAL_UINT8(R_SIL_BOTH, near->silhouette);

    r_sprite_t sprite = makeSprite(100 * kFracUnit, 0, 33 * kFracUnit, "SPRTA0");

    TEST_ASSERT_EQUAL_INT(R_THINGS_OK, fixture.drawMasked(&sprite, 1u));
    TEST_ASSERT_EQUAL_UINT16(1u, fixture.stats.drawn);

    // The sprite spans columns 153..165 at scale 160/100 = 1.6. Wherever the
    // near wall's columns reach, its texel wins; everywhere else the sprite's
    // 42 does. Exactly the covered columns are cut, and no others.
    int visible = 0;
    int clipped = 0;

    for (int x = 140; x <= 180; ++x) {
        const std::uint8_t value = fixture.at(x, 100);

        if (x >= wallX1 && x <= wallX2) {
            if (value == 42u) {
                TEST_FAIL_MESSAGE("a column covered by the near wall kept the sprite");
            }
            if (value == 7u) {
                ++clipped;
            }
        } else if (value == 42u) {
            ++visible;
        }
    }
    TEST_ASSERT_TRUE(visible > 0);
    TEST_ASSERT_TRUE(clipped > 0);
    TEST_ASSERT_EQUAL_UINT32(static_cast<std::uint32_t>(visible) * 25u,
                             fixture.surface.spritePixels);
}

void test_a_sprite_behind_a_solid_wall_writes_nothing() {
    Fixture fixture;
    TestTexture wall;
    TestPatch patch;

    wall.setSolid(7u, 8, 8);
    fixture.textures.add("STONE", &wall.texture);
    patch.setSolid(42u, 8, 16, 4, 16);
    fixture.sprites.add("SPRTA0", &patch.patch);
    OneWall().build(fixture.mapBytes, 50, 100, -100, "STONE");

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_INT(R_SEGS_OK, fixture.emit());

    r_sprite_t sprite = makeSprite(100 * kFracUnit, 0, 33 * kFracUnit, "SPRTA0");

    TEST_ASSERT_EQUAL_INT(R_THINGS_OK, fixture.drawMasked(&sprite, 1u));
    TEST_ASSERT_EQUAL_UINT16(1u, fixture.stats.drawn);
    TEST_ASSERT_EQUAL_UINT32(0u, fixture.surface.spritePixels);
    TEST_ASSERT_EQUAL_INT(0, countValue(fixture, 42u));
    // The wall covers the frame; not one pixel moved.
    TEST_ASSERT_EQUAL_INT(320 * 200, countValue(fixture, 7u));
}

void test_a_sprite_in_front_of_a_two_sided_line_is_not_clipped() {
    Fixture fixture;
    TestTexture top;
    TestTexture bottom;
    TestTexture far;
    TestPatch patch;

    top.setSolid(11u, 8, 8);
    bottom.setSolid(22u, 8, 8);
    far.setSolid(33u, 8, 8);
    fixture.textures.add("TOPTX", &top.texture);
    fixture.textures.add("BOTTXT", &bottom.texture);
    fixture.textures.add("FARTX", &far.texture);
    patch.setSolid(42u, 8, 16, 4, 16);
    fixture.sprites.add("SPRTA0", &patch.patch);

    MapBuilder& map = fixture.mapBytes;
    const std::uint16_t frontSector = map.addSector(16, 128, "FLOOR0", "CEIL0", 255);
    const std::uint16_t backSector = map.addSector(0, 128, "FLOOR1", "CEIL1", 255);
    const std::uint16_t nearV1 = map.addVertex(64, 100);
    const std::uint16_t nearV2 = map.addVertex(64, -100);
    const std::uint16_t side0 = map.addSide(0, 0, "TOPTX", "BOTTXT", "-", frontSector);
    const std::uint16_t side1 = map.addSide(0, 0, "-", "-", "-", backSector);
    const std::uint16_t nearLine = map.addLine(nearV1, nearV2, DOOM_ML_TWOSIDED, side0, side1);
    map.addSeg(nearV1, nearV2, 49152u, nearLine, 0);
    const std::uint16_t solidSector = map.addSector(0, 128, "FLOOR0", "CEIL0", 255);
    const std::uint16_t farV1 = map.addVertex(128, 200);
    const std::uint16_t farV2 = map.addVertex(128, -200);
    const std::uint16_t farSide = map.addSide(0, 0, "-", "-", "FARTX", solidSector);
    const std::uint16_t farLine = map.addLine(farV1, farV2, 0, farSide);
    map.addSeg(farV1, farV2, 49152u, farLine, 0);
    map.addSubsector(2, 0);

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_INT(R_SEGS_OK, fixture.emit());

    // A two-sided line with a step down forces a per-fragment copy of the
    // bottom clip array; that is the capture's second case, and it is what
    // would clip a sprite behind the line. The near line is the first seg.
    const r_drawseg_t* near = nullptr;
    for (std::uint16_t i = 0; i < fixture.view.drawsegCount; ++i) {
        if (fixture.view.storage.drawsegs[i].seg == 0u) {
            near = &fixture.view.storage.drawsegs[i];
        }
    }
    TEST_ASSERT_NOT_NULL(near);
    TEST_ASSERT_EQUAL_UINT8(R_SIL_BOTTOM, near->silhouette);
    TEST_ASSERT_EQUAL_INT16(R_DRAWSEG_NO_BLOCK, near->sprTopClip);
    TEST_ASSERT_TRUE(near->sprBottomClip >= R_DRAWSEG_RESERVED_BLOCKS);

    r_sprite_t sprite = makeSprite(32 * kFracUnit, 0, 33 * kFracUnit, "SPRTA0");

    TEST_ASSERT_EQUAL_INT(R_THINGS_OK, fixture.drawMasked(&sprite, 1u));
    // scale 160/32: a rectangle about 41 columns by 80 rows, all in front of
    // the line and its tiers, so no clip may touch it.
    const ValueBox box = findValue(fixture, 42u);

    TEST_ASSERT_TRUE(box.width() >= 40 && box.width() <= 42);
    TEST_ASSERT_TRUE(box.height() >= 79 && box.height() <= 82);
    TEST_ASSERT_EQUAL_INT(box.width() * box.height(), box.count);
    TEST_ASSERT_EQUAL_UINT32(static_cast<std::uint32_t>(box.count),
                             fixture.surface.spritePixels);
    TEST_ASSERT_TRUE(box.x1 <= 140);
}

void test_a_flipped_sprite_mirrors_its_texture_columns() {
    Fixture fixture;
    TestTexture wall;
    TestPatch patch;
    const std::vector<std::uint8_t> bands{10u, 11u, 12u, 13u,
                                          14u, 15u, 16u, 17u};

    wall.setSolid(7u, 8, 8);
    fixture.textures.add("STONE", &wall.texture);
    patch.setBands(bands, 16, 4, 16);
    fixture.sprites.add("BANDS0", &patch.patch);
    OneWall().build(fixture.mapBytes, 200, 100, -100, "STONE");

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_INT(R_SEGS_OK, fixture.emit());

    r_sprite_t sprite = makeSprite(64 * kFracUnit, 0, 33 * kFracUnit, "BANDS0");

    TEST_ASSERT_EQUAL_INT(R_THINGS_OK, fixture.drawMasked(&sprite, 1u));
    // Unflipped, the leftmost column shows texture column 0 and the third
    // column is still column 0 (frac advances by 1/2.5 per screen column).
    TEST_ASSERT_EQUAL_UINT8(10u, fixture.at(150, 100));
    TEST_ASSERT_EQUAL_UINT8(10u, fixture.at(152, 100));
    TEST_ASSERT_EQUAL_UINT8(11u, fixture.at(153, 100));

    fixture.prefill(kUncovered);
    TEST_ASSERT_EQUAL_INT(R_SEGS_OK, fixture.emit());
    sprite.flip = 1;
    TEST_ASSERT_EQUAL_INT(R_THINGS_OK, fixture.drawMasked(&sprite, 1u));
    // Mirrored: the leftmost column is texture column 7, then 6, then 5.
    TEST_ASSERT_EQUAL_UINT8(17u, fixture.at(150, 100));
    TEST_ASSERT_EQUAL_UINT8(16u, fixture.at(151, 100));
    TEST_ASSERT_EQUAL_UINT8(15u, fixture.at(153, 100));
    // The last column's frac lands below zero texels, which RANGECHECK would
    // report and this port skips; the column keeps the wall behind it.
    TEST_ASSERT_EQUAL_UINT8(7u, fixture.at(169, 100));
    TEST_ASSERT_EQUAL_UINT32(18u * 41u, fixture.surface.spritePixels);
}

void test_a_masked_mid_texture_is_deferred_and_orders_against_sprites_and_flats() {
    Fixture fixture;
    TestTexture mid;
    TestPatch nearPatch;
    TestPatch farPatch;
    std::vector<std::uint8_t> mask;

    // A mid texture with a transparent lower half: the texels rows 0..3 are
    // opaque, 4..7 are holes, so a sprite behind the line shows through them.
    mid.setSolid(33u, 8, 8);
    mask.assign(8u * 8u, 0u);
    for (std::size_t x = 0; x < 8u; ++x) {
        for (std::size_t y = 0; y < 4u; ++y) {
            mask[x * 8u + y] = 1u;
        }
    }
    mid.setMask(std::move(mask));
    fixture.textures.add("MIDTX", &mid.texture);
    nearPatch.setSolid(42u, 8, 16, 4, 16);
    farPatch.setSolid(55u, 8, 16, 4, 16);
    fixture.sprites.add("NEARA0", &nearPatch.patch);
    fixture.sprites.add("FARA0", &farPatch.patch);

    MapBuilder& map = fixture.mapBytes;
    const std::uint16_t front = map.addSector(0, 128, "FLOOR0", "CEIL0", 255);
    const std::uint16_t back = map.addSector(32, 72, "FLOOR1", "CEIL1", 255);
    const std::uint16_t v1 = map.addVertex(64, 100);
    const std::uint16_t v2 = map.addVertex(64, -100);
    const std::uint16_t side0 = map.addSide(0, 0, "-", "-", "MIDTX", front);
    const std::uint16_t side1 = map.addSide(0, 0, "-", "-", "-", back);
    const std::uint16_t line = map.addLine(v1, v2, DOOM_ML_TWOSIDED, side0, side1);
    map.addSeg(v1, v2, 49152u, line, 0);
    map.addSubsector(1, 0);

    TestFlat flat;
    flat.setSolid(5u);
    fixture.flats.add("FLAT0", &flat.flat);

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_UINT16(1u, fixture.view.drawsegCount);
    TEST_ASSERT_EQUAL_INT(R_SEGS_OK, fixture.emit());

    // The masked texture is recorded, not drawn: the wall pass leaves the
    // surface as it found it, and its column values are in an opening block.
    TEST_ASSERT_NOT_EQUAL(R_DRAWSEG_NO_BLOCK,
                          fixture.view.storage.drawsegs[0].maskedTextureCol);    TEST_ASSERT_EQUAL_INT(0, countValue(fixture, 33u));
    TEST_ASSERT_EQUAL_UINT8(R_SIL_BOTH, fixture.view.storage.drawsegs[0].silhouette);

    // A flat everywhere, so "over the flats" is measurable.
    clearPlanes(fixture);
    r_visplane_t* plane = addPlane(fixture, 0, "FLAT0", 255);
    setRun(plane, 0, static_cast<int>(kScreenWidth) - 1, 0,
           static_cast<int>(kScreenHeight) - 1);
    TEST_ASSERT_EQUAL_INT(R_PLANE_OK, fixture.drawPlanes());
    TEST_ASSERT_EQUAL_UINT8(5u, fixture.at(10, 100));

    r_sprite_t sprites[2];
    sprites[0] = makeSprite(32 * kFracUnit, 0, 33 * kFracUnit, "NEARA0");
    sprites[1] = makeSprite(128 * kFracUnit, 40 * kFracUnit, 33 * kFracUnit, "FARA0");
    TEST_ASSERT_EQUAL_INT(R_THINGS_OK, fixture.drawMasked(sprites, 2u));
    TEST_ASSERT_EQUAL_UINT16(2u, fixture.stats.drawn);

    // Column 10 covers no sprite: the mid is over the flat, with its opaque
    // half at 33 and its holes showing the flat's 5.
    bool midOpaque = false;
    bool midHole = false;
    for (int y = 0; y < static_cast<int>(kScreenHeight); ++y) {
        if (fixture.at(10, y) == 33u) {
            midOpaque = true;
        }
        if (fixture.at(10, y) == 5u) {
            midHole = true;
        }
    }
    TEST_ASSERT_TRUE(midOpaque);
    TEST_ASSERT_TRUE(midHole);

    // Column 110 is the far sprite's: the mid is in front of it, so 33 sits
    // over some of its rows while its holes show the sprite's 55.
    bool farShows = false;
    bool midOverFar = false;
    for (int y = 60; y <= 140; ++y) {
        farShows = farShows || fixture.at(110, y) == 55u;
        midOverFar = midOverFar || fixture.at(110, y) == 33u;
    }
    TEST_ASSERT_TRUE(farShows);
    TEST_ASSERT_TRUE(midOverFar);

    // Column 160 is the near sprite's (140..179): it is in front of the line,
    // so the near sprite is drawn over the mid.
    TEST_ASSERT_EQUAL_UINT8(42u, fixture.at(160, 100));
}

void test_a_sprite_post_gap_leaves_the_buffer_untouched() {
    Fixture fixture;
    TestTexture wall;
    TestPatch patch;

    wall.setSolid(7u, 8, 8);
    fixture.textures.add("STONE", &wall.texture);
    // Two posts with a four-row gap: rows 4..7 of every column are transparent.
    patch.build(8, 16, 4, 16, [](int) {
        return std::vector<std::pair<int, std::vector<std::uint8_t>>>{
            {0, std::vector<std::uint8_t>(4u, 42u)},
            {8, std::vector<std::uint8_t>(4u, 43u)}};
    });
    fixture.sprites.add("GAPA0", &patch.patch);
    OneWall().build(fixture.mapBytes, 200, 100, -100, "STONE");

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_INT(R_SEGS_OK, fixture.emit());

    r_sprite_t sprite = makeSprite(64 * kFracUnit, 0, 33 * kFracUnit, "GAPA0");

    TEST_ASSERT_EQUAL_INT(R_THINGS_OK, fixture.drawMasked(&sprite, 1u));
    // Two posts with a four-row gap: the first post's band is 42, the gap keeps
    // the wall's 7, and the second post's band is 43.
    const ValueBox first = findValue(fixture, 42u);
    const ValueBox second = findValue(fixture, 43u);

    TEST_ASSERT_TRUE(first.count > 0);
    TEST_ASSERT_TRUE(second.count > 0);
    TEST_ASSERT_EQUAL_INT(first.x1, second.x1);
    TEST_ASSERT_EQUAL_INT(first.x2, second.x2);
    TEST_ASSERT_TRUE(second.y1 > first.y2);
    for (int x = first.x1; x <= first.x2; ++x) {
        for (int y = first.y2 + 1; y < second.y1; ++y) {
            if (fixture.at(x, y) != 7u) {
                TEST_FAIL_MESSAGE("the transparent gap did not keep the wall");
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT32(static_cast<std::uint32_t>(first.count + second.count),
                             fixture.surface.spritePixels);
}

void test_a_nonresident_sprite_leaves_the_frame_untouched() {
    Fixture fixture;
    TestTexture wall;

    wall.setSolid(7u, 8, 8);
    fixture.textures.add("STONE", &wall.texture);
    OneWall().build(fixture.mapBytes, 200, 100, -100, "STONE");

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_INT(R_SEGS_OK, fixture.emit());
    fixture.prefill(0xAAu);

    r_sprite_t sprite = makeSprite(64 * kFracUnit, 0, 33 * kFracUnit, "MISSING0");

    TEST_ASSERT_EQUAL_INT(R_THINGS_ERR_SPRITE_MISSING,
                          fixture.drawMasked(&sprite, 1u));
    TEST_ASSERT_EQUAL_INT(320 * 200, countValue(fixture, 0xAAu));
    TEST_ASSERT_EQUAL_UINT32(0u, fixture.surface.spritePixels);
}

void test_a_vis_sprite_overflow_fails_before_any_write() {
    Fixture fixture;
    TestTexture wall;
    TestPatch patch;

    wall.setSolid(7u, 8, 8);
    fixture.textures.add("STONE", &wall.texture);
    patch.setSolid(42u, 8, 16, 4, 16);
    fixture.sprites.add("SPRTA0", &patch.patch);
    OneWall().build(fixture.mapBytes, 200, 100, -100, "STONE");

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_INT(R_SEGS_OK, fixture.emit());
    fixture.view.storage.visSpriteCapacity = 1u;
    fixture.prefill(0xAAu);

    r_sprite_t sprites[2];
    sprites[0] = makeSprite(64 * kFracUnit, 0, 33 * kFracUnit, "SPRTA0");
    sprites[1] = makeSprite(96 * kFracUnit, 0, 33 * kFracUnit, "SPRTA0");

    TEST_ASSERT_EQUAL_INT(R_THINGS_ERR_VISSPRITE_OVERFLOW,
                          fixture.drawMasked(sprites, 2u));
    TEST_ASSERT_EQUAL_INT(320 * 200, countValue(fixture, 0xAAu));
}

void test_a_clip_pool_that_cannot_hold_the_frame_is_reported_before_writing() {
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
    const std::uint16_t front = map.addSector(16, 128, "FLOOR0", "CEIL0", 255);
    const std::uint16_t back = map.addSector(0, 128, "FLOOR1", "CEIL1", 255);
    const std::uint16_t nearV1 = map.addVertex(64, 100);
    const std::uint16_t nearV2 = map.addVertex(64, -100);
    const std::uint16_t side0 = map.addSide(0, 0, "TOPTX", "BOTTXT", "-", front);
    const std::uint16_t side1 = map.addSide(0, 0, "-", "-", "-", back);
    const std::uint16_t nearLine =
        map.addLine(nearV1, nearV2, DOOM_ML_TWOSIDED, side0, side1);
    map.addSeg(nearV1, nearV2, 49152u, nearLine, 0);
    map.addSubsector(1, 0);

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));
    // Only the two reserved constant blocks fit: the two-sided line's copies
    // cannot, and that is a reported error with the surface untouched.
    fixture.view.storage.clipBlockCapacity = R_DRAWSEG_RESERVED_BLOCKS;
    fixture.prefill(0xAAu);

    TEST_ASSERT_EQUAL_INT(R_SEGS_ERR_SMALL_CLIP_POOL, fixture.emit());
    TEST_ASSERT_EQUAL_INT(320 * 200, countValue(fixture, 0xAAu));
}

void test_a_back_face_sprite_and_a_zero_span_sprite_draw_nothing() {
    Fixture fixture;
    TestTexture wall;
    TestPatch patch;

    wall.setSolid(7u, 8, 8);
    fixture.textures.add("STONE", &wall.texture);
    patch.setSolid(42u, 8, 16, 4, 16);
    fixture.sprites.add("SPRTA0", &patch.patch);
    OneWall().build(fixture.mapBytes, 200, 100, -100, "STONE");

    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.start());
    TEST_ASSERT_EQUAL_INT(R_BSP_OK, fixture.walk(0, 0, 41 * kFracUnit, 0));
    TEST_ASSERT_EQUAL_INT(R_SEGS_OK, fixture.emit());
    fixture.prefill(kUncovered);

    r_sprite_t sprites[2];
    sprites[0] = makeSprite(-64 * kFracUnit, 0, 33 * kFracUnit, "SPRTA0");
    sprites[1] = makeSprite(64 * kFracUnit, -68 * kFracUnit, 33 * kFracUnit,
                            "SPRTA0");

    TEST_ASSERT_EQUAL_INT(R_THINGS_OK, fixture.drawMasked(sprites, 2u));
    TEST_ASSERT_EQUAL_UINT16(2u, fixture.stats.submitted);
    TEST_ASSERT_EQUAL_UINT16(1u, fixture.stats.backface);
    TEST_ASSERT_EQUAL_UINT16(1u, fixture.stats.zerospan);
    TEST_ASSERT_EQUAL_UINT16(0u, fixture.stats.drawn);
    TEST_ASSERT_EQUAL_UINT32(0u, fixture.surface.spritePixels);
    TEST_ASSERT_EQUAL_INT(320 * 200, countValue(fixture, kUncovered));
}

void test_the_drawseg_layout_is_unchanged_at_fifty_two_bytes() {
    TEST_ASSERT_EQUAL_UINT32(52u, static_cast<std::uint32_t>(sizeof(r_drawseg_t)));
}

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
    RUN_TEST(test_a_single_plane_fills_the_whole_surface);
    RUN_TEST(test_two_disjoint_runs_in_one_plane_are_two_spans);
    RUN_TEST(test_a_column_with_top_below_its_bottom_is_skipped);
    RUN_TEST(test_the_flat_wraps_at_sixty_four_texels);
    RUN_TEST(test_plane_light_is_the_zlight_table_not_the_wall_table);
    RUN_TEST(test_the_sky_plane_draws_the_sky_texture_and_never_a_flat);
    RUN_TEST(test_two_planes_keep_their_own_runs_in_index_order);
    RUN_TEST(test_a_missing_flat_leaves_the_frame_untouched);
    RUN_TEST(test_a_misshapen_flat_leaves_the_frame_untouched);
    RUN_TEST(test_a_sky_plane_with_no_resolvable_texture_fails);
    RUN_TEST(test_the_plane_pass_validates_its_arguments_before_writing);

    // D3.4: the sprite pass and the masked mid textures.
    RUN_TEST(test_a_sprite_is_projected_and_drawn_over_the_wall_behind_it);
    RUN_TEST(test_a_nearer_wall_clips_exactly_the_columns_it_covers);
    RUN_TEST(test_a_sprite_behind_a_solid_wall_writes_nothing);
    RUN_TEST(test_a_sprite_in_front_of_a_two_sided_line_is_not_clipped);
    RUN_TEST(test_a_flipped_sprite_mirrors_its_texture_columns);
    RUN_TEST(test_a_masked_mid_texture_is_deferred_and_orders_against_sprites_and_flats);
    RUN_TEST(test_a_sprite_post_gap_leaves_the_buffer_untouched);
    RUN_TEST(test_a_nonresident_sprite_leaves_the_frame_untouched);
    RUN_TEST(test_a_vis_sprite_overflow_fails_before_any_write);
    RUN_TEST(test_a_clip_pool_that_cannot_hold_the_frame_is_reported_before_writing);
    RUN_TEST(test_a_back_face_sprite_and_a_zero_span_sprite_draw_nothing);
    RUN_TEST(test_the_drawseg_layout_is_unchanged_at_fifty_two_bytes);

    const int result = UNITY_END();
    test_teardown();
    return result;
}
