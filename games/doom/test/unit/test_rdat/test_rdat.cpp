/*
 * Behaviour tests for the three picture decoders (lib/doom/r_data.c).
 *
 * Every lump here is assembled byte for byte by this file. No WAD, no binary
 * fixture and no copyrighted data is involved: each format rule and each
 * validation boundary is stated at the exact byte it names.
 *
 * What the suite pins, one case at a time:
 *
 *  - a valid patch's geometry, the normalised post chain of a column, and the
 *    required-size contract that lets a caller size its buffer without a write;
 *  - a post at row 0, a post whose last pixel is the last row, and a column
 *    whose 0xFF terminator is the last byte of the lump;
 *  - a column offset past the end, a post length overflowing the lump, and a
 *    column with no terminator;
 *  - an empty column, which is one 0xFF byte and no pixels;
 *  - a composite of two overlapping patches, the documented fill, the mask
 *    plane, and the promise that a corrupt patch writes nothing;
 *  - texture lookup in TEXTURE1 and TEXTURE2, the first-match rule, the
 *    documented "only the matched definition is validated" rule, and a miss;
 *  - a flat of the wrong size;
 *  - a TEXTURE1 or PNAMES count whose byte size would wrap a 32-bit size_t,
 *    which is the target's size_t and the one place a naive length check
 *    passes on the board and fails on every 64-bit host.
 *
 * The tests go through the public C API only. They never inspect a decoder's
 * internals, so an implementation that passes here is free to change them.
 */
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <unity.h>

#include "r_data.h"

#include "../../test_config.h"

namespace {

constexpr std::size_t kPatchHeaderSize = 8u;
constexpr std::size_t kTextureHeaderSize = 22u;
constexpr std::size_t kTexturePatchSize = 10u;
constexpr std::size_t kPnamesHeaderSize = 4u;
constexpr std::size_t kFlatBytes = 4096u;

void putLe16(std::uint8_t* at, std::int16_t value) {
    const std::uint16_t bits = static_cast<std::uint16_t>(value);
    at[0] = static_cast<std::uint8_t>(bits & 0xFFu);
    at[1] = static_cast<std::uint8_t>((bits >> 8) & 0xFFu);
}

void putLe32(std::uint8_t* at, std::int32_t value) {
    const std::uint32_t bits = static_cast<std::uint32_t>(value);
    at[0] = static_cast<std::uint8_t>(bits & 0xFFu);
    at[1] = static_cast<std::uint8_t>((bits >> 8) & 0xFFu);
    at[2] = static_cast<std::uint8_t>((bits >> 16) & 0xFFu);
    at[3] = static_cast<std::uint8_t>((bits >> 24) & 0xFFu);
}

/// One post of a synthetic column: the row it starts at and the pixels it
/// carries. The builder derives `length` from the pixel count, as the format
/// does.
struct Post {
    std::uint8_t topDelta;
    std::vector<std::uint8_t> pixels;
};

using Column = std::vector<Post>;

/// A one-post column covering rows [topDelta, topDelta + count).
Column solidColumn(std::uint8_t topDelta, std::uint8_t count, std::uint8_t value) {
    return Column{Post{topDelta, std::vector<std::uint8_t>(count, value)}};
}

/// Assembles a patch: header, the column offset table, then the columns in
/// declaration order. The last byte of the returned lump is the terminator of
/// the last column, which is the "column ending exactly at the end of the lump"
/// layout the format allows.
///
/// `unusedValue` writes the two bytes the format calls unused, so a test can
/// show that the decoder normalises them rather than echoing them.
std::vector<std::uint8_t> buildPatch(std::int16_t width, std::int16_t height,
                                     const std::vector<Column>& columns,
                                     std::int16_t leftOffset = 0,
                                     std::int16_t topOffset = 0,
                                     std::uint8_t unusedValue = 0u) {
    const std::size_t tableEnd =
        kPatchHeaderSize + static_cast<std::size_t>(width) * 4u;
    std::vector<std::uint8_t> lump(tableEnd, 0u);
    putLe16(&lump[0], width);
    putLe16(&lump[2], height);
    putLe16(&lump[4], leftOffset);
    putLe16(&lump[6], topOffset);

    for (std::size_t index = 0; index < columns.size(); ++index) {
        putLe32(&lump[kPatchHeaderSize + index * 4u],
                static_cast<std::int32_t>(lump.size()));

        for (const Post& post : columns[index]) {
            lump.push_back(post.topDelta);
            lump.push_back(static_cast<std::uint8_t>(post.pixels.size()));
            lump.push_back(unusedValue);
            lump.insert(lump.end(), post.pixels.begin(), post.pixels.end());
            lump.push_back(unusedValue);
        }

        lump.push_back(0xFFu);
    }

    return lump;
}

/// One texture definition of a synthetic texture lump.
struct TextureSpec {
    std::string name;
    std::int32_t masked;
    std::int16_t width;
    std::int16_t height;
    /// (originx, originy, patch index) per patch, in draw order.
    std::vector<std::array<std::int16_t, 3>> patches;
};

/// Assembles a TEXTURE1/TEXTURE2 lump: count, offsets, then the definitions.
std::vector<std::uint8_t> buildTextureLump(const std::vector<TextureSpec>& textures) {
    std::vector<std::uint8_t> lump(4u + textures.size() * 4u, 0u);
    putLe32(&lump[0], static_cast<std::int32_t>(textures.size()));

    for (std::size_t index = 0; index < textures.size(); ++index) {
        const TextureSpec& texture = textures[index];
        putLe32(&lump[4u + index * 4u], static_cast<std::int32_t>(lump.size()));

        const std::size_t start = lump.size();
        lump.resize(start + kTextureHeaderSize + texture.patches.size() * kTexturePatchSize,
                    0u);
        for (std::size_t byte = 0; byte < kTextureHeaderSize; ++byte) {
            lump[start + byte] = 0u;
        }
        for (std::size_t byte = 0; byte < texture.name.size() && byte < 8u; ++byte) {
            lump[start + byte] = static_cast<std::uint8_t>(texture.name[byte]);
        }
        putLe32(&lump[start + 8u], texture.masked);
        putLe16(&lump[start + 12u], texture.width);
        putLe16(&lump[start + 14u], texture.height);
        putLe32(&lump[start + 16u], 0); /* obsolete columndirectory */
        putLe16(&lump[start + 20u], static_cast<std::int16_t>(texture.patches.size()));

        for (std::size_t patch = 0; patch < texture.patches.size(); ++patch) {
            std::uint8_t* entry = &lump[start + kTextureHeaderSize + patch * kTexturePatchSize];
            putLe16(entry, texture.patches[patch][0]);
            putLe16(entry + 2, texture.patches[patch][1]);
            putLe16(entry + 4, texture.patches[patch][2]);
            putLe16(entry + 6, 0); /* stepdir, ignored */
            putLe16(entry + 8, 0); /* colormap, ignored */
        }
    }

    return lump;
}

/// Assembles a PNAMES lump from up to 8-character names.
std::vector<std::uint8_t> buildPnames(const std::vector<std::string>& names) {
    std::vector<std::uint8_t> lump(kPnamesHeaderSize + names.size() * 8u, 0u);
    putLe32(&lump[0], static_cast<std::int32_t>(names.size()));

    for (std::size_t index = 0; index < names.size(); ++index) {
        for (std::size_t byte = 0; byte < names[index].size() && byte < 8u; ++byte) {
            lump[kPnamesHeaderSize + index * 8u + byte] =
                static_cast<std::uint8_t>(names[index][byte]);
        }
    }

    return lump;
}

/// The fetch callback r_texture_compose() calls: a name-to-lump table, with a
/// switch that makes every fetch fail.
struct FetchTable {
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> lumps;
    bool refuse = false;
};

r_error_t fetchFromTable(void* user, const char* name, const std::uint8_t** data,
                         std::size_t* size) {
    auto* table = static_cast<FetchTable*>(user);
    if (table->refuse) {
        return R_ERR_PATCH_FETCH;
    }

    for (const auto& entry : table->lumps) {
        if (entry.first == name) {
            *data = entry.second.data();
            *size = entry.second.size();
            return R_ERR_OK;
        }
    }

    return R_ERR_PATCH_FETCH;
}

constexpr std::uint8_t kSentinel = 0x5Au;

// --- Patch validation and column decoding --------------------------------

void test_valid_patch_reports_geometry_and_decodes_a_column() {
    const std::vector<Column> columns{
        Column{Post{1u, {0x11u, 0x22u}}},
        Column{},
    };
    const std::vector<std::uint8_t> lump =
        buildPatch(2, 4, columns, -3, 5, /*unusedValue=*/0x7Eu);

    r_patch_info_t info{};
    TEST_ASSERT_EQUAL_INT(R_ERR_OK, r_patch_validate(lump.data(), lump.size(), &info));
    TEST_ASSERT_EQUAL_INT32(2, info.width);
    TEST_ASSERT_EQUAL_INT32(4, info.height);
    TEST_ASSERT_EQUAL_INT32(-3, info.leftOffset);
    TEST_ASSERT_EQUAL_INT32(5, info.topOffset);
    TEST_ASSERT_EQUAL_size_t(kPatchHeaderSize + 2u * 4u, info.columnTableEnd);

    // "1,2,0,0x11,0x22,0" then the terminator: the two source bytes that were
    // 0x7E are normalised to zero, which is what makes the stream the form
    // R_DrawMaskedColumn() expects.
    const std::uint8_t expected[] = {1u, 2u, 0u, 0x11u, 0x22u, 0u, 0xFFu};
    std::uint8_t out[16];
    std::size_t required = 0u;
    TEST_ASSERT_EQUAL_INT(R_ERR_OK,
                          r_patch_column(&info, lump.data(), lump.size(), 0, out,
                                         sizeof out, &required));
    TEST_ASSERT_EQUAL_size_t(sizeof expected, required);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, sizeof expected);

    // The empty column is the terminator and nothing else.
    std::uint8_t empty[2] = {kSentinel, kSentinel};
    required = 99u;
    TEST_ASSERT_EQUAL_INT(R_ERR_OK,
                          r_patch_column(&info, lump.data(), lump.size(), 1, empty,
                                         sizeof empty, &required));
    TEST_ASSERT_EQUAL_size_t(1u, required);
    TEST_ASSERT_EQUAL_UINT8(0xFFu, empty[0]);
    TEST_ASSERT_EQUAL_UINT8(kSentinel, empty[1]);
}

void test_column_index_and_output_arguments_are_checked() {
    const std::vector<std::uint8_t> lump =
        buildPatch(1, 2, {solidColumn(0u, 2u, 0x33u)});
    r_patch_info_t info{};
    TEST_ASSERT_EQUAL_INT(R_ERR_OK, r_patch_validate(lump.data(), lump.size(), &info));

    std::uint8_t out[8];
    TEST_ASSERT_EQUAL_INT(R_ERR_COLUMN_INDEX,
                          r_patch_column(&info, lump.data(), lump.size(), -1, out,
                                         sizeof out, nullptr));
    TEST_ASSERT_EQUAL_INT(R_ERR_COLUMN_INDEX,
                          r_patch_column(&info, lump.data(), lump.size(), 1, out,
                                         sizeof out, nullptr));
    TEST_ASSERT_EQUAL_INT(R_ERR_NULL_OUTPUT,
                          r_patch_column(&info, lump.data(), lump.size(), 0, nullptr,
                                         0u, nullptr));
    TEST_ASSERT_EQUAL_INT(R_ERR_NULL_INFO,
                          r_patch_column(nullptr, lump.data(), lump.size(), 0, out,
                                         sizeof out, nullptr));
    TEST_ASSERT_EQUAL_INT(R_ERR_NULL_DATA,
                          r_patch_column(&info, nullptr, lump.size(), 0, out,
                                         sizeof out, nullptr));

    // A buffer too short for the info's column table is refused before any
    // offset is read, so a stale descriptor cannot walk off the buffer.
    TEST_ASSERT_EQUAL_INT(R_ERR_PATCH_COLUMN_TABLE,
                          r_patch_column(&info, lump.data(), 8u, 0, out,
                                         sizeof out, nullptr));
}

void test_short_buffer_reports_the_required_length_and_writes_nothing() {
    const std::vector<std::uint8_t> lump =
        buildPatch(1, 3, {solidColumn(0u, 3u, 0x44u)});
    r_patch_info_t info{};
    TEST_ASSERT_EQUAL_INT(R_ERR_OK, r_patch_validate(lump.data(), lump.size(), &info));

    // One post: 3 header bytes + 3 pixels + 1 trailer = 7, plus the terminator.
    const std::size_t expectedLength = 3u + 3u + 1u + 1u;
    std::uint8_t out[8];
    for (std::uint8_t& byte : out) {
        byte = kSentinel;
    }

    std::size_t required = 0u;
    TEST_ASSERT_EQUAL_INT(R_ERR_BUFFER_TOO_SMALL,
                          r_patch_column(&info, lump.data(), lump.size(), 0, out,
                                         expectedLength - 1u, &required));
    TEST_ASSERT_EQUAL_size_t(expectedLength, required);
    for (std::uint8_t byte : out) {
        TEST_ASSERT_EQUAL_UINT8(kSentinel, byte);
    }
}

void test_post_at_row_zero_and_at_the_last_row_decode() {
    const std::vector<Column> columns{
        Column{Post{0u, {0x01u}}, Post{7u, {0x02u}}},
    };
    const std::vector<std::uint8_t> lump = buildPatch(1, 8, columns);

    r_patch_info_t info{};
    TEST_ASSERT_EQUAL_INT(R_ERR_OK, r_patch_validate(lump.data(), lump.size(), &info));

    const std::uint8_t expected[] = {
        0u, 1u, 0u, 0x01u, 0u, /* rows 0..0 */
        7u, 1u, 0u, 0x02u, 0u, /* rows 7..7, the last row */
        0xFFu,
    };
    std::uint8_t out[sizeof expected];
    std::size_t required = 0u;
    TEST_ASSERT_EQUAL_INT(R_ERR_OK,
                          r_patch_column(&info, lump.data(), lump.size(), 0, out,
                                         sizeof out, &required));
    TEST_ASSERT_EQUAL_size_t(sizeof expected, required);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, sizeof expected);

    // A post one row longer than the patch height is rejected: the last row is
    // height-1, so topdelta + length must equal it at most.
    std::vector<std::uint8_t> tooTall =
        buildPatch(1, 8, {Column{Post{7u, {0x02u, 0x03u}}}});
    TEST_ASSERT_EQUAL_INT(R_ERR_PATCH_POST_BOUNDS,
                          r_patch_validate(tooTall.data(), tooTall.size(), &info));
}

void test_column_ending_at_the_end_of_the_lump_is_accepted() {
    const std::vector<std::uint8_t> lump =
        buildPatch(1, 4, {solidColumn(1u, 2u, 0x55u)});
    TEST_ASSERT_EQUAL_UINT8(0xFFu, lump.back());

    r_patch_info_t info{};
    TEST_ASSERT_EQUAL_INT(R_ERR_OK, r_patch_validate(lump.data(), lump.size(), &info));

    // Drop the terminator: the byte that must carry the next topdelta is now
    // outside the lump, so the chain never ends.
    std::vector<std::uint8_t> truncated(lump.begin(), lump.end() - 1);
    TEST_ASSERT_EQUAL_INT(R_ERR_PATCH_COLUMN_END,
                          r_patch_validate(truncated.data(), truncated.size(), &info));
}

void test_column_offset_past_the_end_is_rejected() {
    std::vector<std::uint8_t> lump = buildPatch(2, 2, {solidColumn(0u, 1u, 0x01u),
                                                       solidColumn(0u, 1u, 0x02u)});
    r_patch_info_t info{};

    std::vector<std::uint8_t> past = lump;
    putLe32(&past[kPatchHeaderSize + 4u], static_cast<std::int32_t>(past.size()));
    TEST_ASSERT_EQUAL_INT(R_ERR_PATCH_COLUMN_OFFSET,
                          r_patch_validate(past.data(), past.size(), &info));

    // An offset that points into the column table is not a column either.
    std::vector<std::uint8_t> insideHeader = lump;
    putLe32(&insideHeader[kPatchHeaderSize], 0);
    TEST_ASSERT_EQUAL_INT(R_ERR_PATCH_COLUMN_OFFSET,
                          r_patch_validate(insideHeader.data(), insideHeader.size(), &info));

    // A negative offset is the same failure class.
    std::vector<std::uint8_t> negative = lump;
    putLe32(&negative[kPatchHeaderSize], -4);
    TEST_ASSERT_EQUAL_INT(R_ERR_PATCH_COLUMN_OFFSET,
                          r_patch_validate(negative.data(), negative.size(), &info));
}

void test_post_length_overflowing_the_lump_is_rejected() {
    std::vector<std::uint8_t> lump = buildPatch(1, 4, {solidColumn(0u, 2u, 0x66u)});

    // The post starts right after the table; its length byte is at start + 1.
    const std::size_t columnStart = kPatchHeaderSize + 4u;
    lump[columnStart + 1u] = 200u;

    r_patch_info_t info{};
    TEST_ASSERT_EQUAL_INT(R_ERR_PATCH_POST_RANGE,
                          r_patch_validate(lump.data(), lump.size(), &info));
}

void test_column_with_no_terminator_is_rejected() {
    const std::vector<std::uint8_t> lump =
        buildPatch(1, 4, {solidColumn(0u, 4u, 0x77u)});
    r_patch_info_t info{};

    std::vector<std::uint8_t> truncated(lump.begin(), lump.end() - 1);
    TEST_ASSERT_EQUAL_INT(R_ERR_PATCH_COLUMN_END,
                          r_patch_validate(truncated.data(), truncated.size(), &info));

    // The post's own pixels are still inside: only the terminator is missing.
    TEST_ASSERT_EQUAL_size_t(lump.size() - 1u, truncated.size());
}

void test_dimension_and_table_failures_are_distinct() {
    r_patch_info_t info{};
    const std::vector<std::uint8_t> zeroWidth = buildPatch(0, 4, {});
    TEST_ASSERT_EQUAL_INT(R_ERR_PATCH_WIDTH,
                          r_patch_validate(zeroWidth.data(), zeroWidth.size(), &info));

    const std::vector<std::uint8_t> zeroHeight = buildPatch(1, 0, {solidColumn(0u, 1u, 1u)});
    TEST_ASSERT_EQUAL_INT(R_ERR_PATCH_HEIGHT,
                          r_patch_validate(zeroHeight.data(), zeroHeight.size(), &info));

    // A table that claims more columns than the lump can hold.
    std::vector<std::uint8_t> lump = buildPatch(1, 2, {solidColumn(0u, 1u, 1u)});
    putLe16(&lump[0], 40);
    TEST_ASSERT_EQUAL_INT(R_ERR_PATCH_COLUMN_TABLE,
                          r_patch_validate(lump.data(), lump.size(), &info));

    const std::vector<std::uint8_t> tiny{0u, 1u, 2u, 3u};
    TEST_ASSERT_EQUAL_INT(R_ERR_LUMP_TOO_SMALL,
                          r_patch_validate(tiny.data(), tiny.size(), &info));
}

void test_patch_null_arguments_and_untouched_info() {
    r_patch_info_t info{};
    info.width = 1234;
    info.height = 5678;

    const std::vector<std::uint8_t> lump =
        buildPatch(1, 2, {solidColumn(0u, 2u, 0x01u)});
    TEST_ASSERT_EQUAL_INT(R_ERR_NULL_INFO, r_patch_validate(lump.data(), lump.size(), nullptr));
    TEST_ASSERT_EQUAL_INT(R_ERR_NULL_DATA, r_patch_validate(nullptr, 16u, &info));
    TEST_ASSERT_EQUAL_INT(R_ERR_LUMP_TOO_SMALL, r_patch_validate(lump.data(), 0u, &info));

    // Nothing was accepted, so nothing was written.
    TEST_ASSERT_EQUAL_INT32(1234, info.width);
    TEST_ASSERT_EQUAL_INT32(5678, info.height);
}

// --- Textures ------------------------------------------------------------

void test_texture_lookup_finds_both_lumps_and_first_match_wins() {
    const std::vector<TextureSpec> first{
        TextureSpec{"FIRST", 0, 8, 16, {{0, 0, 0}}},
        TextureSpec{"SECOND", 1, 32, 64, {{1, 2, 0}}},
        TextureSpec{"FIRST", 0, 4, 4, {}},
        TextureSpec{"ABCDEFGH", 0, 16, 16, {}},
    };
    const std::vector<TextureSpec> second{
        TextureSpec{"THIRD", 0, 2, 3, {{0, 0, 0}, {0, 0, 1}}},
    };
    const std::vector<std::uint8_t> texture1 = buildTextureLump(first);
    const std::vector<std::uint8_t> texture2 = buildTextureLump(second);

    r_texture_info_t info{};
    TEST_ASSERT_EQUAL_INT(R_ERR_OK,
                          r_texture_lookup(texture1.data(), texture1.size(),
                                           texture2.data(), texture2.size(),
                                           "first", &info));
    TEST_ASSERT_EQUAL_INT32(1, info.source);
    TEST_ASSERT_EQUAL_INT32(0, info.index);
    TEST_ASSERT_EQUAL_INT32(8, info.width);
    TEST_ASSERT_EQUAL_INT32(16, info.height);
    TEST_ASSERT_EQUAL_INT32(0, info.masked);
    TEST_ASSERT_EQUAL_INT32(1, info.patchCount);
    TEST_ASSERT_EQUAL_STRING("FIRST", info.name);
    TEST_ASSERT_EQUAL_size_t(texture1.size(), info.lumpSize);

    // Exactly the 8 field bytes are compared, so a query longer than the field
    // matches on its first 8...
    TEST_ASSERT_EQUAL_INT(R_ERR_OK,
                          r_texture_lookup(texture1.data(), texture1.size(),
                                           texture2.data(), texture2.size(),
                                           "abcdefghZZZ", &info));
    TEST_ASSERT_EQUAL_INT32(3, info.index);
    TEST_ASSERT_EQUAL_STRING("ABCDEFGH", info.name);

    // ... and a short, NUL-padded field does not match a query that continues
    // past it: "FirstIgnoreTail" differs at byte 5, where the field is NUL.
    TEST_ASSERT_EQUAL_INT(R_ERR_TEXTURE_NAME,
                          r_texture_lookup(texture1.data(), texture1.size(),
                                           texture2.data(), texture2.size(),
                                           "FirstIgnoreTail", &info));

    // The duplicate later in the lump does not shadow the first one.
    TEST_ASSERT_EQUAL_INT(R_ERR_OK,
                          r_texture_lookup(texture1.data(), texture1.size(),
                                           texture2.data(), texture2.size(),
                                           "FIRST", &info));
    TEST_ASSERT_EQUAL_INT32(0, info.index);
    TEST_ASSERT_EQUAL_INT32(8, info.width);

    TEST_ASSERT_EQUAL_INT(R_ERR_OK,
                          r_texture_lookup(texture1.data(), texture1.size(),
                                           texture2.data(), texture2.size(),
                                           "SECOND", &info));
    TEST_ASSERT_EQUAL_INT32(1, info.index);
    TEST_ASSERT_EQUAL_INT32(1, info.masked);

    // TEXTURE2 is searched after TEXTURE1 and is the only place THIRD exists.
    TEST_ASSERT_EQUAL_INT(R_ERR_OK,
                          r_texture_lookup(texture1.data(), texture1.size(),
                                           texture2.data(), texture2.size(),
                                           "THIRD", &info));
    TEST_ASSERT_EQUAL_INT32(2, info.source);
    TEST_ASSERT_EQUAL_INT32(0, info.index);
    TEST_ASSERT_EQUAL_INT32(2, info.patchCount);

    // Missing names carry their own code.
    TEST_ASSERT_EQUAL_INT(R_ERR_TEXTURE_NAME,
                          r_texture_lookup(texture1.data(), texture1.size(),
                                           texture2.data(), texture2.size(),
                                           "NOPE", &info));
}

void test_texture_lookup_accepts_a_single_lump() {
    const std::vector<std::uint8_t> texture2 =
        buildTextureLump({TextureSpec{"ONLY2", 0, 4, 4, {}}});

    r_texture_info_t info{};
    TEST_ASSERT_EQUAL_INT(R_ERR_OK,
                          r_texture_lookup(nullptr, 0u, texture2.data(),
                                           texture2.size(), "ONLY2", &info));
    TEST_ASSERT_EQUAL_INT32(2, info.source);
    TEST_ASSERT_EQUAL_INT32(0, info.index);
}

void test_texture_lookup_rejects_broken_inputs() {
    const std::vector<std::uint8_t> good =
        buildTextureLump({TextureSpec{"GOOD", 0, 4, 4, {}}});
    r_texture_info_t info{};

    TEST_ASSERT_EQUAL_INT(R_ERR_NULL_INFO,
                          r_texture_lookup(good.data(), good.size(), nullptr, 0u,
                                           "GOOD", nullptr));
    TEST_ASSERT_EQUAL_INT(R_ERR_NULL_DATA,
                          r_texture_lookup(good.data(), good.size(), nullptr, 0u,
                                           nullptr, &info));
    TEST_ASSERT_EQUAL_INT(R_ERR_NULL_DATA,
                          r_texture_lookup(nullptr, 0u, nullptr, 0u, "GOOD", &info));

    const std::vector<std::uint8_t> tiny{0u, 0u};
    TEST_ASSERT_EQUAL_INT(R_ERR_LUMP_TOO_SMALL,
                          r_texture_lookup(tiny.data(), tiny.size(), nullptr, 0u,
                                           "GOOD", &info));

    // A directory that cannot hold its own offsets.
    std::vector<std::uint8_t> badCount = good;
    putLe32(&badCount[0], 100);
    TEST_ASSERT_EQUAL_INT(R_ERR_TEXTURE_DIRECTORY,
                          r_texture_lookup(badCount.data(), badCount.size(), nullptr, 0u,
                                           "GOOD", &info));

    std::vector<std::uint8_t> negative = good;
    putLe32(&negative[0], -1);
    TEST_ASSERT_EQUAL_INT(R_ERR_TEXTURE_COUNT,
                          r_texture_lookup(negative.data(), negative.size(), nullptr, 0u,
                                           "GOOD", &info));

    // A count whose byte size wraps a 32-bit size_t must still be refused: the
    // decoder compares the count against the buffer before multiplying.
    std::vector<std::uint8_t> wrapping = good;
    putLe32(&wrapping[0], 0x40000000);
    TEST_ASSERT_EQUAL_INT(R_ERR_TEXTURE_DIRECTORY,
                          r_texture_lookup(wrapping.data(), wrapping.size(), nullptr, 0u,
                                           "GOOD", &info));

    // An offset past the lump.
    std::vector<std::uint8_t> past = good;
    putLe32(&past[4], static_cast<std::int32_t>(past.size()));
    TEST_ASSERT_EQUAL_INT(R_ERR_TEXTURE_DIRECTORY,
                          r_texture_lookup(past.data(), past.size(), nullptr, 0u,
                                           "GOOD", &info));

    // A matched definition whose patch list is cut off.
    std::vector<std::uint8_t> shortHeader(
        good.begin(), good.begin() + static_cast<std::ptrdiff_t>(4u + 4u + 12u));
    TEST_ASSERT_EQUAL_INT(R_ERR_TEXTURE_HEADER,
                          r_texture_lookup(shortHeader.data(), shortHeader.size(), nullptr,
                                           0u, "GOOD", &info));
}

void test_texture_lookup_validates_only_the_matched_definition() {
    // The first definition has a zero width, the second is fine. A lookup for
    // the second must succeed: DOOM never touches a definition it was not
    // asked for, and the header records that this decoder only validates the
    // match.
    std::vector<std::uint8_t> lump = buildTextureLump({
        TextureSpec{"BROKEN", 0, 0, 8, {}},
        TextureSpec{"FINE", 0, 8, 8, {}},
    });

    r_texture_info_t info{};
    TEST_ASSERT_EQUAL_INT(R_ERR_OK,
                          r_texture_lookup(lump.data(), lump.size(), nullptr, 0u, "FINE",
                                           &info));
    TEST_ASSERT_EQUAL_INT32(1, info.index);

    TEST_ASSERT_EQUAL_INT(R_ERR_TEXTURE_DIMENSION,
                          r_texture_lookup(lump.data(), lump.size(), nullptr, 0u, "BROKEN",
                                           &info));
}

// --- Composite surfaces --------------------------------------------------

/// A two-by-two patch of one solid colour, for the composition cases.
std::vector<std::uint8_t> twoByTwo(std::uint8_t value) {
    return buildPatch(2, 2, {solidColumn(0u, 2u, value), solidColumn(0u, 2u, value)});
}

void test_compose_blits_overlapping_patches_and_sets_the_mask() {
    FetchTable table;
    table.lumps.emplace_back("PATCHA", twoByTwo(0xA0u));
    table.lumps.emplace_back("PATCHB", twoByTwo(0xB0u));

    const std::vector<std::uint8_t> pnames = buildPnames({"PATCHA", "PATCHB"});
    const std::vector<std::uint8_t> texture1 = buildTextureLump({
        TextureSpec{"COMPOSITE", 1, 4, 4, {{0, 0, 0}, {1, 1, 1}}},
    });

    r_texture_info_t info{};
    TEST_ASSERT_EQUAL_INT(R_ERR_OK,
                          r_texture_lookup(texture1.data(), texture1.size(), nullptr, 0u,
                                           "COMPOSITE", &info));

    std::uint8_t pixels[16];
    std::uint8_t mask[16];
    std::size_t required = 0u;
    TEST_ASSERT_EQUAL_INT(R_ERR_OK,
                          r_texture_compose(&info, pnames.data(), pnames.size(),
                                            fetchFromTable, &table, pixels, sizeof pixels,
                                            mask, sizeof mask, &required));
    TEST_ASSERT_EQUAL_size_t(sizeof pixels, required);

    // Column-major, as R_GetColumn() and the column drawer expect: pixel (x, y)
    // lives at x * height + y. A is at (0,0)-(1,1); B at (1,1)-(2,2) and wins
    // the (1,1) overlap because it is drawn later.
    const std::uint8_t expectedPixels[16] = {
        0xA0u, 0xA0u, 0x00u, 0x00u, /* column 0 */
        0xA0u, 0xB0u, 0xB0u, 0x00u, /* column 1 */
        0x00u, 0xB0u, 0xB0u, 0x00u, /* column 2 */
        0x00u, 0x00u, 0x00u, 0x00u, /* column 3 */
    };
    const std::uint8_t expectedMask[16] = {
        1u, 1u, 0u, 0u,
        1u, 1u, 1u, 0u,
        0u, 1u, 1u, 0u,
        0u, 0u, 0u, 0u,
    };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedPixels, pixels, sizeof pixels);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedMask, mask, sizeof mask);
}

void test_compose_clips_patches_that_leave_the_texture() {
    // A 2x2 patch at (-1, 2) in a 2x4 texture: only its right column, at rows
    // 2..3, is inside. A negative origin is legal and must clip, not reject.
    FetchTable table;
    table.lumps.emplace_back("CORNER", twoByTwo(0xC0u));

    const std::vector<std::uint8_t> pnames = buildPnames({"CORNER"});
    const std::vector<std::uint8_t> texture1 = buildTextureLump({
        TextureSpec{"CLIPPED", 1, 2, 4, {{-1, 2, 0}}},
    });

    r_texture_info_t info{};
    TEST_ASSERT_EQUAL_INT(R_ERR_OK,
                          r_texture_lookup(texture1.data(), texture1.size(), nullptr, 0u,
                                           "CLIPPED", &info));

    std::uint8_t pixels[8];
    std::uint8_t mask[8];
    std::size_t required = 0u;
    TEST_ASSERT_EQUAL_INT(R_ERR_OK,
                          r_texture_compose(&info, pnames.data(), pnames.size(),
                                            fetchFromTable, &table, pixels, sizeof pixels,
                                            mask, sizeof mask, &required));
    TEST_ASSERT_EQUAL_size_t(8u, required);

    // Column-major: pixel (x, y) is at x * height + y. The patch's left column
    // sits at x = -1 and is clipped away entirely; its right column lands on
    // x = 0 at rows 2..3.
    const std::uint8_t expectedPixels[8] = {0u, 0u, 0xC0u, 0xC0u, 0u, 0u, 0u, 0u};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedPixels, pixels, sizeof pixels);
    TEST_ASSERT_EQUAL_UINT8(0u, mask[0]);
    TEST_ASSERT_EQUAL_UINT8(1u, mask[2]);
    TEST_ASSERT_EQUAL_UINT8(1u, mask[3]);
    TEST_ASSERT_EQUAL_UINT8(0u, mask[7]);
}

void test_compose_surface_can_skip_the_mask() {
    FetchTable table;
    table.lumps.emplace_back("ONE", twoByTwo(0x10u));

    const std::vector<std::uint8_t> pnames = buildPnames({"ONE"});
    const std::vector<std::uint8_t> texture1 = buildTextureLump({
        TextureSpec{"SOLO", 0, 2, 2, {{0, 0, 0}}},
    });

    r_texture_info_t info{};
    TEST_ASSERT_EQUAL_INT(R_ERR_OK,
                          r_texture_lookup(texture1.data(), texture1.size(), nullptr, 0u,
                                           "SOLO", &info));

    std::uint8_t pixels[4];
    TEST_ASSERT_EQUAL_INT(R_ERR_OK,
                          r_texture_compose(&info, pnames.data(), pnames.size(),
                                            fetchFromTable, &table, pixels, sizeof pixels,
                                            nullptr, 0u, nullptr));
    TEST_ASSERT_EQUAL_UINT8(0x10u, pixels[0]);
    TEST_ASSERT_EQUAL_UINT8(0x10u, pixels[3]);
}

void test_compose_short_buffers_write_nothing() {
    FetchTable table;
    table.lumps.emplace_back("ONE", twoByTwo(0x10u));

    const std::vector<std::uint8_t> pnames = buildPnames({"ONE"});
    const std::vector<std::uint8_t> texture1 = buildTextureLump({
        TextureSpec{"SOLO", 0, 2, 2, {{0, 0, 0}}},
    });

    r_texture_info_t info{};
    TEST_ASSERT_EQUAL_INT(R_ERR_OK,
                          r_texture_lookup(texture1.data(), texture1.size(), nullptr, 0u,
                                           "SOLO", &info));

    std::uint8_t pixels[4] = {kSentinel, kSentinel, kSentinel, kSentinel};
    std::uint8_t mask[4] = {kSentinel, kSentinel, kSentinel, kSentinel};
    std::size_t required = 0u;

    TEST_ASSERT_EQUAL_INT(R_ERR_BUFFER_TOO_SMALL,
                          r_texture_compose(&info, pnames.data(), pnames.size(),
                                            fetchFromTable, &table, pixels, 3u, mask,
                                            sizeof mask, &required));
    TEST_ASSERT_EQUAL_size_t(4u, required);
    TEST_ASSERT_EQUAL_UINT8(kSentinel, pixels[0]);

    TEST_ASSERT_EQUAL_INT(R_ERR_BUFFER_TOO_SMALL,
                          r_texture_compose(&info, pnames.data(), pnames.size(),
                                            fetchFromTable, &table, pixels, sizeof pixels,
                                            mask, 3u, &required));
    TEST_ASSERT_EQUAL_size_t(4u, required);
    TEST_ASSERT_EQUAL_UINT8(kSentinel, pixels[0]);

    TEST_ASSERT_EQUAL_INT(R_ERR_NULL_OUTPUT,
                          r_texture_compose(&info, pnames.data(), pnames.size(),
                                            fetchFromTable, &table, nullptr, 0u, nullptr,
                                            0u, &required));
    TEST_ASSERT_EQUAL_size_t(4u, required);
}

void test_compose_validates_every_patch_before_writing() {
    // The second patch is corrupt. The surface must be untouched: the first
    // pass proved the whole texture before the fill.
    std::vector<std::uint8_t> corrupt = twoByTwo(0x20u);
    const std::size_t firstColumn = kPatchHeaderSize + 2u * 4u;
    corrupt[firstColumn + 1u] = 250u; /* the length byte of the first post */

    FetchTable table;
    table.lumps.emplace_back("FINE", twoByTwo(0x20u));
    table.lumps.emplace_back("BROKEN", corrupt);

    const std::vector<std::uint8_t> pnames = buildPnames({"FINE", "BROKEN"});
    const std::vector<std::uint8_t> texture1 = buildTextureLump({
        TextureSpec{"TWO", 0, 2, 2, {{0, 0, 0}, {0, 0, 1}}},
    });

    r_texture_info_t info{};
    TEST_ASSERT_EQUAL_INT(R_ERR_OK,
                          r_texture_lookup(texture1.data(), texture1.size(), nullptr, 0u,
                                           "TWO", &info));

    std::uint8_t pixels[4] = {kSentinel, kSentinel, kSentinel, kSentinel};
    std::uint8_t mask[4] = {kSentinel, kSentinel, kSentinel, kSentinel};
    std::size_t required = 0u;
    TEST_ASSERT_EQUAL_INT(R_ERR_PATCH_POST_RANGE,
                          r_texture_compose(&info, pnames.data(), pnames.size(),
                                            fetchFromTable, &table, pixels, sizeof pixels,
                                            mask, sizeof mask, &required));
    for (std::uint8_t byte : pixels) {
        TEST_ASSERT_EQUAL_UINT8(kSentinel, byte);
    }
    for (std::uint8_t byte : mask) {
        TEST_ASSERT_EQUAL_UINT8(kSentinel, byte);
    }
}

void test_compose_reports_missing_patches_and_bad_indices() {
    FetchTable table;
    table.lumps.emplace_back("PRESENT", twoByTwo(0x30u));

    const std::vector<std::uint8_t> pnames = buildPnames({"PRESENT"});
    const std::vector<std::uint8_t> texture1 = buildTextureLump({
        TextureSpec{"FETCHFAIL", 0, 2, 2, {{0, 0, 0}}},
        TextureSpec{"BADINDEX", 0, 2, 2, {{0, 0, 1}}},
    });

    r_texture_info_t info{};
    std::uint8_t pixels[4];
    std::size_t required = 0u;

    TEST_ASSERT_EQUAL_INT(R_ERR_OK,
                          r_texture_lookup(texture1.data(), texture1.size(), nullptr, 0u,
                                           "BADINDEX", &info));
    TEST_ASSERT_EQUAL_INT(R_ERR_PATCH_INDEX,
                          r_texture_compose(&info, pnames.data(), pnames.size(),
                                            fetchFromTable, &table, pixels, sizeof pixels,
                                            nullptr, 0u, &required));

    // A callback that refuses: its own code, and nothing written.
    std::uint8_t untouched[4] = {kSentinel, kSentinel, kSentinel, kSentinel};
    FetchTable refusing;
    refusing.refuse = true;
    TEST_ASSERT_EQUAL_INT(R_ERR_OK,
                          r_texture_lookup(texture1.data(), texture1.size(), nullptr, 0u,
                                           "FETCHFAIL", &info));
    TEST_ASSERT_EQUAL_INT(R_ERR_PATCH_FETCH,
                          r_texture_compose(&info, pnames.data(), pnames.size(),
                                            fetchFromTable, &refusing, untouched,
                                            sizeof untouched, nullptr, 0u, &required));
    for (std::uint8_t byte : untouched) {
        TEST_ASSERT_EQUAL_UINT8(kSentinel, byte);
    }

    // A PNAMES lump too short for its own name array.
    std::vector<std::uint8_t> badPnames = pnames;
    putLe32(&badPnames[0], 40);
    TEST_ASSERT_EQUAL_INT(R_ERR_PNAMES_RANGE,
                          r_texture_compose(&info, badPnames.data(), badPnames.size(),
                                            fetchFromTable, &table, pixels, sizeof pixels,
                                            nullptr, 0u, &required));

    // And a count whose byte size wraps a 32-bit size_t: 0x20000000 * 8 is 4 GiB.
    std::vector<std::uint8_t> wrappingPnames = pnames;
    putLe32(&wrappingPnames[0], 0x20000000);
    TEST_ASSERT_EQUAL_INT(R_ERR_PNAMES_RANGE,
                          r_texture_compose(&info, wrappingPnames.data(),
                                            wrappingPnames.size(), fetchFromTable, &table,
                                            pixels, sizeof pixels, nullptr, 0u, &required));

    TEST_ASSERT_EQUAL_INT(R_ERR_NULL_CALLBACK,
                          r_texture_compose(&info, pnames.data(), pnames.size(), nullptr,
                                            &table, pixels, sizeof pixels, nullptr, 0u,
                                            &required));
    TEST_ASSERT_EQUAL_INT(R_ERR_NULL_INFO,
                          r_texture_compose(nullptr, pnames.data(), pnames.size(),
                                            fetchFromTable, &table, pixels, sizeof pixels,
                                            nullptr, 0u, &required));
}

// --- Flats ---------------------------------------------------------------

void test_flat_accepts_exactly_sixty_four_squared_bytes() {
    std::vector<std::uint8_t> flat(kFlatBytes);
    for (std::size_t index = 0; index < flat.size(); ++index) {
        flat[index] = static_cast<std::uint8_t>(index & 0xFFu);
    }

    TEST_ASSERT_EQUAL_INT(R_ERR_OK, r_flat_validate(flat.data(), flat.size()));
    TEST_ASSERT_EQUAL_INT(R_ERR_FLAT_SIZE, r_flat_validate(flat.data(), flat.size() - 1u));
    TEST_ASSERT_EQUAL_INT(R_ERR_FLAT_SIZE, r_flat_validate(flat.data(), flat.size() + 1u));
    TEST_ASSERT_EQUAL_INT(R_ERR_FLAT_SIZE, r_flat_validate(flat.data(), 0u));
    TEST_ASSERT_EQUAL_INT(R_ERR_NULL_DATA, r_flat_validate(nullptr, flat.size()));
}

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    test_setup();
    UNITY_BEGIN();

    RUN_TEST(test_valid_patch_reports_geometry_and_decodes_a_column);
    RUN_TEST(test_column_index_and_output_arguments_are_checked);
    RUN_TEST(test_short_buffer_reports_the_required_length_and_writes_nothing);
    RUN_TEST(test_post_at_row_zero_and_at_the_last_row_decode);
    RUN_TEST(test_column_ending_at_the_end_of_the_lump_is_accepted);
    RUN_TEST(test_column_offset_past_the_end_is_rejected);
    RUN_TEST(test_post_length_overflowing_the_lump_is_rejected);
    RUN_TEST(test_column_with_no_terminator_is_rejected);
    RUN_TEST(test_dimension_and_table_failures_are_distinct);
    RUN_TEST(test_patch_null_arguments_and_untouched_info);
    RUN_TEST(test_texture_lookup_finds_both_lumps_and_first_match_wins);
    RUN_TEST(test_texture_lookup_accepts_a_single_lump);
    RUN_TEST(test_texture_lookup_rejects_broken_inputs);
    RUN_TEST(test_texture_lookup_validates_only_the_matched_definition);
    RUN_TEST(test_compose_blits_overlapping_patches_and_sets_the_mask);
    RUN_TEST(test_compose_clips_patches_that_leave_the_texture);
    RUN_TEST(test_compose_surface_can_skip_the_mask);
    RUN_TEST(test_compose_short_buffers_write_nothing);
    RUN_TEST(test_compose_validates_every_patch_before_writing);
    RUN_TEST(test_compose_reports_missing_patches_and_bad_indices);
    RUN_TEST(test_flat_accepts_exactly_sixty_four_squared_bytes);

    const int result = UNITY_END();
    test_teardown();
    return result;
}
