/*
 * TouchControlsScene.h - The touch gesture pipeline, made visible.
 *
 * The scene has no game in it. Its only job is to show what the engine's touch
 * pipeline produced, at the moment it produced it, so that the four stages
 * between the hardware and this file stop being a paragraph in a document:
 *
 *   1. TouchAdapter (XPT2046 / GT911 on hardware, SDL2_Drawer on native)
 *      turns a panel reading or a mouse event into a normalized TouchPoint.
 *   2. TouchStateMachine consumes those points and emits semantic gestures.
 *   3. TouchEventQueue holds them; the Engine drains it once per frame.
 *   4. Scene::processTouchEvents() offers each event to the UI system first,
 *      then hands whatever survives to onUnconsumedTouchEvent().
 *
 * The game never sees a raw touch. It sees the output of stage 4, which is the
 * single override below.
 */
#pragma once

#include <core/Scene.h>
#include <graphics/Color.h>
#include <graphics/Renderer.h>
#include <input/TouchEvent.h>
#include <input/TouchEventTypes.h>
#include <input/TouchStateMachine.h>

#include <cstdint>

namespace touch_controls {

/**
 * @class TouchControlsScene
 * @brief One screen that reports every gesture the touch state machine emits.
 *
 * Draws a crosshair at the live touch point, the current `TouchState`, a
 * counter per gesture type, and a scrolling log of the most recent events.
 * Everything is a `Renderer` primitive: the UI system is compiled out on
 * purpose, because a UIManager would consume events before this scene could
 * report them.
 *
 * Allocation-free by construction. The log is a fixed ring buffer of members
 * and every string is formatted into a member buffer at draw time.
 */
class TouchControlsScene : public pixelroot32::core::Scene {
public:
    /** @brief Clear the log, the counters and every trace of a previous touch. */
    void init() override;

    /**
     * @brief Per-frame update: read the state machine and age the press timer.
     * @param deltaTime Milliseconds since the previous frame.
     */
    void update(unsigned long deltaTime) override;

    /**
     * @brief Draw the readout.
     * @param renderer Renderer to draw through.
     */
    void draw(pixelroot32::graphics::Renderer& renderer) override;

    /**
     * @brief Record one gesture.
     * @param event Gesture reported by the engine's touch pipeline.
     */
    void onUnconsumedTouchEvent(const pixelroot32::input::TouchEvent& event) override;

private:
    /** @brief How many past events the log shows. Older ones fall off the end. */
    static constexpr uint8_t kLogCapacity = 7;

    /**
     * @brief Number of countable gesture types.
     *
     * `TouchEventType` runs None = 0 through DragEnd = 8, so the eight real
     * gestures occupy 1..8 and index into the counters as `type - 1`.
     */
    static constexpr uint8_t kGestureCount = 8;

    /** @brief One logged gesture. 8 bytes; the whole log is 56. */
    struct LogEntry {
        uint16_t sequence;  ///< Ordinal of the event since init(), for spotting repeats.
        int16_t x;          ///< Screen X the pipeline reported.
        int16_t y;          ///< Screen Y the pipeline reported.
        uint8_t type;       ///< TouchEventType, stored raw as the event stores it.
    };

    // --- Drawing helpers. Non-const because they format into textBuffer_. ---

    void drawHeader(pixelroot32::graphics::Renderer& renderer);
    void drawSurface(pixelroot32::graphics::Renderer& renderer);
    void drawCounters(pixelroot32::graphics::Renderer& renderer);
    void drawLog(pixelroot32::graphics::Renderer& renderer);

    /**
     * @brief Short display name for a gesture type.
     * @param type Raw `TouchEventType` value.
     * @return A static string, never null; "?" for anything unexpected.
     */
    static const char* gestureName(uint8_t type);

    /**
     * @brief Colour a gesture is drawn in, shared by its counter and its log line.
     * @param type Raw `TouchEventType` value.
     * @return A palette colour, never `Color::Black` (see the note in the .cpp).
     */
    static pixelroot32::graphics::Color gestureColor(uint8_t type);

    /**
     * @brief Name of a state machine state.
     * @param state State reported by the dispatcher.
     * @return A static string, never null.
     */
    static const char* stateName(pixelroot32::input::TouchState state);

    // --- Live pipeline state ---

    /// State of touch id 0, re-read from the dispatcher every frame.
    pixelroot32::input::TouchState state_ = pixelroot32::input::TouchState::Idle;

    int16_t touchX_ = 0;      ///< Last coordinate any gesture reported.
    int16_t touchY_ = 0;
    bool hasTouchPoint_ = false;  ///< False until the first gesture ever arrives.

    int16_t pressX_ = 0;      ///< Where the current (or last) press started.
    int16_t pressY_ = 0;
    bool pressActive_ = false;

    /**
     * @brief Milliseconds since TouchDown, accumulated from deltaTime.
     *
     * The dispatcher does not expose a press duration -- `getPressDuration` is
     * on TouchStateMachine, and TouchEventDispatcher forwards only getState()
     * and isTouchActive(). Counting frames here is the honest way to draw the
     * long-press progress bar without reaching past the public API.
     */
    unsigned long pressElapsedMs_ = 0;

    // --- Log and counters ---

    LogEntry log_[kLogCapacity] = {};
    uint8_t logCount_ = 0;   ///< Entries currently held, saturating at kLogCapacity.
    uint8_t logHead_ = 0;    ///< Ring cursor: the slot the next entry goes into.
    uint16_t sequence_ = 0;  ///< Total gestures received since init().

    uint16_t counters_[kGestureCount] = {};

    /// Scratch for snprintf. A member, so no frame allocates or touches the heap.
    char textBuffer_[40] = {};
};

}  // namespace touch_controls
