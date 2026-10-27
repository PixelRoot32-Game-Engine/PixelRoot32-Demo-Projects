/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */

/*
 * Local Pong - the scene that plays the arcade court against the CPU.
 *
 * The scene is the composition root of the demo: it owns the match rules, turns
 * their events into sound and particles, and paints the court. There is no
 * transport, no peer and no second controller here -- one player, one CPU.
 */
#include "PongScene.h"

#include <core/Engine.h>
#include <graphics/Renderer.h>
#include <math/Vector2.h>
#include <platforms/EngineConfig.h>

#include "presentation/CourtLayout.h"
#include "presentation/CourtRenderer.h"
#include "presentation/PongSfx.h"

// Declared once by the platform header of whichever target is being built; the
// scene only borrows it, exactly as it borrows nothing else from the platform.
extern pixelroot32::core::Engine engine;

namespace pong {

namespace {

using pixelroot32::graphics::Renderer;

// --- Input contract ----------------------------------------------------------
// Indices into the InputConfig every platform header builds, in the engine's
// fixed order: 0 up, 1 down, 2 left, 3 right, 4 A (Space on native), 5 B. Both
// device headers map 0/1 to the D-pad and 4 to the A button; platforms/native.h
// maps them to the arrow keys and Space, matching the demo this replaced. The
// scene reads the same indices everywhere, which is why the order there and the
// order here are one contract.

/// Held direction towards the top of the court.
constexpr std::uint8_t kUpButton = 0;

/// Held direction towards the bottom of the court.
constexpr std::uint8_t kDownButton = 1;

/// Restart after MatchOver.
constexpr std::uint8_t kRestartButton = 4;

// --- Fixed step --------------------------------------------------------------

/// Fixed steps one frame may replay before the backlog is dropped.
///
/// Three steps is 50 ms of motion: enough to absorb a dropped frame or two on
/// the panel build, small enough that a long stall cannot replay a visible
/// stretch of the rally at once.
constexpr int kMaxCatchUpSteps = 3;

/// Longest frame the accumulator accepts, in milliseconds.
///
/// A first frame after a debugger pause or a slow panel refresh would otherwise
/// hand the accumulator seconds of motion. The clamp trims the frame instead,
/// and the rules clamp their own step for the same reason.
constexpr std::uint32_t kMaxFrameMs = 100;

}  // namespace

PongScene::PongScene() : shakeCamera(game::kCourtWidth, game::kCourtHeight) {}

void PongScene::init() {
    // Clears the entity array and rewinds the scene arena, so a second init()
    // cannot stack state on top of the first.
    Scene::init();

    // A rebuilt match must not inherit a burst or a jolt from the one that was
    // on screen before.
    effects.reset();

    // The court, the HUD and the overlay all name slots of the generated table,
    // so the palette is bound before the first frame is drawn. The draw entry
    // points re-bind it too, which keeps a scene re-init from falling back to an
    // engine preset.
    presentation::installPalette();

    rules.resetMatch();

    lastUpdateMs = static_cast<std::uint32_t>(millis());
    accumulatorUs = 0;
    lastPhase = rules.phase();

    // The opening serve has no phase edge to announce it -- init() itself is the
    // edge, and lastPhase is baselined from the phase this call just created --
    // so the countdown blip is played here, once, and the serve is audible from
    // the first second. Every later serve is announced by its Countdown edge.
    playSfx(engine, SfxId::Countdown);
}

void PongScene::update(unsigned long deltaTime) {
    // Entities first, as everywhere else in the repository; this scene has none,
    // so the call is the base contract rather than work.
    Scene::update(deltaTime);

    // The player drives slot 0 only. Slot 1 belongs to the CPU, which lives
    // inside the rules and reads the ball, so nothing here can move it.
    auto& input = engine.getInputManager();
    std::uint8_t buttons = 0;
    if (input.isButtonDown(kUpButton)) {
        buttons |= game::kButtonLeft;
    }
    if (input.isButtonDown(kDownButton)) {
        buttons |= game::kButtonRight;
    }
    rules.setPlayerInput(0, buttons);

    if (lastPhase == game::Phase::MatchOver && input.isButtonPressed(kRestartButton)) {
        rules.restart();
        // The winning point is still shaking and its burst is still alive. A new
        // match starts on a still court, so the shake is cancelled and the
        // particle clock restarts from this frame; the particles themselves are
        // left to expire, which takes well under a second of the fixed clock.
        effects.reset();
        // Re-baseline the phase machine so the restart is not mistaken for a
        // rally edge, then announce the serve exactly as init() does: the
        // countdown blip is the one sound a fresh match makes. Resetting the
        // baseline is what makes this call explicit instead of an edge that the
        // same reset would have swallowed.
        lastPhase = rules.phase();
        playSfx(engine, SfxId::Countdown);
    }

    // The rules take microseconds, which a millisecond clock cannot express, so
    // the frame delta is accumulated in microseconds and spent in exact
    // kStepDtUs slices. A slow frame therefore changes how many steps run, never
    // how far the ball moved inside one.
    const std::uint32_t nowMs = static_cast<std::uint32_t>(millis());
    std::uint32_t frameMs = nowMs - lastUpdateMs;
    lastUpdateMs = nowMs;
    if (frameMs > kMaxFrameMs) {
        frameMs = kMaxFrameMs;
    }
    accumulatorUs += frameMs * 1000U;

    int steps = 0;
    while (accumulatorUs >= game::kStepDtUs && steps < kMaxCatchUpSteps) {
        accumulatorUs -= game::kStepDtUs;
        rules.step(game::kStepDtUs);
        ++steps;
    }
    if (accumulatorUs >= game::kStepDtUs) {
        // Still a step behind after the cap: the frame is already late, and
        // replaying the rest would land the ball a visible jump away from where
        // the player last saw it. Dropping the backlog keeps the court honest
        // about being behind instead of pretending it never was.
        accumulatorUs = 0;
    }

    // Effects advance before this frame's cues are triggered: a burst queued
    // below must be drawn at full life instead of being aged by the frame that
    // created it.
    effects.update(nowMs);

    // Contacts and the match end arrive as edges. The flags let a cue that could
    // ever arrive both as an event and as a phase edge play once, never twice.
    bool goalSeen = false;
    bool matchSeen = false;

    game::Event event;
    while (rules.pollEvent(event)) {
        switch (event.type) {
            case game::EventType::PaddleHit:
                playPaddleHit(event.player);
                break;
            case game::EventType::WallBounce:
                playWallBounce();
                break;
            case game::EventType::Goal:
                playGoal(event.player);
                goalSeen = true;
                break;
            case game::EventType::MatchOver:
                playMatchPoint();
                matchSeen = true;
                break;
            case game::EventType::ServeStarted:
                // The countdown blip already announced this serve; a second
                // sound on release would double up on every point.
                break;
        }
    }

    const game::Phase phase = rules.phase();
    if (phase == lastPhase) {
        return;
    }

    // A point is the edge out of Play into Point, or straight into MatchOver
    // when it is the winning one. The rules do not re-centre the ball during
    // this frame, so the scorer is still readable from where it left the court.
    if (lastPhase == game::Phase::Play &&
        (phase == game::Phase::Point || phase == game::Phase::MatchOver) && !goalSeen) {
        playGoal(goalScorer());
    }
    if (phase == game::Phase::MatchOver && !matchSeen) {
        playMatchPoint();
    }
    if (phase == game::Phase::Countdown) {
        playSfx(engine, SfxId::Countdown);
    }

    lastPhase = phase;
}

void PongScene::draw(Renderer& renderer) {
    // Court first: it fills the score band as its own strip, so it can be
    // painted before everything else and still never erase a score. This base
    // pass is never shaken, so when the court is drawn again under the offset
    // the pixels the shifted copy vacates are already painted.
    presentation::drawBackground(renderer);

    // One sample per frame: shake() draws fresh random numbers on every call, so
    // the start and the end of the pass must reuse this value or the court would
    // tear against its own actors.
    const pixelroot32::math::Vector2 shake = effects.shake();
    const bool shaking = effects.shaking();
    shakeCamera.apply(renderer, shake);

    if (shaking) {
        // The frame and the centre line ride the jolt with the actors; the HUD
        // band is repainted below and never enters this pass.
        presentation::drawBackground(renderer);
    }

    // Entities belong on top of the court, and only the base class draws them.
    Scene::draw(renderer);

    // Actors, then the particles above them, both inside the shaken pass.
    presentation::drawActors(renderer, rules);
    effects.draw(renderer);

    shakeCamera.apply(renderer);
    if (shaking) {
        repaintHudBand(renderer);
    }

    // The HUD and the overlay are the only things allowed to sit over a live
    // court, and neither one moves with the shake.
    presentation::drawHud(renderer, rules);
    presentation::drawOverlay(renderer, rules);
}

void PongScene::playPaddleHit(std::uint8_t player) {
    presentation::CourtRect ball = presentation::ballRect(rules);
    if (ball.empty()) {
        // A queued contact should still be inside the play area, but if the
        // rule state has already moved on, land on the paddle the court drew
        // rather than throwing the burst at the origin.
        ball = presentation::paddleRect(rules, player);
    }

    const auto contactY = static_cast<std::int16_t>(ball.y + ball.height / 2);
    const auto ballX = static_cast<std::int16_t>(ball.x + ball.width / 2);
    effects.onPaddleHit(player, contactY, ballX);
    playSfx(engine, SfxId::PaddleHit);
}

void PongScene::playWallBounce() {
    const presentation::CourtRect ball = presentation::ballRect(rules);
    const int x = ball.empty() ? presentation::kCourtCentreX : ball.x + ball.width / 2;
    const int y = ball.empty() ? presentation::kPlayY : ball.y + ball.height / 2;
    effects.onWallBounce(static_cast<std::int16_t>(x), static_cast<std::int16_t>(y));
    playSfx(engine, SfxId::WallBounce);
}

void PongScene::playGoal(std::uint8_t scorer) {
    const presentation::CourtRect play = presentation::playAreaRect();
    const presentation::CourtRect ball = presentation::ballRect(rules);
    const int ballY = ball.empty() ? play.y + play.height / 2 : ball.y + ball.height / 2;

    // Scorer 0 put the ball out on the right edge, scorer 1 on the left.
    const int edgeX = (scorer == 0) ? play.x + play.width - 1 : play.x;

    effects.onGoal(scorer, static_cast<std::int16_t>(edgeX),
                   static_cast<std::int16_t>(ballY));
    playSfx(engine, SfxId::Goal);
}

void PongScene::playMatchPoint() {
    effects.onMatchPoint();
    playSfx(engine, SfxId::Win);
}

std::uint8_t PongScene::goalScorer() const {
    if (rules.ballX() < 0) {
        return 1;
    }
    if (rules.ballX() > game::kCourtWidth * game::kOne) {
        return 0;
    }
    // Fallback for a caller that asks after the ball was placed again: the
    // score is the only remaining evidence of who was awarded the point.
    return rules.score(1) > rules.score(0) ? 1 : 0;
}

void PongScene::repaintHudBand(Renderer& renderer) {
    const presentation::ViewOffset origin =
        presentation::viewOffset(renderer.getLogicalWidth(), renderer.getLogicalHeight());

    // The shifted playfield can reach a few pixels under the band, so the band
    // is restored with the same paper slot the court paints it with. The HUD
    // text is drawn afterwards, by drawHud(), never inside the shaken pass.
    renderer.drawFilledRectangle(origin.x, origin.y, presentation::kDesignWidth,
                                 presentation::kHudHeight, presentation::kPaper);
}

}  // namespace pong
