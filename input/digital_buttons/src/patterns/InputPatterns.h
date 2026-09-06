/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <cstdint>

/**
 * @file InputPatterns.h
 * @brief The four input patterns this demo layers on top of the raw engine verbs.
 *
 * Nothing in this file includes an engine header, and nothing in it knows what
 * a button is: every type takes plain booleans and millisecond counts. That is
 * deliberate. `InputManager` answers four questions about *this frame*, and the
 * behaviour players actually feel — a menu that scrolls, a jump that survives
 * being pressed slightly early, a two-button special, a readout of how fast the
 * hardware will let you press — is built on top of those answers, not inside
 * them. Keeping that layer free of the engine is what lets `pio test -e
 * host_test` verify it with no display, no SDL2 and no board.
 *
 * Every type here is fixed-size and allocation-free, so the same code runs
 * unchanged in an ESP32 game loop.
 */
namespace digital_buttons {

/**
 * @class RepeatTimer
 * @brief Turns a held button into a key-repeat train.
 *
 * `isButtonDown()` is a level, not an edge: it is true on every frame the
 * button is held, so driving a menu cursor from it moves the cursor at the
 * frame rate. This is the standard fix — fire once immediately so the first
 * press feels instant, wait out an initial delay so a deliberate single press
 * does not repeat, then repeat at a fixed interval for as long as the button
 * is held.
 *
 * At most one repeat is reported per tick() even when the frame was long
 * enough to cover several intervals; a dropped frame must not teleport a
 * cursor. The leftover time is carried into the next interval so the train
 * does not drift.
 */
class RepeatTimer {
public:
    /**
     * @param initialDelayMs Quiet time between the first fire and the second.
     * @param intervalMs     Time between every fire after that. A zero
     *                       interval is clamped to 1 ms rather than dividing
     *                       by zero.
     */
    RepeatTimer(unsigned long initialDelayMs, unsigned long intervalMs);

    /**
     * @brief Advances the timer by one frame.
     * @param held true while the button is down (an `isButtonDown()` result).
     * @param dt   Frame time in milliseconds.
     * @return true on the frame a repeat fires, including the rising edge.
     */
    bool tick(bool held, unsigned long dt);

    /// Disarms the timer; the next held frame counts as a fresh rising edge.
    void reset();

private:
    unsigned long initialDelayMs_;
    unsigned long intervalMs_;
    unsigned long remaining_;  ///< Milliseconds left before the next fire.
    bool wasHeld_;
};

/**
 * @class InputBuffer
 * @brief Remembers a press for a short window so an early press still counts.
 *
 * A press that arrives one frame before the game is ready to accept it is,
 * from the player's point of view, a press the game ignored. Buffering the
 * press for a window and letting the consumer ask for it when it becomes
 * relevant is what makes a jump land on the frame you touched the ground
 * rather than the frame after.
 *
 * The buffer holds one press, not a queue: a second press replaces the first,
 * because a player mashing a button wants the latest intent honoured once, not
 * every attempt replayed.
 */
class InputBuffer {
public:
    /// @param windowMs How long a press stays consumable, inclusive of the edge.
    explicit InputBuffer(unsigned long windowMs);

    /// Records a press. @param now Current time in milliseconds.
    void press(unsigned long now);

    /**
     * @brief Consumes a buffered press if one is still fresh.
     * @param now Current time in milliseconds.
     * @return true exactly once per buffered press, and only inside the window.
     *
     * A press found to be stale is dropped by this call rather than left armed
     * for a later consumer — a buffer that fires late is worse than one that
     * does not fire at all.
     */
    bool consume(unsigned long now);

    /// Non-destructive form of consume(), for drawing the buffer's state.
    bool pending(unsigned long now) const;

    /// Forgets any buffered press without consuming it.
    void clear();

private:
    unsigned long windowMs_;
    unsigned long pressTime_;
    bool armed_;
};

/**
 * @class ComboDetector
 * @brief Fires when two button edges land close enough together in time.
 *
 * "Both at once" is not something a player can actually do, and it is not
 * something the engine reports either: `InputManager` debounces each button
 * independently, so two buttons pressed in the same physical instant routinely
 * produce their edges on different frames. A tolerance window is what turns
 * two nearby edges into one intent.
 */
class ComboDetector {
public:
    /// @param toleranceMs Largest gap between the two edges, inclusive.
    explicit ComboDetector(unsigned long toleranceMs);

    /**
     * @brief Advances the detector by one frame.
     * @param aEdge true on the frame the first button produced its edge.
     * @param bEdge true on the frame the second button produced its edge.
     * @param dt    Frame time in milliseconds.
     * @return true once, on the frame that completes the pair.
     *
     * Pending edges age by @p dt before the new edges are recorded, so an edge
     * fed with a @p dt larger than the tolerance has already expired when the
     * partner arrives.
     */
    bool feed(bool aEdge, bool bEdge, unsigned long dt);

    /// Forgets any half-finished combo.
    void reset();

    /// Age in milliseconds of the pending A edge (0 when none).
    unsigned long pendingAgeA() const { return haveA_ ? sinceA_ : 0; }

    /// Age in milliseconds of the pending B edge (0 when none).
    unsigned long pendingAgeB() const { return haveB_ ? sinceB_ : 0; }

private:
    unsigned long toleranceMs_;
    unsigned long sinceA_;
    unsigned long sinceB_;
    bool haveA_;
    bool haveB_;
};

/**
 * @class EdgeRateMeter
 * @brief Counts edges in the trailing second, and remembers the best result.
 *
 * A fixed ring of timestamps, no allocation and no division. The live rate is
 * how fast the input pipeline is delivering edges right now; the peak is the
 * number worth reading, because it survives the moment your thumb gets tired.
 */
class EdgeRateMeter {
public:
    /// Ring capacity. Also the highest rate the meter can report.
    static constexpr uint8_t kCapacity = 32;

    /// Length of the trailing window, in milliseconds.
    static constexpr unsigned long kWindowMs = 1000;

    EdgeRateMeter();

    /// Records one edge. @param now Current time in milliseconds.
    void edge(unsigned long now);

    /**
     * @brief Counts the edges inside the trailing window and updates the peak.
     * @param now Current time in milliseconds.
     * @return Edges strictly newer than @p now - kWindowMs, capped at kCapacity.
     *
     * Not const: reading the rate is what maintains peak(). The scene calls it
     * every frame, so the peak tracks every rate the meter ever reported.
     */
    uint8_t ratePerSecond(unsigned long now);

    /// Highest rate ever returned by ratePerSecond().
    uint8_t peak() const { return peak_; }

    /// Clears the history and the peak.
    void reset();

private:
    unsigned long stamps_[kCapacity];
    uint8_t head_;   ///< Next slot to write.
    uint8_t count_;  ///< Valid slots, saturating at kCapacity.
    uint8_t peak_;
};

} // namespace digital_buttons
