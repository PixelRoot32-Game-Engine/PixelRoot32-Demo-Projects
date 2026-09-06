/**
 * @brief The weapon's rules, which are all integer bookkeeping and all of
 *        them the kind that fails quietly.
 *
 * A fire rate off by one step shoots 4.7 times a second instead of 4.5 and
 * nobody sees it. A range that truncates rather than rounding up stops a pixel
 * short of what the table promised. An infinite magazine that decrements
 * underflows to 65535 and then, eventually, jams -- half an hour into a demo,
 * once. None of that needs a Renderer, so none of it may hide on the device:
 * Weapon.h has no engine dependency, for the same reason DayNight.h and
 * MinimapRuns.h do not.
 */
#include <unity.h>

#include <cstdint>

#include "game/rules/Weapon.h"

namespace w = top_down_city::weapons;

void setUp() {}
void tearDown() {}

// --- The table -----------------------------------------------------------

void test_every_weapon_in_the_table_is_usable() {
    // Table-driven on purpose. The point of a spec table is that adding a
    // shotgun is a row rather than a code change, and a row nobody checked is
    // exactly how a weapon ships with a fire rate of zero -- which is not a
    // fast gun, it is a gun that fires every step forever.
    for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(w::WeaponId::Count);
         ++i) {
        const w::WeaponSpec& s = w::spec(static_cast<w::WeaponId>(i));
        TEST_ASSERT_NOT_NULL(s.name);
        TEST_ASSERT_GREATER_THAN_UINT8(0, s.damage);
        TEST_ASSERT_GREATER_THAN_UINT8(0, s.fireRateSteps);
        TEST_ASSERT_GREATER_THAN_UINT16(0, s.rangePx);
        TEST_ASSERT_GREATER_THAN_UINT16(0, s.projectileSpeedSub);
        TEST_ASSERT_GREATER_THAN_UINT8(0, s.tracerLengthPx);
        // A magazine of zero would be a weapon that can never fire at all.
        TEST_ASSERT_GREATER_THAN_UINT16(0, s.magazine);
    }
}

void test_the_pistol_is_the_default_weapon() {
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<std::uint8_t>(w::WeaponId::Pistol));
    // It is also the weapon the economy is built around, so its magazine is
    // finite and it is the number the shop's cheapest line sells. Pinned
    // against the named constant rather than against a literal: the price is
    // balanced against this figure in Economy.h and neither may move alone.
    TEST_ASSERT_EQUAL_UINT16(w::kPistolMagazine,
                             w::spec(w::WeaponId::Pistol).magazine);
}

// --- Range -> lifetime ---------------------------------------------------

void test_a_projectile_lives_long_enough_to_cover_its_range() {
    // Rounded UP, never truncated: `range` is what the table promises the
    // weapon reaches, so a bullet that dies one step early is a weapon that
    // quietly under-delivers at exactly the distance the player aimed from.
    for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(w::WeaponId::Count);
         ++i) {
        const w::WeaponSpec& s = w::spec(static_cast<w::WeaponId>(i));
        const std::uint32_t steps = w::lifetimeSteps(s);
        const std::uint32_t travelledSub = steps * s.projectileSpeedSub;
        const std::uint32_t rangeSub =
            static_cast<std::uint32_t>(s.rangePx) << 8;
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(rangeSub, travelledSub);
        // ...and not a step more than it needs, or the "range" column is a
        // decoration rather than a number.
        TEST_ASSERT_LESS_THAN_UINT32(rangeSub + s.projectileSpeedSub,
                                     travelledSub);
    }
}

// --- The fire rate -------------------------------------------------------

void test_a_loaded_weapon_can_fire() {
    w::State st = w::load(w::spec(w::WeaponId::Pistol));
    TEST_ASSERT_TRUE(w::canFire(st));
}

void test_the_cooldown_is_exactly_the_fire_rate() {
    const w::WeaponSpec& s = w::spec(w::WeaponId::Pistol);
    w::State st = w::load(s);

    w::onFired(st, s);
    TEST_ASSERT_FALSE(w::canFire(st));

    // One tick short: still not ready. This is the off-by-one, and it is the
    // whole reason this test counts instead of sampling.
    for (std::uint8_t i = 0; i < s.fireRateSteps - 1; ++i) {
        w::tick(st);
        TEST_ASSERT_FALSE(w::canFire(st));
    }
    w::tick(st);
    TEST_ASSERT_TRUE(w::canFire(st));
}

void test_ticking_a_ready_weapon_never_underflows() {
    const w::WeaponSpec& s = w::spec(w::WeaponId::Pistol);
    w::State st = w::load(s);
    for (int i = 0; i < 50; ++i) {
        w::tick(st);
    }
    TEST_ASSERT_EQUAL_UINT8(0, st.cooldownSteps);
    TEST_ASSERT_TRUE(w::canFire(st));
}

// --- Ammunition ----------------------------------------------------------

void test_an_infinite_magazine_never_decrements() {
    // The police sidearm, which is now the only infinite one: an officer who
    // ran out of rounds mid-chase would end the pursuit by standing still. A
    // decrement here wraps 0xFFFF downwards and the gun eventually jams --
    // once, deep into a session, unreproducibly.
    const w::WeaponSpec& s = w::spec(w::WeaponId::PolicePistol);
    w::State st = w::load(s);
    TEST_ASSERT_EQUAL_UINT16(w::kInfiniteAmmo, st.ammo);

    for (int shot = 0; shot < 1000; ++shot) {
        w::onFired(st, s);
        for (std::uint8_t i = 0; i < s.fireRateSteps; ++i) {
            w::tick(st);
        }
        TEST_ASSERT_EQUAL_UINT16(w::kInfiniteAmmo, st.ammo);
        TEST_ASSERT_FALSE(w::isEmpty(st));
    }
}

void test_a_finite_magazine_counts_down_and_empties() {
    // Both player weapons count down now. A made-up row rather than either of
    // them, because what is under test is the counting and not the balance:
    // a magazine retuned in the table must not silently rewrite this case.
    const w::WeaponSpec finite = {"Test", 10, 4, 64, 3, 1024, 3};
    w::State st = w::load(finite);
    TEST_ASSERT_EQUAL_UINT16(3, st.ammo);

    for (std::uint16_t left = 3; left > 0; --left) {
        TEST_ASSERT_TRUE(w::canFire(st));
        w::onFired(st, finite);
        TEST_ASSERT_EQUAL_UINT16(left - 1, st.ammo);
        for (std::uint8_t i = 0; i < finite.fireRateSteps; ++i) {
            w::tick(st);
        }
    }
    TEST_ASSERT_TRUE(w::isEmpty(st));
    TEST_ASSERT_FALSE(w::canFire(st));
}

void test_an_empty_weapon_stays_empty_however_long_it_cools() {
    const w::WeaponSpec finite = {"Test", 10, 4, 64, 1, 1024, 3};
    w::State st = w::load(finite);
    w::onFired(st, finite);
    for (int i = 0; i < 100; ++i) {
        w::tick(st);
    }
    TEST_ASSERT_EQUAL_UINT8(0, st.cooldownSteps);
    TEST_ASSERT_TRUE(w::isEmpty(st));
    TEST_ASSERT_FALSE(w::canFire(st));
}

// --- Tunnelling ----------------------------------------------------------

void test_no_weapon_can_shoot_straight_through_a_person() {
    // The one bug in this system that would never be reported as a bug. A
    // projectile moves in whole steps and is tested only where it lands, so a
    // fast enough round clears a six-pixel target box between two samples and
    // passes through the person. It does not crash and it does not look
    // broken -- it looks like the player missing, sometimes, and only when
    // firing along one of the two axes where the box is short.
    for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(w::WeaponId::Count);
         ++i) {
        TEST_ASSERT_TRUE(w::hitsEveryTargetOnItsPath(
            w::spec(static_cast<w::WeaponId>(i))));
    }
}

void test_the_tunnelling_check_actually_rejects_a_fast_weapon() {
    // A guard nothing can fail is a guard nobody has tested. B + S - 1 is
    // 2 + 6 - 1 = 7 px per step; 8 must be refused.
    const w::WeaponSpec ok   = {"OK",   10, 4, 64, 9, 7 * 256, 3};
    const w::WeaponSpec fast = {"Fast", 10, 4, 64, 9, 8 * 256, 3};
    TEST_ASSERT_TRUE(w::hitsEveryTargetOnItsPath(ok));
    TEST_ASSERT_FALSE(w::hitsEveryTargetOnItsPath(fast));

    // And a sub-pixel speed is rounded UP, because a fraction that
    // accumulates alternates between a short step and a long one, and it is
    // the long one that misses.
    const w::WeaponSpec justOver = {"Over", 10, 4, 64, 9, 7 * 256 + 1, 3};
    TEST_ASSERT_FALSE(w::hitsEveryTargetOnItsPath(justOver));
}

// --- Aim -----------------------------------------------------------------

void test_every_facing_aims_somewhere() {
    // Four directions, four unit vectors, no diagonals: the character has
    // four sets of frames and a bullet leaving at an angle the sprite is not
    // pointing would read as a bug in the art.
    const w::Aim aims[4] = {
        w::aimOf(top_down_city::Facing::Down),
        w::aimOf(top_down_city::Facing::Up),
        w::aimOf(top_down_city::Facing::Right),
        w::aimOf(top_down_city::Facing::Left),
    };
    for (int i = 0; i < 4; ++i) {
        const int magnitude = aims[i].dx * aims[i].dx + aims[i].dy * aims[i].dy;
        TEST_ASSERT_EQUAL_INT(1, magnitude);
    }
    TEST_ASSERT_EQUAL_INT(1,  aims[0].dy);     // Down  is +y
    TEST_ASSERT_EQUAL_INT(-1, aims[1].dy);     // Up    is -y
    TEST_ASSERT_EQUAL_INT(1,  aims[2].dx);     // Right is +x
    TEST_ASSERT_EQUAL_INT(-1, aims[3].dx);     // Left  is -x
}

// --- Pellets --------------------------------------------------------------

void test_every_weapon_fires_at_least_one_thing() {
    // A row with a zero pellet count is a gun that consumes a round, plays a
    // flash, charges its cooldown and puts nothing in the air.
    for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(w::WeaponId::Count);
         ++i) {
        TEST_ASSERT_TRUE(w::spec(static_cast<w::WeaponId>(i)).pellets >= 1);
    }
}

void test_a_single_pellet_goes_straight() {
    const w::WeaponSpec& s = w::spec(w::WeaponId::Pistol);
    TEST_ASSERT_EQUAL_INT32(0, w::pelletOffsetSub(s, 0));
}

void test_a_spread_is_symmetric_about_the_aim() {
    // The sum of the offsets has to be zero, or the whole pattern leans and
    // the player learns to aim off-centre to compensate for a bug.
    for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(w::WeaponId::Count);
         ++i) {
        const w::WeaponSpec& s = w::spec(static_cast<w::WeaponId>(i));
        std::int32_t total = 0;
        for (std::uint8_t p = 0; p < s.pellets; ++p) {
            total += w::pelletOffsetSub(s, p);
        }
        TEST_ASSERT_EQUAL_INT32(0, total);
    }
}

void test_an_odd_spread_sends_one_pellet_down_the_middle() {
    const w::WeaponSpec& s = w::spec(w::WeaponId::Shotgun);
    TEST_ASSERT_TRUE(s.pellets % 2 == 1);
    TEST_ASSERT_EQUAL_INT32(0, w::pelletOffsetSub(s, s.pellets / 2));
    // And the outer two really are apart, or the "spread" is decoration.
    TEST_ASSERT_TRUE(w::pelletOffsetSub(s, 0)
                     < w::pelletOffsetSub(s, s.pellets - 1));
}

void test_a_pellet_index_past_the_end_is_harmless() {
    // The system loops to `pellets`, but a row edited down while a shot is in
    // flight would ask past it, and a wild offset is a pellet leaving at an
    // angle nothing drew.
    const w::WeaponSpec& s = w::spec(w::WeaponId::Shotgun);
    TEST_ASSERT_EQUAL_INT32(0, w::pelletOffsetSub(s, s.pellets));
    TEST_ASSERT_EQUAL_INT32(0, w::pelletOffsetSub(s, 200));
}

void test_the_spread_is_inside_the_tunnelling_budget() {
    // hitsEveryTargetOnItsPath now counts the sideways speed too, so this is
    // the same guard as before over a bigger number -- and it is the one that
    // would have let a fast, wide-spreading weapon through.
    for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(w::WeaponId::Count);
         ++i) {
        TEST_ASSERT_TRUE(w::hitsEveryTargetOnItsPath(
            w::spec(static_cast<w::WeaponId>(i))));
    }
}

// --- The pattern ----------------------------------------------------------

void test_a_pattern_can_still_land_whole_at_maximum_range() {
    // The one the shotgun shipped broken. Every other number in that row was
    // checked -- damage against hit points, spread against the tunnelling
    // budget, magazine against the fallback -- and the weapon was still
    // useless, because nothing checked whether the pellets were still NEAR
    // EACH OTHER on arrival. A pattern wider than the target is three separate
    // bullets, two of which always miss: a worse pistol that looks exactly
    // like the player being bad at aiming.
    for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(w::WeaponId::Count);
         ++i) {
        const w::WeaponSpec& s = w::spec(static_cast<w::WeaponId>(i));
        TEST_ASSERT_TRUE_MESSAGE(w::patternFitsSmallestTarget(s), s.name);
    }
}

void test_a_single_projectile_has_no_pattern_at_all() {
    // Or the check is measuring rounding noise on the pistol.
    TEST_ASSERT_EQUAL_INT(0,
        w::patternHalfWidthPx(w::spec(w::WeaponId::Pistol)));
}

void test_the_pattern_check_actually_rejects_a_wide_one() {
    // Same shape as the tunnelling check's own guard: a test that only ever
    // sees passing rows cannot tell a real check from `return true`. These
    // are the numbers the shotgun shipped with.
    w::WeaponSpec wide = w::spec(w::WeaponId::Shotgun);
    wide.spreadSub = 256;
    TEST_ASSERT_FALSE(w::patternFitsSmallestTarget(wide));
    TEST_ASSERT_TRUE(w::patternHalfWidthPx(wide)
                     > w::patternHalfWidthPx(w::spec(w::WeaponId::Shotgun)));
}

void test_the_pattern_grows_with_the_range_it_is_fired_over() {
    // The half-width is a consequence of how long the pellets fly, not a
    // number in the table. Doubling the range doubles the fan, which is why
    // the range and the spread cannot be tuned independently.
    w::WeaponSpec far = w::spec(w::WeaponId::Shotgun);
    far.rangePx = static_cast<std::uint16_t>(far.rangePx * 2);
    TEST_ASSERT_TRUE(w::patternHalfWidthPx(far)
                     > w::patternHalfWidthPx(w::spec(w::WeaponId::Shotgun)));
}

void test_a_landed_pattern_hurts_more_than_a_pistol_round() {
    // What the player is buying with the shorter range, the slower rate and
    // the twelve shells. If a full pattern is not worth more than one pistol
    // round, none of those costs bought anything.
    const w::WeaponSpec& gun = w::spec(w::WeaponId::Shotgun);
    const w::WeaponSpec& pistol = w::spec(w::WeaponId::Pistol);
    TEST_ASSERT_TRUE(gun.damage * gun.pellets > pistol.damage);
    // And the costs are real, or it is simply the better gun.
    TEST_ASSERT_TRUE(gun.rangePx < pistol.rangePx);
    TEST_ASSERT_TRUE(gun.fireRateSteps > pistol.fireRateSteps);
}

// --- The first finite magazine --------------------------------------------

void test_both_player_weapons_run_out() {
    // A pistol that never empties is a player who never needs the counter, and
    // every price on it is then decoration. What the player walks back TO is
    // no longer a gun in the street -- there is one on the whole island and it
    // does not come back -- it is the shop.
    TEST_ASSERT_TRUE(w::spec(w::WeaponId::Shotgun).magazine
                     != w::kInfiniteAmmo);
    TEST_ASSERT_TRUE(w::spec(w::WeaponId::Pistol).magazine
                     != w::kInfiniteAmmo);
}

void test_the_two_player_weapons_hold_different_amounts() {
    // Pinned against the named constants, and pinned as DIFFERENT numbers.
    // Twelve shells is one of the shotgun's three prices against the pistol --
    // the others are its range and its rate -- so a shotgun holding exactly
    // what a pistol does has quietly lost a third of the trade, silently. One
    // shared magazine constant covering both rows is how that happens.
    TEST_ASSERT_EQUAL_UINT16(w::kPistolMagazine,
                             w::spec(w::WeaponId::Pistol).magazine);
    TEST_ASSERT_EQUAL_UINT16(w::kShotgunMagazine,
                             w::spec(w::WeaponId::Shotgun).magazine);
    TEST_ASSERT_TRUE(w::kPistolMagazine != w::kShotgunMagazine);
}

void test_only_the_force_carries_an_endless_magazine() {
    // And it has to: an officer counts no rounds, has no shop to walk to and
    // no way to reload, so a finite police sidearm is a chase that ends with
    // three men standing in the road.
    TEST_ASSERT_EQUAL_UINT16(w::kInfiniteAmmo,
                             w::spec(w::WeaponId::PolicePistol).magazine);
}

void test_a_full_magazine_is_worth_carrying() {
    // The floor under every finite row, and it is the number that decides
    // whether a purchase is a weapon or a souvenir. A magazine that cannot
    // put one person down is a gun the player paid for and cannot use.
    for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(w::WeaponId::Count);
         ++i) {
        const w::WeaponSpec& s = w::spec(static_cast<w::WeaponId>(i));
        if (s.magazine == w::kInfiniteAmmo) {
            continue;
        }
        const int carried = static_cast<int>(s.magazine) * s.damage
                          * s.pellets;
        TEST_ASSERT_TRUE(carried >= w::kSmallestTargetHealth);
    }
}

void test_a_full_shotgun_hit_is_enough_to_put_somebody_down() {
    // Three pellets at 34 against 100 hit points. One pellet short and the
    // shotgun is a worse pistol with a shorter range, which is the version
    // nobody would ever pick up.
    const w::WeaponSpec& s = w::spec(w::WeaponId::Shotgun);
    TEST_ASSERT_TRUE(s.damage * s.pellets >= w::kSmallestTargetHealth);
    // ...and two pellets are not, or the spread stops meaning anything.
    TEST_ASSERT_TRUE(s.damage * (s.pellets - 1) < w::kSmallestTargetHealth);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_every_weapon_in_the_table_is_usable);
    RUN_TEST(test_the_pistol_is_the_default_weapon);
    RUN_TEST(test_a_projectile_lives_long_enough_to_cover_its_range);
    RUN_TEST(test_a_loaded_weapon_can_fire);
    RUN_TEST(test_the_cooldown_is_exactly_the_fire_rate);
    RUN_TEST(test_ticking_a_ready_weapon_never_underflows);
    RUN_TEST(test_an_infinite_magazine_never_decrements);
    RUN_TEST(test_a_finite_magazine_counts_down_and_empties);
    RUN_TEST(test_an_empty_weapon_stays_empty_however_long_it_cools);
    RUN_TEST(test_no_weapon_can_shoot_straight_through_a_person);
    RUN_TEST(test_the_tunnelling_check_actually_rejects_a_fast_weapon);
    RUN_TEST(test_every_facing_aims_somewhere);
    RUN_TEST(test_every_weapon_fires_at_least_one_thing);
    RUN_TEST(test_a_single_pellet_goes_straight);
    RUN_TEST(test_a_spread_is_symmetric_about_the_aim);
    RUN_TEST(test_an_odd_spread_sends_one_pellet_down_the_middle);
    RUN_TEST(test_a_pellet_index_past_the_end_is_harmless);
    RUN_TEST(test_the_spread_is_inside_the_tunnelling_budget);
    RUN_TEST(test_a_pattern_can_still_land_whole_at_maximum_range);
    RUN_TEST(test_a_single_projectile_has_no_pattern_at_all);
    RUN_TEST(test_the_pattern_check_actually_rejects_a_wide_one);
    RUN_TEST(test_the_pattern_grows_with_the_range_it_is_fired_over);
    RUN_TEST(test_a_landed_pattern_hurts_more_than_a_pistol_round);
    RUN_TEST(test_both_player_weapons_run_out);
    RUN_TEST(test_the_two_player_weapons_hold_different_amounts);
    RUN_TEST(test_only_the_force_carries_an_endless_magazine);
    RUN_TEST(test_a_full_magazine_is_worth_carrying);
    RUN_TEST(test_a_full_shotgun_hit_is_enough_to_put_somebody_down);
    return UNITY_END();
}
