/*
 * ChessScene.cpp - Presentation and touch interaction for the chess demo.
 */
#include "ChessScene.h"

#include <graphics/FontManager.h>
#include <graphics/TextLayout.h>

#include "ChessConstants.h"
#include "ChessPalettes.h"
#include "assets/ChessPieces.h"
#include "assets/audio/ChessSfx.h"

namespace chessdemo {

namespace gfx   = pixelroot32::graphics;
namespace input = pixelroot32::input;

namespace {

/**
 * X that centres `text` inside a box, so button labels do not need a helper.
 *
 * Measured by the engine rather than by a character count times a copied
 * advance, so it follows the font. TextLayout stops at the last glyph, while
 * the advance this replaced also counted the gap after it; that gap is added
 * back so every label stays on the pixel it was drawn at.
 */
int centeredTextX(int boxX, int boxWidth, const char* text, int size) {
    const gfx::Font* font = gfx::FontManager::getDefaultFont();
    int textWidth = gfx::TextLayout::measureWidthPx(text, font, static_cast<uint8_t>(size));
    if (textWidth > 0 && font != nullptr) textWidth += font->spacing * size;
    return boxX + (boxWidth - textWidth) / 2;
}

bool pointInRect(int16_t x, int16_t y, int rectX, int rectY, int width, int height) {
    return x >= rectX && x < rectX + width && y >= rectY && y < rectY + height;
}

int clamp(int value, int low, int high) {
    return value < low ? low : (value > high ? high : value);
}

/**
 * Draw a 4bpp sprite at its stored size.
 *
 * Renderer::drawSprite has both a (paletteSlot, flipX) and a (flipX) overload
 * for Sprite4bpp, and both default down to three arguments, so a three-argument
 * call is ambiguous. The slot is spelled out to pick the right one.
 */
void drawSprite4bpp(gfx::Renderer& renderer, const gfx::Sprite4bpp& sprite,
                    int x, int y) {
    renderer.drawSprite(sprite, x, y, static_cast<uint8_t>(0), false);
}

}  // namespace

// --- Scene lifecycle ---------------------------------------------------------

void ChessScene::init() {
    Scene::init();

    // Palette state is global to the engine, not per-renderer, so this is a
    // one-shot rather than something draw() re-establishes every frame.
    applyPalettes();

    startNewGame();
}

void ChessScene::update(unsigned long deltaTime) {
    Scene::update(deltaTime);

    // The rules are turn-based and fully event-driven - they only ever advance
    // in onUnconsumedTouchEvent. The capture feedback is the one thing here
    // with a clock of its own.
    effects_.update(deltaTime);
}

void ChessScene::startNewGame() {
    game_.reset();
    cancelDrag();

    // A jolt left over from the last game's final capture has nothing to do
    // with the fresh board it would be shaking.
    effects_.clear();
}

void ChessScene::cancelDrag() {
    interaction_    = Interaction::Idle;
    dragOrigin_     = chess::kNoSquare;
    dragMoveCount_  = 0;
    hoveredSquare_  = chess::kNoSquare;
    promotion_.close();
    promotionFrom_  = chess::kNoSquare;
    promotionTo_    = chess::kNoSquare;
}

// --- Input -------------------------------------------------------------------

/*
 * Event flow from TouchStateMachine, and what each one means here:
 *
 *   TouchDown            press          - pick the piece up
 *   DragStart, DragMove  finger moves   - carry it
 *   DragEnd              release        - drop it (no TouchUp/Click follows)
 *   TouchUp              release        - only when the finger never moved,
 *                                         so the piece goes back down
 *   Click                release        - HUD buttons and the promotion picker
 *
 * A release that followed a drag emits DragEnd *only*, which is what keeps a
 * piece dropped near the HUD from also pressing a button.
 */
void ChessScene::onUnconsumedTouchEvent(const input::TouchEvent& event) {
    // An event another consumer already claimed must not act twice. Nothing
    // consumes events today (the UI system is compiled out), but the moment a
    // UIManager is added it marks its own events and this becomes load-bearing.
    if (event.isConsumed()) return;

    using T = input::TouchEventType;

    switch (event.getType()) {
        case T::TouchDown:
            handlePress(event.x, event.y);
            break;

        case T::DragStart:
        case T::DragMove:
            handleDragMove(event.x, event.y);
            break;

        case T::DragEnd:
            handleDrop(event.x, event.y);
            break;

        case T::TouchUp:
            // Pressed and released without moving: put the piece back.
            if (interaction_ == Interaction::DraggingPiece) cancelDrag();
            break;

        case T::Click:
            handleTap(event.x, event.y);
            break;

        default:
            break;
    }
}

void ChessScene::handlePress(int16_t x, int16_t y) {
    // While the picker is open nothing on the board is grabbable.
    if (promotion_.isOpen()) return;
    if (chess::isGameOver(game_.status())) return;

    const chess::Square origin = squareAt(x, y);
    if (origin == chess::kNoSquare) return;

    beginDrag(origin, x, y);
}

void ChessScene::beginDrag(chess::Square origin, int16_t x, int16_t y) {
    if (!game_.canSelect(origin)) {
        cancelDrag();
        return;
    }

    dragOrigin_    = origin;
    dragMoveCount_ = game_.legalMovesFrom(origin, dragMoves_, kMaxMovesFromSquare);
    dragX_         = x;
    dragY_         = y;
    hoveredSquare_ = origin;
    interaction_   = Interaction::DraggingPiece;
}

void ChessScene::handleDragMove(int16_t x, int16_t y) {
    if (interaction_ != Interaction::DraggingPiece) return;

    dragX_         = x;
    dragY_         = y;
    hoveredSquare_ = squareAt(x, y);
}

void ChessScene::handleDrop(int16_t x, int16_t y) {
    if (interaction_ != Interaction::DraggingPiece) return;

    const chess::Square origin = dragOrigin_;
    const chess::Square target = squareAt(x, y);

    // Released off the board, or on a square this piece cannot reach: the piece
    // simply goes back where it came from.
    if (target == chess::kNoSquare || dragMoveTo(target) == nullptr) {
        cancelDrag();
        return;
    }

    if (game_.needsPromotionChoice(origin, target)) {
        cancelDrag();
        promotionFrom_ = origin;
        promotionTo_   = target;
        promotion_.open();
        return;
    }

    if (game_.tryMove(origin, target)) emitMoveFeedback();
    cancelDrag();
}

void ChessScene::handleTap(int16_t x, int16_t y) {
    if (promotion_.isOpen()) {
        handlePromotionTap(x, y);
        return;
    }

    // Taps on the board do nothing: pieces move by dragging.
    if (y < kHudY) return;

    if (pointInRect(x, y, kNewGameX, kButtonY, kButtonWidth, kButtonHeight)) {
        startNewGame();
        return;
    }

    if (pointInRect(x, y, kResignX, kButtonY, kButtonWidth, kButtonHeight) &&
        !chess::isGameOver(game_.status())) {
        game_.resign(game_.sideToMove());
        cancelDrag();
    }
}

void ChessScene::handlePromotionTap(int16_t x, int16_t y) {
    // A tap on no cell cancels inside the picker: the move is not committed
    // until a piece is chosen, so there is nothing to undo.
    chess::PieceType piece = chess::PieceType::None;
    if (promotion_.tap(x, y, piece) == PromotionPicker::TapResult::Chosen &&
        game_.tryMove(promotionFrom_, promotionTo_, piece)) {
        emitMoveFeedback();
    }
    cancelDrag();
}

void ChessScene::emitMoveFeedback() {
    const chess::Move* last = game_.lastMove();
    const bool captured = (last != nullptr) && ((last->flags & chess::kCapture) != 0);

    // The shake runs even on a mating capture. Suppressing the capture *sound*
    // under mate keeps the two cues from muddying each other in one channel;
    // the shake is a different channel and takes nothing away from the mate.
    if (captured) emitCaptureImpact(*last);

    // Mate speaks for itself: the descending figure would only be muddied by a
    // capture thud underneath it.
    if (game_.status() == chess::GameStatus::Checkmate) {
        playSfx(SfxId::Checkmate);
        return;
    }

    playSfx(captured ? SfxId::Capture : SfxId::Move);

    // Layered on top of the move, not instead of it: the player needs to hear
    // both what happened and that it left them in check.
    if (game_.status() == chess::GameStatus::Check) playSfx(SfxId::Check);
}

void ChessScene::emitCaptureImpact(const chess::Move& move) {
    // En passant is the one capture whose victim is not standing on the
    // destination square: the taken pawn sits on the destination file, on the
    // rank the capturing pawn started from.
    const chess::Square victim =
        (move.flags & chess::kEnPassant)
            ? chess::square(chess::fileOf(move.to), chess::rankOf(move.from))
            : move.to;

    // The move already flipped the turn, so whoever is to move now is the side
    // that just lost a piece, and the newest entry in their tray is that piece.
    const chess::Side loser = game_.sideToMove();
    const uint8_t taken = game_.capturedCount(loser);
    const chess::PieceType type =
        (taken > 0) ? game_.capturedPiece(loser, taken - 1) : chess::PieceType::Pawn;

    int x = 0, y = 0;
    cellOrigin(victim, x, y);
    effects_.triggerCapture(x + kCellSize / 2, y + kCellSize / 2, type);
}

// --- Geometry ----------------------------------------------------------------

chess::Square ChessScene::squareAt(int16_t x, int16_t y) const {
    if (x < kBoardX || x >= kBoardX + kBoardPixels) return chess::kNoSquare;
    if (y < kBoardY || y >= kBoardY + kBoardPixels) return chess::kNoSquare;

    const int file = (x - kBoardX) / kCellSize;
    const int row  = (y - kBoardY) / kCellSize;

    // White plays from the bottom, so screen row 0 is rank 8.
    return chess::square(file, (kBoardCells - 1) - row);
}

void ChessScene::cellOrigin(chess::Square sq, int& x, int& y) const {
    x = kBoardX + chess::fileOf(sq) * kCellSize;
    y = kBoardY + ((kBoardCells - 1) - chess::rankOf(sq)) * kCellSize;
}

const chess::Move* ChessScene::dragMoveTo(chess::Square target) const {
    for (uint8_t i = 0; i < dragMoveCount_; ++i) {
        if (dragMoves_[i].to == target) return &dragMoves_[i];
    }
    return nullptr;
}

// --- Rendering ---------------------------------------------------------------

void ChessScene::draw(gfx::Renderer& renderer) {
    // Everything on the board rides the capture shake. The HUD and the modal
    // picker are drawn outside the pass on purpose: a jolt that moved the
    // buttons would make them harder to hit, which is the opposite of feedback.
    effects_.beginBoardPass(renderer);

    drawBoard(renderer);
    drawHighlights(renderer);
    drawPieces(renderer);
    // Above the pieces so the burst reads as debris coming off the square, and
    // below the held piece so a dragged piece is never obscured by it.
    effects_.drawParticles(renderer);
    drawHeldPiece(renderer);

    effects_.endBoardPass(renderer);

    drawHud(renderer);

    if (promotion_.isOpen()) drawPromotionPicker(renderer);

    Scene::draw(renderer);
}

void ChessScene::drawBoard(gfx::Renderer& renderer) const {
    // The 64 squares are the only thing in this demo that is a background. The
    // pieces are sprites, and the highlights, the HUD and the promotion picker
    // are all foreground, so they stay on the sprite palette.
    //
    // Without this scope the background palette would be loaded and never read:
    // drawFilledRectangle falls back to PaletteContext::Sprite when no context
    // is set, which is what the demo did before it configured palettes at all.
    const PaletteScope background(renderer, gfx::PaletteContext::Background);

    for (int rank = 0; rank < kBoardCells; ++rank) {
        for (int file = 0; file < kBoardCells; ++file) {
            int x = 0, y = 0;
            cellOrigin(chess::square(file, rank), x, y);

            // a1 is a dark square, and a1 is file 0 rank 0.
            const gfx::Color color = ((file + rank) & 1) ? kLightSquare : kDarkSquare;
            renderer.drawFilledRectangle(x, y, kCellSize, kCellSize, color);
        }
    }
}

void ChessScene::drawHighlights(gfx::Renderer& renderer) const {
    int x = 0, y = 0;

    // Where the last move came from and went to, so the other player can see it.
    if (const chess::Move* last = game_.lastMove()) {
        cellOrigin(last->from, x, y);
        renderer.drawRectangle(x, y, kCellSize, kCellSize, kLastMoveMarker);
        cellOrigin(last->to, x, y);
        renderer.drawRectangle(x, y, kCellSize, kCellSize, kLastMoveMarker);
    }

    const chess::Square checkedKing = game_.checkedKingSquare();
    if (checkedKing != chess::kNoSquare) {
        cellOrigin(checkedKing, x, y);
        renderer.drawRectangle(x, y, kCellSize, kCellSize, kCheckMarker);
        renderer.drawRectangle(x + 1, y + 1, kCellSize - 2, kCellSize - 2, kCheckMarker);
    }

    if (interaction_ != Interaction::DraggingPiece) return;

    // The square the piece was lifted from stays marked while it is in hand.
    cellOrigin(dragOrigin_, x, y);
    renderer.drawRectangle(x, y, kCellSize, kCellSize, kSelectedSquare);

    for (uint8_t i = 0; i < dragMoveCount_; ++i) {
        const chess::Move& move = dragMoves_[i];
        cellOrigin(move.to, x, y);

        const int centreX = x + kCellSize / 2;
        const int centreY = y + kCellSize / 2;

        if (move.flags & chess::kCapture) {
            // A ring, so the piece being taken stays visible inside it.
            renderer.drawCircle(centreX, centreY, kCellSize / 2 - 2, kCaptureMarker);
            renderer.drawCircle(centreX, centreY, kCellSize / 2 - 3, kCaptureMarker);
        } else {
            renderer.drawFilledCircle(centreX, centreY, 4, kTargetMarker);
        }
    }

    // The square under the finger, filled in so aiming does not depend on
    // seeing the piece itself - which the finger is covering.
    if (hoveredSquare_ == chess::kNoSquare || hoveredSquare_ == dragOrigin_) return;

    cellOrigin(hoveredSquare_, x, y);
    const gfx::Color hover =
        (dragMoveTo(hoveredSquare_) != nullptr) ? kSelectedSquare : kCheckMarker;
    renderer.drawRectangle(x, y, kCellSize, kCellSize, hover);
    renderer.drawRectangle(x + 1, y + 1, kCellSize - 2, kCellSize - 2, hover);
}

void ChessScene::drawPieces(gfx::Renderer& renderer) const {
    const chess::Position& position = game_.position();
    const bool dragging = (interaction_ == Interaction::DraggingPiece);

    for (chess::Square sq = 0; sq < 64; ++sq) {
        // The held piece is drawn at the finger instead, not on its old square.
        if (dragging && sq == dragOrigin_) continue;

        const gfx::Sprite4bpp* sprite = spriteFor(position.squares[sq]);
        if (sprite == nullptr) continue;

        int x = 0, y = 0;
        cellOrigin(sq, x, y);
        drawSprite4bpp(renderer, *sprite, x + kPieceInset, y + kPieceInset);
    }
}

void ChessScene::drawHeldPiece(gfx::Renderer& renderer) const {
    if (interaction_ != Interaction::DraggingPiece) return;

    const gfx::Sprite4bpp* sprite = spriteFor(game_.position().squares[dragOrigin_]);
    if (sprite == nullptr) return;

    // Lifted above the finger so the piece stays visible while it is carried.
    // Aiming still goes by the hovered square, which is under the finger.
    const int x = clamp(dragX_ - kPieceSize / 2, 0, kBoardPixels - kPieceSize);
    const int y = clamp(dragY_ - kPieceSize / 2 - kDragLift, 0, kBoardPixels - kPieceSize);

    drawSprite4bpp(renderer, *sprite, x, y);
}

void ChessScene::drawHud(gfx::Renderer& renderer) const {
    renderer.drawFilledRectangle(0, kHudY, kBoardPixels, kHudHeight, kHudBackground);
    renderer.drawLine(0, kHudY, kBoardPixels - 1, kHudY, kHudDivider);

    renderer.drawTextCentered(statusText(), kStatusTextY, kHudText, 2);

    drawCapturedTray(renderer, chess::Side::Black);  // taken by White
    drawCapturedTray(renderer, chess::Side::White);  // taken by Black

    drawButton(renderer, kNewGameX, "NEW GAME", true);
    drawButton(renderer, kResignX, "RESIGN", !chess::isGameOver(game_.status()));
}

void ChessScene::drawCapturedTray(gfx::Renderer& renderer, chess::Side loser) const {
    const int rowY  = kCapturedRowY[static_cast<uint8_t>(loser)];
    const uint8_t n = game_.capturedCount(loser);

    for (uint8_t i = 0; i < n && i < kCapturedMaxShown; ++i) {
        const gfx::Sprite4bpp* icon = iconFor(game_.capturedPiece(loser, i), loser);
        if (icon == nullptr) continue;

        drawSprite4bpp(renderer, *icon, kCapturedOriginX + i * kCapturedStride, rowY);
    }
}

void ChessScene::drawButton(gfx::Renderer& renderer, int x,
                            const char* label, bool enabled) const {
    const gfx::Color fill = enabled ? kButtonFill : kHudBackground;

    renderer.drawFilledRectangle(x, kButtonY, kButtonWidth, kButtonHeight, fill);
    renderer.drawRectangle(x, kButtonY, kButtonWidth, kButtonHeight, kPanelBorder);
    renderer.drawText(label, centeredTextX(x, kButtonWidth, label, 1),
                      kButtonY + (kButtonHeight - 8) / 2,
                      enabled ? kButtonText : kHudDivider, 1);
}

void ChessScene::drawPromotionPicker(gfx::Renderer& renderer) const {
    renderer.drawFilledRectangle(kPromoX, kPromoY, kPromoWidth, kPromoHeight, kPanelFill);
    renderer.drawRectangle(kPromoX, kPromoY, kPromoWidth, kPromoHeight, kPanelBorder);
    renderer.drawText("PROMOTE TO", centeredTextX(kPromoX, kPromoWidth, "PROMOTE TO", 1),
                      kPromoY + 5, kHudText, 1);

    const chess::Side side = game_.sideToMove();

    for (uint8_t choice = 0; choice < promotion_.choiceCount(); ++choice) {
        int cellX = 0, cellY = 0, cellW = 0, cellH = 0;
        PromotionPicker::cellRect(choice, cellX, cellY, cellW, cellH);

        renderer.drawRectangle(cellX, cellY, cellW, cellH, kPanelBorder);

        const gfx::Sprite4bpp* sprite =
            spriteFor(chess::makePiece(promotion_.pieceAt(choice), side));
        if (sprite == nullptr) continue;

        const int inset = (cellW - kPieceSize) / 2;
        drawSprite4bpp(renderer, *sprite, cellX + inset, cellY + inset);
    }
}

const char* ChessScene::statusText() const {
    const bool whiteWins = game_.winner() == chess::Side::White;

    switch (game_.status()) {
        case chess::GameStatus::Playing:
            return game_.sideToMove() == chess::Side::White ? "WHITE TO MOVE"
                                                            : "BLACK TO MOVE";
        case chess::GameStatus::Check:
            return game_.sideToMove() == chess::Side::White ? "WHITE IN CHECK"
                                                            : "BLACK IN CHECK";
        case chess::GameStatus::Checkmate:
            return whiteWins ? "WHITE WINS" : "BLACK WINS";
        case chess::GameStatus::Resigned:
            return whiteWins ? "BLACK RESIGNED" : "WHITE RESIGNED";
        case chess::GameStatus::Stalemate:
            return "STALEMATE";
        case chess::GameStatus::DrawFiftyMove:
            return "DRAW: 50 MOVES";
        case chess::GameStatus::DrawRepetition:
            return "DRAW: REPETITION";
        case chess::GameStatus::DrawInsufficientMaterial:
            return "DRAW: MATERIAL";
    }
    return "";
}

}  // namespace chessdemo
