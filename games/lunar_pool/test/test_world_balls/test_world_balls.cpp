/*
 * Unit tests for ball movement, capture and friction/rest (src/pool/World.*).
 * Covers only this slice's substeps: move(), captureNear(), friction().
 * Ball-ball/cushion resolution (steps 3-4) are added by later slices.
 */
#include <unity.h>

#include <cstdint>

#include "pool/Fixed.h"
#include "pool/Table.h"
#include "pool/World.h"

using namespace pool;

void setUp(void) {}
void tearDown(void) {}

void test_world_move_no_drift_positive_velocity(void) {
    // kSubstepsPerSecond calls to move() is exactly one second, so a
    // constant v=256 raw/s must displace the ball by exactly 256 raw units
    // -- proving the remainder-carry scheme never loses/gains a unit.
    World world;
    world.placeBall(0, 0, 0, 256, 0);
    for (int32_t i = 0; i < kSubstepsPerSecond; ++i) {
        world.move();
    }
    TEST_ASSERT_EQUAL_INT32(256, world.ball(0).x);
}

void test_world_move_no_drift_negative_velocity(void) {
    World world;
    world.placeBall(0, 0, 0, -256, 0);
    for (int32_t i = 0; i < kSubstepsPerSecond; ++i) {
        world.move();
    }
    TEST_ASSERT_EQUAL_INT32(-256, world.ball(0).x);
}

void test_world_capture_radius_is_strict(void) {
    // A ball exactly on the boundary is NOT captured; one raw unit closer IS.
    Table table{};
    table.pockets[0] = Pocket{0, 0};
    table.pocketCount = 1;

    World world;
    world.placeBall(0, kCaptureRadiusRaw, 0, 0, 0);      // dist == radius
    world.placeBall(1, kCaptureRadiusRaw - 1, 0, 0, 0);  // dist < radius
    world.captureNear(table);

    TEST_ASSERT_TRUE(world.ball(0).active);
    TEST_ASSERT_FALSE(world.ball(1).active);
    TEST_ASSERT_EQUAL_UINT8(1, world.pocketLog().count);
    TEST_ASSERT_EQUAL_UINT8(1, world.pocketLog().ball[0]);
}

void test_world_same_substep_captures_ordered_by_ball_number(void) {
    // Ball 5 sits on the FIRST table pocket, ball 2 on the SECOND;
    // captureNear() checks balls index-ascending, so the log reads 2 then
    // 5 -- ordered by ball number, not pocket/table order.
    Table table{};
    table.pockets[0] = Pocket{0, 0};
    table.pockets[1] = Pocket{10000, 10000};
    table.pocketCount = 2;

    World world;
    world.placeBall(5, 0, 0, 0, 0);
    world.placeBall(2, 10000, 10000, 0, 0);
    world.captureNear(table);

    TEST_ASSERT_EQUAL_UINT8(2, world.pocketLog().count);
    TEST_ASSERT_EQUAL_UINT8(2, world.pocketLog().ball[0]);
    TEST_ASSERT_EQUAL_UINT8(5, world.pocketLog().ball[1]);
}

void test_world_captured_ball_stops_and_is_ignored(void) {
    Table table{};
    table.pockets[0] = Pocket{0, 0};
    table.pocketCount = 1;

    // dist=1000 already inside the 1536 radius; 5000/240 truncates to 20,
    // so move() lands it at x=1020 (dist=1020<1536): captured substep 1.
    World world;
    world.placeBall(3, 1000, 0, 5000, 0);
    world.substep(table);
    TEST_ASSERT_FALSE(world.ball(3).active);
    TEST_ASSERT_EQUAL_INT32(1020, world.ball(3).x);
    TEST_ASSERT_EQUAL_INT32(0, world.ball(3).vx);
    TEST_ASSERT_EQUAL_UINT8(1, world.pocketLog().count);

    // A captured ball is fully ignored afterward: no move, no re-log.
    world.substep(table);
    TEST_ASSERT_EQUAL_INT32(1020, world.ball(3).x);
    TEST_ASSERT_EQUAL_UINT8(1, world.pocketLog().count);
}

void test_world_friction_reduces_axis_aligned_speed_exactly(void) {
    // Axis-aligned motion has direction == the single nonzero axis, so
    // divRound(v*119, |v|) == +-119 exactly: no rounding error, no sign
    // flip (119/s <= 1). Checked on both signs.
    World world;
    world.placeBall(0, 0, 0, 1000, 0);
    world.placeBall(1, 0, 0, -1000, 0);
    world.friction();
    TEST_ASSERT_EQUAL_INT32(1000 - kFrictionPerSubstepRaw, world.ball(0).vx);
    TEST_ASSERT_EQUAL_INT32(-1000 + kFrictionPerSubstepRaw, world.ball(1).vx);
}

void test_world_friction_rests_at_or_below_threshold(void) {
    // At threshold, one below, one above (reduces to 1, does NOT rest), and
    // diagonal 84/84: 84^2+84^2=14112 < 119^2=14161, isqrt64(14112)=118 --
    // pins the comparison as squared-magnitude, not a truncated scalar.
    World world;
    world.placeBall(0, 0, 0, kFrictionPerSubstepRaw, 0);
    world.placeBall(1, 0, 0, kFrictionPerSubstepRaw - 1, 0);
    world.placeBall(2, 0, 0, kFrictionPerSubstepRaw + 1, 0);
    world.placeBall(3, 0, 0, 84, 84);
    world.friction();
    TEST_ASSERT_EQUAL_INT32(0, world.ball(0).vx);
    TEST_ASSERT_EQUAL_INT32(0, world.ball(1).vx);
    TEST_ASSERT_EQUAL_INT32(1, world.ball(2).vx);
    TEST_ASSERT_EQUAL_INT32(0, world.ball(3).vx);
    TEST_ASSERT_EQUAL_INT32(0, world.ball(3).vy);
}

void test_world_friction_diagonal_reduces_speed_magnitude_exactly(void) {
    // vx=300,vy=400: a 3-4-5 triangle x100, magnitude exactly 500
    // (300^2+400^2=500^2), so isqrt64 floors nothing -- checked as exact
    // integers, not a float tolerance (no float/double in src/pool/).
    // Each axis loses divRound(axis*119,500): vx: divRound(35700,500)=71
    // (71.4 rounds down); vy: divRound(47600,500)=95 (95.2 rounds down).
    // new vx=300-71=229; new vy=400-95=305. Bound: each axis stays within
    // 0.5 raw units of its ideal share, so the combined 2D error is
    // <=sqrt(0.5^2+0.5^2)~=0.71 raw units (~0.003 px) of the ideal
    // 500-119=381 magnitude; widens only with isqrt64 flooring, which
    // does not apply to this exact-square case.
    World world;
    world.placeBall(0, 0, 0, 300, 400);
    world.friction();
    TEST_ASSERT_EQUAL_INT32(229, world.ball(0).vx);
    TEST_ASSERT_EQUAL_INT32(305, world.ball(0).vy);
}

void test_world_level5_launch_stops_after_exactly_97_frames(void) {
    // Level 5 launch = 5*kSpeedPerLevelRaw = 46,080 raw/s, axis-aligned, so
    // friction removes exactly 119 raw/substep until the remainder is
    // <=119, then zeroes it next substep. 46080/119=387.22..., so 387
    // reductions remove 387*119=46053, leaving 27 (>0,<=119); substep 388
    // zeroes it: ceil(46080/119)=388 substeps to rest. 388/4=97 EXACTLY,
    // so rest lands on frame 97's last substep, not partway into frame 98.
    constexpr int32_t kLevel5Speed = 5 * kSpeedPerLevelRaw;
    constexpr int32_t kExpectedSubsteps = 388;
    static_assert(kExpectedSubsteps % kSubstepsPerFrame == 0, "must land on a frame boundary");

    Table table{};  // No pockets: capture() must never trigger.
    World world;
    world.placeBall(0, 0, 0, kLevel5Speed, 0);
    for (int32_t i = 1; i < kExpectedSubsteps; ++i) {
        world.substep(table);
    }
    TEST_ASSERT_NOT_EQUAL(0, world.ball(0).vx);  // still moving one substep early

    world.substep(table);  // the 388th substep == exactly frame 97.
    TEST_ASSERT_TRUE(world.ball(0).active);
    TEST_ASSERT_EQUAL_INT32(0, world.ball(0).vx);
}

void test_world_place_from_table_sets_positions_number_and_active(void) {
    Table table{};
    table.balls[0] = BallStart{1000, 2000, 0};  // cue
    table.balls[1] = BallStart{3000, 4000, 1};  // target 1
    table.ballCount = 2;

    World world;
    world.placeFromTable(table);
    TEST_ASSERT_EQUAL_INT32(1000, world.ball(0).x);
    TEST_ASSERT_EQUAL_UINT8(0, world.ball(0).number);
    TEST_ASSERT_TRUE(world.ball(0).active);
    TEST_ASSERT_EQUAL_INT32(3000, world.ball(1).x);
    TEST_ASSERT_EQUAL_UINT8(1, world.ball(1).number);
    TEST_ASSERT_EQUAL_UINT8(0, world.pocketLog().count);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_world_move_no_drift_positive_velocity);
    RUN_TEST(test_world_move_no_drift_negative_velocity);
    RUN_TEST(test_world_capture_radius_is_strict);
    RUN_TEST(test_world_same_substep_captures_ordered_by_ball_number);
    RUN_TEST(test_world_captured_ball_stops_and_is_ignored);
    RUN_TEST(test_world_friction_reduces_axis_aligned_speed_exactly);
    RUN_TEST(test_world_friction_rests_at_or_below_threshold);
    RUN_TEST(test_world_friction_diagonal_reduces_speed_magnitude_exactly);
    RUN_TEST(test_world_level5_launch_stops_after_exactly_97_frames);
    RUN_TEST(test_world_place_from_table_sets_positions_number_and_active);
    return UNITY_END();
}
