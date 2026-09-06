/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <core/Scene.h>
#include <graphics/Color.h>
#include <graphics/Renderer.h>
#include <gameplay/ObjectPool.h>

#include <cstdint>
#include <type_traits>

#if !PIXELROOT32_ENABLE_GAMEPLAY_OBJECT_POOL
#error "object_pool needs -D PIXELROOT32_ENABLE_GAMEPLAY_OBJECT_POOL=1 (see lib/platformio.ini)"
#endif

namespace object_pool_demo {

/**
 * @struct Projectile
 * @brief One pooled object: a shot travelling until it leaves the field.
 *
 * Deliberately has no default constructor. `ObjectPool::acquire()` forwards
 * its arguments straight into placement new, so a pooled type only needs the
 * constructor the call site actually uses — see the static_assert below,
 * which fails to compile the moment that stops being true.
 *
 * Position and velocity are Q8 fixed point (256 units = 1 pixel), so motion
 * stays sub-pixel smooth without a float on the ESP32 path.
 */
struct Projectile {
    Projectile(int32_t startX, int32_t startY, int32_t velocityX, int32_t velocityY,
               pixelroot32::graphics::Color shotTint)
        : x(startX), y(startY), vx(velocityX), vy(velocityY), ageMs(0), tint(shotTint) {}

    int32_t x;   ///< Centre X, Q8 pixels.
    int32_t y;   ///< Centre Y, Q8 pixels.
    int32_t vx;  ///< Velocity X, Q8 pixels per second.
    int32_t vy;  ///< Velocity Y, Q8 pixels per second.

    uint16_t ageMs;                        ///< Time alive, used to fade the trail.
    pixelroot32::graphics::Color tint;     ///< Colour, taken from the slot index.
};

static_assert(!std::is_default_constructible<Projectile>::value,
              "Projectile is intentionally not default constructible: acquire() "
              "forwards to a real constructor, so the pool never needs one.");

/**
 * @class ObjectPoolScene
 * @brief A spawn/despawn loop driven entirely by ObjectPool<Projectile, N>.
 *
 * The pool is the whole demo. Firing is `acquire()` (which may hand back
 * `nullptr`), leaving the field is `releaseAt()` from inside the very walk
 * that found the projectile, and B is `reset()`. Nothing here allocates after
 * `init()`, and nothing iterates the pool with a raw index loop.
 */
class ObjectPoolScene : public pixelroot32::core::Scene {
public:
    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

private:
    /// Slot count. Small enough that holding A saturates it in about a second,
    /// which is the point: a full pool must be a state you can reach on
    /// purpose, not a corner case you read about.
    static constexpr uint16_t kCapacity = 24;

    using ProjectilePool = pixelroot32::gameplay::ObjectPool<Projectile, kCapacity>;

    // The pool is a plain member of the scene, NOT arena memory and NOT
    // arenaNew'd. Scene::resetState() rewinds the arena's bump offset without
    // running a single destructor, so an arena-backed pool would keep claiming
    // slots the arena has already handed to somebody else. ObjectPool.h states
    // this as an unsupported configuration; the README explains why.
    ProjectilePool pool_;

    // Layout check, not a magic number: N <= 32 means exactly one uint32_t
    // liveness word, and the two uint16_t counters pack beside it, so the pool
    // costs its slots plus 8 bytes of bookkeeping on both the 64-bit native
    // build and the 32-bit ESP32 (Projectile holds no pointers).
    static_assert(kCapacity <= 32, "The +8 bookkeeping figure below assumes a single liveness word");
    static_assert(sizeof(ProjectilePool) == sizeof(Projectile) * kCapacity + 8,
                  "ObjectPool layout is slots + one uint32_t bitmask + two uint16_t counters");

    // --- Screen layout (128x128, 5x7 font: 6 px per character advance) -----
    static constexpr int kTextX = 4;
    static constexpr int kTitleY = 2;
    static constexpr int kLiveTextY = 11;
    static constexpr int kSlotMapY = 20;
    static constexpr int kSlotMapCell = 5;   ///< 24 cells * 5 px = 120 px wide.
    static constexpr int kSlotMapHeight = 5;
    static constexpr int kRefusedTextY = 27;
    static constexpr int kFieldX = 3;
    static constexpr int kFieldY = 36;
    static constexpr int kFieldWidth = 122;
    static constexpr int kFieldHeight = 74;
    static constexpr int kStatusTextY = 112;
    static constexpr int kControlsTextY = 120;

    /// Button indices as wired by InputConfig in the platform headers.
    static constexpr uint8_t kButtonUp = 0;
    static constexpr uint8_t kButtonDown = 1;
    static constexpr uint8_t kButtonLeft = 2;
    static constexpr uint8_t kButtonRight = 3;
    static constexpr uint8_t kButtonA = 4;
    static constexpr uint8_t kButtonB = 5;

    void moveEmitter(int32_t stepMs);
    void handleFiring(int32_t stepMs);
    void fireOne();
    void stepProjectiles(int32_t stepMs);
    void refreshHud();

    void drawSlotMap(pixelroot32::graphics::Renderer& renderer) const;
    void drawEmitter(pixelroot32::graphics::Renderer& renderer) const;

    /// Emitter position, Q8 pixels.
    int32_t emitterX_ = 0;
    int32_t emitterY_ = 0;

    /// Aim direction as a Q8 unit vector; the D-pad rewrites it while held.
    int32_t aimX_ = 0;
    int32_t aimY_ = -256;

    /// Milliseconds until the next shot is allowed. Holding A fires at a
    /// steady rate instead of once per frame, so the demo behaves the same at
    /// 60 fps on SDL2 and at ~25 fps on the ESP32.
    int32_t fireCooldownMs_ = 0;

    /// Every acquire() that came back nullptr since the last reset(). Counted
    /// and printed, because a full pool is an outcome, not an error.
    uint32_t refusedCount_ = 0;

    /// Live count the HUD strings were last built from, so snprintf runs on
    /// the frames the numbers actually changed and not on every frame.
    uint16_t hudLiveCount_ = 0xFFFFu;
    uint32_t hudRefusedCount_ = 0xFFFFFFFFu;

    char liveText_[24] = {0};
    char refusedText_[24] = {0};
    char statusText_[24] = {0};
    pixelroot32::graphics::Color statusColor_ = pixelroot32::graphics::Color::Green;
};

} // namespace object_pool_demo
