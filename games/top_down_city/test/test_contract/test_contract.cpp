/**
 * @brief The main mission: a phase the courier run never needed.
 *
 * `mission::` is a target index and a clock, which is all a courier needs:
 * every leg looks like every other, so there is nothing to be in the middle
 * of. A boost is two halves that do not look alike -- find the car, then drive
 * it somewhere -- and the half you are in decides what the world may do to
 * you. That is a state machine, whose failures are all in the transitions
 * rather than in the arithmetic.
 *
 * So most of this file is guards. `begin` comes from an input edge and
 * `boarded`/`delivered` from a collision test, and both can fire twice on the
 * same step -- a phone held down for two frames, a car the player is still
 * standing in. None of those double calls draws anything wrong: a re-granted
 * clock looks like a generous mission, and a chapter skipped by a double
 * delivery looks like content the player never saw.
 *
 * The clock is deliberately not a second balance surface: it is
 * `mission::allowanceSteps` summed over the two legs, and the tests pin it
 * against that function rather than against a number typed twice.
 */
#include <unity.h>

#include <cstdint>

#include "game/rules/Contract.h"
#include "game/rules/Mission.h"
#include "game/rules/Weapon.h"

namespace ct = top_down_city::contract;
namespace ms = top_down_city::mission;
namespace wp = top_down_city::weapons;

void setUp() {}
void tearDown() {}

namespace {

/// A contract parked in the middle of its first job, with the clock running.
ct::State running() {
    ct::State st = ct::clear();
    ct::begin(st, 2, 5, 10, 30);
    return st;
}

/// The same, one step further on: the car is taken and the drop is ahead.
ct::State driving() {
    ct::State st = running();
    ct::boarded(st);
    return st;
}

/// Chapter 2, on the walk to the station with the clock running.
ct::State hunting() {
    ct::State st = driving();
    ct::delivered(st);          // chapter 1 done: the phone rings for the Hit
    ct::beginHit(st, 3, 24);
    return st;
}

/// The same, one step further on: the officer is down and the stars are not
/// yet shed. This is the phase with no clock.
ct::State hiding() {
    ct::State st = hunting();
    ct::struck(st);
    return st;
}

/// Chapter 3, with the rampage clock running and every target still up.
ct::State rampaging() {
    ct::State st = hiding();
    ct::cleaned(st);        // chapter 2 done: the phone rings for the Frenzy
    ct::beginFrenzy(st);
    return st;
}

/// Every chapter delivered, which is the only way to reach `Complete`.
ct::State finished() {
    ct::State st = ct::clear();
    for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(ct::Chapter::Count);
         ++i) {
        ct::begin(st, 0, 0, 4, 4);
        ct::boarded(st);
        ct::delivered(st);
    }
    return st;
}

}  // namespace

// --- The clock -----------------------------------------------------------

void test_a_job_is_one_clock_for_both_legs() {
    // One budget rather than two, so the player decides how to spend it: a
    // long walk to a near car and a short one to a far car are the same job.
    TEST_ASSERT_EQUAL_UINT16(
        static_cast<std::uint16_t>(ms::allowanceSteps(12)
                                   + ms::allowanceSteps(40)),
        ct::jobSteps(12, 40));
}

void test_the_job_clock_is_the_courier_allowance_and_not_a_second_number() {
    // Reusing `mission::allowanceSteps` is the whole point: it is already
    // proven walkable over every distance the map can produce, and a second
    // balance surface is a second thing that can be silently wrong.
    for (int tiles = 0; tiles <= 254; tiles += 7) {
        TEST_ASSERT_TRUE(ct::jobSteps(tiles, 0) >= ms::allowanceSteps(tiles));
        TEST_ASSERT_TRUE(ct::jobSteps(0, tiles) >= ms::allowanceSteps(tiles));
    }
}

void test_a_longer_job_is_never_worth_less_time() {
    std::uint16_t previous = 0;
    for (int tiles = 0; tiles <= 254; ++tiles) {
        const std::uint16_t steps = ct::jobSteps(tiles, tiles);
        TEST_ASSERT_TRUE(steps >= previous);
        previous = steps;
    }
}

void test_a_job_of_no_distance_still_has_a_clock() {
    // The car and the drop can both be near the player. A zero clock would
    // fail the mission on the step the phone granted it.
    TEST_ASSERT_TRUE(ct::jobSteps(0, 0) > 0);
}

void test_a_negative_leg_is_treated_as_none() {
    // The scene subtracts two tile coordinates to get here, exactly as the
    // courier run does. A sign slip should shorten the job, not wrap it.
    TEST_ASSERT_EQUAL_UINT16(ct::jobSteps(0, 20), ct::jobSteps(-40, 20));
    TEST_ASSERT_EQUAL_UINT16(ct::jobSteps(20, 0), ct::jobSteps(20, -40));
    TEST_ASSERT_EQUAL_UINT16(ct::jobSteps(0, 0), ct::jobSteps(-1, -1));
}

void test_the_clamp_is_a_guard_and_not_a_rule() {
    // Two clamped legs summed is past a uint16_t on its own, so the sum is
    // widened before it is clamped. And no job the map can hand out may ever
    // reach the clamp, or the longest drives would all be worth the same.
    TEST_ASSERT_EQUAL_UINT16(ct::kMaxJobSteps, ct::jobSteps(1000000, 1000000));
    TEST_ASSERT_TRUE(ct::jobSteps(254, 254) < ct::kMaxJobSteps);
}

// --- Starting a job ------------------------------------------------------

void test_a_fresh_contract_is_idle_on_the_first_chapter() {
    const ct::State st = ct::clear();
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Chapter::Boost),
                            static_cast<std::uint8_t>(st.chapter));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Idle),
                            static_cast<std::uint8_t>(st.phase));
    TEST_ASSERT_EQUAL_UINT16(0, st.stepsLeft);
    TEST_ASSERT_FALSE(ct::active(st));
    TEST_ASSERT_TRUE(ct::phoneLive(st));
}

void test_answering_the_phone_names_the_car_the_drop_and_the_clock() {
    ct::State st = ct::clear();
    ct::begin(st, 7, 3, 10, 30);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::ToCar),
                            static_cast<std::uint8_t>(st.phase));
    TEST_ASSERT_EQUAL_UINT8(7, st.vehicle);
    TEST_ASSERT_EQUAL_UINT8(3, st.drop);
    TEST_ASSERT_EQUAL_UINT16(ct::jobSteps(10, 30), st.stepsLeft);
    TEST_ASSERT_TRUE(ct::active(st));
    TEST_ASSERT_FALSE(ct::expired(st));
}

void test_the_phone_grants_a_clock_once() {
    // The scene calls this from an input edge, and an edge that fires on two
    // consecutive frames is one missed latch away. A second grant would
    // silently reset the timer -- which draws as a mission nobody can fail.
    ct::State st = running();
    const std::uint16_t granted = st.stepsLeft;
    for (int i = 0; i < 50; ++i) {
        ct::tick(st);
    }
    ct::begin(st, 9, 9, 200, 200);
    TEST_ASSERT_EQUAL_UINT16(granted - 50, st.stepsLeft);
    TEST_ASSERT_EQUAL_UINT8(2, st.vehicle);
    TEST_ASSERT_EQUAL_UINT8(5, st.drop);
}

void test_the_phone_is_dead_once_the_car_is_taken() {
    ct::State st = driving();
    ct::begin(st, 9, 9, 200, 200);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::ToDrop),
                            static_cast<std::uint8_t>(st.phase));
    TEST_ASSERT_EQUAL_UINT8(2, st.vehicle);
}

void test_a_finished_contract_has_no_more_jobs_to_grant() {
    ct::State st = finished();
    ct::begin(st, 1, 1, 10, 10);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Complete),
                            static_cast<std::uint8_t>(st.phase));
    TEST_ASSERT_EQUAL_UINT16(0, st.stepsLeft);
}

// --- Running a job -------------------------------------------------------

void test_the_clock_runs_down_and_expires_exactly_once() {
    ct::State st = ct::clear();
    ct::begin(st, 0, 0, 2, 2);
    const std::uint16_t granted = st.stepsLeft;
    for (std::uint16_t i = 0; i < granted - 1; ++i) {
        ct::tick(st);
        TEST_ASSERT_FALSE(ct::expired(st));
    }
    ct::tick(st);
    TEST_ASSERT_TRUE(ct::expired(st));
}

void test_the_clock_keeps_running_across_the_two_legs() {
    // One clock for the whole job: taking the car neither refills it nor
    // stops it, which is what makes the walk to the car cost something.
    ct::State st = running();
    for (int i = 0; i < 30; ++i) {
        ct::tick(st);
    }
    const std::uint16_t left = st.stepsLeft;
    ct::boarded(st);
    TEST_ASSERT_EQUAL_UINT16(left, st.stepsLeft);
    ct::tick(st);
    TEST_ASSERT_EQUAL_UINT16(left - 1, st.stepsLeft);
}

void test_ticking_an_expired_job_never_underflows() {
    // The scene notices an expiry on the same step and fails the job, but
    // "the scene notices" is not something a uint16_t should depend on.
    ct::State st = ct::clear();
    ct::begin(st, 0, 0, 0, 0);
    for (int i = 0; i < 100000; ++i) {
        ct::tick(st);
    }
    TEST_ASSERT_EQUAL_UINT16(0, st.stepsLeft);
    TEST_ASSERT_TRUE(ct::expired(st));
}

void test_the_clock_only_runs_while_a_job_does() {
    // It is ticked every logic step whether or not anything is live, so an
    // idle contract must not be counting down toward an expiry nobody armed.
    ct::State idle = ct::clear();
    ct::tick(idle);
    TEST_ASSERT_EQUAL_UINT16(0, idle.stepsLeft);
    TEST_ASSERT_FALSE(ct::expired(idle));

    ct::State done = finished();
    ct::tick(done);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Complete),
                            static_cast<std::uint8_t>(done.phase));
    TEST_ASSERT_FALSE(ct::expired(done));
}

void test_a_job_is_active_on_both_legs_and_nowhere_else() {
    TEST_ASSERT_FALSE(ct::active(ct::clear()));
    TEST_ASSERT_TRUE(ct::active(running()));
    TEST_ASSERT_TRUE(ct::active(driving()));
    TEST_ASSERT_FALSE(ct::active(finished()));
}

// --- Taking the car ------------------------------------------------------

void test_boarding_hands_the_job_its_second_leg() {
    ct::State st = running();
    ct::boarded(st);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::ToDrop),
                            static_cast<std::uint8_t>(st.phase));
    TEST_ASSERT_EQUAL_UINT8(5, st.drop);
}

void test_boarding_twice_is_a_no_op() {
    // This comes from a collision test, and a player sitting in the car is
    // colliding with it on every step. Only the first one is a boarding.
    ct::State st = driving();
    const std::uint16_t left = st.stepsLeft;
    ct::boarded(st);
    ct::boarded(st);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::ToDrop),
                            static_cast<std::uint8_t>(st.phase));
    TEST_ASSERT_EQUAL_UINT16(left, st.stepsLeft);
}

void test_boarding_without_a_job_is_a_no_op() {
    // Every car in the city is a car. Touching one with no contract running
    // must not start the second leg of a job that was never granted.
    ct::State idle = ct::clear();
    ct::boarded(idle);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Idle),
                            static_cast<std::uint8_t>(idle.phase));

    ct::State done = finished();
    ct::boarded(done);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Complete),
                            static_cast<std::uint8_t>(done.phase));
}

// --- Delivering ----------------------------------------------------------

void test_delivering_advances_the_chapter_and_relights_the_phone() {
    ct::State st = driving();
    ct::delivered(st);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Chapter::Hit),
                            static_cast<std::uint8_t>(st.chapter));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Idle),
                            static_cast<std::uint8_t>(st.phase));
    TEST_ASSERT_EQUAL_UINT16(0, st.stepsLeft);
    TEST_ASSERT_TRUE(ct::phoneLive(st));
}

void test_delivering_twice_does_not_skip_a_chapter() {
    // The drop is a collision test too, and the player is parked in it.
    // A second delivery would hand out a chapter nobody played.
    ct::State st = driving();
    ct::delivered(st);
    ct::delivered(st);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Chapter::Hit),
                            static_cast<std::uint8_t>(st.chapter));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Idle),
                            static_cast<std::uint8_t>(st.phase));
}

void test_delivering_on_foot_is_a_no_op() {
    // Walking over the drop before the car has been taken is not a delivery:
    // the point of the chapter is the vehicle.
    ct::State st = running();
    ct::delivered(st);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Chapter::Boost),
                            static_cast<std::uint8_t>(st.chapter));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::ToCar),
                            static_cast<std::uint8_t>(st.phase));
}

void test_every_chapter_is_played_before_the_contract_ends() {
    ct::State st = ct::clear();
    for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(ct::Chapter::Count);
         ++i) {
        TEST_ASSERT_EQUAL_UINT8(i, static_cast<std::uint8_t>(st.chapter));
        TEST_ASSERT_TRUE(ct::phoneLive(st));
        ct::begin(st, i, i, 10, 10);
        ct::boarded(st);
        ct::delivered(st);
    }
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Complete),
                            static_cast<std::uint8_t>(st.phase));
}

void test_the_last_delivery_ends_the_contract_and_stays_ended() {
    // `Complete` is a terminal phase rather than a fourth chapter index: the
    // chapter enum is a table lookup on the scene side, so walking it past
    // its last entry is how a state machine becomes a crash.
    ct::State st = finished();
    ct::delivered(st);
    ct::boarded(st);
    ct::failed(st);
    ct::tick(st);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Complete),
                            static_cast<std::uint8_t>(st.phase));
    TEST_ASSERT_TRUE(static_cast<std::uint8_t>(st.chapter)
                     < static_cast<std::uint8_t>(ct::Chapter::Count));
    TEST_ASSERT_FALSE(ct::phoneLive(st));
}

// --- Failing -------------------------------------------------------------

void test_a_failed_job_keeps_its_chapter() {
    // There is no game over in this demo and no menu to send anybody to.
    // The phone is still there, and it is still offering the same chapter.
    ct::State st = driving();
    ct::failed(st);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Chapter::Boost),
                            static_cast<std::uint8_t>(st.chapter));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Idle),
                            static_cast<std::uint8_t>(st.phase));
    TEST_ASSERT_EQUAL_UINT16(0, st.stepsLeft);
    TEST_ASSERT_TRUE(ct::phoneLive(st));
}

void test_a_job_failed_on_foot_keeps_its_chapter_too() {
    ct::State st = running();
    ct::failed(st);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Chapter::Boost),
                            static_cast<std::uint8_t>(st.chapter));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Idle),
                            static_cast<std::uint8_t>(st.phase));
}

void test_a_retried_chapter_is_the_same_chapter() {
    ct::State st = driving();
    ct::failed(st);
    ct::begin(st, 4, 6, 10, 10);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Chapter::Boost),
                            static_cast<std::uint8_t>(st.chapter));
    TEST_ASSERT_EQUAL_UINT8(4, st.vehicle);
    TEST_ASSERT_EQUAL_UINT16(ct::jobSteps(10, 10), st.stepsLeft);
}

void test_failing_outside_a_job_is_a_no_op() {
    // The scene fails a job on an expiry and on a wrecked car, and both can
    // be seen on a step where nothing is running. Neither may end a contract
    // that is already finished.
    ct::State done = finished();
    ct::failed(done);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Complete),
                            static_cast<std::uint8_t>(done.phase));

    ct::State idle = ct::clear();
    ct::failed(idle);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Idle),
                            static_cast<std::uint8_t>(idle.phase));
    TEST_ASSERT_TRUE(ct::phoneLive(idle));
}

// --- The phone -----------------------------------------------------------

void test_the_phone_is_live_only_between_jobs() {
    TEST_ASSERT_TRUE(ct::phoneLive(ct::clear()));
    TEST_ASSERT_FALSE(ct::phoneLive(running()));
    TEST_ASSERT_FALSE(ct::phoneLive(driving()));
    TEST_ASSERT_FALSE(ct::phoneLive(finished()));
}

// --- Offering a chapter --------------------------------------------------

void test_a_chapter_past_the_map_is_not_offered() {
    // The reason this rule exists at all. `Chapter` has three members and the
    // map ships one payphone, so a delivered chapter 1 leaves the contract
    // Idle on a chapter with no row in `CONTRACT_PHONES`, no car and no drop.
    // `phoneLive` says yes to that; this must say no, because the scene reads
    // `CONTRACT_PHONES[chapter]` the moment it does.
    ct::State st = ct::clear();
    st.chapter = ct::Chapter::Frenzy;
    TEST_ASSERT_TRUE(ct::phoneLive(st));
    TEST_ASSERT_FALSE(ct::offered(st, 1));
    TEST_ASSERT_FALSE(ct::offered(st, 2));
    // ...and yes again the moment the map grows the rows to match, which is
    // how chapter 3 gets written: a row in the generator, not a branch here.
    TEST_ASSERT_TRUE(ct::offered(st, 3));
}

void test_the_chapter_at_the_count_is_the_first_one_off_the_end() {
    // The off-by-one this guard is really made of. A count of N owns rows
    // 0..N-1, so chapter N is the subscript that reads past the array -- and
    // an out-of-range read draws as a phone ringing somewhere plausible
    // rather than as a crash, which is why it is tested and not asserted.
    for (std::uint8_t count = 1;
         count <= static_cast<std::uint8_t>(ct::Chapter::Count); ++count) {
        ct::State st = ct::clear();
        st.chapter = static_cast<ct::Chapter>(count - 1u);
        TEST_ASSERT_TRUE(ct::offered(st, count));
        st.chapter = static_cast<ct::Chapter>(count);
        TEST_ASSERT_FALSE(ct::offered(st, count));
    }
}

void test_a_map_with_no_phones_offers_nothing() {
    // Chapter zero is the one a fresh run sits on, and zero is the count a
    // generator that emitted no payphones would hand over. `0 < 0` is false,
    // and it has to be: the alternative is the first frame of the game
    // indexing an empty table. CityConstants.h asserts the count is non-zero
    // at build time; this is the same rule where the scene can reach it.
    const ct::State st = ct::clear();
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Chapter::Boost),
                            static_cast<std::uint8_t>(st.chapter));
    TEST_ASSERT_TRUE(ct::phoneLive(st));
    TEST_ASSERT_FALSE(ct::offered(st, 0));
    TEST_ASSERT_TRUE(ct::offered(st, 1));
}

void test_a_finished_contract_is_never_offered_however_many_phones_there_are() {
    // `Complete` parks the chapter index on the last real entry rather than
    // on `Count`, so the bound alone would say yes to a story that is over.
    // The phase test is the half that keeps the last phone quiet afterwards.
    const ct::State done = finished();
    TEST_ASSERT_TRUE(static_cast<std::uint8_t>(done.chapter)
                     < static_cast<std::uint8_t>(ct::Chapter::Count));
    for (std::uint8_t count = 0; count <= 4; ++count) {
        TEST_ASSERT_FALSE(ct::offered(done, count));
    }
    // And a job in progress is not an offer either: the phone is the thing
    // you answer, not the thing you are already doing.
    TEST_ASSERT_FALSE(ct::offered(running(), 3));
    TEST_ASSERT_FALSE(ct::offered(driving(), 3));
}

// --- Chapter 2: taking the hit -------------------------------------------

void test_the_hit_starts_on_the_walk_to_the_station() {
    ct::State st = driving();
    ct::delivered(st);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Chapter::Hit),
                            static_cast<std::uint8_t>(st.chapter));
    ct::beginHit(st, 3, 24);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::ToTarget),
                            static_cast<std::uint8_t>(st.phase));
    TEST_ASSERT_EQUAL_UINT8(3, st.mark);
    TEST_ASSERT_TRUE(ct::active(st));
    TEST_ASSERT_TRUE(ct::underway(st));
    TEST_ASSERT_FALSE(ct::expired(st));
    TEST_ASSERT_FALSE(ct::phoneLive(st));
}

void test_the_mark_is_not_the_drop() {
    // The one reason `mark` is a field of its own. The Hit's target is an
    // index into the station's officers and the boost's is an index into
    // `MISSION_TARGETS`; overloading the one field would make the marker
    // point at a courier drop the first time the two tables disagree, which
    // is a bug that draws as a perfectly ordinary arrow.
    ct::State st = driving();
    ct::delivered(st);
    ct::beginHit(st, 3, 24);
    TEST_ASSERT_EQUAL_UINT8(3, st.mark);
    TEST_ASSERT_EQUAL_UINT8(5, st.drop);
}

void test_the_walk_to_the_station_is_the_courier_allowance() {
    // Pinned against `mission::allowanceSteps` rather than against a number
    // typed twice, for the same reason `jobSteps` is.
    for (int tiles = 0; tiles <= 254; tiles += 11) {
        ct::State st = driving();
        ct::delivered(st);
        ct::beginHit(st, 0, tiles);
        TEST_ASSERT_EQUAL_UINT16(ms::allowanceSteps(tiles), st.stepsLeft);
    }
}

void test_a_walk_of_no_distance_still_has_a_clock() {
    ct::State st = driving();
    ct::delivered(st);
    ct::beginHit(st, 0, 0);
    TEST_ASSERT_TRUE(st.stepsLeft > 0);
    TEST_ASSERT_FALSE(ct::expired(st));
}

void test_a_negative_walk_to_the_station_is_treated_as_none() {
    // The scene subtracts two tile coordinates to get here, exactly as the
    // courier run and the boost do. A sign slip shortens the walk, never
    // wraps it into the longest clock in the game.
    ct::State near = driving();
    ct::delivered(near);
    ct::beginHit(near, 0, 0);

    ct::State back = driving();
    ct::delivered(back);
    ct::beginHit(back, 0, -40);
    TEST_ASSERT_EQUAL_UINT16(near.stepsLeft, back.stepsLeft);
}

void test_the_hit_is_offered_on_its_own_chapter_and_no_other() {
    // The chapter guard, which is the half `phase == Idle` cannot cover. Both
    // phones are Idle-and-live from the state machine's point of view, so
    // without this the scene's boost payphone could start the Hit -- a
    // chapter played out of order, on a marker pointing at an officer index
    // that chapter 1 never named.
    ct::State first = ct::clear();
    TEST_ASSERT_TRUE(ct::phoneLive(first));
    ct::beginHit(first, 3, 24);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Idle),
                            static_cast<std::uint8_t>(first.phase));
    TEST_ASSERT_EQUAL_UINT16(0, first.stepsLeft);

    ct::State third = ct::clear();
    third.chapter = ct::Chapter::Frenzy;
    ct::beginHit(third, 3, 24);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Idle),
                            static_cast<std::uint8_t>(third.phase));

    ct::State done = finished();
    ct::beginHit(done, 3, 24);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Complete),
                            static_cast<std::uint8_t>(done.phase));
}

void test_the_hit_phone_grants_a_clock_once() {
    // Same input edge, same latch, same silent failure as chapter 1: a second
    // grant reads as a generous mission rather than as a bug.
    ct::State st = hunting();
    const std::uint16_t granted = st.stepsLeft;
    for (int i = 0; i < 40; ++i) {
        ct::tick(st);
    }
    ct::beginHit(st, 9, 200);
    TEST_ASSERT_EQUAL_UINT16(granted - 40, st.stepsLeft);
    TEST_ASSERT_EQUAL_UINT8(3, st.mark);
}

void test_the_hit_cannot_be_started_on_top_of_the_boost() {
    // `begin` and `beginHit` are two phones and one state machine. Neither
    // may reach into a job the other is running.
    ct::State onFoot = running();
    ct::beginHit(onFoot, 9, 24);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::ToCar),
                            static_cast<std::uint8_t>(onFoot.phase));

    ct::State inCar = driving();
    ct::beginHit(inCar, 9, 24);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::ToDrop),
                            static_cast<std::uint8_t>(inCar.phase));
}

void test_the_boost_cannot_be_started_on_top_of_the_hit() {
    ct::State walking = hunting();
    const std::uint16_t left = walking.stepsLeft;
    ct::begin(walking, 9, 9, 200, 200);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::ToTarget),
                            static_cast<std::uint8_t>(walking.phase));
    TEST_ASSERT_EQUAL_UINT16(left, walking.stepsLeft);

    ct::State clean = hiding();
    ct::begin(clean, 9, 9, 200, 200);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Struck),
                            static_cast<std::uint8_t>(clean.phase));
}

// --- Putting the officer down --------------------------------------------

void test_the_officer_going_down_ends_the_walk() {
    ct::State st = hunting();
    ct::struck(st);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Struck),
                            static_cast<std::uint8_t>(st.phase));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Chapter::Hit),
                            static_cast<std::uint8_t>(st.chapter));
    TEST_ASSERT_FALSE(ct::phoneLive(st));
}

void test_striking_twice_is_a_no_op() {
    // This arrives from a damage test, and an officer already on the ground
    // can be shot again -- by the player, or by the next pellet of the same
    // shotgun spread on the same step.
    ct::State st = hiding();
    ct::struck(st);
    ct::struck(st);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Struck),
                            static_cast<std::uint8_t>(st.phase));
}

void test_striking_outside_the_hit_is_a_no_op() {
    // Every officer in the city is an officer, and the player can shoot one
    // at any point in the run. Only the marked one, during the Hit, is a hit.
    ct::State idle = ct::clear();
    ct::struck(idle);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Idle),
                            static_cast<std::uint8_t>(idle.phase));

    ct::State inCar = driving();
    ct::struck(inCar);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::ToDrop),
                            static_cast<std::uint8_t>(inCar.phase));

    ct::State done = finished();
    ct::struck(done);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Complete),
                            static_cast<std::uint8_t>(done.phase));
}

void test_the_clock_stops_meaning_anything_once_the_officer_is_down() {
    // Getting clean is untimed: it already carries five stars at six seconds
    // each, and only while nobody has eyes on the player. A second clock over
    // the same beat is two pressures on one moment with no way to tell which
    // one killed you. `Struck` is simply not `active`, which is how the clock
    // stops without a second flag to keep in step with the phase.
    ct::State st = hiding();
    TEST_ASSERT_FALSE(ct::active(st));
    const std::uint16_t left = st.stepsLeft;
    for (int i = 0; i < 100000; ++i) {
        ct::tick(st);
    }
    TEST_ASSERT_EQUAL_UINT16(left, st.stepsLeft);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Struck),
                            static_cast<std::uint8_t>(st.phase));
}

void test_a_struck_contract_never_expires() {
    // Including the case that would otherwise be a coin flip: the officer
    // going down on the very step the walk's clock ran out. An expiry here
    // would fail the chapter for a kill the player had already made.
    ct::State st = hunting();
    while (!ct::expired(st)) {
        ct::tick(st);
    }
    TEST_ASSERT_EQUAL_UINT16(0, st.stepsLeft);
    ct::struck(st);
    TEST_ASSERT_FALSE(ct::expired(st));
    ct::tick(st);
    TEST_ASSERT_FALSE(ct::expired(st));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Struck),
                            static_cast<std::uint8_t>(st.phase));
}

// --- A chapter under way, clock or no clock ------------------------------

void test_a_chapter_is_under_way_from_the_phone_to_getting_clean() {
    // Deliberately NOT `active`. The scene freezes the courier leg and hides
    // the courier ring for as long as the main mission owns the player, and
    // that ownership outlasts the clock by exactly one phase: between the
    // kill and getting clean there is no countdown, but there is very much a
    // chapter in progress. Asking `active` there would relight the courier
    // run mid-hit, over a player with five stars on them.
    TEST_ASSERT_FALSE(ct::underway(ct::clear()));
    TEST_ASSERT_TRUE(ct::underway(running()));
    TEST_ASSERT_TRUE(ct::underway(driving()));
    TEST_ASSERT_TRUE(ct::underway(hunting()));
    TEST_ASSERT_TRUE(ct::underway(hiding()));
    TEST_ASSERT_FALSE(ct::underway(finished()));
}

void test_the_only_gap_between_under_way_and_active_is_the_getting_clean() {
    // Pinned as a difference rather than as two lists, so a phase added later
    // cannot quietly fall out of one of them.
    ct::State st = hiding();
    TEST_ASSERT_TRUE(ct::underway(st));
    TEST_ASSERT_FALSE(ct::active(st));

    const ct::State phases[] = {ct::clear(), running(), driving(), hunting(),
                                rampaging(), finished()};
    for (const ct::State& each : phases) {
        TEST_ASSERT_EQUAL(ct::active(each), ct::underway(each));
    }
}

void test_a_job_is_active_on_every_leg_that_has_a_clock() {
    // The widened version of the chapter-1 claim. `ToTarget` joins the two
    // driving legs; `Struck` deliberately does not.
    TEST_ASSERT_TRUE(ct::active(hunting()));
    TEST_ASSERT_FALSE(ct::active(hiding()));
}

// --- Getting clean -------------------------------------------------------

void test_getting_clean_ends_the_chapter_and_relights_the_phone() {
    ct::State st = hiding();
    ct::cleaned(st);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Chapter::Frenzy),
                            static_cast<std::uint8_t>(st.chapter));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Idle),
                            static_cast<std::uint8_t>(st.phase));
    TEST_ASSERT_EQUAL_UINT16(0, st.stepsLeft);
    TEST_ASSERT_TRUE(ct::phoneLive(st));
    TEST_ASSERT_FALSE(ct::underway(st));
}

void test_getting_clean_twice_does_not_skip_a_chapter() {
    // The scene calls this on the step the wanted level reaches zero, and
    // zero is a level the player then stays at. Every step after the first is
    // the same zero.
    ct::State st = hiding();
    ct::cleaned(st);
    ct::cleaned(st);
    ct::cleaned(st);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Chapter::Frenzy),
                            static_cast<std::uint8_t>(st.chapter));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Idle),
                            static_cast<std::uint8_t>(st.phase));
}

void test_getting_clean_before_the_kill_is_a_no_op() {
    // A player who walks to the station with no stars on them is already
    // clean, and the scene's zero-stars test is true on every one of those
    // steps. Getting clean is only an ending for a chapter that has a body in
    // it.
    ct::State st = hunting();
    ct::cleaned(st);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Chapter::Hit),
                            static_cast<std::uint8_t>(st.chapter));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::ToTarget),
                            static_cast<std::uint8_t>(st.phase));

    ct::State idle = ct::clear();
    ct::cleaned(idle);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Chapter::Boost),
                            static_cast<std::uint8_t>(idle.chapter));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Idle),
                            static_cast<std::uint8_t>(idle.phase));

    ct::State inCar = driving();
    ct::cleaned(inCar);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::ToDrop),
                            static_cast<std::uint8_t>(inCar.phase));

    ct::State done = finished();
    ct::cleaned(done);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Complete),
                            static_cast<std::uint8_t>(done.phase));
}

void test_getting_clean_on_the_last_chapter_ends_the_contract() {
    // `cleaned` and `delivered` share one advancement, so they share the
    // ending too: the same terminal `Complete` the last delivery gets.
    ct::State st = hiding();
    st.chapter = ct::Chapter::Frenzy;
    ct::cleaned(st);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Complete),
                            static_cast<std::uint8_t>(st.phase));
    TEST_ASSERT_TRUE(static_cast<std::uint8_t>(st.chapter)
                     < static_cast<std::uint8_t>(ct::Chapter::Count));
    TEST_ASSERT_FALSE(ct::phoneLive(st));
    TEST_ASSERT_FALSE(ct::underway(st));
}

// --- Failing the hit -----------------------------------------------------

void test_an_arrest_between_the_kill_and_getting_clean_ends_the_job() {
    // The reason `failed` is gated on `underway` rather than on `active`.
    // `Struck` is the phase with no clock, so nothing else can end it -- and
    // an arrest that left the chapter sitting in `Struck` would leave the
    // player owing a clean-up for a mission they had already lost, with no
    // phone to answer and no marker to follow.
    ct::State st = hiding();
    ct::failed(st);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Chapter::Hit),
                            static_cast<std::uint8_t>(st.chapter));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Idle),
                            static_cast<std::uint8_t>(st.phase));
    TEST_ASSERT_EQUAL_UINT16(0, st.stepsLeft);
    TEST_ASSERT_TRUE(ct::phoneLive(st));
}

void test_a_hit_failed_on_the_walk_keeps_its_chapter_too() {
    ct::State st = hunting();
    ct::failed(st);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Chapter::Hit),
                            static_cast<std::uint8_t>(st.chapter));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Idle),
                            static_cast<std::uint8_t>(st.phase));
}

void test_a_retried_hit_is_the_same_chapter() {
    ct::State st = hiding();
    ct::failed(st);
    ct::beginHit(st, 6, 30);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Chapter::Hit),
                            static_cast<std::uint8_t>(st.chapter));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::ToTarget),
                            static_cast<std::uint8_t>(st.phase));
    TEST_ASSERT_EQUAL_UINT8(6, st.mark);
    TEST_ASSERT_EQUAL_UINT16(ms::allowanceSteps(30), st.stepsLeft);
}

void test_a_fresh_contract_has_no_mark() {
    const ct::State st = ct::clear();
    TEST_ASSERT_EQUAL_UINT8(0, st.mark);
}

// --- Chapter 3: the rampage ----------------------------------------------

void test_a_rampage_is_a_window_per_target_and_not_a_typed_number() {
    // The first clock here that is not a distance: there is nowhere to walk
    // to, so `mission::allowanceSteps` has no argument to take. One window per
    // body rather than a picked duration makes the length of the chapter and
    // the size of it the SAME number -- a rampage retuned to fewer targets is
    // automatically shorter, with no second constant to forget.
    for (std::uint8_t targets = 0; targets <= 40; ++targets) {
        TEST_ASSERT_EQUAL_UINT16(
            static_cast<std::uint16_t>(targets * ct::kFrenzyStepsPerTarget),
            ct::frenzySteps(targets));
    }
}

void test_a_rampage_of_no_targets_is_no_time() {
    // Not reachable through `beginFrenzy`, which always asks for
    // `kFrenzyTargets`. Pinned anyway: the multiplication is the only place a
    // zero could turn into a full clock over a chapter with nothing in it.
    TEST_ASSERT_EQUAL_UINT16(0, ct::frenzySteps(0));
}

void test_a_bigger_rampage_is_never_worth_less_time() {
    std::uint16_t previous = 0;
    for (int targets = 0; targets <= 255; ++targets) {
        const std::uint16_t steps =
            ct::frenzySteps(static_cast<std::uint8_t>(targets));
        TEST_ASSERT_TRUE(steps >= previous);
        previous = steps;
    }
}

void test_the_shipped_rampage_never_reaches_the_clamp() {
    // The clamp in `frenzySteps` is a guard against a uint8_t times a
    // uint16_t overflowing, not a design decision. If the shipped chapter
    // ever came near it the clock would stop growing with the target count,
    // and the two would silently stop meaning the same thing.
    TEST_ASSERT_TRUE(ct::frenzySteps(ct::kFrenzyTargets) < ct::kMaxJobSteps);
}

void test_a_rampage_costs_more_than_the_pistol_holds() {
    // The whole argument for this chapter. Ten rounds are not enough, so the
    // chapter is really "go and buy the shotgun" -- and it is a SOFT gate:
    // nothing refuses the phone, the arithmetic just does not work out. Drop
    // the count to the magazine size and the last chapter stops teaching the
    // one thing the finite magazine was built to teach.
    TEST_ASSERT_TRUE(ct::kFrenzyTargets > wp::kPistolMagazine);
}

void test_a_rampage_fits_inside_one_shotgun_magazine() {
    // The other half of the same bound, and it is what keeps the soft gate
    // honest. The gun the chapter sends the player to buy has to be a gun
    // that finishes it -- with every shell counting, but without a second
    // trip to the counter in the middle of a five-star manhunt.
    TEST_ASSERT_TRUE(ct::kFrenzyTargets <= wp::kShotgunMagazine);
}

void test_the_frenzy_is_offered_on_its_own_chapter_and_no_other() {
    // The same guard the Hit has, for the same reason: every live phone is
    // Idle as far as the state machine can see, so without the chapter test
    // chapter 1's phone would start the rampage.
    ct::State early = ct::clear();
    ct::beginFrenzy(early);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Idle),
                            static_cast<std::uint8_t>(early.phase));
    TEST_ASSERT_EQUAL_UINT16(0, early.stepsLeft);
    TEST_ASSERT_EQUAL_UINT8(0, early.targetsLeft);
}

void test_the_frenzy_phone_grants_a_clock_and_a_count_once() {
    ct::State st = rampaging();
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Rampage),
                            static_cast<std::uint8_t>(st.phase));
    TEST_ASSERT_EQUAL_UINT8(ct::kFrenzyTargets, st.targetsLeft);
    TEST_ASSERT_EQUAL_UINT16(ct::frenzySteps(ct::kFrenzyTargets),
                             st.stepsLeft);

    // A RUN edge that latches twice, and this one has a second counter to
    // re-grant on top of the clock: a rampage that hands its bodies back is a
    // chapter nobody can finish rather than one nobody can fail.
    for (int i = 0; i < 8; ++i) {
        ct::tick(st);
    }
    ct::culled(st);
    const std::uint16_t left = st.stepsLeft;
    ct::beginFrenzy(st);
    TEST_ASSERT_EQUAL_UINT16(left, st.stepsLeft);
    TEST_ASSERT_EQUAL_UINT8(ct::kFrenzyTargets - 1, st.targetsLeft);
}

void test_the_boost_cannot_be_started_on_top_of_the_frenzy() {
    // `begin` is guarded on the phase alone -- it is chapter 1's arm and
    // carries no chapter test -- so the rampage's phase is the only thing
    // standing between a held RUN button and a boost clock dropped over a
    // running rampage.
    ct::State st = rampaging();
    const std::uint16_t left = st.stepsLeft;
    ct::begin(st, 1, 1, 40, 40);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Rampage),
                            static_cast<std::uint8_t>(st.phase));
    TEST_ASSERT_EQUAL_UINT16(left, st.stepsLeft);
}

void test_a_body_takes_one_off_the_count_and_nothing_off_the_clock() {
    ct::State st = rampaging();
    const std::uint16_t left = st.stepsLeft;
    ct::culled(st);
    TEST_ASSERT_EQUAL_UINT8(ct::kFrenzyTargets - 1, st.targetsLeft);
    TEST_ASSERT_EQUAL_UINT16(left, st.stepsLeft);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Rampage),
                            static_cast<std::uint8_t>(st.phase));
}

void test_bodies_outside_the_rampage_are_not_counted() {
    // This arrives from the one place a death is reported, which is every
    // space and every step of the run: the crowd under a car, an officer in
    // the station lobby during chapter 2, a bystander shot on the way to a
    // courier drop. Only a body during the rampage is a target.
    const ct::State before[] = {ct::clear(), running(), driving(), hunting(),
                                hiding(), finished()};
    for (const ct::State& each : before) {
        ct::State st = each;
        const ct::Phase phase = st.phase;
        ct::culled(st);
        TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(phase),
                                static_cast<std::uint8_t>(st.phase));
        TEST_ASSERT_EQUAL_UINT8(0, st.targetsLeft);
    }
}

void test_the_last_body_ends_the_contract() {
    // The Frenzy is the last chapter, so its ending is the story's. Shared
    // with `delivered` and `cleaned` through one advancement, so the three
    // endings cannot drift apart.
    ct::State st = rampaging();
    for (std::uint8_t i = 0; i < ct::kFrenzyTargets; ++i) {
        TEST_ASSERT_TRUE(ct::underway(st));
        ct::culled(st);
    }
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Complete),
                            static_cast<std::uint8_t>(st.phase));
    TEST_ASSERT_TRUE(static_cast<std::uint8_t>(st.chapter)
                     < static_cast<std::uint8_t>(ct::Chapter::Count));
    TEST_ASSERT_EQUAL_UINT16(0, st.stepsLeft);
    TEST_ASSERT_FALSE(ct::phoneLive(st));
    TEST_ASSERT_FALSE(ct::underway(st));
}

void test_counting_past_the_last_body_does_not_walk_the_chapter_off_the_end() {
    // A shotgun spread is three pellets on one step and the crowd is packed
    // during a rampage: the last two can land together. The second of them
    // must not advance a chapter that has already ended.
    ct::State st = rampaging();
    for (int i = 0; i < static_cast<int>(ct::kFrenzyTargets) + 6; ++i) {
        ct::culled(st);
    }
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Complete),
                            static_cast<std::uint8_t>(st.phase));
    TEST_ASSERT_TRUE(static_cast<std::uint8_t>(st.chapter)
                     < static_cast<std::uint8_t>(ct::Chapter::Count));
}

void test_a_rampage_runs_on_a_clock_and_expires_exactly_once() {
    ct::State st = rampaging();
    const std::uint16_t granted = st.stepsLeft;
    for (std::uint16_t i = 0; i < granted - 1; ++i) {
        ct::tick(st);
        TEST_ASSERT_FALSE(ct::expired(st));
    }
    ct::tick(st);
    TEST_ASSERT_TRUE(ct::expired(st));
}

void test_a_rampage_is_active_but_has_nowhere_to_point() {
    // The distinction chapter 3 adds, and it is a third widening rather than
    // a new list: a destination, a clock, a chapter. The rampage has a clock
    // and no destination -- the objective is a count, and a cyan ring over
    // the corner the player is standing on would be pointing at nothing they
    // have left to do.
    ct::State st = rampaging();
    TEST_ASSERT_TRUE(ct::underway(st));
    TEST_ASSERT_TRUE(ct::active(st));
    TEST_ASSERT_FALSE(ct::hasDestination(st));
}

void test_every_phase_with_a_destination_has_a_clock_and_every_clock_a_chapter() {
    // The three predicates pinned as NESTED rather than as three lists, which
    // is the only thing that keeps a phase added later from quietly falling
    // out of the middle one.
    const ct::State phases[] = {ct::clear(), running(), driving(), hunting(),
                                hiding(), rampaging(), finished()};
    for (const ct::State& each : phases) {
        if (ct::hasDestination(each)) {
            TEST_ASSERT_TRUE(ct::active(each));
        }
        if (ct::active(each)) {
            TEST_ASSERT_TRUE(ct::underway(each));
        }
    }
    // And each widening is a real one: something sits in each gap.
    TEST_ASSERT_TRUE(ct::active(rampaging()));
    TEST_ASSERT_FALSE(ct::hasDestination(rampaging()));
    TEST_ASSERT_TRUE(ct::underway(hiding()));
    TEST_ASSERT_FALSE(ct::active(hiding()));
}

void test_a_failed_rampage_keeps_its_chapter_and_forgets_its_count() {
    // Arrested mid-rampage, or simply too slow. The chapter stays where it
    // was -- the phone rings again with the same job -- and the count goes
    // with the clock: a residue left behind would be a HUD reading four
    // bodies owed on a chapter nobody has started.
    ct::State st = rampaging();
    ct::culled(st);
    ct::culled(st);
    ct::failed(st);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Chapter::Frenzy),
                            static_cast<std::uint8_t>(st.chapter));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ct::Phase::Idle),
                            static_cast<std::uint8_t>(st.phase));
    TEST_ASSERT_EQUAL_UINT16(0, st.stepsLeft);
    TEST_ASSERT_EQUAL_UINT8(0, st.targetsLeft);
    TEST_ASSERT_TRUE(ct::phoneLive(st));
}

void test_a_retried_rampage_gets_its_full_count_back() {
    ct::State st = rampaging();
    ct::culled(st);
    ct::culled(st);
    ct::failed(st);
    ct::beginFrenzy(st);
    TEST_ASSERT_EQUAL_UINT8(ct::kFrenzyTargets, st.targetsLeft);
    TEST_ASSERT_EQUAL_UINT16(ct::frenzySteps(ct::kFrenzyTargets),
                             st.stepsLeft);
}

void test_a_fresh_contract_has_no_targets() {
    TEST_ASSERT_EQUAL_UINT8(0, ct::clear().targetsLeft);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_a_job_is_one_clock_for_both_legs);
    RUN_TEST(test_the_job_clock_is_the_courier_allowance_and_not_a_second_number);
    RUN_TEST(test_a_longer_job_is_never_worth_less_time);
    RUN_TEST(test_a_job_of_no_distance_still_has_a_clock);
    RUN_TEST(test_a_negative_leg_is_treated_as_none);
    RUN_TEST(test_the_clamp_is_a_guard_and_not_a_rule);
    RUN_TEST(test_a_fresh_contract_is_idle_on_the_first_chapter);
    RUN_TEST(test_answering_the_phone_names_the_car_the_drop_and_the_clock);
    RUN_TEST(test_the_phone_grants_a_clock_once);
    RUN_TEST(test_the_phone_is_dead_once_the_car_is_taken);
    RUN_TEST(test_a_finished_contract_has_no_more_jobs_to_grant);
    RUN_TEST(test_the_clock_runs_down_and_expires_exactly_once);
    RUN_TEST(test_the_clock_keeps_running_across_the_two_legs);
    RUN_TEST(test_ticking_an_expired_job_never_underflows);
    RUN_TEST(test_the_clock_only_runs_while_a_job_does);
    RUN_TEST(test_a_job_is_active_on_both_legs_and_nowhere_else);
    RUN_TEST(test_boarding_hands_the_job_its_second_leg);
    RUN_TEST(test_boarding_twice_is_a_no_op);
    RUN_TEST(test_boarding_without_a_job_is_a_no_op);
    RUN_TEST(test_delivering_advances_the_chapter_and_relights_the_phone);
    RUN_TEST(test_delivering_twice_does_not_skip_a_chapter);
    RUN_TEST(test_delivering_on_foot_is_a_no_op);
    RUN_TEST(test_every_chapter_is_played_before_the_contract_ends);
    RUN_TEST(test_the_last_delivery_ends_the_contract_and_stays_ended);
    RUN_TEST(test_a_failed_job_keeps_its_chapter);
    RUN_TEST(test_a_job_failed_on_foot_keeps_its_chapter_too);
    RUN_TEST(test_a_retried_chapter_is_the_same_chapter);
    RUN_TEST(test_failing_outside_a_job_is_a_no_op);
    RUN_TEST(test_the_phone_is_live_only_between_jobs);
    RUN_TEST(test_a_chapter_past_the_map_is_not_offered);
    RUN_TEST(test_the_chapter_at_the_count_is_the_first_one_off_the_end);
    RUN_TEST(test_a_map_with_no_phones_offers_nothing);
    RUN_TEST(test_a_finished_contract_is_never_offered_however_many_phones_there_are);
    RUN_TEST(test_the_hit_starts_on_the_walk_to_the_station);
    RUN_TEST(test_the_mark_is_not_the_drop);
    RUN_TEST(test_the_walk_to_the_station_is_the_courier_allowance);
    RUN_TEST(test_a_walk_of_no_distance_still_has_a_clock);
    RUN_TEST(test_a_negative_walk_to_the_station_is_treated_as_none);
    RUN_TEST(test_the_hit_is_offered_on_its_own_chapter_and_no_other);
    RUN_TEST(test_the_hit_phone_grants_a_clock_once);
    RUN_TEST(test_the_hit_cannot_be_started_on_top_of_the_boost);
    RUN_TEST(test_the_boost_cannot_be_started_on_top_of_the_hit);
    RUN_TEST(test_the_officer_going_down_ends_the_walk);
    RUN_TEST(test_striking_twice_is_a_no_op);
    RUN_TEST(test_striking_outside_the_hit_is_a_no_op);
    RUN_TEST(test_the_clock_stops_meaning_anything_once_the_officer_is_down);
    RUN_TEST(test_a_struck_contract_never_expires);
    RUN_TEST(test_a_chapter_is_under_way_from_the_phone_to_getting_clean);
    RUN_TEST(test_the_only_gap_between_under_way_and_active_is_the_getting_clean);
    RUN_TEST(test_a_job_is_active_on_every_leg_that_has_a_clock);
    RUN_TEST(test_getting_clean_ends_the_chapter_and_relights_the_phone);
    RUN_TEST(test_getting_clean_twice_does_not_skip_a_chapter);
    RUN_TEST(test_getting_clean_before_the_kill_is_a_no_op);
    RUN_TEST(test_getting_clean_on_the_last_chapter_ends_the_contract);
    RUN_TEST(test_an_arrest_between_the_kill_and_getting_clean_ends_the_job);
    RUN_TEST(test_a_hit_failed_on_the_walk_keeps_its_chapter_too);
    RUN_TEST(test_a_retried_hit_is_the_same_chapter);
    RUN_TEST(test_a_fresh_contract_has_no_mark);
    RUN_TEST(test_a_rampage_is_a_window_per_target_and_not_a_typed_number);
    RUN_TEST(test_a_rampage_of_no_targets_is_no_time);
    RUN_TEST(test_a_bigger_rampage_is_never_worth_less_time);
    RUN_TEST(test_the_shipped_rampage_never_reaches_the_clamp);
    RUN_TEST(test_a_rampage_costs_more_than_the_pistol_holds);
    RUN_TEST(test_a_rampage_fits_inside_one_shotgun_magazine);
    RUN_TEST(test_the_frenzy_is_offered_on_its_own_chapter_and_no_other);
    RUN_TEST(test_the_frenzy_phone_grants_a_clock_and_a_count_once);
    RUN_TEST(test_the_boost_cannot_be_started_on_top_of_the_frenzy);
    RUN_TEST(test_a_body_takes_one_off_the_count_and_nothing_off_the_clock);
    RUN_TEST(test_bodies_outside_the_rampage_are_not_counted);
    RUN_TEST(test_the_last_body_ends_the_contract);
    RUN_TEST(test_counting_past_the_last_body_does_not_walk_the_chapter_off_the_end);
    RUN_TEST(test_a_rampage_runs_on_a_clock_and_expires_exactly_once);
    RUN_TEST(test_a_rampage_is_active_but_has_nowhere_to_point);
    RUN_TEST(test_every_phase_with_a_destination_has_a_clock_and_every_clock_a_chapter);
    RUN_TEST(test_a_failed_rampage_keeps_its_chapter_and_forgets_its_count);
    RUN_TEST(test_a_retried_rampage_gets_its_full_count_back);
    RUN_TEST(test_a_fresh_contract_has_no_targets);
    return UNITY_END();
}
