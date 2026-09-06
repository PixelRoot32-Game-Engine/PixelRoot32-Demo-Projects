#include "game/rules/Wanted.h"

namespace top_down_city::wanted {

namespace {

//                         floor  bump
constexpr Penalty kPenalties[static_cast<std::uint8_t>(Crime::Count)] = {
    /* ShotFired      */ {    1,    0 },
    /* CivilianHurt   */ {    1,    1 },
    /* CivilianKilled */ {    2,    1 },
    /* OfficerHurt    */ {    2,    1 },
    /* OfficerKilled  */ {    3,    2 },
};

/// Police on the street, as a 1-in-N spawn roll. Six is the crowd's ordinary
/// beat; two is one pedestrian in two wearing a uniform, which on a 240x240
/// viewport is as many as fit without the street reading as a parade rather
/// than a manhunt. So this is the one ladder that PLATEAUS, deliberately:
/// there is no honest entry above two, and the top rung is bought below in
/// cars and reach and speed rather than by writing a 1 here and turning the
/// whole pavement into police.
constexpr std::uint8_t kOfficerChance[kMaxStars + 1] = { 6, 5, 4, 3, 2, 2 };

/// Patrol cars on the street at once. This used to be a bool -- one car at
/// three stars and above -- which flattened the top half of the ladder into a
/// single step. Three at five stars is half the pool: enough to arrive from
/// more than one direction, few enough to leave traffic to hide in and steal.
constexpr std::uint8_t kPatrolCars[kMaxStars + 1] = { 0, 0, 1, 1, 2, 3 };

/// Chase speed in sub-pixels per step. The base is what an officer on a
/// one-star call has always moved at; the top is 89% of a running player, so
/// the gap closes on somebody who walks, hesitates or gets stuck on a lamp
/// post, and never on somebody who commits to running.
constexpr std::int32_t kChaseSpeed[kMaxStars + 1] = {
    288, 288, 320, 352, 384, 400,
};

/// How far they will walk to reach you. The top of this table is a screen
/// width: an officer who starts a chase from further away than the player can
/// see arrives out of nowhere.
constexpr std::uint16_t kPursuitRange[kMaxStars + 1] = {
    0, 112, 144, 176, 208, 240,
};

std::uint8_t clampStars(std::uint8_t stars) {
    return stars > kMaxStars ? kMaxStars : stars;
}

}  // namespace

const Penalty& penalty(Crime crime) {
    const std::uint8_t index = static_cast<std::uint8_t>(crime);
    // A crime nothing in the demo can commit still has to answer something.
    // Falling back to the mildest entry rather than reading past the end.
    if (index >= static_cast<std::uint8_t>(Crime::Count)) {
        return kPenalties[0];
    }
    return kPenalties[index];
}

State clear() {
    return State{0, 0};
}

void report(State& state, Crime crime) {
    const Penalty& p = penalty(crime);

    int raised = static_cast<int>(state.stars) + static_cast<int>(p.bump);
    if (raised < static_cast<int>(p.floor)) {
        raised = p.floor;
    }
    if (raised > static_cast<int>(kMaxStars)) {
        raised = kMaxStars;
    }
    state.stars = static_cast<std::uint8_t>(raised);

    // Unconditionally, even when the level did not move: the timer measures
    // time since the last thing you did, not since the last one that raised it.
    state.coolSteps = static_cast<std::uint16_t>(kCoolSteps);
}

void tick(State& state, bool seen) {
    if (state.stars == 0) {
        state.coolSteps = 0;
        return;
    }
    if (seen) {
        // Held at full, not paused -- see the header.
        state.coolSteps = static_cast<std::uint16_t>(kCoolSteps);
        return;
    }
    if (state.coolSteps > 0) {
        --state.coolSteps;
    }
    if (state.coolSteps == 0) {
        --state.stars;
        // Only rearm while there is something left to lose, so a cleared
        // level does not sit holding a countdown to nothing.
        state.coolSteps = state.stars > 0
            ? static_cast<std::uint16_t>(kCoolSteps) : 0;
    }
}

bool isWanted(const State& state) {
    return state.stars > 0;
}

std::uint8_t officerChanceIn(std::uint8_t stars) {
    return kOfficerChance[clampStars(stars)];
}

std::uint16_t pursuitRangePx(std::uint8_t stars) {
    return kPursuitRange[clampStars(stars)];
}

std::uint8_t patrolCars(std::uint8_t stars) {
    return kPatrolCars[clampStars(stars)];
}

std::int32_t chaseSpeedSub(std::uint8_t stars) {
    return kChaseSpeed[clampStars(stars)];
}

}  // namespace top_down_city::wanted
