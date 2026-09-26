/*
 * PromotionPicker.h - The promotion choice, as one choice line on the engine's
 * headless DialogRunner.
 */
#pragma once

#include <cstdint>

#include <gameplay/DialogRunner.h>
#include <gameplay/DialogTypes.h>
#include <platforms/EngineConfig.h>

#include "ChessConstants.h"
#include "chess/ChessRules.h"

namespace chessdemo {

/**
 * @class PromotionPicker
 * @brief Which piece a pawn becomes: the picker's state and its cells.
 *
 * The runner owns what is open and which piece was taken; each choice's tag
 * is its chess::PieceType. Headless: the scene still draws the panel, because
 * the options are piece sprites and graphics::DialogBox draws text.
 *
 * The one piece of geometry the draw loop and the hit test both need lives
 * here, in cellRect(), so the two can no longer disagree about where a cell
 * is. The option count comes from the script, and kPromoChoiceCount -- which
 * sizes the panel at compile time -- is pinned to it below.
 *
 * Touch only, so a tap is translated straight into select() and Confirm:
 * there is no cursor to walk. A tap that hits no cell cancels, as it always
 * did; the move is not committed until a piece is chosen, so there is nothing
 * to undo.
 */
class PromotionPicker {
public:
    /** @brief What a tap did. */
    enum class TapResult : uint8_t {
        Ignored,   ///< The picker was not open.
        Chosen,    ///< A piece was taken; the picker is closed.
        Cancelled  ///< The tap hit no cell; the picker is closed.
    };

    /** @brief Binds the runner's event sink to this picker. */
    PromotionPicker();
    PromotionPicker(const PromotionPicker&) = delete;
    PromotionPicker& operator=(const PromotionPicker&) = delete;

    /** @brief Show the choices. */
    void open();

    /** @brief Close without choosing. A no-op when already closed. */
    void close();

    /** @brief Whether the picker is up and the only thing accepting taps. */
    [[nodiscard]] bool isOpen() const;

    /** @brief Options to draw: kPromoChoiceCount while open, 0 while closed. */
    [[nodiscard]] uint8_t choiceCount() const;

    /**
     * @brief The piece on option `index`.
     * @return PieceType::None for an index that is not an option, or while closed.
     */
    [[nodiscard]] chess::PieceType pieceAt(uint8_t index) const;

    /**
     * @brief Screen rect of option `index`, for both drawing and hit-testing.
     * @param index Zero-based option index.
     * @param x Receives the left edge.
     * @param y Receives the top edge.
     * @param width Receives the width.
     * @param height Receives the height.
     */
    static void cellRect(uint8_t index, int& x, int& y, int& width, int& height);

    /**
     * @brief Apply a tap.
     * @param x Screen X of the tap.
     * @param y Screen Y of the tap.
     * @param chosen Receives the piece, only when the result is Chosen.
     * @return What the tap did.
     */
    TapResult tap(int16_t x, int16_t y, chess::PieceType& chosen);

private:
    static void onDialogEvent(void* owner, const pixelroot32::gameplay::DialogEvent& event);

    pixelroot32::gameplay::DialogRunner runner_;

    /** Tag of the ChoiceConfirmed seen during the current tap, or a sentinel. */
    uint16_t confirmedTag_;
};

static_assert(kPromoChoiceCount <= pixelroot32::platforms::config::DialogMaxChoices,
              "the runner clamps a choice line to DialogMaxChoices -- a picker "
              "with more options than that has pieces nobody can reach");

}  // namespace chessdemo
