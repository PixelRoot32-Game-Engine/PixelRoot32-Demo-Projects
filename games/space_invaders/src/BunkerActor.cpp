#include "BunkerActor.h"
#include "BandColor.h"

namespace spaceinvaders {

namespace {

/// Origin of the mask in world pixels; actor positions are integral by design.
inline int originX(const pixelroot32::math::Vector2& position) {
    return static_cast<int>(position.x);
}

inline int originY(const pixelroot32::math::Vector2& position) {
    return static_cast<int>(position.y);
}

}  // namespace

BunkerActor::BunkerActor(pixelroot32::math::Vector2 position, rules::ShieldField& field,
                         int shieldIndex)
    : StaticActor(position, rules::ShieldWidth, rules::ShieldHeight),
      field(field),
      shieldIndex(shieldIndex) {
    setShape(pixelroot32::core::CollisionShape::AABB);
}

void BunkerActor::update(unsigned long deltaTime) {
    (void)deltaTime;
}

void BunkerActor::draw(pixelroot32::graphics::Renderer& renderer) {
    if (isDestroyed()) {
        return;
    }

    const int left = originX(position);
    const int top = originY(position);

    for (int y = 0; y < rules::ShieldHeight; ++y) {
        const uint32_t bits = field.rowBits(shieldIndex, y);
        if (bits == 0u) {
            continue;
        }

        // The band is resolved from the screen row, so the whole mask keeps the
        // single colour the cabinet overlay gives it (S3).
        const pixelroot32::graphics::Color color = bandColorFor(top + y);

        int x = 0;
        while (x < rules::ShieldWidth) {
            if ((bits & (1u << x)) == 0u) {
                ++x;
                continue;
            }
            const int runStart = x;
            while (x < rules::ShieldWidth && (bits & (1u << x)) != 0u) {
                ++x;
            }
            renderer.drawFilledRectangle(left + runStart, top + y, x - runStart, 1, color);
        }
    }
}

void BunkerActor::onCollision(pixelroot32::core::Actor* other) {
    (void)other;
}

bool BunkerActor::isDestroyed() const {
    return field.destroyed(shieldIndex);
}

bool BunkerActor::solidAtWorld(int worldX, int worldY) const {
    const int localX = worldX - originX(position);
    const int localY = worldY - originY(position);
    if (localX < 0 || localX >= rules::ShieldWidth || localY < 0 ||
        localY >= rules::ShieldHeight) {
        return false;
    }
    return field.solid(shieldIndex, localX, localY);
}

bool BunkerActor::carveSweptWorld(int fromWorldX, int fromWorldY, int toWorldX, int toWorldY) {
    return rules::carveSwept(field, shieldIndex, fromWorldX - originX(position),
                             fromWorldY - originY(position), toWorldX - originX(position),
                             toWorldY - originY(position));
}

}
