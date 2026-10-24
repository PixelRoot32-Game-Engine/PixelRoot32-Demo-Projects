#include "AlienActor.h"
#include "BandColor.h"
#include "assets/AlienSprites.h"

namespace spaceinvaders {

using pixelroot32::graphics::Sprite;
using pixelroot32::graphics::SpriteAnimationFrame;

static const SpriteAnimationFrame SQUID_ANIM_FRAMES[] = {
    { &SQUID_F1, nullptr },
    { &SQUID_F2, nullptr }
};

static const SpriteAnimationFrame CRAB_ANIM_FRAMES[] = {
    { &CRAB_F1, nullptr },
    { &CRAB_F2, nullptr }
};

static const SpriteAnimationFrame OCTOPUS_ANIM_FRAMES[] = {
    { &OCTOPUS_F1, nullptr },
    { &OCTOPUS_F2, nullptr }
};

AlienActor::AlienActor(pixelroot32::math::Vector2 position, rules::AlienType type)
    : pixelroot32::core::Actor(position, 0, 0), type(type), active(true) {

    // The rules layer owns the art dimensions: the sweep's edge check and this
    // hit box must describe the same rectangle, so neither may keep its own copy.
    width = rules::alienWidth(type);
    height = rules::alienHeight(type);

    if (type == rules::AlienType::Squid) {
        animation.frames     = SQUID_ANIM_FRAMES;
        animation.frameCount = static_cast<uint8_t>(sizeof(SQUID_ANIM_FRAMES) / sizeof(SpriteAnimationFrame));
    } else if (type == rules::AlienType::Crab) {
        animation.frames     = CRAB_ANIM_FRAMES;
        animation.frameCount = static_cast<uint8_t>(sizeof(CRAB_ANIM_FRAMES) / sizeof(SpriteAnimationFrame));
    } else {
        animation.frames     = OCTOPUS_ANIM_FRAMES;
        animation.frameCount = static_cast<uint8_t>(sizeof(OCTOPUS_ANIM_FRAMES) / sizeof(SpriteAnimationFrame));
    }
    animation.reset();
}

void AlienActor::update(unsigned long deltaTime) {
    (void)deltaTime;
}

void AlienActor::move(pixelroot32::math::Scalar dx, pixelroot32::math::Scalar dy) {
    // Position only: the frame is written by setAnimationFrame() from the rack,
    // which flips it once per completed sweep rather than once per movement.
    position.x += dx;
    position.y += dy;
}

void AlienActor::setAnimationFrame(int frame) {
    if (!animation.frames || animation.frameCount == 0) {
        return;
    }
    animation.current = static_cast<uint8_t>(frame % animation.frameCount);
}

void AlienActor::draw(pixelroot32::graphics::Renderer& renderer) {
    if (!active) return;

    using Sprite = pixelroot32::graphics::Sprite;

    const Sprite* sprite = animation.getCurrentSprite();

    const int drawX = static_cast<int>(position.x);
    const int drawY = static_cast<int>(position.y);

    // 1:1 draws: the arcade screen model has no scale factor, and a scaled draw
    // would make the sprite disagree with its collision box. The overlay tints
    // the mask by the row it is drawn on (S3), so the alien row decides it.
    if (sprite) {
        renderer.drawSprite(*sprite, drawX, drawY, bandColorFor(drawY));
    }
}

pixelroot32::core::Rect AlienActor::getHitBox() {
    return {position, width, height};
}

void AlienActor::onCollision(pixelroot32::core::Actor* other) {
    (void)other;
}

int AlienActor::getScoreValue() const {
    return rules::alienScore(type);
}

}  // namespace spaceinvaders
