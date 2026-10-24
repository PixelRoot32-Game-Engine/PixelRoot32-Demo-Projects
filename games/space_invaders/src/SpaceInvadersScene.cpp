#include "SpaceInvadersScene.h"
#include "PlayerActor.h"
#include "AlienActor.h"
#include "ProjectileActor.h"
#include "BunkerActor.h"
#include "GameConstants.h"
#include "platforms/EngineConfig.h"
#include "core/Engine.h"
#include "audio/AudioTypes.h"
#include "audio/AudioMusicTypes.h"
#include <cstdlib>
#include <cstdio>
#include "assets/FontData.h"
#include "assets/PlayerExplosionSprites.h"

namespace pr32 = pixelroot32;
extern pr32::core::Engine engine;

namespace spaceinvaders {

using pixelroot32::graphics::Sprite;
using pixelroot32::graphics::SpriteAnimationFrame;
using pixelroot32::audio::AudioEvent;
using pixelroot32::audio::WaveType;
using pixelroot32::audio::MusicNote;
using pixelroot32::audio::MusicTrack;
using pixelroot32::audio::InstrumentPreset;
using pixelroot32::audio::INSTR_PULSE_BASS;
using pixelroot32::audio::Note;
using pixelroot32::math::Scalar;
using pixelroot32::math::Vector2;

#ifdef PIXELROOT32_ENABLE_SCENE_ARENA
using pixelroot32::core::SceneArena;
using pixelroot32::core::arenaNew;

/** Arena for all scene entities (player, aliens, bunkers, projectiles).
 *  The arena never frees an individual actor, so the whole wave has to fit at
 *  once: one player, 55 aliens, four shields and twelve shots. Measured on the
 *  64-bit native build that is 248 + 4400 + 736 + 2400 = 7784 bytes, and the
 *  previous 4096 budget silently dropped every alien past the forty-eighth,
 *  all four shields and most shots because arenaNew() returns null and the
 *  spawn loops skip the actor. 8192 leaves one spare actor of headroom, and
 *  the static_assert in init() turns a future size regression into a compile
 *  error instead of a formation with holes.
 *  If you need 2048 again (e.g. tight RAM), build this target without
 *  PIXELROOT32_ENABLE_SCENE_ARENA so the scene uses heap instead of this
 *  fixed buffer. */
static unsigned char SPACE_INVADERS_SCENE_ARENA_BUFFER[8192];
#endif


// Base four-note bass pattern: "tu tu tu tu"
static const InstrumentPreset BASS_INSTRUMENT = INSTR_PULSE_BASS;

static const MusicNote BGM_SLOW_NOTES[] = {
    pixelroot32::audio::makeNote(BASS_INSTRUMENT, Note::C, 0.21f),
    pixelroot32::audio::makeRest(0.207f),
    pixelroot32::audio::makeNote(BASS_INSTRUMENT, Note::C, 0.21f),
    pixelroot32::audio::makeRest(0.207f),
    pixelroot32::audio::makeNote(BASS_INSTRUMENT, Note::C, 0.21f),
    pixelroot32::audio::makeRest(0.207f),
    pixelroot32::audio::makeNote(BASS_INSTRUMENT, Note::C, 0.21f),
    pixelroot32::audio::makeRest(0.207f),
};

static const MusicNote BGM_MEDIUM_NOTES[] = {
    pixelroot32::audio::makeNote(BASS_INSTRUMENT, Note::C, 0.12f),
    pixelroot32::audio::makeRest(0.06f),
    pixelroot32::audio::makeNote(BASS_INSTRUMENT, Note::C, 0.12f),
    pixelroot32::audio::makeRest(0.06f),
    pixelroot32::audio::makeNote(BASS_INSTRUMENT, Note::C, 0.12f),
    pixelroot32::audio::makeRest(0.06f),
    pixelroot32::audio::makeNote(BASS_INSTRUMENT, Note::C, 0.12f),
    pixelroot32::audio::makeRest(0.06f),
};

static const MusicNote BGM_FAST_NOTES[] = {
    pixelroot32::audio::makeNote(BASS_INSTRUMENT, Note::C, 0.08f),
    pixelroot32::audio::makeRest(0.04f),
    pixelroot32::audio::makeNote(BASS_INSTRUMENT, Note::C, 0.08f),
    pixelroot32::audio::makeRest(0.04f),
    pixelroot32::audio::makeNote(BASS_INSTRUMENT, Note::C, 0.08f),
    pixelroot32::audio::makeRest(0.04f),
    pixelroot32::audio::makeNote(BASS_INSTRUMENT, Note::C, 0.08f),
    pixelroot32::audio::makeRest(0.04f),
};

static const MusicTrack BGM_SLOW_TRACK = {
    BGM_SLOW_NOTES,
    sizeof(BGM_SLOW_NOTES) / sizeof(MusicNote),
    true,
    WaveType::PULSE,
    BASS_INSTRUMENT.duty
};

static const MusicTrack BGM_MEDIUM_TRACK = {
    BGM_MEDIUM_NOTES,
    sizeof(BGM_MEDIUM_NOTES) / sizeof(MusicNote),
    true,
    WaveType::PULSE,
    BASS_INSTRUMENT.duty
};

static const MusicTrack BGM_FAST_TRACK = {
    BGM_FAST_NOTES,
    sizeof(BGM_FAST_NOTES) / sizeof(MusicNote),
    true,
    WaveType::PULSE,
    BASS_INSTRUMENT.duty
};

// --- WIN / GAME OVER MUSIC ---

static const MusicNote WIN_NOTES[] = {
    pixelroot32::audio::makeNote(BASS_INSTRUMENT, Note::C, 0.15f),
    pixelroot32::audio::makeNote(BASS_INSTRUMENT, Note::E, 0.15f),
    pixelroot32::audio::makeNote(BASS_INSTRUMENT, Note::G, 0.15f),
    pixelroot32::audio::makeNote(BASS_INSTRUMENT, Note::C, 0.4f), // C High ideally, but using C for safety if C_High undefined
    pixelroot32::audio::makeRest(0.1f)
};

static const MusicTrack WIN_TRACK = {
    WIN_NOTES,
    sizeof(WIN_NOTES) / sizeof(MusicNote),
    false, // No loop
    WaveType::PULSE,
    BASS_INSTRUMENT.duty
};

static const MusicNote GAME_OVER_NOTES[] = {
    pixelroot32::audio::makeNote(BASS_INSTRUMENT, Note::G, 0.2f),
    pixelroot32::audio::makeNote(BASS_INSTRUMENT, Note::E, 0.2f), // Using E instead of Eb if Eb undefined, checking later
    pixelroot32::audio::makeNote(BASS_INSTRUMENT, Note::C, 0.4f),
    pixelroot32::audio::makeRest(0.1f)
};

static const MusicTrack GAME_OVER_TRACK = {
    GAME_OVER_NOTES,
    sizeof(GAME_OVER_NOTES) / sizeof(MusicNote),
    false, // No loop
    WaveType::PULSE,
    BASS_INSTRUMENT.duty
};


ExplosionAnimation::ExplosionAnimation()
    : active(false), position(pr32::math::toScalar(0), pr32::math::toScalar(0)), timeAccumulator(0), stepsDone(0) {
    animation.frames = PLAYER_EXPLOSION_FRAMES;
    animation.frameCount = static_cast<uint8_t>(sizeof(PLAYER_EXPLOSION_FRAMES) / sizeof(SpriteAnimationFrame));
    animation.current = 0;
}

void ExplosionAnimation::start(pr32::math::Vector2 pos) {
    position = pos;
    timeAccumulator = 0;
    stepsDone = 0;
    animation.reset();
    active = true;
}

void ExplosionAnimation::update(unsigned long deltaTime) {
    if (!active) {
        return;
    }

    const unsigned long frameTimeMs = 60;
    timeAccumulator += deltaTime;

    while (timeAccumulator >= frameTimeMs && active) {
        timeAccumulator -= frameTimeMs;

        if (stepsDone + 1 < animation.frameCount) {
            animation.step();
            stepsDone++;
        } else {
            active = false;
        }
    }
}

void ExplosionAnimation::draw(pr32::graphics::Renderer& renderer) {
    if (!active) {
        return;
    }

    const Sprite* sprite = animation.getCurrentSprite();
    if (!sprite) {
        return;
    }

    const int drawX = static_cast<int>(position.x);
    const int drawY = static_cast<int>(position.y);

    renderer.drawSprite(*sprite, drawX, drawY, pr32::graphics::Color::White);
}

bool ExplosionAnimation::isActive() const {
    return active;
}

SpaceInvadersScene::SpaceInvadersScene()
    : player(nullptr),
      score(0),
      lives(3),
      gameOver(false),
      gameWon(false),
      isPaused(false),
      fireInputReady(false),
      currentMusicTempoFactor(1.0f) {

    // Null grid entries mean "no actor for this cell"; spawnAliens() fills it
    // after the rack allocates the wave, and cleanup() clears it again.
    for (int row = 0; row < rules::RackRows; ++row) {
        for (int col = 0; col < rules::RackCols; ++col) {
            alienGrid[row][col] = nullptr;
        }
    }

    for (int i = 0; i < MaxEnemyExplosions; ++i) {
        enemyExplosions[i].active = false;
        enemyExplosions[i].position = pr32::math::Vector2(0, 0);
        enemyExplosions[i].remainingMs = 0;
    }
}

SpaceInvadersScene::~SpaceInvadersScene() {
    cleanup();
}

void SpaceInvadersScene::init() {
#ifdef PIXELROOT32_ENABLE_SCENE_ARENA
    arena.init(SPACE_INVADERS_SCENE_ARENA_BUFFER, sizeof(SPACE_INVADERS_SCENE_ARENA_BUFFER));

    // A failed arenaNew() is silent: the spawn loops skip the actor and the
    // wave comes out short. Every actor the scene can own must fit together,
    // so the bound is checked at compile time instead of hours into a run.
    static_assert(sizeof(PlayerActor) +
                      rules::RackRows * rules::RackCols * sizeof(AlienActor) +
                      BUNKER_COUNT * sizeof(BunkerActor) +
                      MaxProjectiles * sizeof(ProjectileActor) <=
                  sizeof(SPACE_INVADERS_SCENE_ARENA_BUFFER),
                  "scene arena must hold one full wave");
#endif
    pr32::graphics::setSpritePalette(pr32::graphics::PaletteType::NES);
    resetGame();

    engine.getMusicPlayer().play(BGM_SLOW_TRACK);
    currentMusicTempoFactor = 1.0f;
    engine.getMusicPlayer().setTempoFactor(currentMusicTempoFactor);
}

void SpaceInvadersScene::cleanup() {
    clearEntities();

    for (int row = 0; row < rules::RackRows; ++row) {
        for (int col = 0; col < rules::RackCols; ++col) {
            alienGrid[row][col] = nullptr;
        }
    }

#ifdef PIXELROOT32_ENABLE_SCENE_ARENA
    player = nullptr;
    aliens.clear();
    projectiles.clear();
    bunkers.clear();
#else
    player.reset();
    aliens.clear();
    projectiles.clear();
    bunkers.clear();
#endif
}

void SpaceInvadersScene::resetGame() {
    cleanup();

#ifdef PIXELROOT32_ENABLE_SCENE_ARENA
    arena.reset();
#endif

#ifdef PIXELROOT32_ENABLE_SCENE_ARENA
    player = arenaNew<PlayerActor>(arena, pr32::math::Vector2(PLAYER_START_X, PLAYER_START_Y));
    addEntity(player);
#else
    player = std::make_unique<PlayerActor>(pr32::math::Vector2(PLAYER_START_X, PLAYER_START_Y));
    addEntity(player.get());
#endif

    spawnAliens();
    spawnBunkers();

    projectiles.reserve(MaxProjectiles);
    for (int i = 0; i < MaxProjectiles; ++i) {
#ifdef PIXELROOT32_ENABLE_SCENE_ARENA
        ProjectileActor* projectile = arenaNew<ProjectileActor>(arena, pr32::math::Vector2(0, -PROJECTILE_HEIGHT), ProjectileType::PLAYER_BULLET);
        if (!projectile) {
            continue;
        }
        projectile->deactivate();
        projectiles.push_back(projectile);
        addEntity(projectile);
#else
        auto projectile = std::make_unique<ProjectileActor>(pr32::math::Vector2(0, -PROJECTILE_HEIGHT), ProjectileType::PLAYER_BULLET);
        projectile->deactivate();
        addEntity(projectile.get());
        projectiles.push_back(std::move(projectile));
#endif
    }

    score = 0;
    lives = 3;
    gameOver = false;
    gameWon = false;
    isPaused = false;
    fireInputReady = false;

    for (int i = 0; i < MaxEnemyExplosions; ++i) {
        enemyExplosions[i].active = false;
        enemyExplosions[i].remainingMs = 0;
    }
    
    fixedStep.reset();
}

void SpaceInvadersScene::spawnAliens() {
    // The rack owns the formation: origin, 16 px pitch, per-row variant. The
    // scene only renders it, offset by FIELD_X because the rules layer works in
    // playfield coordinates (S4).
    rack.spawn(rules::RackOriginX, rules::RackOriginY);

    for (int row = 0; row < rules::RackRows; ++row) {
        for (int col = 0; col < rules::RackCols; ++col) {
            const rules::RackCell& cell = rack.cell(row, col);
            const Scalar x = pr32::math::toScalar(FIELD_X + static_cast<int>(cell.x));
            const Scalar y = pr32::math::toScalar(static_cast<int>(cell.y));

#ifdef PIXELROOT32_ENABLE_SCENE_ARENA
            AlienActor* alien = arenaNew<AlienActor>(arena, pr32::math::Vector2(x, y), cell.type);
            if (!alien) {
                continue;
            }
            aliens.push_back(alien);
            alienGrid[row][col] = alien;
            addEntity(alien);
#else
            auto alien = std::make_unique<AlienActor>(pr32::math::Vector2(x, y), cell.type);
            alienGrid[row][col] = alien.get();
            addEntity(alien.get());
            aliens.push_back(std::move(alien));
#endif
        }
    }
}

void SpaceInvadersScene::spawnBunkers() {
    if (BUNKER_COUNT <= 0) {
        return;
    }

    float totalBunkersWidth = BUNKER_COUNT * BUNKER_WIDTH;
    float gap = (DISPLAY_WIDTH - totalBunkersWidth) / (BUNKER_COUNT + 1);

    for (int i = 0; i < BUNKER_COUNT; ++i) {
        float x = gap + i * (BUNKER_WIDTH + gap);
        float y = BUNKER_Y - BUNKER_HEIGHT;
#ifdef PIXELROOT32_ENABLE_SCENE_ARENA
        BunkerActor* bunker = arenaNew<BunkerActor>(arena, pr32::math::Vector2(x, y), BUNKER_WIDTH, BUNKER_HEIGHT, 4);
        if (!bunker) {
            continue;
        }
        bunkers.push_back(bunker);
        addEntity(bunker);
#else
        auto bunker = std::make_unique<BunkerActor>(pr32::math::Vector2(x, y), BUNKER_WIDTH, BUNKER_HEIGHT, 4);
        addEntity(bunker.get());
        bunkers.push_back(std::move(bunker));
#endif
    }
}

void SpaceInvadersScene::update(unsigned long deltaTime) {
    // The frame delta only advances the clock: rules run in fixed ticks, so a
    // slow frame cannot stretch or skip game time (S15).
    const int tickCount = fixedStep.advance(static_cast<uint32_t>(deltaTime));
    for (int index = 0; index < tickCount; ++index) {
        tick(rules::FixedStep::TickMs);
    }
}

void SpaceInvadersScene::tick(unsigned long tickMs) {
    if (gameOver) {
        if (engine.getInputManager().isButtonPressed(BTN_FIRE)) {
            resetGame();
            engine.getMusicPlayer().play(BGM_SLOW_TRACK);
            currentMusicTempoFactor = 1.0f;
            engine.getMusicPlayer().setTempoFactor(currentMusicTempoFactor);
        }
        return;
    }

    if (isPaused) {
        playerExplosion.update(tickMs);
        updateEnemyExplosions(tickMs);

        if (!playerExplosion.isActive()) {
            respawnPlayer();
            isPaused = false;
        }
        return;
    }

    Scene::update(tickMs);

    if (player) {
        if (!fireInputReady) {
            if (!player->isFireDown()) {
                fireInputReady = true;
            }
        }

        if (fireInputReady && player->wantsToShoot()) {
            bool hasPlayerBullet = false;
            for (const auto& proj : projectiles) {
                if (proj->isActive() && proj->getType() == ProjectileType::PLAYER_BULLET) {
                    hasPlayerBullet = true;
                    break;
                }
            }
            if (!hasPlayerBullet) {
                pr32::math::Scalar px = player->position.x + pr32::math::toScalar(PLAYER_WIDTH - PROJECTILE_WIDTH) * pr32::math::toScalar(0.5f);
                pr32::math::Scalar py = player->position.y - pr32::math::toScalar(PROJECTILE_HEIGHT);

                for (auto& proj : projectiles) {
                    if (!proj->isActive()) {
                        proj->reset(pr32::math::Vector2(px, py), ProjectileType::PLAYER_BULLET);

                        AudioEvent event{};
                        event.type = WaveType::PULSE;
                        event.frequency = 880.0f;
                        event.duration = 0.08f;
                        event.volume = 0.4f;
                        event.duty = 0.5f;
                        engine.getAudioEngine().playEvent(event);
                        break;
                    }
                }
            } else {
                // S8: a press made while a shot is still in flight does not
                // count. A frame that runs several rules ticks keeps the same
                // press edge, so without this the edge could fire again on the
                // tick the old shot left the panel, with no fresh press.
                fireInputReady = false;
            }
        }
    }

    updateAliens();

    handleCollisions();
    updateEnemyExplosions(tickMs);

    updateMusicTempo();
}

void SpaceInvadersScene::updateAliens() {
    // One rack tick per 60 Hz tick: the rules decide which single alien moves,
    // where the formation is, and when the sweep (and its beat) closes. Nothing
    // here is scaled by the frame delta or the music tempo, so the march follows
    // the live count by itself (S4-S6, D5).
    const rules::RackTick result = rack.tick();

    syncAliensToRack();

    if (result.frameFlipped) {
        const int frame = rack.animationFrame();
        for (auto& alien : aliens) {
            if (alien->isActive()) {
                alien->setAnimationFrame(frame);
            }
        }
    }

    // P4 replaces the cadence, not this hook: firing is still once per sweep.
    if (result.sweepCompleted) {
        enemyShoot();
    }

    if (!gameOver && player) {
        // S13: the run ends when any alien bottom reaches the cannon baseline.
        // The rack reports the lowest live bottom, so a cleared bottom row stops
        // holding the check at its old height.
        const int cannonBaseline = static_cast<int>(CANNON_TOP_Y) + PLAYER_HEIGHT;
        if (rack.lowestAlienBottom() >= cannonBaseline) {
            lives = 0;
            gameOver = true;
            engine.getMusicPlayer().setTempoFactor(1.0f);
            engine.getMusicPlayer().play(GAME_OVER_TRACK);
        }
    }
}

void SpaceInvadersScene::syncAliensToRack() {
    for (int row = 0; row < rules::RackRows; ++row) {
        for (int col = 0; col < rules::RackCols; ++col) {
            AlienActor* alien = alienGrid[row][col];
            const rules::RackCell& cell = rack.cell(row, col);
            if (!alien || !cell.alive) {
                continue;
            }

            // The rack works in playfield pixels; the scene draws at FIELD_X.
            const Scalar targetX = pr32::math::toScalar(FIELD_X + static_cast<int>(cell.x));
            const Scalar targetY = pr32::math::toScalar(static_cast<int>(cell.y));
            if (alien->position.x == targetX && alien->position.y == targetY) {
                continue;
            }
            alien->move(targetX - alien->position.x, targetY - alien->position.y);
        }
    }
}

void SpaceInvadersScene::handleCollisions() {
    using pixelroot32::physics::Circle;
    using pixelroot32::physics::sweepCircleVsRect;

    for (auto& proj : projectiles) {
        if (!proj->isActive()) {
            continue;
        }
        if (proj->getType() == ProjectileType::PLAYER_BULLET) {
            pr32::math::Scalar radius = pr32::math::toScalar(PROJECTILE_WIDTH * 0.5f);
            pr32::math::Scalar halfHeight = pr32::math::toScalar(PROJECTILE_HEIGHT * 0.5f);

            Circle startCircle;
            startCircle.x = proj->getPreviousX() + radius;
            startCircle.y = proj->getPreviousY() + halfHeight;
            startCircle.radius = radius;

            Circle endCircle;
            endCircle.x = proj->position.x + radius;
            endCircle.y = proj->position.y + halfHeight;
            endCircle.radius = radius;

            bool alienHit = false;

            for (int row = 0; row < rules::RackRows && !alienHit; ++row) {
                for (int col = 0; col < rules::RackCols; ++col) {
                    AlienActor* alien = alienGrid[row][col];
                    if (!alien || !rack.cell(row, col).alive) {
                        continue;
                    }
                    pr32::math::Scalar tHit = pr32::math::toScalar(0);
                    pixelroot32::core::Rect targetBox = alien->getHitBox();
                    if (sweepCircleVsRect(startCircle, endCircle, targetBox, tHit) ||
                        proj->getHitBox().intersects(targetBox)) {
                        proj->deactivate();

                        // Kill in the rack first, then mirror it onto the actor:
                        // the rack is the single source of truth, and the hold
                        // freezes the formation while the explosion plays (S5).
                        rack.kill(row, col);
                        rack.hold(rules::RackHoldTicks);
                        alien->setAlive(false);
                        score += alien->getScoreValue();

                        pr32::math::Scalar ex = alien->position.x + pr32::math::toScalar(alien->width) * pr32::math::toScalar(0.5f);
                        pr32::math::Scalar ey = alien->position.y + pr32::math::toScalar(alien->height) * pr32::math::toScalar(0.5f);
                        spawnEnemyExplosion(ex, ey);

                        AudioEvent event{};
                        event.type = WaveType::NOISE;
                        event.frequency = 600.0f;
                        event.duration = 0.12f;
                        event.volume = 0.6f;
                        event.duty = 0.5f;
                        engine.getAudioEngine().playEvent(event);

                        if (getActiveAlienCount() == 0) {
                            gameOver = true;
                            gameWon = true;
                            engine.getMusicPlayer().setTempoFactor(1.0f);
                            engine.getMusicPlayer().play(WIN_TRACK);
                        }

                        alienHit = true;
                        break;
                    }
                }
            }
            if (!alienHit && proj->isActive()) {
                for (auto& bunker : bunkers) {
                    if (bunker->isDestroyed()) {
                        continue;
                    }
                    pr32::math::Scalar tHit = pr32::math::toScalar(0);
                    pixelroot32::core::Rect bunkerBox = bunker->getHitBox();
                    if (sweepCircleVsRect(startCircle, endCircle, bunkerBox, tHit) ||
                        proj->getHitBox().intersects(bunkerBox)) {
                        proj->deactivate();
                        bunker->applyDamage(1);
                        break;
                    }
                }
            }
        }
    }

    if (!player) {
        return;
    }

    pixelroot32::core::Rect playerBox = player->getHitBox();
    for (auto& proj : projectiles) {
        if (!proj->isActive()) {
            continue;
        }
        if (proj->getType() == ProjectileType::ENEMY_BULLET) {
            pr32::math::Scalar radius = pr32::math::toScalar(PROJECTILE_WIDTH * 0.5f);
            pr32::math::Scalar halfHeight = pr32::math::toScalar(PROJECTILE_HEIGHT * 0.5f);

            using pixelroot32::physics::Circle;
            Circle startCircle;
            startCircle.x = proj->getPreviousX() + radius;
            startCircle.y = proj->getPreviousY() + halfHeight;
            startCircle.radius = radius;

            Circle endCircle;
            endCircle.x = proj->position.x + radius;
            endCircle.y = proj->position.y + halfHeight;
            endCircle.radius = radius;

            pixelroot32::core::Rect eBox = proj->getHitBox();
            bool handled = false;
            for (auto& bunker : bunkers) {
                if (bunker->isDestroyed()) {
                    continue;
                }
                pixelroot32::core::Rect bunkerBox = bunker->getHitBox();
                pr32::math::Scalar tHit = pr32::math::toScalar(0);
                if (sweepCircleVsRect(startCircle, endCircle, bunkerBox, tHit) ||
                    eBox.intersects(bunkerBox)) {
                    proj->deactivate();
                    bunker->applyDamage(1);
                    handled = true;
                    break;
                }
            }
            if (handled) {
                continue;
            }
            pr32::math::Scalar tHitPlayer = pr32::math::toScalar(0);
            if (sweepCircleVsRect(startCircle, endCircle, playerBox, tHitPlayer) ||
                eBox.intersects(playerBox)) {
                proj->deactivate();
                handlePlayerHit();
                break;
            }
        }
    }
}

void SpaceInvadersScene::enemyShoot() {
    // Fixed-size candidate list: at most one shooter per rack column, so the
    // array is sized by the formation and this runs without allocating.
    AlienActor* potentialShooters[rules::RackCols];
    int shooterCount = 0;

    for (int col = 0; col < rules::RackCols; ++col) {
        // Column identity comes from the rack grid, not from a float comparison
        // against where the formation started: the old match aliased to the
        // neighbouring column once the rack had stepped (F1).
        for (int row = rules::RackRows - 1; row >= 0; --row) {
            AlienActor* alien = alienGrid[row][col];
            if (!alien || !rack.cell(row, col).alive) {
                continue;
            }
            potentialShooters[shooterCount++] = alien;
            break;
        }
    }

    if (shooterCount == 0) return;

    int idx = std::rand() % shooterCount;
    AlienActor* shooter = potentialShooters[idx];

    for (auto& proj : projectiles) {
        if (!proj->isActive()) {
            pr32::math::Scalar sx = shooter->position.x + pr32::math::toScalar(shooter->width) * pr32::math::toScalar(0.5f);
            pr32::math::Scalar sy = shooter->position.y + pr32::math::toScalar(shooter->height);
            
            proj->reset(Vector2(sx, sy), ProjectileType::ENEMY_BULLET);
            break;
        }
    }
}

int SpaceInvadersScene::getActiveAlienCount() const {
    // The rack is the only alive counter; the actor list can lag one kill
    // behind it, and an arena failure can leave it short.
    return rack.aliveCount();
}

void SpaceInvadersScene::draw(pr32::graphics::Renderer& renderer) {
    renderer.drawFilledRectangle(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, pr32::graphics::Color::Black);
    Scene::draw(renderer);
    drawEnemyExplosions(renderer);
    playerExplosion.draw(renderer);

    char buffer[32];

    std::snprintf(buffer, sizeof(buffer), "SCORE %04d", score);
    renderer.drawText(buffer, 4, 4, pr32::graphics::Color::White, 1, &ARCADE_FONT);

    std::snprintf(buffer, sizeof(buffer), "LIVES %d", lives);
    renderer.drawText(buffer, DISPLAY_WIDTH - 70, 4, pr32::graphics::Color::White, 1, &ARCADE_FONT);

    if (gameOver) {
        if (gameWon) {
            std::snprintf(buffer, sizeof(buffer), "YOU WIN!");
            int textY = DISPLAY_HEIGHT / 2 - 8;
            renderer.drawTextCentered(buffer, textY, pr32::graphics::Color::Green, 1, &ARCADE_FONT);
        } else {
            std::snprintf(buffer, sizeof(buffer), "GAME OVER");
            int textY = DISPLAY_HEIGHT / 2 - 8;
            renderer.drawTextCentered(buffer, textY, pr32::graphics::Color::Red, 1, &ARCADE_FONT);
        }

        std::snprintf(buffer, sizeof(buffer), "PRESS FIRE");
        int textY = DISPLAY_HEIGHT / 2 - 8;
        renderer.drawTextCentered(buffer, textY + 20, pr32::graphics::Color::White, 1, &ARCADE_FONT);
    }
}

void SpaceInvadersScene::updateMusicTempo() {
    if (gameOver) return;
    if (!rack.anyAlive()) return;

    // The rack's lowest bottom is the same number the invasion check reads, so
    // the soundtrack can never disagree with where the formation actually is.
    const float lowestY = static_cast<float>(rack.lowestAlienBottom());
    float threatFactor = (lowestY - static_cast<float>(rules::RackOriginY)) * INV_Y_RANGE;
    if (threatFactor < 0.0f) threatFactor = 0.0f;
    if (threatFactor > 1.0f) threatFactor = 1.0f;

    float targetTempo = 1.0f + (threatFactor * 0.9f);
    currentMusicTempoFactor += (targetTempo - currentMusicTempoFactor) * 0.05f;
    engine.getMusicPlayer().setTempoFactor(currentMusicTempoFactor);
}

void SpaceInvadersScene::updateEnemyExplosions(unsigned long deltaTime) {
    for (int i = 0; i < MaxEnemyExplosions; ++i) {
        EnemyExplosion& e = enemyExplosions[i];
        if (!e.active) {
            continue;
        }

        if (deltaTime >= e.remainingMs) {
            e.active = false;
            e.remainingMs = 0;
        } else {
            e.remainingMs -= deltaTime;
        }
    }
}

void SpaceInvadersScene::drawEnemyExplosions(pr32::graphics::Renderer& renderer) {
    using Color = pr32::graphics::Color;

    for (int i = 0; i < MaxEnemyExplosions; ++i) {
        const EnemyExplosion& e = enemyExplosions[i];
        if (!e.active) {
            continue;
        }

        int cx = static_cast<int>(e.position.x);
        int cy = static_cast<int>(e.position.y);

        int hx = cx - 2;
        int hw = 5;
        int hy = cy;

        if (hx < 0) {
            hw += hx;
            hx = 0;
        }
        if (hx < DISPLAY_WIDTH && hw > 0) {
            if (hx + hw > DISPLAY_WIDTH) {
                hw = DISPLAY_WIDTH - hx;
            }
            if (hw > 0 && hy >= 0 && hy < DISPLAY_HEIGHT) {
                renderer.drawFilledRectangle(hx, hy, hw, 1, Color::White);
            }
        }

        int vx = cx;
        int vy = cy - 2;
        int vh = 5;

        if (vy < 0) {
            vh += vy;
            vy = 0;
        }
        if (vy < DISPLAY_HEIGHT && vh > 0) {
            if (vy + vh > DISPLAY_HEIGHT) {
                vh = DISPLAY_HEIGHT - vy;
            }
            if (vh > 0 && vx >= 0 && vx < DISPLAY_WIDTH) {
                renderer.drawFilledRectangle(vx, vy, 1, vh, Color::White);
            }
        }
    }
}

void SpaceInvadersScene::spawnEnemyExplosion(pr32::math::Scalar x, pr32::math::Scalar y) {
    for (int i = 0; i < MaxEnemyExplosions; ++i) {
        EnemyExplosion& e = enemyExplosions[i];
        if (!e.active) {
            e.active = true;
            e.position.x = x;
            e.position.y = y;
            e.remainingMs = 200;
            break;
        }
    }
}

void SpaceInvadersScene::handlePlayerHit() {
    if (lives > 0) {
        lives -= 1;
    }

    AudioEvent event{};
    event.type = WaveType::NOISE;
    event.frequency = 400.0f;
    event.duration = 0.18f;
    event.volume = 0.7f;
    event.duty = 0.5f;
    engine.getAudioEngine().playEvent(event);

    if (lives <= 0) {
        gameOver = true;
        engine.getMusicPlayer().setTempoFactor(1.0f);
        engine.getMusicPlayer().play(GAME_OVER_TRACK);
        return;
    }

    if (!player) {
        return;
    }

    playerExplosion.start(player->position);
    player->setVisible(false);
    isPaused = true;
}

void SpaceInvadersScene::respawnPlayer() {
    if (!player) {
        return;
    }

    // S14: the cannon returns at the left edge of the playfield, which is
    // FIELD_X in the screen coordinates the actor shares with everything else.
    player->setPosition(pr32::math::Vector2(pr32::math::toScalar(FIELD_X), player->position.y));
    player->setVisible(true);
}

}
