/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#include "ObjectPoolScene.h"

#include <core/Engine.h>
#include <core/Log.h>

#include <cstdio>

namespace pr32 = pixelroot32;

extern pr32::core::Engine engine;

namespace object_pool_demo {

namespace gfx = pr32::graphics;
using gfx::Color;

namespace {

/// Q8 fixed point: 256 units = 1 pixel, and also 256 = 1.0 for the aim vector.
constexpr int kFixedShift = 8;
constexpr int32_t kFixedOne = 1 << kFixedShift;

/// Q8 approximation of 1/sqrt(2), so a diagonal is not faster than a straight.
constexpr int32_t kDiagonal = 181;

/// Interior of the field border, in whole pixels. A projectile whose centre
/// leaves this rectangle is released.
constexpr int32_t kInnerLeft = 4;
constexpr int32_t kInnerTop = 37;
constexpr int32_t kInnerRight = 123;
constexpr int32_t kInnerBottom = 108;

constexpr int32_t kEmitterHalf = 3;      ///< Half the emitter square, in pixels.
constexpr int32_t kEmitterSpeed = 55;    ///< Pixels per second.
constexpr int32_t kProjectileSpeed = 55; ///< Pixels per second.
constexpr int kProjectileRadius = 2;

/// Milliseconds between two shots while A is held. At this rate the field
/// hands back fewer slots than firing takes, so holding A reaches a full pool
/// in about a second — which is the state worth showing.
constexpr int32_t kFireCooldownMs = 45;

/// How far behind itself a projectile draws its trail, in milliseconds of
/// travel. Also the age at which the trail reaches full length.
constexpr int32_t kTrailMs = 60;

/// Upper bound on one integration step, so a stall (a breakpoint, the first
/// frame after init) cannot teleport a projectile straight through the border
/// and leave it circling forever outside the field.
constexpr int32_t kMaxStepMs = 100;

/// One colour per slot index, so a projectile wears the colour of the slot it
/// occupies and the slot map below matches what is flying. Watching a colour
/// disappear and come back is watching a slot get recycled.
constexpr Color kSlotPalette[] = {
    Color::Cyan,
    Color::Green,
    Color::Yellow,
    Color::Orange,
    Color::Magenta,
    Color::LightGreen,
    Color::Blue,
    Color::LightRed
};
constexpr uint16_t kSlotPaletteSize = sizeof(kSlotPalette) / sizeof(kSlotPalette[0]);

constexpr Color slotColor(uint16_t index) {
    return kSlotPalette[index % kSlotPaletteSize];
}

} // namespace

void ObjectPoolScene::init() {
    // Scene::init() runs resetState() first, which clears entities and rewinds
    // the scene arena.
    Scene::init();

    // ObjectPool.h's supported reset contract: reset the pool *after* the base
    // Scene::init(), never before. Destructing pooled objects first would leave
    // the base entity list holding dangling pointers until clearEntities() ran.
    // (Nothing here registers as an Entity, but the ordering is the contract and
    // a demo that got it backwards would teach the wrong habit.)
    pool_.reset();

    gfx::setPalette(gfx::PaletteType::PR32);

    emitterX_ = ((kInnerLeft + kInnerRight) / 2) << kFixedShift;
    emitterY_ = (kInnerBottom - 8) << kFixedShift;
    aimX_ = 0;
    aimY_ = -kFixedOne;
    fireCooldownMs_ = 0;
    refusedCount_ = 0;

    hudLiveCount_ = 0xFFFFu;
    hudRefusedCount_ = 0xFFFFFFFFu;
    refreshHud();

    pr32::core::logging::log("ObjectPoolScene: sizeof(ObjectPool<Projectile,%u>) = %lu bytes"
                             " (sizeof(Projectile) = %lu)",
                             static_cast<unsigned>(kCapacity),
                             static_cast<unsigned long>(sizeof(ProjectilePool)),
                             static_cast<unsigned long>(sizeof(Projectile)));
}

void ObjectPoolScene::update(unsigned long deltaTime) {
    Scene::update(deltaTime);

    int32_t stepMs = static_cast<int32_t>(deltaTime);
    if (stepMs < 0) {
        stepMs = 0;
    } else if (stepMs > kMaxStepMs) {
        stepMs = kMaxStepMs;
    }

    auto& input = engine.getInputManager();

    // B frees the whole pool in one call. reset() runs ~Projectile() over every
    // live slot in ascending index order and clears the bitmask; it is safe on
    // an already-empty pool, so there is nothing to guard here.
    if (input.isButtonPressed(kButtonB)) {
        pool_.reset();
        refusedCount_ = 0;
    }

    moveEmitter(stepMs);
    handleFiring(stepMs);
    stepProjectiles(stepMs);

    // snprintf only on the frames where a printed number actually moved.
    if (pool_.size() != hudLiveCount_ || refusedCount_ != hudRefusedCount_) {
        refreshHud();
    }
}

void ObjectPoolScene::draw(gfx::Renderer& renderer) {
    // Background first, then the base call: Scene::draw() paints the scene's
    // entities, so filling after it would overpaint them. init() and update()
    // are the overrides that call their base first, not draw().
    renderer.drawFilledRectangle(0, 0, renderer.getLogicalWidth(), renderer.getLogicalHeight(),
                                 Color::Black);

    Scene::draw(renderer);

    renderer.drawTextCentered("OBJECT POOL", kTitleY, Color::White, 1);
    renderer.drawText(liveText_, kTextX, kLiveTextY, Color::White, 1);
    drawSlotMap(renderer);
    renderer.drawText(refusedText_, kTextX, kRefusedTextY,
                      refusedCount_ != 0 ? Color::Orange : Color::Gray, 1);

    renderer.drawRectangle(kFieldX, kFieldY, kFieldWidth, kFieldHeight, Color::Gray);

    // The iteration idiom. nextLive() walks the liveness bitmask, so it visits
    // exactly the live slots in ascending order; a raw `for (i = 0; i < N; ++i)`
    // would walk dead slots too and at() would hand back nullptr for them.
    for (uint16_t i = pool_.nextLive(0); i != ProjectilePool::kEnd;
         i = pool_.nextLive(static_cast<uint16_t>(i + 1))) {
        const Projectile& shot = *pool_.at(i);

        const int px = static_cast<int>(shot.x >> kFixedShift);
        const int py = static_cast<int>(shot.y >> kFixedShift);

        const int32_t trailMs = (shot.ageMs < kTrailMs) ? static_cast<int32_t>(shot.ageMs) : kTrailMs;
        const int tailX = static_cast<int>((shot.x - (shot.vx * trailMs) / 1000) >> kFixedShift);
        const int tailY = static_cast<int>((shot.y - (shot.vy * trailMs) / 1000) >> kFixedShift);

        renderer.drawLine(tailX, tailY, px, py, Color::Gray);
        renderer.drawFilledCircle(px, py, kProjectileRadius, shot.tint);
    }

    drawEmitter(renderer);

    renderer.drawText(statusText_, kTextX, kStatusTextY, statusColor_, 1);
    renderer.drawText("PAD:MOVE A:FIRE B:RST", 1, kControlsTextY, Color::Gray, 1);
}

void ObjectPoolScene::moveEmitter(int32_t stepMs) {
    auto& input = engine.getInputManager();

    int32_t dirX = 0;
    int32_t dirY = 0;
    if (input.isButtonDown(kButtonLeft))  { dirX -= kFixedOne; }
    if (input.isButtonDown(kButtonRight)) { dirX += kFixedOne; }
    if (input.isButtonDown(kButtonUp))    { dirY -= kFixedOne; }
    if (input.isButtonDown(kButtonDown))  { dirY += kFixedOne; }

    if (dirX == 0 && dirY == 0) {
        return; // Aim keeps its last direction; only movement stops.
    }

    if (dirX != 0 && dirY != 0) {
        dirX = (dirX * kDiagonal) / kFixedOne;
        dirY = (dirY * kDiagonal) / kFixedOne;
    }

    aimX_ = dirX;
    aimY_ = dirY;

    emitterX_ += (dirX * kEmitterSpeed * stepMs) / 1000;
    emitterY_ += (dirY * kEmitterSpeed * stepMs) / 1000;

    const int32_t minX = (kInnerLeft + kEmitterHalf) << kFixedShift;
    const int32_t maxX = (kInnerRight - kEmitterHalf) << kFixedShift;
    const int32_t minY = (kInnerTop + kEmitterHalf) << kFixedShift;
    const int32_t maxY = (kInnerBottom - kEmitterHalf) << kFixedShift;

    if (emitterX_ < minX) { emitterX_ = minX; }
    if (emitterX_ > maxX) { emitterX_ = maxX; }
    if (emitterY_ < minY) { emitterY_ = minY; }
    if (emitterY_ > maxY) { emitterY_ = maxY; }
}

void ObjectPoolScene::handleFiring(int32_t stepMs) {
    auto& input = engine.getInputManager();

    if (!input.isButtonDown(kButtonA)) {
        // Releasing A arms the next shot, so a tap always fires immediately.
        fireCooldownMs_ = 0;
        return;
    }

    if (fireCooldownMs_ > 0) {
        fireCooldownMs_ -= stepMs;
        return;
    }

    fireOne();
    fireCooldownMs_ = kFireCooldownMs;
}

void ObjectPoolScene::fireOne() {
    // Spawn just past the muzzle so a fresh shot is not hidden by the emitter.
    const int32_t muzzle = kEmitterHalf + 2;
    const int32_t spawnX = emitterX_ + aimX_ * muzzle;
    const int32_t spawnY = emitterY_ + aimY_ * muzzle;

    // acquire() forwards these five arguments to Projectile's constructor via
    // placement new. It returns nullptr when every slot is live: a normal
    // outcome of a fixed-capacity pool, counted here rather than asserted on.
    Projectile* shot = pool_.acquire(spawnX,
                                     spawnY,
                                     aimX_ * kProjectileSpeed,
                                     aimY_ * kProjectileSpeed,
                                     Color::White);
    if (shot == nullptr) {
        ++refusedCount_;
        return;
    }

    // indexOf() maps the pointer back to its slot, which is where the colour
    // comes from: what you see flying is the slot it lives in.
    const uint16_t index = pool_.indexOf(shot);
    if (index != ProjectilePool::kEnd) {
        shot->tint = slotColor(index);
    }
}

void ObjectPoolScene::stepProjectiles(int32_t stepMs) {
    if (stepMs <= 0) {
        return;
    }

    // Releasing the current slot from inside a nextLive() walk is safe, and
    // this is the loop that proves it. releaseAt(i) only clears bit i of the
    // liveness bitmask; the next step asks for nextLive(i + 1), which masks off
    // every bit at or below i before it looks. Clearing a bit can never create
    // a live index either, so no slot is skipped and none is visited twice.
    // (The one thing not to do inside this walk is acquire(): a freed low slot
    // is invisible to the rest of the pass, but a slot above i would be picked
    // up and stepped in the same frame it was created.)
    for (uint16_t i = pool_.nextLive(0); i != ProjectilePool::kEnd;
         i = pool_.nextLive(static_cast<uint16_t>(i + 1))) {
        Projectile& shot = *pool_.at(i);

        shot.x += (shot.vx * stepMs) / 1000;
        shot.y += (shot.vy * stepMs) / 1000;

        const int32_t px = shot.x >> kFixedShift;
        const int32_t py = shot.y >> kFixedShift;
        if (px < kInnerLeft || px > kInnerRight || py < kInnerTop || py > kInnerBottom) {
            pool_.releaseAt(i);
            continue;
        }

        const int32_t age = static_cast<int32_t>(shot.ageMs) + stepMs;
        shot.ageMs = (age > 0xFFFF) ? 0xFFFFu : static_cast<uint16_t>(age);
    }
}

void ObjectPoolScene::refreshHud() {
    hudLiveCount_ = pool_.size();
    hudRefusedCount_ = refusedCount_;

    snprintf(liveText_, sizeof(liveText_), "LIVE %u/%u",
             static_cast<unsigned>(pool_.size()),
             static_cast<unsigned>(pool_.capacity()));

    snprintf(refusedText_, sizeof(refusedText_), "REFUSED %lu",
             static_cast<unsigned long>(refusedCount_));

    if (pool_.isFull()) {
        snprintf(statusText_, sizeof(statusText_), "FULL: ACQUIRE=NULL");
        statusColor_ = Color::Red;
    } else {
        snprintf(statusText_, sizeof(statusText_), "%u SLOTS FREE",
                 static_cast<unsigned>(pool_.capacity() - pool_.size()));
        statusColor_ = Color::Green;
    }
}

void ObjectPoolScene::drawSlotMap(gfx::Renderer& renderer) const {
    // One cell per slot index, drawn straight off isLive(). This is the pool's
    // liveness bitmask made visible: projectiles leave the field out of order,
    // so a release opens a hole in the middle and the next acquire() refills
    // that hole. The lit cells are a set, not a prefix.
    for (uint16_t i = 0; i < kCapacity; ++i) {
        const int x = kTextX + static_cast<int>(i) * kSlotMapCell;
        if (pool_.isLive(i)) {
            renderer.drawFilledRectangle(x, kSlotMapY, kSlotMapCell - 1, kSlotMapHeight,
                                         slotColor(i));
        } else {
            renderer.drawRectangle(x, kSlotMapY, kSlotMapCell - 1, kSlotMapHeight, Color::Gray);
        }
    }
}

void ObjectPoolScene::drawEmitter(gfx::Renderer& renderer) const {
    const int ex = static_cast<int>(emitterX_ >> kFixedShift);
    const int ey = static_cast<int>(emitterY_ >> kFixedShift);
    const int edge = static_cast<int>(kEmitterHalf) * 2 + 1;

    renderer.drawFilledRectangle(ex - static_cast<int>(kEmitterHalf),
                                 ey - static_cast<int>(kEmitterHalf),
                                 edge, edge, Color::White);

    // Barrel: a short line the length of the muzzle offset, pointing wherever
    // the pad last pushed, so it is obvious where the next shot goes.
    const int barrel = static_cast<int>(kEmitterHalf) + 3;
    renderer.drawLine(ex, ey,
                      ex + static_cast<int>((aimX_ * barrel) >> kFixedShift),
                      ey + static_cast<int>((aimY_ * barrel) >> kFixedShift),
                      Color::Cyan);
}

} // namespace object_pool_demo
