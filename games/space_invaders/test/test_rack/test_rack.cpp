/*
 * RED suite for the rack reference numbers (S4-S6).
 *
 * Every assertion is a number the arcade reaches and the demo does not: 55
 * cells, one cell per tick, a sweep that takes the live count, 3 px for the
 * last alien, an 8 px drop on the edge. The stubs in src/rules/ return zero,
 * so this file has to fail until T1.3 implements the sweep.
 */
#include <unity.h>

#include "rules/Rack.h"

using namespace spaceinvaders::rules;

namespace {

struct Snapshot {
    int16_t x[RackRows][RackCols];
    int16_t y[RackRows][RackCols];
};

void takeSnapshot(const Rack& rack, Snapshot& snapshot) {
    for (int row = 0; row < RackRows; ++row) {
        for (int col = 0; col < RackCols; ++col) {
            snapshot.x[row][col] = rack.cell(row, col).x;
            snapshot.y[row][col] = rack.cell(row, col).y;
        }
    }
}

int movedCells(const Rack& rack, const Snapshot& before) {
    int moved = 0;
    for (int row = 0; row < RackRows; ++row) {
        for (int col = 0; col < RackCols; ++col) {
            const RackCell& current = rack.cell(row, col);
            if (current.x != before.x[row][col] || current.y != before.y[row][col]) {
                ++moved;
            }
        }
    }
    return moved;
}

void assertLiveCellsInsideBounds(const Rack& rack) {
    for (int row = 0; row < RackRows; ++row) {
        for (int col = 0; col < RackCols; ++col) {
            const RackCell& current = rack.cell(row, col);
            if (!current.alive) {
                continue;
            }
            TEST_ASSERT_GREATER_OR_EQUAL_INT(RackMinLeft, static_cast<int>(current.x));
            TEST_ASSERT_LESS_OR_EQUAL_INT(RackMaxRight, static_cast<int>(current.x));
        }
    }
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_rack_spawn_fills_fifty_five_cells(void) {
    Rack rack;
    rack.spawn(RackOriginX, RackOriginY);

    TEST_ASSERT_EQUAL_INT(55, rack.aliveCount());
    TEST_ASSERT_TRUE(rack.anyAlive());

    for (int row = 0; row < RackRows; ++row) {
        for (int col = 0; col < RackCols; ++col) {
            TEST_ASSERT_TRUE(rack.cell(row, col).alive);
        }
    }
}

void test_rack_spawn_assigns_types_per_row(void) {
    Rack rack;
    rack.spawn(RackOriginX, RackOriginY);

    for (int col = 0; col < RackCols; ++col) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(AlienType::Squid),
                              static_cast<int>(rack.cell(0, col).type));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(AlienType::Crab),
                              static_cast<int>(rack.cell(1, col).type));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(AlienType::Crab),
                              static_cast<int>(rack.cell(2, col).type));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(AlienType::Octopus),
                              static_cast<int>(rack.cell(3, col).type));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(AlienType::Octopus),
                              static_cast<int>(rack.cell(4, col).type));
    }
}

void test_rack_spawn_places_first_cell(void) {
    Rack rack;
    rack.spawn(RackOriginX, RackOriginY);

    TEST_ASSERT_EQUAL_INT(RackOriginX, static_cast<int>(rack.cell(0, 0).x));
    TEST_ASSERT_EQUAL_INT(RackOriginY, static_cast<int>(rack.cell(0, 0).y));
}

void test_rack_spawn_places_last_cell(void) {
    Rack rack;
    rack.spawn(RackOriginX, RackOriginY);

    TEST_ASSERT_EQUAL_INT(186, static_cast<int>(rack.cell(4, 10).x));
    TEST_ASSERT_EQUAL_INT(112, static_cast<int>(rack.cell(4, 10).y));
}

void test_rack_tick_moves_exactly_one_alien(void) {
    Rack rack;
    rack.spawn(RackOriginX, RackOriginY);
    Snapshot before;
    takeSnapshot(rack, before);

    const RackTick result = rack.tick();

    TEST_ASSERT_TRUE(result.movedAlien);
    TEST_ASSERT_EQUAL_INT(1, movedCells(rack, before));
    TEST_ASSERT_FALSE(result.sweepCompleted);
}

void test_rack_sweep_takes_fifty_five_ticks_and_flips_frame(void) {
    Rack rack;
    rack.spawn(RackOriginX, RackOriginY);
    TEST_ASSERT_EQUAL_INT(0, rack.animationFrame());

    for (int tickIndex = 0; tickIndex < 55; ++tickIndex) {
        const RackTick result = rack.tick();
        TEST_ASSERT_TRUE(result.movedAlien);
        if (tickIndex < 54) {
            TEST_ASSERT_FALSE(result.sweepCompleted);
            TEST_ASSERT_FALSE(result.frameFlipped);
        } else {
            TEST_ASSERT_TRUE(result.sweepCompleted);
            TEST_ASSERT_TRUE(result.frameFlipped);
        }
    }

    TEST_ASSERT_EQUAL_INT(1, rack.animationFrame());
}

void test_rack_sweep_advances_every_cell_two_pixels(void) {
    Rack rack;
    rack.spawn(RackOriginX, RackOriginY);
    Snapshot before;
    takeSnapshot(rack, before);

    for (int tickIndex = 0; tickIndex < 55; ++tickIndex) {
        rack.tick();
    }

    for (int row = 0; row < RackRows; ++row) {
        for (int col = 0; col < RackCols; ++col) {
            TEST_ASSERT_EQUAL_INT(before.x[row][col] + RackStepX,
                                  static_cast<int>(rack.cell(row, col).x));
            TEST_ASSERT_EQUAL_INT(before.y[row][col],
                                  static_cast<int>(rack.cell(row, col).y));
        }
    }
}

void test_rack_last_alien_advances_three_pixels(void) {
    Rack rack;
    rack.spawn(RackOriginX, RackOriginY);

    for (int row = 0; row < RackRows; ++row) {
        for (int col = 0; col < RackCols; ++col) {
            if (row != 0 || col != 0) {
                rack.kill(row, col);
            }
        }
    }
    TEST_ASSERT_EQUAL_INT(1, rack.aliveCount());

    const int16_t before = rack.cell(0, 0).x;
    rack.tick();

    TEST_ASSERT_EQUAL_INT(before + RackLastStepX,
                          static_cast<int>(rack.cell(0, 0).x));
}

void test_rack_edge_turn_drops_eight_and_reverses(void) {
    Rack rack;
    rack.spawn(RackOriginX, RackOriginY);
    const int directionBefore = rack.direction();
    TEST_ASSERT_EQUAL_INT(1, directionBefore);

    Snapshot beforeTurn;
    takeSnapshot(rack, beforeTurn);

    bool turned = false;
    for (int tickIndex = 0; tickIndex < 1200 && !turned; ++tickIndex) {
        takeSnapshot(rack, beforeTurn);
        const RackTick result = rack.tick();
        assertLiveCellsInsideBounds(rack);
        if (result.turnedAtEdge) {
            turned = true;
        }
    }

    TEST_ASSERT_TRUE(turned);
    TEST_ASSERT_EQUAL_INT(-directionBefore, rack.direction());

    for (int row = 0; row < RackRows; ++row) {
        for (int col = 0; col < RackCols; ++col) {
            if (!rack.cell(row, col).alive) {
                continue;
            }
            TEST_ASSERT_EQUAL_INT(beforeTurn.y[row][col] + RackDropY,
                                  static_cast<int>(rack.cell(row, col).y));
        }
    }
}

void test_rack_hold_freezes_exactly_ten_ticks(void) {
    Rack rack;
    rack.spawn(RackOriginX, RackOriginY);
    Snapshot frozen;
    takeSnapshot(rack, frozen);

    rack.hold(10);
    for (int tickIndex = 0; tickIndex < 10; ++tickIndex) {
        const RackTick result = rack.tick();
        TEST_ASSERT_FALSE(result.movedAlien);
        TEST_ASSERT_EQUAL_INT(0, movedCells(rack, frozen));
    }

    const RackTick afterHold = rack.tick();
    TEST_ASSERT_TRUE(afterHold.movedAlien);
    TEST_ASSERT_EQUAL_INT(1, movedCells(rack, frozen));
    TEST_ASSERT_EQUAL_INT(frozen.x[0][0] + RackStepX,
                          static_cast<int>(rack.cell(0, 0).x));
}

void test_rack_kill_reduces_alive_count(void) {
    Rack rack;
    rack.spawn(RackOriginX, RackOriginY);

    rack.kill(0, 0);
    TEST_ASSERT_EQUAL_INT(54, rack.aliveCount());
    TEST_ASSERT_FALSE(rack.cell(0, 0).alive);

    rack.kill(4, 10);
    TEST_ASSERT_EQUAL_INT(53, rack.aliveCount());
    TEST_ASSERT_FALSE(rack.cell(4, 10).alive);

    rack.kill(0, 0);
    TEST_ASSERT_EQUAL_INT(53, rack.aliveCount());
}

void test_rack_shift_down_moves_formation(void) {
    Rack rack;
    rack.spawn(RackOriginX, RackOriginY);
    Snapshot before;
    takeSnapshot(rack, before);

    rack.shiftDown(RackDropY);

    for (int row = 0; row < RackRows; ++row) {
        for (int col = 0; col < RackCols; ++col) {
            TEST_ASSERT_EQUAL_INT(before.y[row][col] + RackDropY,
                                  static_cast<int>(rack.cell(row, col).y));
            TEST_ASSERT_EQUAL_INT(before.x[row][col],
                                  static_cast<int>(rack.cell(row, col).x));
        }
    }
}

void test_rack_lowest_alien_bottom_tracks_row_four(void) {
    Rack rack;
    rack.spawn(RackOriginX, RackOriginY);

    // Row 4 starts at y=112 and every alien is 8 px tall: bottom 120.
    TEST_ASSERT_EQUAL_INT(120, static_cast<int>(rack.lowestAlienBottom()));

    for (int col = 0; col < RackCols; ++col) {
        rack.kill(4, col);
    }

    // Row 3 starts at y=96, so the crab bottom becomes 104.
    TEST_ASSERT_EQUAL_INT(104, static_cast<int>(rack.lowestAlienBottom()));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_rack_spawn_fills_fifty_five_cells);
    RUN_TEST(test_rack_spawn_assigns_types_per_row);
    RUN_TEST(test_rack_spawn_places_first_cell);
    RUN_TEST(test_rack_spawn_places_last_cell);
    RUN_TEST(test_rack_tick_moves_exactly_one_alien);
    RUN_TEST(test_rack_sweep_takes_fifty_five_ticks_and_flips_frame);
    RUN_TEST(test_rack_sweep_advances_every_cell_two_pixels);
    RUN_TEST(test_rack_last_alien_advances_three_pixels);
    RUN_TEST(test_rack_edge_turn_drops_eight_and_reverses);
    RUN_TEST(test_rack_hold_freezes_exactly_ten_ticks);
    RUN_TEST(test_rack_kill_reduces_alive_count);
    RUN_TEST(test_rack_shift_down_moves_formation);
    RUN_TEST(test_rack_lowest_alien_bottom_tracks_row_four);

    return UNITY_END();
}
