/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#include "presentation/PongEffects.h"

#include <math/Scalar.h>

namespace pong::presentation {

namespace gfx = pixelroot32::graphics;
namespace math = pixelroot32::math;

namespace {

#if PIXELROOT32_ENABLE_PARTICLES

/// How far inside the court a goal burst starts, in design-box pixels.
///
/// A point is awarded when the ball is already past the edge, so a burst placed
/// exactly on the edge spends half its particles outside the panel. Four pixels
/// keeps the origin on the drawn edge band while giving the inward half of the
/// cone room to be seen.
constexpr int kGoalEdgeInset = 4;

/**
 * @brief Builds the one particle configuration the court uses.
 *
 * Both colours are high-luminance palette slots on purpose. A fade towards a
 * dark slot looks like a soft fade under SDL2, but `Color::Black` is RGB565
 * 0x0000 - the value the palette-indexed framebuffer reads as transparent - so
 * a burst that ended there would simply disappear on the device builds. Yellow
 * -> Orange keeps the spark visible against both the navy court and the white
 * actors, and the staggered lifetimes thin the burst out without dimming it.
 *
 * Friction is multiplicative per step, so it sets the reach: total travel is
 * roughly maxSpeed / (1 - friction), which at 3.2 and 0.90 is about 32 px - far
 * enough to read as a strike, near enough that the burst still belongs to the
 * rectangle it came from.
 *
 * Lifetimes are steps of the 25 Hz clock, not frames and not milliseconds:
 * 8-16 steps is 0.32-0.64 s of wall-clock time on both the native build and the
 * panel, which is exactly the difference the fixed clock is there to remove.
 */
gfx::particles::ParticleConfig impactConfig() {
    gfx::particles::ParticleConfig cfg{};

    cfg.startColor = gfx::Color::Yellow;
    cfg.endColor = gfx::Color::Orange;
    cfg.minSpeed = math::toScalar(1.2f);
    cfg.maxSpeed = math::toScalar(3.2f);
    cfg.gravity = math::toScalar(0.10f);   // positive is downward
    cfg.friction = math::toScalar(0.90f);
    cfg.minLife = 8;                       // steps of the clock above
    cfg.maxLife = 16;
    cfg.fadeColor = true;
    cfg.minAngleDeg = math::toScalar(0.0f);    // full circle: a contact has no
    cfg.maxAngleDeg = math::toScalar(360.0f);  // single direction to throw in

    return cfg;
}

/**
 * @brief Converts a design-box point to a burst origin inside the play area.
 * @param x Design-box X.
 * @param y Design-box Y.
 * @return Scalar position clamped to the playfield.
 *
 * The ball is clipped to the play area when it is drawn, and a goal happens
 * with the ball already past an edge. Clamping here means the burst is always
 * on screen and always over the court, never under the score band and never off
 * the canvas.
 */
math::Vector2 clampToPlay(int x, int y) {
    const CourtRect play = playAreaRect();
    const int maxX = play.x + play.width - 1;
    const int maxY = play.y + play.height - 1;

    if (x < play.x) {
        x = play.x;
    } else if (x > maxX) {
        x = maxX;
    }
    if (y < play.y) {
        y = play.y;
    } else if (y > maxY) {
        y = maxY;
    }

    return math::Vector2(math::toScalar(x), math::toScalar(y));
}

#endif  // PIXELROOT32_ENABLE_PARTICLES

}  // namespace

PongEffects::PongEffects()
#if PIXELROOT32_ENABLE_PARTICLES
    // The emitter's own position is unused: every event passes its origin to
    // burst(), which is what lets one pool serve the whole court.
    : particles_(math::Vector2(math::toScalar(0), math::toScalar(0)), impactConfig())
#endif
{
}

void PongEffects::reset() {
#if PIXELROOT32_ENABLE_CAMERA_EFFECTS
    shake_.cancelAll();
#endif
    lastNowMs_ = 0;
    clockStarted_ = false;
#if PIXELROOT32_ENABLE_PARTICLES
    particleClockMs_ = 0;
#endif
}

void PongEffects::update(std::uint32_t nowMs) {
    if (!clockStarted_) {
        // The first frame establishes the baseline. Without this the first
        // delta would be the whole uptime and would spend the catch-up budget.
        clockStarted_ = true;
        lastNowMs_ = nowMs;
    }
    // Unsigned subtraction is wrap-safe, so a `millis()` rollover cannot make
    // the particle clock jump backwards.
    const std::uint32_t deltaMs = nowMs - lastNowMs_;
    lastNowMs_ = nowMs;

#if PIXELROOT32_ENABLE_CAMERA_EFFECTS
    // The shake reads real milliseconds, so it needs no clock of its own.
    shake_.update(deltaMs);
#endif

#if PIXELROOT32_ENABLE_PARTICLES
    particleClockMs_ += deltaMs;

    // A stalled frame must not spend a whole burst at once, and the unspent
    // remainder must not grow without bound either.
    const std::uint32_t ceiling = kParticleStepMs * kMaxCatchUpSteps;
    if (particleClockMs_ > ceiling) {
        particleClockMs_ = ceiling;
    }

    while (particleClockMs_ >= kParticleStepMs) {
        particleClockMs_ -= kParticleStepMs;
        particles_.update(kParticleStepMs);
    }
#else
    (void)deltaMs;
#endif
}

void PongEffects::draw(gfx::Renderer& renderer) {
#if PIXELROOT32_ENABLE_PARTICLES
    const ViewOffset origin =
        viewOffset(renderer.getLogicalWidth(), renderer.getLogicalHeight());
    if (origin.x == 0 && origin.y == 0) {
        particles_.draw(renderer);
        return;
    }

    // Add the design-box offset on top of whatever camera offset the scene has
    // already applied, then put the renderer back exactly as it was: the score
    // band and the overlay are drawn after this call and must not inherit it.
    const int previousX = renderer.getXOffset();
    const int previousY = renderer.getYOffset();
    renderer.setDisplayOffset(previousX + origin.x, previousY + origin.y);
    particles_.draw(renderer);
    renderer.setDisplayOffset(previousX, previousY);
#else
    (void)renderer;
#endif
}

void PongEffects::onPaddleHit(std::uint8_t player, std::int16_t contactY,
                              std::int16_t ballX) {
#if PIXELROOT32_ENABLE_CAMERA_EFFECTS
    shake_.triggerShake(math::toScalar(2.0f), 100);
#endif

#if PIXELROOT32_ENABLE_PARTICLES
    const CourtRect play = playAreaRect();
    int x = ballX;
    if (x < play.x || x >= play.x + play.width) {
        // A contact should always arrive near a paddle, but if a caller ever
        // passes a stale or off-court X, land on the paddle face the rules draw
        // instead of throwing the burst into the score band.
        x = (player == 0) ? kPaddleLeftX + kPaddleWidth : kPaddleRightX;
    }
    particles_.burst(clampToPlay(x, contactY), kPaddleBurst);
#else
    (void)player;
    (void)contactY;
    (void)ballX;
#endif
}

void PongEffects::onWallBounce(std::int16_t x, std::int16_t y) {
#if PIXELROOT32_ENABLE_CAMERA_EFFECTS
    // The wall is a lighter contact than the paddle: a one-pixel nudge reads as
    // "the ball changed direction" without competing with the rally.
    shake_.triggerShake(math::toScalar(1.5f), 70);
#endif

#if PIXELROOT32_ENABLE_PARTICLES
    particles_.burst(clampToPlay(x, y), kWallBurst);
#else
    (void)x;
    (void)y;
#endif
}

void PongEffects::onGoal(std::uint8_t scorer, std::int16_t edgeX,
                         std::int16_t ballY) {
#if PIXELROOT32_ENABLE_CAMERA_EFFECTS
    shake_.triggerShake(math::toScalar(3.5f), 200);
#endif

#if PIXELROOT32_ENABLE_PARTICLES
    // A player point leaves through the right edge and a CPU point through the
    // left, so the burst is nudged a few pixels back into the court either way.
    // A burst exactly on the edge throws half its particles off-panel before
    // the first step can draw them, which reads as a smaller effect than the 24
    // particles it spent.
    const int insetX = (scorer == 0) ? edgeX - kGoalEdgeInset : edgeX + kGoalEdgeInset;
    particles_.burst(clampToPlay(insetX, ballY), kGoalBurst);
#else
    (void)scorer;
    (void)edgeX;
    (void)ballY;
#endif
}

void PongEffects::onMatchPoint() {
#if PIXELROOT32_ENABLE_CAMERA_EFFECTS
    // The closing point is the one moment the whole cabinet is allowed to move:
    // five pixels is well under half a paddle and still unmistakable.
    shake_.triggerShake(math::toScalar(5.0f), 320);
#endif
}

math::Vector2 PongEffects::shake() const {
#if PIXELROOT32_ENABLE_CAMERA_EFFECTS
    return shake_.getOffset();
#else
    return math::Vector2::ZERO();
#endif
}

bool PongEffects::shaking() const {
#if PIXELROOT32_ENABLE_CAMERA_EFFECTS
    return shake_.hasActiveEffects();
#else
    return false;
#endif
}

}  // namespace pong::presentation
