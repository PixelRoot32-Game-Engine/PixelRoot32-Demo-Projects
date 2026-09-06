/*
 * ChessEffects.cpp - Capture feedback: screen shake and debris.
 */
#include "effects/ChessEffects.h"

#include <math/Scalar.h>
#include <math/Vector2.h>

#include "ChessConstants.h"

namespace chessdemo {

namespace gfx  = pixelroot32::graphics;
namespace math = pixelroot32::math;

namespace {

/**
 * @struct CaptureImpact
 * @brief How loud, visually, one kind of capture is.
 */
struct CaptureImpact {
    float    shakePixels;   ///< Peak offset; the shake decays linearly to zero.
    uint16_t shakeMs;       ///< How long that decay takes.
    int      debrisCount;   ///< Particles thrown, out of the emitter's 50.
};

/**
 * Debris counts are deliberately close to MAX_PARTICLES_PER_EMITTER, which is
 * 50 and is a hard engine boundary. Filling the pool is nearly free: the
 * emitter's update and draw loops run over all 50 slots every frame regardless
 * of how many are alive, so a bigger burst adds no iteration at all - only the
 * colour interpolation and the 2x2 blit for each particle that is actually
 * live, and only while it is live. Going past 50 would mean a second emitter,
 * which costs about 1.3 KB of RAM and a second permanent 50-slot loop per
 * frame even when nothing is on screen. That is the trade to weigh, not the
 * count itself.
 *
 * A capture that lands while the previous burst is still in the air gets only
 * the free slots. Turn-based play makes that essentially unreachable: a burst
 * is gone in about a second and a move takes longer than that.
 *
 * The table is indexed by chess::PieceType, which runs None, Pawn, Knight,
 * Bishop, Rook, Queen, King. Trading a pawn happens constantly and must not
 * become annoying; losing a queen should be felt. The King row is unreachable -
 * the rules never let a king be captured - but the table is indexed directly,
 * so it is filled in rather than left as a hole.
 */
constexpr CaptureImpact kImpact[] = {
    { 2.0f, 110, 16 },  // None: a capture always names a piece, so this is only
                        //       a safe landing spot for an unexpected index.
    { 2.0f, 110, 16 },  // Pawn
    { 3.0f, 160, 26 },  // Knight
    { 3.0f, 160, 26 },  // Bishop
    { 4.0f, 200, 36 },  // Rook
    { 5.0f, 260, 46 },  // Queen
    { 5.0f, 260, 46 }   // King
};

constexpr uint8_t kImpactCount = sizeof(kImpact) / sizeof(kImpact[0]);

#if PIXELROOT32_ENABLE_PARTICLES

/**
 * Milliseconds per particle step, i.e. 25 Hz.
 *
 * ParticleEmitter::update() ignores the delta it is handed and ages every
 * particle by exactly one step per call, so a particle's life is counted in
 * calls rather than in seconds. Calling it once per frame therefore ties the
 * burst to the frame rate, and the two targets here are nowhere near each
 * other: the native loop has no cap beyond SDL_Delay(1) and runs several
 * hundred frames a second, which burns a 28-step life in well under a tenth of
 * a second, while the CYD's ~25 fps stretches the same burst past a second.
 * Stepping on a fixed clock is what makes the burst last the same wall-clock
 * time on both, and on native it is also strictly less work than before.
 *
 * 25 Hz rather than 30 or 60 because it sits at the CYD's own frame rate, so
 * hardware almost never needs to catch up and native simply down-samples.
 */
constexpr unsigned long kParticleStepMs = 40;

/**
 * Steps one frame may run. A long frame must not spend a whole burst at once,
 * and the unspent remainder must not grow without bound.
 */
constexpr unsigned long kMaxCatchUpSteps = 2;

/**
 * @brief Build the debris configuration.
 *
 * Friction is multiplicative per step, so it sets the reach: total travel is
 * roughly maxSpeed / (1 - friction), which at 4.2 and 0.90 is about 40 px, or
 * a square and a bit. Far enough to read as an impact, near enough that the
 * burst still belongs to the square it came from.
 *
 * Both colours are high-luminance on purpose. Fading toward a dark colour looks
 * right on a light square and disappears on the dark ones, and half the board
 * is dark green - so the debris fades bright-to-bright and simply stops when
 * each particle's life runs out. The staggered lifetimes thin the burst out on
 * their own, so nothing is lost by not dimming it.
 */
gfx::particles::ParticleConfig captureDebrisConfig() {
    gfx::particles::ParticleConfig cfg{};

    cfg.startColor  = kCaptureSparkStart;
    cfg.endColor    = kCaptureSparkEnd;
    cfg.minSpeed    = math::toScalar(1.6f);
    cfg.maxSpeed    = math::toScalar(4.2f);
    cfg.gravity     = math::toScalar(0.12f);   // positive is downward
    cfg.friction    = math::toScalar(0.90f);
    cfg.minLife     = 14;                      // steps of the clock above,
    cfg.maxLife     = 28;                      // not frames and not ms
    cfg.fadeColor   = true;
    cfg.minAngleDeg = math::toScalar(0.0f);    // full circle: a capture has no
    cfg.maxAngleDeg = math::toScalar(360.0f);  // direction to throw debris in

    return cfg;
}

#endif  // PIXELROOT32_ENABLE_PARTICLES

}  // namespace

ChessEffects::ChessEffects()
    : camera_(kBoardPixels, kBoardPixels)
#if PIXELROOT32_ENABLE_PARTICLES
    , debris_(math::Vector2(math::toScalar(kBoardPixels / 2),
                            math::toScalar(kBoardPixels / 2)),
              captureDebrisConfig())
#endif
{
}

void ChessEffects::triggerCapture(int centreX, int centreY, chess::PieceType captured) {
    const uint8_t index = static_cast<uint8_t>(captured);
    const CaptureImpact& impact = kImpact[index < kImpactCount ? index : 0];

    shake_.triggerShake(math::toScalar(impact.shakePixels), impact.shakeMs);

#if PIXELROOT32_ENABLE_PARTICLES
    // burst() takes the origin explicitly; the emitter's own position is not
    // used for it, which is why this one emitter serves all 64 squares.
    debris_.burst(math::Vector2(math::toScalar(centreX), math::toScalar(centreY)),
                  impact.debrisCount);
#else
    (void)centreX;
    (void)centreY;
#endif
}

void ChessEffects::clear() {
    shake_.cancelAll();

#if PIXELROOT32_ENABLE_PARTICLES
    particleClockMs_ = 0;
#endif
}

void ChessEffects::update(unsigned long deltaTimeMs) {
    // The shake reads real milliseconds, so it needs no clock of its own.
    shake_.update(deltaTimeMs);

#if PIXELROOT32_ENABLE_PARTICLES
    particleClockMs_ += deltaTimeMs;

    const unsigned long ceiling = kParticleStepMs * kMaxCatchUpSteps;
    if (particleClockMs_ > ceiling) particleClockMs_ = ceiling;

    while (particleClockMs_ >= kParticleStepMs) {
        particleClockMs_ -= kParticleStepMs;
        debris_.update(kParticleStepMs);
    }
#endif
}

void ChessEffects::beginBoardPass(gfx::Renderer& renderer) const {
    // getOffset() draws fresh random numbers on every call, so the whole pass
    // has to be built from this one sample. Two calls in a frame would tear the
    // board against its own pieces.
    camera_.apply(renderer, shake_.getOffset());
}

void ChessEffects::drawParticles(gfx::Renderer& renderer) {
#if PIXELROOT32_ENABLE_PARTICLES
    debris_.draw(renderer);
#else
    (void)renderer;
#endif
}

void ChessEffects::endBoardPass(gfx::Renderer& renderer) const {
    // The camera never leaves the origin, so applying it without an effect is
    // the reset.
    camera_.apply(renderer);
}

}  // namespace chessdemo
