#pragma once

#include <cstdint>

#include "game/Minefield.h"
#include "game/MinefieldConfig.h"

namespace minesweeper {

/**
 * @class GameSession
 * @brief Owns the board plus playthrough state, still platform-free.
 *
 * Mine placement is deferred until the first reveal so that cell is always
 * safe. Time comes from a monotonic millisecond source handed in by the
 * platform layer (Arduino/SDL2 millis()), never read directly, which keeps
 * the core unit-testable and freezes the clock on win/lose.
 */
class GameSession {
public:
    GameSession();

    /**
     * @brief Begins a playthrough; the board is cleared, mines are pending.
     * @param config Board geometry and mine count.
     * @param seed RNG seed used when the first cell is revealed.
     * @return True when the config was accepted.
     */
    bool start(const BoardConfig& config, std::uint32_t seed);

    /**
     * @brief Reveals a cell; the first call also places mines around it.
     * @param position Cell to reveal.
     * @param nowMs Monotonic clock in milliseconds.
     * @return True when the board changed.
     */
    bool reveal(Position position, unsigned long nowMs);

    /**
     * @brief Toggles a flag; starts the clock but never places mines.
     * @param position Cell to flag or unflag.
     * @param nowMs Monotonic clock in milliseconds.
     * @return True when the flag state changed.
     */
    bool toggleFlag(Position position, unsigned long nowMs);

    /**
     * @brief Restarts with the same geometry and a fresh seed.
     * @param seed RNG seed for the next board.
     * @return True when the board was cleared.
     */
    bool restart(std::uint32_t seed);

    /**
     * @brief Current playthrough phase.
     * @return Ready, Playing, Won or Lost.
     */
    [[nodiscard]] GameStatus getStatus() const;

    /**
     * @brief Whole seconds since the first valid action, frozen at the end.
     * @param nowMs Monotonic clock in milliseconds.
     * @return Elapsed seconds, 0 before the first action.
     */
    [[nodiscard]] std::uint32_t getElapsedSeconds(unsigned long nowMs) const;

    /**
     * @brief Mines minus flags, saturated at zero for over-flagging.
     * @return Remaining mine estimate for the HUD.
     */
    [[nodiscard]] std::uint16_t getRemainingMines() const;

    /**
     * @brief Read-only board for the renderer.
     * @return Active minefield.
     */
    [[nodiscard]] const Minefield& getMinefield() const;

    /**
     * @brief Active board geometry.
     * @return Config from the last successful start().
     */
    [[nodiscard]] const BoardConfig& getConfig() const;

private:
    Minefield field_;
    BoardConfig config_;
    std::uint32_t seed_;
    GameStatus status_;
    unsigned long startMs_;
    unsigned long endMs_;

    void beginPlaying(unsigned long nowMs);
    void finish(GameStatus terminal, unsigned long nowMs);
};

}  // namespace minesweeper
