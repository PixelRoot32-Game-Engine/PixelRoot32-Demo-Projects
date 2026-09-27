#pragma once

#include "platforms/PlatformDefaults.h"
#include "gameplay/DialogRunner.h"

#include "game/DialogScripts.h"
#include "game/GameState.h"
#include "game/Interaction.h"
#include "game/ShopRules.h"

namespace legend_of_clone {

/// This frame's press edges, already read from the buttons by the scene.
struct DialogInput {
    bool up = false;
    bool down = false;
    bool confirm = false;  ///< A: opens a dialog, advances a line, confirms a choice.
    bool cancel = false;   ///< B: leaves a menu that allows it.
};

/**
 * @class DialogController
 * @brief Starts the slice's dialogs, feeds them input, and applies their tags
 *        to the game state. One frame per update() call.
 *
 * Knows nothing about pixels or buttons: the scene reads the buttons, works
 * out what the player is facing, and draws whatever runner() is showing. That
 * keeps every rule here testable against the engine's real DialogRunner with
 * no Renderer.
 *
 * ### One frame
 *
 *  1. While a dialog is active, Up, Down, A (Confirm) and B (Cancel) are fed to
 *     it, in that order.
 *  2. Otherwise an A press starts the dialog for what the player faces. That
 *     press is not also fed to the runner, or the first line would be skipped.
 *
 * ### How a purchase is answered (R7)
 *
 * A choice's next line is a constant in flash, so it cannot fork on the
 * player's rupees, and a start() made from inside the runner's event callback
 * is dropped by its reentrancy guard. So every buy choice ends the dialog, and
 * confirm() answers it right after feed() returns: still inside the same
 * update(), but no longer inside the callback. The scene polls the runner only
 * after update() returns, so it never sees the Finished state in between.
 */
class DialogController {
public:
    /// Binds the game state this controller reads and writes. Never owned.
    explicit DialogController(GameState& state);

    DialogController(const DialogController&) = delete;
    DialogController& operator=(const DialogController&) = delete;

    /**
     * @brief Runs one frame.
     * @param input       This frame's press edges.
     * @param facing      What occupies the cell in front of the player.
     * @param deltaTimeMs Milliseconds since the previous frame.
     */
    void update(const DialogInput& input, Interactable facing, unsigned long deltaTimeMs);

    /// Drops any dialog. For scene init.
    void reset();

    /// True while a dialog is on screen.
    [[nodiscard]] bool blocksPlayer() const;

    /// The runner to present. Non-const because DialogBox::draw sets its page count.
    [[nodiscard]] pixelroot32::gameplay::DialogRunner& runner() { return runner_; }
    [[nodiscard]] const pixelroot32::gameplay::DialogRunner& runner() const { return runner_; }

    /**
     * @brief The DialogEventFn the constructor binds, with `owner` = this controller.
     *
     * Public only so a test can interpose a recorder on the runner and forward
     * each event here; the game never calls it directly.
     */
    static void onDialogEvent(void* owner, const pixelroot32::gameplay::DialogEvent& event);

private:
    void handleDialogEvent(const pixelroot32::gameplay::DialogEvent& event);
    void startFor(Interactable target);
    void confirm();

    GameState& state_;
    pixelroot32::gameplay::DialogRunner runner_;
};

} // namespace legend_of_clone
