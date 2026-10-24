#include "scenes/SpaceInvadersScene.h"
#include "actors/PlayerActor.h"
#include "actors/AlienActor.h"
#include "actors/ProjectileActor.h"
#include "actors/BunkerActor.h"
#include "GameConstants.h"
#include "platforms/EngineConfig.h"
#include "core/Engine.h"
#include "audio/AudioTypes.h"
#include <cassert>
#include <cstddef>
#include <cstdio>
#include "assets/FontData.h"
#include "assets/sprites/PlayerExplosionSprites.h"
#include "assets/sprites/PlayerSprites.h"

namespace pr32 = pixelroot32;
extern pr32::core::Engine engine;

namespace spaceinvaders {

using pixelroot32::graphics::Sprite;
using pixelroot32::graphics::SpriteAnimationFrame;
using pixelroot32::audio::AudioEvent;
using pixelroot32::audio::WaveType;
using pixelroot32::audio::SweepCurve;
using pixelroot32::math::Scalar;
using pixelroot32::math::Vector2;

#ifdef PIXELROOT32_ENABLE_SCENE_ARENA
using pixelroot32::core::SceneArena;
using pixelroot32::core::arenaNew;

/**
 * Arena budget for one full wave (S19, T6.2).
 *
 * Measured from the native build (`pio run -e native`, PLATFORM_NATIVE, -O2)
 * and checked by the static_asserts below:
 *
 *   actor              count   sizeof   bytes
 *   PlayerActor            1      216      216
 *   AlienActor            55       80     4400
 *   BunkerActor            4      192      768
 *   ProjectileActor       12      200     2400
 *   ---------------------------------------------------
 *   total arena bytes                      7784
 *   arena buffer bytes                     8192
 *   remaining arena headroom                408
 *
 *   entity list: 72 registered (1 + 55 + 4 + 12) against MAX_ENTITIES = 80,
 *   so the entity list keeps 8 free slots.
 *
 * The arena never frees an individual actor, so the whole wave has to fit at
 * once: one player, 55 aliens, four shields and twelve shots. The previous
 * 4096 budget silently dropped every alien past the forty-eighth, all four
 * shields and most shots, because arenaNew() returns null and the spawn loops
 * skip the actor, which reads as a formation with holes and not as an
 * out-of-memory bug. 8192 was measured to hold the wave with 408 bytes spare.
 * S19 lets this buffer keep its size or grow with a measured number, never
 * shrink silently, so every number is a named constant here and the compiler
 * re-checks each actor's share: a future size change breaks the build instead
 * of the game.
 *
 * The per-type byte constants are the measured ceilings. They are intentionally
 * target-independent: the 32-bit ESP32 build lays these actors out no larger
 * than the native one, so the same asserts hold there, and the byte count is
 * what this game ships (S19) rather than a per-ABI figure.
 */
namespace arenaBudget {
/// The cannon: exactly one instance.
constexpr int PlayerCount = 1;
/// The 5 x 11 rack (rules::RackRows x rules::RackCols).
constexpr int AlienCount = rules::RackRows * rules::RackCols;
/// The four shields; each actor only views the scene's shared shield mask.
constexpr int BunkerCount = rules::ShieldCount;
/// The fixed shot pool. init() asserts this equals MaxProjectiles.
constexpr int ProjectileCount = 12;

/// Measured sizeof() per actor type, used as the budgeted ceiling.
constexpr std::size_t PlayerBytes = 216;
constexpr std::size_t AlienBytes = 80;
constexpr std::size_t BunkerBytes = 192;
constexpr std::size_t ProjectileBytes = 200;

/// Bytes reserved for the arena buffer.
constexpr std::size_t BufferBytes = 8192;

/// Sum of the four actor shares: the bytes one full wave consumes.
constexpr std::size_t TotalBytes = PlayerBytes * PlayerCount +
                                   AlienBytes * AlienCount +
                                   BunkerBytes * BunkerCount +
                                   ProjectileBytes * ProjectileCount;
/// Bytes left in the buffer once the wave is allocated.
constexpr std::size_t HeadroomBytes = 408;

/// Actors registered with the scene: player, rack, shields and shot pool.
constexpr int EntityCount =
    PlayerCount + AlienCount + BunkerCount + ProjectileCount;
}  // namespace arenaBudget

/** Arena for all scene entities (player, aliens, bunkers, projectiles).
 *  Sized from arenaBudget::BufferBytes so the budget above and the storage cannot
 *  disagree. If you need 2048 again (e.g. tight RAM), build this target
 *  without PIXELROOT32_ENABLE_SCENE_ARENA so the scene uses heap instead of
 *  this fixed buffer. */
static unsigned char SPACE_INVADERS_SCENE_ARENA_BUFFER[arenaBudget::BufferBytes];

static_assert(sizeof(SPACE_INVADERS_SCENE_ARENA_BUFFER) == arenaBudget::BufferBytes,
              "arena buffer must be arenaBudget::BufferBytes long");

// One check per actor type: the compiled sizeof() may not exceed the measured
// ceiling recorded in the budget. A grown actor fails here, which is what keeps
// the numbers in the comment block above honest.
static_assert(sizeof(PlayerActor) <= arenaBudget::PlayerBytes,
              "PlayerActor grew: re-measure the arena budget");
static_assert(sizeof(AlienActor) <= arenaBudget::AlienBytes,
              "AlienActor grew: re-measure the arena budget");
static_assert(sizeof(BunkerActor) <= arenaBudget::BunkerBytes,
              "BunkerActor grew: re-measure the arena budget");
static_assert(sizeof(ProjectileActor) <= arenaBudget::ProjectileBytes,
              "ProjectileActor grew: re-measure the arena budget");

// A full wave must fit the buffer, and the stated headroom must still be the
// buffer minus the wave, so changing one number forces the others to be
// restated rather than left stale.
static_assert(arenaBudget::PlayerBytes * arenaBudget::PlayerCount +
                      arenaBudget::AlienBytes * arenaBudget::AlienCount +
                      arenaBudget::BunkerBytes * arenaBudget::BunkerCount +
                      arenaBudget::ProjectileBytes * arenaBudget::ProjectileCount ==
                  arenaBudget::TotalBytes,
              "arena total must equal the four actor shares");
static_assert(arenaBudget::TotalBytes <= arenaBudget::BufferBytes,
              "scene arena must hold one full wave");
static_assert(arenaBudget::TotalBytes + arenaBudget::HeadroomBytes == arenaBudget::BufferBytes,
              "arena headroom must be the buffer minus the wave");

// The wave is registered in the scene's fixed entity list as well, and
// addEntity() silently drops anything past MaxEntities.
static_assert(arenaBudget::EntityCount <= pixelroot32::platforms::config::MaxEntities,
              "a full wave must fit MAX_ENTITIES");
#endif


// --- NO MUSIC ---
//
// S17: this game plays no win, game-over or extra-ship sound. The death and the
// invasion already fire their own effect voices, so a jingle would only compete
// with them for the APU's four SFX voices. There is deliberately no win track
// either: S12/S13 have no win state, because a cleared rack advances the wave
// instead of ending the run (F5).

// --- EFFECT VOICES (S17) ---
//
// The shot, the kill, the death, the siren and the saucer hit are five one-shot
// events competing with the march beat for the APU's four SFX voices (slots
// 4-7). No event can name a slot: ApuCore picks the first idle voice of the same
// waveform, then the first idle voice of any waveform, and only when all four
// are busy steals the voice with the fewest samples left (a looping voice
// counts as zero). That is a preference, not a reservation.
//
// The split below works with that policy: the shot and the siren are pulses
// because that is the timbre they want, both bursts are noise, and the saucer
// hit is a triangle because no other effect uses that waveform, so its long
// ring-out prefers a voice none of them is shaping. Durations keep every effect
// but the death short, which keeps a mid-effect steal cheap.
//
// What this cannot do is protect the march. Four concurrent effects fill the
// subpool, and the march's four-tick tone is then the shortest voice in it, so
// a fifth sound on that tick is entitled to take the beat's voice. The scene
// therefore fires the siren once per period instead of layering sweeps, and
// leaves the rest to the pool's own ordering.


SpaceInvadersScene::SpaceInvadersScene()
    : player(nullptr),
      gameOver(false),
      fireInputReady(false) {

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

    // The compile-time half of S19 lives next to the buffer: every actor's
    // share is a named constant checked by a static_assert, so a size
    // regression is a build error. The shot pool count is the one number the
    // scene also owns as MaxProjectiles, so pin the two declarations together.
    static_assert(arenaBudget::ProjectileCount == MaxProjectiles,
                  "the arena budget must cover the whole projectile pool");
#endif
    pr32::graphics::setSpritePalette(pr32::graphics::PaletteType::NES);

    // resetGame() holds every arenaNew() call site - the player, the rack, the
    // shields and the shot pool - and it runs last, so this is the point where
    // the count describes a complete wave.
    resetGame();

#ifdef PIXELROOT32_ENABLE_SCENE_ARENA
    // The runtime half of S19. This is a debug-build check: assert() compiles
    // out under NDEBUG, so it can never fail a device run. The device build is
    // protected by the static_asserts next to the buffer instead, which is why
    // a shortfall shows up as a counted null here and as a compile error on
    // hardware, never as a missing alien.
    assert(arenaFailures == 0 && "scene arena could not hold a full wave");
#endif
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
    // The arena is empty again, so the failure count from here on can only
    // describe the wave this build is about to allocate.
    arenaFailures = 0;
#endif

#ifdef PIXELROOT32_ENABLE_SCENE_ARENA
    player = arenaNew<PlayerActor>(arena, pr32::math::Vector2(PLAYER_START_X, PLAYER_START_Y));
    if (!player) {
        // Count the shortfall instead of registering a null actor: addEntity()
        // asserts on null, and a null cannon has no owner to fall back to. The
        // rest of the scene already tests `player`, so the run survives.
        ++arenaFailures;
    } else {
        addEntity(player);
    }
#else
    player = std::make_unique<PlayerActor>(pr32::math::Vector2(PLAYER_START_X, PLAYER_START_Y));
    addEntity(player.get());
#endif

    spawnAliens();
    // S10: the masks are pristine at the start of a wave, so a run or a wave
    // rebuild starts from the intact shape instead of last wave's damage.
    shieldField.reset();
    spawnBunkers();

    projectiles.reserve(MaxProjectiles);
    for (int i = 0; i < MaxProjectiles; ++i) {
#ifdef PIXELROOT32_ENABLE_SCENE_ARENA
        ProjectileActor* projectile = arenaNew<ProjectileActor>(arena, pr32::math::Vector2(0, -PROJECTILE_HEIGHT), ProjectileType::PLAYER_BULLET);
        if (!projectile) {
            // A missing shot slot is a shortfall, not a design choice (S19).
            ++arenaFailures;
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

    // S12: score, ships and the wave counter return to the start of a run. The
    // high score belongs to the session, so reset() leaves it alone by design.
    scoring.reset();
    gameOver = false;
    fireInputReady = false;

    // S16: a run opens on the first fleet voice and the full-rack interval, so
    // the march never inherits a tone or a gap from the previous run.
    marchSound.reset();

    // S14: a reset ends any death in progress, so the hold and the position it
    // recorded start empty and a run never opens on a burst left over from the
    // previous one. The cannon is owned by init(), not rebuilt here, so the
    // visibility the hold hides has to be restored explicitly: clearing the
    // counter alone would leave an invisible ship on an empty field.
    playerDeathHoldTicks = 0;
    playerDeathPosition = pr32::math::Vector2(0, 0);
    if (player) {
        player->setVisible(true);
    }

    // S9: a new run starts in the ladder's first band with a full budget, on a
    // fresh shot-step clock. S20: the column draws start from the run seed, so
    // the same input script always produces the same shooters.
    enemyFireBand = 0;
    enemyFireTimer.reset(enemyFireBand);
    enemyShotStepTick = 0;
    rng.reseed(RUN_SEED);

    // S11: the period, the score table, the mask and the entry parity all
    // restart with the run, and the siren flag goes quiet because nothing is on
    // screen. The counter feeds the parity, so it is part of the same reset.
    saucerRules.reset();
    saucer.reset();
    shotsFired = 0;
    saucerPopup.active = false;
    saucerPopup.remainingTicks = 0;
    stopSaucerSiren();

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
                // Counted, then skipped: alienGrid keeps this cell null and no
                // later alien is shifted onto the wrong cell (S19).
                ++arenaFailures;
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
    // S10 fixes the four shields at absolute panel columns rather than spacing
    // them by a computed gap: the arcade positions are part of the reference
    // layout, and the rules layer owns them next to ShieldWidth/ShieldHeight.
    for (int i = 0; i < rules::ShieldCount; ++i) {
        const pr32::math::Vector2 position(rules::ShieldX[i], rules::ShieldY);
#ifdef PIXELROOT32_ENABLE_SCENE_ARENA
        BunkerActor* bunker = arenaNew<BunkerActor>(arena, position, shieldField, i);
        if (!bunker) {
            // Counted, then skipped: the shield simply stays absent (S19).
            ++arenaFailures;
            continue;
        }
        bunkers.push_back(bunker);
        addEntity(bunker);
#else
        auto bunker = std::make_unique<BunkerActor>(position, shieldField, i);
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
        }
        return;
    }

    if (playerDeathHoldTicks > 0) {
        // S14: the hold is the whole death. Every rules step is skipped, so the
        // rack resumes where it stopped and no enemy bolt, saucer tick or fire
        // timer advances while the cannon is gone. The enemy bursts keep ageing
        // so a kill from the tick before the hit does not freeze on screen for
        // the length of the hold.
        --playerDeathHoldTicks;
        updateEnemyExplosions(tickMs);

        if (playerDeathHoldTicks == 0) {
            // The last count spends the tick returning the cannon, so the frame
            // after the hold shows the ship at FIELD_X and never an empty field.
            respawnPlayer();
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

                        // S11: the saucer's entry side is the parity of this
                        // counter, so it advances when a bullet really exists —
                        // not on a press and not while searching for a slot.
                        ++shotsFired;

                        // S17: a short bright pulse that falls, so the shot
                        // reads as a laser and not a click. Duration equals the
                        // sweep so the voice frees itself in 0.09 s: it is the
                        // shortest of the five effects, so a mid-shot steal
                        // costs little and nothing has to stop it.
                        AudioEvent event{};
                        event.type = WaveType::PULSE;
                        event.frequency = 1200.0f;
                        event.duration = 0.09f;
                        event.volume = 0.35f;
                        event.duty = 0.5f;
                        event.sweepEndHz = 500.0f;
                        event.sweepDurationSec = 0.09f;
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
    updateEnemyFire();

    // S11: the saucer spawns and crosses on the tick clock. It runs before the
    // hit tests so a shot that landed this tick is compared against the row the
    // ship has just moved to, and before the popup ages so an award made by a
    // hit keeps its whole lifetime.
    updateSaucer();

    handleCollisions();
    updateEnemyExplosions(tickMs);

    // S16: the beat is its own clock. It runs last so it reads the live count
    // after this tick's kills and after a wave rebuild, and it never borrows the
    // rack's sweep length (D5).
    updateMarchSound();
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

    // Enemy fire is not keyed off the sweep any more: the sweep is the march
    // clock, and hanging the shots on it made the fire rate follow the rack
    // speed instead of the reload ladder (S9). updateEnemyFire() owns that.

    if (!gameOver && player) {
        // S13: the run ends when any alien bottom reaches the cannon baseline.
        // The rack reports the lowest live bottom, so a cleared bottom row stops
        // holding the check at its old height. An invasion is the end of the
        // run, not a lost ship: nothing decrements the ship count here, because
        // the cannon was still on the field when the formation arrived.
        const int cannonBaseline = static_cast<int>(CANNON_TOP_Y) + PLAYER_HEIGHT;
        if (rack.lowestAlienBottom() >= cannonBaseline) {
            gameOver = true;
        }
    }
}

void SpaceInvadersScene::updateEnemyFire() {
    // S9: the cadence is the scene's own three-tick clock. The rack sweep cannot
    // be that clock because its length is the live count and it stops while an
    // explosion holds the formation, so the shots would speed up with the march
    // and pause with the kills instead of obeying the reload ladder.
    if (++enemyShotStepTick < rules::EnemyShotTicksPerStep) {
        return;
    }
    enemyShotStepTick = 0;

    // Every bolt steps on the same tick, so the three slots stay in lockstep and
    // their two animation frames flip together (S9).
    const bool fast = rules::enemyShotIsFast(static_cast<uint8_t>(rack.aliveCount()));
    for (auto& projectile : projectiles) {
        if (projectile->isActive() && projectile->getType() == ProjectileType::ENEMY_BULLET) {
            projectile->stepEnemyShot(fast);
        }
    }

    // The budget is indexed by the score high byte, so a band change adopts the
    // tighter reload from this step on instead of waiting for the next run. The
    // countdown restarts, which is the same reload the timer just performed: a
    // full gap still has to pass before the next shot leaves.
    const uint16_t band = scoring.scoreBand();
    if (rules::enemyReloadTicks(band) != rules::enemyReloadTicks(enemyFireBand)) {
        enemyFireBand = band;
        enemyFireTimer.reset(band);
    }

    // One step of budget per shot step, whether or not a slot is free: the
    // reference keeps charging while the three slots are occupied, so a full
    // rack of bolts must not stall the countdown.
    if (!enemyFireTimer.step()) {
        return;
    }

    enemyShoot();
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

void SpaceInvadersScene::advanceWave() {
    // S12: the wave counter moves first, because the origin is derived from it,
    // and spawn() also returns the sweep to its start-of-wave state (direction,
    // animation frame, cursor and the explosion hold), so no march state from
    // the cleared rack survives into the new formation.
    scoring.nextWave();
    rack.spawn(rules::RackOriginX, rules::rackOriginYForWave(scoring.wave()));

    // S16: the new formation opens on the first fleet voice and the first band's
    // interval, so no tone or countdown from the cleared rack carries over.
    marchSound.reset();

    // The actors are views of the rack: every cell is alive again, back on frame
    // 0, and the positions are copied by the same helper a march tick uses.
    // Nothing is rebuilt, so a wave costs no arena allocation (S19) and a cell
    // whose actor failed to allocate stays null exactly as it was.
    for (int row = 0; row < rules::RackRows; ++row) {
        for (int col = 0; col < rules::RackCols; ++col) {
            AlienActor* alien = alienGrid[row][col];
            if (!alien) {
                continue;
            }
            alien->setAlive(true);
            alien->setAnimationFrame(0);
        }
    }
    syncAliensToRack();

    // S10: shields are rebuilt at the start of every wave. The BunkerActors are
    // views over the scene's mask set, so resetting the field restores all four
    // without an actor rebuild or a re-added entity.
    shieldField.reset();

    // Shots in flight belong to the wave that fired them. Clearing them also
    // stops an enemy bolt that would otherwise arrive before the new formation
    // has drawn its first frame.
    for (auto& projectile : projectiles) {
        projectile->deactivate();
    }

    // The burst of the kill that cleared the rack belongs to the wave it ended;
    // letting it run would draw a stale explosion over the new formation for the
    // rest of its 200 ms.
    for (int i = 0; i < MaxEnemyExplosions; ++i) {
        enemyExplosions[i].active = false;
        enemyExplosions[i].remainingMs = 0;
    }

    // S11: the saucer and its whole support state restart with the wave - the
    // crossing, the 1536-tick period, the score table, the entry parity it reads
    // from the shot counter, the popup and the siren. The new wave starts with
    // an empty sky, so no crossing straddles two formations.
    saucer.reset();
    saucerRules.reset();
    shotsFired = 0;
    saucerPopup.active = false;
    saucerPopup.remainingTicks = 0;
    stopSaucerSiren();

    // S9: the reload ladder is indexed by the carried-over score, so the band is
    // re-latched to it and the shot-step clock restarts with the formation.
    enemyShotStepTick = 0;
    enemyFireBand = scoring.scoreBand();
    enemyFireTimer.reset(enemyFireBand);

    // Score, ships and the high score are deliberately untouched: they belong to
    // the run, and only resetGame() starts a new one.
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

                        // S12: the award is written by the rules object, and the
                        // high score follows it on the same tick so the HUD never
                        // shows a run score above its own best. The 1500-point
                        // ship is claimed right after the award that may have
                        // crossed it, and it is a ship only: S17 gives the extra
                        // ship no sound.
                        scoring.add(static_cast<uint16_t>(alien->getScoreValue()));
                        scoring.commitHighScore();
                        scoring.consumeExtraShip();

                        pr32::math::Scalar ex = alien->position.x + pr32::math::toScalar(alien->width) * pr32::math::toScalar(0.5f);
                        pr32::math::Scalar ey = alien->position.y + pr32::math::toScalar(alien->height) * pr32::math::toScalar(0.5f);
                        spawnEnemyExplosion(ex, ey);

                        // S17: the kill is its own noise voice - a short bright
                        // burst (period 12 at the 22050 Hz device rate is about
                        // 1.8 kHz) deliberately unlike the death's low, long
                        // rumble. One-shot, so the pool can take it back.
                        AudioEvent event{};
                        event.type = WaveType::NOISE;
                        event.frequency = 1800.0f;
                        event.duration = 0.11f;
                        event.volume = 0.55f;
                        event.duty = 0.0f;
                        event.noisePeriod = 12;
                        engine.getAudioEngine().playEvent(event);

                        if (getActiveAlienCount() == 0) {
                            // S12/S13: a cleared rack is not a win. The next wave
                            // rebuilds the formation one drop lower and the run
                            // continues with the score, the ships and the high
                            // score it already has (F5).
                            advanceWave();
                        }

                        alienHit = true;
                        break;
                    }
                }
            }

            // S11: the saucer is above the rack, so a shot that reaches it has
            // already passed every alien; the swept circle is the one the rack
            // test just used, so a four-pixel step cannot skip the 7 px mask.
            if (!alienHit && proj->isActive() && saucer.isActive()) {
                pr32::math::Scalar tHitSaucer = pr32::math::toScalar(0);
                pixelroot32::core::Rect saucerBox = saucer.getHitBox();
                if (sweepCircleVsRect(startCircle, endCircle, saucerBox, tHitSaucer) ||
                    proj->getHitBox().intersects(saucerBox)) {
                    proj->deactivate();

                    // The table, not a random draw, decides the award (S11), and
                    // the popup shows it where the ship died. The hit removes the
                    // saucer, so the siren stops with it.
                    const uint16_t awarded = saucerRules.takeScore();
                    scoring.add(awarded);
                    scoring.commitHighScore();
                    scoring.consumeExtraShip();
                    spawnSaucerPopup(saucer.position, awarded);

                    // S17: the hit is the fifth voice, played where the award
                    // and the popup already happen. A triangle sweep is the one
                    // waveform none of the other four uses, so this widest and
                    // longest ring-out asks the pool for a voice the shot, the
                    // siren and the march are not already shaping.
                    AudioEvent hitEvent{};
                    hitEvent.type = WaveType::TRIANGLE;
                    hitEvent.frequency = 1600.0f;
                    hitEvent.duration = 0.35f;
                    hitEvent.volume = 0.6f;
                    hitEvent.duty = 0.5f;
                    hitEvent.sweepEndHz = 300.0f;
                    hitEvent.sweepDurationSec = 0.35f;
                    hitEvent.sweepCurve = SweepCurve::Exponential;
                    engine.getAudioEngine().playEvent(hitEvent);

                    saucer.collect();
                    stopSaucerSiren();

                    // The shot is spent: proj->isActive() is now false, so the
                    // shield test below short-circuits instead of eroding a
                    // bunker with a bullet that already hit the saucer.
                }
            }

            if (!alienHit && proj->isActive() && hitShields(*proj)) {
                continue;
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
            // The bolt now collides as the 3x7 body it draws (S9), not as the
            // player shot's 1x4, so the swept circle is centred in its own box.
            pr32::math::Scalar radius = proj->width * pr32::math::toScalar(0.5f);
            pr32::math::Scalar halfHeight = proj->height * pr32::math::toScalar(0.5f);

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
            if (hitShields(*proj)) {
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

bool SpaceInvadersScene::hitShields(ProjectileActor& projectile) {
    // The swept path runs centre to centre: the shot's own width is not part of
    // the mask, and the centre is the point the pixel walk follows. Starting
    // from the previous tick's centre is what stops a four-pixel step from
    // stepping over a shield row between frames (S10).
    const int halfWidth = static_cast<int>(projectile.width) / 2;
    const int halfHeight = static_cast<int>(projectile.height) / 2;
    const int fromX = static_cast<int>(projectile.getPreviousX()) + halfWidth;
    const int fromY = static_cast<int>(projectile.getPreviousY()) + halfHeight;
    const int toX = static_cast<int>(projectile.position.x) + halfWidth;
    const int toY = static_cast<int>(projectile.position.y) + halfHeight;

    // Broad phase only: a box that the segment's bounds miss cannot hide a
    // solid pixel, so the mask walk is skipped for the other shields. A box
    // the bounds touch is still decided by the mask below.
    const int minX = fromX < toX ? fromX : toX;
    const int minY = fromY < toY ? fromY : toY;
    const int maxX = fromX > toX ? fromX : toX;
    const int maxY = fromY > toY ? fromY : toY;
    const pixelroot32::core::Rect sweepBounds{
        pr32::math::Vector2(minX, minY), maxX - minX + 1, maxY - minY + 1};

    for (auto& bunker : bunkers) {
        if (!bunker || bunker->isDestroyed()) {
            continue;
        }
        if (!sweepBounds.intersects(bunker->getHitBox())) {
            continue;
        }
        // Only a solid pixel ends the shot. A path through a hole this shield
        // already lost carves nothing, so the shot keeps its flight and can
        // still hit the next shield or leave the panel.
        if (bunker->carveSweptWorld(fromX, fromY, toX, toY)) {
            projectile.deactivate();
            return true;
        }
    }
    return false;
}

void SpaceInvadersScene::enemyShoot() {
    // S9: three bolts at most share the pool with the player's shot, so the cap
    // lives with the firing rather than with each caller.
    int enemyShots = 0;
    for (const auto& projectile : projectiles) {
        if (projectile->isActive() && projectile->getType() == ProjectileType::ENEMY_BULLET) {
            ++enemyShots;
        }
    }
    if (enemyShots >= rules::EnemyShotMax) {
        return;
    }

    // Fixed-size candidate list: at most one shooter per rack column, so the
    // array is sized by the formation and this runs without allocating.
    uint8_t liveColumns[rules::RackCols];
    int columnCount = 0;

    for (int col = 0; col < rules::RackCols; ++col) {
        // Column identity comes from the rack grid, not from a float comparison
        // against where the formation started: the old match aliased to the
        // neighbouring column once the rack had stepped (F1).
        for (int row = rules::RackRows - 1; row >= 0; --row) {
            if (rack.cell(row, col).alive) {
                liveColumns[columnCount++] = static_cast<uint8_t>(col);
                break;
            }
        }
    }

    if (columnCount == 0) {
        return;
    }

    // S20: the run's own generator replaces std::rand(), so the same seed and
    // input script reproduce the same shot columns.
    const int column = static_cast<int>(rng.below(static_cast<uint32_t>(columnCount)));

    // The shooter is the lowest alien of that column: the one closest to the
    // cannon, which is why the shot appears to come from the bottom row (S9).
    AlienActor* shooter = nullptr;
    for (int row = rules::RackRows - 1; row >= 0; --row) {
        AlienActor* alien = alienGrid[row][liveColumns[column]];
        if (alien && rack.cell(row, liveColumns[column]).alive) {
            shooter = alien;
            break;
        }
    }

    if (!shooter) {
        return;
    }

    // Centre the 3 px bolt on the alien instead of using the centre as its left
    // edge, the same way the player shot is centred on the cannon.
    const Scalar sx = shooter->position.x +
                      pr32::math::toScalar(shooter->width - ENEMY_SHOT_WIDTH) * pr32::math::toScalar(0.5f);
    const Scalar sy = shooter->position.y + pr32::math::toScalar(shooter->height);

    for (auto& proj : projectiles) {
        if (!proj->isActive()) {
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

void SpaceInvadersScene::updateSaucer() {
    // The award is aged first so a popup spawned later in this tick (by the hit
    // test) keeps its whole lifetime instead of losing a tick immediately.
    if (saucerPopup.active) {
        if (saucerPopup.remainingTicks > 0) {
            --saucerPopup.remainingTicks;
        }
        if (saucerPopup.remainingTicks == 0) {
            saucerPopup.active = false;
        }
    }

    if (saucer.isActive()) {
        saucer.update(rules::FixedStep::TickMs);

        // Leaving the panel is one of the two moments the siren must stop; the
        // other is a hit, which handleCollisions() reports directly.
        if (!saucer.isActive()) {
            stopSaucerSiren();
            return;
        }

        // S17: the cadence only runs while the ship is on screen. The exit
        // branch above returns first, so the tick that reports the crossing's
        // end cannot fire a sweep for a saucer that is already gone.
        advanceSaucerSiren();
        return;
    }

    // The period only advances while no saucer is on screen, so a long crossing
    // never counts towards the next spawn (S11).
    saucerRules.tick();

    if (!saucerRules.spawnDue(static_cast<uint8_t>(rack.aliveCount()))) {
        return;
    }

    // S11: the side comes from the run's shot parity, not from the generator, so
    // the same input script reproduces every crossing (S20).
    saucer.spawn(saucerRules.side(shotsFired));
    saucerRules.onSpawned();
    startSaucerSiren();
}

void SpaceInvadersScene::drawSaucer(pr32::graphics::Renderer& renderer) {
    saucer.draw(renderer);

    // A run that has ended stops ticking, so the award would otherwise freeze
    // over the game-over text. Dropping it from the draw path is enough because
    // the next reset clears the slot.
    if (!saucerPopup.active || gameOver) {
        return;
    }

    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned>(saucerPopup.value));
    renderer.drawText(buffer, static_cast<int16_t>(static_cast<int>(saucerPopup.position.x)),
                      static_cast<int16_t>(static_cast<int>(saucerPopup.position.y)),
                      pr32::graphics::Color::Red, 1, &ARCADE_FONT);
}

void SpaceInvadersScene::spawnSaucerPopup(pr32::math::Vector2 position, uint16_t value) {
    saucerPopup.active = true;
    saucerPopup.position = position;
    saucerPopup.value = value;
    saucerPopup.remainingTicks = SaucerPopupTicks;
}

void SpaceInvadersScene::startSaucerSiren() {
    saucerSirenActive = true;

    // S11/S17: the entry is the first sweep of the warble, not a separate blip.
    // Clearing the cadence makes advanceSaucerSiren() fire on this tick and
    // reload the period, so entry and repeats come from the same code path.
    saucerSirenTicks = 0;
    advanceSaucerSiren();
}

void SpaceInvadersScene::stopSaucerSiren() {
    // The flag is what a later tick reads, and the cadence is dropped with it:
    // leaving a part-spent count behind would let the next crossing open on a
    // sweep that the previous crossing had already earned.
    saucerSirenActive = false;
    saucerSirenTicks = 0;
}

void SpaceInvadersScene::advanceSaucerSiren() {
    // A stopped siren owns no sound. Checking the flag here rather than relying
    // on the caller is what makes "no sweep after the saucer leaves or dies"
    // structural instead of a promise about call order.
    if (!saucerSirenActive) {
        return;
    }

    if (saucerSirenTicks > 0) {
        --saucerSirenTicks;
    }
    if (saucerSirenTicks > 0) {
        return;
    }

    // S17: one low-to-high pulse sweep per period. It is a one-shot of exactly
    // one period, so the next sweep replaces it as the warble the reference
    // plays, and nothing has to be stopped by slot index. The pulse is the
    // siren's own timbre and keeps it out of the noise family the two bursts
    // use, and the 0.28 volume sits under the effects so it masks neither a
    // shot nor a kill.
    AudioEvent event{};
    event.type = WaveType::PULSE;
    event.frequency = 200.0f;
    event.duration = 0.2f;
    event.volume = 0.28f;
    event.duty = 0.5f;
    event.sweepEndHz = 800.0f;
    event.sweepDurationSec = 0.2f;
    engine.getAudioEngine().playEvent(event);

    saucerSirenTicks = rules::SaucerSirenPeriodTicks;
}

void SpaceInvadersScene::draw(pr32::graphics::Renderer& renderer) {
    renderer.drawFilledRectangle(0, 0, CANVAS_WIDTH, CANVAS_HEIGHT, pr32::graphics::Color::Black);
    Scene::draw(renderer);

    // S11: the saucer is a plain member, not an entity, so the scene draws it
    // explicitly here; the popup rides with it because both belong to the ship.
    drawSaucer(renderer);

    drawEnemyExplosions(renderer);

    // S14: the death burst belongs to the scene, not to an entity, because the
    // cannon it replaces is hidden and nothing else knows where it died.
    drawPlayerExplosion(renderer);

    char buffer[32];

    // S12: the arcade's one-player header, drawn in the 8x8 font inside the
    // 16 px score band. SCORE<1> sits over the four-digit score at the left edge
    // of the field and HI-SCORE is centred over its own value.
    //
    // There is deliberately no SCORE<2> slot: this demo has one player, and a
    // permanently empty second slot would claim a mode that does not exist. The
    // arcade's one-player screen shows only these two headers.
    renderer.drawText("SCORE<1>", FIELD_X, 0, pr32::graphics::Color::White, 1, &ARCADE_FONT);
    std::snprintf(buffer, sizeof(buffer), "%04u", static_cast<unsigned>(scoring.score()));
    renderer.drawText(buffer, FIELD_X, 8, pr32::graphics::Color::White, 1, &ARCADE_FONT);

    renderer.drawTextCentered("HI-SCORE", 0, pr32::graphics::Color::White, 1, &ARCADE_FONT);
    std::snprintf(buffer, sizeof(buffer), "%04u", static_cast<unsigned>(scoring.highScore()));
    renderer.drawTextCentered(buffer, 8, pr32::graphics::Color::White, 1, &ARCADE_FONT);

    // S12: the reserve ships are cannon icons, not a number. The ship on the
    // field is not a reserve, so the row holds lives - 1 of them, 16 px apart
    // from the field's left edge.
    const int reserves = static_cast<int>(scoring.lives()) - 1;
    for (int index = 0; index < reserves; ++index) {
        renderer.drawSprite(PLAYER_SHIP_SPRITE, FIELD_X + index * 16, static_cast<int>(RESERVE_Y),
                            pr32::graphics::Color::White);
    }

    if (gameOver) {
        // S12/S13: there is one ending. A cleared rack starts the next wave, so
        // the only game-over state left is the last ship lost or an invasion.
        std::snprintf(buffer, sizeof(buffer), "GAME OVER");
        int textY = CANVAS_HEIGHT / 2 - 8;
        renderer.drawTextCentered(buffer, textY, pr32::graphics::Color::Red, 1, &ARCADE_FONT);

        std::snprintf(buffer, sizeof(buffer), "PRESS FIRE");
        renderer.drawTextCentered(buffer, textY + 20, pr32::graphics::Color::White, 1, &ARCADE_FONT);
    }
}

void SpaceInvadersScene::updateMarchSound() {
    // The run is over: no beat can start, and any tone already playing stops on
    // its four-tick duration without the scene sending a stop command.
    if (gameOver) {
        return;
    }

    if (!marchSound.tick(static_cast<uint8_t>(getActiveAlienCount()))) {
        return;
    }

    // S16: one short pulse per beat. The documented fleet hold is four ticks, so
    // the duration is 4/60 s at the rules clock; the APU disables the voice
    // itself when it expires, and nothing loops.
    AudioEvent event{};
    event.type = WaveType::PULSE;
    event.frequency = rules::MarchToneHz[marchSound.toneIndex()];
    event.duration = static_cast<float>(rules::MarchHoldTicks) / 60.0f;
    event.volume = 0.35f;
    event.duty = 0.5f;
    engine.getAudioEngine().playEvent(event);
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
        if (hx < CANVAS_WIDTH && hw > 0) {
            if (hx + hw > CANVAS_WIDTH) {
                hw = CANVAS_WIDTH - hx;
            }
            if (hw > 0 && hy >= 0 && hy < CANVAS_HEIGHT) {
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
        if (vy < CANVAS_HEIGHT && vh > 0) {
            if (vy + vh > CANVAS_HEIGHT) {
                vh = CANVAS_HEIGHT - vy;
            }
            if (vh > 0 && vx >= 0 && vx < CANVAS_WIDTH) {
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

void SpaceInvadersScene::drawPlayerExplosion(pr32::graphics::Renderer& renderer) {
    if (playerDeathHoldTicks == 0) {
        return;
    }

    // The counter's low bit is the frame: one flip per rules tick for the whole
    // hold, which is the literal "flips back and forth for half a second" of
    // the reference player task. A separate frame clock would have to be kept
    // in step with this count for no gain, and its cadence is undocumented.
    const SpriteAnimationFrame& frame = PLAYER_EXPLOSION_FRAMES[playerDeathHoldTicks & 1u];
    if (!frame.sprite) {
        return;
    }

    renderer.drawSprite(*frame.sprite, static_cast<int>(playerDeathPosition.x),
                        static_cast<int>(playerDeathPosition.y),
                        pr32::graphics::Color::White);
}

void SpaceInvadersScene::handlePlayerHit() {
    // S12: the ship count belongs to the rules object. A hit spends exactly one
    // ship, and the run ends when that was the last one.
    scoring.loseLife();

    // S17: the heaviest of the five and the only long one. It runs for exactly
    // the death hold, so it dies with the pause instead of ringing into the
    // respawn, and it is low (period 70 is about 315 Hz at the device rate) so
    // it cannot be mistaken for the kill burst.
    AudioEvent event{};
    event.type = WaveType::NOISE;
    event.frequency = 315.0f;
    event.duration = static_cast<float>(rules::PlayerDeathHoldTicks) / 60.0f;
    event.volume = 0.7f;
    event.duty = 0.0f;
    event.noisePeriod = 70;
    engine.getAudioEngine().playEvent(event);

    if (scoring.lives() == 0) {
        gameOver = true;
        return;
    }

    if (!player) {
        return;
    }

    // S14: every shot in flight dies with the cannon - the player's own shot as
    // well as each enemy bolt. Clearing them before the hold starts is what
    // stops the pause from ending with a bolt already on top of the new ship.
    for (auto& projectile : projectiles) {
        projectile->deactivate();
    }

    // The burst draws where the cannon was, so the position is recorded before
    // the ship is hidden: the actor keeps moving the moment it is visible again,
    // but a death effect must not follow it.
    playerDeathPosition = player->position;
    playerDeathHoldTicks = rules::PlayerDeathHoldTicks;
    player->setVisible(false);
}

void SpaceInvadersScene::respawnPlayer() {
    if (!player) {
        return;
    }

    // S14: the cannon returns at the left edge of the playfield, which is
    // FIELD_X in the screen coordinates the actor shares with everything else.
    player->setPosition(pr32::math::Vector2(pr32::math::toScalar(FIELD_X), player->position.y));
    player->setVisible(true);

    // A button held through the death must not fire on the respawn tick: the
    // gate only reopens on a tick that sees the button released.
    fireInputReady = false;
}

}
