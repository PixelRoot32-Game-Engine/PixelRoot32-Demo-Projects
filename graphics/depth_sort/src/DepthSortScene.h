/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <core/Entity.h>
#include <core/Scene.h>
#include <graphics/Color.h>
#include <graphics/Renderer.h>
#include <math/Vector2.h>
#include <platforms/EngineConfig.h>

#include <cstdint>

namespace depth_sort {

/**
 * @namespace depth_sort::layout
 * @brief Fixed 128x128 screen layout.
 *
 * The playfield border is drawn at (kFieldX, kFieldY, kFieldW, kFieldH), so the
 * first and last pixels an actor may occupy are one row/column inside it. Those
 * four bounds are what Walker::moveBy() clamps against.
 */
namespace layout {

constexpr int kModeTextY = 2;    ///< SORTED / UNSORTED banner.
constexpr int kHintTextY = 11;   ///< Button hints.

constexpr int kFieldX = 1;
constexpr int kFieldY = 20;
constexpr int kFieldW = 126;
constexpr int kFieldH = 86;

constexpr int kFieldLeft = kFieldX + 1;                 ///< First drawable column.
constexpr int kFieldTop = kFieldY + 1;                  ///< First drawable row.
constexpr int kFieldRight = kFieldX + kFieldW - 2;      ///< Last drawable column.
constexpr int kFieldBottom = kFieldY + kFieldH - 2;     ///< Last drawable row.

constexpr int kLegendTextY = 109;  ///< "BOTTOM Y: HIGH=FRONT".
constexpr int kValueTextY = 118;   ///< The four live bottom-Y readouts.

}  // namespace layout

/**
 * @class Walker
 * @brief One coloured box standing on the floor of a top-down scene.
 *
 * The whole demo hangs on one number: `position.y + height`, the bottom edge.
 * Entity's origin is its TOP-left corner, so `position.y` alone says nothing
 * about how far down the screen an actor is standing — a tall actor and a short
 * one with the same `position.y` have their feet in completely different
 * places. Walker draws a bright bar across its bottom three rows so that edge
 * is something you can point at on screen rather than infer.
 *
 * Position is kept in Q8 fixed point (256 units = 1 pixel) so motion is smooth
 * without a float on the ESP32 path, and mirrored into Entity::position on
 * every move — the comparator reads Entity::position, not these members.
 */
class Walker : public pixelroot32::core::Entity {
public:
    Walker(int x,
           int y,
           int w,
           int h,
           pixelroot32::graphics::Color body,
           pixelroot32::graphics::Color trim,
           char label);

    /// Static by default; PatrolWalker and PlayerWalker override this.
    void update(unsigned long deltaTime) override { (void)deltaTime; }

    void draw(pixelroot32::graphics::Renderer& renderer) override;

    /// @return X of the top-left corner, in whole pixels.
    int pixelX() const { return static_cast<int>(xQ8_ >> kShift); }

    /// @return Y of the top-left corner, in whole pixels.
    int pixelY() const { return static_cast<int>(yQ8_ >> kShift); }

    /// @return The sort key: the row of this actor's feet. Bigger = further
    ///         down the screen = drawn later = in front.
    int bottomY() const { return pixelY() + height; }

    char label() const { return label_[0]; }

    pixelroot32::graphics::Color bodyColor() const { return body_; }

    /// Jumps to a pixel position and re-syncs Entity::position. Used by reset.
    void teleport(int x, int y);

protected:
    static constexpr int kShift = 8;
    static constexpr std::int32_t kOne = 1 << kShift;

    /// Upper bound on one integration step, so a stalled frame (a breakpoint,
    /// the frame right after init) cannot fling an actor across the field.
    static constexpr std::int32_t kMaxStepMs = 100;

    /// Converts a speed in pixels per second into a Q8 step for `deltaTime`.
    static std::int32_t stepQ8(int pixelsPerSecond, unsigned long deltaTime);

    /// Moves by a Q8 delta, clamps to the playfield, mirrors into `position`.
    void moveBy(std::int32_t dxQ8, std::int32_t dyQ8);

    std::int32_t xQ8_;
    std::int32_t yQ8_;

private:
    /// Copies the Q8 position into Entity::position, which is what the depth
    /// comparator reads. Nothing else writes Entity::position.
    void commit();

    pixelroot32::graphics::Color body_;
    pixelroot32::graphics::Color trim_;
    char label_[2];
};

/**
 * @class PatrolWalker
 * @brief A Walker that paces up and down between two rows on its own.
 *
 * Three of these at three speeds keep re-ordering themselves even when nobody
 * touches the D-pad, so the comparator has something to do every frame.
 */
class PatrolWalker : public Walker {
public:
    PatrolWalker(int x,
                 int y,
                 int w,
                 int h,
                 pixelroot32::graphics::Color body,
                 pixelroot32::graphics::Color trim,
                 char label,
                 int topY,
                 int bottomLimitY,
                 int pixelsPerSecond,
                 bool startsDownward);

    void update(unsigned long deltaTime) override;

    /// Back to the starting row and the starting direction.
    void resetPatrol();

private:
    int startY_;
    int topY_;
    int bottomLimitY_;
    int speed_;
    bool startsDownward_;
    bool movingDown_;
};

/**
 * @class PlayerWalker
 * @brief The D-pad-driven Walker.
 *
 * Reads the InputManager itself rather than being pushed by the scene: the
 * scene calls Scene::update() first (engine contract), so an actor that waited
 * to be told where to go would always be acting on last frame's buttons.
 */
class PlayerWalker : public Walker {
public:
    PlayerWalker(int x,
                 int y,
                 int w,
                 int h,
                 pixelroot32::graphics::Color body,
                 pixelroot32::graphics::Color trim,
                 char label,
                 int pixelsPerSecond);

    void update(unsigned long deltaTime) override;

    /// Back to the starting position.
    void resetToStart();

private:
    int startX_;
    int startY_;
    int speed_;
};

/**
 * @class DepthSortScene
 * @brief Y-axis depth sorting, with an off switch next to it.
 *
 * Four actors of four different heights share render layer 1 and overlap each
 * other. With the comparator assigned they are drawn feet-first-from-the-top:
 * whoever stands lower on screen is painted last and therefore in front. With
 * it cleared they are drawn in the order they were added, which is a fixed,
 * plausible-looking, wrong answer.
 *
 * Nothing here allocates: the four actors are members, added with addEntity()
 * and re-added in a fixed order when the mode changes.
 */
class DepthSortScene : public pixelroot32::core::Scene {
public:
    DepthSortScene();

    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

private:
    /// Button indices, in the order InputConfig receives them in the platform
    /// headers: Up, Down, Left, Right, A, B.
    static constexpr std::uint8_t kButtonA = 4;
    static constexpr std::uint8_t kButtonB = 5;

    static constexpr int kWalkerCount = 4;

    /**
     * @brief Rebuilds the entity array and (re)binds the comparator.
     *
     * The rebuild is the point. Scene::sortEntities() sorts the array in place,
     * so simply clearing depthComparator would leave the actors frozen in
     * whatever order the last sorted frame produced — not the order they were
     * added in. Clearing and re-adding restores the canonical add order, which
     * is what UNSORTED is supposed to show.
     */
    void applyMode();

    /// Every actor back to its starting row and direction.
    void resetPositions();

    /// Refreshes the four bottom-Y readouts. snprintf into fixed members.
    void refreshHud();

    PlayerWalker player_;
    PatrolWalker tall_;
    PatrolWalker medium_;
    PatrolWalker squat_;

    /// Canonical add order: the player goes in FIRST, so with the comparator
    /// cleared it is drawn first and therefore always behind — visibly wrong
    /// the moment it walks below anybody.
    Walker* order_[kWalkerCount];

    /// True while depthComparator is assigned. Starts at the value of the
    /// build flag, so a build without PIXELROOT32_ENABLE_DEPTH_SORT is honest
    /// about being stuck in UNSORTED rather than pretending the toggle works.
    bool sorted_ = pixelroot32::platforms::config::EnableDepthSort;

    char valueText_[kWalkerCount][8] = {{0}, {0}, {0}, {0}};
};

}  // namespace depth_sort
