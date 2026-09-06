#include "PongScene.h"
#include "core/Engine.h"
#include "physics/RigidActor.h"
#include "physics/StaticActor.h"
#include "audio/AudioTypes.h"
#include "GameLayers.h"
#include "graphics/particles/ParticlePresets.h"
#include "PongPhysicsValidator.h"

namespace pr32 = pixelroot32;

extern pr32::core::Engine engine;

namespace pong {

using Color = pr32::graphics::Color;

/**
 * @brief Classic Pong clone with particle effects and stress testing
 * 
 * Gameplay Features:
 * - Particle System: Generates visual sparks on collision (in StressBall::onCollision).
 * - "English" Effect: Paddle movement influences ball reflection angle.
 * - Stress Test Mode: Can spawn multiple balls to test physics performance.
 * 
 * Architecture:
 * - Local Classes: Defines PongBackground and StressBall locally to keep the
 *   example self-contained.
 * - Direct Physics: Uses RigidActor for balls but handles paddle movement
 *   kinematically based on input.
 */
class PongBackground : public pr32::core::Entity {
public:
    PongBackground(int top, int bottom)
        : pr32::core::Entity(pr32::math::Vector2(0.0f, 0.0f), 0, 0, pr32::core::EntityType::GENERIC),
          playAreaTop(top),
          playAreaBottom(bottom) {
        setRenderLayer(0);
    }

    void update(unsigned long) override {
    }

    void draw(pr32::graphics::Renderer& renderer) override {
        int screenWidth = renderer.getLogicalWidth();
        int screenHeight = renderer.getLogicalHeight();

        renderer.drawFilledRectangle(0, 0, screenWidth, playAreaTop, Color::DarkGray);
        renderer.drawFilledRectangle(0, playAreaBottom, screenWidth, screenHeight - playAreaBottom, Color::DarkGray);

        renderer.drawLine(0, playAreaTop, screenWidth, playAreaTop, Color::White);
        renderer.drawLine(0, playAreaBottom, screenWidth, playAreaBottom, Color::White);

        int16_t centerX = screenWidth / 2;
        int16_t dashHeight = 10;
        int16_t dashGap = 5;

        for (int16_t y = 0; y < screenHeight; y += dashHeight + dashGap) {
            int16_t dashEnd = y + dashHeight;
            if (dashEnd > screenHeight) {
                dashEnd = screenHeight;
            }
            renderer.drawLine(centerX, y, centerX, dashEnd, Color::LightGray);
        }
    }

private:
    int playAreaTop;
    int playAreaBottom;
};

class StressBall : public pr32::physics::RigidActor {
public:
    int radius;

    StressBall(pixelroot32::math::Vector2 position, float initialSpeed, int radius, int index)
        : pr32::physics::RigidActor(position, radius * 2, radius * 2),
          radius(radius) {
        setRestitution(pr32::math::toScalar(1.0f));
        setFriction(pr32::math::toScalar(0.0f));
        setGravityScale(pr32::math::toScalar(0.0f));

        setRenderLayer(1);
        setCollisionLayer(Layers::BALL);
        setCollisionMask(Layers::BALL | Layers::PADDLE | Layers::WALL);

        float sx = initialSpeed;
        float sy = initialSpeed;
        if (index % 2 != 0) {
            sx = -sx;
        }
        if ((index / 2) % 2 != 0) {
            sy = -sy;
        }
        velocity.x = pixelroot32::math::toScalar(sx);
        velocity.y = pixelroot32::math::toScalar(sy);
    }

    void update(unsigned long deltaTime) override {
        pr32::core::PhysicsActor::update(deltaTime);
    }

    void draw(pr32::graphics::Renderer& renderer) override {
        renderer.drawFilledCircle(static_cast<int>(position.x), static_cast<int>(position.y), radius, Color::LightGray);
    }

    pr32::core::Rect getHitBox() override {
        return { {position.x - radius, position.y - radius}, radius * 2, radius * 2 };
    }

    void onCollision(pr32::core::Actor* other) override {
        pr32::physics::RigidActor::onCollision(other);
        
        // Trigger particle effect on collision
        auto scene = static_cast<PongScene*>(engine.getCurrentScene().value_or(nullptr));
        if (scene && scene->getParticleEmitter()) {
            scene->getParticleEmitter()->burst(position, 3); // Small burst for performance
        }
    }
};

PongScene::PongScene() = default;
PongScene::~PongScene() = default;

void PongScene::init() {
    pr32::graphics::setPalette(pr32::graphics::PaletteType::PICO8);
    int screenWidth = engine.getRenderer().getLogicalWidth();
    int screenHeight = engine.getRenderer().getLogicalHeight();

    // Calculate play area
    playAreaTop = (screenHeight - PONG_PLAY_AREA_HEIGHT) / 2;
    playAreaBottom = playAreaTop + PONG_PLAY_AREA_HEIGHT;

    leftScore = 0;
    rightScore = 0;
    gameOver = false;

    // Clear any existing entities owned by this scene
    ownedEntities.clear();

    auto bg = std::make_unique<PongBackground>(playAreaTop, playAreaBottom);
    addEntity(bg.get());
    ownedEntities.push_back(std::move(bg));

    // Create Static Walls for Physics
    // Top Wall
    auto topWall = std::make_unique<pr32::physics::StaticActor>(
        pixelroot32::math::toScalar(0), 
        pixelroot32::math::toScalar(playAreaTop - 19), 
        screenWidth, 
        20
    );
    topWall->setCollisionLayer(Layers::WALL);
    topWall->setRestitution(pixelroot32::math::toScalar(1.0f));
    topWall->setFriction(pixelroot32::math::toScalar(0.0f));
    addEntity(topWall.get());
    ownedEntities.push_back(std::move(topWall));

    // Bottom Wall
    auto bottomWall = std::make_unique<pr32::physics::StaticActor>(
        pixelroot32::math::toScalar(0), 
        pixelroot32::math::toScalar(playAreaBottom - 1), 
        screenWidth, 
        20
    );
    bottomWall->setCollisionLayer(Layers::WALL);
    bottomWall->setRestitution(pixelroot32::math::toScalar(1.0f));
    bottomWall->setFriction(pixelroot32::math::toScalar(0.0f));
    addEntity(bottomWall.get());
    ownedEntities.push_back(std::move(bottomWall));

    leftPaddle = std::make_unique<PaddleActor>(pixelroot32::math::Vector2(0, screenHeight/2 - PADDLE_HEIGHT/2), PADDLE_WIDTH, PADDLE_HEIGHT, false);
    leftPaddle->setTopLimit(playAreaTop);
    leftPaddle->setBottomLimit(playAreaBottom);
    leftPaddle->setCollisionLayer(Layers::PADDLE);
    leftPaddle->setCollisionMask(Layers::BALL);
    leftPaddle->setShape(pr32::core::CollisionShape::AABB);
    
    rightPaddle = std::make_unique<PaddleActor>(pixelroot32::math::Vector2(screenWidth - PADDLE_WIDTH, screenHeight/2 - PADDLE_HEIGHT/2), PADDLE_WIDTH, PADDLE_HEIGHT, true);
    rightPaddle->setTopLimit(playAreaTop);
    rightPaddle->setBottomLimit(playAreaBottom);
    rightPaddle->setCollisionLayer(Layers::PADDLE);
    rightPaddle->setCollisionMask(Layers::BALL);
    rightPaddle->setShape(pr32::core::CollisionShape::AABB);
    
    // Use CCD test speed if enabled (for testing ultra-fast collisions)
    float ballSpeed = CCD_TEST_MODE ? CCD_TEST_BALL_SPEED : BALL_SPEED;
    ball = std::make_unique<BallActor>(pixelroot32::math::Vector2(screenWidth/2, screenHeight/2), ballSpeed, BALL_RADIUS);
    ball->reset();

    // Initialize particle system for stress test
    explosionEffect = std::make_unique<pr32::graphics::particles::ParticleEmitter>(pixelroot32::math::Vector2(100, 100), pr32::graphics::particles::ParticlePresets::Explosion);
    addEntity(explosionEffect.get());

    stressBalls.clear();
    if (STRESS_TEST_MODE) {
        int maxEntities = pr32::platforms::config::MaxEntities;
        int baseStress = STRESS_BASE_BALL_COUNT;
        int playAreaHeight = playAreaBottom - playAreaTop;
        int referenceArea = 160 * 160;
        int screenArea = screenWidth * playAreaHeight;
        int scaledStress = baseStress * screenArea / referenceArea;
        if (scaledStress < 8) {
            scaledStress = 8;
        }
        if (scaledStress > baseStress) {
            scaledStress = baseStress;
        }

        int reservedEntities = 3 + static_cast<int>(ownedEntities.size());
        int availableSlots = maxEntities - reservedEntities;
        if (availableSlots < 0) {
            availableSlots = 0;
        }

        int stressCount = scaledStress;
        if (stressCount > availableSlots) {
            stressCount = availableSlots;
        }

        for (int i = 0; i < stressCount; ++i) {
            float startX = static_cast<float>(screenWidth / 4 + (i % 8) * (BALL_RADIUS * 2));
            float startY = static_cast<float>(playAreaTop + BALL_RADIUS + (i / 8) * (BALL_RADIUS * 2));
            auto stressBall = std::make_unique<StressBall>(pixelroot32::math::Vector2(startX, startY), BALL_SPEED * 1.5f, BALL_RADIUS, i);
            addEntity(stressBall.get());
            stressBalls.push_back(std::move(stressBall));
        }
    }

    addEntity(leftPaddle.get());
    addEntity(rightPaddle.get());
    addEntity(ball.get());
    
    // Initialize physics validator
    PongPhysicsValidator::getInstance().startTracking(BALL_SPEED);
}

void PongScene::update(unsigned long deltaTime) {
    // 1. Input Processing
    if (!gameOver) {
        leftPaddle->velocity = 0;
        if (engine.getInputManager().isButtonDown(BTN_UP)) leftPaddle->velocity = -150.0f;
        if (engine.getInputManager().isButtonDown(BTN_DOWN)) leftPaddle->velocity = 150.0f;
    } else {
        if (engine.getInputManager().isButtonPressed(BTN_START)) resetGame();
    }

    // 2. Physics & Entity Update (Always call this!)
    Scene::update(deltaTime);

    // 3. Physics Validation
    PongPhysicsValidator::getInstance().update(
        static_cast<float>(ball->position.x),
        static_cast<float>(ball->position.y),
        static_cast<float>(ball->getVelocityX()),
        static_cast<float>(ball->getVelocityY()),
        ball->isActive
    );

    // 4. Game Logic
    if (!gameOver) {
        // --- Check if ball is out of bounds ---
        // Left side (Player failed to return) - AI scores
        int ballLeft = static_cast<int>(ball->position.x);
        int ballRight = ballLeft + BALL_RADIUS * 2;
        int screenWidth = engine.getRenderer().getLogicalWidth();
        
        if (ball->isActive && ballRight < 0) {
            rightScore++;
            
            // Score sound (bad for player)
            pr32::audio::AudioEvent scoreEv{};
            scoreEv.type = pr32::audio::WaveType::NOISE;
            scoreEv.frequency = 200.0f;
            scoreEv.duration = 0.3f;
            scoreEv.volume = 0.7f;
            engine.getAudioEngine().playEvent(scoreEv);

            ball->reset();
            PongPhysicsValidator::getInstance().onReset();
        }
        
        // Right side (AI failed to return) - Player scores
        if (ball->isActive && ballLeft > screenWidth) {
            leftScore++;

            pr32::audio::AudioEvent coinEv{};
            coinEv.type = pr32::audio::WaveType::PULSE;
            coinEv.frequency = 900.0f;
            coinEv.duration = 0.2f;
            coinEv.volume = 0.7f;
            coinEv.duty = 0.5f;
            engine.getAudioEngine().playEvent(coinEv);

            ball->reset();
            PongPhysicsValidator::getInstance().onReset();
        }

        // --- Game Over ---
        if (leftScore >= SCORE_TO_WIN || rightScore >= SCORE_TO_WIN) {
            gameOver = true;

            if (leftScore >= SCORE_TO_WIN) {
                // Player Wins
                pr32::audio::AudioEvent winEv{};
                winEv.type = pr32::audio::WaveType::PULSE;
                winEv.frequency = 600.0f;
                winEv.duration = 0.6f;
                winEv.volume = 0.8f;
                engine.getAudioEngine().playEvent(winEv);
            } else {
                // CPU Wins
                pr32::audio::AudioEvent loseEv{};
                loseEv.type = pr32::audio::WaveType::NOISE;
                loseEv.frequency = 200.0f;
                loseEv.duration = 0.6f;
                loseEv.volume = 0.8f;
                engine.getAudioEngine().playEvent(loseEv);
            }
        }

    }
}

void PongScene::draw(pr32::graphics::Renderer& renderer) {
    Scene::draw(renderer);

    char scoreStr[16];
    snprintf(scoreStr, sizeof(scoreStr), "%d : %d", leftScore, rightScore);
    int16_t scoreY = playAreaTop / 2 - 8;
    renderer.drawTextCentered(scoreStr, scoreY, Color::Black, 2);

    if (gameOver) {
        renderer.drawTextCentered("GAME OVER", 120, Color::White, 2);
        renderer.drawTextCentered("PRESS A TO START", 150, Color::White, 1);
    }
}

void PongScene::resetGame() {
    // Print physics validation report before resetting
    PongPhysicsValidator::getInstance().printReport();
    PongPhysicsValidator::getInstance().reset();
    
    leftScore = 0;
    rightScore = 0;
    gameOver = false;
    ball->reset();
    
    // Restart tracking with fresh metrics
    PongPhysicsValidator::getInstance().startTracking(BALL_SPEED);
}

}
