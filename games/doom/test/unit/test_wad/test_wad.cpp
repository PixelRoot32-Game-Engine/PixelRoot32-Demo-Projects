/*
 * Behaviour tests for the WAD directory reader (lib/doom/w_wad.c).
 *
 * Every WAD here is built in memory by this file. No binary fixture is
 * committed and no copyrighted data is used: the point of the synthetic images
 * is that each validation rule and each lookup rule can be stated exactly, at
 * the boundary it names, without a real doom1.wad being present.
 *
 * The tests go through the public C API only. They never read the state struct
 * to decide whether an open succeeded, so a reader that filled its fields but
 * rejected the file would still fail here.
 */
#include <cstddef>
#include <cstdint>
#include <vector>

#include <unity.h>

#include "w_wad.h"

#include "../../test_config.h"

namespace {

constexpr std::size_t kHeaderSize = 12u;
constexpr std::size_t kDirectoryEntrySize = 16u;
constexpr std::size_t kNameSize = 8u;

/// One lump in a synthetic WAD: its 8-byte name and the bytes it stores.
struct LumpSpec {
    const char* name;
    std::vector<std::uint8_t> bytes;
};

void putLe32(std::uint8_t* at, std::uint32_t value) {
    at[0] = static_cast<std::uint8_t>(value & 0xFFu);
    at[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    at[2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    at[3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

std::uint32_t le32At(const std::uint8_t* at) {
    return static_cast<std::uint32_t>(at[0])
         | (static_cast<std::uint32_t>(at[1]) << 8)
         | (static_cast<std::uint32_t>(at[2]) << 16)
         | (static_cast<std::uint32_t>(at[3]) << 24);
}

void putName(std::uint8_t* at, const char* name) {
    std::size_t index = 0;
    while (index < kNameSize && name[index] != '\0') {
        at[index] = static_cast<std::uint8_t>(name[index]);
        ++index;
    }
    while (index < kNameSize) {
        at[index] = 0u;
        ++index;
    }
}

/// Builds a well-formed WAD: header, lump bytes in declaration order, then the
/// directory at the end of the buffer. Tests that need a malformed image patch
/// one field of the returned vector afterwards.
std::vector<std::uint8_t> buildWad(const char* identification,
                                   const std::vector<LumpSpec>& lumps) {
    std::size_t dataSize = 0;
    for (const LumpSpec& lump : lumps) {
        dataSize += lump.bytes.size();
    }

    const std::size_t directoryOffset = kHeaderSize + dataSize;
    std::vector<std::uint8_t> wad(directoryOffset
                                      + lumps.size() * kDirectoryEntrySize,
                                  0u);

    for (std::size_t index = 0; index < 4; ++index) {
        wad[index] = static_cast<std::uint8_t>(identification[index]);
    }
    putLe32(&wad[4], static_cast<std::uint32_t>(lumps.size()));
    putLe32(&wad[8], static_cast<std::uint32_t>(directoryOffset));

    std::size_t cursor = kHeaderSize;
    std::size_t entryOffset = directoryOffset;
    for (const LumpSpec& lump : lumps) {
        putLe32(&wad[entryOffset], static_cast<std::uint32_t>(cursor));
        putLe32(&wad[entryOffset + 4], static_cast<std::uint32_t>(lump.bytes.size()));
        putName(&wad[entryOffset + 8], lump.name);
        for (const std::uint8_t byte : lump.bytes) {
            wad[cursor] = byte;
            ++cursor;
        }
        entryOffset += kDirectoryEntrySize;
    }

    return wad;
}

/// Address of directory entry @p index in an already built WAD.
std::uint8_t* directoryEntryAt(std::vector<std::uint8_t>& wad, std::size_t index) {
    return wad.data() + le32At(wad.data() + 8)
         + index * kDirectoryEntrySize;
}

/// A 32-byte WAD whose single lump occupies the last four bytes: the directory
/// sits directly after the header and the data ends exactly at the buffer end.
std::vector<std::uint8_t> buildTrailingLumpWad() {
    std::vector<std::uint8_t> wad(32u, 0u);

    wad[0] = static_cast<std::uint8_t>('I');
    wad[1] = static_cast<std::uint8_t>('W');
    wad[2] = static_cast<std::uint8_t>('A');
    wad[3] = static_cast<std::uint8_t>('D');
    putLe32(&wad[4], 1u);
    putLe32(&wad[8], static_cast<std::uint32_t>(kHeaderSize));
    putLe32(&wad[12], 28u);
    putLe32(&wad[16], 4u);
    putName(&wad[20], "END");
    wad[28] = 9u;
    wad[29] = 8u;
    wad[30] = 7u;
    wad[31] = 6u;

    return wad;
}

}  // namespace

// A minimal image: one lump directly after the header, directory at the end.
void test_minimal_valid_wad_opens_and_reads_lump(void) {
    const std::vector<std::uint8_t> wad =
        buildWad("IWAD", {{"LUMP", {0xDEu, 0xADu, 0xBEu, 0xEFu}}});
    wad_state_t state;

    TEST_ASSERT_EQUAL_INT(WAD_ERR_OK, wad_open(&state, wad.data(), wad.size()));
    TEST_ASSERT_EQUAL_INT(0, W_CheckNumForName(&state, "LUMP"));
    TEST_ASSERT_EQUAL_INT32(4, W_LumpLength(&state, 0));

    const std::uint8_t* at = W_LumpPointer(&state, 0);
    TEST_ASSERT_NOT_NULL(at);
    TEST_ASSERT_TRUE(at == wad.data() + kHeaderSize);
    TEST_ASSERT_EQUAL_UINT8(0xDEu, at[0]);
    TEST_ASSERT_EQUAL_UINT8(0xADu, at[1]);
    TEST_ASSERT_EQUAL_UINT8(0xBEu, at[2]);
    TEST_ASSERT_EQUAL_UINT8(0xEFu, at[3]);
}

// A patch file is as valid as an IWAD: only the first four bytes differ.
void test_pwad_identification_is_accepted(void) {
    const std::vector<std::uint8_t> wad = buildWad("PWAD", {{"PATCH", {1u, 2u, 3u}}});
    wad_state_t state;

    TEST_ASSERT_EQUAL_INT(WAD_ERR_OK, wad_open(&state, wad.data(), wad.size()));
    TEST_ASSERT_EQUAL_INT(0, W_CheckNumForName(&state, "PATCH"));
}

// Zero lumps is a legal WAD with an empty directory at the end of the header.
void test_empty_wad_is_valid(void) {
    const std::vector<std::uint8_t> wad = buildWad("IWAD", {});
    wad_state_t state;

    TEST_ASSERT_EQUAL_INT(WAD_ERR_OK, wad_open(&state, wad.data(), wad.size()));
    TEST_ASSERT_EQUAL_INT(-1, W_CheckNumForName(&state, "ANY"));
    TEST_ASSERT_EQUAL_INT32(0, W_LumpLength(&state, 0));
    TEST_ASSERT_NULL(W_LumpPointer(&state, 0));
}

// DOOM's rule: a directory is scanned from the first entry and stops on the
// first match. The later duplicate must not shadow the earlier one.
void test_duplicate_names_first_match_wins(void) {
    const std::vector<std::uint8_t> wad =
        buildWad("IWAD", {{"DUPLIC", {1u, 2u, 3u}},
                          {"DUPLIC", {4u, 5u, 6u, 7u, 8u}}});
    wad_state_t state;

    TEST_ASSERT_EQUAL_INT(WAD_ERR_OK, wad_open(&state, wad.data(), wad.size()));
    TEST_ASSERT_EQUAL_INT(0, W_CheckNumForName(&state, "DUPLIC"));
    TEST_ASSERT_EQUAL_INT32(3, W_LumpLength(&state, 0));
    TEST_ASSERT_EQUAL_INT32(5, W_LumpLength(&state, 1));
}

// The query is upper-cased, never the stored field: "map01" and "Map01" both
// reach a lump stored as "MAP01".
void test_name_lookup_is_case_insensitive(void) {
    const std::vector<std::uint8_t> wad =
        buildWad("IWAD", {{"MAP01", {1u}}, {"THINGS", {2u}}});
    wad_state_t state;

    TEST_ASSERT_EQUAL_INT(WAD_ERR_OK, wad_open(&state, wad.data(), wad.size()));
    TEST_ASSERT_EQUAL_INT(0, W_CheckNumForName(&state, "MAP01"));
    TEST_ASSERT_EQUAL_INT(0, W_CheckNumForName(&state, "map01"));
    TEST_ASSERT_EQUAL_INT(0, W_CheckNumForName(&state, "Map01"));
    TEST_ASSERT_EQUAL_INT(1, W_CheckNumForName(&state, "things"));
}

// No entry is not an error and not a partial match.
void test_missing_name_returns_minus_one(void) {
    const std::vector<std::uint8_t> wad = buildWad("IWAD", {{"MAP01", {1u}}});
    wad_state_t state;

    TEST_ASSERT_EQUAL_INT(WAD_ERR_OK, wad_open(&state, wad.data(), wad.size()));
    TEST_ASSERT_EQUAL_INT(-1, W_CheckNumForName(&state, "MAP02"));
    TEST_ASSERT_EQUAL_INT(-1, W_CheckNumForName(&state, "MAP0"));
    TEST_ASSERT_EQUAL_INT(-1, W_CheckNumForName(&state, ""));
}

// DOOM compares the fixed 8-byte field: a longer query matches only when its
// first 8 characters equal the field, which is what strncpy into name8 does in
// the original. A 7-character field is NUL padded, so an 8th query byte can
// never match it.
void test_name_lookup_compares_the_fixed_eight_byte_field(void) {
    const std::vector<std::uint8_t> wad =
        buildWad("IWAD", {{"PLAYPAL", {1u}}, {"ABCDEFGH", {2u}}});
    wad_state_t state;

    TEST_ASSERT_EQUAL_INT(WAD_ERR_OK, wad_open(&state, wad.data(), wad.size()));
    TEST_ASSERT_EQUAL_INT(1, W_CheckNumForName(&state, "ABCDEFGHX"));
    TEST_ASSERT_EQUAL_INT(1, W_CheckNumForName(&state, "abcdefghx"));
    TEST_ASSERT_EQUAL_INT(0, W_CheckNumForName(&state, "PLAYPAL"));
    TEST_ASSERT_EQUAL_INT(-1, W_CheckNumForName(&state, "PLAYPALX"));
}

// The boundary is inclusive: filepos + size == buffer size is inside.
void test_lump_at_end_of_buffer_is_accepted(void) {
    const std::vector<std::uint8_t> wad = buildTrailingLumpWad();
    wad_state_t state;

    TEST_ASSERT_EQUAL_INT(WAD_ERR_OK, wad_open(&state, wad.data(), wad.size()));
    TEST_ASSERT_EQUAL_INT32(4, W_LumpLength(&state, 0));

    const std::uint8_t* at = W_LumpPointer(&state, 0);
    TEST_ASSERT_NOT_NULL(at);
    TEST_ASSERT_TRUE(at == wad.data() + wad.size() - 4u);
    TEST_ASSERT_EQUAL_UINT8(9u, at[0]);
    TEST_ASSERT_EQUAL_UINT8(6u, at[3]);
}

// One byte past the end is rejected, and the rejection is its own code.
void test_filepos_plus_size_past_end_is_rejected(void) {
    std::vector<std::uint8_t> wad = buildTrailingLumpWad();
    putLe32(directoryEntryAt(wad, 0) + 4, 5u);
    wad_state_t state;

    TEST_ASSERT_EQUAL_INT(WAD_ERR_LUMP_RANGE,
                          wad_open(&state, wad.data(), wad.size()));
}

void test_negative_filepos_is_rejected(void) {
    std::vector<std::uint8_t> wad = buildWad("IWAD", {{"LUMP", {1u, 2u}}});
    putLe32(directoryEntryAt(wad, 0), 0xFFFFFFFFu);
    wad_state_t state;

    TEST_ASSERT_EQUAL_INT(WAD_ERR_LUMP_RANGE,
                          wad_open(&state, wad.data(), wad.size()));
}

void test_negative_size_is_rejected(void) {
    std::vector<std::uint8_t> wad = buildWad("IWAD", {{"LUMP", {1u, 2u}}});
    putLe32(directoryEntryAt(wad, 0) + 4, 0xFFFFFFFFu);
    wad_state_t state;

    TEST_ASSERT_EQUAL_INT(WAD_ERR_LUMP_RANGE,
                          wad_open(&state, wad.data(), wad.size()));
}

// infotableofs must be inside the buffer, and the directory it points at must
// have room for every entry.
void test_infotableofs_past_end_is_rejected(void) {
    std::vector<std::uint8_t> wad = buildWad("IWAD", {});
    putLe32(&wad[8], static_cast<std::uint32_t>(wad.size() + 1u));
    wad_state_t state;

    TEST_ASSERT_EQUAL_INT(WAD_ERR_INFOTABLE_RANGE,
                          wad_open(&state, wad.data(), wad.size()));
}

void test_directory_with_no_room_for_entries_is_rejected(void) {
    std::vector<std::uint8_t> wad = buildWad("IWAD", {{"LUMP", {1u}}});
    // Leave exactly one byte after the directory start: not one 16-byte entry.
    putLe32(&wad[8], static_cast<std::uint32_t>(wad.size() - 1u));
    wad_state_t state;

    TEST_ASSERT_EQUAL_INT(WAD_ERR_INFOTABLE_RANGE,
                          wad_open(&state, wad.data(), wad.size()));
}

// A directory that starts inside the 12-byte header overlaps it.
void test_directory_overlapping_header_is_rejected(void) {
    std::vector<std::uint8_t> wad = buildWad("IWAD", {});
    putLe32(&wad[8], 8u);
    wad_state_t state;

    TEST_ASSERT_EQUAL_INT(WAD_ERR_INFOTABLE_OVERLAP,
                          wad_open(&state, wad.data(), wad.size()));
}

void test_bad_identification_is_rejected(void) {
    const std::vector<std::uint8_t> wad = buildWad("XWAD", {{"LUMP", {1u}}});
    wad_state_t state;

    TEST_ASSERT_EQUAL_INT(WAD_ERR_BAD_IDENTIFICATION,
                          wad_open(&state, wad.data(), wad.size()));
}

// A count that cannot fit the buffer is a count failure, not an index failure.
void test_huge_numlumps_is_rejected(void) {
    std::vector<std::uint8_t> wad = buildWad("IWAD", {});
    putLe32(&wad[4], 0x7FFFFFFFu);
    wad_state_t state;

    TEST_ASSERT_EQUAL_INT(WAD_ERR_NUMLUMPS,
                          wad_open(&state, wad.data(), wad.size()));
}

void test_negative_numlumps_is_rejected(void) {
    std::vector<std::uint8_t> wad = buildWad("IWAD", {});
    putLe32(&wad[4], 0xFFFFFFFFu);
    wad_state_t state;

    TEST_ASSERT_EQUAL_INT(WAD_ERR_NUMLUMPS,
                          wad_open(&state, wad.data(), wad.size()));
}

void test_too_small_buffer_is_rejected(void) {
    const std::uint8_t bytes[8] = {'I', 'W', 'A', 'D', 0u, 0u, 0u, 0u};
    wad_state_t state;

    TEST_ASSERT_EQUAL_INT(WAD_ERR_TOO_SMALL, wad_open(&state, bytes, sizeof bytes));
}

void test_null_arguments_are_rejected(void) {
    const std::vector<std::uint8_t> wad = buildWad("IWAD", {{"LUMP", {1u}}});
    wad_state_t state;

    TEST_ASSERT_EQUAL_INT(WAD_ERR_NULL_STATE,
                          wad_open(nullptr, wad.data(), wad.size()));
    TEST_ASSERT_EQUAL_INT(WAD_ERR_NULL_BUFFER,
                          wad_open(&state, nullptr, wad.size()));
    TEST_ASSERT_EQUAL_INT(-1, W_CheckNumForName(nullptr, "LUMP"));
    TEST_ASSERT_EQUAL_INT32(0, W_LumpLength(nullptr, 0));
    TEST_ASSERT_NULL(W_LumpPointer(nullptr, 0));
}

// The format addresses with signed 32-bit offsets, so a larger buffer cannot be
// described at all. The size check runs before the first byte is read.
void test_size_beyond_int32_is_rejected(void) {
    const std::vector<std::uint8_t> wad = buildWad("IWAD", {{"LUMP", {1u}}});
    wad_state_t state;

    TEST_ASSERT_EQUAL_INT(
        WAD_ERR_SIZE_RANGE,
        wad_open(&state, wad.data(),
                 static_cast<std::size_t>(WAD_MAX_BUFFER_SIZE) + 1u));
}

// Out-of-range indices answer the documented neutral values, on a reader that
// is open and has real lumps.
void test_invalid_lump_index_answers_empty(void) {
    const std::vector<std::uint8_t> wad =
        buildWad("IWAD", {{"ONE", {1u}}, {"TWO", {2u, 3u}}});
    wad_state_t state;

    TEST_ASSERT_EQUAL_INT(WAD_ERR_OK, wad_open(&state, wad.data(), wad.size()));
    TEST_ASSERT_EQUAL_INT32(0, W_LumpLength(&state, -1));
    TEST_ASSERT_EQUAL_INT32(0, W_LumpLength(&state, 2));
    TEST_ASSERT_NULL(W_LumpPointer(&state, -1));
    TEST_ASSERT_NULL(W_LumpPointer(&state, 2));
    TEST_ASSERT_EQUAL_INT(-1, W_CheckNumForName(&state, nullptr));
}

// A rejected open must not leave the previous reader half usable.
void test_failed_open_clears_previous_reader(void) {
    const std::vector<std::uint8_t> good =
        buildWad("IWAD", {{"LUMP", {1u, 2u, 3u}}});
    std::vector<std::uint8_t> bad = buildTrailingLumpWad();
    putLe32(directoryEntryAt(bad, 0) + 4, 5u);

    wad_state_t state;
    TEST_ASSERT_EQUAL_INT(WAD_ERR_OK, wad_open(&state, good.data(), good.size()));
    TEST_ASSERT_EQUAL_INT(0, W_CheckNumForName(&state, "LUMP"));

    TEST_ASSERT_EQUAL_INT(WAD_ERR_LUMP_RANGE,
                          wad_open(&state, bad.data(), bad.size()));
    TEST_ASSERT_EQUAL_INT(-1, W_CheckNumForName(&state, "LUMP"));
    TEST_ASSERT_EQUAL_INT32(0, W_LumpLength(&state, 0));
    TEST_ASSERT_NULL(W_LumpPointer(&state, 0));

    // And a good file opens again afterwards: a rejection is not sticky.
    TEST_ASSERT_EQUAL_INT(WAD_ERR_OK, wad_open(&state, good.data(), good.size()));
    TEST_ASSERT_EQUAL_INT(0, W_CheckNumForName(&state, "LUMP"));
}

// Walk a whole synthetic directory and compare the reported sizes and bytes
// against the bytes that were actually stored, including the empty lump.
void test_directory_walk_reports_exact_bytes(void) {
    const std::vector<LumpSpec> lumps = {
        {"FIRST", {1u, 2u, 3u, 4u}},
        {"EMPTY", {}},
        {"THIRD", {5u, 6u, 7u, 8u, 9u}},
    };
    const std::vector<std::uint8_t> wad = buildWad("IWAD", lumps);
    wad_state_t state;

    TEST_ASSERT_EQUAL_INT(WAD_ERR_OK, wad_open(&state, wad.data(), wad.size()));

    for (std::size_t index = 0; index < lumps.size(); ++index) {
        TEST_ASSERT_EQUAL_INT32(static_cast<std::int32_t>(lumps[index].bytes.size()),
                                W_LumpLength(&state, static_cast<int>(index)));

        const std::uint8_t* at = W_LumpPointer(&state, static_cast<int>(index));
        TEST_ASSERT_NOT_NULL(at);
        for (std::size_t byte = 0; byte < lumps[index].bytes.size(); ++byte) {
            TEST_ASSERT_EQUAL_UINT8(lumps[index].bytes[byte], at[byte]);
        }
    }

    TEST_ASSERT_EQUAL_INT32(0, W_LumpLength(&state, 3));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    test_setup();
    UNITY_BEGIN();

    RUN_TEST(test_minimal_valid_wad_opens_and_reads_lump);
    RUN_TEST(test_pwad_identification_is_accepted);
    RUN_TEST(test_empty_wad_is_valid);
    RUN_TEST(test_duplicate_names_first_match_wins);
    RUN_TEST(test_name_lookup_is_case_insensitive);
    RUN_TEST(test_missing_name_returns_minus_one);
    RUN_TEST(test_name_lookup_compares_the_fixed_eight_byte_field);
    RUN_TEST(test_lump_at_end_of_buffer_is_accepted);
    RUN_TEST(test_filepos_plus_size_past_end_is_rejected);
    RUN_TEST(test_negative_filepos_is_rejected);
    RUN_TEST(test_negative_size_is_rejected);
    RUN_TEST(test_infotableofs_past_end_is_rejected);
    RUN_TEST(test_directory_with_no_room_for_entries_is_rejected);
    RUN_TEST(test_directory_overlapping_header_is_rejected);
    RUN_TEST(test_bad_identification_is_rejected);
    RUN_TEST(test_huge_numlumps_is_rejected);
    RUN_TEST(test_negative_numlumps_is_rejected);
    RUN_TEST(test_too_small_buffer_is_rejected);
    RUN_TEST(test_null_arguments_are_rejected);
    RUN_TEST(test_size_beyond_int32_is_rejected);
    RUN_TEST(test_invalid_lump_index_answers_empty);
    RUN_TEST(test_failed_open_clears_previous_reader);
    RUN_TEST(test_directory_walk_reports_exact_bytes);

    const int result = UNITY_END();
    test_teardown();
    return result;
}
