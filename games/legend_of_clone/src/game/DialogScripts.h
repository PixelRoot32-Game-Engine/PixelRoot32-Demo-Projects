#pragma once

#include "platforms/PlatformDefaults.h"
#include "gameplay/DialogTypes.h"

#include <cstdint>

/**
 * @file DialogScripts.h
 * @brief Every line the slice can show, as const flash tables.
 *
 * One script per object. The text is the content defined in the engine's
 * docs/audits/dialog-rpg-slice-requirements.md, word for word. The line ids
 * below are the table positions, so a script edit that moves a line has to
 * move its constant with it; test_rules checks the links between them.
 */
namespace legend_of_clone {

// --- Tags ----------------------------------------------------------------
// Opaque to the engine. DialogController reads them from LineEnter and
// ChoiceConfirmed events.

inline constexpr uint16_t TAG_NONE         = 0;
inline constexpr uint16_t TAG_GIVE_SWORD   = 1;  ///< LineEnter: sets hasSword.
inline constexpr uint16_t TAG_CHEST_REWARD = 2;  ///< LineEnter: pays the chest once.
inline constexpr uint16_t TAG_BUY_SHIELD   = 3;  ///< ChoiceConfirmed: once only.
inline constexpr uint16_t TAG_BUY_KEY      = 4;  ///< ChoiceConfirmed: once only.
inline constexpr uint16_t TAG_BUY_POTION   = 5;  ///< ChoiceConfirmed: repeatable.

// --- Sign ----------------------------------------------------------------

inline constexpr pixelroot32::gameplay::LineId kSignLine = 0;

// --- Old man -------------------------------------------------------------

/// First visit. Long enough to page, then leads to kOldManSword.
inline constexpr pixelroot32::gameplay::LineId kOldManIntro      = 0;
/// Carries TAG_GIVE_SWORD.
inline constexpr pixelroot32::gameplay::LineId kOldManSword      = 1;
/// Every visit once the player has the sword.
inline constexpr pixelroot32::gameplay::LineId kOldManDirections = 2;

// --- Chest ---------------------------------------------------------------

/// Carries TAG_CHEST_REWARD.
inline constexpr pixelroot32::gameplay::LineId kChestLine = 0;

// --- Shop ----------------------------------------------------------------
// One choice line per (hasShield, hasKey) state, each listing only what is
// still for sale plus LEAVE. That is 2^2 = 4 lines for two once-only items;
// the engine has no choice filter yet (roadmap ChoiceFilterFn).

inline constexpr pixelroot32::gameplay::LineId kShopMenuNothingOwned = 0;
inline constexpr pixelroot32::gameplay::LineId kShopMenuShieldOwned  = 1;
inline constexpr pixelroot32::gameplay::LineId kShopMenuKeyOwned     = 2;
inline constexpr pixelroot32::gameplay::LineId kShopMenuBothOwned    = 3;
/// Started by the controller on the frame after an affordable purchase.
inline constexpr pixelroot32::gameplay::LineId kShopThankYou         = 4;
/// Started by the controller on the frame after an unaffordable purchase.
inline constexpr pixelroot32::gameplay::LineId kShopNotEnough        = 5;

extern const pixelroot32::gameplay::DialogScript kSignScript;
extern const pixelroot32::gameplay::DialogScript kOldManScript;
extern const pixelroot32::gameplay::DialogScript kChestScript;
extern const pixelroot32::gameplay::DialogScript kShopScript;

} // namespace legend_of_clone
