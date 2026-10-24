#pragma once
#include "physics/StaticActor.h"
#include "graphics/Renderer.h"
#include "rules/ShieldMask.h"

namespace spaceinvaders {

/**
 * @class BunkerActor
 * @brief One destructible shield: a 22x16 pixel mask, not a health bar (S10).
 *
 * Architecture Pattern:
 * Demonstrates StaticActor usage - immovable objects that participate in
 * collision detection but have zero physics response. The actor owns no pixel
 * data of its own: the scene owns a single rules::ShieldField for all four
 * shields, and this actor is a view over one index of it. Rebuilding a wave
 * therefore costs one reset() on the field instead of four actor rebuilds.
 *
 * Damage System:
 * A hit erodes a 3x3 blast centred on the impact cell (D6). Erosion is the
 * only damage path: there is no health counter and no height shrink, because
 * the arcade wall is a shape that loses pixels rather than a bar that loses
 * length. A shield with no bits left is gone.
 *
 * Collision Strategy:
 * The AABB body is a broad-phase box only; the pixel mask decides the hit.
 * The scene sweeps each shot's segment against the box, then asks the mask
 * which cell the shot entered, so a shot through an existing hole keeps
 * flying instead of stopping at a box the wall no longer fills.
 *
 * Optimization Note:
 * As a StaticActor, position is never integrated by physics system,
 * saving computation compared to Rigid/Kinematic actors.
 */
class BunkerActor : public pixelroot32::physics::StaticActor {
public:
    /**
     * @brief Binds the actor to one shield of the scene's mask set.
     * @param position World position of the mask's top-left pixel.
     * @param field The scene-owned shield set that stores this shield's bits.
     * @param shieldIndex Shield index, 0..rules::ShieldCount-1.
     */
    BunkerActor(pixelroot32::math::Vector2 position, rules::ShieldField& field, int shieldIndex);

    /**
     * @brief Static actors require no per-frame updates.
     *
     * Design Note: Override exists because base Actor class declares
     * update() as pure virtual. Static geometry doesn't change state
     * without external intervention (erosion).
     * @param deltaTime Frame delta in ms. Unused.
     */
    void update(unsigned long deltaTime) override;

    /**
     * @brief Renders the mask as one filled rectangle per run of set bits.
     *
     * D6: drawBitmap is not used because the SDL2 and TFT_eSPI backends read
     * its bit order differently, so the same bytes would mirror between the
     * two panels. Rows are emitted from the field, so an eroded row simply
     * draws nothing where the bits are gone.
     *
     * @param renderer Renderer to draw into.
     */
    void draw(pixelroot32::graphics::Renderer& renderer) override;

    /**
     * @brief Collision callback - detection only.
     *
     * Damage belongs to the scene's swept mask path, not to a contact event:
     * the box says "somewhere in this wall", the mask says where.
     * @param other The actor this one touched.
     */
    void onCollision(pixelroot32::core::Actor* other) override;

    /**
     * @brief Reports whether the mask has lost every pixel.
     * @return true when the shield's field rows are all empty.
     */
    bool isDestroyed() const;

    /**
     * @brief Tests a world pixel against this shield.
     *
     * The field stores shield-local pixels, so both coordinates are converted
     * before the lookup. A point outside the 22x16 mask is not part of the
     * wall and is rejected here rather than relying on the caller's bounds.
     *
     * @param worldX Screen x of the pixel.
     * @param worldY Screen y of the pixel.
     * @return true when the mask still holds that pixel.
     */
    bool solidAtWorld(int worldX, int worldY) const;

    /**
     * @brief Carves the first solid pixel a shot crosses, in world coordinates.
     *
     * Converts the swept segment to shield-local coordinates and hands it to
     * rules::carveSwept(), so the blast lands on the cell the shot entered.
     * A segment that only crosses cells this shield has already lost reports
     * no hit, which is what lets a shot fly on through its own hole.
     *
     * @param fromWorldX Shot column on the previous tick.
     * @param fromWorldY Shot row on the previous tick.
     * @param toWorldX Current shot column.
     * @param toWorldY Current shot row.
     * @return true when a solid pixel was hit and the 3x3 blast applied.
     */
    bool carveSweptWorld(int fromWorldX, int fromWorldY, int toWorldX, int toWorldY);

private:
    rules::ShieldField& field;  ///< The scene's shield set; the actor holds no pixels.
    int shieldIndex;            ///< Which shield of that set this actor draws.
};

}
