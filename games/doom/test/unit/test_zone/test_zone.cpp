/*
 * Behaviour tests for the zone allocator (lib/doom/z_zone.c).
 *
 * The arena is a local array in every test, so each case starts from a buffer
 * whose bytes the test wrote (all zero) and no case depends on another. The
 * tests go through the public C API only: they read z_stats(), never the state
 * struct, so an allocator that filled its fields but reported the wrong numbers
 * would still fail here.
 *
 * The arithmetic is written with ZONE_HEADER_SIZE and ZONE_ALIGNMENT rather
 * than the literal 16 and 8, and every expected number is derived from the
 * layout the header documents:
 *
 *   inUse + freeBytes + overhead == arenaSize
 *
 * with each split costing one header (free loses the request plus the new
 * header) and each free merging a header back into payload capacity.
 */
#include <cstddef>
#include <cstdint>

#include <unity.h>

#include "z_zone.h"

#include "../../test_config.h"

namespace {

using u8 = std::uint8_t;
using u32 = std::uint32_t;

constexpr u32 kHeader = ZONE_HEADER_SIZE;
constexpr u32 kAlign = ZONE_ALIGNMENT;
constexpr u32 kMinArena = ZONE_MIN_ARENA;

/// A request's rounded payload capacity, as the allocator charges it.
u32 rounded(u32 size) {
    return (size + kAlign - 1u) & ~(kAlign - 1u);
}

void assertAligned(const void* pointer) {
    TEST_ASSERT_NOT_NULL(pointer);
    TEST_ASSERT_EQUAL_UINT32(0u,
        static_cast<u32>(reinterpret_cast<std::uintptr_t>(pointer) % kAlign));
}

/// The three accounting fields, their identity, and the arena size.
void assertAccounting(u32 arenaSize, const zone_stats_t& stats, u32 inUse,
                      u32 freeBytes, u32 overhead) {
    TEST_ASSERT_EQUAL_UINT32(arenaSize, stats.arenaSize);
    TEST_ASSERT_EQUAL_UINT32(inUse, stats.inUse);
    TEST_ASSERT_EQUAL_UINT32(freeBytes, stats.freeBytes);
    TEST_ASSERT_EQUAL_UINT32(overhead, stats.overhead);
    TEST_ASSERT_EQUAL_UINT32(arenaSize,
        stats.inUse + stats.freeBytes + stats.overhead);
    TEST_ASSERT_TRUE(stats.largestFree <= stats.freeBytes);
    TEST_ASSERT_TRUE(stats.peakInUse >= stats.inUse);
}

/// Every field, for the misuse cases that must change nothing at all.
void assertStatsEqual(const zone_stats_t& expected, const zone_stats_t& actual) {
    TEST_ASSERT_EQUAL_UINT32(expected.arenaSize, actual.arenaSize);
    TEST_ASSERT_EQUAL_UINT32(expected.inUse, actual.inUse);
    TEST_ASSERT_EQUAL_UINT32(expected.freeBytes, actual.freeBytes);
    TEST_ASSERT_EQUAL_UINT32(expected.overhead, actual.overhead);
    TEST_ASSERT_EQUAL_UINT32(expected.largestFree, actual.largestFree);
    TEST_ASSERT_EQUAL_UINT32(expected.peakInUse, actual.peakInUse);
    TEST_ASSERT_EQUAL_UINT32(expected.failures, actual.failures);
    TEST_ASSERT_EQUAL_UINT32(expected.purges, actual.purges);
}

// --- Initialisation ---------------------------------------------------------

void test_init_accepts_aligned_arena(void) {
    alignas(kAlign) std::uint8_t arena[256] = {};
    zone_state_t zone;

    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, arena, sizeof arena));

    const zone_stats_t stats = z_stats(&zone);
    assertAccounting(sizeof arena, stats, 0u, 256u - kHeader, kHeader);
    TEST_ASSERT_EQUAL_UINT32(256u - kHeader, stats.largestFree);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.peakInUse);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.failures);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.purges);
    TEST_ASSERT_EQUAL_UINT32(256u - kHeader, z_largest_free(&zone));
    TEST_ASSERT_EQUAL_UINT32(0u, z_largest_free(nullptr));
}

void test_init_rejects_invalid_arena(void) {
    alignas(kAlign) std::uint8_t arena[256] = {};
    zone_state_t zone;

    TEST_ASSERT_EQUAL_INT(ZONE_ERR_NULL_STATE, z_init(nullptr, arena, sizeof arena));

    TEST_ASSERT_EQUAL_INT(ZONE_ERR_NULL_BUFFER, z_init(&zone, nullptr, sizeof arena));
    TEST_ASSERT_EQUAL_UINT32(0u, z_stats(&zone).arenaSize);

    // One byte off the alignment is a misaligned arena, not a small one.
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_BAD_ARENA, z_init(&zone, arena + 1, sizeof arena - 1));
    TEST_ASSERT_EQUAL_UINT32(0u, z_stats(&zone).arenaSize);

    // One byte short of a header plus one minimum payload.
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_BAD_ARENA, z_init(&zone, arena, kMinArena - 1u));
    // The minimum is exactly accepted and exposes one minimum payload.
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, arena, kMinArena));
    assertAccounting(kMinArena, z_stats(&zone), 0u, kAlign, kHeader);

#if SIZE_MAX > UINT32_MAX
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_SIZE_RANGE,
                          z_init(&zone, arena, static_cast<std::size_t>(UINT32_MAX) + 1u));
    TEST_ASSERT_EQUAL_UINT32(0u, z_stats(&zone).arenaSize);
#endif
}

// --- Allocation -------------------------------------------------------------

void test_alloc_rejects_invalid_arguments(void) {
    alignas(kAlign) std::uint8_t arena[256] = {};
    zone_state_t zone;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, arena, sizeof arena));

    const zone_stats_t before = z_stats(&zone);
    zone_error_t error = ZONE_ERR_OK;

    TEST_ASSERT_NULL(z_alloc(nullptr, 8u, PU_STATIC, &error));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_NULL_STATE, error);

    TEST_ASSERT_NULL(z_alloc(&zone, 0u, PU_STATIC, &error));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_SIZE_ZERO, error);

    TEST_ASSERT_NULL(z_alloc(&zone, 8u, static_cast<zone_tag_t>(3), &error));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_BAD_TAG, error);

    TEST_ASSERT_NULL(z_alloc(&zone, static_cast<std::size_t>(ZONE_MAX_ALLOC) + 1u,
                             PU_STATIC, &error));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_SIZE_RANGE, error);

    // A NULL error out-parameter is allowed.
    TEST_ASSERT_NULL(z_alloc(&zone, 0u, PU_STATIC, nullptr));

    assertStatsEqual(before, z_stats(&zone));

    zone_state_t unopened = {};
    TEST_ASSERT_NULL(z_alloc(&unopened, 8u, PU_STATIC, &error));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_NOT_OPEN, error);
}

void test_alloc_rounds_up_and_aligns_every_pointer(void) {
    alignas(kAlign) std::uint8_t arena[2048] = {};
    zone_state_t zone;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, arena, sizeof arena));

    u32 expectedInUse = 0u;
    for (u32 size = 1u; size <= 33u; ++size) {
        zone_error_t error = ZONE_ERR_OK;
        void* pointer = z_alloc(&zone, size, PU_LEVEL, &error);
        TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, error);
        assertAligned(pointer);
        expectedInUse += rounded(size);

        // Covers the byte the header would occupy if it were exposed: the
        // first payload byte is arena[0] + kHeader, and the header is behind it.
        TEST_ASSERT_EQUAL_UINT32(0u, *static_cast<std::uint8_t*>(pointer));
    }

    const zone_stats_t stats = z_stats(&zone);
    TEST_ASSERT_EQUAL_UINT32(expectedInUse, stats.inUse);
    // One header for the initial block plus one per split; every allocation
    // above splits because the arena is much larger than the accumulated set.
    TEST_ASSERT_EQUAL_UINT32(kHeader + 33u * kHeader, stats.overhead);
    assertAccounting(sizeof arena, stats, expectedInUse,
                     sizeof arena - expectedInUse - (kHeader + 33u * kHeader),
                     kHeader + 33u * kHeader);
    TEST_ASSERT_EQUAL_UINT32(expectedInUse, stats.peakInUse);
}

void test_free_and_realloc_restore_accounting(void) {
    alignas(kAlign) std::uint8_t arena[256] = {};
    zone_state_t zone;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, arena, sizeof arena));

    const zone_stats_t fresh = z_stats(&zone);
    zone_error_t error = ZONE_ERR_OK;

    void* first = z_alloc(&zone, 40u, PU_STATIC, &error);
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, error);
    TEST_ASSERT_EQUAL_UINT32(40u, z_stats(&zone).inUse);
    TEST_ASSERT_EQUAL_UINT32(40u, z_stats(&zone).peakInUse);

    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_free(&zone, first));

    const zone_stats_t afterFree = z_stats(&zone);
    TEST_ASSERT_EQUAL_UINT32(fresh.inUse, afterFree.inUse);
    TEST_ASSERT_EQUAL_UINT32(fresh.freeBytes, afterFree.freeBytes);
    TEST_ASSERT_EQUAL_UINT32(fresh.overhead, afterFree.overhead);
    TEST_ASSERT_EQUAL_UINT32(fresh.largestFree, afterFree.largestFree);
    TEST_ASSERT_EQUAL_UINT32(40u, afterFree.peakInUse);

    // The freed block is the first fit again, so the same address is reused.
    void* second = z_alloc(&zone, 40u, PU_CACHE, &error);
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, error);
    TEST_ASSERT_EQUAL_PTR(first, second);
}

void test_adjacent_blocks_do_not_overlap(void) {
    alignas(kAlign) std::uint8_t arena[256] = {};
    zone_state_t zone;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, arena, sizeof arena));

    zone_error_t error = ZONE_ERR_OK;
    auto* first = static_cast<std::uint8_t*>(z_alloc(&zone, 16u, PU_STATIC, &error));
    auto* second = static_cast<std::uint8_t*>(z_alloc(&zone, 16u, PU_STATIC, &error));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, error);
    assertAligned(first);
    assertAligned(second);
    TEST_ASSERT_TRUE(first != second);
    // The first payload starts after the leading block header: the header is
    // never part of the bytes the caller may write.
    TEST_ASSERT_TRUE(first >= arena + kHeader);
    TEST_ASSERT_TRUE(first + 16u <= second);

    for (u32 index = 0; index < 16u; ++index) {
        first[index] = 0xAAu;
    }
    for (u32 index = 0; index < 16u; ++index) {
        TEST_ASSERT_EQUAL_UINT8(0u, second[index]);
    }

    for (u32 index = 0; index < 16u; ++index) {
        second[index] = 0x55u;
    }
    for (u32 index = 0; index < 16u; ++index) {
        TEST_ASSERT_EQUAL_UINT8(0xAAu, first[index]);
    }
}

// --- Coalescing -------------------------------------------------------------

void test_free_coalesces_three_adjacent_blocks(void) {
    // Three blocks of 16 fill the arena exactly: 3 * (header + payload) = 96,
    // so there is no trailing free block to hide a failure to merge.
    alignas(kAlign) std::uint8_t arena[96] = {};
    zone_state_t zone;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, arena, sizeof arena));

    zone_error_t error = ZONE_ERR_OK;
    void* first = z_alloc(&zone, 16u, PU_STATIC, &error);
    void* middle = z_alloc(&zone, 16u, PU_STATIC, &error);
    void* last = z_alloc(&zone, 16u, PU_STATIC, &error);
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, error);
    TEST_ASSERT_EQUAL_UINT32(0u, z_stats(&zone).freeBytes);
    assertAccounting(sizeof arena, z_stats(&zone), 48u, 0u, 3u * kHeader);

    // The middle free has no free neighbour yet.
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_free(&zone, middle));
    assertAccounting(sizeof arena, z_stats(&zone), 32u, 16u, 3u * kHeader);
    TEST_ASSERT_EQUAL_UINT32(16u, z_largest_free(&zone));

    // Freeing the first neighbour merges first + middle.
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_free(&zone, first));
    assertAccounting(sizeof arena, z_stats(&zone), 16u, 48u, 2u * kHeader);
    TEST_ASSERT_EQUAL_UINT32(48u, z_largest_free(&zone));

    // Freeing the last merges the whole arena back into one block.
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_free(&zone, last));
    assertAccounting(sizeof arena, z_stats(&zone), 0u, 96u - kHeader, kHeader);
    TEST_ASSERT_EQUAL_UINT32(96u - kHeader, z_largest_free(&zone));
}

void test_allocation_fits_only_after_coalescing(void) {
    // Four 16-byte blocks and one 16-byte hole between them.
    alignas(kAlign) std::uint8_t arena[160] = {};
    zone_state_t zone;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, arena, sizeof arena));

    zone_error_t error = ZONE_ERR_OK;
    void* a = z_alloc(&zone, 16u, PU_STATIC, &error);
    void* b = z_alloc(&zone, 16u, PU_STATIC, &error);
    void* c = z_alloc(&zone, 16u, PU_STATIC, &error);
    void* d = z_alloc(&zone, 16u, PU_STATIC, &error);
    (void)d;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, error);
    TEST_ASSERT_EQUAL_UINT32(16u, z_stats(&zone).freeBytes);

    // Two isolated 16-byte holes cannot satisfy 40 bytes between them.
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_free(&zone, a));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_free(&zone, c));
    TEST_ASSERT_EQUAL_UINT32(48u, z_stats(&zone).freeBytes);
    TEST_ASSERT_EQUAL_UINT32(16u, z_stats(&zone).largestFree);
    TEST_ASSERT_NULL(z_alloc(&zone, 40u, PU_LEVEL, &error));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_NO_BLOCK, error);

    // Freeing the block between the two holes turns them into one 80-byte
    // block, and the same request now succeeds.
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_free(&zone, b));
    TEST_ASSERT_EQUAL_UINT32(80u, z_stats(&zone).largestFree);

    void* fit = z_alloc(&zone, 40u, PU_LEVEL, &error);
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, error);
    assertAligned(fit);
    TEST_ASSERT_EQUAL_UINT32(56u, z_stats(&zone).inUse);
    // The 40-byte block split the 80-byte hole; the untouched 16-byte hole at
    // the end of the arena is still there, so 24 + 16 bytes are free.
    TEST_ASSERT_EQUAL_UINT32(40u, z_stats(&zone).freeBytes);
    TEST_ASSERT_EQUAL_UINT32(24u, z_stats(&zone).largestFree);
    TEST_ASSERT_EQUAL_UINT32(4u * kHeader, z_stats(&zone).overhead);
    // The failed 40-byte attempt earlier in this test is the one recorded.
    TEST_ASSERT_EQUAL_UINT32(1u, z_stats(&zone).failures);
}

// --- Tags and purges --------------------------------------------------------

void test_level_purge_leaves_statics_intact(void) {
    alignas(kAlign) std::uint8_t arena[320] = {};
    zone_state_t zone;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, arena, sizeof arena));

    zone_error_t error = ZONE_ERR_OK;
    auto* stat = static_cast<std::uint8_t*>(z_alloc(&zone, 16u, PU_STATIC, &error));
    auto* level = static_cast<std::uint8_t*>(z_alloc(&zone, 24u, PU_LEVEL, &error));
    auto* cache = static_cast<std::uint8_t*>(z_alloc(&zone, 8u, PU_CACHE, &error));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, error);
    for (u32 index = 0; index < 16u; ++index) stat[index] = 0x11u;
    for (u32 index = 0; index < 24u; ++index) level[index] = 0x22u;
    for (u32 index = 0; index < 8u; ++index) cache[index] = 0x33u;

    u32 freed = 0u;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_free_tag(&zone, PU_LEVEL, &freed));
    TEST_ASSERT_EQUAL_UINT32(24u, freed);
    TEST_ASSERT_EQUAL_UINT32(24u, z_stats(&zone).inUse);

    // Statics and cache are alive and byte-identical.
    for (u32 index = 0; index < 16u; ++index) TEST_ASSERT_EQUAL_UINT8(0x11u, stat[index]);
    for (u32 index = 0; index < 8u; ++index) TEST_ASSERT_EQUAL_UINT8(0x33u, cache[index]);

    // A tag with no live blocks is a successful no-op.
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_free_tag(&zone, PU_LEVEL, &freed));
    TEST_ASSERT_EQUAL_UINT32(0u, freed);
    TEST_ASSERT_EQUAL_UINT32(24u, z_stats(&zone).inUse);
}

void test_cache_purge_leaves_statics_and_level_blocks_intact(void) {
    alignas(kAlign) std::uint8_t arena[320] = {};
    zone_state_t zone;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, arena, sizeof arena));

    zone_error_t error = ZONE_ERR_OK;
    auto* stat = static_cast<std::uint8_t*>(z_alloc(&zone, 16u, PU_STATIC, &error));
    auto* level = static_cast<std::uint8_t*>(z_alloc(&zone, 16u, PU_LEVEL, &error));
    auto* cache = static_cast<std::uint8_t*>(z_alloc(&zone, 16u, PU_CACHE, &error));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, error);
    for (u32 index = 0; index < 16u; ++index) stat[index] = 0x44u;
    for (u32 index = 0; index < 16u; ++index) level[index] = 0x55u;
    for (u32 index = 0; index < 16u; ++index) cache[index] = 0x66u;

    u32 freed = 0u;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_purge(&zone, &freed));
    TEST_ASSERT_EQUAL_UINT32(16u, freed);
    TEST_ASSERT_EQUAL_UINT32(32u, z_stats(&zone).inUse);
    TEST_ASSERT_EQUAL_UINT32(1u, z_stats(&zone).purges);

    for (u32 index = 0; index < 16u; ++index) TEST_ASSERT_EQUAL_UINT8(0x44u, stat[index]);
    for (u32 index = 0; index < 16u; ++index) TEST_ASSERT_EQUAL_UINT8(0x55u, level[index]);

    // The cache block itself is free, and a second purge finds nothing.
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_purge(&zone, &freed));
    TEST_ASSERT_EQUAL_UINT32(0u, freed);
    TEST_ASSERT_EQUAL_UINT32(2u, z_stats(&zone).purges);
}

void test_purge_reports_recovered_bytes(void) {
    alignas(kAlign) std::uint8_t arena[512] = {};
    zone_state_t zone;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, arena, sizeof arena));

    zone_error_t error = ZONE_ERR_OK;
    TEST_ASSERT_NOT_NULL(z_alloc(&zone, 24u, PU_CACHE, &error));
    TEST_ASSERT_NOT_NULL(z_alloc(&zone, 16u, PU_CACHE, &error));
    TEST_ASSERT_NOT_NULL(z_alloc(&zone, 8u, PU_STATIC, &error));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, error);
    TEST_ASSERT_EQUAL_UINT32(48u, z_stats(&zone).inUse);

    u32 freed = 0u;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_purge(&zone, &freed));
    TEST_ASSERT_EQUAL_UINT32(24u + 16u, freed);
    TEST_ASSERT_EQUAL_UINT32(8u, z_stats(&zone).inUse);
    // The two cache blocks were separated by the static, so the recovered
    // bytes land in two holes: 56 around the static plus the untouched 400.
    TEST_ASSERT_EQUAL_UINT32(456u, z_stats(&zone).freeBytes);
    TEST_ASSERT_EQUAL_UINT32(512u - 8u - 3u * kHeader, z_stats(&zone).freeBytes);
    // Reported fragmentation, not hidden: no single hole is the whole free set.
    TEST_ASSERT_EQUAL_UINT32(400u, z_stats(&zone).largestFree);
    TEST_ASSERT_TRUE(z_stats(&zone).largestFree < z_stats(&zone).freeBytes);
}

void test_alloc_purging_succeeds_when_purge_frees_enough(void) {
    // Same three-block layout as the coalescing test, one block cache-tagged.
    alignas(kAlign) std::uint8_t arena[96] = {};
    zone_state_t zone;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, arena, sizeof arena));

    zone_error_t error = ZONE_ERR_OK;
    void* cache = z_alloc(&zone, 16u, PU_CACHE, &error);
    TEST_ASSERT_NOT_NULL(z_alloc(&zone, 16u, PU_STATIC, &error));
    TEST_ASSERT_NOT_NULL(z_alloc(&zone, 16u, PU_STATIC, &error));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, error);
    TEST_ASSERT_EQUAL_UINT32(0u, z_stats(&zone).freeBytes);

    TEST_ASSERT_NULL(z_alloc(&zone, 16u, PU_LEVEL, &error));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_NO_BLOCK, error);
    TEST_ASSERT_EQUAL_UINT32(1u, z_stats(&zone).failures);

    void* level = z_alloc_purging(&zone, 16u, PU_LEVEL, &error);
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, error);
    TEST_ASSERT_EQUAL_PTR(cache, level);
    TEST_ASSERT_EQUAL_UINT32(1u, z_stats(&zone).failures);
    TEST_ASSERT_EQUAL_UINT32(1u, z_stats(&zone).purges);
    TEST_ASSERT_EQUAL_UINT32(48u, z_stats(&zone).inUse);
}

void test_alloc_purging_fails_when_purge_frees_nothing(void) {
    alignas(kAlign) std::uint8_t arena[96] = {};
    zone_state_t zone;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, arena, sizeof arena));

    zone_error_t error = ZONE_ERR_OK;
    TEST_ASSERT_NOT_NULL(z_alloc(&zone, 16u, PU_STATIC, &error));
    TEST_ASSERT_NOT_NULL(z_alloc(&zone, 16u, PU_STATIC, &error));
    TEST_ASSERT_NOT_NULL(z_alloc(&zone, 16u, PU_STATIC, &error));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, error);
    const zone_stats_t before = z_stats(&zone);

    TEST_ASSERT_NULL(z_alloc_purging(&zone, 16u, PU_LEVEL, &error));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_NO_BLOCK, error);

    const zone_stats_t after = z_stats(&zone);
    TEST_ASSERT_EQUAL_UINT32(before.inUse, after.inUse);
    TEST_ASSERT_EQUAL_UINT32(before.freeBytes, after.freeBytes);
    TEST_ASSERT_EQUAL_UINT32(before.overhead, after.overhead);
    TEST_ASSERT_EQUAL_UINT32(before.largestFree, after.largestFree);
    // One call is one failure, not two, and it performs exactly one purge.
    TEST_ASSERT_EQUAL_UINT32(before.failures + 1u, after.failures);
    TEST_ASSERT_EQUAL_UINT32(before.purges + 1u, after.purges);
}

void test_alloc_purging_fails_when_purge_frees_too_little(void) {
    // A lone 8-byte cache block between statics: purging recovers 8 bytes,
    // still short of the 16 the request needs.
    alignas(kAlign) std::uint8_t arena[96] = {};
    zone_state_t zone;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, arena, sizeof arena));

    zone_error_t error = ZONE_ERR_OK;
    TEST_ASSERT_NOT_NULL(z_alloc(&zone, 16u, PU_STATIC, &error));
    TEST_ASSERT_NOT_NULL(z_alloc(&zone, 8u, PU_CACHE, &error));
    TEST_ASSERT_NOT_NULL(z_alloc(&zone, 16u, PU_STATIC, &error));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, error);
    TEST_ASSERT_EQUAL_UINT32(0u, z_stats(&zone).freeBytes);

    TEST_ASSERT_NULL(z_alloc_purging(&zone, 16u, PU_LEVEL, &error));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_NO_BLOCK, error);
    TEST_ASSERT_EQUAL_UINT32(1u, z_stats(&zone).purges);
    TEST_ASSERT_EQUAL_UINT32(1u, z_stats(&zone).failures);
    TEST_ASSERT_EQUAL_UINT32(8u, z_stats(&zone).freeBytes);
    TEST_ASSERT_EQUAL_UINT32(40u, z_stats(&zone).inUse);
}

// --- Budget ----------------------------------------------------------------

void test_request_larger_than_arena_changes_no_accounting(void) {
    alignas(kAlign) std::uint8_t arena[256] = {};
    zone_state_t zone;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, arena, sizeof arena));

    const zone_stats_t before = z_stats(&zone);
    zone_error_t error = ZONE_ERR_OK;

    TEST_ASSERT_NULL(z_alloc(&zone, 1000u, PU_LEVEL, &error));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_NO_BLOCK, error);

    // The refusal is recorded once; every byte of the arena is where it was.
    const zone_stats_t after = z_stats(&zone);
    TEST_ASSERT_EQUAL_UINT32(before.arenaSize, after.arenaSize);
    TEST_ASSERT_EQUAL_UINT32(before.inUse, after.inUse);
    TEST_ASSERT_EQUAL_UINT32(before.freeBytes, after.freeBytes);
    TEST_ASSERT_EQUAL_UINT32(before.overhead, after.overhead);
    TEST_ASSERT_EQUAL_UINT32(before.largestFree, after.largestFree);
    TEST_ASSERT_EQUAL_UINT32(before.peakInUse, after.peakInUse);
    TEST_ASSERT_EQUAL_UINT32(before.purges, after.purges);
    TEST_ASSERT_EQUAL_UINT32(before.failures + 1u, after.failures);
    TEST_ASSERT_EQUAL_UINT32(0u, after.inUse);
    TEST_ASSERT_EQUAL_UINT32(256u - kHeader, after.freeBytes);
    TEST_ASSERT_EQUAL_UINT32(kHeader, after.overhead);
    TEST_ASSERT_EQUAL_UINT32(256u - kHeader, after.largestFree);
}

void test_real_help1_lump_does_not_fit_a_64k_arena(void) {
    // The shareware container's largest lump is HELP1 at 68,168 B: a
    // full-screen image that must be handled outside the level zone.
    alignas(kAlign) std::uint8_t arena[65536] = {};
    zone_state_t zone;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, arena, sizeof arena));

    const zone_stats_t before = z_stats(&zone);
    TEST_ASSERT_EQUAL_UINT32(65536u, before.arenaSize);
    TEST_ASSERT_EQUAL_UINT32(65536u - kHeader, before.freeBytes);
    TEST_ASSERT_EQUAL_UINT32(65536u - kHeader, before.largestFree);

    zone_error_t error = ZONE_ERR_OK;
    TEST_ASSERT_NULL(z_alloc(&zone, 68168u, PU_LEVEL, &error));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_NO_BLOCK, error);

    const zone_stats_t after = z_stats(&zone);
    assertAccounting(65536u, after, 0u, 65536u - kHeader, kHeader);
    TEST_ASSERT_EQUAL_UINT32(65536u - kHeader, after.largestFree);
    TEST_ASSERT_EQUAL_UINT32(1u, after.failures);
}

// --- Misuse ----------------------------------------------------------------

void test_misuse_double_free(void) {
    alignas(kAlign) std::uint8_t arena[256] = {};
    zone_state_t zone;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, arena, sizeof arena));

    zone_error_t error = ZONE_ERR_OK;
    void* block = z_alloc(&zone, 32u, PU_STATIC, &error);
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, error);
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_free(&zone, block));

    const zone_stats_t before = z_stats(&zone);
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_DOUBLE_FREE, z_free(&zone, block));
    assertStatsEqual(before, z_stats(&zone));
    assertAccounting(sizeof arena, z_stats(&zone), 0u, 256u - kHeader, kHeader);
}

void test_misuse_foreign_pointer(void) {
    alignas(kAlign) std::uint8_t arena[256] = {};
    alignas(kAlign) std::uint8_t other[64] = {};
    zone_state_t zone;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, arena, sizeof arena));

    const zone_stats_t before = z_stats(&zone);
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(arena);

    TEST_ASSERT_EQUAL_INT(ZONE_ERR_NOT_OWNED, z_free(&zone, other));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_NOT_OWNED,
        z_free(&zone, reinterpret_cast<void*>(base - kAlign)));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_NOT_OWNED,
        z_free(&zone, reinterpret_cast<void*>(base + sizeof arena)));
    assertStatsEqual(before, z_stats(&zone));
}

void test_misuse_pointer_into_block_middle(void) {
    alignas(kAlign) std::uint8_t arena[256] = {};
    zone_state_t zone;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, arena, sizeof arena));

    zone_error_t error = ZONE_ERR_OK;
    auto* block = static_cast<std::uint8_t*>(z_alloc(&zone, 32u, PU_STATIC, &error));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, error);
    const zone_stats_t before = z_stats(&zone);

    TEST_ASSERT_EQUAL_INT(ZONE_ERR_NOT_OWNED, z_free(&zone, block + 1u));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_NOT_OWNED, z_free(&zone, block + 8u));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_NOT_OWNED, z_free(&zone, block + 16u));
    assertStatsEqual(before, z_stats(&zone));

    // The real pointer is still the only one that frees the block.
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_free(&zone, block));
}

void test_misuse_null_pointer_and_unopened_state(void) {
    alignas(kAlign) std::uint8_t arena[256] = {};
    zone_state_t zone = {};
    const zone_stats_t empty = z_stats(&zone);
    TEST_ASSERT_EQUAL_UINT32(0u, empty.arenaSize);
    TEST_ASSERT_EQUAL_UINT32(0u, empty.overhead);

    zone_error_t error = ZONE_ERR_OK;
    TEST_ASSERT_NULL(z_alloc(&zone, 8u, PU_LEVEL, &error));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_NOT_OPEN, error);
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_NOT_OPEN, z_free(&zone, arena));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_NOT_OPEN, z_purge(&zone, nullptr));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_NOT_OPEN, z_free_tag(&zone, PU_CACHE, nullptr));

    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, arena, sizeof arena));
    const zone_stats_t before = z_stats(&zone);
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_NULL_POINTER, z_free(&zone, nullptr));

    // A tag outside the three is rejected before any block is touched.
    u32 freed = 99u;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_BAD_TAG,
                          z_free_tag(&zone, static_cast<zone_tag_t>(9), &freed));
    TEST_ASSERT_EQUAL_UINT32(0u, freed);
    assertStatsEqual(before, z_stats(&zone));
}

void test_failed_init_clears_previous_zone(void) {
    alignas(kAlign) std::uint8_t arena[256] = {};
    alignas(kAlign) std::uint8_t second[256] = {};
    zone_state_t zone;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, arena, sizeof arena));

    zone_error_t error = ZONE_ERR_OK;
    TEST_ASSERT_NOT_NULL(z_alloc(&zone, 16u, PU_STATIC, &error));

    TEST_ASSERT_EQUAL_INT(ZONE_ERR_BAD_ARENA, z_init(&zone, second + 1, 255u));
    const zone_stats_t cleared = z_stats(&zone);
    TEST_ASSERT_EQUAL_UINT32(0u, cleared.arenaSize);
    TEST_ASSERT_EQUAL_UINT32(0u, cleared.inUse);
    TEST_ASSERT_EQUAL_UINT32(0u, cleared.freeBytes);
    TEST_ASSERT_EQUAL_UINT32(0u, cleared.overhead);

    TEST_ASSERT_NULL(z_alloc(&zone, 8u, PU_STATIC, &error));
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_NOT_OPEN, error);
}

// --- Deterministic fuzz -----------------------------------------------------

/// A fixed-seed linear congruential generator: no rand(), no <cstdlib>, and the
/// same sequence on every host and every run.
struct Lcg {
    u32 state;
    u32 next() {
        state = state * 1664525u + 1013904223u;
        return state;
    }
};

struct LiveBlock {
    std::uint8_t* pointer;
    u32 size;
    zone_tag_t tag;
    bool live;
};

void test_fuzz_accounting_and_non_overlap(void) {
    alignas(kAlign) std::uint8_t arena[4096] = {};
    zone_state_t zone;
    TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_init(&zone, arena, sizeof arena));

    constexpr u32 kSlots = 64u;
    LiveBlock slots[kSlots] = {};
    Lcg rng{123456789u};

    for (u32 step = 0; step < 4000u; ++step) {
        const u32 op = rng.next() % 10u;

        if (op <= 4u) {
            const u32 slot = rng.next() % kSlots;
            if (slots[slot].live) {
                continue;
            }
            const u32 size = 1u + rng.next() % 96u;
            const zone_tag_t tag = static_cast<zone_tag_t>(rng.next() % 3u);
            zone_error_t error = ZONE_ERR_OK;
            void* pointer = (op == 4u)
                                ? z_alloc_purging(&zone, size, tag, &error)
                                : z_alloc(&zone, size, tag, &error);
            if (pointer == nullptr) {
                TEST_ASSERT_EQUAL_INT(ZONE_ERR_NO_BLOCK, error);
            } else {
                TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, error);
                assertAligned(pointer);
                const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(pointer);
                TEST_ASSERT_TRUE(address >= reinterpret_cast<std::uintptr_t>(arena));
                TEST_ASSERT_TRUE(address < reinterpret_cast<std::uintptr_t>(arena) + sizeof arena);
                slots[slot] = LiveBlock{static_cast<std::uint8_t*>(pointer), size, tag, true};
            }
        } else if (op <= 7u) {
            const u32 slot = rng.next() % kSlots;
            if (!slots[slot].live) {
                continue;
            }
            TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_free(&zone, slots[slot].pointer));
            slots[slot].live = false;
        } else if (op == 8u) {
            TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_free_tag(&zone, PU_LEVEL, nullptr));
            for (u32 slot = 0; slot < kSlots; ++slot) {
                if (slots[slot].live && slots[slot].tag == PU_LEVEL) {
                    slots[slot].live = false;
                }
            }
        } else {
            TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_purge(&zone, nullptr));
            for (u32 slot = 0; slot < kSlots; ++slot) {
                if (slots[slot].live && slots[slot].tag == PU_CACHE) {
                    slots[slot].live = false;
                }
            }
        }

        const zone_stats_t stats = z_stats(&zone);
        TEST_ASSERT_EQUAL_UINT32(sizeof arena,
            stats.inUse + stats.freeBytes + stats.overhead);
        TEST_ASSERT_TRUE(stats.largestFree <= stats.freeBytes);
        TEST_ASSERT_TRUE(stats.peakInUse >= stats.inUse);

        // No two live payloads overlap. The requested size is rounded up, which
        // is at most the block's capacity, so disjoint capacities imply these
        // regions are disjoint too.
        for (u32 first = 0; first < kSlots; ++first) {
            if (!slots[first].live) {
                continue;
            }
            const std::uintptr_t firstBegin =
                reinterpret_cast<std::uintptr_t>(slots[first].pointer);
            const std::uintptr_t firstEnd = firstBegin + rounded(slots[first].size);
            for (u32 second = first + 1u; second < kSlots; ++second) {
                if (!slots[second].live) {
                    continue;
                }
                const std::uintptr_t secondBegin =
                    reinterpret_cast<std::uintptr_t>(slots[second].pointer);
                const std::uintptr_t secondEnd = secondBegin + rounded(slots[second].size);
                TEST_ASSERT_TRUE(firstEnd <= secondBegin || secondEnd <= firstBegin);
            }
        }
    }

    // Release everything: the arena must return to one coalesced free block.
    for (u32 slot = 0; slot < kSlots; ++slot) {
        if (slots[slot].live) {
            TEST_ASSERT_EQUAL_INT(ZONE_ERR_OK, z_free(&zone, slots[slot].pointer));
        }
    }

    const zone_stats_t finalStats = z_stats(&zone);
    assertAccounting(sizeof arena, finalStats, 0u, sizeof arena - kHeader, kHeader);
    TEST_ASSERT_EQUAL_UINT32(sizeof arena - kHeader, finalStats.largestFree);
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    test_setup();
    UNITY_BEGIN();

    RUN_TEST(test_init_accepts_aligned_arena);
    RUN_TEST(test_init_rejects_invalid_arena);
    RUN_TEST(test_alloc_rejects_invalid_arguments);
    RUN_TEST(test_alloc_rounds_up_and_aligns_every_pointer);
    RUN_TEST(test_free_and_realloc_restore_accounting);
    RUN_TEST(test_adjacent_blocks_do_not_overlap);
    RUN_TEST(test_free_coalesces_three_adjacent_blocks);
    RUN_TEST(test_allocation_fits_only_after_coalescing);
    RUN_TEST(test_level_purge_leaves_statics_intact);
    RUN_TEST(test_cache_purge_leaves_statics_and_level_blocks_intact);
    RUN_TEST(test_purge_reports_recovered_bytes);
    RUN_TEST(test_alloc_purging_succeeds_when_purge_frees_enough);
    RUN_TEST(test_alloc_purging_fails_when_purge_frees_nothing);
    RUN_TEST(test_alloc_purging_fails_when_purge_frees_too_little);
    RUN_TEST(test_request_larger_than_arena_changes_no_accounting);
    RUN_TEST(test_real_help1_lump_does_not_fit_a_64k_arena);
    RUN_TEST(test_misuse_double_free);
    RUN_TEST(test_misuse_foreign_pointer);
    RUN_TEST(test_misuse_pointer_into_block_middle);
    RUN_TEST(test_misuse_null_pointer_and_unopened_state);
    RUN_TEST(test_failed_init_clears_previous_zone);
    RUN_TEST(test_fuzz_accounting_and_non_overlap);

    const int result = UNITY_END();
    test_teardown();
    return result;
}
