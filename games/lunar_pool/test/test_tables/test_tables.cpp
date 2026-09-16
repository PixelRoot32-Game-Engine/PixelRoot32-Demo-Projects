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

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_geometry_segment_contact_within_radius);
    RUN_TEST(test_geometry_segment_contact_outside_radius);
    RUN_TEST(test_geometry_segment_contact_beyond_endpoint);
    RUN_TEST(test_geometry_vertex_contact_within_radius);
    RUN_TEST(test_geometry_vertex_contact_outside_radius);
    RUN_TEST(test_geometry_point_in_polygon_inside);
    RUN_TEST(test_geometry_point_in_polygon_outside);
    RUN_TEST(test_geometry_rowspans_rectangle);
    RUN_TEST(test_geometry_rowspans_cut_corner);
    return UNITY_END();
}
