#pragma once
#include "core/Scene.h"
#include "PaddleActor.h"
#include "BallActor.h"
#include "graphics/Color.h"
#include "platforms/EngineConfig.h"
#include "GameConstants.h"
#include <memory>
#include <vector>
#include "graphics/particles/ParticleEmitter.h"

namespace pong {

class StressBall;

class PongScene : public pixelroot32::core::Scene {
public:
    PongScene();
    ~PongScene() override;

    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

    float getBallX() { return static_cast<float>(ball->position.x); }
    float getBallY() { return static_cast<float>(ball->position.y); }
    
    pixelroot32::graphics::particles::ParticleEmitter* getParticleEmitter() { return explosionEffect.get(); }

private:
    std::unique_ptr<PaddleActor> leftPaddle;
    std::unique_ptr<PaddleActor> rightPaddle;
    std::unique_ptr<BallActor> ball;
    
    // Container for other entities owned by the scene (like background)
    std::vector<std::unique_ptr<pixelroot32::core::Entity>> ownedEntities;
    
    // Particle system for stress test
    std::unique_ptr<pixelroot32::graphics::particles::ParticleEmitter> explosionEffect;

    int leftScore, rightScore;
    bool gameOver;
    
    // Play area bounds
    int playAreaTop;
    int playAreaBottom;

    std::vector<std::unique_ptr<StressBall>> stressBalls;

    void resetGame();
};

}
