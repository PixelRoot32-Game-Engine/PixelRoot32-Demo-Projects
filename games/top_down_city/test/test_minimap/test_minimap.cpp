/**
 * @brief The minimap's run decomposition, which is the only part of the
 *        overlay that can be wrong without being obvious.
 *
 * The radar used to emit one 1x1 filled rectangle per cell: 1874 on an average
 * frame, each paying two virtual dispatches, a palette resolve and a
 * dirty-region mark to write a single pixel. Measured over the generated map
 * those 1874 cells hold only about 500 maximal runs of equal colour -- 16-tile
 * blocks make roads long and buildings solid -- so coalescing is worth 3.7x
 * the draw calls for exactly the same pixels. "Exactly the same" is the whole
 * claim, and the two ways to lose it are forgetting to flush the run still
 * open at the end of a row, and letting a run span a cell outside the world
 * that must not be painted at all.
 */
#include <unity.h>

#include <cstdint>

#include "game/rules/MinimapRuns.h"

namespace mm = top_down_city::minimap;

namespace {

constexpr int kMaxRuns = 64;

/// Paint `runs` back into a row of cells and compare against the source, so
/// the assertion is "the runs draw what the row said" rather than a
/// hand-counted list of offsets that would have to be rewritten every time a
/// fixture changes.
void assertRunsReproduce(const std::uint8_t* row, int count) {
    mm::Run runs[kMaxRuns];
    const int emitted = mm::rowRuns(row, count, runs);

    TEST_ASSERT_LESS_OR_EQUAL_INT(kMaxRuns, emitted);

    std::uint8_t painted[kMaxRuns * 2];
    for (int i = 0; i < count; ++i) {
        painted[i] = mm::kAbsent;
    }
    for (int r = 0; r < emitted; ++r) {
        TEST_ASSERT_GREATER_THAN_INT(0, runs[r].length);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, runs[r].start);
        TEST_ASSERT_LESS_OR_EQUAL_INT(count, runs[r].start + runs[r].length);
        TEST_ASSERT_NOT_EQUAL_UINT8(mm::kAbsent, runs[r].swatch);
        for (int i = 0; i < runs[r].length; ++i) {
            // Nothing may be painted twice: overlapping runs would still
            // reproduce the row but would cost draw calls the count promised
            // were saved.
            TEST_ASSERT_EQUAL_UINT8(mm::kAbsent, painted[runs[r].start + i]);
            painted[runs[r].start + i] = runs[r].swatch;
        }
    }
    for (int i = 0; i < count; ++i) {
        TEST_ASSERT_EQUAL_UINT8(row[i], painted[i]);
    }
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_an_empty_row_emits_nothing() {
    mm::Run runs[kMaxRuns];
    TEST_ASSERT_EQUAL_INT(0, mm::rowRuns(nullptr, 0, runs));
}

void test_a_row_of_one_colour_is_one_run() {
    std::uint8_t row[8];
    for (int i = 0; i < 8; ++i) {
        row[i] = 3;
    }
    mm::Run runs[kMaxRuns];
    TEST_ASSERT_EQUAL_INT(1, mm::rowRuns(row, 8, runs));
    TEST_ASSERT_EQUAL_INT(0, runs[0].start);
    TEST_ASSERT_EQUAL_INT(8, runs[0].length);
    TEST_ASSERT_EQUAL_UINT8(3, runs[0].swatch);
    assertRunsReproduce(row, 8);
}

void test_alternating_cells_never_coalesce() {
    // The worst case, and the one that must not silently merge: a chequered
    // row is 8 runs and paying 8 draw calls for it is correct.
    std::uint8_t row[8];
    for (int i = 0; i < 8; ++i) {
        row[i] = static_cast<std::uint8_t>((i % 2) ? 1 : 2);
    }
    mm::Run runs[kMaxRuns];
    TEST_ASSERT_EQUAL_INT(8, mm::rowRuns(row, 8, runs));
    assertRunsReproduce(row, 8);
}

void test_the_last_run_is_flushed() {
    // The classic off-by-one: a run still open when the row ends. Without the
    // flush the right-hand edge of the radar goes missing, which on a plate
    // that is always on screen reads as the map being wrong, not the loop.
    const std::uint8_t row[5] = {4, 4, 4, 9, 9};
    mm::Run runs[kMaxRuns];
    TEST_ASSERT_EQUAL_INT(2, mm::rowRuns(row, 5, runs));
    TEST_ASSERT_EQUAL_INT(3, runs[1].start);
    TEST_ASSERT_EQUAL_INT(2, runs[1].length);
    TEST_ASSERT_EQUAL_UINT8(9, runs[1].swatch);
    assertRunsReproduce(row, 5);
}

void test_a_cell_outside_the_world_breaks_the_run() {
    // The radar window is never clamped to the island: it stays centred on
    // the player and cells past the coast are skipped so the backing plate
    // shows through. A run that spanned the gap would paint over the sea.
    const std::uint8_t row[5] = {7, mm::kAbsent, 7, 7, mm::kAbsent};
    mm::Run runs[kMaxRuns];
    TEST_ASSERT_EQUAL_INT(2, mm::rowRuns(row, 5, runs));
    TEST_ASSERT_EQUAL_INT(0, runs[0].start);
    TEST_ASSERT_EQUAL_INT(1, runs[0].length);
    TEST_ASSERT_EQUAL_INT(2, runs[1].start);
    TEST_ASSERT_EQUAL_INT(2, runs[1].length);
    assertRunsReproduce(row, 5);
}

void test_a_row_entirely_outside_the_world_emits_nothing() {
    const std::uint8_t row[4] = {mm::kAbsent, mm::kAbsent,
                                 mm::kAbsent, mm::kAbsent};
    mm::Run runs[kMaxRuns];
    TEST_ASSERT_EQUAL_INT(0, mm::rowRuns(row, 4, runs));
}

void test_equal_colours_across_a_gap_stay_two_runs() {
    const std::uint8_t row[3] = {5, mm::kAbsent, 5};
    mm::Run runs[kMaxRuns];
    TEST_ASSERT_EQUAL_INT(2, mm::rowRuns(row, 3, runs));
    assertRunsReproduce(row, 3);
}

void test_runs_reproduce_a_full_radar_row() {
    // A 48-cell row, the width the overlay actually asks for, shaped like a
    // city block: long solid stretches with a kerb and a gap in them.
    std::uint8_t row[48];
    for (int i = 0; i < 48; ++i) {
        if (i < 4) {
            row[i] = mm::kAbsent;         // past the coast
        } else if (i < 20) {
            row[i] = 11;                  // a block
        } else if (i == 20) {
            row[i] = 2;                   // the kerb
        } else if (i < 44) {
            row[i] = 6;                   // the road
        } else {
            row[i] = 11;                  // the next block
        }
    }
    mm::Run runs[kMaxRuns];
    TEST_ASSERT_EQUAL_INT(4, mm::rowRuns(row, 48, runs));
    assertRunsReproduce(row, 48);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_an_empty_row_emits_nothing);
    RUN_TEST(test_a_row_of_one_colour_is_one_run);
    RUN_TEST(test_alternating_cells_never_coalesce);
    RUN_TEST(test_the_last_run_is_flushed);
    RUN_TEST(test_a_cell_outside_the_world_breaks_the_run);
    RUN_TEST(test_a_row_entirely_outside_the_world_emits_nothing);
    RUN_TEST(test_equal_colours_across_a_gap_stay_two_runs);
    RUN_TEST(test_runs_reproduce_a_full_radar_row);
    return UNITY_END();
}
