/**
 * @brief Which way a frightened person runs, and how close a fright has to be.
 *
 * Small enough to look obvious and wrong in two ways that are not: the tie on
 * an exact diagonal, where "no best axis" must not become "no direction"; and
 * the threat landing exactly on top of somebody. Both arguments are written
 * out at the cases that pin them --
 * `test_an_exact_diagonal_still_picks_one_axis` and
 * `test_a_threat_on_top_of_somebody_still_moves_them`.
 */
#include <unity.h>

#include <cstdint>

#include "game/rules/Threat.h"

namespace th = top_down_city::threat;

void setUp() {}
void tearDown() {}

namespace {

/// Every escape must be one whole step along exactly one axis.
void assertUnitAxis(th::Bearing e) {
    const int magnitude = e.dx * e.dx + e.dy * e.dy;
    TEST_ASSERT_EQUAL_INT(1, magnitude);
}

}  // namespace

// --- Which way to run ----------------------------------------------------

void test_a_person_runs_directly_away_on_an_axis() {
    // The offset is FROM the threat TO the person, so a positive x means the
    // threat is to their left and they should keep going right.
    assertUnitAxis(th::awayFrom(10, 0));
    TEST_ASSERT_EQUAL_INT(1, th::awayFrom(10, 0).dx);
    TEST_ASSERT_EQUAL_INT(-1, th::awayFrom(-10, 0).dx);
    TEST_ASSERT_EQUAL_INT(1, th::awayFrom(0, 10).dy);
    TEST_ASSERT_EQUAL_INT(-1, th::awayFrom(0, -10).dy);
}

void test_the_dominant_axis_wins() {
    // Mostly to the right and slightly below: run right, not down. Choosing
    // the smaller axis would send them across the line of fire.
    const th::Bearing e = th::awayFrom(30, 4);
    assertUnitAxis(e);
    TEST_ASSERT_EQUAL_INT(1, e.dx);
    TEST_ASSERT_EQUAL_INT(0, e.dy);

    const th::Bearing f = th::awayFrom(-4, -30);
    assertUnitAxis(f);
    TEST_ASSERT_EQUAL_INT(0, f.dx);
    TEST_ASSERT_EQUAL_INT(-1, f.dy);
}

void test_an_exact_diagonal_still_picks_one_axis() {
    // No best answer, so any answer will do -- except both, and except
    // neither. Both would be a diagonal the art cannot draw and a pedestrian
    // moving 1.41x their own speed; neither would be a statue.
    for (int d = 1; d < 64; ++d) {
        assertUnitAxis(th::awayFrom(d, d));
        assertUnitAxis(th::awayFrom(-d, d));
        assertUnitAxis(th::awayFrom(d, -d));
        assertUnitAxis(th::awayFrom(-d, -d));
    }
}

void test_a_threat_on_top_of_somebody_still_moves_them() {
    // What a bullet is at the instant it connects. A survivor standing
    // perfectly still on the spot they were just shot at is the one reaction
    // worse than no reaction at all.
    assertUnitAxis(th::awayFrom(0, 0));
}

void test_escapes_are_exhaustively_valid() {
    // Every offset in a 41x41 window around the threat, including the centre
    // and all four axes. There is no input to this function that may return
    // a diagonal, a zero, or anything longer than one step.
    for (int dy = -20; dy <= 20; ++dy) {
        for (int dx = -20; dx <= 20; ++dx) {
            assertUnitAxis(th::awayFrom(dx, dy));
        }
    }
}

// --- How close counts ----------------------------------------------------

void test_a_threat_inside_the_radius_is_noticed() {
    TEST_ASSERT_TRUE(th::within(0, 0, 10));
    TEST_ASSERT_TRUE(th::within(6, 8, 10));      // exactly on the circle
    TEST_ASSERT_TRUE(th::within(-6, -8, 10));
}

void test_a_threat_outside_the_radius_is_not() {
    TEST_ASSERT_FALSE(th::within(7, 8, 10));     // just past it
    TEST_ASSERT_FALSE(th::within(11, 0, 10));
    TEST_ASSERT_FALSE(th::within(0, -11, 10));
}

void test_the_radius_is_a_circle_not_a_square() {
    // The corner of the bounding box is 1.41 radii away. A square would have
    // people on the diagonal reacting to a shot they are nowhere near.
    TEST_ASSERT_FALSE(th::within(10, 10, 10));
}

void test_distance_does_not_overflow_at_map_scale() {
    // The world is 2048 px square, so the widest offset squared is about
    // 4.2 million and the sum about 8.4 million. Comfortable in 32 bits --
    // this is here so that a bigger map is a failing test rather than a
    // pedestrian who panics at a gunshot two districts away.
    TEST_ASSERT_FALSE(th::within(2047, 2047, 64));
    TEST_ASSERT_FALSE(th::within(-2047, 2047, 64));
    TEST_ASSERT_TRUE(th::within(2047, 2047, 4096));
}

void test_a_zero_radius_notices_only_a_direct_hit() {
    TEST_ASSERT_TRUE(th::within(0, 0, 0));
    TEST_ASSERT_FALSE(th::within(1, 0, 0));
}

// --- Which way to shoot --------------------------------------------------

void test_an_officer_aims_back_along_the_axis() {
    // The offset runs FROM the shooter TO the target, so a target to the
    // right is shot at rightwards.
    TEST_ASSERT_EQUAL_INT(1,  th::toward(10, 0).dx);
    TEST_ASSERT_EQUAL_INT(-1, th::toward(-10, 0).dx);
    TEST_ASSERT_EQUAL_INT(1,  th::toward(0, 10).dy);
    TEST_ASSERT_EQUAL_INT(-1, th::toward(0, -10).dy);
    assertUnitAxis(th::toward(0, 0));
}

void test_toward_and_away_are_the_same_computation() {
    // They agree, and the first draft asserted they were opposites -- the
    // mistake worth pinning down. A person at P flees a threat at S along
    // P - S, and a shooter at S aims along P - S too; the sign that separates
    // "away" from "toward" is in which offset the CALLER subtracts, never in
    // the function. Both names exist because `awayFrom(player - officer)` at
    // an officer's call site reads as the opposite of what it does, and this
    // is what stops somebody "simplifying" it by negating one of them.
    for (int dy = -20; dy <= 20; ++dy) {
        for (int dx = -20; dx <= 20; ++dx) {
            const th::Bearing to = th::toward(dx, dy);
            const th::Bearing away = th::awayFrom(dx, dy);
            assertUnitAxis(to);
            TEST_ASSERT_EQUAL_INT(away.dx, to.dx);
            TEST_ASSERT_EQUAL_INT(away.dy, to.dy);
        }
    }
}

// --- Whether the shot is worth taking ------------------------------------

void test_a_target_on_the_axis_is_worth_shooting() {
    TEST_ASSERT_TRUE(th::hasLineOfFire(40, 0, 8));
    TEST_ASSERT_TRUE(th::hasLineOfFire(0, -40, 8));
    // Slightly off the axis but still inside the target's own width.
    TEST_ASSERT_TRUE(th::hasLineOfFire(40, 8, 8));
    TEST_ASSERT_TRUE(th::hasLineOfFire(-40, -8, 8));
}

void test_a_target_off_the_axis_is_not() {
    // The whole point. A four-way aim fired at anything diagonal sprays past
    // the target, and an officer emptying a magazine into the pavement beside
    // the player does not read as bad luck -- it reads as broken.
    TEST_ASSERT_FALSE(th::hasLineOfFire(40, 9, 8));
    TEST_ASSERT_FALSE(th::hasLineOfFire(40, 40, 8));
    TEST_ASSERT_FALSE(th::hasLineOfFire(-9, 40, 8));
}

void test_the_minor_axis_is_the_one_that_matters() {
    // An officer 200 px away but dead level has a clean shot; one 12 px away
    // and diagonal does not. Distance is `within`'s job, not this one's.
    TEST_ASSERT_TRUE(th::hasLineOfFire(2000, 0, 8));
    TEST_ASSERT_FALSE(th::hasLineOfFire(12, 12, 8));
}

void test_a_target_underfoot_is_always_shootable() {
    // Both axes zero: degenerate, but it must not answer "no shot" and leave
    // an officer standing on the player doing nothing at all.
    TEST_ASSERT_TRUE(th::hasLineOfFire(0, 0, 8));
}

// --- Getting the shot ----------------------------------------------------

void test_a_shooter_already_lined_up_does_not_move() {
    // The one answer that has to be zero, and the only place in this file a
    // zero bearing is legal. An officer who keeps sidestepping once they are
    // lined up walks straight back out of the line they just found.
    TEST_ASSERT_EQUAL_INT(0, th::intoLine(40, 0, 8).dx);
    TEST_ASSERT_EQUAL_INT(0, th::intoLine(40, 0, 8).dy);
    TEST_ASSERT_EQUAL_INT(0, th::intoLine(40, 8, 8).dy);
    TEST_ASSERT_EQUAL_INT(0, th::intoLine(0, 0, 8).dx);
}

void test_a_shooter_steps_across_the_bullet_not_along_it() {
    // Forty right and twenty down: the round would travel east, so the offset
    // that decides whether it connects is the twenty. Closing the forty
    // instead is the bug -- the officer walks the whole way to the player and
    // is never once lined up, because the axis they were fixing was already
    // the one that did not matter.
    const th::Bearing step = th::intoLine(40, 20, 8);
    TEST_ASSERT_EQUAL_INT(0, step.dx);
    TEST_ASSERT_EQUAL_INT(1, step.dy);

    const th::Bearing back = th::intoLine(40, -20, 8);
    TEST_ASSERT_EQUAL_INT(0, back.dx);
    TEST_ASSERT_EQUAL_INT(-1, back.dy);
}

void test_the_axes_swap_with_the_dominant_one() {
    // Mostly below and slightly right: the round travels south, so the step
    // is east-west.
    const th::Bearing step = th::intoLine(20, 40, 8);
    TEST_ASSERT_EQUAL_INT(1, step.dx);
    TEST_ASSERT_EQUAL_INT(0, step.dy);
}

void test_the_step_and_the_line_use_the_same_tie_break() {
    // An exact diagonal has no dominant axis, and the two functions must pick
    // the SAME one -- otherwise the officer steps along the axis the bullet
    // is about to travel down and the two fight each other forever.
    for (int d = 9; d < 64; ++d) {
        // Not lined up at this tolerance, so there must be a step...
        TEST_ASSERT_FALSE(th::hasLineOfFire(d, d, 8));
        const th::Bearing step = th::intoLine(d, d, 8);
        TEST_ASSERT_EQUAL_INT(1, step.dx * step.dx + step.dy * step.dy);
        // ...and taking it must reduce the offset the line test looks at.
        const int before = 8;
        (void)before;
        TEST_ASSERT_TRUE(th::hasLineOfFire(d - step.dx * d, d - step.dy * d, 0));
    }
}

void test_stepping_repeatedly_always_reaches_the_line() {
    // The property the officer depends on: follow this one pixel at a time
    // and the shot comes on, from anywhere, in a bounded number of steps.
    for (int dy = -40; dy <= 40; dy += 3) {
        for (int dx = -40; dx <= 40; dx += 3) {
            int x = dx;
            int y = dy;
            int guard = 0;
            while (!th::hasLineOfFire(x, y, 8) && guard < 200) {
                const th::Bearing step = th::intoLine(x, y, 8);
                TEST_ASSERT_EQUAL_INT(1, step.dx * step.dx + step.dy * step.dy);
                // The shooter moves, so the offset TO the target shrinks by
                // what they just covered.
                x -= step.dx;
                y -= step.dy;
                ++guard;
            }
            TEST_ASSERT_TRUE(guard < 200);
        }
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_a_person_runs_directly_away_on_an_axis);
    RUN_TEST(test_the_dominant_axis_wins);
    RUN_TEST(test_an_exact_diagonal_still_picks_one_axis);
    RUN_TEST(test_a_threat_on_top_of_somebody_still_moves_them);
    RUN_TEST(test_escapes_are_exhaustively_valid);
    RUN_TEST(test_a_threat_inside_the_radius_is_noticed);
    RUN_TEST(test_a_threat_outside_the_radius_is_not);
    RUN_TEST(test_the_radius_is_a_circle_not_a_square);
    RUN_TEST(test_distance_does_not_overflow_at_map_scale);
    RUN_TEST(test_a_zero_radius_notices_only_a_direct_hit);
    RUN_TEST(test_an_officer_aims_back_along_the_axis);
    RUN_TEST(test_toward_and_away_are_the_same_computation);
    RUN_TEST(test_a_target_on_the_axis_is_worth_shooting);
    RUN_TEST(test_a_target_off_the_axis_is_not);
    RUN_TEST(test_the_minor_axis_is_the_one_that_matters);
    RUN_TEST(test_a_target_underfoot_is_always_shootable);
    RUN_TEST(test_a_shooter_already_lined_up_does_not_move);
    RUN_TEST(test_a_shooter_steps_across_the_bullet_not_along_it);
    RUN_TEST(test_the_axes_swap_with_the_dominant_one);
    RUN_TEST(test_the_step_and_the_line_use_the_same_tie_break);
    RUN_TEST(test_stepping_repeatedly_always_reaches_the_line);
    return UNITY_END();
}
