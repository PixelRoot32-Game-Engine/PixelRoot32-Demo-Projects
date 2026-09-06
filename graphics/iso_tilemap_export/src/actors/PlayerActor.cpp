#include "PlayerActor.h"

#include <core/Engine.h>
#include <math/Vector2.h>

#include "assets/tilemap/isometric_scene_main_scene.h"

namespace pr32 = pixelroot32;
extern pr32::core::Engine engine;

namespace iso_tilemap_export {

namespace gfx = pr32::graphics;
namespace gameplay = pr32::gameplay;
namespace scene_assets = isometric_scene::main_scene;

namespace {

/// Frames one cell of travel takes, and the fixed clock that advances them.
/// 16 steps at 16 ms is a little over a quarter second per tile -- fast
/// enough to feel responsive, slow enough to watch the interpolation work.
constexpr int kStepsPerCell = 16;
constexpr unsigned long kLogicStepMs = 16;

/// How long one idle frame is held. Four frames at 160 ms is a ~0.64 s loop --
/// slow enough to read as breathing rather than as a second walk cycle.
constexpr unsigned long kIdleFrameMs = 160;

/// Frames of the run cycle spent crossing ONE cell.
///
/// Half of them, not all eight, and that is the fix for legs that churned
/// faster than the ground went by. An eight-frame cycle is two strides -- left
/// foot, right foot -- so spending the whole thing on one 32x16 tile asks the
/// actor to take two full strides to cover one cell, which is what reads as
/// running on the spot. One stride per cell instead, and the cycle spans two.
constexpr int kRunFramesPerCell = player_sprites::kRunFrameCount / 2;

static_assert(player_sprites::kRunFrameCount % 2 == 0,
              "A run cycle is two strides, so it needs an even frame count to "
              "be split across two cells.");
static_assert(kStepsPerCell % kRunFramesPerCell == 0,
              "The run cycle is indexed straight off motion progress, so the "
              "frames spent on a cell have to divide the steps that cell takes "
              "evenly. Otherwise the last frame of each half is held for a "
              "different number of ticks than the rest and the run visibly "
              "limps.");

/// Which block of the export a (dx, dy) cell step is drawn from.
///
/// Read as CELL axes, not screen ones. Under ISO_PROJECTION a step along -cellX
/// travels up-left on screen, not left -- the four blocks are named for the
/// buttons that reach them, which is the same naming the export uses and the
/// same one the four fixed-priority branches below already commit to.
player_sprites::Direction facingFor(int dx, int dy) {
    if (dy < 0) {
        return player_sprites::Direction::Up;
    }
    if (dy > 0) {
        return player_sprites::Direction::Down;
    }
    if (dx < 0) {
        return player_sprites::Direction::Left;
    }
    return player_sprites::Direction::Right;
}

}  // namespace

void PlayerActor::spawn(int cellX, int cellY) {
    gameplay::placeAt(motion_, cellX, cellY);
    logicAccumulator_ = 0;
    idleAccumulator_ = 0;
    idleFrame_ = 0;
    runStride_ = 0;
    updateProjectedPosition();
}

void PlayerActor::update(unsigned long deltaTime) {
    logicAccumulator_ += deltaTime;
    while (logicAccumulator_ >= kLogicStepMs) {
        logicAccumulator_ -= kLogicStepMs;
        logicStep();
    }

    // Advanced even while the actor is walking, and deliberately so: the idle
    // loop is not the animation on screen then, so where its clock stands does
    // not show. Freezing it would only mean every arrival restarts the loop
    // from the same frame, which reads as a twitch on the frame movement ends.
    idleAccumulator_ += deltaTime;
    while (idleAccumulator_ >= kIdleFrameMs) {
        idleAccumulator_ -= kIdleFrameMs;
        idleFrame_ = (idleFrame_ + 1) % player_sprites::kIdleFrameCount;
    }

    updateProjectedPosition();
}

void PlayerActor::logicStep() {
    if (gameplay::isMoving(motion_)) {
        // A step is already in flight: finish it and ignore input until it
        // lands. That refusal is what keeps the player always either ON a
        // cell or travelling between two named ones, never anywhere else.
        //
        // The arrival edge is what the run cycle is counted in. One cell is
        // half a cycle, so which half comes next flips here and nowhere else.
        if (gameplay::tickStep(motion_, kStepsPerCell)) {
            runStride_ ^= 1;
        }
        return;
    }

    auto& input = engine.getInputManager();

    // Fixed priority, so exactly one (dx, dy) with |dx| + |dy| == 1 is ever
    // chosen and diagonals are unrepresentable by construction.
    //
    // No remapping is needed for the isometric view, and that is worth being
    // precise about rather than calling it luck. Under ISO_PROJECTION the
    // cell axes project to the four screen DIAGONALS -- +cellX goes
    // down-right, +cellY down-left -- so the four buttons already cover the
    // four directions a player can see. A different basis would have to
    // decide this again.
    int dx = 0;
    int dy = 0;
    if (input.isButtonDown(BTN_UP)) {
        dy = -1;
    } else if (input.isButtonDown(BTN_DOWN)) {
        dy = 1;
    } else if (input.isButtonDown(BTN_LEFT)) {
        dx = -1;
    } else if (input.isButtonDown(BTN_RIGHT)) {
        dx = 1;
    }

    if (dx == 0 && dy == 0) {
        return;
    }

    const int nextX = motion_.cellX + dx;
    const int nextY = motion_.cellY + dy;

    // Facing turns even when the step is refused, and that is the useful
    // behaviour rather than an accident of ordering: holding a direction into
    // an unwalkable cell turns the actor to look at it, which is what tells
    // the player the input was read at all.
    facing_ = facingFor(dx, dy);

    if (isWalkable(nextX, nextY)) {
        gameplay::beginStep(motion_, nextX, nextY);
    }
    // else: stay put. Holding a direction into an empty cell is inert -- not a
    // collision event, not a retry, not an error.
}

bool PlayerActor::isWalkable(int cellX, int cellY) {
    const auto& ground = scene_assets::ground;

    if (ground.indices == nullptr) {
        // spawn() ran before the scene's init(). Refusing every cell is the
        // safe answer: the alternative reads a null pointer.
        return false;
    }
    if (cellX < 0 || cellY < 0 ||
        cellX >= static_cast<int>(ground.width) ||
        cellY >= static_cast<int>(ground.height)) {
        return false;
    }

    // Index 0 is the exporter's empty-tile sentinel, the one drawTileMap
    // skips. A ground cell that holds it is a hole in the map -- nothing to
    // stand on -- so it is exactly the cell that must not be entered.
    const int cell = cellY * static_cast<int>(ground.width) + cellX;
    return ground.indices[cell] != 0;
}

void PlayerActor::updateProjectedPosition() {
    const pr32::math::Vector2 world =
        gameplay::interpolatedWorld(motion_, kStepsPerCell,
                                    scene_assets::ISO_PROJECTION);
    screenX_ = static_cast<int>(world.x);
    screenY_ = static_cast<int>(world.y);
}

void PlayerActor::draw(gfx::Renderer& renderer) const {
    const bool moving = gameplay::isMoving(motion_);

    // The run cycle is read off `motion_.progress` and the stride bit rather
    // than off a clock of its own, and that is the difference between an
    // animation that happens to look like walking and one that IS the walk:
    // four frames over the sixteen steps a cell takes, and which four decided
    // by how many cells have been crossed. The cycle lands on the same frame at
    // the same point of every tile, at any frame rate, with no accumulator to
    // drift out of step with the feet.
    const std::uint8_t runFrameIndex =
        static_cast<std::uint8_t>(runStride_ * kRunFramesPerCell +
                                  motion_.progress * kRunFramesPerCell /
                                      kStepsPerCell);

    // Bitmap, mirroring and foot row arrive together rather than through three
    // lookups: half the directions are drawn as the mirror of another block's
    // art, and each block's feet sit on its own row, so a caller that asked for
    // those separately would have three chances to disagree with itself.
    const player_sprites::Frame frame =
        moving ? player_sprites::runFrame(facing_, runFrameIndex)
               : player_sprites::idleFrame(facing_, idleFrame_);

    // The FOOT ROW sits on the cell's diamond centre, not the frame's middle.
    // Same rule as the tilemap's own `tileFootY`: an actor STANDS ON a cell,
    // where a floor tile FILLS it. A 64x64 frame is mostly transparent padding,
    // so anchoring by the bitmap's bottom edge instead would hang the actor
    // more than a tile's height below the ground it is walking on.
    //
    // Only that offset and the horizontal centring are applied here. The camera
    // does not appear in this expression at all: the scene's `Camera2D::apply`
    // has already written the negated camera position into the renderer's
    // display offset, and `drawSprite` adds that offset like every other draw.
    // So this is a position in PROJECTION space, the same space the tilemaps
    // are placed in, and the two stay aligned because one translation is
    // applied to both -- not because two hand-written ones happen to cancel.
    renderer.drawSprite(*frame.sprite,
                        screenX_ - player_sprites::kSpriteWidth / 2,
                        screenY_ - frame.footY,
                        player_sprites::kPaletteSlot,
                        frame.flipX);
}

}  // namespace iso_tilemap_export
