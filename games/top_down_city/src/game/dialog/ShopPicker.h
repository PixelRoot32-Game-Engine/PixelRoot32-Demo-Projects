#pragma once

#include <cstdint>

#include <gameplay/DialogRunner.h>
#include <gameplay/DialogTypes.h>
#include <platforms/EngineConfig.h>

#include "game/rules/Shop.h"

namespace top_down_city {

/**
 * @class ShopPicker
 * @brief The corner shop's catalogue as one choice line on DialogRunner.
 *
 * The runner owns what is open and what is highlighted; game/rules/Shop.h
 * still owns the catalogue, its order and its wrap, and each choice's tag is
 * its `shop::Line`. Headless: the scene keeps drawing the panel, because a row
 * is two columns -- label left, price right, both recoloured when the purse
 * cannot cover it -- and `graphics::DialogBox` draws one text per row in one
 * ink.
 *
 * Two places where the runner and the shop disagree are settled here rather
 * than at the call site:
 *
 * - The runner clamps Up/Down at the ends of a list; the shop wraps. So a walk
 *   asks `shop::next`/`shop::prev` for the line and hands the runner the
 *   index with `select()`.
 * - Confirm LEAVES a choice line; a sale must not close the shop. Every choice
 *   therefore leads back to the same line, and the selection the re-entry
 *   resets is put back straight after.
 *
 * Edge detection stays with the caller: the runner consumes semantic actions
 * and never sees the pad, so it cannot tell a press from a held direction.
 *
 * Not copyable: the runner points at `script_`, which points at `line_` and
 * `choices_`, all inside this object.
 */
class ShopPicker {
public:
    ShopPicker();
    ShopPicker(const ShopPicker&) = delete;
    ShopPicker& operator=(const ShopPicker&) = delete;

    /// Open on the cheapest line, every time.
    void open();
    void close();
    [[nodiscard]] bool isOpen() const;

    /**
     * @brief One logic step's presses. A no-op while closed.
     * @param upPressed UP went down this step.
     * @param downPressed DOWN went down this step. Wins over UP when both did,
     *        because both at once is reachable on a real D-pad.
     */
    void navigate(bool upPressed, bool downPressed);

    /**
     * @brief FIRE: take the highlighted line. The picker stays open on it.
     * @param out Written with the line, only when this returns true.
     * @return false, writing nothing, while closed.
     */
    bool confirm(shop::Line& out);

    /// The highlighted line, or `shop::first()` while closed.
    [[nodiscard]] shop::Line selected() const;

    /// Rows to draw: the catalogue's size while open, 0 while closed.
    [[nodiscard]] std::uint8_t rowCount() const;

    /// The line on row `row`, or `shop::first()` for a row that is not one.
    [[nodiscard]] shop::Line lineAt(std::uint8_t row) const;

    /// Changes whenever what the panel shows may have changed: opening,
    /// closing, a move, a sale. Compare by inequality only: it wraps.
    [[nodiscard]] std::uint16_t revision() const;

private:
    static void onDialogEvent(void* owner, const pixelroot32::gameplay::DialogEvent& event);

    pixelroot32::gameplay::DialogChoice choices_[shop::kLineCount];
    pixelroot32::gameplay::DialogLine   line_;
    pixelroot32::gameplay::DialogScript script_;
    pixelroot32::gameplay::DialogRunner runner_;

    /// The tag ChoiceConfirmed carried during the current confirm(), or a
    /// sentinel no `shop::Line` can be. Set by the event, read once the feed
    /// returns.
    std::uint16_t confirmedTag_;
};

static_assert(shop::kLineCount <= pixelroot32::platforms::config::DialogMaxChoices,
              "the runner clamps a choice line to DialogMaxChoices -- a catalogue "
              "longer than that has lines nobody can reach");

}  // namespace top_down_city
