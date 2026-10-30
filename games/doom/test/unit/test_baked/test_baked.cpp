/*
 * Behaviour tests for the baked-graphics adapter (src/doomc/BakedGraphics.cpp).
 *
 * Every container here is assembled byte for byte by this file. No binary
 * fixture is committed and no WAD is needed: the manifest format and the raw
 * payload contract are stated at the boundary they name, and the adapter is
 * driven through its public API only.
 *
 * The cases cover the manifest parser (valid, truncated, wrong magic, duplicate
 * name, index out of range, unknown kind, bad shape), the wall-record
 * derivation (widthMask, heightMask == 127, mask present and absent), the flat
 * 4,096-byte shape and the raw sprite record. Two failure cases pin the
 * no-fallback rule: a stored payload and a payload whose size disagrees with its
 * record are refused, not decoded.
 */
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <unity.h>

#include "doomc/BakedGraphics.h"
#include "whd.h"

#include "../../test_config.h"

using namespace doom::doomc;

namespace {

constexpr std::size_t kHeaderSize = 40u;
constexpr std::size_t kDirectoryEntrySize = 16u;
constexpr std::size_t kNameEntrySize = 12u;
constexpr std::size_t kNameSize = 8u;
constexpr std::size_t kRawAlignment = 4u;

constexpr std::uint8_t kMethodStored = 0u;
constexpr std::uint8_t kMethodRaw = 2u;

void putLe16(std::uint8_t* at, std::uint16_t value) {
    at[0] = static_cast<std::uint8_t>(value & 0xFFu);
    at[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
}

void putLe32(std::uint8_t* at, std::uint32_t value) {
    at[0] = static_cast<std::uint8_t>(value & 0xFFu);
    at[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    at[2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    at[3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

r_name_t packName(const char* text) {
    std::uint8_t field[8] = {};
    for (std::size_t index = 0; index < 8u && text[index] != '\0'; ++index) {
        field[index] = static_cast<std::uint8_t>(text[index]);
    }
    return r_pack_name(field);
}

struct LumpSpec {
    std::string name;
    std::vector<std::uint8_t> payload;
    std::uint8_t method = kMethodRaw;
};

std::vector<std::uint8_t> buildContainer(const std::vector<LumpSpec>& lumps) {
    const std::uint32_t count = static_cast<std::uint32_t>(lumps.size());
    const std::uint32_t directoryOffset = static_cast<std::uint32_t>(kHeaderSize);
    const std::uint32_t nameTableOffset =
        directoryOffset + count * static_cast<std::uint32_t>(kDirectoryEntrySize);
    const std::uint32_t dataOffset =
        nameTableOffset + count * static_cast<std::uint32_t>(kNameEntrySize);

    std::vector<std::uint32_t> offsets(count, 0u);
    std::vector<std::uint32_t> padding(count, 0u);
    std::uint32_t cursor = 0u;
    for (std::uint32_t index = 0; index < count; ++index) {
        if (lumps[index].method == kMethodRaw) {
            const std::uint32_t pad = (kRawAlignment - ((dataOffset + cursor) % kRawAlignment))
                                      % kRawAlignment;
            padding[index] = pad;
            cursor += pad;
        }
        offsets[index] = cursor;
        cursor += static_cast<std::uint32_t>(lumps[index].payload.size());
    }

    std::vector<std::uint8_t> container;
    container.resize(kHeaderSize, 0u);
    container[0] = 'P';
    container[1] = 'W';
    container[2] = 'H';
    container[3] = 'D';
    putLe16(container.data() + 4, 2u);
    putLe16(container.data() + 6, 0u);
    putLe32(container.data() + 8, count);
    putLe32(container.data() + 12, count);
    putLe32(container.data() + 16, directoryOffset);
    putLe32(container.data() + 20, nameTableOffset);
    putLe32(container.data() + 24, dataOffset);
    putLe32(container.data() + 28, dataOffset + cursor);
    putLe32(container.data() + 32, 0u);
    putLe32(container.data() + 36, 0u);
    container.resize(dataOffset, 0u);

    for (std::uint32_t index = 0; index < count; ++index) {
        const std::size_t at = directoryOffset + index * kDirectoryEntrySize;
        putLe32(container.data() + at, offsets[index]);
        putLe32(container.data() + at + 4,
                static_cast<std::uint32_t>(lumps[index].payload.size()));
        putLe32(container.data() + at + 8,
                static_cast<std::uint32_t>(lumps[index].payload.size()));
        container[at + 12] = lumps[index].method;
    }

    for (std::uint32_t index = 0; index < count; ++index) {
        const std::size_t at = nameTableOffset + index * kNameEntrySize;
        const std::string name = lumps[index].name.substr(0, kNameSize);
        for (std::size_t byte = 0; byte < kNameSize; ++byte) {
            container[at + byte] = byte < name.size()
                                       ? static_cast<std::uint8_t>(name[byte]) : 0u;
        }
        putLe32(container.data() + at + 8, index);
    }

    for (std::uint32_t index = 0; index < count; ++index) {
        container.insert(container.end(), padding[index], 0u);
        container.insert(container.end(), lumps[index].payload.begin(),
                         lumps[index].payload.end());
    }
    return container;
}

struct ManifestSpec {
    std::string name;
    std::uint16_t width;
    std::uint16_t rows;
    std::uint8_t flags;
    std::uint8_t kind;
    std::uint32_t lumpIndex;
};

std::vector<std::uint8_t> buildManifest(const std::vector<ManifestSpec>& records) {
    std::vector<std::uint8_t> manifest(kBakedManifestHeaderSize, 0u);
    manifest[0] = 'P';
    manifest[1] = 'W';
    manifest[2] = 'B';
    manifest[3] = 'K';
    putLe16(manifest.data() + 4, kBakedManifestVersion);
    putLe16(manifest.data() + 6, static_cast<std::uint16_t>(kBakedManifestRecordSize));
    putLe32(manifest.data() + 8, static_cast<std::uint32_t>(records.size()));
    putLe32(manifest.data() + 12, 0u);

    for (const ManifestSpec& record : records) {
        std::vector<std::uint8_t> entry(kBakedManifestRecordSize, 0u);
        const std::string name = record.name.substr(0, kNameSize);
        for (std::size_t byte = 0; byte < kNameSize; ++byte) {
            entry[byte] = byte < name.size() ? static_cast<std::uint8_t>(name[byte]) : 0u;
        }
        putLe16(entry.data() + 8, record.width);
        putLe16(entry.data() + 10, record.rows);
        entry[12] = record.flags;
        entry[13] = record.kind;
        putLe32(entry.data() + 16, record.lumpIndex);
        manifest.insert(manifest.end(), entry.begin(), entry.end());
    }
    return manifest;
}

std::vector<std::uint8_t> pattern(std::size_t bytes, std::uint8_t seed) {
    std::vector<std::uint8_t> out(bytes);
    for (std::size_t index = 0; index < bytes; ++index) {
        out[index] = static_cast<std::uint8_t>((index * 7u + seed) & 0xFFu);
    }
    return out;
}

/// One container with a manifest at index 0 and three raw payloads after it.
struct Fixture {
    std::vector<std::uint8_t> container;
    whd_state_t state;
};

Fixture makeFixture(std::vector<ManifestSpec> records,
                    const std::vector<std::vector<std::uint8_t>>& payloads,
                    const std::vector<std::uint8_t>& methods) {
    const std::vector<std::uint8_t> manifest = buildManifest(records);
    std::vector<LumpSpec> lumps;
    lumps.push_back({kBakedManifestName, manifest, kMethodRaw});
    for (std::size_t index = 0; index < payloads.size(); ++index) {
        lumps.push_back({"PAYLOAD" + std::to_string(index), payloads[index], methods[index]});
    }
    Fixture fixture;
    fixture.container = buildContainer(lumps);
    const whd_error_t error =
        whd_open(&fixture.state, fixture.container.data(), fixture.container.size());
    TEST_ASSERT_EQUAL(static_cast<int>(WHD_ERR_OK), static_cast<int>(error));
    return fixture;
}

std::vector<std::vector<std::uint8_t>> standardPayloads(bool wallMask, std::size_t wallWidth,
                                                        std::size_t wallRows,
                                                        std::size_t wallBytes) {
    (void)wallWidth;
    (void)wallRows;
    return {
        pattern(wallBytes + (wallMask ? wallBytes : 0u), 1u),  // WALL  index 1
        pattern(R_FLAT_BYTES, 2u),                             // FLAT  index 2
        pattern(64u, 3u)                                       // SPR   index 3
    };
}

std::vector<ManifestSpec> standardRecords(std::size_t wallWidth, std::size_t wallRows,
                                          bool wallMask) {
    return {
        {"WALL", static_cast<std::uint16_t>(wallWidth),
         static_cast<std::uint16_t>(wallRows),
         static_cast<std::uint8_t>(wallMask ? kBakedFlagMask : 0u), kBakedKindWall, 1u},
        {"FLAT", 64u, 64u, 0u, kBakedKindFlat, 2u},
        {"SPRTA0", 0u, 0u, 0u, kBakedKindSprite, 3u},
    };
}

}  // namespace

void test_manifest_accepts_a_valid_payload() {
    const std::vector<std::uint8_t> manifest = buildManifest({
        {"WALL64", 64u, 128u, kBakedFlagMask, kBakedKindWall, 1u},
        {"SKY1", 256u, 128u, 0u, kBakedKindSky, 2u},
        {"FLOOR1", 64u, 64u, 0u, kBakedKindFlat, 3u},
    });
    BakedManifest parsed;
    TEST_ASSERT_EQUAL(static_cast<int>(BakedError::Ok),
                      static_cast<int>(bakedManifestParse(manifest.data(), manifest.size(),
                                                          100u, &parsed)));
    TEST_ASSERT_EQUAL_INT(3, parsed.count);
    TEST_ASSERT_EQUAL_STRING("WALL64", parsed.entries[0].name);
    TEST_ASSERT_EQUAL_UINT16(64u, parsed.entries[0].width);
    TEST_ASSERT_EQUAL_UINT16(128u, parsed.entries[0].rows);
    TEST_ASSERT_EQUAL_UINT8(kBakedFlagMask, parsed.entries[0].flags);
    TEST_ASSERT_EQUAL_UINT8(kBakedKindSky, parsed.entries[1].kind);
    TEST_ASSERT_EQUAL_UINT32(3u, parsed.entries[2].lumpIndex);
}

void test_manifest_rejects_a_truncated_payload() {
    std::vector<std::uint8_t> manifest = buildManifest({
        {"WALL64", 64u, 128u, 0u, kBakedKindWall, 1u},
        {"WALL32", 32u, 128u, 0u, kBakedKindWall, 2u},
    });
    manifest.resize(manifest.size() - 4u);
    BakedManifest parsed;
    TEST_ASSERT_EQUAL(static_cast<int>(BakedError::Truncated),
                      static_cast<int>(bakedManifestParse(manifest.data(), manifest.size(),
                                                          100u, &parsed)));
}

void test_manifest_rejects_wrong_magic() {
    std::vector<std::uint8_t> manifest = buildManifest({
        {"WALL64", 64u, 128u, 0u, kBakedKindWall, 1u},
    });
    manifest[0] = 'X';
    BakedManifest parsed;
    TEST_ASSERT_EQUAL(static_cast<int>(BakedError::BadMagic),
                      static_cast<int>(bakedManifestParse(manifest.data(), manifest.size(),
                                                          100u, &parsed)));
}

void test_manifest_rejects_a_duplicate_name() {
    const std::vector<std::uint8_t> manifest = buildManifest({
        {"WALL64", 64u, 128u, 0u, kBakedKindWall, 1u},
        {"WALL64", 64u, 128u, 0u, kBakedKindWall, 2u},
    });
    BakedManifest parsed;
    TEST_ASSERT_EQUAL(static_cast<int>(BakedError::DuplicateName),
                      static_cast<int>(bakedManifestParse(manifest.data(), manifest.size(),
                                                          100u, &parsed)));
}

void test_manifest_rejects_an_index_out_of_range() {
    const std::vector<std::uint8_t> manifest = buildManifest({
        {"WALL64", 64u, 128u, 0u, kBakedKindWall, 9u},
    });
    BakedManifest parsed;
    TEST_ASSERT_EQUAL(static_cast<int>(BakedError::IndexOutOfRange),
                      static_cast<int>(bakedManifestParse(manifest.data(), manifest.size(),
                                                          9u, &parsed)));
}

void test_manifest_rejects_an_unknown_kind() {
    const std::vector<std::uint8_t> manifest = buildManifest({
        {"WALL64", 64u, 128u, 0u, 9u, 1u},
    });
    BakedManifest parsed;
    TEST_ASSERT_EQUAL(static_cast<int>(BakedError::BadKind),
                      static_cast<int>(bakedManifestParse(manifest.data(), manifest.size(),
                                                          100u, &parsed)));
}

void test_wall_record_derives_width_mask_and_height_mask() {
    Fixture fixture = makeFixture(standardRecords(72u, 96u, true),
                                  standardPayloads(true, 72u, 96u, 72u * 128u),
                                  {kMethodRaw, kMethodRaw, kMethodRaw});
    BakedGraphics graphics;
    TEST_ASSERT_EQUAL(static_cast<int>(BakedError::Ok),
                      static_cast<int>(graphics.open(&fixture.state)));
    const r_walltex_t* wall = graphics.resolveWall(packName("WALL"));
    TEST_ASSERT_NOT_NULL(wall);
    TEST_ASSERT_EQUAL_INT16(72, wall->width);
    TEST_ASSERT_EQUAL_INT16(96, wall->height);
    TEST_ASSERT_EQUAL_INT16(63, wall->widthMask);
    TEST_ASSERT_EQUAL_INT16(127, wall->heightMask);
    TEST_ASSERT_NOT_NULL(wall->pixels);
    TEST_ASSERT_NOT_NULL(wall->mask);
    TEST_ASSERT_EQUAL_PTR(wall->pixels + 72u * 128u, wall->mask);
}

void test_wall_record_without_a_mask_has_no_mask_pointer() {
    Fixture fixture = makeFixture(standardRecords(32u, 128u, false),
                                  standardPayloads(false, 32u, 128u, 32u * 128u),
                                  {kMethodRaw, kMethodRaw, kMethodRaw});
    BakedGraphics graphics;
    TEST_ASSERT_EQUAL(static_cast<int>(BakedError::Ok),
                      static_cast<int>(graphics.open(&fixture.state)));
    const r_walltex_t* wall = graphics.resolveWall(packName("WALL"));
    TEST_ASSERT_NOT_NULL(wall);
    TEST_ASSERT_EQUAL_INT16(32, wall->width);
    TEST_ASSERT_EQUAL_INT16(127, wall->heightMask);
    TEST_ASSERT_NULL(wall->mask);
}

void test_flat_record_is_a_4096_byte_square() {
    Fixture fixture = makeFixture(standardRecords(64u, 128u, false),
                                  standardPayloads(false, 64u, 128u, 64u * 128u),
                                  {kMethodRaw, kMethodRaw, kMethodRaw});
    BakedGraphics graphics;
    TEST_ASSERT_EQUAL(static_cast<int>(BakedError::Ok),
                      static_cast<int>(graphics.open(&fixture.state)));
    const r_flat_t* flat = graphics.resolveFlat(packName("FLAT"));
    TEST_ASSERT_NOT_NULL(flat);
    TEST_ASSERT_NOT_NULL(flat->pixels);
    TEST_ASSERT_EQUAL_INT16(64, flat->width);
    TEST_ASSERT_EQUAL_INT16(64, flat->height);
    TEST_ASSERT_EQUAL_INT16(63, flat->widthMask);
    TEST_ASSERT_EQUAL_INT16(63, flat->heightMask);
    TEST_ASSERT_EQUAL_INT(1, graphics.count(BakedKind::kBakedKindFlat));
}

void test_sprite_record_points_at_the_raw_patch() {
    Fixture fixture = makeFixture(standardRecords(64u, 128u, false),
                                  standardPayloads(false, 64u, 128u, 64u * 128u),
                                  {kMethodRaw, kMethodRaw, kMethodRaw});
    BakedGraphics graphics;
    TEST_ASSERT_EQUAL(static_cast<int>(BakedError::Ok),
                      static_cast<int>(graphics.open(&fixture.state)));
    const r_sprite_patch_t* sprite = graphics.resolveSprite(packName("SPRTA0"));
    TEST_ASSERT_NOT_NULL(sprite);
    TEST_ASSERT_EQUAL_UINT32(64u, static_cast<std::uint32_t>(sprite->size));
    TEST_ASSERT_NOT_NULL(sprite->data);
}

void test_a_missing_name_resolves_to_null() {
    Fixture fixture = makeFixture(standardRecords(64u, 128u, false),
                                  standardPayloads(false, 64u, 128u, 64u * 128u),
                                  {kMethodRaw, kMethodRaw, kMethodRaw});
    BakedGraphics graphics;
    TEST_ASSERT_EQUAL(static_cast<int>(BakedError::Ok),
                      static_cast<int>(graphics.open(&fixture.state)));
    TEST_ASSERT_NULL(graphics.resolveWall(packName("NOPE")));
    TEST_ASSERT_NULL(graphics.resolveFlat(packName("NOPE")));
    TEST_ASSERT_NULL(graphics.resolveSprite(packName("NOPE")));
}

void test_a_stored_payload_is_refused_not_decoded() {
    Fixture fixture = makeFixture(standardRecords(16u, 128u, false),
                                  standardPayloads(false, 16u, 128u, 16u * 128u),
                                  {kMethodStored, kMethodRaw, kMethodRaw});
    BakedGraphics graphics;
    TEST_ASSERT_EQUAL(static_cast<int>(BakedError::NotRaw),
                      static_cast<int>(graphics.open(&fixture.state)));
}

void test_a_payload_size_disagreement_is_refused() {
    Fixture fixture = makeFixture(standardRecords(16u, 128u, false),
                                  {pattern(10u, 1u), pattern(R_FLAT_BYTES, 2u),
                                   pattern(64u, 3u)},
                                  {kMethodRaw, kMethodRaw, kMethodRaw});
    BakedGraphics graphics;
    TEST_ASSERT_EQUAL(static_cast<int>(BakedError::SizeMismatch),
                      static_cast<int>(graphics.open(&fixture.state)));
}

void test_a_container_without_a_manifest_is_refused() {
    std::vector<LumpSpec> lumps;
    lumps.push_back({"NOTMANIF", pattern(32u, 1u), kMethodRaw});
    const std::vector<std::uint8_t> container = buildContainer(lumps);
    whd_state_t state;
    TEST_ASSERT_EQUAL(static_cast<int>(WHD_ERR_OK),
                      static_cast<int>(whd_open(&state, container.data(), container.size())));
    BakedGraphics graphics;
    TEST_ASSERT_EQUAL(static_cast<int>(BakedError::ManifestMissing),
                      static_cast<int>(graphics.open(&state)));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    test_setup();
    UNITY_BEGIN();

    RUN_TEST(test_manifest_accepts_a_valid_payload);
    RUN_TEST(test_manifest_rejects_a_truncated_payload);
    RUN_TEST(test_manifest_rejects_wrong_magic);
    RUN_TEST(test_manifest_rejects_a_duplicate_name);
    RUN_TEST(test_manifest_rejects_an_index_out_of_range);
    RUN_TEST(test_manifest_rejects_an_unknown_kind);
    RUN_TEST(test_wall_record_derives_width_mask_and_height_mask);
    RUN_TEST(test_wall_record_without_a_mask_has_no_mask_pointer);
    RUN_TEST(test_flat_record_is_a_4096_byte_square);
    RUN_TEST(test_sprite_record_points_at_the_raw_patch);
    RUN_TEST(test_a_missing_name_resolves_to_null);
    RUN_TEST(test_a_stored_payload_is_refused_not_decoded);
    RUN_TEST(test_a_payload_size_disagreement_is_refused);
    RUN_TEST(test_a_container_without_a_manifest_is_refused);

    const int result = UNITY_END();
    test_teardown();
    return result;
}
