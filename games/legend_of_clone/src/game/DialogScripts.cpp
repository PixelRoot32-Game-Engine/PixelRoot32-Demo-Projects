#include "game/DialogScripts.h"

#include <cstddef>
#include <iterator>

namespace legend_of_clone {

namespace gp = pixelroot32::gameplay;

namespace {

constexpr const char* kOldMan     = "OLD MAN";
constexpr const char* kShopkeeper = "SHOPKEEPER";

constexpr uint16_t countOf(std::size_t n) { return static_cast<uint16_t>(n); }

// Field order: text, speaker, next, tag, autoAdvanceMs, firstChoice,
// choiceCount, kind, flags. Every line waits for the player (autoAdvanceMs 0).

const gp::DialogLine kSignLines[] = {
    {"CAVE AHEAD. THE OLD MAN INSIDE KNOWS THE WAY.", nullptr, gp::kNoLine, TAG_NONE, 0, 0, 0,
     gp::LineKind::Text, 0},
};

const gp::DialogLine kOldManLines[] = {
    // kOldManIntro. Wraps to more than DialogMaxWrappedLines at the box width,
    // so the player pages through it (test_dialog_layout pins this).
    {"IT'S DANGEROUS TO GO ALONE! THE CAVES BELOW ARE FULL OF MONSTERS, "
     "AND THE CHEST TO THE NORTH WILL NOT OPEN ITSELF. TAKE THIS.",
     kOldMan, kOldManSword, TAG_NONE, 0, 0, 0, gp::LineKind::Text, 0},
    // kOldManSword
    {"YOU GOT THE SWORD!", kOldMan, gp::kNoLine, TAG_GIVE_SWORD, 0, 0, 0, gp::LineKind::Text, 0},
    // kOldManDirections
    {"GO NOW. THE SHOPKEEPER TO THE EAST TRADES FOR RUPEES.", kOldMan, gp::kNoLine, TAG_NONE, 0,
     0, 0, gp::LineKind::Text, 0},
};

const gp::DialogLine kChestLines[] = {
    {"YOU FOUND 40 RUPEES!", nullptr, gp::kNoLine, TAG_CHEST_REWARD, 0, 0, 0, gp::LineKind::Text,
     0},
};

// Every buy choice points at kNoLine. DialogChoice::next is a constant, so it
// cannot fork on the player's rupees; once Confirm has ended the menu, the
// controller starts THANK YOU or NOT ENOUGH RUPEES in the same frame.
const gp::DialogChoice kShopChoices[] = {
    // kShopMenuNothingOwned: 0..3
    {"SHIELD   30", gp::kNoLine, TAG_BUY_SHIELD},
    {"KEY      20", gp::kNoLine, TAG_BUY_KEY},
    {"POTION   10", gp::kNoLine, TAG_BUY_POTION},
    {"LEAVE", gp::kNoLine, TAG_NONE},
    // kShopMenuShieldOwned: 4..6
    {"KEY      20", gp::kNoLine, TAG_BUY_KEY},
    {"POTION   10", gp::kNoLine, TAG_BUY_POTION},
    {"LEAVE", gp::kNoLine, TAG_NONE},
    // kShopMenuKeyOwned: 7..9
    {"SHIELD   30", gp::kNoLine, TAG_BUY_SHIELD},
    {"POTION   10", gp::kNoLine, TAG_BUY_POTION},
    {"LEAVE", gp::kNoLine, TAG_NONE},
    // kShopMenuBothOwned: 10..11
    {"POTION   10", gp::kNoLine, TAG_BUY_POTION},
    {"LEAVE", gp::kNoLine, TAG_NONE},
};

const gp::DialogLine kShopLines[] = {
    {"BUY SOMETHING?", kShopkeeper, gp::kNoLine, TAG_NONE, 0, 0, 4, gp::LineKind::Choice,
     gp::kLineFlagAllowCancel},
    {"BUY SOMETHING?", kShopkeeper, gp::kNoLine, TAG_NONE, 0, 4, 3, gp::LineKind::Choice,
     gp::kLineFlagAllowCancel},
    {"BUY SOMETHING?", kShopkeeper, gp::kNoLine, TAG_NONE, 0, 7, 3, gp::LineKind::Choice,
     gp::kLineFlagAllowCancel},
    {"BUY SOMETHING?", kShopkeeper, gp::kNoLine, TAG_NONE, 0, 10, 2, gp::LineKind::Choice,
     gp::kLineFlagAllowCancel},
    // kShopThankYou
    {"THANK YOU!", kShopkeeper, gp::kNoLine, TAG_NONE, 0, 0, 0, gp::LineKind::Text, 0},
    // kShopNotEnough
    {"NOT ENOUGH RUPEES.", kShopkeeper, gp::kNoLine, TAG_NONE, 0, 0, 0, gp::LineKind::Text, 0},
};

}  // namespace

const gp::DialogScript kSignScript{kSignLines, nullptr, countOf(std::size(kSignLines)), 0};
const gp::DialogScript kOldManScript{kOldManLines, nullptr, countOf(std::size(kOldManLines)), 0};
const gp::DialogScript kChestScript{kChestLines, nullptr, countOf(std::size(kChestLines)), 0};
const gp::DialogScript kShopScript{kShopLines, kShopChoices, countOf(std::size(kShopLines)),
                                   countOf(std::size(kShopChoices))};

} // namespace legend_of_clone
