/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <cstdint>

/**
 * @file PongRules.h
 * @brief Engine-free, deterministic court rules for the local 1P-vs-CPU match.
 *
 * The scene owns the match: it advances the ball, both paddles, the score and
 * the serve. This header is that whole authority with no engine, Arduino,
 * display or networking dependency, so the host test suite can replay a match
 * on a desktop and the board runs exactly the same arithmetic.
 *
 * Everything is fixed point Q8.8 (`Fixed` is a signed 32-bit integer with eight
 * fractional bits). There is deliberately no `float` and no `double` anywhere in
 * this layer: the ESP32 and the host have to produce byte-identical simulation
 * results, and the two cores do not necessarily agree on floating point
 * rounding or on the width of `long double`. The simulation never leaves that
 * domain: the accessors hand the renderer the same Q8.8 value, and only the
 * renderer's integer shift turns it into a pixel.
 *
 * A match advances by `step(deltaUs)` calls, never by reading a clock. The scene
 * feeds a fixed 60 Hz step (kStepDtUs) from its accumulator, so a slow frame
 * cannot change how far the ball travelled.
 *
 * The phase set is the one a local match has. There is no waiting gate to open
 * and no pause to leave: a fresh match counts down immediately.
 *
 * Slot 0 is the player and slot 1 is the CPU (game/PongCpu.h) in the production
 * configuration. The button path for slot 1 is kept because the suite needs a
 * way to place the right paddle by hand, and the constructor flag selects
 * between the two.
 */
namespace pong::game {

/// Fixed-point scalar: Q8.8 signed 32-bit integer.
using Fixed = std::int32_t;

/// Fractional bits in a `Fixed` value.
inline constexpr int kFixedShift = 8;

/// One whole unit (one pixel, or one pixel per second) in Q8.8.
inline constexpr Fixed kOne = 1 << kFixedShift;

/// Microseconds in one second, the denominator of every velocity integration.
inline constexpr std::int64_t kMicrosPerSecond = 1000000;

/**
 * @brief Integrates a Q8.8 per-second value over `deltaUs`, rounding to nearest.
 * @param perSecond Velocity, or any per-second rate, in Q8.8.
 * @param deltaUs Elapsed microseconds.
 * @return The travelled distance in Q8.8, rounded to the nearest 1/256 of a unit.
 *
 * Truncation would lose up to a fixed unit per sub-step and make the ball
 * quietly slower than 180 px/s; rounding keeps a 60 Hz step of the nominal
 * velocity exactly three pixels, which is what the tests assume. The rounding is
 * symmetric around zero so a leftward and a rightward ball travel the same
 * distance.
 *
 * It lives here, in the court header, because three layers integrate the same
 * way: the ball, the player paddle and the CPU paddle. A second copy of the
 * rounding rule would be a second way for the host and the board to disagree.
 */
inline constexpr Fixed scaleByTime(Fixed perSecond, std::uint32_t deltaUs) {
    const std::int64_t scaled =
        static_cast<std::int64_t>(perSecond) * static_cast<std::int64_t>(deltaUs);
    if (scaled >= 0) {
        return static_cast<Fixed>((scaled + kMicrosPerSecond / 2) / kMicrosPerSecond);
    }
    return static_cast<Fixed>(-((-scaled + kMicrosPerSecond / 2) / kMicrosPerSecond));
}

#ifndef PONG_COURT_WIDTH
#error "pong: PONG_COURT_WIDTH must be defined by the environment (240 or 320)"
#endif

/// Width of the logical court in pixels: 240 on the square panel, 320 on the
/// ILI9341 after its quarter turn.
inline constexpr int kCourtWidth = PONG_COURT_WIDTH;

/// Height of the logical court in pixels; both panels are 240 tall.
inline constexpr int kCourtHeight = 240;

/// Height of the HUD score band; the play area starts below it.
inline constexpr int kHudHeight = 24;

/// Paddle width in pixels.
inline constexpr int kPaddleWidth = 6;

/// Paddle height in pixels.
inline constexpr int kPaddleHeight = 44;

/// Horizontal inset of both paddles from the court edge.
inline constexpr int kPaddleInset = 8;

/// Left edge of the left paddle (player one).
inline constexpr int kPaddleLeftX = kPaddleInset;

/// Left edge of the right paddle (the CPU in the production configuration).
inline constexpr int kPaddleRightX = kCourtWidth - kPaddleInset - kPaddleWidth;

/// Ball collision radius; the ball is drawn as a square of side 2 * radius.
inline constexpr int kBallRadius = 3;

/// Ball centre at serve time (court centre), so the centre line stays centred.
inline constexpr Fixed kBallCenterX = (kCourtWidth / 2) * kOne;

/// Ball centre at serve time (centre of the play area, below the HUD).
inline constexpr Fixed kBallCenterY = 132 * kOne;

/// Ball speed in Q8.8 pixels per second.
inline constexpr Fixed kBallSpeed = 180 * kOne;

/// Paddle speed in Q8.8 pixels per second.
inline constexpr Fixed kPaddleSpeed = 190 * kOne;

/// Largest deflection angle off a paddle, in degrees.
inline constexpr int kMaxBounceAngleDeg = 60;

/// Points needed to win the single set.
inline constexpr std::uint8_t kWinScore = 5;

/// Pre-serve countdown, in milliseconds.
inline constexpr std::uint32_t kCountdownMs = 1200;

/// Gap between a point and the next serve, in milliseconds.
inline constexpr std::uint32_t kPointPauseMs = 800;

/// Nominal fixed step, in microseconds (60 Hz).
inline constexpr std::uint32_t kStepDtUs = 16667;

/// Held direction towards the top of the court.
inline constexpr std::uint8_t kButtonLeft = 1u << 0;

/// Held direction towards the bottom of the court.
inline constexpr std::uint8_t kButtonRight = 1u << 1;

static_assert(kCourtWidth == 240 || kCourtWidth == 320,
              "pong: PONG_COURT_WIDTH must be 240 (ST7789) or 320 (ILI9341 quarter turn)");

/**
 * @enum Phase
 * @brief Match phase.
 *
 * A local match has no waiting gate and no suspended state, so a freshly built
 * match is already in Countdown.
 */
enum class Phase : std::uint8_t {
    Countdown,  ///< A serve is imminent but the ball is not live.
    Play,       ///< The ball is live.
    Point,      ///< A rally ended; the next serve is pending.
    MatchOver   ///< Someone reached kWinScore; nothing moves.
};

/**
 * @enum EventType
 * @brief Discrete things the scene turns into sound, particles or a repaint.
 *
 * These are the only reliable edges the scene consumes, so they are edges,
 * never state: the caller polls them once and reacts.
 */
enum class EventType : std::uint8_t {
    PaddleHit,    ///< The ball bounced off a paddle.
    WallBounce,   ///< The ball bounced off the top or bottom wall.
    Goal,         ///< A rally ended and a point was awarded.
    ServeStarted, ///< The ball was placed at the centre and made live.
    MatchOver     ///< A side reached kWinScore.
};

/**
 * @struct Event
 * @brief One discrete match event.
 *
 * `player` carries the meaningful owner when there is one (paddle owner, goal
 * scorer, server, match winner) and 0xFF when the event belongs to the court
 * rather than to a side.
 */
struct Event {
    EventType type = EventType::MatchOver;  ///< What happened.
    std::uint8_t player = 0xFF;             ///< 0 (left), 1 (right) or 0xFF.
};

}  // namespace pong::game

// The CPU policy reads the court primitives above -- the paddle geometry,
// kPaddleSpeed and scaleByTime() -- and `Rules` below owns one, so it is
// declared between the two. The dependency only runs one way: game/PongCpu.h
// uses this header, and never includes it back.
#include "game/PongCpu.h"

namespace pong::game {

/**
 * @class Rules
 * @brief Authoritative, engine-free court simulation.
 *
 * The class owns the ball, both paddles, the score, the phase machine and the
 * serve sequence. It has no clock and no allocation: the same sequence of
 * `setPlayerInput()` and `step()` calls always produces the same state on every
 * build, because the only one-sided "random" value in the match is the CPU's
 * imperfection, and that comes from a seeded word rather than from a clock. The
 * difficulty is part of that state too: the same script at the same level
 * replays the same match. The scene feeds it the D-pad on slot 0, the CPU drives
 * slot 1, then the scene reads the accessors for rendering and `pollEvent()` for
 * feedback.
 */
class Rules {
public:
    /// Capacity of the event ring. Old events are kept, new ones are dropped.
    static constexpr int kMaxEvents = 8;

    /**
     * @brief Builds a fresh match in Countdown.
     * @param firstServer Side that serves first (0 = left, anything else is
     *        normalised to the right).
     * @param cpuEnabled True for the production match, where the CPU plays slot
     *        1. False hands slot 1 back to `setPlayerInput()`, which is what a
     *        test does when it needs to place the right paddle by hand.
     * @param level CPU strength; Medium is the default and predicts the shot.
     */
    explicit Rules(std::uint8_t firstServer = 0, bool cpuEnabled = true,
                   Difficulty level = Difficulty::Medium);

    /**
     * @brief Selects the CPU difficulty for this match.
     * @param level CPU strength; any value outside the enum falls back to
     *        Medium through `cpuTuning()`.
     *
     * The caller owns the match reset: the rules store the level and re-tune the
     * controller (rewinding its accumulator and noise stream), and the scene
     * calls `resetMatch()` afterwards so the score, the serve and the paddles
     * start from the same place. `resetMatch()` preserves the level, which is
     * what makes the selection survive a rebuild.
     */
    void setDifficulty(Difficulty level);

    /**
     * @brief The CPU level this match is playing.
     * @return The level passed to the constructor or `setDifficulty()`.
     */
    [[nodiscard]] Difficulty difficulty() const;

    /**
     * @brief Returns to Countdown at 0-0 with a still ball.
     *
     * Also clears the event ring, because events queued by the previous match
     * (a stale MatchOver in particular) must never be replayed, and rewinds the
     * CPU controller, so a rebuilt match is a replay and not a continuation. The
     * selected difficulty is kept: a reset is a new match at the same level.
     */
    void resetMatch();

    /**
     * @brief Replaces one slot's held direction.
     * @param player 0 (left) or 1 (right); other values are ignored.
     * @param buttons Mask of kButtonLeft (towards the top) and kButtonRight
     *        (towards the bottom). Holding both cancels out.
     *
     * Slot 1 is stored but ignored while the CPU is enabled, which is the
     * game's production configuration: the only way to move the right paddle in
     * a real match is the opponent. A `Rules` built with `cpuEnabled = false`
     * does read it, and that is the mode the tests use to steer slot 1.
     */
    void setPlayerInput(std::uint8_t player, std::uint8_t buttons);

    /**
     * @brief Advances the match by one fixed step.
     * @param deltaUs Elapsed microseconds for this step.
     *
     * All phase timers, paddle movement and ball movement happen here and only
     * here. The call is clamped to at most 100 ms internally: the caller owns
     * catch-up, and an unbounded delta would otherwise buy a long sub-step loop
     * for a single stalled frame.
     */
    void step(std::uint32_t deltaUs);

    /**
     * @brief Starts a new set after MatchOver.
     *
     * Ignored outside MatchOver so a stray button press during a rally cannot
     * wipe the score. Scores go back to 0-0 and the phase to Countdown. The
     * selected difficulty is kept.
     */
    void restart();

    /**
     * @brief Current phase.
     * @return The phase machine's current state.
     */
    [[nodiscard]] Phase phase() const;

    /**
     * @brief Milliseconds left before the serve goes live.
     * @return Remaining countdown time while in Countdown, 0 in every other
     *         phase.
     *
     * Read-only view of the countdown timer the HUD paints as 3-2-1. It is
     * derived from the same accumulator `step()` advances, so the number the
     * player sees cannot disagree with the step that starts the rally.
     */
    [[nodiscard]] std::uint32_t countdownRemainingMs() const;

    /**
     * @brief Score of one side.
     * @param player 0 or 1; any other value yields 0.
     * @return Points scored by that side.
     */
    [[nodiscard]] std::uint8_t score(std::uint8_t player) const;

    /**
     * @brief Ball centre X in Q8.8, for the renderer.
     * @return Ball centre X.
     */
    [[nodiscard]] Fixed ballX() const;

    /**
     * @brief Ball centre Y in Q8.8, for the renderer.
     * @return Ball centre Y.
     */
    [[nodiscard]] Fixed ballY() const;

    /**
     * @brief Paddle top edge in Q8.8, for the renderer.
     * @param player 0 or 1; any other value yields 0.
     * @return Paddle top edge Y.
     */
    [[nodiscard]] Fixed paddleY(std::uint8_t player) const;

    /**
     * @brief Pops the oldest queued event.
     * @param out Destination, written only when an event is available.
     * @return true when an event was popped; false when the ring is empty.
     *
     * The ring holds kMaxEvents entries and drops the newest when full: during
     * a burst the beginning of the story (the serve, the paddle hit) is more
     * useful than the tail.
     */
    bool pollEvent(Event& out);

private:
    /// Side that serves the first ball of a set.
    std::uint8_t firstServer_ = 0;
    /// Number of serves already started; indexes the angle sequence.
    std::uint8_t serveCount_ = 0;
    /// Horizontal direction of the next serve: +1 right, -1 left.
    std::int8_t serveDir_ = 1;
    /// Current phase.
    Phase phase_ = Phase::Countdown;
    /// Time already spent in the current timed phase.
    std::uint32_t phaseElapsedUs_ = 0;
    std::uint8_t score1_ = 0;
    std::uint8_t score2_ = 0;
    Fixed ballX_ = kBallCenterX;
    Fixed ballY_ = kBallCenterY;
    Fixed ballVX_ = 0;
    Fixed ballVY_ = 0;
    /// Paddle top edge per slot, in Q8.8.
    Fixed paddleY_[2] = {0, 0};
    /// Held button mask per slot.
    std::uint8_t input_[2] = {0, 0};
    /// The opponent that plays slot 1 while cpuEnabled_ is true.
    CpuPaddle cpu_;
    /// Selected CPU difficulty; part of the deterministic match state.
    Difficulty level_ = Difficulty::Medium;
    /// Whether slot 1 is the CPU instead of a second button-driven paddle.
    bool cpuEnabled_ = true;
    /// Fixed-size event ring.
    Event events_[kMaxEvents];
    std::uint8_t eventHead_ = 0;
    std::uint8_t eventCount_ = 0;

    /// Places the ball at the centre, still, and both paddles at mid-court.
    void centreActors();

    /// Places the ball at the centre and makes it live with the next serve.
    void beginServe();

    /// Moves both paddles for one step: the player from its buttons, the CPU
    /// from the ball when it is enabled. Both end up inside the play area.
    void updatePaddles(std::uint32_t deltaUs);

    /// Moves the ball in sub-steps of at most one pixel, resolving collisions.
    void advanceBall(std::uint32_t deltaUs);

    /// Bounces the ball off the top or bottom wall when it crossed one.
    void bounceWalls();

    /// Bounces the ball off one paddle when it is overlapping and approaching.
    bool hitPaddle(std::uint8_t player);

    /// Awards a point and ends the match when a side reaches kWinScore.
    void awardPoint(std::uint8_t scorer);

    /// Awards a point when the ball fully crossed a court edge.
    bool resolveGoal();

    /// Appends one event, dropping it when the ring is full.
    void pushEvent(EventType type, std::uint8_t player);
};

}  // namespace pong::game
