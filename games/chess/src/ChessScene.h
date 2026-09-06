/*
 * ChessScene.h - Presentation and touch interaction for the chess demo.
 *
 * The scene owns no rules. It turns touch gestures into ChessGame calls and
 * draws whatever ChessGame reports, which is why the rules can be unit-tested
 * on the host without a display anywhere in the picture.
 */
#pragma once

#include <core/Scene.h>
#include <graphics/Renderer.h>
#include <input/TouchEvent.h>

#include "chess/ChessGame.h"
#include "effects/ChessEffects.h"

namespace chessdemo {

/**
 * @class ChessScene
 * @brief Draws the board and turns touch gestures into moves.
 *
 * Moving a piece is a drag: press it, carry it, release it on a square. The
 * engine's touch state machine is what makes that safe to implement, because a
 * release that followed a drag emits only DragEnd - no TouchUp and no Click -
 * so a piece dropped near the bottom of the board can never also press a HUD
 * button.
 */
class ChessScene : public pixelroot32::core::Scene {
public:
    /** @brief Start a new game and clear any interaction state. */
    void init() override;

    /**
     * @brief Per-frame update.
     * @param deltaTime Milliseconds since the previous frame.
     */
    void update(unsigned long deltaTime) override;

    /**
     * @brief Draw the board, the pieces and the HUD.
     * @param renderer Renderer to draw through.
     */
    void draw(pixelroot32::graphics::Renderer& renderer) override;

    /**
     * @brief Route a touch gesture into the game.
     * @param event Gesture reported by the engine's touch pipeline.
     */
    void onUnconsumedTouchEvent(const pixelroot32::input::TouchEvent& event) override;

private:
    /**
     * @enum Interaction
     * @brief What the scene is currently in the middle of.
     */
    enum class Interaction : uint8_t {
        Idle,              ///< Nothing held; a press can pick a piece up.
        DraggingPiece,     ///< A piece is in hand and follows the finger.
        ChoosingPromotion  ///< The picker is open; only it accepts taps.
    };

    /** @brief Largest number of legal destinations any single piece can have. */
    static constexpr uint8_t kMaxMovesFromSquare = 32;

    /**
     * @brief Handle a press: pick up whatever piece is under it.
     * @param x Screen X of the press.
     * @param y Screen Y of the press.
     */
    void handlePress(int16_t x, int16_t y);

    /**
     * @brief Handle the finger moving while a piece is held.
     * @param x Current screen X.
     * @param y Current screen Y.
     */
    void handleDragMove(int16_t x, int16_t y);

    /**
     * @brief Handle a release: play the move, or put the piece back.
     * @param x Screen X of the release.
     * @param y Screen Y of the release.
     */
    void handleDrop(int16_t x, int16_t y);

    /**
     * @brief Handle a tap, which only the HUD and the picker respond to.
     * @param x Screen X of the tap.
     * @param y Screen Y of the tap.
     */
    void handleTap(int16_t x, int16_t y);

    /**
     * @brief Handle a tap while the promotion picker is open.
     * @param x Screen X of the tap.
     * @param y Screen Y of the tap.
     */
    void handlePromotionTap(int16_t x, int16_t y);

    /**
     * @brief Lift a piece and cache where it may go.
     * @param origin Square the piece is on.
     * @param x Screen X of the finger.
     * @param y Screen Y of the finger.
     */
    void beginDrag(chess::Square origin, int16_t x, int16_t y);

    /** @brief Drop everything held and close the picker. */
    void cancelDrag();

    /** @brief Reset the game to the starting position. */
    void startNewGame();

    /**
     * @brief Report the move that was just made, in sound and on screen.
     *
     * Called after the game state has already advanced, so it reads the outcome
     * rather than predicting it.
     */
    void emitMoveFeedback();

    /**
     * @brief Shake the board and throw debris for a capture.
     * @param move The capturing move, already played.
     */
    void emitCaptureImpact(const chess::Move& move);

    /**
     * @brief Board square under a screen point.
     * @param x Screen X.
     * @param y Screen Y.
     * @return The square, or chess::kNoSquare when the point is off the board.
     */
    chess::Square squareAt(int16_t x, int16_t y) const;

    /**
     * @brief Top-left pixel of a square.
     * @param sq Square to locate.
     * @param x Receives the X coordinate.
     * @param y Receives the Y coordinate.
     */
    void cellOrigin(chess::Square sq, int& x, int& y) const;

    /**
     * @brief Look up a destination among the held piece's legal moves.
     * @param target Square being aimed at.
     * @return The matching move, or nullptr when the piece cannot go there.
     */
    const chess::Move* dragMoveTo(chess::Square target) const;

    /**
     * @brief Draw the 64 squares.
     * @param renderer Renderer to draw through.
     */
    void drawBoard(pixelroot32::graphics::Renderer& renderer) const;

    /**
     * @brief Draw the last move, check, and everything the held piece can do.
     * @param renderer Renderer to draw through.
     */
    void drawHighlights(pixelroot32::graphics::Renderer& renderer) const;

    /**
     * @brief Draw every piece standing on the board.
     * @param renderer Renderer to draw through.
     */
    void drawPieces(pixelroot32::graphics::Renderer& renderer) const;

    /**
     * @brief Draw the piece currently in hand, at the finger.
     * @param renderer Renderer to draw through.
     */
    void drawHeldPiece(pixelroot32::graphics::Renderer& renderer) const;

    /**
     * @brief Draw the status line, capture trays and buttons.
     * @param renderer Renderer to draw through.
     */
    void drawHud(pixelroot32::graphics::Renderer& renderer) const;

    /**
     * @brief Draw one side's captured pieces.
     * @param renderer Renderer to draw through.
     * @param loser Side whose losses are shown.
     */
    void drawCapturedTray(pixelroot32::graphics::Renderer& renderer, chess::Side loser) const;

    /**
     * @brief Draw one HUD button.
     * @param renderer Renderer to draw through.
     * @param x Left edge of the button.
     * @param label Text to centre inside it.
     * @param enabled Whether it is drawn as pressable.
     */
    void drawButton(pixelroot32::graphics::Renderer& renderer,
                    int x, const char* label, bool enabled) const;

    /**
     * @brief Draw the four-choice promotion panel over the board.
     * @param renderer Renderer to draw through.
     */
    void drawPromotionPicker(pixelroot32::graphics::Renderer& renderer) const;

    /**
     * @brief Status line for the HUD.
     * @return A string literal describing whose turn it is, or how it ended.
     */
    const char* statusText() const;

    chess::ChessGame game_;
    ChessEffects     effects_;

    Interaction   interaction_ = Interaction::Idle;
    chess::Square dragOrigin_  = chess::kNoSquare;

    /** Destinations for the held piece, cached so draw() stays read-only. */
    chess::Move dragMoves_[kMaxMovesFromSquare]{};
    uint8_t     dragMoveCount_ = 0;

    int16_t       dragX_         = 0;                ///< Finger X while dragging.
    int16_t       dragY_         = 0;                ///< Finger Y while dragging.
    chess::Square hoveredSquare_ = chess::kNoSquare; ///< Square under the finger.

    chess::Square promotionFrom_ = chess::kNoSquare; ///< Move waiting on a choice.
    chess::Square promotionTo_   = chess::kNoSquare;
};

}  // namespace chessdemo
