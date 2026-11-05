/*
 * Behaviour tests for the vanilla screen melt (lib/doom/f_wipe.c).
 *
 * The melt is deliberately engine-free: it takes the caller's two 320x200
 * index planes and writes the composite into a caller-provided destination, so
 * every rule can be stated over plain byte buffers. No binary fixture is
 * committed and no copyrighted data is used; the planes are generated here.
 *
 * The tests go through the public C API only. They never read the state struct
 * to decide where a column is: a column's position is observed as the first
 * row of the composite that still differs from the destination (new) screen,
 * which is exactly the melt front the screen shows. That keeps a reader that
 * happens to store its state differently from being able to pass.
 */
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <unity.h>

#include "f_wipe.h"

#include "../../test_config.h"

namespace {

constexpr int kW = WIPE_SCREEN_WIDTH;
constexpr int kH = WIPE_SCREEN_HEIGHT;
constexpr std::size_t kBytes = static_cast<std::size_t>(kW) * static_cast<std::size_t>(kH);

/// Ticks the scene advances per rendered frame; the melt tests use the same.
constexpr int kTicksPerFrame = 2;

/*
 * Patterns differ at every pixel, so "this row came from the start screen" and
 * "this row came from the end screen" is decided by the source, never by a
 * coincidence where two different sources happen to hold the same byte.
 */
void fillPlane(std::vector<std::uint8_t>& plane, std::uint8_t base) {
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            plane[static_cast<std::size_t>(y) * static_cast<std::size_t>(kW)
                  + static_cast<std::size_t>(x)] =
                static_cast<std::uint8_t>(base + static_cast<unsigned>(y * 7 + x * 13));
        }
    }
}

/*
 * The row where this two-pixel column stops matching the end screen. Rows above
 * the front hold the new screen, rows at and below it hold the old one, so the
 * first mismatch is the melt front clamped to [0, height].
 */
int meltFront(const std::vector<std::uint8_t>& dest, const std::vector<std::uint8_t>& end,
              int column) {
    const std::size_t x = static_cast<std::size_t>(column) * 2u;
    for (int row = 0; row < kH; ++row) {
        const std::size_t at = static_cast<std::size_t>(row) * static_cast<std::size_t>(kW) + x;
        if (dest[at] != end[at] || dest[at + 1u] != end[at + 1u]) {
            return row;
        }
    }
    return kH;
}

/// Runs a melt to completion and answers the number of `wipe_DoMelt` calls.
int runToDone(wipe_state_t& state, std::vector<std::uint8_t>& dest) {
    int done = 0;
    int frames = 0;
    while (done == 0 && frames < 1000) {
        done = wipe_DoMelt(&state, dest.data(), kTicksPerFrame);
        TEST_ASSERT_TRUE(done >= 0);
        ++frames;
    }
    TEST_ASSERT_EQUAL_INT(1, done);
    return frames;
}

}  // namespace

void test_wipe_final_frame_equals_end_screen(void) {
    std::vector<std::uint8_t> start(kBytes);
    std::vector<std::uint8_t> end(kBytes);
    std::vector<std::uint8_t> dest(kBytes, 0u);
    fillPlane(start, 0x00);
    fillPlane(end, 0x80);
    const std::vector<std::uint8_t> startBefore = start;

    wipe_state_t state;
    wipe_Init(&state);
    TEST_ASSERT_EQUAL_INT(0, wipe_StartScreen(&state, start.data(), kW, kH));
    TEST_ASSERT_EQUAL_INT(0, wipe_EndScreen(&state, end.data()));

    (void)runToDone(state, dest);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(end.data(), dest.data(), kBytes);
    // The melt reads its sources; it never edits the old level's frame.
    TEST_ASSERT_EQUAL_UINT8_ARRAY(startBefore.data(), start.data(), kBytes);
    wipe_Shutdown(&state);
}

void test_wipe_mid_frame_differs_from_both_sources(void) {
    std::vector<std::uint8_t> start(kBytes);
    std::vector<std::uint8_t> end(kBytes);
    std::vector<std::uint8_t> dest(kBytes, 0u);
    fillPlane(start, 0x00);
    fillPlane(end, 0x80);

    wipe_state_t state;
    wipe_Init(&state);
    TEST_ASSERT_EQUAL_INT(0, wipe_StartScreen(&state, start.data(), kW, kH));
    TEST_ASSERT_EQUAL_INT(0, wipe_EndScreen(&state, end.data()));

    // Ten frames put every column past its random start delay and every front
    // strictly between the top and the bottom of the screen.
    for (int frame = 0; frame < 10; ++frame) {
        TEST_ASSERT_EQUAL_INT(0, wipe_DoMelt(&state, dest.data(), kTicksPerFrame));
    }

    TEST_ASSERT_TRUE(std::memcmp(dest.data(), start.data(), kBytes) != 0);
    TEST_ASSERT_TRUE(std::memcmp(dest.data(), end.data(), kBytes) != 0);
    for (int column = 0; column < WIPE_MAX_COLUMNS; ++column) {
        const int front = meltFront(dest, end, column);
        TEST_ASSERT_GREATER_THAN_INT(0, front);
        TEST_ASSERT_LESS_THAN_INT(kH, front);
    }
    wipe_Shutdown(&state);
}

void test_wipe_advance_is_monotonic_per_column(void) {
    std::vector<std::uint8_t> start(kBytes);
    std::vector<std::uint8_t> end(kBytes);
    std::vector<std::uint8_t> dest(kBytes, 0u);
    fillPlane(start, 0x00);
    fillPlane(end, 0x80);

    wipe_state_t state;
    wipe_Init(&state);
    TEST_ASSERT_EQUAL_INT(0, wipe_StartScreen(&state, start.data(), kW, kH));
    TEST_ASSERT_EQUAL_INT(0, wipe_EndScreen(&state, end.data()));

    int previous[WIPE_MAX_COLUMNS] = {0};
    bool advanced[WIPE_MAX_COLUMNS] = {false};

    int done = 0;
    int frames = 0;
    while (done == 0 && frames < 1000) {
        done = wipe_DoMelt(&state, dest.data(), kTicksPerFrame);
        TEST_ASSERT_TRUE(done >= 0);
        for (int column = 0; column < WIPE_MAX_COLUMNS; ++column) {
            const int front = meltFront(dest, end, column);
            TEST_ASSERT_GREATER_OR_EQUAL_INT(previous[column], front);
            if (front > previous[column]) {
                advanced[column] = true;
            }
            previous[column] = front;
        }
        ++frames;
    }

    TEST_ASSERT_EQUAL_INT(1, done);
    for (int column = 0; column < WIPE_MAX_COLUMNS; ++column) {
        TEST_ASSERT_TRUE(advanced[column]);
        TEST_ASSERT_EQUAL_INT(kH, previous[column]);
    }
    wipe_Shutdown(&state);
}

void test_wipe_no_ticks_shows_the_start_screen(void) {
    std::vector<std::uint8_t> start(kBytes);
    std::vector<std::uint8_t> end(kBytes);
    std::vector<std::uint8_t> dest(kBytes, 0u);
    fillPlane(start, 0x00);
    fillPlane(end, 0x80);

    wipe_state_t state;
    wipe_Init(&state);
    TEST_ASSERT_EQUAL_INT(0, wipe_StartScreen(&state, start.data(), kW, kH));
    TEST_ASSERT_EQUAL_INT(0, wipe_EndScreen(&state, end.data()));

    // No advance is not "done": the screen must still be the old frame.
    TEST_ASSERT_EQUAL_INT(0, wipe_DoMelt(&state, dest.data(), 0));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(start.data(), dest.data(), kBytes);
    wipe_Shutdown(&state);
}

void test_wipe_destination_may_alias_end_screen(void) {
    std::vector<std::uint8_t> start(kBytes);
    std::vector<std::uint8_t> endMaster(kBytes);
    fillPlane(start, 0x00);
    fillPlane(endMaster, 0x80);

    // Out-of-place reference: separate destination, a source that never moves.
    std::vector<std::uint8_t> endOut(endMaster);
    std::vector<std::uint8_t> destOut(kBytes, 0u);
    wipe_state_t out;
    wipe_Init(&out);
    TEST_ASSERT_EQUAL_INT(0, wipe_StartScreen(&out, start.data(), kW, kH));
    TEST_ASSERT_EQUAL_INT(0, wipe_EndScreen(&out, endOut.data()));

    // In-place: the destination is the new-frame plane, which the scene
    // re-renders before every call. Both states start from the same random
    // position, so their fronts must advance together.
    std::vector<std::uint8_t> endIn(endMaster);
    wipe_state_t in;
    wipe_Init(&in);
    TEST_ASSERT_EQUAL_INT(0, wipe_StartScreen(&in, start.data(), kW, kH));
    TEST_ASSERT_EQUAL_INT(0, wipe_EndScreen(&in, endIn.data()));

    int doneOut = 0;
    int doneIn = 0;
    for (int frame = 0; frame < 1000 && doneOut == 0; ++frame) {
        std::copy(endMaster.begin(), endMaster.end(), endIn.begin());
        doneOut = wipe_DoMelt(&out, destOut.data(), kTicksPerFrame);
        doneIn = wipe_DoMelt(&in, endIn.data(), kTicksPerFrame);
        TEST_ASSERT_EQUAL_INT(doneOut, doneIn);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(destOut.data(), endIn.data(), kBytes);
    }

    TEST_ASSERT_EQUAL_INT(1, doneOut);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(endMaster.data(), endIn.data(), kBytes);
    wipe_Shutdown(&out);
    wipe_Shutdown(&in);
}

void test_wipe_second_melt_after_shutdown(void) {
    std::vector<std::uint8_t> start(kBytes);
    std::vector<std::uint8_t> end(kBytes);
    std::vector<std::uint8_t> dest(kBytes, 0u);
    fillPlane(start, 0x00);
    fillPlane(end, 0x80);

    wipe_state_t state;
    wipe_Init(&state);
    TEST_ASSERT_EQUAL_INT(0, wipe_StartScreen(&state, start.data(), kW, kH));
    TEST_ASSERT_EQUAL_INT(0, wipe_EndScreen(&state, end.data()));
    (void)runToDone(state, dest);

    wipe_Shutdown(&state);

    // A second level transition must be able to reuse the same state object.
    std::vector<std::uint8_t> start2(kBytes);
    std::vector<std::uint8_t> end2(kBytes);
    std::vector<std::uint8_t> dest2(kBytes, 0xFFu);
    fillPlane(start2, 0x10);
    fillPlane(end2, 0xC0);

    TEST_ASSERT_EQUAL_INT(0, wipe_StartScreen(&state, start2.data(), kW, kH));
    TEST_ASSERT_EQUAL_INT(0, wipe_EndScreen(&state, end2.data()));
    (void)runToDone(state, dest2);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(end2.data(), dest2.data(), kBytes);
    wipe_Shutdown(&state);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    test_setup();
    UNITY_BEGIN();

    RUN_TEST(test_wipe_final_frame_equals_end_screen);
    RUN_TEST(test_wipe_mid_frame_differs_from_both_sources);
    RUN_TEST(test_wipe_advance_is_monotonic_per_column);
    RUN_TEST(test_wipe_no_ticks_shows_the_start_screen);
    RUN_TEST(test_wipe_destination_may_alias_end_screen);
    RUN_TEST(test_wipe_second_melt_after_shutdown);

    const int result = UNITY_END();
    test_teardown();
    return result;
}
