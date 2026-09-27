#include "game/dialog/ShopPicker.h"

#include <cstdint>

namespace top_down_city {

namespace dlg = pixelroot32::gameplay;

namespace {

/// The script's only line.
constexpr dlg::LineId kCatalogueLine = 0;

/// No ChoiceConfirmed arrived. Outside every `shop::Line` a tag can carry.
constexpr std::uint16_t kNoConfirmed = 0xFFFF;

}  // namespace

ShopPicker::ShopPicker()
    : choices_{},
      line_{nullptr, nullptr, dlg::kNoLine, 0, 0, 0, shop::kLineCount,
            dlg::LineKind::Choice, 0},
      script_{&line_, choices_, 1, shop::kLineCount},
      runner_(),
      confirmedTag_(kNoConfirmed) {
    for (std::uint8_t i = 0; i < shop::kLineCount; ++i) {
        const shop::Line line = static_cast<shop::Line>(i);
        // Back to the catalogue, not kNoLine: a sale does not close the shop.
        choices_[i] = dlg::DialogChoice{shop::offer(line).label, kCatalogueLine, i};
    }
    runner_.configure(this, &ShopPicker::onDialogEvent);
}

void ShopPicker::open() {
    runner_.start(script_, kCatalogueLine);
}

void ShopPicker::close() {
    if (runner_.isActive()) {
        runner_.stop();
    }
}

bool ShopPicker::isOpen() const {
    return runner_.isActive();
}

void ShopPicker::navigate(bool upPressed, bool downPressed) {
    if (!isOpen()) {
        return;
    }
    const shop::Line current = selected();
    shop::Line target = current;
    if (downPressed) {
        target = shop::next(current);
    } else if (upPressed) {
        target = shop::prev(current);
    }
    runner_.select(static_cast<dlg::ChoiceId>(target));
}

bool ShopPicker::confirm(shop::Line& out) {
    if (!isOpen()) {
        return false;
    }
    const dlg::ChoiceId highlighted = runner_.selectedChoice();
    confirmedTag_ = kNoConfirmed;
    runner_.feed(dlg::DialogAction::Confirm);
    // Re-entering the catalogue line reset the highlight to the top row.
    runner_.select(highlighted);
    if (confirmedTag_ == kNoConfirmed) {
        return false;
    }
    out = static_cast<shop::Line>(confirmedTag_);
    return true;
}

shop::Line ShopPicker::selected() const {
    const dlg::ChoiceId index = runner_.selectedChoice();
    return index == dlg::kNoChoice ? shop::first() : lineAt(index);
}

std::uint8_t ShopPicker::rowCount() const {
    return runner_.choiceCount();
}

shop::Line ShopPicker::lineAt(std::uint8_t row) const {
    const dlg::DialogChoice* choice = runner_.choice(row);
    return choice != nullptr ? static_cast<shop::Line>(choice->tag) : shop::first();
}

std::uint16_t ShopPicker::revision() const {
    return runner_.revision();
}

void ShopPicker::onDialogEvent(void* owner, const dlg::DialogEvent& event) {
    if (event.type != dlg::DialogEventType::ChoiceConfirmed) {
        return;
    }
    static_cast<ShopPicker*>(owner)->confirmedTag_ = event.tag;
}

}  // namespace top_down_city
