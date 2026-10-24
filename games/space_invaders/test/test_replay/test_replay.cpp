/*
 * Determinism replay (S20, T6.1).
 *
 * Rng alone already proves its own sequence repeats, but that says nothing
 * about a run: the scene's rules only reproduce if the real objects, driven in
 * a fixed order, produce the same columns, sides and march every time. This
 * suite drives Rack, EnemyFireTimer, the three enemy-shot slots, Saucer and
 * Scoring through their public APIs for a fixed script of ticks and kills, then
 * folds what they did into a small transcript.
 *
 * The transcript is deliberately not a per-tick dump. A checksum catches any
 * drift anywhere in the run, while a handful of samples make a failure readable
 * and pin the values the script is supposed to reach. A constant run cannot
 * pass: the samples must differ from each other, the saucer sides must alternate
 * and a one-field perturbation must be rejected by the comparison itself.
 *
 * The suite is engine-free like every other host_test suite: integer state only,
 * no clock, no allocation beyond the stack, and the seed is a literal mirrored
 * from GameConstants.h so this layer never includes a scene header.
 */
#include <unity.h>

#include "rules/FireTiming.h"
#include "rules/Rack.h"
#include "rules/Rng.h"
#include "rules/RulesConstants.h"
#include "rules/Saucer.h"
#include "rules/Scoring.h"
#include "rules/Shots.h"

using namespace spaceinvaders::rules;

namespace {

/// GameConstants.h RUN_SEED. Repeated as a literal because the rules layer must
/// not include the scene's header just to learn one constant.
constexpr uint32_t SceneRunSeed = 0x1978u;
/// Long enough for two saucer periods (2 * 1536) plus the crossing tail.
constexpr int RunTicks = 3300;

/// One scripted kill every KillPeriodTicks, starting after the first sweep.
constexpr int KillFirstTick = 40;
constexpr int KillPeriodTicks = 50;
constexpr int ScriptedKillCount = 30;
/// One scripted player shot every 100 ticks; the bolt leaves in 59, so a slot
/// is always free on the next script point and the shot counter really advances.
constexpr int PlayerFireFirstTick = 10;
constexpr int PlayerFirePeriodTicks = 100;
constexpr int PlayerShotStartY = 232;

constexpr int StartSampleTick = 1;
constexpr int MidSampleTick = 1000;

/// FNV-1a over 32-bit words; integer-only so every host folds the same bits.
uint32_t mix(uint32_t hash, uint32_t value) {
    hash ^= value;
    hash *= 16777619u;
    return hash;
}

/// Compact record of a scripted run: one checksum plus readable samples.
struct Transcript {
    uint32_t stateChecksum = 0;        ///< Every observed tick, folded together.
    uint32_t columnChecksum = 0;       ///< One mix per enemy shot's live column.
    uint16_t aliveAtStart = 0;         ///< Live aliens on the first tick.
    uint16_t aliveAtMid = 0;           ///< Live aliens after 20 scripted kills.
    uint16_t aliveAtEnd = 0;           ///< Live aliens when the run ends.
    int16_t rackLeftAtEnd = 0;         ///< Leftmost live cell x at the end.
    int16_t rackBottomAtEnd = 0;       ///< Lowest live alien bottom at the end.
    int8_t directionAtEnd = 0;         ///< +1 right, -1 left.
    int animationFrameAtEnd = 0;       ///< 0 or 1, flipped per completed sweep.
    uint16_t finalScore = 0;
    uint16_t highScore = 0;
    uint8_t lives = 0;
    uint16_t enemyShotsFired = 0;
    uint16_t maxActiveEnemyShots = 0;  ///< Peak occupancy of the three slots.
    uint16_t enemyShotsRefused = 0;    ///< Fires with no free slot (cap reached).
    uint8_t saucerSpawns = 0;
    int8_t saucerSides[2] = {0, 0};
    uint16_t saucerAwards[2] = {0, 0};
};

bool sameTranscript(const Transcript& a, const Transcript& b) {
    if (a.stateChecksum != b.stateChecksum || a.columnChecksum != b.columnChecksum) {
        return false;
    }
    if (a.aliveAtStart != b.aliveAtStart || a.aliveAtMid != b.aliveAtMid ||
        a.aliveAtEnd != b.aliveAtEnd) {
        return false;
    }
    if (a.rackLeftAtEnd != b.rackLeftAtEnd || a.rackBottomAtEnd != b.rackBottomAtEnd ||
        a.directionAtEnd != b.directionAtEnd ||
        a.animationFrameAtEnd != b.animationFrameAtEnd) {
        return false;
    }
    if (a.finalScore != b.finalScore || a.highScore != b.highScore ||
        a.lives != b.lives) {
        return false;
    }
    if (a.enemyShotsFired != b.enemyShotsFired ||
        a.maxActiveEnemyShots != b.maxActiveEnemyShots ||
        a.enemyShotsRefused != b.enemyShotsRefused) {
        return false;
    }
    if (a.saucerSpawns != b.saucerSpawns) {
        return false;
    }
    for (int i = 0; i < 2; ++i) {
        if (a.saucerSides[i] != b.saucerSides[i] ||
            a.saucerAwards[i] != b.saucerAwards[i]) {
            return false;
        }
    }
    return true;
}

/// One framed enemy bolt: the rules own the descent, the test owns the slot.
struct EnemySlot {
    EnemyShotMotion motion;
    int16_t columnX = 0;
    bool active = false;
};

/**
 * Drives the real rules objects through the scene's tick order.
 *
 * The tick order mirrors SpaceInvadersScene::update: player shot, rack sweep,
 * the collision pass (here a scripted kill), the three-tick enemy step, the
 * saucer, then scoring. The script stands in for the scene's collision test on
 * purpose: this suite proves the rules replay, not the scene's hit geometry.
 *
 * @param seed Initial Rng state; the scene reseeds to RUN_SEED per run.
 * @return The folded transcript of the run.
 */
Transcript runReplay(uint32_t seed) {
    Rng rng(seed);
    Rack rack;
    Saucer saucer;
    EnemyFireTimer enemyFire;
    Scoring scoring;
    EnemySlot slots[EnemyShotMax];

    bool playerShotActive = false;
    int16_t playerShotY = 0;
    uint32_t shotsFired = 0;
    int enemyStepTick = 0;
    // The scene starts with the timer armed for band 0, and calls reset() again
    // whenever the score high byte changes into a tighter reload band.
    uint16_t enemyFireBand = 0;

    Transcript transcript{};
    uint16_t maxActive = 0;

    rack.spawn(RackOriginX, RackOriginY);
    saucer.reset();
    enemyFire.reset(enemyFireBand);
    scoring.reset();

    int killsApplied = 0;

    for (int tickIndex = StartSampleTick; tickIndex <= RunTicks; ++tickIndex) {
        // S8: the player bolt moves every tick and leaves the field on its own.
        if (playerShotActive) {
            playerShotY = advancePlayerShot(playerShotY);
            if (playerShotY < 0) {
                playerShotActive = false;
            }
        }

        // The input script: fire only when a bolt is not already in flight, the
        // same one-alive rule the scene enforces before it counts a shot (S11).
        if (tickIndex >= PlayerFireFirstTick &&
            (tickIndex - PlayerFireFirstTick) % PlayerFirePeriodTicks == 0 &&
            !playerShotActive) {
            playerShotActive = true;
            playerShotY = static_cast<int16_t>(PlayerShotStartY);
            ++shotsFired;
        }

        // S4-S6: exactly one live alien moves per tick.
        rack.tick();

        // The scripted collision pass. Kill, hold the formation for the
        // explosion and write the award, exactly as the scene's hit path does.
        if (killsApplied < ScriptedKillCount &&
            tickIndex == KillFirstTick + killsApplied * KillPeriodTicks) {
            const int row = RackRows - 1 - (killsApplied % RackRows);
            const int col = killsApplied / RackRows;
            if (rack.cell(row, col).alive) {
                rack.kill(row, col);
                rack.hold(RackHoldTicks);
                scoring.add(static_cast<uint16_t>(alienScore(rack.cell(row, col).type)));
                scoring.commitHighScore();
                scoring.consumeExtraShip();
            }
            ++killsApplied;
        }

        // S9: the scene owns the three-tick clock; one step moves every bolt.
        if (++enemyStepTick >= EnemyShotTicksPerStep) {
            enemyStepTick = 0;
            const bool fast = enemyShotIsFast(static_cast<uint8_t>(rack.aliveCount()));

            for (int slot = 0; slot < EnemyShotMax; ++slot) {
                if (!slots[slot].active) {
                    continue;
                }
                slots[slot].motion.step(fast);
                if (slots[slot].motion.y() > FieldHeight) {
                    slots[slot].active = false;
                }
            }

            const uint16_t band = scoring.scoreBand();
            if (enemyReloadTicks(band) != enemyReloadTicks(enemyFireBand)) {
                enemyFireBand = band;
                enemyFire.reset(band);
            }

            if (enemyFire.step()) {
                // Live columns are rebuilt from the rack every shot, and the
                // draw is the run's own generator: this is the value S20 says
                // must reproduce with the seed.
                uint8_t liveColumns[RackCols];
                int columnCount = 0;
                for (int col = 0; col < RackCols; ++col) {
                    for (int row = RackRows - 1; row >= 0; --row) {
                        if (rack.cell(row, col).alive) {
                            liveColumns[columnCount++] = static_cast<uint8_t>(col);
                            break;
                        }
                    }
                }

                if (columnCount > 0) {
                    const int picked =
                        static_cast<int>(rng.below(static_cast<uint32_t>(columnCount)));
                    const uint8_t column = liveColumns[picked];
                    transcript.columnChecksum = mix(transcript.columnChecksum, column);

                    int16_t spawnY = 0;
                    for (int row = RackRows - 1; row >= 0; --row) {
                        if (rack.cell(row, column).alive) {
                            spawnY = static_cast<int16_t>(
                                rack.cell(row, column).y + alienHeight(rack.cell(row, column).type));
                            break;
                        }
                    }

                    bool placed = false;
                    for (int slot = 0; slot < EnemyShotMax; ++slot) {
                        if (slots[slot].active) {
                            continue;
                        }
                        slots[slot].columnX = rack.cell(0, column).x;
                        slots[slot].motion.reset(spawnY);
                        slots[slot].active = true;
                        placed = true;
                        ++transcript.enemyShotsFired;
                        break;
                    }
                    // The cap is an invariant, not a dropped shot: the timer
                    // keeps charging on every step, but a fourth bolt cannot
                    // exist (S9).
                    if (!placed) {
                        ++transcript.enemyShotsRefused;
                    }
                }
            }

            uint16_t activeNow = 0;
            for (int slot = 0; slot < EnemyShotMax; ++slot) {
                if (slots[slot].active) {
                    ++activeNow;
                }
            }
            if (activeNow > maxActive) {
                maxActive = activeNow;
            }
        }

        // S11: period, then the eight-alien gate; the side is the parity of the
        // player shot count and the award is the next table entry.
        saucer.tick();
        if (saucer.spawnDue(static_cast<uint8_t>(rack.aliveCount()))) {
            const int8_t side = saucer.side(shotsFired);
            // The scripted run awards every saucer it spawns, so the table is
            // exercised in ROM order; a real hit test belongs to the scene.
            const uint16_t award = saucer.takeScore();
            if (transcript.saucerSpawns < 2) {
                transcript.saucerSides[transcript.saucerSpawns] = side;
                transcript.saucerAwards[transcript.saucerSpawns] = award;
            }
            ++transcript.saucerSpawns;
            scoring.add(award);
            scoring.commitHighScore();
            scoring.consumeExtraShip();
            saucer.onSpawned();
        }

        if (tickIndex == StartSampleTick) {
            transcript.aliveAtStart = static_cast<uint16_t>(rack.aliveCount());
        }
        if (tickIndex == MidSampleTick) {
            transcript.aliveAtMid = static_cast<uint16_t>(rack.aliveCount());
        }

        // Fold everything a later tick can build on. A checksum over the whole
        // run catches drift even when the sampled ticks happen to agree.
        transcript.stateChecksum = mix(transcript.stateChecksum,
                                       static_cast<uint32_t>(rack.aliveCount()));
        transcript.stateChecksum = mix(transcript.stateChecksum,
                                       static_cast<uint32_t>(rack.animationFrame()));
        transcript.stateChecksum =
            mix(transcript.stateChecksum,
                static_cast<uint32_t>(static_cast<uint16_t>(rack.direction())));
        transcript.stateChecksum =
            mix(transcript.stateChecksum,
                static_cast<uint32_t>(static_cast<uint16_t>(rack.lowestAlienBottom())));
        transcript.stateChecksum = mix(transcript.stateChecksum, saucer.timer());
        transcript.stateChecksum = mix(transcript.stateChecksum, scoring.score());
        transcript.stateChecksum =
            mix(transcript.stateChecksum,
                playerShotActive ? static_cast<uint32_t>(static_cast<uint16_t>(playerShotY))
                                 : 0xFFFFu);
        for (int slot = 0; slot < EnemyShotMax; ++slot) {
            const uint32_t packed =
                slots[slot].active
                    ? (static_cast<uint32_t>(static_cast<uint16_t>(slots[slot].motion.y())) |
                       (static_cast<uint32_t>(static_cast<uint16_t>(slots[slot].columnX)) << 16))
                    : 0xFFFFFFFFu;
            transcript.stateChecksum = mix(transcript.stateChecksum, packed);
        }
    }

    transcript.aliveAtEnd = static_cast<uint16_t>(rack.aliveCount());
    transcript.rackBottomAtEnd = rack.lowestAlienBottom();
    transcript.directionAtEnd = static_cast<int8_t>(rack.direction());
    transcript.animationFrameAtEnd = rack.animationFrame();

    int16_t leftMost = 0;
    bool haveLive = false;
    for (int row = 0; row < RackRows; ++row) {
        for (int col = 0; col < RackCols; ++col) {
            const RackCell& cell = rack.cell(row, col);
            if (!cell.alive) {
                continue;
            }
            if (!haveLive || cell.x < leftMost) {
                leftMost = cell.x;
            }
            haveLive = true;
        }
    }
    transcript.rackLeftAtEnd = leftMost;

    transcript.finalScore = scoring.score();
    transcript.highScore = scoring.highScore();
    transcript.lives = scoring.lives();
    transcript.maxActiveEnemyShots = maxActive;

    return transcript;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_replay_same_seed_reproduces_the_same_transcript(void) {
    const Transcript first = runReplay(SceneRunSeed);
    const Transcript second = runReplay(SceneRunSeed);

    TEST_ASSERT_TRUE(sameTranscript(first, second));
}

void test_replay_different_seed_changes_the_shot_columns(void) {
    const Transcript reference = runReplay(SceneRunSeed);
    const Transcript other = runReplay(SceneRunSeed + 1u);

    // The seed only feeds the column draw; every other rule must stay put. That
    // makes the column fingerprint the honest proof that the seed matters.
    TEST_ASSERT_NOT_EQUAL(reference.columnChecksum, other.columnChecksum);
    TEST_ASSERT_FALSE(sameTranscript(reference, other));
    TEST_ASSERT_EQUAL_UINT16(reference.aliveAtEnd, other.aliveAtEnd);
    TEST_ASSERT_EQUAL_UINT16(reference.finalScore, other.finalScore);
    TEST_ASSERT_EQUAL_UINT16(reference.enemyShotsFired, other.enemyShotsFired);
}

void test_replay_scripted_run_matches_the_recorded_expectations(void) {
    const Transcript run = runReplay(SceneRunSeed);

    // The script is fixed, so these are recorded facts, not "whatever happened".
    TEST_ASSERT_EQUAL_UINT16(55, run.aliveAtStart);
    TEST_ASSERT_EQUAL_UINT16(35, run.aliveAtMid);
    TEST_ASSERT_EQUAL_UINT16(25, run.aliveAtEnd);

    // 30 kills (six full columns: 90 points each) plus the two saucer awards.
    TEST_ASSERT_EQUAL_UINT16(690, run.finalScore);
    TEST_ASSERT_EQUAL_UINT16(690, run.highScore);
    TEST_ASSERT_EQUAL_UINT8(3, run.lives);

    TEST_ASSERT_EQUAL_UINT8(2, run.saucerSpawns);
    TEST_ASSERT_EQUAL_INT8(1, run.saucerSides[0]);
    TEST_ASSERT_EQUAL_INT8(-1, run.saucerSides[1]);
    TEST_ASSERT_EQUAL_UINT16(100, run.saucerAwards[0]);
    TEST_ASSERT_EQUAL_UINT16(50, run.saucerAwards[1]);

    // The fire ladder really fired, and the three-slot cap stayed a cap.
    TEST_ASSERT_GREATER_THAN_UINT16(20, run.enemyShotsFired);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(2, run.maxActiveEnemyShots);
    TEST_ASSERT_LESS_OR_EQUAL_INT(EnemyShotMax, run.maxActiveEnemyShots);
    TEST_ASSERT_EQUAL_UINT16(0, run.enemyShotsRefused);
}

void test_replay_transcript_is_not_constant(void) {
    const Transcript run = runReplay(SceneRunSeed);

    // A generator or a hash stubbed to one value would satisfy "same seed, same
    // transcript" trivially, so the run itself must visibly change over time.
    TEST_ASSERT_NOT_EQUAL(0u, run.stateChecksum);
    TEST_ASSERT_NOT_EQUAL(run.aliveAtStart, run.aliveAtMid);
    TEST_ASSERT_NOT_EQUAL(run.aliveAtMid, run.aliveAtEnd);
    TEST_ASSERT_NOT_EQUAL(0u, run.columnChecksum);
    // Parity of the shot counter flips between the two entries: the sides are a
    // property of the script, and a constant side would mean the parity is lost.
    TEST_ASSERT_NOT_EQUAL(run.saucerSides[0], run.saucerSides[1]);

    // The comparison itself must reject a one-field drift, or every assertion
    // above would pass on a constant equality function.
    Transcript perturbed = run;
    perturbed.aliveAtMid = static_cast<uint16_t>(perturbed.aliveAtMid + 1u);
    TEST_ASSERT_FALSE(sameTranscript(run, perturbed));

    perturbed = run;
    perturbed.columnChecksum ^= 0x1u;
    TEST_ASSERT_FALSE(sameTranscript(run, perturbed));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_replay_same_seed_reproduces_the_same_transcript);
    RUN_TEST(test_replay_different_seed_changes_the_shot_columns);
    RUN_TEST(test_replay_scripted_run_matches_the_recorded_expectations);
    RUN_TEST(test_replay_transcript_is_not_constant);

    return UNITY_END();
}
