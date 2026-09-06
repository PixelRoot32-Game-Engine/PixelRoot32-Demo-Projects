#include "game/rules/Mission.h"

namespace top_down_city::mission {

std::uint16_t allowanceSteps(int distanceTiles) {
    if (distanceTiles < 0) {
        distanceTiles = 0;
    }
    // Held in a long and narrowed only at the return, so the clamp below
    // compares a true value rather than one already wrapped into the
    // uint16_t this hands back. Nothing on the island gets near it: 254
    // tiles is the widest Manhattan leg, and at kOnFootStepsPerTile that is
    // 6216 steps -- see `test_the_clamp_is_a_guard_and_not_a_rule`.
    const long walk = static_cast<long>(distanceTiles) * kOnFootStepsPerTile;
    const long allowance = kBaseAllowanceSteps
                         + walk * kDetourNum / kDetourDen;
    if (allowance >= static_cast<long>(kMaxAllowanceSteps)) {
        return kMaxAllowanceSteps;
    }
    return static_cast<std::uint16_t>(allowance);
}

State begin(std::uint8_t target, int distanceTiles) {
    return State{target, allowanceSteps(distanceTiles), 0, 0};
}

void tick(State& state) {
    if (state.stepsLeft > 0) {
        --state.stepsLeft;
    }
}

bool expired(const State& state) {
    return state.stepsLeft == 0;
}

void delivered(State& state, std::uint8_t nextTarget, int distanceTiles) {
    if (state.streak < kMaxStreak) {
        ++state.streak;
    }
    if (state.streak > state.best) {
        state.best = state.streak;
    }
    state.target = nextTarget;
    state.stepsLeft = allowanceSteps(distanceTiles);
}

void failed(State& state, std::uint8_t nextTarget, int distanceTiles) {
    // `best` is deliberately untouched. It is the only thing in the demo that
    // survives a mistake, and that is what makes it worth having.
    state.streak = 0;
    state.target = nextTarget;
    state.stepsLeft = allowanceSteps(distanceTiles);
}

std::uint8_t nextTarget(std::uint8_t current, std::uint8_t count,
                        std::uint32_t roll) {
    if (count <= 1) {
        // Nowhere else to go. Answering rather than searching: the generator
        // emits one target per district and could in principle emit one.
        return 0;
    }
    // Roll over the OTHER count - 1 entries and step past the current one,
    // rather than rolling over all of them and retrying: a retry loop here
    // has no bound, and the bias does not matter at six targets.
    const std::uint8_t offset =
        static_cast<std::uint8_t>(roll % (count - 1u));
    const std::uint8_t picked =
        static_cast<std::uint8_t>((current + 1u + offset) % count);
    return picked;
}

}  // namespace top_down_city::mission
