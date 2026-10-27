/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <cstdint>

#include <graphics/CameraEffects.h>
#include <graphics/Renderer.h>
#include <graphics/particles/ParticleEmitter.h>
#include <math/Vector2.h>
#include <platforms/EngineConfig.h>

#include "presentation/CourtRenderer.h"

/**
 * @file PongEffects.h
 * @brief Court feedback: one particle emitter and one screen shake.
 *
 * The scene reports *what happened* - a paddle hit at this height, a goal at
 * this edge - and this module decides how big the burst is and how hard the
 * court jolts. Keeping the decision here is also what keeps the emitter member,
 * which only exists when the particles gate is on, out of `PongHostScene.h`.
 *
 * The two engine gates are not symmetric. `CameraEffects.h` ships a
 * same-signature stub when `PIXELROOT32_ENABLE_CAMERA_EFFECTS` is 0, but the
 * particle header has no stub: the whole of `ParticleEmitter.h` sits inside its
 * `#if`. Both are guarded here anyway, with the same no-op shims the chess demo
 * uses, so the C3 builds (audio, particles and camera effects compiled out) and
 * the host suites can ignore this file.
 *
 * S12 sets the visual budget: custom `ParticleConfig` only, one reused emitter,
 * a fixed 25 Hz step and a hard ceiling of 50 particles. `Explosion` and `Smoke`
 * are deliberately unused because both end on `Color::Black`, which is RGB565
 * 0x0000 and therefore transparent on the ESP32.
 */
namespace netpong::presentation {

/**
 * @class PongEffects
 * @brief Bursts and shake for the arcs of a Pong rally.
 *
 * The emitter is reused by every event: `burst()` takes the origin explicitly,
 * so one 50-slot pool serves the paddles, both walls, the goal edges and the
 * match point without a second permanent 50-slot update/draw loop.
 *
 * The emitter ages a particle by exactly one step per `update()` call and
 * ignores the delta it is handed (see `ParticleEmitter::update()`), so this
 * class stores the wall clock and steps it at a fixed 25 Hz instead of once per
 * frame. Without that, the same burst lasts a fraction of a second on the
 * uncapped native loop and more than a second on the ~25 fps CYD.
 */
class PongEffects {
public:
    /// Particle step period: 25 Hz, the CYD's own frame rate.
    static constexpr std::uint32_t kParticleStepMs = 40;

    /// Steps one frame may consume; the remainder is dropped, never banked.
    static constexpr std::uint32_t kMaxCatchUpSteps = 2;

    /// Particles thrown off a paddle contact.
    static constexpr int kPaddleBurst = 8;

    /// Particles thrown off a wall contact.
    static constexpr int kWallBurst = 4;

    /// Particles thrown off the goal edge.
    static constexpr int kGoalBurst = 24;

    /// Builds the effects with no live particles and no active shake.
    PongEffects();

    /// @brief Cancels the shake and restarts the particle clock.
    ///
    /// Called from `PongHostScene::init()`; live particles are left to expire on
    /// their own, because the emitter has no cancel API and a burst is gone in
    /// under a second of the fixed clock.
    void reset();

    /**
     * @brief Advances the shake and steps the particles on their own clock.
     * @param nowMs Current `millis()` value, not a delta.
     *
     * Absolute time is what the scene already has for the match adapter, so the
     * effect module derives its own delta and stays independent of how often the
     * Engine happens to call `update()`. A long frame is capped at
     * `kMaxCatchUpSteps` steps so it cannot spend a whole burst at once.
     */
    void update(std::uint32_t nowMs);

    /**
     * @brief Draws live particles above the actors.
     * @param renderer Renderer to draw with.
     *
     * Particles are authored in design-box pixels, while the court helpers add
     * the viewport offset themselves. This call adds the same offset for the
     * duration of the draw and restores it, so a future canvas that is not
     * 320x240 shifts the debris with the court instead of away from it.
     */
    void draw(pixelroot32::graphics::Renderer& renderer);

    /**
     * @brief Fires the feedback for a ball/paddle contact.
     * @param player 0 for P1 (left), 1 for P2 (right).
     * @param contactY Design-box Y of the contact point.
     * @param ballX Design-box X of the ball centre at the contact.
     */
    void onPaddleHit(std::uint8_t player, std::int16_t contactY, std::int16_t ballX);

    /**
     * @brief Fires the feedback for a ball/wall contact.
     * @param x Design-box X of the ball centre.
     * @param y Design-box Y of the ball centre at the wall.
     */
    void onWallBounce(std::int16_t x, std::int16_t y);

    /**
     * @brief Fires the feedback for a point.
     * @param scorer 0 for P1, 1 for P2.
     * @param edgeX Design-box X of the edge the ball crossed.
     * @param ballY Design-box Y of the ball when it crossed.
     */
    void onGoal(std::uint8_t scorer, std::int16_t edgeX, std::int16_t ballY);

    /// @brief Fires the heavier shake that closes a match.
    void onMatchPoint();

    /**
     * @brief Returns one frame's shake offset.
     * @return Offset to apply to the court pass; zero when no shake is active.
     *
     * `CameraEffectsSystem::getOffset()` draws fresh random numbers on every
     * call, so the scene must sample this exactly once per frame and reuse that
     * value for both the start and the end of the shaken pass. Sampling twice
     * would tear the court against its own actors.
     */
    [[nodiscard]] pixelroot32::math::Vector2 shake() const;

    /**
     * @brief Reports whether a shake is currently running.
     * @return true while at least one camera-effect slot is active.
     *
     * The scene uses this to skip the second (shaken) court pass on the frames
     * that have nothing to jolt, which is almost all of them.
     */
    [[nodiscard]] bool shaking() const;

private:
#if PIXELROOT32_ENABLE_PARTICLES
    /// Emitter pool shared by every event; only compiled when the gate is on.
    pixelroot32::graphics::particles::ParticleEmitter particles_;
#endif

#if PIXELROOT32_ENABLE_CAMERA_EFFECTS
    /// Shake slots; the stub header makes this a same-signature no-op system.
    pixelroot32::graphics::CameraEffectsSystem shake_;
#endif

    /// Absolute time of the previous `update()`, for the derived delta.
    std::uint32_t lastNowMs_ = 0;

    /// Whether `lastNowMs_` holds a real sample rather than its initial zero.
    bool clockStarted_ = false;

#if PIXELROOT32_ENABLE_PARTICLES
    /// Milliseconds owed to the emitter, which steps once per call.
    std::uint32_t particleClockMs_ = 0;
#endif
};

}  // namespace netpong::presentation
