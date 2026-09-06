/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#include "StateMachineScene.h"

#include <core/Engine.h>
#include <core/Log.h>

#include <cstdio>

namespace pr32 = pixelroot32;

extern pr32::core::Engine engine;

namespace state_machine_demo {

namespace gfx = pr32::graphics;
using gfx::Color;

namespace {

/// Q8 fixed point: 256 units = 1 pixel.
constexpr int kFixedShift = 8;

/// Largest millisecond figure the HUD prints, so a five-digit field never
/// runs past the right edge of a 128 px screen.
constexpr uint32_t kMaxPrintedMs = 99999;

inline uint32_t clampMs(uint32_t ms) {
    return (ms > kMaxPrintedMs) ? kMaxPrintedMs : ms;
}

} // namespace

// -----------------------------------------------------------------------------
// The state table
// -----------------------------------------------------------------------------
// A class-static const array: it lives in .rodata for the whole life of the
// program. configure() stores a pointer to this and never copies it, so the
// one shape that must NOT be used is a table local to a function.
//
// Rows are matched by State::id, not by position, so this order is a
// readability choice and nothing depends on it.
const StateMachineScene::StateMachine::State
StateMachineScene::kStates[StateMachineScene::kStateCount] = {
    { &StateMachineScene::onEnterIdle,   &StateMachineScene::onUpdateIdle,   &StateMachineScene::onAnyExit, StateMachineScene::kStateIdle   },
    { &StateMachineScene::onEnterWalk,   &StateMachineScene::onUpdateWalk,   &StateMachineScene::onAnyExit, StateMachineScene::kStateWalk   },
    { &StateMachineScene::onEnterAttack, &StateMachineScene::onUpdateAttack, &StateMachineScene::onAnyExit, StateMachineScene::kStateAttack },
    { &StateMachineScene::onEnterHurt,   &StateMachineScene::onUpdateHurt,   &StateMachineScene::onAnyExit, StateMachineScene::kStateHurt   }
};

// -----------------------------------------------------------------------------
// Scene lifecycle
// -----------------------------------------------------------------------------

void StateMachineScene::init() {
    Scene::init();

    gfx::setPalette(gfx::PaletteType::PR32);

    actor_.x = static_cast<int32_t>((kActorMinX + kActorMaxX) / 2) << kFixedShift;
    actor_.y = static_cast<int32_t>((kActorMinY + kActorMaxY) / 2) << kFixedShift;
    actor_.facingX = 1;
    actor_.shake = 0;

    inputX_ = 0;
    inputY_ = 0;
    chainStressActive_ = false;
    transitionCount_ = 0;

    logCount_ = 0;
    logHead_ = kLogLines - 1;   // the first push wraps round to index 0

    // reset() is teardown, not a transition: it fires no onExit, and it does
    // NOT clear the transition-overflow counter, which is monotonic by
    // design. Calling it here makes init() re-entrant, because start()
    // asserts the machine is not already running.
    fsm_.reset();
    fsm_.configure(this, kStates, kStateCount);

    // start() fires only onEnter, with fromState == kInvalidStateId.
    fsm_.start(kStateIdle);
    pushLog("START > IDLE");

    refreshHud();

    pr32::core::logging::log("StateMachineScene: four states, one const table");
}

void StateMachineScene::update(unsigned long deltaTime) {
    Scene::update(deltaTime);

    auto& input = engine.getInputManager();

    readInput();

    const bool bHeld = input.isButtonDown(kButtonB);
    const bool aPressed = input.isButtonPressed(kButtonA);
    const bool bPressed = input.isButtonPressed(kButtonB);

    // Transitions requested here, BEFORE fsm_.update(), are the well-behaved
    // shape: the state that is entered gets this frame's delta credited to
    // it. The two timed states transition from inside onUpdate instead, which
    // is the shape that loses a frame.
    if (aPressed && bHeld) {
        triggerChainOverflow();
    } else if (aPressed) {
        // Attack cannot interrupt Hurt; Hurt interrupts everything.
        if (fsm_.getCurrentState() != kStateHurt) {
            enterOrRestart(kStateAttack);
        }
    } else if (bPressed) {
        enterOrRestart(kStateHurt);
    }

    // The real delta, not a clamped one: time-in-state should be wall time.
    // Only the movement integration is clamped, in moveActor().
    fsm_.update(deltaTime);

    refreshHud();
}

void StateMachineScene::draw(pr32::graphics::Renderer& renderer) {
    // Background first, then the base call. Scene::draw() paints the scene's
    // entities, so a fill placed after it would overpaint them. init() and
    // update() are the overrides that call their base first, not draw().
    renderer.drawFilledRectangle(0, 0, renderer.getLogicalWidth(), renderer.getLogicalHeight(), Color::Black);

    Scene::draw(renderer);

    renderer.drawTextCentered("STATE MACHINE", kTitleY, Color::White, 1);

    renderer.drawFilledRectangle(kFieldX, kFieldY, kFieldWidth, kFieldHeight, Color::Navy);
    drawActor(renderer);
    drawTimerBar(renderer);
    renderer.drawRectangle(kFieldX, kFieldY, kFieldWidth, kFieldHeight, Color::Gray);

    renderer.drawText(nowText_, kTextX, kNowY, stateColor(fsm_.getCurrentState()), 1);
    renderer.drawText(prevText_, kTextX, kPrevY, Color::Gray, 1);
    renderer.drawText(overflowText_, kTextX, kOverflowY,
                      (fsm_.getTransitionOverflowCount() > 0) ? Color::Red : Color::DarkGreen, 1);

    renderer.drawLine(kTextX, kDividerY, kFieldX + kFieldWidth - 3, kDividerY, Color::Gray);
    drawLog(renderer);

    renderer.drawText("A:ATK B:HIT B+A:CHAIN", 1, kControlsY, Color::Gray, 1);
}

// -----------------------------------------------------------------------------
// State callbacks
//
// Every one of these is a `static` member so it converts to the plain
// function pointer the table wants. A capturing lambda does not convert, and
// neither does a non-static member function; hence the `owner` cast that
// opens each body.
// -----------------------------------------------------------------------------

void StateMachineScene::onEnterIdle(void* owner, StateId fromState) {
    (void)fromState;
    auto* self = static_cast<StateMachineScene*>(owner);
    self->actor_.shake = 0;
}

void StateMachineScene::onUpdateIdle(void* owner, unsigned long deltaTime, uint32_t timeInStateMs) {
    (void)deltaTime;
    (void)timeInStateMs;
    auto* self = static_cast<StateMachineScene*>(owner);

    if (self->inputX_ != 0 || self->inputY_ != 0) {
        self->fsm_.requestState(kStateWalk);
    }
}

void StateMachineScene::onEnterWalk(void* owner, StateId fromState) {
    (void)fromState;
    auto* self = static_cast<StateMachineScene*>(owner);

    if (self->inputX_ != 0) {
        self->actor_.facingX = self->inputX_;
    }
}

void StateMachineScene::onUpdateWalk(void* owner, unsigned long deltaTime, uint32_t timeInStateMs) {
    (void)timeInStateMs;
    auto* self = static_cast<StateMachineScene*>(owner);

    if (self->inputX_ == 0 && self->inputY_ == 0) {
        self->fsm_.requestState(kStateIdle);
        return;
    }

    if (self->inputX_ != 0) {
        self->actor_.facingX = self->inputX_;
    }
    self->moveActor(deltaTime);
}

void StateMachineScene::onEnterAttack(void* owner, StateId fromState) {
    (void)fromState;
    auto* self = static_cast<StateMachineScene*>(owner);
    self->actor_.shake = 0;

    // The chain-overflow demonstration, and one of the only two places this
    // demo requests a transition from inside onEnter. requestState() here
    // does not recurse: it records the request, and the requestState() call
    // that started the whole thing drains it on its next iteration.
    if (self->chainStressActive_) {
        self->fsm_.requestState(kStateHurt);
    }
}

void StateMachineScene::onUpdateAttack(void* owner, unsigned long deltaTime, uint32_t timeInStateMs) {
    (void)deltaTime;
    auto* self = static_cast<StateMachineScene*>(owner);

    // Transitioning from inside onUpdate: this frame's delta has already been
    // added to Attack's time-in-state, and requestState() then zeroes it. Idle
    // therefore begins with getTimeInState() == 0, having consumed none of
    // this frame, and its own onUpdate does not run until the next one.
    if (timeInStateMs >= kAttackMs) {
        self->fsm_.requestState(kStateIdle);
    }
}

void StateMachineScene::onEnterHurt(void* owner, StateId fromState) {
    (void)fromState;
    auto* self = static_cast<StateMachineScene*>(owner);

    // Knock back, away from the facing direction.
    self->actor_.x -= static_cast<int32_t>(self->actor_.facingX) * (6 << kFixedShift);
    self->clampActor();
    self->actor_.shake = 1;

    if (self->chainStressActive_) {
        self->fsm_.requestState(kStateAttack);
    }
}

void StateMachineScene::onUpdateHurt(void* owner, unsigned long deltaTime, uint32_t timeInStateMs) {
    (void)deltaTime;
    auto* self = static_cast<StateMachineScene*>(owner);

    // A visible wobble driven straight off time-in-state.
    self->actor_.shake = ((timeInStateMs / 60u) % 2u == 0u) ? 1 : -1;

    if (timeInStateMs >= kHurtMs) {
        self->fsm_.requestState(kStateIdle);
    }
}

void StateMachineScene::onAnyExit(void* owner, StateId toState) {
    auto* self = static_cast<StateMachineScene*>(owner);

    // Inside onExit the machine has not moved yet: current_ is still the
    // state being left, time-in-state is still that state's dwell, and
    // toState is where the machine is about to go. One shared onExit can
    // therefore log every transition without a per-state copy.
    const StateId from = self->fsm_.getCurrentState();
    const uint32_t dwell = self->fsm_.getTimeInState();

    ++self->transitionCount_;
    self->actor_.shake = 0;

    // The chain-stress burst produces eight transitions inside a single
    // requestState(); logging each would flush the four-line ring and hide
    // what the overflow counter is there to show.
    if (!self->chainStressActive_) {
        self->pushTransitionLog(from, toState, dwell);
    }
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

void StateMachineScene::readInput() {
    auto& input = engine.getInputManager();

    inputX_ = 0;
    inputY_ = 0;

    if (input.isButtonDown(kButtonLeft)) {
        inputX_ = -1;
    } else if (input.isButtonDown(kButtonRight)) {
        inputX_ = 1;
    }

    if (input.isButtonDown(kButtonUp)) {
        inputY_ = -1;
    } else if (input.isButtonDown(kButtonDown)) {
        inputY_ = 1;
    }
}

void StateMachineScene::enterOrRestart(StateId target) {
    // requestState(getCurrentState()) returns true and does nothing at all:
    // no onExit, no onEnter, and time-in-state keeps running. restartState()
    // is the verb for a real re-entry, and the log shows the difference —
    // pressing A during Attack prints ATTACK>ATTACK and resets the timer bar.
    if (fsm_.getCurrentState() == target) {
        fsm_.restartState();
    } else {
        fsm_.requestState(target);
    }
}

void StateMachineScene::triggerChainOverflow() {
    // Seed the ping-pong with whichever of the two states we are not in, so
    // the first request is never the no-op case.
    const StateId seed = (fsm_.getCurrentState() == kStateAttack) ? kStateHurt : kStateAttack;

    chainStressActive_ = true;
    fsm_.requestState(seed);   // drains eight chained transitions, then stops
    chainStressActive_ = false;

    pushLog("B+A CHAIN x8");
    refreshHud();
}

void StateMachineScene::pushLog(const char* text) {
    logHead_ = static_cast<uint8_t>((logHead_ + 1) % kLogLines);
    std::snprintf(logText_[logHead_], kLogTextLength, "%s", text);
    if (logCount_ < kLogLines) {
        ++logCount_;
    }
}

void StateMachineScene::pushTransitionLog(StateId from, StateId to, uint32_t dwellMs) {
    logHead_ = static_cast<uint8_t>((logHead_ + 1) % kLogLines);
    std::snprintf(logText_[logHead_], kLogTextLength, "%s>%s %lums",
                  stateName(from), stateName(to),
                  static_cast<unsigned long>(clampMs(dwellMs)));
    if (logCount_ < kLogLines) {
        ++logCount_;
    }
}

void StateMachineScene::refreshHud() {
    // snprintf into fixed char members: no std::string, no heap. Called once
    // per frame from update() rather than from draw(), so the render pass
    // stays pure drawing.
    std::snprintf(nowText_, sizeof(nowText_), "NOW %-6s %lums",
                  stateName(fsm_.getCurrentState()),
                  static_cast<unsigned long>(clampMs(fsm_.getTimeInState())));

    std::snprintf(prevText_, sizeof(prevText_), "PREV %-6s T:%u",
                  stateName(fsm_.getPreviousState()),
                  static_cast<unsigned>(transitionCount_));

    std::snprintf(overflowText_, sizeof(overflowText_), "CHAIN OVERFLOW %u",
                  static_cast<unsigned>(fsm_.getTransitionOverflowCount()));
}

void StateMachineScene::moveActor(unsigned long deltaTime) {
    unsigned long step = deltaTime;
    if (step > kMaxStepMs) {
        step = kMaxStepMs;
    }

    const int32_t travel = ((kWalkSpeed << kFixedShift) * static_cast<int32_t>(step)) / 1000;
    actor_.x += static_cast<int32_t>(inputX_) * travel;
    actor_.y += static_cast<int32_t>(inputY_) * travel;
    clampActor();
}

void StateMachineScene::clampActor() {
    const int32_t minX = static_cast<int32_t>(kActorMinX) << kFixedShift;
    const int32_t maxX = static_cast<int32_t>(kActorMaxX) << kFixedShift;
    const int32_t minY = static_cast<int32_t>(kActorMinY) << kFixedShift;
    const int32_t maxY = static_cast<int32_t>(kActorMaxY) << kFixedShift;

    if (actor_.x < minX) actor_.x = minX;
    if (actor_.x > maxX) actor_.x = maxX;
    if (actor_.y < minY) actor_.y = minY;
    if (actor_.y > maxY) actor_.y = maxY;
}

// -----------------------------------------------------------------------------
// Drawing
// -----------------------------------------------------------------------------

void StateMachineScene::drawActor(pr32::graphics::Renderer& renderer) const {
    const StateId state = fsm_.getCurrentState();
    const int cx = static_cast<int>(actor_.x >> kFixedShift) + actor_.shake;
    const int cy = static_cast<int>(actor_.y >> kFixedShift);
    const int left = cx - kActorHalfWidth;
    const int top = cy - kActorHalfHeight;

    if (state == kStateAttack) {
        const int bladeX = (actor_.facingX > 0) ? (cx + kActorHalfWidth + 1)
                                                : (cx - kActorHalfWidth - 6);
        renderer.drawFilledRectangle(bladeX, cy - 1, 6, 3, Color::Yellow);
    }

    renderer.drawFilledRectangle(left, top, kActorHalfWidth * 2, kActorHalfHeight * 2, stateColor(state));
    renderer.drawRectangle(left, top, kActorHalfWidth * 2, kActorHalfHeight * 2, Color::White);

    // Facing marker.
    renderer.drawFilledRectangle(cx + ((actor_.facingX > 0) ? 1 : -3), cy - 3, 2, 2, Color::Black);
}

void StateMachineScene::drawTimerBar(pr32::graphics::Renderer& renderer) const {
    const StateId state = fsm_.getCurrentState();
    uint32_t duration = 0;
    if (state == kStateAttack) {
        duration = kAttackMs;
    } else if (state == kStateHurt) {
        duration = kHurtMs;
    } else {
        return;   // Idle and Walk never time out; an empty track would lie.
    }

    const int trackX = kFieldX + 1;
    const int trackWidth = kFieldWidth - 2;
    renderer.drawFilledRectangle(trackX, kTimerBarY, trackWidth, kTimerBarHeight, Color::Gray);

    uint32_t elapsed = fsm_.getTimeInState();
    if (elapsed > duration) {
        elapsed = duration;
    }
    const int filled = static_cast<int>((elapsed * static_cast<uint32_t>(trackWidth)) / duration);
    if (filled > 0) {
        renderer.drawFilledRectangle(trackX, kTimerBarY, filled, kTimerBarHeight, stateColor(state));
    }
}

void StateMachineScene::drawLog(pr32::graphics::Renderer& renderer) const {
    for (uint8_t i = 0; i < logCount_; ++i) {
        const uint8_t index = static_cast<uint8_t>((logHead_ + kLogLines - i) % kLogLines);
        renderer.drawText(logText_[index], kTextX, kLogY + i * kLogStep,
                          (i == 0) ? Color::White : Color::Gray, 1);
    }
}

// -----------------------------------------------------------------------------
// Small lookups
// -----------------------------------------------------------------------------

const char* StateMachineScene::stateName(StateId id) {
    switch (id) {
        case kStateIdle:   return "IDLE";
        case kStateWalk:   return "WALK";
        case kStateAttack: return "ATTACK";
        case kStateHurt:   return "HURT";
        default:           return "-";   // kInvalidStateId, before the first transition
    }
}

gfx::Color StateMachineScene::stateColor(StateId id) {
    switch (id) {
        case kStateIdle:   return Color::Cyan;
        case kStateWalk:   return Color::Green;
        case kStateAttack: return Color::Orange;
        case kStateHurt:   return Color::Red;
        default:           return Color::Gray;
    }
}

} // namespace state_machine_demo
