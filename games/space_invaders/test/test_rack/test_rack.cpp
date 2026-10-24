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
#include "rules/Scoring.h"

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

void test_rack_origin_y_for_wave_keeps_wave_one_at_the_rack_origin(void) {
    // Wave 1 is the reference start of S4; the per-wave drop is relative to it.
    TEST_ASSERT_EQUAL_INT(RackOriginY, static_cast<int>(rackOriginYForWave(1)));
}

void test_rack_origin_y_drops_eight_pixels_per_wave(void) {
    // S12: each wave restarts the formation one edge-drop lower than the last,
    // so the wave offset accumulates exactly like the drop at an edge (S5).
    TEST_ASSERT_EQUAL_INT(RackDropY, RackWaveDropY);
    TEST_ASSERT_EQUAL_INT(RackOriginY + RackWaveDropY,
                          static_cast<int>(rackOriginYForWave(2)));
    TEST_ASSERT_EQUAL_INT(RackOriginY + 2 * RackWaveDropY,
                          static_cast<int>(rackOriginYForWave(3)));
    TEST_ASSERT_EQUAL_INT(RackOriginY + 3 * RackWaveDropY,
                          static_cast<int>(rackOriginYForWave(4)));
}

void test_rack_origin_y_never_drops_past_the_playable_field(void) {
    // Wave 0 is a number Scoring can never report (its counter is 1-based), so
    // it resolves to the first wave instead of raising the formation.
    TEST_ASSERT_EQUAL_INT(RackOriginY, static_cast<int>(rackOriginYForWave(0)));

    // The drop saturates at the deepest formation the field can hold: a wave
    // starting any lower would already have invaded on its first tick, so no
    // run reaches it and the origin must not walk off the panel.
    TEST_ASSERT_EQUAL_INT(RackMaxOriginY, static_cast<int>(rackOriginYForWave(1000)));
    TEST_ASSERT_EQUAL_INT(RackMaxOriginY, static_cast<int>(rackOriginYForWave(65535)));

    int16_t previous = rackOriginYForWave(1);
    for (uint16_t wave = 2; wave < 200; ++wave) {
        const int16_t origin = rackOriginYForWave(wave);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(RackOriginY, static_cast<int>(origin));
        TEST_ASSERT_LESS_OR_EQUAL_INT(RackMaxOriginY, static_cast<int>(origin));
        TEST_ASSERT_GREATER_OR_EQUAL_INT(static_cast<int>(previous), static_cast<int>(origin));
        previous = origin;
    }
}

void test_racking_a_cleared_wave_respawns_the_formation_one_drop_lower(void) {
    // The wave advance the scene performs (S12): the counter moves, the rack is
    // spawned at the derived origin, and the run's score and ships carry over.
    // This pins the parts of that path that live in src/rules/; the actor
    // mirroring, the shield reset and the shot/saucer clears are scene state and
    // are covered by the scene, not here.
    Rack rack;
    Scoring scoring;
    rack.spawn(RackOriginX, rackOriginYForWave(scoring.wave()));
    const int16_t waveOneTop = rack.cell(0, 0).y;

    for (int row = 0; row < RackRows; ++row) {
        for (int col = 0; col < RackCols; ++col) {
            rack.kill(row, col);
        }
    }
    TEST_ASSERT_FALSE(rack.anyAlive());

    scoring.add(1234);
    scoring.nextWave();
    rack.spawn(RackOriginX, rackOriginYForWave(scoring.wave()));

    TEST_ASSERT_EQUAL_INT(2, scoring.wave());
    TEST_ASSERT_EQUAL_INT(1234, scoring.score());
    TEST_ASSERT_EQUAL_INT(StartLives, scoring.lives());

    // The whole formation is back, at the start of the sweep and one drop lower.
    TEST_ASSERT_EQUAL_INT(55, rack.aliveCount());
    TEST_ASSERT_EQUAL_INT(0, rack.animationFrame());
    TEST_ASSERT_EQUAL_INT(1, rack.direction());
    TEST_ASSERT_EQUAL_INT(waveOneTop + RackWaveDropY, static_cast<int>(rack.cell(0, 0).y));
    TEST_ASSERT_EQUAL_INT(120 + RackWaveDropY, static_cast<int>(rack.lowestAlienBottom()));

    // spawn() clears the explosion hold, so the new wave marches on its first tick.
    TEST_ASSERT_TRUE(rack.tick().movedAlien);
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
    RUN_TEST(test_rack_origin_y_for_wave_keeps_wave_one_at_the_rack_origin);
    RUN_TEST(test_rack_origin_y_drops_eight_pixels_per_wave);
    RUN_TEST(test_rack_origin_y_never_drops_past_the_playable_field);
    RUN_TEST(test_racking_a_cleared_wave_respawns_the_formation_one_drop_lower);

    return UNITY_END();
}
