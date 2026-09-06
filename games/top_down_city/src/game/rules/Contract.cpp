#include "game/rules/Contract.h"

// The one include the header does not have. `Contract.h` names nothing from
// the courier run, so a scene that wants a chapter does not also get an
// allowance -- but the clock is the courier's, and that is where it lives.
#include "game/rules/Mission.h"

namespace top_down_city::contract {

namespace {

/// The one ending, shared by `delivered`, `cleaned` and `culled`. Three
/// chapters finish three different ways -- a car parked on a marker, a wanted
/// level back at zero, a count reaching it -- and all three have to end on
/// exactly the same terms, or the last chapter terminates correctly through
/// one route and walks the index off the end of a scene-side table through the
/// others.
void advanceChapter(State& state) {
    state.stepsLeft = 0;
    // The count goes with the clock. "Nothing reads it outside `Rampage`" is a
    // claim about every call site rather than about this file, and the two
    // counters this chapter runs on should stop together for the same reason
    // they started together.
    state.targetsLeft = 0;
    const std::uint8_t next = static_cast<std::uint8_t>(
        static_cast<std::uint8_t>(state.chapter) + 1u);
    if (next >= static_cast<std::uint8_t>(Chapter::Count)) {
        // The end of the story. The chapter index stays on the last real
        // entry rather than advancing to `Count`: the scene reads it to pick
        // a table row, and `Count` is not a row.
        state.phase = Phase::Complete;
        return;
    }
    state.chapter = static_cast<Chapter>(next);
    state.phase = Phase::Idle;
}

}  // namespace

std::uint16_t jobSteps(int toCarTiles, int carToDropTiles) {
    // `allowanceSteps` already floors a negative distance at zero and clamps
    // its own result, so the only new overflow here is the sum: two clamped
    // legs are 131070, which a uint16_t does not hold. Widen, then clamp.
    const std::uint32_t toCar = mission::allowanceSteps(toCarTiles);
    const std::uint32_t toDrop = mission::allowanceSteps(carToDropTiles);
    const std::uint32_t total = toCar + toDrop;
    if (total >= static_cast<std::uint32_t>(kMaxJobSteps)) {
        return kMaxJobSteps;
    }
    return static_cast<std::uint16_t>(total);
}

State clear() {
    State state;
    state.chapter = Chapter::Boost;
    state.phase = Phase::Idle;
    state.vehicle = 0;
    state.drop = 0;
    state.mark = 0;
    state.targetsLeft = 0;
    state.stepsLeft = 0;
    return state;
}

void begin(State& state, std::uint8_t vehicle, std::uint8_t drop,
           int toCarTiles, int carToDropTiles) {
    if (state.phase != Phase::Idle) {
        return;
    }
    state.phase = Phase::ToCar;
    state.vehicle = vehicle;
    state.drop = drop;
    state.stepsLeft = jobSteps(toCarTiles, carToDropTiles);
}

void tick(State& state) {
    if (!active(state)) {
        return;
    }
    if (state.stepsLeft > 0) {
        --state.stepsLeft;
    }
}

bool hasDestination(const State& state) {
    // The innermost of the three, and the only one written as a list.
    return state.phase == Phase::ToCar
        || state.phase == Phase::ToDrop
        || state.phase == Phase::ToTarget;
}

bool active(const State& state) {
    // A widening, not a second list: a rampage has a clock and nowhere to aim.
    return hasDestination(state) || state.phase == Phase::Rampage;
}

bool underway(const State& state) {
    // `active` plus the one phase that has a chapter running and no clock.
    return active(state) || state.phase == Phase::Struck;
}

bool expired(const State& state) {
    // Gated on `active`, not `underway`. That is what keeps `Struck` from
    // ever expiring -- including the case where the officer went down on the
    // very step the walk's clock ran out.
    return active(state) && state.stepsLeft == 0;
}

void boarded(State& state) {
    if (state.phase != Phase::ToCar) {
        return;
    }
    state.phase = Phase::ToDrop;
}

void delivered(State& state) {
    if (state.phase != Phase::ToDrop) {
        return;
    }
    advanceChapter(state);
}

void beginHit(State& state, std::uint8_t officer, int toStationTiles) {
    if (state.phase != Phase::Idle || state.chapter != Chapter::Hit) {
        return;
    }
    state.phase = Phase::ToTarget;
    state.mark = officer;
    // `mission::allowanceSteps` floors a negative distance at zero and clamps
    // its own result, so unlike `jobSteps` there is nothing left to widen:
    // one leg cannot overflow what one leg already fits in.
    state.stepsLeft = mission::allowanceSteps(toStationTiles);
}

void struck(State& state) {
    if (state.phase != Phase::ToTarget) {
        return;
    }
    // `stepsLeft` is deliberately left alone: the phase IS the fact.
    state.phase = Phase::Struck;
}

void cleaned(State& state) {
    if (state.phase != Phase::Struck) {
        return;
    }
    advanceChapter(state);
}

std::uint16_t frenzySteps(std::uint8_t targets) {
    // Widen before the clamp, not after. Integral promotion makes the multiply
    // itself safe (255 * 500 = 127500 in an int), but narrowing that into a
    // uint16_t gives 61964 -- under the clamp, and a SHORTER clock for the
    // biggest rampage than for a middling one.
    const std::uint32_t total =
        static_cast<std::uint32_t>(targets) * kFrenzyStepsPerTarget;
    if (total >= static_cast<std::uint32_t>(kMaxJobSteps)) {
        return kMaxJobSteps;
    }
    return static_cast<std::uint16_t>(total);
}

void beginFrenzy(State& state) {
    if (state.phase != Phase::Idle || state.chapter != Chapter::Frenzy) {
        return;
    }
    state.phase = Phase::Rampage;
    state.targetsLeft = kFrenzyTargets;
    // One window per body, so the size of the chapter is its length. There is
    // no leg to price here: the corner is the phone.
    state.stepsLeft = frenzySteps(kFrenzyTargets);
}

void culled(State& state) {
    if (state.phase != Phase::Rampage) {
        return;
    }
    // Guarded against an underflow as well as against the phase, and the two
    // are different failures. A shotgun spread is three pellets on one step,
    // so the last two bodies can land together: one arriving after
    // `advanceChapter` has run is stopped by the phase, one arriving on the
    // same step as the first is stopped here.
    if (state.targetsLeft > 0) {
        --state.targetsLeft;
    }
    if (state.targetsLeft == 0) {
        advanceChapter(state);
    }
}

void failed(State& state) {
    if (!underway(state)) {
        return;
    }
    // `chapter` is deliberately untouched: failing costs the job and nothing
    // else, because a main mission that ends the run is one most players see
    // the front half of once.
    state.phase = Phase::Idle;
    state.stepsLeft = 0;
    // And the bodies still owed, on the same terms as the clock: a residue
    // left behind is a count sitting on a chapter nobody has started.
    state.targetsLeft = 0;
}

bool phoneLive(const State& state) {
    return state.phase == Phase::Idle;
}

bool offered(const State& state, std::uint8_t chapterCount) {
    // Both halves are load-bearing and neither implies the other. `Complete`
    // parks the chapter on the last REAL entry rather than on `Count`, so the
    // bound alone would keep ringing the final phone forever; and an Idle
    // chapter past the last row is the out-of-range read this exists to stop.
    return phoneLive(state)
        && static_cast<std::uint8_t>(state.chapter) < chapterCount;
}

}  // namespace top_down_city::contract
