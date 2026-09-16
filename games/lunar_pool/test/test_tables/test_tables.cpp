/*
 * Unit tests for table geometry (src/pool/Geometry.*, src/pool/TableDef.h).
 *
 * These cover the predicates load-time table validation will use once
 * src/pool/Table.* lands in a later batch: segment/vertex contact, whether a
 * point sits inside a polygon, and the row-scan fill spans the scene will
 * later reuse to paint the felt.
 */
#include <unity.h>

#include <cstdint>

#include "pool/Geometry.h"
#include "pool/Table.h"
#include "pool/TableDef.h"

using namespace pool;

void setUp(void) {}
void tearDown(void) {}

// --- segmentContact ------------------------------------------------------------

void test_geometry_segment_contact_within_radius(void) {
    // A 100px-long horizontal segment from (0,0) to (100,0).
    const Segment seg{0, 0, 100, 0, 10000};
    // 2px above the segment's midpoint, well inside a 4px-radius ball.
    TEST_ASSERT_TRUE(segmentContact(seg, 50 * 256, 512, 1024));
}

void test_geometry_segment_contact_outside_radius(void) {
    const Segment seg{0, 0, 100, 0, 10000};
    // 8px above the midpoint: still over the segment's interior, but past
    // the 4px radius.
    TEST_ASSERT_FALSE(segmentContact(seg, 50 * 256, 2048, 1024));
}

void test_geometry_segment_contact_beyond_endpoint(void) {
    const Segment seg{0, 0, 100, 0, 10000};
    // Past x=100 (the segment's end), 2px above the line: close enough by
    // distance alone, but outside the interior span, so no contact here --
    // that is vertexContact's job instead.
    TEST_ASSERT_FALSE(segmentContact(seg, 150 * 256, 512, 1024));
}

void test_geometry_segment_contact_behind_cushion(void) {
    const Segment seg{0, 0, 100, 0, 10000};
    // 2px on the far side of the segment from its normal (-ey, ex) = (0,
    // 100): the early return for sdNum < 0, not the radius test.
    TEST_ASSERT_FALSE(segmentContact(seg, 50 * 256, -512, 1024));
}

void test_geometry_segment_contact_at_exact_radius(void) {
    const Segment seg{0, 0, 100, 0, 10000};
    // Exactly 4px from the segment: the comparison is strict '<', so a
    // distance equal to the radius is NOT a contact.
    TEST_ASSERT_FALSE(segmentContact(seg, 50 * 256, 1024, 1024));
}

// --- vertexContact ---------------------------------------------------------------

void test_geometry_vertex_contact_within_radius(void) {
    const Segment seg{0, 0, 100, 0, 10000};
    // 2px from the segment's start point on both axes.
    TEST_ASSERT_TRUE(vertexContact(seg, 512, 512, 1024));
}

void test_geometry_vertex_contact_outside_radius(void) {
    const Segment seg{0, 0, 100, 0, 10000};
    TEST_ASSERT_FALSE(vertexContact(seg, 2048, 0, 1024));
}

void test_geometry_vertex_contact_at_exact_radius(void) {
    const Segment seg{0, 0, 100, 0, 10000};
    // Exactly 4px from the vertex (0,0): strict '<' means no contact.
    TEST_ASSERT_FALSE(vertexContact(seg, 1024, 0, 1024));
}

// --- pointInPolygon --------------------------------------------------------------

void test_geometry_point_in_polygon_inside(void) {
    static const PointPx kSquare[4] = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    const Polyline square{kSquare, 4};
    TEST_ASSERT_TRUE(pointInPolygon(square, 50 * 256, 50 * 256));
}

void test_geometry_point_in_polygon_outside(void) {
    static const PointPx kSquare[4] = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    const Polyline square{kSquare, 4};
    TEST_ASSERT_FALSE(pointInPolygon(square, 150 * 256, 50 * 256));
}

void test_geometry_point_in_polygon_on_edge(void) {
    // The half-open rule assigns the top edge (y=0) to the polygon: a point
    // sitting exactly on it, away from any vertex, is inside.
    static const PointPx kSquare[4] = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    const Polyline square{kSquare, 4};
    TEST_ASSERT_TRUE(pointInPolygon(square, 50 * 256, 0));
}

void test_geometry_point_in_polygon_on_vertex(void) {
    // The same half-open rule excludes the bottom edge (y=100): a vertex on
    // it is outside, the mirror case of the on-edge test above.
    static const PointPx kSquare[4] = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    const Polyline square{kSquare, 4};
    TEST_ASSERT_FALSE(pointInPolygon(square, 0, 100 * 256));
}

// --- rowSpans ----------------------------------------------------------------

void test_geometry_rowspans_rectangle(void) {
    static const PointPx kSquare[4] = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    const Polyline square{kSquare, 4};
    int16_t xs[8];

    // A middle row spans the square's full width.
    const uint8_t midCount = rowSpans(square, 5, xs, 8);
    TEST_ASSERT_EQUAL_UINT8(2, midCount);
    TEST_ASSERT_EQUAL_INT16(0, xs[0]);
    TEST_ASSERT_EQUAL_INT16(10, xs[1]);

    // The half-open y rule excludes the bottom edge row: a 10px-tall square
    // spanning y=0..10 fills rows 0..9, not row 10.
    const uint8_t bottomCount = rowSpans(square, 10, xs, 8);
    TEST_ASSERT_EQUAL_UINT8(0, bottomCount);
}

void test_geometry_rowspans_cut_corner(void) {
    // A square with its top-left corner cut off by the (20,0)-(0,20) edge.
    static const PointPx kCutCorner[5] = {
        {20, 0}, {0, 20}, {0, 100}, {100, 100}, {100, 0}};
    const Polyline shape{kCutCorner, 5};
    int16_t xs[8];

    // Row 10 crosses the diagonal cut: the span starts at x=10, not x=0.
    const uint8_t cutRowCount = rowSpans(shape, 10, xs, 8);
    TEST_ASSERT_EQUAL_UINT8(2, cutRowCount);
    TEST_ASSERT_EQUAL_INT16(10, xs[0]);
    TEST_ASSERT_EQUAL_INT16(100, xs[1]);

    // Below the cut, the row is the full width again.
    const uint8_t fullRowCount = rowSpans(shape, 25, xs, 8);
    TEST_ASSERT_EQUAL_UINT8(2, fullRowCount);
    TEST_ASSERT_EQUAL_INT16(0, xs[0]);
    TEST_ASSERT_EQUAL_INT16(100, xs[1]);
}

// --- Table load-time validation ---------------------------------------------

namespace {

// A safe rectangle far from every cushion, pocket and other ball used below;
// every TableError case that does not itself mutate the border reuses it.
constexpr PointPx kBorder[4] = {{0, 0}, {200, 0}, {200, 200}, {0, 200}};
constexpr Polyline kBorderLine{kBorder, 4};
constexpr PocketDef kPocket{{10, 10}, 0, 1};  // top-edge mouth, 200px wide
constexpr TargetDef kOneTarget{1, {100, 130}};  // 30px from the cue
TableDef baseTableDef() {
    TableDef def{};
    def.border = kBorderLine;
    def.pockets = &kPocket;
    def.pocketCount = 1;
    def.cue = {100, 100};
    def.targets = &kOneTarget;
    def.targetCount = 1;
    return def;
}

void expectTableError(TableError expected, const TableDef& def) {
    Table table;
    const TableError actual = loadTable(def, table);
    TEST_ASSERT_EQUAL(static_cast<int>(expected), static_cast<int>(actual));
}

}  // namespace

void test_table_valid_table_loads(void) {
    Table table;
    const TableError err = loadTable(baseTableDef(), table);
    TEST_ASSERT_EQUAL(static_cast<int>(TableError::None), static_cast<int>(err));
    TEST_ASSERT_EQUAL_UINT8(4, table.segmentCount);
    TEST_ASSERT_EQUAL_UINT8(1, table.pocketCount);
    TEST_ASSERT_EQUAL_UINT8(2, table.ballCount);
    TEST_ASSERT_EQUAL_UINT8(1, table.balls[1].number);
}

void test_table_too_many_balls(void) {
    // 10 targets + the cue = 11 balls, over kMaxBalls (10). Numbers don't
    // need to be valid: TooManyBalls is the first check and returns before
    // any target is inspected.
    static const TargetDef kTenTargets[10] = {
        {1, {101, 100}}, {2, {102, 100}}, {3, {103, 100}}, {4, {104, 100}}, {5, {105, 100}},
        {6, {106, 100}}, {7, {107, 100}}, {8, {108, 100}}, {9, {109, 100}}, {10, {110, 100}},
    };
    TableDef def = baseTableDef();
    def.targets = kTenTargets;
    def.targetCount = 10;
    expectTableError(TableError::TooManyBalls, def);
}

void test_table_bad_ball_number(void) {
    // A single target numbered 2 instead of 1 stays under kMaxBalls, so this
    // isolates BadBallNumber from TooManyBalls.
    TableDef def = baseTableDef();
    static const TargetDef kBadNumber{2, {100, 130}};
    def.targets = &kBadNumber;
    expectTableError(TableError::BadBallNumber, def);
}

void test_table_too_many_segments(void) {
    // border (4) + one 45-point obstacle = 49, over kMaxSegments (48). Point
    // values are irrelevant: TooManySegments only sums counts and returns
    // before any point is read.
    static const PointPx kManyPoints[45] = {};
    static const Polyline kManyObstacle{kManyPoints, 45};
    TableDef def = baseTableDef();
    def.obstacles = &kManyObstacle;
    def.obstacleCount = 1;
    expectTableError(TableError::TooManySegments, def);
}

void test_table_bad_mouth_index(void) {
    // kBorderLine has 4 points (valid indices 0..3); mouthA=4 is out of range.
    static const PocketDef kBadMouth{{10, 10}, 4, 0};
    TableDef def = baseTableDef();
    def.pockets = &kBadMouth;
    expectTableError(TableError::BadMouthIndex, def);
}

void test_table_ball_outside_table(void) {
    TableDef def = baseTableDef();
    def.cue = {300, 300};  // outside the 0..200 border
    expectTableError(TableError::BallOutsideTable, def);
}

void test_table_obstacle_valid_loads(void) {
    // A 20x20 obstacle well inside the border, wound counter-clockwise (the
    // reverse of the border's own valid order) so shoelaceTwiceArea is
    // negative, as loadTable()'s BadWinding check requires for obstacles.
    static const PointPx kObstaclePoints[4] = {{150, 50}, {150, 70}, {170, 70}, {170, 50}};
    static const Polyline kObstacle{kObstaclePoints, 4};
    TableDef def = baseTableDef();
    def.obstacles = &kObstacle;
    def.obstacleCount = 1;
    Table table;
    const TableError err = loadTable(def, table);
    TEST_ASSERT_EQUAL(static_cast<int>(TableError::None), static_cast<int>(err));
    TEST_ASSERT_EQUAL_UINT8(8, table.segmentCount);  // 4 border + 4 obstacle
}

void test_table_obstacle_zero_length_segment(void) {
    static const PointPx kBadObstacle[4] = {{150, 50}, {150, 50}, {170, 70}, {170, 50}};
    static const Polyline kObstacle{kBadObstacle, 4};
    TableDef def = baseTableDef();
    def.obstacles = &kObstacle;
    def.obstacleCount = 1;
    expectTableError(TableError::ZeroLengthSegment, def);
}

void test_table_obstacle_bad_winding(void) {
    // Same shape as the valid obstacle above, but wound clockwise (the
    // border's own valid order) -- a positive shoelace, invalid for an
    // obstacle.
    static const PointPx kCwObstacle[4] = {{150, 50}, {170, 50}, {170, 70}, {150, 70}};
    static const Polyline kObstacle{kCwObstacle, 4};
    TableDef def = baseTableDef();
    def.obstacles = &kObstacle;
    def.obstacleCount = 1;
    expectTableError(TableError::BadWinding, def);
}

void test_table_ball_overlaps_ball(void) {
    TableDef def = baseTableDef();
    static const TargetDef kOverlapping{1, {102, 100}};  // 2px from the cue
    def.targets = &kOverlapping;
    expectTableError(TableError::BallOverlapsBall, def);
}

void test_table_ball_overlaps_cushion(void) {
    TableDef def = baseTableDef();
    def.cue = {2, 100};  // 2px from the left cushion
    expectTableError(TableError::BallOverlapsCushion, def);
}

void test_table_ball_in_pocket(void) {
    TableDef def = baseTableDef();
    def.cue = {10, 10};  // exactly the pocket center
    expectTableError(TableError::BallInPocket, def);
}

void test_table_zero_length_segment(void) {
    static const PointPx kPoints[5] = {{0, 0}, {0, 0}, {200, 0}, {200, 200}, {0, 200}};
    static const Polyline kLine{kPoints, 5};
    TableDef def = baseTableDef();
    def.border = kLine;
    expectTableError(TableError::ZeroLengthSegment, def);
}

void test_table_narrow_pocket_mouth(void) {
    static const PointPx kPoints[5] = {{0, 0}, {5, 0}, {200, 0}, {200, 200}, {0, 200}};
    static const Polyline kLine{kPoints, 5};
    static const PocketDef kNarrow{{2, 10}, 0, 1};  // 5px gap, under 12px
    TableDef def = baseTableDef();
    def.border = kLine;
    def.pockets = &kNarrow;
    expectTableError(TableError::NarrowPocketMouth, def);
}

void test_table_bad_winding(void) {
    static const PointPx kPoints[4] = {{0, 0}, {0, 200}, {200, 200}, {200, 0}};
    static const Polyline kLine{kPoints, 4};
    TableDef def = baseTableDef();
    def.border = kLine;
    expectTableError(TableError::BadWinding, def);
}

void test_table_ball_inside_obstacle_interior(void) {
    // A 40x40 obstacle; the cue sits at its center, 20px from every edge and
    // vertex -- past segmentContact()/vertexContact()'s 4px reach, so only a
    // containment check catches it.
    static const PointPx kObstaclePoints[4] = {{150, 50}, {150, 90}, {190, 90}, {190, 50}};
    static const Polyline kObstacle{kObstaclePoints, 4};
    TableDef def = baseTableDef();
    def.obstacles = &kObstacle;
    def.obstacleCount = 1;
    def.cue = {170, 70};
    expectTableError(TableError::BallOverlapsCushion, def);
}

void test_table_too_many_pockets(void) {
    // kMaxPockets + 1 identical, otherwise-valid pockets must be rejected before any pocket is written.
    static const PocketDef kPockets[kMaxPockets + 1] = {
        {{10, 10}, 0, 1}, {{10, 10}, 0, 1}, {{10, 10}, 0, 1}, {{10, 10}, 0, 1}, {{10, 10}, 0, 1},
        {{10, 10}, 0, 1}, {{10, 10}, 0, 1}, {{10, 10}, 0, 1}, {{10, 10}, 0, 1},
    };
    TableDef def = baseTableDef();
    def.pockets = kPockets;
    def.pocketCount = kMaxPockets + 1;
    Table table;
    const TableError err = loadTable(def, table);
    TEST_ASSERT_EQUAL(static_cast<int>(TableError::TooManyPockets), static_cast<int>(err));
    TEST_ASSERT_EQUAL_UINT8(0, table.pocketCount);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_geometry_segment_contact_within_radius);
    RUN_TEST(test_geometry_segment_contact_outside_radius);
    RUN_TEST(test_geometry_segment_contact_beyond_endpoint);
    RUN_TEST(test_geometry_segment_contact_behind_cushion);
    RUN_TEST(test_geometry_segment_contact_at_exact_radius);
    RUN_TEST(test_geometry_vertex_contact_within_radius);
    RUN_TEST(test_geometry_vertex_contact_outside_radius);
    RUN_TEST(test_geometry_vertex_contact_at_exact_radius);
    RUN_TEST(test_geometry_point_in_polygon_inside);
    RUN_TEST(test_geometry_point_in_polygon_outside);
    RUN_TEST(test_geometry_point_in_polygon_on_edge);
    RUN_TEST(test_geometry_point_in_polygon_on_vertex);
    RUN_TEST(test_geometry_rowspans_rectangle);
    RUN_TEST(test_geometry_rowspans_cut_corner);
    RUN_TEST(test_table_valid_table_loads);
    RUN_TEST(test_table_too_many_balls);
    RUN_TEST(test_table_bad_ball_number);
    RUN_TEST(test_table_too_many_segments);
    RUN_TEST(test_table_bad_mouth_index);
    RUN_TEST(test_table_ball_outside_table);
    RUN_TEST(test_table_obstacle_valid_loads);
    RUN_TEST(test_table_obstacle_zero_length_segment);
    RUN_TEST(test_table_obstacle_bad_winding);
    RUN_TEST(test_table_ball_overlaps_ball);
    RUN_TEST(test_table_ball_overlaps_cushion);
    RUN_TEST(test_table_ball_in_pocket);
    RUN_TEST(test_table_zero_length_segment);
    RUN_TEST(test_table_narrow_pocket_mouth);
    RUN_TEST(test_table_bad_winding);
    RUN_TEST(test_table_ball_inside_obstacle_interior);
    RUN_TEST(test_table_too_many_pockets);
    return UNITY_END();
}
