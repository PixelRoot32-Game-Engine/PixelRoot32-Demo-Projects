#pragma once
#include <cstdint>

#include "game/rules/Weapon.h"

/**
 * @brief What the corner shop sells, in what order, and for how much.
 *
 * **The buttons this file is made of.** The counter used to be one line and
 * one price, because a menu needs a button and the pad has six. That was true
 * everywhere except at the till: the shop is the one room in the city with
 * nobody in it -- `hitPersonBox` there reports nothing hit, on purpose -- so
 * a picker opened at the counter can OWN the pad without taking anything away
 * from the player, there being nothing to walk towards and nothing to shoot
 * at. RUN opens the picker, UP and DOWN walk it, FIRE buys the highlighted
 * line and RUN closes it again: no seventh button, no scene, no input mode
 * outside those few tiles. The same shape as the chess demo's promotion
 * picker -- a modal panel that is the only thing accepting input while it is
 * up -- with a D-pad where that one has a finger.
 *
 * Which makes the list a rule rather than a constant, and every part of it
 * fails silently: a `next` that skips a line is a product in the table and
 * nowhere the player can reach; a walk that does not wrap is a list they run
 * off the end of; a `prev` written as an unsigned decrement wraps to 255 and
 * indexes nothing; a price typed here as well as in Economy.h is a counter
 * that advertises one number and takes another. Engine-free, so
 * `pio test -e host_test` answers all of it.
 *
 * **Why an offer carries no formatted text.** The picker draws the label and
 * formats the price into a fixed buffer at draw time, the same way the HUD's
 * cash and health readouts already do -- there is no formatting call anywhere
 * in this HUD and a shop is not the place to introduce one. So a price cannot
 * drift away from what is displayed: there is only ever one copy of it.
 */
namespace top_down_city::shop {

/// The counter's lines, cheapest first. The order is the design: a player who
/// walks in with one delivery's wages sees something they can nearly afford
/// at the top and it gets dearer downwards, so the picker reads as a ladder
/// rather than as three unrelated things.
enum class Line : std::uint8_t {
    Pistol = 0,
    Vest,
    Shotgun,
    Count,
};

/// How many rows the picker has to draw. A constant rather than a cast at
/// every use because CityConstants.h sizes the panel from it: a fourth line
/// added to the enum and not to the panel is a product drawn off its own box.
constexpr std::uint8_t kLineCount = static_cast<std::uint8_t>(Line::Count);

/// One line of the catalogue: everything the till needs, nothing it does.
/// `arms` and `vest` are exclusive flags rather than something derived from
/// `weapon`, because a weapon id of zero is a real weapon -- a bool that
/// meant "not the pistol" would make the pistol line the vest.
struct Offer {
    /// What the picker draws in the row, and what the banner says when the
    /// sale goes through. At most `kMaxLabelChars`.
    const char*       label;
    std::uint16_t     price;
    /// Only meaningful when `arms`.
    weapons::WeaponId weapon;
    bool              arms;
    bool              vest;
    /// Points of armour, or 0. Pinned against `armor::kVestPoints`.
    std::uint8_t      armor;
};

/// The longest label a row can hold. The picker draws the label from the left
/// of a row and the price hard against the right; text is drawn from a
/// pointer, not measured and wrapped, so a label too long for its row is not
/// shrunk -- it runs straight through the price. CityConstants.h asserts the
/// other end of the same arithmetic: this many characters plus a price still
/// fits inside the panel.
constexpr std::uint8_t kMaxLabelChars = 7;   // SHOTGUN

/// The largest price the picker can draw. Four digits, matching
/// `economy::kMaxCash`, because the buffer is fixed and a price wider than it
/// does not overflow -- it silently loses its leading digit, and a 1500
/// dollar line is advertised at 500.
constexpr std::uint16_t kMaxDrawnPrice = 9999;

/// The line the picker opens on. Also the answer for any `Line` that is not
/// one -- see `next`.
Line first();

/// DOWN: the line after this one, wrapping past the last back to the first.
/// Out of range returns `first()` rather than running off the table: same
/// policy as `weapons::spec`, and for the same reason -- this is reached from
/// a keypress on a board with no console to print an assertion to.
Line next(Line line);

/// UP: the line before this one, wrapping past the first back to the last.
/// The unsigned decrement is the whole reason this is a function and not an
/// expression at the call site.
Line prev(Line line);

/// The table. Out of line so the array has one definition rather than one per
/// translation unit that mentions the shop.
const Offer& offer(Line line);

}  // namespace top_down_city::shop
