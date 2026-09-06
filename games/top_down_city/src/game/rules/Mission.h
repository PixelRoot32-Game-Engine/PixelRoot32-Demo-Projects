#pragma once
#include <cstdint>

/**
 * @brief The courier run: a place to be, and a clock to be there by.
 *
 * The smallest thing that ASKS something of the player rather than reacting
 * to them: a marker somewhere on the island, a timer, and a streak. No
 * contractor and no mission list, and for two stages no money either -- the
 * note here said an economy grows a shop, a currency, a HUD row and a balance
 * problem, and none of that was the drive. Three of those four turned out
 * cheap once the shop was a room that already existed; the fourth is
 * `game/rules/Economy.h`, where the money lives, deliberately NOT here. This
 * file answers where to be and by when; what a leg is worth is a different
 * question with a different failure, and the streak is all the two share.
 *
 * The one rule here worth testing is the allowance. Everything else is a
 * state machine that fails loudly; an allowance too tight to WALK fails
 * silently and repeatedly, telling a player who has not yet found a car that
 * they are bad at a game whose rules they cannot see.
 */
namespace top_down_city::mission {

/// Logic steps a walking player needs to cross one tile, rounded up. Mirrors
/// what CityConstants.h's kWalkSpeedSub buys, kept here so the allowance can
/// be written engine-free, and asserted equal over there, which sees both.
constexpr int kOnFootStepsPerTile = 16;

/// The clock every leg starts with even at zero distance, so two targets that
/// land near each other do not hand out a run that has already failed.
constexpr int kBaseAllowanceSteps = 120;      // ~2 s

/// Slack over the straight-line walk, as a fraction. Manhattan distance is
/// not the path -- buildings, water and the park all push a courier sideways
/// -- and an allowance with no margin is one nobody makes.
constexpr int kDetourNum = 3;
constexpr int kDetourDen = 2;

/// stepsLeft is a uint16_t and the map is 128 tiles square, so a leg can be
/// 254 tiles of Manhattan -- 6216 steps, nowhere near this. The clamp is a
/// guard against a distance from off the island, not a design decision --
/// `test_the_clamp_is_a_guard_and_not_a_rule` asserts no leg the map can hand
/// out reaches it.
constexpr std::uint16_t kMaxAllowanceSteps = 0xFFFFu;

/// A uint8_t of deliveries. Saturating rather than wrapping, because a streak
/// that rolls to zero at 256 reads as a run that was failed.
constexpr std::uint8_t kMaxStreak = 0xFFu;

struct State {
    std::uint8_t  target;      ///< index into city_scene::MISSION_TARGETS
    std::uint16_t stepsLeft;
    std::uint8_t  streak;      ///< deliveries since the last miss
    std::uint8_t  best;        ///< longest streak this session
};

/// How long a leg of this Manhattan distance is worth.
std::uint16_t allowanceSteps(int distanceTiles);

State begin(std::uint8_t target, int distanceTiles);

/// One fixed logic step off the clock. Saturates at zero rather than wrapping
/// -- the scene notices an expiry on the same step and hands out a new leg,
/// but "the scene notices" is not something a counter should depend on.
void tick(State& state);

bool expired(const State& state);

/// Reached in time: the streak lengthens and the next leg begins.
void delivered(State& state, std::uint8_t nextTarget, int distanceTiles);

/// Missed: the streak resets and the next leg begins anyway. There is no game
/// over in this demo and no menu to send anybody to, and a courier loop that
/// stops when you are late is a loop most players see once.
void failed(State& state, std::uint8_t nextTarget, int distanceTiles);

/**
 * @brief Pick somewhere else to be.
 * @param roll  Any random value; only its remainder is used.
 *
 * Never the target just reached. Otherwise a delivery is followed by standing
 * still and delivering again, which is a streak nobody earned.
 */
std::uint8_t nextTarget(std::uint8_t current, std::uint8_t count,
                        std::uint32_t roll);

}  // namespace top_down_city::mission
