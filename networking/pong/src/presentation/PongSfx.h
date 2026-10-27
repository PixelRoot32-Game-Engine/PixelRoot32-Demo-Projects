/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <cstdint>

namespace pixelroot32::core {
class Engine;
}

/**
 * @file PongSfx.h
 * @brief Arcade sound effects for the networking Pong demo.
 *
 * This header is deliberately free of audio includes: the scene should say
 * *what happened* and nothing more, and choosing a waveform is this module's
 * job. Keeping the contract down to an enum and one call also means the scene
 * stays linkable without the APU library, which is what lets the host suites
 * keep compiling the engine-free layers around it.
 *
 * S13 fixes the shape: the event tables live in PongSfx.cpp, every `AudioEvent`
 * field is assigned by name (never positionally, because `preset` sits at field
 * 7 rather than last), and the whole implementation disappears behind an
 * `#else` no-op when `PIXELROOT32_ENABLE_AUDIO` is 0, so call sites stay
 * unguarded.
 */
namespace netpong {

/**
 * @enum SfxId
 * @brief The sounds a Pong match makes.
 */
enum class SfxId : std::uint8_t {
    PaddleHit,   ///< The ball bounced off a paddle.
    WallBounce,  ///< The ball bounced off the top or bottom wall.
    Goal,        ///< A point was awarded: a falling sweep into the point.
    Countdown,   ///< The match entered its pre-serve countdown.
    Pause,       ///< The match was suspended or resumed.
    Win          ///< Someone reached the winning score.
};

/**
 * @brief Plays one sound effect on the Engine's audio system.
 * @param engine Engine that owns the audio scheduler; it is not stored.
 * @param id Which sound to play.
 *
 * A no-op when the demo is built with `PIXELROOT32_ENABLE_AUDIO=0`, so call
 * sites never need to guard themselves. Effects share the four-slot SFX voice
 * subpool (voices 4-7) and can only steal from each other, so a burst never
 * interrupts music.
 */
void playSfx(pixelroot32::core::Engine& engine, SfxId id);

}  // namespace netpong
