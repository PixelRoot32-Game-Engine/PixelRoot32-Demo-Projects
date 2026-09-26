/*
 * Unit tests for collision response (src/pool/World::resolveCushions and
 * resolveBalls). Every expectation is an exact integer: src/pool/ has no
 * float/double, so axial cases are bit-exact and momentum sums are raw-unit
 * exact by construction (J is computed once and applied symmetrically).
 */
#include <unity.h>

#include <cstdint>

#include "pool/Fixed.h"
#include "pool/Geometry.h"
#include "pool/Table.h"
#include "pool/World.h"

using namespace pool;

void setUp(void) {}
void tearDown(void) {}

namespace {

// A table with a single cushion segment from (ax, ay) raw spanning
// (ex, ey) px; no pockets, so captureNear() never interferes.
Table singleSegmentTable(int32_t ax, int32_t ay, int16_t ex, int16_t ey) {
    Table table{};
    table.segments[0].ax = ax;
    table.segments[0].ay = ay;
    table.segments[0].ex = ex;
    table.segments[0].ey = ey;
    table.segments[0].lenSqPx = static_cast<int32_t>(ex) * ex + static_cast<int32_t>(ey) * ey;
    table.segmentCount = 1;
    return table;
}

// Horizontal cushion (0,0)-(100,0): inward normal +y, |N| = 100 exactly.
Table flatCushion() {
    return singleSegmentTable(0, 0, 100, 0);
}

}  // namespace

void test_cushion_head_on_reflects_exactly(void) {
    // 2 px above the cushion (radius 4 px), heading straight down at 5000.
    Table table = flatCushion();
    World world;
    world.placeBall(0, 50 * kPxScale, 512, 0, -5000);
    world.resolveCushions(table);

    // Gap pushed to exactly radius + slop (1024 + 2), normal flipped.
    TEST_ASSERT_EQUAL_INT32(50 * kPxScale, world.ball(0).x);
    TEST_ASSERT_EQUAL_INT32(kBallRadiusRaw + kSlopRaw, world.ball(0).y);
    TEST_ASSERT_EQUAL_INT32(0, world.ball(0).vx);
    TEST_ASSERT_EQUAL_INT32(5000, world.ball(0).vy);
}

void test_cushion_preserves_tangential_component(void) {
    // Oblique approach: the x share must survive untouched, the y share flips.
    Table table = flatCushion();
    World world;
    world.placeBall(0, 50 * kPxScale, 512, 3000, -4000);
    world.resolveCushions(table);

    TEST_ASSERT_EQUAL_INT32(3000, world.ball(0).vx);
    TEST_ASSERT_EQUAL_INT32(4000, world.ball(0).vy);
}

void test_cushion_separating_ball_keeps_velocity(void) {
    // Overlapping but moving away: position is still pushed out (contact must
    // read clear afterwards), velocity is left alone so resting contact never
    // injects energy.
    Table table = flatCushion();
    World world;
    world.placeBall(0, 50 * kPxScale, 512, 0, 2000);
    world.resolveCushions(table);

    TEST_ASSERT_EQUAL_INT32(kBallRadiusRaw + kSlopRaw, world.ball(0).y);
    TEST_ASSERT_EQUAL_INT32(0, world.ball(0).vx);
    TEST_ASSERT_EQUAL_INT32(2000, world.ball(0).vy);
}

void test_cushion_resolve_clears_contact(void) {
    // The slop contract: after a resolve, the same predicate that triggered
    // must read clear, or the next substep would re-fire from rounding alone.
    Table table = flatCushion();
    World world;
    world.placeBall(0, 50 * kPxScale, 512, 0, -5000);
    world.resolveCushions(table);

    TEST_ASSERT_FALSE(segmentContact(table.segments[0], world.ball(0).x, world.ball(0).y, kBallRadiusRaw));
}

void test_cushion_vertex_reflects(void) {
    // Past the segment end (interior test rejects it), 2 px from the start
    // vertex, heading into it: vertex job, not segment job.
    Table table = flatCushion();
    World world;
    world.placeBall(0, -512, 0, 2000, 0);
    world.resolveCushions(table);

    TEST_ASSERT_EQUAL_INT32(-(kBallRadiusRaw + kSlopRaw), world.ball(0).x);
    TEST_ASSERT_EQUAL_INT32(0, world.ball(0).y);
    TEST_ASSERT_EQUAL_INT32(-2000, world.ball(0).vx);
    TEST_ASSERT_EQUAL_INT32(0, world.ball(0).vy);
}

void test_balls_head_on_transfers_all_velocity(void) {
    // 2000 raw apart (contact 2048), A at 1000 into resting B: full transfer
    // plus a symmetric split to contact + slop (2050).
    World world;
    world.placeBall(0, 0, 0, 1000, 0);
    world.placeBall(1, 2000, 0, 0, 0);
    world.resolveBalls();

    TEST_ASSERT_EQUAL_INT32(0, world.ball(0).vx);
    TEST_ASSERT_EQUAL_INT32(0, world.ball(0).vy);
    TEST_ASSERT_EQUAL_INT32(1000, world.ball(1).vx);
    TEST_ASSERT_EQUAL_INT32(0, world.ball(1).vy);
    TEST_ASSERT_EQUAL_INT32(-25, world.ball(0).x);
    TEST_ASSERT_EQUAL_INT32(2025, world.ball(1).x);
}

void test_balls_oblique_conserves_momentum_exactly(void) {
    // A (300,400) into resting B 1500 raw away on +x: J lands exactly on
    // (300,0), so A keeps (0,400) and the raw-unit sums never move.
    World world;
    world.placeBall(0, 0, 0, 300, 400);
    world.placeBall(1, 1500, 0, 0, 0);
    world.resolveBalls();

    TEST_ASSERT_EQUAL_INT32(0, world.ball(0).vx);
    TEST_ASSERT_EQUAL_INT32(400, world.ball(0).vy);
    TEST_ASSERT_EQUAL_INT32(300, world.ball(1).vx);
    TEST_ASSERT_EQUAL_INT32(0, world.ball(1).vy);
    TEST_ASSERT_EQUAL_INT32(-275, world.ball(0).x);
    TEST_ASSERT_EQUAL_INT32(1775, world.ball(1).x);
}

void test_balls_separating_pair_keeps_velocity(void) {
    // Overlapping but receding: still separated positionally, velocities
    // untouched.
    World world;
    world.placeBall(0, 0, 0, -1000, 0);
    world.placeBall(1, 2000, 0, 0, 0);
    world.resolveBalls();

    TEST_ASSERT_EQUAL_INT32(-1000, world.ball(0).vx);
    TEST_ASSERT_EQUAL_INT32(0, world.ball(1).vx);
    TEST_ASSERT_EQUAL_INT32(-25, world.ball(0).x);
    TEST_ASSERT_EQUAL_INT32(2025, world.ball(1).x);
}

void test_balls_resolve_clears_overlap(void) {
    World world;
    world.placeBall(0, 0, 0, 1000, 0);
    world.placeBall(1, 2000, 0, 0, 0);
    world.resolveBalls();

    const int64_t dx = static_cast<int64_t>(world.ball(1).x) - world.ball(0).x;
    const int64_t dy = static_cast<int64_t>(world.ball(1).y) - world.ball(0).y;
    TEST_ASSERT_TRUE(dx * dx + dy * dy >= static_cast<int64_t>(kBallContactRaw) * kBallContactRaw);
}

void test_balls_coincident_centers_separate_deterministically(void) {
    // Degenerate input (unreachable from validated starts): split along +x
    // and swap the x velocities instead of dividing by zero.
    World world;
    world.placeBall(0, 5000, 5000, 100, 0);
    world.placeBall(1, 5000, 5000, -50, 0);
    world.resolveBalls();

    TEST_ASSERT_EQUAL_INT32(5000 - (kBallContactRaw + kSlopRaw) / 2, world.ball(0).x);
    TEST_ASSERT_EQUAL_INT32(5000 + (kBallContactRaw + kSlopRaw) - (kBallContactRaw + kSlopRaw) / 2,
                            world.ball(1).x);
    TEST_ASSERT_EQUAL_INT32(-50, world.ball(0).vx);
    TEST_ASSERT_EQUAL_INT32(100, world.ball(1).vx);
}

void test_substep_bounces_ball_off_cushion(void) {
    // End to end through the full pipeline: roll down into y=0, bounce, leave
    // with positive velocity (minus one friction tick of 119).
    Table table = singleSegmentTable(-100 * kPxScale, 0, 200, 0);
    World world;
    world.placeBall(0, 0, 1100, 0, -24000);
    world.substep(table);

    TEST_ASSERT_TRUE(world.ball(0).active);
    TEST_ASSERT_EQUAL_INT32(kBallRadiusRaw + kSlopRaw, world.ball(0).y);
    TEST_ASSERT_EQUAL_INT32(24000 - kFrictionPerSubstepRaw, world.ball(0).vy);
}

void test_contact_flags_report_reflections_only(void) {
    // Cushion reflection sets the cushion flag; a second drain reads clear.
    Table table = flatCushion();
    World world;
    world.placeBall(0, 50 * kPxScale, 512, 0, -5000);
    world.resolveCushions(table);

    TEST_ASSERT_TRUE(world.drainCushionHit());
    TEST_ASSERT_FALSE(world.drainCushionHit());
    TEST_ASSERT_FALSE(world.drainBallHit());
}

void test_contact_flags_ignore_separating_contact(void) {
    // Overlapping but receding: pushed out, velocities kept, no flags — a
    // resting ball must not click every substep.
    Table table = flatCushion();
    World world;
    world.placeBall(0, 50 * kPxScale, 512, 0, 2000);
    world.resolveCushions(table);

    TEST_ASSERT_FALSE(world.drainCushionHit());

    World pair;
    pair.placeBall(0, 0, 0, -1000, 0);
    pair.placeBall(1, 2000, 0, 0, 0);
    pair.resolveBalls();

    TEST_ASSERT_FALSE(pair.drainBallHit());
}

void test_contact_flags_report_ball_impact(void) {
    World world;
    world.placeBall(0, 0, 0, 1000, 0);
    world.placeBall(1, 2000, 0, 0, 0);
    world.resolveBalls();

    TEST_ASSERT_TRUE(world.drainBallHit());
    TEST_ASSERT_FALSE(world.drainBallHit());
    TEST_ASSERT_FALSE(world.drainCushionHit());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_cushion_head_on_reflects_exactly);
    RUN_TEST(test_cushion_preserves_tangential_component);
    RUN_TEST(test_cushion_separating_ball_keeps_velocity);
    RUN_TEST(test_cushion_resolve_clears_contact);
    RUN_TEST(test_cushion_vertex_reflects);
    RUN_TEST(test_balls_head_on_transfers_all_velocity);
    RUN_TEST(test_balls_oblique_conserves_momentum_exactly);
    RUN_TEST(test_balls_separating_pair_keeps_velocity);
    RUN_TEST(test_balls_resolve_clears_overlap);
    RUN_TEST(test_balls_coincident_centers_separate_deterministically);
    RUN_TEST(test_substep_bounces_ball_off_cushion);
    RUN_TEST(test_contact_flags_report_reflections_only);
    RUN_TEST(test_contact_flags_ignore_separating_contact);
    RUN_TEST(test_contact_flags_report_ball_impact);
    return UNITY_END();
}
