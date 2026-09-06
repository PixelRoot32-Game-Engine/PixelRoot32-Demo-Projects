#pragma once
#include <cstdint>

/**
 * How much trouble the player is in, and how fast it goes away.
 *
 * A star count and a timer, and the timer is NOT a countdown to being
 * forgiven: it measures how long since the police last had eyes on you, and
 * does not run at all while somebody does (see `tick`). A level that falls on
 * a clock is a queue, not a manhunt -- it can be waited out standing in front
 * of the officer chasing you.
 *
 * Every table lives here rather than at the call sites, and that is not
 * tidiness: `officerChanceIn` feeds a `rand_int(0, N - 1)`, and an N of zero
 * is an empty range that would end policing in the city permanently.
 *
 * Engine-free like the rest of `game/rules/`, the clearest case of it: a star
 * counter has no visual failure of its own. A level that never rises is a city
 * that does not care what you do; one that never falls is a city you can never
 * get out of. `test_every_rung_of_the_ladder_buys_something` walks the four
 * tables and fails on any adjacent pair that are identical: a rung that changes
 * nothing is a HUD that lies.
 */
namespace top_down_city::wanted {

constexpr std::uint8_t kMaxStars = 5;

/// Logic steps a single star takes to fall off once the police have lost you.
/// At 62.5 steps a second that is six seconds a star, so the full descent
/// from five is half a minute of staying out of sight.
constexpr int kCoolSteps = 375;

/// The officers' weapon range, repeated here so `pursuitRangePx` can be
/// checked against it without this header pulling in the weapon table.
/// static_asserted equal in CityConstants.h, which sees both.
constexpr std::uint16_t kOfficerWeaponRangePx = 96;

/// Three more copies, tied down the same way. Each turns a table entry into a
/// claim that can be checked instead of eyeballed: a chase speed below the base
/// would mean the force gets SLOWER as the player gets worse; one at or above
/// the player's run makes five stars a cutscene rather than a chase they can
/// lose; a patrol count that fills the car pool leaves no ordinary traffic, so
/// every vehicle in the city is police and there is nothing left to steal.
constexpr std::int32_t  kOfficerChaseSpeedSub = 288;   // 1.125 px/step
constexpr std::int32_t  kPlayerRunSpeedSub    = 448;   // 1.75 px/step
constexpr std::uint8_t  kStreetCarSlots       = 6;

/// What the player can be seen doing. Deliberately not an exhaustive list of
/// wrongdoing -- these are the five things the player actually does.
enum class Crime : std::uint8_t {
    ShotFired = 0,     ///< discharging a weapon anywhere in public
    CivilianHurt,
    CivilianKilled,
    OfficerHurt,
    OfficerKilled,
    Count
};

/**
 * @brief What one crime is worth.
 *
 * `floor` is where a crime puts you if you were below it, so shooting somebody
 * is never a one-star affair; a floor alone would pin the level there and make
 * every crime after the first free, hence the `bump` on top of what you had.
 * `ShotFired` has a floor and no bump on purpose: firing gets you noticed, and
 * emptying a magazine into a wall brings nobody else.
 */
struct Penalty {
    std::uint8_t floor;
    std::uint8_t bump;
};

const Penalty& penalty(Crime crime);

struct State {
    std::uint8_t  stars;
    std::uint16_t coolSteps;   ///< steps until the next star falls off
};

/// A clean slate. What the player starts with and what a respawn restores.
State clear();

/// Raise the level for a crime -- and restart the cooldown whether or not it
/// rose, so a player under constant fire cannot shake the police by spreading
/// six seconds of standing still across a firefight.
void report(State& state, Crime crime);

/**
 * @brief One fixed logic step of cooling off.
 * @param seen  Does anybody on the force have a clear line to the player? A
 *              foot officer, a patrol car, or the station's duty staff -- the
 *              scene asks whichever exist and passes the answer.
 *
 * While `seen` the cooldown is HELD at full, not paused: a player who ducks
 * behind a block, is spotted for one step and ducks again would otherwise bank
 * the seconds either side of that step and shed a star having never got away.
 * Holding restarts the six seconds from the last moment anybody laid eyes on
 * them. A no-op at zero stars, including the `seen` branch -- somebody who has
 * done nothing is not hiding from anybody.
 */
void tick(State& state, bool seen);

bool isWanted(const State& state);

/// The 1-in-N chance that a street pedestrian is an officer. Smaller is more
/// police. Never zero, and clamped above `kMaxStars`.
std::uint8_t officerChanceIn(std::uint8_t stars);

/// How many patrol cars the force puts on the street. Zero below two stars:
/// one star is a foot patrol answering a gunshot, and the first siren has to
/// be a thing that HAPPENS or the player never learns the level went up.
/// Always strictly fewer than `kStreetCarSlots` -- patrol cars and ordinary
/// traffic are the same pool, and a force that could fill it would read as the
/// traffic being broken.
std::uint8_t patrolCars(std::uint8_t stars);

/// How fast a chasing officer walks. A speed rather than a wider aim because a
/// four-way weapon fired off its axis simply misses: loosening their aim would
/// make a higher level LESS dangerous while looking like the opposite. Never as
/// fast as a running player, at any level.
std::int32_t chaseSpeedSub(std::uint8_t stars);

/// How far an alerted officer will walk toward the player; zero at zero stars.
/// Always longer than the weapon's range, which is the whole point of the
/// number: an officer who can shoot from further than they will walk stands at
/// the edge of their range and fires, which is a turret.
std::uint16_t pursuitRangePx(std::uint8_t stars);

}  // namespace top_down_city::wanted
