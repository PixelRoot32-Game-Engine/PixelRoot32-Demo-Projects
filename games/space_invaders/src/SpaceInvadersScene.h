#pragma once
#include "core/Scene.h"
#include "graphics/Renderer.h"
#include <vector>
#include <memory>
#include "platforms/EngineConfig.h"
#include "SaucerActor.h"
#include "rules/FireTiming.h"
#include "rules/FixedStep.h"
#include "rules/Rack.h"
#include "rules/Rng.h"
#include "rules/Saucer.h"
#include "rules/Scoring.h"
#include "rules/ShieldMask.h"

namespace spaceinvaders {

    class PlayerActor;
    class AlienActor;
    class ProjectileActor;
    class BunkerActor;

    /**
     * @struct EnemyExplosion
     * @brief Simple explosion effect for enemy hits.
     *
     * Uses a fixed array slot for visual feedback without dynamic allocation.
     */
    struct EnemyExplosion {
        bool active;                              ///< Whether this slot is in use
        pixelroot32::math::Vector2 position;       ///< World position of the explosion
        unsigned long remainingMs;                 ///< Time left before effect ends
    };

    /**
     * @class ExplosionAnimation
     * @brief Multi-frame explosion animation for the player death sequence.
     *
     * Plays a non-looping sprite animation at a fixed framerate.
     * Used during the pause state between player death and respawn.
     */
    class ExplosionAnimation {
    public:
        ExplosionAnimation();

        /**
         * @brief Starts the explosion animation at the given world position.
         * @param pos World position where the explosion is drawn.
         */
        void start(pixelroot32::math::Vector2 pos);

        /**
         * @brief Advances the animation based on elapsed time.
         * @param deltaTime Time elapsed since last frame in milliseconds.
         */
        void update(unsigned long deltaTime);

        /**
         * @brief Draws the current frame if the animation is active.
         * @param renderer Reference to the renderer.
         */
        void draw(pixelroot32::graphics::Renderer& renderer);

        /**
         * @brief Returns true while the animation is still playing.
         */
        bool isActive() const;

    private:
        bool active;                               ///< Whether the animation is playing
        pixelroot32::math::Vector2 position;       ///< Draw position
        unsigned long timeAccumulator;             ///< Accumulated time for frame stepping
        unsigned char stepsDone;                   ///< Frames already advanced
        pixelroot32::graphics::SpriteAnimation animation; ///< Sprite animation data
    };

    /**
     * @class SpaceInvadersScene
     * @brief Main scene for the Space Invaders clone.
     *
     * Manages game state, entity lifecycle, collision resolution, and procedural
     * audio. Uses swept-circle collision for projectiles and supports arena-based
     * memory when PIXELROOT32_ENABLE_SCENE_ARENA is defined.
     */
    class SpaceInvadersScene : public pixelroot32::core::Scene {
    public:
        SpaceInvadersScene();
        virtual ~SpaceInvadersScene();

        /**
         * @brief Initializes the scene and resets game state.
         */
        void init() override;

        /**
         * @brief Advances the fixed 60 Hz clock and runs its ticks.
         *
         * The frame delta only feeds the accumulator (S15). Each reported
         * tick runs the rules through tick(); a frame that yields none simply
         * redraws the current state.
         *
         * @param deltaTime Time elapsed since last frame in milliseconds.
         */
        void update(unsigned long deltaTime) override;

        /**
         * @brief Draws all entities, HUD, and visual effects.
         * @param renderer Reference to the renderer.
         */
        void draw(pixelroot32::graphics::Renderer& renderer) override;

    private:
        void resetGame();
        void spawnAliens();
        void spawnBunkers();
        void cleanup();

        /**
         * @brief Runs one 60 Hz rules tick.
         * @param tickMs Duration of one tick in milliseconds.
         */
        void tick(unsigned long tickMs);

        /**
         * The four shields' pixels. One field, not four actors' state, so a
         * wave rebuild is a single reset() and no actor can hold a stale copy
         * of a mask another actor just eroded (S10). It is declared before the
         * actors so it outlives every BunkerActor that references it.
         */
        rules::ShieldField shieldField;

#ifdef PIXELROOT32_ENABLE_SCENE_ARENA
        PlayerActor* player;
        std::vector<AlienActor*> aliens;
        std::vector<ProjectileActor*> projectiles;
        std::vector<BunkerActor*> bunkers;
#else
        std::unique_ptr<PlayerActor> player;
        std::vector<std::unique_ptr<AlienActor>> aliens;
        std::vector<std::unique_ptr<ProjectileActor>> projectiles;
        std::vector<std::unique_ptr<BunkerActor>> bunkers;
#endif

        /**
         * Rack cell -> actor lookup. Indexed by the rack's own row/column so
         * the scene never assumes the actor vector is a dense row-major copy:
         * an arena allocation that fails leaves a null here instead of
         * shifting every later alien onto the wrong cell.
         */
        AlienActor* alienGrid[rules::RackRows][rules::RackCols];

        /**
         * The formation rules. The scene owns exactly one instance so a reset
         * can rebuild the wave in place; positions, alive count, animation
         * frame and the invasion check all read from it.
         */
        rules::Rack rack;

        /**
         * The mystery-ship model (S11). The rules object owns the 1536-tick
         * period, the shot-parity side and the 15-value table; the actor owns
         * only the crossing. Both are plain members: a single object that
         * respawns at most every 1536 ticks does not belong in the wave's
         * entity list, and the arena never frees what it hands out.
         */
        rules::Saucer saucerRules;
        SaucerActor saucer;

        /**
         * Player shots created this run. S11 picks the entry side from its
         * parity, so it advances only when a bullet is actually created and
         * never on a press that found a shot already in flight.
         */
        uint32_t shotsFired = 0;

        /**
         * One slot for the score shown where the saucer died (S11). A value and
         * a position rather than an actor, so a hit costs neither an allocation
         * nor an entity-list slot.
         */
        struct SaucerPopup {
            bool active = false;                  ///< True while the value is shown
            pixelroot32::math::Vector2 position;  ///< Top-left the saucer had at the hit
            uint16_t value = 0;                   ///< Score the hit awarded
            uint16_t remainingTicks = 0;          ///< Ticks left before the slot clears
        };
        SaucerPopup saucerPopup;

        /// Ticks the award stays on screen: one second at 60 Hz, long enough to
        /// read and short enough not to survive into the next crossing.
        static constexpr uint16_t SaucerPopupTicks = 60;

        /**
         * True while a saucer is on screen and its siren should sound (S11,
         * S17). The audio rewrite in T5.2 gives the siren its own voice and
         * reads this flag; today only the start and stop edges are wired, so
         * its meaning stays exactly "a saucer is on screen".
         */
        bool saucerSirenActive = false;

        /**
         * Score, ships, wave counter and the session high score (S12). One
         * rules object owns all four so the HUD, the enemy-fire band and the
         * end-of-run checks read the same numbers the awards wrote, and reset()
         * can start a run without discarding the high score.
         */
        rules::Scoring scoring;

        bool gameOver;                             ///< True when the run has ended

        rules::FixedStep fixedStep;                ///< 60 Hz rules clock (S15)

        /**
         * Enemy fire model (S9). The reload ladder and every live bolt share
         * one shot-step clock that belongs to the scene: the rack sweep is not
         * a clock, because it changes length with the live count and stops
         * while an explosion holds the formation.
         */
        rules::EnemyFireTimer enemyFireTimer;
        uint16_t enemyShotStepTick = 0;            ///< Ticks into the current three-tick step
        uint16_t enemyFireBand = 0;                ///< Score high byte the ladder is latched for
        rules::Rng rng;                            ///< Run's own generator, seeded per run (S20)

        static constexpr int MaxEnemyExplosions = 8;
        EnemyExplosion enemyExplosions[MaxEnemyExplosions];
        ExplosionAnimation playerExplosion;
        bool isPaused;                              ///< True during player death/respawn

        static constexpr int MaxProjectiles = 12;

        bool fireInputReady;                        ///< Fire rate limiting state
        float currentMusicTempoFactor;              ///< BGM tempo based on alien proximity

        /**
         * @brief Advances the rack one tick and mirrors it onto the actors.
         *
         * Runs once per 60 Hz tick: rules::Rack::tick() moves exactly one live
         * cell, the moved cell is copied onto its actor, the shared animation
         * frame is applied when the sweep closes, and the invasion check reads
         * the rack's lowest bottom. The rack's own hold covers explosion
         * pauses. Enemy fire does not hang off the sweep: updateEnemyFire()
         * owns that clock.
         */
        void updateAliens();

        /**
         * @brief Starts the next wave over the same run (S12).
         *
         * Clearing the rack is not a win: the wave counter advances, the
         * formation respawns one drop lower than the previous wave, the shields
         * are rebuilt and the field is cleared of shots and of the saucer.
         * Score, ships and the high score carry over, so this is a wave rebuild
         * and not a run reset.
         */
        void advanceWave();

        /**
         * @brief Runs the enemy shot-step clock and the reload ladder (S9).
         *
         * Every third tick all live bolts step together, then the reload timer
         * spends one step and a shot leaves when it says so and a slot is free.
         * Keeping this off the rack sweep is what decouples the fire rate from
         * the march, and the fixed division keeps it off the frame delta.
         */
        void updateEnemyFire();

        /**
         * @brief Runs the mystery saucer and its score popup for one tick (S11).
         *
         * While nothing is on screen the spawn period advances; once it expires
         * with enough aliens alive the saucer enters from the side the shot
         * counter picks. While it is on screen the rules do not advance: the
         * crossing only moves and leaves. The popup counts down on the same
         * tick clock, so its lifetime is tick-accurate like every other rule
         * (S20).
         */
        void updateSaucer();

        /**
         * @brief Draws the crossing saucer and the score popup (S11).
         *
         * The saucer is not an entity, so nothing else can draw it; this is the
         * scene's explicit draw call for the one transient it owns.
         *
         * @param renderer Renderer to draw into.
         */
        void drawSaucer(pixelroot32::graphics::Renderer& renderer);

        /**
         * @brief Shows an awarded score where the saucer died.
         *
         * Writes the single popup slot in place, so a hit allocates nothing.
         *
         * @param position Top-left position the saucer had at the hit.
         * @param value Score the hit awarded.
         */
        void spawnSaucerPopup(pixelroot32::math::Vector2 position, uint16_t value);

        /**
         * @brief Marks the siren as sounding and cues the entry sound.
         *
         * T5.2 gives the siren its own voice; until then this emits one short
         * pulse in the same style as the shot and hit sounds, so the entry is
         * audible and saucerSirenActive is accurate for T5.2 to build on.
         */
        void startSaucerSiren();

        /**
         * @brief Marks the siren as silent.
         *
         * Called when the saucer leaves the panel and when a hit removes it, so
         * the flag never outlives the ship it describes.
         */
        void stopSaucerSiren();

        /**
         * @brief Copies the rack's cell positions onto the actors.
         *
         * Only cells whose target differs from the actor are written, so a
         * normal tick touches exactly one actor and an edge turn touches all
         * live ones with the rack's 8 px drop.
         */
        void syncAliensToRack();

        void handleCollisions();

        /**
         * @brief Resolves a shot against the shields along the segment it swept (S10).
         *
         * The shield boxes are a broad-phase filter only. The path runs between
         * the shot's centre on the previous tick and its centre now, so a
         * four-pixel step cannot skip a row, and the mask decides whether the
         * cell the path entered is still solid. A path through an existing hole
         * carves nothing, leaves the shot alive and reports false, which is what
         * lets it reach the next shield or leave the panel.
         *
         * @param projectile The live shot to test.
         * @return true when a solid shield pixel was hit; the shot is deactivated.
         */
        bool hitShields(ProjectileActor& projectile);

        /**
         * @brief Fires one enemy bolt from the lowest alien of a live column.
         *
         * Enforces the three-shot cap, draws the column with the scene's seeded
         * generator, and reuses a free pool slot: no allocation on the tick.
         */
        void enemyShoot();
        int getActiveAlienCount() const;
        void updateMusicTempo();

        void updateEnemyExplosions(unsigned long deltaTime);
        void drawEnemyExplosions(pixelroot32::graphics::Renderer& renderer);
        void spawnEnemyExplosion(pixelroot32::math::Scalar x, pixelroot32::math::Scalar y);

        void handlePlayerHit();

        /**
         * @brief Returns the cannon to the left edge of the playfield (S14).
         *
         * The cannon is continuous, so a respawn is a single coordinate: the
         * actor stores screen columns, so it is placed at FIELD_X, the left
         * edge of the playfield. The old version centred the ship under the
         * first intact shield and left the grid state stale, which is the
         * respawn bug F2 by construction.
         */
        void respawnPlayer();
    };

}
