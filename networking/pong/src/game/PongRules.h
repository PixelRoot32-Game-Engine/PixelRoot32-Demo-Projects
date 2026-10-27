/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <cstdint>

/**
 * @file PongRules.h
 * @brief Engine-free, deterministic Pong match rules shared by the host and the tests.
 *
 * The CYD owns the match: it is the only node that advances the ball, the
 * paddles, the score and the serve. This header is that whole authority with no
 * engine, Arduino or networking dependency, so the host test suite can replay a
 * match on a desktop and the CYD firmware runs exactly the same arithmetic.
 *
 * Everything is fixed point Q8.8 (`Fixed` is a signed 32-bit integer with eight
 * fractional bits). There is deliberately no `float` and no `double` anywhere in
 * this layer: the ESP32 and the host have to produce byte-identical simulation
 * results, and the two cores do not necessarily agree on floating point
 * rounding or on the width of `long double`. Positions and velocities are
 * `Fixed`; only `snapshot()` and the accessors used by the renderer leave that
 * domain, and only through integer shifts.
 *
 * A match advances by `step(deltaUs)` calls, never by reading a clock. The host
 * feeds a fixed 60 Hz step (kStepDtUs) from its accumulator, so a slow frame
 * cannot change how far the ball travelled and a paused match is simply a match
 * that receives no steps.
 */
namespace netpong::game {

/// Fixed-point scalar: Q8.8 signed 32-bit integer.
using Fixed = std::int32_t;

/// Fractional bits in a `Fixed` value.
inline constexpr int kFixedShift = 8;

/// One whole unit (one pixel, or one pixel per second) in Q8.8.
inline constexpr Fixed kOne = 1 << kFixedShift;

/// Width of the design canvas in pixels.
inline constexpr int kCourtWidth = 320;

/// Height of the design canvas in pixels.
inline constexpr int kCourtHeight = 240;

/// Height of the HUD score band; the play area starts below it.
inline constexpr int kHudHeight = 24;

/// Paddle width in pixels.
inline constexpr int kPaddleWidth = 6;

/// Paddle height in pixels.
inline constexpr int kPaddleHeight = 44;

/// Horizontal inset of both paddles from the court edge.
inline constexpr int kPaddleInset = 8;

/// Left edge of player one's paddle.
inline constexpr int kPaddleLeftX = kPaddleInset;

/// Left edge of player two's paddle.
inline constexpr int kPaddleRightX = kCourtWidth - kPaddleInset - kPaddleWidth;

/// Ball collision radius; the ball is drawn as a square of side 2 * radius.
inline constexpr int kBallRadius = 3;

/// Ball centre at serve time (court centre).
inline constexpr Fixed kBallCenterX = 160 * kOne;

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

/// Held direction towards the top of the court (bit value of `net::kButtonLeft`).
inline constexpr std::uint8_t kButtonLeft = 1u << 0;

/// Held direction towards the bottom of the court (bit value of `net::kButtonRight`).
inline constexpr std::uint8_t kButtonRight = 1u << 1;

/**
 * @enum Phase
 * @brief Match phase.
 *
 * The numeric values are the wire values of `net::kPhase*`; the test suite pins
 * that mapping so a controller can copy the byte straight into a STATE frame.
 */
enum class Phase : std::uint8_t {
    WaitingForPlayers,  ///< No match yet; the ball is parked at the centre.
    Countdown,          ///< A serve is imminent but the ball is not live.
    Play,               ///< The ball is live.
    Point,              ///< A rally ended; the next serve is pending.
    MatchOver,          ///< Someone reached kWinScore; nothing moves.
    Paused              ///< Suspended (peer loss); nothing moves.
};

/**
 * @enum EventType
 * @brief Discrete things the host forwards to the controllers and the scene.
 *
 * These are the only reliable sends in the demo, so they are edges, never
 * state: the receiver polls them once and reacts with sound, particles or an
 * OLED repaint.
 */
enum class EventType : std::uint8_t {
    PaddleHit,   ///< The ball bounced off a paddle.
    WallBounce,  ///< The ball bounced off the top or bottom wall.
    Goal,        ///< A rally ended and a point was awarded.
    ServeStarted,///< The ball was placed at the centre and made live.
    MatchOver,   ///< A side reached kWinScore.
    Paused,      ///< The match was suspended.
    Resumed      ///< The match left the suspended state.
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
    EventType type = EventType::Paused;  ///< What happened.
    std::uint8_t player = 0xFF;          ///< 0 (P1/left), 1 (P2/right) or 0xFF.
};

/**
 * @struct Snapshot
 * @brief Wire-shaped view of the match, ready to fill a STATE frame.
 *
 * Positions and velocities are whole pixels (or pixels per second) obtained by
 * an arithmetic shift of the Q8.8 state, and every numeric field is clamped to
 * `int16_t` so a pathological value can never wrap into the opposite sign on
 * the wire. `p1Y`/`p2Y` are the paddle *centre* Y, matching `net::State`;
 * `paddleY()` is the top edge the renderer needs.
 */
struct Snapshot {
    std::uint16_t seq = 0;   ///< Caller's frame counter, preserved untouched.
    std::uint8_t phase = 0;  ///< `static_cast<std::uint8_t>(Phase)`.
    std::int16_t ballX = 0;  ///< Ball centre X in whole pixels.
    std::int16_t ballY = 0;  ///< Ball centre Y in whole pixels.
    std::int16_t ballVX = 0; ///< Horizontal velocity in whole pixels per second.
    std::int16_t ballVY = 0; ///< Vertical velocity in whole pixels per second.
    std::int16_t p1Y = 0;    ///< Left paddle centre Y in whole pixels.
    std::int16_t p2Y = 0;    ///< Right paddle centre Y in whole pixels.
    std::uint8_t score1 = 0; ///< Points scored by player one.
    std::uint8_t score2 = 0; ///< Points scored by player two.
    std::uint8_t flags = 0;  ///< Reserved presentation bits; always 0 here.
};

/**
 * @class Rules
 * @brief Authoritative, engine-free Pong simulation.
 *
 * The class owns the ball, both paddles, the score, the phase machine and the
 * serve sequence. It has no clock, no RNG and no allocation: the same sequence
 * of `setPlayerInput()` and `step()` calls always produces the same state on
 * every build. The host adapter (T3) feeds it controller input and reads
 * `snapshot()` and `pollEvent()`; the scene (T5) reads the fixed-point accessors
 * for smooth rendering.
 */
class Rules {
public:
    /// Capacity of the event ring. Old events are kept, new ones are dropped.
    static constexpr int kMaxEvents = 8;

    /**
     * @brief Builds a fresh match in WaitingForPlayers.
     * @param firstServer Side that serves first (0 = P1/left, anything else is
     *        normalised to P2/right).
     */
    explicit Rules(std::uint8_t firstServer = 0);

    /**
     * @brief Returns to WaitingForPlayers at 0-0 with a still ball.
     *
     * Also clears the event ring, because events queued by the previous match
     * (a stale MatchOver in particular) must never be replayed.
     */
    void resetMatch();

    /**
     * @brief Starts the pre-serve countdown once both controllers are present.
     * @param bothReady True when both peers have completed the handshake.
     *
     * Ignored outside WaitingForPlayers: a peer that reconnects mid-match must
     * not restart the countdown behind the players' backs.
     */
    void setPlayersReady(bool bothReady);

    /**
     * @brief Replaces one player's held direction.
     * @param player 0 (P1/left) or 1 (P2/right); other values are ignored.
     * @param buttons Mask of kButtonLeft (towards the top) and kButtonRight
     *        (towards the bottom). Holding both cancels out.
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
     * @brief Suspends Play, Countdown or Point, remembering the phase.
     *
     * Queueing the Paused event is what tells the controllers to paint a frozen
     * board, so it happens once per transition, not once per call.
     */
    void pause();

    /**
     * @brief Leaves Paused and restarts a serve countdown.
     *
     * Resuming into Countdown rather than straight into Play gives both players
     * a moment to move their hands back after the interruption.
     */
    void resume();

    /**
     * @brief Starts a new set after MatchOver.
     *
     * Ignored outside MatchOver so a stray button press during a rally cannot
     * wipe the score. Scores go back to 0-0 and the phase to Countdown.
     */
    void restart();

    /**
     * @brief Current phase.
     * @return The phase machine's current state.
     */
    [[nodiscard]] Phase phase() const;

    /**
     * @brief True while the match is suspended.
     * @return `phase() == Phase::Paused`.
     */
    [[nodiscard]] bool paused() const;

    /**
     * @brief Milliseconds left before the serve goes live.
     * @return Remaining countdown time while in Countdown, 0 in every other
     *         phase.
     *
     * Read-only view of the countdown timer the host HUD paints as 3-2-1. It is
     * derived from the same accumulator `step()` advances, so the number the
     * player sees cannot disagree with the step that starts the rally. A phase
     * that paused a countdown reports 0: `resume()` restarts the countdown from
     * the top instead of inheriting frozen time.
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
     * @brief Whole-pixel view of the match, shaped for a STATE frame.
     * @param seq Frame counter to stamp on the snapshot; returned unchanged.
     * @return Snapshot with every numeric field mapped to whole pixels and
     *         clamped into `int16_t`.
     */
    [[nodiscard]] Snapshot snapshot(std::uint16_t seq) const;

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
    Phase phase_ = Phase::WaitingForPlayers;
    /// Phase pause() interrupted, kept for diagnostics and resume policy.
    Phase pausedFrom_ = Phase::WaitingForPlayers;
    /// Time already spent in the current timed phase.
    std::uint32_t phaseElapsedUs_ = 0;
    std::uint8_t score1_ = 0;
    std::uint8_t score2_ = 0;
    Fixed ballX_ = kBallCenterX;
    Fixed ballY_ = kBallCenterY;
    Fixed ballVX_ = 0;
    Fixed ballVY_ = 0;
    /// Paddle top edge per player, in Q8.8.
    Fixed paddleY_[2] = {0, 0};
    /// Held button mask per player.
    std::uint8_t input_[2] = {0, 0};
    /// Fixed-size event ring.
    Event events_[kMaxEvents];
    std::uint8_t eventHead_ = 0;
    std::uint8_t eventCount_ = 0;

    /// Places the ball at the centre, still, and both paddles at mid-court.
    void centreActors();

    /// Places the ball at the centre and makes it live with the next serve.
    void beginServe();

    /// Moves both paddles for one step and clamps them to the play area.
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

}  // namespace netpong::game
