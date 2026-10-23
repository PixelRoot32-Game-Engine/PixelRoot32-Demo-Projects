/*
 * RED suite for the destructible shields (S10).
 *
 * The initial mask has cut top corners, a full-width body and a bottom arch;
 * a hit erodes a clipped 3x3 block and a shield with no bits left disappears.
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
    RUN_TEST(test_shields_reset_restores_the_intact_mask);

    return UNITY_END();
}
