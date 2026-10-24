/*
 * RED suite for the destructible shields (S10).
 *
 * The initial mask has cut top corners, a full-width body and a bottom arch;
 * a hit erodes a clipped 3x3 block and a shield with no bits left disappears.
 * A shot's segment is walked one pixel at a time, so a multi-pixel step cannot
 * skip a row, and a path that crosses only cells the shield already lost
 * reports no hit and erodes nothing.
 * A stub that reports every pixel empty fails both on the "solid" assertions
 * and on the erosion counts.
 */
#include <unity.h>

#include "rules/ShieldMask.h"

using namespace spaceinvaders::rules;

namespace {

int solidNeighbourhood(const ShieldField& field, int shield, int x, int y) {
    int solid = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (field.solid(shield, x + dx, y + dy)) {
                ++solid;
            }
        }
    }
    return solid;
}

void carveEveryPixel(ShieldField& field, int shield) {
    for (int y = 0; y < ShieldHeight; ++y) {
        for (int x = 0; x < ShieldWidth; ++x) {
            field.carve(shield, x, y);
        }
    }
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_shields_top_row_has_cut_corners(void) {
    ShieldField field;
    field.reset();

    TEST_ASSERT_FALSE(field.solid(0, 0, 0));
    TEST_ASSERT_FALSE(field.solid(0, 1, 0));
    TEST_ASSERT_TRUE(field.solid(0, 2, 0));
    TEST_ASSERT_TRUE(field.solid(0, 19, 0));
    TEST_ASSERT_FALSE(field.solid(0, 20, 0));
    TEST_ASSERT_FALSE(field.solid(0, 21, 0));

    // Bits 2..19 set, bit 0 at the left edge.
    TEST_ASSERT_EQUAL_UINT32(0x000FFFFCu, field.rowBits(0, 0));
}

void test_shields_second_row_is_full_width(void) {
    ShieldField field;
    field.reset();

    TEST_ASSERT_TRUE(field.solid(0, 0, 1));
    TEST_ASSERT_TRUE(field.solid(0, 21, 1));
    TEST_ASSERT_EQUAL_UINT32(0x003FFFFFu, field.rowBits(0, 1));
}

void test_shields_bottom_arch_rows(void) {
    ShieldField field;
    field.reset();

    // Row 12 clears x=9..12.
    TEST_ASSERT_FALSE(field.solid(0, 9, 12));
    TEST_ASSERT_FALSE(field.solid(0, 12, 12));
    TEST_ASSERT_TRUE(field.solid(0, 8, 12));
    TEST_ASSERT_TRUE(field.solid(0, 13, 12));

    // Row 13 clears x=8..13.
    TEST_ASSERT_FALSE(field.solid(0, 8, 13));
    TEST_ASSERT_FALSE(field.solid(0, 13, 13));
    TEST_ASSERT_TRUE(field.solid(0, 7, 13));
    TEST_ASSERT_TRUE(field.solid(0, 14, 13));

    // Row 14 clears x=7..14.
    TEST_ASSERT_FALSE(field.solid(0, 7, 14));
    TEST_ASSERT_FALSE(field.solid(0, 14, 14));
    TEST_ASSERT_TRUE(field.solid(0, 6, 14));
    TEST_ASSERT_TRUE(field.solid(0, 15, 14));

    // Row 15 clears x=6..15.
    TEST_ASSERT_FALSE(field.solid(0, 6, 15));
    TEST_ASSERT_FALSE(field.solid(0, 15, 15));
    TEST_ASSERT_TRUE(field.solid(0, 5, 15));
    TEST_ASSERT_TRUE(field.solid(0, 16, 15));
}

void test_shields_carve_clears_a_three_by_three_block(void) {
    ShieldField field;
    field.reset();

    TEST_ASSERT_EQUAL_INT(9, solidNeighbourhood(field, 0, 10, 5));

    field.carve(0, 10, 5);

    TEST_ASSERT_EQUAL_INT(0, solidNeighbourhood(field, 0, 10, 5));
    TEST_ASSERT_TRUE(field.solid(0, 8, 5));   // left of the blast
    TEST_ASSERT_TRUE(field.solid(0, 10, 3));  // above the blast
}

void test_shields_carve_at_corner_clips_to_the_mask(void) {
    ShieldField field;
    field.reset();

    field.carve(0, 0, 0);

    TEST_ASSERT_FALSE(field.solid(0, 0, 0));
    TEST_ASSERT_FALSE(field.solid(0, 1, 0));
    TEST_ASSERT_FALSE(field.solid(0, 0, 1));
    TEST_ASSERT_FALSE(field.solid(0, 1, 1));
    TEST_ASSERT_TRUE(field.solid(0, 2, 0));   // outside the clipped block
    TEST_ASSERT_TRUE(field.solid(0, 0, 2));   // below the clipped block
}

void test_shields_carving_every_pixel_destroys_the_shield(void) {
    ShieldField field;
    field.reset();

    carveEveryPixel(field, 0);

    TEST_ASSERT_TRUE(field.destroyed(0));
    TEST_ASSERT_FALSE(field.destroyed(1));
}

void test_shields_swept_carve_erodes_the_first_solid_pixel(void) {
    ShieldField field;
    field.reset();

    // The shot enters the mask from above: row 0 is the first solid row, so
    // the blast lands there and never reaches the current end position.
    TEST_ASSERT_TRUE(carveSwept(field, 0, 10, -4, 10, 4));

    TEST_ASSERT_FALSE(field.solid(0, 10, 0));
    TEST_ASSERT_FALSE(field.solid(0, 9, 0));
    TEST_ASSERT_FALSE(field.solid(0, 11, 1));
    TEST_ASSERT_TRUE(field.solid(0, 8, 0));    // left of the blast
    TEST_ASSERT_TRUE(field.solid(0, 10, 2));   // row after the blast
}

void test_shields_swept_carve_walks_the_segment_not_its_endpoints(void) {
    ShieldField field;
    field.reset();

    // Both endpoints are solid, but the first solid pixel on the diagonal is
    // (1,1). A rule that only tested the endpoints would blast (11,11).
    TEST_ASSERT_TRUE(carveSwept(field, 0, 0, 0, 11, 11));

    TEST_ASSERT_FALSE(field.solid(0, 1, 1));
    TEST_ASSERT_FALSE(field.solid(0, 0, 2));
    TEST_ASSERT_FALSE(field.solid(0, 2, 0));
    TEST_ASSERT_TRUE(field.solid(0, 3, 1));    // right of the blast
    TEST_ASSERT_TRUE(field.solid(0, 11, 11));  // the far end was never reached
}

void test_shields_swept_carve_through_a_hole_reports_no_hit(void) {
    ShieldField field;
    field.reset();

    // The first sweep stops at row 4 and clears rows 3..5 around column 10.
    TEST_ASSERT_TRUE(carveSwept(field, 0, 10, 4, 10, 6));

    const uint32_t aboveHole = field.rowBits(0, 2);
    const uint32_t belowHole = field.rowBits(0, 6);

    // The second sweep follows the same column through the hole: nothing
    // solid is left on the path, so it reports no hit and erodes nothing.
    TEST_ASSERT_FALSE(carveSwept(field, 0, 10, 3, 10, 5));

    TEST_ASSERT_EQUAL_UINT32(aboveHole, field.rowBits(0, 2));
    TEST_ASSERT_EQUAL_UINT32(belowHole, field.rowBits(0, 6));
    TEST_ASSERT_TRUE(field.solid(0, 10, 2));
    TEST_ASSERT_TRUE(field.solid(0, 10, 6));
    TEST_ASSERT_FALSE(field.destroyed(0));
}

void test_shields_swept_carve_can_stop_at_the_next_shield(void) {
    ShieldField field;
    field.reset();

    // A shot carves a hole in the first shield and then flies through it on the
    // next ticks: no hit and no erosion.
    TEST_ASSERT_TRUE(carveSwept(field, 0, 10, 4, 10, 6));
    TEST_ASSERT_FALSE(carveSwept(field, 0, 10, 3, 10, 5));

    // The same segment against the next, intact shield still hits it, so
    // passing through a hole does not disarm the shot for the rest of its run.
    TEST_ASSERT_TRUE(carveSwept(field, 1, 10, 3, 10, 5));
    TEST_ASSERT_FALSE(field.solid(1, 10, 2));
    TEST_ASSERT_FALSE(field.solid(1, 10, 3));
    TEST_ASSERT_TRUE(field.solid(1, 10, 5));
    TEST_ASSERT_TRUE(field.solid(0, 10, 2));
}

void test_shields_swept_carve_ignores_columns_past_the_mask(void) {
    ShieldField fresh;
    fresh.reset();

    ShieldField field;
    field.reset();

    // Column 30 is past the 22-pixel mask. Wrapping it modulo the width would
    // land on column 8 and erode a shield the shot never touched.
    TEST_ASSERT_FALSE(carveSwept(field, 0, 30, -4, 30, 4));

    for (int y = 0; y < ShieldHeight; ++y) {
        TEST_ASSERT_EQUAL_UINT32(fresh.rowBits(0, y), field.rowBits(0, y));
    }
}

void test_shields_swept_carve_clips_a_start_above_the_mask(void) {
    ShieldField field;
    field.reset();

    // Starting above the mask must clip at row 0, not wrap the negative row
    // onto the bottom row the arch carved out.
    TEST_ASSERT_TRUE(carveSwept(field, 0, 5, -4, 5, 4));

    TEST_ASSERT_FALSE(field.solid(0, 5, 0));
    TEST_ASSERT_TRUE(field.solid(0, 5, 15));
    TEST_ASSERT_TRUE(field.solid(0, 5, 3));
}

void test_shields_reset_restores_the_intact_mask(void) {
    ShieldField field;
    field.reset();
    carveEveryPixel(field, 0);
    TEST_ASSERT_TRUE(field.destroyed(0));

    field.reset();

    TEST_ASSERT_FALSE(field.destroyed(0));
    TEST_ASSERT_TRUE(field.solid(0, 11, 1));
    TEST_ASSERT_TRUE(field.solid(0, 19, 0));
    TEST_ASSERT_FALSE(field.solid(0, 0, 0));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_shields_top_row_has_cut_corners);
    RUN_TEST(test_shields_second_row_is_full_width);
    RUN_TEST(test_shields_bottom_arch_rows);
    RUN_TEST(test_shields_carve_clears_a_three_by_three_block);
    RUN_TEST(test_shields_carve_at_corner_clips_to_the_mask);
    RUN_TEST(test_shields_carving_every_pixel_destroys_the_shield);
    RUN_TEST(test_shields_swept_carve_erodes_the_first_solid_pixel);
    RUN_TEST(test_shields_swept_carve_walks_the_segment_not_its_endpoints);
    RUN_TEST(test_shields_swept_carve_through_a_hole_reports_no_hit);
    RUN_TEST(test_shields_swept_carve_can_stop_at_the_next_shield);
    RUN_TEST(test_shields_swept_carve_ignores_columns_past_the_mask);
    RUN_TEST(test_shields_swept_carve_clips_a_start_above_the_mask);
    RUN_TEST(test_shields_reset_restores_the_intact_mask);

    return UNITY_END();
}
