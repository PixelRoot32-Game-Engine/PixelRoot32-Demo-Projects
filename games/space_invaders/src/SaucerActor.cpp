#include "SaucerActor.h"

#include "GameConstants.h"
#include "assets/SaucerSprites.h"
#include "rules/RulesConstants.h"

namespace pr32 = pixelroot32;

namespace spaceinvaders {

namespace {

/// Screen row the saucer flies along; S11 fixes it at y=24.
pr32::math::Scalar saucerRow() {
    return pr32::math::toScalar(static_cast<int>(SAUCER_Y));
}

}  // namespace

SaucerActor::SaucerActor()
    : StaticActor(pr32::math::Vector2(pr32::math::toScalar(0), saucerRow()), SAUCER_WIDTH,
                  SAUCER_HEIGHT) {
    // AABB is the whole 16x7 mask here: the scene sweeps a player shot against
    // this box, and the art fills it, so no pixel-level test is needed.
    setShape(pr32::core::CollisionShape::AABB);
}

void SaucerActor::reset() {
    active = false;
    // The side is part of the start-of-run state too, so a fresh run cannot
    // inherit the previous run's last crossing.
    travelDirection = 1;
}

void SaucerActor::spawn(int8_t direction) {
    // Only a negative side enters from the right; anything else crosses
    // rightwards, so a stray value can never park the saucer mid-field.
    travelDirection = (direction < 0) ? static_cast<int8_t>(-1) : static_cast<int8_t>(1);

    // Both entries are fully outside the playfield: entering from the left
    // means the mask's right edge starts at FIELD_X, and symmetrically for the
    // right entry, so the ship slides in instead of appearing on the row. The
    // scene composites on top of a full-screen black fill, so the columns the
    // mask covers outside the field cost nothing visible.
    const int startX = (travelDirection > 0) ? FIELD_X - SAUCER_WIDTH : FIELD_X + FIELD_W;
    setPosition(pr32::math::Vector2(pr32::math::toScalar(startX), saucerRow()));
    active = true;
}

void SaucerActor::update(unsigned long deltaTime) {
    // S20: the tick is the clock. The scene calls this once per fixed rules
    // tick, so a whole two-pixel step lands per tick at any frame rate.
    (void)deltaTime;

    if (!active) {
        return;
    }

    const int nextX =
        static_cast<int>(position.x) + static_cast<int>(travelDirection) * rules::SaucerStepX;

    // setPosition keeps the base previousPosition in sync, so the static body
    // never carries a stale crossing segment into a later swept test.
    setPosition(pr32::math::Vector2(pr32::math::toScalar(nextX), position.y));

    // The crossing ends when the whole 16 px mask has left the far edge.
    // Checking the leading edge instead would pop the ship out while half of
    // it is still on the row.
    const bool leftPlayfield = (travelDirection > 0)
                                   ? nextX >= FIELD_X + FIELD_W
                                   : nextX + SAUCER_WIDTH <= FIELD_X;
    if (leftPlayfield) {
        active = false;
    }
}

void SaucerActor::draw(pr32::graphics::Renderer& renderer) {
    if (!active) {
        return;
    }

    renderer.drawSprite(SAUCER_SPRITE, static_cast<int>(position.x), static_cast<int>(position.y),
                        pr32::graphics::Color::White);
}

void SaucerActor::collect() {
    active = false;
}

bool SaucerActor::isActive() const {
    return active;
}

int8_t SaucerActor::direction() const {
    return travelDirection;
}

}  // namespace spaceinvaders
