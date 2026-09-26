#pragma once
#include <core/Scene.h>
#include <graphics/Renderer.h>

#include "audio/PoolAudio.h"
#include "pool/Game.h"

namespace pool {

/**
 * @class PoolScene
 * @brief Engine-side presentation layer for the Pool demo.
 *
 * Owns the engine-free `pool::Game` core, maps the 6 engine buttons to it
 * (Up/Down/Left/Right/A/B, see the InputConfig order in platforms/native.h),
 * paces fixed 1/60 s simulation steps against wall-clock time, and draws the
 * table, balls, aim guide and HUD from the core's const accessors.
 */
class PoolScene : public pixelroot32::core::Scene {
public:
    /**
     * @brief Deals a fresh run and leaves the menu: Menu -> Aiming.
     *
     * Called by the menu scene right after engine.setScene() re-inits this
     * scene (setScene always runs init, so the run starts from stage 1).
     */
    void startGameFromMenu();

    /**
     * @brief Applies the MUSIC checkbox (checked = sound on).
     * @param enabled False mutes stage music and jingles.
     */
    void setMusicEnabled(bool enabled);

    /**
     * @brief Applies the SFX checkbox (checked = sound on).
     * @param enabled False mutes every sound effect.
     */
    void setSfxEnabled(bool enabled);

    /**
     * @brief Soft tick for menu cursor moves.
     */
    void playMenuTick();
    /**
     * @brief Initializes the scene. Always calls `Scene::init()` first.
     */
    void init() override;

    /**
     * @brief Advances the scene by one engine update tick.
     * @param deltaTime Elapsed time in milliseconds since the last update.
     */
    void update(unsigned long deltaTime) override;

    /**
     * @brief Draws the scene.
     * @param renderer The renderer to draw into.
     */
    void draw(pixelroot32::graphics::Renderer& renderer) override;

private:
    /**
     * @brief Applies button edges and levels to the game for its current state.
     */
    void handleInput();

    /**
     * @brief Advances fixed simulation steps from wall-clock milliseconds.
     * @param deltaTime Elapsed time in milliseconds since the last update.
     */
    void stepSimulation(unsigned long deltaTime);

    /**
     * @brief Draws felt, cushions and pockets from the loaded table.
     * @param renderer The renderer to draw into.
     */
    void drawTable(pixelroot32::graphics::Renderer& renderer) const;

    /**
     * @brief Polls the simulation for audible events (contacts, pockets, fouls).
     *
     * Drains the World's contact flags and diffs the active-ball mask and the
     * score against the previous tick. Read-only: it never writes the sim.
     */
    void pollAudio();

    /**
     * @brief Switches music to match the game state (stage track, jingle, stop).
     *
     * Transient EvaluateShot/NextTurn frames keep whatever is playing.
     */
    void syncMusic();

    /**
     * @brief Resets the audio baseline after a (re)deal: full mask, score 0.
     */
    void snapshotAudioBaseline();

    /**
     * @brief Draws every active ball, ringing the next expected target.
     * @param renderer The renderer to draw into.
     */
    void drawBalls(pixelroot32::graphics::Renderer& renderer) const;

    /**
     * @brief Draws the aim guide while aiming with a live cue ball.
     * @param renderer The renderer to draw into.
     */
    void drawAim(pixelroot32::graphics::Renderer& renderer) const;

    /**
     * @brief Draws the HUD band and the Menu/Pause/GameOver overlays.
     * @param renderer The renderer to draw into.
     */
    void drawHud(pixelroot32::graphics::Renderer& renderer) const;

    /// The rules core. draw() only reads it; update() drives it. A failed
    /// newGame() leaves it in GameOver (lost), which is why draw() checks
    /// tableError_ first instead of drawing from a partial load.
    Game game_;
    /// Result of loading the stage table in init(). The engine always calls
    /// init() before draw(), so a fresh Scene never draws with a stale None.
    TableError tableError_ = TableError::None;
    /// ms x 60 accumulator: 1000 units make exactly one 1/60 s frame.
    unsigned long accUnits_ = 0;
    /// Freeze flag toggled with B; update() skips input and simulation while set.
    bool paused_ = false;
    /// Sound director: stage BGM, jingles and SFX. Silent when the audio flag is 0.
    PoolAudio audio_;
    /// Music key currently playing: 0 = none, 1-10 = stage, 11 = win, 12 = lose.
    uint8_t audioKey_ = 0;
    /// Active-ball bitmask at the previous tick; a 1->0 edge is a pocket.
    uint8_t prevActiveMask_ = 0;
    /// Score at the previous tick; a drop is a foul.
    int32_t prevScore_ = 0;
};

}  // namespace pool
