#include "game/dialog/ContractDialog.h"

#include <cstdint>

namespace top_down_city {

namespace dlg = pixelroot32::gameplay;

namespace {

constexpr const char* kCaller = "CALLER";

/// Tags on the choice lines. Neither is interpreted by the runner.
constexpr std::uint16_t kTagAccept  = 1;
constexpr std::uint16_t kTagDecline = 2;

/// No ChoiceConfirmed arrived. Outside every tag above.
constexpr std::uint16_t kNoConfirmed = 0xFFFF;

constexpr dlg::LineId kBriefLine = 0;
constexpr dlg::LineId kDetailLine = 1;
constexpr dlg::LineId kChoiceLine = 2;

// --- Chapter 1: Boost ------------------------------------------------------
// Downtown phone, a parked car in the Marina, a drop in the Suburbs, one
// clock for the walk plus the drive, in THAT car. Pays $100: the pistol
// plus change, for the chapter that follows.

const dlg::DialogChoice kBoostChoices[] = {
    {"ACCEPT", dlg::kNoLine, kTagAccept},
    {"HANG UP", dlg::kNoLine, kTagDecline},
};

const dlg::DialogLine kBoostLines[] = {
    {"Downtown job. A car waits in the Marina.", kCaller, kDetailLine,
     0, 0, 0, 0, dlg::LineKind::Text, 0},
    {"Steal THAT car, drive it to the Suburbs drop. One clock for walk plus "
     "drive. Pays $100.",
     kCaller, kChoiceLine, 0, 0, 0, 0, dlg::LineKind::Text, 0},
    {"Take the boost?", kCaller, dlg::kNoLine, 0, 0, 0, 2,
     dlg::LineKind::Choice, dlg::kLineFlagAllowCancel},
};

const dlg::DialogScript kBoostScript{
    kBoostLines, kBoostChoices,
    static_cast<std::uint16_t>(sizeof(kBoostLines) / sizeof(kBoostLines[0])),
    static_cast<std::uint16_t>(
        sizeof(kBoostChoices) / sizeof(kBoostChoices[0]))};

// --- Chapter 2: The Hit ----------------------------------------------------
// Suburbs phone beside chapter 1's drop, a walk to the station, the marked
// officer at the back of the lobby, then an untimed retreat under the
// stars. Pays $125: the gun it took plus most of a vest.

const dlg::DialogChoice kHitChoices[] = {
    {"ACCEPT", dlg::kNoLine, kTagAccept},
    {"HANG UP", dlg::kNoLine, kTagDecline},
};

const dlg::DialogLine kHitLines[] = {
    {"Station job. One marked officer, back of the lobby.", kCaller,
     kDetailLine, 0, 0, 0, 0, dlg::LineKind::Text, 0},
    {"Walk in past the others, drop him. Clock stops on the hit, then lose "
     "the stars. Pays $125.",
     kCaller, kChoiceLine, 0, 0, 0, 0, dlg::LineKind::Text, 0},
    {"Take the hit?", kCaller, dlg::kNoLine, 0, 0, 0, 2,
     dlg::LineKind::Choice, dlg::kLineFlagAllowCancel},
};

const dlg::DialogScript kHitScript{
    kHitLines, kHitChoices,
    static_cast<std::uint16_t>(sizeof(kHitLines) / sizeof(kHitLines[0])),
    static_cast<std::uint16_t>(
        sizeof(kHitChoices) / sizeof(kHitChoices[0]))};

// --- Chapter 3: Frenzy -----------------------------------------------------
// Park phone on the busiest corner, no destination: twelve bodies on one
// clock. A pistol holds ten and a shotgun twelve, so the arithmetic sends
// the player to the shop first. Pays $200, the story ends with it.

const dlg::DialogChoice kFrenzyChoices[] = {
    {"ACCEPT", dlg::kNoLine, kTagAccept},
    {"HANG UP", dlg::kNoLine, kTagDecline},
};

const dlg::DialogLine kFrenzyLines[] = {
    {"Last job. This Park corner IS the job.", kCaller, kDetailLine, 0, 0,
     0, 0, dlg::LineKind::Text, 0},
    {"12 bodies before the clock dies. A pistol holds 10. Buy the shotgun "
     "first. Pays $200.",
     kCaller, kChoiceLine, 0, 0, 0, 0, dlg::LineKind::Text, 0},
    {"Start the rampage?", kCaller, dlg::kNoLine, 0, 0, 0, 2,
     dlg::LineKind::Choice, dlg::kLineFlagAllowCancel},
};

const dlg::DialogScript kFrenzyScript{
    kFrenzyLines, kFrenzyChoices,
    static_cast<std::uint16_t>(
        sizeof(kFrenzyLines) / sizeof(kFrenzyLines[0])),
    static_cast<std::uint16_t>(
        sizeof(kFrenzyChoices) / sizeof(kFrenzyChoices[0]))};

}  // namespace

ContractDialog::ContractDialog()
    : runner_(),
      chapter_(contract::Chapter::Boost),
      confirmedTag_(kNoConfirmed),
      cancelled_(false) {
    runner_.configure(this, &ContractDialog::onDialogEvent);
}

const dlg::DialogScript& ContractDialog::scriptFor(
    contract::Chapter chapter) {
    switch (chapter) {
        case contract::Chapter::Hit:
            return kHitScript;
        case contract::Chapter::Frenzy:
            return kFrenzyScript;
        case contract::Chapter::Boost:
        default:
            return kBoostScript;
    }
}

void ContractDialog::open(contract::Chapter chapter) {
    if (static_cast<std::uint8_t>(chapter)
        >= static_cast<std::uint8_t>(contract::Chapter::Count)) {
        return;
    }
    chapter_ = chapter;
    confirmedTag_ = kNoConfirmed;
    cancelled_ = false;
    runner_.start(scriptFor(chapter_), kBriefLine);
}

void ContractDialog::close() {
    if (runner_.isActive()) {
        runner_.stop();
    }
    confirmedTag_ = kNoConfirmed;
    cancelled_ = false;
}

bool ContractDialog::isOpen() const {
    return runner_.isActive();
}

void ContractDialog::advance() {
    if (!isOpen()) {
        return;
    }
    // Confirm aliases Advance on text lines, so one button both walks the
    // briefing and answers the choice -- the shape examples/dialog uses.
    runner_.feed(dlg::DialogAction::Confirm);
}

void ContractDialog::navigate(bool upPressed, bool downPressed) {
    if (!isOpen()) {
        return;
    }
    if (downPressed) {
        runner_.feed(dlg::DialogAction::Down);
    } else if (upPressed) {
        runner_.feed(dlg::DialogAction::Up);
    }
}

void ContractDialog::cancel() {
    if (!isOpen()) {
        return;
    }
    runner_.feed(dlg::DialogAction::Cancel);
}

void ContractDialog::update(unsigned long deltaMs) {
    if (!isOpen()) {
        return;
    }
    runner_.update(deltaMs);
}

ContractDialog::Verdict ContractDialog::takeVerdict() {
    if (confirmedTag_ != kNoConfirmed) {
        const std::uint16_t tag = confirmedTag_;
        confirmedTag_ = kNoConfirmed;
        cancelled_ = false;
        return tag == kTagAccept ? Verdict::Accepted : Verdict::Declined;
    }
    if (cancelled_) {
        cancelled_ = false;
        return Verdict::Declined;
    }
    return Verdict::None;
}

std::uint16_t ContractDialog::revision() const {
    return runner_.revision();
}

void ContractDialog::onDialogEvent(void* owner, const dlg::DialogEvent& event) {
    ContractDialog* self = static_cast<ContractDialog*>(owner);
    if (event.type == dlg::DialogEventType::ChoiceConfirmed) {
        self->confirmedTag_ = event.tag;
    } else if (event.type == dlg::DialogEventType::Cancelled) {
        self->cancelled_ = true;
    }
}

}  // namespace top_down_city
