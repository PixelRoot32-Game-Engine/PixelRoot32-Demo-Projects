#pragma once

#include <core/Scene.h>
#include <graphics/Camera2D.h>
#include <graphics/ProjectedMapBounds.h>
#include <graphics/Renderer.h>

#include "actors/PlayerActor.h"

namespace iso_tilemap_export {

/**
 * Draws the exported scene, plus one actor walking it.
 *
 * The map is the Tilemap Editor's output, byte for byte -- nothing in
 * src/assets/ is hand-written and nothing here corrects it. What the example
 * adds is the smallest possible CONSUMER of that output: a marker that has to
 * agree with the map about where the cells are. Anything less and the export
 * is only being looked at; this is the first thing that has to navigate it.
 *
 * The window is 240x240 and the drawn map is bigger than that on both axes, so
 * the view follows the player and stops at the map's edges. How big the drawn
 * map is, is not something this file gets to assert from the cell counts:
 * under an isometric basis a step along either cell axis moves BOTH screen
 * axes, and the art overhangs its cell. `expandProjectedMapBounds` derives it
 * from the export instead, once in `init()`, into `world_`. For the export
 * that ships here -- 36x29 cells on a 32x16 stride, 32x32 art anchored at
 * foot row 24 -- that comes out 1040x536 px, and the derivation is worth
 * writing down because neither number is a cell count times a cell size: the
 * four projected cell ANCHORS span x [16, 1024] and y [24, 528], and the
 * tileset's worst overhang around an anchor widens that by 16 px left, 16
 * right, 24 up and 8 down, giving the half-open box x [0, 1040) by y [0, 536).
 * The 8 px of downward overhang is what index 1's 32 px art hangs below its
 * foot row; tile index 0, the exporter's empty sentinel, is a full-height
 * tile with foot 0 and would contribute 32 instead -- `expandProjectedMapBounds`
 * skips it precisely so the camera is not allowed to scroll into 24 px of
 * space nothing draws. Re-export a different map and these numbers change
 * underneath this comment without anyone having to notice, which is the reason
 * they are derived at runtime and not written down in code.
 *
 * Scrolling still costs the exported data nothing. `Camera2D::apply` pushes
 * the camera into the renderer as a display offset and every primitive drawn
 * afterwards honours it, so `ISO_PROJECTION` is used exactly as the editor
 * wrote it and no call site has to carry the camera by hand.
 */
class IsoTilemapExportScene : public pixelroot32::core::Scene {
public:
    void init() override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;
    void update(unsigned long deltaTime) override;

private:
    /// Held by value and drawn by hand rather than registered with
    /// `addEntity`, because paint order here is not a matter of layers: the
    /// player must land on top of the tilemap, and the scene's own draw()
    /// is what draws it. An entity would be painted before it and disappear
    /// under the map.
    PlayerActor player_;

    /// Screen-space extent of everything the ground layer draws, accumulated
    /// in init() and then read every frame. Default-constructed on purpose:
    /// the `valid == false` a fresh `ScreenBounds` carries is precisely what
    /// tells the `expandProjectedMapBounds` call to SEED the box instead of
    /// widening one that was never measured.
    pixelroot32::graphics::ScreenBounds world_{};

    /// Re-aimed every frame from `world_` and the player. Held by the scene
    /// rather than the player because it is a VIEW decision: the player has no
    /// opinion about what is on screen.
    ///
    /// The 1x1 viewport is a placeholder, not a size. `Camera2D` has no
    /// default constructor, and at construction time the scene has never been
    /// handed a Renderer, so there is no display size to give it yet. `draw()`
    /// replaces it via `setViewportSize` with the logical size of the renderer
    /// it is about to draw into -- which is deliberately the only place that
    /// size is read. A copy of it cached anywhere else is a copy that can
    /// drift out of step with the surface actually being painted.
    pixelroot32::graphics::Camera2D camera_{1, 1};

/// register sprite and tilemap palette slots. Call once, before the first draw -- the engine stores
/// the pointer rather than copying the table.
void registerPalette();

};

}  // namespace iso_tilemap_export
