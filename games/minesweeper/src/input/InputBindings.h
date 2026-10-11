#pragma once

#include <cstdint>

namespace minesweeper {

/**
 * @file InputBindings.h
 * @brief Physical button indices and controller timing, engine-free.
 *
 * Indices follow the engine InputConfig order (Up, Down, Left, Right, A, B),
 * so the scene builds the held mask straight from isButtonDown(i).
 */

// --- Physical buttons (held-mask bits) --------------------------------------
constexpr std::uint8_t BTN_UP = 0;
constexpr std::uint8_t BTN_DOWN = 1;
constexpr std::uint8_t BTN_LEFT = 2;
constexpr std::uint8_t BTN_RIGHT = 3;
constexpr std::uint8_t BTN_A = 4;  ///< Reveal / confirm.
constexpr std::uint8_t BTN_B = 5;  ///< Flag / back.
constexpr std::uint8_t BUTTON_COUNT = 6;

// --- Logical commands (poll() result bits) -----------------------------------
enum class GameCommand : std::uint8_t {
    Up = 0,
    Down = 1,
    Left = 2,
    Right = 3,
    Reveal = 4,
    ToggleFlag = 5,
    Cancel = 6  ///< A+B chord: back to menu without flagging or revealing.
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

// --- Cursor auto-repeat (spec section 8: initial delay plus interval) --------
constexpr unsigned long REPEAT_DELAY_MS = 350;
constexpr unsigned long REPEAT_INTERVAL_MS = 120;

}  // namespace minesweeper
