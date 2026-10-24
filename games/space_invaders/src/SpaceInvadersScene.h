#pragma once
#include "core/Scene.h"
#include "graphics/Renderer.h"
#include <vector>
#include <memory>
#include "platforms/EngineConfig.h"
#include "SaucerActor.h"
#include "rules/FireTiming.h"
#include "rules/FixedStep.h"
#include "rules/MarchSound.h"
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
         *
         * While the player-death hold is set this only ages the hold and the
         * enemy bursts: the rack, the enemy fire clock, the saucer, the shots
         * and the march beat all keep the state they had when the cannon died,
         * which is what makes the wave resume exactly where it stopped (S14).
         *
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
        /**
         * @brief Failed arenaNew() calls in the last wave build (S19).
         *
         * A failed allocation is otherwise silent: the spawn loops skip the
         * actor and the wave comes out short, which looks like a layout bug
         * instead of a memory one. Every arenaNew() call site increments this
         * instead of swallowing the null, and init() asserts it is zero after
         * the first full wave. It covers exactly one wave build: it is cleared
         * where the arena is reset, so it always describes the actors the
         * scene has just allocated.
         */
        uint16_t arenaFailures = 0;

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
         * The march beat (S16). One short tone per beat from the rotating fleet
         * voices, with the gap taken from the live count. It is a rules object
         * rather than a music track because the reference's beat is its own
         * clock, not the formation's sweep: the two are keyed on the same count
         * but run at different rates (D5). The scene only turns a due beat into
         * an AudioEvent.
         */
        rules::MarchSound marchSound;

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
         * S17). It is the siren's entire on/off control: the cadence below only
         * advances while it is set, so clearing it is what stops the sweeps when
         * the ship leaves or dies.
         */
        bool saucerSirenActive = false;

        /**
         * Ticks left before the next siren sweep (S17), or zero when one is due.
         *
         * The siren is re-fired short sweeps rather than one looping voice
         * because an event cannot pin a slot: a looped siren would have to be
         * stopped by a slot index the scene never learns, and the pool could
         * steal it instead. One-shots that expire on their own need no cleanup,
         * so when this counter reaches zero the scene plays the next sweep and
         * reloads it from rules::SaucerSirenPeriodTicks. Zero doubles as the
         * start state, which is what makes the entry audible on the spawn tick.
         */
        uint16_t saucerSirenTicks = 0;

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

        /**
         * @brief Ticks left in the player-death hold (S14); zero means the
         *        cannon is on the field.
         *
         * The counter is the whole death sequence: it is set to
         * rules::PlayerDeathHoldTicks on the hit and decremented once per rules
         * tick, and while it is non-zero every rules step is skipped, so the
         * formation resumes exactly where it stopped. Its low bit also picks
         * the burst frame, which makes the visual alternate exactly once per
         * tick for the whole hold with no second clock to keep in step.
         */
        uint16_t playerDeathHoldTicks = 0;

        /// Top-left the cannon had when it died. The burst draws here because
        /// the cannon is hidden during the hold and no entity carries the
        /// effect: an effect that outlived its actor would need its own actor.
        pixelroot32::math::Vector2 playerDeathPosition;

        static constexpr int MaxProjectiles = 12;

        bool fireInputReady;                        ///< Fire rate limiting state

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
         * @brief Starts the siren and sounds its first sweep (S11, S17).
         *
         * Sets the flag, clears the cadence and lets advanceSaucerSiren() fire
         * on the same tick, so the entry never waits a tick to be heard.
         */
        void startSaucerSiren();

        /**
         * @brief Marks the siren as silent and drops the cadence.
         *
         * Called when the saucer leaves the panel and when a hit removes it, so
         * the flag never outlives the ship it describes and no later tick can
         * find a due sweep for a ship that is gone.
         */
        void stopSaucerSiren();

        /**
         * @brief Advances the siren cadence and plays a sweep when one is due.
         *
         * Runs once per active saucer tick. It is guarded by
         * saucerSirenActive, so a stopped siren cannot fire even if a caller
         * reaches it: the sweep only leaves the scene through this method.
         */
        void advanceSaucerSiren();

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

        /**
         * @brief Advances the march beat and plays its tone when one is due (S16).
         *
         * The rules object owns the countdown, the interval table and the
         * four-voice rotation; this method only reads the live count, asks for
         * a beat, and emits one short pulse on the voice the beat selected.
         * There is no music track and no loop: the APU stops the voice after
         * the documented four-tick hold on its own.
         */
        void updateMarchSound();

        void updateEnemyExplosions(unsigned long deltaTime);
        void drawEnemyExplosions(pixelroot32::graphics::Renderer& renderer);
        void spawnEnemyExplosion(pixelroot32::math::Scalar x, pixelroot32::math::Scalar y);

        /**
         * @brief Draws the death burst at the position the cannon died (S14).
         *
         * The hold counter's low bit selects the frame, so the two masks
         * alternate once per rules tick for the whole hold. That is the literal
         * reading of the reference's "flips back and forth for half a second"
         * and needs no invented intermediate cadence. Nothing is drawn once the
         * hold expires, because the cannon is back on the field by then.
         *
         * @param renderer Renderer to draw into.
         */
        void drawPlayerExplosion(pixelroot32::graphics::Renderer& renderer);

        /**
         * @brief Spends a ship on a hit and starts the death hold (S14).
         *
         * The ship count drops first, so losing the last ship ends the run
         * instead of starting a hold. Otherwise every shot in flight is cleared
         * - the player's own shot and every enemy bolt - before the hold
         * begins, the death position is recorded, the cannon is hidden and the
         * counter starts. Clearing here rather than in the hold keeps the field
         * empty from the first frame of the death, so no bolt the player never
         * saw leave can arrive during the pause.
         */
        void handlePlayerHit();

        /**
         * @brief Returns the cannon to the left edge of the playfield (S14).
         *
         * The cannon is continuous, so a respawn is a single coordinate: the
         * actor stores screen columns, so it is placed at FIELD_X, the left
         * edge of the playfield. The old version centred the ship under the
         * first intact shield and left the grid state stale, which is the
         * respawn bug F2 by construction.
         *
         * The fire gate is re-armed here so a button still held through the
         * death cannot fire on the tick the cannon comes back: the gate only
         * opens again after the button has been released.
         */
        void respawnPlayer();
    };

}
