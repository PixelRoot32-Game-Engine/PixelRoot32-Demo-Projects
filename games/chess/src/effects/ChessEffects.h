/*
 * ChessEffects.h - Screen shake and debris for a capture.
 *
 * Same split as the sound module: the scene reports *what happened* - a piece
 * of this kind was taken on this square - and this decides how hard the board
 * jolts and how much debris flies. Keeping the decision here is also what keeps
 * the ParticleEmitter member, which only exists when the particles flag is on,
 * out of ChessScene.h.
 *
 * Note the asymmetry between the two engine gates used here. CameraEffects.h
 * ships a same-signature stub when PIXELROOT32_ENABLE_CAMERA_EFFECTS is 0, so
 * shake calls compile either way. ParticleEmitter.h has no stub - the whole
 * header is inside its #if - so every particle member and call needs a guard.
 */
#pragma once

#include <cstdint>

#include <graphics/Camera2D.h>
#include <graphics/CameraEffects.h>
#include <graphics/Renderer.h>
#include <graphics/particles/ParticleEmitter.h>
#include <platforms/EngineConfig.h>

#include "chess/ChessRules.h"

namespace chessdemo {

/**
 * @class ChessEffects
 * @brief Capture feedback: a decaying screen shake plus a burst of debris.
 *
 * The shake is applied as a renderer offset, so it moves whatever is drawn
 * between beginBoardPass() and endBoardPass() and nothing else. The HUD and the
 * promotion picker are drawn outside that pair on purpose: a jolt that moved
 * the buttons would make them harder to hit on a 240x320 touch panel, which is
 * the opposite of feedback.
 */
class ChessEffects {
public:
    /** @brief Build the effects with the board's viewport size. */
    ChessEffects();

    /**
     * @brief Fire the feedback for a piece being taken.
     *
     * Amplitude, duration and debris count all scale with the piece: a pawn
     * is a tap, a queen is an event.
     *
     * @param centreX Screen X of the centre of the square the victim stood on.
     * @param centreY Screen Y of the centre of that square.
     * @param captured Kind of piece that was taken.
     */
    void triggerCapture(int centreX, int centreY, chess::PieceType captured);

    /**
     * @brief Cancel the shake, for a new game or a resignation.
     *
     * Live debris is left to expire on its own - the emitter has no cancel API
     * and a particle lives at most 28 steps of the fixed particle clock, so it
     * is gone within about a second.
     */
    void clear();

    /**
     * @brief Advance the shake timers and step the debris on its own clock.
     * @param deltaTimeMs Milliseconds since the previous frame.
     */
    void update(unsigned long deltaTimeMs);

    /**
     * @brief Start the shaken pass: everything drawn after this moves with it.
     *
     * While a shake is running the board is offset by a few pixels, so one or
     * two edges of the screen expose whatever beginFrame() left there. With
     * dirty regions off - as this demo builds - that is a full clear, so the
     * gap reads as a clean black border rather than as stale pixels.
     *
     * @param renderer Renderer to offset.
     */
    void beginBoardPass(pixelroot32::graphics::Renderer& renderer) const;

    /**
     * @brief Draw live debris. Call inside the board pass, above the pieces.
     * @param renderer Renderer to draw through.
     */
    void drawParticles(pixelroot32::graphics::Renderer& renderer);

    /**
     * @brief End the shaken pass and put the renderer back at the origin.
     * @param renderer Renderer to reset.
     */
    void endBoardPass(pixelroot32::graphics::Renderer& renderer) const;

private:
    /**
     * Parked at the origin and never moved: the board does not scroll. It is
     * here because it owns the sign convention - camera coordinates are negated
     * to become the renderer offset - so the shake never has to restate it.
     */
    pixelroot32::graphics::Camera2D camera_;

    pixelroot32::graphics::CameraEffectsSystem shake_;

#if PIXELROOT32_ENABLE_PARTICLES
    pixelroot32::graphics::particles::ParticleEmitter debris_;

    /**
     * Milliseconds owed to the emitter, which ages particles one step per call
     * rather than by elapsed time. Stepping it on a fixed clock instead of once
     * per frame is what keeps a burst the same length in seconds on a native
     * build running at several hundred frames a second and on the CYD at
     * roughly twenty-five.
     */
    unsigned long particleClockMs_ = 0;
#endif
};

}  // namespace chessdemo
