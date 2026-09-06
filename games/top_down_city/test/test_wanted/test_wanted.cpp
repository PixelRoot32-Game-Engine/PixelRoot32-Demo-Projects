/**
 * @brief How much trouble the player is in, and how fast it goes away.
 *
 * A star counter looks trivial and its failures are all silent: a level that
 * never rises is a city that does not care what you do, one that never falls
 * is a city you can never get out of, and one that saturates at the wrong end
 * is either free or hopeless -- all three read as the mechanic being missing
 * rather than wrong.
 *
 * The two that matter most are the pair at the bottom: a table indexed by
 * stars must be safe at every star count the rest of the demo can produce,
 * and "one in N officers" with N of zero is either a divide by zero or a
 * street that never sees a policeman again.
 */
#include <unity.h>

#include <cstdint>

#include "game/rules/Wanted.h"
#include "game/rules/Weapon.h"

namespace wn = top_down_city::wanted;

void setUp() {}
void tearDown() {}

/// Named rather than a bare `false` at fifteen call sites. `tick(st, false)`
/// reads as "do not tick", which is the opposite of what it means.
constexpr bool kUnseen = false;
constexpr bool kWatched = true;

// --- Raising it ----------------------------------------------------------

void test_a_clean_slate_is_not_wanted() {
    wn::State st = wn::clear();
    TEST_ASSERT_EQUAL_UINT8(0, st.stars);
    TEST_ASSERT_FALSE(wn::isWanted(st));
}

void test_a_crime_puts_you_at_least_at_its_floor() {
    for (std::uint8_t c = 0; c < static_cast<std::uint8_t>(wn::Crime::Count);
         ++c) {
        const wn::Crime crime = static_cast<wn::Crime>(c);
        wn::State st = wn::clear();
        wn::report(st, crime);
        TEST_ASSERT_TRUE(wn::isWanted(st));
        TEST_ASSERT_TRUE(st.stars >= wn::penalty(crime).floor);
    }
}

void test_repeating_a_crime_escalates() {
    // Hurting somebody twice is worse than hurting them once. Without the
    // bump the level would pin at the floor and every crime after the first
    // would be free.
    wn::State st = wn::clear();
    wn::report(st, wn::Crime::CivilianHurt);
    const std::uint8_t first = st.stars;
    wn::report(st, wn::Crime::CivilianHurt);
    TEST_ASSERT_TRUE(st.stars > first);
}

void test_firing_at_nothing_never_escalates() {
    // A shot fired gets you noticed and no more. Otherwise emptying a
    // magazine into a wall would bring the whole force down on you, and the
    // player would learn not to touch the trigger.
    wn::State st = wn::clear();
    for (int i = 0; i < 50; ++i) {
        wn::report(st, wn::Crime::ShotFired);
    }
    TEST_ASSERT_EQUAL_UINT8(wn::penalty(wn::Crime::ShotFired).floor, st.stars);
}

void test_a_lesser_crime_never_lowers_the_level() {
    // The floor is a floor, not a level. Firing a shot while wanted for
    // killing an officer must not talk the police down.
    wn::State st = wn::clear();
    wn::report(st, wn::Crime::OfficerKilled);
    wn::report(st, wn::Crime::OfficerKilled);
    const std::uint8_t high = st.stars;
    wn::report(st, wn::Crime::ShotFired);
    TEST_ASSERT_TRUE(st.stars >= high);
}

void test_the_level_saturates_at_the_maximum() {
    wn::State st = wn::clear();
    for (int i = 0; i < 100; ++i) {
        wn::report(st, wn::Crime::OfficerKilled);
    }
    TEST_ASSERT_EQUAL_UINT8(wn::kMaxStars, st.stars);
}

// --- Losing it -----------------------------------------------------------

void test_cooling_off_takes_one_period_per_star() {
    wn::State st = wn::clear();
    wn::report(st, wn::Crime::CivilianHurt);
    const std::uint8_t stars = st.stars;
    TEST_ASSERT_TRUE(stars > 0);

    // One step short of the whole descent: still wanted.
    for (int i = 0; i < stars * wn::kCoolSteps - 1; ++i) {
        wn::tick(st, kUnseen);
    }
    TEST_ASSERT_TRUE(wn::isWanted(st));
    wn::tick(st, kUnseen);
    TEST_ASSERT_FALSE(wn::isWanted(st));
}

void test_a_star_falls_off_exactly_on_time() {
    wn::State st = wn::clear();
    wn::report(st, wn::Crime::OfficerKilled);
    const std::uint8_t stars = st.stars;
    for (int i = 0; i < wn::kCoolSteps - 1; ++i) {
        wn::tick(st, kUnseen);
    }
    TEST_ASSERT_EQUAL_UINT8(stars, st.stars);
    wn::tick(st, kUnseen);
    TEST_ASSERT_EQUAL_UINT8(stars - 1, st.stars);
}

void test_a_fresh_crime_restarts_the_clock() {
    // Committing a crime while cooling off has to reset the timer, or a
    // player under constant fire would still shake the police by standing
    // still for six seconds' worth of steps spread over a firefight.
    wn::State st = wn::clear();
    wn::report(st, wn::Crime::CivilianHurt);
    for (int i = 0; i < wn::kCoolSteps - 1; ++i) {
        wn::tick(st, kUnseen);
    }
    wn::report(st, wn::Crime::ShotFired);
    const std::uint8_t stars = st.stars;
    wn::tick(st, kUnseen);
    TEST_ASSERT_EQUAL_UINT8(stars, st.stars);
}

void test_ticking_a_clean_slate_never_underflows() {
    // 62.5 times a second for as long as the demo runs, on a uint8_t.
    wn::State st = wn::clear();
    for (int i = 0; i < 10000; ++i) {
        wn::tick(st, kUnseen);
    }
    TEST_ASSERT_EQUAL_UINT8(0, st.stars);
    TEST_ASSERT_FALSE(wn::isWanted(st));
}

void test_the_level_always_reaches_zero_eventually() {
    wn::State st = wn::clear();
    for (int i = 0; i < 100; ++i) {
        wn::report(st, wn::Crime::OfficerKilled);
    }
    for (int i = 0; i < (wn::kMaxStars + 1) * wn::kCoolSteps; ++i) {
        wn::tick(st, kUnseen);
    }
    TEST_ASSERT_EQUAL_UINT8(0, st.stars);
}

// --- Losing it while they are still looking at you ------------------------

void test_being_watched_stops_the_level_falling() {
    // The change that turns the star counter from a timer into a chase.
    // Standing in front of an officer for a full descent used to clear the
    // level; the police were a countdown the player waited out.
    wn::State st = wn::clear();
    wn::report(st, wn::Crime::OfficerKilled);
    const std::uint8_t stars = st.stars;
    for (int i = 0; i < wn::kCoolSteps * (wn::kMaxStars + 2); ++i) {
        wn::tick(st, kWatched);
    }
    TEST_ASSERT_EQUAL_UINT8(stars, st.stars);
}

void test_the_clock_starts_when_they_lose_you_and_not_before() {
    // And it starts from the top. Being seen holds the cooldown at full
    // rather than pausing it, so a player who breaks cover, is spotted again
    // for one step and breaks it again does not bank the seconds in between.
    wn::State st = wn::clear();
    wn::report(st, wn::Crime::CivilianHurt);
    const std::uint8_t stars = st.stars;

    for (int i = 0; i < wn::kCoolSteps - 1; ++i) {
        wn::tick(st, kUnseen);
    }
    // One step from losing a star -- and then they spot you.
    wn::tick(st, kWatched);
    TEST_ASSERT_EQUAL_UINT8(stars, st.stars);

    // Which has to have cost the whole period, not one step of it.
    for (int i = 0; i < wn::kCoolSteps - 1; ++i) {
        wn::tick(st, kUnseen);
    }
    TEST_ASSERT_EQUAL_UINT8(stars, st.stars);
    wn::tick(st, kUnseen);
    TEST_ASSERT_EQUAL_UINT8(stars - 1, st.stars);
}

void test_being_watched_at_zero_stars_is_still_nothing() {
    // Somebody who has done nothing is not hiding from anybody, and a
    // policeman looking at them must not start a countdown that has no
    // level to count down.
    wn::State st = wn::clear();
    for (int i = 0; i < 1000; ++i) {
        wn::tick(st, kWatched);
    }
    TEST_ASSERT_EQUAL_UINT8(0, st.stars);
    TEST_ASSERT_EQUAL_UINT16(0, st.coolSteps);
}

void test_breaking_away_from_five_stars_is_still_possible() {
    // The city has to have an exit. Watched forever is the previous case;
    // this is the one that says a player who actually gets away gets away,
    // from the worst level the demo can produce.
    wn::State st = wn::clear();
    for (int i = 0; i < 100; ++i) {
        wn::report(st, wn::Crime::OfficerKilled);
    }
    TEST_ASSERT_EQUAL_UINT8(wn::kMaxStars, st.stars);
    for (int i = 0; i < (wn::kMaxStars + 1) * wn::kCoolSteps; ++i) {
        wn::tick(st, kUnseen);
    }
    TEST_ASSERT_EQUAL_UINT8(0, st.stars);
}

// --- What the level buys the police --------------------------------------

void test_more_stars_never_means_fewer_police() {
    std::uint8_t previous = 0xFF;
    for (std::uint8_t stars = 0; stars <= wn::kMaxStars; ++stars) {
        const std::uint8_t chance = wn::officerChanceIn(stars);
        // A one-in-N roll, so a SMALLER N is more police.
        TEST_ASSERT_TRUE(chance <= previous);
        previous = chance;
    }
}

void test_the_officer_roll_is_never_one_in_zero() {
    // The whole reason this is a function and not a table read at the call
    // site. `rand_int(0, N - 1)` with N of zero is an empty range, and one
    // star would end policing in the city permanently.
    for (std::uint8_t stars = 0; stars <= wn::kMaxStars + 4; ++stars) {
        TEST_ASSERT_TRUE(wn::officerChanceIn(stars) >= 1);
    }
}

void test_more_stars_never_means_a_shorter_chase() {
    std::uint16_t previous = 0;
    for (std::uint8_t stars = 0; stars <= wn::kMaxStars; ++stars) {
        const std::uint16_t range = wn::pursuitRangePx(stars);
        TEST_ASSERT_TRUE(range >= previous);
        previous = range;
    }
}

void test_nobody_is_chased_when_nobody_is_wanted() {
    TEST_ASSERT_EQUAL_UINT16(0, wn::pursuitRangePx(0));
    TEST_ASSERT_TRUE(wn::pursuitRangePx(1) > 0);
}

void test_a_chase_outreaches_the_weapon() {
    // An officer whose pursuit range is inside their firing range never has a
    // reason to walk: they stand at the edge of it and shoot. Closing in is
    // the whole difference between a turret and a threat.
    TEST_ASSERT_TRUE(wn::pursuitRangePx(1) > wn::kOfficerWeaponRangePx);
}

void test_the_street_is_empty_of_patrols_until_it_is_earned() {
    // Nobody is hunted at zero, and one star is a foot patrol answering a
    // gunshot -- not a car. The first siren has to be a thing that HAPPENS,
    // or the player never learns that the level went up.
    TEST_ASSERT_EQUAL_UINT8(0, wn::patrolCars(0));
    TEST_ASSERT_EQUAL_UINT8(0, wn::patrolCars(1));
    TEST_ASSERT_TRUE(wn::patrolCars(2) > 0);
}

void test_more_stars_never_means_fewer_patrol_cars() {
    std::uint8_t previous = 0;
    for (std::uint8_t stars = 0; stars <= wn::kMaxStars; ++stars) {
        const std::uint8_t cars = wn::patrolCars(stars);
        TEST_ASSERT_TRUE(cars >= previous);
        previous = cars;
    }
}

void test_the_force_never_takes_the_whole_road() {
    // The patrol cars and the ordinary traffic share one pool. A force that
    // could fill it would not be a manhunt -- it would be a city where all
    // traffic is police, which reads as the traffic being broken rather than
    // the player being hunted. Strictly fewer, so there is always somebody
    // else on the road to steal a car from.
    for (std::uint8_t stars = 0; stars <= wn::kMaxStars + 4; ++stars) {
        TEST_ASSERT_TRUE(wn::patrolCars(stars) < wn::kStreetCarSlots);
    }
}

void test_a_chase_never_slows_down_as_it_escalates() {
    std::int32_t previous = 0;
    for (std::uint8_t stars = 0; stars <= wn::kMaxStars; ++stars) {
        const std::int32_t speed = wn::chaseSpeedSub(stars);
        TEST_ASSERT_TRUE(speed >= previous);
        previous = speed;
    }
    TEST_ASSERT_TRUE(wn::chaseSpeedSub(wn::kMaxStars)
                     > wn::chaseSpeedSub(1));
}

void test_a_chasing_officer_never_outruns_a_running_player() {
    // The escape has to stay possible on foot at every level, or five stars
    // is not a hard chase -- it is a cutscene where the player is caught.
    // Strictly slower, at the top of the table.
    for (std::uint8_t stars = 0; stars <= wn::kMaxStars + 4; ++stars) {
        TEST_ASSERT_TRUE(wn::chaseSpeedSub(stars) < wn::kPlayerRunSpeedSub);
    }
}

void test_an_officer_never_walks_slower_for_being_angrier() {
    // The base is what an officer on a one-star call already moves at. A
    // table that dipped below it would have the force get SLOWER the moment
    // the player did something worse.
    for (std::uint8_t stars = 1; stars <= wn::kMaxStars; ++stars) {
        TEST_ASSERT_TRUE(wn::chaseSpeedSub(stars)
                         >= wn::kOfficerChaseSpeedSub);
    }
}

void test_every_rung_of_the_ladder_buys_something() {
    // Five stars with three levels of actual content is a counter that lies:
    // it went up, the HUD says so, and the city is identical. So every
    // adjacent pair must differ in at least ONE of the four things the level
    // controls -- deliberately not all four, because police DENSITY has a
    // ceiling (one pedestrian in two in uniform is a parade, not a manhunt)
    // and the rungs above its plateau are paid for in cars, reach and speed.
    for (std::uint8_t stars = 1; stars <= wn::kMaxStars; ++stars) {
        const std::uint8_t below = static_cast<std::uint8_t>(stars - 1);
        const bool differs =
            wn::officerChanceIn(stars) != wn::officerChanceIn(below)
            || wn::pursuitRangePx(stars) != wn::pursuitRangePx(below)
            || wn::patrolCars(stars) != wn::patrolCars(below)
            || wn::chaseSpeedSub(stars) != wn::chaseSpeedSub(below);
        TEST_ASSERT_TRUE_MESSAGE(differs, "a star that changes nothing");
    }
}

void test_a_star_count_past_the_maximum_is_still_safe() {
    // Nothing should produce one, and a table read at the call site would
    // walk off the end if something did. Clamped rather than asserted:
    // this is a HUD counter, not a memory allocation.
    TEST_ASSERT_EQUAL_UINT8(wn::officerChanceIn(wn::kMaxStars),
                            wn::officerChanceIn(200));
    TEST_ASSERT_EQUAL_UINT16(wn::pursuitRangePx(wn::kMaxStars),
                             wn::pursuitRangePx(200));
    TEST_ASSERT_EQUAL_UINT8(wn::patrolCars(wn::kMaxStars),
                            wn::patrolCars(200));
    TEST_ASSERT_EQUAL_INT32(wn::chaseSpeedSub(wn::kMaxStars),
                            wn::chaseSpeedSub(200));
}

void test_the_mirrored_weapon_range_still_matches_the_armoury() {
    // Wanted.h carries its own copy of the police sidearm's reach so that
    // pursuitRangePx can be written as "further than they can shoot" without
    // pulling the weapon table into a header that has nothing else to do with
    // it. `weapons::spec` is a runtime lookup, so a static_assert cannot
    // reach it -- this is the tie-down instead, and it is the only thing
    // standing between a retuned pistol and officers who stand still.
    TEST_ASSERT_EQUAL_UINT16(
        top_down_city::weapons::spec(
            top_down_city::weapons::WeaponId::PolicePistol).rangePx,
        wn::kOfficerWeaponRangePx);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_a_clean_slate_is_not_wanted);
    RUN_TEST(test_a_crime_puts_you_at_least_at_its_floor);
    RUN_TEST(test_repeating_a_crime_escalates);
    RUN_TEST(test_firing_at_nothing_never_escalates);
    RUN_TEST(test_a_lesser_crime_never_lowers_the_level);
    RUN_TEST(test_the_level_saturates_at_the_maximum);
    RUN_TEST(test_cooling_off_takes_one_period_per_star);
    RUN_TEST(test_a_star_falls_off_exactly_on_time);
    RUN_TEST(test_a_fresh_crime_restarts_the_clock);
    RUN_TEST(test_ticking_a_clean_slate_never_underflows);
    RUN_TEST(test_the_level_always_reaches_zero_eventually);
    RUN_TEST(test_being_watched_stops_the_level_falling);
    RUN_TEST(test_the_clock_starts_when_they_lose_you_and_not_before);
    RUN_TEST(test_being_watched_at_zero_stars_is_still_nothing);
    RUN_TEST(test_breaking_away_from_five_stars_is_still_possible);
    RUN_TEST(test_more_stars_never_means_fewer_police);
    RUN_TEST(test_the_officer_roll_is_never_one_in_zero);
    RUN_TEST(test_more_stars_never_means_a_shorter_chase);
    RUN_TEST(test_nobody_is_chased_when_nobody_is_wanted);
    RUN_TEST(test_a_chase_outreaches_the_weapon);
    RUN_TEST(test_the_street_is_empty_of_patrols_until_it_is_earned);
    RUN_TEST(test_more_stars_never_means_fewer_patrol_cars);
    RUN_TEST(test_the_force_never_takes_the_whole_road);
    RUN_TEST(test_a_chase_never_slows_down_as_it_escalates);
    RUN_TEST(test_a_chasing_officer_never_outruns_a_running_player);
    RUN_TEST(test_an_officer_never_walks_slower_for_being_angrier);
    RUN_TEST(test_every_rung_of_the_ladder_buys_something);
    RUN_TEST(test_the_mirrored_weapon_range_still_matches_the_armoury);
    RUN_TEST(test_a_star_count_past_the_maximum_is_still_safe);
    return UNITY_END();
}
