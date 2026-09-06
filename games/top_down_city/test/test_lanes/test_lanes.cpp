/**
 * @brief The grammar of the street grid: which tile is a lane, and which way
 *        traffic runs down it.
 *
 * Every mistake this file can catch produces a car that keeps driving: on the
 * wrong side of the road, into oncoming traffic, or straight through a
 * junction it should have been able to turn at. None of those crash, and none
 * look like a bug in a lookup table -- they look like the traffic being
 * stupid, which is a much longer thing to trace.
 *
 * The band layout the tests assume is the generator's:
 *
 *     offset 0   sidewalk
 *     offset 1   lane
 *     offset 2   centre line
 *     offset 3   lane
 *     offset 4   sidewalk
 */
#include <unity.h>

#include <cstdint>

#include "game/rules/Lanes.h"

namespace ln = top_down_city::lanes;

void setUp() {}
void tearDown() {}

namespace {

// A horizontal band across rows 20..24, running from x=10 to x=100, and a
// vertical band down columns 40..44, running from y=5 to y=90. They cross,
// so the fixture carries a junction as well as two plain stretches.
constexpr ln::Band kBands[] = {
    { 20, 10, 100, 1 },
    { 40,  5,  90, 0 },
};
constexpr int kBandCount = 2;

int headingCount(int tx, int ty) {
    ln::Heading out[ln::kMaxHeadingsPerTile];
    return ln::headingsAt(tx, ty, kBands, kBandCount, out);
}

bool carries(int tx, int ty, int dx, int dy) {
    ln::Heading out[ln::kMaxHeadingsPerTile];
    const int n = ln::headingsAt(tx, ty, kBands, kBandCount, out);
    for (int i = 0; i < n; ++i) {
        if (out[i].dx == dx && out[i].dy == dy) {
            return true;
        }
    }
    return false;
}

}  // namespace

// --- What counts as a lane -----------------------------------------------

void test_open_ground_is_not_a_lane() {
    TEST_ASSERT_FALSE(ln::isLane(0, 0, kBands, kBandCount));
    TEST_ASSERT_FALSE(ln::isLane(70, 70, kBands, kBandCount));
    TEST_ASSERT_EQUAL_INT(0, headingCount(70, 70));
}

void test_the_kerb_and_the_centre_line_are_not_lanes() {
    // Offsets 0 and 4 are pavement and offset 2 is the painted line. A car
    // that treats any of the three as drivable ends up half on the kerb or
    // nose to nose with the oncoming lane.
    TEST_ASSERT_FALSE(ln::isLane(50, 20, kBands, kBandCount));  // offset 0
    TEST_ASSERT_FALSE(ln::isLane(50, 22, kBands, kBandCount));  // offset 2
    TEST_ASSERT_FALSE(ln::isLane(50, 24, kBands, kBandCount));  // offset 4
}

void test_the_two_lanes_of_a_band_are_lanes() {
    TEST_ASSERT_TRUE(ln::isLane(50, 21, kBands, kBandCount));
    TEST_ASSERT_TRUE(ln::isLane(50, 23, kBands, kBandCount));
}

void test_a_band_stops_where_the_generator_clipped_it() {
    // The table carries the stretch that is actually carriageway, so the
    // tile past the end is open ground -- not a lane that runs on into the
    // park or off the shore.
    TEST_ASSERT_TRUE(ln::isLane(10, 21, kBands, kBandCount));
    TEST_ASSERT_FALSE(ln::isLane(9, 21, kBands, kBandCount));
    TEST_ASSERT_TRUE(ln::isLane(100, 21, kBands, kBandCount));
    TEST_ASSERT_FALSE(ln::isLane(101, 21, kBands, kBandCount));
}

void test_an_empty_table_has_no_lanes_anywhere() {
    // Defensive rather than hypothetical: the generator clips every band to
    // real asphalt and could in principle clip them all away. A traffic pool
    // that then spawns nothing is fine; one that reads past the table is not.
    for (int y = 0; y < 128; y += 7) {
        for (int x = 0; x < 128; x += 7) {
            TEST_ASSERT_FALSE(ln::isLane(x, y, kBands, 0));
        }
    }
}

// --- Which way traffic runs ----------------------------------------------

void test_traffic_keeps_right_on_an_east_west_street() {
    // Facing east, a driver's right hand points south, so the eastbound lane
    // is the SOUTHERN one -- offset 3, the higher row. Getting this pair the
    // wrong way round is a city where every car drives on the left, which is
    // perfectly consistent and completely wrong.
    TEST_ASSERT_TRUE(carries(50, 23, 1, 0));    // offset 3: east
    TEST_ASSERT_TRUE(carries(50, 21, -1, 0));   // offset 1: west
    TEST_ASSERT_FALSE(carries(50, 23, -1, 0));
    TEST_ASSERT_FALSE(carries(50, 21, 1, 0));
}

void test_traffic_keeps_right_on_a_north_south_street() {
    // Facing south, right points west, so the southbound lane is the WESTERN
    // one -- offset 1, the lower column.
    TEST_ASSERT_TRUE(carries(41, 60, 0, 1));    // offset 1: south
    TEST_ASSERT_TRUE(carries(43, 60, 0, -1));   // offset 3: north
    TEST_ASSERT_FALSE(carries(41, 60, 0, -1));
    TEST_ASSERT_FALSE(carries(43, 60, 0, 1));
}

void test_a_plain_lane_offers_exactly_one_way_out() {
    TEST_ASSERT_EQUAL_INT(1, headingCount(50, 21));
    TEST_ASSERT_EQUAL_INT(1, headingCount(50, 23));
    TEST_ASSERT_EQUAL_INT(1, headingCount(41, 60));
    TEST_ASSERT_EQUAL_INT(1, headingCount(43, 60));
}

// --- Junctions ------------------------------------------------------------

void test_a_junction_tile_offers_two_ways_out() {
    // Where the two bands' lanes cross, a car may carry on or turn. Four
    // such tiles per junction, one per pair of lanes, and each of them is
    // the only place that particular turn can be taken from.
    TEST_ASSERT_EQUAL_INT(2, headingCount(41, 21));
    TEST_ASSERT_EQUAL_INT(2, headingCount(41, 23));
    TEST_ASSERT_EQUAL_INT(2, headingCount(43, 21));
    TEST_ASSERT_EQUAL_INT(2, headingCount(43, 23));
}

void test_the_right_turn_out_of_a_junction_is_where_it_should_be() {
    // Eastbound (row 23) meeting the southbound lane (column 41): the turn
    // exists there and nowhere else along that row.
    TEST_ASSERT_TRUE(carries(41, 23, 1, 0));
    TEST_ASSERT_TRUE(carries(41, 23, 0, 1));
    TEST_ASSERT_FALSE(carries(42, 23, 0, 1));   // centre line, no turn here
}

void test_the_centre_of_a_junction_is_still_not_a_lane() {
    // Both bands call (42, 22) their centre line. Two nothings do not add up
    // to a lane, and a car that thought otherwise would sit on the paint in
    // the middle of the crossroads.
    TEST_ASSERT_FALSE(ln::isLane(42, 22, kBands, kBandCount));
}

void test_a_junction_never_offers_a_u_turn() {
    // The two bands that meet are perpendicular by construction and each lane
    // has one direction, so no tile can offer a heading and its opposite. A
    // car allowed to reverse into the lane it just left would spin on the spot
    // forever -- checked over every tile of the fixture, junction and all.
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 110; ++x) {
            ln::Heading out[ln::kMaxHeadingsPerTile];
            const int n = ln::headingsAt(x, y, kBands, kBandCount, out);
            TEST_ASSERT_TRUE(n >= 0 && n <= ln::kMaxHeadingsPerTile);
            for (int i = 0; i < n; ++i) {
                // Every heading is one whole step along one axis.
                TEST_ASSERT_EQUAL_INT(1, out[i].dx * out[i].dx
                                       + out[i].dy * out[i].dy);
                for (int j = i + 1; j < n; ++j) {
                    const bool opposite = out[j].dx == -out[i].dx
                                       && out[j].dy == -out[i].dy;
                    TEST_ASSERT_FALSE(opposite);
                    const bool duplicate = out[j].dx == out[i].dx
                                        && out[j].dy == out[i].dy;
                    TEST_ASSERT_FALSE(duplicate);
                }
            }
        }
    }
}

void test_is_lane_and_headings_at_always_agree() {
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 110; ++x) {
            const bool lane = ln::isLane(x, y, kBands, kBandCount);
            TEST_ASSERT_EQUAL_INT(lane ? 1 : 0, headingCount(x, y) > 0 ? 1 : 0);
        }
    }
}

// --- The carriageway, kerb to kerb ---------------------------------------

void test_the_whole_carriageway_is_carriageway() {
    // Wider than isLane on purpose. The painted centre line is not a lane --
    // a car sitting on it is nose to nose with the oncoming side -- but it is
    // very much the middle of the road, and a pedestrian standing on it is
    // standing in traffic.
    TEST_ASSERT_TRUE(ln::isCarriageway(50, 21, kBands, kBandCount));
    TEST_ASSERT_TRUE(ln::isCarriageway(50, 22, kBands, kBandCount));
    TEST_ASSERT_TRUE(ln::isCarriageway(50, 23, kBands, kBandCount));
}

void test_the_pavement_is_not_carriageway() {
    TEST_ASSERT_FALSE(ln::isCarriageway(50, 20, kBands, kBandCount));
    TEST_ASSERT_FALSE(ln::isCarriageway(50, 24, kBands, kBandCount));
    TEST_ASSERT_FALSE(ln::isCarriageway(50, 60, kBands, kBandCount));
    TEST_ASSERT_FALSE(ln::isCarriageway(-1, 21, kBands, kBandCount));
}

void test_every_lane_is_carriageway_but_not_the_other_way_round() {
    int laneCount = 0;
    int roadCount = 0;
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 110; ++x) {
            const bool lane = ln::isLane(x, y, kBands, kBandCount);
            const bool road = ln::isCarriageway(x, y, kBands, kBandCount);
            if (lane) {
                TEST_ASSERT_TRUE(road);
                ++laneCount;
            }
            if (road) {
                ++roadCount;
            }
        }
    }
    TEST_ASSERT_TRUE(laneCount > 0);
    TEST_ASSERT_TRUE(roadCount > laneCount);
}

// --- Crossings ------------------------------------------------------------

namespace {

// Three tiles running east-west at (40, 19), and three running north-south
// at (9, 21) -- the shapes the generator paints on the two approaches to a
// junction.
constexpr ln::Crossing kCrossings[] = {
    { 41, 19, 3, 1 },
    {  9, 21, 3, 0 },
};
constexpr int kCrossingCount = 2;

bool crossing(int x, int y) {
    return ln::isCrossing(x, y, kCrossings, kCrossingCount);
}

}  // namespace

void test_every_tile_of_a_strip_is_a_crossing() {
    TEST_ASSERT_TRUE(crossing(41, 19));
    TEST_ASSERT_TRUE(crossing(42, 19));
    TEST_ASSERT_TRUE(crossing(43, 19));
    TEST_ASSERT_TRUE(crossing(9, 21));
    TEST_ASSERT_TRUE(crossing(9, 22));
    TEST_ASSERT_TRUE(crossing(9, 23));
}

void test_a_strip_ends_where_it_says_it_does() {
    // The off-by-one that would let somebody step off the zebra and into the
    // lane beside it, which is the one place the whole rule is supposed to
    // stop them.
    TEST_ASSERT_FALSE(crossing(40, 19));
    TEST_ASSERT_FALSE(crossing(44, 19));
    TEST_ASSERT_FALSE(crossing(9, 20));
    TEST_ASSERT_FALSE(crossing(9, 24));
}

void test_a_strip_is_one_tile_thick() {
    // A three-tile strip is three tiles, not a three-by-three square.
    TEST_ASSERT_FALSE(crossing(42, 18));
    TEST_ASSERT_FALSE(crossing(42, 20));
    TEST_ASSERT_FALSE(crossing(8, 22));
    TEST_ASSERT_FALSE(crossing(10, 22));
}

void test_nothing_is_a_crossing_without_a_table() {
    TEST_ASSERT_FALSE(ln::isCrossing(41, 19, kCrossings, 0));
    TEST_ASSERT_FALSE(ln::isCrossing(41, 19, nullptr, kCrossingCount));
    TEST_ASSERT_FALSE(crossing(-1, 19));
    TEST_ASSERT_FALSE(crossing(41, -1));
}

void test_a_zero_length_strip_covers_nothing() {
    // Defensive: the generator groups the tiles it really painted into runs,
    // and a guard that painted none would otherwise open a one-tile hole in
    // the kerb rule at whatever coordinate the empty record carried.
    const ln::Crossing empty[] = { { 41, 19, 0, 1 } };
    TEST_ASSERT_FALSE(ln::isCrossing(41, 19, empty, 1));
}

// --- Negative coordinates -------------------------------------------------

void test_a_tile_off_the_map_is_not_a_lane() {
    // The traffic pool samples a ring around the camera, and the camera sits
    // on the map edge often enough that this is a normal input rather than a
    // corner case. The table is unsigned; the query must not be.
    TEST_ASSERT_FALSE(ln::isLane(-1, 21, kBands, kBandCount));
    TEST_ASSERT_FALSE(ln::isLane(50, -3, kBands, kBandCount));
    TEST_ASSERT_FALSE(ln::isLane(-40, -40, kBands, kBandCount));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_open_ground_is_not_a_lane);
    RUN_TEST(test_the_kerb_and_the_centre_line_are_not_lanes);
    RUN_TEST(test_the_two_lanes_of_a_band_are_lanes);
    RUN_TEST(test_a_band_stops_where_the_generator_clipped_it);
    RUN_TEST(test_an_empty_table_has_no_lanes_anywhere);
    RUN_TEST(test_traffic_keeps_right_on_an_east_west_street);
    RUN_TEST(test_traffic_keeps_right_on_a_north_south_street);
    RUN_TEST(test_a_plain_lane_offers_exactly_one_way_out);
    RUN_TEST(test_a_junction_tile_offers_two_ways_out);
    RUN_TEST(test_the_right_turn_out_of_a_junction_is_where_it_should_be);
    RUN_TEST(test_the_centre_of_a_junction_is_still_not_a_lane);
    RUN_TEST(test_a_junction_never_offers_a_u_turn);
    RUN_TEST(test_is_lane_and_headings_at_always_agree);
    RUN_TEST(test_a_tile_off_the_map_is_not_a_lane);
    RUN_TEST(test_the_whole_carriageway_is_carriageway);
    RUN_TEST(test_the_pavement_is_not_carriageway);
    RUN_TEST(test_every_lane_is_carriageway_but_not_the_other_way_round);
    RUN_TEST(test_every_tile_of_a_strip_is_a_crossing);
    RUN_TEST(test_a_strip_ends_where_it_says_it_does);
    RUN_TEST(test_a_strip_is_one_tile_thick);
    RUN_TEST(test_nothing_is_a_crossing_without_a_table);
    RUN_TEST(test_a_zero_length_strip_covers_nothing);
    return UNITY_END();
}
