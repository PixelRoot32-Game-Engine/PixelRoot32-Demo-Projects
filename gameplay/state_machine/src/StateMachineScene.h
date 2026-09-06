/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <core/Scene.h>
#include <graphics/Color.h>
#include <graphics/Renderer.h>
#include <gameplay/StateMachine.h>

#include <cstdint>

#if !PIXELROOT32_ENABLE_GAMEPLAY_STATE_MACHINE
#error "state_machine needs -D PIXELROOT32_ENABLE_GAMEPLAY_STATE_MACHINE=1 (see lib/platformio.ini)"
#endif

namespace state_machine_demo {

/**
 * @struct Actor
 * @brief The single body the state machine drives.
 *
 * Position is Q8 fixed point (256 units = 1 pixel) so the walk reads smoothly
 * without a float on the ESP32 path. The struct holds no state id of its own:
 * the machine owns that, and every value here is something a state callback
 * writes.
 */
struct Actor {
    int32_t x = 0;        ///< Centre X, Q8 pixels.
    int32_t y = 0;        ///< Centre Y, Q8 pixels.
    int8_t facingX = 1;   ///< Last horizontal walk direction: -1 or +1.
    int8_t shake = 0;     ///< Horizontal offset in pixels, written by Hurt.
};

/**
 * @class StateMachineScene
 * @brief One `pixelroot32::gameplay::StateMachine` driving one actor, with
 *        every transition visible on screen.
 *
 * Four states — Idle, Walk, Attack, Hurt — live in `kStates`, a class-static
 * `const` table. The machine stores a pointer to it and never copies it, so
 * the table has to outlive the machine; a `static const` member lands in
 * flash and trivially does.
 *
 * The callbacks are raw function pointers taking `void* owner`, so they are
 * `static` members that cast `owner` back to `StateMachineScene*`. A
 * capturing lambda does not convert to that type and cannot be used here.
 */
class StateMachineScene : public pixelroot32::core::Scene {
public:
    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

private:
    using StateMachine = pixelroot32::gameplay::StateMachine;
    using StateId = pixelroot32::gameplay::StateId;

    // --- State ids --------------------------------------------------------
    // Plain constants rather than an enum class: the table stores a StateId
    // (uint8_t), and every comparison in this file is against these names.
    static constexpr StateId kStateIdle = 0;
    static constexpr StateId kStateWalk = 1;
    static constexpr StateId kStateAttack = 2;
    static constexpr StateId kStateHurt = 3;
    static constexpr uint8_t kStateCount = 4;

    /// How long Attack and Hurt last before they return to Idle on their own.
    static constexpr uint32_t kAttackMs = 400;
    static constexpr uint32_t kHurtMs = 600;

    /// Walk speed in pixels per second.
    static constexpr int32_t kWalkSpeed = 42;

    /// Upper bound on one integration step, so a stall cannot teleport the
    /// actor across the field.
    static constexpr unsigned long kMaxStepMs = 100;

    /// Button indices, in the order InputConfig takes them in the platform
    /// headers: Up, Down, Left, Right, A, B.
    static constexpr uint8_t kButtonUp = 0;
    static constexpr uint8_t kButtonDown = 1;
    static constexpr uint8_t kButtonLeft = 2;
    static constexpr uint8_t kButtonRight = 3;
    static constexpr uint8_t kButtonA = 4;
    static constexpr uint8_t kButtonB = 5;

    // --- Screen layout (128x128, 5x7 font: 6 px per character advance) -----
    static constexpr int kTitleY = 1;
    static constexpr int kFieldX = 2;
    static constexpr int kFieldY = 10;
    static constexpr int kFieldWidth = 124;
    static constexpr int kFieldHeight = 42;
    static constexpr int kTextX = 3;
    static constexpr int kNowY = 55;
    static constexpr int kPrevY = 64;
    static constexpr int kOverflowY = 73;
    static constexpr int kDividerY = 82;
    static constexpr int kLogY = 85;
    static constexpr int kLogStep = 8;
    static constexpr int kControlsY = 119;

    /// Bar showing time-in-state against a timed state's duration.
    static constexpr int kTimerBarY = 47;
    static constexpr int kTimerBarHeight = 3;

    // --- Actor bounds inside the field border ------------------------------
    static constexpr int kActorHalfWidth = 4;
    static constexpr int kActorHalfHeight = 5;
    static constexpr int kActorMinX = kFieldX + 1 + kActorHalfWidth;
    static constexpr int kActorMaxX = kFieldX + kFieldWidth - 2 - kActorHalfWidth;
    static constexpr int kActorMinY = kFieldY + 1 + kActorHalfHeight;
    static constexpr int kActorMaxY = kTimerBarY - 1 - kActorHalfHeight;

    /// Transition log: a fixed ring of formatted lines, newest first.
    static constexpr uint8_t kLogLines = 4;
    static constexpr uint8_t kLogTextLength = 24;

    // --- State callbacks --------------------------------------------------
    // StateMachine::EnterFn / UpdateFn / ExitFn are `void (*)(void* owner, ...)`.
    // These statics are the only legal shape for them.
    static void onEnterIdle(void* owner, StateId fromState);
    static void onUpdateIdle(void* owner, unsigned long deltaTime, uint32_t timeInStateMs);
    static void onEnterWalk(void* owner, StateId fromState);
    static void onUpdateWalk(void* owner, unsigned long deltaTime, uint32_t timeInStateMs);
    static void onEnterAttack(void* owner, StateId fromState);
    static void onUpdateAttack(void* owner, unsigned long deltaTime, uint32_t timeInStateMs);
    static void onEnterHurt(void* owner, StateId fromState);
    static void onUpdateHurt(void* owner, unsigned long deltaTime, uint32_t timeInStateMs);

    /// Shared by all four rows. During `onExit` the machine has not moved
    /// `current_` yet, so `getCurrentState()` is the state being left and
    /// `getTimeInState()` is how long it actually lasted.
    static void onAnyExit(void* owner, StateId toState);

    /// The caller-owned table. `configure()` binds a pointer to it and never
    /// copies it — a table with automatic storage duration would dangle.
    static const StateMachine::State kStates[kStateCount];

    // --- Helpers ----------------------------------------------------------
    void readInput();
    void enterOrRestart(StateId target);
    void triggerChainOverflow();
    void pushLog(const char* text);
    void pushTransitionLog(StateId from, StateId to, uint32_t dwellMs);
    void refreshHud();
    void moveActor(unsigned long deltaTime);
    void clampActor();

    void drawActor(pixelroot32::graphics::Renderer& renderer) const;
    void drawTimerBar(pixelroot32::graphics::Renderer& renderer) const;
    void drawLog(pixelroot32::graphics::Renderer& renderer) const;

    static const char* stateName(StateId id);
    static pixelroot32::graphics::Color stateColor(StateId id);

    StateMachine fsm_;
    Actor actor_;

    /// D-pad, sampled once per frame in update() so the state callbacks all
    /// see the same input for that frame.
    int8_t inputX_ = 0;
    int8_t inputY_ = 0;

    /// True only for the duration of the one requestState() call that the
    /// B+A combo makes. While set, Attack's and Hurt's onEnter each request
    /// the other, which is what drives the chain past the machine's cap.
    bool chainStressActive_ = false;

    uint16_t transitionCount_ = 0;

    char logText_[kLogLines][kLogTextLength] = {};
    uint8_t logCount_ = 0;
    uint8_t logHead_ = 0;   ///< Index of the newest entry.

    char nowText_[kLogTextLength] = {};
    char prevText_[kLogTextLength] = {};
    char overflowText_[kLogTextLength] = {};
};

} // namespace state_machine_demo
