/*
 * TouchControlsScene.cpp - The touch gesture pipeline, made visible.
 */
#include "TouchControlsScene.h"

#include <core/Engine.h>

#include <cstdio>

namespace pr32 = pixelroot32;

/*
 * The Engine instance lives in the platform header that main.cpp selected. The
 * scene reads it for one thing only: the current TouchState of touch id 0,
 * which is the state machine's own variable and is not carried on any event.
 */
extern pr32::core::Engine engine;

namespace touch_controls {

namespace gfx   = pixelroot32::graphics;
namespace input = pixelroot32::input;

namespace {

using gfx::Color;

/*
 * Layout. Both environments run a 240x320 panel, so these are constants rather
 * than fractions of getLogicalWidth(): a readout of fixed-width text lines does
 * not scale gracefully, and pretending otherwise would be the lie.
 *
 * Vertically the screen is four bands: header, touch surface, counters, log.
 */
constexpr int16_t kScreenWidth = 240;

constexpr int16_t kTitleY = 3;
constexpr int16_t kStateY = 13;

constexpr int16_t kSurfaceY      = 22;
constexpr int16_t kSurfaceHeight = 152;

constexpr int16_t kCountersY      = 178;
constexpr int16_t kCountersHeight = 52;

constexpr int16_t kLogY      = 233;
constexpr int16_t kLogHeight = 86;

/** Left inset shared by every panel's text. */
constexpr int16_t kPad = 6;

/** Progress bar that fills as a press approaches LONG_PRESS_THRESHOLD. */
constexpr int16_t kBarLabelY = 150;
constexpr int16_t kBarY      = 160;
constexpr int16_t kBarHeight = 8;

/** Counter grid: four rows of two columns, inside the counters panel. */
constexpr int16_t kCounterRowY[4]   = {182, 194, 206, 218};
constexpr int16_t kCounterColumnX[2] = {kPad, 124};

constexpr int16_t kLogHeaderY = 236;
constexpr int16_t kLogFirstY  = 248;
constexpr int16_t kLogPitch   = 10;

/** Radius of the dot that follows the live touch point. */
constexpr int16_t kMarkerRadius = 4;

/*
 * Colours.
 *
 * Color::Black is palette index 0, which packs to the byte the 8bpp framebuffer
 * reads as transparent: anything drawn in it shows up under SDL2 and vanishes
 * on the ESP32. Nothing here is Black, and the background is Navy for that
 * reason and not for taste.
 */
constexpr Color kBackground = Color::Navy;
constexpr Color kBorder     = Color::Gray;
constexpr Color kTitleText  = Color::White;
constexpr Color kHintText   = Color::Gray;

}  // namespace

// --- Scene lifecycle ---------------------------------------------------------

void TouchControlsScene::init() {
    Scene::init();

    gfx::setPalette(gfx::PaletteType::PR32);

    state_          = input::TouchState::Idle;
    touchX_         = 0;
    touchY_         = 0;
    hasTouchPoint_  = false;
    pressX_         = 0;
    pressY_         = 0;
    pressActive_    = false;
    pressElapsedMs_ = 0;

    logCount_ = 0;
    logHead_  = 0;
    sequence_ = 0;

    for (uint8_t i = 0; i < kGestureCount; ++i) {
        counters_[i] = 0;
    }
}

void TouchControlsScene::update(unsigned long deltaTime) {
    Scene::update(deltaTime);

#if PIXELROOT32_ENABLE_TOUCH
    /*
     * Stage 2's own variable, read straight from the dispatcher. This is the
     * one call in the demo that the feature flag actually gates: with
     * PIXELROOT32_ENABLE_TOUCH at 0 the Engine has no getTouchDispatcher() and
     * no dispatcher member, so the guard is around the call site, not around
     * TouchEventDispatcher itself -- that class carries no #if at all.
     */
    state_ = engine.getTouchDispatcher().getTouchState(0);
#endif

    if (state_ == input::TouchState::Idle) {
        pressActive_    = false;
        pressElapsedMs_ = 0;
    } else if (pressActive_) {
        pressElapsedMs_ += deltaTime;
    }
}

// --- Pipeline output ---------------------------------------------------------

void TouchControlsScene::onUnconsumedTouchEvent(const input::TouchEvent& event) {
    /*
     * An event another consumer already claimed must not act twice. The UI
     * system is compiled out here, so nothing marks events today -- but
     * Scene::processTouchEvents() runs UIManager::processEvents() before this
     * hook whenever it is on, and skipping this check is how a button press
     * also lands on the scene behind it.
     */
    if (event.isConsumed()) return;

    const uint8_t type = event.type;
    if (type == 0 || type > kGestureCount) return;

    touchX_        = event.x;
    touchY_        = event.y;
    hasTouchPoint_ = true;

    using T = input::TouchEventType;
    switch (event.getType()) {
        case T::TouchDown:
            pressX_         = event.x;
            pressY_         = event.y;
            pressActive_    = true;
            pressElapsedMs_ = 0;
            break;

        case T::TouchUp:
        case T::DragEnd:
            pressActive_ = false;
            break;

        default:
            break;
    }

    // Capped so the printed field never widens and reflows the column.
    if (counters_[type - 1] < 9999) {
        ++counters_[type - 1];
    }

    ++sequence_;

    log_[logHead_].sequence = sequence_;
    log_[logHead_].x        = event.x;
    log_[logHead_].y        = event.y;
    log_[logHead_].type     = type;

    logHead_ = static_cast<uint8_t>((logHead_ + 1) % kLogCapacity);
    if (logCount_ < kLogCapacity) {
        ++logCount_;
    }
}

// --- Naming ------------------------------------------------------------------

const char* TouchControlsScene::gestureName(uint8_t type) {
    switch (static_cast<input::TouchEventType>(type)) {
        case input::TouchEventType::TouchDown:   return "TOUCHDOWN";
        case input::TouchEventType::TouchUp:     return "TOUCHUP";
        case input::TouchEventType::Click:       return "CLICK";
        case input::TouchEventType::DoubleClick: return "DBLCLICK";
        case input::TouchEventType::LongPress:   return "LONGPRESS";
        case input::TouchEventType::DragStart:   return "DRAGSTART";
        case input::TouchEventType::DragMove:    return "DRAGMOVE";
        case input::TouchEventType::DragEnd:     return "DRAGEND";
        default:                                 return "?";
    }
}

Color TouchControlsScene::gestureColor(uint8_t type) {
    switch (static_cast<input::TouchEventType>(type)) {
        case input::TouchEventType::TouchDown:   return Color::Cyan;
        case input::TouchEventType::TouchUp:     return Color::White;
        case input::TouchEventType::Click:       return Color::LightGreen;
        case input::TouchEventType::DoubleClick: return Color::Yellow;
        case input::TouchEventType::LongPress:   return Color::Orange;
        case input::TouchEventType::DragStart:   return Color::Magenta;
        case input::TouchEventType::DragMove:    return Color::Gray;
        case input::TouchEventType::DragEnd:     return Color::LightRed;
        default:                                 return Color::Gray;
    }
}

const char* TouchControlsScene::stateName(input::TouchState state) {
    switch (state) {
        case input::TouchState::Idle:      return "IDLE";
        case input::TouchState::Pressed:   return "PRESSED";
        case input::TouchState::LongPress: return "LONGPRESS";
        case input::TouchState::Dragging:  return "DRAGGING";
        default:                           return "?";
    }
}

// --- Rendering ---------------------------------------------------------------

void TouchControlsScene::draw(gfx::Renderer& renderer) {
    // Background first, base class last: Scene::draw() paints the scene's
    // entities, so a fill placed after it would overpaint them.
    renderer.drawFilledRectangle(0, 0, renderer.getLogicalWidth(),
                                 renderer.getLogicalHeight(), kBackground);

    drawHeader(renderer);
    drawSurface(renderer);
    drawCounters(renderer);
    drawLog(renderer);

    Scene::draw(renderer);
}

void TouchControlsScene::drawHeader(gfx::Renderer& renderer) {
    renderer.drawText("TOUCH PIPELINE", kPad, kTitleY, kTitleText, 1);

    std::snprintf(textBuffer_, sizeof(textBuffer_), "EVENTS %u",
                  static_cast<unsigned>(sequence_));
    renderer.drawText(textBuffer_, 160, kTitleY, kHintText, 1);

    // Held time is the scene's own count, not a pipeline value -- see the note
    // on pressElapsedMs_ in the header.
    const unsigned long heldMs = pressElapsedMs_ > 9999UL ? 9999UL : pressElapsedMs_;
    std::snprintf(textBuffer_, sizeof(textBuffer_), "STATE %-9s HELD %4lu MS",
                  stateName(state_), heldMs);
    renderer.drawText(textBuffer_, kPad, kStateY, kTitleText, 1);
}

void TouchControlsScene::drawSurface(gfx::Renderer& renderer) {
    renderer.drawRectangle(0, kSurfaceY, kScreenWidth, kSurfaceHeight, kBorder);

    renderer.drawText("TOUCH AND DRAG ANYWHERE", kPad, kSurfaceY + 5, kHintText, 1);

    // The thresholds are printed from the engine's own constants so the screen
    // can never disagree with the state machine that enforces them.
    std::snprintf(textBuffer_, sizeof(textBuffer_), "CLICK<%uMS  DOUBLE<%uMS",
                  static_cast<unsigned>(input::TouchTiming::CLICK_MAX_DURATION),
                  static_cast<unsigned>(input::TouchTiming::DOUBLE_CLICK_INTERVAL));
    renderer.drawText(textBuffer_, kPad, kSurfaceY + 15, kHintText, 1);

    std::snprintf(textBuffer_, sizeof(textBuffer_), "HOLD %uMS  DRAG>%uPX",
                  static_cast<unsigned>(input::TouchTiming::LONG_PRESS_THRESHOLD),
                  static_cast<unsigned>(input::TouchTiming::DRAG_THRESHOLD));
    renderer.drawText(textBuffer_, kPad, kSurfaceY + 25, kHintText, 1);

    // Long-press progress. Empty whenever the state machine says Idle.
    const int16_t barWidth = static_cast<int16_t>(kScreenWidth - 2 * kPad);
    renderer.drawText("LONG PRESS PROGRESS", kPad, kBarLabelY, kHintText, 1);
    renderer.drawRectangle(kPad, kBarY, barWidth, kBarHeight, kBorder);

    if (pressActive_) {
        const unsigned long threshold =
            static_cast<unsigned long>(input::TouchTiming::LONG_PRESS_THRESHOLD);
        const unsigned long held = pressElapsedMs_ > threshold ? threshold : pressElapsedMs_;
        const int16_t filled =
            static_cast<int16_t>((held * static_cast<unsigned long>(barWidth - 2)) / threshold);
        if (filled > 0) {
            const Color fill = (state_ == input::TouchState::LongPress) ? Color::Orange
                                                                       : Color::Yellow;
            renderer.drawFilledRectangle(kPad + 1, kBarY + 1, filled, kBarHeight - 2, fill);
        }
    }

    if (!hasTouchPoint_) return;

    const Color crosshair = (state_ == input::TouchState::Dragging)  ? Color::Magenta
                            : (state_ == input::TouchState::LongPress) ? Color::Orange
                            : (state_ == input::TouchState::Pressed)   ? Color::Yellow
                                                                       : Color::Gray;

    // The crosshair lines stay inside the surface panel so they never scribble
    // across the counters or the log; the dot follows the finger everywhere.
    const int16_t top    = static_cast<int16_t>(kSurfaceY + 1);
    const int16_t bottom = static_cast<int16_t>(kSurfaceY + kSurfaceHeight - 2);

    renderer.drawLine(touchX_, top, touchX_, bottom, crosshair);
    if (touchY_ > top && touchY_ < bottom) {
        renderer.drawLine(1, touchY_, kScreenWidth - 2, touchY_, crosshair);
    }

    renderer.drawCircle(touchX_, touchY_, kMarkerRadius, crosshair);
    renderer.drawPixel(touchX_, touchY_, crosshair);

    /*
     * While a press is live, ring the origin at DRAG_THRESHOLD px. Leaving that
     * ring is the exact moment the state machine stops calling this a press and
     * starts calling it a drag, which is otherwise invisible.
     */
    if (pressActive_ && state_ == input::TouchState::Pressed) {
        renderer.drawCircle(pressX_, pressY_,
                            static_cast<int>(input::TouchTiming::DRAG_THRESHOLD),
                            Color::Cyan);
    }
}

void TouchControlsScene::drawCounters(gfx::Renderer& renderer) {
    renderer.drawRectangle(0, kCountersY, kScreenWidth, kCountersHeight, kBorder);

    for (uint8_t i = 0; i < kGestureCount; ++i) {
        const uint8_t type = static_cast<uint8_t>(i + 1);
        const int16_t x    = kCounterColumnX[i / 4];
        const int16_t y    = static_cast<int16_t>(kCounterRowY[i % 4]);

        std::snprintf(textBuffer_, sizeof(textBuffer_), "%-9s %4u",
                      gestureName(type), static_cast<unsigned>(counters_[i]));

        // A gesture that has never fired is dimmed, so the ones still to be
        // provoked -- DoubleClick and LongPress, usually -- are obvious.
        const Color color = (counters_[i] == 0) ? kHintText : gestureColor(type);
        renderer.drawText(textBuffer_, x, y, color, 1);
    }
}

void TouchControlsScene::drawLog(gfx::Renderer& renderer) {
    renderer.drawRectangle(0, kLogY, kScreenWidth, kLogHeight, kBorder);
    renderer.drawText("RECENT EVENTS, NEWEST FIRST", kPad, kLogHeaderY, kHintText, 1);

    if (logCount_ == 0) {
        renderer.drawText("(QUEUE EMPTY - TOUCH THE SCREEN)", kPad, kLogFirstY,
                          kHintText, 1);
        return;
    }

    for (int i = 0; i < static_cast<int>(logCount_); ++i) {
        const int slot =
            (static_cast<int>(logHead_) + kLogCapacity - 1 - i) % kLogCapacity;
        const LogEntry& entry = log_[slot];

        std::snprintf(textBuffer_, sizeof(textBuffer_), "%3u %-9s %3d,%3d",
                      static_cast<unsigned>(entry.sequence), gestureName(entry.type),
                      static_cast<int>(entry.x), static_cast<int>(entry.y));

        renderer.drawText(textBuffer_, kPad,
                          static_cast<int16_t>(kLogFirstY + i * kLogPitch),
                          gestureColor(entry.type), 1);
    }
}

}  // namespace touch_controls
