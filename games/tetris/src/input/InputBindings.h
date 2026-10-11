#pragma once

#include <cstdint>

namespace tetris {

/**
 * @file InputBindings.h
 * @brief Physical button indices and controller timing, engine-free.
 *
 * Indices follow the engine InputConfig order (Up, Down, Left, Right, A, B),
 * so the scene builds the held mask straight from isButtonDown(i).
 *
 * Mapping (6 buttons, no extra GPIO needed):
 * - Left/Right: D-pad step (auto-repeat DAS).
 * - Down: soft drop while held (level, not a pulse).
 * - Up: hard drop (edge only).
 * - A: rotate clockwise / confirm (edge only).
 * - B: rotate counter-clockwise (edge only).
 * - A+B chord: pause/resume, back-to-title, retry (edge, swallows A/B).
 */

constexpr std::uint8_t BTN_UP = 0;
constexpr std::uint8_t BTN_DOWN = 1;
constexpr std::uint8_t BTN_LEFT = 2;
constexpr std::uint8_t BTN_RIGHT = 3;
constexpr std::uint8_t BTN_A = 4;
constexpr std::uint8_t BTN_B = 5;
constexpr std::uint8_t BUTTON_COUNT = 6;

/** Logical commands (poll() result bits). */
enum class GameCommand : std::uint8_t {
    Left = 0,
    Right = 1,
    RotateCw = 2,
    RotateCcw = 3,
    HardDrop = 4,
    Confirm = 5,
    Pause = 6
};

/**
 * @brief Result-mask bit for one logical command.
 * @param command Command to encode.
 * @return Single-bit mask usable with hasCommand().
 */
constexpr std::uint16_t commandBit(GameCommand command) {
    return static_cast<std::uint16_t>(1U << static_cast<std::uint8_t>(command));
}

/**
 * @brief Tests one command inside a poll() result mask.
 * @param mask Value returned by InputController::poll().
 * @param command Command to look for.
 * @return True when the command fired this frame.
 */
constexpr bool hasCommand(std::uint16_t mask, GameCommand command) {
    return (mask & commandBit(command)) != 0;
}

/** True while the soft-drop button is down in a held mask. */
constexpr bool softDropHeld(std::uint16_t heldMask) {
    return (heldMask & (1U << BTN_DOWN)) != 0;
}

// --- Horizontal auto-repeat (DAS): first step at once, then repeat --------
constexpr unsigned long DAS_DELAY_MS = 170;
constexpr unsigned long DAS_INTERVAL_MS = 50;

}  // namespace tetris
