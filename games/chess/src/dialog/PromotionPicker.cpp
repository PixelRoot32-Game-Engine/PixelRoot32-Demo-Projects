/*
 * PromotionPicker.cpp - The promotion choice, as one choice line on the
 * engine's headless DialogRunner.
 */
#include "dialog/PromotionPicker.h"

namespace chessdemo {

namespace dlg = pixelroot32::gameplay;

namespace {

/*
 * The script: one choice line, four options, in the order the picker shows
 * them. Every option ends the dialog -- choosing is the whole interaction.
 * The scene draws sprites and its own title, so no text is ever drawn from
 * here; the option letters only keep the table readable.
 */
constexpr dlg::DialogChoice kChoices[] = {
    { "Q", dlg::kNoLine, static_cast<uint16_t>(chess::PieceType::Queen)  },
    { "R", dlg::kNoLine, static_cast<uint16_t>(chess::PieceType::Rook)   },
    { "B", dlg::kNoLine, static_cast<uint16_t>(chess::PieceType::Bishop) },
    { "N", dlg::kNoLine, static_cast<uint16_t>(chess::PieceType::Knight) },
};

constexpr uint8_t kChoiceCount = sizeof(kChoices) / sizeof(kChoices[0]);

constexpr dlg::DialogLine kLines[] = {
    { nullptr, nullptr, dlg::kNoLine, 0, 0, 0, kChoiceCount, dlg::LineKind::Choice, 0 },
};

constexpr dlg::DialogScript kScript = { kLines, kChoices, 1, kChoiceCount };

static_assert(kChoiceCount == kPromoChoiceCount,
              "kPromoChoiceCount sizes the panel; it must match the script's options");

/** No ChoiceConfirmed arrived. PieceType tags are all below it. */
constexpr uint16_t kNoConfirmed = 0xFFFF;

/** Left margin between the panel's edge and the first cell. */
constexpr int kCellMarginX = 4;

bool pointInRect(int16_t x, int16_t y, int rectX, int rectY, int width, int height) {
    return x >= rectX && x < rectX + width && y >= rectY && y < rectY + height;
}

}  // namespace

PromotionPicker::PromotionPicker() : runner_(), confirmedTag_(kNoConfirmed) {
    runner_.configure(this, &PromotionPicker::onDialogEvent);
}

void PromotionPicker::open() {
    runner_.start(kScript);
}

void PromotionPicker::close() {
    if (runner_.isActive()) runner_.stop();
}

bool PromotionPicker::isOpen() const {
    return runner_.isActive();
}

uint8_t PromotionPicker::choiceCount() const {
    return runner_.choiceCount();
}

chess::PieceType PromotionPicker::pieceAt(uint8_t index) const {
    const dlg::DialogChoice* choice = runner_.choice(index);
    return choice != nullptr ? static_cast<chess::PieceType>(choice->tag)
                             : chess::PieceType::None;
}

void PromotionPicker::cellRect(uint8_t index, int& x, int& y, int& width, int& height) {
    x      = kPromoX + kCellMarginX + index * kPromoCell;
    y      = kPromoRowY;
    width  = kPromoCell;
    height = kPromoCell;
}

PromotionPicker::TapResult PromotionPicker::tap(int16_t x, int16_t y, chess::PieceType& chosen) {
    if (!isOpen()) return TapResult::Ignored;

    for (uint8_t i = 0; i < runner_.choiceCount(); ++i) {
        int cellX = 0, cellY = 0, cellW = 0, cellH = 0;
        cellRect(i, cellX, cellY, cellW, cellH);
        if (!pointInRect(x, y, cellX, cellY, cellW, cellH)) continue;

        confirmedTag_ = kNoConfirmed;
        runner_.select(i);
        runner_.feed(dlg::DialogAction::Confirm);
        if (confirmedTag_ == kNoConfirmed) break;  // Unreachable while open.

        chosen = static_cast<chess::PieceType>(confirmedTag_);
        return TapResult::Chosen;
    }

    close();
    return TapResult::Cancelled;
}

void PromotionPicker::onDialogEvent(void* owner, const dlg::DialogEvent& event) {
    if (event.type != dlg::DialogEventType::ChoiceConfirmed) return;
    static_cast<PromotionPicker*>(owner)->confirmedTag_ = event.tag;
}

}  // namespace chessdemo
