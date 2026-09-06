/**
 * @brief Whether a watcher can actually see the thing it is watching.
 *
 * A level that falls while an officer is standing in front of you is a manhunt
 * the player can wait out; one that falls only once they lose you is a chase.
 * The difference between those two games is entirely this function, and every
 * one of its failures is invisible: a ray that always returns clear is the old
 * timer back with more code; one that always returns blocked is a city the
 * player can never get out of; an ASYMMETRIC ray -- the officer sees the
 * player from a spot the player cannot see back from -- is worse than both,
 * because it is right most of the time and the rule it breaks cannot be
 * learned.
 *
 * The fake city here is a 16x16 tile grid the test paints walls into. No
 * engine, no tilemap, no scene: the caller answers "is this tile solid" and
 * that is the entire coupling.
 */
#include <unity.h>

#include <cstdint>
#include <cstring>

#include "game/rules/Sight.h"

namespace sg = top_down_city::sight;

namespace {

constexpr int kTile = 16;
constexpr int kGridW = 16;
constexpr int kGridH = 16;

/// The fake city. One byte per tile, non-zero means a wall.
std::uint8_t g_solid[kGridH][kGridW];

bool fakeSolid(const void* /*context*/, int tileX, int tileY) {
    // Off the map is solid, the way the edge of the world is. A ray that
    // wandered off the grid and came back would otherwise read uninitialised
    // memory and the test would be the flakiest thing in the suite.
    if (tileX < 0 || tileY < 0 || tileX >= kGridW || tileY >= kGridH) {
        return true;
    }
    return g_solid[tileY][tileX] != 0;
}

void clearCity() {
    std::memset(g_solid, 0, sizeof(g_solid));
}

void wallAt(int tileX, int tileY) {
    g_solid[tileY][tileX] = 1;
}

/// The centre of a tile, in world pixels. Every case below places people at
/// tile centres so that "which tile is this pixel in" is never the thing
/// under test.
int centreOf(int tile) {
    return tile * kTile + kTile / 2;
}

bool clearBetween(int fromTileX, int fromTileY, int toTileX, int toTileY) {
    return sg::isClear(centreOf(fromTileX), centreOf(fromTileY),
                       centreOf(toTileX), centreOf(toTileY),
                       kTile, fakeSolid, nullptr);
}

}  // namespace

void setUp() {
    clearCity();
}

void tearDown() {}

// --- The two answers ------------------------------------------------------

void test_an_open_street_is_clear() {
    // The case that must not be the only one that works. A ray that returns
    // clear unconditionally passes this and nothing else.
    TEST_ASSERT_TRUE(clearBetween(2, 8, 12, 8));
}

void test_a_wall_between_two_people_blocks_the_view() {
    wallAt(7, 8);
    TEST_ASSERT_FALSE(clearBetween(2, 8, 12, 8));
}

void test_a_wall_beside_the_line_does_not_block() {
    // The other half of the previous case, and the one that catches a ray
    // that is testing a bounding box instead of a line. A city block is
    // solid for tiles in every direction; if being NEAR a wall blocked the
    // view, nobody in this demo could ever see anybody.
    wallAt(7, 6);
    wallAt(7, 10);
    TEST_ASSERT_TRUE(clearBetween(2, 8, 12, 8));
}

// --- The endpoints --------------------------------------------------------

void test_a_watcher_standing_in_a_doorway_can_still_see_out() {
    // An officer's collision box is not the whole tile, so an officer stands
    // in a solid tile more often than it sounds -- in a doorway, against a
    // wall, at the corner of a building. If their own tile blinded them the
    // police would go blind exactly where the player is easiest to corner.
    wallAt(2, 8);
    TEST_ASSERT_TRUE(clearBetween(2, 8, 12, 8));
}

void test_a_target_standing_in_a_doorway_is_still_seen() {
    // Same rule at the far end, and the abuse it prevents is the sharper
    // one: if the target's own tile blocked, the player could shed a wanted
    // level by walking into a doorway in plain view of the officer chasing
    // them.
    wallAt(12, 8);
    TEST_ASSERT_TRUE(clearBetween(2, 8, 12, 8));
}

void test_looking_at_your_own_feet_is_clear() {
    // Degenerate, and it happens: an officer and the player can occupy the
    // same pixel for one step during a collision. There is nothing between
    // them, and a ray with no length must not decide otherwise.
    wallAt(5, 5);
    TEST_ASSERT_TRUE(clearBetween(5, 5, 5, 5));
}

void test_an_adjacent_tile_is_always_visible() {
    // Two tiles side by side have nothing between them at all, so this must
    // hold whatever the sampling does. A ray whose first sample lands past
    // the target would fail this and pass every longer case.
    wallAt(6, 5);
    TEST_ASSERT_TRUE(clearBetween(5, 5, 6, 5));
}

// --- The property that stops it being random ------------------------------

void test_the_view_is_symmetric() {
    // The one failure the player cannot learn around. If an officer can see
    // through a corner the player cannot see back through, then hiding works
    // sometimes and the rule looks like noise rather than cover.
    //
    // Swept over the whole grid against a real block, because asymmetry in a
    // stepped ray shows up at specific angles and not at the tidy ones a
    // hand-written case would pick.
    for (int y = 6; y <= 9; ++y) {
        for (int x = 6; x <= 9; ++x) {
            wallAt(x, y);
        }
    }
    for (int y = 0; y < kGridH; ++y) {
        for (int x = 0; x < kGridW; ++x) {
            const bool there = clearBetween(2, 2, x, y);
            const bool back  = clearBetween(x, y, 2, 2);
            TEST_ASSERT_EQUAL_MESSAGE(there, back, "the view is one-way");
        }
    }
}

// --- Diagonals ------------------------------------------------------------

void test_a_wall_on_a_diagonal_blocks_it() {
    // Streets run north-south and east-west, so every interesting sightline
    // in this city is diagonal. A ray that only walks its dominant axis
    // would let officers see straight through a block at 45 degrees.
    wallAt(5, 5);
    TEST_ASSERT_FALSE(clearBetween(2, 2, 8, 8));
}

void test_a_solid_block_cannot_be_seen_through_at_any_angle() {
    // The whole feature in one case: a city block, and a watcher who cannot
    // see the far side of it from anywhere along the near side. If any of
    // these gets through, there is a diagonal seam in every building in the
    // city and the player will find it before this test does.
    for (int y = 6; y <= 9; ++y) {
        for (int x = 6; x <= 9; ++x) {
            wallAt(x, y);
        }
    }
    for (int y = 6; y <= 9; ++y) {
        TEST_ASSERT_FALSE_MESSAGE(clearBetween(3, y, 12, y),
                                  "saw straight through a block");
    }
    TEST_ASSERT_FALSE(clearBetween(3, 3, 12, 12));
    TEST_ASSERT_FALSE(clearBetween(12, 3, 3, 12));
}

void test_the_corner_of_a_block_can_be_looked_around() {
    // And the reverse: cover has to END somewhere, or the rule is "you are
    // never seen" rather than "buildings hide you". Standing one tile past
    // the corner of the block above is standing in the open.
    for (int y = 6; y <= 9; ++y) {
        for (int x = 6; x <= 9; ++x) {
            wallAt(x, y);
        }
    }
    TEST_ASSERT_TRUE(clearBetween(3, 4, 12, 4));
    TEST_ASSERT_TRUE(clearBetween(4, 3, 4, 12));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_an_open_street_is_clear);
    RUN_TEST(test_a_wall_between_two_people_blocks_the_view);
    RUN_TEST(test_a_wall_beside_the_line_does_not_block);
    RUN_TEST(test_a_watcher_standing_in_a_doorway_can_still_see_out);
    RUN_TEST(test_a_target_standing_in_a_doorway_is_still_seen);
    RUN_TEST(test_looking_at_your_own_feet_is_clear);
    RUN_TEST(test_an_adjacent_tile_is_always_visible);
    RUN_TEST(test_the_view_is_symmetric);
    RUN_TEST(test_a_wall_on_a_diagonal_blocks_it);
    RUN_TEST(test_a_solid_block_cannot_be_seen_through_at_any_angle);
    RUN_TEST(test_the_corner_of_a_block_can_be_looked_around);
    return UNITY_END();
}
