#pragma once

#include <cstdint>

#include <gameplay/DialogRunner.h>
#include <gameplay/DialogTypes.h>

#include "game/rules/Contract.h"

namespace top_down_city {

/**
 * @class ContractDialog
 * @brief The payphone briefing: what the job is, before the clock starts.
 *
 * Answering a phone used to start the chapter on the spot, with a single
 * banner as the whole explanation. Each chapter is now a short script on the
 * engine's headless DialogRunner, in the shape
 * `examples/dialog` demonstrates: two text lines the player advances by
 * hand, then a choice line with ACCEPT / HANG UP. Confirming ACCEPT ends
 * the dialog with an Accepted verdict and the scene starts the chapter;
 * HANG UP -- or Cancel on the choice line -- ends it Declined and the
 * phone keeps ringing.
 *
 * Headless, like CityBanner and ShopPicker: the scene draws the panel
 * through `graphics::DialogBox` and only reads the runner through this
 * adapter. Edge detection stays with the caller -- the runner consumes
 * semantic actions and never sees the pad.
 *
 * Not copyable: the runner points at one of the static scripts, and the
 * verdict belongs to the object that was fed.
 */
class ContractDialog {
public:
    /// What `takeVerdict` reports. None means "no decision yet" -- the
    /// dialog is still running, or it finished without one.
    enum class Verdict : std::uint8_t { None, Accepted, Declined };

    ContractDialog();
    ContractDialog(const ContractDialog&) = delete;
    ContractDialog& operator=(const ContractDialog&) = delete;

    /// Open the briefing for `chapter`. A no-op for values past the story
    /// (Chapter::Count is not a chapter); the scene only calls this under
    /// `contract::offered`, which already bounds it.
    void open(contract::Chapter chapter);

    void close();

    [[nodiscard]] bool isOpen() const;

    /// The chapter whose briefing is (or was) up. Boost until first open.
    [[nodiscard]] contract::Chapter chapter() const { return chapter_; }

    /// RUN: advance a text line, or confirm the highlighted choice.
    void advance();

    /**
     * @brief UP/DOWN on a logic step. A no-op while closed.
     * @param upPressed UP went down this step.
     * @param downPressed DOWN went down this step. Wins over UP when both
     *        did, because both at once is reachable on a real D-pad.
     */
    void navigate(bool upPressed, bool downPressed);

    /// FIRE: cancel out of the choice line. A no-op on text lines, where
    /// the runner ignores Cancel -- answering a phone by accident is
    /// undone at the choice, not halfway through reading it.
    void cancel();

    /// One slice of real time, for text lines that auto-advance. The
    /// shipped scripts never set one, so this is usually a formality --
    /// kept because a briefing without an update call is a timer nobody
    /// ticks.
    void update(unsigned long deltaMs);

    /**
     * @brief Read and clear the pending decision.
     * @return Accepted once, after the player confirmed ACCEPT; Declined
     *         once, after HANG UP or an allowed Cancel; None otherwise.
     *         Each decision is reported exactly once -- the runner fires
     *         ChoiceConfirmed synchronously inside feed(), and this is
     *         where the scene picks it up, after feed() has returned.
     */
    Verdict takeVerdict();

    /// The runner, for the scene's DialogBox to draw. Non-const: drawing
    /// settles the page count, which only the presenter can compute.
    pixelroot32::gameplay::DialogRunner& runner() { return runner_; }

    /// Changes whenever what the panel shows may have changed: opening,
    /// advancing, moving the highlight, closing. Compare by inequality
    /// only: it wraps.
    [[nodiscard]] std::uint16_t revision() const;

    /// The script behind `chapter`, for measuring the panel. Borrowed, as
    /// the runner's contract states for every script.
    static const pixelroot32::gameplay::DialogScript& scriptFor(
        contract::Chapter chapter);

private:
    static void onDialogEvent(void* owner,
                              const pixelroot32::gameplay::DialogEvent& event);

    pixelroot32::gameplay::DialogRunner runner_;
    contract::Chapter chapter_;

    /// Tag carried by the last ChoiceConfirmed, or a sentinel no choice
    /// tag can be. Set by the event, read once by takeVerdict().
    std::uint16_t confirmedTag_;

    /// A Cancelled event arrived since the last takeVerdict().
    bool cancelled_;
};

}  // namespace top_down_city
