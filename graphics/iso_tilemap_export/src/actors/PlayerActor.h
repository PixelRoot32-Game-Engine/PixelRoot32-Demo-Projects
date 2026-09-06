#pragma once

#include <cstdint>

#include <gameplay/GridMotion.h>
#include <graphics/Renderer.h>

#include "assets/sprites/player_sprites.h"

namespace iso_tilemap_export {

/// Button indices, in the order every platform header passes them to
/// InputConfig. `isButtonDown` takes an INDEX, not the pin or scancode the
/// platform header names -- those are two different numbering schemes and
/// mixing them reads the wrong button silently.
inline constexpr std::uint8_t BTN_UP    = 0;
inline constexpr std::uint8_t BTN_DOWN  = 1;
inline constexpr std::uint8_t BTN_LEFT  = 2;
inline constexpr std::uint8_t BTN_RIGHT = 3;

/**
 * An animated actor that walks the exported map, cell to cell.
 *
 * The three things a moving actor needs on an isometric map are already
 * answered by data the export ships, and none of them require the actor to
 * know it is isometric:
 *
 *   - WHERE it may go: `ground.indices[cell]`, where 0 is the empty-tile
 *     sentinel. Land is exactly the non-zero cells, so the walkable set is
 *     the layer itself rather than a second table to keep in sync.
 *   - WHERE it is between cells: `gameplay::GridMotion`, which is
 *     projection-blind integer state.
 *   - WHERE that lands on screen: one call to `gameplay::interpolatedWorld`
 *     with the exported `ISO_PROJECTION`.
 *
 * The projection enters in that last call and nowhere else. Hand the same
 * motion a `GridSpec` and this is a top-down game with no other edit, which
 * is the reason navigation is kept out of the view -- the same argument
 * examples/iso_dungeon makes with its HeroActor.
 */
class PlayerActor {
public:
    /// Places the player at rest on a cell. Call AFTER the scene's `init()`:
    /// the walkability test reads the exported tilemap, and `init()` is what
    /// points that tilemap at its data.
    void spawn(int cellX, int cellY);

    void update(unsigned long deltaTime);

    /// Takes no camera, and that absence is the point: the scene's `Camera2D`
    /// has already pushed its offset into the renderer, and every draw -- this
    /// sprite included -- is translated by it on the way out. There is no
    /// second copy of the camera here that could disagree with the one the
    /// tilemaps were drawn under.
    void draw(pixelroot32::graphics::Renderer& renderer) const;

    /// The logical cell. In flight this still names the cell being LEFT,
    /// which is what a gameplay rule reading the player's position wants.
    int cellX() const { return motion_.cellX; }
    int cellY() const { return motion_.cellY; }

    /// Projection-space position of the cell centre the player currently
    /// stands on or travels through -- before the renderer's display offset,
    /// which is applied at draw time and is not this actor's business. This is
    /// what the scene's camera centres itself on.
    int screenX() const { return screenX_; }
    int screenY() const { return screenY_; }

private:
    /// One fixed-clock movement step: finish a step in flight, or start one.
    void logicStep();

    /// Recomputes the screen position from the motion state.
    void updateProjectedPosition();

    /// True when the ground layer has a tile on that cell. Out of bounds is
    /// false, so the map's edge holds without a ring of blockers around it.
    static bool isWalkable(int cellX, int cellY);

    pixelroot32::gameplay::GridMotion motion_;

    /// Which block of the export to draw from. Set when a step BEGINS and kept
    /// afterwards, so an actor at rest keeps facing the way it last walked
    /// rather than snapping back to a default.
    player_sprites::Direction facing_ = player_sprites::Direction::Down;

    /// Milliseconds carried toward the next idle frame. Only the idle loop
    /// needs a clock of its own: the run loop reads `motion_.progress`, so it
    /// is a function of how far through the cell the actor is and cannot drift
    /// out of step with the movement it is supposed to be showing.
    unsigned long idleAccumulator_ = 0;
    std::uint8_t  idleFrame_ = 0;

    /// Which HALF of the run cycle the current cell is showing, 0 or 1. A cycle
    /// is two strides and one cell is one stride, so this is the only state the
    /// run animation carries between cells -- it flips on arrival and is read
    /// straight back out at draw time.
    std::uint8_t  runStride_ = 0;

    /// Milliseconds carried toward the next logic step. Movement runs on a
    /// fixed clock rather than on deltaTime so a step always takes the same
    /// number of frames, and the player cannot come to rest between cells.
    unsigned long logicAccumulator_ = 0;

    int screenX_ = 0;
    int screenY_ = 0;
};

}  // namespace iso_tilemap_export
