/*
 * ChessSfx.h - Sound effects for the chess demo.
 *
 * This header is deliberately free of audio includes. The scene should say
 * *what happened* and nothing more; picking the waveform is this module's job.
 * Keeping the contract down to an enum also means the rules and the scene stay
 * linkable without the APU library, which is what lets them be tested on the
 * host.
 *
 * The event tables live in ChessSfx.cpp.
 */
#pragma once

#include <cstdint>

namespace chessdemo {

/**
 * @enum SfxId
 * @brief The sounds a chess game makes.
 */
enum class SfxId : uint8_t {
    Move,       ///< A piece was set down on an empty square.
    Capture,    ///< A piece took another piece.
    Check,      ///< The move gave check.
    Checkmate,  ///< The move ended the game.
    Count
};

/**
 * @brief Play one sound effect.
 *
 * A no-op when the demo is built with PIXELROOT32_ENABLE_AUDIO=0, so call
 * sites never need to guard themselves.
 *
 * @param id Which sound to play.
 */
void playSfx(SfxId id);

}  // namespace chessdemo
